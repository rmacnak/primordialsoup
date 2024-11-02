// Copyright (c) 2012, the Newspeak project authors. Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#ifndef VM_GLOBALS_H_
#define VM_GLOBALS_H_

#if defined(_WIN32)
// Cut down on the amount of stuff that gets included via windows.h.
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#include <windows.h>
#endif  // defined(_WIN32)

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// OS detection.
// for more information on predefined macros:
//   - http://msdn.microsoft.com/en-us/library/b0084kay.aspx
//   - with gcc, run: "echo | gcc -E -dM -"
#if defined(__ANDROID__)
#define OS_ANDROID 1
#elif defined(__linux__) || defined(__FreeBSD__)
#define OS_LINUX 1
#elif defined(__APPLE__)
// Define the flavor of Mac OS we are running on.
#include <TargetConditionals.h>
#define OS_MACOS 1
#if TARGET_OS_IPHONE
#define OS_IOS 1
#endif

#elif defined(_WIN32)
#define OS_WINDOWS 1
#elif defined(__Fuchsia__)
#define OS_FUCHSIA 1
#elif defined(__EMSCRIPTEN__)
#define OS_EMSCRIPTEN 1
#else
#error Automatic OS detection failed.
#endif

#if defined(_M_X64) || defined(_M_ARM64) || (__SIZEOF_POINTER__ == 8)
#define ARCH_IS_64_BIT 1
#elif defined(_M_IX86) || defined(_M_ARM) || (__SIZEOF_POINTER__ == 4)
#define ARCH_IS_32_BIT 1
#else
#error Unknown architecture.
#endif

// ATTRIBUTE_UNUSED indicates to the compiler that a variable/typedef is
// expected to be unused and disables the related warning.
#ifdef __GNUC__
#define ATTRIBUTE_UNUSED __attribute__((unused))
#else
#define ATTRIBUTE_UNUSED
#endif

// Short form printf format specifiers
#if defined(OS_EMSCRIPTEN)
#define Pd "ld"
#define Pu "lu"
#define Px "lx"
#else
#define Pd PRIdPTR
#define Pu PRIuPTR
#define Px PRIxPTR
#endif
#define Pd64 PRId64
#define Pu64 PRIu64
#define Px64 PRIx64

// Suffixes for 64-bit integer literals.
#ifdef _MSC_VER
#define PSOUP_INT64_C(x) x##I64
#define PSOUP_UINT64_C(x) x##UI64
#else
#define PSOUP_INT64_C(x) x##LL
#define PSOUP_UINT64_C(x) x##ULL
#endif

// The following macro works on both 32 and 64-bit platforms.
// Usage: instead of writing 0x1234567890123456ULL
//      write PSOUP_2PART_UINT64_C(0x12345678,90123456);
#define PSOUP_2PART_UINT64_C(a, b)                                             \
  (((static_cast<uint64_t>(a) << 32) + 0x##b##u))

// Integer constants.
constexpr int32_t kMinInt32 = 0x80000000;
constexpr int32_t kMaxInt32 = 0x7FFFFFFF;
constexpr uint32_t kMaxUint32 = 0xFFFFFFFF;
constexpr int64_t kMinInt64 = PSOUP_INT64_C(0x8000000000000000);
constexpr int64_t kMaxInt64 = PSOUP_INT64_C(0x7FFFFFFFFFFFFFFF);
constexpr uint64_t kMaxUint64 = PSOUP_2PART_UINT64_C(0xFFFFFFFF, FFFFFFFF);
constexpr int64_t kSignBitDouble = PSOUP_INT64_C(0x8000000000000000);

// Types for native machine words. Guaranteed to be able to hold pointers and
// integers.
typedef intptr_t word;
typedef uintptr_t uword;

// Various toolchains are missing either std::nullptr_t or unqualified
// nullptr_t, so we declare it ourselves.
typedef decltype(nullptr) nullptr_t;

// Byte sizes.
constexpr size_t kWordSize = sizeof(word);
constexpr size_t kDoubleSize = sizeof(double);
constexpr size_t kFloatSize = sizeof(float);
constexpr size_t kInt32Size = sizeof(int32_t);
constexpr size_t kInt16Size = sizeof(int16_t);
#if defined(ARCH_IS_32_BIT)
constexpr size_t kWordSizeLog2 = 2;
constexpr uword kUwordMax = kMaxUint32;
#elif defined(ARCH_IS_64_BIT)
constexpr size_t kWordSizeLog2 = 3;
constexpr uword kUwordMax = kMaxUint64;
#endif

// Bit sizes.
constexpr size_t kBitsPerByte = 8;
constexpr size_t kBitsPerWord = kWordSize * kBitsPerByte;

// System-wide named constants.
constexpr size_t KB = 1024;
constexpr size_t KBLog2 = 10;
constexpr size_t MB = KB * KB;
constexpr size_t MBLog2 = KBLog2 + KBLog2;
constexpr size_t GB = MB * KB;
constexpr size_t GBLog2 = MBLog2 + KBLog2;

// Time constants.
constexpr int64_t kMillisecondsPerSecond = 1000;
constexpr int64_t kMicrosecondsPerMillisecond = 1000;
constexpr int64_t kMicrosecondsPerSecond = (kMicrosecondsPerMillisecond *
                                            kMillisecondsPerSecond);
constexpr int64_t kNanosecondsPerMicrosecond = 1000;
constexpr int64_t kNanosecondsPerMillisecond = (kNanosecondsPerMicrosecond *
                                                kMicrosecondsPerMillisecond);
constexpr int64_t kNanosecondsPerSecond = (kNanosecondsPerMicrosecond *
                                           kMicrosecondsPerSecond);

// A macro to disallow the copy constructor and operator= functions.
// This should be used in the private: declarations for a class.
#if !defined(DISALLOW_COPY_AND_ASSIGN)
#define DISALLOW_COPY_AND_ASSIGN(TypeName)                                     \
 private:                                                                      \
  TypeName(const TypeName&);                                                   \
  void operator=(const TypeName&)
#endif  // !defined(DISALLOW_COPY_AND_ASSIGN)

// A macro to disallow all the implicit constructors, namely the default
// constructor, copy constructor and operator= functions. This should be
// used in the private: declarations for a class that wants to prevent
// anyone from instantiating it. This is especially useful for classes
// containing only static methods.
#if !defined(DISALLOW_IMPLICIT_CONSTRUCTORS)
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName)                               \
 private:                                                                      \
  TypeName();                                                                  \
  DISALLOW_COPY_AND_ASSIGN(TypeName)
#endif  // !defined(DISALLOW_IMPLICIT_CONSTRUCTORS)

// Macro to disallow allocation in the C++ heap. This should be used
// in the private section for a class. Don't use UNREACHABLE here to
// avoid circular dependencies between platform/globals.h and
// platform/assert.h.
#if !defined(DISALLOW_ALLOCATION)
#define DISALLOW_ALLOCATION()                                                  \
 public:                                                                       \
  void operator delete(void* pointer) {                                        \
    fprintf(stderr, "unreachable code\n");                                     \
    abort();                                                                   \
  }                                                                            \
                                                                               \
 private:                                                                      \
  void* operator new(size_t size)
#endif  // !defined(DISALLOW_ALLOCATION)

// The type-based aliasing rule allows the compiler to assume that
// pointers of different types (for some definition of different)
// never alias each other. Thus the following code does not work:
//
// float f = foo();
// int fbits = *(int*)(&f);
//
// The compiler 'knows' that the int pointer can't refer to f since
// the types don't match, so the compiler may cache f in a register,
// leaving random data in fbits.  Using C++ style casts makes no
// difference, however a pointer to char data is assumed to alias any
// other pointer. This is the 'memcpy exception'.
//
// The bit_cast function uses the memcpy exception to move the bits
// from a variable of one type to a variable of another type. Of
// course the end result is likely to be implementation dependent.
// Most compilers (gcc-4.2 and MSVC 2005) will completely optimize
// bit_cast away.
//
// There is an additional use for bit_cast. Recent gccs will warn when
// they see casts that may result in breakage due to the type-based
// aliasing rule. If you have checked that there is no breakage you
// can use bit_cast to cast one pointer type to another. This confuses
// gcc enough that it can no longer see that you have cast one pointer
// type to another thus avoiding the warning.
template <class D, class S>
inline D bit_cast(const S& source) {
  static_assert(sizeof(D) == sizeof(S),
                "Destination and source of bit_cast must have the same size");

  D destination;
  // This use of memcpy is safe: source and destination cannot overlap.
  memcpy(&destination, &source, sizeof(destination));
  return destination;
}

#if defined(__GNUC__) || defined(__clang__)
// Tell the compiler to do printf format string checking if the
// compiler supports it; see the 'format' attribute in
// <http://gcc.gnu.org/onlinedocs/gcc-4.3.0/gcc/Function-Attributes.html>.
//
// N.B.: As the GCC manual states, "[s]ince non-static C++ methods
// have an implicit 'this' argument, the arguments of such methods
// should be counted from two, not one."
#define PRINTF_ATTRIBUTE(string_index, first_to_check)                         \
  __attribute__((__format__(__printf__, string_index, first_to_check)))
#else
#define PRINTF_ATTRIBUTE(string_index, first_to_check)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define NORETURN __declspec(noreturn)
#else
#define NORETURN
#endif

#if defined(__GNUC__) || defined(__clang__)
#define INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
#define INLINE __forceinline
#else
#define INLINE
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE
#endif

#if defined(__has_feature)
#if __has_feature(undefined_behavior_sanitizer)
#define USING_UNDEFINED_BEHAVIOR_SANITIZER
#endif
#endif

#if defined(USING_UNDEFINED_BEHAVIOR_SANITIZER)
#define NO_SANITIZE_UNDEFINED(check) __attribute__((no_sanitize(check)))
#else
#define NO_SANITIZE_UNDEFINED(check)
#endif

#endif  // VM_GLOBALS_H_
