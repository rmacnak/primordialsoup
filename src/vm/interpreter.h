// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_INTERPRETER_H_
#define VM_INTERPRETER_H_

#include "vm/globals.h"
#include "vm/assert.h"
#include "vm/flags.h"
#include "vm/lookup_cache.h"
#include "vm/object.h"

namespace psoup {

class Heap;
class Object;
class Isolate;

class Interpreter {
 public:
  explicit Interpreter(Heap* heap, Isolate* isolate);

  void Interpret();
  void SendOrdinary(ByteString* selector, intptr_t num_args);
  Method* MethodAt(Behavior*, ByteString* selector);

  void Interrupt() { interrupt_ = 1; }

 private:
  void PushReceiverVariable(intptr_t offset);
  void PushLiteralVariable(intptr_t offset);
  void PushTemporary(intptr_t offset);
  void PushRemoteTemp(intptr_t vector_offset, intptr_t offset);

  void StoreIntoReceiverVariable(intptr_t offset);
  void StoreIntoLiteralVariable(intptr_t offset);
  void StoreIntoTemporary(intptr_t offset);
  void StoreIntoRemoteTemp(intptr_t vector_offset, intptr_t offset);

  void PopIntoReceiverVariable(intptr_t offset);
  void PopIntoLiteralVariable(intptr_t offset);
  void PopIntoTemporary(intptr_t offset);
  void PopIntoRemoteTemp(intptr_t vector_offset, intptr_t offset);

  void PushLiteral(intptr_t offset);
  void PushReceiver();
  void PushFalse();
  void PushTrue();
  void PushNil();
  void PushThisContext();
  void PushEnclosingObject(intptr_t depth);
  void PushInteger(intptr_t value);
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

  void StaticSuperSend(intptr_t selector_index, intptr_t num_args);

  void MethodReturnReceiver();
  void MethodReturnTop();
  void BlockReturnTop();

  void Jump(intptr_t delta);
  void PopJumpTrue(intptr_t delta);
  void PopJumpFalse(intptr_t delta);

  void Dup();
  void Pop();

  void Nop();
  void Break();

  void CallPrimitive(intptr_t primitive);

  uint8_t FetchNextByte();

  Behavior* FindApplicationOf(AbstractMixin* mixin, Behavior* klass);
  bool HasMethod(Behavior*, ByteString* selector);
  ByteString* SelectorAt(intptr_t index);
  Object* LiteralAt(intptr_t index);

  void SendLexical(ByteString* selector,
                   intptr_t num_args,
                   Object* receiver,
                   AbstractMixin* mixin,
                   intptr_t rule);
  void SendProtected(ByteString* selector,
                     intptr_t num_args,
                     Object* receiver,
                     Behavior* starting_at,
                     intptr_t rule);

  void SendDNU(ByteString* selector,
               intptr_t num_args,
               Object* receiver,
               Behavior* lookup_class,
               bool present_receiver);
  void SendCannotReturn(Object* result);
  void SendAboutToReturnThrough(Object* result,
                                Activation* unwind);
  void SendNonBooleanReceiver(Object* non_boolean);

  void ActivateAbsent(Method* method, Object* receiver, intptr_t num_args);
  void InsertAbsentReceiver(Object* receiver, intptr_t num_args);
  void Activate(Method* method, intptr_t num_args);

  void LocalReturn(Object* result);
  void NonLocalReturn(Object* result);

#if RECYCLE_ACTIVATIONS
  // How many activations are known to be reachable only from the current
  // activation.
  intptr_t recycle_depth_;
#endif
  Heap* const heap_;
  Isolate* const isolate_;
  volatile uword interrupt_;
  LookupCache lookup_cache_;
};

}  // namespace psoup

#endif  // VM_INTERPRETER_H_
