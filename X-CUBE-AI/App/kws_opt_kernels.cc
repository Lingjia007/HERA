/**
 * @file    kws_opt_kernels.cc
 * @brief   Optimized int8 ADD / MUL / LOGISTIC kernels for the KWS model.
 *
 * See kws_opt_kernels.h. Numerics follow the stock integer reference paths
 * exactly (same requantization chain); only the iteration order differs:
 * broadcast ops with a leading-axis [1,C,1,1]-style operand use a flat
 * outer/inner loop with the constant operand requantized once per channel.
 */
#include "kws_opt_kernels.h"

#include <cmath>
#include <cstdint>

#include "arm_nnfunctions.h"
#include "arm_nnsupportfunctions.h"
#include "tensorflow/lite/c/builtin_op_data.h"
#include "tensorflow/lite/kernels/internal/reference/integer_ops/add.h"
#include "tensorflow/lite/kernels/internal/reference/integer_ops/mul.h"
#include "tensorflow/lite/kernels/internal/quantization_util.h"
#include "tensorflow/lite/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/kernel_util.h"
#include "tensorflow/lite/micro/kernels/mul.h"
#include "tensorflow/lite/micro/memory_helpers.h"
#include "tensorflow/lite/micro/micro_log.h"

namespace tflite
{
namespace
{

/* ---------------------------------------------------------------------------
 * Shared: broadcast shape analysis
 * ------------------------------------------------------------------------ */

/* A broadcast operand whose ONLY non-1 dim sits at 'axis' (with matching
 * main dim there) lets us iterate main as [outer, inner]: the inner run of
 * prod(dims[axis+1..]) elements is contiguous in memory and shares ONE
 * requantized constant of the broadcast operand. Row-major flat offsets:
 * main[outer_iter * inner .. +inner), bc element = bc[outer_iter % bc_dim]. */
bool FastBcastAxis(const RuntimeShape &main_s, const RuntimeShape &bc_s,
                   int *axis_out)
{
  const int rank = main_s.DimensionsCount();
  if (rank != bc_s.DimensionsCount())
    return false;
  int k = -1;
  for (int i = 0; i < rank; ++i)
  {
    if (bc_s.Dims(i) != 1)
    {
      if (k >= 0 || bc_s.Dims(i) != main_s.Dims(i))
        return false;
      k = i;
    }
  }
  if (k < 0)
    return false;
  *axis_out = k;
  return true;
}

/* outer = prod(dims[0..axis]) inclusive, inner = flat / outer. */
uint32_t OuterOf(const RuntimeShape &s, int axis)
{
  uint32_t outer = 1;
  for (int i = 0; i <= axis; ++i)
    outer *= static_cast<uint32_t>(s.Dims(i));
  return outer;
}

/* Exact dimension equality for eval-tensor shapes. */
bool SameShape(const RuntimeShape &a, const RuntimeShape &b)
{
  if (a.DimensionsCount() != b.DimensionsCount())
    return false;
  for (int i = 0; i < a.DimensionsCount(); ++i)
    if (a.Dims(i) != b.Dims(i))
      return false;
  return true;
}

/* ---------------------------------------------------------------------------
 * ADD (int8)
 * ------------------------------------------------------------------------ */

struct KwsAddData
{
  bool requires_broadcast;
  int32_t input1_offset;
  int32_t input2_offset;
  int32_t output_offset;
  int32_t input1_multiplier;
  int32_t input2_multiplier;
  int32_t output_multiplier;
  int input1_shift;
  int input2_shift;
  int output_shift;
  int left_shift;
  int32_t act_min;
  int32_t act_max;
};

void *KwsAddInit(TfLiteContext *context, const char *buffer, size_t length)
{
  (void)buffer;
  (void)length;
  TFLITE_DCHECK(context->AllocatePersistentBuffer != nullptr);
  return context->AllocatePersistentBuffer(context, sizeof(KwsAddData));
}

/* Param math replicated from kernels/cmsis_nn/add.cc PrepareAdd (int8). */
TfLiteStatus KwsAddPrepare(TfLiteContext *context, TfLiteNode *node)
{
  TFLITE_DCHECK(node->user_data != nullptr);
  KwsAddData *d = static_cast<KwsAddData *>(node->user_data);
  auto *params = reinterpret_cast<TfLiteAddParams *>(node->builtin_data);

  MicroContext *mc = GetMicroContext(context);
  TfLiteTensor *in1 = mc->AllocateTempInputTensor(node, 0);
  TfLiteTensor *in2 = mc->AllocateTempInputTensor(node, 1);
  TfLiteTensor *out = mc->AllocateTempOutputTensor(node, 0);
  if (in1 == nullptr || in2 == nullptr || out == nullptr)
    return kTfLiteError;
  TF_LITE_ENSURE_EQ(context, in1->type, kTfLiteInt8);
  TF_LITE_ENSURE_EQ(context, in2->type, kTfLiteInt8);
  TF_LITE_ENSURE_EQ(context, out->type, kTfLiteInt8);

  d->input1_offset = -in1->params.zero_point;
  d->input2_offset = -in2->params.zero_point;
  d->output_offset = out->params.zero_point;
  d->left_shift = 20;
  const double twice_max_scale =
      2.0 * static_cast<double>(
                in1->params.scale > in2->params.scale ? in1->params.scale
                                                      : in2->params.scale);
  const double real_in1_mult =
      static_cast<double>(in1->params.scale) / twice_max_scale;
  const double real_in2_mult =
      static_cast<double>(in2->params.scale) / twice_max_scale;
  const double real_out_mult =
      twice_max_scale /
      (static_cast<double>(1 << d->left_shift) *
       static_cast<double>(out->params.scale));
  QuantizeMultiplierSmallerThanOneExp(real_in1_mult, &d->input1_multiplier,
                                      &d->input1_shift);
  QuantizeMultiplierSmallerThanOneExp(real_in2_mult, &d->input2_multiplier,
                                      &d->input2_shift);
  QuantizeMultiplierSmallerThanOneExp(real_out_mult, &d->output_multiplier,
                                      &d->output_shift);
  TF_LITE_ENSURE_STATUS(CalculateActivationRangeQuantized(
      context, params->activation, out, &d->act_min, &d->act_max));
  d->requires_broadcast = !HaveSameShapes(in1, in2);

  mc->DeallocateTempTfLiteTensor(in1);
  mc->DeallocateTempTfLiteTensor(in2);
  mc->DeallocateTempTfLiteTensor(out);
  return kTfLiteOk;
}

/* Flat broadcast add: main = [outer x inner], bc constant per outer step.
 * Requantization chain identical to reference_integer_ops::Add. */
void KwsAddFast(const KwsAddData *d, const int8_t *main_d, const int8_t *bc_d,
                int8_t *out_d, uint32_t outer, uint32_t inner, uint32_t bc_dim,
                int32_t main_off, int32_t main_mult, int main_shift,
                int32_t bc_off, int32_t bc_mult, int bc_shift)
{
  for (uint32_t o = 0; o < outer; ++o)
  {
    const int32_t bc_term = arm_nn_requantize(
        (static_cast<int32_t>(bc_d[o % bc_dim]) + bc_off) << d->left_shift,
        bc_mult, bc_shift);
    const int8_t *mp = main_d + static_cast<size_t>(o) * inner;
    int8_t *op = out_d + static_cast<size_t>(o) * inner;
    for (uint32_t i = 0; i < inner; ++i)
    {
      int32_t acc =
          arm_nn_requantize((static_cast<int32_t>(mp[i]) + main_off)
                                << d->left_shift,
                            main_mult, main_shift) +
          bc_term;
      acc = arm_nn_requantize(acc, d->output_multiplier, d->output_shift) +
            d->output_offset;
      acc = acc < d->act_min ? d->act_min : acc;
      op[i] = static_cast<int8_t>(acc > d->act_max ? d->act_max : acc);
    }
  }
}

TfLiteStatus KwsAddInvoke(TfLiteContext *context, TfLiteNode *node)
{
  const KwsAddData *d = static_cast<const KwsAddData *>(node->user_data);
  TFLITE_DCHECK(d != nullptr);

  const TfLiteEvalTensor *in1 = micro::GetEvalInput(context, node, 0);
  const TfLiteEvalTensor *in2 = micro::GetEvalInput(context, node, 1);
  TfLiteEvalTensor *out = micro::GetEvalOutput(context, node, 0);
  const int8_t *p1 = micro::GetTensorData<int8_t>(in1);
  const int8_t *p2 = micro::GetTensorData<int8_t>(in2);
  int8_t *po = micro::GetTensorData<int8_t>(out);
  const RuntimeShape &s1 = micro::GetTensorShape(in1);
  const RuntimeShape &s2 = micro::GetTensorShape(in2);

  if (!d->requires_broadcast)
  {
    arm_elementwise_add_s8(
        p1, p2, d->input1_offset, d->input1_multiplier, d->input1_shift,
        d->input2_offset, d->input2_multiplier, d->input2_shift, d->left_shift,
        po, d->output_offset, d->output_multiplier, d->output_shift, d->act_min,
        d->act_max, MatchingElementsSize(s1, s2, micro::GetTensorShape(out)));
    return kTfLiteOk;
  }

  int axis;
  if (FastBcastAxis(s1, s2, &axis))
  {
    const uint32_t outer = OuterOf(s1, axis);
    KwsAddFast(d, p1, p2, po, outer, s1.FlatSize() / outer,
               static_cast<uint32_t>(s2.Dims(axis)), d->input1_offset,
               d->input1_multiplier, d->input1_shift, d->input2_offset,
               d->input2_multiplier, d->input2_shift);
  }
  else if (FastBcastAxis(s2, s1, &axis))
  {
    const uint32_t outer = OuterOf(s2, axis);
    KwsAddFast(d, p2, p1, po, outer, s2.FlatSize() / outer,
               static_cast<uint32_t>(s1.Dims(axis)), d->input2_offset,
               d->input2_multiplier, d->input2_shift, d->input1_offset,
               d->input1_multiplier, d->input1_shift);
  }
  else
  {
    /* Odd broadcast pattern: stock slow path. */
    ArithmeticParams op_params;
    op_params.left_shift = d->left_shift;
    op_params.input1_offset = d->input1_offset;
    op_params.input1_multiplier = d->input1_multiplier;
    op_params.input1_shift = d->input1_shift;
    op_params.input2_offset = d->input2_offset;
    op_params.input2_multiplier = d->input2_multiplier;
    op_params.input2_shift = d->input2_shift;
    op_params.output_offset = d->output_offset;
    op_params.output_multiplier = d->output_multiplier;
    op_params.output_shift = d->output_shift;
    op_params.quantized_activation_min = d->act_min;
    op_params.quantized_activation_max = d->act_max;
    reference_integer_ops::BroadcastAdd4DSlow(op_params, s1, p1, s2, p2,
                                              micro::GetTensorShape(out), po);
  }
  return kTfLiteOk;
}

} /* namespace */

TFLMRegistration Register_KWS_ADD_INT8()
{
  TFLMRegistration r = {};
  r.init = KwsAddInit;
  r.prepare = KwsAddPrepare;
  r.invoke = KwsAddInvoke;
  r.builtin_code = BuiltinOperator_ADD;
  return r;
}

namespace
{

/* ---------------------------------------------------------------------------
 * MUL (int8): stock OpDataMul / MulInit / MulPrepare are public - reuse.
 * ------------------------------------------------------------------------ */

/* Flat broadcast mul, chain identical to reference_integer_ops::Mul:
 * acc = (in1+off1)*(in2+off2) -> requantize -> +out_zp -> clamp. */
void KwsMulFast(const OpDataMul *d, const int8_t *main_d, const int8_t *bc_d,
                int8_t *out_d, uint32_t outer, uint32_t inner, uint32_t bc_dim,
                int32_t main_off, int32_t bc_off)
{
  const int32_t out_off = d->output_zero_point;
  const int32_t amin = d->output_activation_min;
  const int32_t amax = d->output_activation_max;
  for (uint32_t o = 0; o < outer; ++o)
  {
    const int32_t bc_v = static_cast<int32_t>(bc_d[o % bc_dim]) + bc_off;
    const int8_t *mp = main_d + static_cast<size_t>(o) * inner;
    int8_t *op = out_d + static_cast<size_t>(o) * inner;
    for (uint32_t i = 0; i < inner; ++i)
    {
      int32_t acc = (static_cast<int32_t>(mp[i]) + main_off) * bc_v;
      acc = arm_nn_requantize(acc, d->output_multiplier, d->output_shift) +
            out_off;
      acc = acc < amin ? amin : acc;
      op[i] = static_cast<int8_t>(acc > amax ? amax : acc);
    }
  }
}

TfLiteStatus KwsMulInvoke(TfLiteContext *context, TfLiteNode *node)
{
  const OpDataMul *d = static_cast<const OpDataMul *>(node->user_data);
  TFLITE_DCHECK(d != nullptr);

  const TfLiteEvalTensor *in1 = micro::GetEvalInput(context, node, 0);
  const TfLiteEvalTensor *in2 = micro::GetEvalInput(context, node, 1);
  TfLiteEvalTensor *out = micro::GetEvalOutput(context, node, 0);
  const int8_t *p1 = micro::GetTensorData<int8_t>(in1);
  const int8_t *p2 = micro::GetTensorData<int8_t>(in2);
  int8_t *po = micro::GetTensorData<int8_t>(out);
  const RuntimeShape &s1 = micro::GetTensorShape(in1);
  const RuntimeShape &s2 = micro::GetTensorShape(in2);

  const int32_t off1 = -d->input1_zero_point;
  const int32_t off2 = -d->input2_zero_point;

  int axis;
  if (FastBcastAxis(s1, s2, &axis))
  {
    const uint32_t outer = OuterOf(s1, axis);
    KwsMulFast(d, p1, p2, po, outer, s1.FlatSize() / outer,
               static_cast<uint32_t>(s2.Dims(axis)), off1, off2);
  }
  else if (FastBcastAxis(s2, s1, &axis))
  {
    const uint32_t outer = OuterOf(s2, axis);
    KwsMulFast(d, p2, p1, po, outer, s2.FlatSize() / outer,
               static_cast<uint32_t>(s1.Dims(axis)), off2, off1);
  }
  else
  {
    /* Same shapes or odd broadcast: stock paths. */
    ArithmeticParams op_params;
    op_params.quantized_activation_min = d->output_activation_min;
    op_params.quantized_activation_max = d->output_activation_max;
    op_params.input1_offset = off1;
    op_params.input2_offset = off2;
    op_params.output_offset = d->output_zero_point;
    op_params.output_multiplier = d->output_multiplier;
    op_params.output_shift = d->output_shift;
    if (SameShape(s1, s2))
    {
      arm_elementwise_mul_s8(p1, p2, off1, off2, po, d->output_zero_point,
                             d->output_multiplier, d->output_shift,
                             d->output_activation_min,
                             d->output_activation_max,
                             MatchingElementsSize(s1, s2, micro::GetTensorShape(out)));
    }
    else
    {
      reference_integer_ops::BroadcastMul4DSlow(op_params, s1, p1, s2, p2,
                                                micro::GetTensorShape(out), po);
    }
  }
  return kTfLiteOk;
}

} /* namespace */

TFLMRegistration Register_KWS_MUL_INT8()
{
  TFLMRegistration r = {};
  r.init = MulInit; /* public stock init/prepare (OpDataMul) */
  r.prepare = MulPrepare;
  r.invoke = KwsMulInvoke;
  r.builtin_code = BuiltinOperator_MUL;
  return r;
}

namespace
{

/* ---------------------------------------------------------------------------
 * LOGISTIC (int8): 256-entry LUT. int8 input has exactly 256 distinct values,
 * so the table is bit-identical to a per-element double-sigmoid evaluation.
 * ------------------------------------------------------------------------ */

struct KwsLogisticData
{
  int8_t lut[256];
};

void *KwsLogisticInit(TfLiteContext *context, const char *buffer, size_t length)
{
  (void)buffer;
  (void)length;
  TFLITE_DCHECK(context->AllocatePersistentBuffer != nullptr);
  return context->AllocatePersistentBuffer(context, sizeof(KwsLogisticData));
}

TfLiteStatus KwsLogisticPrepare(TfLiteContext *context, TfLiteNode *node)
{
  TFLITE_DCHECK(node->user_data != nullptr);
  KwsLogisticData *d = static_cast<KwsLogisticData *>(node->user_data);

  MicroContext *mc = GetMicroContext(context);
  TfLiteTensor *in = mc->AllocateTempInputTensor(node, 0);
  TfLiteTensor *out = mc->AllocateTempOutputTensor(node, 0);
  if (in == nullptr || out == nullptr)
    return kTfLiteError;
  TF_LITE_ENSURE_EQ(context, in->type, kTfLiteInt8);
  TF_LITE_ENSURE_EQ(context, out->type, kTfLiteInt8);

  const double in_scale = static_cast<double>(in->params.scale);
  const int32_t in_zp = in->params.zero_point;
  const double out_scale = static_cast<double>(out->params.scale);
  const int32_t out_zp = out->params.zero_point;

  for (int i = 0; i < 256; ++i)
  {
    const double x = (i - 128 - in_zp) * in_scale;
    const double y = 1.0 / (1.0 + std::exp(-x));
    int32_t q = static_cast<int32_t>(std::lround(y / out_scale)) + out_zp;
    if (q < -128)
      q = -128;
    if (q > 127)
      q = 127;
    d->lut[i] = static_cast<int8_t>(q);
  }

  mc->DeallocateTempTfLiteTensor(in);
  mc->DeallocateTempTfLiteTensor(out);
  return kTfLiteOk;
}

TfLiteStatus KwsLogisticInvoke(TfLiteContext *context, TfLiteNode *node)
{
  const KwsLogisticData *d =
      static_cast<const KwsLogisticData *>(node->user_data);
  TFLITE_DCHECK(d != nullptr);

  const TfLiteEvalTensor *in = micro::GetEvalInput(context, node, 0);
  TfLiteEvalTensor *out = micro::GetEvalOutput(context, node, 0);
  const int8_t *p = micro::GetTensorData<int8_t>(in);
  int8_t *po = micro::GetTensorData<int8_t>(out);
  const int8_t *lut = d->lut;
  const uint32_t n = static_cast<uint32_t>(micro::GetTensorShape(out).FlatSize());
  for (uint32_t i = 0; i < n; ++i)
    po[i] = lut[p[i] + 128];
  return kTfLiteOk;
}

} /* namespace */

TFLMRegistration Register_KWS_LOGISTIC_INT8()
{
  TFLMRegistration r = {};
  r.init = KwsLogisticInit;
  r.prepare = KwsLogisticPrepare;
  r.invoke = KwsLogisticInvoke;
  r.builtin_code = BuiltinOperator_LOGISTIC;
  return r;
}

} /* namespace tflite */
