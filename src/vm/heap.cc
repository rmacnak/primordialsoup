// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/heap.h"

#include "vm/lookup_cache.h"
#include "vm/os.h"

namespace psoup {

Heap::Heap() :
    top_(0),
    end_(0),
    to_(),
    from_(),
    next_semispace_capacity_(kInitialSemispaceCapacity),
    class_table_(NULL),
    class_table_size_(0),
    class_table_capacity_(0),
    class_table_free_(0),
    object_store_(NULL),
    current_activation_(NULL),
    handles_(),
    handles_size_(0),
#if RECYCLE_ACTIVATIONS
    recycle_list_(NULL),
#endif
#if LOOKUP_CACHE
    lookup_cache_(NULL),
#endif
    ephemeron_list_(NULL),
    weak_list_(NULL) {
  to_.Allocate(kInitialSemispaceCapacity);
  from_.Allocate(kInitialSemispaceCapacity);
  top_ = to_.object_start();
  end_ = to_.limit();

  // Class table.
  class_table_capacity_ = 1024;
  class_table_ = new Object*[class_table_capacity_];
#if defined(DEBUG)
  for (intptr_t i = 0; i < kFirstRegularObjectCid; i++) {
    class_table_[i] = reinterpret_cast<Object*>(kUninitializedWord);
  }
  for (intptr_t i = kFirstRegularObjectCid; i < class_table_capacity_; i++) {
    class_table_[i] = reinterpret_cast<Object*>(kUnallocatedWord);
  }
#endif
  class_table_size_ = kFirstRegularObjectCid;
}


Heap::~Heap() {
  to_.Free();
  from_.Free();
  delete[] class_table_;
}


uword Heap::Allocate(intptr_t size) {
  ASSERT((size & kObjectAlignmentMask) == 0);
  uword addr = TryAllocate(size);
  if (addr == 0) {
    Scavenge("allocate");
    addr = TryAllocate(size);
    if (addr == 0) {
      Grow(size);
      addr = TryAllocate(size);
      if (addr == 0) {
        FATAL1("Failed to allocate %" Pd " bytes\n", size);
      }
    }
  }
#if defined(DEBUG)
  memset(reinterpret_cast<void*>(addr), kUninitializedByte, size);
#endif
  return addr;
}


void Heap::Grow(size_t free_needed) {
  while ((next_semispace_capacity_ - Size()) < free_needed) {
    next_semispace_capacity_ *= 2;
  }
  if (next_semispace_capacity_ > kMaxSemispaceCapacity) {
    next_semispace_capacity_ = kMaxSemispaceCapacity;
  }
  Scavenge("grow");
}


void Heap::Scavenge(const char* reason) {
#if REPORT_GC
  int64_t start = OS::CurrentMonotonicNanos();
  size_t size_before = Size();
  OS::PrintErr("Begin scavenge (%" Pd "kB used, %s)\n",
               size_before / KB, reason);
#endif

  FlipSpaces();

#if defined(DEBUG)
  to_.ReadWrite();
#endif

  // Strong references.
  ScavengeRoots();
  uword scan = to_.object_start();
  while (scan < top_) {
    scan = ScavengeToSpace(scan);
    ScavengeEphemeronList();
  }

  // Weak references.
  MournEphemeronList();
  MournWeakList();
  MournClassTable();

  ClearCaches();

#if defined(DEBUG)
  from_.MarkUnallocated();
  from_.NoAccess();
#endif

#if REPORT_GC
  size_t size_after = Size();
  int64_t stop = OS::CurrentMonotonicNanos();
  int64_t time = stop - start;
  OS::PrintErr("End scavenge (%" Pd "kB used, %" Pd "kB freed, %" Pd64 " us)\n",
               size_after / KB, (size_before - size_after) / KB,
               time / kNanosecondsPerMicrosecond);
#endif

  if (Size() > (7 * Capacity() / 8)) {
    next_semispace_capacity_ = Capacity() * 2;
    if (next_semispace_capacity_ > kMaxSemispaceCapacity) {
      next_semispace_capacity_ = kMaxSemispaceCapacity;
    }
  }
}


void Heap::FlipSpaces() {
  Semispace temp = to_;
  to_ = from_;
  from_ = temp;

  ASSERT(next_semispace_capacity_ <= kMaxSemispaceCapacity);
  if (to_.size() < next_semispace_capacity_) {
    if (TRACE_GROWTH && (from_.size() < next_semispace_capacity_)) {
      OS::PrintErr("Growing heap to %" Pd "MB\n",
                   next_semispace_capacity_ / MB);
    }
    to_.Free();
    to_.Allocate(next_semispace_capacity_);
  }

  ASSERT(to_.size() >= from_.size());

  top_ = to_.object_start();
  end_ = to_.limit();
  ASSERT((top_ & kObjectAlignmentMask) == kNewObjectAlignmentOffset);
}


static void ForwardClass(Heap* heap, Object* object) {
  ASSERT(object->IsHeapObject());
  Behavior* old_class = heap->ClassAt(object->cid());
  if (old_class->IsForwardingCorpse()) {
    Behavior* new_class = static_cast<Behavior*>(
        reinterpret_cast<ForwardingCorpse*>(old_class)->target());
    ASSERT(!new_class->IsForwardingCorpse());
    new_class->AssertCouldBeBehavior();
    if (new_class->id() == heap->object_store()->nil_obj()) {
      ASSERT(old_class->id()->IsSmallInteger());
      new_class->set_id(old_class->id());
    }
    object->set_cid(new_class->id()->value());
  }
}


static void ForwardPointer(Object** ptr) {
  Object* old_target = *ptr;
  if (old_target->IsForwardingCorpse()) {
    Object* new_target =
        reinterpret_cast<ForwardingCorpse*>(old_target)->target();
    ASSERT(!new_target->IsForwardingCorpse());
    *ptr = new_target;
  }
}


void Heap::ScavengeRoots() {
  ScavengePointer(reinterpret_cast<Object**>(&object_store_));
  ScavengePointer(reinterpret_cast<Object**>(&current_activation_));

  for (intptr_t i = 0; i < handles_size_; i++) {
    ScavengePointer(handles_[i]);
  }
}


void Heap::ForwardRoots() {
  ForwardPointer(reinterpret_cast<Object**>(&object_store_));
  ForwardPointer(reinterpret_cast<Object**>(&current_activation_));

  for (intptr_t i = 0; i < handles_size_; i++) {
    ForwardPointer(handles_[i]);
  }
}


uword Heap::ScavengeToSpace(uword scan) {
  while (scan < top_) {
    Object* obj = Object::FromAddr(scan);
    intptr_t cid = obj->cid();
    ScavengeClass(cid);
    if (cid == kWeakArrayCid) {
      AddToWeakList(static_cast<WeakArray*>(obj));
    } else if (cid == kEphemeronCid) {
      AddToEphemeronList(static_cast<Ephemeron*>(obj));
    } else {
      Object** from;
      Object** to;
      obj->Pointers(&from, &to);
      for (Object** ptr = from; ptr <= to; ptr++) {
        ScavengePointer(ptr);
      }
    }
    scan += obj->HeapSize();
  }
  return scan;
}


void Heap::ForwardToSpace() {
  uword scan = to_.object_start();
  while (scan < top_) {
    Object* obj = Object::FromAddr(scan);
    if (!obj->IsForwardingCorpse()) {
      ForwardClass(this, obj);
      Object** from;
      Object** to;
      obj->Pointers(&from, &to);
      for (Object** ptr = from; ptr <= to; ptr++) {
        ForwardPointer(ptr);
      }
    }
    scan += obj->HeapSize();
  }
}


intptr_t Heap::AllocateClassId() {
  intptr_t cid;
  if (class_table_free_ != 0) {
    cid = class_table_free_;
    class_table_free_ =
        static_cast<SmallInteger*>(class_table_[cid])->value();
  } else if (class_table_size_ == class_table_capacity_) {
    if (TRACE_GROWTH) {
      OS::PrintErr("Scavenging to free class table entries\n");
    }
    Scavenge("allocate-cid");
    if (class_table_free_ != 0) {
      cid = class_table_free_;
      class_table_free_ =
          static_cast<SmallInteger*>(class_table_[cid])->value();
    } else {
      FATAL("Class table growth unimplemented");
      cid = -1;
    }
  } else {
    cid = class_table_size_;
    class_table_size_++;
  }
#if defined(DEBUG)
  class_table_[cid] = reinterpret_cast<Object*>(kUninitializedWord);
#endif
  return cid;
}


void Heap::ForwardClassTable() {
  Object* nil = object_store()->nil_obj();

  for (intptr_t i = kFirstLegalCid; i < class_table_size_; i++) {
    Behavior* old_class = static_cast<Behavior*>(class_table_[i]);
    if (!old_class->IsForwardingCorpse()) {
      continue;
    }

    Behavior* new_class = static_cast<Behavior*>(
        reinterpret_cast<ForwardingCorpse*>(old_class)->target());
    ASSERT(!new_class->IsForwardingCorpse());

    ASSERT(old_class->id()->IsSmallInteger());
    ASSERT(new_class->id()->IsSmallInteger() ||
           new_class->id() == nil);
    if (old_class->id() == new_class->id()) {
      class_table_[i] = new_class;
    } else {
      // new_class is not registered or registered with a different cid.
      // Instances of old_class (if any) have already had their headers updated
      // to the new cid, so release the old_class's cid.
      class_table_[i] = SmallInteger::New(class_table_free_);
      class_table_free_ = i;
    }
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


void Heap::MournClassTable() {
  for (intptr_t i = kFirstLegalCid; i < class_table_size_; i++) {
    Object** ptr = &class_table_[i];

    Object* old_target = *ptr;
    if (old_target->IsImmediateOrOldObject()) {
      continue;
    }

    uword old_target_addr = old_target->Addr();
    DEBUG_ASSERT(InFromSpace(old_target));

    Object* new_target;
    if (IsForwarded(old_target_addr)) {
      new_target = ForwardingTarget(old_target_addr);
      DEBUG_ASSERT(InToSpace(new_target));
    } else {
      new_target = SmallInteger::New(class_table_free_);
      class_table_free_ = i;
    }

    *ptr = new_target;
  }
}


void Heap::ScavengePointer(Object** ptr) {
  Object* old_target = *ptr;

  if (old_target->IsImmediateOrOldObject()) {
    // Target isn't gonna move.
    return;
  }

  uword old_target_addr = old_target->Addr();
  DEBUG_ASSERT(InFromSpace(old_target));

  Object* new_target;
  if (IsForwarded(old_target_addr)) {
    new_target = ForwardingTarget(old_target_addr);
  } else {
    // Target is now known to be reachable. Move it to to-space.
    intptr_t size = old_target->HeapSize();
    uword new_target_addr = TryAllocate(size);
    ASSERT(new_target_addr != 0);
    memcpy(reinterpret_cast<void*>(new_target_addr),
           reinterpret_cast<void*>(old_target_addr),
           size);
    SetForwarded(old_target_addr, new_target_addr);

    new_target = Object::FromAddr(new_target_addr);
  }

  DEBUG_ASSERT(InToSpace(new_target));

  *ptr = new_target;
}


void Heap::AddToEphemeronList(Ephemeron* survivor) {
  DEBUG_ASSERT(InToSpace(survivor));
  survivor->set_next(ephemeron_list_);
  ephemeron_list_ = survivor;
}


void Heap::ScavengeEphemeronList() {
  Ephemeron* survivor = ephemeron_list_;
  ephemeron_list_ = NULL;

  while (survivor != NULL) {
    ASSERT(survivor->IsEphemeron());
    Ephemeron* next = survivor->next();
    survivor->set_next(NULL);

    if (survivor->key()->IsImmediateOrOldObject() ||
        IsForwarded(survivor->key()->Addr())) {
      ScavengePointer(survivor->key_ptr());
      ScavengePointer(survivor->value_ptr());
      ScavengePointer(survivor->finalizer_ptr());
    } else {
      // Fate of the key is not yet known; add the ephemeron back to the list.
      AddToEphemeronList(survivor);
    }

    survivor = next;
  }
}


void Heap::MournEphemeronList() {
  Object* nil = object_store()->nil_obj();
  Ephemeron* survivor = ephemeron_list_;
  ephemeron_list_ = NULL;
  while (survivor != NULL) {
    ASSERT(survivor->IsEphemeron());

    DEBUG_ASSERT(InFromSpace(survivor->key()));
    survivor->set_key(nil);
    survivor->set_value(nil);
    // TODO(rmacnak): Put the finalizer on a queue for the event loop
    // to process.
    survivor->set_finalizer(nil);

    survivor = survivor->next();
  }
}


void Heap::AddToWeakList(WeakArray* survivor) {
  DEBUG_ASSERT(InToSpace(survivor));
  survivor->set_next(weak_list_);
  weak_list_ = survivor;
}


void Heap::MournWeakList() {
  WeakArray* survivor = weak_list_;
  weak_list_ = NULL;
  while (survivor != NULL) {
    ASSERT(survivor->IsWeakArray());

    Object** from;
    Object** to;
    survivor->Pointers(&from, &to);
    for (Object** ptr = from; ptr <= to; ptr++) {
      MournWeakPointer(ptr);
    }

    survivor = survivor->next();
  }
}


void Heap::MournWeakPointer(Object** ptr) {
  Object* old_target = *ptr;

  if (old_target->IsImmediateOrOldObject()) {
    // Target isn't gonna move.
    return;
  }

  uword old_target_addr = old_target->Addr();
  DEBUG_ASSERT(InFromSpace(old_target));

  Object* new_target;
  if (IsForwarded(old_target_addr)) {
    new_target = ForwardingTarget(old_target_addr);
  } else {
    // The object store and nil have already been scavenged.
    new_target = object_store()->nil_obj();
  }

  DEBUG_ASSERT(InToSpace(new_target));

  *ptr = new_target;
}


void Heap::ScavengeClass(intptr_t cid) {
  ASSERT(cid < class_table_size_);
  // This is very similar to ScavengePointer.

  Object* old_target = class_table_[cid];

  if (old_target->IsImmediateOrOldObject()) {
    // Target isn't gonna move.
    return;
  }

  uword old_target_addr = old_target->Addr();
  DEBUG_ASSERT(InFromSpace(old_target));

  if (IsForwarded(old_target_addr)) {
    // Already scavenged.
    return;
  }

  // Target is now known to be reachable. Move it to to-space.
  intptr_t size = old_target->HeapSize();
  uword new_target_addr = TryAllocate(size);
  ASSERT(new_target_addr != 0);
  memcpy(reinterpret_cast<void*>(new_target_addr),
         reinterpret_cast<void*>(old_target_addr),
         size);
  SetForwarded(old_target_addr, new_target_addr);
}


void Heap::ClearCaches() {
#if LOOKUP_CACHE
  lookup_cache_->Clear();
#endif
#if RECYCLE_ACTIVATIONS
  recycle_list_ = NULL;
#endif
}


intptr_t Heap::CountInstances(intptr_t cid) {
  intptr_t instances = 0;
  uword scan = to_.object_start();
  while (scan < top_) {
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
  while (scan < top_) {
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

  intptr_t length = old->Size();
  if (TRACE_BECOME) {
    OS::PrintErr("become(%" Pd ")\n", length);
  }

  for (intptr_t i = 0; i < length; i++) {
    Object* forwarder = old->element(i);
    Object* forwardee = neu->element(i);
    if (forwarder->IsImmediateObject() ||
        forwardee->IsImmediateObject()) {
      return false;
    }
  }

  for (intptr_t i = 0; i < length; i++) {
    Object* forwarder = old->element(i);
    Object* forwardee = neu->element(i);

    ASSERT(!forwarder->IsForwardingCorpse());
    ASSERT(!forwardee->IsForwardingCorpse());

    forwardee->set_identity_hash(forwarder->identity_hash());

    intptr_t heap_size = forwarder->HeapSize();

    Object::InitializeObject(forwarder->Addr(),
                             kForwardingCorpseCid,
                             heap_size);
    ASSERT(forwarder->IsForwardingCorpse());
    ForwardingCorpse* corpse = reinterpret_cast<ForwardingCorpse*>(forwarder);
    if (forwarder->heap_size() == 0) {
      corpse->set_overflow_size(heap_size);
    }
    ASSERT(forwarder->HeapSize() == heap_size);

    corpse->set_target(forwardee);
  }

  ForwardRoots();
  ForwardToSpace();  // Using old class table.
  ForwardClassTable();

  ClearCaches();

  return true;
}


static void PrintStringError(String* string) {
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
    String* receiver_mixin_name = receiver_mixin->name();
    if (receiver_mixin_name->IsString()) {
      PrintStringError(receiver_mixin_name);
    } else {
      receiver_mixin_name =
          reinterpret_cast<AbstractMixin*>(receiver_mixin_name)->name();
      ASSERT(receiver_mixin_name->IsString());
      PrintStringError(receiver_mixin_name);
      OS::PrintErr(" class");
    }

    AbstractMixin* method_mixin = home->method()->mixin();
    if (receiver_mixin != method_mixin) {
      String* method_mixin_name = method_mixin->name();
      OS::PrintErr("(");
      if (method_mixin_name->IsString()) {
        PrintStringError(method_mixin_name);
      } else {
        method_mixin_name =
            reinterpret_cast<AbstractMixin*>(method_mixin_name)->name();
        ASSERT(method_mixin_name->IsString());
        PrintStringError(method_mixin_name);
        OS::PrintErr(" class");
      }
      OS::PrintErr(")");
    }

    String* method_name = home->method()->selector();
    OS::PrintErr(" ");
    PrintStringError(method_name);
    OS::PrintErr("\n");

    act = act->sender();
  }
}

}  // namespace psoup
