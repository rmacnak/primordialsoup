// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(OS_WINDOWS)

#include "vm/virtual_memory.h"

#include "vm/assert.h"
#include "vm/os.h"

namespace psoup {

VirtualMemory VirtualMemory::MapReadOnly(const char* filename) {
  HANDLE file = CreateFile(filename,
                           GENERIC_READ,
                           FILE_SHARE_READ,
                           nullptr,
                           OPEN_EXISTING,
                           0,
                           nullptr);
  if (file == nullptr) {
    FATAL("Failed to open '%s'\n", filename);
  }
  BY_HANDLE_FILE_INFORMATION stat;
  bool r = GetFileInformationByHandle(file, &stat);
  ASSERT(r);
  int64_t size = (static_cast<int64_t>(stat.nFileSizeHigh) << 32) |
      stat.nFileSizeLow;
  HANDLE mapping = CreateFileMapping(file, nullptr, PAGE_READONLY, 0, 0,
                                     nullptr);
  if (mapping == nullptr) {
    FATAL("Failed CreateFileMapping\n");
  }
  void* address = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, size);
  if (address == 0) {
    FATAL("Failed MapViewOfFile\n");
  }
  r = CloseHandle(file);
  ASSERT(r);

  return VirtualMemory(address, size);
}

VirtualMemory VirtualMemory::Allocate(size_t size,
                                      Protection protection,
                                      const char* name) {
  DWORD prot;
  switch (protection) {
    case kNoAccess: prot = PAGE_NOACCESS; break;
    case kReadOnly: prot = PAGE_READONLY; break;
    case kReadWrite: prot = PAGE_READWRITE; break;
    default:
      UNREACHABLE();
      prot = 0;
  }

  void* address = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, prot);
  if (address == nullptr) {
    FATAL("Failed to VirtualAlloc %" Pd " bytes\n", size);
  }

  return VirtualMemory(address, size);
}

void VirtualMemory::Free() {
  if (VirtualFree(address_, 0, MEM_RELEASE) == 0) {
    FATAL("VirtualFree failed %d", GetLastError());
  }
}

bool VirtualMemory::Protect(Protection protection) {
  DWORD prot;
  switch (protection) {
    case kNoAccess: prot = PAGE_NOACCESS; break;
    case kReadOnly: prot = PAGE_READONLY; break;
    case kReadWrite: prot = PAGE_READWRITE; break;
    default:
      UNREACHABLE();
      prot = 0;
  }
  DWORD old_prot = 0;
  bool result = VirtualProtect(address_, size_, prot, &old_prot);
  return result;
}

}  // namespace psoup

#endif  // defined(OS_WINDOWS)
