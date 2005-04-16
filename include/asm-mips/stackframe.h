/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 99, 2001 Ralf Baechle
 * Copyright (C) 1994, 1995, 1996 Paul M. Antoine.
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_STACKFRAME_H
#define _ASM_STACKFRAME_H

#include <linux/config.h>
#include <linux/threads.h>

#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/offset.h>

		.macro	SAVE_AT
		.set	push
		.set	noat
		LONG_S	$1, PT_R1(sp)
		.set	pop
		.endm

		.macro	SAVE_TEMP
		mfhi	v1
#ifdef CONFIG_MIPS32
		LONG_S	$8, PT_R8(sp)
		LONG_S	$9, PT_R9(sp)
#endif
		LONG_S	v1, PT_HI(sp)
		mflo	v1
		LONG_S	$10, PT_R10(sp)
		LONG_S	$11, PT_R11(sp)
		LONG_S	v1,  PT_LO(sp)
		LONG_S	$12, PT_R12(sp)
		LONG_S	$13, PT_R13(sp)
		LONG_S	$14, PT_R14(sp)
		LONG_S	$15, PT_R15(sp)
		LONG_S	$24, PT_R24(sp)
		.endm

		.macro	SAVE_STATIC
		LONG_S	$16, PT_R16(sp)
		LONG_S	$17, PT_R17(sp)
		LONG_S	$18, PT_R18(sp)
		LONG_S	$19, PT_R19(sp)
		LONG_S	$20, PT_R20(sp)
		LONG_S	$21, PT_R21(sp)
		LONG_S	$22, PT_R22(sp)
		LONG_S	$23, PT_R23(sp)
		LONG_S	$30, PT_R30(sp)
		.endm

#ifdef CONFIG_SMP
		.macro	get_saved_sp	/* SMP variation */
#ifdef CONFIG_MIPS32
		mfc0	k0, CP0_CONTEXT
		lui	k1, %hi(kernelsp)
		srl	k0, k0, 23
		sll	k0, k0, 2
		addu	k1, k0
		LONG_L	k1, %lo(kernelsp)(k1)
#endif
#if defined(CONFIG_MIPS64) && !defined(CONFIG_BUILD_ELF64)
		MFC0	k1, CP0_CONTEXT
		dsra	k1, 23
		lui	k0, %hi(pgd_current)
		addiu	k0, %lo(pgd_current)
		dsubu	k1, k0
		lui	k0, %hi(kernelsp)
		daddu	k1, k0
		LONG_L	k1, %lo(kernelsp)(k1)
#endif
#if defined(CONFIG_MIPS64) && defined(CONFIG_BUILD_ELF64)
		MFC0	k1, CP0_CONTEXT
		dsrl	k1, 23
		dsll	k1, k1, 3
		LONG_L	k1, kernelsp(k1)
#endif
		.endm

		.macro	set_saved_sp stackp temp temp2
#ifdef CONFIG_MIPS32
		mfc0	\temp, CP0_CONTEXT
		srl	\temp, 23
		sll	\temp, 2
		LONG_S	\stackp, kernelsp(\temp)
#endif
#if defined(CONFIG_MIPS64) && !defined(CONFIG_BUILD_ELF64)
		lw	\temp, TI_CPU(gp)
		dsll	\temp, 3
		lui	\temp2, %hi(kernelsp)
		daddu	\temp, \temp2
		LONG_S	\stackp, %lo(kernelsp)(\temp)
#endif
#if defined(CONFIG_MIPS64) && defined(CONFIG_BUILD_ELF64)
		lw	\temp, TI_CPU(gp)
		dsll	\temp, 3
		LONG_S	\stackp, kernelsp(\temp)
#endif
		.endm
#else
		.macro	get_saved_sp	/* Uniprocessor variation */
		lui	k1, %hi(kernelsp)
		LONG_L	k1, %lo(kernelsp)(k1)
		.endm

		.macro	set_saved_sp stackp temp temp2
		LONG_S	\stackp, kernelsp
		.endm
#endif

		.macro	SAVE_SOME
		.set	push
		.set	noat
		.set	reorder
		mfc0	k0, CP0_STATUS
		sll	k0, 3		/* extract cu0 bit */
		.set	noreorder
		bltz	k0, 8f
		 move	k1, sp
		.set	reorder
		/* Called from user mode, new stack. */
		get_saved_sp
8:		move	k0, sp
		PTR_SUBU sp, k1, PT_SIZE
		LONG_S	k0, PT_R29(sp)
		LONG_S	$3, PT_R3(sp)
		LONG_S	$0, PT_R0(sp)
		mfc0	v1, CP0_STATUS
		LONG_S	$2, PT_R2(sp)
		LONG_S	v1, PT_STATUS(sp)
		LONG_S	$4, PT_R4(sp)
		mfc0	v1, CP0_CAUSE
		LONG_S	$5, PT_R5(sp)
		LONG_S	v1, PT_CAUSE(sp)
		LONG_S	$6, PT_R6(sp)
		MFC0	v1, CP0_EPC
		LONG_S	$7, PT_R7(sp)
#ifdef CONFIG_MIPS64
		LONG_S	$8, PT_R8(sp)
		LONG_S	$9, PT_R9(sp)
#endif
		LONG_S	v1, PT_EPC(sp)
		LONG_S	$25, PT_R25(sp)
		LONG_S	$28, PT_R28(sp)
		LONG_S	$31, PT_R31(sp)
		ori	$28, sp, _THREAD_MASK
		xori	$28, _THREAD_MASK
		.set	pop
		.endm

		.macro	SAVE_ALL
		SAVE_SOME
		SAVE_AT
		SAVE_TEMP
		SAVE_STATIC
		.endm

		.macro	RESTORE_AT
		.set	push
		.set	noat
		LONG_L	$1,  PT_R1(sp)
		.set	pop
		.endm

		.macro	RESTORE_TEMP
		LONG_L	$24, PT_LO(sp)
#ifdef CONFIG_MIPS32
		LONG_L	$8, PT_R8(sp)
		LONG_L	$9, PT_R9(sp)
#endif
		mtlo	$24
		LONG_L	$24, PT_HI(sp)
		LONG_L	$10, PT_R10(sp)
		LONG_L	$11, PT_R11(sp)
		mthi	$24
		LONG_L	$12, PT_R12(sp)
		LONG_L	$13, PT_R13(sp)
		LONG_L	$14, PT_R14(sp)
		LONG_L	$15, PT_R15(sp)
		LONG_L	$24, PT_R24(sp)
		.endm

		.macro	RESTORE_STATIC
		LONG_L	$16, PT_R16(sp)
		LONG_L	$17, PT_R17(sp)
		LONG_L	$18, PT_R18(sp)
		LONG_L	$19, PT_R19(sp)
		LONG_L	$20, PT_R20(sp)
		LONG_L	$21, PT_R21(sp)
		LONG_L	$22, PT_R22(sp)
		LONG_L	$23, PT_R23(sp)
		LONG_L	$30, PT_R30(sp)
		.endm

#if defined(CONFIG_CPU_R3000) || defined(CONFIG_CPU_TX39XX)

		.macro	RESTORE_SOME
		.set	push
		.set	reorder
		.set	noat
		mfc0	a0, CP0_STATUS
		ori	a0, 0x1f
		xori	a0, 0x1f
		mtc0	a0, CP0_STATUS
		li	v1, 0xff00
		and	a0, v1
		LONG_L	v0, PT_STATUS(sp)
		nor	v1, $0, v1
		and	v0, v1
		or	v0, a0
		mtc0	v0, CP0_STATUS
		LONG_L	$31, PT_R31(sp)
		LONG_L	$28, PT_R28(sp)
		LONG_L	$25, PT_R25(sp)
#ifdef CONFIG_MIPS64
		LONG_L	$8, PT_R8(sp)
		LONG_L	$9, PT_R9(sp)
#endif
		LONG_L	$7,  PT_R7(sp)
		LONG_L	$6,  PT_R6(sp)
		LONG_L	$5,  PT_R5(sp)
		LONG_L	$4,  PT_R4(sp)
		LONG_L	$3,  PT_R3(sp)
		LONG_L	$2,  PT_R2(sp)
		.set	pop
		.endm

		.macro	RESTORE_SP_AND_RET
		.set	push
		.set	noreorder
		LONG_L	k0, PT_EPC(sp)
		LONG_L	sp, PT_R29(sp)
		jr	k0
		 rfe
		.set	pop
		.endm

#else

		.macro	RESTORE_SOME
		.set	push
		.set	reorder
		.set	noat
		mfc0	a0, CP0_STATUS
		ori	a0, 0x1f
		xori	a0, 0x1f
		mtc0	a0, CP0_STATUS
		li	v1, 0xff00
		and	a0, v1
		LONG_L	v0, PT_STATUS(sp)
		nor	v1, $0, v1
		and	v0, v1
		or	v0, a0
		mtc0	v0, CP0_STATUS
		LONG_L	v1, PT_EPC(sp)
		MTC0	v1, CP0_EPC
		LONG_L	$31, PT_R31(sp)
		LONG_L	$28, PT_R28(sp)
		LONG_L	$25, PT_R25(sp)
#ifdef CONFIG_MIPS64
		LONG_L	$8, PT_R8(sp)
		LONG_L	$9, PT_R9(sp)
#endif
		LONG_L	$7,  PT_R7(sp)
		LONG_L	$6,  PT_R6(sp)
		LONG_L	$5,  PT_R5(sp)
		LONG_L	$4,  PT_R4(sp)
		LONG_L	$3,  PT_R3(sp)
		LONG_L	$2,  PT_R2(sp)
		.set	pop
		.endm

		.macro	RESTORE_SP_AND_RET
		LONG_L	sp, PT_R29(sp)
		.set	mips3
		eret
		.set	mips0
		.endm

#endif

		.macro	RESTORE_SP
		LONG_L	sp, PT_R29(sp)
		.endm

		.macro	RESTORE_ALL
		RESTORE_TEMP
		RESTORE_STATIC
		RESTORE_AT
		RESTORE_SOME
		RESTORE_SP
		.endm

		.macro	RESTORE_ALL_AND_RET
		RESTORE_TEMP
		RESTORE_STATIC
		RESTORE_AT
		RESTORE_SOME
		RESTORE_SP_AND_RET
		.endm

/*
 * Move to kernel mode and disable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	CLI
		mfc0	t0, CP0_STATUS
		li	t1, ST0_CU0 | 0x1f
		or	t0, t1
		xori	t0, 0x1f
		mtc0	t0, CP0_STATUS
		irq_disable_hazard
		.endm

/*
 * Move to kernel mode and enable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	STI
		mfc0	t0, CP0_STATUS
		li	t1, ST0_CU0 | 0x1f
		or	t0, t1
		xori	t0, 0x1e
		mtc0	t0, CP0_STATUS
		irq_enable_hazard
		.endm

/*
 * Just move to kernel mode and leave interrupts as they are.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	KMODE
		mfc0	t0, CP0_STATUS
		li	t1, ST0_CU0 | 0x1e
		or	t0, t1
		xori	t0, 0x1e
		mtc0	t0, CP0_STATUS
		irq_disable_hazard
		.endm

#endif /* _ASM_STACKFRAME_H */
