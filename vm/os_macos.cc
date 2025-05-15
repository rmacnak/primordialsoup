// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(OS_MACOS)

#include "vm/os.h"

#include <errno.h>
#include <mach/mach_time.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

#include "vm/assert.h"

namespace psoup {

void OS::Startup() {}

void OS::Shutdown() {}

int64_t OS::CurrentMonotonicNanos() {
  return clock_gettime_nsec_np(CLOCK_MONOTONIC);
}

int64_t OS::CurrentRealtimeNanos() {
  return clock_gettime_nsec_np(CLOCK_REALTIME);
}

intptr_t OS::GetEntropy(void* buffer, size_t size) {
  if (getentropy(buffer, size) == -1) {
    return errno;
  }
  return 0;
}

const char* OS::Name() { return "macos"; }

intptr_t OS::NumberOfAvailableProcessors() {
  return sysconf(_SC_NPROCESSORS_ONLN);
}

void OS::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vdprintf(STDOUT_FILENO, format, args);
  va_end(args);
}

void OS::PrintErr(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vdprintf(STDERR_FILENO, format, args);
  va_end(args);
}

char* OS::PrintStr(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list measure_args;
  va_copy(measure_args, args);
  intptr_t len = vsnprintf(nullptr, 0, format, measure_args);
  va_end(measure_args);

  char* buffer = reinterpret_cast<char*>(malloc(len + 1));

  va_list print_args;
  va_copy(print_args, args);
  int r = vsnprintf(buffer, len + 1, format, print_args);
  ASSERT(r >= 0);
  va_end(print_args);
  va_end(args);
  return buffer;
}

char* OS::StrError(int err, char* buffer, size_t bufsize) {
  if (strerror_r(err, buffer, bufsize) != 0) {
    snprintf(buffer, bufsize, "%s", "strerror_r failed");
  }
  return buffer;
}

void OS::Exit(int code) {
  exit(code);
}

}  // namespace psoup

#endif  // __APPLE__
