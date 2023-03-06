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
  Assert(const char* file, int line) : file_(file), line_(line) {}

  NORETURN void Fail(const char* format, ...) PRINTF_ATTRIBUTE(2, 3);

 private:
  const char* file_;
  int line_;
};

}  // namespace psoup

#if defined(_MSC_VER)
#define FATAL(format, ...)                                                     \
  psoup::Assert(__FILE__, __LINE__).Fail(format, __VA_ARGS__)
#else
#define FATAL(format, ...)                                                     \
  psoup::Assert(__FILE__, __LINE__).Fail(format, ##__VA_ARGS__)
#endif

#define UNIMPLEMENTED() FATAL("unimplemented code")

#define UNREACHABLE() FATAL("unreachable code")

#define OUT_OF_MEMORY() FATAL("Out of memory.")

#if defined(DEBUG)
// DEBUG binaries use assertions in the code.
// Note: We wrap the if statement in a do-while so that we get a compile
//       error if there is no semicolon after ASSERT(condition). This
//       ensures that we get the same behavior on DEBUG and RELEASE builds.

#define ASSERT(cond)                                                           \
  do {                                                                         \
    if (!(cond))                                                               \
        psoup::Assert(__FILE__, __LINE__).Fail("expected: %s", #cond);         \
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
