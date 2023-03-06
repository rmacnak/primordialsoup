// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/heap.h"

#include "vm/interpreter.h"
#include "vm/os.h"

namespace psoup {

class Region {
 public:
  static Region* Allocate(intptr_t size) {
    VirtualMemory memory = VirtualMemory::Allocate(size,
                                                   VirtualMemory::kReadWrite,
                                                   "primordialsoup-heap");
#if defined(DEBUG)
    memset(reinterpret_cast<void*>(memory.base()), kUnallocatedByte, size);
#endif
    Region* region = reinterpret_cast<Region*>(memory.base());
    region->memory_ = memory;
    region->object_end_ = region->object_start();
    return region;
  }

  void Free() { memory_.Free(); }

  uword TryAllocate(intptr_t size) {
    ASSERT(Utils::IsAligned(size, kObjectAlignment));
    uword result = object_end_;
    intptr_t remaining = memory_.limit() - object_end_;
    if (remaining < size) {
      return 0;
    }
    ASSERT((result & kObjectAlignmentMask) == kOldObjectAlignmentOffset);
    object_end_ += size;
    return result;
  }

  uword size() const { return memory_.size(); }
  uword limit() const { return memory_.limit(); }
  uword object_start() const {
    return reinterpret_cast<uword>(this) + AllocationSize(sizeof(Region));
  }
  uword object_end() const { return object_end_; }
  void set_object_end(uword value) { object_end_ = value; }

  Region* next() const { return next_; }
  void set_next(Region* next) { next_ = next; }

 private:
  Region* next_;
  VirtualMemory memory_;
  uword object_end_;
};

class MarkStack {
 public:
  void Init(uword limit) {
    top_ = &stack_[0];
    end_ = reinterpret_cast<HeapObject*>(limit);
  }

  bool IsEmpty() const { return top_ == &stack_[0]; }
  void Push(HeapObject obj) {
    if (top_ == end_) {
      FATAL("Mark stack overflow");
    } else {
      *top_++ = obj;
    }
  }
  HeapObject Pop() { return *--top_; }

 private:
  HeapObject* top_;
  HeapObject* end_;
  HeapObject stack_[];
};

Heap::Heap()
    : top_(0),
      end_(0),
      survivor_end_(0),
      to_(),
      from_(),
      next_semispace_capacity_(kInitialSemispaceCapacity),
      regions_(nullptr),
      freelist_(),
      old_size_(0),
      old_capacity_(0),
      old_limit_(0),
      remembered_set_(nullptr),
      remembered_set_size_(0),
      remembered_set_capacity_(0),
      class_table_(nullptr),
      class_table_size_(0),
      class_table_capacity_(0),
      class_table_free_(0),
      interpreter_(nullptr),
      handles_(),
      handles_size_(0),
      ephemeron_list_(nullptr),
      weak_list_(nullptr) {
  to_.Allocate(kInitialSemispaceCapacity);
  from_.Allocate(kInitialSemispaceCapacity);

  remembered_set_capacity_ = 1024;
  remembered_set_ = new HeapObject[remembered_set_capacity_];

  // Class table.
  class_table_capacity_ = 1024;
  class_table_ = new Object[class_table_capacity_];
#if defined(DEBUG)
  for (intptr_t i = 0; i < kFirstRegularObjectCid; i++) {
    class_table_[i] = static_cast<Object>(kUninitializedWord);
  }
  for (intptr_t i = kFirstRegularObjectCid; i < class_table_capacity_; i++) {
    class_table_[i] = static_cast<Object>(kUnallocatedWord);
  }
#endif
  class_table_size_ = kFirstRegularObjectCid;
}

Heap::~Heap() {
  to_.Free();
  from_.Free();
  Region* region = regions_;
  while (region != nullptr) {
    Region* next = region->next();
    region->Free();
    region = next;
  }
  delete[] remembered_set_;
  delete[] class_table_;
}

Message Heap::AllocateMessage() {
  Behavior behavior = interpreter_->object_store()->Message();
  ASSERT(behavior->IsRegularObject());
  behavior->AssertCouldBeBehavior();
  SmallInteger id = behavior->id();
  if (id == interpreter_->nil_obj()) {
    id = SmallInteger::New(AllocateClassId());  // SAFEPOINT
    behavior = interpreter_->object_store()->Message();
    RegisterClass(id->value(), behavior);
  }
  ASSERT(id->IsSmallInteger());
  SmallInteger format = behavior->format();
  ASSERT(format->IsSmallInteger());
  intptr_t num_slots = format->value();
  ASSERT(num_slots == 2);
  Object new_instance = AllocateRegularObject(id->value(), num_slots);
  return static_cast<Message>(new_instance);
}

uword Heap::AllocateNormal(intptr_t size) {
  if (size >= kLargeAllocation) {
    return AllocateOldLarge(size, kControlGrowth);
  }

  uword addr = top_;
  if (addr + size > end_) {
    Scavenge(kNewSpace);
    if (old_size_ > old_limit_) {
      MarkSweep(kTenure);
    }
    addr = top_;
    if (addr + size > end_) {
      return AllocateOldSmall(size, kControlGrowth);
    }
  }
  top_ = addr + size;
#if defined(DEBUG)
  memset(reinterpret_cast<void*>(addr), kUninitializedByte, size);
#endif
  return addr;
}

uword Heap::AllocateCopy(intptr_t size) {
  uword result = top_;
  intptr_t remaining = end_ - top_;
  ASSERT(size <= remaining);
  ASSERT((result & kObjectAlignmentMask) == kNewObjectAlignmentOffset);
  top_ = result + size;
  return result;
}

uword Heap::AllocateTenure(intptr_t size) {
  uword result = AllocateOldSmall(size, kForceGrowth);
  PushTenureStack(result);
  return result;
}

uword Heap::AllocateOldSmall(intptr_t size, GrowthPolicy growth) {
  ASSERT(size < kLargeAllocation);
  uword addr = freelist_.TryAllocate(size);
  if (addr == 0) {
    Region* region = AllocateRegion(kRegionSize, growth);
    addr = region->TryAllocate(size);
    intptr_t remaining = region->limit() - region->object_end();
    if (remaining > 0) {
      freelist_.EnqueueRange(region->object_end(), remaining);
      region->set_object_end(region->limit());
    }
  }
  ASSERT(addr != 0);
  old_size_ += size;
#if defined(DEBUG)
  memset(reinterpret_cast<void*>(addr), kUninitializedByte, size);
#endif
  return addr;
}

uword Heap::AllocateOldLarge(intptr_t size, GrowthPolicy growth) {
  ASSERT(size >= kLargeAllocation);
  Region* region = AllocateRegion(size + AllocationSize(sizeof(Region)),
                                  growth);
  uword addr = region->TryAllocate(size);
  ASSERT(addr != 0);
  old_size_ += size;
#if defined(DEBUG)
  memset(reinterpret_cast<void*>(addr), kUninitializedByte, size);
#endif
  return addr;
}

uword Heap::AllocateSnapshot(intptr_t size) {
  if (size >= kLargeAllocation) {
    return AllocateOldLarge(size, kForceGrowth);
  }

  uword addr = top_;
  if (addr + size > end_) {
    intptr_t remaining = end_ - top_;
    if (remaining > 0) {
      freelist_.EnqueueRange(top_, remaining);
      old_size_ -= remaining;
    }
    Region* region = AllocateRegion(kRegionSize, kForceGrowth);
    top_ = region->object_start();
    end_ = region->limit();
    region->set_object_end(end_);
    remaining = end_ - top_;
    old_size_ += remaining;
    ASSERT(size <= remaining);
    addr = top_;
  }
  top_ = addr + size;
#if defined(DEBUG)
  memset(reinterpret_cast<void*>(addr), kUninitializedByte, size);
#endif
  return addr;
}

Region* Heap::AllocateRegion(intptr_t region_size, GrowthPolicy growth) {
  if ((growth == kControlGrowth) && ((old_size_ + region_size) > old_limit_)) {
    MarkSweep(kOldSpace);
  }
  Region* region = Region::Allocate(region_size);
  old_capacity_ += region->size();
  region->set_next(regions_);
  regions_ = region;
  return region;
}

void Heap::GrowRememberedSet() {
  // TODO(rmacnak): Investigate a limit to trigger GC instead of letting this
  // grow in an unbounded way.
  remembered_set_capacity_ += (remembered_set_capacity_ >> 1);
  if (TRACE_GROWTH) {
    OS::PrintErr("Growing remembered set to %" Pd "\n",
                 remembered_set_capacity_);
  }
  HeapObject* old_remembered_set = remembered_set_;
  remembered_set_ = new HeapObject[remembered_set_capacity_];
  for (intptr_t i = 0; i < remembered_set_size_; i++) {
    remembered_set_[i] = old_remembered_set[i];
  }
  delete[] old_remembered_set;
}

void Heap::ShrinkRememberedSet() {
  intptr_t preferred_capacity =
      Utils::RoundUp(remembered_set_size_ + (remembered_set_size_ >> 1) + 1,
                     KB);

  if (remembered_set_capacity_ <= preferred_capacity) {
    return;
  }
  remembered_set_capacity_ = preferred_capacity;
  if (TRACE_GROWTH) {
    OS::PrintErr("Shrinking remembered set to %" Pd "\n",
                 remembered_set_capacity_);
  }
  HeapObject* old_remembered_set = remembered_set_;
  remembered_set_ = new HeapObject[remembered_set_capacity_];
  for (intptr_t i = 0; i < remembered_set_size_; i++) {
    remembered_set_[i] = old_remembered_set[i];
  }
  delete[] old_remembered_set;
}

NOINLINE
void Heap::Scavenge(Reason reason) {
#if REPORT_GC
  int64_t start = OS::CurrentMonotonicNanos();
  size_t new_before = top_ - to_.object_start();
#endif
  size_t old_before = old_size_;

  FlipSpaces();

#if defined(DEBUG)
  to_.ReadWrite();
#endif

  interpreter_->GCPrologue();

  // Strong references.
  ScavengeRoots();
  uword scan = to_.object_start();
  while (scan < top_ || end_ < to_.limit()) {
    scan = ScavengeToSpace(scan);
    ProcessTenureStack();
    ScavengeEphemeronList();
  }

  // Weak references.
  MournEphemeronList();
  MournWeakListScavenge();
  MournClassTableScavenge();

#if defined(DEBUG)
  from_.MarkUnallocated();
  from_.NoAccess();
#endif

  interpreter_->GCEpilogue();

  survivor_end_ = top_;

  size_t new_after = top_ - to_.object_start();
  size_t old_after = old_size_;
  size_t tenured = old_after - old_before;
  size_t survived = new_after + tenured;

  if (survived > (to_.size() / 3)) {
    next_semispace_capacity_ = to_.size() * 2;
    if (next_semispace_capacity_ > kMaxSemispaceCapacity) {
      next_semispace_capacity_ = kMaxSemispaceCapacity;
    }
  }

#if REPORT_GC
  size_t freed = (new_before + old_before) - (new_after + old_after);
  int64_t stop = OS::CurrentMonotonicNanos();
  int64_t time = stop - start;
  OS::PrintErr("Scavenge (%s, %" Pd "kB new, "
               "%" Pd "kB tenured, %" Pd "kB freed, %" Pd64 " us)\n",
               ReasonToCString(reason), new_after / KB, tenured / KB,
               freed / KB, time / kNanosecondsPerMicrosecond);
#endif
}

void Heap::FlipSpaces() {
  Semispace temp = to_;
  to_ = from_;
  from_ = temp;

  ASSERT(next_semispace_capacity_ <= kMaxSemispaceCapacity);
  if (to_.size() < next_semispace_capacity_) {
    if (TRACE_GROWTH && (from_.size() < next_semispace_capacity_)) {
      OS::PrintErr("Growing new space to %" Pd "MB\n",
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

static bool ForwardClass(Heap* heap, HeapObject object) {
  ASSERT(object->IsHeapObject());
  Behavior old_class = heap->ClassAt(object->cid());
  if (old_class->IsForwardingCorpse()) {
    Behavior new_class = static_cast<Behavior>(
        static_cast<ForwardingCorpse>(old_class)->target());
    ASSERT(!new_class->IsForwardingCorpse());
    new_class->AssertCouldBeBehavior();
    ASSERT(new_class->id()->IsSmallInteger());
    object->set_cid(new_class->id()->value());
    return new_class->IsNewObject();
  }
  return old_class->IsNewObject();
}

static bool ForwardPointer(Object* ptr) {
  Object old_target = *ptr;
  if (old_target->IsForwardingCorpse()) {
    Object new_target = static_cast<ForwardingCorpse>(old_target)->target();
    ASSERT(!new_target->IsForwardingCorpse());
    *ptr = new_target;
    return new_target->IsNewObject();
  }
  return old_target->IsNewObject();
}

void Heap::ScavengeRoots() {
  // Process the remembered set first so we can visit and reset in one pass.
  intptr_t saved_remembered_set_size = remembered_set_size_;
  remembered_set_size_ = 0;

  for (intptr_t i = 0; i < saved_remembered_set_size; i++) {
    HeapObject obj = remembered_set_[i];
    ASSERT(obj->IsOldObject());
    ASSERT(obj->is_remembered());
    obj->set_is_remembered(false);
    ScavengeOldObject(obj);
  }

  for (intptr_t i = 0; i < handles_size_; i++) {
    ScavengePointer(handles_[i]);
  }

  Object* from;
  Object* to;
  interpreter_->RootPointers(&from, &to);
  for (Object* ptr = from; ptr <= to; ptr++) {
    ScavengePointer(ptr);
  }
  interpreter_->StackPointers(&from, &to);
  for (Object* ptr = from; ptr <= to; ptr++) {
    ScavengePointer(ptr);
  }
}

uword Heap::ScavengeToSpace(uword scan) {
  while (scan < top_) {
    HeapObject obj = HeapObject::FromAddr(scan);
    intptr_t cid = obj->cid();
    if (cid == kWeakArrayCid) {
      AddToWeakList(static_cast<WeakArray>(obj));
    } else if (cid == kEphemeronCid) {
      AddToEphemeronList(static_cast<Ephemeron>(obj));
    } else {
      ScavengeClass(cid);
      Object* from;
      Object* to;
      obj->Pointers(&from, &to);
      for (Object* ptr = from; ptr <= to; ptr++) {
        ScavengePointer(ptr);
      }
    }
    scan += obj->HeapSize();
  }
  return scan;
}

void Heap::PushTenureStack(uword addr) {
  ASSERT(end_ > top_);
  ASSERT(end_ <= to_.limit());
  end_ -= sizeof(addr);
  *reinterpret_cast<uword*>(end_) = addr;
}

uword Heap::PopTenureStack() {
  ASSERT(end_ > top_);
  ASSERT(end_ <= to_.limit());
  uword addr = *reinterpret_cast<uword*>(end_);
  end_ += sizeof(uword);
  return addr;
}

bool Heap::IsTenureStackEmpty() {
  ASSERT(end_ > top_);
  ASSERT(end_ <= to_.limit());
  return end_ == to_.limit();
}

void Heap::ProcessTenureStack() {
  while (!IsTenureStackEmpty()) {
    HeapObject obj = HeapObject::FromAddr(PopTenureStack());
    ScavengeOldObject(obj);
  }
}

static bool IsForwarded(HeapObject obj) {
  uword header = *reinterpret_cast<uword*>(obj->Addr());
  return header & (1 << kMarkBit);
}

static HeapObject ForwardingTarget(HeapObject obj) {
  ASSERT(IsForwarded(obj));
  uword header = *reinterpret_cast<uword*>(obj->Addr());
  // Mark bit and tag bit are conveniently in the same place.
  ASSERT((header & kSmiTagMask) == kHeapObjectTag);
  return static_cast<HeapObject>(header);
}

static void SetForwarded(HeapObject old_obj, HeapObject new_obj) {
  ASSERT(old_obj->IsNewObject());
  ASSERT(!IsForwarded(old_obj));
  uword header = static_cast<uword>(new_obj);
  // Mark bit and tag bit are conveniently in the same place.
  ASSERT((header & kSmiTagMask) == kHeapObjectTag);
  *reinterpret_cast<uword*>(old_obj->Addr()) = header;
}

// Specialization of memcpy that can assume the addresses are word-aligned and
// the size is a positive multiple of two words.
static void objcpy(void* __restrict dst,
                   const void* __restrict src,
                   size_t size) {
  COMPILE_ASSERT(kObjectAlignment == 2 * kWordSize);
  uword* dst_cursor = reinterpret_cast<uword*>(dst);
  const uword* src_cursor = reinterpret_cast<const uword*>(src);
  do {
    uword a = *src_cursor++;
    uword b = *src_cursor++;
    *dst_cursor++ = a;
    *dst_cursor++ = b;
    size -= kObjectAlignment;
  } while (size != 0);
}

bool Heap::ScavengePointer(Object* ptr) {
  HeapObject old_target = static_cast<HeapObject>(*ptr);
  if (old_target->IsImmediateOrOldObject()) {
    return false;
  }

  DEBUG_ASSERT(InFromSpace(old_target));

  HeapObject new_target;
  if (IsForwarded(old_target)) {
    new_target = ForwardingTarget(old_target);
  } else {
    // Target is now known to be reachable. Move it to to-space.
    intptr_t size = old_target->HeapSize();

    uword new_target_addr;
    if (old_target->Addr() < survivor_end_) {
      new_target_addr = AllocateTenure(size);
    } else {
      new_target_addr = AllocateCopy(size);
    }

    ASSERT(new_target_addr != 0);
    objcpy(reinterpret_cast<void*>(new_target_addr),
           reinterpret_cast<void*>(old_target->Addr()),
           size);
    new_target = HeapObject::FromAddr(new_target_addr);
    SetForwarded(old_target, new_target);
  }

  DEBUG_ASSERT(new_target->IsOldObject() || InToSpace(new_target));

  *ptr = new_target;
  return new_target->IsNewObject();
}

void Heap::ScavengeOldObject(HeapObject obj) {
  intptr_t cid = obj->cid();
  if (cid == kWeakArrayCid) {
    AddToWeakList(static_cast<WeakArray>(obj));
  } else if (cid == kEphemeronCid) {
    AddToEphemeronList(static_cast<Ephemeron>(obj));
  } else {
    bool has_new_target = ScavengeClass(cid);
    Object* from;
    Object* to;
    obj->Pointers(&from, &to);
    for (Object* ptr = from; ptr <= to; ptr++) {
      has_new_target |= ScavengePointer(ptr);
    }
    if (has_new_target) {
      AddToRememberedSet(obj);
    }
  }
}

bool Heap::ScavengeClass(intptr_t cid) {
  ASSERT(cid < class_table_size_);
  // This is very similar to ScavengePointer.

  HeapObject old_target = static_cast<HeapObject>(class_table_[cid]);
  if (old_target->IsImmediateOrOldObject()) {
    return false;
  }

  DEBUG_ASSERT(InFromSpace(old_target));

  if (IsForwarded(old_target)) {
    // Already scavenged.
    return ForwardingTarget(old_target)->IsNewObject();
  }

  // Target is now known to be reachable. Move it to to-space.
  intptr_t size = old_target->HeapSize();

  uword new_target_addr;
  if (old_target->Addr() < survivor_end_) {
    new_target_addr = AllocateTenure(size);
  } else {
    new_target_addr = AllocateCopy(size);
  }

  ASSERT(new_target_addr != 0);
  objcpy(reinterpret_cast<void*>(new_target_addr),
         reinterpret_cast<void*>(old_target->Addr()),
         size);
  HeapObject new_target = HeapObject::FromAddr(new_target_addr);
  SetForwarded(old_target, new_target);
  return true;
}

NOINLINE
void Heap::MarkSweep(Reason reason) {
#if REPORT_GC
  int64_t start = OS::CurrentMonotonicNanos();
  size_t size_before = old_size_;
#endif

#if defined(DEBUG)
  from_.ReadWrite();
#endif

  MarkStack* mark_stack = reinterpret_cast<MarkStack*>(from_.base());
  mark_stack->Init(from_.limit());

  // Remembered set will be re-built during marking.
  remembered_set_size_ = 0;
  old_size_ = 0;

  interpreter_->GCPrologue();

  // Strong references.
  MarkRoots();
  while (!mark_stack->IsEmpty()) {
    ProcessMarkStack();
    MarkEphemeronList();
  }

#if defined(DEBUG)
  from_.NoAccess();
#endif

  ASSERT(old_size_ <= old_capacity_);

  // Weak references.
  MournEphemeronList();
  MournWeakListMarkSweep();
  MournClassTableMarkSweep();

  interpreter_->GCEpilogue();

  Sweep();

  ShrinkRememberedSet();

  SetOldAllocationLimit();

#if REPORT_GC
  size_t size_after = old_size_;
  int64_t stop = OS::CurrentMonotonicNanos();
  int64_t time = stop - start;
  OS::PrintErr("Mark-sweep "
               "(%s, %" Pd "kB old, %" Pd "kB freed, %" Pd64 " us)\n",
               ReasonToCString(reason), size_after / KB,
               (size_before - size_after) / KB,
               time / kNanosecondsPerMicrosecond);
#endif
}

void Heap::MarkRoots() {
  for (intptr_t i = 0; i < handles_size_; i++) {
    MarkObject(*handles_[i]);
  }

  Object* from;
  Object* to;
  interpreter_->RootPointers(&from, &to);
  for (Object* ptr = from; ptr <= to; ptr++) {
    MarkObject(*ptr);
  }
  interpreter_->StackPointers(&from, &to);
  for (Object* ptr = from; ptr <= to; ptr++) {
    MarkObject(*ptr);
  }
}

void Heap::MarkObject(Object obj) {
  if (obj->IsImmediateObject()) return;

  HeapObject heap_obj = static_cast<HeapObject>(obj);
  if (heap_obj->is_marked()) return;

  heap_obj->set_is_marked(true);
  heap_obj->set_is_remembered(false);
  MarkStack* mark_stack = reinterpret_cast<MarkStack*>(from_.base());
  mark_stack->Push(heap_obj);
}

void Heap::ProcessMarkStack() {
  MarkStack* mark_stack = reinterpret_cast<MarkStack*>(from_.base());
  while (!mark_stack->IsEmpty()) {
    HeapObject obj = mark_stack->Pop();
    ASSERT(obj->is_marked());
    ASSERT(!obj->is_remembered());

    intptr_t cid = obj->cid();
    ASSERT(cid != kIllegalCid);
    ASSERT(cid != kForwardingCorpseCid);
    ASSERT(cid != kFreeListElementCid);

    if (cid == kWeakArrayCid) {
      AddToWeakList(static_cast<WeakArray>(obj));
    } else if (cid == kEphemeronCid) {
      AddToEphemeronList(static_cast<Ephemeron>(obj));
    } else {
      MarkObject(ClassAt(cid));
      bool has_new_target = ClassAt(cid)->IsNewObject();
      Object* from;
      Object* to;
      obj->Pointers(&from, &to);
      for (Object* ptr = from; ptr <= to; ptr++) {
        Object target = *ptr;
        has_new_target |= target->IsNewObject();
        MarkObject(target);
      }
      if (has_new_target && obj->IsOldObject()) {
        AddToRememberedSet(obj);
      }
    }
  }
}

void Heap::Sweep() {
  freelist_.Reset();

  uword scan = to_.object_start();
  while (scan < top_) {
    HeapObject obj = HeapObject::FromAddr(scan);
    if (obj->is_marked()) {
      obj->set_is_marked(false);
      scan += obj->HeapSize();
    } else {
      uword free_scan = scan + obj->HeapSize();
      while (free_scan < top_) {
        HeapObject next = HeapObject::FromAddr(free_scan);
        if (next->is_marked()) break;
        free_scan += next->HeapSize();
      }

      intptr_t size = free_scan - scan;
      HeapObject object =
          HeapObject::Initialize(scan, kFreeListElementCid, size);
      FreeListElement element = static_cast<FreeListElement>(object);
      if (element->heap_size() == 0) {
        ASSERT(size > kObjectAlignment);
        element->set_overflow_size(size);
      }
      ASSERT(object->HeapSize() == size);
      ASSERT(element->HeapSize() == size);

      scan = free_scan;
    }
  }

  Region* prev = nullptr;
  Region* region = regions_;
  while (region != nullptr) {
    bool in_use = SweepRegion(region);
    if (in_use) {
      prev = region;
      region = region->next();
    } else {
      Region* next = region->next();
      if (prev == nullptr) {
        regions_ = next;
      } else {
        prev->set_next(next);
      }
      old_capacity_ -= region->size();
      region->Free();
      region = next;
    }
  }
}

bool Heap::SweepRegion(Region* region) {
  uword scan = region->object_start();
  uword end = region->object_end();
  while (scan < end) {
    HeapObject obj = HeapObject::FromAddr(scan);
    if (obj->is_marked()) {
      obj->set_is_marked(false);
      intptr_t size = obj->HeapSize();
      old_size_ += size;
      scan += size;
    } else {
      uword free_scan = scan + obj->HeapSize();
      while (free_scan < end) {
        HeapObject next = HeapObject::FromAddr(free_scan);
        if (next->is_marked()) break;
        free_scan += next->HeapSize();
      }

      if ((scan == region->object_start()) && (free_scan == end)) {
        // Note that Region::Of relies on us releasing large regions instead of
        // adding them to the freelist.
        return false;  // Not in use.
      }

      freelist_.EnqueueRange(scan, free_scan - scan);
      scan = free_scan;
    }
  }
  return true;  // In use.
}

void Heap::SetOldAllocationLimit() {
  old_limit_ = old_size_ + old_size_ / 2;
  if (old_limit_ < old_size_ + 2 * kRegionSize) {
    old_limit_ = old_size_ + 2 * kRegionSize;
  }
  if (TRACE_GROWTH) {
    OS::PrintErr("Old %" Pd "kB size, %" Pd "kB capacity, %" Pd "kB limit\n",
                 old_size_ / KB, old_capacity_ / KB, old_limit_ / KB);
  }
}

void Heap::AddToEphemeronList(Ephemeron survivor) {
  DEBUG_ASSERT(survivor->IsOldObject() || InToSpace(survivor));
  survivor->set_next(ephemeron_list_);
  ephemeron_list_ = survivor;
}

static bool IsScavengeSurvivor(Object obj) {
  return obj->IsImmediateOrOldObject() ||
         IsForwarded(static_cast<HeapObject>(obj));
}

void Heap::ScavengeEphemeronList() {
  Ephemeron survivor = ephemeron_list_;
  ephemeron_list_ = nullptr;

  while (survivor != nullptr) {
    ASSERT(survivor->IsEphemeron());
    Ephemeron next = survivor->next();
    survivor->set_next(nullptr);

    if (IsScavengeSurvivor(survivor->key())) {
      bool has_new_target = false;
      Object* from = survivor->key_ptr();
      Object* to = survivor->finalizer_ptr();
      for (Object* ptr = from; ptr <= to; ptr++) {
        has_new_target |= ScavengePointer(ptr);
      }
      if (has_new_target && survivor->IsOldObject()) {
        AddToRememberedSet(survivor);
      }
    } else {
      // Fate of key is not yet known, return the ephemeron to list.
      AddToEphemeronList(survivor);
    }

    survivor = next;
  }
}

static bool IsMarkSweepSurvivor(Object obj) {
  return obj->IsImmediateObject() || static_cast<HeapObject>(obj)->is_marked();
}

void Heap::MarkEphemeronList() {
  Ephemeron survivor = ephemeron_list_;
  ephemeron_list_ = nullptr;

  while (survivor != nullptr) {
    ASSERT(survivor->IsEphemeron());
    Ephemeron next = survivor->next();
    survivor->set_next(nullptr);

    if (IsMarkSweepSurvivor(survivor->key())) {
      bool has_new_target = false;
      Object* from = survivor->key_ptr();
      Object* to = survivor->finalizer_ptr();
      for (Object* ptr = from; ptr <= to; ptr++) {
        Object target = *ptr;
        has_new_target |= target->IsNewObject();
        MarkObject(target);
      }
      if (has_new_target && survivor->IsOldObject()) {
        AddToRememberedSet(survivor);
      }
    } else {
      // Fate of the key is not yet known; add the ephemeron back to the list.
      AddToEphemeronList(survivor);
    }

    survivor = next;
  }
}

void Heap::MournEphemeronList() {
  Object nil = interpreter_->nil_obj();
  Ephemeron survivor = ephemeron_list_;
  ephemeron_list_ = nullptr;

  while (survivor != nullptr) {
    ASSERT(survivor->IsEphemeron());

    survivor->set_key(nil, kNoBarrier);
    survivor->set_value(nil, kNoBarrier);
    // TODO(rmacnak): Put the finalizer on a queue for the event loop
    // to process.
    survivor->set_finalizer(nil, kNoBarrier);

    Ephemeron next = survivor->next();
    survivor->set_next(nullptr);
    survivor = next;
  }
}

void Heap::AddToWeakList(WeakArray survivor) {
  DEBUG_ASSERT(survivor->IsOldObject() || InToSpace(survivor));
  survivor->set_next(weak_list_);
  weak_list_ = survivor;
}

void Heap::MournWeakListScavenge() {
  WeakArray survivor = weak_list_;
  weak_list_ = nullptr;
  while (survivor != nullptr) {
    ASSERT(survivor->IsWeakArray());

    bool has_new_target = false;
    Object* from;
    Object* to;
    survivor->Pointers(&from, &to);
    for (Object* ptr = from; ptr <= to; ptr++) {
      has_new_target |= MournWeakPointerScavenge(ptr);
    }
    if (has_new_target && survivor->IsOldObject()) {
      AddToRememberedSet(survivor);
    }

    WeakArray next = survivor->next();
    survivor->set_next(nullptr);
    survivor = next;
  }
  ASSERT(weak_list_ == nullptr);
}

void Heap::MournWeakListMarkSweep() {
  WeakArray survivor = weak_list_;
  weak_list_ = nullptr;
  while (survivor != nullptr) {
    ASSERT(survivor->IsWeakArray());

    bool has_new_target = false;
    Object* from;
    Object* to;
    survivor->Pointers(&from, &to);
    for (Object* ptr = from; ptr <= to; ptr++) {
      MournWeakPointerMarkSweep(ptr);
      has_new_target |= (*ptr)->IsNewObject();
    }
    if (has_new_target && survivor->IsOldObject()) {
      AddToRememberedSet(survivor);
    }

    WeakArray next = survivor->next();
    survivor->set_next(nullptr);
    survivor = next;
  }
  ASSERT(weak_list_ == nullptr);
}

bool Heap::MournWeakPointerScavenge(Object* ptr) {
  HeapObject old_target = static_cast<HeapObject>(*ptr);
  if (old_target->IsImmediateOrOldObject()) {
    return false;
  }

  DEBUG_ASSERT(InFromSpace(old_target));

  HeapObject new_target;
  if (IsForwarded(old_target)) {
    new_target = ForwardingTarget(old_target);
  } else {
    // The object store and nil have already been scavenged.
    new_target = static_cast<HeapObject>(interpreter_->nil_obj());
  }

  DEBUG_ASSERT(new_target->IsOldObject() || InToSpace(new_target));

  *ptr = new_target;
  return new_target->IsNewObject();
}

void Heap::MournWeakPointerMarkSweep(Object* ptr) {
  Object target = *ptr;

  if (IsMarkSweepSurvivor(target)) {
    // Target is still alive.
    return;
  }

  ASSERT(IsMarkSweepSurvivor(interpreter_->nil_obj()));
  *ptr = interpreter_->nil_obj();
}

void Heap::MournClassTableScavenge() {
  for (intptr_t i = kFirstLegalCid; i < class_table_size_; i++) {
    Object* ptr = &class_table_[i];

    HeapObject old_target = static_cast<HeapObject>(*ptr);
    if (old_target->IsImmediateOrOldObject()) {
      continue;
    }

    DEBUG_ASSERT(InFromSpace(old_target));

    if (IsForwarded(old_target)) {
      HeapObject new_target = ForwardingTarget(old_target);
      DEBUG_ASSERT(new_target->IsOldObject() || InToSpace(new_target));
      *ptr = new_target;
    } else {
      *ptr = SmallInteger::New(class_table_free_);
      class_table_free_ = i;
    }
  }
}

void Heap::MournClassTableMarkSweep() {
  for (intptr_t i = kFirstLegalCid; i < class_table_size_; i++) {
    Object* ptr = &class_table_[i];

    Object target = *ptr;

    if (IsMarkSweepSurvivor(target)) {
      continue;
    }

    *ptr = SmallInteger::New(class_table_free_);
    class_table_free_ = i;
  }
}

void Heap::MournClassTableForwarded() {
  for (intptr_t i = kFirstLegalCid; i < class_table_size_; i++) {
    Behavior old_class = static_cast<Behavior>(class_table_[i]);
    if (!old_class->IsForwardingCorpse()) {
      continue;
    }

    ASSERT(i >= kFirstRegularObjectCid);
    class_table_[i] = SmallInteger::New(class_table_free_);
    class_table_free_ = i;
  }
}

bool Heap::BecomeForward(Array old, Array neu) {
  if (old->Size() != neu->Size()) {
    return false;
  }

  intptr_t length = old->Size();
  if (TRACE_BECOME) {
    OS::PrintErr("become(%" Pd ")\n", length);
  }

  for (intptr_t i = 0; i < length; i++) {
    Object forwarder = old->element(i);
    Object forwardee = neu->element(i);
    if (forwarder->IsImmediateObject() ||
        forwardee->IsImmediateObject()) {
      return false;
    }
  }

  interpreter_->GCPrologue();  // Before creating forwarders!

  for (intptr_t i = 0; i < length; i++) {
    HeapObject forwarder = static_cast<HeapObject>(old->element(i));
    HeapObject forwardee = static_cast<HeapObject>(neu->element(i));

    ASSERT(!forwarder->IsForwardingCorpse());
    ASSERT(!forwardee->IsForwardingCorpse());

    forwardee->set_header_hash(forwarder->header_hash());

    intptr_t heap_size = forwarder->HeapSize();

    HeapObject::Initialize(forwarder->Addr(), kForwardingCorpseCid, heap_size);
    ASSERT(forwarder->IsForwardingCorpse());
    ForwardingCorpse corpse = static_cast<ForwardingCorpse>(forwarder);
    if (forwarder->heap_size() == 0) {
      corpse->set_overflow_size(heap_size);
    }
    ASSERT(forwarder->HeapSize() == heap_size);

    corpse->set_target(forwardee);
  }

  ForwardClassIds();
  ForwardRoots();
  ForwardHeap();  // With forwarded class ids.
  MournClassTableForwarded();

  interpreter_->GCEpilogue();

  return true;
}

void Heap::ForwardRoots() {
  for (intptr_t i = 0; i < handles_size_; i++) {
    ForwardPointer(handles_[i]);
  }

  Object* from;
  Object* to;
  interpreter_->RootPointers(&from, &to);
  for (Object* ptr = from; ptr <= to; ptr++) {
    ForwardPointer(ptr);
  }
  interpreter_->StackPointers(&from, &to);
  for (Object* ptr = from; ptr <= to; ptr++) {
    ForwardPointer(ptr);
  }
}

void Heap::ForwardHeap() {
  uword scan = to_.object_start();
  while (scan < top_) {
    HeapObject obj = HeapObject::FromAddr(scan);
    if (obj->cid() >= kFirstLegalCid) {
      ForwardClass(this, obj);
      Object* from;
      Object* to;
      obj->Pointers(&from, &to);
      for (Object* ptr = from; ptr <= to; ptr++) {
        ForwardPointer(ptr);
      }
    }
    scan += obj->HeapSize();
  }

  remembered_set_size_ = 0;
  for (Region* region = regions_; region != nullptr; region = region->next()) {
    uword scan = region->object_start();
    while (scan < region->object_end()) {
      HeapObject obj = HeapObject::FromAddr(scan);
      if (obj->cid() >= kFirstLegalCid) {
        obj->set_is_remembered(false);
        bool has_new_target = ForwardClass(this, obj);
        Object* from;
        Object* to;
        obj->Pointers(&from, &to);
        for (Object* ptr = from; ptr <= to; ptr++) {
          has_new_target |= ForwardPointer(ptr);
        }
        if (has_new_target) {
          AddToRememberedSet(obj);
        }
      }
      scan += obj->HeapSize();
    }
  }
}

void Heap::ForwardClassIds() {
  // For forwarded classes, use the cid of the old class. For most classes, we
  // could use the cid of the new class or a newlly allocated cid (provided all
  // the instances are updated). But for the classes whose representation is
  // defined by the VM we need to keep the fixed cids (e.g., kSmallIntegerCid),
  // so we may as well treat them all the same way.
  Object nil = interpreter_->nil_obj();
  for (intptr_t old_cid = kFirstLegalCid;
       old_cid < class_table_size_;
       old_cid++) {
    Behavior old_class = static_cast<Behavior>(class_table_[old_cid]);
    if (!old_class->IsForwardingCorpse()) {
      continue;
    }
    if (old_class->is_marked()) {
      continue;  // Already swapped: don't swap back.
    }

    Behavior new_class = static_cast<Behavior>(
        static_cast<ForwardingCorpse>(old_class)->target());
    ASSERT(!new_class->IsForwardingCorpse());

    if (new_class->id() != nil) {
      ASSERT(new_class->id()->IsSmallInteger());
      // Arrange for instances with new_cid to be migrated to old_cid.
      intptr_t new_cid = new_class->id()->value();
      class_table_[new_cid] = old_class;
    }

    new_class->set_id(SmallInteger::New(old_cid));
    class_table_[old_cid] = new_class;
    old_class->set_is_marked(true);
  }

  for (intptr_t cid = kFirstLegalCid; cid < class_table_size_; cid++) {
    Behavior klass = static_cast<Behavior>(class_table_[cid]);
    if (klass->IsForwardingCorpse()) {
      klass->set_is_marked(false);
    }
  }
}

intptr_t Heap::AllocateClassId() {
  intptr_t cid;
  if (class_table_free_ != 0) {
    cid = class_table_free_;
    class_table_free_ = static_cast<SmallInteger>(class_table_[cid])->value();
  } else if (class_table_size_ == class_table_capacity_) {
    if (TRACE_GROWTH) {
      OS::PrintErr("Scavenging to free class table entries\n");
    }
    CollectAll(kClassTable);
    if (class_table_free_ != 0) {
      cid = class_table_free_;
      class_table_free_ = static_cast<SmallInteger>(class_table_[cid])->value();
    } else {
      class_table_capacity_ += (class_table_capacity_ >> 1);
      if (TRACE_GROWTH) {
        OS::PrintErr("Growing class table to %" Pd "\n", class_table_capacity_);
      }
      Object* old_class_table = class_table_;
      class_table_ = new Object[class_table_capacity_];
      for (intptr_t i = 0; i < class_table_size_; i++) {
        class_table_[i] = old_class_table[i];
      }
#if defined(DEBUG)
      for (intptr_t i = class_table_size_; i < class_table_capacity_; i++) {
        class_table_[i] = static_cast<Object>(kUnallocatedWord);
      }
#endif
      delete[] old_class_table;
      cid = class_table_size_;
      class_table_size_++;
    }
  } else {
    cid = class_table_size_;
    class_table_size_++;
  }
#if defined(DEBUG)
  class_table_[cid] = static_cast<Object>(kUninitializedWord);
#endif
  return cid;
}

void Heap::InitializeAfterSnapshot() {
  // Classes are registered before they are known to have been initialized, so
  // we have to delay setting the ids in the class objects or risk them being
  // overwritten. After all snapshot objects have been initialized, we can
  // correct the ids in the classes.
  for (intptr_t cid = kFirstLegalCid; cid < class_table_size_; cid++) {
    Behavior cls = static_cast<Behavior>(class_table_[cid]);
    cls->AssertCouldBeBehavior();
    if (cls->id() == interpreter_->nil_obj()) {
      cls->set_id(SmallInteger::New(cid));
    } else {
      ASSERT(cls->id() == SmallInteger::New(cid) ||
             cls->id() == SmallInteger::New(kEphemeronCid));
    }
  }

  // Switch bump pointer allocation from old-space to new-space.
  intptr_t remaining = end_ - top_;
  if (remaining > 0) {
    freelist_.EnqueueRange(top_, remaining);
    old_size_ -= remaining;
  }
  top_ = to_.object_start();
  end_ = to_.limit();

  SetOldAllocationLimit();
}

static void Truncate(Array array, intptr_t new_size) {
  ASSERT(new_size >= 0);
  ASSERT(new_size <= array->Size());

  intptr_t old_heap_size = AllocationSize(array->Size() * sizeof(Object) +
                                          sizeof(Array::Layout));
  intptr_t new_heap_size = AllocationSize(new_size * sizeof(Object) +
                                          sizeof(Array::Layout));

  ASSERT(array->HeapSize() == old_heap_size);
  ASSERT(array->HeapSizeFromClass() == old_heap_size);
  array->set_size(SmallInteger::New(new_size));
  array->set_heap_size(new_heap_size);
  ASSERT(array->HeapSize() == new_heap_size);
  ASSERT(array->HeapSizeFromClass() == new_heap_size);

  intptr_t free_size = old_heap_size - new_heap_size;
  if (free_size != 0) {
    uword free_start = array->Addr() + new_heap_size;
    HeapObject object =
        HeapObject::Initialize(free_start, kFreeListElementCid, free_size);
    FreeListElement element = static_cast<FreeListElement>(object);
    if (element->heap_size() == 0) {
      ASSERT(free_size > kObjectAlignment);
      element->set_overflow_size(free_size);
    }
    ASSERT(object->HeapSize() == free_size);
    ASSERT(element->HeapSize() == free_size);
  }
}

static intptr_t CountInstancesOf(intptr_t count,
                                 intptr_t cid,
                                 uword start,
                                 uword end) {
  uword scan = start;
  while (scan < end) {
    HeapObject obj = HeapObject::FromAddr(scan);
    if (obj->cid() == cid) {
      count++;
    }
    scan += obj->HeapSize();
  }
  return count;
}

static intptr_t CollectInstancesOf(intptr_t cursor,
                                   Array result,
                                   intptr_t cid,
                                   uword start,
                                   uword end) {
  uword scan = start;
  while (scan < end) {
    HeapObject obj = HeapObject::FromAddr(scan);
    if (obj->cid() == cid) {
      result->set_element(cursor++, obj);
    }
    scan += obj->HeapSize();
  }
  return cursor;
}

Array Heap::InstancesOf(Behavior cls) {
  if (cls->id() == interpreter_->nil_obj()) {
    // Class not yet registered: no instance has been allocated.
    return AllocateArray(0);  // SAFEPOINT.
  }
  ASSERT(cls->id()->IsSmallInteger());
  intptr_t cid = cls->id()->value();

  intptr_t count = CountInstancesOf(0, cid,
                                    to_.object_start(), top_);
  for (Region* region = regions_; region != nullptr; region = region->next()) {
    count = CountInstancesOf(count, cid,
                             region->object_start(), region->object_end());
  }

  if (cid == kArrayCid) {
    count++;
  }
  if (TEST_SLOW_PATH) {
    count++;  // Ensure truncation is needed.
  }
  Array result = AllocateArray(count);  // SAFEPOINT

  intptr_t cursor = CollectInstancesOf(0, result, cid,
                                       to_.object_start(), top_);
  for (Region* region = regions_; region != nullptr; region = region->next()) {
    cursor = CollectInstancesOf(cursor, result, cid,
                                region->object_start(), region->object_end());
  }

  // There may be fewer instances than we initially counted if allocating the
  // result array triggered a GC.
  Truncate(result, cursor);
  return result;
}

static intptr_t CountReferencesTo(intptr_t count,
                                  Object target,
                                  uword start,
                                  uword end) {
  uword scan = start;
  while (scan < end) {
    HeapObject obj = HeapObject::FromAddr(scan);
    if (obj->cid() >= kFirstLegalCid) {
      Object* from;
      Object* to;
      obj->Pointers(&from, &to);
      for (Object* ptr = from; ptr <= to; ptr++) {
        if (*ptr == target) {
          count++;
          break;
        }
      }
    }
    scan += obj->HeapSize();
  }
  return count;
}

static intptr_t CollectReferencesTo(intptr_t cursor,
                                    Array result,
                                    Object target,
                                    uword start,
                                    uword end) {
  uword scan = start;
  while (scan < end) {
    HeapObject obj = HeapObject::FromAddr(scan);
    if (obj->cid() >= kFirstLegalCid) {
      Object* from;
      Object* to;
      obj->Pointers(&from, &to);
      for (Object* ptr = from; ptr <= to; ptr++) {
        if (*ptr == target) {
          result->set_element(cursor++, obj);
          break;
        }
      }
    }
    scan += obj->HeapSize();
  }
  return cursor;
}

Array Heap::ReferencesTo(Object target) {
  // TODO(rmacnak): Consider reifying activations in case they refer to target.
  intptr_t count = CountReferencesTo(0, target,
                                     to_.object_start(), top_);
  for (Region* region = regions_; region != nullptr; region = region->next()) {
    count = CountReferencesTo(count, target,
                              region->object_start(), region->object_end());
  }

  if (TEST_SLOW_PATH) {
    count++;  // Ensure truncation is needed.
  }
  Array result;
  {
    HandleScope h1(this, &target);
    result = AllocateArray(count);  // SAFEPOINT
  }

  intptr_t cursor = CollectReferencesTo(0, result, target,
                                        to_.object_start(), top_);
  for (Region* region = regions_; region != nullptr; region = region->next()) {
    cursor = CollectReferencesTo(cursor, result, target,
                                 region->object_start(), region->object_end());
  }

  // There may be fewer instances than we initially counted if allocating the
  // result array triggered a GC.
  Truncate(result, cursor);
  return result;
}

uword FreeList::TryAllocate(intptr_t size) {
  intptr_t index = IndexForSize(size);
  while (index < kSizeClasses) {
    if (free_lists_[index] != nullptr) {
      FreeListElement element = Dequeue(index);
      SplitAndRequeue(element, size);
      return element.Addr();
    }
    index++;
  }

  FreeListElement prev = nullptr;
  FreeListElement element = free_lists_[kSizeClasses];
  while (element != nullptr) {
    if (element->HeapSize() >= size) {
      if (prev == nullptr) {
        free_lists_[kSizeClasses] = element->next();
      } else {
        prev->set_next(element->next());
      }
      SplitAndRequeue(element, size);
      return element->Addr();
    }
    element = element->next();
  }

  return 0;
}

void FreeList::SplitAndRequeue(FreeListElement element, intptr_t size) {
  ASSERT(size > 0);
  ASSERT((size & kObjectAlignmentMask) == 0);
  ASSERT(element->HeapSize() >= size);
  intptr_t remaining_size = element->HeapSize() - size;
  ASSERT(remaining_size >= 0);
  if (remaining_size > 0) {
    uword remaining_addr = element->Addr() + size;
    EnqueueRange(remaining_addr, remaining_size);
  }
}

FreeListElement FreeList::Dequeue(intptr_t index) {
  FreeListElement element = free_lists_[index];
  if (element == nullptr) {
    return element;
  }
  ASSERT((element->next() == nullptr) || element->next()->IsFreeListElement());
  free_lists_[index] = element->next();
  return element;
}

void FreeList::Enqueue(FreeListElement element) {
  ASSERT(element->IsFreeListElement());
  intptr_t index = IndexForSize(element->HeapSize());
  element->set_next(free_lists_[index]);
  free_lists_[index] = element;
}

void FreeList::EnqueueRange(uword addr, intptr_t size) {
#if defined(DEBUG)
  memset(reinterpret_cast<void*>(addr), kUnallocatedByte, size);
#endif
  ASSERT(size >= kObjectAlignment);
  ASSERT((addr & kObjectAlignmentMask) == 0);
  ASSERT((size & kObjectAlignmentMask) == 0);
  HeapObject object = HeapObject::Initialize(addr, kFreeListElementCid, size);
  FreeListElement element = static_cast<FreeListElement>(object);
  if (element->heap_size() == 0) {
    ASSERT(size > kObjectAlignment);
    element->set_overflow_size(size);
  }
  ASSERT(object->HeapSize() == size);
  ASSERT(element->HeapSize() == size);
  Enqueue(element);
}

}  // namespace psoup
