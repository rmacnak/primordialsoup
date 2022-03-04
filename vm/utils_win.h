// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_UTILS_WIN_H_
#define VM_UTILS_WIN_H_

namespace psoup {

inline char* Utils::StrError(int err, char* buffer, size_t bufsize) {
  DWORD message_size =
      FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    buffer, static_cast<DWORD>(bufsize), NULL);
  if (message_size == 0) {
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
      snprintf(buffer, bufsize,
               "FormatMessage failed for error code %d (error %d)\n",
               err, GetLastError());
    }
    snprintf(buffer, bufsize, "OS Error %d", err);
  }
  // Ensure string termination.
  buffer[bufsize - 1] = 0;
  return buffer;
}

}  // namespace psoup

#endif  // VM_UTILS_WIN_H_
