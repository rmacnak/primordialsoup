// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_MATH_H_
#define VM_MATH_H_

#include <limits>
#include <type_traits>

#include "vm/assert.h"
#include "vm/globals.h"

namespace psoup {

class Math {
 public:
  template <typename T>
  static inline bool AddHasOverflow(T left, T right, T* result) {
    if (TEST_SLOW_PATH) return true;

#if defined(__has_builtin)
#if __has_builtin(__builtin_add_overflow)
#define HAS_BUILTIN_ADD_OVERFLOW
#endif
#elif defined(__GNUC__)
#if __GNUC__ >= 5
#define HAS_BUILTIN_ADD_OVERFLOW
#endif
#endif

#if defined(HAS_BUILTIN_ADD_OVERFLOW)
    return __builtin_add_overflow(left, right, result);
#else
    if (((right > 0) && (left > (std::numeric_limits<T>::max() - right))) ||
        ((right < 0) && (left < (std::numeric_limits<T>::min() - right)))) {
      return true;
    }
    *result = left + right;
    return false;
#endif
  }

  template <typename T>
  static inline bool SubtractHasOverflow(T left, T right, T* result) {
    if (TEST_SLOW_PATH) return true;

#if defined(__has_builtin)
#if __has_builtin(__builtin_sub_overflow)
#define HAS_BUILTIN_SUB_OVERFLOW
#endif
#elif defined(__GNUC__)
#if __GNUC__ >= 5
#define HAS_BUILTIN_SUB_OVERFLOW
#endif
#endif

#if defined(HAS_BUILTIN_SUB_OVERFLOW)
    return __builtin_sub_overflow(left, right, result);
#else
    if (((right > 0) && (left < (std::numeric_limits<T>::min() + right))) ||
        ((right < 0) && (left > (std::numeric_limits<T>::max() + right)))) {
      return true;
    }
    *result = left - right;
    return false;
#endif
  }

  template <typename T>
  static inline bool MultiplyHasOverflow(T left, T right, T* result) {
    if (TEST_SLOW_PATH) return true;

#if defined(__has_builtin)
#if __has_builtin(__builtin_mul_overflow)
#define HAS_BUILTIN_MUL_OVERFLOW
#endif
#elif defined(__GNUC__)
#if __GNUC__ >= 5
#define HAS_BUILTIN_MUL_OVERFLOW
#endif
#endif

#if defined(HAS_BUILTIN_MUL_OVERFLOW)
    return __builtin_mul_overflow(left, right, result);
#else
    constexpr intptr_t kMaxBits = (sizeof(T) * kBitsPerByte) - 2;
    if ((Utils::HighestBit(left) + Utils::HighestBit(right)) < kMaxBits) {
      *result = left * right;
      return false;
    }
    return true;
#endif
  }

  // See DAAN LEIJEN. "Division and Modulus for Computer Scientists". 2001.

  template <typename T>
  static constexpr inline T TruncDiv(T dividend, T divisor) {
    ASSERT(divisor != 0);
    ASSERT((divisor != -1) || (dividend != std::numeric_limits<T>::min()));
    return dividend / divisor;  // Undefined before C99.
  }

  template <typename T>
  static constexpr inline T TruncMod(T dividend, T divisor) {
    ASSERT(divisor != 0);
    ASSERT((divisor != -1) || (dividend != std::numeric_limits<T>::min()));
    return dividend % divisor;  // Undefined before C99.
  }

  template <typename T>
  static constexpr inline T FloorDiv(T dividend, T divisor) {
    ASSERT(divisor != 0);
    ASSERT((divisor != -1) || (dividend != std::numeric_limits<T>::min()));
    T truncating_quoitent = dividend / divisor;  // Undefined before C99.
    T truncating_remainder = dividend % divisor;
    if ((truncating_remainder != 0) &&
        ((dividend < 0) != (divisor < 0))) {
      return truncating_quoitent - 1;
    } else {
      return truncating_quoitent;
    }
  }

  template <typename T>
  static constexpr inline T FloorMod(T dividend, T divisor) {
    ASSERT(divisor != 0);
    ASSERT((divisor != -1) || (dividend != std::numeric_limits<T>::min()));
    T truncating_remainder = dividend % divisor;
    if ((truncating_remainder != 0) &&
        ((truncating_remainder < 0) != (divisor < 0))) {
      return truncating_remainder + divisor;
    } else {
      return truncating_remainder;
    }
  }

  template <typename T>
  static constexpr inline T ShiftLeft(T left, T right) {
    typedef typename std::make_unsigned<T>::type Unsigned;
    return static_cast<T>(static_cast<Unsigned>(left) << right);
  }

  NO_SANITIZE_UNDEFINED("float-divide-by-zero")
  static constexpr inline double DivideF64(double dividend, double divisor) {
    return dividend / divisor;
  }
};

}  // namespace psoup

#endif  // VM_MATH_H_
