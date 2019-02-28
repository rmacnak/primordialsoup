// Copyright (c) 2011, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_DOUBLE_CONVERSION_H_
#define VM_DOUBLE_CONVERSION_H_

#include "vm/globals.h"

namespace psoup {

void DoubleToCString(double d, char* buffer, int buffer_size);
bool CStringToDouble(const char* str, intptr_t length, double* result);

}  // namespace psoup

#endif  // VM_DOUBLE_CONVERSION_H_
