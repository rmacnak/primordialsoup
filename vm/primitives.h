// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_PRIMITIVES_H_
#define VM_PRIMITIVES_H_

#include "vm/assert.h"
#include "vm/globals.h"

namespace psoup {

class Heap;
class Interpreter;

typedef bool(PrimitiveFunction)(intptr_t num_args,
                                Heap* heap,
                                Interpreter* interpreter);

class Primitives {
 public:
  static void Startup();
  static void Shutdown();

  static bool IsUnwindProtect(intptr_t prim) { return prim == 162; }
  static bool IsSimulationRoot(intptr_t prim) { return prim == 163; }

  static bool Invoke(intptr_t prim,
                     intptr_t num_args,
                     Heap* heap,
                     Interpreter* interpreter) {
    ASSERT(prim > 0);
    ASSERT(prim < kNumPrimitives);
    PrimitiveFunction* func = primitive_table_[prim];
    return func(num_args, heap, interpreter);
  }

 private:
  static constexpr intptr_t kNumPrimitives = 512;

  static PrimitiveFunction* primitive_table_[kNumPrimitives];
};

}  // namespace psoup

#endif  // VM_PRIMITIVES_H_
