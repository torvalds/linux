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
#include <asm/asmmacro.h>
#include <asm/mipsregs.h>
#include <asm/asm-offsets.h>

#ifdef CONFIG_MIPS_MT_SMTC
#include <asm/mipsmtregs.h>
#endif /* CONFIG_MIPS_MT_SMTC */

		.macro	SAVE_AT
		.set	push
		.set	noat
		LONG_S	$1, PT_R1(sp)
		.set	pop
		.endm

		.macro	SAVE_TEMP
		mfhi	v1
#ifdef CONFIG_32BIT
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
#ifdef CONFIG_32BIT
#ifdef CONFIG_MIPS_MT_SMTC
		.set	mips32
		mfc0	k0, CP0_TCBIND;
		.set	mips0
		lui	k1, %hi(kernelsp)
		srl	k0, k0, 19
		/* No need to shift down and up to clear bits 0-1 */
#else
		mfc0	k0, CP0_CONTEXT
		lui	k1, %hi(kernelsp)
		srl	k0, k0, 23
#endif
		addu	k1, k0
		LONG_L	k1, %lo(kernelsp)(k1)
#endif
#ifdef CONFIG_64BIT
#ifdef CONFIG_MIPS_MT_SMTC
		.set	mips64
		mfc0	k0, CP0_TCBIND;
		.set	mips0
		lui	k0, %highest(kernelsp)
		dsrl	k1, 19
		/* No need to shift down and up to clear bits 0-2 */
#else
		MFC0	k1, CP0_CONTEXT
		lui	k0, %highest(kernelsp)
		dsrl	k1, 23
		daddiu	k0, %higher(kernelsp)
		dsll	k0, k0, 16
		daddiu	k0, %hi(kernelsp)
		dsll	k0, k0, 16
#endif /* CONFIG_MIPS_MT_SMTC */
		daddu	k1, k1, k0
		LONG_L	k1, %lo(kernelsp)(k1)
#endif /* CONFIG_64BIT */
		.endm

		.macro	set_saved_sp stackp temp temp2
#ifdef CONFIG_32BIT
#ifdef CONFIG_MIPS_MT_SMTC
		mfc0	\temp, CP0_TCBIND
		srl	\temp, 19
#else
		mfc0	\temp, CP0_CONTEXT
		srl	\temp, 23
#endif
#endif
#ifdef CONFIG_64BIT
#ifdef CONFIG_MIPS_MT_SMTC
		mfc0	\temp, CP0_TCBIND
		dsrl	\temp, 19
#else
		MFC0	\temp, CP0_CONTEXT
		dsrl	\temp, 23
#endif
#endif
		LONG_S	\stackp, kernelsp(\temp)
		.endm
#else
		.macro	get_saved_sp	/* Uniprocessor variation */
#ifdef CONFIG_64BIT
		lui	k1, %highest(kernelsp)
		daddiu	k1, %higher(kernelsp)
		dsll	k1, k1, 16
		daddiu	k1, %hi(kernelsp)
		dsll	k1, k1, 16
#else
		lui	k1, %hi(kernelsp)
#endif
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
		/*
		 * You might think that you don't need to save $0,
		 * but the FPU emulator and gdb remote debug stub
		 * need it to operate correctly
		 */
		LONG_S	$0, PT_R0(sp)
		mfc0	v1, CP0_STATUS
		LONG_S	$2, PT_R2(sp)
		LONG_S	v1, PT_STATUS(sp)
#ifdef CONFIG_MIPS_MT_SMTC
		/*
		 * Ideally, these instructions would be shuffled in
		 * to cover the pipeline delay.
		 */
		.set	mips32
		mfc0	v1, CP0_TCSTATUS
		.set	mips0
		LONG_S	v1, PT_TCSTATUS(sp)
#endif /* CONFIG_MIPS_MT_SMTC */
		LONG_S	$4, PT_R4(sp)
		mfc0	v1, CP0_CAUSE
		LONG_S	$5, PT_R5(sp)
		LONG_S	v1, PT_CAUSE(sp)
		LONG_S	$6, PT_R6(sp)
		MFC0	v1, CP0_EPC
		LONG_S	$7, PT_R7(sp)
#ifdef CONFIG_64BIT
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
#ifdef CONFIG_32BIT
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
#ifdef CONFIG_64BIT
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
/*
 * For SMTC kernel, global IE should be left set, and interrupts
 * controlled exclusively via IXMT.
 */

#ifdef CONFIG_MIPS_MT_SMTC
#define STATMASK 0x1e
#else
#define STATMASK 0x1f
#endif
		.macro	RESTORE_SOME
		.set	push
		.set	reorder
		.set	noat
#ifdef CONFIG_MIPS_MT_SMTC
		.set	mips32r2
		/*
		 * This may not really be necessary if ints are already
		 * inhibited here.
		 */
		mfc0	v0, CP0_TCSTATUS
		ori	v0, TCSTATUS_IXMT
		mtc0	v0, CP0_TCSTATUS
		ehb
		DMT	5				# dmt a1
		jal	mips_ihb
#endif /* CONFIG_MIPS_MT_SMTC */
		mfc0	a0, CP0_STATUS
		ori	a0, STATMASK
		xori	a0, STATMASK
		mtc0	a0, CP0_STATUS
		li	v1, 0xff00
		and	a0, v1
		LONG_L	v0, PT_STATUS(sp)
		nor	v1, $0, v1
		and	v0, v1
		or	v0, a0
		mtc0	v0, CP0_STATUS
#ifdef CONFIG_MIPS_MT_SMTC
/*
 * Only after EXL/ERL have been restored to status can we
 * restore TCStatus.IXMT.
 */
		LONG_L	v1, PT_TCSTATUS(sp)
		ehb
		mfc0	v0, CP0_TCSTATUS
		andi	v1, TCSTATUS_IXMT
		/* We know that TCStatua.IXMT should be set from above */
		xori	v0, v0, TCSTATUS_IXMT
		or	v0, v0, v1
		mtc0	v0, CP0_TCSTATUS
		ehb
		andi	a1, a1, VPECONTROL_TE
		beqz	a1, 1f
		emt
1:
		.set	mips0
#endif /* CONFIG_MIPS_MT_SMTC */
		LONG_L	v1, PT_EPC(sp)
		MTC0	v1, CP0_EPC
		LONG_L	$31, PT_R31(sp)
		LONG_L	$28, PT_R28(sp)
		LONG_L	$25, PT_R25(sp)
#ifdef CONFIG_64BIT
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
#if !defined(CONFIG_MIPS_MT_SMTC)
		mfc0	t0, CP0_STATUS
		li	t1, ST0_CU0 | 0x1f
		or	t0, t1
		xori	t0, 0x1f
		mtc0	t0, CP0_STATUS
#else /* CONFIG_MIPS_MT_SMTC */
		/*
		 * For SMTC, we need to set privilege
		 * and disable interrupts only for the
		 * current TC, using the TCStatus register.
		 */
		mfc0	t0,CP0_TCSTATUS
		/* Fortunately CU 0 is in the same place in both registers */
		/* Set TCU0, TMX, TKSU (for later inversion) and IXMT */
		li	t1, ST0_CU0 | 0x08001c00
		or	t0,t1
		/* Clear TKSU, leave IXMT */
		xori	t0, 0x00001800
		mtc0	t0, CP0_TCSTATUS
		ehb
		/* We need to leave the global IE bit set, but clear EXL...*/
		mfc0	t0, CP0_STATUS
		ori	t0, ST0_EXL | ST0_ERL
		xori	t0, ST0_EXL | ST0_ERL
		mtc0	t0, CP0_STATUS
#endif /* CONFIG_MIPS_MT_SMTC */
		irq_disable_hazard
		.endm

/*
 * Move to kernel mode and enable interrupts.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	STI
#if !defined(CONFIG_MIPS_MT_SMTC)
		mfc0	t0, CP0_STATUS
		li	t1, ST0_CU0 | 0x1f
		or	t0, t1
		xori	t0, 0x1e
		mtc0	t0, CP0_STATUS
#else /* CONFIG_MIPS_MT_SMTC */
		/*
		 * For SMTC, we need to set privilege
		 * and enable interrupts only for the
		 * current TC, using the TCStatus register.
		 */
		ehb
		mfc0	t0,CP0_TCSTATUS
		/* Fortunately CU 0 is in the same place in both registers */
		/* Set TCU0, TKSU (for later inversion) and IXMT */
		li	t1, ST0_CU0 | 0x08001c00
		or	t0,t1
		/* Clear TKSU *and* IXMT */
		xori	t0, 0x00001c00
		mtc0	t0, CP0_TCSTATUS
		ehb
		/* We need to leave the global IE bit set, but clear EXL...*/
		mfc0	t0, CP0_STATUS
		ori	t0, ST0_EXL
		xori	t0, ST0_EXL
		mtc0	t0, CP0_STATUS
		/* irq_enable_hazard below should expand to EHB for 24K/34K cpus */
#endif /* CONFIG_MIPS_MT_SMTC */
		irq_enable_hazard
		.endm

/*
 * Just move to kernel mode and leave interrupts as they are.
 * Set cp0 enable bit as sign that we're running on the kernel stack
 */
		.macro	KMODE
#ifdef CONFIG_MIPS_MT_SMTC
		/*
		 * This gets baroque in SMTC.  We want to
		 * protect the non-atomic clearing of EXL
		 * with DMT/EMT, but we don't want to take
		 * an interrupt while DMT is still in effect.
		 */

		/* KMODE gets invoked from both reorder and noreorder code */
		.set	push
		.set	mips32r2
		.set	noreorder
		mfc0	v0, CP0_TCSTATUS
		andi	v1, v0, TCSTATUS_IXMT
		ori	v0, TCSTATUS_IXMT
		mtc0	v0, CP0_TCSTATUS
		ehb
		DMT	2				# dmt	v0
		/*
		 * We don't know a priori if ra is "live"
		 */
		move	t0, ra
		jal	mips_ihb
		nop	/* delay slot */
		move	ra, t0
#endif /* CONFIG_MIPS_MT_SMTC */
		mfc0	t0, CP0_STATUS
		li	t1, ST0_CU0 | 0x1e
		or	t0, t1
		xori	t0, 0x1e
		mtc0	t0, CP0_STATUS
#ifdef CONFIG_MIPS_MT_SMTC
		ehb
		andi	v0, v0, VPECONTROL_TE
		beqz	v0, 2f
		nop	/* delay slot */
		emt
2:
		mfc0	v0, CP0_TCSTATUS
		/* Clear IXMT, then OR in previous value */
		ori	v0, TCSTATUS_IXMT
		xori	v0, TCSTATUS_IXMT
		or	v0, v1, v0
		mtc0	v0, CP0_TCSTATUS
		/*
		 * irq_disable_hazard below should expand to EHB
		 * on 24K/34K CPUS
		 */
		.set pop
#endif /* CONFIG_MIPS_MT_SMTC */
		irq_disable_hazard
		.endm

#endif /* _ASM_STACKFRAME_H */
