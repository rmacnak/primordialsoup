// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/object.h"

#include <stdlib.h>
#include <strings.h>

#include "vm/heap.h"

namespace psoup {

intptr_t ByteString::hash_random_;


Behavior* Object::Klass(Heap* heap) const {
  return heap->ClassAt(ClassId());
}


intptr_t Object::HeapSizeFromClass() {
  ASSERT(IsHeapObject());

  switch (cid()) {
  case kIllegalCid:
  case kSmiCid:
    UNREACHABLE();
  case kMintCid:
  case kFloat64Cid:
    UNIMPLEMENTED();
  case kByteArrayCid:
    return AllocationSize(sizeof(ByteArray) +
                          sizeof(uint8_t) * ByteArray::Cast(this)->Size());
  case kByteStringCid:
    return AllocationSize(sizeof(ByteString) +
                          sizeof(uint8_t) * ByteString::Cast(this)->Size());
  case kWideStringCid:
    return AllocationSize(sizeof(WideString) +
                          sizeof(uint32_t) * WideString::Cast(this)->Size());
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


void Object::Pointers(Object*** from, Object*** to) {
  ASSERT(IsHeapObject());

  switch (cid()) {
  case kIllegalCid:
    UNREACHABLE();
  case kForwardingCorpseCid:
    // become-only
    *from = reinterpret_cast<Object**>(1);
    *to = reinterpret_cast<Object**>(0);
    return;
  case kSmiCid:
    UNREACHABLE();
  case kMintCid:
  case kFloat64Cid:
  case kByteArrayCid:
  case kByteStringCid:
  case kWideStringCid:
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
    // UNREACHABLE();
    // become-only
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


const char* Object::ToCString(Heap* heap) {
  char* result = 0;
  intptr_t length;
  Object* cls;
  Object* cls2;
  ByteString* name;

  switch (ClassId()) {
  case kIllegalCid:
    UNREACHABLE();
  case kSmiCid: {
    int r = asprintf(&result, "a Smi(%" Pd ")",
                     static_cast<SmallInteger*>(this)->value());
    ASSERT(r != -1);
    return result;
  }
  case kMintCid: {
    int r = asprintf(&result, "a Mint(%" Pd64 ")",
                     static_cast<MediumInteger*>(this)->value());
    ASSERT(r != -1);
    return result;
  }
  case kFloat64Cid: {
    int r = asprintf(&result, "a Float(%lf)",
                     Float64::Cast(this)->value());
    ASSERT(r != -1);
    return result;
  }
  case kByteArrayCid: {
    int r = asprintf(&result, "a ByteArray(%" Pd ")",
                     ByteArray::Cast(this)->Size());
    ASSERT(r != 1);
    return result;
  }
  case kByteStringCid:
    length = ByteString::Cast(this)->Size();
    result = reinterpret_cast<char*>(malloc(length + 14));
    memcpy(&result[0], "a ByteString ", 13);
    memcpy(&result[13], ByteString::Cast(this)->element_addr(0), length);
    result[length + 13] = '\0';
    for (intptr_t i = 0; i < length + 13; i++) {
      if (result[i] == '\r') {
        result[i] = '\n';
      }
    }
    return result;
  case kWideStringCid:
    // TODO(rmacnak): Need to convert to utf8 to print.
    length = WideString::Cast(this)->Size();
    result = reinterpret_cast<char*>(malloc(length + 2));
    result[0] = '#';
    memcpy(&result[1], WideString::Cast(this)->element_addr(0), length);
    result[length + 1] = '\0';
    for (intptr_t i = 0; i < length + 1; i++) {
      if (result[i] == '\r') {
        result[i] = '\n';
      }
    }
    return result;
  case kArrayCid: {
    int r = asprintf(&result, "an Array(%" Pd ")",
                     Array::Cast(this)->Size());
    ASSERT(r != -1);
    return result;
  }
  case kWeakArrayCid: {
    int r = asprintf(&result, "a WeakArray(%" Pd ")",
                     WeakArray::Cast(this)->Size());
    ASSERT(r != -1);
    return result;
  }
  case kEphemeronCid:
    return strdup("an Ephemeron");
  case kActivationCid:
    return strdup("an Activation");
  case kClosureCid:
    return strdup("a Closure");
  default:
    cls = heap->ClassAt(cid());
    if ((cls->HeapSize() / sizeof(uword)) == 8) {
      // A Metaclass.
      cls2 = static_cast<Metaclass*>(cls)->this_class();
      name = static_cast<Class*>(cls2)->name();
      if (!name->IsByteString()) {
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
      ASSERT(name->IsByteString());
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


void Object::Print(Heap* heap) {
  const char* cstr = ToCString(heap);
  OS::Print("%s\n", cstr);
  free((void*)cstr);  // NOLINT
}


}  // namespace psoup
