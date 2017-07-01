// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_VIRTUAL_MEMORY_H_
#define VM_VIRTUAL_MEMORY_H_

#include "vm/globals.h"

namespace psoup {

class VirtualMemory {
 public:
  enum Protection {
    kNoAccess,
    kReadOnly,
    kReadWrite,
  };

  static VirtualMemory MapReadOnly(const char* filename);
  static VirtualMemory Allocate(intptr_t size,
                                Protection protection,
                                const char* name);
  void Free();
  bool Protect(Protection protection);

  uword base() const { return base_; }
  uword limit() const { return limit_; }
  intptr_t size() const { return limit_ - base_; }

  VirtualMemory() : base_(0), limit_(0), handle_(0) { }

 private:
  VirtualMemory(uword base, uword limit, int32_t handle = 0)
     : base_(base), limit_(limit), handle_(handle) { }

  uword base_;
  uword limit_;
  int32_t handle_;
};

}  // namespace psoup

#endif  // VM_VIRTUAL_MEMORY_H_
