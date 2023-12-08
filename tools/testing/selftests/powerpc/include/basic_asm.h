/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SELFTESTS_POWERPC_BASIC_ASM_H
#define _SELFTESTS_POWERPC_BASIC_ASM_H

#include <ppc-asm.h>
#include <asm/unistd.h>

#ifdef __powerpc64__
#define PPC_LL		ld
#define PPC_STL		std
#define PPC_STLU	stdu
#else
#define PPC_LL		lwz
#define PPC_STL		stw
#define PPC_STLU	stwu
#endif

#define LOAD_REG_IMMEDIATE(reg, expr) \
	lis	reg, (expr)@highest;	\
	ori	reg, reg, (expr)@higher;	\
	rldicr	reg, reg, 32, 31;	\
	oris	reg, reg, (expr)@high;	\
	ori	reg, reg, (expr)@l;

/*
 * Note: These macros assume that variables being stored on the stack are
 * sizeof(long), while this is usually the case it may not always be the
 * case for each use case.
 */
#ifdef  __powerpc64__

// ABIv2
#if defined(_CALL_ELF) && _CALL_ELF == 2
#define STACK_FRAME_MIN_SIZE 32
#define STACK_FRAME_TOC_POS  24
#define __STACK_FRAME_PARAM(_param)  (32 + ((_param)*8))
#define __STACK_FRAME_LOCAL(_num_params, _var_num)  \
	((STACK_FRAME_PARAM(_num_params)) + ((_var_num)*8))

#else // ABIv1 below
#define STACK_FRAME_MIN_SIZE 112
#define STACK_FRAME_TOC_POS  40
#define __STACK_FRAME_PARAM(i)  (48 + ((i)*8))

/*
 * Caveat: if a function passed more than 8 doublewords, the caller will have
 * made more space... which would render the 112 incorrect.
 */
#define __STACK_FRAME_LOCAL(_num_params, _var_num)  \
	(112 + ((_var_num)*8))


#endif // ABIv2

// Common 64-bit
#define STACK_FRAME_LR_POS   16
#define STACK_FRAME_CR_POS   8

#else // 32-bit below

#define STACK_FRAME_MIN_SIZE 16
#define STACK_FRAME_LR_POS   4

#define __STACK_FRAME_PARAM(_param)  (STACK_FRAME_MIN_SIZE + ((_param)*4))
#define __STACK_FRAME_LOCAL(_num_params, _var_num)  \
	((STACK_FRAME_PARAM(_num_params)) + ((_var_num)*4))

#endif // __powerpc64__

/* Parameter x saved to the stack */
#define STACK_FRAME_PARAM(var)    __STACK_FRAME_PARAM(var)

/* Local variable x saved to the stack after x parameters */
#define STACK_FRAME_LOCAL(num_params, var)    \
	__STACK_FRAME_LOCAL(num_params, var)

/*
 * It is very important to note here that _extra is the extra amount of
 * stack space needed. This space can be accessed using STACK_FRAME_PARAM()
 * or STACK_FRAME_LOCAL() macros.
 *
 * r1 and r2 are not defined in ppc-asm.h (instead they are defined as sp
 * and toc). Kernel programmers tend to prefer rX even for r1 and r2, hence
 * %1 and %r2. r0 is defined in ppc-asm.h and therefore %r0 gets
 * preprocessed incorrectly, hence r0.
 */
#define PUSH_BASIC_STACK(_extra) \
	mflr	 r0; \
	PPC_STL	 r0, STACK_FRAME_LR_POS(%r1); \
	PPC_STLU %r1, -(((_extra + 15) & ~15) + STACK_FRAME_MIN_SIZE)(%r1);

#define POP_BASIC_STACK(_extra) \
	addi	%r1, %r1, (((_extra + 15) & ~15) + STACK_FRAME_MIN_SIZE); \
	PPC_LL	r0, STACK_FRAME_LR_POS(%r1); \
	mtlr	r0;

.macro OP_REGS op, reg_width, start_reg, end_reg, base_reg, base_reg_offset=0, skip=0
	.set i, \start_reg
	.rept (\end_reg - \start_reg + 1)
	\op	i, (\reg_width * (i - \skip) + \base_reg_offset)(\base_reg)
	.set i, i + 1
	.endr
.endm

#endif /* _SELFTESTS_POWERPC_BASIC_ASM_H */
