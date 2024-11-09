// Copyright (c) 2011, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_OS_H_
#define VM_OS_H_

#include "vm/allocation.h"
#include "vm/globals.h"

namespace psoup {

class OS : public AllStatic {
 public:
  static void Startup();
  static void Shutdown();

  static int64_t CurrentMonotonicNanos();
  static int64_t CurrentRealtimeNanos();

  static intptr_t GetEntropy(void* buffer, size_t size);

  static const char* Name();
  static intptr_t NumberOfAvailableProcessors();

  static void Print(const char* format, ...) PRINTF_ATTRIBUTE(1, 2);
  static void PrintErr(const char* format, ...) PRINTF_ATTRIBUTE(1, 2);
  static char* PrintStr(const char* format, ...) PRINTF_ATTRIBUTE(1, 2);

  static char* StrError(int err, char* buffer, size_t bufsize);

  NORETURN static void Exit(int code);
};

}  // namespace psoup

#endif  // VM_OS_H_
