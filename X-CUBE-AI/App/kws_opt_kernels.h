/**
 * @file    kws_opt_kernels.h
 * @brief   Optimized TFLM kernels for the KWS model hot path.
 *
 * The stock INT8 registrations fall back to the slow reference
 * implementations whenever shapes broadcast:
 *   - cmsis_nn add/mul: arm_elementwise_*_s8 only for identical shapes,
 *     BroadcastAdd4DSlow / BroadcastMul4DSlow otherwise (per-element
 *     NdArray index math, ~4x the time of a flat loop).
 *   - logistic: per-element fixed-point sigmoid approximation.
 *
 * The KWS model (BC-ResNet style) broadcasts a [1,C,1,1] tensor onto
 * [1,C,H,W] in every broadcast block (TRANSPOSE -> MUL -> ADD groups in the
 * node profile), i.e. the broadcast axis is the leading (channel) axis, so a
 * tight outer/inner loop per channel with hoisted requantization is ~4x
 * faster. LOGISTIC becomes a 256-entry table lookup (int8 input -> exactly
 * 256 distinct values, table built once at prepare with true double sigmoid,
 * matching the training math).
 *
 * Everything else (non-broadcast, odd shapes) falls back to the stock
 * implementations, so numerics are identical to the reference path.
 */
#ifndef KWS_OPT_KERNELS_H
#define KWS_OPT_KERNELS_H

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_common.h"

namespace tflite
{

/* Broadcast-optimized int8 ADD (falls back to stock for other shapes). */
TFLMRegistration Register_KWS_ADD_INT8();

/* Broadcast-optimized int8 MUL (reuses the stock OpDataMul + prepare). */
TFLMRegistration Register_KWS_MUL_INT8();

/* 256-entry LUT int8 LOGISTIC. */
TFLMRegistration Register_KWS_LOGISTIC_INT8();

} /* namespace tflite */

#endif /* KWS_OPT_KERNELS_H */
