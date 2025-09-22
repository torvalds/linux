//===-- internal_defs.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_INTERNAL_DEFS_H_
#define SCUDO_INTERNAL_DEFS_H_

#include "platform.h"

#include <stdint.h>

#ifndef SCUDO_DEBUG
#define SCUDO_DEBUG 0
#endif

#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

// String related macros.

#define STRINGIFY_(S) #S
#define STRINGIFY(S) STRINGIFY_(S)
#define CONCATENATE_(S, C) S##C
#define CONCATENATE(S, C) CONCATENATE_(S, C)

// Attributes & builtins related macros.

#define INTERFACE __attribute__((visibility("default")))
#define HIDDEN __attribute__((visibility("hidden")))
#define WEAK __attribute__((weak))
#define ALWAYS_INLINE inline __attribute__((always_inline))
#define ALIAS(X) __attribute__((alias(X)))
#define FORMAT(F, A) __attribute__((format(printf, F, A)))
#define NOINLINE __attribute__((noinline))
#define NORETURN __attribute__((noreturn))
#define LIKELY(X) __builtin_expect(!!(X), 1)
#define UNLIKELY(X) __builtin_expect(!!(X), 0)
#if defined(__i386__) || defined(__x86_64__)
// __builtin_prefetch(X) generates prefetchnt0 on x86
#define PREFETCH(X) __asm__("prefetchnta (%0)" : : "r"(X))
#else
#define PREFETCH(X) __builtin_prefetch(X)
#endif
#define UNUSED __attribute__((unused))
#define USED __attribute__((used))
#define NOEXCEPT noexcept

// This check is only available on Clang. This is essentially an alias of
// C++20's 'constinit' specifier which will take care of this when (if?) we can
// ask all libc's that use Scudo to compile us with C++20. Dynamic
// initialization is bad; Scudo is designed to be lazy-initializated on the
// first call to malloc/free (and friends), and this generally happens in the
// loader somewhere in libdl's init. After the loader is done, control is
// transferred to libc's initialization, and the dynamic initializers are run.
// If there's a dynamic initializer for Scudo, then it will clobber the
// already-initialized Scudo, and re-initialize all its members back to default
// values, causing various explosions. Unfortunately, marking
// scudo::Allocator<>'s constructor as 'constexpr' isn't sufficient to prevent
// dynamic initialization, as default initialization is fine under 'constexpr'
// (but not 'constinit'). Clang at -O0, and gcc at all opt levels will emit a
// dynamic initializer for any constant-initialized variables if there is a mix
// of default-initialized and constant-initialized variables.
//
// If you're looking at this because your build failed, you probably introduced
// a new member to scudo::Allocator<> (possibly transiently) that didn't have an
// initializer. The fix is easy - just add one.
#if defined(__has_attribute)
#if __has_attribute(require_constant_initialization)
#define SCUDO_REQUIRE_CONSTANT_INITIALIZATION                                  \
  __attribute__((__require_constant_initialization__))
#else
#define SCUDO_REQUIRE_CONSTANT_INITIALIZATION
#endif
#endif

namespace scudo {

typedef uintptr_t uptr;
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef intptr_t sptr;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

// The following two functions have platform specific implementations.
void outputRaw(const char *Buffer);
void NORETURN die();

#define RAW_CHECK_MSG(Expr, Msg)                                               \
  do {                                                                         \
    if (UNLIKELY(!(Expr))) {                                                   \
      outputRaw(Msg);                                                          \
      die();                                                                   \
    }                                                                          \
  } while (false)

#define RAW_CHECK(Expr) RAW_CHECK_MSG(Expr, #Expr)

void NORETURN reportCheckFailed(const char *File, int Line,
                                const char *Condition, u64 Value1, u64 Value2);
#define CHECK_IMPL(C1, Op, C2)                                                 \
  do {                                                                         \
    if (UNLIKELY(!(C1 Op C2))) {                                               \
      scudo::reportCheckFailed(__FILE__, __LINE__, #C1 " " #Op " " #C2,        \
                               (scudo::u64)C1, (scudo::u64)C2);                \
      scudo::die();                                                            \
    }                                                                          \
  } while (false)

#define CHECK(A) CHECK_IMPL((A), !=, 0)
#define CHECK_EQ(A, B) CHECK_IMPL((A), ==, (B))
#define CHECK_NE(A, B) CHECK_IMPL((A), !=, (B))
#define CHECK_LT(A, B) CHECK_IMPL((A), <, (B))
#define CHECK_LE(A, B) CHECK_IMPL((A), <=, (B))
#define CHECK_GT(A, B) CHECK_IMPL((A), >, (B))
#define CHECK_GE(A, B) CHECK_IMPL((A), >=, (B))

#if SCUDO_DEBUG
#define DCHECK(A) CHECK(A)
#define DCHECK_EQ(A, B) CHECK_EQ(A, B)
#define DCHECK_NE(A, B) CHECK_NE(A, B)
#define DCHECK_LT(A, B) CHECK_LT(A, B)
#define DCHECK_LE(A, B) CHECK_LE(A, B)
#define DCHECK_GT(A, B) CHECK_GT(A, B)
#define DCHECK_GE(A, B) CHECK_GE(A, B)
#else
#define DCHECK(A)                                                              \
  do {                                                                         \
  } while (false && (A))
#define DCHECK_EQ(A, B)                                                        \
  do {                                                                         \
  } while (false && (A) == (B))
#define DCHECK_NE(A, B)                                                        \
  do {                                                                         \
  } while (false && (A) != (B))
#define DCHECK_LT(A, B)                                                        \
  do {                                                                         \
  } while (false && (A) < (B))
#define DCHECK_LE(A, B)                                                        \
  do {                                                                         \
  } while (false && (A) <= (B))
#define DCHECK_GT(A, B)                                                        \
  do {                                                                         \
  } while (false && (A) > (B))
#define DCHECK_GE(A, B)                                                        \
  do {                                                                         \
  } while (false && (A) >= (B))
#endif

// The superfluous die() call effectively makes this macro NORETURN.
#define UNREACHABLE(Msg)                                                       \
  do {                                                                         \
    CHECK(0 && Msg);                                                           \
    die();                                                                     \
  } while (0)

} // namespace scudo

#endif // SCUDO_INTERNAL_DEFS_H_
