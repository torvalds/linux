/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005 Embedded Alley Solutions, Inc
 */
#ifndef __ASM_MACH_KERNEL_ENTRY_INIT_H
#define __ASM_MACH_KERNEL_ENTRY_INIT_H

#include <asm/cacheops.h>
#include <asm/addrspace.h>

#define CO_CONFIGPR_VALID  0x3F1F41FF    /* valid bits to write to ConfigPR */
#define HAZARD_CP0 nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop; nop;
#define CACHE_OPC      0xBC000000  /* MIPS cache instruction opcode */
#define ICACHE_LINE_SIZE        32      /* Instruction cache line size bytes */
#define DCACHE_LINE_SIZE        32      /* Data cache line size in bytes */

#define ICACHE_SET_COUNT        256     /* Instruction cache set count */
#define DCACHE_SET_COUNT        128     /* Data cache set count */

#define ICACHE_SET_SIZE         (ICACHE_SET_COUNT * ICACHE_LINE_SIZE)
#define DCACHE_SET_SIZE         (DCACHE_SET_COUNT * DCACHE_LINE_SIZE)

	.macro	kernel_entry_setup
	.set	push
	.set	noreorder
	/*
	 * PNX8550 entry point, when running a non compressed
	 * kernel. When loading a zImage, the head.S code in
	 * arch/mips/zboot/pnx8550 will init the caches and,
	 * decompress the kernel, and branch to kernel_entry.
		 */
cache_begin:	li	t0, (1<<28)
	mtc0	t0, CP0_STATUS /* cp0 usable */
	HAZARD_CP0

	mtc0 	zero, CP0_CAUSE
	HAZARD_CP0


	/* Set static virtual to phys address translation and TLB disabled */
	mfc0 	t0, CP0_CONFIG, 7
	HAZARD_CP0

	and t0,~((1<<19) | (1<<20))     /* TLB/MAP cleared */
	mtc0	t0, CP0_CONFIG, 7
	HAZARD_CP0

	/* CPU boots with kseg0 cache algo set to 0x2 -- uncached */

	init_icache
	nop
	init_dcache
	nop

	cachePr4450ICReset
	nop

	cachePr4450DCReset
	nop

	/* read ConfigPR into t0 */
	mfc0	t0, CP0_CONFIG, 7
	HAZARD_CP0

	/*  enable the TLB */
	or      t0, (1<<19)

	/* disable the ICACHE: at least 10x slower */
	/* or      t0, (1<<26) */

	/* disable the DCACHE; CONFIG_CPU_HAS_LLSC should not be set  */
	/* or      t0, (1<<27) */

	and	t0, CO_CONFIGPR_VALID

	/* enable TLB. */
	mtc0	t0, CP0_CONFIG, 7
	HAZARD_CP0
cache_end:
	/* Setup CMEM_0 to MMIO address space, 2MB */
	lui    t0, 0x1BE0
	addi   t0, t0, 0x3
	mtc0   $8, $22, 4
	nop

	/* Setup CMEM_1, 128MB */
	lui    t0, 0x1000
	addi   t0, t0, 0xf
	mtc0   $8, $22, 5
	nop


	/* Setup CMEM_2, 32MB */
	lui    t0, 0x1C00
	addi   t0, t0, 0xb
	mtc0   $8, $22, 6
	nop

	/* Setup CMEM_3, 0MB */
	lui    t0, 0x0
	addi   t0, t0, 0x0
	mtc0   $8, $22, 7
	nop

	/* Enable cache */
	mfc0	t0, CP0_CONFIG
	HAZARD_CP0
	and	t0, t0, 0xFFFFFFF8
	or	t0, t0, 3
	mtc0	t0, CP0_CONFIG
	HAZARD_CP0
	.set	pop
	.endm

	.macro	init_icache
	.set	push
	.set	noreorder

	/* Get Cache Configuration */
	mfc0	t3, CP0_CONFIG, 1
	HAZARD_CP0

	/* get cache Line size */

	srl   t1, t3, 19   /* C0_CONFIGPR_IL_SHIFT */
	andi  t1, t1, 0x7  /* C0_CONFIGPR_IL_MASK */
	beq   t1, zero, pr4450_instr_cache_invalidated /* if zero instruction cache is absent */
	nop
	addiu t0, t1, 1
	ori   t1, zero, 1
	sllv  t1, t1, t0

	/* get max cache Index */
	srl   t2, t3, 22  /* C0_CONFIGPR_IS_SHIFT */
	andi  t2, t2, 0x7 /* C0_CONFIGPR_IS_MASK */
	addiu t0, t2, 6
	ori   t2, zero, 1
	sllv  t2, t2, t0

	/* get max cache way */
	srl   t3, t3, 16  /* C0_CONFIGPR_IA_SHIFT */
	andi  t3, t3, 0x7 /* C0_CONFIGPR_IA_MASK */
	addiu t3, t3, 1

	/* total no of cache lines */
	multu t2, t3             /* max index * max way */
	mflo  t2
	addiu t2, t2, -1

	move  t0, zero
pr4450_next_instruction_cache_set:
	cache  Index_Invalidate_I, 0(t0)
	addu  t0, t0, t1         /* add bytes in a line */
	bne   t2, zero, pr4450_next_instruction_cache_set
	addiu t2, t2, -1   /* reduce no of lines to invalidate by one */
pr4450_instr_cache_invalidated:
	.set	pop
	.endm

	.macro	init_dcache
	.set	push
	.set	noreorder
	move t1, zero

	/* Store Tag Information */
	mtc0	zero, CP0_TAGLO, 0
	HAZARD_CP0

	mtc0	zero, CP0_TAGHI, 0
	HAZARD_CP0

	/* Cache size is 16384 = 512 lines x 32 bytes per line */
	or       t2, zero, (128*4)-1  /* 512 lines  */
	/* Invalidate all lines */
2:
	cache Index_Store_Tag_D, 0(t1)
	addiu    t2, t2, -1
	bne      t2, zero, 2b
	addiu    t1, t1, 32        /* 32 bytes in a line */
	.set pop
	.endm

	.macro	cachePr4450ICReset
	.set	push
	.set	noreorder

	/* Save CP0 status reg on entry; */
	/* disable interrupts during cache reset */
	mfc0    t0, CP0_STATUS      /* T0 = interrupt status on entry */
	HAZARD_CP0

	mtc0    zero, CP0_STATUS   /* disable CPU interrupts */
	HAZARD_CP0

	or      t1, zero, zero              /* T1 = starting cache index (0) */
	ori     t2, zero, (256 - 1) /* T2 = inst cache set cnt - 1 */

	icache_invd_loop:
	/* 9 == register t1 */
	.word   (CACHE_OPC | (9 << 21) | (Index_Invalidate_I << 16) | \
		(0 * ICACHE_SET_SIZE))  /* invalidate inst cache WAY0 */
	.word   (CACHE_OPC | (9 << 21) | (Index_Invalidate_I << 16) | \
		(1 * ICACHE_SET_SIZE))  /* invalidate inst cache WAY1 */

	addiu   t1, t1, ICACHE_LINE_SIZE    /* T1 = next cache line index */
	bne     t2, zero, icache_invd_loop /* T2 = 0 if all sets invalidated */
	addiu   t2, t2, -1        /* decrement T2 set cnt (delay slot) */

	/* Initialize the latches in the instruction cache tag */
	/* that drive the way selection tri-state bus drivers, by doing a */
	/* dummy load while the instruction cache is still disabled. */
	/* TODO: Is this needed ? */
	la      t1, KSEG0            /* T1 = cached memory base address */
	lw      zero, 0x0000(t1)      /* (dummy read of first memory word) */

	mtc0    t0, CP0_STATUS        /* restore interrupt status on entry */
	HAZARD_CP0
	.set	pop
	.endm

	.macro	cachePr4450DCReset
	.set	push
	.set	noreorder
	mfc0    t0, CP0_STATUS           /* T0 = interrupt status on entry */
	HAZARD_CP0
	mtc0    zero, CP0_STATUS         /* disable CPU interrupts */
	HAZARD_CP0

	/* Writeback/invalidate entire data cache sets/ways/lines */
	or      t1, zero, zero              /* T1 = starting cache index (0) */
	ori     t2, zero, (DCACHE_SET_COUNT - 1) /* T2 = data cache set cnt - 1 */

	dcache_wbinvd_loop:
	/* 9 == register t1 */
	.word   (CACHE_OPC | (9 << 21) | (Index_Writeback_Inv_D << 16) | \
		(0 * DCACHE_SET_SIZE))  /* writeback/invalidate WAY0 */
	.word   (CACHE_OPC | (9 << 21) | (Index_Writeback_Inv_D << 16) | \
		(1 * DCACHE_SET_SIZE))  /* writeback/invalidate WAY1 */
	.word   (CACHE_OPC | (9 << 21) | (Index_Writeback_Inv_D << 16) | \
		(2 * DCACHE_SET_SIZE))  /* writeback/invalidate WAY2 */
	.word   (CACHE_OPC | (9 << 21) | (Index_Writeback_Inv_D << 16) | \
		(3 * DCACHE_SET_SIZE))  /* writeback/invalidate WAY3 */

	addiu   t1, t1, DCACHE_LINE_SIZE  /* T1 = next data cache line index */
	bne     t2, zero, dcache_wbinvd_loop /* T2 = 0 when wbinvd entire cache */
	addiu   t2, t2, -1          /* decrement T2 set cnt (delay slot) */

	/* Initialize the latches in the data cache tag that drive the way
	selection tri-state bus drivers, by doing a dummy load while the
	data cache is still in the disabled mode.  TODO: Is this needed ? */
	la      t1, KSEG0            /* T1 = cached memory base address */
	lw      zero, 0x0000(t1)      /* (dummy read of first memory word) */

	mtc0    t0, CP0_STATUS       /* restore interrupt status on entry */
	HAZARD_CP0
	.set	pop
	.endm

#endif /* __ASM_MACH_KERNEL_ENTRY_INIT_H */
