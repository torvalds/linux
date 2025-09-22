/*	$OpenBSD: segments.h,v 1.18 2025/06/27 17:23:49 bluhm Exp $	*/
/*	$NetBSD: segments.h,v 1.1 2003/04/26 18:39:47 fvdl Exp $	*/

/*-
 * Copyright (c) 1995, 1997
 *	Charles M. Hannum.  All rights reserved.
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
 *	@(#)segments.h	7.1 (Berkeley) 5/9/91
 */

/*
 * Adapted for NetBSD/amd64 by fvdl@wasabisystems.com.
 */

/*
 * 386 Segmentation Data Structures and definitions
 *	William F. Jolitz (william@ernie.berkeley.edu) 6/20/1989
 */

#ifndef _MACHINE_SEGMENTS_H_
#define _MACHINE_SEGMENTS_H_

/*
 * Selectors
 */

#define	ISPL(s)		((s) & SEL_RPL)	/* what is the priority level of a selector */
#define	SEL_KPL		0		/* kernel privilege level */	
#define	SEL_UPL		3		/* user privilege level */	
#define	SEL_RPL		3		/* requester's privilege level mask */
#define	ISLDT(s)	((s) & SEL_LDT)	/* is it local or global */
#define	SEL_LDT		4		/* local descriptor table */	

#define SYSSEL_START	(NGDT_MEM << 3)
#define GDT_SIZE	(SYSSEL_START + (NGDT_SYS << 4))

/*
 * These define the index not from the start of the GDT, but from
 * the part of the GDT that they're allocated from.
 * First NGDT_MEM entries are 8-byte descriptors for CS and DS.
 * Next NGDT_SYS entries are 16-byte descriptors defining TSSs.
 */

#define	IDXSEL(s)	(((s) >> 3) & 0x1fff)
#define IDXDYNSEL(s)	((((s) & ~SEL_RPL) - DYNSEL_START) >> 4)

#define	GSEL(s,r)	(((s) << 3) | r)
#define	GSYSSEL(s,r)	((((s) << 4) + SYSSEL_START) | r)
#define GDYNSEL(s,r)	((((s) << 4) + DYNSEL_START) | r | SEL_KPL)

#define LSEL(s,r)	((s) | r | SEL_LDT)

#define	USERMODE(c, f)		(ISPL(c) == SEL_UPL)
#define	KERNELMODE(c, f)	(ISPL(c) == SEL_KPL)

#ifndef _LOCORE

/*
 * Memory and System segment descriptors
 */

/*
 * Below is used for TSS and LDT.
 */
struct sys_segment_descriptor {
	u_int64_t sd_lolimit:16;	/* segment extent (lsb) */
	u_int64_t sd_lobase:24;		/* segment base address (lsb) */
	u_int64_t sd_type:5;		/* segment type */
	u_int64_t sd_dpl:2;		/* segment descriptor priority level */
	u_int64_t sd_p:1;		/* segment descriptor present */
	u_int64_t sd_hilimit:4;		/* segment extent (msb) */
	u_int64_t sd_xx1:3;		/* avl, long and def32 (not used) */
	u_int64_t sd_gran:1;		/* limit granularity (byte/page) */
	u_int64_t sd_hibase:40;		/* segment base address (msb) */
	u_int64_t sd_xx2:8;		/* reserved */
	u_int64_t sd_zero:5;		/* must be zero */
	u_int64_t sd_xx3:19;		/* reserved */
} __packed;

/*
 * Below is used for cs, ds, etc.
 */
struct mem_segment_descriptor {
	unsigned int sd_lolimit:16;         /* segment extent (lsb) */
	unsigned int sd_lobase:24;          /* segment base address (lsb) */
	unsigned int sd_type:5;             /* segment type */
	unsigned int sd_dpl:2;              /* segment descriptor priority level */
	unsigned int sd_p:1;                /* segment descriptor present */
	unsigned int sd_hilimit:4;          /* segment extent (msb) */
	unsigned int sd_avl:1;		/* available */
	unsigned int sd_long:1;		/* long mode */
	unsigned int sd_def32:1;            /* default 32 vs 16 bit size */
	unsigned int sd_gran:1;             /* limit granularity (byte/page) */
	unsigned int sd_hibase:8;           /* segment base address (msb) */
} __packed;

/*
 * Gate descriptors (e.g. indirect descriptors)
 */
struct gate_descriptor {
	u_int64_t gd_looffset:16;	/* gate offset (lsb) */
	u_int64_t gd_selector:16;	/* gate segment selector */
	u_int64_t gd_ist:3;		/* IST select */
	u_int64_t gd_xx1:5;		/* reserved */
	u_int64_t gd_type:5;		/* segment type */
	u_int64_t gd_dpl:2;		/* segment descriptor priority level */
	u_int64_t gd_p:1;		/* segment descriptor present */
	u_int64_t gd_hioffset:48;	/* gate offset (msb) */
	u_int64_t gd_xx2:8;		/* reserved */
	u_int64_t gd_zero:5;		/* must be zero */
	u_int64_t gd_xx3:19;		/* reserved */
} __packed;

/*
 * region descriptors, used to load gdt/idt tables before segments yet exist.
 */
struct region_descriptor {
	u_int16_t rd_limit;		/* segment extent */
	u_int64_t rd_base;		/* base address  */
} __packed;

#ifdef _KERNEL
extern struct gate_descriptor *idt;
extern struct gate_descriptor early_idt[];

void setgate(struct gate_descriptor *, void *, int, int, int, int);
void unsetgate(struct gate_descriptor *);
void setregion(struct region_descriptor *, void *, u_int16_t);
void set_sys_segment(struct sys_segment_descriptor *, void *, size_t,
			  int, int, int);
void set_mem_segment(struct mem_segment_descriptor *, void *, size_t,
			  int, int, int, int, int);
int idt_vec_alloc(int, int);
int idt_vec_alloc_range(int, int, int);
void idt_vec_set(int, void (*)(void));
void idt_vec_free(int);
void cpu_init_idt(void);

#endif /* _KERNEL */

#endif /* !_LOCORE */

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
#define	SDT_SYSNULL3	10	/* system null again */
#define	SDT_SYS386BSY	11	/* system 386 TSS busy */
#define	SDT_SYS386CGT	12	/* system 386 call gate */
#define	SDT_SYSNULL4	13	/* system null again */
#define	SDT_SYS386IGT	14	/* system 386 interrupt gate */
#define	SDT_SYS386TGT	15	/* system 386 trap gate */

/* memory segment types */
#define	SDT_MEMRO	16	/* memory read only */
#define	SDT_MEMROA	17	/* memory read only accessed */
#define	SDT_MEMRW	18	/* memory read write */
#define	SDT_MEMRWA	19	/* memory read write accessed */
#define	SDT_MEMROD	20	/* memory read only expand dwn limit */
#define	SDT_MEMRODA	21	/* memory read only expand dwn limit accessed */
#define	SDT_MEMRWD	22	/* memory read write expand dwn limit */
#define	SDT_MEMRWDA	23	/* memory read write expand dwn limit acessed */
#define	SDT_MEME	24	/* memory execute only */
#define	SDT_MEMEA	25	/* memory execute only accessed */
#define	SDT_MEMER	26	/* memory execute read */
#define	SDT_MEMERA	27	/* memory execute read accessed */
#define	SDT_MEMEC	28	/* memory execute only conforming */
#define	SDT_MEMEAC	29	/* memory execute only accessed conforming */
#define	SDT_MEMERC	30	/* memory execute read conforming */
#define	SDT_MEMERAC	31	/* memory execute read accessed conforming */

/* is memory segment descriptor pointer ? */
#define ISMEMSDP(s)	((s->d_type) >= SDT_MEMRO && \
			 (s->d_type) <= SDT_MEMERAC)

/* is 286 gate descriptor pointer ? */
#define IS286GDP(s)	((s->d_type) >= SDT_SYS286CGT && \
			 (s->d_type) < SDT_SYS286TGT)

/* is 386 gate descriptor pointer ? */
#define IS386GDP(s)	((s->d_type) >= SDT_SYS386CGT && \
			 (s->d_type) < SDT_SYS386TGT)

/* is gate descriptor pointer ? */
#define ISGDP(s)	(IS286GDP(s) || IS386GDP(s))

/* is segment descriptor pointer ? */
#define ISSDP(s)	(ISMEMSDP(s) || !ISGDP(s))

/* is system segment descriptor pointer ? */
#define ISSYSSDP(s)	(!ISMEMSDP(s) && !ISGDP(s))

/*
 * Segment Protection Exception code bits
 */
#define	SEGEX_EXT	0x01	/* recursive or externally induced */
#define	SEGEX_IDT	0x02	/* interrupt descriptor table */
#define	SEGEX_TI	0x04	/* local descriptor table */

/*
 * Entries in the Interrupt Descriptor Table (IDT)
 */
#define	NIDT	256
#define	NRSVIDT	32		/* reserved entries for cpu exceptions */

/*
 * Entries in the Global Descriptor Table (GDT)
 * The code and data descriptors must come first. There
 * are NGDT_MEM of them.
 *
 * Then comes the predefined TSS descriptor.
 * There are NGDT_SYS of them.
 *
 * The particular order of the UDATA and UCODE descriptors is 
 * required by the sysretq instruction.
 */
#define	GNULL_SEL	0	/* Null descriptor */
#define	GCODE_SEL	1	/* Kernel code descriptor */
#define	GDATA_SEL	2	/* Kernel data descriptor */
#define	GUDATA_SEL	3	/* User data descriptor */
#define	GUCODE_SEL	4	/* User code descriptor */
#define NGDT_MEM 5

#define	GPROC0_SEL	0	/* common TSS */
#define NGDT_SYS	1

#define GDT_SYS_OFFSET	(NGDT_MEM << 3)

#define GDT_ADDR_MEM(s,i)	\
    ((struct mem_segment_descriptor *)((char *)(s) + ((i) << 3)))
#define GDT_ADDR_SYS(s,i)	\
    ((struct sys_segment_descriptor *)((char *)(s) + ((i) << 4) + SYSSEL_START))

/*
 * Checks for valid user selectors.
 */
#define VALID_USER_CSEL(s) \
    ((s) == GSEL(GUCODE_SEL, SEL_UPL))
#define VALID_USER_DSEL(s) \
    ((s) == GSEL(GUDATA_SEL, SEL_UPL))

#endif /* _MACHINE_SEGMENTS_H_ */
