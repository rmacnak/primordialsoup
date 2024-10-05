// Copyright (c) 2024, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/random.h"

#include "vm/assert.h"
#include "vm/os.h"

namespace psoup {

Random::Random() {
  int result = OS::GetEntropy(&state_, sizeof(state_));
  if (result != 0) {
    const size_t kBufferSize = 256;                                     \
    char error_message[kBufferSize];                                    \
    FATAL("GetEntropy error: %d (%s)", result,                          \
          OS::StrError(result, error_message, kBufferSize));            \
  }
}

}  // namespace psoup
