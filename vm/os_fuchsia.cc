// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(OS_FUCHSIA)

#include "vm/os.h"

#include <errno.h>
#include <stdarg.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include "vm/assert.h"

namespace psoup {

void OS::Startup() {}
void OS::Shutdown() {}

int64_t OS::CurrentMonotonicNanos() {
  return zx_clock_get_monotonic();
}

int64_t OS::CurrentRealtimeNanos() {
  struct timespec ts;
  if (timespec_get(&ts, TIME_UTC) == 0) {
    FATAL("timespec_get failed");
    return 0;
  }
  return zx_time_add_duration(ZX_SEC(ts.tv_sec), ZX_NSEC(ts.tv_nsec));
}

intptr_t OS::GetEntropy(void* buffer, size_t size) {
  zx_cprng_draw(buffer, size);
  return ZX_OK;
}

const char* OS::Name() { return "fuchsia"; }

intptr_t OS::NumberOfAvailableProcessors() {
  return zx_system_get_num_cpus();
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

#endif  // defined(OS_FUCHSIA)
