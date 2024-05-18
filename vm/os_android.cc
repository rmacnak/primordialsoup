// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(OS_ANDROID)

#include "vm/os.h"

#include <android/log.h>
#include <errno.h>
#include <sys/random.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "vm/assert.h"

namespace psoup {

void OS::Startup() {}
void OS::Shutdown() {}

int64_t OS::CurrentMonotonicNanos() {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    UNREACHABLE();
    return 0;
  }
  // Convert to nanoseconds.
  int64_t result = ts.tv_sec;
  result *= kNanosecondsPerSecond;
  result += ts.tv_nsec;

  return result;
}

int64_t OS::CurrentRealtimeNanos() {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    UNREACHABLE();
    return 0;
  }
  // Convert to nanoseconds.
  int64_t result = ts.tv_sec;
  result *= kNanosecondsPerSecond;
  result += ts.tv_nsec;

  return result;
}

intptr_t OS::GetEntropy(void* buffer, size_t size) {
  if (getentropy(buffer, size) == -1) {
    return errno;
  }
  return 0;
}

const char* OS::Name() { return "android"; }

intptr_t OS::NumberOfAvailableProcessors() {
  return sysconf(_SC_NPROCESSORS_ONLN);
}

void OS::Print(const char* format, ...) {
  va_list args;

  va_start(args, format);
  vfprintf(stdout, format, args);
  fflush(stdout);
  va_end(args);

  va_start(args, format);
  __android_log_vprint(ANDROID_LOG_INFO, "PrimordialSoup", format, args);
  va_end(args);
}

void OS::PrintErr(const char* format, ...) {
  va_list args;

  va_start(args, format);
  vfprintf(stderr, format, args);
  fflush(stderr);
  va_end(args);

  va_start(args, format);
  __android_log_vprint(ANDROID_LOG_ERROR, "PrimordialSoup", format, args);
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

#endif  // defined(OS_ANDROID)
