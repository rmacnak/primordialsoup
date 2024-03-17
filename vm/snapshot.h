// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_SNAPSHOT_H_
#define VM_SNAPSHOT_H_

#include "vm/globals.h"

namespace psoup {

class Heap;

// Reads a variant of VictoryFuel.
void Deserialize(Heap* heap, const void* snapshot, size_t snapshot_length);

}  // namespace psoup

#endif  // VM_SNAPSHOT_H_
