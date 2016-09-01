// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/primitives.h"

#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include "vm/assert.h"
#include "vm/double_conversion.h"
#include "vm/heap.h"
#include "vm/interpreter.h"
#include "vm/isolate.h"
#include "vm/math.h"
#include "vm/object.h"
#include "vm/os.h"
#include "vm/port.h"
#include "vm/thread_pool.h"

#define A H->activation()
#define nil H->object_store()->nil_obj()

namespace psoup {

const bool kSuccess = true;
const bool kFailure = false;

// TODO(rmacnak): Re-group the primitives and re-assign their numbers once the
// set of primitives is more stable.

#define PRIMITIVE_LIST(V)                                                      \
  V(1, Number_add)                                                             \
  V(2, Number_subtract)                                                        \
  V(3, Number_multiply)                                                        \
  V(4, Number_divide)                                                          \
  V(5, Number_div)                                                             \
  V(6, Number_mod)                                                             \
  V(7, Number_quo)                                                             \
  V(8, Number_rem)                                                             \
  V(9, Number_equal)                                                           \
  V(10, Number_less)                                                           \
  V(11, Number_greater)                                                        \
  V(12, Number_lessOrEqual)                                                    \
  V(13, Number_greaterOrEqual)                                                 \
  V(14, Number_asInteger)                                                      \
  V(15, Number_asDouble)                                                       \
  V(16, Integer_bitAnd)                                                        \
  V(17, Integer_bitOr)                                                         \
  V(18, Integer_bitXor)                                                        \
  V(19, Integer_bitShiftLeft)                                                  \
  V(20, Integer_bitShiftRight)                                                 \
  V(21, Double_floor)                                                          \
  V(22, Double_ceiling)                                                        \
  V(23, Double_sin)                                                            \
  V(24, Double_cos)                                                            \
  V(25, Double_tan)                                                            \
  V(26, Double_asin)                                                           \
  V(27, Double_acos)                                                           \
  V(28, Double_atan)                                                           \
  V(29, Double_atan2)                                                          \
  V(30, Double_exp)                                                            \
  V(31, Double_ln)                                                             \
  V(32, Double_log)                                                            \
  V(33, Double_sqrt)                                                           \
  V(34, Behavior_basicNew)                                                     \
  V(35, Object_instVarAt)                                                      \
  V(36, Object_instVarAtPut)                                                   \
  V(37, Object_instVarSize)                                                    \
  V(38, Array_class_new)                                                       \
  V(39, Array_at)                                                              \
  V(40, Array_atPut)                                                           \
  V(41, Array_size)                                                            \
  V(42, WeakArray_class_new)                                                   \
  V(43, WeakArray_at)                                                          \
  V(44, WeakArray_atPut)                                                       \
  V(45, WeakArray_size)                                                        \
  V(46, ByteArray_class_new)                                                   \
  V(47, ByteArray_at)                                                          \
  V(48, ByteArray_atPut)                                                       \
  V(49, ByteArray_size)                                                        \
  V(51, String_at)                                                             \
  V(53, String_size)                                                           \
  V(54, ByteString_hash)                                                       \
  V(55, Activation_sender)                                                     \
  V(56, Activation_senderPut)                                                  \
  V(57, Activation_bci)                                                        \
  V(58, Activation_bciPut)                                                     \
  V(59, Activation_method)                                                     \
  V(60, Activation_methodPut)                                                  \
  V(61, Activation_closure)                                                    \
  V(62, Activation_closurePut)                                                 \
  V(63, Activation_receiver)                                                   \
  V(64, Activation_receiverPut)                                                \
  V(65, Activation_tempAt)                                                     \
  V(66, Activation_tempAtPut)                                                  \
  V(67, Activation_tempSize)                                                   \
  V(68, Activation_class_new)                                                  \
  V(69, Closure_class_new)                                                     \
  V(70, Closure_numCopied)                                                     \
  V(71, Closure_definingActivation)                                            \
  V(72, Closure_definingActivationPut)                                         \
  V(73, Closure_initialBci)                                                    \
  V(74, Closure_initialBciPut)                                                 \
  V(75, Closure_numArgs)                                                       \
  V(76, Closure_numArgsPut)                                                    \
  V(77, Closure_copiedAt)                                                      \
  V(78, Closure_copiedAtPut)                                                   \
  V(85, Object_class)                                                          \
  V(86, Object_identical)                                                      \
  V(87, Object_identityHash)                                                   \
  V(89, Object_performWithAll)                                                 \
  V(90, Closure_value0)                                                        \
  V(91, Closure_value1)                                                        \
  V(92, Closure_value2)                                                        \
  V(93, Closure_value3)                                                        \
  V(94, Closure_valueArray)                                                    \
  V(95, executeMethod)                                                         \
  V(96, Behavior_allInstances)                                                 \
  V(97, Behavior_adoptInstance)                                                \
  V(98, Array_elementsForwardIdentity)                                         \
  V(100, Time_monotonicMicros)                                                 \
  V(101, Time_utcEpochMicros)                                                  \
  V(102, print)                                                                \
  V(103, exitWithBacktrace)                                                    \
  V(104, flushCache)                                                           \
  V(105, collectGarbage)                                                       \
  V(107, processExit)                                                          \
  V(108, createThread)                                                         \
  V(109, threadExit)                                                           \
  V(110, threadReceive)                                                        \
  V(111, Number_printString)                                                   \
  V(112, simulationGuard)                                                      \
  V(113, unwindProtect)                                                        \
  V(114, String_equals)                                                        \
  V(115, String_concat)                                                        \
  V(116, handlerMarker)                                                        \
  V(117, String_startsWith)                                                    \
  V(118, String_endsWith)                                                      \
  V(119, String_indexOf)                                                       \
  V(120, String_lastIndexOf)                                                   \
  V(121, String_substring)                                                     \
  V(122, String_runeAt)                                                        \
  V(123, String_class_fromRune)                                                \
  V(125, Double_rounded)                                                       \
  V(124, String_class_fromRunes)                                               \
  V(126, Object_isCanonical)                                                   \
  V(127, Object_markCanonical)                                                 \
  V(128, writeBytesToFile)                                                     \
  V(129, mainArguments)                                                        \
  V(130, readFileAsBytes)                                                      \
  V(131, Double_pow)                                                           \
  V(132, Double_class_parse)                                                   \
  V(133, currentActivation)                                                    \
  V(134, adoptInstance)                                                        \
  V(135, createPort)                                                           \
  V(136, receive)                                                              \
  V(137, spawn)                                                                \
  V(138, send)                                                                 \
  V(139, finish)                                                               \
  V(200, quickReturnSelf)                                                      \


#define DEFINE_PRIMITIVE(name)                                                 \
  static bool primitive##name(intptr_t num_args, Heap* H, Interpreter* I)

#define IS_SMI_OP(left, right)                                                 \
  left->IsSmallInteger() && right->IsSmallInteger()                            \

#define IS_MINT_OP(left, right)                                                \
  (left->IsSmallInteger() || left->IsMediumInteger()) &&                       \
  (right->IsSmallInteger() || right->IsMediumInteger())                        \

#define IS_LINT_OP(left, right)                                                \
  (left->IsSmallInteger() ||                                                   \
    left->IsMediumInteger() ||                                                 \
    left->IsLargeInteger()) &&                                                 \
  (right->IsSmallInteger() ||                                                  \
    right->IsMediumInteger() ||                                                \
    right->IsLargeInteger())                                                   \

#define IS_FLOAT_OP(left, right)                                               \
  (left->IsFloat64() || right->IsFloat64())                                    \

#define SMI_VALUE(integer)                                                     \
  static_cast<SmallInteger*>(integer)->value()                                 \

#define MINT_VALUE(integer)                                                    \
  (integer->IsSmallInteger()                                                   \
     ? static_cast<SmallInteger*>(integer)->value()                            \
     : static_cast<MediumInteger*>(integer)->value())                          \

#define LINT_VALUES                                                            \
  LargeInteger* large_left;                                                    \
  LargeInteger* large_right;                                                   \
  {                                                                            \
    HandleScope h1(H, reinterpret_cast<Object**>(&right));                     \
    large_left = LargeInteger::Expand(left, H);                                \
    HandleScope h2(H, reinterpret_cast<Object**>(&large_left));                \
    large_right = LargeInteger::Expand(right, H);                              \
  }                                                                            \

#define FLOAT_VALUE(raw_float, number)                                         \
  if (number->IsSmallInteger()) {                                              \
    raw_float = static_cast<SmallInteger*>(number)->value();                   \
  } else if (number->IsMediumInteger()) {                                      \
    raw_float = static_cast<MediumInteger*>(number)->value();                  \
  } else if (number->IsFloat64()) {                                            \
    raw_float = static_cast<Float64*>(number)->value();                        \
  } else if (number->IsLargeInteger()) {                                       \
    UNIMPLEMENTED();                                                           \
    raw_float = 0.0;                                                           \
  } else {                                                                     \
    return kFailure;                                                           \
  }                                                                            \

#define FLOAT_VALUE_OR_FALSE(raw_float, number)                                \
  if (number->IsSmallInteger()) {                                              \
    raw_float = static_cast<SmallInteger*>(number)->value();                   \
  } else if (number->IsMediumInteger()) {                                      \
    raw_float = static_cast<MediumInteger*>(number)->value();                  \
  } else if (number->IsFloat64()) {                                            \
    raw_float = static_cast<Float64*>(number)->value();                        \
  } else if (number->IsLargeInteger()) {                                       \
    UNIMPLEMENTED();                                                           \
    raw_float = 0.0;                                                           \
  } else {                                                                     \
    RETURN_BOOL(false);                                                        \
  }                                                                            \

#define RETURN_SMI(raw_integer)                                                \
  ASSERT(SmallInteger::IsSmiValue(raw_integer));                               \
  A->PopNAndPush(num_args + 1, SmallInteger::New(raw_integer));                \
  return kSuccess;                                                             \

#define RETURN_MINT(raw_integer)                                               \
  if (SmallInteger::IsSmiValue(raw_integer)) {                                 \
    A->PopNAndPush(num_args + 1, SmallInteger::New(raw_integer));              \
    return kSuccess;                                                           \
  } else {                                                                     \
    MediumInteger* result = H->AllocateMediumInteger();                        \
    result->set_value(raw_integer);                                            \
    A->PopNAndPush(num_args + 1, result);                                      \
    return kSuccess;                                                           \
  }                                                                            \

#define RETURN_LINT(large_integer)                                             \
  Object* result = LargeInteger::Reduce(large_integer, H);                     \
  A->PopNAndPush(num_args + 1, result);                                        \
  return kSuccess;                                                             \

#define RETURN_FLOAT(raw_float)                                                \
  Float64* result = H->AllocateFloat64();                                      \
  result->set_value(raw_float);                                                \
  A->PopNAndPush(num_args + 1, result);                                        \
  return kSuccess;                                                             \

#define RETURN_BOOL(raw_bool)                                                  \
  A->PopNAndPush(num_args + 1, (raw_bool) ? H->object_store()->true_obj()      \
                                          : H->object_store()->false_obj());   \
  return kSuccess;                                                             \


DEFINE_PRIMITIVE(Unimplemented) {
  UNIMPLEMENTED();
  return kFailure;
}


DEFINE_PRIMITIVE(Number_add) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    intptr_t raw_result = raw_left + raw_right;  // Cannot overflow.
    RETURN_MINT(raw_result);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    int64_t raw_result;
    if (Math::AddHasOverflow64(raw_left, raw_right, &raw_result)) {
      // Fall through to large integer operation.
    } else {
      RETURN_MINT(raw_result);
    }
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    LargeInteger* large_result = LargeInteger::Add(large_left, large_right, H);
    RETURN_LINT(large_result);
  }

  if (IS_FLOAT_OP(left, right)) {
    double raw_left, raw_right;
    FLOAT_VALUE(raw_left, left);
    FLOAT_VALUE(raw_right, right);
    double raw_result = raw_left + raw_right;
    RETURN_FLOAT(raw_result);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Number_subtract) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    intptr_t raw_result = raw_left - raw_right;  // Cannot overflow.
    RETURN_MINT(raw_result);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    int64_t raw_result;
    if (Math::SubtractHasOverflow64(raw_left, raw_right, &raw_result)) {
      // Fall through to large integer operation.
    } else {
      RETURN_MINT(raw_result);
    }
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    LargeInteger* large_result =
        LargeInteger::Subtract(large_left, large_right, H);
    RETURN_LINT(large_result);
  }

  if (IS_FLOAT_OP(left, right)) {
    double raw_left, raw_right;
    FLOAT_VALUE(raw_left, left);
    FLOAT_VALUE(raw_right, right);
    double raw_result = raw_left - raw_right;
    RETURN_FLOAT(raw_result);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Number_multiply) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
#if defined(ARCH_IS_32_BIT)
    int64_t raw_left = SMI_VALUE(left);
    int64_t raw_right = SMI_VALUE(right);
    int64_t raw_result = raw_left * raw_right;  // Cannot overflow.
    RETURN_MINT(raw_result);
#elif defined(ARCH_IS_64_BIT)
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    intptr_t raw_result;
    if (Math::MultiplyHasOverflow(raw_left, raw_right, &raw_result)) {
      // Fall through to large integer operation.
    } else {
      RETURN_MINT(raw_result);
    }
#else
#error Unimplemented
#endif
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    int64_t raw_result;
    if (Math::MultiplyHasOverflow64(raw_left, raw_right, &raw_result)) {
      // Fall through to large integer operation.
    } else {
      RETURN_MINT(raw_result);
    }
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    LargeInteger* large_result =
        LargeInteger::Multiply(large_left, large_right, H);
    RETURN_LINT(large_result);
  }

  if (IS_FLOAT_OP(left, right)) {
    double raw_left, raw_right;
    FLOAT_VALUE(raw_left, left);
    FLOAT_VALUE(raw_right, right);
    double raw_result = raw_left * raw_right;
    RETURN_FLOAT(raw_result);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Number_divide) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    if (raw_right == 0) {
      return kFailure;  // Division by zero.
    }
    if ((raw_left % raw_right) != 0) {
      return kFailure;  // Inexact division.
    }
    intptr_t raw_result = raw_left / raw_right;
    RETURN_MINT(raw_result);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    if (raw_right == 0) {
      return kFailure;  // Division by zero.
    }
    if ((raw_right == -1) && (raw_left == kMinInt64)) {
      // Overflow. Fall through to large integer operation.
    } else {
      if ((raw_left % raw_right) != 0) {
        return kFailure;  // Inexact division.
      }
      int64_t raw_result = raw_left / raw_right;
      RETURN_MINT(raw_result);
    }
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    if (large_right->size() == 0) {
      return kFailure;  // Division by zero.
    }
    LargeInteger* large_result = LargeInteger::Divide(LargeInteger::kExact,
                                                      LargeInteger::kQuoitent,
                                                      large_left,
                                                      large_right, H);
    if (large_result == NULL) {
      return kFailure;  // Inexact division.
    }
    RETURN_LINT(large_result);
  }

  if (IS_FLOAT_OP(left, right)) {
    double raw_left, raw_right;
    FLOAT_VALUE(raw_left, left);
    FLOAT_VALUE(raw_right, right);
    if (raw_right == 0.0) {
      return kFailure;  // Division by zero.
    }
    double raw_result = raw_left / raw_right;
    RETURN_FLOAT(raw_result);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Number_div) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    if (raw_right == 0) {
      return kFailure;  // Division by zero.
    }
    intptr_t raw_result = Math::FloorDiv(raw_left, raw_right);
    RETURN_MINT(raw_result);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    if (raw_right == 0) {
      return kFailure;  // Division by zero.
    }
    if ((raw_right == -1) && (raw_left == kMinInt64)) {
      // Overflow. Fall through to large integer operation.
    } else {
      int64_t raw_result = Math::FloorDiv64(raw_left, raw_right);
      RETURN_MINT(raw_result);
    }
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    if (large_right->size() == 0) {
      return kFailure;  // Division by zero.
    }
    LargeInteger* large_result = LargeInteger::Divide(LargeInteger::kFloored,
                                                      LargeInteger::kQuoitent,
                                                      large_left,
                                                      large_right, H);
    RETURN_LINT(large_result);
  }

  if (IS_FLOAT_OP(left, right)) {
    double raw_left, raw_right;
    FLOAT_VALUE(raw_left, left);
    FLOAT_VALUE(raw_right, right);
    if (raw_right == 0) {
      return kFailure;  // Division by zero.
    }
    // TODO(rmacnak): Return result as float or integer?
    double raw_result = floor(raw_left / raw_right);
    RETURN_FLOAT(raw_result);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Number_mod) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    if (raw_right == 0) {
      return kFailure;  // Division by zero.
    }
    intptr_t raw_result = Math::FloorMod(raw_left, raw_right);
    RETURN_MINT(raw_result);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    if (raw_right == 0) {
      return kFailure;  // Division by zero.
    }
    if ((raw_right == -1) && (raw_left == kMinInt64)) {
      // Overflow. Fall through to large integer operation.
    } else {
      int64_t raw_result = Math::FloorMod64(raw_left, raw_right);
      RETURN_MINT(raw_result);
    }
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    if (large_right->size() == 0) {
      return kFailure;  // Division by zero.
    }
    LargeInteger* large_result = LargeInteger::Divide(LargeInteger::kFloored,
                                                      LargeInteger::kRemainder,
                                                      large_left,
                                                      large_right, H);
    RETURN_LINT(large_result);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Number_quo) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    if (raw_right == 0) {
      return kFailure;  // Division by zero.
    }
    intptr_t raw_result = Math::TruncDiv(raw_left, raw_right);
    RETURN_MINT(raw_result);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    if (raw_right == 0) {
      return kFailure;  // Division by zero.
    }
    if ((raw_right == -1) && (raw_left == kMinInt64)) {
      // Overflow. Fall through to large integer operation.
    } else {
      int64_t raw_result = Math::TruncDiv64(raw_left, raw_right);
      RETURN_MINT(raw_result);
    }
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    if (large_right->size() == 0) {
      return kFailure;  // Division by zero.
    }
    LargeInteger* large_result = LargeInteger::Divide(LargeInteger::kTruncated,
                                                      LargeInteger::kQuoitent,
                                                      large_left,
                                                      large_right, H);
    RETURN_LINT(large_result);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Number_rem) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    if (raw_right == 0) {
      return kFailure;  // Division by zero.
    }
    intptr_t raw_result = Math::TruncMod(raw_left, raw_right);
    RETURN_MINT(raw_result);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    if (raw_right == 0) {
      return kFailure;  // Division by zero.
    }
    if ((raw_right == -1) && (raw_left == kMinInt64)) {
      // Overflow. Fall through to large integer operation.
    } else {
      int64_t raw_result = Math::TruncMod64(raw_left, raw_right);
      RETURN_MINT(raw_result);
    }
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    if (large_right->size() == 0) {
      return kFailure;  // Division by zero.
    }
    LargeInteger* large_result = LargeInteger::Divide(LargeInteger::kTruncated,
                                                      LargeInteger::kRemainder,
                                                      large_left,
                                                      large_right, H);
    RETURN_LINT(large_result);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Number_equal) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    RETURN_BOOL(raw_left == raw_right);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    RETURN_BOOL(raw_left == raw_right);
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    RETURN_BOOL(LargeInteger::Compare(large_left, large_right) == 0);
  }

  if (IS_FLOAT_OP(left, right)) {
    double raw_left, raw_right;
    FLOAT_VALUE(raw_left, left);
    FLOAT_VALUE_OR_FALSE(raw_right, right);
    RETURN_BOOL(raw_left == raw_right);
  }

  RETURN_BOOL(false);
}


DEFINE_PRIMITIVE(Number_less) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    RETURN_BOOL(raw_left < raw_right);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    RETURN_BOOL(raw_left < raw_right);
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    RETURN_BOOL(LargeInteger::Compare(large_left, large_right) > 0);
  }

  if (IS_FLOAT_OP(left, right)) {
    double raw_left, raw_right;
    FLOAT_VALUE(raw_left, left);
    FLOAT_VALUE(raw_right, right);
    RETURN_BOOL(raw_left < raw_right);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Number_greater) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    RETURN_BOOL(raw_left > raw_right);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    RETURN_BOOL(raw_left > raw_right);
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    RETURN_BOOL(LargeInteger::Compare(large_left, large_right) < 0);
  }

  if (IS_FLOAT_OP(left, right)) {
    double raw_left, raw_right;
    FLOAT_VALUE(raw_left, left);
    FLOAT_VALUE(raw_right, right);
    RETURN_BOOL(raw_left > raw_right);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Number_lessOrEqual) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    RETURN_BOOL(raw_left <= raw_right);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    RETURN_BOOL(raw_left <= raw_right);
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    RETURN_BOOL(LargeInteger::Compare(large_left, large_right) >= 0);
  }

  if (IS_FLOAT_OP(left, right)) {
    double raw_left, raw_right;
    FLOAT_VALUE(raw_left, left);
    FLOAT_VALUE(raw_right, right);
    RETURN_BOOL(raw_left <= raw_right);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Number_greaterOrEqual) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    RETURN_BOOL(raw_left >= raw_right);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    RETURN_BOOL(raw_left >= raw_right);
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    RETURN_BOOL(LargeInteger::Compare(large_left, large_right) <= 0);
  }

  if (IS_FLOAT_OP(left, right)) {
    double raw_left, raw_right;
    FLOAT_VALUE(raw_left, left);
    FLOAT_VALUE(raw_right, right);
    RETURN_BOOL(raw_left >= raw_right);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Number_asInteger) {
  ASSERT(num_args == 0);
  Object* receiver = A->Stack(0);
  if (receiver->IsFloat64()) {
    double raw_receiver = static_cast<Float64*>(receiver)->value();
    int64_t raw_result = trunc(raw_receiver);
    // TODO(rmacnak): May require LargeInteger.  Infinities and NaNs.
    RETURN_MINT(raw_result);
  }
  UNIMPLEMENTED();
  return kFailure;
}


DEFINE_PRIMITIVE(Number_asDouble) {
  ASSERT(num_args == 0);
  Object* receiver = A->Stack(0);
  if (receiver->IsSmallInteger()) {
    intptr_t raw_receiver = static_cast<SmallInteger*>(receiver)->value();
    RETURN_FLOAT(static_cast<double>(raw_receiver));
  }
  if (receiver->IsMediumInteger()) {
    int64_t raw_receiver = static_cast<MediumInteger*>(receiver)->value();
    RETURN_FLOAT(static_cast<double>(raw_receiver));
  }
  UNIMPLEMENTED();
  return kFailure;
}


DEFINE_PRIMITIVE(Integer_bitAnd) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    intptr_t raw_result = raw_left & raw_right;
    RETURN_SMI(raw_result);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    int64_t raw_result = raw_left & raw_right;
    RETURN_MINT(raw_result);
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    LargeInteger* large_result = LargeInteger::And(large_left, large_right, H);
    RETURN_LINT(large_result);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Integer_bitOr) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    intptr_t raw_result = raw_left | raw_right;
    RETURN_SMI(raw_result);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    int64_t raw_result = raw_left | raw_right;
    RETURN_MINT(raw_result);  // In fact, we know it can't be Smi.
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    LargeInteger* large_result = LargeInteger::Or(large_left, large_right, H);
    RETURN_LINT(large_result);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Integer_bitXor) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    intptr_t raw_result = raw_left ^ raw_right;
    RETURN_SMI(raw_result);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    int64_t raw_result = raw_left ^ raw_right;
    RETURN_MINT(raw_result);
  }

  if (IS_LINT_OP(left, right)) {
    LINT_VALUES;
    LargeInteger* large_result = LargeInteger::Xor(large_left, large_right, H);
    RETURN_LINT(large_result);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Integer_bitShiftLeft) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (false && IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    if (raw_right < 0) {
      return kFailure;
    }
    // bit length is for wrong size
    if (Utils::BitLength(raw_left) + raw_right < kSmiBits) {
      intptr_t raw_result = raw_left << raw_right;
      ASSERT((raw_result >> raw_right) == raw_left);
      RETURN_SMI(raw_result);
    }
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    if (raw_right < 0) {
      return kFailure;
    }
    if (Utils::BitLength(raw_left) <= (63 - raw_right)) {
      int64_t raw_result = raw_left << raw_right;
      ASSERT(raw_result >> raw_right == raw_left);
      RETURN_MINT(raw_result);
    }
  }

  if (IS_LINT_OP(left, right)) {
    LargeInteger* large_left = LargeInteger::Expand(left, H);
    right = A->Stack(0);
    if (right->IsLargeInteger()) {
      if (static_cast<LargeInteger*>(right)->negative()) {
        return kFailure;
      }
      FATAL("Out of memory.");
    }
    if (right->IsMediumInteger()) {
      int64_t raw_right = MINT_VALUE(right);
      if (raw_right < 0) {
        return kFailure;
      }
      FATAL("Out of memory.");
    }
    intptr_t raw_right = SMI_VALUE(right);
    if (raw_right < 0) {
      return kFailure;
    }
    LargeInteger* large_result =
        LargeInteger::ShiftLeft(large_left, raw_right, H);
    RETURN_LINT(large_result);
  }

  return kFailure;
}


DEFINE_PRIMITIVE(Integer_bitShiftRight) {
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (IS_SMI_OP(left, right)) {
    intptr_t raw_left = SMI_VALUE(left);
    intptr_t raw_right = SMI_VALUE(right);
    if (raw_right < 0) {
      return kFailure;
    }
    if (raw_right > kSmiBits) {
      raw_right = kSmiBits;
    }
    intptr_t raw_result = raw_left >> raw_right;
    RETURN_SMI(raw_result);
  }

  if (IS_MINT_OP(left, right)) {
    int64_t raw_left = MINT_VALUE(left);
    int64_t raw_right = MINT_VALUE(right);
    if (raw_right < 0) {
      return kFailure;
    }
    if (raw_right > 63) {
      raw_right = 63;
    }
    int64_t raw_result = raw_left >> raw_right;
    RETURN_MINT(raw_result);
  }

  if (IS_LINT_OP(left, right)) {
    LargeInteger* large_left = LargeInteger::Expand(left, H);
    right = A->Stack(0);
    if (right->IsLargeInteger()) {
      if (static_cast<LargeInteger*>(right)->negative()) {
        return kFailure;
      }
      // Shift leaves only sign bit.
      intptr_t raw_result = large_left->negative() ? -1 : 0;
      RETURN_SMI(raw_result);
    }
    if (right->IsMediumInteger()) {
      int64_t raw_right = MINT_VALUE(right);
      if (raw_right < 0) {
        return kFailure;
      }
      // Shift leaves only sign bit.
      intptr_t raw_result = large_left->negative() ? -1 : 0;
      RETURN_SMI(raw_result);
    }
    intptr_t raw_right = SMI_VALUE(right);
    if (raw_right < 0) {
      return kFailure;
    }
    intptr_t left_bits = large_left->size() * sizeof(digit_t) * kBitsPerByte;
    if (raw_right >= left_bits) {
      intptr_t raw_result = large_left->negative() ? -1 : 0;
      RETURN_SMI(raw_result);
    }

    LargeInteger* large_result =
        LargeInteger::ShiftRight(large_left, raw_right, H);
    RETURN_LINT(large_result);
  }

  return kFailure;
}


#define FLOAT_FUNCTION_1(func)                                  \
  ASSERT(num_args == 0);                                        \
  Float64* rcvr = static_cast<Float64*>(A->Stack(0));           \
  if (!rcvr->IsFloat64()) {                                     \
    return kFailure;                                            \
  }                                                             \
  RETURN_FLOAT(func(rcvr->value()));                            \


#define FLOAT_FUNCTION_2(func)                                  \
  ASSERT(num_args == 1);                                        \
  Float64* rcvr = static_cast<Float64*>(A->Stack(1));           \
  Float64* arg = static_cast<Float64*>(A->Stack(0));            \
  if (!rcvr->IsFloat64()) {                                     \
    return kFailure;                                            \
  }                                                             \
  if (!arg->IsFloat64()) {                                      \
    return kFailure;                                            \
  }                                                             \
  RETURN_FLOAT(func(rcvr->value(), arg->value()));              \


DEFINE_PRIMITIVE(Double_floor) { FLOAT_FUNCTION_1(floor); }
DEFINE_PRIMITIVE(Double_ceiling) { FLOAT_FUNCTION_1(ceil); }
DEFINE_PRIMITIVE(Double_rounded) { FLOAT_FUNCTION_1(round); }
DEFINE_PRIMITIVE(Double_sin) { FLOAT_FUNCTION_1(sin); }
DEFINE_PRIMITIVE(Double_cos) { FLOAT_FUNCTION_1(cos); }
DEFINE_PRIMITIVE(Double_tan) { FLOAT_FUNCTION_1(tan); }
DEFINE_PRIMITIVE(Double_asin) { FLOAT_FUNCTION_1(asin); }
DEFINE_PRIMITIVE(Double_acos) { FLOAT_FUNCTION_1(acos); }
DEFINE_PRIMITIVE(Double_atan) { FLOAT_FUNCTION_1(atan); }
DEFINE_PRIMITIVE(Double_atan2) { FLOAT_FUNCTION_2(atan2); }
DEFINE_PRIMITIVE(Double_exp) { FLOAT_FUNCTION_1(exp); }
DEFINE_PRIMITIVE(Double_ln) { FLOAT_FUNCTION_1(log); }
DEFINE_PRIMITIVE(Double_log) { FLOAT_FUNCTION_1(log10); }
DEFINE_PRIMITIVE(Double_sqrt) { FLOAT_FUNCTION_1(sqrt); }
DEFINE_PRIMITIVE(Double_pow) { FLOAT_FUNCTION_2(pow); }


DEFINE_PRIMITIVE(Behavior_basicNew) {
  ASSERT((num_args == 0) || (num_args == 1));

  Behavior* behavior = static_cast<Behavior*>(A->Stack(0));
  ASSERT(behavior->IsRegularObject());
  behavior->AssertCouldBeBehavior();
  SmallInteger* id = behavior->id();
  if (id == nil) {
    id = SmallInteger::New(H->AllocateClassId());  // SAFEPOINT
    behavior = static_cast<Behavior*>(A->Stack(0));
    behavior->set_id(id);
    H->RegisterClass(id->value(), behavior);
  }
  ASSERT(id->IsSmallInteger());
  SmallInteger* format = behavior->format();
  ASSERT(format->IsSmallInteger());
  intptr_t num_slots = format->value();
  ASSERT(num_slots >= 0);
  ASSERT(num_slots < 255);

  RegularObject* new_instance = H->AllocateRegularObject(id->value(),
                                                         num_slots);
  if (new_instance == 0) {
    FATAL("Out of memory");
  }
  for (intptr_t i = 0; i < num_slots; i++) {
    new_instance->set_slot(i, nil);
  }
  A->PopNAndPush(num_args + 1, new_instance);
  return kSuccess;
}


DEFINE_PRIMITIVE(Object_instVarAt) {
  ASSERT(num_args == 2);  // Always a mirror primitive.
  RegularObject* object = static_cast<RegularObject*>(A->Stack(1));
  SmallInteger* index = static_cast<SmallInteger*>(A->Stack(0));
  if (!object->IsRegularObject()) {
    return kFailure;
  }
  if (!index->IsSmallInteger()) {
    return kFailure;
  }
  Behavior* klass = object->Klass(H);
  SmallInteger* format = klass->format();
  if ((index->value() <= 0) ||
      (index->value() > format->value())) {
    UNIMPLEMENTED();
    return kFailure;
  }
  A->PopNAndPush(num_args + 1, object->slot(index->value() - 1));
  return kSuccess;
}


DEFINE_PRIMITIVE(Object_instVarAtPut) {
  ASSERT(num_args == 3);  // Always a mirror primitive.
  RegularObject* object = static_cast<RegularObject*>(A->Stack(2));
  SmallInteger* index = static_cast<SmallInteger*>(A->Stack(1));
  Object* value = A->Stack(0);
  if (!object->IsRegularObject()) {
    return kFailure;
  }
  if (!index->IsSmallInteger()) {
    return kFailure;
  }
  Behavior* klass = object->Klass(H);
  SmallInteger* format = klass->format();
  if ((index->value() <= 0) ||
      (index->value() > format->value())) {
    UNIMPLEMENTED();
    return kFailure;
  }
  object->set_slot(index->value() - 1, value);
  A->PopNAndPush(num_args + 1, value);
  return kSuccess;
}


DEFINE_PRIMITIVE(Object_instVarSize) { UNIMPLEMENTED(); return kSuccess; }


DEFINE_PRIMITIVE(Array_class_new) {
  ASSERT(num_args == 1);
  Object* top = A->Stack(0);
  if (!top->IsSmallInteger()) {
    return kFailure;
  }
  intptr_t length = static_cast<SmallInteger*>(top)->value();
  if (length < 0) {
    return kFailure;
  }
  Array* result = H->AllocateArray(length);  // SAFEPOINT
  for (intptr_t i = 0; i < length; i++) {
    result->set_element(i, nil);
  }
  A->PopNAndPush(num_args + 1, result);
  return kSuccess;
}


DEFINE_PRIMITIVE(Array_at) {
  ASSERT(num_args == 1);
  Array* array = static_cast<Array*>(A->Stack(1));
  SmallInteger* index = static_cast<SmallInteger*>(A->Stack(0));
  if (!array->IsArray()) {
    UNREACHABLE();
    return kFailure;
  }
  if (!index->IsSmallInteger()) {
    return kFailure;
  }
  if (index->value() <= 0) {
    return kFailure;
  }
  if (index->value() > array->size()->value()) {
    return kFailure;
  }
  Object* value = array->element(index->value() - 1);
  A->PopNAndPush(num_args + 1, value);
  return kSuccess;
}


DEFINE_PRIMITIVE(Array_atPut) {
  ASSERT(num_args == 2);
  Array* array = static_cast<Array*>(A->Stack(2));
  SmallInteger* index = static_cast<SmallInteger*>(A->Stack(1));
  Object* value = A->Stack(0);
  if (!array->IsArray()) {
    UNREACHABLE();
    return kFailure;
  }
  if (!index->IsSmallInteger()) {
    return kFailure;
  }
  if (index->value() <= 0) {
    return kFailure;
  }
  if (index->value() > array->size()->value()) {
    return kFailure;
  }
  array->set_element(index->value() - 1, value);
  A->PopNAndPush(num_args + 1, value);
  return kSuccess;
}


DEFINE_PRIMITIVE(Array_size) {
  ASSERT(num_args == 0);
  Array* array = static_cast<Array*>(A->Stack(0));
  if (!array->IsArray()) {
    return kFailure;
  }
  A->PopNAndPush(num_args + 1, array->size());
  return kSuccess;
}


DEFINE_PRIMITIVE(WeakArray_class_new) {
  ASSERT(num_args == 1);
  Object* top = A->Stack(0);
  if (!top->IsSmallInteger()) {
    return kFailure;
  }
  intptr_t length = static_cast<SmallInteger*>(top)->value();
  if (length < 0) {
    return kFailure;
  }
  WeakArray* result = H->AllocateWeakArray(length);  // SAFEPOINT
  for (intptr_t i = 0; i < length; i++) {
    result->set_element(i, nil);
  }
  A->PopNAndPush(num_args + 1, result);
  return kSuccess;
}


DEFINE_PRIMITIVE(WeakArray_at) {
  ASSERT(num_args == 1);
  WeakArray* array = static_cast<WeakArray*>(A->Stack(1));
  SmallInteger* index = static_cast<SmallInteger*>(A->Stack(0));
  if (!array->IsWeakArray()) {
    UNREACHABLE();
    return kFailure;
  }
  if (!index->IsSmallInteger()) {
    return kFailure;
  }
  if (index->value() <= 0) {
    return kFailure;
  }
  if (index->value() > array->size()->value()) {
    return kFailure;
  }
  Object* value = array->element(index->value() - 1);
  A->PopNAndPush(num_args + 1, value);
  return kSuccess;
}


DEFINE_PRIMITIVE(WeakArray_atPut) {
  ASSERT(num_args == 2);
  WeakArray* array = static_cast<WeakArray*>(A->Stack(2));
  SmallInteger* index = static_cast<SmallInteger*>(A->Stack(1));
  Object* value = A->Stack(0);
  if (!array->IsWeakArray()) {
    UNREACHABLE();
    return kFailure;
  }
  if (!index->IsSmallInteger()) {
    return kFailure;
  }
  if (index->value() <= 0) {
    return kFailure;
  }
  if (index->value() > array->size()->value()) {
    return kFailure;
  }
  array->set_element(index->value() - 1, value);
  A->PopNAndPush(num_args + 1, value);
  return kSuccess;
}


DEFINE_PRIMITIVE(WeakArray_size) {
  ASSERT(num_args == 0);
  WeakArray* array = static_cast<WeakArray*>(A->Stack(0));
  if (!array->IsWeakArray()) {
    return kFailure;
  }
  A->PopNAndPush(num_args + 1, array->size());
  return kSuccess;
}


DEFINE_PRIMITIVE(ByteArray_class_new) {
  ASSERT(num_args == 1);
  Behavior* cls = static_cast<Behavior*>(A->Stack(1));
  SmallInteger* size = static_cast<SmallInteger*>(A->Stack(0));
  if (!size->IsSmallInteger()) {
    return kFailure;
  }
  if (cls->id()->value() != kByteArrayCid) {
    UNREACHABLE();
  }
  intptr_t length = size->value();
  if (length < 0) {
    return kFailure;
  }
  ByteArray* result = H->AllocateByteArray(length);  // SAFEPOINT
  memset(result->element_addr(0), 0, length);

  A->PopNAndPush(num_args + 1, result);
  return kSuccess;
}


DEFINE_PRIMITIVE(ByteArray_at) {
  ASSERT(num_args == 1);
  ByteArray* array = static_cast<ByteArray*>(A->Stack(1));
  SmallInteger* index = static_cast<SmallInteger*>(A->Stack(0));
  if (!array->IsByteArray()) {
    UNREACHABLE();
    return kFailure;
  }
  if (!index->IsSmallInteger()) {
    return kFailure;
  }
  if (index->value() <= 0) {
    return kFailure;
  }
  if (index->value() > array->size()->value()) {
    return kFailure;
  }
  uint8_t value = array->element(index->value() - 1);
  A->PopNAndPush(num_args + 1, SmallInteger::New(value));
  return kSuccess;
}


DEFINE_PRIMITIVE(ByteArray_atPut) {
  ASSERT(num_args == 2);
  ByteArray* array = static_cast<ByteArray*>(A->Stack(2));
  SmallInteger* index = static_cast<SmallInteger*>(A->Stack(1));
  SmallInteger* value = static_cast<SmallInteger*>(A->Stack(0));
  if (!array->IsByteArray()) {
    UNREACHABLE();
    return kFailure;
  }
  if (!index->IsSmallInteger()) {
    return kFailure;
  }
  if (index->value() <= 0) {
    return kFailure;
  }
  if (index->value() > array->size()->value()) {
    return kFailure;
  }
  if (!value->IsSmallInteger()) {
    return kFailure;
  }
  if (value->value() < 0) {
    return kFailure;
  }
  if (value->value() > 255) {
    return kFailure;
  }
  array->set_element(index->value() - 1, value->value());

  A->PopNAndPush(num_args + 1, value);
  return kSuccess;
}


DEFINE_PRIMITIVE(ByteArray_size) {
  ASSERT(num_args == 0);
  ByteArray* array = static_cast<ByteArray*>(A->Stack(0));
  if (!array->IsByteArray()) {
    return kFailure;
  }
  A->PopNAndPush(num_args + 1, array->size());
  return kSuccess;
}


DEFINE_PRIMITIVE(String_at) {
  ASSERT(num_args == 1);
  Object* receiver = A->Stack(1);
  SmallInteger* index = static_cast<SmallInteger*>(A->Stack(0));
  if (!index->IsSmallInteger()) {
    return kFailure;
  }
  if (receiver->IsByteString()) {
    ByteString* string = static_cast<ByteString*>(receiver);
    if ((index->value() <= 0) || (index->value() > string->size()->value())) {
      return kFailure;
    }
    uint8_t the_char = string->element(index->value() - 1);

    ByteString* result = H->AllocateByteString(1);  // SAFEPOINT
    result->set_element(0, the_char);
    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  } else if (receiver->IsWideString()) {
    WideString* string = static_cast<WideString*>(receiver);
    if ((index->value() <= 0) || (index->value() > string->size()->value())) {
      return kFailure;
    }
    uint32_t the_char = string->element(index->value() - 1);

    WideString* result = H->AllocateWideString(1);  // SAFEPOINT
    result->set_element(0, the_char);
    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  } else {
    UNREACHABLE();
    return kFailure;
  }
}


DEFINE_PRIMITIVE(String_size) {
  ASSERT(num_args == 0);
  Object* rcvr = A->Stack(0);
  if (rcvr->IsByteString()) {
    ByteString* string = static_cast<ByteString*>(rcvr);
    A->PopNAndPush(num_args + 1, string->size());
    return kSuccess;
  } else if (rcvr->IsWideString()) {
    WideString* string = static_cast<WideString*>(rcvr);
    A->PopNAndPush(num_args + 1, string->size());
    return kSuccess;
  }
  UNREACHABLE();
  return kFailure;
}


DEFINE_PRIMITIVE(ByteString_hash) {
  ASSERT(num_args == 0);
  Object* rcvr = A->Stack(0);
  if (rcvr->IsByteString()) {
    ByteString* string = static_cast<ByteString*>(rcvr);
    string->EnsureHash();
    A->PopNAndPush(num_args + 1, string->hash());
    return kSuccess;
  } else if (rcvr->IsWideString()) {
    WideString* string = static_cast<WideString*>(rcvr);
    string->EnsureHash();
    A->PopNAndPush(num_args + 1, string->hash());
    return kSuccess;
  }
  return kFailure;
}


DEFINE_PRIMITIVE(Activation_sender) {
  ASSERT(num_args == 0);
  Activation* rcvr = static_cast<Activation*>(A->Stack(0));
  if (!rcvr->IsActivation()) {
    UNIMPLEMENTED();
  }
  A->PopNAndPush(num_args + 1, rcvr->sender());
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_senderPut) {
  ASSERT(num_args == 1);
  Activation* rcvr = static_cast<Activation*>(A->Stack(1));
  Activation* val = static_cast<Activation*>(A->Stack(0));
  if (!rcvr->IsActivation()) {
    UNIMPLEMENTED();
  }
  if (!val->IsActivation() && val != nil) {
    UNIMPLEMENTED();
  }
  rcvr->set_sender(val);
  A->PopNAndPush(num_args + 1, rcvr);
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_bci) {
  ASSERT(num_args == 0);
  Activation* rcvr = static_cast<Activation*>(A->Stack(0));
  if (!rcvr->IsActivation()) {
    UNIMPLEMENTED();
  }
  A->PopNAndPush(num_args + 1, rcvr->bci());
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_bciPut) {
  ASSERT(num_args == 1);
  Activation* rcvr = static_cast<Activation*>(A->Stack(1));
  SmallInteger* val = static_cast<SmallInteger*>(A->Stack(0));
  if (!rcvr->IsActivation()) {
    UNIMPLEMENTED();
  }
  if (!val->IsSmallInteger() && val != nil) {
    UNIMPLEMENTED();
  }
  rcvr->set_bci(val);
  A->PopNAndPush(num_args + 1, rcvr);
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_method) {
  ASSERT(num_args == 0);
  Activation* subject = static_cast<Activation*>(A->Stack(0));
  if (!subject->IsActivation()) {
    UNIMPLEMENTED();
  }
  A->PopNAndPush(num_args + 1, subject->method());
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_methodPut) {
  ASSERT(num_args == 1);
  Activation* subject = static_cast<Activation*>(A->Stack(1));
  Method* new_method = static_cast<Method*>(A->Stack(0));
  if (!subject->IsActivation()) {
    UNIMPLEMENTED();
  }
  subject->set_method(new_method);
  A->PopNAndPush(num_args + 1, subject);
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_closure) {
  ASSERT(num_args == 0);
  Activation* subject = static_cast<Activation*>(A->Stack(0));
  if (!subject->IsActivation()) {
    UNIMPLEMENTED();
  }
  A->PopNAndPush(num_args + 1, subject->closure());
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_closurePut) {
  ASSERT(num_args == 1);
  Activation* subject = static_cast<Activation*>(A->Stack(1));
  Closure* new_closure = static_cast<Closure*>(A->Stack(0));
  if (!subject->IsActivation() || !new_closure->IsClosure()) {
    UNIMPLEMENTED();
  }
  subject->set_closure(new_closure);
  A->PopNAndPush(num_args + 1, subject);
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_receiver) {
  ASSERT(num_args == 0);
  Activation* subject = static_cast<Activation*>(A->Stack(0));
  if (!subject->IsActivation()) {
    UNIMPLEMENTED();
  }
  A->PopNAndPush(num_args + 1, subject->receiver());
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_receiverPut) {
  ASSERT(num_args == 1);
  Activation* subject = static_cast<Activation*>(A->Stack(1));
  Object* new_receiver = A->Stack(0);
  if (!subject->IsActivation()) {
    UNIMPLEMENTED();
  }
  subject->set_receiver(new_receiver);
  A->PopNAndPush(num_args + 1, subject);
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_tempAt) {
  ASSERT(num_args == 1);
  Activation* rcvr = static_cast<Activation*>(A->Stack(1));
  SmallInteger* index = static_cast<SmallInteger*>(A->Stack(0));
  if (!rcvr->IsActivation()) {
    UNIMPLEMENTED();
  }
  if (!index->IsSmallInteger()) {
    UNIMPLEMENTED();
  }
  A->PopNAndPush(num_args + 1, rcvr->temp(index->value() - 1));
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_tempAtPut) {
  ASSERT(num_args == 2);
  Activation* rcvr = static_cast<Activation*>(A->Stack(2));
  SmallInteger* index = static_cast<SmallInteger*>(A->Stack(1));
  Object* value = static_cast<SmallInteger*>(A->Stack(0));
  if (!rcvr->IsActivation()) {
    UNIMPLEMENTED();
  }
  if (!index->IsSmallInteger()) {
    UNIMPLEMENTED();
  }
  rcvr->set_temp(index->value() - 1, value);
  A->PopNAndPush(num_args + 1, value);
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_tempSize) {
  ASSERT(num_args == 0);
  Activation* receiver = static_cast<Activation*>(A->Stack(0));
  if (!receiver->IsActivation()) {
    UNREACHABLE();
  }
  A->PopNAndPush(num_args + 1, SmallInteger::New(receiver->stack_depth()));
  return kSuccess;
}


DEFINE_PRIMITIVE(Activation_class_new) {
  ASSERT(num_args == 0);
  Activation* result = H->AllocateActivation();  // SAFEPOINT
  result->set_sender(static_cast<Activation*>(nil));
  result->set_bci(static_cast<SmallInteger*>(nil));
  result->set_method(static_cast<Method*>(nil));
  result->set_closure(static_cast<Closure*>(nil));
  result->set_receiver(nil);
  result->set_stack_depth(0);
  A->PopNAndPush(num_args + 1, result);
  return kSuccess;
}


DEFINE_PRIMITIVE(Closure_class_new) { UNIMPLEMENTED(); return kSuccess; }


DEFINE_PRIMITIVE(Closure_numCopied) {
  ASSERT(num_args == 1);
  Closure* subject = static_cast<Closure*>(A->Stack(0));
  if (!subject->IsClosure()) {
    UNIMPLEMENTED();
  }
  A->PopNAndPush(num_args + 1, SmallInteger::New(subject->num_copied()));
  return kSuccess;
}


DEFINE_PRIMITIVE(Closure_definingActivation) {
  ASSERT(num_args == 0 || num_args == 1);
  Closure* subject = static_cast<Closure*>(A->Stack(0));
  if (!subject->IsClosure()) {
    UNIMPLEMENTED();
  }
  A->PopNAndPush(num_args + 1, subject->defining_activation());
  return kSuccess;
}


DEFINE_PRIMITIVE(Closure_definingActivationPut) {
  UNIMPLEMENTED();
  return kSuccess;
}


DEFINE_PRIMITIVE(Closure_initialBci) {
  ASSERT(num_args == 1);
  Closure* subject = static_cast<Closure*>(A->Stack(0));
  if (!subject->IsClosure()) {
    UNIMPLEMENTED();
  }
  A->PopNAndPush(num_args + 1, subject->initial_bci());
  return kSuccess;
}


DEFINE_PRIMITIVE(Closure_initialBciPut) { UNIMPLEMENTED(); return kSuccess; }


DEFINE_PRIMITIVE(Closure_numArgs) {
  ASSERT(num_args == 0);
  Closure* rcvr = static_cast<Closure*>(A->Stack(0));
  if (!rcvr->IsClosure()) {
    UNREACHABLE();
    return kFailure;
  }
  A->PopNAndPush(num_args + 1, rcvr->num_args());
  return kSuccess;
}


DEFINE_PRIMITIVE(Closure_numArgsPut) { UNIMPLEMENTED(); return kSuccess; }
DEFINE_PRIMITIVE(Closure_copiedAt) { UNIMPLEMENTED(); return kSuccess; }
DEFINE_PRIMITIVE(Closure_copiedAtPut) { UNIMPLEMENTED(); return kSuccess; }


DEFINE_PRIMITIVE(Object_class) {
  ASSERT((num_args == 0) || (num_args == 1));
  Object* subject = A->Stack(0);
  A->PopNAndPush(num_args + 1, subject->Klass(H));
  return kSuccess;
}


DEFINE_PRIMITIVE(Object_identical) {
  ASSERT(num_args == 1 || num_args == 2);
  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  Object* result;
  if (left == right) {
    result = H->object_store()->true_obj();
  } else {
    result = H->object_store()->false_obj();
  }
  A->PopNAndPush(num_args + 1, result);
  return kSuccess;
}


DEFINE_PRIMITIVE(Object_identityHash) {
  ASSERT(num_args == 0 || num_args == 1);
  const intptr_t kHashMask = 0x1FFFFFFF;  // TODO(rmacnak): kPositiveSmiMask.
  Object* receiver = A->Stack(0);
  if (receiver->IsSmallInteger()) {
    intptr_t hash = static_cast<SmallInteger*>(receiver)->value();
    hash = (hash & kHashMask) + 1;
    ASSERT(hash > 0);
    A->PopNAndPush(num_args + 1, SmallInteger::New(hash));
    return kSuccess;
  } else {
    // TODO(rmacnak): Use the string's hash? ASSERT(!receiver->IsByteString());

    intptr_t hash = receiver->identity_hash();
    if (hash == 0) {
      hash = (H->NextIdentityHash() + 1) & kHashMask;
      ASSERT(hash > 0);
      receiver->set_identity_hash(hash);
    }
    A->PopNAndPush(num_args + 1, SmallInteger::New(hash));
    return kSuccess;
  }
}


DEFINE_PRIMITIVE(Object_performWithAll) {
  ASSERT(num_args == 3);

  Object* receiver = A->Stack(2);
  ByteString* selector = static_cast<ByteString*>(A->Stack(1));
  Array* arguments = static_cast<Array*>(A->Stack(0));

  if (!selector->IsByteString() ||
      !selector->is_canonical() ||
      !arguments->IsArray()) {
    UNIMPLEMENTED();
    return kFailure;
  }

  A->Drop(num_args + 1);

  intptr_t perform_args = arguments->Size();
  A->Push(receiver);
  for (intptr_t i = 0; i < perform_args; i++) {
    A->Push(arguments->element(i));
  }

  I->SendOrdinary(selector, perform_args);
  return kSuccess;
}


DEFINE_PRIMITIVE(Closure_value0) {
  ASSERT(num_args == 0);

  Activation* new_activation = H->AllocateActivation();  // SAFEPOINT

  Closure* closure = static_cast<Closure*>(A->Stack(0));
  ASSERT(closure->IsClosure());
  if (closure->num_args() != 0) {
    return kFailure;
  }

  Activation* home_context = closure->defining_activation();

  new_activation->set_sender(A);
  new_activation->set_bci(closure->initial_bci());
  new_activation->set_method(home_context->method());
  new_activation->set_closure(closure);
  new_activation->set_receiver(home_context->receiver());
  new_activation->set_stack_depth(0);

  // No arguments.

  intptr_t num_copied = closure->num_copied();
  for (intptr_t i = 0; i < num_copied; i++) {
    new_activation->Push(closure->copied(i));
  }

  // Temps allocated by bytecodes

  A->Drop(1);
  H->set_activation(new_activation);
  return kSuccess;
}


DEFINE_PRIMITIVE(Closure_value1) {
  ASSERT(num_args == 1);

  Activation* new_activation = H->AllocateActivation();  // SAFEPOINT

  Closure* closure = static_cast<Closure*>(A->Stack(1));
  ASSERT(closure->IsClosure());
  if (closure->num_args()->value() != 1) {
    return kFailure;
  }

  Activation* home_context = closure->defining_activation();

  new_activation->set_sender(A);
  new_activation->set_bci(closure->initial_bci());
  new_activation->set_method(home_context->method());
  new_activation->set_closure(closure);
  new_activation->set_receiver(home_context->receiver());
  new_activation->set_stack_depth(0);

  new_activation->Push(A->Stack(0));  // Arg 0

  intptr_t num_copied = closure->num_copied();
  for (intptr_t i = 0; i < num_copied; i++) {
    new_activation->Push(closure->copied(i));
  }

  // Temps allocated by bytecodes

  A->Drop(2);
  H->set_activation(new_activation);
  return kSuccess;
}


DEFINE_PRIMITIVE(Closure_value2) {
  ASSERT(num_args == 2);

  Activation* new_activation = H->AllocateActivation();  // SAFEPOINT

  Closure* closure = static_cast<Closure*>(A->Stack(2));
  ASSERT(closure->IsClosure());
  if (closure->num_args()->value() != 2) {
    return kFailure;
  }

  Activation* home_context = closure->defining_activation();

  new_activation->set_sender(A);
  new_activation->set_bci(closure->initial_bci());
  new_activation->set_method(home_context->method());
  new_activation->set_closure(closure);
  new_activation->set_receiver(home_context->receiver());
  new_activation->set_stack_depth(0);

  new_activation->Push(A->Stack(1));  // Arg 0
  new_activation->Push(A->Stack(0));  // Arg 1

  intptr_t num_copied = closure->num_copied();
  for (intptr_t i = 0; i < num_copied; i++) {
    new_activation->Push(closure->copied(i));
  }

  // Temps allocated by bytecodes

  A->Drop(3);
  H->set_activation(new_activation);
  return kSuccess;
}


DEFINE_PRIMITIVE(Closure_value3) {
  ASSERT(num_args == 3);

  Activation* new_activation = H->AllocateActivation();  // SAFEPOINT

  Closure* closure = static_cast<Closure*>(A->Stack(3));
  ASSERT(closure->IsClosure());
  if (closure->num_args()->value() != 3) {
    return kFailure;
  }

  Activation* home_context = closure->defining_activation();

  new_activation->set_sender(A);
  new_activation->set_bci(closure->initial_bci());
  new_activation->set_method(home_context->method());
  new_activation->set_closure(closure);
  new_activation->set_receiver(home_context->receiver());
  new_activation->set_stack_depth(0);

  new_activation->Push(A->Stack(2));  // Arg 0
  new_activation->Push(A->Stack(1));  // Arg 1
  new_activation->Push(A->Stack(0));  // Arg 2

  intptr_t num_copied = closure->num_copied();
  for (intptr_t i = 0; i < num_copied; i++) {
    new_activation->Push(closure->copied(i));
  }

  // Temps allocated by bytecodes

  A->Drop(4);
  H->set_activation(new_activation);
  return kSuccess;
}


DEFINE_PRIMITIVE(Closure_valueArray) {
  ASSERT(num_args == 1);

  Activation* new_activation = H->AllocateActivation();  // SAFEPOINT

  Closure* closure = static_cast<Closure*>(A->Stack(1));
  Array* args = static_cast<Array*>(A->Stack(0));
  ASSERT(closure->IsClosure());
  ASSERT(args->IsArray());
  if (closure->num_args() != args->size()) {
    return kFailure;
  }
  Activation* home_context = closure->defining_activation();

  new_activation->set_sender(A);
  new_activation->set_bci(closure->initial_bci());
  new_activation->set_method(home_context->method());
  new_activation->set_closure(closure);
  new_activation->set_receiver(home_context->receiver());
  new_activation->set_stack_depth(0);

  for (intptr_t i = 0; i < args->Size(); i++) {
    new_activation->Push(args->element(i));
  }

  intptr_t num_copied = closure->num_copied();
  for (intptr_t i = 0; i < num_copied; i++) {
    new_activation->Push(closure->copied(i));
  }

  // Temps allocated by bytecodes

  A->Drop(num_args + 1);
  H->set_activation(new_activation);
  return kSuccess;
}


DEFINE_PRIMITIVE(executeMethod) {
  ASSERT(num_args == 3);

  Activation* new_activation = H->AllocateActivation();  // SAFEPOINT

  Object* new_receiver = A->Stack(2);
  Method* method = static_cast<Method*>(A->Stack(1));
  Array* arguments = static_cast<Array*>(A->Stack(0));
  if (!arguments->IsArray()) {
    UNIMPLEMENTED();
  }
  if (!method->IsRegularObject()) {
    UNIMPLEMENTED();
  }
  if (arguments->Size() != method->NumArgs()) {
    UNIMPLEMENTED();
  }

  new_activation->set_sender(A);
  new_activation->set_bci(SmallInteger::New(1));
  new_activation->set_method(method);
  new_activation->set_closure(static_cast<Closure*>(nil));
  new_activation->set_receiver(new_receiver);
  new_activation->set_stack_depth(0);

  for (intptr_t i = 0; i < arguments->Size(); i++) {
    new_activation->Push(arguments->element(i));
  }
  intptr_t num_temps = method->NumTemps();
  for (intptr_t i = 0; i < num_temps; i++) {
    new_activation->Push(nil);
  }

  A->Drop(num_args + 1);
  H->set_activation(new_activation);
  return kSuccess;
}


DEFINE_PRIMITIVE(Behavior_allInstances) {
  ASSERT(num_args == 1);
  Behavior* cls = static_cast<Behavior*>(A->Stack(0));
  if (!cls->IsRegularObject()) {
    UNREACHABLE();
  }
  intptr_t cid = cls->id()->value();
  if (cid == kIllegalCid) {
    UNREACHABLE();
  }
  intptr_t num_instances = H->CountInstances(cid);
  if (cid == kArrayCid) {
    num_instances++;
  }
  Array* result = H->AllocateArray(num_instances);  // SAFEPOINT
  result->set_size(SmallInteger::New(num_instances));
  intptr_t num_instances2 = H->CollectInstances(cid, result);

  ASSERT(num_instances == num_instances2);
  // Note that if a GC happens there may be fewer instances than
  // we initially counted. TODO(rmacnak): truncate result.
  OS::PrintErr("Found %" Pd " instances of %" Pd "\n", num_instances, cid);

  A->PopNAndPush(num_args + 1, result);
  return kSuccess;
}


DEFINE_PRIMITIVE(Behavior_adoptInstance) {
  UNIMPLEMENTED();
  return kFailure;
}


DEFINE_PRIMITIVE(Array_elementsForwardIdentity) {
  ASSERT(num_args == 2);
  Array* left = static_cast<Array*>(A->Stack(1));
  Array* right = static_cast<Array*>(A->Stack(0));
  if (left->IsArray() && right->IsArray()) {
    if (H->BecomeForward(left, right)) {
      A->Drop(num_args);
      return kSuccess;
    }
  }
  return kFailure;
}


DEFINE_PRIMITIVE(Time_monotonicMicros) {
  ASSERT(num_args == 0);
  int64_t now = OS::CurrentMonotonicMicros();
  if (SmallInteger::IsSmiValue(now)) {
    A->PopNAndPush(num_args + 1, SmallInteger::New(now));
    return kSuccess;
  } else {
    MediumInteger* result = H->AllocateMediumInteger();  // SAFEPOINT
    result->set_value(now);
    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  }
}


DEFINE_PRIMITIVE(Time_utcEpochMicros) { UNIMPLEMENTED(); return kSuccess; }


DEFINE_PRIMITIVE(print) {
  ASSERT(num_args == 1);
  ASSERT(A->stack_depth() >= 2);

  Object* message = A->Stack(0);
  if (message->IsByteString()) {
    ByteString* string = static_cast<ByteString*>(message);
    const char* cstr =
        reinterpret_cast<const char*>(string->element_addr(0));
    OS::Print("%.*s\n", static_cast<int>(string->Size()), cstr);
  } else if (message->IsWideString()) {
    WideString* string = static_cast<WideString*>(message);
    COMPILE_ASSERT(sizeof(wchar_t) == sizeof(uint32_t));
    const wchar_t* cstr =
        reinterpret_cast<const wchar_t*>(string->element_addr(0));
    OS::PrintErr("[%.*ls]\n", static_cast<int>(string->Size()), cstr);
  } else {
    const char* cstr = message->ToCString(H);
    OS::Print("[print] %s\n", cstr);
    free((void*)cstr);  // NOLINT
  }
  A->Drop(num_args);
  return kSuccess;
}


DEFINE_PRIMITIVE(exitWithBacktrace) {
  H->PrintStack();
  OS::Exit(-1);
  UNREACHABLE();
  return kFailure;
}


DEFINE_PRIMITIVE(flushCache) {
  // Atomicity may require this to be part of an atomic install's become. If so,
  // remove this separate primitive.
  UNREACHABLE();
  return kFailure;
}


DEFINE_PRIMITIVE(collectGarbage) {
  H->Scavenge();  // SAFEPOINT
  A->Drop(num_args);
  return kSuccess;
}


DEFINE_PRIMITIVE(processExit) {
  ASSERT(num_args == 1);
  SmallInteger* code = static_cast<SmallInteger*>(A->Stack(0));
  if (!code->IsSmallInteger()) {
    code->Print(H);
    FATAL("Non-integer exit code.");
  }
  OS::Exit(code->value());
  UNREACHABLE();
  return kFailure;
}


DEFINE_PRIMITIVE(createThread) { UNIMPLEMENTED(); return kSuccess; }


DEFINE_PRIMITIVE(threadExit) {
  UNIMPLEMENTED();

  UNREACHABLE();
  A->Drop(num_args);
  return kFailure;
}


DEFINE_PRIMITIVE(threadReceive) {
  UNIMPLEMENTED();

  A->PopNAndPush(num_args + 1, SmallInteger::New(0));
  return kSuccess;
}


DEFINE_PRIMITIVE(Number_printString) {
  ASSERT(num_args == 0);
  Object* receiver = A->Stack(0);

  char buffer[64];
  intptr_t length = -1;

  if (receiver->IsSmallInteger()) {
    intptr_t value = static_cast<SmallInteger*>(receiver)->value();
    length = snprintf(buffer, sizeof(buffer), "%" Pd "", value);
  } else if (receiver->IsMediumInteger()) {
    int64_t value = static_cast<MediumInteger*>(receiver)->value();
    length = snprintf(buffer, sizeof(buffer), "%" Pd64 "", value);
  } else if (receiver->IsFloat64()) {
    double value = static_cast<Float64*>(receiver)->value();
    DoubleToCString(value, buffer, sizeof(buffer));
    length = strlen(buffer);
  } else if (receiver->IsLargeInteger()) {
    LargeInteger* large = static_cast<LargeInteger*>(receiver);
    ByteString* result = LargeInteger::PrintString(large, H);
    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  } else {
    UNIMPLEMENTED();
  }
  ASSERT(length < 64);

  ByteString* result = H->AllocateByteString(length);  // SAFEPOINT
  memcpy(result->element_addr(0), buffer, length);

  A->PopNAndPush(num_args + 1, result);
  return kSuccess;
}


DEFINE_PRIMITIVE(simulationGuard) {
  // This is a marker primitive for the in-image simulation.
  return kFailure;
}


DEFINE_PRIMITIVE(unwindProtect) {
  // This is a marker primitive checked on non-local return.
  return kFailure;
}


template<typename L, typename R>
static bool StringEquals(L* left, R* right, Heap* H, intptr_t num_args) {
  if (left->size() != right->size()) {
    RETURN_BOOL(false);
  }
  if (left->EnsureHash() != right->EnsureHash()) {
    RETURN_BOOL(false);
  }
  intptr_t length = left->Size();
  for (intptr_t i = 0; i < length; i++) {
    if (left->element(i) != right->element(i)) {
      RETURN_BOOL(false);
    }
  }
  RETURN_BOOL(true);
}


DEFINE_PRIMITIVE(String_equals) {
  ASSERT(num_args == 1);

  Object* left = A->Stack(1);
  Object* right = A->Stack(0);
  if (left == right) {
    RETURN_BOOL(true);
    return kSuccess;
  }
  if (left->IsByteString()) {
    if (right->IsByteString()) {
      return StringEquals(static_cast<ByteString*>(left),
                          static_cast<ByteString*>(right),
                          H, num_args);
    } else if (right->IsWideString()) {
      return StringEquals(static_cast<ByteString*>(left),
                          static_cast<WideString*>(right),
                          H, num_args);
    } else {
      RETURN_BOOL(false);
    }
  } else if (left->IsWideString()) {
    if (right->IsByteString()) {
      return StringEquals(static_cast<WideString*>(left),
                          static_cast<ByteString*>(right),
                          H, num_args);
    } else if (right->IsWideString()) {
      return StringEquals(static_cast<WideString*>(left),
                          static_cast<WideString*>(right),
                          H, num_args);
    } else {
      RETURN_BOOL(false);
    }
  } else {
    UNREACHABLE();
    return kFailure;
  }
}


DEFINE_PRIMITIVE(String_concat) {
  ASSERT(num_args == 1);

  if (A->Stack(1)->IsByteString() &&
      A->Stack(0)->IsByteString()) {
    ByteString* a = static_cast<ByteString*>(A->Stack(1));
    ByteString* b = static_cast<ByteString*>(A->Stack(0));
    intptr_t a_length = a->Size();
    intptr_t b_length = b->Size();
    ByteString* result =
        H->AllocateByteString(a_length + b_length);  // SAFEPOINT
    a = static_cast<ByteString*>(A->Stack(1));
    b = static_cast<ByteString*>(A->Stack(0));

    memcpy(result->element_addr(0), a->element_addr(0), a_length);
    memcpy(result->element_addr(a_length), b->element_addr(0), b_length);

    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  }

  if (A->Stack(1)->IsWideString() &&
      A->Stack(0)->IsWideString()) {
    WideString* a = static_cast<WideString*>(A->Stack(1));
    WideString* b = static_cast<WideString*>(A->Stack(0));
    intptr_t a_length = a->Size();
    intptr_t b_length = b->Size();
    WideString* result =
      H->AllocateWideString(a_length + b_length);  // SAFEPOINT
    a = static_cast<WideString*>(A->Stack(1));
    b = static_cast<WideString*>(A->Stack(0));

    memcpy(result->element_addr(0), a->element_addr(0),
           a_length * sizeof(uint32_t));
    memcpy(result->element_addr(a_length), b->element_addr(0),
           b_length * sizeof(uint32_t));

    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  }

  return kFailure;
}


DEFINE_PRIMITIVE(handlerMarker) {
  return kFailure;
}



DEFINE_PRIMITIVE(String_startsWith) {
  ASSERT(num_args == 1);

  ByteString* string = static_cast<ByteString*>(A->Stack(1));
  ByteString* substring = static_cast<ByteString*>(A->Stack(0));

  if (!(string->IsByteString()) ||
      !(substring->IsByteString())) {
    return kFailure;
  }

  intptr_t string_length = string->Size();
  intptr_t substring_length = substring->Size();
  if (substring_length > string_length) {
    RETURN_BOOL(false);
  }

  for (intptr_t i = 0; i < substring_length; i++) {
    if (string->element(i) != substring->element(i)) {
      RETURN_BOOL(false);
    }
  }
  RETURN_BOOL(true);
}


DEFINE_PRIMITIVE(String_endsWith) {
  ASSERT(num_args == 1);

  ByteString* string = static_cast<ByteString*>(A->Stack(1));
  ByteString* substring = static_cast<ByteString*>(A->Stack(0));
  if (!(string->IsByteString()) ||
      !(substring->IsByteString())) {
    return kFailure;
  }

  intptr_t string_length = string->Size();
  intptr_t substring_length = substring->Size();
  if (substring_length > string_length) {
    RETURN_BOOL(false);
  }

  intptr_t offset = string_length - substring_length;
  for (intptr_t i = 0; i < substring_length; i++) {
    if (string->element(offset + i) != substring->element(i)) {
      RETURN_BOOL(false);
    }
  }
  RETURN_BOOL(true);
}


DEFINE_PRIMITIVE(String_indexOf) {
  ASSERT(num_args == 2);

  ByteString* string = static_cast<ByteString*>(A->Stack(2));
  ByteString* substring = static_cast<ByteString*>(A->Stack(1));
  SmallInteger* start = static_cast<SmallInteger*>(A->Stack(0));
  if (!(string->IsByteString())) {
    UNREACHABLE();
    return kFailure;
  }
  if (!(substring->IsByteString())) {
    return kFailure;
  }
  if (!start->IsSmallInteger()) {
    return kFailure;
  }
  intptr_t string_length = string->Size();
  intptr_t substring_length = substring->Size();
  intptr_t start_index = start->value() - 1;
  if (start_index < 0) {
    return kFailure;
  }
  if (start_index > string_length) {
    return kFailure;
  }

  if (substring_length > string_length) {
    A->PopNAndPush(num_args + 1, SmallInteger::New(0));
    return kSuccess;
  }

  intptr_t limit = string_length - substring_length;
  for (intptr_t start = start_index; start <= limit; start++) {
    bool found = true;
    for (intptr_t i = 0; i < substring_length; i++) {
      if (string->element(start + i) != substring->element(i)) {
        found = false;
        break;
      }
    }
    if (found) {
      A->PopNAndPush(num_args + 1, SmallInteger::New(start + 1));
      return kSuccess;
    }
  }
  A->PopNAndPush(num_args + 1, SmallInteger::New(0));
  return kSuccess;
}


DEFINE_PRIMITIVE(String_lastIndexOf) {
  ASSERT(num_args == 2);

  ByteString* string = static_cast<ByteString*>(A->Stack(2));
  ByteString* substring = static_cast<ByteString*>(A->Stack(1));
  SmallInteger* start = static_cast<SmallInteger*>(A->Stack(0));
  if (!(string->IsByteString())) {
    UNREACHABLE();
    return kFailure;
  }
  if (!(substring->IsByteString())) {
    return kFailure;
  }
  if (!start->IsSmallInteger()) {
    return kFailure;
  }

  intptr_t string_length = string->Size();
  intptr_t substring_length = substring->Size();
  intptr_t start_index = start->value() - 1;
  if (start_index < 0) {
    return kFailure;
  }
  if (start_index > string_length) {
    return kFailure;
  }
  if (substring_length > string_length) {
    A->PopNAndPush(num_args + 1, SmallInteger::New(0));
    return kSuccess;
  }

  intptr_t limit = string_length - substring_length;
  if (limit > start_index) {
    limit = start_index;
  }
  for (intptr_t start = limit; start >= 0; start--) {
    bool found = true;
    for (intptr_t i = 0; i < substring_length; i++) {
      if (string->element(start + i) != substring->element(i)) {
        found = false;
      }
    }
    if (found) {
      A->PopNAndPush(num_args + 1, SmallInteger::New(start + 1));
      return kSuccess;
    }
  }
  A->PopNAndPush(num_args + 1, SmallInteger::New(0));
  return kSuccess;
}


DEFINE_PRIMITIVE(String_substring) {
  ASSERT(num_args == 2);

  ByteString* string = static_cast<ByteString*>(A->Stack(2));
  SmallInteger* start = static_cast<SmallInteger*>(A->Stack(1));
  SmallInteger* stop = static_cast<SmallInteger*>(A->Stack(0));

  if (!start->IsSmallInteger() ||
      !stop->IsSmallInteger()) {
    return kFailure;
  }

  if (0 < start->value() &&
      stop->value() <= string->Size()) {
    intptr_t subsize;
    if (start->value() <= stop->value()) {
      subsize = stop->value() - start->value() + 1;
    } else {
      subsize = 0;
    }
    ByteString* result = H->AllocateByteString(subsize);  // SAFEPOINT
    string = static_cast<ByteString*>(A->Stack(2));

    memcpy(result->element_addr(0),
           string->element_addr(start->value() - 1),
           subsize);

    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  }

  return kFailure;
}


DEFINE_PRIMITIVE(String_runeAt) {
  ASSERT(num_args == 1);

  Object* rcvr = A->Stack(1);
  SmallInteger* index = static_cast<SmallInteger*>(A->Stack(0));
  if (rcvr->IsByteString()) {
    ByteString* string = static_cast<ByteString*>(A->Stack(1));
    if (!index->IsSmallInteger()) {
      return kFailure;
    }
    if (index->value() <= 0) {
     return kFailure;
    }
     if (index->value() > string->Size()) {
      return kFailure;
    }

    intptr_t rune = string->element(index->value() - 1);
    A->PopNAndPush(num_args + 1, SmallInteger::New(rune));
    return kSuccess;
  } else if (rcvr->IsWideString()) {
    WideString* string = static_cast<WideString*>(A->Stack(1));
    if (!index->IsSmallInteger()) {
      return kFailure;
    }
    if (index->value() <= 0) {
     return kFailure;
    }
     if (index->value() > string->Size()) {
      return kFailure;
    }

    intptr_t rune = string->element(index->value() - 1);
    A->PopNAndPush(num_args + 1, SmallInteger::New(rune));
    return kSuccess;
  } else {
    UNREACHABLE();
    return kFailure;
  }
}


DEFINE_PRIMITIVE(String_class_fromRune) {
  ASSERT(num_args == 1);
  SmallInteger* rune = static_cast<SmallInteger*>(A->Stack(0));
  if (!rune->IsSmallInteger()) {
    return kFailure;
  }
  intptr_t crune = rune->value();
  if (crune < 0 || crune > 0x10FFFF) {
    return kFailure;
  }

  if (crune > 255) {
    WideString* result = H->AllocateWideString(1);  // SAFEPOINT
    result->set_element(0, crune);
    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  } else {
    ByteString* result = H->AllocateByteString(1);  // SAFEPOINT
    result->set_element(0, crune);
    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  }
}


DEFINE_PRIMITIVE(String_class_fromRunes) {
  ASSERT(num_args == 1);
  Array* runes = static_cast<Array*>(A->Stack(0));
  if (!runes->IsArray()) {
    return kFailure;
  }

  intptr_t length = runes->Size();
  runes = static_cast<Array*>(A->Stack(0));

  bool needs_wide = false;
  for (intptr_t i = 0; i < length; i++) {
    SmallInteger* rune = static_cast<SmallInteger*>(runes->element(i));
    if (!rune->IsSmallInteger()) {
      return kFailure;
    }
    intptr_t crune = rune->value();
    if (crune < 0 || crune > 0x10FFFF) {
      return kFailure;
    }
    if (crune > 255) {
      needs_wide = true;
    }
  }

  if (!needs_wide) {
    ByteString* result = H->AllocateByteString(length);  // SAFEPOINT
    runes = static_cast<Array*>(A->Stack(0));

    for (intptr_t i = 0; i < length; i++) {
      SmallInteger* rune = static_cast<SmallInteger*>(runes->element(i));
      if (!rune->IsSmallInteger()) {
        UNREACHABLE();
        return kFailure;
      }
      intptr_t crune = rune->value();
      if (crune < 0 || crune > 0x10FFFF) {
        UNREACHABLE();
        return kFailure;
      }
      result->set_element(i, crune);
    }

    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  } else {
    WideString* result = H->AllocateWideString(length);  // SAFEPOINT
    runes = static_cast<Array*>(A->Stack(0));

    for (intptr_t i = 0; i < length; i++) {
      SmallInteger* rune = static_cast<SmallInteger*>(runes->element(i));
      if (!rune->IsSmallInteger()) {
        UNREACHABLE();
        return kFailure;
      }
      intptr_t crune = rune->value();
      if (crune < 0 || crune > 0x10FFFF) {
        UNREACHABLE();
        return kFailure;
      }
      result->set_element(i, crune);
    }

    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  }
}


DEFINE_PRIMITIVE(Object_isCanonical) {
  ASSERT(num_args == 1);
  Object* object = A->Stack(0);
  if (!object->IsHeapObject()) {
    RETURN_BOOL(true);
  } else {
    RETURN_BOOL(object->is_canonical());
  }
}


DEFINE_PRIMITIVE(Object_markCanonical) {
  ASSERT(num_args == 1);
  Object* object = A->Stack(0);
  if (!object->IsHeapObject()) {
    // Nop.
  } else {
    object->set_is_canonical(true);
  }
  A->PopNAndPush(num_args + 1, object);
  return kSuccess;
}


DEFINE_PRIMITIVE(writeBytesToFile) {
  ASSERT(num_args == 2);
  ByteArray* bytes = static_cast<ByteArray*>(A->Stack(1));
  ByteString* filename = static_cast<ByteString*>(A->Stack(0));
  if (!bytes->IsByteArray() || !filename->IsByteString()) {
    UNIMPLEMENTED();
  }

  char* cfilename = reinterpret_cast<char*>(malloc(filename->Size() + 1));
  memcpy(cfilename, filename->element_addr(0), filename->Size());
  cfilename[filename->Size()] = 0;
  FILE* f = fopen(cfilename, "w");
  if (f == NULL) {
    OS::PrintErr("Cannot open %s\n", cfilename);
    UNREACHABLE();
  }

  intptr_t length = bytes->Size();
  int start = 0;
  while (start != length) {
    int written = fwrite(bytes->element_addr(start), 1, length - start, f);
    ASSERT(written != -1);
    start += written;
  }
  fflush(f);
  fclose(f);

  free(cfilename);

  A->Drop(num_args);
  return kSuccess;
}


DEFINE_PRIMITIVE(mainArguments) {
  UNREACHABLE();
  return kFailure;
}


DEFINE_PRIMITIVE(readFileAsBytes) {
  ASSERT(num_args == 1);
  ByteString* filename = static_cast<ByteString*>(A->Stack(0));
  if (!filename->IsByteString()) {
    UNIMPLEMENTED();
  }

  char* cfilename = reinterpret_cast<char*>(malloc(filename->Size() + 1));
  memcpy(cfilename, filename->element_addr(0), filename->Size());
  cfilename[filename->Size()] = 0;
  FILE* f = fopen(cfilename, "r");
  if (f == NULL) {
    FATAL1("Failed to stat '%s'\n", cfilename);
  }
  struct stat st;
  if (fstat(fileno(f), &st) != 0) {
    FATAL1("Failed to stat '%s'\n", cfilename);
  }
  intptr_t length = st.st_size;

  ByteArray* result = H->AllocateByteArray(length);  // SAFEPOINT
  int start = 0;
  while (start != length) {
    int read = fread(result->element_addr(start), 1, length - start, f);
    ASSERT(read != -1);
    start += read;
  }

  fclose(f);
  free(cfilename);

  A->PopNAndPush(num_args + 1, result);
  return kSuccess;
}


DEFINE_PRIMITIVE(Double_class_parse) {
  ASSERT(num_args == 1);
  ByteString* string = static_cast<ByteString*>(A->Stack(0));
  if (!string->IsByteString()) {
    return kFailure;
  }
  double cresult;
  const char* cstr = reinterpret_cast<const char*>(string->element_addr(0));
  intptr_t length = string->Size();
  if (CStringToDouble(cstr, length, &cresult)) {
    Float64* result = H->AllocateFloat64();  // SAFEPOINT
    result->set_value(cresult);
    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  }
  return kFailure;
}


DEFINE_PRIMITIVE(currentActivation) {
  ASSERT(num_args == 0);
  A->PopNAndPush(num_args + 1, A);
  return kSuccess;
}


DEFINE_PRIMITIVE(adoptInstance) {
  ASSERT(num_args == 2);
  Behavior* new_cls = static_cast<Behavior*>(A->Stack(1));
  Object* instance = A->Stack(0);
  Behavior* old_cls = instance->Klass(H);

  ASSERT(old_cls->cid() >= kFirstRegularObjectCid);
  ASSERT(old_cls->format() == new_cls->format());

  SmallInteger* id = new_cls->id();
  if (id == nil) {
    id = SmallInteger::New(H->AllocateClassId());  // SAFEPOINT
    new_cls = static_cast<Behavior*>(A->Stack(1));
    instance = A->Stack(0);
    new_cls->set_id(id);
    H->RegisterClass(id->value(), new_cls);
  }
  ASSERT(id->IsSmallInteger());
  instance->set_cid(id->value());

  A->Drop(num_args);
  return kSuccess;
}


DEFINE_PRIMITIVE(createPort) {
  ASSERT(num_args == 0);
  MessageQueue* queue = H->isolate()->queue();
  Port new_port = PortMap::CreatePort(queue);

  if (SmallInteger::IsSmiValue(new_port)) {
    A->PopNAndPush(num_args + 1, SmallInteger::New(new_port));
    return kSuccess;
  } else {
    MediumInteger* result = H->AllocateMediumInteger();  // SAFEPOINT
    result->set_value(new_port);
    A->PopNAndPush(num_args + 1, result);
    return kSuccess;
  }
  return kSuccess;
}


DEFINE_PRIMITIVE(receive) {
  ASSERT(num_args == 1);

  Object* nstimeout = A->Stack(0);
  intptr_t timeout;
  if (nstimeout->IsSmallInteger()) {
    timeout = static_cast<SmallInteger*>(nstimeout)->value();
  } else if (nstimeout->IsMediumInteger()) {
    timeout = static_cast<MediumInteger*>(nstimeout)->value();
  } else {
    return kFailure;
  }

  MessageQueue* queue = H->isolate()->queue();

  IsolateMessage* message = queue->Receive(timeout);
  if (message == NULL) {
    // Interrupt or timeout.
    A->PopNAndPush(num_args + 1, nil);
    return kSuccess;
  }

  ByteArray* result = H->AllocateByteArray(message->length());  // SAFEPOINT
  memcpy(result->element_addr(0), message->data(), message->length());

  delete message;

  A->PopNAndPush(num_args + 1, result);
  return kSuccess;
}


DEFINE_PRIMITIVE(spawn) {
  ASSERT(num_args == 1);
  ByteArray* message = static_cast<ByteArray*>(A->Stack(0));
  if (message->IsByteArray()) {
    intptr_t length = message->Size();
    uint8_t* data = reinterpret_cast<uint8_t*>(malloc(length));
    memcpy(data, message->element_addr(0), length);

    H->isolate()->Spawn(data, length);

    A->Drop(num_args);
    return kSuccess;
  }

  return kFailure;
}


DEFINE_PRIMITIVE(send) {
  ASSERT(num_args == 2);
  Object* nsport = A->Stack(1);
  Port cport;
  if (nsport->IsSmallInteger()) {
    cport = static_cast<SmallInteger*>(nsport)->value();
  } else if (nsport->IsMediumInteger()) {
    cport = static_cast<MediumInteger*>(nsport)->value();
  } else {
    return kFailure;
  }

  ByteArray* nsdata = static_cast<ByteArray*>(A->Stack(0));
  if (!nsdata->IsByteArray()) {
    return kFailure;
  }

  intptr_t length = nsdata->Size();
  uint8_t* data = reinterpret_cast<uint8_t*>(malloc(length));
  memcpy(data, nsdata->element_addr(0), length);
  IsolateMessage* message = new IsolateMessage(cport, data, length);
  bool cresult = PortMap::PostMessage(message);

  RETURN_BOOL(cresult);
}


DEFINE_PRIMITIVE(finish) {
  ASSERT(num_args == 0);
  H->isolate()->Finish();
  UNREACHABLE();
  return kFailure;
}


DEFINE_PRIMITIVE(quickReturnSelf) {
  ASSERT(num_args == 0);
  return kSuccess;
}


PrimitiveFunction** Primitives::primitive_table_ = NULL;


void Primitives::InitOnce() {
  primitive_table_ = new PrimitiveFunction*[kNumPrimitives];
  for (intptr_t i = 0; i < kNumPrimitives; i++) {
    primitive_table_[i] = primitiveUnimplemented;
  }
#define ADD_PRIMITIVE(number, name)                                            \
  primitive_table_[number] = primitive##name;
PRIMITIVE_LIST(ADD_PRIMITIVE);
#undef ADD_PRIMITIVE
}


bool Primitives::Invoke(intptr_t prim, intptr_t num_args,
                        Heap* heap, Interpreter* I) {
  ASSERT(prim > 0);
  ASSERT(prim < kNumPrimitives);
  PrimitiveFunction* func = primitive_table_[prim];
  return func(num_args, heap, I);
}

}  // namespace psoup
