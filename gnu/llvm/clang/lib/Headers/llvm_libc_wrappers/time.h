//===-- Wrapper for C standard time.h declarations on the GPU -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __CLANG_LLVM_LIBC_WRAPPERS_TIME_H__
#define __CLANG_LLVM_LIBC_WRAPPERS_TIME_H__

#if !defined(_OPENMP) && !defined(__HIP__) && !defined(__CUDA__)
#error "This file is for GPU offloading compilation only"
#endif

#include_next <time.h>

#if __has_include(<llvm-libc-decls/time.h>)

#if defined(__HIP__) || defined(__CUDA__)
#define __LIBC_ATTRS __attribute__((device))
#endif

#pragma omp begin declare target

_Static_assert(sizeof(clock_t) == sizeof(long), "ABI mismatch!");

#include <llvm-libc-decls/time.h>

#pragma omp end declare target

#endif

#endif // __CLANG_LLVM_LIBC_WRAPPERS_TIME_H__
