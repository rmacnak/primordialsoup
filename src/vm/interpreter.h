// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_INTERPRETER_H_
#define VM_INTERPRETER_H_

#include <setjmp.h>

#include "vm/globals.h"
#include "vm/assert.h"
#include "vm/flags.h"
#include "vm/lookup_cache.h"
#include "vm/object.h"

namespace psoup {

class Heap;
class Isolate;
class Object;

class Interpreter {
 public:
  Interpreter(Heap* heap, Isolate* isolate);
  ~Interpreter();

  Isolate* isolate() const { return isolate_; }

  void Enter();
  void Exit();
  void SendOrdinary(String* selector, intptr_t num_args);
  Method* MethodAt(Behavior* cls, String* selector);
  void ActivateClosure(intptr_t num_args);

  void Interrupt() { checked_stack_limit_ = reinterpret_cast<Object**>(-1); }
  void PrintStack();

  const uint8_t* IPForAssert() { return ip_; }

  Activation* CurrentActivation();
  void SetCurrentActivation(Activation* new_activation);
  Object* ActivationSender(Activation* activation);
  void ActivationSenderPut(Activation* activation,
                           Activation* new_sender);
  Object* ActivationBCI(Activation* activation);
  void ActivationBCIPut(Activation* activation, SmallInteger* new_bci);
  void ActivationMethodPut(Activation* activation, Method* new_method);
  void ActivationClosurePut(Activation* activation, Closure* new_closure);
  void ActivationReceiverPut(Activation* activation, Object* value);
  Object* ActivationTempAt(Activation* activation, intptr_t index);
  void ActivationTempAtPut(Activation* activation, intptr_t index,
                           Object* value);
  intptr_t ActivationTempSize(Activation* activation);
  void ActivationTempSizePut(Activation* activation, intptr_t new_size);

  void GCPrologue();
  void RootPointers(Object*** from, Object*** to);
  void StackPointers(Object*** from, Object*** to);
  void GCEpilogue();

  void Push(Object* value) {
    ASSERT(sp_ <= stack_base_);
    ASSERT(sp_ > stack_limit_);
    *--sp_ = value;
  }
  Object* Pop() {
    ASSERT(StackDepth() > 0);
    return *sp_++;
  }
  void PopNAndPush(intptr_t n, Object* value) {
    ASSERT(StackDepth() >= n);
    sp_ += (n - 1);
    *sp_ = value;
  }
  Object* Stack(intptr_t depth) {
    ASSERT(StackDepth() >= depth);
    return sp_[depth];
  }
  void StackPut(intptr_t depth, Object* value) {
    ASSERT(StackDepth() >= depth);
    sp_[depth] = value;
  }
  void Grow(intptr_t elements) { sp_ -= elements; }
  void Drop(intptr_t elements) {
    ASSERT(StackDepth() >= elements);
    sp_ += elements;
  }
  intptr_t StackDepth() { return &fp_[-4] - sp_; }  // Magic!

  ObjectStore* object_store() const { return object_store_; }
  Object* nil_obj() const { return nil_; }
  Object* false_obj() const { return false_; }
  Object* true_obj() const { return true_; }
  void InitializeRoot(ObjectStore* object_store) {
    ASSERT(object_store_ == NULL);
    ASSERT(object_store->IsArray());
    nil_ = object_store->nil_obj();
    false_ = object_store->false_obj();
    true_ = object_store->true_obj();
    object_store_ = object_store;
  }

 private:
  void Interpret();

  void PushLiteralVariable(intptr_t offset);
  void PushTemporary(intptr_t offset);
  void PushRemoteTemp(intptr_t vector_offset, intptr_t offset);

  void StoreIntoTemporary(intptr_t offset);
  void StoreIntoRemoteTemp(intptr_t vector_offset, intptr_t offset);

  void PopIntoTemporary(intptr_t offset);
  void PopIntoRemoteTemp(intptr_t vector_offset, intptr_t offset);

  void PushLiteral(intptr_t offset);
  void PushEnclosingObject(intptr_t depth);
  void PushNewArrayWithElements(intptr_t size);
  void PushNewArray(intptr_t size);
  void PushClosure(intptr_t num_copied, intptr_t num_args, intptr_t block_size);

  void QuickArithmeticSend(intptr_t offset);
  void QuickCommonSend(intptr_t offset);

  void OrdinarySend(intptr_t selector_index, intptr_t num_args);
  void SuperSend(intptr_t selector_index, intptr_t num_args);
  void ImplicitReceiverSend(intptr_t selector_index, intptr_t num_args);
  void OuterSend(intptr_t selector_index, intptr_t num_args, intptr_t depth);
  void SelfSend(intptr_t selector_index, intptr_t num_args);

  Behavior* FindApplicationOf(AbstractMixin* mixin, Behavior* klass);
  bool HasMethod(Behavior*, String* selector);
  String* SelectorAt(intptr_t index);

  void SendLexical(String* selector,
                   intptr_t num_args,
                   Object* receiver,
                   AbstractMixin* mixin,
                   intptr_t rule);
  void SendProtected(String* selector,
                     intptr_t num_args,
                     Object* receiver,
                     Behavior* starting_at,
                     intptr_t rule);

  void SendDNU(String* selector,
               intptr_t num_args,
               Object* receiver,
               Behavior* lookup_class,
               bool present_receiver);
  void SendCannotReturn(Object* result);
  void SendAboutToReturnThrough(Object* result, Activation* unwind);
  void SendNonBooleanReceiver(Object* non_boolean);

  void ActivateAbsent(Method* method, Object* receiver, intptr_t num_args);
  void InsertAbsentReceiver(Object* receiver, intptr_t num_args);
  void Activate(Method* method, intptr_t num_args);
  void StackOverflow();

  void MethodReturn(Object* result);
  void LocalReturn(Object* result);
  void NonLocalReturn(Object* result);

  void CreateBaseFrame(Activation* activation);
  Activation* EnsureActivation(Object** fp);
  Activation* FlushAllFrames();
  bool HasLivingFrame(Activation* activation);

  static constexpr intptr_t kStackSlots = 1024;
  static constexpr intptr_t kStackSize = kStackSlots * sizeof(Object*);

  const uint8_t* ip_;
  Object** sp_;
  Object** fp_;
  Object** stack_base_;
  Object** stack_limit_;
  Object** volatile checked_stack_limit_;

  Object* nil_;
  Object* false_;
  Object* true_;
  ObjectStore* object_store_;

  Heap* const heap_;
  Isolate* const isolate_;
  jmp_buf* environment_;
  LookupCache lookup_cache_;
};

}  // namespace psoup

#endif  // VM_INTERPRETER_H_
