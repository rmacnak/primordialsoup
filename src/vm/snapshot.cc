// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/snapshot.h"

#include "vm/heap.h"
#include "vm/object.h"
#include "vm/virtual_memory.h"

namespace psoup {

static VirtualMemory isolate_snapshot_;

void Snapshot::InitOnce(const char* filename) {
  isolate_snapshot_ = VirtualMemory::MapReadOnly(filename);
}


void Snapshot::Shutdown() {
  isolate_snapshot_.Free();
}


Deserializer::Deserializer(Heap* heap) :
  snapshot_(reinterpret_cast<const uint8_t*>(isolate_snapshot_.base())),
  snapshot_length_(isolate_snapshot_.size()),
  cursor_(reinterpret_cast<const uint8_t*>(isolate_snapshot_.base())),
  heap_(heap),
  clusters_(NULL),
  back_refs_(NULL),
  next_back_ref_(0) {
}


Deserializer::~Deserializer() {
  for (intptr_t i = 0; i < num_clusters_; i++) {
    delete clusters_[i];
  }

  delete clusters_;
  delete back_refs_;
}


void Deserializer::Deserialize() {
  int64_t start = OS::CurrentMonotonicMicros();
  uint16_t magic = ReadUint16();
  if (magic != 0x1984) {
    FATAL("Wrong magic value");
  }
  uint16_t version = ReadUint16();
  if (version != 0) {
    FATAL1("Wrong version (%d)", version);
  }

  num_clusters_ = ReadUint16();
  if (TRACE_FUEL) {
    OS::PrintErr("%" Pd " clusters\n", num_clusters_);
  }
  clusters_ = new Cluster*[num_clusters_];

  intptr_t num_nodes = ReadUint32();
  if (TRACE_FUEL) {
    OS::PrintErr("num_nodes=%" Pd "\n", num_nodes);
  }
  back_refs_ = new Object*[num_nodes + 1];  // Back refs are 1-origin.
  next_back_ref_ = 1;

  for (intptr_t i = 0; i < num_clusters_; i++) {
    Cluster* c = ReadCluster();
    clusters_[i] = c;
    c->ReadNodes(this, heap_);
  }
  ASSERT(next_back_ref_ == num_nodes);
  for (intptr_t i = 0; i < num_clusters_; i++) {
    clusters_[i]->ReadEdges(this, heap_);
  }

  ObjectStore* os = static_cast<ObjectStore*>(ReadBackRef());
  ASSERT(position() == snapshot_length_);

  heap_->RegisterClass(kSmiCid, os->SmallInteger());
  heap_->RegisterClass(kMintCid, os->MediumInteger());
  heap_->RegisterClass(kBigintCid, os->LargeInteger());
  heap_->RegisterClass(kFloat64Cid, os->Float64());
  heap_->RegisterClass(kByteArrayCid, os->ByteArray());
  heap_->RegisterClass(kByteStringCid, os->ByteString());
  heap_->RegisterClass(kWideStringCid, os->WideString());
  heap_->RegisterClass(kArrayCid, os->Array());
  heap_->RegisterClass(kWeakArrayCid, os->WeakArray());
  heap_->RegisterClass(kEphemeronCid, os->Ephemeron());
  heap_->RegisterClass(kActivationCid, os->Activation());
  heap_->RegisterClass(kClosureCid, os->Closure());

  heap_->ClassAt(kSmiCid)->set_id(SmallInteger::New(kSmiCid));
  heap_->ClassAt(kMintCid)->set_id(SmallInteger::New(kMintCid));
  heap_->ClassAt(kBigintCid)->set_id(SmallInteger::New(kBigintCid));
  heap_->ClassAt(kFloat64Cid)->set_id(SmallInteger::New(kFloat64Cid));
  heap_->ClassAt(kByteArrayCid)->set_id(SmallInteger::New(kByteArrayCid));
  heap_->ClassAt(kByteStringCid)->set_id(SmallInteger::New(kByteStringCid));
  heap_->ClassAt(kWideStringCid)->set_id(SmallInteger::New(kWideStringCid));
  heap_->ClassAt(kArrayCid)->set_id(SmallInteger::New(kArrayCid));
  heap_->ClassAt(kWeakArrayCid)->set_id(SmallInteger::New(kWeakArrayCid));
  heap_->ClassAt(kEphemeronCid)->set_id(SmallInteger::New(kEphemeronCid));
  heap_->ClassAt(kActivationCid)->set_id(SmallInteger::New(kActivationCid));
  heap_->ClassAt(kClosureCid)->set_id(SmallInteger::New(kClosureCid));

  heap_->InitializeRoot(os);
  // heap_->Print();

  int64_t stop = OS::CurrentMonotonicMicros();
  intptr_t time = stop - start;
  OS::PrintErr("Deserialized %" Pd "kB snapshot "
               "into %" Pd "kB heap "
               "with %" Pd " objects "
               "in %" Pd " us\n",
               snapshot_length_ / 1024,
               heap_->used() / 1024,
               next_back_ref_ - 1,
               time);

#if defined(DEBUG)
  intptr_t before = heap_->used();
  heap_->Scavenge();
  intptr_t after = heap_->used();
  ASSERT(before == after);  // Snapshots should not contain garbage.
#endif
}


uint8_t Deserializer::ReadUint8() {
  return *cursor_++;
}


uint16_t Deserializer::ReadUint16() {
  int16_t result = ReadUint8();
  result = (result << 8) | ReadUint8();
  return result;
}


uint32_t Deserializer::ReadUint32() {
  uint32_t result = ReadUint8();
  result = (result << 8) | ReadUint8();
  result = (result << 8) | ReadUint8();
  result = (result << 8) | ReadUint8();
  return result;
}


int32_t Deserializer::ReadInt32() {
  uint32_t result = ReadUint8();
  result = (result << 8) | ReadUint8();
  result = (result << 8) | ReadUint8();
  result = (result << 8) | ReadUint8();
  return static_cast<int32_t>(result);
}


int64_t Deserializer::ReadInt64() {
  uint64_t result = ReadUint8();
  result = (result << 8) | ReadUint8();
  result = (result << 8) | ReadUint8();
  result = (result << 8) | ReadUint8();
  result = (result << 8) | ReadUint8();
  result = (result << 8) | ReadUint8();
  result = (result << 8) | ReadUint8();
  result = (result << 8) | ReadUint8();
  return static_cast<int64_t>(result);
}

static const int8_t kDataBitsPerByte = 7;
static const int8_t kByteMask = (1 << kDataBitsPerByte) - 1;
static const int8_t kMaxUnsignedDataPerByte = kByteMask;
static const uint8_t kEndUnsignedByteMarker = (255 - kMaxUnsignedDataPerByte);

intptr_t Deserializer::ReadUnsigned32() {
  const uint8_t* c = cursor_;
  // ASSERT(c < end_);
  uint8_t b = *c++;
  if (b > kMaxUnsignedDataPerByte) {
    cursor_ = c;
    return static_cast<uint32_t>(b) - kEndUnsignedByteMarker;
  }

  int32_t r = 0;
  r |= static_cast<uint32_t>(b);
  // ASSERT(c < end_);
  b = *c++;
  if (b > kMaxUnsignedDataPerByte) {
    cursor_ = c;
    return r | ((static_cast<uint32_t>(b) - kEndUnsignedByteMarker) << 7);
  }

  r |= static_cast<uint32_t>(b) << 7;
  // ASSERT(c < end_);
  b = *c++;
  if (b > kMaxUnsignedDataPerByte) {
    cursor_ = c;
    return r | ((static_cast<uint32_t>(b) - kEndUnsignedByteMarker) << 14);
  }

  r |= static_cast<uint32_t>(b) << 14;
  // ASSERT(c < end_);
  b = *c++;
  if (b > kMaxUnsignedDataPerByte) {
    cursor_ = c;
    return r | ((static_cast<uint32_t>(b) - kEndUnsignedByteMarker) << 21);
  }

  r |= static_cast<uint32_t>(b) << 21;
  // ASSERT(c < end_);
  b = *c++;
  ASSERT(b > kMaxUnsignedDataPerByte);
  cursor_ = c;
  return r | ((static_cast<uint32_t>(b) - kEndUnsignedByteMarker) << 28);
}


Cluster* Deserializer::ReadCluster() {
  intptr_t format = ReadInt32();

  if (format >= 0) {
    return new RegularObjectCluster(format);
  } else {
    switch (-format) {
      case kByteArrayCid: return new ByteArrayCluster();
      case kByteStringCid: return new ByteStringCluster();
      case kWideStringCid: return new WideStringCluster();
      case kArrayCid: return new ArrayCluster();
      case kWeakArrayCid: return new WeakArrayCluster();
      case kClosureCid: return new ClosureCluster();
      case kActivationCid: return new ActivationCluster();
      case kSmiCid: return new SmallIntegerCluster();
    }
    OS::PrintErr("Cluster format %" Pd "\n", format);
    UNIMPLEMENTED();
    return 0;
  }
}


void RegularObjectCluster::ReadNodes(Deserializer* d, Heap* h) {
  intptr_t num_objects = d->ReadUint16();
  if (TRACE_FUEL) {
    OS::PrintErr("Nodes format=%" Pd " objects=%" Pd " %" Pd "\n",
                 format_, num_objects, d->position());
  }

  cid_ = h->AllocateClassId();

  back_ref_start_ = d->next_back_ref();
  back_ref_stop_ = back_ref_start_ + num_objects;
  for (intptr_t i = 0; i < num_objects; i++) {
    Object* object = h->AllocateRegularObject(cid_, format_);
    d->RegisterBackRef(object);
  }
  ASSERT(d->next_back_ref() == back_ref_stop_);
}


void RegularObjectCluster::ReadEdges(Deserializer* d, Heap* h) {
  if (TRACE_FUEL) {
    OS::PrintErr("Edges format=%" Pd " objects=%" Pd " %" Pd "\n",
                 format_, back_ref_stop_ - back_ref_start_, d->position());
  }
  class_ = d->ReadBackRef();
  h->RegisterClass(cid_, class_);

  for (intptr_t i = back_ref_start_; i < back_ref_stop_; i++) {
    RegularObject* object = static_cast<RegularObject*>(d->BackRef(i));
    for (intptr_t j = 0; j < format_; j++) {
      object->set_slot(j, d->ReadBackRef());
    }
  }
}


void ByteArrayCluster::ReadNodes(Deserializer* d, Heap* h) {
  intptr_t num_objects = d->ReadUint16();
  if (TRACE_FUEL) {
    OS::PrintErr("Nodes bytearray objects=%" Pd " %" Pd "\n",
                 num_objects, d->position());
  }

  back_ref_start_ = d->next_back_ref();
  back_ref_stop_ = back_ref_start_ + num_objects;
  for (intptr_t i = 0; i < num_objects; i++) {
    intptr_t size = d->ReadUint32();
    ByteArray* object = h->AllocateByteArray(size);
    for (intptr_t j = 0; j < size; j++) {
      object->set_element(j, d->ReadUint8());
    }
    d->RegisterBackRef(object);
    ASSERT(object->IsByteArray());
  }
  ASSERT(d->next_back_ref() == back_ref_stop_);
}


void ByteArrayCluster::ReadEdges(Deserializer* d, Heap* h) {
  if (TRACE_FUEL) {
    OS::PrintErr("Edges bytearray objects=%" Pd " %" Pd "\n",
                 back_ref_stop_ - back_ref_start_, d->position());
  }
}


void ByteStringCluster::ReadNodes(Deserializer* d, Heap* h) {
  ReadNodes(d, h, false);
  ReadNodes(d, h, true);
}


void ByteStringCluster::ReadNodes(Deserializer* d, Heap* h, bool is_canonical) {
  intptr_t num_objects = d->ReadUint16();
  if (TRACE_FUEL) {
    OS::PrintErr("Nodes bytestring objects=%" Pd " %" Pd "\n",
                 num_objects, d->position());
  }

  back_ref_start_ = d->next_back_ref();
  back_ref_stop_ = back_ref_start_ + num_objects;
  for (intptr_t i = 0; i < num_objects; i++) {
    intptr_t size = d->ReadUint32();
    ByteString* object = h->AllocateByteString(size);
    ASSERT(!object->is_canonical());
    object->set_is_canonical(is_canonical);
    for (intptr_t j = 0; j < size; j++) {
      object->set_element(j, d->ReadUint8());
    }
    d->RegisterBackRef(object);
  }
  ASSERT(d->next_back_ref() == back_ref_stop_);
}


void ByteStringCluster::ReadEdges(Deserializer* d, Heap* h) {
  if (TRACE_FUEL) {
    OS::PrintErr("Edges bytestring objects=%" Pd " %" Pd "\n",
                 back_ref_stop_ - back_ref_start_, d->position());
  }
}


void WideStringCluster::ReadNodes(Deserializer* d, Heap* h) {
  ReadNodes(d, h, false);
  ReadNodes(d, h, true);
}


void WideStringCluster::ReadNodes(Deserializer* d, Heap* h, bool is_canonical) {
  intptr_t num_objects = d->ReadUint16();
  if (TRACE_FUEL) {
    OS::PrintErr("Nodes bytesymbol objects=%" Pd " %" Pd "\n",
                 num_objects, d->position());
  }

  back_ref_start_ = d->next_back_ref();
  back_ref_stop_ = back_ref_start_ + num_objects;
  for (intptr_t i = 0; i < num_objects; i++) {
    intptr_t size = d->ReadUint32();
    WideString* object = h->AllocateWideString(size);
    ASSERT(!object->is_canonical());
    object->set_is_canonical(is_canonical);
    for (intptr_t j = 0; j < size; j++) {
      object->set_element(j, d->ReadUint32());
    }
    ASSERT(object->IsWideString());
    d->RegisterBackRef(object);
  }
  ASSERT(d->next_back_ref() == back_ref_stop_);
}


void WideStringCluster::ReadEdges(Deserializer* d, Heap* h) {
  if (TRACE_FUEL) {
    OS::PrintErr("Edges bytesymbol objects=%" Pd " %" Pd "\n",
                 back_ref_stop_ - back_ref_start_, d->position());
  }
}


void ArrayCluster::ReadNodes(Deserializer* d, Heap* h) {
  intptr_t num_objects = d->ReadUint16();
  if (TRACE_FUEL) {
    OS::PrintErr("Nodes array objects=%" Pd " %" Pd "\n",
                 num_objects, d->position());
  }

  back_ref_start_ = d->next_back_ref();
  back_ref_stop_ = back_ref_start_ + num_objects;
  for (intptr_t i = 0; i < num_objects; i++) {
    intptr_t size = d->ReadUint16();
    Array* object = h->AllocateArray(size);
    ASSERT(object->IsArray());
    d->RegisterBackRef(object);
  }
  ASSERT(d->next_back_ref() == back_ref_stop_);
}


void ArrayCluster::ReadEdges(Deserializer* d, Heap* h) {
  if (TRACE_FUEL) {
    OS::PrintErr("Edges array objects=%" Pd " %" Pd "\n",
                 back_ref_stop_ - back_ref_start_, d->position());
  }

  for (intptr_t i = back_ref_start_; i < back_ref_stop_; i++) {
    Array* object = static_cast<Array*>(d->BackRef(i));
    ASSERT(object->IsArray());
    intptr_t size = object->Size();
    for (intptr_t j = 0; j < size; j++) {
      object->set_element(j, d->ReadBackRef());
    }
  }
}

void WeakArrayCluster::ReadNodes(Deserializer* d, Heap* h) {
  intptr_t num_objects = d->ReadUint16();
  if (TRACE_FUEL) {
    OS::PrintErr("Nodes weakarray objects=%" Pd " %" Pd "\n",
                 num_objects, d->position());
  }

  back_ref_start_ = d->next_back_ref();
  back_ref_stop_ = back_ref_start_ + num_objects;
  for (intptr_t i = 0; i < num_objects; i++) {
    intptr_t size = d->ReadUint16();
    WeakArray* object = h->AllocateWeakArray(size);
    ASSERT(object->IsWeakArray());
    d->RegisterBackRef(object);
  }
  ASSERT(d->next_back_ref() == back_ref_stop_);
}


void WeakArrayCluster::ReadEdges(Deserializer* d, Heap* h) {
  if (TRACE_FUEL) {
    OS::PrintErr("Edges weakarray objects=%" Pd " %" Pd "\n",
                 back_ref_stop_ - back_ref_start_, d->position());
  }

  for (intptr_t i = back_ref_start_; i < back_ref_stop_; i++) {
    WeakArray* object = static_cast<WeakArray*>(d->BackRef(i));
    ASSERT(object->IsWeakArray());
    intptr_t size = object->Size();
    for (intptr_t j = 0; j < size; j++) {
      object->set_element(j, d->ReadBackRef());
    }
  }
}

void ClosureCluster::ReadNodes(Deserializer* d, Heap* h) {
  intptr_t num_objects = d->ReadUint16();
  if (TRACE_FUEL) {
    OS::PrintErr("Nodes closure objects=%" Pd " %" Pd "\n",
                 num_objects, d->position());
  }

  back_ref_start_ = d->next_back_ref();
  back_ref_stop_ = back_ref_start_ + num_objects;
  for (intptr_t i = 0; i < num_objects; i++) {
    intptr_t size = d->ReadUint16();
    Closure* object = h->AllocateClosure(size);
    object->set_num_copied(size);
    ASSERT(object->IsClosure());
    d->RegisterBackRef(object);
  }
  ASSERT(d->next_back_ref() == back_ref_stop_);
}


void ClosureCluster::ReadEdges(Deserializer* d, Heap* h) {
  if (TRACE_FUEL) {
    OS::PrintErr("Edges closure objects=%" Pd " %" Pd "\n",
                 back_ref_stop_ - back_ref_start_, d->position());
  }

  for (intptr_t i = back_ref_start_; i < back_ref_stop_; i++) {
    Closure* object = Closure::Cast(d->BackRef(i));
    ASSERT(object->IsClosure());

    object->set_defining_activation(Activation::Cast(d->ReadBackRef()));
    object->set_initial_bci(static_cast<SmallInteger*>(d->ReadBackRef()));
    object->set_num_args(static_cast<SmallInteger*>(d->ReadBackRef()));

    intptr_t size = object->num_copied();
    for (intptr_t j = 0; j < size; j++) {
      object->set_copied(j, d->ReadBackRef());
    }
  }
}

void ActivationCluster::ReadNodes(Deserializer* d, Heap* h) {
  intptr_t num_objects = d->ReadUint16();
  if (TRACE_FUEL) {
    OS::PrintErr("Nodes activation objects=%" Pd " %" Pd "\n",
                 num_objects, d->position());
  }

  back_ref_start_ = d->next_back_ref();
  back_ref_stop_ = back_ref_start_ + num_objects;
  for (intptr_t i = 0; i < num_objects; i++) {
    Activation* object = h->AllocateActivation();
    d->RegisterBackRef(object);
  }
  ASSERT(d->next_back_ref() == back_ref_stop_);
}


void ActivationCluster::ReadEdges(Deserializer* d, Heap* h) {
  if (TRACE_FUEL) {
    OS::PrintErr("Edges activation objects=%" Pd " %" Pd "\n",
                 back_ref_stop_ - back_ref_start_, d->position());
  }

  for (intptr_t i = back_ref_start_; i < back_ref_stop_; i++) {
    Activation* object = Activation::Cast(d->BackRef(i));

    object->set_sender(Activation::Cast(d->ReadBackRef()));
    object->set_bci(static_cast<SmallInteger*>(d->ReadBackRef()));  // nullable
    object->set_method(Method::Cast(d->ReadBackRef()));
    object->set_closure(Closure::Cast(d->ReadBackRef()));
    object->set_receiver(d->ReadBackRef());

    intptr_t size = d->ReadUint32();
    if (TRACE_FUEL) OS::PrintErr(" stack %" Pd "\n", size);
    ASSERT(size < Activation::kMaxTemps);
    object->set_stack_depth(SmallInteger::New(size));

    for (intptr_t j = 0; j < size; j++) {
      object->set_temp(j, d->ReadBackRef());
    }
    for (intptr_t j = size; j < Activation::kMaxTemps; j++) {
      object->set_temp(j, SmallInteger::New(0));
    }
  }
}

void SmallIntegerCluster::ReadNodes(Deserializer* d, Heap* h) {
  intptr_t num_objects = d->ReadUint16();
  if (TRACE_FUEL) {
    OS::PrintErr("Nodes smi objects=%" Pd " %" Pd "\n",
                 num_objects, d->position());
  }

  back_ref_start_ = d->next_back_ref();
  back_ref_stop_ = back_ref_start_ + num_objects;
  for (intptr_t i = 0; i < num_objects; i++) {
    int64_t value = d->ReadInt64();
    if (SmallInteger::IsSmiValue(value)) {
      SmallInteger* object = SmallInteger::New(value);
      ASSERT(object->IsSmallInteger());
      d->RegisterBackRef(object);
    } else {
      MediumInteger* object = h->AllocateMediumInteger();
      ASSERT(object->IsMediumInteger());
      object->set_value(value);
      d->RegisterBackRef(object);
    }
  }
  ASSERT(d->next_back_ref() == back_ref_stop_);
}


void SmallIntegerCluster::ReadEdges(Deserializer* d, Heap* h) {
  if (TRACE_FUEL) {
    OS::PrintErr("Edges smi objects=%" Pd " %" Pd "\n",
                 back_ref_stop_ - back_ref_start_, d->position());
  }
}

}  // namespace psoup
