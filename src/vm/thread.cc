// Copyright (c) 2015, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/thread.h"

#include "vm/lockers.h"
#include "vm/os_thread.h"

namespace psoup {

Thread::~Thread() {
  // We should cleanly exit any isolate before destruction.
  ASSERT(isolate_ == NULL);
}


Thread::Thread(Isolate* isolate)
    : BaseThread(false),
      isolate_(NULL),
      os_thread_(NULL),
      next_(NULL) {
}

}  // namespace psoup
