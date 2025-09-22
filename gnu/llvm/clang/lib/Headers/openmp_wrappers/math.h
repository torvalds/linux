/*===---- openmp_wrapper/math.h -------- OpenMP math.h intercept ------ c++ -===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

// If we are in C++ mode and include <math.h> (not <cmath>) first, we still need
// to make sure <cmath> is read first. The problem otherwise is that we haven't
// seen the declarations of the math.h functions when the system math.h includes
// our cmath overlay. However, our cmath overlay, or better the underlying
// overlay, e.g. CUDA, uses the math.h functions. Since we haven't declared them
// yet we get errors. CUDA avoids this by eagerly declaring all math functions
// (in the __device__ space) but we cannot do this. Instead we break the
// dependence by forcing cmath to go first. While our cmath will in turn include
// this file, the cmath guards will prevent recursion.
#ifdef __cplusplus
#include <cmath>
#endif

#ifndef __CLANG_OPENMP_MATH_H__
#define __CLANG_OPENMP_MATH_H__

#ifndef _OPENMP
#error "This file is for OpenMP compilation only."
#endif

#include_next <math.h>

// We need limits.h for __clang_cuda_math.h below and because it should not hurt
// we include it eagerly here.
#include <limits.h>

// We need stdlib.h because (for now) __clang_cuda_math.h below declares `abs`
// which should live in stdlib.h.
#include <stdlib.h>

#pragma omp begin declare variant match(                                       \
    device = {arch(nvptx, nvptx64)}, implementation = {extension(match_any)})

#define __CUDA__
#define __OPENMP_NVPTX__
#include <__clang_cuda_math.h>
#undef __OPENMP_NVPTX__
#undef __CUDA__

#pragma omp end declare variant

#ifdef __AMDGCN__
#pragma omp begin declare variant match(device = {arch(amdgcn)})

#define __OPENMP_AMDGCN__
#include <__clang_hip_math.h>
#undef __OPENMP_AMDGCN__

#pragma omp end declare variant
#endif

#endif
