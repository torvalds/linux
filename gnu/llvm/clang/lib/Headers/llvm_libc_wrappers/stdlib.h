//===-- Wrapper for C standard stdlib.h declarations on the GPU -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef __CLANG_LLVM_LIBC_WRAPPERS_STDLIB_H__
#define __CLANG_LLVM_LIBC_WRAPPERS_STDLIB_H__

#if !defined(_OPENMP) && !defined(__HIP__) && !defined(__CUDA__)
#error "This file is for GPU offloading compilation only"
#endif

#include_next <stdlib.h>

#if __has_include(<llvm-libc-decls/stdlib.h>)

#if defined(__HIP__) || defined(__CUDA__)
#define __LIBC_ATTRS __attribute__((device))
#endif

#pragma omp begin declare target

// The LLVM C library uses these named types so we forward declare them.
typedef void (*__atexithandler_t)(void);
typedef int (*__bsearchcompare_t)(const void *, const void *);
typedef int (*__qsortcompare_t)(const void *, const void *);
typedef int (*__qsortrcompare_t)(const void *, const void *, void *);

// Enforce ABI compatibility with the structs used by the LLVM C library.
_Static_assert(__builtin_offsetof(div_t, quot) == 0, "ABI mismatch!");
_Static_assert(__builtin_offsetof(ldiv_t, quot) == 0, "ABI mismatch!");
_Static_assert(__builtin_offsetof(lldiv_t, quot) == 0, "ABI mismatch!");

#include <llvm-libc-decls/stdlib.h>

#pragma omp end declare target

#undef __LIBC_ATTRS

#endif

#endif // __CLANG_LLVM_LIBC_WRAPPERS_STDLIB_H__
