// Copyright (c) 2012, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_UTILS_H_
#define VM_UTILS_H_

#include "vm/globals.h"
#include "vm/assert.h"

namespace psoup {

class Utils {
 public:
  template<typename T>
  static inline T RoundDown(T x, intptr_t n) {
    ASSERT(IsPowerOfTwo(n));
    return (x & -n);
  }

  template<typename T>
  static inline T RoundUp(T x, intptr_t n) {
    return RoundDown(x + n - 1, n);
  }

  template<typename T>
  static inline bool IsPowerOfTwo(T x) {
    return ((x & (x - 1)) == 0) && (x != 0);
  }

  template<typename T>
  static inline bool IsAligned(T x, intptr_t n) {
    ASSERT(IsPowerOfTwo(n));
    return (x & (n - 1)) == 0;
  }

  static char* StrError(int err, char* buffer, size_t bufsize);
};

}  // namespace psoup

#if defined(TARGET_OS_ANDROID)
#include "vm/utils_android.h"
#elif defined(TARGET_OS_LINUX)
#include "vm/utils_linux.h"
#elif defined(TARGET_OS_MACOS)
#include "vm/utils_macos.h"
#elif defined(TARGET_OS_WINDOWS)
#include "vm/utils_win.h"
#else
#error Unknown target os.
#endif

#endif  // VM_UTILS_H_
