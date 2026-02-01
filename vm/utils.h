// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_UTILS_H_
#define VM_UTILS_H_

#include <bit>
#include <type_traits>

#include "vm/allocation.h"
#include "vm/assert.h"
#include "vm/globals.h"

namespace psoup {

class Utils : public AllStatic {
 public:
  template <typename T>
  static constexpr inline T RoundDown(T x, intptr_t n) {
    ASSERT(IsPowerOfTwo(n));
    return (x & -n);
  }

  template <typename T>
  static constexpr inline T RoundUp(T x, intptr_t n) {
    return RoundDown(x + n - 1, n);
  }

  static constexpr inline intptr_t HighestBit(int64_t v) {
    uint64_t x = static_cast<uint64_t>((v > 0) ? v : -v);
    if (v == 0) return 0;
    return 63 - std::countl_zero(x);
  }

  static constexpr inline intptr_t BitLength(int64_t value) {
    // Flip bits if negative (-1 becomes 0).
    value ^= value >> (kBitsPerByte * sizeof(value) - 1);
    return (value == 0) ? 0 : (Utils::HighestBit(value) + 1);
  }

  template <typename T>
  static constexpr inline bool IsPowerOfTwo(T x) {
    using U = std::make_unsigned<T>::type;
    return std::has_single_bit(static_cast<U>(x));
  }

  template <typename T>
  static constexpr inline intptr_t ShiftForPowerOfTwo(T x) {
    ASSERT(IsPowerOfTwo(x));
    return std::bit_width<T>(x);
  }

  template <typename T>
  static constexpr inline bool IsAligned(T x,
                                         uintptr_t alignment,
                                         uintptr_t offset = 0) {
    ASSERT(IsPowerOfTwo(alignment));
    ASSERT(offset < alignment);
    return (x & (alignment - 1)) == offset;
  }

  // Check whether an N-bit two's-complement representation can hold value.
  template <typename T>
  static constexpr inline bool IsInt(intptr_t N, T value) {
    ASSERT(N >= 1);
    constexpr intptr_t value_size_in_bits = kBitsPerByte * sizeof(T);
    if constexpr (std::is_signed<T>::value) {
      if (N >= value_size_in_bits) return true;  // Trivially fits.
      const T limit = static_cast<T>(1) << (N - 1);
      return (-limit <= value) && (value < limit);
    } else {
      if (N > value_size_in_bits) return true;  // Trivially fits.
      const T limit = static_cast<T>(1) << (N - 1);
      return value < limit;
    }
  }

  template <typename T>
  static constexpr inline bool IsUint(intptr_t N, T value) {
    ASSERT(N >= 1);
    constexpr intptr_t value_size_in_bits = kBitsPerByte * sizeof(T);
    if constexpr (std::is_signed<T>::value) {
      if (value < 0) return false;  // Not an unsigned value.
      if (N >= value_size_in_bits - 1) {
        return true;  // N can fit the magnitude bits.
      }
    } else {
      if (N >= value_size_in_bits) return true;  // Trivially fits.
    }
    const T limit = (static_cast<T>(1) << N) - 1;
    return value <= limit;
  }
};

}  // namespace psoup

#endif  // VM_UTILS_H_
