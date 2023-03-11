// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_HEAP_H_
#define VM_HEAP_H_

#include "vm/assert.h"
#include "vm/flags.h"
#include "vm/globals.h"
#include "vm/object.h"
#include "vm/utils.h"
#include "vm/virtual_memory.h"

namespace psoup {

class Interpreter;
class Region;

// Note these values are never valid Object.
#if defined(ARCH_IS_32_BIT)
static constexpr uword kUnallocatedWord = 0xabababab;
static constexpr uword kUninitializedWord = 0xcbcbcbcb;
#elif defined(ARCH_IS_64_BIT)
static constexpr uword kUnallocatedWord = 0xabababababababab;
static constexpr uword kUninitializedWord = 0xcbcbcbcbcbcbcbcb;
#endif
static constexpr uint8_t kUnallocatedByte = 0xab;
static constexpr uint8_t kUninitializedByte = 0xcb;

static size_t AllocationSize(size_t size) {
  return Utils::RoundUp(size, kObjectAlignment);
}

class Semispace {
 private:
  friend class Heap;

  void Allocate(size_t size) {
    memory_ = VirtualMemory::Allocate(size, VirtualMemory::kReadWrite,
                                      "primordialsoup-heap");
    ASSERT(Utils::IsAligned(memory_.base(), kObjectAlignment));
    ASSERT(memory_.size() == size);
#if defined(DEBUG)
    MarkUnallocated();
#endif
  }

  void Free() { memory_.Free(); }

  size_t size() const { return memory_.size(); }
  uword base() const { return memory_.base(); }
  uword limit() const { return memory_.limit(); }
  uword object_start() const {
    return memory_.base() + kNewObjectAlignmentOffset;
  }

  void MarkUnallocated() const {
    memset(reinterpret_cast<void*>(base()), kUnallocatedByte, size());
  }

  void ReadWrite() { memory_.Protect(VirtualMemory::kReadWrite); }
  void NoAccess() { memory_.Protect(VirtualMemory::kNoAccess); }

  Semispace() : memory_() {}

  VirtualMemory memory_;
};

class FreeList {
 private:
  friend class Heap;

  FreeList() { Reset(); }

  uword TryAllocate(size_t size);

  intptr_t IndexForSize(size_t size) {
    intptr_t index = size >> kObjectAlignmentLog2;
    if (index > kSizeClasses) {
      return kSizeClasses;
    }
    return index;
  }

  void SplitAndRequeue(FreeListElement element, size_t size);
  FreeListElement Dequeue(intptr_t index);
  void Enqueue(FreeListElement element);
  void EnqueueRange(uword address, size_t size);
  void Reset() {
    for (intptr_t i = 0; i <= kSizeClasses; i++) {
      free_lists_[i] = nullptr;
    }
  }

  static constexpr intptr_t kSizeClasses = 7;
  FreeListElement free_lists_[kSizeClasses + 1];
};

// C. J. Cheney. "A nonrecursive list compacting algorithm." Communications of
// the ACM. 1970.
//
// Barry Hayes. "Ephemerons: a New Finalization Mechanism." Object-Oriented
// Languages, Programming, Systems, and Applications. 1997.
class Heap {
 private:
  static constexpr intptr_t kLargeAllocation = 32 * KB;
  static constexpr size_t kInitialSemispaceCapacity = sizeof(uword) * MB / 8;
  static constexpr size_t kMaxSemispaceCapacity = 2 * sizeof(uword) * MB;
  static constexpr size_t kRegionSize = 256 * KB;

 public:
  enum Allocator { kNormal, kSnapshot };

  enum GrowthPolicy { kControlGrowth, kForceGrowth };

  enum Reason {
    kNewSpace,
    kTenure,
    kOldSpace,
    kClassTable,
    kPrimitive,
    kSnapshotTest
  };

  static const char* ReasonToCString(Reason reason) {
    switch (reason) {
      case kNewSpace: return "new-space";
      case kTenure: return "tenure";
      case kOldSpace: return "old-space";
      case kClassTable: return "class-table";
      case kPrimitive: return "primitive";
      case kSnapshotTest: return "snapshot-test";
    }
    UNREACHABLE();
    return nullptr;
  }

  Heap();
  ~Heap();

  void AddToRememberedSet(HeapObject object) {
    ASSERT(object->IsOldObject());
    ASSERT(!object->is_remembered());
    if (remembered_set_size_ == remembered_set_capacity_) {
      GrowRememberedSet();
    }
    remembered_set_[remembered_set_size_++] = object;
    object->set_is_remembered(true);
  }

  RegularObject AllocateRegularObject(intptr_t cid, intptr_t num_slots,
                                      Allocator allocator = kNormal) {
    ASSERT(cid == kEphemeronCid || cid >= kFirstRegularObjectCid);
    size_t heap_size = AllocationSize(sizeof(HeapObject::Layout) +
                                      num_slots * sizeof(Object));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, cid, heap_size);
    RegularObject result = static_cast<RegularObject>(obj);
    ASSERT(result->IsRegularObject() || result->IsEphemeron());
    ASSERT(result->HeapSize() == heap_size);

    intptr_t header_slots = sizeof(HeapObject::Layout) / sizeof(uword);
    if (((header_slots + num_slots) & 1) == 1) {
      // The leftover slot will be visited by the GC. Make it a valid oop.
      result->set_slot(num_slots, SmallInteger::New(0), kNoBarrier);
    }

    return result;
  }

  ByteArray AllocateByteArray(intptr_t num_bytes,
                              Allocator allocator = kNormal) {
    size_t heap_size = AllocationSize(sizeof(ByteArray::Layout) +
                                      num_bytes * sizeof(uint8_t));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kByteArrayCid, heap_size);
    ByteArray result = static_cast<ByteArray>(obj);
    result->set_size(SmallInteger::New(num_bytes));
    ASSERT(result->IsByteArray());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  String AllocateString(intptr_t num_bytes, Allocator allocator = kNormal) {
    size_t heap_size = AllocationSize(sizeof(String::Layout) +
                                      num_bytes * sizeof(uint8_t));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kStringCid, heap_size);
    String result = static_cast<String>(obj);
    result->set_size(SmallInteger::New(num_bytes));
    ASSERT(result->IsString());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Array AllocateArray(intptr_t num_slots, Allocator allocator = kNormal) {
    size_t heap_size = AllocationSize(sizeof(Array::Layout) +
                                      num_slots * sizeof(Object));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kArrayCid, heap_size);
    Array result = static_cast<Array>(obj);
    result->set_size(SmallInteger::New(num_slots));
    ASSERT(result->IsArray());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  WeakArray AllocateWeakArray(intptr_t num_slots,
                              Allocator allocator = kNormal) {
    size_t heap_size = AllocationSize(sizeof(WeakArray::Layout) +
                                      num_slots * sizeof(Object));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kWeakArrayCid, heap_size);
    WeakArray result = static_cast<WeakArray>(obj);
    result->set_size(SmallInteger::New(num_slots));
    ASSERT(result->IsWeakArray());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Closure AllocateClosure(intptr_t num_copied, Allocator allocator = kNormal) {
    size_t heap_size = AllocationSize(sizeof(Closure::Layout) +
                                      num_copied * sizeof(Object));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kClosureCid, heap_size);
    Closure result = static_cast<Closure>(obj);
    result->set_num_copied(SmallInteger::New(num_copied));
    ASSERT(result->IsClosure());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Activation AllocateActivation(Allocator allocator = kNormal) {
    size_t heap_size = AllocationSize(sizeof(Activation::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kActivationCid, heap_size);
    Activation result = static_cast<Activation>(obj);
    ASSERT(result->IsActivation());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  MediumInteger AllocateMediumInteger(Allocator allocator = kNormal) {
    size_t heap_size = AllocationSize(sizeof(MediumInteger::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kMediumIntegerCid, heap_size);
    MediumInteger result = static_cast<MediumInteger>(obj);
    ASSERT(result->IsMediumInteger());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  LargeInteger AllocateLargeInteger(intptr_t capacity,
                                    Allocator allocator = kNormal) {
    size_t heap_size = AllocationSize(sizeof(LargeInteger::Layout) +
                                      capacity * sizeof(digit_t));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kLargeIntegerCid, heap_size);
    LargeInteger result = static_cast<LargeInteger>(obj);
    result->set_capacity(capacity);
    ASSERT(result->IsLargeInteger());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Float AllocateFloat(Allocator allocator = kNormal) {
    size_t heap_size = AllocationSize(sizeof(Float::Layout));
    uword addr = Allocate(heap_size, allocator);
    HeapObject obj = HeapObject::Initialize(addr, kFloatCid, heap_size);
    Float result = static_cast<Float>(obj);
    ASSERT(result->IsFloat());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Message AllocateMessage();

  size_t Size() const {
    size_t new_size = top_ - to_.object_start();
    return new_size + old_size_;
  }

  void CollectAll(Reason reason) { MarkSweep(reason); }

  Array InstancesOf(Behavior cls);
  Array ReferencesTo(Object target);

  bool BecomeForward(Array old, Array neu);

  intptr_t AllocateClassId();
  void RegisterClass(intptr_t cid, Behavior cls) {
    ASSERT((class_table_[cid] == static_cast<Object>(kUninitializedWord)) ||
           (cid == kEphemeronCid));
    class_table_[cid] = cls;
    cls->set_id(SmallInteger::New(cid));
    cls->AssertCouldBeBehavior();
    ASSERT(cls->cid() >= kFirstRegularObjectCid);
  }
  Behavior ClassAt(intptr_t cid) const {
    ASSERT(cid > kIllegalCid);
    ASSERT(cid < class_table_size_);
    return static_cast<Behavior>(class_table_[cid]);
  }

  void InitializeInterpreter(Interpreter* interpreter) {
    ASSERT(interpreter_ == nullptr);
    interpreter_ = interpreter;
  }
  void InitializeAfterSnapshot();

  Interpreter* interpreter() const { return interpreter_; }

  intptr_t handles() const { return handles_size_; }
  void set_handles(intptr_t value) { handles_size_ = value; }

 private:
  void GrowRememberedSet();
  void ShrinkRememberedSet();

  // Scavenging.
  void Scavenge(Reason reason);
  void FlipSpaces();
  void ScavengeRoots();
  uword ScavengeToSpace(uword scan);
  void PushTenureStack(uword addr);
  uword PopTenureStack();
  bool IsTenureStackEmpty();
  void ProcessTenureStack();
  bool ScavengePointer(Object* ptr);
  void ScavengeOldObject(HeapObject obj);
  bool ScavengeClass(intptr_t cid);

  // Mark-sweep.
  void MarkSweep(Reason reason);
  void MarkRoots();
  void MarkObject(Object obj);
  void ProcessMarkStack();
  void Sweep();
  bool SweepRegion(Region* region);
  void SetOldAllocationLimit();

  // Ephemerons.
  void AddToEphemeronList(Ephemeron ephemeron_corpse);
  void ScavengeEphemeronList();
  void MarkEphemeronList();
  void MournEphemeronList();

  // WeakArrays.
  void AddToWeakList(WeakArray survivor);
  void MournWeakListScavenge();
  void MournWeakListMarkSweep();
  bool MournWeakPointerScavenge(Object* ptr);
  void MournWeakPointerMarkSweep(Object* ptr);

  // Weak class table.
  void MournClassTableScavenge();
  void MournClassTableMarkSweep();
  void MournClassTableForwarded();

  // Become.
  void ForwardClassIds();
  void ForwardRoots();
  void ForwardHeap();

  uword Allocate(size_t size, Allocator allocator) {
    ASSERT(Utils::IsAligned(size, kObjectAlignment));
    if (size < kLargeAllocation) {
      uword result = top_;
      if (result + size <= end_) {
        top_ = result + size;
#if defined(DEBUG)
        memset(reinterpret_cast<void*>(result), kUninitializedByte, size);
#endif
        return result;
      }
    }
    return allocator == kSnapshot ? AllocateSnapshot(size)
                                  : AllocateNormal(size);
  }

  uword AllocateNormal(size_t size);
  uword AllocateSnapshot(size_t size);
  uword AllocateCopy(size_t size);
  uword AllocateTenure(size_t size);
  uword AllocateOldSmall(size_t size, GrowthPolicy growth);
  uword AllocateOldLarge(size_t size, GrowthPolicy growth);

  Region* AllocateRegion(size_t region_size, GrowthPolicy growth);

#if defined(DEBUG)
  bool InFromSpace(HeapObject obj) {
    return (obj->Addr() >= from_.base()) && (obj->Addr() < from_.limit());
  }
  bool InToSpace(HeapObject obj) {
    return (obj->Addr() >= to_.base()) && (obj->Addr() < to_.limit());
  }
#endif

  // New space.
  uword top_;
  uword end_;
  uword survivor_end_;
  Semispace to_;
  Semispace from_;
  size_t next_semispace_capacity_;

  // Old space.
  Region* regions_;
  FreeList freelist_;
  size_t old_size_;
  size_t old_capacity_;
  size_t old_limit_;

  // Remembered set.
  HeapObject* remembered_set_;
  intptr_t remembered_set_size_;
  intptr_t remembered_set_capacity_;

  // Class table.
  Object* class_table_;
  intptr_t class_table_size_;
  intptr_t class_table_capacity_;
  intptr_t class_table_free_;

  // Roots.
  Interpreter* interpreter_;
  static constexpr intptr_t kHandlesCapacity = 8;
  Object* handles_[kHandlesCapacity];
  intptr_t handles_size_;
  friend class HandleScope;

  Ephemeron ephemeron_list_;
  WeakArray weak_list_;

  DISALLOW_COPY_AND_ASSIGN(Heap);
};

class HandleScope {
 public:
  HandleScope(Heap* heap, Object* ptr) : heap_(heap) {
    ASSERT(heap_->handles_size_ < Heap::kHandlesCapacity);
    heap_->handles_[heap_->handles_size_++] = ptr;
  }

  ~HandleScope() {
    heap_->handles_size_--;
  }

 private:
  Heap* heap_;
};

}  // namespace psoup

#endif  // VM_HEAP_H_
