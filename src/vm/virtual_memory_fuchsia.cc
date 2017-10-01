// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/globals.h"
#if defined(OS_FUCHSIA)

#include "vm/virtual_memory.h"

#include <sys/stat.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "vm/assert.h"

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
  char* buffer = reinterpret_cast<char*>(malloc(size));
  if (buffer == 0) {
    FATAL1("Failed to malloc %" Pd " bytes\n", size);
  }

  size_t start = 0;
  size_t remaining = size;
  while (remaining > 0) {
    size_t bytes_read = fread(buffer + start, 1, remaining, file);
    if (bytes_read == 0) {
      FATAL1("Failed to read '%s'\n", filename);
    }
    start += bytes_read;
    remaining -= bytes_read;
  }

  int result = fclose(file);
  ASSERT(result == 0);

  uword base = reinterpret_cast<uword>(buffer);
  uword limit = base + size;
  return VirtualMemory(base, limit, ZX_HANDLE_INVALID);
}


VirtualMemory VirtualMemory::Allocate(intptr_t size,
                                      Protection protection,
                                      const char* name) {
  ASSERT(size > 0);
  uint32_t prot;
  switch (protection) {
    case kNoAccess:
      // ZX-426: zx_vmar_protect() requires at least one permission.
      prot = ZX_VM_FLAG_PERM_READ;
      break;
    case kReadOnly:
      prot = ZX_VM_FLAG_PERM_READ;
      break;
    case kReadWrite:
      prot = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
      break;
    default:
      UNREACHABLE();
      prot = 0;
  }

  zx_handle_t vmar = ZX_HANDLE_INVALID;
  uint32_t flags = ZX_VM_FLAG_CAN_MAP_READ | ZX_VM_FLAG_CAN_MAP_WRITE;
  uword vmar_addr = 0;
  zx_status_t status = zx_vmar_allocate(zx_vmar_root_self(), 0, size,
                                        flags, &vmar, &vmar_addr);
  if (status != ZX_OK) {
    FATAL2("zx_vmar_allocate(%" Pd ") failed: %s\n", size,
            zx_status_get_string(status));
  }

  zx_handle_t vmo = ZX_HANDLE_INVALID;
  status = zx_vmo_create(size, 0u, &vmo);
  if (status != ZX_OK) {
    FATAL2("zx_vmo_create(%" Pd ") failed: %s\n", size,
            zx_status_get_string(status));
  }

  if (name != NULL) {
    zx_object_set_property(vmo, ZX_PROP_NAME, name, strlen(name));
  }

  uintptr_t vmo_addr;
  status = zx_vmar_map(vmar, 0, vmo, 0, size, prot, &vmo_addr);
  if (status != ZX_OK) {
    FATAL2("zx_vmar_map(%" Pd ") failed: %s\n", size,
            zx_status_get_string(status));
  }
  ASSERT(vmar_addr == vmo_addr);
  zx_handle_close(vmo);

  uword base = reinterpret_cast<uword>(vmo_addr);
  uword limit = base + size;
  return VirtualMemory(base, limit, vmar);
}


void VirtualMemory::Free() {
  zx_handle_t vmar = static_cast<zx_handle_t>(handle_);
  zx_status_t status = zx_vmar_destroy(vmar);
  if (status != ZX_OK) {
    FATAL1("zx_vmar_destroy failed: %s\n", zx_status_get_string(status));
  }
  status = zx_handle_close(vmar);
  if (status != ZX_OK) {
    FATAL1("zx_handle_close failed: %s\n", zx_status_get_string(status));
  }
}


bool VirtualMemory::Protect(Protection protection) {
  uint32_t prot;
  switch (protection) {
    case kNoAccess:
      // MG-426: zx_vmar_protect() requires at least one permission.
      prot = ZX_VM_FLAG_PERM_READ;
      break;
    case kReadOnly:
      prot = ZX_VM_FLAG_PERM_READ;
      break;
    case kReadWrite:
      prot = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
      break;
    default:
      UNREACHABLE();
      prot = 0;
  }
  zx_handle_t vmar = static_cast<zx_handle_t>(handle_);
  ASSERT(vmar != ZX_HANDLE_INVALID);

  intptr_t size = limit_ - base_;
  zx_status_t status = zx_vmar_protect(vmar, base_, size, prot);
  if (status != ZX_OK) {
    FATAL1("zx_vmar_protect failed: %s\n", zx_status_get_string(status));
  }
  return true;
}

}  // namespace psoup

#endif  // defined(OS_FUCHSIA)
