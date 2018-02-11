/* SPDX-License-Identifier: GPL-2.0 */
#include <ppc-asm.h>

#ifndef r1
#define r1 sp
#endif

#define _GLOBAL(A) FUNC_START(test_ ## A)
