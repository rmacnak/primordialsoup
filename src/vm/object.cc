// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/object.h"

#include "vm/heap.h"
#include "vm/isolate.h"
#include "vm/os.h"

namespace psoup {

Behavior* Object::Klass(Heap* heap) const {
  return heap->ClassAt(ClassId());
}


intptr_t HeapObject::HeapSizeFromClass() const {
  ASSERT(IsHeapObject());

  switch (cid()) {
  case kIllegalCid:
    UNREACHABLE();
  case kForwardingCorpseCid:
    return static_cast<const ForwardingCorpse*>(this)->overflow_size();
  case kFreeListElementCid:
    return static_cast<const FreeListElement*>(this)->overflow_size();
  case kSmiCid:
    UNREACHABLE();
  case kMintCid:
    return AllocationSize(sizeof(MediumInteger));
  case kFloat64Cid:
    return AllocationSize(sizeof(Float64));
  case kBigintCid:
    return AllocationSize(sizeof(LargeInteger) +
        sizeof(digit_t) * LargeInteger::Cast(this)->capacity());
  case kByteArrayCid:
    return AllocationSize(sizeof(ByteArray) +
                          sizeof(uint8_t) * ByteArray::Cast(this)->Size());
  case kStringCid:
    return AllocationSize(sizeof(String) +
                          sizeof(uint8_t) * String::Cast(this)->Size());
  case kArrayCid:
    return AllocationSize(sizeof(Array) +
                          sizeof(Object*) * Array::Cast(this)->Size());
  case kWeakArrayCid:
    return AllocationSize(sizeof(WeakArray) +
                          sizeof(Object*) * WeakArray::Cast(this)->Size());
  case kEphemeronCid:
    return AllocationSize(sizeof(Ephemeron));
  case kActivationCid:
    return AllocationSize(sizeof(Activation));
  case kClosureCid:
    return AllocationSize(sizeof(Closure) +
                          sizeof(Object*) * Closure::Cast(this)->num_copied());
  default:
    UNREACHABLE();
    // Need to get num slots from class.
    return -1;
  }
}


void HeapObject::Pointers(Object*** from, Object*** to) {
  ASSERT(IsHeapObject());

  switch (cid()) {
  case kIllegalCid:
  case kForwardingCorpseCid:
  case kFreeListElementCid:
  case kSmiCid:
    UNREACHABLE();
  case kMintCid:
  case kBigintCid:
  case kFloat64Cid:
  case kByteArrayCid:
  case kStringCid:
    // No pointers (or only smis for size/hash)
    *from = reinterpret_cast<Object**>(1);
    *to = reinterpret_cast<Object**>(0);
    return;
  case kArrayCid:
    *from = Array::Cast(this)->from();
    *to = Array::Cast(this)->to();
    return;
  case kWeakArrayCid:
    *from = WeakArray::Cast(this)->from();
    *to = WeakArray::Cast(this)->to();
    return;
  case kEphemeronCid:
    *from = RegularObject::Cast(this)->from();
    *to = RegularObject::Cast(this)->to();
    return;
  case kActivationCid:
    *from = Activation::Cast(this)->from();
    *to = Activation::Cast(this)->to();
    return;
  case kClosureCid:
    *from = Closure::Cast(this)->from();
    *to = Closure::Cast(this)->to();
    return;
  default:
    *from = RegularObject::Cast(this)->from();
    *to = RegularObject::Cast(this)->to();
    return;
  }
}


void HeapObject::AddToRememberedSet() const {
  Isolate* isolate = Isolate::Current();
  ASSERT(isolate != NULL);
  isolate->heap()->AddToRememberedSet(const_cast<HeapObject*>(this));
}


char* Object::ToCString(Heap* heap) const {
  char* result = NULL;
  intptr_t length;
  Behavior* cls;
  Behavior* cls2;
  String* name;

  switch (ClassId()) {
  case kIllegalCid:
  case kForwardingCorpseCid:
  case kFreeListElementCid:
    UNREACHABLE();
  case kSmiCid:
    return OS::PrintStr("a SmallInteger(%" Pd ")",
                        static_cast<const SmallInteger*>(this)->value());
  case kMintCid:
    return OS::PrintStr("a MediumInteger(%" Pd64 ")",
                        MediumInteger::Cast(this)->value());
  case kBigintCid:
    return OS::PrintStr("a LargeInteger(%" Pd "digits)",
                        LargeInteger::Cast(this)->size());
  case kFloat64Cid:
    return OS::PrintStr("a Float(%lf)", Float64::Cast(this)->value());
  case kByteArrayCid:
    return OS::PrintStr("a ByteArray(%" Pd ")", ByteArray::Cast(this)->Size());
  case kStringCid:
    length = String::Cast(this)->Size();
    result = reinterpret_cast<char*>(malloc(length + 14));
    memcpy(&result[0], "a String ", 9);
    memcpy(&result[9], String::Cast(this)->element_addr(0), length);
    result[length + 9] = '\0';
    for (intptr_t i = 0; i < length + 9; i++) {
      if (result[i] == '\r') {
        result[i] = '\n';
      }
    }
    return result;
  case kArrayCid:
    return OS::PrintStr("an Array(%" Pd ")", Array::Cast(this)->Size());
  case kWeakArrayCid:
    return OS::PrintStr("a WeakArray(%" Pd ")", WeakArray::Cast(this)->Size());
  case kEphemeronCid:
    return strdup("an Ephemeron");
  case kActivationCid:
    return strdup("an Activation");
  case kClosureCid:
    return strdup("a Closure");
  default:
    cls = Klass(heap);
    if ((cls->HeapSize() / sizeof(uword)) == 8) {
      // A Metaclass.
      cls2 = static_cast<Metaclass*>(cls)->this_class();
      name = static_cast<Class*>(cls2)->name();
      if (!name->IsString()) {
        return strdup("Instance of uninitialized metaclass?");
      }
      length = name->Size();
      result = reinterpret_cast<char*>(malloc(length + 12 + 6 + 1));
      memcpy(&result[0], "instance of ", 13);
      memcpy(&result[12], name->element_addr(0), length);
      memcpy(&result[12+length], " class", 6 + 1);
      return result;
    } else if ((cls->HeapSize() / sizeof(uword)) == 10) {
      // A Class.
      name = static_cast<Class*>(cls)->name();
      ASSERT(name->IsString());
      length = name->Size();
      result = reinterpret_cast<char*>(malloc(length + 12 + 1));
      memcpy(&result[0], "instance of ", 13);
      memcpy(&result[12], name->element_addr(0), length);
      result[12+length] = 0;
      return result;
    }
    UNREACHABLE();
    return strdup("a RegularObject");
  }
}


void Object::Print(Heap* heap) const {
  char* cstr = ToCString(heap);
  OS::Print("%s\n", cstr);
  free(cstr);
}


#if defined(ARCH_IS_32_BIT)
static uintptr_t kFNVPrime = 16777619;
#elif defined(ARCH_IS_64_BIT)
static uintptr_t kFNVPrime = 1099511628211;
#endif


SmallInteger* String::EnsureHash(Isolate* isolate) {
  if (header_hash() == 0) {
    // FNV-1a hash
    intptr_t length = Size();
    uintptr_t h = length + 1;
    for (intptr_t i = 0; i < length; i++) {
      h = h ^ element(i);
      h = h * kFNVPrime;
    }
    h = h ^ isolate->salt();
    h = h & SmallInteger::kMaxValue;
    if (h == 0) {
      h = 1;
    }
    set_header_hash(h);
  }
  return SmallInteger::New(header_hash());
}

}  // namespace psoup
