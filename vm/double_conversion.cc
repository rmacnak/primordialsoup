// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/double_conversion.h"

#include "vm/assert.h"

#include "double-conversion/double-conversion.h"

namespace psoup {

static const char kDoubleToStringCommonExponentChar = 'e';
static const char* kDoubleToStringCommonInfinitySymbol = "Infinity";
static const char* kDoubleToStringCommonNaNSymbol = "NaN";

void DoubleToCString(double d, char* buffer, int buffer_size) {
  static const int kDecimalLow = -6;
  static const int kDecimalHigh = 21;

  // The output contains the sign, at most kDecimalHigh - 1 digits,
  // the decimal point followed by a 0 plus the \0.
  ASSERT(buffer_size >= 1 + (kDecimalHigh - 1) + 1 + 1 + 1);
  // Or it contains the sign, a 0, the decimal point, kDecimalLow '0's,
  // 17 digits (the precision needed for doubles), plus the \0.
  ASSERT(buffer_size >= 1 + 1 + 1 + kDecimalLow + 17 + 1);
  // Alternatively it contains a sign, at most 17 digits (precision needed for
  // any double), the decimal point, the exponent character, the exponent's
  // sign, at most three exponent digits, plus the \0.
  ASSERT(buffer_size >= 1 + 17 + 1 + 1 + 1 + 3 + 1);

  static const int kConversionFlags =
      double_conversion::DoubleToStringConverter::EMIT_POSITIVE_EXPONENT_SIGN |
      double_conversion::DoubleToStringConverter::EMIT_TRAILING_DECIMAL_POINT |
     double_conversion::DoubleToStringConverter::EMIT_TRAILING_ZERO_AFTER_POINT;

  const double_conversion::DoubleToStringConverter converter(
      kConversionFlags,
      kDoubleToStringCommonInfinitySymbol,
      kDoubleToStringCommonNaNSymbol,
      kDoubleToStringCommonExponentChar,
      kDecimalLow,
      kDecimalHigh,
      0, 0);  // Last two values are ignored in shortest mode.

  double_conversion::StringBuilder builder(buffer, buffer_size);
  bool status = converter.ToShortest(d, &builder);
  ASSERT(status);
  char* result = builder.Finalize();
  ASSERT(result == buffer);
}


bool CStringToDouble(const char* str, intptr_t length, double* result) {
  if (length == 0) {
    return false;
  }

  double_conversion::StringToDoubleConverter converter(
    double_conversion::StringToDoubleConverter::NO_FLAGS,
    0.0,
    0.0,
    kDoubleToStringCommonInfinitySymbol,
    kDoubleToStringCommonNaNSymbol);

  int parsed_count = 0;
  *result = converter.StringToDouble(str,
                                     static_cast<int>(length),
                                     &parsed_count);
  return (parsed_count == length);
}

}  // namespace psoup
