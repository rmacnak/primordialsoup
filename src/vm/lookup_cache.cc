// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/lookup_cache.h"

namespace psoup {

void LookupCache::InsertOrdinary(intptr_t cid,
                                 String* selector,
                                 Method* target,
                                 PrimitiveFunction* primitive) {
  intptr_t hash =
      cid ^ (reinterpret_cast<intptr_t>(selector) >> kObjectAlignmentLog2);
  intptr_t probe1 = hash & kCacheMask;
  if (entries[probe1].cid == kIllegalCid) {
    entries[probe1].cid = cid;
    entries[probe1].selector = selector;
    entries[probe1].target = target;
    return;
  }

  intptr_t probe2 = (hash >> 3) & kCacheMask;
  if (entries[probe2].cid == kIllegalCid) {
    entries[probe2].cid = cid;
    entries[probe2].selector = selector;
    entries[probe2].target = target;
    return;
  }

  // TODO(rmacnak): write to probe1 ?
}


bool LookupCache::InsertNS(intptr_t cid,
                           String* selector,
                           Method* caller,
                           intptr_t rule,
                           Object* absent_receiver,
                           Method* target,
                           PrimitiveFunction* primitive) {
  intptr_t hash = cid
    ^ (reinterpret_cast<intptr_t>(selector) >> kObjectAlignmentLog2)
    ^ (reinterpret_cast<intptr_t>(caller) >> kObjectAlignmentLog2)
    ^ rule;

  intptr_t probe1 = hash & kNSCacheMask;
  if (ns_entries[probe1].cid == kIllegalCid) {
    ns_entries[probe1].cid = cid;
    ns_entries[probe1].selector = selector;
    ns_entries[probe1].caller = caller;
    ns_entries[probe1].rule = rule;
    ns_entries[probe1].target = target;
    ns_entries[probe1].absent_receiver = absent_receiver;
    return true;
  }

  intptr_t probe2 = (hash >> 3) & kNSCacheMask;
  if (ns_entries[probe2].cid == kIllegalCid) {
    ns_entries[probe2].cid = cid;
    ns_entries[probe2].selector = selector;
    ns_entries[probe2].caller = caller;
    ns_entries[probe2].rule = rule;
    ns_entries[probe2].target = target;
    ns_entries[probe2].absent_receiver = absent_receiver;
    return true;
  }

  // TODO(rmacnak): write to probe 1?

  return false;
}


void LookupCache::Clear() {
  for (intptr_t i = 0; i < kCacheSize; i++) {
    entries[i].cid = kIllegalCid;
  }
  for (intptr_t i = 0; i < kNSCacheSize; i++) {
    ns_entries[i].cid = kIllegalCid;
  }
}

}  // namespace psoup
