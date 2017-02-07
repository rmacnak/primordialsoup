// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_PRIMITIVES_H_
#define VM_PRIMITIVES_H_

#include "vm/globals.h"

namespace psoup {

class Heap;
class Interpreter;

typedef bool (PrimitiveFunction)(intptr_t num_args,
                                 Heap* heap,
                                 Interpreter* interpreter);

class Primitives {
 public:
  static void Startup();
  static void Shutdown();

  static bool Invoke(intptr_t prim,
                     intptr_t num_args,
                     Heap* heap,
                     Interpreter* I);
  static bool IsUnwindProtect(intptr_t prim) {
    return prim == 113;
  }

 private:
  static const intptr_t kNumPrimitives = 1024;

  static PrimitiveFunction** primitive_table_;
};

}  // namespace psoup

#endif  // VM_PRIMITIVES_H_
