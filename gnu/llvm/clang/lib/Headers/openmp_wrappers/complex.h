/*===-- complex --- OpenMP complex wrapper for target regions --------- c++ -===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __CLANG_OPENMP_COMPLEX_H__
#define __CLANG_OPENMP_COMPLEX_H__

#ifndef _OPENMP
#error "This file is for OpenMP compilation only."
#endif

// We require math functions in the complex builtins below.
#include <math.h>

#ifdef __NVPTX__
#define __OPENMP_NVPTX__
#include <__clang_cuda_complex_builtins.h>
#undef __OPENMP_NVPTX__
#endif

#ifdef __AMDGCN__
#define __OPENMP_AMDGCN__
#include <__clang_cuda_complex_builtins.h>
#undef __OPENMP_AMDGCN__
#endif

#endif

// Grab the host header too.
#include_next <complex.h>
