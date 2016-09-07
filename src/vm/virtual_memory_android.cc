// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(TARGET_OS_ANDROID)

#include "vm/virtual_memory.h"

#include <sys/mman.h>
#include <sys/stat.h>

#include "vm/assert.h"
#include "vm/os.h"

namespace psoup {

VirtualMemory VirtualMemory::MapReadOnly(const char* filename) {
  FILE* file = fopen(filename, "r");
  if (file == NULL) {
    FATAL1("Failed to open '%s'\n", filename);
  }
  struct stat st;
  if (fstat(fileno(file), &st) != 0) {
    FATAL1("Failed to stat '%s'\n", filename);
  }
  intptr_t size = st.st_size;
  void* address = mmap(0, size, PROT_READ,
                       MAP_FILE | MAP_PRIVATE,
                       fileno(file), 0);
  if (address == MAP_FAILED) {
    FATAL1("Failed to mmap '%s'\n", filename);
  }
  int result = fclose(file);
  ASSERT(result == 0);

  uword base = reinterpret_cast<uword>(address);
  uword limit = base + size;
  return VirtualMemory(base, limit);
}


VirtualMemory VirtualMemory::Allocate(intptr_t size, Protection protection) {
  ASSERT(size > 0);
  int prot;
  switch (protection) {
    case kNone: prot = PROT_NONE; break;
    case kReadOnly: prot = PROT_READ; break;
    case kReadWrite: prot = PROT_READ | PROT_WRITE; break;
    default:
     UNREACHABLE();
     prot = 0;
  }

  void* address = mmap(0, size, prot,
                       MAP_PRIVATE | MAP_ANON,
                       0, 0);
  if (address == MAP_FAILED) {
    FATAL1("Failed to mmap %" Pd " bytes\n", size);
  }

  uword base = reinterpret_cast<uword>(address);
  uword limit = base + size;
  return VirtualMemory(base, limit);
}


void VirtualMemory::Free() {
  intptr_t size = limit_ - base_;
  int result = munmap(reinterpret_cast<void*>(base_), size);
  if (result != 0) {
    FATAL1("Failed to munmap %" Pd " bytes\n", size);
  }
}


bool VirtualMemory::Protect(Protection protection) {
  int prot;
  switch (protection) {
    case kNone: prot = PROT_NONE; break;
    case kReadOnly: prot = PROT_READ; break;
    case kReadWrite: prot = PROT_READ | PROT_WRITE; break;
    default:
     UNREACHABLE();
     prot = 0;
  }

  intptr_t size = limit_ - base_;
  int result = mprotect(reinterpret_cast<void*>(base_), size, prot);
  return result == 0;
}

}  // namespace psoup

#endif  // defined(TARGET_OS_LINUX)
