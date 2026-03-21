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

// Short form printf format specifiers
#define Pd PRIdPTR
#define Pu PRIuPTR
#define Px PRIxPTR
#define Pd64 PRId64
#define Pu64 PRIu64
#define Px64 PRIx64

// Integer constants.
constexpr int32_t kMinInt32 = 0x80000000;
constexpr int32_t kMaxInt32 = 0x7FFFFFFF;
constexpr uint32_t kMaxUint32 = 0xFFFFFFFF;
constexpr int64_t kMinInt64 = 0x8000000000000000;
constexpr int64_t kMaxInt64 = 0x7FFFFFFFFFFFFFFF;
constexpr uint64_t kMaxUint64 = 0xFFFFFFFFFFFFFFFF;
constexpr int64_t kSignBitDouble = 0x8000000000000000;

// Types for native machine words. Guaranteed to be able to hold pointers and
// integers.
typedef intptr_t word;
typedef uintptr_t uword;

// Various toolchains are missing either std::nullptr_t or unqualified
// nullptr_t, so we declare it ourselves.
typedef decltype(nullptr) nullptr_t;

// Byte sizes.
constexpr size_t kWordSize = sizeof(word);
#if defined(ARCH_IS_32_BIT)
constexpr size_t kWordSizeLog2 = 2;
#elif defined(ARCH_IS_64_BIT)
constexpr size_t kWordSizeLog2 = 3;
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
  TypeName(const TypeName&) = delete;                                          \
  void operator=(const TypeName&) = delete
#endif  // !defined(DISALLOW_COPY_AND_ASSIGN)

// A macro to disallow all the implicit constructors, namely the default
// constructor, copy constructor and operator= functions. This should be
// used in the private: declarations for a class that wants to prevent
// anyone from instantiating it. This is especially useful for classes
// containing only static methods.
#if !defined(DISALLOW_IMPLICIT_CONSTRUCTORS)
#define DISALLOW_IMPLICIT_CONSTRUCTORS(TypeName)                               \
 private:                                                                      \
  TypeName() = delete;                                                         \
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
  void* operator new(size_t size) = delete
#endif  // !defined(DISALLOW_ALLOCATION)

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

#if defined(__clang__)
#define ASSUME(expression) __builtin_assume(expression)
#elif defined(__GNUC__)
#define ASSUME(expression) if (!(expression)) __builtin_unreachable()
#elif defined(_MSC_VER)
#define ASSUME(expression) __assume(expression)
#else
#define ASSUME(expression) do { } while (false && (expression))
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
