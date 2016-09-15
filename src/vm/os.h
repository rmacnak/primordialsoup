// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_OS_H_
#define VM_OS_H_

#include "vm/globals.h"

namespace psoup {

class OS {
 public:
  static void InitOnce();

  static int64_t CurrentMonotonicMicros();
  static int64_t CurrentMonotonicMillis();

  static int NumberOfAvailableProcessors();

  static void DebugBreak();

  static void Print(const char* format, ...) PRINTF_ATTRIBUTE(1, 2);
  static void PrintErr(const char* format, ...) PRINTF_ATTRIBUTE(1, 2);

  static void Abort();

  static void Exit(int code);
};

}  // namespace psoup

#endif  // VM_OS_H_
