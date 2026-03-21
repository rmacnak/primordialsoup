// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_ASSERT_H_
#define VM_ASSERT_H_

#include "vm/globals.h"

#if !defined(DEBUG) && !defined(NDEBUG)
#error neither DEBUG nor NDEBUG defined
#elif defined(DEBUG) && defined(NDEBUG)
#error both DEBUG and NDEBUG defined
#endif

namespace psoup {

class Assert {
 public:
  NORETURN static void Fail(const char* file, int line, const char* format, ...)
      PRINTF_ATTRIBUTE(3, 4);
  NORETURN static void Unimplemented(const char* file, int line);
  NORETURN static void Unreachable(const char* file, int line);
  NORETURN static void OutOfMemory(const char* file, int line);
};

}  // namespace psoup

#define FATAL(format, ...)                                                     \
  psoup::Assert::Fail(__FILE__, __LINE__, format, ##__VA_ARGS__)

#define UNIMPLEMENTED() psoup::Assert::Unimplemented(__FILE__, __LINE__)

#define UNREACHABLE() psoup::Assert::Unreachable(__FILE__, __LINE__)

#define OUT_OF_MEMORY() psoup::Assert::OutOfMemory(__FILE__, __LINE__)

#if defined(DEBUG)
// DEBUG binaries use assertions in the code.
// Note: We wrap the if statement in a do-while so that we get a compile
//       error if there is no semicolon after ASSERT(condition). This
//       ensures that we get the same behavior on DEBUG and RELEASE builds.

#define ASSERT(cond)                                                           \
  do {                                                                         \
    if (!(cond))                                                               \
        psoup::Assert::Fail(__FILE__, __LINE__, "expected: %s", #cond);        \
  } while (false)

// DEBUG_ASSERT allows identifiers in condition to be undeclared in release
// mode.
#define DEBUG_ASSERT(cond) ASSERT(cond)

#else  // if defined(DEBUG)

// In order to avoid variable unused warnings for code that only uses
// a variable in an ASSERT or EXPECT, we make sure to use the macro
// argument.
#define ASSERT(condition)                                                      \
  do {                                                                         \
  } while (false && (condition))

#define DEBUG_ASSERT(cond)

#endif  // if defined(DEBUG)

#define COMPILE_ASSERT(cond) static_assert(cond, "")

#endif  // VM_ASSERT_H_
