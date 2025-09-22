//===-- platform.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_PLATFORM_H_
#define SCUDO_PLATFORM_H_

// Transitive includes of stdint.h specify some of the defines checked below.
#include <stdint.h>

#if defined(__linux__) && !defined(__TRUSTY__)
#define SCUDO_LINUX 1
#else
#define SCUDO_LINUX 0
#endif

// See https://android.googlesource.com/platform/bionic/+/master/docs/defines.md
#if defined(__BIONIC__)
#define SCUDO_ANDROID 1
#else
#define SCUDO_ANDROID 0
#endif

#if defined(__Fuchsia__)
#define SCUDO_FUCHSIA 1
#else
#define SCUDO_FUCHSIA 0
#endif

#if defined(__TRUSTY__)
#define SCUDO_TRUSTY 1
#else
#define SCUDO_TRUSTY 0
#endif

#if defined(__riscv) && (__riscv_xlen == 64)
#define SCUDO_RISCV64 1
#else
#define SCUDO_RISCV64 0
#endif

#if defined(__LP64__)
#define SCUDO_WORDSIZE 64U
#else
#define SCUDO_WORDSIZE 32U
#endif

#if SCUDO_WORDSIZE == 64U
#define FIRST_32_SECOND_64(a, b) (b)
#else
#define FIRST_32_SECOND_64(a, b) (a)
#endif

#ifndef SCUDO_CAN_USE_PRIMARY64
#define SCUDO_CAN_USE_PRIMARY64 (SCUDO_WORDSIZE == 64U)
#endif

#ifndef SCUDO_CAN_USE_MTE
#define SCUDO_CAN_USE_MTE (SCUDO_LINUX || SCUDO_TRUSTY)
#endif

#ifndef SCUDO_ENABLE_HOOKS
#define SCUDO_ENABLE_HOOKS 0
#endif

#ifndef SCUDO_MIN_ALIGNMENT_LOG
// We force malloc-type functions to be aligned to std::max_align_t, but there
// is no reason why the minimum alignment for all other functions can't be 8
// bytes. Except obviously for applications making incorrect assumptions.
// TODO(kostyak): define SCUDO_MIN_ALIGNMENT_LOG 3
#define SCUDO_MIN_ALIGNMENT_LOG FIRST_32_SECOND_64(3, 4)
#endif

#if defined(__aarch64__)
#define SCUDO_MMAP_RANGE_SIZE FIRST_32_SECOND_64(1ULL << 32, 1ULL << 48)
#else
#define SCUDO_MMAP_RANGE_SIZE FIRST_32_SECOND_64(1ULL << 32, 1ULL << 47)
#endif

// Older gcc have issues aligning to a constexpr, and require an integer.
// See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=56859 among others.
#if defined(__powerpc__) || defined(__powerpc64__)
#define SCUDO_CACHE_LINE_SIZE 128
#else
#define SCUDO_CACHE_LINE_SIZE 64
#endif

#define SCUDO_POINTER_FORMAT_LENGTH FIRST_32_SECOND_64(8, 12)

#endif // SCUDO_PLATFORM_H_
