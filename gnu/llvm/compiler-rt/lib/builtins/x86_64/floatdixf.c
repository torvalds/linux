// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// xf_float __floatdixf(di_int a);

#ifdef __x86_64__

#include "../int_lib.h"

xf_float __floatdixf(int64_t a) { return (xf_float)a; }

#endif // __i386__
