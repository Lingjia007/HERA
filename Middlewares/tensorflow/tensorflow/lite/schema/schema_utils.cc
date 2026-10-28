/* Copyright 2020 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
// Upstream TFLM file, restored for this ST drop (the ST middleware package
// ships the header but omits the .cc).

#include "tensorflow/lite/schema/schema_utils.h"

namespace tflite {

BuiltinOperator GetBuiltinCode(const OperatorCode *op_code) {
  // Operator codes were first assigned to an int8_t field; newer operators
  // live in the int32_t field. This helper picks the right one.
  const int8_t builtin = op_code->deprecated_builtin_code();
  return (builtin == BuiltinOperator_PLACEHOLDER_FOR_GREATER_OP_CODES)
             ? op_code->builtin_code()
             : static_cast<BuiltinOperator>(builtin);
}

BuiltinOperator GetBuiltinCode(const OperatorCodeT *op_codeT) {
  if (op_codeT->deprecated_builtin_code !=
      BuiltinOperator_PLACEHOLDER_FOR_GREATER_OP_CODES) {
    return static_cast<BuiltinOperator>(op_codeT->deprecated_builtin_code);
  } else {
    return op_codeT->builtin_code;
  }
}

}  // namespace tflite
