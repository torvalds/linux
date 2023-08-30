/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PPC_ASM_H
#define _PPC_ASM_H
#include <ppc-asm.h>

#ifndef r1
#define r1 sp
#endif

#define _GLOBAL(A) FUNC_START(test_ ## A)
#define _GLOBAL_TOC(A) FUNC_START(test_ ## A)
#define CFUNC(name) name

#define CONFIG_ALTIVEC

#define R14 r14
#define R15 r15
#define R16 r16
#define R17 r17
#define R18 r18
#define R19 r19
#define R20 r20
#define R21 r21
#define R22 r22
#define R29 r29
#define R30 r30
#define R31 r31

#define STACKFRAMESIZE	256
#define STK_REG(i)	(112 + ((i)-14)*8)

#define BEGIN_FTR_SECTION
#define END_FTR_SECTION_IFSET(val)
#endif
