// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

// Henry S. Warren, Jr. "Hacker's Delight." (2nd Edition) Addison-Wesley. 2012.

#include "vm/assert.h"
#include "vm/heap.h"
#include "vm/object.h"

namespace psoup {

const intptr_t kMintDigits = sizeof(uint64_t) / sizeof(digit_t);


LargeInteger* LargeInteger::Expand(Object* integer, Heap* H) {
  if (integer->IsLargeInteger()) {
    return static_cast<LargeInteger*>(integer);
  }

  int64_t value;
  if (integer->IsSmallInteger()) {
    value = static_cast<SmallInteger*>(integer)->value();
  } else if (integer->IsMediumInteger()) {
    value = static_cast<MediumInteger*>(integer)->value();
  } else {
    UNREACHABLE();
    value = 0;
  }

  LargeInteger* result = H->AllocateLargeInteger(kMintDigits);
  result->set_negative(value < 0);
  uint64_t absolute_value;
  if (value < 0) {
    absolute_value = -static_cast<uint64_t>(value);
  } else {
    absolute_value = static_cast<uint64_t>(value);
  }

  intptr_t i = 0;
  while (absolute_value != 0) {
    result->set_digit(i, absolute_value & kDigitMask);
    absolute_value = absolute_value >> kDigitShift;
    i++;
  }
  result->set_size(i);
  while (i < kMintDigits) {
    result->set_digit(i, 0);
    i++;
  }

  return result;
}


Object* LargeInteger::Reduce(LargeInteger* large, Heap* H) {
  if (large->size() > kMintDigits) {
    return large;
  }

  uint64_t absolute_value = 0;
  for (intptr_t i = large->size() - 1; i >= 0; i--) {
    absolute_value <<= kDigitShift;
    absolute_value |= large->digit(i);
  }

  if (large->negative()) {
    uint64_t limit = static_cast<uint64_t>(MediumInteger::kMaxValue) + 1;
    if (absolute_value > limit) {
      return large;
    }

    int64_t value = static_cast<int64_t>(-absolute_value);
    if (SmallInteger::IsSmiValue(value)) {
      return SmallInteger::New(value);
    }

    MediumInteger* result = H->AllocateMediumInteger();
    result->set_value(value);
    return result;
  } else {
    uint64_t limit = static_cast<uint64_t>(MediumInteger::kMaxValue);
    if (absolute_value > limit) {
      return large;
    }

    int64_t value = static_cast<int64_t>(absolute_value);
    if (SmallInteger::IsSmiValue(value)) {
      return SmallInteger::New(value);
    }

    MediumInteger* result = H->AllocateMediumInteger();
    result->set_value(static_cast<int64_t>(value));
    return result;
  }
}


static void Clamp(LargeInteger* result) {
  for (intptr_t used = result->capacity() - 1; used >= 0; used--) {
    if (result->digit(used) != 0) {
      result->set_size(used + 1);
      return;
    }
  }
  result->set_size(0);
  result->set_negative(false);
}


static void Verify(LargeInteger* integer) {
  ASSERT(integer->capacity() >= 0);
  ASSERT(integer->size() >= 0);
  ASSERT(integer->capacity() >= integer->size());

  if (integer->size() == 0) {
    ASSERT(!integer->negative());
  } else {
    ASSERT(integer->digit(integer->size() - 1) != 0);
  }

  for (intptr_t i = integer->size(); i < integer->capacity(); i++) {
    ASSERT(integer->digit(i) == 0);
  }
}


static intptr_t AbsCompare(LargeInteger* left, LargeInteger* right) {
  intptr_t d = right->size() - left->size();
  if (d != 0) {
    return d;
  }

  for (intptr_t i = left->size() - 1; i >= 0; i--) {
    // Digit difference might not fit in intptr_t.
    if (right->digit(i) < left->digit(i)) {
      return -1;
    }
    if (right->digit(i) > left->digit(i)) {
      return 1;
    }
  }
  return 0;
}


static bool AbsGreater(LargeInteger* left, LargeInteger* right) {
  return AbsCompare(left, right) < 0;
}


intptr_t LargeInteger::Compare(LargeInteger* left, LargeInteger* right) {
  if (left->negative()) {
    if (right->negative()) {
      return AbsCompare(right, left);
    }
    return 1;
  } else {
    if (right->negative()) {
      return -1;
    } else {
      return AbsCompare(left, right);
    }
  }
}


static LargeInteger* AddAbsolutesWithSign(LargeInteger* left,
                                          LargeInteger* right,
                                          bool negative,
                                          Heap* H) {
  Verify(left);
  Verify(right);

  LargeInteger* shorter;
  LargeInteger* longer;
  if (left->size() <= right->size()) {
    shorter = left;
    longer = right;
  } else {
    shorter = right;
    longer = left;
  }

  HandleScope h1(H, reinterpret_cast<Object**>(&shorter));
  HandleScope h2(H, reinterpret_cast<Object**>(&longer));
  LargeInteger* result = H->AllocateLargeInteger(longer->size() + 1);

  ddigit_t carry = 0;
  for (intptr_t i = 0; i < shorter->size(); i++) {
    carry += static_cast<ddigit_t>(shorter->digit(i)) +
        static_cast<ddigit_t>(longer->digit(i));
    result->set_digit(i, carry & kDigitMask);
    carry >>= kDigitShift;
    ASSERT((carry == 0) || (carry == 1));
  }
  for (intptr_t i = shorter->size(); i < longer->size(); i++) {
    carry += static_cast<ddigit_t>(longer->digit(i));
    result->set_digit(i, carry & kDigitMask);
    carry >>= kDigitShift;
    ASSERT((carry == 0) || (carry == 1));
  }
  result->set_digit(longer->size(), carry);

  result->set_negative(negative);
  Clamp(result);
  Verify(result);
  return result;
}


static LargeInteger* SubtractAbsolutesWithSign(LargeInteger* left,
                                               LargeInteger* right,
                                               bool negative,
                                               Heap* H) {
  Verify(left);
  Verify(right);
  ASSERT(left->size() >= right->size());
  ASSERT(!AbsGreater(right, left));

  HandleScope h1(H, reinterpret_cast<Object**>(&left));
  HandleScope h2(H, reinterpret_cast<Object**>(&right));
  LargeInteger* result = H->AllocateLargeInteger(left->size());

  sddigit_t borrow = 0;
  for (intptr_t i = 0; i < right->size(); i++) {
    borrow += static_cast<ddigit_t>(left->digit(i)) -
        static_cast<ddigit_t>(right->digit(i));
    result->set_digit(i, borrow & kDigitMask);
    borrow >>= kDigitShift;
    ASSERT((borrow == 0) || (borrow == -1));
  }
  for (intptr_t i = right->size(); i < left->size(); i++) {
    borrow += static_cast<ddigit_t>(left->digit(i));
    result->set_digit(i, borrow & kDigitMask);
    borrow >>= kDigitShift;
    ASSERT((borrow == 0) || (borrow == -1));
  }
  ASSERT(borrow == 0);

  result->set_negative(negative);
  Clamp(result);

  Verify(result);
  return result;
}


static void AbsAddOneInPlace(LargeInteger* result) {
  if (result->size() == 0) {
    result->set_size(1);
    result->set_digit(0, 1);
    return;
  }

  sddigit_t carry = 1;
  for (intptr_t i = 0; i < result->size(); i++) {
    carry += static_cast<sddigit_t>(result->digit(i));
    result->set_digit(i, carry & kDigitMask);
    carry >>= kDigitShift;
    ASSERT(carry == 0 || carry == 1);
  }
  ASSERT(carry == 0);
  Verify(result);
}


LargeInteger* MultiplyAbsolutesWithSign(LargeInteger* left,
                                        LargeInteger* right,
                                        bool negative,
                                        Heap* H) {
  Verify(left);
  Verify(right);

  HandleScope h1(H, reinterpret_cast<Object**>(&left));
  HandleScope h2(H, reinterpret_cast<Object**>(&right));
  LargeInteger* result = H->AllocateLargeInteger(left->size() + right->size());

  for (intptr_t i = 0; i < left->size(); i++) {
    result->set_digit(i, 0);
  }

  for (intptr_t i = 0; i < right->size(); i++) {
    ddigit_t carry = 0;
    ddigit_t right_digit = right->digit(i);
    for (intptr_t j = 0; j < left->size(); j++) {
      ddigit_t left_digit = left->digit(j);
      carry += left_digit * right_digit +
          static_cast<ddigit_t>(result->digit(i + j));
      result->set_digit(i + j, carry & kDigitMask);
      carry >>= kDigitShift;
    }
    ASSERT((carry >> kDigitShift) == 0);
    result->set_digit(i + left->size(), carry);
  }

  result->set_negative(negative);
  Clamp(result);

  Verify(result);
  return result;
}


LargeInteger* LargeInteger::Add(LargeInteger* left,
                                LargeInteger* right,
                                Heap* H) {
  if (left->negative() == right->negative()) {
    // -l + -r = -(l + r)
    //  l +  r =   l + r
    return AddAbsolutesWithSign(left, right, left->negative(), H);
  }

  if (AbsGreater(left, right)) {
    //  l + -r =   l - r
    // -l +  r = -(l - r)
    return SubtractAbsolutesWithSign(left, right, left->negative(), H);
  } else {
    //  l + -r = -(r - l)
    // -l +  r =  (r - l)
    return SubtractAbsolutesWithSign(right, left, right->negative(), H);
  }
}


LargeInteger* LargeInteger::Subtract(LargeInteger* left,
                                     LargeInteger* right,
                                     Heap* H) {
  if (left->negative() != right->negative()) {
    // -l -  r = -(l + r)
    //  l - -r =   l + r
    return AddAbsolutesWithSign(left, right, left->negative(), H);
  }

  if (AbsGreater(left, right)) {
    // -l - -r = -(l - r)
    //  l -  r =   l - r
    return SubtractAbsolutesWithSign(left, right, left->negative(), H);
  } else {
    // -l - -r =   r - l
    //  l -  r = -(r - l)
    return SubtractAbsolutesWithSign(right, left, !left->negative(), H);
  }
}


LargeInteger* LargeInteger::Multiply(LargeInteger* left,
                                     LargeInteger* right,
                                     Heap* h) {
  //  l *  r =   l * r
  // -l * -r =   l * r
  // -l *  r = -(l * r)
  //  l * -r = -(l * r)
  bool negative = left->negative() != right->negative();
  return MultiplyAbsolutesWithSign(left, right, negative, h);
}


LargeInteger* LargeInteger::And(LargeInteger* left,
                                LargeInteger* right,
                                Heap* H) {
  Verify(left);
  Verify(right);

  LargeInteger* shorter;
  LargeInteger* longer;
  if (left->size() > right->size()) {
    longer = left;
    shorter = right;
  } else {
    longer = right;
    shorter = left;
  }

  intptr_t result_size = longer->size();
  HandleScope h1(H, reinterpret_cast<Object**>(&shorter));
  HandleScope h2(H, reinterpret_cast<Object**>(&longer));
  LargeInteger* result = H->AllocateLargeInteger(result_size);

  if (!shorter->negative() && !longer->negative()) {
    // s & l
    result->set_negative(false);
    for (intptr_t i = 0; i < shorter->size(); i++) {
      result->set_digit(i, shorter->digit(i) & longer->digit(i));
    }
    for (intptr_t i = shorter->size(); i < longer->size(); i++) {
      result->set_digit(i, 0);
    }
  } else if (!shorter->negative() && longer->negative()) {
    // s & -l
    // s & ~(l - 1)
    result->set_negative(false);
    sddigit_t l1_borrow = -1;
    for (intptr_t i = 0; i < shorter->size(); i++) {
      l1_borrow += static_cast<sddigit_t>(longer->digit(i));
      digit_t l1 = l1_borrow & kDigitMask;
      l1_borrow >>= kDigitShift;
      result->set_digit(i, shorter->digit(i) & ~l1);
    }
    for (intptr_t i = shorter->size(); i < longer->size(); i++) {
      result->set_digit(i, 0);
    }
  } else if (shorter->negative() && !longer->negative()) {
    //       -s & l
    // ~(s - 1) & l
    result->set_negative(false);
    sddigit_t s1_borrow = -1;
    for (intptr_t i = 0; i < shorter->size(); i++) {
      s1_borrow += static_cast<sddigit_t>(shorter->digit(i));
      digit_t s1 = s1_borrow & kDigitMask;
      s1_borrow >>= kDigitShift;
      result->set_digit(i, ~s1 & longer->digit(i));
    }
    for (intptr_t i = shorter->size(); i < longer->size(); i++) {
      result->set_digit(i, longer->digit(i));
    }
  } else {
    //         -s & -l
    //   ~(s - 1) & ~(l - 1)
    //  ~((s - 1) | (l - 1))
    // -(((s - 1) | (l - 1)) + 1)
    result->set_negative(true);
    sddigit_t s1_borrow = -1;
    sddigit_t l1_borrow = -1;
    ddigit_t r1_carry = 1;
    for (intptr_t i = 0; i < shorter->size(); i++) {
      s1_borrow += static_cast<sddigit_t>(shorter->digit(i));
      digit_t s1 = s1_borrow & kDigitMask;
      s1_borrow >>= kDigitShift;

      l1_borrow += static_cast<sddigit_t>(longer->digit(i));
      digit_t l1 = l1_borrow & kDigitMask;
      l1_borrow >>= kDigitShift;
      ASSERT(l1_borrow == 0 || l1_borrow == -1);

      digit_t r = s1 | l1;

      r1_carry += static_cast<ddigit_t>(r);
      result->set_digit(i, r1_carry & kDigitMask);
      r1_carry >>= kDigitShift;
    }
    ASSERT(s1_borrow == 0);
    for (intptr_t i = shorter->size(); i < longer->size(); i++) {
      digit_t s1 = 0;

      l1_borrow += static_cast<sddigit_t>(longer->digit(i));
      digit_t l1 = l1_borrow & kDigitMask;
      l1_borrow >>= kDigitShift;

      digit_t r = s1 | l1;

      r1_carry += static_cast<ddigit_t>(r);
      result->set_digit(i, r1_carry & kDigitMask);
      r1_carry >>= kDigitShift;
    }
    ASSERT(r1_carry == 0);
  }

  Clamp(result);
  Verify(result);
  return result;
}


LargeInteger* LargeInteger::Or(LargeInteger* left,
                               LargeInteger* right,
                               Heap* H) {
  Verify(left);
  Verify(right);

  LargeInteger* shorter;
  LargeInteger* longer;
  if (left->size() > right->size()) {
    longer = left;
    shorter = right;
  } else {
    longer = right;
    shorter = left;
  }

  intptr_t result_size = longer->size();
  HandleScope h1(H, reinterpret_cast<Object**>(&shorter));
  HandleScope h2(H, reinterpret_cast<Object**>(&longer));
  LargeInteger* result = H->AllocateLargeInteger(result_size);

  if (!shorter->negative() && !longer->negative()) {
    // s | l
    result->set_negative(false);
    for (intptr_t i = 0; i < shorter->size(); i++) {
      result->set_digit(i, shorter->digit(i) | longer->digit(i));
    }
    for (intptr_t i = shorter->size(); i < longer->size(); i++) {
      result->set_digit(i, longer->digit(i));
    }
    result->set_size(result_size);
  } else if (!shorter->negative() && longer->negative()) {
    //     s | -l
    //     s | ~(l - 1)
    //  ~(~s & (l - 1))
    // -((~s & (l - 1)) + 1)
    result->set_negative(true);
    sddigit_t l1_borrow = -1;
    ddigit_t r1_carry = 1;
    for (intptr_t i = 0; i < shorter->size(); i++) {
      l1_borrow += static_cast<sddigit_t>(longer->digit(i));
      digit_t l1 = l1_borrow & kDigitMask;
      l1_borrow >>= kDigitShift;

      digit_t andnot = ~shorter->digit(i) & l1;

      r1_carry += static_cast<ddigit_t>(andnot);
      result->set_digit(i, r1_carry & kDigitMask);
      r1_carry >>= kDigitShift;
    }
    for (intptr_t i = shorter->size(); i < longer->size(); i++) {
      l1_borrow += static_cast<sddigit_t>(longer->digit(i));
      digit_t l1 = l1_borrow & kDigitMask;
      l1_borrow >>= kDigitShift;

      digit_t andnot = ~0 & l1;

      r1_carry += static_cast<ddigit_t>(andnot);
      result->set_digit(i, r1_carry & kDigitMask);
      r1_carry >>= kDigitShift;
    }
    ASSERT(r1_carry == 0);
    Clamp(result);
  } else if (shorter->negative() && !longer->negative()) {
    //     l | -s
    //     l | ~(s - 1)
    //  ~(~l & (s - 1))
    // -((~l & (s - 1)) + 1)
    result->set_negative(true);
    sddigit_t s1_borrow = -1;
    ddigit_t r1_carry = 1;
    for (intptr_t i = 0; i < shorter->size(); i++) {
      s1_borrow += static_cast<sddigit_t>(shorter->digit(i));
      digit_t s1 = s1_borrow & kDigitMask;
      s1_borrow >>= kDigitShift;

      digit_t andnot = ~longer->digit(i) & s1;

      r1_carry += static_cast<ddigit_t>(andnot);
      result->set_digit(i, r1_carry & kDigitMask);
      r1_carry >>= kDigitShift;
    }
    for (intptr_t i = shorter->size(); i < longer->size(); i++) {
      s1_borrow += static_cast<sddigit_t>(0);
      digit_t s1 = s1_borrow & kDigitMask;
      s1_borrow >>= kDigitShift;

      digit_t andnot = ~longer->digit(i) & s1;

      r1_carry += static_cast<ddigit_t>(andnot);
      result->set_digit(i, r1_carry & kDigitMask);
      r1_carry >>= kDigitShift;
    }
    ASSERT(r1_carry == 0);
    Clamp(result);
  } else if (shorter->negative() && longer->negative()) {
    //         -s | -l
    //   ~(s - 1) | ~(l - 1)
    //  ~((s - 1) & (l - 1))
    // -(((s - 1) & (l - 1)) + 1)
    result->set_negative(true);

    sddigit_t s1_borrow = -1;
    sddigit_t l1_borrow = -1;
    ddigit_t r1_carry = 1;
    for (intptr_t i = 0; i < shorter->size(); i++) {
      s1_borrow += static_cast<sddigit_t>(shorter->digit(i));
      ddigit_t s1 = s1_borrow & kDigitMask;
      s1_borrow >>= kDigitShift;

      l1_borrow += static_cast<sddigit_t>(longer->digit(i));
      ddigit_t l1 = l1_borrow & kDigitMask;
      l1_borrow >>= kDigitShift;

      ddigit_t r = s1 & l1;
      r1_carry += static_cast<ddigit_t>(r);
      result->set_digit(i, r1_carry & kDigitMask);
      r1_carry >>= kDigitShift;
    }
    for (intptr_t i = shorter->size(); i < longer->size(); i++) {
      s1_borrow += static_cast<sddigit_t>(0);
      ddigit_t s1 = s1_borrow & kDigitMask;
      s1_borrow >>= kDigitShift;

      l1_borrow += static_cast<sddigit_t>(longer->digit(i));
      ddigit_t l1 = l1_borrow & kDigitMask;
      l1_borrow >>= kDigitShift;

      ddigit_t r = s1 & l1;
      r1_carry += static_cast<ddigit_t>(r);
      result->set_digit(i, r1_carry & kDigitMask);
      r1_carry >>= kDigitShift;
    }
    ASSERT(r1_carry == 0);
    Clamp(result);
  }

  Verify(result);
  return result;
}


LargeInteger* LargeInteger::Xor(LargeInteger* left,
                                LargeInteger* right,
                                Heap* H) {
  Verify(left);
  Verify(right);

  LargeInteger* shorter;
  LargeInteger* longer;
  if (left->size() > right->size()) {
    longer = left;
    shorter = right;
  } else {
    longer = right;
    shorter = left;
  }

  intptr_t result_size = longer->size();
  HandleScope h1(H, reinterpret_cast<Object**>(&shorter));
  HandleScope h2(H, reinterpret_cast<Object**>(&longer));
  LargeInteger* result = H->AllocateLargeInteger(result_size);

  if (!shorter->negative() && !longer->negative()) {
    // s ^ l
    result->set_negative(false);
    for (intptr_t i = 0; i < shorter->size(); i++) {
      result->set_digit(i, shorter->digit(i) ^ longer->digit(i));
    }
    for (intptr_t i = shorter->size(); i < longer->size(); i++) {
      result->set_digit(i, 0 ^ longer->digit(i));
    }
  } else if (!shorter->negative() && longer->negative()) {
    //     s ^ -l
    //     s ^ ~(l - 1)
    //   ~(s ^ (l - 1))
    //  -((s ^ (l - 1)) + 1)
    result->set_negative(true);
    sddigit_t l1_borrow = -1;
    ddigit_t r1_carry = 1;
    for (intptr_t i = 0; i < shorter->size(); i++) {
      l1_borrow += static_cast<sddigit_t>(longer->digit(i));
      digit_t l1 = l1_borrow & kDigitMask;
      l1_borrow >>= kDigitShift;

      digit_t r = shorter->digit(i) ^ l1;

      r1_carry += static_cast<ddigit_t>(r);
      result->set_digit(i, r1_carry & kDigitMask);
      r1_carry >>= kDigitShift;
    }
    for (intptr_t i = shorter->size(); i < longer->size(); i++) {
      l1_borrow += static_cast<sddigit_t>(longer->digit(i));
      digit_t l1 = l1_borrow & kDigitMask;
      l1_borrow >>= kDigitShift;
      ASSERT(l1_borrow == 0 || l1_borrow == -1);

      digit_t r = 0 ^ l1;

      r1_carry += static_cast<ddigit_t>(r);
      result->set_digit(i, r1_carry & kDigitMask);
      r1_carry >>= kDigitShift;
    }
    ASSERT(r1_carry == 0);
  } else if (shorter->negative() && !longer->negative()) {
    //         -s ^ l
    //   ~(s - 1) ^ l
    //  ~((s - 1) ^ l)
    // -(((s - 1) ^ l) + 1)
    result->set_negative(true);
    sddigit_t s1_borrow = -1;
    ddigit_t r1_carry = 1;
    for (intptr_t i = 0; i < shorter->size(); i++) {
      s1_borrow += static_cast<sddigit_t>(shorter->digit(i));
      digit_t s1 = s1_borrow & kDigitMask;
      s1_borrow >>= kDigitShift;

      digit_t r = s1 ^ longer->digit(i);

      r1_carry += static_cast<ddigit_t>(r);
      result->set_digit(i, r1_carry & kDigitMask);
      r1_carry >>= kDigitShift;
    }
    for (intptr_t i = shorter->size(); i < longer->size(); i++) {
      s1_borrow += static_cast<sddigit_t>(0);
      digit_t s1 = s1_borrow & kDigitMask;
      s1_borrow >>= kDigitShift;

      digit_t r = s1 ^ longer->digit(i);

      r1_carry += static_cast<ddigit_t>(r);
      result->set_digit(i, r1_carry & kDigitMask);
      r1_carry >>= kDigitShift;
    }
    ASSERT(r1_carry == 0);
  } else if (shorter->negative() && longer->negative()) {
    //       -s ^ -l
    // ~(s - 1) ^ ~(l - 1)
    //  (s - 1) ^  (l - 1)
    result->set_negative(false);

    sddigit_t s1_borrow = -1;
    sddigit_t l1_borrow = -1;
    for (intptr_t i = 0; i < shorter->size(); i++) {
      s1_borrow += static_cast<sddigit_t>(shorter->digit(i));
      ddigit_t s1 = s1_borrow & kDigitMask;
      s1_borrow >>= kDigitShift;

      l1_borrow += static_cast<sddigit_t>(longer->digit(i));
      ddigit_t l1 = l1_borrow & kDigitMask;
      l1_borrow >>= kDigitShift;

      result->set_digit(i, s1 ^ l1);
    }
    for (intptr_t i = shorter->size(); i < longer->size(); i++) {
      s1_borrow += static_cast<sddigit_t>(0);
      ddigit_t s1 = s1_borrow & kDigitMask;
      s1_borrow >>= kDigitShift;

      l1_borrow += static_cast<sddigit_t>(longer->digit(i));
      ddigit_t l1 = l1_borrow & kDigitMask;
      l1_borrow >>= kDigitShift;

      result->set_digit(i, s1 ^ l1);
    }
    ASSERT(s1_borrow == 0);
    ASSERT(l1_borrow == 0);
  }

  Clamp(result);
  Verify(result);
  return result;
}


LargeInteger* LargeInteger::ShiftLeft(LargeInteger* left,
                                      intptr_t right,
                                      Heap* H) {
  Verify(left);

  ASSERT(right >= 0);
  intptr_t digit_shift = right / kDigitBits;
  intptr_t bit_shift_up = right % kDigitBits;
  intptr_t bit_shift_down = kDigitBits - bit_shift_up;

  if (bit_shift_up == 0) {
    // This case is singled out not for performance but to avoid
    // the undefined behavior of digit_t >> kDigitBits.
    intptr_t result_size = left->size() + digit_shift;
    HandleScope h1(H, reinterpret_cast<Object**>(&left));
    LargeInteger* result = H->AllocateLargeInteger(result_size);

    for (intptr_t i = 0; i < digit_shift; i++) {
      result->set_digit(i, 0);
    }
    for (intptr_t i = digit_shift; i < result_size; i++) {
      result->set_digit(i, left->digit(i - digit_shift));
    }

    result->set_negative(left->negative());
    result->set_size(result_size);
    Verify(result);
    return result;
  }

  intptr_t result_size = left->size() + digit_shift + 1;
  HandleScope h1(H, reinterpret_cast<Object**>(&left));
  LargeInteger* result = H->AllocateLargeInteger(result_size);

  for (intptr_t i = 0; i < digit_shift; i++) {
    result->set_digit(i, 0);
  }
  digit_t carry = 0;
  for (intptr_t i = digit_shift; i < result_size - 1; i++) {
    digit_t d = left->digit(i - digit_shift);
    result->set_digit(i, (d << bit_shift_up) | carry);
    carry = d >> bit_shift_down;
  }
  result->set_digit(result_size - 1, carry);

  if (carry == 0) {
    result->set_size(result_size - 1);
  } else {
    result->set_size(result_size);
  }

  result->set_negative(left->negative());
  Verify(result);
  return result;
}


LargeInteger* LargeInteger::ShiftRight(LargeInteger* left,
                                       intptr_t right,
                                       Heap* H) {
  Verify(left);

  intptr_t digit_shift = right / kDigitBits;
  intptr_t bit_shift_down = right % kDigitBits;
  intptr_t bit_shift_up = kDigitBits - bit_shift_down;

  if (bit_shift_down == 0) {
    // This case is singled out not for performance but to avoid
    // the undefined behavior of digit_t << kDigitBits.
    intptr_t result_size = left->size() - digit_shift;
    HandleScope h1(H, reinterpret_cast<Object**>(&left));
    LargeInteger* result = H->AllocateLargeInteger(result_size);

    for (intptr_t i = 0; i < result_size; i++) {
      result->set_digit(i, left->digit(i + digit_shift));
    }
    result->set_size(result_size);
    result->set_negative(left->negative());

    if (result->negative()) {
      // We shifted in sign-magnitude form. To get the two's complement result,
      // we need to subtract by 1 if we shifted out any bit.
      for (intptr_t i = 0; i < digit_shift; i++) {
        if (left->digit(i) != 0) {
          AbsAddOneInPlace(result);
          Verify(result);
          return result;
        }
      }
    }
    Verify(result);
    return result;
  }

  intptr_t result_size = left->size() - digit_shift;
  HandleScope h1(H, reinterpret_cast<Object**>(&left));
  LargeInteger* result = H->AllocateLargeInteger(result_size);

  digit_t carry = left->digit(digit_shift) >> bit_shift_down;
  for (intptr_t i = 0; i < result_size - 1; i++) {
    digit_t d = left->digit(i + digit_shift + 1);
    result->set_digit(i, (d << bit_shift_up) | carry);
    carry = d >> bit_shift_down;
  }
  result->set_digit(result_size - 1, carry);

  if (carry == 0) {
    result->set_size(result_size - 1);
  } else {
    result->set_size(result_size);
  }

  result->set_negative(left->negative());

  if (result->negative()) {
    // We shifted in sign-magnitude form. To get the two's complement result, we
    // need to subtract by 1 if we shifted out any bit.
    if (bit_shift_down != 0 &&
        ((left->digit(digit_shift) << bit_shift_up) & kDigitMask) != 0) {
      AbsAddOneInPlace(result);
      Verify(result);
      return result;
    }
    for (intptr_t i = 0; i < digit_shift; i++) {
      if (left->digit(i) != 0) {
        AbsAddOneInPlace(result);
        Verify(result);
        return result;
      }
    }
  }

  Verify(result);
  return result;
}


PSOUP_UNUSED static intptr_t CountLeadingZeros(uint16_t x)  {
  if (x == 0) return 16;
  intptr_t n = 0;
  if (x <= 0x00FF) { n = n + 8; x = x << 8; }
  if (x <= 0x0FFF) { n = n + 4; x = x << 4; }
  if (x <= 0x3FFF) { n = n + 2; x = x << 2; }
  if (x <= 0x7FFF) { n = n + 1; }
  return n;
}


PSOUP_UNUSED static intptr_t CountLeadingZeros(uint32_t x) {
  if (x == 0) return 32;
  intptr_t n = 0;
  if (x <= 0x0000FFFF) { n = n + 16; x = x << 16; }
  if (x <= 0x00FFFFFF) { n = n +  8; x = x <<  8; }
  if (x <= 0x0FFFFFFF) { n = n +  4; x = x <<  4; }
  if (x <= 0x3FFFFFFF) { n = n +  2; x = x <<  2; }
  if (x <= 0x7FFFFFFF) { n = n +  1; }
  return n;
}


LargeInteger* LargeInteger::Divide(DivOperationType op_type,
                                   DivResultType result_type,
                                   LargeInteger* dividend,
                                   LargeInteger* divisor,
                                   Heap* H) {
  Verify(dividend);
  Verify(divisor);

  if (divisor->size() == 0) {
    // Division by zero handled by caller.
    UNREACHABLE();
  }

  intptr_t m = dividend->size();
  intptr_t n = divisor->size();
  if (dividend->size() < divisor->size()) {
    if (result_type == kQuoitent) {
      if (op_type == kTruncated) {
        // return 0
        LargeInteger* quotient = H->AllocateLargeInteger(0);
        quotient->set_negative(false);
        quotient->set_size(0);
        return quotient;
      }

      if (op_type == kFloored) {
        if ((dividend->size() == 0) ||
            (dividend->negative() == divisor->negative())) {
          // return 0
          LargeInteger* quotient = H->AllocateLargeInteger(0);
          quotient->set_negative(false);
          quotient->set_size(0);
          return quotient;
        } else {
          // return -1
          LargeInteger* quotient = H->AllocateLargeInteger(1);
          quotient->set_negative(true);
          quotient->set_size(1);
          quotient->set_digit(0, 1);
          return quotient;
        }
      }

      if (op_type == kExact) {
        if (dividend->size() == 0) {
          return dividend;
        }
        return NULL;
      }
    }

    if (result_type == kRemainder) {
      if (op_type == kTruncated) {
        return dividend;
      }

      if (op_type == kFloored) {
        if ((dividend->size() == 0) ||
            (dividend->negative() == divisor->negative())) {
          return dividend;
        } else {
          return Add(dividend, divisor, H);
        }
      }
    }

    UNREACHABLE();
  }

  HandleScope h1(H, reinterpret_cast<Object**>(&dividend));
  HandleScope h2(H, reinterpret_cast<Object**>(&divisor));
  LargeInteger* quoitent = H->AllocateLargeInteger(m - n + 1);
  quoitent->set_negative(dividend->negative() != divisor->negative());

  if (n == 1) {
    // Single-digit divisor.

    digit_t divisor_d = divisor->digit(0);
    ddigit_t remainder_d = 0;
    for (intptr_t j = m - 1; j >= 0; j--) {
      digit_t dividend_d = dividend->digit(j);
      digit_t quoitent_d = (remainder_d * kDigitBase + dividend_d) / divisor_d;
      quoitent->set_digit(j, quoitent_d);
      remainder_d = (remainder_d * kDigitBase + dividend_d) -
          (static_cast<ddigit_t>(quoitent_d) *
           static_cast<ddigit_t>(divisor_d));
    }
    Clamp(quoitent);
    Verify(quoitent);

    if (result_type == kQuoitent) {
      if (op_type == kTruncated) {
        return quoitent;
      }

      if (op_type == kFloored) {
        if ((remainder_d != 0) &&
            (dividend->negative() != divisor->negative())) {
          // return quoitent - 1;
          ASSERT(quoitent->negative());
          AbsAddOneInPlace(quoitent);
          return quoitent;
        }
        return quoitent;
      }

      if (op_type == kExact) {
        if (remainder_d == 0) {
          return quoitent;
        } else {
          return NULL;
        }
      }
    }

    if (result_type == kRemainder) {
      if (op_type == kTruncated) {
        if (remainder_d == 0) {
          LargeInteger* remainder = H->AllocateLargeInteger(0);
          remainder->set_negative(false);
          remainder->set_size(0);
          Verify(remainder);
          return remainder;
        }

        LargeInteger* remainder = H->AllocateLargeInteger(1);
        remainder->set_negative(dividend->negative());
        remainder->set_size(1);
        remainder->set_digit(0, remainder_d);
        Verify(remainder);
        return remainder;
      }

      if (op_type == kFloored) {
        if (remainder_d == 0) {
          LargeInteger* remainder = H->AllocateLargeInteger(0);
          remainder->set_negative(false);
          remainder->set_size(0);
          Verify(remainder);
          return remainder;
        }

        if (dividend->negative() != divisor->negative()) {
          LargeInteger* remainder = H->AllocateLargeInteger(1);
          remainder->set_negative(divisor->negative());
          remainder->set_size(1);
          remainder->set_digit(0, divisor_d - remainder_d);
          Verify(remainder);
          return remainder;
        }

        LargeInteger* remainder = H->AllocateLargeInteger(1);
        remainder->set_negative(divisor->negative());
        remainder->set_size(1);
        remainder->set_digit(0, remainder_d);
        Verify(remainder);
        return remainder;
      }
    }

    UNREACHABLE();
  }

  // Multi-digit divisor.

  intptr_t normalize_shift = CountLeadingZeros(divisor->digit(n - 1));
  intptr_t inv_normalize_shift = kDigitBits - normalize_shift;
  digit_t* norm_div = new digit_t[n];
  for (intptr_t i = n - 1; i > 0; i--) {
    norm_div[i] = (divisor->digit(i) << normalize_shift) |
        (static_cast<ddigit_t>(divisor->digit(i - 1)) >>
            inv_normalize_shift);
  }
  norm_div[0] = divisor->digit(0) << normalize_shift;

  digit_t* norm_rem = new digit_t[m + 1];
  norm_rem[m] = static_cast<ddigit_t>(dividend->digit(m-1)) >>
      inv_normalize_shift;
  for (intptr_t i = m - 1; i > 0; i--) {
    norm_rem[i] = (dividend->digit(i) << normalize_shift) |
        (static_cast<ddigit_t>(dividend->digit(i - 1)) >>
            inv_normalize_shift);
  }
  norm_rem[0] = dividend->digit(0) << normalize_shift;

  for (intptr_t j = m - n; j >= 0; j--) {
    ddigit_t p = norm_rem[j+n] * kDigitBase + norm_rem[j+n-1];
    ddigit_t q_est = p / norm_div[n-1];
    ddigit_t r_est = p - (q_est * norm_div[n-1]);
  again:
    if ((q_est >= kDigitBase) ||
        (q_est * norm_div[n-2]) > (kDigitBase * r_est + norm_rem[j+n-2])) {
      q_est = q_est - 1;
      r_est = r_est + norm_div[n-1];
      if (r_est < kDigitBase) goto again;
    }

    sddigit_t k = 0;
    sddigit_t t;
    for (intptr_t i = 0; i < n; i++) {
      ddigit_t p = q_est * norm_div[i];
      t = norm_rem[i+j] - k - (p & kDigitMask);
      norm_rem[i+j] = t;
      k = (p >> kDigitBits) - (t >> kDigitBits);
    }
    t = norm_rem[j+n] - k;
    norm_rem[j+n] = t;

    quoitent->set_digit(j, q_est);
    if (t < 0) {
      quoitent->set_digit(j, quoitent->digit(j) - 1);
      k = 0;
      for (intptr_t i = 0; i < n; i++) {
        t = static_cast<ddigit_t>(norm_rem[i+j]) + norm_div[i] + k;
        norm_rem[i + j] = t;
        k = t >> kDigitBits;
      }
      norm_rem[j+n] = norm_rem[j+n] + k;
    }
  }

  if (result_type == kQuoitent) {
    Clamp(quoitent);
    Verify(quoitent);

    bool remainder_is_zero = true;
    for (intptr_t i = 0; i <= n; i++) {
      if (norm_rem[i] != 0) {
        remainder_is_zero = false;
        break;
      }
    }

    delete[] norm_div;
    delete[] norm_rem;

    if (op_type == kTruncated) {
      return quoitent;
    }

    if (op_type == kFloored) {
      if (!remainder_is_zero &&
          (dividend->negative() != divisor->negative())) {
        ASSERT(quoitent->negative() || (quoitent->size() == 0));
        AbsAddOneInPlace(quoitent);
        quoitent->set_negative(true);
        return quoitent;
      }
      return quoitent;
    }

    if (op_type == kExact) {
      if (remainder_is_zero) {
        return quoitent;
      } else {
        return NULL;
      }
    }
  }

  if (result_type == kRemainder) {
    LargeInteger* remainder = H->AllocateLargeInteger(n);
    remainder->set_negative(dividend->negative());
    for (intptr_t i = 0; i < n - 1; i++) {
      digit_t d = (norm_rem[i] >> normalize_shift) |
          (static_cast<ddigit_t>(norm_rem[i + 1]) <<
              inv_normalize_shift);
      remainder->set_digit(i, d);
    }
    remainder->set_digit(n - 1, norm_rem[n - 1] >> normalize_shift);

    Clamp(remainder);
    Verify(remainder);
    delete[] norm_div;
    delete[] norm_rem;

    if (op_type == kTruncated) {
      return remainder;
    }

    if (op_type == kFloored) {
      if ((remainder->size() != 0) &&
          (dividend->negative() != divisor->negative())) {
        return Add(remainder, divisor, H);
      }
      return remainder;
    }
  }

  UNREACHABLE();
  return NULL;
}


ByteString* LargeInteger::PrintString(LargeInteger* large, Heap* H) {
#if defined(ARCH_IS_32_BIT)
  const ddigit_t kDivisor = 10000;
  const intptr_t kDivisorLog10 = 4;
#elif defined(ARCH_IS_64_BIT)
  const ddigit_t kDivisor = 1000000000;
  const intptr_t kDivisorLog10 = 9;
#else
#error Unexpected architecture word size
#endif
  ASSERT(kDivisor < kDigitBase);
  // log10(2) = 0.30102999566398114
  const intptr_t kLog2Dividend = 30103;
  const intptr_t kLog2Divisor = 100000;
  intptr_t binary_digits = large->size() * sizeof(digit_t) * kBitsPerByte;
  intptr_t est_decimal_digits =
      (binary_digits * kLog2Dividend / kLog2Divisor) + 1 + kDivisorLog10;
  if (large->negative()) {
    est_decimal_digits++;
  }

  char* chars = new char[est_decimal_digits];
  intptr_t pos = est_decimal_digits;

  digit_t* scratch = new digit_t[large->size()];
  for (intptr_t i = 0; i < large->size(); i++) {
    scratch[i] = large->digit(i);
  }

  intptr_t used = large->size();
  while (used > 0) {
    digit_t remainder = 0;
    for (intptr_t i = used - 1; i >= 0; i--) {
      ddigit_t dividend =
          (static_cast<ddigit_t>(remainder) << kDigitShift) + scratch[i];
      digit_t quotient = dividend / kDivisor;
      remainder = dividend - (static_cast<ddigit_t>(quotient) * kDivisor);
      scratch[i] = quotient;
    }
    while ((used > 0) && (scratch[used - 1] == 0)) {
      used--;
    }
    for (intptr_t i = 0; i < kDivisorLog10; i++) {
      chars[--pos] = '0' + (remainder % 10);
      remainder /= 10;
    }
    ASSERT(remainder == 0);
  }

  ASSERT((0 <= pos) && (pos < est_decimal_digits));
  // Remove leading zeros.
  while ((pos < est_decimal_digits) && (chars[pos] == '0')) {
    pos++;
  }
  ASSERT((0 <= pos) && (pos < est_decimal_digits));
  if (large->negative()) {
    chars[--pos] = '-';
  }
  ASSERT((0 <= pos) && (pos < est_decimal_digits));

  delete[] scratch;

  intptr_t nchars = est_decimal_digits - pos;
  ByteString* result = H->AllocateByteString(nchars);
  memcpy(result->element_addr(0), &chars[pos], nchars);

  delete[] chars;

  return result;
}


}  // namespace psoup
