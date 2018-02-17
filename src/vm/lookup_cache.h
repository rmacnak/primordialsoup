// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_LOOKUP_CACHE_H_
#define VM_LOOKUP_CACHE_H_

#include "vm/globals.h"
#include "vm/object.h"
#include "vm/primitives.h"

namespace psoup {

class CacheEntry {
 public:
  intptr_t cid;
  String* selector;
  Method* target;
  PrimitiveFunction* primitive;
};

enum LookupRule {
  kSelf = 0,
  kSuper = 256,
  kImplicitReceiver = 257,
  kMNU = 258,
};

class NSCacheEntry {
 public:
  intptr_t cid;
  String* selector;
  Method* caller;
  intptr_t rule;
  Object* absent_receiver;
  Method* target;
  PrimitiveFunction* primitive;
  uword alignment_;
};

class LookupCache {
 public:
  LookupCache() {
    Clear();
  }

  bool LookupOrdinary(intptr_t cid,
                      String* selector,
                      Method** target,
                      PrimitiveFunction** primitive) {
    intptr_t hash =
        cid ^ (reinterpret_cast<intptr_t>(selector) >> kObjectAlignmentLog2);
    intptr_t probe1 = hash & kCacheMask;
    if (entries[probe1].cid == cid &&
        entries[probe1].selector == selector) {
      *target = entries[probe1].target;
      return true;
    }

    intptr_t probe2 = (hash >> 3) & kCacheMask;
    if (entries[probe2].cid == cid &&
        entries[probe2].selector == selector) {
      *target = entries[probe2].target;
      return true;
    }

    return false;
  }

  void InsertOrdinary(intptr_t cid,
                      String* selector,
                      Method* target,
                      PrimitiveFunction* primitive);

  bool LookupNS(intptr_t cid,
                String* selector,
                Method* caller,
                intptr_t rule,
                Object** absent_receiver,
                Method** target,
                PrimitiveFunction** primitive) {
    intptr_t hash = cid
      ^ (reinterpret_cast<intptr_t>(selector) >> kObjectAlignmentLog2)
      ^ (reinterpret_cast<intptr_t>(caller) >> kObjectAlignmentLog2)
      ^ rule;

    intptr_t probe1 = hash & kNSCacheMask;
    if (ns_entries[probe1].cid == cid &&
        ns_entries[probe1].selector == selector &&
        ns_entries[probe1].caller == caller &&
        ns_entries[probe1].rule == rule) {
      *absent_receiver = ns_entries[probe1].absent_receiver;
      *target = ns_entries[probe1].target;
      return true;
    }

    intptr_t probe2 = (hash >> 3) & kNSCacheMask;
    if (ns_entries[probe2].cid == cid &&
        ns_entries[probe2].selector == selector &&
        ns_entries[probe2].caller == caller &&
        ns_entries[probe2].rule == rule) {
      *absent_receiver = ns_entries[probe2].absent_receiver;
      *target = ns_entries[probe2].target;
      return true;
    }

    return false;
  }

  bool InsertNS(intptr_t cid,
                String* selector,
                Method* caller,
                intptr_t rule,
                Object* absent_receiver,
                Method* target,
                PrimitiveFunction* primitive);

  void Clear();

 private:
  static const intptr_t kCacheSize = 256;
  static const intptr_t kCacheMask = kCacheSize - 1;

  static const intptr_t kNSCacheSize = 128;
  static const intptr_t kNSCacheMask = kNSCacheSize - 1;

  CacheEntry entries[kCacheSize];
  NSCacheEntry ns_entries[kNSCacheSize];
};

}  // namespace psoup

#endif  // VM_LOOKUP_CACHE_H_
