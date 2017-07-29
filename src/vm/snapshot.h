// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_SNAPSHOT_H_
#define VM_SNAPSHOT_H_

#include "vm/allocation.h"
#include "vm/globals.h"

namespace psoup {

class Cluster;
class Heap;
class Object;

// Reads a variant of VictoryFuel.
class Deserializer : public ValueObject {
 public:
  Deserializer(Heap* heap, void* snapshot, size_t snapshot_length);
  ~Deserializer();

  intptr_t position() { return cursor_ - snapshot_; }
  uint8_t ReadUint8();
  uint16_t ReadUint16();
  uint32_t ReadUint32();
  int32_t ReadInt32();
  int64_t ReadInt64();
  intptr_t ReadUnsigned32();

  void Deserialize();

  Cluster* ReadCluster();

  intptr_t next_back_ref() const { return next_back_ref_; }

  void RegisterBackRef(Object* object) {
    back_refs_[next_back_ref_++] = object;
  }
  Object* ReadBackRef() {
    return BackRef(ReadUnsigned32());
  }
  Object* BackRef(intptr_t i) {
    ASSERT(i > 0);
    ASSERT(i < next_back_ref_);
    return back_refs_[i];
  }

 private:
  const uint8_t* const snapshot_;
  const intptr_t snapshot_length_;
  const uint8_t* cursor_;

  Heap* const heap_;

  intptr_t num_clusters_;
  Cluster** clusters_;

  Object** back_refs_;
  intptr_t next_back_ref_;
};


class Cluster {
 public:
  Cluster() :
    class_(NULL),
    back_ref_start_(0),
    back_ref_stop_(0) {
  }

  virtual ~Cluster() {}

  virtual void ReadNodes(Deserializer* deserializer, Heap* heap) = 0;
  virtual void ReadEdges(Deserializer* deserializer, Heap* heap) = 0;

 protected:
  Object* class_;
  intptr_t back_ref_start_;
  intptr_t back_ref_stop_;
};


class RegularObjectCluster : public Cluster {
 public:
  explicit RegularObjectCluster(intptr_t format) : format_(format), cid_(0) {}

  virtual ~RegularObjectCluster() {}

  virtual void ReadNodes(Deserializer* deserializer, Heap* heap);
  virtual void ReadEdges(Deserializer* deserializer, Heap* heap);

 private:
  intptr_t format_;
  intptr_t cid_;
};


class ByteArrayCluster : public Cluster {
 public:
  ByteArrayCluster() {}
  virtual ~ByteArrayCluster() {}

  virtual void ReadNodes(Deserializer* deserializer, Heap* heap);
  virtual void ReadEdges(Deserializer* deserializer, Heap* heap);
};


class ByteStringCluster : public Cluster {
 public:
  ByteStringCluster() {}
  virtual ~ByteStringCluster() {}

  virtual void ReadNodes(Deserializer* deserializer, Heap* heap);
  void ReadNodes(Deserializer* deserializer, Heap* heap, bool is_canonical);
  virtual void ReadEdges(Deserializer* deserializer, Heap* heap);
};


class WideStringCluster : public Cluster {
 public:
  WideStringCluster() {}
  virtual ~WideStringCluster() {}

  virtual void ReadNodes(Deserializer* deserializer, Heap* heap);
  void ReadNodes(Deserializer* deserializer, Heap* heap, bool is_canonical);
  virtual void ReadEdges(Deserializer* deserializer, Heap* heap);
};


class ArrayCluster : public Cluster {
 public:
  ArrayCluster() {}
  virtual ~ArrayCluster() {}

  virtual void ReadNodes(Deserializer* deserializer, Heap* heap);
  virtual void ReadEdges(Deserializer* deserializer, Heap* heap);
};


class WeakArrayCluster : public Cluster {
 public:
  WeakArrayCluster() {}
  virtual ~WeakArrayCluster() {}

  virtual void ReadNodes(Deserializer* deserializer, Heap* heap);
  virtual void ReadEdges(Deserializer* deserializer, Heap* heap);
};


class ClosureCluster : public Cluster {
 public:
  ClosureCluster() {}
  virtual ~ClosureCluster() {}

  virtual void ReadNodes(Deserializer* deserializer, Heap* heap);
  virtual void ReadEdges(Deserializer* deserializer, Heap* heap);
};


class ActivationCluster : public Cluster {
 public:
  ActivationCluster() {}
  virtual ~ActivationCluster() {}

  virtual void ReadNodes(Deserializer* deserializer, Heap* heap);
  virtual void ReadEdges(Deserializer* deserializer, Heap* heap);
};


class SmallIntegerCluster : public Cluster {
 public:
  SmallIntegerCluster() {}
  virtual ~SmallIntegerCluster() {}

  virtual void ReadNodes(Deserializer* deserializer, Heap* heap);
  virtual void ReadEdges(Deserializer* deserializer, Heap* heap);
};

}  // namespace psoup

#endif  // VM_SNAPSHOT_H_
