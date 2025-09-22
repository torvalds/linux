/*===---- openmp_wrapper/stdlib.h ------ OpenMP math.h intercept ----- c++ -===
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===-----------------------------------------------------------------------===
 */

#ifndef __CLANG_OPENMP_STDLIB_H__
#define __CLANG_OPENMP_STDLIB_H__

#ifndef _OPENMP
#error "This file is for OpenMP compilation only."
#endif

#include_next <stdlib.h>

#ifdef __AMDGCN__
#pragma omp begin declare variant match(device = {arch(amdgcn)})

#define __OPENMP_AMDGCN__
#include <__clang_hip_stdlib.h>
#undef __OPENMP_AMDGCN__

#pragma omp end declare variant
#endif

#endif // __CLANG_OPENMP_STDLIB_H__
