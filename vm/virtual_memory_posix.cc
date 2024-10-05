// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(OS_ANDROID) || defined(OS_MACOS) || defined(OS_LINUX)

#include "vm/virtual_memory.h"

#include <sys/mman.h>
#include <sys/stat.h>

#if defined(OS_ANDROID) || defined(OS_LINUX)
#include <sys/prctl.h>
#endif

#include "vm/assert.h"
#include "vm/os.h"

namespace psoup {

MappedMemory MappedMemory::MapReadOnly(const char* filename) {
  FILE* file = fopen(filename, "r");
  if (file == nullptr) {
    FATAL("Failed to open '%s'\n", filename);
  }
  struct stat st;
  if (fstat(fileno(file), &st) != 0) {
    FATAL("Failed to stat '%s'\n", filename);
  }
  intptr_t size = st.st_size;
  void* address = mmap(nullptr, size, PROT_READ, MAP_FILE | MAP_PRIVATE,
                       fileno(file), 0);
  if (address == MAP_FAILED) {
    FATAL("Failed to mmap '%s'\n", filename);
  }
  int result = fclose(file);
  ASSERT(result == 0);

  return MappedMemory(address, size);
}

void MappedMemory::Free() {
  int result = munmap(address_, size_);
  if (result != 0) {
    FATAL("Failed to munmap %" Pd " bytes\n", size_);
  }
}

VirtualMemory VirtualMemory::Allocate(size_t size,
                                      Protection protection,
                                      const char* name) {
  int prot;
  switch (protection) {
    case kNoAccess: prot = PROT_NONE; break;
    case kReadOnly: prot = PROT_READ; break;
    case kReadWrite: prot = PROT_READ | PROT_WRITE; break;
    default:
      UNREACHABLE();
      prot = 0;
  }

  void* address = mmap(nullptr, size, prot, MAP_PRIVATE | MAP_ANON, 0, 0);
  if (address == MAP_FAILED) {
    FATAL("Failed to mmap %" Pd " bytes\n", size);
  }

#if defined(OS_ANDROID) || defined(OS_LINUX)
  // PR_SET_VMA was only added to mainline Linux in 5.17, and some versions of
  // the Android NDK have incorrect headers, so we manually define it if absent.
#if !defined(PR_SET_VMA)
#define PR_SET_VMA 0x53564d41
#endif
#if !defined(PR_SET_VMA_ANON_NAME)
#define PR_SET_VMA_ANON_NAME 0
#endif
  prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, address, size, name);
#endif

  return VirtualMemory(address, size);
}

void VirtualMemory::Free() {
  int result = munmap(address_, size_);
  if (result != 0) {
    FATAL("Failed to munmap %" Pd " bytes\n", size_);
  }
}

bool VirtualMemory::Protect(Protection protection) {
  int prot;
  switch (protection) {
    case kNoAccess: prot = PROT_NONE; break;
    case kReadOnly: prot = PROT_READ; break;
    case kReadWrite: prot = PROT_READ | PROT_WRITE; break;
    default:
      UNREACHABLE();
      prot = 0;
  }

  int result = mprotect(address_, size_, prot);
  return result == 0;
}

}  // namespace psoup

#endif  // defined(OS_ANDROID) || defined(OS_MACOS) || defined(OS_LINUX)
