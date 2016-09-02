// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_ASSERT_H_
#define VM_ASSERT_H_

#include "vm/globals.h"

namespace psoup {

class Assert {
 public:
  Assert(const char* file, int line) : file_(file), line_(line) {}

  void Fail(const char* format, ...) PRINTF_ATTRIBUTE(2, 3) NORETURN_ATTRIBUTE;

 private:
  const char* file_;
  int line_;
};

}  // namespace psoup

#define FATAL(error)                                                           \
  psoup::Assert(__FILE__, __LINE__).Fail("%s", error)

#define FATAL1(format, p1)                                                     \
  psoup::Assert(__FILE__, __LINE__).Fail(format, (p1))

#define FATAL2(format, p1, p2)                                                 \
  psoup::Assert(__FILE__, __LINE__).Fail(format, (p1), (p2))

#define UNIMPLEMENTED() \
  FATAL("unimplemented code")

#define UNREACHABLE() \
  FATAL("unreachable code")

#if defined(DEBUG)

#define ASSERT(cond)                                                           \
  do {                                                                         \
    if (!(cond))                                                               \
        psoup::Assert(__FILE__, __LINE__).Fail("expected: %s", #cond);         \
  } while (false)

#else  // if defined(DEBUG)

#define ASSERT(cond) do {} while (false && (cond))

#endif  // if defined(DEBUG)

#if !defined(COMPILE_ASSERT)
template <bool>
struct CompileAssert {
};
#define COMPILE_ASSERT_JOIN(a, b) COMPILE_ASSERT_JOIN_HELPER(a, b)
#define COMPILE_ASSERT_JOIN_HELPER(a, b) a##b
#define COMPILE_ASSERT(expr)                                                   \
  PSOUP_UNUSED typedef CompileAssert<(static_cast<bool>(expr))>     \
  COMPILE_ASSERT_JOIN(CompileAssertTypeDef, __LINE__)[static_cast<bool>(expr)  \
  ? 1 : -1]
#endif  // !defined(COMPILE_ASSERT)

#endif  // VM_ASSERT_H_
