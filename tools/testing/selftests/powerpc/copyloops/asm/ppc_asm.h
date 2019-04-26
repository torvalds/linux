/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SELFTESTS_POWERPC_PPC_ASM_H
#define __SELFTESTS_POWERPC_PPC_ASM_H
#include <ppc-asm.h>

#define CONFIG_ALTIVEC

#define r1	1

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

#define _GLOBAL(A) FUNC_START(test_ ## A)
#define _GLOBAL_TOC(A) _GLOBAL(A)
#define _GLOBAL_TOC_KASAN(A) _GLOBAL(A)

#define PPC_MTOCRF(A, B)	mtocrf A, B

#define EX_TABLE(x, y)			\
	.section __ex_table,"a";	\
	.8byte	x, y;			\
	.previous

#define BEGIN_FTR_SECTION		.if test_feature
#define FTR_SECTION_ELSE		.else
#define ALT_FTR_SECTION_END_IFCLR(x)	.endif
#define ALT_FTR_SECTION_END_IFSET(x)	.endif
#define ALT_FTR_SECTION_END(x, y)	.endif
#define END_FTR_SECTION_IFCLR(x)	.endif
#define END_FTR_SECTION_IFSET(x)	.endif

/* Default to taking the first of any alternative feature sections */
test_feature = 1

#endif /* __SELFTESTS_POWERPC_PPC_ASM_H */
