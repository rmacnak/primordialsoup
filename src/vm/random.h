// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_RANDOM_H_
#define VM_RANDOM_H_

namespace psoup {

// George Marsaglia. "Xorshift RNGs." Journal of Statistical Software. 2003.
class Random {
 public:
  explicit Random(uint32_t seed) {
    x = 123456789;
    y = 362436069;
    z = 521288629;
    w = 88675123 ^ seed;
  }

  uint32_t NextUInt32() {
    uint32_t t = (x ^ (x << 11));
    x = y;
    y = z;
    z = w;
    w = (w ^ (w >> 19)) ^ (t ^ (t >> 8));
    return w;
  }

 private:
  uint32_t x, y, z, w;
};

}  // namespace psoup

#endif  // VM_RANDOM_H_
