/* Copyright (C) 2000, 2001, 2003, 2004, 2005 Free Software Foundation, Inc.
   This file was pretty much copied from newlib.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#ifdef MMU_SUPPORT
	/* Section used for exception/timer interrupt stack area */
	.section .data.vbr.stack,"aw"
	.align 4
	.global __ST_VBR
__ST_VBR:
	.zero 1024 * 2          /* ; 2k for VBR handlers */
/* Label at the highest stack address where the stack grows from */
__timer_stack:
#endif /* MMU_SUPPORT */
	
	/* ;----------------------------------------
	Normal newlib crt1.asm */

#ifdef __SH5__
	.section .data,"aw"
	.global ___data
___data:

	.section .rodata,"a"
	.global ___rodata
___rodata:

#define ICCR_BASE  0x01600000
#define OCCR_BASE  0x01e00000
#define MMUIR_BASE 0x00000000
#define MMUDR_BASE 0x00800000

#define PTE_ENABLED     1
#define PTE_DISABLED    0

#define PTE_SHARED (1 << 1)
#define PTE_NOT_SHARED  0

#define PTE_CB_UNCACHEABLE  0
#define PTE_CB_DEVICE       1
#define PTE_CB_CACHEABLE_WB 2
#define PTE_CB_CACHEABLE_WT 3

#define PTE_SZ_4KB   (0 << 3)
#define PTE_SZ_64KB  (1 << 3)
#define PTE_SZ_1MB   (2 << 3)
#define PTE_SZ_512MB (3 << 3)

#define PTE_PRR      (1 << 6)
#define PTE_PRX      (1 << 7)
#define PTE_PRW      (1 << 8)
#define PTE_PRU      (1 << 9)

#define SR_MMU_BIT          31
#define SR_BL_BIT           28

#define ALIGN_4KB  (0xfff)
#define ALIGN_1MB  (0xfffff)
#define ALIGN_512MB (0x1fffffff)

#define DYNACON_BASE               0x0f000000
#define DM_CB_DLINK_BASE           0x0c000000
#define DM_DB_DLINK_BASE           0x0b000000

#define FEMI_AREA_0                0x00000000
#define FEMI_AREA_1                0x04000000
#define FEMI_AREA_2                0x05000000
#define FEMI_AREA_3                0x06000000
#define FEMI_AREA_4                0x07000000
#define FEMI_CB                    0x08000000

#define EMI_BASE                   0X80000000

#define DMA_BASE                   0X0e000000

#define CPU_BASE                   0X0d000000

#define PERIPH_BASE                0X09000000
#define DMAC_BASE                  0x0e000000
#define INTC_BASE                  0x0a000000
#define CPRC_BASE                  0x0a010000
#define TMU_BASE                   0x0a020000
#define SCIF_BASE                  0x0a030000
#define RTC_BASE                   0x0a040000



#define LOAD_CONST32(val, reg) \
	movi	((val) >> 16) & 65535, reg; \
	shori	(val) & 65535, reg

#define LOAD_PTEH_VAL(sym, align, bits, scratch_reg, reg) \
	LOAD_ADDR (sym, reg); \
	LOAD_CONST32 ((align), scratch_reg); \
	andc	reg, scratch_reg, reg; \
	LOAD_CONST32 ((bits), scratch_reg); \
	or	reg, scratch_reg, reg

#define LOAD_PTEL_VAL(sym, align, bits, scratch_reg, reg) \
	LOAD_ADDR (sym, reg); \
	LOAD_CONST32 ((align), scratch_reg); \
	andc	reg, scratch_reg, reg; \
	LOAD_CONST32 ((bits), scratch_reg); \
	or	reg, scratch_reg, reg

#define SET_PTE(pte_addr_reg, pteh_val_reg, ptel_val_reg) \
	putcfg  pte_addr_reg, 0, r63; \
	putcfg  pte_addr_reg, 1, ptel_val_reg; \
	putcfg  pte_addr_reg, 0, pteh_val_reg

#if __SH5__ == 64
	.section .text,"ax"
#define LOAD_ADDR(sym, reg) \
	movi	(sym >> 48) & 65535, reg; \
	shori	(sym >> 32) & 65535, reg; \
	shori	(sym >> 16) & 65535, reg; \
	shori	sym & 65535, reg
#else
	.mode	SHmedia
	.section .text..SHmedia32,"ax"
#define LOAD_ADDR(sym, reg) \
	movi	(sym >> 16) & 65535, reg; \
	shori	sym & 65535, reg
#endif
	.global start
start:
	LOAD_ADDR (_stack, r15)

#ifdef MMU_SUPPORT
	! Set up the VM using the MMU and caches

	! .vm_ep is first instruction to execute
	! after VM initialization
	pt/l	.vm_ep, tr1
	
	! Configure instruction cache (ICCR)
	movi	3, r2
	movi	0, r3
	LOAD_ADDR (ICCR_BASE, r1)
	putcfg	r1, 0, r2
	putcfg	r1, 1, r3

	! movi	7, r2 ! write through
	! Configure operand cache (OCCR)
	LOAD_ADDR (OCCR_BASE, r1)
	putcfg	r1, 0, r2
	putcfg	r1, 1, r3

	! Disable all PTE translations
	LOAD_ADDR (MMUIR_BASE, r1)
	LOAD_ADDR (MMUDR_BASE, r2)
	movi	64, r3
	pt/l	.disable_ptes_loop, tr0
.disable_ptes_loop:
	putcfg	r1, 0, r63
	putcfg	r2, 0, r63
	addi	r1, 16, r1
	addi	r2, 16, r2
	addi	r3, -1, r3
	bgt	r3, r63, tr0

	LOAD_ADDR (MMUIR_BASE, r1)

	! FEMI instruction mappings
	!   Area 0 - 1Mb cacheable at 0x00000000
	!   Area 1 - None
	!   Area 2 - 1Mb cacheable at 0x05000000
	!          - 1Mb cacheable at 0x05100000
	!   Area 3 - None
	!   Area 4 - None

	! Map a 1Mb page for instructions at 0x00000000
	LOAD_PTEH_VAL (FEMI_AREA_0, ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (FEMI_AREA_0, ALIGN_1MB, PTE_CB_CACHEABLE_WB | PTE_SZ_1MB | PTE_PRX | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 1Mb page for instructions at 0x05000000
	addi	r1, 16, r1
	LOAD_PTEH_VAL (FEMI_AREA_2, ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (FEMI_AREA_2, ALIGN_1MB, PTE_CB_CACHEABLE_WB | PTE_SZ_1MB | PTE_PRX | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 1Mb page for instructions at 0x05100000
	addi	r1, 16, r1
	LOAD_PTEH_VAL ((FEMI_AREA_2+0x100000), ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL ((FEMI_AREA_2+0x100000), ALIGN_1MB, PTE_CB_CACHEABLE_WB | PTE_SZ_1MB | PTE_PRX | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 512M page for instructions at EMI base
	addi	r1, 16, r1
	LOAD_PTEH_VAL (EMI_BASE, ALIGN_512MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (EMI_BASE, ALIGN_512MB, PTE_CB_CACHEABLE_WB | PTE_SZ_512MB | PTE_PRX | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 4K page for instructions at DM_DB_DLINK_BASE
	addi	r1, 16, r1
	LOAD_PTEH_VAL (DM_DB_DLINK_BASE, ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (DM_DB_DLINK_BASE, ALIGN_4KB, PTE_CB_CACHEABLE_WB | PTE_SZ_4KB | PTE_PRX | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	LOAD_ADDR (MMUDR_BASE, r1)

	! FEMI data mappings
	!   Area 0 - 1Mb cacheable at 0x00000000
	!   Area 1 - 1Mb device at 0x04000000
	!   Area 2 - 1Mb cacheable at 0x05000000
	!          - 1Mb cacheable at 0x05100000
	!   Area 3 - None
	!   Area 4 - None
	!   CB     - 1Mb device at 0x08000000

	! Map a 1Mb page for data at 0x00000000
	LOAD_PTEH_VAL (FEMI_AREA_0, ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (FEMI_AREA_0, ALIGN_1MB, PTE_CB_CACHEABLE_WB | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 1Mb page for data at 0x04000000
	addi	r1, 16, r1
	LOAD_PTEH_VAL (FEMI_AREA_1, ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (FEMI_AREA_1, ALIGN_1MB, PTE_CB_DEVICE | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 1Mb page for data at 0x05000000
	addi	r1, 16, r1
	LOAD_PTEH_VAL (FEMI_AREA_2, ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (FEMI_AREA_2, ALIGN_1MB, PTE_CB_CACHEABLE_WB | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 1Mb page for data at 0x05100000
	addi	r1, 16, r1
	LOAD_PTEH_VAL ((FEMI_AREA_2+0x100000), ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL ((FEMI_AREA_2+0x100000), ALIGN_1MB, PTE_CB_CACHEABLE_WB | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 4K page for registers at 0x08000000
	addi	r1, 16, r1
	LOAD_PTEH_VAL (FEMI_CB, ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (FEMI_CB, ALIGN_4KB, PTE_CB_DEVICE | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 512M page for data at EMI
	addi	r1, 16, r1
	LOAD_PTEH_VAL (EMI_BASE, ALIGN_512MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (EMI_BASE, ALIGN_512MB, PTE_CB_CACHEABLE_WB | PTE_SZ_512MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 4K page for DYNACON at DYNACON_BASE
	addi	r1, 16, r1
	LOAD_PTEH_VAL (DYNACON_BASE, ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (DYNACON_BASE, ALIGN_4KB, PTE_CB_DEVICE | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 4K page for instructions at DM_DB_DLINK_BASE
	addi	r1, 16, r1
	LOAD_PTEH_VAL (DM_DB_DLINK_BASE, ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (DM_DB_DLINK_BASE, ALIGN_4KB, PTE_CB_CACHEABLE_WB | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 4K page for data at DM_DB_DLINK_BASE+0x1000
	addi	r1, 16, r1
	LOAD_PTEH_VAL ((DM_DB_DLINK_BASE+0x1000), ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL ((DM_DB_DLINK_BASE+0x1000), ALIGN_4KB, PTE_CB_UNCACHEABLE | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 4K page for stack DM_DB_DLINK_BASE+0x2000
	addi	r1, 16, r1
	LOAD_PTEH_VAL ((DM_DB_DLINK_BASE+0x2000), ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL ((DM_DB_DLINK_BASE+0x2000), ALIGN_4KB, PTE_CB_CACHEABLE_WB | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 1M page for DM_CB_BASE2 at DM_CB_DLINK 
	! 0x0c000000 - 0x0c0fffff
	addi	r1, 16, r1
	LOAD_PTEH_VAL (DM_CB_DLINK_BASE, ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (DM_CB_DLINK_BASE, ALIGN_1MB, PTE_CB_DEVICE | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 1M page for DM_CB_BASE2 at DM_CB_DLINK 
	! 0x0c100000 - 0x0c1fffff
	addi	r1, 16, r1
	LOAD_PTEH_VAL ((DM_CB_DLINK_BASE+0x100000), ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL ((DM_CB_DLINK_BASE+0x100000), ALIGN_1MB, PTE_CB_DEVICE | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 1M page for DM_CB_BASE2 at DM_CB_DLINK 
	! 0x0c200000 - 0x0c2fffff
	addi	r1, 16, r1
	LOAD_PTEH_VAL ((DM_CB_DLINK_BASE+0x200000), ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL ((DM_CB_DLINK_BASE+0x200000), ALIGN_1MB, PTE_CB_DEVICE | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 1M page for DM_CB_BASE2 at DM_CB_DLINK 
	! 0x0c400000 - 0x0c4fffff
	addi	r1, 16, r1
	LOAD_PTEH_VAL ((DM_CB_DLINK_BASE+0x400000), ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL ((DM_CB_DLINK_BASE+0x400000), ALIGN_1MB, PTE_CB_DEVICE | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 1M page for DM_CB_BASE2 at DM_CB_DLINK 
	! 0x0c800000 - 0x0c8fffff
	addi	r1, 16, r1
	LOAD_PTEH_VAL ((DM_CB_DLINK_BASE+0x800000), ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL ((DM_CB_DLINK_BASE+0x800000), ALIGN_1MB, PTE_CB_DEVICE | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map a 4K page for DMA control registers
	addi	r1, 16, r1
	LOAD_PTEH_VAL (DMA_BASE, ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (DMA_BASE, ALIGN_4KB, PTE_CB_DEVICE | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map lots of 4K pages for peripherals

	! /* peripheral */
	addi	r1, 16, r1
	LOAD_PTEH_VAL (PERIPH_BASE, ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (PERIPH_BASE, ALIGN_4KB, PTE_CB_DEVICE | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)
	! /* dmac */
	addi	r1, 16, r1
	LOAD_PTEH_VAL (DMAC_BASE, ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (DMAC_BASE, ALIGN_4KB, PTE_CB_DEVICE | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)
	! /* intc */
	addi	r1, 16, r1
	LOAD_PTEH_VAL (INTC_BASE, ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (INTC_BASE, ALIGN_4KB, PTE_CB_DEVICE | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)
	! /* rtc */
	addi	r1, 16, r1
	LOAD_PTEH_VAL (RTC_BASE, ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (RTC_BASE, ALIGN_4KB, PTE_CB_DEVICE | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)
	! /* dmac */
	addi	r1, 16, r1
	LOAD_PTEH_VAL (TMU_BASE, ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (TMU_BASE, ALIGN_4KB, PTE_CB_DEVICE | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)
	! /* scif */
	addi	r1, 16, r1
	LOAD_PTEH_VAL (SCIF_BASE, ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (SCIF_BASE, ALIGN_4KB, PTE_CB_DEVICE | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)
	! /* cprc */
	addi	r1, 16, r1
	LOAD_PTEH_VAL (CPRC_BASE, ALIGN_4KB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (CPRC_BASE, ALIGN_4KB, PTE_CB_DEVICE | PTE_SZ_4KB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Map CPU WPC registers 
	addi	r1, 16, r1
	LOAD_PTEH_VAL (CPU_BASE, ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL (CPU_BASE, ALIGN_1MB, PTE_CB_DEVICE | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)
	addi	r1, 16, r1

	LOAD_PTEH_VAL ((CPU_BASE+0x100000), ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL ((CPU_BASE+0x100000), ALIGN_1MB, PTE_CB_DEVICE | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	addi	r1, 16, r1
	LOAD_PTEH_VAL ((CPU_BASE+0x200000), ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL ((CPU_BASE+0x200000), ALIGN_1MB, PTE_CB_DEVICE | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	addi	r1, 16, r1
	LOAD_PTEH_VAL ((CPU_BASE+0x400000), ALIGN_1MB, PTE_ENABLED | PTE_NOT_SHARED, r25, r2)
	LOAD_PTEL_VAL ((CPU_BASE+0x400000), ALIGN_1MB, PTE_CB_DEVICE | PTE_SZ_1MB | PTE_PRR | PTE_PRW | PTE_PRU, r25, r3)
	SET_PTE (r1, r2, r3)

	! Switch over to virtual addressing and enabled cache
	getcon	sr, r1
	movi	1, r2
	shlli	r2, SR_BL_BIT, r2
	or	r1, r2, r1
	putcon	r1, ssr
	getcon	sr, r1
	movi	1, r2
	shlli	r2, SR_MMU_BIT, r2
	or	r1, r2, r1
	putcon	r1, ssr
	gettr	tr1, r1
	putcon	r1, spc
	synco
	rte

	! VM entry point.  From now on, we are in VM mode.
.vm_ep:

	! Install the trap handler, by seeding vbr with the
	! correct value, and by assigning sr.bl = 0.

	LOAD_ADDR (vbr_start, r1)
	putcon	r1, vbr
	movi	~(1<<28), r1
	getcon	sr, r2
	and     r1, r2, r2
	putcon	r2, sr
#endif /* MMU_SUPPORT */

	pt/l	.Lzero_bss_loop, tr0
	pt/l	_init, tr5
	pt/l	___setup_argv_and_call_main, tr6
	pt/l	_exit, tr7

	! zero out bss
	LOAD_ADDR (_edata, r0)
	LOAD_ADDR (_end, r1)
.Lzero_bss_loop:
	stx.q	r0, r63, r63
	addi	r0, 8, r0
	bgt/l	r1, r0, tr0

	LOAD_ADDR (___data, r26)
	LOAD_ADDR (___rodata, r27)

#ifdef __SH_FPU_ANY__
	getcon	sr, r0
	! enable the FP unit, by resetting SR.FD
	! also zero out SR.FR, SR.SZ and SR.PR, as mandated by the ABI
	movi	0, r1
	shori	0xf000, r1
	andc	r0, r1, r0
	putcon	r0, sr
#if __SH5__ == 32
	pt/l ___set_fpscr, tr0
	movi	0, r4
	blink	tr0, r18
#endif
#endif

	! arrange for exit to call fini
	pt/l	_atexit, tr1
	LOAD_ADDR (_fini, r2)
	blink	tr1, r18

	! call init
	blink	tr5, r18

	! call the mainline
	blink	tr6, r18

	! call exit
	blink	tr7, r18
	! We should never return from _exit but in case we do we would enter the
	! the following tight loop. This avoids executing any data that might follow.
limbo:
	pt/l limbo, tr0
	blink tr0, r63
	
#ifdef MMU_SUPPORT
	! All these traps are handled in the same place. 
	.balign 256
vbr_start:
	pt/l handler, tr0	! tr0 trashed.
	blink tr0, r63
	.balign 256
vbr_100:
	pt/l handler, tr0	! tr0 trashed.
	blink tr0, r63
vbr_100_end:
	.balign 256
vbr_200:
	pt/l handler, tr0	! tr0 trashed.
	blink tr0, r63
	.balign 256
vbr_300:
	pt/l handler, tr0	! tr0 trashed.
	blink tr0, r63
	.balign 256	
vbr_400:	! Should be at vbr+0x400
handler:
	/* If the trap handler is there call it */
	LOAD_ADDR (__superh_trap_handler, r2)
	pta chandler,tr2
	beq r2, r63, tr2 /* If zero, ie not present branch around to chandler */
	/* Now call the trap handler with as much of the context unchanged as possible.
	   Move trapping address into R18 to make it look like the trap point */
	getcon spc, r18
	pt/l __superh_trap_handler, tr0
	blink tr0, r7
chandler:	
	getcon	spc, r62
	getcon expevt, r2
	pt/l	_exit, tr0
	blink	tr0, r63

	/* Simulated trap handler */
	.section	.text..SHmedia32,"ax"
gcc2_compiled.:
	.section	.debug_abbrev
.Ldebug_abbrev0:
	.section	.text..SHmedia32
.Ltext0:
	.section	.debug_info
.Ldebug_info0:
	.section	.debug_line
.Ldebug_line0:
	.section	.text..SHmedia32,"ax"
	.align 5
	.global	__superh_trap_handler
	.type	__superh_trap_handler,@function
__superh_trap_handler:
.LFB1:
	ptabs	r18, tr0
	addi.l	r15, -8, r15
	st.l	r15, 4, r14
	addi.l	r15, -8, r15
	add.l	r15, r63, r14
	st.l	r14, 0, r2
	 ptabs r7, tr0 
	addi.l	r14, 8, r14
	add.l	r14, r63, r15
	ld.l	r15, 4, r14
	addi.l	r15, 8, r15
	blink	tr0, r63
.LFE1:
.Lfe1:
	.size	__superh_trap_handler,.Lfe1-__superh_trap_handler

	.section	.text..SHmedia32
.Letext0:

	.section	.debug_info
	.ualong	0xa7
	.uaword	0x2
	.ualong	.Ldebug_abbrev0
	.byte	0x4
	.byte	0x1
	.ualong	.Ldebug_line0
	.ualong	.Letext0
	.ualong	.Ltext0
	.string	"trap_handler.c"

	.string	"xxxxxxxxxxxxxxxxxxxxxxxxxxxx"

	.string	"GNU C 2.97-sh5-010522"

	.byte	0x1
	.byte	0x2
	.ualong	0x9a
	.byte	0x1
	.string	"_superh_trap_handler"

	.byte	0x1
	.byte	0x2
	.byte	0x1
	.ualong	.LFB1
	.ualong	.LFE1
	.byte	0x1
	.byte	0x5e
	.byte	0x3
	.string	"trap_reason"

	.byte	0x1
	.byte	0x1
	.ualong	0x9a
	.byte	0x2
	.byte	0x91
	.byte	0x0
	.byte	0x0
	.byte	0x4
	.string	"unsigned int"

	.byte	0x4
	.byte	0x7
	.byte	0x0

	.section	.debug_abbrev
	.byte	0x1
	.byte	0x11
	.byte	0x1
	.byte	0x10
	.byte	0x6
	.byte	0x12
	.byte	0x1
	.byte	0x11
	.byte	0x1
	.byte	0x3
	.byte	0x8
	.byte	0x1b
	.byte	0x8
	.byte	0x25
	.byte	0x8
	.byte	0x13
	.byte	0xb
	.byte	0,0
	.byte	0x2
	.byte	0x2e
	.byte	0x1
	.byte	0x1
	.byte	0x13
	.byte	0x3f
	.byte	0xc
	.byte	0x3
	.byte	0x8
	.byte	0x3a
	.byte	0xb
	.byte	0x3b
	.byte	0xb
	.byte	0x27
	.byte	0xc
	.byte	0x11
	.byte	0x1
	.byte	0x12
	.byte	0x1
	.byte	0x40
	.byte	0xa
	.byte	0,0
	.byte	0x3
	.byte	0x5
	.byte	0x0
	.byte	0x3
	.byte	0x8
	.byte	0x3a
	.byte	0xb
	.byte	0x3b
	.byte	0xb
	.byte	0x49
	.byte	0x13
	.byte	0x2
	.byte	0xa
	.byte	0,0
	.byte	0x4
	.byte	0x24
	.byte	0x0
	.byte	0x3
	.byte	0x8
	.byte	0xb
	.byte	0xb
	.byte	0x3e
	.byte	0xb
	.byte	0,0
	.byte	0

	.section	.debug_pubnames
	.ualong	0x27
	.uaword	0x2
	.ualong	.Ldebug_info0
	.ualong	0xab
	.ualong	0x5b
	.string	"_superh_trap_handler"

	.ualong	0x0

	.section	.debug_aranges
	.ualong	0x1c
	.uaword	0x2
	.ualong	.Ldebug_info0
	.byte	0x4
	.byte	0x0
	.uaword	0x0,0
	.ualong	.Ltext0
	.ualong	.Letext0-.Ltext0
	.ualong	0x0
	.ualong	0x0
	.ident	"GCC: (GNU) 2.97-sh5-010522"
#endif /* MMU_SUPPORT */
#else /* ! __SH5__ */

	! make a place to keep any previous value of the vbr register
	! this will only have a value if it has been set by redboot (for example)
	.section .bss
old_vbr:
	.long 0
#ifdef PROFILE
profiling_enabled:
	.long 0
#endif


	.section .text
	.global	start
	.import ___rtos_profiler_start_timer
	.weak   ___rtos_profiler_start_timer
start:
	mov.l	stack_k,r15

#if defined (__SH3__) || (defined (__SH_FPU_ANY__) && ! defined (__SH2A__)) || defined (__SH4_NOFPU__)
#define VBR_SETUP
	! before zeroing the bss ...
	! if the vbr is already set to vbr_start then the program has been restarted
	! (i.e. it is not the first time the program has been run since reset)
	! reset the vbr to its old value before old_vbr (in bss) is wiped
	! this ensures that the later code does not create a circular vbr chain
	stc	vbr, r1
	mov.l	vbr_start_k, r2
	cmp/eq	r1, r2
	bf	0f
	! reset the old vbr value
	mov.l	old_vbr_k, r1
	mov.l	@r1, r2
	ldc	r2, vbr
0:	
#endif /* VBR_SETUP */
	
	! zero out bss
	mov.l	edata_k,r0
	mov.l	end_k,r1
	mov	#0,r2
start_l:
	mov.l	r2,@r0
	add	#4,r0
	cmp/ge	r0,r1
	bt	start_l

#if defined (__SH_FPU_ANY__)
	mov.l set_fpscr_k, r1
	mov #4,r4
	jsr @r1
	shll16 r4	! Set DN bit (flush denormal inputs to zero)
	lds r3,fpscr	! Switch to default precision
#endif /* defined (__SH_FPU_ANY__) */

#ifdef VBR_SETUP
	! save the existing contents of the vbr
	! there will only be a prior value when using something like redboot
	! otherwise it will be zero
	stc	vbr, r1
	mov.l	old_vbr_k, r2
	mov.l	r1, @r2
	! setup vbr
	mov.l	vbr_start_k, r1
	ldc	r1,vbr
#endif /* VBR_SETUP */

	! if an rtos is exporting a timer start fn,
	! then pick up an SR which does not enable ints
	! (the rtos will take care of this)
	mov.l rtos_start_fn, r0
	mov.l sr_initial_bare, r1
	tst	r0, r0
	bt	set_sr

	mov.l sr_initial_rtos, r1

set_sr:
	! Set status register (sr)
	ldc	r1, sr

	! arrange for exit to call fini
	mov.l	atexit_k,r0
	mov.l	fini_k,r4
	jsr	@r0
	nop

#ifdef PROFILE
	! arrange for exit to call _mcleanup (via stop_profiling)
	mova    stop_profiling,r0
	mov.l   atexit_k,r1
	jsr     @r1
	mov	r0, r4

	! Call profiler startup code
	mov.l monstartup_k, r0
	mov.l start_k, r4
	mov.l etext_k, r5
	jsr @r0
	nop

	! enable profiling trap
	! until now any trap 33s will have been ignored
	! This means that all library functions called before this point
	! (directly or indirectly) may have the profiling trap at the start.
	! Therefore, only mcount itself may not have the extra header.
	mov.l	profiling_enabled_k2, r0
	mov	#1, r1
	mov.l	r1, @r0
#endif /* PROFILE */

	! call init
	mov.l	init_k,r0
	jsr	@r0
	nop

	! call the mainline	
	mov.l	main_k,r0
	jsr	@r0
	nop

	! call exit
	mov	r0,r4
	mov.l	exit_k,r0
	jsr	@r0
	nop
	
		.balign 4
#ifdef PROFILE
stop_profiling:
	# stop mcount counting
	mov.l	profiling_enabled_k2, r0
	mov	#0, r1
	mov.l	r1, @r0

	# call mcleanup
	mov.l	mcleanup_k, r0
	jmp	@r0
	nop
		
		.balign 4
mcleanup_k:
	.long __mcleanup
monstartup_k:
	.long ___monstartup
profiling_enabled_k2:
	.long profiling_enabled
start_k:
	.long _start
etext_k:
	.long __etext
#endif /* PROFILE */

	.align 2
#if defined (__SH_FPU_ANY__)
set_fpscr_k:
	.long	___set_fpscr
#endif /*  defined (__SH_FPU_ANY__) */

stack_k:
	.long	_stack	
edata_k:
	.long	_edata
end_k:
	.long	_end
main_k:
	.long	___setup_argv_and_call_main
exit_k:
	.long	_exit
atexit_k:
	.long	_atexit
init_k:
	.long	_init
fini_k:
	.long	_fini
#ifdef VBR_SETUP
old_vbr_k:
	.long	old_vbr
vbr_start_k:
	.long	vbr_start
#endif /* VBR_SETUP */
	
sr_initial_rtos:
	! Privileged mode RB 1 BL 0. Keep BL 0 to allow default trap handlers to work.
	! Whether profiling or not, keep interrupts masked,
	! the RTOS will enable these if required.
	.long 0x600000f1 

rtos_start_fn:
	.long ___rtos_profiler_start_timer
	
#ifdef PROFILE
sr_initial_bare:
	! Privileged mode RB 1 BL 0. Keep BL 0 to allow default trap handlers to work.
	! For bare machine, we need to enable interrupts to get profiling working
	.long 0x60000001
#else

sr_initial_bare:
	! Privileged mode RB 1 BL 0. Keep BL 0 to allow default trap handlers to work.
	! Keep interrupts disabled - the application will enable as required.
	.long 0x600000f1
#endif

	! supplied for backward compatibility only, in case of linking
	! code whose main() was compiled with an older version of GCC.
	.global ___main
___main:
	rts
	nop
#ifdef VBR_SETUP
! Exception handlers	
	.balign 256
vbr_start:
	mov.l 2f, r0     ! load the old vbr setting (if any)
	mov.l @r0, r0
	cmp/eq #0, r0
	bf 1f
	! no previous vbr - jump to own generic handler
	bra handler
	nop
1:	! there was a previous handler - chain them
	jmp @r0
	nop
	.balign 4
2:
	.long old_vbr

	.balign 256
vbr_100:
	#ifdef PROFILE
	! Note on register usage.
	! we use r0..r3 as scratch in this code. If we are here due to a trapa for profiling
	! then this is OK as we are just before executing any function code.
	! The other r4..r7 we save explicityl on the stack
	! Remaining registers are saved by normal ABI conventions and we assert we do not
	! use floating point registers.
	mov.l expevt_k1, r1
	mov.l @r1, r1
	mov.l event_mask, r0
	and r0,r1
	mov.l trapcode_k, r2
	cmp/eq r1,r2
	bt 1f
	bra handler_100   ! if not a trapa, go to default handler
	nop
1:	
	mov.l trapa_k, r0
	mov.l @r0, r0
	shlr2 r0      ! trapa code is shifted by 2.
	cmp/eq #33, r0
	bt 2f
	bra handler_100
	nop
2:	
	
	! If here then it looks like we have trap #33
	! Now we need to call mcount with the following convention
	! Save and restore r4..r7
	mov.l	r4,@-r15
	mov.l	r5,@-r15
	mov.l	r6,@-r15
	mov.l	r7,@-r15
	sts.l	pr,@-r15

	! r4 is frompc.
	! r5 is selfpc
	! r0 is the branch back address.
	! The code sequence emitted by gcc for the profiling trap is
	! .align 2
	! trapa #33
	! .align 2
	! .long lab Where lab is planted by the compiler. This is the address
	! of a datum that needs to be incremented. 
	sts pr,  r4     ! frompc
	stc spc, r5	! selfpc
	mov #2, r2
	not r2, r2      ! pattern to align to 4
	and r2, r5      ! r5 now has aligned address
!	add #4, r5      ! r5 now has address of address
	mov r5, r2      ! Remember it.
!	mov.l @r5, r5   ! r5 has value of lable (lab in above example)
	add #8, r2
	ldc r2, spc     ! our return address avoiding address word

	! only call mcount if profiling is enabled
	mov.l profiling_enabled_k, r0
	mov.l @r0, r0
	cmp/eq #0, r0
	bt 3f
	! call mcount
	mov.l mcount_k, r2
	jsr @r2
	nop
3:
	lds.l @r15+,pr
	mov.l @r15+,r7
	mov.l @r15+,r6
	mov.l @r15+,r5
	mov.l @r15+,r4
	rte
	nop
	.balign 4
event_mask:
	.long 0xfff
trapcode_k:	
	.long 0x160
expevt_k1:
	.long 0xff000024 ! Address of expevt
trapa_k:	
	.long 0xff000020
mcount_k:
	.long __call_mcount
profiling_enabled_k:
	.long profiling_enabled
#endif
	! Non profiling case.
handler_100:
	mov.l 2f, r0     ! load the old vbr setting (if any)
	mov.l @r0, r0
	cmp/eq #0, r0
	bf 1f
	! no previous vbr - jump to own generic handler
	bra handler
	nop	
1:	! there was a previous handler - chain them
	add #0x7f, r0	 ! 0x7f
	add #0x7f, r0	 ! 0xfe
	add #0x2, r0     ! add 0x100 without corrupting another register
	jmp @r0
	nop
	.balign 4
2:	
	.long old_vbr

	.balign 256
vbr_200:
	mov.l 2f, r0     ! load the old vbr setting (if any)
	mov.l @r0, r0
	cmp/eq #0, r0
	bf 1f
	! no previous vbr - jump to own generic handler
	bra handler
	nop	
1:	! there was a previous handler - chain them
	add #0x7f, r0	 ! 0x7f
	add #0x7f, r0	 ! 0xfe
	add #0x7f, r0	 ! 0x17d
	add #0x7f, r0    ! 0x1fc
	add #0x4, r0     ! add 0x200 without corrupting another register
	jmp @r0
	nop
	.balign 4
2:
	.long old_vbr

	.balign 256
vbr_300:
	mov.l 2f, r0     ! load the old vbr setting (if any)
	mov.l @r0, r0
	cmp/eq #0, r0
	bf 1f
	! no previous vbr - jump to own generic handler
	bra handler
	nop	
1:	! there was a previous handler - chain them
	rotcr r0
	rotcr r0
	add #0x7f, r0	 ! 0x1fc
	add #0x41, r0	 ! 0x300
	rotcl r0
	rotcl r0	 ! Add 0x300 without corrupting another register
	jmp @r0
	nop
	.balign 4
2:
	.long old_vbr

	.balign 256	
vbr_400:	! Should be at vbr+0x400
	mov.l 2f, r0     ! load the old vbr setting (if any)
	mov.l @r0, r0
	cmp/eq #0, r0
	! no previous vbr - jump to own generic handler
	bt handler
	! there was a previous handler - chain them
	rotcr r0
	rotcr r0
	add #0x7f, r0	 ! 0x1fc
	add #0x7f, r0	 ! 0x3f8
	add #0x02, r0	 ! 0x400
	rotcl r0
	rotcl r0	 ! Add 0x400 without corrupting another register
	jmp @r0
	nop
	.balign 4
2:
	.long old_vbr
handler:
	/* If the trap handler is there call it */
	mov.l	superh_trap_handler_k, r0
	cmp/eq	#0, r0       ! True if zero.
	bf 3f
	bra   chandler
	nop
3:	
	! Here handler available, call it. 
	/* Now call the trap handler with as much of the context unchanged as possible.
	   Move trapping address into PR to make it look like the trap point */
	stc spc, r1
	lds r1, pr
	mov.l expevt_k, r4
	mov.l @r4, r4 ! r4 is value of expevt, first parameter.
	mov r1, r5   ! Remember trapping pc.
	mov r1, r6   ! Remember trapping pc.
	mov.l chandler_k, r1
	mov.l superh_trap_handler_k, r2
	! jmp to trap handler to avoid disturbing pr. 
	jmp @r2
	nop

	.balign 256
vbr_500:
	mov.l 2f, r0     ! load the old vbr setting (if any)
	mov.l @r0, r0
	cmp/eq #0, r0
	! no previous vbr - jump to own generic handler
	bt handler
	! there was a previous handler - chain them
	rotcr r0
	rotcr r0
	add #0x7f, r0	 ! 0x1fc
	add #0x7f, r0	 ! 0x3f8
	add #0x42, r0	 ! 0x500
	rotcl r0
	rotcl r0	 ! Add 0x500 without corrupting another register
	jmp @r0
	nop
	.balign 4
2:
	.long old_vbr

	.balign 256
vbr_600:
#ifdef PROFILE	
	! Should be at vbr+0x600
	! Now we are in the land of interrupts so need to save more state. 
	! Save register state
	mov.l interrupt_stack_k, r15 ! r15 has been saved to sgr.
	mov.l	r0,@-r15	
	mov.l	r1,@-r15
	mov.l	r2,@-r15
	mov.l	r3,@-r15
	mov.l	r4,@-r15
	mov.l	r5,@-r15
	mov.l	r6,@-r15
	mov.l	r7,@-r15
	sts.l	pr,@-r15
	! Pass interrupted pc to timer_handler as first parameter (r4).
	stc    spc, r4
	mov.l timer_handler_k, r0
	jsr @r0
	nop
	lds.l @r15+,pr
	mov.l @r15+,r7
	mov.l @r15+,r6
	mov.l @r15+,r5
	mov.l @r15+,r4
	mov.l @r15+,r3
	mov.l @r15+,r2
	mov.l @r15+,r1
	mov.l @r15+,r0
	stc sgr, r15    ! Restore r15, destroyed by this sequence. 
	rte
	nop
#else
	mov.l 2f, r0     ! Load the old vbr setting (if any).
	mov.l @r0, r0
	cmp/eq #0, r0
	! no previous vbr - jump to own handler
	bt chandler
	! there was a previous handler - chain them
	rotcr r0
	rotcr r0
	add #0x7f, r0	 ! 0x1fc
	add #0x7f, r0	 ! 0x3f8
	add #0x7f, r0	 ! 0x5f4
	add #0x03, r0	 ! 0x600
	rotcl r0
	rotcl r0	 ! Add 0x600 without corrupting another register
	jmp @r0
	nop
	.balign 4
2:
	.long old_vbr
#endif	 /* PROFILE code */
chandler:
	mov.l expevt_k, r4
	mov.l @r4, r4 ! r4 is value of expevt hence making this the return code
	mov.l handler_exit_k,r0
	jsr   @r0
	nop
	! We should never return from _exit but in case we do we would enter the
	! the following tight loop
limbo:
	bra limbo
	nop
	.balign 4
#ifdef PROFILE
interrupt_stack_k:
	.long __timer_stack	! The high end of the stack
timer_handler_k:
	.long __profil_counter
#endif
expevt_k:
	.long 0xff000024 ! Address of expevt
chandler_k:	
	.long chandler	
superh_trap_handler_k:
	.long	__superh_trap_handler
handler_exit_k:
	.long _exit
	.align 2
! Simulated compile of trap handler.
	.section	.debug_abbrev,"",@progbits
.Ldebug_abbrev0:
	.section	.debug_info,"",@progbits
.Ldebug_info0:
	.section	.debug_line,"",@progbits
.Ldebug_line0:
	.text
.Ltext0:
	.align 5
	.type	__superh_trap_handler,@function
__superh_trap_handler:
.LFB1:
	mov.l	r14,@-r15
.LCFI0:
	add	#-4,r15
.LCFI1:
	mov	r15,r14
.LCFI2:
	mov.l	r4,@r14
	lds	r1, pr
	add	#4,r14
	mov	r14,r15
	mov.l	@r15+,r14
	rts	
	nop
.LFE1:
.Lfe1:
	.size	__superh_trap_handler,.Lfe1-__superh_trap_handler
	.section	.debug_frame,"",@progbits
.Lframe0:
	.ualong	.LECIE0-.LSCIE0
.LSCIE0:
	.ualong	0xffffffff
	.byte	0x1
	.string	""
	.uleb128 0x1
	.sleb128 -4
	.byte	0x11
	.byte	0xc
	.uleb128 0xf
	.uleb128 0x0
	.align 2
.LECIE0:
.LSFDE0:
	.ualong	.LEFDE0-.LASFDE0
.LASFDE0:
	.ualong	.Lframe0
	.ualong	.LFB1
	.ualong	.LFE1-.LFB1
	.byte	0x4
	.ualong	.LCFI0-.LFB1
	.byte	0xe
	.uleb128 0x4
	.byte	0x4
	.ualong	.LCFI1-.LCFI0
	.byte	0xe
	.uleb128 0x8
	.byte	0x8e
	.uleb128 0x1
	.byte	0x4
	.ualong	.LCFI2-.LCFI1
	.byte	0xd
	.uleb128 0xe
	.align 2
.LEFDE0:
	.text
.Letext0:
	.section	.debug_info
	.ualong	0xb3
	.uaword	0x2
	.ualong	.Ldebug_abbrev0
	.byte	0x4
	.uleb128 0x1
	.ualong	.Ldebug_line0
	.ualong	.Letext0
	.ualong	.Ltext0
	.string	"trap_handler.c"
	.string	"xxxxxxxxxxxxxxxxxxxxxxxxxxxx"
	.string	"GNU C 3.2 20020529 (experimental)"
	.byte	0x1
	.uleb128 0x2
	.ualong	0xa6
	.byte	0x1
	.string	"_superh_trap_handler"
	.byte	0x1
	.byte	0x2
	.byte	0x1
	.ualong	.LFB1
	.ualong	.LFE1
	.byte	0x1
	.byte	0x5e
	.uleb128 0x3
	.string	"trap_reason"
	.byte	0x1
	.byte	0x1
	.ualong	0xa6
	.byte	0x2
	.byte	0x91
	.sleb128 0
	.byte	0x0
	.uleb128 0x4
	.string	"unsigned int"
	.byte	0x4
	.byte	0x7
	.byte	0x0
	.section	.debug_abbrev
	.uleb128 0x1
	.uleb128 0x11
	.byte	0x1
	.uleb128 0x10
	.uleb128 0x6
	.uleb128 0x12
	.uleb128 0x1
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x1b
	.uleb128 0x8
	.uleb128 0x25
	.uleb128 0x8
	.uleb128 0x13
	.uleb128 0xb
	.byte	0x0
	.byte	0x0
	.uleb128 0x2
	.uleb128 0x2e
	.byte	0x1
	.uleb128 0x1
	.uleb128 0x13
	.uleb128 0x3f
	.uleb128 0xc
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x27
	.uleb128 0xc
	.uleb128 0x11
	.uleb128 0x1
	.uleb128 0x12
	.uleb128 0x1
	.uleb128 0x40
	.uleb128 0xa
	.byte	0x0
	.byte	0x0
	.uleb128 0x3
	.uleb128 0x5
	.byte	0x0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0x3a
	.uleb128 0xb
	.uleb128 0x3b
	.uleb128 0xb
	.uleb128 0x49
	.uleb128 0x13
	.uleb128 0x2
	.uleb128 0xa
	.byte	0x0
	.byte	0x0
	.uleb128 0x4
	.uleb128 0x24
	.byte	0x0
	.uleb128 0x3
	.uleb128 0x8
	.uleb128 0xb
	.uleb128 0xb
	.uleb128 0x3e
	.uleb128 0xb
	.byte	0x0
	.byte	0x0
	.byte	0x0
	.section	.debug_pubnames,"",@progbits
	.ualong	0x27
	.uaword	0x2
	.ualong	.Ldebug_info0
	.ualong	0xb7
	.ualong	0x67
	.string	"_superh_trap_handler"
	.ualong	0x0
	.section	.debug_aranges,"",@progbits
	.ualong	0x1c
	.uaword	0x2
	.ualong	.Ldebug_info0
	.byte	0x4
	.byte	0x0
	.uaword	0x0
	.uaword	0x0
	.ualong	.Ltext0
	.ualong	.Letext0-.Ltext0
	.ualong	0x0
	.ualong	0x0
#endif /* VBR_SETUP */
#endif /* ! __SH5__ */
