// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/interpreter.h"

#include "vm/heap.h"
#include "vm/primitives.h"
#include "vm/isolate.h"

#define H heap_
#define A heap_->activation()
#define nil heap_->object_store()->nil_obj()

namespace psoup {

Interpreter::Interpreter(Heap* heap, Isolate* isolate) :
#if RECYCLE_ACTIVATIONS
  recycle_depth_(0),
#endif
  heap_(heap),
  isolate_(isolate),
  interrupt_(0),
  lookup_cache_() {
  heap->InitializeLookupCache(&lookup_cache_);
}


void Interpreter::PushReceiverVariable(intptr_t offset) {
  UNREACHABLE();  // Not used in Newspeak
}


void Interpreter::PushLiteralVariable(intptr_t offset) {
  // Not used in Newspeak, except by the implementation of eventual sends.
  // TODO(rmacnak): Add proper push scheduler bytecode.
  A->Push(heap_->object_store()->scheduler());
}


void Interpreter::PushTemporary(intptr_t offset) {
  Object* temp = A->temp(offset);
  A->Push(temp);
}


void Interpreter::PushRemoteTemp(intptr_t vector_offset, intptr_t offset) {
  Object* vector = A->temp(vector_offset);
  ASSERT(vector->IsArray());
  Object* temp = static_cast<Array*>(vector)->element(offset);
  A->Push(temp);
}


void Interpreter::StoreIntoReceiverVariable(intptr_t offset) {
  UNREACHABLE();  // Setters only use pop into.
}


void Interpreter::StoreIntoLiteralVariable(intptr_t offset) {
  UNREACHABLE();  // Only used by Smalltalk.
}


void Interpreter::StoreIntoTemporary(intptr_t offset) {
  Object* top = A->Stack(0);
  A->set_temp(offset, top);
}


void Interpreter::StoreIntoRemoteTemp(intptr_t vector_offset, intptr_t offset) {
  Object* top = A->Stack(0);
  Object* vector = A->temp(vector_offset);
  ASSERT(vector->IsArray());
  static_cast<Array*>(vector)->set_element(offset, top);
}


void Interpreter::PopIntoReceiverVariable(intptr_t offset) {
  UNREACHABLE();  // Not used in Newspeak.
}


void Interpreter::PopIntoLiteralVariable(intptr_t offset) {
  UNIMPLEMENTED();  // Only used by Smalltalk.
}


void Interpreter::PopIntoTemporary(intptr_t offset) {
  Object* top = A->Pop();
  A->set_temp(offset, top);
}


void Interpreter::PopIntoRemoteTemp(intptr_t vector_offset, intptr_t offset) {
  Object* top = A->Pop();
  Object* vector = A->temp(vector_offset);
  ASSERT(vector->IsArray());
  static_cast<Array*>(vector)->set_element(offset, top);
}


void Interpreter::PushLiteral(intptr_t offset) {
  Object* literal = LiteralAt(offset);
  A->Push(literal);
}


void Interpreter::PushReceiver() {
  Object* receiver = A->receiver();
  A->Push(receiver);
}


void Interpreter::PushFalse() {
  A->Push(H->object_store()->false_obj());
}


void Interpreter::PushTrue() {
  A->Push(H->object_store()->true_obj());
}


void Interpreter::PushNil() {
  A->Push(H->object_store()->nil_obj());
}


void Interpreter::PushThisContext() {
  UNREACHABLE();  // No longer used by Newspeak.
  A->Push(A);
}


void Interpreter::PushEnclosingObject(intptr_t depth) {
  ASSERT(depth > 0);  // Compiler should have used push receiver.

  Object* enclosing_object = A->receiver();
  AbstractMixin* target_mixin = A->method()->mixin();
  intptr_t count = 0;
  while (count < depth) {
    count++;
    Behavior* mixin_app = FindApplicationOf(target_mixin,
                                            enclosing_object->Klass(H));
    enclosing_object = mixin_app->enclosing_object();
    target_mixin = target_mixin->enclosing_mixin();
  }
  A->Push(enclosing_object);
}


void Interpreter::PushInteger(intptr_t value) {
  A->Push(SmallInteger::New(value));
}


void Interpreter::PushConsArray(intptr_t size) {
  Array* result = H->AllocateArray(size);  // SAFEPOINT
  for (intptr_t i = 0; i < size; i++) {
    Object* e = A->Stack(size - i - 1);
    result->set_element(i, e);
  }
  A->PopNAndPush(size, result);
}


void Interpreter::PushEmptyArray(intptr_t size) {
  Array* result = H->AllocateArray(size);  // SAFEPOINT
  for (intptr_t i = 0; i < size; i++) {
    result->set_element(i, nil);
  }
  A->Push(result);
}


void Interpreter::PushClosure(intptr_t num_copied,
                              intptr_t num_args,
                              intptr_t block_size) {
  Closure* result = H->AllocateClosure(num_copied);  // SAFEPOINT
  result->set_num_copied(num_copied);

  result->set_defining_activation(A);
#if RECYCLE_ACTIVATIONS
  recycle_depth_ = 0;
#endif
  result->set_initial_bci(A->bci());
  result->set_num_args(SmallInteger::New(num_args));
  for (intptr_t i = 0; i < num_copied; i++) {
    Object* e = A->Stack(num_copied - i - 1);
    result->set_copied(i, e);
  }

  A->set_bci(SmallInteger::New(A->bci()->value() + block_size));
  A->PopNAndPush(num_copied, result);
}


void Interpreter::QuickArithmeticSend(intptr_t offset) {
  Array* quick_selectors = H->object_store()->quick_selectors();
  ByteString* selector =
    static_cast<ByteString*>(quick_selectors->element(offset * 2));
  ASSERT(selector->is_canonical());
  SmallInteger* arity =
    static_cast<SmallInteger*>(quick_selectors->element(offset * 2 + 1));
  ASSERT(arity->IsSmallInteger());
  SendOrdinary(selector, arity->value());
}


void Interpreter::QuickCommonSend(intptr_t offset) {
  Array* quick_selectors = H->object_store()->quick_selectors();
  ByteString* selector =
    static_cast<ByteString*>(quick_selectors->element((offset + 16) * 2));
  ASSERT(selector->IsByteString() && selector->is_canonical());
  SmallInteger* arity =
    static_cast<SmallInteger*>(quick_selectors->element((offset +16) * 2 + 1));
  ASSERT(arity->IsSmallInteger());
  SendOrdinary(selector, arity->value());
}


Method* Interpreter::MethodAt(Behavior* cls, ByteString* selector) {
  Array* methods = cls->methods();
  ASSERT(methods->IsArray());
  intptr_t length = methods->Size();
  for (intptr_t i = 0; i < length; i++) {
    Method* method = static_cast<Method*>(methods->element(i));
    if (method->selector() == selector) {
      return method;
    }
  }
  return static_cast<Method*>(nil);
}


bool Interpreter::HasMethod(Behavior* cls, ByteString* selector) {
  return MethodAt(cls, selector) != nil;
}


void Interpreter::OrdinarySend(intptr_t selector_index,
                               intptr_t num_args) {
  ByteString* selector = SelectorAt(selector_index);
  SendOrdinary(selector, num_args);
}


void Interpreter::SendOrdinary(ByteString* selector,
                               intptr_t num_args) {
  Object* receiver = A->Stack(num_args);

#if LOOKUP_CACHE
  Method* target;
  PrimitiveFunction* primitive;
  if (lookup_cache_.LookupOrdinary(receiver->ClassId(), selector,
                                   &target, &primitive)) {
    Activate(target, num_args);  // SAFEPOINT
    return;
  }
#endif

  Behavior* receiver_class = receiver->Klass(H);
  Behavior* lookup_class = receiver_class;
  while (lookup_class != nil) {
    Method* method = MethodAt(lookup_class, selector);
    if (method != nil) {
      if (method->IsPublic()) {
#if LOOKUP_CACHE
        lookup_cache_.InsertOrdinary(receiver->ClassId(), selector,
                                     method, 0);
#endif
        Activate(method, num_args);  // SAFEPOINT
        return;
      } else if (method->IsProtected()) {
        bool present_receiver = true;
        SendDNU(selector, num_args, receiver,
                receiver_class, present_receiver);  // SAFEPOINT
        return;
      }
    }
    lookup_class = lookup_class->superclass();
  }
  bool present_receiver = true;
  SendDNU(selector, num_args, receiver,
          receiver_class, present_receiver);  // SAFEPOINT
}


Behavior* Interpreter::FindApplicationOf(AbstractMixin* mixin,
                                         Behavior* klass) {
  while (klass->mixin() != mixin) {
    klass = klass->superclass();
    if (klass == nil) {
      FATAL("Cannot find mixin application");
    }
  }
  return klass;
}


void Interpreter::SuperSend(intptr_t selector_index,
                            intptr_t num_args) {
  ByteString* selector = SelectorAt(selector_index);
  Object* receiver = A->receiver();

#if LOOKUP_CACHE
  Object* absent_receiver;
  Method* target;
  PrimitiveFunction* primitive;
  if (lookup_cache_.LookupNS(receiver->ClassId(),
                             selector,
                             A->method(),
                             kSuper,
                             &absent_receiver,
                             &target,
                             &primitive)) {
    if (absent_receiver == 0) {
      absent_receiver = receiver;
    }
    ActivateAbsent(target, absent_receiver, num_args);  // SAFEPOINT
    return;
  }
#endif

  AbstractMixin* method_mixin = A->method()->mixin();
  Behavior* receiver_class = receiver->Klass(H);
  Behavior* method_mixin_app = FindApplicationOf(method_mixin,
                                                 receiver_class);
  SendProtected(selector,
                num_args,
                receiver,
                method_mixin_app->superclass(),
                kSuper);
}


void Interpreter::ImplicitReceiverSend(intptr_t selector_index,
                                       intptr_t num_args) {
  ByteString* selector = SelectorAt(selector_index);
  Object* method_receiver = A->receiver();

#if LOOKUP_CACHE
  Object* absent_receiver;
  Method* target;
  PrimitiveFunction* primitive;
  if (lookup_cache_.LookupNS(method_receiver->ClassId(),
                             selector,
                             A->method(),
                             kImplicitReceiver,
                             &absent_receiver,
                             &target,
                             &primitive)) {
    if (absent_receiver == 0) {
      absent_receiver = method_receiver;
    }
    ActivateAbsent(target, absent_receiver, num_args);  // SAFEPOINT
    return;
  }
#endif

  Object* candidate_receiver = method_receiver;
  AbstractMixin* candidate_mixin = A->method()->mixin();

  for (;;) {
    Behavior* candidateMixinApplication =
        FindApplicationOf(candidate_mixin,
                          candidate_receiver->Klass(H));
    if (HasMethod(candidateMixinApplication, selector)) {
      SendLexical(selector,
                  num_args,
                  candidate_receiver,
                  candidate_mixin,
                  kImplicitReceiver);
      return;
    }
    candidate_mixin = candidate_mixin->enclosing_mixin();
    if (candidate_mixin == nil) break;
    candidate_receiver = candidateMixinApplication->enclosing_object();
  }
  SendProtected(selector,
                num_args,
                method_receiver,
                method_receiver->Klass(H),
                kImplicitReceiver);
  return;
}


void Interpreter::OuterSend(intptr_t selector_index,
                            intptr_t num_args,
                            intptr_t depth) {
  ByteString* selector = SelectorAt(selector_index);
  Object* receiver = A->receiver();

#if LOOKUP_CACHE
  Object* absent_receiver;
  Method* target;
  PrimitiveFunction* primitive;
  if (lookup_cache_.LookupNS(receiver->ClassId(),
                             selector,
                             A->method(),
                             depth,
                             &absent_receiver,
                             &target,
                             &primitive)) {
    if (absent_receiver == 0) {
      absent_receiver = receiver;
    }
    ActivateAbsent(target, absent_receiver, num_args);  // SAFEPOINT
    return;
  }
#endif

  AbstractMixin* target_mixin = A->method()->mixin();
  intptr_t count = 0;
  while (count < depth) {
    count++;
    Behavior* mixin_app = FindApplicationOf(target_mixin,
                                            receiver->Klass(H));
    receiver = mixin_app->enclosing_object();
    target_mixin = target_mixin->enclosing_mixin();
  }
  SendLexical(selector, num_args, receiver, target_mixin, depth);
}


void Interpreter::SelfSend(intptr_t selector_index,
                           intptr_t num_args) {
  ByteString* selector = SelectorAt(selector_index);
  Object* receiver = A->receiver();

#if LOOKUP_CACHE
  Object* absent_receiver;
  Method* target;
  PrimitiveFunction* primitive;
  if (lookup_cache_.LookupNS(receiver->ClassId(),
                             selector,
                             A->method(),
                             kSelf,
                             &absent_receiver,
                             &target,
                             &primitive)) {
    if (absent_receiver == 0) {
      absent_receiver = receiver;
    }
    ActivateAbsent(target, absent_receiver, num_args);  // SAFEPOINT
    return;
  }
#endif

  AbstractMixin* method_mixin = A->method()->mixin();
  SendLexical(selector, num_args, receiver, method_mixin, kSelf);
}


void Interpreter::SendLexical(ByteString* selector,
                              intptr_t num_args,
                              Object* receiver,
                              AbstractMixin* mixin,
                              intptr_t rule) {
  Behavior* receiver_class = receiver->Klass(H);
  Behavior* mixin_application = FindApplicationOf(mixin, receiver_class);
  Method* method = MethodAt(mixin_application, selector);
  if (method != nil && method->IsPrivate()) {
#if LOOKUP_CACHE
    Object* method_receiver = A->receiver();
    lookup_cache_.InsertNS(method_receiver->ClassId(),
                           selector,
                           A->method(),
                           rule,
                           receiver == method_receiver ? 0 : receiver,
                           method,
                           0);
#endif
    ActivateAbsent(method, receiver, num_args);  // SAFEPOINT
    return;
  }
  SendProtected(selector, num_args, receiver,
                receiver_class, rule);  // SAFEPOINT
}


void Interpreter::SendProtected(ByteString* selector,
                                intptr_t num_args,
                                Object* receiver,
                                Behavior* mixin_application,
                                intptr_t rule) {
  Behavior* lookup_class = mixin_application;
  while (lookup_class != nil) {
    Method* method = MethodAt(lookup_class, selector);
    if (method != nil && !method->IsPrivate()) {
#if LOOKUP_CACHE
      Object* method_receiver = A->receiver();
      lookup_cache_.InsertNS(method_receiver->ClassId(),
                             selector,
                             A->method(),
                             rule,
                             receiver == method_receiver ? 0 : receiver,
                             method,
                             0);
#endif
      ActivateAbsent(method, receiver, num_args);  // SAFEPOINT
      return;
    }
    lookup_class = lookup_class->superclass();
  }
  bool present_receiver = false;
  SendDNU(selector, num_args, receiver, mixin_application, present_receiver);
}


void Interpreter::SendDNU(ByteString* selector,
                          intptr_t num_args,
                          Object* receiver,
                          Behavior* lookup_class,
                          bool present_receiver) {
  if (TRACE_DNU) {
    const char *c1, *c2, *c3;
    c1 = receiver->ToCString(H);
    c2 = selector->ToCString(H);
    c3 = A->method()->selector()->ToCString(H);
    OS::PrintErr("DNU %s %s from %s\n", c1, c2, c3);
    free((void*)c1);  // NOLINT
    free((void*)c2);  // NOLINT
    free((void*)c3);  // NOLINT
  }

  Behavior* cls = lookup_class;
  Method* method;
  do {
    method = MethodAt(cls, H->object_store()->does_not_understand());
    if (method != nil) {
      break;
    }
    cls = cls->superclass();
  } while (cls != nil);

  if (method == nil) {
    FATAL("Recursive #doesNotUnderstand:");
  }

  Array* arguments;
  {
    HandleScope h1(H, reinterpret_cast<Object**>(&selector));
    HandleScope h2(H, &receiver);
    HandleScope h3(H, reinterpret_cast<Object**>(&method));
    arguments = H->AllocateArray(num_args);  // SAFEPOINT
  }
  for (intptr_t i = 0; i < num_args; i++) {
    Object* e = A->Stack(num_args - i - 1);
    arguments->set_element(i, e);
  }
  Message* message;
  {
    HandleScope h1(H, reinterpret_cast<Object**>(&selector));
    HandleScope h2(H, &receiver);
    HandleScope h3(H, reinterpret_cast<Object**>(&method));
    HandleScope h4(H, reinterpret_cast<Object**>(&arguments));
    message = H->AllocateMessage();  // SAFEPOINT
  }

  message->set_selector(selector);
  message->set_arguments(arguments);

  A->Drop(num_args);
  if (!present_receiver) {
    A->Push(receiver);
  }
  A->Push(message);
  Activate(method, 1);  // SAFEPOINT
}


void Interpreter::SendCannotReturn(Activation* returner,
                                   Object* result) {
  if (TRACE_SPECIAL_CONTROL) {
    OS::PrintErr("#cannotReturn:\n");
  }

  Behavior* receiver_class = returner->Klass(H);
  Behavior* cls = receiver_class;
  Method* method;
  do {
    method = MethodAt(cls, H->object_store()->cannot_return());
    if (method != nil) {
      break;
    }
    cls = cls->superclass();
  } while (cls != nil);

  if (method == nil) {
    FATAL("Missing #cannotReturn:");
  }

  A->Push(returner);
  A->Push(result);
  Activate(method, 1);  // SAFEPOINT
}


void Interpreter::SendUnwindProtect(Activation* returner,
                                    Object* result,
                                    Activation* unwind) {
  if (TRACE_SPECIAL_CONTROL) {
    OS::PrintErr("#aboutToReturn:through:\n");
  }

  Behavior* receiver_class = returner->Klass(H);
  Behavior* cls = receiver_class;
  Method* method;
  do {
    method = MethodAt(cls, H->object_store()->about_to_return_through());
    if (method != nil) {
      break;
    }
    cls = cls->superclass();
  } while (cls != nil);

  if (method == nil) {
    FATAL("Missing #aboutToReturn:through:");
  }

  A->Push(returner);
  A->Push(result);
  A->Push(unwind);
  Activate(method, 2);  // SAFEPOINT
}


void Interpreter::InsertAbsentReceiver(Object* receiver, intptr_t num_args) {
  ASSERT(num_args >= 0);
  ASSERT(num_args < 255);

  ASSERT(A->stack_depth() >= num_args);
  A->Grow(1);
  for (intptr_t i = 0; i < num_args; i++) {
    A->StackPut(i, A->Stack(i + 1));
  }
  A->StackPut(num_args, receiver);
}


void Interpreter::ActivateAbsent(Method* method,
                                 Object* receiver,
                                 intptr_t num_args) {
  InsertAbsentReceiver(receiver, num_args);
  Activate(method, num_args);  // SAFEPOINT
}


void Interpreter::Activate(Method* method,
                           intptr_t num_args) {
  ASSERT(num_args == method->NumArgs());

  HandleScope h1(H, reinterpret_cast<Object**>(&method));

  intptr_t prim = method->Primitive();
  if (prim != 0) {
    if (TRACE_PRIMITIVES) {
      OS::PrintErr("Primitive %" Pd "\n", prim);
    }
    if ((prim & 256) != 0) {
      // Getter
      intptr_t offset = prim & 255;
      ASSERT(num_args == 0);
      Object* receiver = A->Stack(0);
      ASSERT(receiver->IsRegularObject() || receiver->IsEphemeron());
      Object* value = static_cast<RegularObject*>(receiver)->slot(offset);
      A->PopNAndPush(num_args + 1, value);
      return;
    } else if ((prim & 512) != 0) {
      // Setter
      intptr_t offset = prim & 255;
      ASSERT(num_args == 1);
      Object* receiver = A->Stack(1);
      Object* value = A->Stack(0);
      ASSERT(receiver->IsRegularObject() || receiver->IsEphemeron());
      static_cast<RegularObject*>(receiver)->set_slot(offset, value);
      A->PopNAndPush(num_args + 1, receiver);
      return;
    } else if (Primitives::Invoke(prim, num_args, H, this)) {  // SAFEPOINT
      ASSERT(A->stack_depth() >= 0);
      return;
    }
  }

  if (interrupt_ != 0) {
    isolate_->Interrupted();
    UNREACHABLE();
  }

#if RECYCLE_ACTIVATIONS
  recycle_depth_++;
  Activation* new_activation = H->AllocateOrRecycleActivation();  // SAFEPOINT
#else
  Activation* new_activation = H->AllocateActivation();  // SAFEPOINT
#endif
  new_activation->set_sender(A);
  new_activation->set_bci(SmallInteger::New(1));
  new_activation->set_method(method);
  new_activation->set_closure(static_cast<Closure*>(nil));
  new_activation->set_receiver(A->Stack(num_args));
  new_activation->set_stack_depth(0);

  for (intptr_t i = num_args - 1; i >= 0; i--) {
    new_activation->Push(A->Stack(i));
  }
  intptr_t num_temps = method->NumTemps();
  for (intptr_t i = 0; i < num_temps; i++) {
    new_activation->Push(nil);
  }
  A->Drop(num_args + 1);

  H->set_activation(new_activation);
}


ByteString* Interpreter::SelectorAt(intptr_t index) {
  Object* selector = LiteralAt(index);
  ASSERT(selector->IsByteString() && selector->is_canonical());
  return static_cast<ByteString*>(selector);
}


Object* Interpreter::LiteralAt(intptr_t index) {
  ASSERT(index >= 0);
  if (index == A->method()->literals()->Size() + 1) {
    // Hack. When transforming from Squeak CompiledMethod, we drop
    // the last two literals (the selector/AdditionalMethodState and
    // the mixin class association) because we put them in the Method
    // instead of the literal array. The mixin class associtation is
    // accessed by nested class accessors.
    return A->method()->mixin();
  }
  ASSERT(index < A->method()->literals()->Size());
  return A->method()->literals()->element(index);
}


void Interpreter::StaticSuperSend(intptr_t selector_index,
                                  intptr_t num_args) {
  UNREACHABLE();  // Only used by Smalltalk.
}


void Interpreter::LocalReturn(Object* result) {
  Activation* sender = A->sender();
  ASSERT(sender->IsActivation());  // Not nil.
  SmallInteger* sender_bci = sender->bci();
  ASSERT(sender_bci->IsSmallInteger());  // Not nil.

  A->set_sender(static_cast<Activation*>(nil));
  A->set_bci(static_cast<SmallInteger*>(nil));
  sender->Push(result);
#if RECYCLE_ACTIVATIONS
  if (recycle_depth_ > 0) {
    H->RecycleActivation(A);
    recycle_depth_--;
  }
#endif
  H->set_activation(sender);
}


void Interpreter::NonLocalReturn(Object* result) {
  // Search the static chain for the enclosing method activation.
  Closure* c = A->closure();
  Activation* home = c->defining_activation();
  ASSERT(home->IsActivation());
  c = home->closure();
  while (c != nil) {
    home = c->defining_activation();
    ASSERT(home->IsActivation());
    c = home->closure();
  }

  // Walk backward along the dynamic chain looking the enclosing method
  // activation or an unwind-protect activation.
  Activation* unwind = A->sender();
  for (;;) {
    if (unwind == home) break;

    if (!unwind->IsActivation()) {
      ASSERT(unwind == nil);
      // <thisContext> cannotReturn: <result>
      SendCannotReturn(A, result);  // SAFEPOINT
      return;
    }

    if (Primitives::IsUnwindProtect(unwind->method()->Primitive())) {
      // <thisContext> aboutToReturn: <result> through: <unwind>
      SendUnwindProtect(A, result, unwind);  // SAFEPOINT
      return;
    }

    unwind = unwind->sender();
  }

  Activation* sender = home->sender();
  if (!sender->IsActivation() ||
      !sender->bci()->IsSmallInteger()) {
    // <thisContext> cannotReturn: <result>
    SendCannotReturn(A, result);  // SAFEPOINT
    return;
  }

  // The blue book only zaps A. The Squeak context interpreter zaps from A to
  // home. I can't follow what the stack interpreter does.
  Activation* zap = A;
  do {
    Activation* next = zap->sender();
    zap->set_sender(static_cast<Activation*>(nil));
    zap->set_bci(static_cast<SmallInteger*>(nil));
    zap = next;
  } while (zap != sender);

  sender->Push(result);
  H->set_activation(sender);
}


void Interpreter::MethodReturnReceiver() {
  // TODO(rmacnak): Squeak groups the syntactically similar method return and
  // non-local return into the same bytecode. Change the bytecodes to group the
  // functionally similar method return with closure local return instead.
  if (A->closure() == nil) {
    LocalReturn(A->receiver());
  } else {
    ASSERT(A->closure()->IsClosure());
    NonLocalReturn(A->receiver());
  }
}


void Interpreter::MethodReturnTop() {
  // TODO(rmacnak): Squeak groups the syntactically similar method return and
  // non-local return into the same bytecode. Change the bytecodes to group the
  // functionally similar method return with closure local return instead.
  if (A->closure() == nil) {
    LocalReturn(A->Pop());
  } else {
    ASSERT(A->closure()->IsClosure());
    NonLocalReturn(A->Pop());
  }
}


void Interpreter::BlockReturnTop() {
  ASSERT(A->closure()->IsClosure());  // Not nil.
  LocalReturn(A->Pop());
}


void Interpreter::Jump(intptr_t delta) {
  SmallInteger* bci = A->bci();
  A->set_bci(SmallInteger::New(bci->value() + delta));
}


void Interpreter::PopJumpTrue(intptr_t delta) {
  Object* top = A->Stack(0);
  if (top == H->object_store()->false_obj()) {
    A->Drop(1);
  } else if (top == H->object_store()->true_obj()) {
    SmallInteger* bci = A->bci();
    A->set_bci(SmallInteger::New(bci->value() + delta));
    A->Drop(1);
  } else {
    top->Print(H);
    H->PrintStack();
    UNIMPLEMENTED();  // #mustBeBoolean:
  }
}


void Interpreter::PopJumpFalse(intptr_t delta) {
  Object* top = A->Stack(0);
  if (top == H->object_store()->true_obj()) {
    A->Drop(1);
  } else if (top == H->object_store()->false_obj()) {
    SmallInteger* bci = A->bci();
    A->set_bci(SmallInteger::New(bci->value() + delta));
    A->Drop(1);
  } else {
    top->Print(H);
    UNIMPLEMENTED();  // #mustBeBoolean:
  }
}


void Interpreter::Dup() {
  ASSERT(A->stack_depth() > 0);
  A->Push(A->Stack(0));
}


void Interpreter::Pop() {
  ASSERT(A->stack_depth() > 0);
  A->Drop(1);
}


void Interpreter::Nop() {
}


void Interpreter::Break() {
  // <A> #unusedBytecode?
  UNIMPLEMENTED();
}


void Interpreter::CallPrimitive(intptr_t) {
  UNREACHABLE();  // We use header bits instead.
}


uint8_t Interpreter::FetchNextByte() {
  Activation* a = H->activation();
  Method* m = a->method();
  SmallInteger* bci = a->bci();
  ByteArray* bc = m->bytecode();

  uint8_t byte = bc->element(bci->value() - 1);
  a->set_bci(SmallInteger::New(bci->value() + 1));
  return byte;
}


void Interpreter::Interpret() {
  intptr_t extA = 0;
  intptr_t extB = 0;
  for (;;) {
    uint8_t byte1 = FetchNextByte();
    switch (byte1) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
    case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15: {
      PushReceiverVariable(byte1 & 15);
      break;
    }
    case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
    case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31: {
      PushLiteralVariable(byte1 & 15);
      break;
    }
    case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39:
    case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47:
    case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55:
    case 56: case 57: case 58: case 59: case 60: case 61: case 62: case 63: {
      PushLiteral(byte1 & 31);
      break;
    }
    case 64: case 65: case 66: case 67: case 68: case 69: case 70: case 71: {
      PushTemporary(byte1 & 7);
      break;
    }
    case 72: case 73: case 74: case 75: {
      PushTemporary((byte1 & 7) + 8);
      break;
    }
    case 76: {
      PushReceiver();
      break;
    }
    case 77: {
      switch (extB) {
        case 0:
          PushFalse();
          break;
        case 1:
          PushTrue();
          break;
        case 2:
          PushNil();
          break;
        case 3:
          PushThisContext();
          break;
        default:
          PushEnclosingObject(-extB);
          break;
      }
      extB = 0;
      break;
    }
    case 78: {
      PushInteger(0);
      break;
    }
    case 79: {
      PushInteger(1);
      break;
    }
#if STATIC_PREDICTION_BYTECODES
    case 80: {
      // +
      Object* left = A->Stack(1);
      Object* right = A->Stack(0);
      if (left->IsSmallInteger() && right->IsSmallInteger()) {
        intptr_t cleft = static_cast<SmallInteger*>(left)->value();
        intptr_t cright = static_cast<SmallInteger*>(right)->value();
        intptr_t cresult = cleft + cright;
        if (SmallInteger::IsSmiValue(cresult)) {
          A->PopNAndPush(2, SmallInteger::New(cresult));
          break;
        }
      }
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 81: {
      // -
      Object* left = A->Stack(1);
      Object* right = A->Stack(0);
      if (left->IsSmallInteger() && right->IsSmallInteger()) {
        intptr_t cleft = static_cast<SmallInteger*>(left)->value();
        intptr_t cright = static_cast<SmallInteger*>(right)->value();
        intptr_t cresult = cleft - cright;
        if (SmallInteger::IsSmiValue(cresult)) {
          A->PopNAndPush(2, SmallInteger::New(cresult));
          break;
        }
      }
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 82: {
      // <
      Object* left = A->Stack(1);
      Object* right = A->Stack(0);
      if (left->IsSmallInteger() && right->IsSmallInteger()) {
        if (reinterpret_cast<intptr_t>(left) <
            reinterpret_cast<intptr_t>(right)) {
          A->PopNAndPush(2, H->object_store()->true_obj());
        } else {
          A->PopNAndPush(2, H->object_store()->false_obj());
        }
        break;
      }
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 83: {
      // >
      Object* left = A->Stack(1);
      Object* right = A->Stack(0);
      if (left->IsSmallInteger() && right->IsSmallInteger()) {
        if (reinterpret_cast<intptr_t>(left) >
            reinterpret_cast<intptr_t>(right)) {
          A->PopNAndPush(2, H->object_store()->true_obj());
        } else {
          A->PopNAndPush(2, H->object_store()->false_obj());
        }
        break;
      }
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 84: {
      // <=
      Object* left = A->Stack(1);
      Object* right = A->Stack(0);
      if (left->IsSmallInteger() && right->IsSmallInteger()) {
        if (reinterpret_cast<intptr_t>(left) <=
            reinterpret_cast<intptr_t>(right)) {
          A->PopNAndPush(2, H->object_store()->true_obj());
        } else {
          A->PopNAndPush(2, H->object_store()->false_obj());
        }
        break;
      }
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 85: {
      // >=
      Object* left = A->Stack(1);
      Object* right = A->Stack(0);
      if (left->IsSmallInteger() && right->IsSmallInteger()) {
        if (reinterpret_cast<intptr_t>(left) >=
            reinterpret_cast<intptr_t>(right)) {
          A->PopNAndPush(2, H->object_store()->true_obj());
        } else {
          A->PopNAndPush(2, H->object_store()->false_obj());
        }
        break;
      }
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 86: {
      // =
      Object* left = A->Stack(1);
      Object* right = A->Stack(0);
      if (left->IsSmallInteger() && right->IsSmallInteger()) {
        if (reinterpret_cast<intptr_t>(left) ==
            reinterpret_cast<intptr_t>(right)) {
          A->PopNAndPush(2, H->object_store()->true_obj());
        } else {
          A->PopNAndPush(2, H->object_store()->false_obj());
        }
        break;
      }
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 87: {
      // ~=
      Object* left = A->Stack(1);
      Object* right = A->Stack(0);
      if (left->IsSmallInteger() && right->IsSmallInteger()) {
        if (reinterpret_cast<intptr_t>(left) !=
            reinterpret_cast<intptr_t>(right)) {
          A->PopNAndPush(2, H->object_store()->true_obj());
        } else {
          A->PopNAndPush(2, H->object_store()->false_obj());
        }
        break;
      }
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 88: {
      // *
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 89: {
      // /
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 90: {
      /* \\ */
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 91: {
      // @
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 92: {
      // bitShift:
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 93: {
      // //
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 94: {
      // bitAnd:
      Object* left = A->Stack(1);
      Object* right = A->Stack(0);
      if (left->IsSmallInteger() && right->IsSmallInteger()) {
        intptr_t cleft = static_cast<SmallInteger*>(left)->value();
        intptr_t cright = static_cast<SmallInteger*>(right)->value();
        intptr_t cresult = cleft & cright;
        A->PopNAndPush(2, SmallInteger::New(cresult));
        break;
      }
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 95: {
      // bitOr:
      Object* left = A->Stack(1);
      Object* right = A->Stack(0);
      if (left->IsSmallInteger() && right->IsSmallInteger()) {
        intptr_t cleft = static_cast<SmallInteger*>(left)->value();
        intptr_t cright = static_cast<SmallInteger*>(right)->value();
        intptr_t cresult = cleft | cright;
        A->PopNAndPush(2, SmallInteger::New(cresult));
        break;
      }
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 96: {
      // at:
      Array* array = static_cast<Array*>(A->Stack(1));
      SmallInteger* index = static_cast<SmallInteger*>(A->Stack(0));
      if (array->IsArray() && index->IsSmallInteger()) {
        if (index->value() > 0 && index->value() <= array->Size()) {
          Object* value = array->element(index->value() - 1);
          A->PopNAndPush(2, value);
          break;
        }
      }
      QuickCommonSend(byte1 & 15);
      break;
    }
    case 97: {
      // at:put:
      Array* array = static_cast<Array*>(A->Stack(2));
      SmallInteger* index = static_cast<SmallInteger*>(A->Stack(1));
      if (array->IsArray() && index->IsSmallInteger()) {
        if (index->value() > 0 && index->value() <= array->Size()) {
          Object* value = A->Stack(0);
          array->set_element(index->value() - 1, value);
          A->PopNAndPush(3, value);
          break;
        }
      }
      QuickCommonSend(byte1 & 15);
      break;
    }
    case 98: {
      // size
      Array* array = static_cast<Array*>(A->Stack(0));
      if (array->IsArray()) {
        A->PopNAndPush(1, array->size());
        break;
      }
      QuickCommonSend(byte1 & 15);
      break;
    }
    case 99: case 100: case 101: case 102: case 103:
    case 104: case 105: case 106: case 107:
    case 108: case 109: case 110: case 111:
      QuickCommonSend(byte1 & 15);
      break;
#else  // !STATIC_PREDICTION_BYTECODES
    case 80: case 81: case 82: case 83:
    case 84: case 85: case 86: case 87:
    case 88: case 89: case 90: case 91:
    case 92: case 93: case 94: case 95:
      QuickArithmeticSend(byte1 & 15);
      break;
    case 96: case 97: case 98: case 99:
    case 100: case 101: case 102: case 103:
    case 104: case 105: case 106: case 107:
    case 108: case 109: case 110: case 111:
      QuickCommonSend(byte1 & 15);
      break;
#endif  // STATIC_PREDICTION_BYTECODES
    case 112: case 113: case 114: case 115:
    case 116: case 117: case 118: case 119:
    case 120: case 121: case 122: case 123:
    case 124: case 125: case 126: case 127:
      OrdinarySend(byte1 & 15, 0);
      break;
    case 128: case 129: case 130: case 131:
    case 132: case 133: case 134: case 135:
    case 136: case 137: case 138: case 139:
    case 140: case 141: case 142: case 143:
      OrdinarySend(byte1 & 15, 1);
      break;
    case 144: case 145: case 146: case 147:
    case 148: case 149: case 150: case 151:
    case 152: case 153: case 154: case 155:
    case 156: case 157: case 158: case 159:
      OrdinarySend(byte1 & 15, 2);
      break;
    case 160: case 161: case 162: case 163:
    case 164: case 165: case 166: case 167:
    case 168: case 169: case 170: case 171:
    case 172: case 173: case 174: case 175:
      ImplicitReceiverSend(byte1 & 15, 0);
      break;
    case 176: case 177: case 178: case 179:
    case 180: case 181: case 182: case 183:
      PopIntoReceiverVariable(byte1 & 7);
      break;
    case 184: case 185: case 186: case 187:
    case 188: case 189: case 190: case 191:
      PopIntoTemporary(byte1 & 7);
      break;
    case 192: case 193: case 194: case 195:
    case 196: case 197: case 198: case 199:
      Jump((byte1 & 7) + 1);
      break;
    case 200: case 201: case 202: case 203:
    case 204: case 205: case 206: case 207:
      PopJumpTrue((byte1 & 7) + 1);
      break;
    case 208: case 209: case 210: case 211:
    case 212: case 213: case 214: case 215:
      PopJumpFalse((byte1 & 7) + 1);
      break;
    case 216:
      MethodReturnReceiver();
      break;
    case 217:
      MethodReturnTop();
      break;
    case 218:
      BlockReturnTop();
      break;
    case 219:
      Dup();
      break;
    case 220:
      Pop();
      break;
    case 221:
      Nop();
      break;
    case 222:
      Break();
      break;
    case 223:
      FATAL("Unassigned bytecode");
      break;
    case 224: {
      uint8_t byte2 = FetchNextByte();
      extA = (extA << 8) + byte2;
      break;
    }
    case 225: {
      uint8_t byte2 = FetchNextByte();
      if (extB == 0 && byte2 > 127) {
        extB = byte2 - 256;
      } else {
        extB = (extB << 8) + byte2;
      }
      break;
    }
    case 226: {
      uint8_t byte2 = FetchNextByte();
      PushReceiverVariable((extA << 8) + byte2);
      extA = 0;
      break;
    }
    case 227: {
      uint8_t byte2 = FetchNextByte();
      PushLiteralVariable((extA << 8) + byte2);
      extA = 0;
      break;
    }
    case 228: {
      uint8_t byte2 = FetchNextByte();
      PushLiteral(byte2 + extA * 256);
      extA = 0;
      break;
    }
    case 229: {
      uint8_t byte2 = FetchNextByte();
      PushInteger((extB << 8) + byte2);
      extB = 0;
      break;
    }
    case 230: {
      uint8_t byte2 = FetchNextByte();
      PushTemporary(byte2);
      break;
    }
    case 231: {
      uint8_t byte2 = FetchNextByte();
      if (static_cast<int8_t>(byte2) < 0) {
        PushConsArray(byte2 & 127);
      } else {
        PushEmptyArray(byte2 & 127);
      }
      break;
    }
    case 232: {
      uint8_t byte2 = FetchNextByte();
      StoreIntoReceiverVariable((extA << 8) + byte2);
      extA = 0;
      break;
    }
    case 233: {
      uint8_t byte2 = FetchNextByte();
      StoreIntoLiteralVariable((extA << 8) + byte2);
      extA = 0;
      break;
    }
    case 234: {
      uint8_t byte2 = FetchNextByte();
      StoreIntoTemporary((extA << 8) + byte2);
      extA = 0;
      break;
    }
    case 235: {
      uint8_t byte2 = FetchNextByte();
      PopIntoReceiverVariable((extA << 8) + byte2);
      extA = 0;
      break;
    }
    case 236: {
      uint8_t byte2 = FetchNextByte();
      PopIntoLiteralVariable((extA << 8) + byte2);
      extA = 0;
      break;
    }
    case 237: {
      uint8_t byte2 = FetchNextByte();
      PopIntoTemporary((extA << 8) + byte2);
      extA = 0;
      break;
    }
    case 238: {
      uint8_t byte2 = FetchNextByte();
      intptr_t selector_index = (extA << 5) + (byte2 >> 3);
      intptr_t num_args = (extB << 3) | (byte2 & 7);
      OrdinarySend(selector_index, num_args);
      extA = extB = 0;
      break;
    }
    case 239: {
      uint8_t byte2 = FetchNextByte();
      intptr_t selector_index = (extA << 5) + (byte2 >> 3);
      intptr_t num_args = (extB << 3) | (byte2 & 7);
      StaticSuperSend(selector_index, num_args);
      extA = extB = 0;
      break;
    }
    case 240: {
      uint8_t byte2 = FetchNextByte();
      intptr_t selector_index = (extA << 5) + (byte2 >> 3);
      intptr_t num_args = (extB << 3) | (byte2 & 7);
      ImplicitReceiverSend(selector_index, num_args);
      extA = extB = 0;
      break;
    }
    case 241: {
      uint8_t byte2 = FetchNextByte();
      intptr_t selector_index = (extA << 5) + (byte2 >> 3);
      intptr_t num_args = (extB << 3) | (byte2 & 7);
      SuperSend(selector_index, num_args);
      extA = extB = 0;
      break;
    }
    case 242: {
      uint8_t byte2 = FetchNextByte();
      Jump((extB << 8) + byte2);
      extB = 0;
      break;
    }
    case 243: {
      uint8_t byte2 = FetchNextByte();
      PopJumpTrue((extB << 8) + byte2);
      extB = 0;
      break;
    }
    case 244: {
      uint8_t byte2 = FetchNextByte();
      PopJumpFalse((extB << 8) + byte2);
      extB = 0;
      break;
    }
    case 245: {
      uint8_t byte2 = FetchNextByte();
      intptr_t selector_index = (extA << 5) + (byte2 >> 3);
      intptr_t num_args = (extB << 3) | (byte2 & 7);
      SelfSend(selector_index, num_args);
      extA = extB = 0;
      break;
    }
    case 246:
    case 247:
    case 248:
      FATAL("Unassigned bytecode");
    case 249: {
      uint8_t byte2 = FetchNextByte();
      uint8_t byte3 = FetchNextByte();
      CallPrimitive((byte3 << 8) | byte2);
      break;
    }
    case 250: {
      uint8_t byte2 = FetchNextByte();
      uint8_t byte3 = FetchNextByte();
      PushRemoteTemp(byte3, byte2);
      break;
    }
    case 251: {
      uint8_t byte2 = FetchNextByte();
      uint8_t byte3 = FetchNextByte();
      StoreIntoRemoteTemp(byte3, byte2);
      break;
    }
    case 252: {
      uint8_t byte2 = FetchNextByte();
      uint8_t byte3 = FetchNextByte();
      PopIntoRemoteTemp(byte3, byte2);
      break;
    }
    case 253: {
      uint8_t byte2 = FetchNextByte();
      uint8_t byte3 = FetchNextByte();
      intptr_t num_copied = (byte2 >> 3 & 7) + ((extA / 16) << 3);
      intptr_t num_args = (byte2 & 7) + ((extA % 16) << 3);
      intptr_t block_size = byte3 + (extB << 8);
      PushClosure(num_copied, num_args, block_size);
      extA = extB = 0;
      break;
    }
    case 254: {
      uint8_t byte2 = FetchNextByte();
      uint8_t byte3 = FetchNextByte();
      intptr_t selector_index = (extA << 5) + (byte2 >> 3);
      intptr_t num_args = (extB << 3) | (byte2 & 7);
      intptr_t depth = byte3;
      OuterSend(selector_index, num_args, depth);
      extA = extB = 0;
      break;
    }
    case 255:
      FATAL("Unassigned bytecode");
      break;
    default:
      UNREACHABLE();
    }
  }
}

}  // namespace psoup
