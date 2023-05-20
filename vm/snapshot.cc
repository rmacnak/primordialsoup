// Copyright (c) 2016, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/snapshot.h"

#include <type_traits>

#include "vm/heap.h"
#include "vm/interpreter.h"
#include "vm/object.h"
#include "vm/os.h"

namespace psoup {

class Cluster {
 public:
  Cluster() : ref_start_(0), ref_stop_(0) {}

  virtual ~Cluster() {}

  virtual void ReadNodes(Deserializer* d, Heap* h) = 0;
  virtual void ReadEdges(Deserializer* d, Heap* h) = 0;

 protected:
  intptr_t ref_start_;
  intptr_t ref_stop_;
};

class RegularObjectCluster : public Cluster {
 public:
  explicit RegularObjectCluster(intptr_t format, intptr_t cid = kIllegalCid)
      : format_(format), cid_(cid) {}
  ~RegularObjectCluster() {}

  void ReadNodes(Deserializer* d, Heap* h) {
    intptr_t num_objects = d->ReadLEB128();
    if (cid_ == kIllegalCid) {
      cid_ = h->AllocateClassId();
    }
    ref_start_ = d->next_ref();
    ref_stop_ = ref_start_ + num_objects;
    for (intptr_t i = 0; i < num_objects; i++) {
      Object object = h->AllocateRegularObject(cid_, format_, Heap::kSnapshot);
      d->RegisterRef(object);
    }
    ASSERT(d->next_ref() == ref_stop_);
  }

  void ReadEdges(Deserializer* d, Heap* h) {
    Object cls = d->ReadRef();
    h->RegisterClass(cid_, static_cast<Behavior>(cls));

    for (intptr_t i = ref_start_; i < ref_stop_; i++) {
      RegularObject object = static_cast<RegularObject>(d->Ref(i));
      for (intptr_t j = 0; j < format_; j++) {
        object->set_slot(j, d->ReadRef(), kNoBarrier);
      }
    }
  }

 private:
  intptr_t format_;
  intptr_t cid_;
};

class ByteArrayCluster : public Cluster {
 public:
  ByteArrayCluster() {}
  ~ByteArrayCluster() {}

  void ReadNodes(Deserializer* d, Heap* h) {
    intptr_t num_objects = d->ReadLEB128();
    ref_start_ = d->next_ref();
    ref_stop_ = ref_start_ + num_objects;
    for (intptr_t i = 0; i < num_objects; i++) {
      intptr_t size = d->ReadLEB128();
      ByteArray object = h->AllocateByteArray(size, Heap::kSnapshot);
      for (intptr_t j = 0; j < size; j++) {
        object->set_element(j, d->Read<uint8_t>());
      }
      d->RegisterRef(object);
      ASSERT(object->IsByteArray());
    }
    ASSERT(d->next_ref() == ref_stop_);
  }

  void ReadEdges(Deserializer* d, Heap* h) {}
};

class StringCluster : public Cluster {
 public:
  StringCluster() {}
  ~StringCluster() {}

  void ReadNodes(Deserializer* d, Heap* h) {
    ReadNodes(d, h, false);
    ReadNodes(d, h, true);
  }

  void ReadNodes(Deserializer* d, Heap* h, bool is_canonical) {
    intptr_t num_objects = d->ReadLEB128();
    ref_start_ = d->next_ref();
    ref_stop_ = ref_start_ + num_objects;
    for (intptr_t i = 0; i < num_objects; i++) {
      intptr_t size = d->ReadLEB128();
      String object = h->AllocateString(size, Heap::kSnapshot);
      ASSERT(!object->is_canonical());
      object->set_is_canonical(is_canonical);
      for (intptr_t j = 0; j < size; j++) {
        object->set_element(j, d->Read<uint8_t>());
      }
      d->RegisterRef(object);
    }
    ASSERT(d->next_ref() == ref_stop_);
  }

  void ReadEdges(Deserializer* d, Heap* h) {}
};

class ArrayCluster : public Cluster {
 public:
  ArrayCluster() {}
  ~ArrayCluster() {}

  void ReadNodes(Deserializer* d, Heap* h) {
    intptr_t num_objects = d->ReadLEB128();
    ref_start_ = d->next_ref();
    ref_stop_ = ref_start_ + num_objects;
    for (intptr_t i = 0; i < num_objects; i++) {
      intptr_t size = d->ReadLEB128();
      Array object = h->AllocateArray(size, Heap::kSnapshot);
      d->RegisterRef(object);
    }
    ASSERT(d->next_ref() == ref_stop_);
  }

  void ReadEdges(Deserializer* d, Heap* h) {
    for (intptr_t i = ref_start_; i < ref_stop_; i++) {
      Array object = Array::Cast(d->Ref(i));
      intptr_t size = object->Size();
      for (intptr_t j = 0; j < size; j++) {
        object->set_element(j, d->ReadRef(), kNoBarrier);
      }
    }
  }
};

class WeakArrayCluster : public Cluster {
 public:
  WeakArrayCluster() {}
  ~WeakArrayCluster() {}

  void ReadNodes(Deserializer* d, Heap* h) {
    intptr_t num_objects = d->ReadLEB128();
    ref_start_ = d->next_ref();
    ref_stop_ = ref_start_ + num_objects;
    for (intptr_t i = 0; i < num_objects; i++) {
      intptr_t size = d->ReadLEB128();
      WeakArray object = h->AllocateWeakArray(size, Heap::kSnapshot);
      d->RegisterRef(object);
    }
    ASSERT(d->next_ref() == ref_stop_);
  }

  void ReadEdges(Deserializer* d, Heap* h) {
    for (intptr_t i = ref_start_; i < ref_stop_; i++) {
      WeakArray object = WeakArray::Cast(d->Ref(i));
      intptr_t size = object->Size();
      for (intptr_t j = 0; j < size; j++) {
        object->set_element(j, d->ReadRef(), kNoBarrier);
      }
    }
  }
};

class ClosureCluster : public Cluster {
 public:
  ClosureCluster() {}
  ~ClosureCluster() {}

  void ReadNodes(Deserializer* d, Heap* h) {
    intptr_t num_objects = d->ReadLEB128();
    ref_start_ = d->next_ref();
    ref_stop_ = ref_start_ + num_objects;
    for (intptr_t i = 0; i < num_objects; i++) {
      intptr_t size = d->ReadLEB128();
      Closure object = h->AllocateClosure(size, Heap::kSnapshot);
      d->RegisterRef(object);
    }
    ASSERT(d->next_ref() == ref_stop_);
  }

  void ReadEdges(Deserializer* d, Heap* h) {
    for (intptr_t i = ref_start_; i < ref_stop_; i++) {
      Closure object = Closure::Cast(d->Ref(i));

      object->set_defining_activation(Activation::Cast(d->ReadRef()),
                                      kNoBarrier);
      object->set_initial_bci(static_cast<SmallInteger>(d->ReadRef()));
      object->set_num_args(static_cast<SmallInteger>(d->ReadRef()));

      intptr_t size = object->NumCopied();
      for (intptr_t j = 0; j < size; j++) {
        object->set_copied(j, d->ReadRef(), kNoBarrier);
      }
    }
  }
};

class ActivationCluster : public Cluster {
 public:
  ActivationCluster() {}
  ~ActivationCluster() {}

  void ReadNodes(Deserializer* d, Heap* h) {
    intptr_t num_objects = d->ReadLEB128();
    ref_start_ = d->next_ref();
    ref_stop_ = ref_start_ + num_objects;
    for (intptr_t i = 0; i < num_objects; i++) {
      Activation object = h->AllocateActivation(Heap::kSnapshot);
      d->RegisterRef(object);
    }
    ASSERT(d->next_ref() == ref_stop_);
  }

  void ReadEdges(Deserializer* d, Heap* h) {
    for (intptr_t i = ref_start_; i < ref_stop_; i++) {
      Activation object = Activation::Cast(d->Ref(i));

      object->set_sender(Activation::Cast(d->ReadRef()), kNoBarrier);
      object->set_bci(static_cast<SmallInteger>(d->ReadRef()));
      object->set_method(Method::Cast(d->ReadRef()), kNoBarrier);
      object->set_closure(Closure::Cast(d->ReadRef()), kNoBarrier);
      object->set_receiver(d->ReadRef(), kNoBarrier);

      intptr_t size = d->ReadLEB128();
      ASSERT(size < kMaxTemps);
      object->set_stack_depth(SmallInteger::New(size));

      for (intptr_t j = 0; j < size; j++) {
        object->set_temp(j, d->ReadRef(), kNoBarrier);
      }
      for (intptr_t j = size; j < kMaxTemps; j++) {
        object->set_temp(j, SmallInteger::New(0), kNoBarrier);
      }
    }
  }
};

class IntegerCluster : public Cluster {
 public:
  IntegerCluster() {}
  ~IntegerCluster() {}

  void ReadNodes(Deserializer* d, Heap* h) {
    intptr_t num_objects = d->ReadLEB128();
    ref_start_ = d->next_ref();
    ref_stop_ = ref_start_ + num_objects;
    for (intptr_t i = 0; i < num_objects; i++) {
      int64_t value = d->ReadSLEB128<int64_t>();
      if (SmallInteger::IsSmiValue(value)) {
        SmallInteger object = SmallInteger::New(static_cast<intptr_t>(value));
        ASSERT(object->IsSmallInteger());
        d->RegisterRef(object);
      } else {
        MediumInteger object = h->AllocateMediumInteger(Heap::kSnapshot);
        object->set_value(value);
        d->RegisterRef(object);
      }
    }
    ASSERT(d->next_ref() == ref_stop_);
  }

  void ReadEdges(Deserializer* d, Heap* h) {}
};

class LargeIntegerCluster : public Cluster {
 public:
  LargeIntegerCluster() {}
  ~LargeIntegerCluster() {}

  void ReadNodes(Deserializer* d, Heap* h) {
    intptr_t num_objects = d->ReadLEB128();
    ref_start_ = d->next_ref();
    ref_stop_ = ref_start_ + num_objects;
    for (intptr_t i = 0; i < num_objects; i++) {
      bool negative = d->Read<uint8_t>();
      intptr_t bytes = d->ReadLEB128();
      intptr_t digits = (bytes + (sizeof(digit_t) - 1)) / sizeof(digit_t);
      intptr_t full_digits = bytes / sizeof(digit_t);

      LargeInteger object = h->AllocateLargeInteger(digits, Heap::kSnapshot);
      object->set_negative(negative);
      object->set_size(digits);

      for (intptr_t j = 0; j < full_digits; j++) {
        digit_t digit = 0;
        for (intptr_t shift = 0;
             shift < static_cast<intptr_t>(kDigitBits);
             shift += 8) {
          digit = digit | (d->Read<uint8_t>() << shift);
        }
        object->set_digit(j, digit);
      }

      if (full_digits != digits) {
        intptr_t leftover_bytes = bytes % sizeof(digit_t);
        ASSERT(leftover_bytes != 0);
        digit_t digit = 0;
        for (intptr_t shift = 0;
             shift < (leftover_bytes * 8);
             shift += 8) {
          digit = digit | (d->Read<uint8_t>() << shift);
        }
        object->set_digit(digits - 1, digit);
      }

      d->RegisterRef(object);
    }
    ASSERT(d->next_ref() == ref_stop_);
  }

  void ReadEdges(Deserializer* d, Heap* h) {}
};

class FloatCluster : public Cluster {
 public:
  FloatCluster() {}
  ~FloatCluster() {}

  void ReadNodes(Deserializer* d, Heap* h) {
    intptr_t num_objects = d->ReadLEB128();
    ref_start_ = d->next_ref();
    ref_stop_ = ref_start_ + num_objects;
    for (intptr_t i = 0; i < num_objects; i++) {
      double value = d->Read<double>();
      Float object = h->AllocateFloat(Heap::kSnapshot);
      object->set_value(value);
      d->RegisterRef(object);
    }
    ASSERT(d->next_ref() == ref_stop_);
  }

  void ReadEdges(Deserializer* d, Heap* h) {}
};

Deserializer::Deserializer(Heap* heap, void* snapshot, size_t snapshot_length)
    : snapshot_(reinterpret_cast<const uint8_t*>(snapshot)),
      snapshot_length_(snapshot_length),
      cursor_(snapshot_),
      heap_(heap),
      clusters_(nullptr),
      refs_(nullptr),
      next_ref_(0) {}

Deserializer::~Deserializer() {
  for (intptr_t i = 0; i < num_clusters_; i++) {
    delete clusters_[i];
  }

  delete[] clusters_;
  delete[] refs_;
}

void Deserializer::Deserialize() {
  int64_t start = OS::CurrentMonotonicNanos();

  // Skip interpreter directive, if any.
  if ((cursor_[0] == static_cast<uint8_t>('#')) &&
      (cursor_[1] == static_cast<uint8_t>('!'))) {
    cursor_ += 2;
    while (*cursor_++ != static_cast<uint8_t>('\n')) {}
  }

  uint16_t magic = Read<uint16_t>();
  if (magic != 0x1984) {
    FATAL("Wrong magic value");
  }
  uint16_t version = ReadLEB128();
  if (version != 0) {
    FATAL("Wrong version (%d)", version);
  }

  num_clusters_ = ReadLEB128();
  clusters_ = new Cluster*[num_clusters_];

  intptr_t num_nodes = ReadLEB128();
  refs_ = new Object[num_nodes + 1];  // Refs are 1-origin.
  next_ref_ = 1;

  for (intptr_t i = 0; i < num_clusters_; i++) {
    Cluster* c = ReadCluster();
    clusters_[i] = c;
    c->ReadNodes(this, heap_);
  }
  ASSERT((next_ref_ - 1) == num_nodes);
  for (intptr_t i = 0; i < num_clusters_; i++) {
    clusters_[i]->ReadEdges(this, heap_);
  }

  ObjectStore os = static_cast<ObjectStore>(ReadRef());

  heap_->RegisterClass(kSmallIntegerCid, os->SmallInteger());
  heap_->RegisterClass(kMediumIntegerCid, os->MediumInteger());
  heap_->RegisterClass(kLargeIntegerCid, os->LargeInteger());
  heap_->RegisterClass(kFloatCid, os->Float());
  heap_->RegisterClass(kByteArrayCid, os->ByteArray());
  heap_->RegisterClass(kStringCid, os->String());
  heap_->RegisterClass(kArrayCid, os->Array());
  heap_->RegisterClass(kWeakArrayCid, os->WeakArray());
  heap_->RegisterClass(kEphemeronCid, os->Ephemeron());
  heap_->RegisterClass(kActivationCid, os->Activation());
  heap_->RegisterClass(kClosureCid, os->Closure());

  heap_->interpreter()->InitializeRoot(os);
  heap_->InitializeAfterSnapshot();

  int64_t stop = OS::CurrentMonotonicNanos();
  int64_t time = stop - start;
  if (TRACE_GROWTH) {
    OS::PrintErr("Deserialized %" Pd "kB snapshot "
                 "into %" Pd "kB heap "
                 "with %" Pd " objects "
                 "in %" Pd64 " us\n",
                 snapshot_length_ / KB,
                 heap_->Size() / KB,
                 next_ref_ - 1,
                 time / kNanosecondsPerMicrosecond);
  }

#if defined(DEBUG)
  size_t before = heap_->Size();
  heap_->CollectAll(Heap::kSnapshotTest);
  size_t after = heap_->Size();
  ASSERT(before == after);  // Snapshots should not contain garbage.
#endif
}

template <typename T>
T Deserializer::ReadLEB128() {
  COMPILE_ASSERT(std::is_unsigned<T>());
  const int8_t* cursor = reinterpret_cast<const int8_t*>(cursor_);
  T result = 0;
  uintptr_t shift = 0;
  intptr_t byte;
  do {
    byte = *cursor++;
    result |= static_cast<T>(byte & 0x7F) << shift;
    shift += 7;
  } while (byte < 0);
  cursor_ = reinterpret_cast<const uint8_t*>(cursor);
  return result;
}

template <typename T>
T Deserializer::ReadSLEB128() {
  COMPILE_ASSERT(std::is_signed<T>());
  typedef typename std::make_unsigned<T>::type Unsigned;
  const int8_t* cursor = reinterpret_cast<const int8_t*>(cursor_);
  T result = 0;
  uintptr_t shift = 0;
  intptr_t byte;
  do {
    byte = *cursor++;
    result |= static_cast<Unsigned>(byte & 0x7F) << shift;
    shift += 7;
  } while (byte < 0);
  if (((byte & 0x40) != 0) && (shift < sizeof(T) * 8)) {
    result |= ~static_cast<Unsigned>(0) << shift;
  }
  cursor_ = reinterpret_cast<const uint8_t*>(cursor);
  return result;
}

enum {
  kIntegerCluster = -1,
  kLargeIntegerCluster = -2,
  kFloatCluster = -3,
  kStringCluster = -4,
  kByteArrayCluster = -5,
  kArrayCluster = -6,
  kWeakArrayCluster = -7,
  kClosureCluster = -8,
  kActivationCluster = -9,
  kEphemeronCluster = -10,
};

Cluster* Deserializer::ReadCluster() {
  intptr_t format = ReadSLEB128();

  if (format >= 0) {
    return new RegularObjectCluster(format);
  } else {
    switch (format) {
      case kByteArrayCluster: return new ByteArrayCluster();
      case kStringCluster: return new StringCluster();
      case kArrayCluster: return new ArrayCluster();
      case kWeakArrayCluster: return new WeakArrayCluster();
      case kEphemeronCluster: return new RegularObjectCluster(3, kEphemeronCid);
      case kClosureCluster: return new ClosureCluster();
      case kActivationCluster: return new ActivationCluster();
      case kIntegerCluster: return new IntegerCluster();
      case kLargeIntegerCluster: return new LargeIntegerCluster();
      case kFloatCluster: return new FloatCluster();
    }
    FATAL("Unknown cluster format %" Pd "\n", format);
    return nullptr;
  }
}

}  // namespace psoup
