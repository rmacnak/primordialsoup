// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_OBJECT_H_
#define VM_OBJECT_H_

#include "vm/assert.h"
#include "vm/globals.h"
#include "vm/bitfield.h"
#include "vm/utils.h"

namespace psoup {

enum PointerBits {
  kSmiTag = 0,
  kHeapObjectTag = 1,
  kSmiTagSize = 1,
  kSmiTagMask = 1,
  kSmiTagShift = 1,
};

enum ObjectAlignment {
  // Alignment offsets are used to determine object age.
  kNewObjectAlignmentOffset = kWordSize,
  kOldObjectAlignmentOffset = 0,
  // Object sizes are aligned to kObjectAlignment.
  kObjectAlignment = 2 * kWordSize,
  kObjectAlignmentLog2 = kWordSizeLog2 + 1,
  kObjectAlignmentMask = kObjectAlignment - 1,

  kNewObjectBits = kNewObjectAlignmentOffset | kHeapObjectTag,
  kOldObjectBits = kOldObjectAlignmentOffset | kHeapObjectTag,
};

enum HeaderBits {
  // New object: Already copied to to-space (is forwarding pointer).
  // Old object: Already seen by the marker (is gray or black).
  kMarkBit = 0,

  // In remembered set.
  kRememberedBit = 1,

  // For symbols.
  kCanonicalBit = 2,

#if defined(ARCH_IS_32_BIT)
  kSizeFieldOffset = 8,
  kSizeFieldSize = 8,
  kClassIdFieldOffset = 16,
  kClassIdFieldSize = 16,
#elif defined(ARCH_IS_64_BIT)
  kSizeFieldOffset = 16,
  kSizeFieldSize = 16,
  kClassIdFieldOffset = 32,
  kClassIdFieldSize = 32,
#endif
};

enum ClassIds {
  kIllegalCid = 0,
  kForwardingCorpseCid = 1,
  kFreeListElementCid = 2,

  kFirstLegalCid = 3,

  kSmiCid = 3,
  kMintCid = 4,
  kBigintCid = 5,
  kFloat64Cid = 6,
  kByteArrayCid = 7,
  kStringCid = 8,
  kArrayCid = 9,
  kWeakArrayCid = 10,
  kEphemeronCid = 11,
  kActivationCid = 12,
  kClosureCid = 13,

  kFirstRegularObjectCid = 14,
};

class AbstractMixin;
class Activation;
class Array;
class Behavior;
class ByteArray;
class String;
class Closure;
class Ephemeron;
class Heap;
class Isolate;
class Method;
class Object;
class SmallInteger;
class WeakArray;

enum Barrier {
  kNoBarrier,
  kBarrier
};

#define HEAP_OBJECT_IMPLEMENTATION(klass)                                      \
 private:                                                                      \
  const klass* ptr() const {                                                   \
    ASSERT(IsHeapObject());                                                    \
    return reinterpret_cast<const klass*>(                                     \
        reinterpret_cast<uword>(this) - kHeapObjectTag);                       \
  }                                                                            \
  klass* ptr() {                                                               \
    ASSERT(IsHeapObject());                                                    \
    return reinterpret_cast<klass*>(                                           \
        reinterpret_cast<uword>(this) - kHeapObjectTag);                       \
  }                                                                            \
  friend class HeapObject;                                                     \
 public:                                                                       \
  static const klass* Cast(const Object* object) {                             \
    return static_cast<const klass*>(object);                                  \
  }                                                                            \
  static klass* Cast(Object* object) {                                         \
    return static_cast<klass*>(object);                                        \
  }                                                                            \

class Object {
 public:
  bool IsForwardingCorpse() const { return ClassId() == kForwardingCorpseCid; }
  bool IsFreeListElement() const { return ClassId() == kFreeListElementCid; }
  bool IsArray() const { return ClassId() == kArrayCid; }
  bool IsByteArray() const { return ClassId() == kByteArrayCid; }
  bool IsString() const { return ClassId() == kStringCid; }
  bool IsActivation() const { return ClassId() == kActivationCid; }
  bool IsMediumInteger() const { return ClassId() == kMintCid; }
  bool IsLargeInteger() const { return ClassId() == kBigintCid; }
  bool IsFloat64() const { return ClassId() == kFloat64Cid; }
  bool IsWeakArray() const { return ClassId() == kWeakArrayCid; }
  bool IsEphemeron() const { return ClassId() == kEphemeronCid; }
  bool IsClosure() const { return ClassId() == kClosureCid; }
  bool IsRegularObject() const { return ClassId() >= kFirstRegularObjectCid; }
  bool IsBytes() const {
    return (ClassId() == kByteArrayCid) || (ClassId() == kStringCid);
  }

  bool IsHeapObject() const {
    return (reinterpret_cast<uword>(this) & kSmiTagMask) == kHeapObjectTag;
  }
  bool IsImmediateObject() const { return IsSmallInteger(); }
  bool IsSmallInteger() const {
    return (reinterpret_cast<uword>(this) & kSmiTagMask) == kSmiTag;
  }
  bool IsOldObject() const {
    uword addr = reinterpret_cast<uword>(this);
    return (addr & kObjectAlignmentMask) == kOldObjectBits;
  }
  bool IsNewObject() const {
    uword addr = reinterpret_cast<uword>(this);
    return (addr & kObjectAlignmentMask) == kNewObjectBits;
  }
  // Like !IsHeapObject() || IsOldObject(), but compiles to a single branch.
  bool IsImmediateOrOldObject() const {
    const uword addr = reinterpret_cast<uword>(this);
    return (addr & kObjectAlignmentMask) != kNewObjectBits;
  }
  bool IsImmediateOrNewObject() const {
    const uword addr = reinterpret_cast<uword>(this);
    return (addr & kObjectAlignmentMask) != kOldObjectBits;
  }

  inline intptr_t ClassId() const;
  Behavior* Klass(Heap* heap) const;

  char* ToCString(Heap* heap) const;
  void Print(Heap* heap) const;
};

class HeapObject : public Object {
 public:
  void AssertCouldBeBehavior() const {
    ASSERT(IsHeapObject());
    ASSERT(IsRegularObject());
    // 8 slots for a class, 7 slots for a metaclass, plus 1 header.
    intptr_t heap_slots = heap_size() / sizeof(uword);
    ASSERT((heap_slots == 8) || (heap_slots == 10));
  }

  bool is_marked() const {
    return MarkBit::decode(ptr()->header_);
  }
  void set_is_marked(bool value) {
    ptr()->header_ = MarkBit::update(value, ptr()->header_);
  }
  bool is_remembered() const {
    return RememberedBit::decode(ptr()->header_);
  }
  void set_is_remembered(bool value) {
    ptr()->header_ = RememberedBit::update(value, ptr()->header_);
  }
  bool is_canonical() const {
    return CanonicalBit::decode(ptr()->header_);
  }
  void set_is_canonical(bool value) {
    ptr()->header_ = CanonicalBit::update(value, ptr()->header_);
  }
  intptr_t heap_size() const {
    return SizeField::decode(ptr()->header_) << kObjectAlignmentLog2;
  }
  intptr_t cid() const {
    return ClassIdField::decode(ptr()->header_);
  }
  void set_cid(intptr_t value) {
    ptr()->header_ = ClassIdField::update(value, ptr()->header_);
  }
  intptr_t header_hash() const {
    return ptr()->header_hash_;
  }
  void set_header_hash(intptr_t value) {
    ptr()->header_hash_ = value;
  }

  uword Addr() const {
    return reinterpret_cast<uword>(this) - kHeapObjectTag;
  }
  static HeapObject* FromAddr(uword addr) {
    return reinterpret_cast<HeapObject*>(addr + kHeapObjectTag);
  }

  static HeapObject* Initialize(uword addr,
                                intptr_t cid,
                                intptr_t heap_size) {
    ASSERT(cid != kIllegalCid);
    ASSERT((heap_size & kObjectAlignmentMask) == 0);
    ASSERT(heap_size > 0);
    intptr_t size_tag = heap_size >> kObjectAlignmentLog2;
    if (!SizeField::is_valid(size_tag)) {
      size_tag = 0;
      ASSERT(cid < kFirstRegularObjectCid);
    }
    uword header = 0;
    header = SizeField::update(size_tag, header);
    header = ClassIdField::update(cid, header);
    HeapObject* obj = FromAddr(addr);
    obj->ptr()->header_ = header;
    obj->ptr()->header_hash_ = 0;
    ASSERT(obj->cid() == cid);
    ASSERT(!obj->is_marked());
    return obj;
  }

  intptr_t HeapSize() const {
    ASSERT(IsHeapObject());
    intptr_t heap_size_from_tag = heap_size();
    if (heap_size_from_tag != 0) {
      return heap_size_from_tag;
    }
    return HeapSizeFromClass();
  }
  intptr_t HeapSizeFromClass() const;
  void Pointers(Object*** from, Object*** to);

 protected:
  template<typename type>
  type Load(type* addr, Barrier barrier = kBarrier) const {
    return *addr;
  }

  template<typename type>
  void Store(type* addr, type value, Barrier barrier) {
    *addr = value;
    if (barrier == kNoBarrier) {
      ASSERT(value->IsImmediateOrOldObject());
    } else {
      // Generational write barrier:
      if (IsOldObject() && value->IsNewObject() && !is_remembered()) {
        AddToRememberedSet();
      }
    }
  }

  uword header_;
  uword header_hash_;

 private:
  void AddToRememberedSet() const;

  const HeapObject* ptr() const {
    ASSERT(IsHeapObject());
    return reinterpret_cast<const HeapObject*>(
        reinterpret_cast<uword>(this) - kHeapObjectTag);
  }
  HeapObject* ptr() {
    ASSERT(IsHeapObject());
    return reinterpret_cast<HeapObject*>(
        reinterpret_cast<uword>(this) - kHeapObjectTag);
  }

  class MarkBit : public BitField<bool, kMarkBit, 1> {};
  class RememberedBit : public BitField<bool, kRememberedBit, 1> {};
  class CanonicalBit : public BitField<bool, kCanonicalBit, 1> {};
  class SizeField :
      public BitField<intptr_t, kSizeFieldOffset, kSizeFieldSize> {};
  class ClassIdField :
      public BitField<intptr_t, kClassIdFieldOffset, kClassIdFieldSize> {};
};

intptr_t Object::ClassId() const {
  if (IsSmallInteger()) {
    return kSmiCid;
  } else {
    return static_cast<const HeapObject*>(this)->cid();
  }
}

class ForwardingCorpse : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(ForwardingCorpse);

 public:
  Object* target() const {
    return reinterpret_cast<Object*>(ptr()->header_hash_);
  }
  void set_target(Object* value) {
    ptr()->header_hash_ = reinterpret_cast<intptr_t>(value);
  }
  intptr_t overflow_size() const {
    return ptr()->overflow_size_;
  }
  void set_overflow_size(intptr_t value) {
    ptr()->overflow_size_ = value;
  }

 private:
  intptr_t overflow_size_;
};

class FreeListElement : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(FreeListElement);

 public:
  FreeListElement* next() const {
    return reinterpret_cast<FreeListElement*>(ptr()->header_hash_);
  }
  void set_next(FreeListElement* value) {
    ASSERT((value == nullptr) || value->IsHeapObject());  // Tagged.
    ptr()->header_hash_ = reinterpret_cast<intptr_t>(value);
  }
  intptr_t overflow_size() const {
    return ptr()->overflow_size_;
  }
  void set_overflow_size(intptr_t value) {
    ptr()->overflow_size_ = value;
  }

 private:
  intptr_t overflow_size_;
};

class SmallInteger : public Object {
 public:
  static const intptr_t kBits = kBitsPerWord - 2;
  static const intptr_t kMaxValue = (static_cast<intptr_t>(1) << kBits) - 1;
  static const intptr_t kMinValue = -(static_cast<intptr_t>(1) << kBits);

  static SmallInteger* New(intptr_t value) {
    return reinterpret_cast<SmallInteger*>(value << kSmiTagShift);
  }

  intptr_t value() const {
    ASSERT(IsSmallInteger());
    return reinterpret_cast<intptr_t>(this) >> kSmiTagShift;
  }

#if defined(ARCH_IS_32_BIT)
  static bool IsSmiValue(int64_t value) {
    return (value >= static_cast<int64_t>(kMinValue)) &&
           (value <= static_cast<int64_t>(kMaxValue));
  }
#endif

  static bool IsSmiValue(intptr_t value) {
    // Check if the top two bits are equal.
    ASSERT(kSmiTagShift == 1);
    return (value ^ (value << 1)) >= 0;
  }
};

class MediumInteger : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(MediumInteger);

 public:
  static const int64_t kMinValue = kMinInt64;
  static const int64_t kMaxValue = kMaxInt64;

  int64_t value() const { return ptr()->value_; }
  void set_value(int64_t value) { ptr()->value_ = value; }

 private:
  int64_t value_;
};

#if defined(ARCH_IS_32_BIT)
typedef uint16_t digit_t;
typedef uint32_t ddigit_t;
typedef int32_t sddigit_t;
#elif defined(ARCH_IS_64_BIT)
typedef uint32_t digit_t;
typedef uint64_t ddigit_t;
typedef int64_t sddigit_t;
#endif
const ddigit_t kDigitBits = sizeof(digit_t) * kBitsPerByte;
const ddigit_t kDigitShift = sizeof(digit_t) * kBitsPerByte;
const ddigit_t kDigitBase = static_cast<ddigit_t>(1) << kDigitBits;
const ddigit_t kDigitMask = kDigitBase - static_cast<ddigit_t>(1);

class LargeInteger : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(LargeInteger);

 public:
  enum DivOperationType {
    kTruncated,
    kFloored,
    kExact,
  };

  enum DivResultType {
    kQuoitent,
    kRemainder
  };

  static LargeInteger* Expand(Object* integer, Heap* H);
  static Object* Reduce(LargeInteger* integer, Heap* H);

  static intptr_t Compare(LargeInteger* left, LargeInteger* right);

  static LargeInteger* Add(LargeInteger* left,
                           LargeInteger* right, Heap* H);
  static LargeInteger* Subtract(LargeInteger* left,
                                LargeInteger* right, Heap* H);
  static LargeInteger* Multiply(LargeInteger* left,
                                LargeInteger* right, Heap* H);
  static LargeInteger* Divide(DivOperationType op_type,
                              DivResultType result_type,
                              LargeInteger* left,
                              LargeInteger* right,
                              Heap* H);

  static LargeInteger* And(LargeInteger* left,
                           LargeInteger* right, Heap* H);
  static LargeInteger* Or(LargeInteger* left,
                          LargeInteger* right, Heap* H);
  static LargeInteger* Xor(LargeInteger* left,
                           LargeInteger* right, Heap* H);
  static LargeInteger* ShiftRight(LargeInteger* left,
                                  intptr_t raw_right, Heap* H);
  static LargeInteger* ShiftLeft(LargeInteger* left,
                                 intptr_t raw_right, Heap* H);

  static String* PrintString(LargeInteger* large, Heap* H);

  static double AsDouble(LargeInteger* integer);
  static bool FromDouble(double raw_value, Object** result, Heap* H);

  bool negative() const {
    return ptr()->negative_;
  }
  void set_negative(bool v) {
    ptr()->negative_ = v;
  }
  intptr_t size() const {
    return ptr()->size_;
  }
  void set_size(intptr_t value) {
    ptr()->size_ = value;
  }
  intptr_t capacity() const {
    return ptr()->capacity_;
  }
  void set_capacity(intptr_t value) {
    ptr()->capacity_ = value;
  }
  digit_t digit(intptr_t index) const {
    return ptr()->digits_[index];
  }
  void set_digit(intptr_t index, digit_t value) {
    ptr()->digits_[index] = value;
  }

 private:
  intptr_t capacity_;
  intptr_t negative_;  // TODO(rmacnak): Use a header bit?
  intptr_t size_;
  digit_t digits_[];
};

class RegularObject : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(RegularObject);

 public:
  Object* slot(intptr_t index) const { return Load(&ptr()->slots_[index]); }
  void set_slot(intptr_t index, Object* value, Barrier barrier = kBarrier) {
    Store(&ptr()->slots_[index], value, barrier);
  }

 private:
  Object** from() {
    return &ptr()->slots_[0];
  }
  Object* slots_[];
  Object** to() {
    intptr_t num_slots = (heap_size() - sizeof(HeapObject)) >> kWordSizeLog2;
    return &ptr()->slots_[num_slots - 1];
  }
};

class Array : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Array);

 public:
  SmallInteger* size() const { return Load(&ptr()->size_, kNoBarrier); }
  void set_size(SmallInteger* s) { Store(&ptr()->size_, s, kNoBarrier); }
  intptr_t Size() const { return size()->value(); }

  Object* element(intptr_t index) const {
    return Load(&ptr()->elements_[index]);
  }
  void set_element(intptr_t index, Object* value, Barrier barrier = kBarrier) {
    Store(&ptr()->elements_[index], value, barrier);
  }

 private:
  Object** from() {
    return &ptr()->elements_[0];
  }
  SmallInteger* size_;
  Object* elements_[];
  Object** to() {
    return &ptr()->elements_[Size() - 1];
  }
};

class WeakArray : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(WeakArray);

 public:
  SmallInteger* size() const { return Load(&ptr()->size_, kNoBarrier); }
  void set_size(SmallInteger* s) { Store(&ptr()->size_, s, kNoBarrier); }
  intptr_t Size() const { return size()->value(); }

  // Only accessed by the GC. Bypasses barrier, including assertions.
  WeakArray* next() const { return ptr()->next_; }
  void set_next(WeakArray* value) { ptr()->next_ = value; }

  Object* element(intptr_t index) const {
    return Load(&ptr()->elements_[index]);
  }
  void set_element(intptr_t index, Object* value, Barrier barrier = kBarrier) {
    Store(&ptr()->elements_[index], value, barrier);
  }

 private:
  Object** from() {
    return &ptr()->elements_[0];
  }
  SmallInteger* size_;  // Not visited.
  WeakArray* next_;  // Not visited.
  Object* elements_[];
  Object** to() {
    return &ptr()->elements_[Size() - 1];
  }
};

class Ephemeron : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Ephemeron);

 public:
  Object* key() const { return Load(&ptr()->key_); }
  Object** key_ptr() { return &ptr()->key_; }
  void set_key(Object* key, Barrier barrier = kBarrier) {
    Store(&ptr()->key_, key, barrier);
  }

  Object* value() const { return Load(&ptr()->value_); }
  Object** value_ptr() { return &ptr()->value_; }
  void set_value(Object* value, Barrier barrier = kBarrier) {
    Store(&ptr()->value_, value, barrier);
  }

  Object* finalizer() const { return Load(&ptr()->finalizer_); }
  Object** finalizer_ptr() { return &ptr()->finalizer_; }
  void set_finalizer(Object* finalizer, Barrier barrier = kBarrier) {
    Store(&ptr()->finalizer_, finalizer, barrier);
  }

  // Only accessed by the GC. Bypasses barrier, including assertions.
  Ephemeron* next() const { return ptr()->next_; }
  void set_next(Ephemeron* value) { ptr()->next_ = value; }

 private:
  Object** from() {
    return &ptr()->key_;
  }
  Object* key_;
  Object* value_;
  Object* finalizer_;
  Ephemeron* next_;  // Not visited.
  Object** to() {
    return &ptr()->finalizer_;
  }
};

class Bytes : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Bytes);

 public:
  SmallInteger* size() const { return Load(&ptr()->size_, kNoBarrier); }
  void set_size(SmallInteger* s) { Store(&ptr()->size_, s, kNoBarrier); }
  intptr_t Size() const { return size()->value(); }

  uint8_t element(intptr_t index) const {
    return *element_addr(index);
  }
  void set_element(intptr_t index, uint8_t value) {
    *element_addr(index) = value;
  }
  uint8_t* element_addr(intptr_t index) {
    uint8_t* elements = reinterpret_cast<uint8_t*>(
        reinterpret_cast<uword>(ptr()) + sizeof(Bytes));
    return &elements[index];
  }
  const uint8_t* element_addr(intptr_t index) const {
    const uint8_t* elements = reinterpret_cast<const uint8_t*>(
        reinterpret_cast<uword>(ptr()) + sizeof(Bytes));
    return &elements[index];
  }

 private:
  SmallInteger* size_;
};

class String : public Bytes {
  HEAP_OBJECT_IMPLEMENTATION(String);

 public:
  SmallInteger* EnsureHash(Isolate* isolate);
};

class ByteArray : public Bytes {
  HEAP_OBJECT_IMPLEMENTATION(ByteArray);
};

class Activation : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Activation);

 public:
  static const intptr_t kMaxTemps = 35;

  Activation* sender() const { return Load(&ptr()->sender_); }
  void set_sender(Activation* s, Barrier barrier = kBarrier) {
    Store(&ptr()->sender_, s, barrier);
  }

  SmallInteger* bci() const { return Load(&ptr()->bci_, kNoBarrier); }
  void set_bci(SmallInteger* i) { Store(&ptr()->bci_, i, kNoBarrier); }

  Method* method() const { return Load(&ptr()->method_); }
  void set_method(Method* m, Barrier barrier = kBarrier) {
    Store(&ptr()->method_, m, barrier);
  }

  Closure* closure() const { return Load(&ptr()->closure_); }
  void set_closure(Closure* m, Barrier barrier = kBarrier) {
    Store(&ptr()->closure_, m, barrier);
  }

  Object* receiver() const { return Load(&ptr()->receiver_); }
  void set_receiver(Object* o, Barrier barrier = kBarrier) {
    Store(&ptr()->receiver_, o, barrier);
  }

  SmallInteger* stack_depth() const {
    return Load(&ptr()->stack_depth_, kNoBarrier);
  }
  void set_stack_depth(SmallInteger* d) {
    Store(&ptr()->stack_depth_, d, kNoBarrier);
  }
  intptr_t StackDepth() const { return stack_depth()->value(); }

  Object* temp(intptr_t index) const { return Load(&ptr()->temps_[index]); }
  void set_temp(intptr_t index, Object* o, Barrier barrier = kBarrier) {
    Store(&ptr()->temps_[index], o, barrier);
  }

  void PopNAndPush(intptr_t drop_count, Object* value) {
    ASSERT(drop_count >= 0);
    ASSERT(drop_count <= StackDepth());
    set_stack_depth(SmallInteger::New(StackDepth() - drop_count + 1));
    set_temp(StackDepth() - 1, value);
  }
  void Push(Object* value) {
    PopNAndPush(0, value);
  }

  void PrintStack(Heap* heap);

 private:
  Object** from() {
    return reinterpret_cast<Object**>(&ptr()->sender_);
  }
  Activation* sender_;
  SmallInteger* bci_;
  Method* method_;
  Closure* closure_;
  Object* receiver_;
  SmallInteger* stack_depth_;
  Object* temps_[kMaxTemps];
  Object** to() {
    return reinterpret_cast<Object**>(&ptr()->temps_[StackDepth() - 1]);
  }
};

class Float64 : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Float64);

 public:
  double value() const { return ptr()->value_; }
  void set_value(double v) { ptr()->value_ = v; }

 private:
  double value_;
};

class Closure : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Closure);

 public:
  SmallInteger* num_copied() const {
    return Load(&ptr()->num_copied_, kNoBarrier);
  }
  void set_num_copied(SmallInteger* v) {
    Store(&ptr()->num_copied_, v, kNoBarrier);
  }
  intptr_t NumCopied() const { return num_copied()->value(); }

  Activation* defining_activation() const {
    return Load(&ptr()->defining_activation_);
  }
  void set_defining_activation(Activation* a, Barrier barrier = kBarrier) {
    Store(&ptr()->defining_activation_, a, barrier);
  }

  SmallInteger* initial_bci() const {
    return Load(&ptr()->initial_bci_, kNoBarrier);
  }
  void set_initial_bci(SmallInteger* bci) {
    Store(&ptr()->initial_bci_, bci, kNoBarrier);
  }

  SmallInteger* num_args() const {
    return Load(&ptr()->num_args_, kNoBarrier);
  }
  void set_num_args(SmallInteger* num) {
    Store(&ptr()->num_args_, num, kNoBarrier);
  }

  Object* copied(intptr_t index) const {
    return Load(&ptr()->copied_[index]);
  }
  void set_copied(intptr_t index, Object* o, Barrier barrier = kBarrier) {
    Store(&ptr()->copied_[index], o, barrier);
  }

 private:
  Object** from() {
    return reinterpret_cast<Object**>(&ptr()->num_copied_);
  }
  SmallInteger* num_copied_;
  Activation* defining_activation_;
  SmallInteger* initial_bci_;
  SmallInteger* num_args_;
  Object* copied_[];
  Object** to() {
    return reinterpret_cast<Object**>(&ptr()->copied_[NumCopied() - 1]);
  }
};

class Behavior : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Behavior);

 public:
  Behavior* superclass() const { return Load(&ptr()->superclass_); }
  Array* methods() const { return Load(&ptr()->methods_); }
  AbstractMixin* mixin() const { return Load(&ptr()->mixin_); }
  Object* enclosing_object() const { return Load(&ptr()->enclosing_object_); }
  SmallInteger* id() const { return Load(&ptr()->classid_, kNoBarrier); }
  void set_id(SmallInteger* id) { Store(&ptr()->classid_, id, kNoBarrier); }
  SmallInteger* format() const { return Load(&ptr()->format_, kNoBarrier); }

 private:
  Behavior* superclass_;
  Array* methods_;
  Object* enclosing_object_;
  AbstractMixin* mixin_;
  SmallInteger* classid_;
  SmallInteger* format_;
};

class Class : public Behavior {
  HEAP_OBJECT_IMPLEMENTATION(Class);

 public:
  String* name() const { return Load(&ptr()->name_); }
  WeakArray* subclasses() const { return Load(&ptr()->subclasses_); }

 private:
  String* name_;
  WeakArray* subclasses_;
};

class Metaclass : public Behavior {
  HEAP_OBJECT_IMPLEMENTATION(Metaclass);

 public:
  Class* this_class() const { return Load(&ptr()->this_class_); }

 private:
  Class* this_class_;
};

class AbstractMixin : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(AbstractMixin);

 public:
  String* name() const { return Load(&ptr()->name_); }
  Array* methods() const { return Load(&ptr()->methods_); }
  AbstractMixin* enclosing_mixin() const {
    return Load(&ptr()->enclosing_mixin_);
  }

 private:
  String* name_;
  Array* methods_;
  AbstractMixin* enclosing_mixin_;
};

class Method : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Method);

 public:
  SmallInteger* header() const { return Load(&ptr()->header_); }
  Array* literals() const { return Load(&ptr()->literals_); }
  ByteArray* bytecode() const { return Load(&ptr()->bytecode_); }
  AbstractMixin* mixin() const { return Load(&ptr()->mixin_); }
  String* selector() const { return Load(&ptr()->selector_); }
  Object* source() const { return Load(&ptr()->source_); }

  bool IsPublic() const {
    uword am = header()->value() >> 28;
    ASSERT((am == 0) || (am == 1) || (am == 2));
    return am == 0;
  }
  bool IsProtected() const {
    uword am = header()->value() >> 28;
    ASSERT((am == 0) || (am == 1) || (am == 2));
    return am == 1;
  }
  bool IsPrivate() const {
    uword am = header()->value() >> 28;
    ASSERT((am == 0) || (am == 1) || (am == 2));
    return am == 2;
  }
  intptr_t Primitive() const {
    return (header()->value() >> 16) & 1023;
  }
  intptr_t NumArgs() const {
    return (header()->value() >> 0) & 255;
  }
  intptr_t NumTemps() const {
    return (header()->value() >> 8) & 255;
  }

  const uint8_t* IP(const SmallInteger* bci) {
    return bytecode()->element_addr(bci->value() - 1);
  }
  SmallInteger* BCI(const uint8_t* ip) {
    return SmallInteger::New((ip - bytecode()->element_addr(0)) + 1);
  }

 private:
  SmallInteger* header_;
  Array* literals_;
  ByteArray* bytecode_;
  AbstractMixin* mixin_;
  String* selector_;
  Object* source_;
};

class Message : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(Message);

 public:
  void set_selector(String* selector, Barrier barrier = kBarrier) {
    Store(&ptr()->selector_, selector, barrier);
  }
  void set_arguments(Array* arguments, Barrier barrier = kBarrier) {
    Store(&ptr()->arguments_, arguments, barrier);
  }

 private:
  String* selector_;
  Array* arguments_;
};

class ObjectStore : public HeapObject {
  HEAP_OBJECT_IMPLEMENTATION(ObjectStore);

 public:
  class SmallInteger* size() const { return ptr()->array_size_; }
  Object* nil_obj() const { return ptr()->nil_; }
  Object* false_obj() const { return ptr()->false_; }
  Object* true_obj() const { return ptr()->true_; }
  Object* message_loop() const { return ptr()->message_loop_; }
  class Array* common_selectors() const { return ptr()->common_selectors_; }
  class String* does_not_understand() const {
    return ptr()->does_not_understand_;
  }
  class String* non_boolean_receiver() const {
    return ptr()->non_boolean_receiver_;
  }
  class String* cannot_return() const {
    return ptr()->cannot_return_;
  }
  class String* about_to_return_through() const {
    return ptr()->about_to_return_through_;
  }
  class String* unused_bytecode() const {
    return ptr()->unused_bytecode_;
  }
  class String* dispatch_message() const {
    return ptr()->dispatch_message_;
  }
  class String* dispatch_signal() const {
    return ptr()->dispatch_signal_;
  }
  Behavior* Array() const { return ptr()->Array_; }
  Behavior* ByteArray() const { return ptr()->ByteArray_; }
  Behavior* String() const { return ptr()->String_; }
  Behavior* Closure() const { return ptr()->Closure_; }
  Behavior* Ephemeron() const { return ptr()->Ephemeron_; }
  Behavior* Float64() const { return ptr()->Float64_; }
  Behavior* LargeInteger() const { return ptr()->LargeInteger_; }
  Behavior* MediumInteger() const { return ptr()->MediumInteger_; }
  Behavior* Message() const { return ptr()->Message_; }
  Behavior* SmallInteger() const { return ptr()->SmallInteger_; }
  Behavior* WeakArray() const { return ptr()->WeakArray_; }
  Behavior* Activation() const { return ptr()->Activation_; }
  Behavior* Method() const { return ptr()->Method_; }

 private:
  class SmallInteger* array_size_;
  Object* nil_;
  Object* false_;
  Object* true_;
  Object* message_loop_;
  class Array* common_selectors_;
  class String* does_not_understand_;
  class String* non_boolean_receiver_;
  class String* cannot_return_;
  class String* about_to_return_through_;
  class String* unused_bytecode_;
  class String* dispatch_message_;
  class String* dispatch_signal_;
  Behavior* Array_;
  Behavior* ByteArray_;
  Behavior* String_;
  Behavior* Closure_;
  Behavior* Ephemeron_;
  Behavior* Float64_;
  Behavior* LargeInteger_;
  Behavior* MediumInteger_;
  Behavior* Message_;
  Behavior* SmallInteger_;
  Behavior* WeakArray_;
  Behavior* Activation_;
  Behavior* Method_;
};

}  // namespace psoup

#endif  // VM_OBJECT_H_
