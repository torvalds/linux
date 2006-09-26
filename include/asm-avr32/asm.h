/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_ASM_H__
#define __ASM_AVR32_ASM_H__

#include <asm/sysreg.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

#define mask_interrupts		ssrf	SR_GM_BIT
#define mask_exceptions		ssrf	SR_EM_BIT
#define unmask_interrupts	csrf	SR_GM_BIT
#define unmask_exceptions	csrf	SR_EM_BIT

#ifdef CONFIG_FRAME_POINTER
	.macro	save_fp
	st.w	--sp, r7
	.endm
	.macro	restore_fp
	ld.w	r7, sp++
	.endm
	.macro	zero_fp
	mov	r7, 0
	.endm
#else
	.macro	save_fp
	.endm
	.macro	restore_fp
	.endm
	.macro	zero_fp
	.endm
#endif
	.macro	get_thread_info reg
	mov	\reg, sp
	andl	\reg, ~(THREAD_SIZE - 1) & 0xffff
	.endm

	/* Save and restore registers */
	.macro	save_min sr, tmp=lr
	pushm	lr
	mfsr	\tmp, \sr
	zero_fp
	st.w	--sp, \tmp
	.endm

	.macro	restore_min sr, tmp=lr
	ld.w	\tmp, sp++
	mtsr	\sr, \tmp
	popm	lr
	.endm

	.macro	save_half sr, tmp=lr
	save_fp
	pushm	r8-r9,r10,r11,r12,lr
	zero_fp
	mfsr	\tmp, \sr
	st.w	--sp, \tmp
	.endm

	.macro	restore_half sr, tmp=lr
	ld.w	\tmp, sp++
	mtsr	\sr, \tmp
	popm	r8-r9,r10,r11,r12,lr
	restore_fp
	.endm

	.macro	save_full_user sr, tmp=lr
	stmts	--sp, r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,sp,lr
	st.w	--sp, lr
	zero_fp
	mfsr	\tmp, \sr
	st.w	--sp, \tmp
	.endm

	.macro	restore_full_user sr, tmp=lr
	ld.w	\tmp, sp++
	mtsr	\sr, \tmp
	ld.w	lr, sp++
	ldmts	sp++, r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,sp,lr
	.endm

	/* uaccess macros */
	.macro branch_if_kernel scratch, label
	get_thread_info \scratch
	ld.w	\scratch, \scratch[TI_flags]
	bld	\scratch, TIF_USERSPACE
	brcc	\label
	.endm

	.macro ret_if_privileged scratch, addr, size, ret
	sub	\scratch, \size, 1
	add	\scratch, \addr
	retcs	\ret
	retmi	\ret
	.endm

#endif /* __ASM_AVR32_ASM_H__ */
