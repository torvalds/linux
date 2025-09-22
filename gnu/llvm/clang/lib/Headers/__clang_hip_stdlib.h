/*===---- __clang_hip_stdlib.h - Device-side HIP math support --------------===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */
#ifndef __CLANG_HIP_STDLIB_H__

#if !defined(__HIP__) && !defined(__OPENMP_AMDGCN__)
#error "This file is for HIP and OpenMP AMDGCN device compilation only."
#endif

#if !defined(__cplusplus)

#include <limits.h>

#ifdef __OPENMP_AMDGCN__
#define __DEVICE__ static inline __attribute__((always_inline, nothrow))
#else
#define __DEVICE__ static __device__ inline __attribute__((always_inline))
#endif

__DEVICE__
int abs(int __x) {
  int __sgn = __x >> (sizeof(int) * CHAR_BIT - 1);
  return (__x ^ __sgn) - __sgn;
}
__DEVICE__
long labs(long __x) {
  long __sgn = __x >> (sizeof(long) * CHAR_BIT - 1);
  return (__x ^ __sgn) - __sgn;
}
__DEVICE__
long long llabs(long long __x) {
  long long __sgn = __x >> (sizeof(long long) * CHAR_BIT - 1);
  return (__x ^ __sgn) - __sgn;
}

#endif // !defined(__cplusplus)

#endif // #define __CLANG_HIP_STDLIB_H__
