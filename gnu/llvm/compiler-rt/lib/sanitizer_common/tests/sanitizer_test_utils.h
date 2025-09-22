//===-- sanitizer_test_utils.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of *Sanitizer runtime.
// Common unit tests utilities.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_TEST_UTILS_H
#define SANITIZER_TEST_UTILS_H

#if defined(_WIN32)
// <windows.h> should always be the first include on Windows.
# include <windows.h>
// MSVS headers define max/min as macros, so std::max/min gets crazy.
# undef max
# undef min
#endif

#if !defined(SANITIZER_EXTERNAL_TEST_CONFIG)
# define INCLUDED_FROM_SANITIZER_TEST_UTILS_H
# include "sanitizer_test_config.h"
# undef INCLUDED_FROM_SANITIZER_TEST_UTILS_H
#endif

#include <stdint.h>

#if defined(_MSC_VER)
# define NOINLINE __declspec(noinline)
#else  // defined(_MSC_VER)
# define NOINLINE __attribute__((noinline))
#endif  // defined(_MSC_VER)

#if !defined(_MSC_VER) || defined(__clang__)
# define UNUSED __attribute__((unused))
# define USED __attribute__((used))
#else
# define UNUSED
# define USED
#endif

#if !defined(__has_feature)
#define __has_feature(x) 0
#endif

#ifndef ATTRIBUTE_NO_SANITIZE_ADDRESS
# if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
#  define ATTRIBUTE_NO_SANITIZE_ADDRESS \
    __attribute__((no_sanitize_address))
# else
#  define ATTRIBUTE_NO_SANITIZE_ADDRESS
# endif
#endif  // ATTRIBUTE_NO_SANITIZE_ADDRESS

#if __LP64__ || defined(_WIN64)
#  define SANITIZER_WORDSIZE 64
#else
#  define SANITIZER_WORDSIZE 32
#endif

// Make the compiler thinks that something is going on there.
inline void break_optimization(void *arg) {
#if !defined(_WIN32) || defined(__clang__)
  __asm__ __volatile__("" : : "r" (arg) : "memory");
#endif
}

// This function returns its parameter but in such a way that compiler
// can not prove it.
template<class T>
NOINLINE
static T Ident(T t) {
  T ret = t;
  break_optimization(&ret);
  return ret;
}

// Simple stand-alone pseudorandom number generator.
// Current algorithm is ANSI C linear congruential PRNG.
static inline uint32_t my_rand_r(uint32_t* state) {
  return (*state = *state * 1103515245 + 12345) >> 16;
}

static uint32_t global_seed = 0;

static inline uint32_t my_rand() {
  return my_rand_r(&global_seed);
}

// Set availability of platform-specific functions.

#if !defined(__APPLE__) && !defined(__ANDROID__) && !defined(_WIN32)
# define SANITIZER_TEST_HAS_POSIX_MEMALIGN 1
#else
# define SANITIZER_TEST_HAS_POSIX_MEMALIGN 0
#endif

#if !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__ANDROID__) && \
    !defined(__NetBSD__) && !defined(_WIN32)
# define SANITIZER_TEST_HAS_MEMALIGN 1
#else
# define SANITIZER_TEST_HAS_MEMALIGN 0
#endif

#if defined(__GLIBC__)
# define SANITIZER_TEST_HAS_PVALLOC 1
# define SANITIZER_TEST_HAS_MALLOC_USABLE_SIZE 1
#else
# define SANITIZER_TEST_HAS_PVALLOC 0
# define SANITIZER_TEST_HAS_MALLOC_USABLE_SIZE 0
#endif

#if !defined(__APPLE__)
# define SANITIZER_TEST_HAS_STRNLEN 1
#else
# define SANITIZER_TEST_HAS_STRNLEN 0
#endif

#if defined(__FreeBSD__) || defined(__NetBSD__)
# define SANITIZER_TEST_HAS_PRINTF_L 1
#else
# define SANITIZER_TEST_HAS_PRINTF_L 0
#endif

#if !defined(_WIN32)
#  define SANITIZER_TEST_HAS_STRNDUP 1
#else
# define SANITIZER_TEST_HAS_STRNDUP 0
#endif

#endif  // SANITIZER_TEST_UTILS_H
