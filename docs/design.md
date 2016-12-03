# Primordial Soup Design

Primordial Soup is a bytecode interpreter, using the V4 bytecode of the Cog VM and the object representation of the Dart VM. It uses an architecture-independent clustering snapshot format inspired by Fuel and Parcels. 

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

The objects of a handful of classes have special representations, and their cids are constant values known to the VM. These classes are SmallInteger, MediumInteger, LargeInteger, Float64, ByteArray, ByteString, WideString, Array, WeakArray, Ephemeron, Activation and Closure. All other objects have a fixed number of pointers as determined by their class. In particular, Method, Class, Metaclass, InstanceMixin and ClassMixin are regular classes.

SmallIntegers represent signed 31-/63-bit values on 32-/64-bit architectures respectively, MediumIntegers represent signed 64-bit values, and LargeIntegers represent values with greater magnitude.

The variable-length objects (ByteArray, ByteString, WideString, Array, WeakArray, Closure) have a length field encoded as a SmallInteger. This allows bounds checks and length access to happen without checking for an overflow field and without converting the length or index. Each of these classes has its own set of accessing primitives, so there is no need to check a format field as in Squeak, the class check for method lookup was enough.

## Garbage Collector

Primordial Soup uses a simple stop-the-world copying garbage collector based on the Dart VM's scavenger. Although the object representation has reserved alignment bits for distinguishing old and new objects, there is currently only a single generation.

The garbage collector supports weak arrays and a weak class table, as a well as a restricted version of [ephemerons](http://dl.acm.org/citation.cfm?id=263733) where the only action an ephemeron takes on firing is to nil its value slot.

## Behaviors

The hash function for strings is random for each invocation of the VM. To avoid rehashing after snapshot loading, method dictionaries and nested mixins are represented as simple lists instead of hash tables as in Squeak.

## Doubles

Double parsing and printing uses the V8-derived [double-conversion library](https://github.com/google/double-conversion).

## Isolates

Primordial Soup allows creating multiple "isolates" in the same process. Isolates have separate heaps and communicate via message passing (byte arrays). Each isolate has its own thread of control and can run concurrently with other isolates.

Each isolate may contain multiple actors.

## Snapshot Format

The initial heap of an isolate is loaded from a snapshot. Unlike traditional Smalltalk images, this snapshot is not a memory dump with pointer fixups. Nor is it a traditional recursive serialization like the Dart VM's snapshots. Instead it is clustered serialization like [Fuel](http://rmod.inria.fr/web/software/Fuel) and [Parcels](http://scg.unibe.ch/archive/papers/Mira05aParcels.pdf).

Clustered serialization groups objects into clusters (usually by class) and encodes all of the nodes of the graph before all of the edges. Grouping allows writing type information once per cluster instead of once per object. Placing all nodes before all edges allows filling objects with a simple unconditional table load in a loop.

Unlike Smalltalk images, these snapshots are portable between architectures with different word sizes.

Also unlike Smalltalk images, these snapshots are not used to provide process persistence. The VM contains only a deserializer. The serializer needed to create a new snapshot is Newspeak code.

A snapshot used to start the VM contains a complete graph. Its root object is an array containing all the objects known to the VM, including the classes with special formats, the #doesNotUnderstand:/#cannotReturn:/etc selectors, and the scheduler object. This array is known as the object store. (Its equivalent object in Squeak Smalltalk is known as the specialObjectsArray). The object store and the current activation record are the GC roots.

Messages between isolates use the same snapshot format, but they contain partial graphs. A set of common objects known to the sender and receiver is implicitly used as the first nodes. The common objects are mostly the classes of literals and classes for the representation of compiled code.

## Bytecode

Primordial Soup uses the Newsqueak V4 bytecode of the [Cog VM](http://www.mirandabanda.org/cogblog/about-cog/), but only the subset required by Newspeak. A description of this bytecode set may be found in the class comment of EncoderForNewsqueakV4 in a [VMMaker](http://www.mirandabanda.org/cogblog/build-image/) or [Newspeak-on-Squeak](http://www.newspeaklanguage.org/downloads) image.

Using this bytecode set allows us to take advantage of the existing Newspeak-on-Squeak bytecode compiler. Eventually we may use a modified bytecode set to make use of the opcode space occupied by Smalltalk-only bytecodes and to add VM-level support for eventual sends.

## Bootstraping

Circularizing the next kernel.
