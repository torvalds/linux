/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1990 William F. Jolitz
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)segments.h	7.1 (Berkeley) 5/9/91
 * $FreeBSD$
 */

#ifndef _X86_SEGMENTS_H_
#define	_X86_SEGMENTS_H_

/*
 * X86 Segmentation Data Structures and definitions
 */

/*
 * Selectors
 */
#define	SEL_RPL_MASK	3		/* requester priv level */
#define	ISPL(s)		((s)&3)		/* priority level of a selector */
#define	SEL_KPL		0		/* kernel priority level */
#define	SEL_UPL		3		/* user priority level */
#define	ISLDT(s)	((s)&SEL_LDT)	/* is it local or global */
#define	SEL_LDT		4		/* local descriptor table */
#define	IDXSEL(s)	(((s)>>3) & 0x1fff) /* index of selector */
#define	LSEL(s,r)	(((s)<<3) | SEL_LDT | r) /* a local selector */
#define	GSEL(s,r)	(((s)<<3) | r)	/* a global selector */

/*
 * User segment descriptors (%cs, %ds etc for i386 apps. 64 bit wide)
 * For long-mode apps, %cs only has the conforming bit in sd_type, the sd_dpl,
 * sd_p, sd_l and sd_def32 which must be zero).  %ds only has sd_p.
 */
struct segment_descriptor {
	unsigned sd_lolimit:16;		/* segment extent (lsb) */
	unsigned sd_lobase:24;		/* segment base address (lsb) */
	unsigned sd_type:5;		/* segment type */
	unsigned sd_dpl:2;		/* segment descriptor priority level */
	unsigned sd_p:1;		/* segment descriptor present */
	unsigned sd_hilimit:4;		/* segment extent (msb) */
	unsigned sd_xx:2;		/* unused */
	unsigned sd_def32:1;		/* default 32 vs 16 bit size */
	unsigned sd_gran:1;		/* limit granularity (byte/page units)*/
	unsigned sd_hibase:8;		/* segment base address  (msb) */
} __packed;

struct user_segment_descriptor {
	unsigned sd_lolimit:16;		/* segment extent (lsb) */
	unsigned sd_lobase:24;		/* segment base address (lsb) */
	unsigned sd_type:5;		/* segment type */
	unsigned sd_dpl:2;		/* segment descriptor priority level */
	unsigned sd_p:1;		/* segment descriptor present */
	unsigned sd_hilimit:4;		/* segment extent (msb) */
	unsigned sd_xx:1;		/* unused */
	unsigned sd_long:1;		/* long mode (cs only) */
	unsigned sd_def32:1;		/* default 32 vs 16 bit size */
	unsigned sd_gran:1;		/* limit granularity (byte/page units)*/
	unsigned sd_hibase:8;		/* segment base address  (msb) */
} __packed;

#define	USD_GETBASE(sd)		(((sd)->sd_lobase) | (sd)->sd_hibase << 24)
#define	USD_SETBASE(sd, b)	(sd)->sd_lobase = (b);	\
				(sd)->sd_hibase = ((b) >> 24);
#define	USD_GETLIMIT(sd)	(((sd)->sd_lolimit) | (sd)->sd_hilimit << 16)
#define	USD_SETLIMIT(sd, l)	(sd)->sd_lolimit = (l);	\
				(sd)->sd_hilimit = ((l) >> 16);

#ifdef __i386__
/*
 * Gate descriptors (e.g. indirect descriptors)
 */
struct gate_descriptor {
	unsigned gd_looffset:16;	/* gate offset (lsb) */
	unsigned gd_selector:16;	/* gate segment selector */
	unsigned gd_stkcpy:5;		/* number of stack wds to cpy */
	unsigned gd_xx:3;		/* unused */
	unsigned gd_type:5;		/* segment type */
	unsigned gd_dpl:2;		/* segment descriptor priority level */
	unsigned gd_p:1;		/* segment descriptor present */
	unsigned gd_hioffset:16;	/* gate offset (msb) */
} __packed;

/*
 * Generic descriptor
 */
union descriptor {
	struct segment_descriptor sd;
	struct gate_descriptor gd;
};
#else
/*
 * Gate descriptors (e.g. indirect descriptors, trap, interrupt etc. 128 bit)
 * Only interrupt and trap gates have gd_ist.
 */
struct gate_descriptor {
	uint64_t gd_looffset:16;	/* gate offset (lsb) */
	uint64_t gd_selector:16;	/* gate segment selector */
	uint64_t gd_ist:3;		/* IST table index */
	uint64_t gd_xx:5;		/* unused */
	uint64_t gd_type:5;		/* segment type */
	uint64_t gd_dpl:2;		/* segment descriptor priority level */
	uint64_t gd_p:1;		/* segment descriptor present */
	uint64_t gd_hioffset:48;	/* gate offset (msb) */
	uint64_t sd_xx1:32;
} __packed;

/*
 * Generic descriptor
 */
union descriptor {
	struct user_segment_descriptor sd;
	struct gate_descriptor gd;
};
#endif

	/* system segments and gate types */
#define	SDT_SYSNULL	 0	/* system null */
#define	SDT_SYS286TSS	 1	/* system 286 TSS available */
#define	SDT_SYSLDT	 2	/* system local descriptor table */
#define	SDT_SYS286BSY	 3	/* system 286 TSS busy */
#define	SDT_SYS286CGT	 4	/* system 286 call gate */
#define	SDT_SYSTASKGT	 5	/* system task gate */
#define	SDT_SYS286IGT	 6	/* system 286 interrupt gate */
#define	SDT_SYS286TGT	 7	/* system 286 trap gate */
#define	SDT_SYSNULL2	 8	/* system null again */
#define	SDT_SYS386TSS	 9	/* system 386 TSS available */
#define	SDT_SYSTSS	 9	/* system available 64 bit TSS */
#define	SDT_SYSNULL3	10	/* system null again */
#define	SDT_SYS386BSY	11	/* system 386 TSS busy */
#define	SDT_SYSBSY	11	/* system busy 64 bit TSS */
#define	SDT_SYS386CGT	12	/* system 386 call gate */
#define	SDT_SYSCGT	12	/* system 64 bit call gate */
#define	SDT_SYSNULL4	13	/* system null again */
#define	SDT_SYS386IGT	14	/* system 386 interrupt gate */
#define	SDT_SYSIGT	14	/* system 64 bit interrupt gate */
#define	SDT_SYS386TGT	15	/* system 386 trap gate */
#define	SDT_SYSTGT	15	/* system 64 bit trap gate */

	/* memory segment types */
#define	SDT_MEMRO	16	/* memory read only */
#define	SDT_MEMROA	17	/* memory read only accessed */
#define	SDT_MEMRW	18	/* memory read write */
#define	SDT_MEMRWA	19	/* memory read write accessed */
#define	SDT_MEMROD	20	/* memory read only expand dwn limit */
#define	SDT_MEMRODA	21	/* memory read only expand dwn limit accessed */
#define	SDT_MEMRWD	22	/* memory read write expand dwn limit */
#define	SDT_MEMRWDA	23	/* memory read write expand dwn limit accessed*/
#define	SDT_MEME	24	/* memory execute only */
#define	SDT_MEMEA	25	/* memory execute only accessed */
#define	SDT_MEMER	26	/* memory execute read */
#define	SDT_MEMERA	27	/* memory execute read accessed */
#define	SDT_MEMEC	28	/* memory execute only conforming */
#define	SDT_MEMEAC	29	/* memory execute only accessed conforming */
#define	SDT_MEMERC	30	/* memory execute read conforming */
#define	SDT_MEMERAC	31	/* memory execute read accessed conforming */

/*
 * Size of IDT table
 */
#define	NIDT		256	/* 32 reserved, 0x80 syscall, most are h/w */
#define	NRSVIDT		32	/* reserved entries for cpu exceptions */

/*
 * Entries in the Interrupt Descriptor Table (IDT)
 */
#define	IDT_DE		0	/* #DE: Divide Error */
#define	IDT_DB		1	/* #DB: Debug */
#define	IDT_NMI		2	/* Nonmaskable External Interrupt */
#define	IDT_BP		3	/* #BP: Breakpoint */
#define	IDT_OF		4	/* #OF: Overflow */
#define	IDT_BR		5	/* #BR: Bound Range Exceeded */
#define	IDT_UD		6	/* #UD: Undefined/Invalid Opcode */
#define	IDT_NM		7	/* #NM: No Math Coprocessor */
#define	IDT_DF		8	/* #DF: Double Fault */
#define	IDT_FPUGP	9	/* Coprocessor Segment Overrun */
#define	IDT_TS		10	/* #TS: Invalid TSS */
#define	IDT_NP		11	/* #NP: Segment Not Present */
#define	IDT_SS		12	/* #SS: Stack Segment Fault */
#define	IDT_GP		13	/* #GP: General Protection Fault */
#define	IDT_PF		14	/* #PF: Page Fault */
#define	IDT_MF		16	/* #MF: FPU Floating-Point Error */
#define	IDT_AC		17	/* #AC: Alignment Check */
#define	IDT_MC		18	/* #MC: Machine Check */
#define	IDT_XF		19	/* #XF: SIMD Floating-Point Exception */
#define	IDT_IO_INTS	NRSVIDT	/* Base of IDT entries for I/O interrupts. */
#define	IDT_SYSCALL	0x80	/* System Call Interrupt Vector */
#define	IDT_DTRACE_RET	0x92	/* DTrace pid provider Interrupt Vector */
#define	IDT_EVTCHN	0x93	/* Xen HVM Event Channel Interrupt Vector */

#if defined(__i386__)
/*
 * Entries in the Global Descriptor Table (GDT)
 * Note that each 4 entries share a single 32 byte L1 cache line.
 * Some of the fast syscall instructions require a specific order here.
 */
#define	GNULL_SEL	0	/* Null Descriptor */
#define	GPRIV_SEL	1	/* SMP Per-Processor Private Data */
#define	GUFS_SEL	2	/* User %fs Descriptor (order critical: 1) */
#define	GUGS_SEL	3	/* User %gs Descriptor (order critical: 2) */
#define	GCODE_SEL	4	/* Kernel Code Descriptor (order critical: 1) */
#define	GDATA_SEL	5	/* Kernel Data Descriptor (order critical: 2) */
#define	GUCODE_SEL	6	/* User Code Descriptor (order critical: 3) */
#define	GUDATA_SEL	7	/* User Data Descriptor (order critical: 4) */
#define	GBIOSLOWMEM_SEL	8	/* BIOS low memory access (must be entry 8) */
#define	GPROC0_SEL	9	/* Task state process slot zero and up */
#define	GLDT_SEL	10	/* Default User LDT */
#define	GUSERLDT_SEL	11	/* User LDT */
#define	GPANIC_SEL	12	/* Task state to consider panic from */
#define	GBIOSCODE32_SEL	13	/* BIOS interface (32bit Code) */
#define	GBIOSCODE16_SEL	14	/* BIOS interface (16bit Code) */
#define	GBIOSDATA_SEL	15	/* BIOS interface (Data) */
#define	GBIOSUTIL_SEL	16	/* BIOS interface (Utility) */
#define	GBIOSARGS_SEL	17	/* BIOS interface (Arguments) */
#define	GNDIS_SEL	18	/* For the NDIS layer */
#define	NGDT		19

/*
 * Entries in the Local Descriptor Table (LDT)
 */
#define	LSYS5CALLS_SEL	0	/* forced by intel BCS */
#define	LSYS5SIGR_SEL	1
#define	LUCODE_SEL	3
#define	LUDATA_SEL	5
#define	NLDT		(LUDATA_SEL + 1)

#else /* !__i386__ */
/*
 * Entries in the Global Descriptor Table (GDT)
 */
#define	GNULL_SEL	0	/* Null Descriptor */
#define	GNULL2_SEL	1	/* Null Descriptor */
#define	GUFS32_SEL	2	/* User 32 bit %fs Descriptor */
#define	GUGS32_SEL	3	/* User 32 bit %gs Descriptor */
#define	GCODE_SEL	4	/* Kernel Code Descriptor */
#define	GDATA_SEL	5	/* Kernel Data Descriptor */
#define	GUCODE32_SEL	6	/* User 32 bit code Descriptor */
#define	GUDATA_SEL	7	/* User 32/64 bit Data Descriptor */
#define	GUCODE_SEL	8	/* User 64 bit Code Descriptor */
#define	GPROC0_SEL	9	/* TSS for entering kernel etc */
/* slot 10 is second half of GPROC0_SEL */
#define	GUSERLDT_SEL	11	/* LDT */
/* slot 12 is second half of GUSERLDT_SEL */
#define	NGDT 		13
#endif /* __i386__ */

#endif /* !_X86_SEGMENTS_H_ */
