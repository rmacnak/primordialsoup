// Copyright (c) 2016, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/interpreter.h"

#include "vm/heap.h"
#include "vm/isolate.h"
#include "vm/math.h"
#include "vm/os.h"
#include "vm/primitives.h"

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
  environment_(NULL),
  lookup_cache_() {
#if LOOKUP_CACHE
  heap->InitializeLookupCache(&lookup_cache_);
#endif
}


void Interpreter::PushLiteralVariable(intptr_t offset) {
  // Not used in Newspeak, except by the implementation of eventual sends.
  // TODO(rmacnak): Add proper eventual send bytecode.
  A->Push(heap_->object_store()->message_loop());
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
  Method* method = A->method();
  Object* literal;
  if (offset == method->literals()->Size() + 1) {
    // Hack. When transforming from Squeak CompiledMethod, we drop
    // the last two literals (the selector/AdditionalMethodState and
    // the mixin class association) because we put them in the Method
    // instead of the literal array. The mixin class associtation is
    // accessed by nested class accessors.
    literal = method->mixin();
  } else {
    ASSERT((offset >= 0) && (offset < method->literals()->Size()));
    literal = method->literals()->element(offset);
  }
  A->Push(literal);
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


void Interpreter::PushNewArrayWithElements(intptr_t size) {
  Array* result = H->AllocateArray(size);  // SAFEPOINT
  for (intptr_t i = 0; i < size; i++) {
    Object* e = A->Stack(size - i - 1);
    result->set_element(i, e);
  }
  A->PopNAndPush(size, result);
}


void Interpreter::PushNewArray(intptr_t size) {
  Array* result = H->AllocateArray(size);  // SAFEPOINT
  for (intptr_t i = 0; i < size; i++) {
    result->set_element(i, nil, kNoBarrier);
  }
  A->Push(result);
}


void Interpreter::PushClosure(intptr_t num_copied,
                              intptr_t num_args,
                              intptr_t block_size) {
  Closure* result = H->AllocateClosure(num_copied);  // SAFEPOINT

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
  String* selector =
    static_cast<String*>(quick_selectors->element(offset * 2));
  ASSERT(selector->is_canonical());
  SmallInteger* arity =
    static_cast<SmallInteger*>(quick_selectors->element(offset * 2 + 1));
  ASSERT(arity->IsSmallInteger());
  SendOrdinary(selector, arity->value());  // SAFEPOINT
}


void Interpreter::QuickCommonSend(intptr_t offset) {
  Array* quick_selectors = H->object_store()->quick_selectors();
  String* selector =
    static_cast<String*>(quick_selectors->element((offset + 16) * 2));
  ASSERT(selector->IsString() && selector->is_canonical());
  SmallInteger* arity =
    static_cast<SmallInteger*>(quick_selectors->element((offset +16) * 2 + 1));
  ASSERT(arity->IsSmallInteger());
  SendOrdinary(selector, arity->value());  // SAFEPOINT
}


Method* Interpreter::MethodAt(Behavior* cls, String* selector) {
  ASSERT(selector->IsString());
  ASSERT(selector->is_canonical());
  Array* methods = cls->methods();
  ASSERT(methods->IsArray());
  intptr_t length = methods->Size();
  for (intptr_t i = 0; i < length; i++) {
    Method* method = static_cast<Method*>(methods->element(i));
    ASSERT(method->selector()->IsString());
    ASSERT(method->selector()->is_canonical());
    if (method->selector() == selector) {
      return method;
    }
  }
  return static_cast<Method*>(nil);
}


bool Interpreter::HasMethod(Behavior* cls, String* selector) {
  return MethodAt(cls, selector) != nil;
}


void Interpreter::OrdinarySend(intptr_t selector_index,
                               intptr_t num_args) {
  String* selector = SelectorAt(selector_index);
  SendOrdinary(selector, num_args);  // SAFEPOINT
}


void Interpreter::SendOrdinary(String* selector,
                               intptr_t num_args) {
  Object* receiver = A->Stack(num_args);

#if LOOKUP_CACHE
  Method* target;
  if (lookup_cache_.LookupOrdinary(receiver->ClassId(), selector, &target)) {
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
        lookup_cache_.InsertOrdinary(receiver->ClassId(), selector, method);
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
  String* selector = SelectorAt(selector_index);
  Object* receiver = A->receiver();

#if LOOKUP_CACHE
  Object* absent_receiver;
  Method* target;
  if (lookup_cache_.LookupNS(receiver->ClassId(),
                             selector,
                             A->method(),
                             kSuper,
                             &absent_receiver,
                             &target)) {
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
                kSuper);  // SAFEPOINT
}


void Interpreter::ImplicitReceiverSend(intptr_t selector_index,
                                       intptr_t num_args) {
  String* selector = SelectorAt(selector_index);
  Object* method_receiver = A->receiver();

#if LOOKUP_CACHE
  Object* absent_receiver;
  Method* target;
  if (lookup_cache_.LookupNS(method_receiver->ClassId(),
                             selector,
                             A->method(),
                             kImplicitReceiver,
                             &absent_receiver,
                             &target)) {
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
                  kImplicitReceiver);  // SAFEPOINT
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
                kImplicitReceiver);  // SAFEPOINT
}


void Interpreter::OuterSend(intptr_t selector_index,
                            intptr_t num_args,
                            intptr_t depth) {
  String* selector = SelectorAt(selector_index);
  Object* receiver = A->receiver();

#if LOOKUP_CACHE
  Object* absent_receiver;
  Method* target;
  if (lookup_cache_.LookupNS(receiver->ClassId(),
                             selector,
                             A->method(),
                             depth,
                             &absent_receiver,
                             &target)) {
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
  SendLexical(selector, num_args, receiver, target_mixin, depth);  // SAFEPOINT
}


void Interpreter::SelfSend(intptr_t selector_index,
                           intptr_t num_args) {
  String* selector = SelectorAt(selector_index);
  Object* receiver = A->receiver();

#if LOOKUP_CACHE
  Object* absent_receiver;
  Method* target;
  if (lookup_cache_.LookupNS(receiver->ClassId(),
                             selector,
                             A->method(),
                             kSelf,
                             &absent_receiver,
                             &target)) {
    if (absent_receiver == 0) {
      absent_receiver = receiver;
    }
    ActivateAbsent(target, absent_receiver, num_args);  // SAFEPOINT
    return;
  }
#endif

  AbstractMixin* method_mixin = A->method()->mixin();
  SendLexical(selector, num_args, receiver, method_mixin, kSelf);  // SAFEPOINT
}


void Interpreter::SendLexical(String* selector,
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
                           method);
#endif
    ActivateAbsent(method, receiver, num_args);  // SAFEPOINT
    return;
  }
  SendProtected(selector, num_args, receiver,
                receiver_class, rule);  // SAFEPOINT
}


void Interpreter::SendProtected(String* selector,
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
                             method);
#endif
      ActivateAbsent(method, receiver, num_args);  // SAFEPOINT
      return;
    }
    lookup_class = lookup_class->superclass();
  }
  bool present_receiver = false;
  SendDNU(selector, num_args, receiver, mixin_application,
          present_receiver);  // SAFEPOINT
}


void Interpreter::SendDNU(String* selector,
                          intptr_t num_args,
                          Object* receiver,
                          Behavior* lookup_class,
                          bool present_receiver) {
  if (TRACE_DNU) {
    char* c1 = receiver->ToCString(H);
    char* c2 = selector->ToCString(H);
    char* c3 = A->method()->selector()->ToCString(H);
    OS::PrintErr("DNU %s %s from %s\n", c1, c2, c3);
    free(c1);
    free(c2);
    free(c3);
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


void Interpreter::SendCannotReturn(Object* result) {
  if (TRACE_SPECIAL_CONTROL) {
    OS::PrintErr("#cannotReturn:\n");
  }

  Behavior* receiver_class = A->Klass(H);
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

  A->Push(A);
  A->Push(result);
  Activate(method, 1);  // SAFEPOINT
}


void Interpreter::SendAboutToReturnThrough(Object* result,
                                           Activation* unwind) {
  if (TRACE_SPECIAL_CONTROL) {
    OS::PrintErr("#aboutToReturn:through:\n");
  }

  Behavior* receiver_class = A->Klass(H);
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

  A->Push(A);
  A->Push(result);
  A->Push(unwind);
  Activate(method, 2);  // SAFEPOINT
}


void Interpreter::SendNonBooleanReceiver(Object* non_boolean) {
  // Note that Squeak instead sends #mustBeBoolean to the non-boolean.
  if (TRACE_SPECIAL_CONTROL) {
    OS::PrintErr("#nonBooleanReceiver:\n");
  }

  Behavior* receiver_class = A->Klass(H);
  Behavior* cls = receiver_class;
  Method* method;
  do {
    method = MethodAt(cls, H->object_store()->non_boolean_receiver());
    if (method != nil) {
      break;
    }
    cls = cls->superclass();
  } while (cls != nil);

  if (method == nil) {
    FATAL("Missing #nonBooleanReceiver:");
  }

  A->Push(A);
  A->Push(non_boolean);
  Activate(method, 1);  // SAFEPOINT
}


void Interpreter::InsertAbsentReceiver(Object* receiver, intptr_t num_args) {
  ASSERT(num_args >= 0);
  ASSERT(num_args < 255);

  ASSERT(A->StackDepth() >= num_args);
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


void Interpreter::Activate(Method* method, intptr_t num_args) {
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
      A->PopNAndPush(1, value);
      return;
    } else if ((prim & 512) != 0) {
      // Setter
      intptr_t offset = prim & 255;
      ASSERT(num_args == 1);
      Object* receiver = A->Stack(1);
      Object* value = A->Stack(0);
      ASSERT(receiver->IsRegularObject() || receiver->IsEphemeron());
      static_cast<RegularObject*>(receiver)->set_slot(offset, value);
      A->PopNAndPush(2, receiver);
      return;
    } else if (Primitives::Invoke(prim, num_args, H, this)) {  // SAFEPOINT
      ASSERT(A->StackDepth() >= 0);
      return;
    }
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
  new_activation->set_closure(static_cast<Closure*>(nil), kNoBarrier);
  new_activation->set_receiver(A->Stack(num_args));
  new_activation->set_stack_depth(SmallInteger::New(0));

  for (intptr_t i = num_args - 1; i >= 0; i--) {
    new_activation->Push(A->Stack(i));
  }
  intptr_t num_temps = method->NumTemps();
  for (intptr_t i = 0; i < num_temps; i++) {
    new_activation->Push(nil);
  }
  A->Drop(num_args + 1);

  H->set_activation(new_activation);

  if (interrupt_ != 0) {
    isolate_->PrintStack();
    Exit();
  }
}


String* Interpreter::SelectorAt(intptr_t index) {
  Array* literals = A->method()->literals();
  ASSERT((index >= 0) && (index < literals->Size()));
  Object* selector = literals->element(index);
  ASSERT(selector->IsString());
  ASSERT(static_cast<String*>(selector)->is_canonical());
  return static_cast<String*>(selector);
}


void Interpreter::LocalReturn(Object* result) {
  // TODO(rmacnak): In Smalltalk, local returns also might trigger
  // #cannotReturn: after manipulation of contexts for coroutining
  // or continuations. This might arise in Newspeak as well depending
  // what activation mirrors can do.
  Activation* sender = A->sender();
  ASSERT(sender->IsActivation());
  SmallInteger* sender_bci = sender->bci();
  ASSERT(sender_bci->IsSmallInteger());  // Not nil.

  A->set_sender(static_cast<Activation*>(nil), kNoBarrier);
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

  // Search the dynamic chain for a dead activation or an unwind-protect
  // activation that would block the return.
  for (Activation* unwind = A->sender();
       unwind != home;
       unwind = unwind->sender()) {
    if (!unwind->IsActivation()) {
      ASSERT(unwind == nil);
      SendCannotReturn(result);  // SAFEPOINT
      return;
    }

    intptr_t prim = unwind->method()->Primitive();
    if (Primitives::IsUnwindProtect(prim)) {
      SendAboutToReturnThrough(result, unwind);  // SAFEPOINT
      return;
    }
    if (Primitives::IsSimulationRoot(prim)) {
      SendCannotReturn(result);  // SAFEPOINT
      return;
    }
  }

  Activation* sender = home->sender();
  if (!sender->IsActivation() ||
      !sender->bci()->IsSmallInteger()) {
    SendCannotReturn(result);  // SAFEPOINT
    return;
  }

  // Mark activations on the dynamic chain up to the return target as dead.
  // Note this follows the behavior of Squeak instead of the blue book, which
  // only zaps A.
  Activation* zap = A;
  do {
    Activation* next = zap->sender();
    zap->set_sender(static_cast<Activation*>(nil), kNoBarrier);
    zap->set_bci(static_cast<SmallInteger*>(nil));
    zap = next;
  } while (zap != sender);

  sender->Push(result);
  H->set_activation(sender);
}


void Interpreter::MethodReturn(Object* result) {
  // TODO(rmacnak): Squeak groups the syntactically similar method return and
  // non-local return into the same bytecode. Change the bytecodes to group the
  // functionally similar method return with closure local return instead.
  if (A->closure() == nil) {
    LocalReturn(result);
  } else {
    ASSERT(A->closure()->IsClosure());
    NonLocalReturn(result);
  }
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


void Interpreter::Enter() {
  ASSERT(environment_ == NULL);
  jmp_buf environment;
  environment_ = &environment;

  if (setjmp(environment) == 0) {
    Interpret();
    UNREACHABLE();
  }

  environment_ = NULL;

  // The longjmp skipped the HandleScope destructors.
  H->DropHandles();
}


void Interpreter::Exit() {
  ASSERT(environment_ != NULL);
  longjmp(*environment_, 1);
}


void Interpreter::Interpret() {
  intptr_t extA = 0;
  intptr_t extB = 0;
  for (;;) {
    uint8_t byte1 = FetchNextByte();
    switch (byte1) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
    case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
      FATAL("Unused bytecode");  // V4: push receiver variable
      break;
    case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
    case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31:
      PushLiteralVariable(byte1 - 16);
      break;
    case 32: case 33: case 34: case 35: case 36: case 37: case 38: case 39:
    case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 47:
    case 48: case 49: case 50: case 51: case 52: case 53: case 54: case 55:
    case 56: case 57: case 58: case 59: case 60: case 61: case 62: case 63:
      PushLiteral(byte1 - 32);
      break;
    case 64: case 65: case 66: case 67: case 68: case 69: case 70: case 71:
    case 72: case 73: case 74: case 75:
      PushTemporary(byte1 - 64);
      break;
    case 76:
      A->Push(A->receiver());
      break;
    case 77:
      switch (extB) {
        case 0:
          A->Push(H->object_store()->false_obj());
          break;
        case 1:
          A->Push(H->object_store()->true_obj());
          break;
        case 2:
          A->Push(H->object_store()->nil_obj());
          break;
        case 3:
          FATAL("Unused bytecode");  // V4: push thisContext
          break;
        default:
          PushEnclosingObject(-extB);
          break;
      }
      extB = 0;
      break;
    case 78:
      A->Push(SmallInteger::New(0));
      break;
    case 79:
      A->Push(SmallInteger::New(1));
      break;
#if STATIC_PREDICTION_BYTECODES
    case 80: {
      // +
      Object* left = A->Stack(1);
      Object* right = A->Stack(0);
      if (left->IsSmallInteger() && right->IsSmallInteger()) {
        intptr_t raw_left = static_cast<SmallInteger*>(left)->value();
        intptr_t raw_right = static_cast<SmallInteger*>(right)->value();
        intptr_t raw_result = raw_left + raw_right;
        if (SmallInteger::IsSmiValue(raw_result)) {
          A->PopNAndPush(2, SmallInteger::New(raw_result));
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
        intptr_t raw_left = static_cast<SmallInteger*>(left)->value();
        intptr_t raw_right = static_cast<SmallInteger*>(right)->value();
        intptr_t raw_result = raw_left - raw_right;
        if (SmallInteger::IsSmiValue(raw_result)) {
          A->PopNAndPush(2, SmallInteger::New(raw_result));
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
      Object* left = A->Stack(1);
      Object* right = A->Stack(0);
      if (left->IsSmallInteger() && right->IsSmallInteger()) {
        intptr_t raw_left = static_cast<SmallInteger*>(left)->value();
        intptr_t raw_right = static_cast<SmallInteger*>(right)->value();
        if (raw_right != 0) {
          intptr_t raw_result = Math::FloorMod(raw_left, raw_right);
          ASSERT(SmallInteger::IsSmiValue(raw_result));
          A->PopNAndPush(2, SmallInteger::New(raw_result));
          break;
        }
      }
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
        intptr_t raw_left = static_cast<SmallInteger*>(left)->value();
        intptr_t raw_right = static_cast<SmallInteger*>(right)->value();
        intptr_t raw_result = raw_left & raw_right;
        A->PopNAndPush(2, SmallInteger::New(raw_result));
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
        intptr_t raw_left = static_cast<SmallInteger*>(left)->value();
        intptr_t raw_right = static_cast<SmallInteger*>(right)->value();
        intptr_t raw_result = raw_left | raw_right;
        A->PopNAndPush(2, SmallInteger::New(raw_result));
        break;
      }
      QuickArithmeticSend(byte1 & 15);
      break;
    }
    case 96: {
      // at:
      Object* array = A->Stack(1);
      SmallInteger* index = static_cast<SmallInteger*>(A->Stack(0));
      if (index->IsSmallInteger()) {
        intptr_t raw_index = index->value() - 1;
        if (array->IsArray()) {
          if ((raw_index >= 0) &&
              (raw_index < static_cast<Array*>(array)->Size())) {
            Object* value = static_cast<Array*>(array)->element(raw_index);
            A->PopNAndPush(2, value);
            break;
          }
        } else if (array->IsBytes()) {
          if ((raw_index >= 0) &&
              (raw_index < static_cast<Bytes*>(array)->Size())) {
            uint8_t raw_value = static_cast<Bytes*>(array)->element(raw_index);
            A->PopNAndPush(2, SmallInteger::New(raw_value));
            break;
          }
        }
      }
      QuickCommonSend(byte1 & 15);
      break;
    }
    case 97: {
      // at:put:
      Object* array = A->Stack(2);
      SmallInteger* index = static_cast<SmallInteger*>(A->Stack(1));
      if (index->IsSmallInteger()) {
        intptr_t raw_index = index->value() - 1;
        if (array->IsArray()) {
          if ((raw_index >= 0) &&
              (raw_index < static_cast<Array*>(array)->Size())) {
            Object* value = A->Stack(0);
            static_cast<Array*>(array)->set_element(raw_index, value);
            A->PopNAndPush(3, value);
            break;
          }
        } else if (array->IsByteArray()) {
          SmallInteger* value = static_cast<SmallInteger*>(A->Stack(0));
          if ((raw_index >= 0) &&
              (raw_index < static_cast<ByteArray*>(array)->Size()) &&
              value <= SmallInteger::New(255)) {
            static_cast<ByteArray*>(array)->set_element(raw_index,
                                                        value->value());
            A->PopNAndPush(3, value);
            break;
          }
        }
      }
      QuickCommonSend(byte1 & 15);
      break;
    }
    case 98: {
      // size
      Object* array = A->Stack(0);
      if (array->IsArray()) {
        A->PopNAndPush(1, static_cast<Array*>(array)->size());
        break;
      } else if (array->IsBytes()) {
        A->PopNAndPush(1, static_cast<Bytes*>(array)->size());
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
      FATAL("Unused bytecode");  // V4: pop into receiver variable
      break;
    case 184: case 185: case 186: case 187:
    case 188: case 189: case 190: case 191:
      PopIntoTemporary(byte1 & 7);
      break;
    case 192: case 193: case 194: case 195:  // V4: short jump
    case 196: case 197: case 198: case 199:  // V4: short jump
    case 200: case 201: case 202: case 203:  // V4: short branch true
    case 204: case 205: case 206: case 207:  // V4: short branch true
    case 208: case 209: case 210: case 211:  // V4: short branch false
    case 212: case 213: case 214: case 215:  // V4: short branch false
      FATAL("Unused bytecode");
      break;
    case 216:
      MethodReturn(A->receiver());
      break;
    case 217:
      MethodReturn(A->Pop());
      break;
    case 218:
      LocalReturn(A->Pop());
      break;
    case 219:
      A->Push(A->Stack(0));
      break;
    case 220:
      A->Drop(1);
      break;
    case 221:  // V4: nop
    case 222:  // V4: break
    case 223:  // V4: not assigned
      FATAL("Unused bytecode");
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
    case 226:
      FATAL("Unused bytecode");  // V4: push receiver variable
      break;
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
      A->Push(SmallInteger::New((extB << 8) + byte2));
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
      if (byte2 < 128) {
        PushNewArray(byte2);
      } else {
        PushNewArrayWithElements(byte2 - 128);
      }
      break;
    }
    case 232:  // V4: store into receiver variable
    case 233:  // V4: store into literal variable
      FATAL("Unused bytecode");
      break;
    case 234: {
      uint8_t byte2 = FetchNextByte();
      StoreIntoTemporary(byte2);
      break;
    }
    case 235:  // V4: pop into receiver variable
    case 236:  // V4: pop into literal variable
      FATAL("Unused bytecode");
      break;
    case 237: {
      uint8_t byte2 = FetchNextByte();
      PopIntoTemporary(byte2);
      break;
    }
    case 238: {
      uint8_t byte2 = FetchNextByte();
      intptr_t selector_index = (extA << 5) + (byte2 >> 3);
      intptr_t num_args = (extB << 3) | (byte2 & 7);
      extA = extB = 0;
      OrdinarySend(selector_index, num_args);
      break;
    }
    case 239:
      FATAL("Unused bytecode");  // V4: static super send
      break;
    case 240: {
      uint8_t byte2 = FetchNextByte();
      intptr_t selector_index = (extA << 5) + (byte2 >> 3);
      intptr_t num_args = (extB << 3) | (byte2 & 7);
      extA = extB = 0;
      ImplicitReceiverSend(selector_index, num_args);
      break;
    }
    case 241: {
      uint8_t byte2 = FetchNextByte();
      intptr_t selector_index = (extA << 5) + (byte2 >> 3);
      intptr_t num_args = (extB << 3) | (byte2 & 7);
      extA = extB = 0;
      SuperSend(selector_index, num_args);
      break;
    }
    case 242: {
      uint8_t byte2 = FetchNextByte();
      intptr_t delta = (extB << 8) + byte2;
      extB = 0;
      A->set_bci(SmallInteger::New(A->bci()->value() + delta));
      break;
    }
    case 243: {
      uint8_t byte2 = FetchNextByte();
      intptr_t delta = (extB << 8) + byte2;
      extB = 0;
      Object* top = A->Pop();
      if (top == H->object_store()->false_obj()) {
      } else if (top == H->object_store()->true_obj()) {
        A->set_bci(SmallInteger::New(A->bci()->value() + delta));
      } else {
        SendNonBooleanReceiver(top);
      }
      break;
    }
    case 244: {
      uint8_t byte2 = FetchNextByte();
      intptr_t delta = (extB << 8) + byte2;
      extB = 0;
      Object* top = A->Pop();
      if (top == H->object_store()->true_obj()) {
      } else if (top == H->object_store()->false_obj()) {
        A->set_bci(SmallInteger::New(A->bci()->value() + delta));
      } else {
        SendNonBooleanReceiver(top);
      }
      break;
    }
    case 245: {
      uint8_t byte2 = FetchNextByte();
      intptr_t selector_index = (extA << 5) + (byte2 >> 3);
      intptr_t num_args = (extB << 3) | (byte2 & 7);
      extA = extB = 0;
      SelfSend(selector_index, num_args);
      break;
    }
    case 246:  // V4: unassigned
    case 247:  // V4: unassigned
    case 248:  // V4: unassigned
    case 249:  // V4: call primitive
      FATAL("Unused bytecode");
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
      extA = extB = 0;
      PushClosure(num_copied, num_args, block_size);
      break;
    }
    case 254: {
      uint8_t byte2 = FetchNextByte();
      uint8_t byte3 = FetchNextByte();
      intptr_t selector_index = (extA << 5) + (byte2 >> 3);
      intptr_t num_args = (extB << 3) | (byte2 & 7);
      intptr_t depth = byte3;
      extA = extB = 0;
      OuterSend(selector_index, num_args, depth);
      break;
    }
    case 255:
      FATAL("Unused bytecode");  // V4: unassigned
      break;
    default:
      UNREACHABLE();
    }
  }
}

}  // namespace psoup
