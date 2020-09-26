# Design of Primordial Soup

Primordial Soup (or psoup) is an implementation of the Newspeak programming language. It consists of a virtual machine and platform modules written against the primitives the VM provides.

A Newspeak program defines a graph of objects that evolves over time as a result of message sends. The virtual machine runs snapshots, which are the serialized form of the platform object and an application object. The VM starts by deserializing a snapshot into a heap, then interprets bytecode from an initial message until it encounters an exit primitive. This repository includes a snapshot for a compiler that takes Newspeak source code and produces application snapshots that can be run by the VM.

The major components of the virtual machine are an object memory, an interpreter and a set of primitives. The object memory defines the representation of objects in memory, and manages their allocation and garbage collection. The interpreter executes bytecode methods produced by the compiler. It implements operations on the expression stack, message sends and returns. Some methods have a corresponding primitive, which runs in place of the method body. Primitives implement operations that cannot be expressed by the language as a regular method body, such as arithmetic, array access, string operations, become, allInstances, isolate messaging, etc. Together, these components direct the evolution of a program's objects over time.

(It is essential for the object-capability model that code outside of the platform implementation cannot mark its methods as primitive, otherwise the set of primitives would constitute ambient authority. Currently the compiler marks methods as primitive based metadata in their bodies, providing an ambient authority.)

## Object Representation

The object representation is based on that of the [Dart VM](https://github.com/dart-lang/sdk/blob/master/runtime/vm/raw_object.h).

Object pointers refer either to immediate objects or heap objects, as distinguished by a tag in the low bits of the pointer. Primordial Soup has only one kind of immediate object, SmallIntegers, whose pointers have a tag of 0. Heap objects have a pointer tag of 1. The upper bits of a SmallInteger pointer are its value, and the upper bits of a heap object pointer are the most signficant bits of its address (the least significant bit is always 0 because heap objects always have greater than 2-byte alignment).

A tag of 0 allows many operations on SmallInteger without untagging and retagging. It also allows hiding aligned addresses to the C heap from the GC.

A tag of 1 has no penalty on heap object access because removing the tag can be folded into the offset used for memory access.

Heap objects are always allocated in double-word increments. Objects in old-space are kept at double-word alignment, and objects in new-space are kept offset from double-word alignment. This allows checking an object's age without comparing to a boundry address, avoiding restrictions on heap placement and avoiding loading the boundry from thread-local storage. Additionally, the scavenger can quickly skip over both immediates and old objects with a single branch.

Heap objects have a single-word header, which encodes the object's class, size, and some status flags.

(Currently objects have a second header word storing an identity hash. This will be moved to a side table.)

An object's class is encoded as an index into a class table, its cid. The cid occupies the upper half-word of the header and can be loaded with a single instruction.

An object's size is encoded in allocation (double-word) units, or 0 if it is larger than the 8/16-bit header field allows. The size field can only overflow for variable-length objects such as arrays or strings, in which case the object's size in memory is computed from its length field. The size field is only used for computing the address of the next object in the heap for iteration, and is not used for bounds checks.

The objects of a handful of classes have special representations, and their cids are constant values known to the VM. These classes are SmallInteger, MediumInteger, LargeInteger, Float64, ByteArray, String, Array, WeakArray, Ephemeron, Activation and Closure. All other objects have a fixed number of pointers as determined by their class. In particular, Method, Class, Metaclass, InstanceMixin and ClassMixin are regular classes.

SmallIntegers represent signed 31-/63-bit values on 32-/64-bit architectures respectively, MediumIntegers represent signed 64-bit values, and LargeIntegers represent values with greater magnitude. Integer operations produce the smallest representation that can fit the result.

The variable-length objects (ByteArray, String, Array, WeakArray, Closure) have a length field encoded as a SmallInteger. This allows bounds checks and length access to happen without checking for an overflow field and without converting the length or index. Each of these classes has its own set of accessing primitives, so there is no need to check a format field as in Squeak, the class check for method lookup was enough.

## Garbage Collector

Primordial Soup uses a stop-the-world, generational garbage collector. The new generation uses a semispace scavenger; the old generation uses mark-sweep. New objects are allocated out of double-word alignment and old objects are allocated at double-word aligment. The generational write barrier detects old->new stores by examining the low bits of the source and target objects.

The garbage collector supports weak arrays and a weak class table, as a well as a restricted version of [ephemerons](http://dl.acm.org/citation.cfm?id=263733) where the only action an ephemeron takes on firing is to nil its value slot.

## Behaviors

The hash function for strings is random for each invocation of the VM. To avoid rehashing after snapshot loading, method dictionaries and nested mixins are represented as simple lists instead of hash tables as in Squeak.

## Doubles

Double parsing and printing uses the V8-derived [double-conversion library](https://github.com/google/double-conversion).

## Isolates

Primordial Soup allows creating multiple "isolates" in the same process. Isolates have separate heaps and communicate via message passing (byte arrays). Each isolate has its own thread of control and can run concurrently with other isolates.

Each isolate may contain multiple actors.

## Snapshots

The initial heap of an isolate is loaded from a snapshot. Unlike traditional Smalltalk images, this snapshot is not a memory dump with pointer fixups. Nor is it a traditional recursive serialization like the Dart VM's snapshots. Instead it is clustered serialization like [Fuel](http://rmod.inria.fr/web/software/Fuel) and [Parcels](http://scg.unibe.ch/archive/papers/Mira05aParcels.pdf).

Clustered serialization groups objects into clusters (usually by class) and encodes all of the nodes of the graph before all of the edges. Grouping allows writing type information once per cluster instead of once per object. Placing all nodes before all edges allows filling objects with a simple unconditional table load in a loop.

Unlike Smalltalk images, these snapshots are portable between architectures with different word sizes.

Also unlike Smalltalk images, these snapshots are not used to provide process persistence. The VM contains only a deserializer. The serializer needed to create a new snapshot is Newspeak code.

A snapshot used to start the VM contains a complete graph. Its root object is an array containing all the objects known to the VM, including the classes with special formats, the #doesNotUnderstand:/#cannotReturn:/etc selectors, and the scheduler object. This array is known as the object store. (Its equivalent object in Squeak Smalltalk is known as the specialObjectsArray). The object store and the current activation record are the GC roots.

Messages between isolates use the same snapshot format, but they contain partial graphs. A set of common objects known to the sender and receiver is implicitly used as the first nodes. The common objects are mostly the classes of literals and classes for the representation of compiled code.

## Bytecode

Primordial Soup uses a variable-length, stack-machine bytecode derived from the Newsqueak V4 bytecode of the [Cog VM](http://www.mirandabanda.org/cogblog/about-cog/).

## Stack-to-Context Mapping

Newspeak, like Smalltalk, provides first-class activation records. Newspeak calls them _Activations_ and Smalltalk calls them _Contexts_. In Smalltalk they are accessible from the psuedo-variable `thisContext` and in Newspeak they are accessed via activation mirrors. Activations make possible introspection of the program state and arbitrary control constructs without specific support from the VM, including

 - displaying the state of computation in a debugger
 - stepping in a debugger with arbitrary stopping policies
 - expression evaluation with access to local slots
 - resumable exceptions
 - generators
 - continutations, full or delimited
 - corountines or other kinds cooperative multi-threading

The simplest implementation allocates activation objects for each method or closure invocation and executes the program by manipulating these objects. Most program time, however, is spent in simple sends and returns. Primordial Soup avoids the allocation and indirection costs of such an implementation by running execution on a mostly conventional stack.

Each frame has an extra zero-initialized slot to remember a paired activation object. Whenever the program requires a first-class activation, the object is allocated, stable state like the method is copied into the object, and the object and frame are given pointers to each other. Whenever state is accessed through the activation object, it can be in three states:

 - the object is no longer paired with a frame: its state can be accessed directly
 - the object is paired with a dead frame: all the volatile state is set to nil and frame pointer is cleared
 - the object is paired with a living frame: we read state out of the frame or flush the stack to objects, write state to the object, then restore the top activation to the stack

In the common case where first-class activations are not used, the only overhead compared to an implementation not providing first-class activations is the initialization of the extra frame slot.  In particular, no extra work is performed on return; all volatile state is implicitly cleared by return making the frame pointer from activation object invalid. For a more detailed account of this scheme in the Cog VM, see [Under Cover Contexts and the Big Frame-Up](http://www.mirandabanda.org/cogblog/2009/01/14/under-cover-contexts-and-the-big-frame-up).

## Bootstraping

Circularizing the next kernel.
