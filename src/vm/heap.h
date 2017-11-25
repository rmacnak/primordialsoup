// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_HEAP_H_
#define VM_HEAP_H_

#include "vm/assert.h"
#include "vm/globals.h"
#include "vm/utils.h"
#include "vm/object.h"
#include "vm/os.h"
#include "vm/random.h"
#include "vm/flags.h"
#include "vm/virtual_memory.h"

namespace psoup {

class Heap;
class LookupCache;
class Isolate;

// Note that these values are odd, so they will not hide as SmallIntegers.
// These values are not well-formed objects.
#if defined(ARCH_IS_64_BIT)
static const uword kUnallocatedWord = 0xabababababababab;
static const uword kUninitializedWord = 0xcbcbcbcbcbcbcbcb;
#else
static const uword kUnallocatedWord = 0xabababab;
static const uword kUninitializedWord = 0xcbcbcbcb;
#endif
static const uint8_t kUnallocatedByte = 0xab;
static const uint8_t kUninitializedByte = 0xcb;

class Semispace {
 private:
  friend class Heap;

  void Allocate(size_t size) {
    memory_ = VirtualMemory::Allocate(size,
                                      VirtualMemory::kReadWrite,
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

  Semispace() : memory_() { }

  VirtualMemory memory_;
};

// C. J. Cheney. "A nonrecursive list compacting algorithm." Communications of
// the ACM. 1970.
//
// Barry Hayes. "Ephemerons: a New Finalization Mechanism." Object-Oriented
// Languages, Programming, Systems, and Applications. 1997.
class Heap {
 public:
  static const intptr_t kInitialSemispaceSize = sizeof(uword) * MB;
  static const intptr_t kMaxSemispaceSize = 16 * sizeof(uword) * MB;
  static const intptr_t kMaxHandles = 8;

  Heap(Isolate* isolate, uint64_t seed);
  ~Heap();

  static intptr_t AllocationSize(intptr_t size) {
    return Utils::RoundUp(size, kObjectAlignment);
  }

  size_t used() const { return top_ - to_.object_start(); }

  RegularObject* AllocateRegularObject(intptr_t cid, intptr_t num_slots) {
    ASSERT(cid == kEphemeronCid || cid >= kFirstRegularObjectCid);
    const intptr_t heap_size =
        AllocationSize(num_slots * sizeof(Object*) + sizeof(Object));
    uword raw = Allocate(heap_size);
    Object* obj = Object::InitializeObject(raw, cid, heap_size);
    RegularObject* result = static_cast<RegularObject*>(obj);
    ASSERT(result->IsRegularObject() || result->IsEphemeron());
    ASSERT(result->HeapSize() == heap_size);

    const intptr_t header_slots = sizeof(Object) / sizeof(uword);
    if (((header_slots + num_slots) & 1) == 1) {
      // The leftover slot will be visited by the GC. Make it a valid oop.
      result->set_slot(num_slots, SmallInteger::New(0));
    }

    return result;
  }

  ByteArray* AllocateByteArray(intptr_t num_chars) {
    const intptr_t heap_size =
        AllocationSize(num_chars * sizeof(uint8_t) + sizeof(ByteArray));
    uword raw = Allocate(heap_size);
    Object* obj = Object::InitializeObject(raw, kByteArrayCid, heap_size);
    ByteArray* result = static_cast<ByteArray*>(obj);
    result->set_size(SmallInteger::New(num_chars));
    ASSERT(result->IsByteArray());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  ByteString* AllocateByteString(intptr_t num_chars) {
    const intptr_t heap_size =
        AllocationSize(num_chars * sizeof(uint8_t) + sizeof(ByteString));
    uword raw = Allocate(heap_size);
    Object* obj = Object::InitializeObject(raw, kByteStringCid, heap_size);
    ByteString* result = static_cast<ByteString*>(obj);
    result->set_size(SmallInteger::New(num_chars));
    result->set_hash(SmallInteger::New(0));
    ASSERT(result->IsByteString());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  WideString* AllocateWideString(intptr_t num_chars) {
    const intptr_t heap_size =
        AllocationSize(num_chars * sizeof(uint32_t) + sizeof(WideString));
    uword raw = Allocate(heap_size);
    Object* obj = Object::InitializeObject(raw, kWideStringCid, heap_size);
    WideString* result = static_cast<WideString*>(obj);
    result->set_size(SmallInteger::New(num_chars));
    result->set_hash(SmallInteger::New(0));
    ASSERT(result->IsWideString());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Array* AllocateArray(intptr_t num_slots) {
    const intptr_t heap_size =
        AllocationSize(num_slots * sizeof(Object*) + sizeof(Array));
    uword raw = Allocate(heap_size);
    Object* obj = Object::InitializeObject(raw, kArrayCid, heap_size);
    Array* result = static_cast<Array*>(obj);
    result->set_size(SmallInteger::New(num_slots));
    ASSERT(result->IsArray());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  WeakArray* AllocateWeakArray(intptr_t num_slots) {
    const intptr_t heap_size =
        AllocationSize(num_slots * sizeof(Object*) + sizeof(WeakArray));
    uword raw = Allocate(heap_size);
    Object* obj = Object::InitializeObject(raw, kWeakArrayCid, heap_size);
    WeakArray* result = static_cast<WeakArray*>(obj);
    result->set_size(SmallInteger::New(num_slots));
    ASSERT(result->IsWeakArray());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Closure* AllocateClosure(intptr_t num_copied) {
    const intptr_t heap_size =
        AllocationSize(num_copied * sizeof(Object*) + sizeof(Closure));
    uword raw = Allocate(heap_size);
    Object* obj = Object::InitializeObject(raw, kClosureCid, heap_size);
    Closure* result = static_cast<Closure*>(obj);
    result->set_num_copied(num_copied);
    ASSERT(result->IsClosure());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Activation* AllocateActivation() {
    const intptr_t heap_size = AllocationSize(sizeof(Activation));
    uword raw = Allocate(heap_size);
    Object* obj = Object::InitializeObject(raw, kActivationCid, heap_size);
    Activation* result = static_cast<Activation*>(obj);
    ASSERT(result->IsActivation());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  MediumInteger* AllocateMediumInteger() {
    const intptr_t heap_size = AllocationSize(sizeof(MediumInteger));
    uword raw = Allocate(heap_size);
    Object* obj = Object::InitializeObject(raw, kMintCid, heap_size);
    MediumInteger* result = static_cast<MediumInteger*>(obj);
    ASSERT(result->IsMediumInteger());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  LargeInteger* AllocateLargeInteger(intptr_t capacity) {
    const intptr_t heap_size =
        AllocationSize(capacity * sizeof(digit_t) + sizeof(LargeInteger));
    uword raw = Allocate(heap_size);
    Object* obj = Object::InitializeObject(raw, kBigintCid, heap_size);
    LargeInteger* result = static_cast<LargeInteger*>(obj);
    result->set_capacity(capacity);
    ASSERT(result->IsLargeInteger());
    ASSERT(result->HeapSize() == heap_size);
    ASSERT(result->HeapSizeFromClass() == heap_size);
    return result;
  }

  Float64* AllocateFloat64() {
    const intptr_t heap_size = AllocationSize(sizeof(Float64));
    uword raw = Allocate(heap_size);
    Object* obj = Object::InitializeObject(raw, kFloat64Cid, heap_size);
    Float64* result = static_cast<Float64*>(obj);
    ASSERT(result->IsFloat64());
    ASSERT(result->HeapSize() == heap_size);
    return result;
  }

  Message* AllocateMessage() {
    Behavior* behavior = object_store()->Message();
    ASSERT(behavior->IsRegularObject());
    behavior->AssertCouldBeBehavior();
    SmallInteger* id = behavior->id();
    if (id == object_store()->nil_obj()) {
      id = SmallInteger::New(AllocateClassId());
      behavior->set_id(id);
      RegisterClass(id->value(), behavior);
    }
    ASSERT(id->IsSmallInteger());
    SmallInteger* format = behavior->format();
    ASSERT(format->IsSmallInteger());
    intptr_t num_slots = format->value();
    ASSERT(num_slots == 2);
    Object* new_instance = AllocateRegularObject(id->value(),
                                                 num_slots);
    return static_cast<Message*>(new_instance);
  }

#if RECYCLE_ACTIVATIONS
  Activation* AllocateOrRecycleActivation() {
    Activation* result = recycle_list_;
    if (result != 0) {
      recycle_list_ = result->sender();
      return result;
    }
    return AllocateActivation();
  }
  void RecycleActivation(Activation* a) {
    a->set_sender(recycle_list_);
    recycle_list_ = a;
  }
#endif

  void PrintStack();

  void Scavenge();
  void Grow(intptr_t size_requested, const char* reason);

  intptr_t CountInstances(intptr_t cid);
  intptr_t CollectInstances(intptr_t cid, Array* array);

  bool BecomeForward(Array* old, Array* neu);

  intptr_t AllocateClassId();
  void RegisterClass(intptr_t cid, Behavior* cls) {
    ASSERT(class_table_[cid] == reinterpret_cast<Object*>(kUninitializedWord));
    class_table_[cid] = cls;
    cls->set_id(SmallInteger::New(cid));
    cls->AssertCouldBeBehavior();
    ASSERT(cls->cid() >= kFirstRegularObjectCid);
  }
  Behavior* ClassAt(intptr_t cid) const {
    ASSERT(cid > kIllegalCid);
    ASSERT(cid < class_table_top_);
    return reinterpret_cast<Behavior*>(class_table_[cid]);
  }

  void InitializeRoot(ObjectStore* os) {
    ASSERT(object_store_ == NULL);
    object_store_ = os;
    ASSERT(object_store_->IsArray());

    // GC safe value until we create the initial message.
    current_activation_ = reinterpret_cast<Activation*>(SmallInteger::New(0));
  }
  void InitializeLookupCache(LookupCache* cache) {
    ASSERT(lookup_cache_ == NULL);
    lookup_cache_ = cache;
  }

  ObjectStore* object_store() const { return object_store_; }
  Activation* activation() const { return current_activation_; }
  void set_activation(Activation* new_activation) {
    ASSERT(new_activation->IsActivation());
    current_activation_ = new_activation;
  }

  Isolate* isolate() const { return isolate_; }

  uintptr_t string_hash_salt() { return string_hash_salt_; }
  uint64_t NextIdentityHash() { return identity_hash_random_.NextUInt64(); }

  void DropHandles() {
    handles_top_ = 0;
  }

 private:
  void FlipSpaces();
  void ProcessRoots();
  void MournClassTable();
  uword ProcessToSpace(uword scan);
  void ScavengePointer(Object** ptr);
  void ScavengeClass(intptr_t cid);

  void AddToEphemeronList(Ephemeron* ephemeron_corpse);
  void ProcessEphemeronList();
  void MournEphemeronList();

  void AddToWeakList(WeakArray* weak_corpse);
  void MournWeakList();
  void MournWeakPointer(Object** ptr);

  void ClearCaches();

  void ForwardRoots();
  void ForwardClassTable();
  void ForwardToSpace();

  uword TryAllocate(intptr_t size) {
    ASSERT(Utils::IsAligned(size, kObjectAlignment));
    uword result = top_;
    intptr_t remaining = end_ - top_;
    if (remaining < size) {
      return 0;
    }
    ASSERT((result & kObjectAlignmentMask) == kNewObjectAlignmentOffset);
    top_ += size;
    return result;
  }
  uword Allocate(intptr_t size);

#if defined(DEBUG)
  bool InFromSpace(Object* obj) {
    return (obj->Addr() >= from_.base()) && (obj->Addr() < from_.limit());
  }
  bool InToSpace(Object* obj) {
    return (obj->Addr() >= to_.base()) && (obj->Addr() < to_.limit());
  }
#endif

  uword top_;
  uword end_;
  Semispace to_;
  Semispace from_;

  Ephemeron* ephemeron_list_;
  WeakArray* weak_list_;

  Object** class_table_;
  intptr_t class_table_capacity_;
  intptr_t class_table_top_;
  intptr_t class_table_free_;

  ObjectStore* object_store_;
  Activation* current_activation_;

#if RECYCLE_ACTIVATIONS
  Activation* recycle_list_;
#endif

  LookupCache* lookup_cache_;
  Object** handles_[kMaxHandles];
  intptr_t handles_top_;
  friend class HandleScope;

  uintptr_t string_hash_salt_;
  Random identity_hash_random_;

  Isolate* isolate_;

  DISALLOW_COPY_AND_ASSIGN(Heap);
};


class HandleScope {
 public:
  HandleScope(Heap* heap, Object** ptr) : heap_(heap) {
    heap_->handles_[heap_->handles_top_] = ptr;
    heap_->handles_top_++;
    ASSERT(heap_->handles_top_ <= Heap::kMaxHandles);
  }

  ~HandleScope() {
    heap_->handles_top_--;
  }

 private:
  Heap* heap_;
};

}  // namespace psoup

#endif  // VM_HEAP_H_
