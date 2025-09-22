//===-- lib/arm/aeabi_drsub.c - Double-precision subtraction --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#define DOUBLE_PRECISION
#include "../fp_lib.h"

AEABI_RTABI fp_t __aeabi_dsub(fp_t, fp_t);

AEABI_RTABI fp_t __aeabi_drsub(fp_t a, fp_t b) { return __aeabi_dsub(b, a); }
