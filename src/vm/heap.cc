// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/heap.h"

#include "vm/lookup_cache.h"

namespace psoup {

Heap::Heap(Isolate* isolate) :
    to_(),
    from_(),
    scan_(0),
    ephemeron_list_(NULL),
    weak_list_(NULL),
    class_table_(NULL),
    class_table_capacity_(0),
    class_table_top_(0),
#if WEAK_CLASS_TABLE
    class_table_free_(0),
#endif
    object_store_(NULL),
    current_activation_(NULL),
#if RECYCLE_ACTIVATIONS
    recycle_list_(NULL),
#endif
#if LOOKUP_CACHE
    lookup_cache_(NULL),
#endif
    handles_(),
    handles_top_(0),
    identity_hash_random_(OS::CurrentMonotonicMicros()),
    isolate_(isolate) {
  to_.Allocate(kInitialSemispaceSize);
  from_.Allocate(kInitialSemispaceSize);

  // Class table.
#if WEAK_CLASS_TABLE
  class_table_capacity_ = 1028;
#else
  class_table_capacity_ = 4096;
#endif
  class_table_ = new Object*[class_table_capacity_];
  for (intptr_t i = 0; i < kFirstRegularObjectCid; i++) {
    class_table_[i] = reinterpret_cast<Object*>(kZapWord);
  }
  class_table_top_ = kFirstRegularObjectCid;
}


Heap::~Heap() {
  to_.Free();
  from_.Free();
  delete[] class_table_;
}


void Heap::Grow(intptr_t size_requested, const char* reason) {
  intptr_t current_size = to_.size();
  intptr_t new_size = current_size * 2;
  while ((new_size - current_size) < size_requested) {
    new_size *= 2;
  }
  OS::PrintErr("Growing heap to %" Pd "MB (%s)\n",
               new_size / MB, reason);
  if (new_size > kMaxSemispaceSize) {
    FATAL("Growing really big. Runaway recursion?");
  }
  from_.Free();
  from_.Allocate(new_size);
  Scavenge();
}


void Heap::Scavenge() {
  int64_t start = OS::CurrentMonotonicMicros();
  intptr_t size_before = used();

  if (REPORT_GC) {
    OS::PrintErr("Begin scavenge (%" Pd "kB used)\n", size_before / KB);
  }

  FlipSpaces();

#ifdef DEBUG
  to_.ReadWrite();
#endif

  // Strong references.
  ProcessRoots();
#if !WEAK_CLASS_TABLE
  ProcessClassTableStrong();
#endif

  while (scan_ < to_.top_) {
    ProcessToSpace();
    ASSERT(scan_ == to_.top_);
    ProcessEphemeronListScavenge();
  }
  ASSERT(scan_ == to_.top_);

  // Weak references.
  ProcessEphemeronListMourn();
  ASSERT(ephemeron_list_ == 0);

  ProcessWeakList();
  ASSERT(weak_list_ == 0);

#if WEAK_CLASS_TABLE
  ProcessClassTableWeak();
#endif

#if LOOKUP_CACHE
  if (lookup_cache_ != NULL) {
    // TODO(rmacnak): null only from deserialization scavenge?
    lookup_cache_->Clear();
  }
#endif
#if RECYCLE_ACTIVATIONS
  recycle_list_ = 0;
#endif

#ifdef DEBUG
  from_.Zap();
  from_.NoAccess();
#endif

  intptr_t size_after = used();
  int64_t stop = OS::CurrentMonotonicMicros();
  intptr_t time = stop - start;
  if (REPORT_GC) {
    OS::PrintErr("End scavenge (%" Pd "kB used, %" Pd "kB freed, %" Pd " us)\n",
                 size_after / KB, (size_before - size_after) / KB, time);
  }

  if (used() > (7 * to_.size() / 8)) {
    // Grow before actually filling up the current capacity to avoid
    // many GCs that don't free much memory as the capacity is approached.
    Grow(to_.size(), "early growth heuristic");
  }
}


void Heap::FlipSpaces() {
  Semispace temp = to_;
  to_ = from_;
  from_ = temp;

  if (to_.size() < from_.size()) {
    // This is the scavenge after a grow. Resize the other space.
    to_.Free();
    to_.Allocate(from_.size());
  }

  to_.ResetTop();
  ASSERT((to_.top_ & kObjectAlignmentMask) == kNewObjectAlignmentOffset);
  scan_ = to_.top_;

  ephemeron_list_ = 0;
}


static void ForwardPointer(Object** p) {
  Object* old_target = *p;
  if (old_target->IsForwardingCorpse()) {
    Object* new_target =
        reinterpret_cast<ForwardingCorpse*>(old_target)->target();
    ASSERT(!new_target->IsForwardingCorpse());
    *p = new_target;
  }
}


void Heap::ProcessRoots() {
  ScavengePointer(reinterpret_cast<Object**>(&object_store_));
  ScavengePointer(&current_activation_);

  for (intptr_t i = 0; i < handles_top_; i++) {
    ScavengePointer(handles_[i]);
  }
}


void Heap::ForwardRoots() {
  ForwardPointer(reinterpret_cast<Object**>(&object_store_));
  ForwardPointer(&current_activation_);

  for (intptr_t i = 0; i < handles_top_; i++) {
    ForwardPointer(handles_[i]);
  }
}


void Heap::ProcessToSpace() {
  while (scan_ < to_.top_) {
    Object* obj = Object::FromAddr(scan_);
    intptr_t cid = obj->cid();
    ScavengeClass(cid);
    if (cid == kWeakArrayCid) {
      // Processed later.
    } else if (cid == kEphemeronCid) {
      // Processed later.
    } else {
      Object** from;
      Object** to;
      obj->Pointers(&from, &to);
      for (Object** p = from; p <= to; p++) {
        ScavengePointer(p);
      }
    }
    scan_ += obj->HeapSize();
  }
}


void Heap::ForwardToSpace() {
  uword scan = to_.object_start();
  while (scan < to_.top_) {
    Object* obj = Object::FromAddr(scan);
    Object** from;
    Object** to;
    obj->Pointers(&from, &to);
    for (Object** p = from; p <= to; p++) {
      ForwardPointer(p);
    }
    scan += obj->HeapSize();
  }
}


#if !WEAK_CLASS_TABLE
void Heap::ProcessClassTableStrong() {
  for (intptr_t i = kFirstLegalCid; i < class_table_top_; i++) {
    ScavengePointer(&class_table_[i]);
  }
}
#endif



void Heap::ForwardClassTable() {
  for (intptr_t i = kFirstLegalCid; i < class_table_top_; i++) {
    ForwardPointer(&class_table_[i]);
  }
}


static bool IsForwarded(uword addr) {
  ASSERT(Utils::IsAligned(addr, kWordSize));  // Untagged pointer.
  uword header = *reinterpret_cast<uword*>(addr);
  return header & (1 << kMarkBit);
}


static Object* ForwardingTarget(uword addr) {
  ASSERT(IsForwarded(addr));
  uword header = *reinterpret_cast<uword*>(addr);
  // Mark bit and tag bit are conveniently in the same place.
  ASSERT((header & kSmiTagMask) == kHeapObjectTag);
  return reinterpret_cast<Object*>(header);
}


static void SetForwarded(uword old_addr, uword new_addr) {
  ASSERT(!IsForwarded(old_addr));
  uword forwarding_header = new_addr | (1 << kMarkBit);
  *reinterpret_cast<uword*>(old_addr) = forwarding_header;
}


#if WEAK_CLASS_TABLE
void Heap::ProcessClassTableWeak() {
  for (intptr_t i = kFirstLegalCid; i < class_table_top_; i++) {
    Object** p = &class_table_[i];

    Object* old_target = *p;
    if (old_target->IsImmediateOrOldObject()) {
      continue;
    }

    uword old_target_addr = old_target->Addr();
    ASSERT(InFromSpace(old_target_addr));

    Object* new_target;
    if (IsForwarded(old_target_addr)) {
      new_target = ForwardingTarget(old_target_addr);
      uword new_target_addr = new_target->Addr();
      ASSERT(InToSpace(new_target_addr));
    } else {
      new_target = SmallInteger::New(class_table_free_);
      class_table_free_ = i;
    }

    *p = new_target;
  }
}
#endif


void Heap::ScavengePointer(Object** p) {
  Object* old_target = *p;

  if (old_target->IsImmediateOrOldObject()) {
    // Target isn't gonna move.
    return;
  }

  uword old_target_addr = old_target->Addr();
  ASSERT(InFromSpace(old_target_addr));

  Object* new_target;
  if (IsForwarded(old_target_addr)) {
    new_target = ForwardingTarget(old_target_addr);
  } else {
    // Target is now known to be reachable. Move it to to-space.
    intptr_t cid = old_target->cid();
    ASSERT(cid != kForwardingCorpseCid);
    uword size = old_target->HeapSize();
    ASSERT((size & kObjectAlignmentMask) == 0);
    uword new_target_addr = TryAllocate(size);
    ASSERT(new_target_addr != 0);
    memcpy(reinterpret_cast<void*>(new_target_addr),
           reinterpret_cast<void*>(old_target_addr),
           size);
    SetForwarded(old_target_addr, new_target_addr);

    if (cid == kEphemeronCid) {
      AddToEphemeronList(static_cast<Ephemeron*>(old_target));
    } else if (cid == kWeakArrayCid) {
      AddToWeakList(static_cast<WeakArray*>(old_target));
    }

    new_target = Object::FromAddr(new_target_addr);
  }

  uword new_target_addr = new_target->Addr();
  ASSERT(InToSpace(new_target_addr));

  *p = new_target;
}


void Heap::AddToEphemeronList(Ephemeron* corpse) {
  ASSERT(InFromSpace(corpse->Addr()));
  corpse->set_next(ephemeron_list_);
  ephemeron_list_ = corpse;
}


void Heap::ProcessEphemeronListScavenge() {
  Ephemeron* prev = 0;
  Ephemeron* corpse = ephemeron_list_;
  while (corpse != 0) {
    ASSERT(IsForwarded(corpse->Addr()));
    Ephemeron* survivor =
        static_cast<Ephemeron*>(ForwardingTarget(corpse->Addr()));
    ASSERT(survivor->IsEphemeron());

    if (IsForwarded(survivor->key()->Addr())) {
      // TODO(rmacnak): These scavenges potentially add to the ephemeron list
      // that we are in the middle of traversing. Add tests for ephemerons
      // only reachable from another ephemeron.
      ScavengePointer(survivor->key_ptr());
      ScavengePointer(survivor->value_ptr());
      ScavengePointer(survivor->finalizer_ptr());
      // Remove from list.
      Ephemeron* next = corpse->next();
      if (prev == 0) {
        ephemeron_list_ = next;
      } else {
        prev->set_next(next);
      }
      corpse = next;
    } else {
      // Keep on list.
      Ephemeron* next = corpse->next();
      prev = corpse;
      corpse = next;
    }
  }
}


void Heap::ProcessEphemeronListMourn() {
  Object* nil = object_store()->nil_obj();
  Ephemeron* corpse = ephemeron_list_;
  while (corpse != 0) {
    ASSERT(IsForwarded(corpse->Addr()));
    Ephemeron* survivor =
        static_cast<Ephemeron*>(ForwardingTarget(corpse->Addr()));
    ASSERT(survivor->IsEphemeron());

    ASSERT(InFromSpace(survivor->key()->Addr()));
    survivor->set_key(nil);
    survivor->set_value(nil);
    // TODO(rmacnak): Put the finalizer on a queue for the event loop
    // to process.
    survivor->set_finalizer(nil);

    corpse = corpse->next();
  }
  ephemeron_list_ = 0;
}


void Heap::AddToWeakList(WeakArray* corpse) {
  ASSERT(InFromSpace(corpse->Addr()));
  corpse->set_next(weak_list_);
  weak_list_ = corpse;
}


void Heap::ProcessWeakList() {
  WeakArray* corpse = weak_list_;
  while (corpse != 0) {
    ASSERT(IsForwarded(corpse->Addr()));
    WeakArray* survivor =
        static_cast<WeakArray*>(ForwardingTarget(corpse->Addr()));
    ASSERT(survivor->IsWeakArray());

    Object** from;
    Object** to;
    survivor->Pointers(&from, &to);
    for (Object** p = from; p <= to; p++) {
      ScavengeWeakPointer(p);
    }

    corpse = corpse->next();
  }
  weak_list_ = 0;
}


void Heap::ScavengeWeakPointer(Object** p) {
  Object* old_target = *p;

  if (old_target->IsImmediateOrOldObject()) {
    // Target isn't gonna move.
    return;
  }

  uword old_target_addr = old_target->Addr();
  ASSERT(InFromSpace(old_target_addr));

  Object* new_target;
  if (IsForwarded(old_target_addr)) {
    new_target = ForwardingTarget(old_target_addr);
  } else {
    // The object store and nil have already been scavenged.
    new_target = object_store()->nil_obj();
  }

  uword new_target_addr = new_target->Addr();
  ASSERT(InToSpace(new_target_addr));

  *p = new_target;
}


void Heap::ScavengeClass(intptr_t cid) {
  ASSERT(cid < class_table_top_);
  if (WEAK_CLASS_TABLE) {
    // This is very similar to ScavengePointer.

    Object* old_target = class_table_[cid];

    if (old_target->IsImmediateOrOldObject()) {
      // Target isn't gonna move.
      return;
    }

    uword old_target_addr = old_target->Addr();
    ASSERT(InFromSpace(old_target_addr));

    if (IsForwarded(old_target_addr)) {
      // Already scavenged.
      return;
    }

    // Target is now known to be reachable. Move it to to-space.
    intptr_t cid = old_target->cid();
    uword size = old_target->HeapSize();
    ASSERT((size & kObjectAlignmentMask) == 0);
    uword new_target_addr = TryAllocate(size);
    ASSERT(new_target_addr != 0);
    memcpy(reinterpret_cast<void*>(new_target_addr),
           reinterpret_cast<void*>(old_target_addr),
           size);
    SetForwarded(old_target_addr, new_target_addr);

    ASSERT(cid != kEphemeronCid);
    ASSERT(cid != kWeakArrayCid);
  }
}


intptr_t Heap::CountInstances(intptr_t cid) {
  intptr_t instances = 0;
  uword scan = to_.object_start();
  while (scan < to_.top_) {
    Object* obj = Object::FromAddr(scan);
    if (obj->cid() == cid) {
      instances++;
    }
    scan += obj->HeapSize();
  }
  return instances;
}


intptr_t Heap::CollectInstances(intptr_t cid, Array* array) {
  intptr_t instances = 0;
  uword scan = to_.object_start();
  while (scan < to_.top_) {
    Object* obj = Object::FromAddr(scan);
    if (obj->cid() == cid) {
      array->set_element(instances, obj);
      instances++;
    }
    scan += obj->HeapSize();
  }
  return instances;
}


bool Heap::BecomeForward(Array* old, Array* neu) {
  if (old->Size() != neu->Size()) {
    return false;
  }

  intptr_t len = old->Size();
  if (TRACE_BECOME) {
    OS::Print("become(%" Pd ")\n", len);
  }

  for (intptr_t i = 0; i < len; i++) {
    Object* forwarder = old->element(i);
    Object* forwardee = neu->element(i);
    if (forwarder->IsImmediateObject() ||
        forwardee->IsImmediateObject()) {
      return false;
    }
  }

  for (intptr_t i = 0; i < len; i++) {
    Object* forwarder = old->element(i);
    Object* forwardee = neu->element(i);

    ASSERT(!forwarder->IsForwardingCorpse());
    ASSERT(!forwardee->IsForwardingCorpse());

    forwardee->set_identity_hash(forwarder->identity_hash());

    intptr_t size = forwarder->HeapSize();

    Object::InitializeObject(forwarder->Addr(),
                             kForwardingCorpseCid,
                             size);
    ASSERT(forwarder->IsForwardingCorpse());
    ForwardingCorpse* corpse = reinterpret_cast<ForwardingCorpse*>(forwarder);
    if (forwarder->heap_size() == 0) {
      corpse->set_overflow_size(size);
    }
    ASSERT(forwarder->HeapSize() == size);

    corpse->set_target(forwardee);
  }

  ForwardRoots();
  ForwardClassTable();
  ForwardToSpace();

#if LOOKUP_CACHE
  lookup_cache_->Clear();
#endif
#if RECYCLE_ACTIVATIONS
  recycle_list_ = 0;
#endif

  return true;
}


static void PrintStringError(ByteString* string) {
  const char* cstr = reinterpret_cast<const char*>(string->element_addr(0));
  OS::PrintErr("%.*s", static_cast<int>(string->Size()), cstr);
}


void Heap::PrintStack() {
  Activation* act = activation();
  while (act != object_store()->nil_obj()) {
    OS::PrintErr("  ");

    Activation* home = act;
    while (home->closure() != object_store()->nil_obj()) {
      ASSERT(home->closure()->IsClosure());
      OS::PrintErr("[] in ");
      home = home->closure()->defining_activation();
    }

    AbstractMixin* receiver_mixin = home->receiver()->Klass(this)->mixin();
    ByteString* receiver_mixin_name = receiver_mixin->name();
    if (receiver_mixin_name->IsByteString()) {
      PrintStringError(receiver_mixin_name);
    } else {
      receiver_mixin_name =
          reinterpret_cast<AbstractMixin*>(receiver_mixin_name)->name();
      ASSERT(receiver_mixin_name->IsByteString());
      PrintStringError(receiver_mixin_name);
      OS::PrintErr(" class");
    }

    AbstractMixin* method_mixin = home->method()->mixin();
    if (receiver_mixin != method_mixin) {
      ByteString* method_mixin_name = method_mixin->name();
      OS::Print("(");
      if (method_mixin_name->IsByteString()) {
        PrintStringError(method_mixin_name);
      } else {
        method_mixin_name =
            reinterpret_cast<AbstractMixin*>(method_mixin_name)->name();
        ASSERT(method_mixin_name->IsByteString());
        PrintStringError(method_mixin_name);
        OS::PrintErr(" class");
      }
      OS::Print(")");
    }

    ByteString* method_name = home->method()->selector();
    OS::PrintErr(" ");
    PrintStringError(method_name);
    OS::PrintErr("\n");

    act = act->sender();
  }
}

}  // namespace psoup
