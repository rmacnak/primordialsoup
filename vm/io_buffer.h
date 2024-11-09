// Copyright (c) 2024, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_IO_BUFFER_H_
#define VM_IO_BUFFER_H_

#include "vm/assert.h"
#include "vm/globals.h"

namespace psoup {

enum Operation {
  kInvalidOperation,
  kReadOperation,
  kWriteOperation,
  kCancelledOperation,
};

class IOBuffer {
 public:
  static IOBuffer* Allocate(Operation op) {
    return Allocate(op, 8 * KB);
  }
  static IOBuffer* Allocate(Operation op, size_t buffer_size) {
    return new (buffer_size) IOBuffer(op, buffer_size);
  }
  static void Dispose(IOBuffer* buffer) {
    delete buffer;
  }

  static IOBuffer* FromOverlapped(OVERLAPPED* overlapped) {
    return CONTAINING_RECORD(overlapped, IOBuffer, overlapped_);
  }

  Operation operation() const { return operation_; }
  void Cancel() { operation_ = kCancelledOperation; }

  uint8_t* buffer() { return buffer_; }
  size_t buffer_size() const { return buffer_size_; }

  size_t Read(void* buffer, size_t size) {
    ASSERT(operation_ == kReadOperation);
    if (size > read_available_) {
      size = read_available_;
    }
    memcpy(buffer, buffer_ + read_offset_, size);
    read_offset_ += size;
    read_available_ -= size;
    return size;
  }
  void ReadComplete(size_t size) {
    ASSERT(operation_ == kReadOperation);
    ASSERT(read_offset_ == 0);
    ASSERT(read_available_ == 0);
    read_available_ = size;
  }
  size_t read_available() const { return read_available_; }

  OVERLAPPED* overlapped() { return &overlapped_; }
  OVERLAPPED* GetCleanOverlapped() {
    memset(&overlapped_, 0, sizeof(overlapped_));
    return &overlapped_;
  }

 private:
  void* operator new(size_t size, size_t buffer_size) {
    return malloc(size + buffer_size);
  }
  void operator delete(void* buffer) { free(buffer); }

  IOBuffer(Operation operation, size_t buffer_size)
    : operation_(operation),
      buffer_size_(buffer_size) {}

  Operation operation_;
  size_t buffer_size_;
  size_t read_offset_ = 0;
  size_t read_available_ = 0;
  OVERLAPPED overlapped_;
  uint8_t buffer_[];

  DISALLOW_COPY_AND_ASSIGN(IOBuffer);
};

}  // namespace psoup

#endif  // VM_IO_BUFFER_H_
