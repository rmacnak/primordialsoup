// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(OS_WINDOWS)

#include "vm/os.h"

#include <bcrypt.h>
#include <malloc.h>
#include <process.h>
#include <time.h>

#include "vm/assert.h"
#include "vm/thread.h"

namespace psoup {

static int64_t qpc_ticks_per_second = 0;

static int64_t GetCurrentMonotonicTicks() {
  if (qpc_ticks_per_second == 0) {
    FATAL("QueryPerformanceCounter not supported");
  }
  // Grab performance counter value.
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return static_cast<int64_t>(now.QuadPart);
}

static int64_t GetCurrentMonotonicFrequency() {
  if (qpc_ticks_per_second == 0) {
    FATAL("QueryPerformanceCounter not supported");
  }
  return qpc_ticks_per_second;
}

intptr_t OS::GetEntropy(void* buffer, size_t size) {
  if (size <= 0) {
    return 0;
  }
  NTSTATUS status = BCryptGenRandom(nullptr,
                                    reinterpret_cast<PUCHAR>(buffer),
                                    static_cast<ULONG>(size),
                                    BCRYPT_USE_SYSTEM_PREFERRED_RNG);
  return status >= 0 ? 0 : 1;
}

int64_t OS::CurrentMonotonicNanos() {
  int64_t ticks = GetCurrentMonotonicTicks();
  int64_t frequency = GetCurrentMonotonicFrequency();

  // Convert to microseconds.
  int64_t seconds = ticks / frequency;
  int64_t leftover_ticks = ticks - (seconds * frequency);
  int64_t result = seconds * kNanosecondsPerSecond;
  result += ((leftover_ticks * kNanosecondsPerSecond) / frequency);
  return result;
}

int64_t OS::CurrentRealtimeNanos() {
  static const int64_t kTimeEpoc = 116444736000000000LL;
  static const int64_t kTimeScaler = 100;  // 100 ns to ns.
  // Although win32 uses 64-bit integers for representing timestamps,
  // these are packed into a FILETIME structure. The FILETIME
  // structure is just a struct representing a 64-bit integer. The
  // TimeStamp union allows access to both a FILETIME and an integer
  // representation of the timestamp. The Windows timestamp is in
  // 100-nanosecond intervals since January 1, 1601.
  union TimeStamp {
    FILETIME ft_;
    int64_t t_;
  };
  TimeStamp time;
  GetSystemTimeAsFileTime(&time.ft_);
  return (time.t_ - kTimeEpoc) * kTimeScaler;
}

const char* OS::Name() { return "windows"; }

intptr_t OS::NumberOfAvailableProcessors() {
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwNumberOfProcessors;
}

static void VFPrint(FILE* stream, const char* format, va_list args) {
  vfprintf(stream, format, args);
  fflush(stream);
}

void OS::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VFPrint(stdout, format, args);
  va_end(args);
}

void OS::PrintErr(const char* format, ...) {
  va_list args;
  va_start(args, format);
  VFPrint(stderr, format, args);
  va_end(args);
}

static int VSNPrint(char* str, size_t size, const char* format, va_list args) {
  if (str == nullptr || size == 0) {
    int retval = _vscprintf(format, args);
    if (retval < 0) {
      FATAL("Fatal error in OS::VSNPrint with format '%s'", format);
    }
    return retval;
  }
  va_list args_copy;
  va_copy(args_copy, args);
  int written = _vsnprintf(str, size, format, args_copy);
  va_end(args_copy);
  if (written < 0) {
    // _vsnprintf returns -1 if the number of characters to be written is
    // larger than 'size', so we call _vscprintf which returns the number
    // of characters that would have been written.
    va_list args_retry;
    va_copy(args_retry, args);
    written = _vscprintf(format, args_retry);
    if (written < 0) {
      FATAL("Fatal error in OS::VSNPrint with format '%s'", format);
    }
    va_end(args_retry);
  }
  // Make sure to zero-terminate the string if the output was
  // truncated or if there was an error.
  // The static cast is safe here as we have already determined that 'written'
  // is >= 0.
  if (static_cast<size_t>(written) >= size) {
    str[size - 1] = '\0';
  }
  return written;
}

char* OS::PrintStr(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list measure_args;
  va_copy(measure_args, args);
  intptr_t len = VSNPrint(nullptr, 0, format, measure_args);
  va_end(measure_args);

  char* buffer = reinterpret_cast<char*>(malloc(len + 1));

  va_list print_args;
  va_copy(print_args, args);
  int r = VSNPrint(buffer, len + 1, format, print_args);
  ASSERT(r >= 0);
  va_end(print_args);
  va_end(args);
  return buffer;
}

char* OS::StrError(int err, char* buffer, size_t bufsize) {
  DWORD message_size =
      FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    buffer, static_cast<DWORD>(bufsize), nullptr);
  if (message_size == 0) {
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      snprintf(buffer, bufsize,
               "FormatMessage failed for error code %d (error %d)\n", err,
               GetLastError());
    }
    snprintf(buffer, bufsize, "OS Error %d", err);
  }
  // Ensure string termination.
  buffer[bufsize - 1] = 0;
  return buffer;
}

void OS::Startup() {
  // Do not pop up a message box when abort is called.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
  LARGE_INTEGER ticks_per_sec;
  if (!QueryPerformanceFrequency(&ticks_per_sec)) {
    qpc_ticks_per_second = 0;
  } else {
    qpc_ticks_per_second = static_cast<int64_t>(ticks_per_sec.QuadPart);
  }
}

void OS::Shutdown() {}

void OS::Exit(int code) {
  // On Windows we use ExitProcess so that threads can't clobber the exit_code.
  // See: https://code.google.com/p/nativeclient/issues/detail?id=2870
  ::ExitProcess(code);
}

}  // namespace psoup

#endif  // defined(OS_WINDOWS)
