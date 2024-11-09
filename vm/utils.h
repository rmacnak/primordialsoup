// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_UTILS_H_
#define VM_UTILS_H_

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
#if defined(__GNUC__)
    ASSERT(sizeof(long long) == sizeof(int64_t));  // NOLINT
    if (v == 0) return 0;
    return 63 - __builtin_clzll(x);
#else
    uint64_t t = 0;
    int r = 0;
    if ((t = x >> 32) != 0) { x = t; r += 32; }
    if ((t = x >> 16) != 0) { x = t; r += 16; }
    if ((t = x >> 8) != 0) { x = t; r += 8; }
    if ((t = x >> 4) != 0) { x = t; r += 4; }
    if ((t = x >> 2) != 0) { x = t; r += 2; }
    if (x > 1) r += 1;
    return r;
#endif
  }

  static constexpr inline intptr_t BitLength(int64_t value) {
    // Flip bits if negative (-1 becomes 0).
    value ^= value >> (kBitsPerByte * sizeof(value) - 1);
    return (value == 0) ? 0 : (Utils::HighestBit(value) + 1);
  }

  template <typename T>
  static constexpr inline bool IsPowerOfTwo(T x) {
    return ((x & (x - 1)) == 0) && (x != 0);
  }

  template <typename T>
  static constexpr inline intptr_t ShiftForPowerOfTwo(T x) {
    ASSERT(IsPowerOfTwo(x));
    intptr_t num_shifts = 0;
    while (x > 1) {
      num_shifts++;
      x = x >> 1;
    }
    return num_shifts;
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
