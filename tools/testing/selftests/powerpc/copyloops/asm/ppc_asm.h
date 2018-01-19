/* SPDX-License-Identifier: GPL-2.0 */
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

#define PPC_MTOCRF(A, B)	mtocrf A, B

#define EX_TABLE(x, y)

FUNC_START(enter_vmx_usercopy)
	li	r3,1
	blr

FUNC_START(exit_vmx_usercopy)
	li	r3,0
	blr

FUNC_START(enter_vmx_copy)
	li	r3,1
	blr

FUNC_START(exit_vmx_copy)
	blr

FUNC_START(memcpy_power7)
	blr

FUNC_START(__copy_tofrom_user_power7)
	blr

FUNC_START(__copy_tofrom_user_base)
	blr

#define BEGIN_FTR_SECTION
#define FTR_SECTION_ELSE
#define ALT_FTR_SECTION_END_IFCLR(x)
#define ALT_FTR_SECTION_END(x, y)
#define END_FTR_SECTION_IFCLR(x)
