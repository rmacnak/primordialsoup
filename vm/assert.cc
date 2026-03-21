// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/assert.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

namespace psoup {

void Assert::Fail(const char* file, int line, const char* format, ...) {
  fprintf(stderr, "%s:%d: error: ", file, line);
  va_list arguments;
  va_start(arguments, format);
  vfprintf(stderr, format, arguments);
  va_end(arguments);
  fprintf(stderr, "\n");
  fflush(stderr);
  abort();
}

void Assert::Unimplemented(const char* file, int line) {
  Fail(file, line, "unimplemented code");
}

void Assert::Unreachable(const char* file, int line) {
  Fail(file, line, "unreachable code");
}

void Assert::OutOfMemory(const char* file, int line) {
  Fail(file, line, "out of memory");
}

}  // namespace psoup
