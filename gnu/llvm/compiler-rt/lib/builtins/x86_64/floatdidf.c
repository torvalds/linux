// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// double __floatdidf(di_int a);

#if defined(__x86_64__) || defined(_M_X64)

#include "../int_lib.h"

double __floatdidf(int64_t a) { return (double)a; }

#endif // __x86_64__
