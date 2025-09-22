/*	$OpenBSD: isareg.h,v 1.5 2025/07/14 10:13:54 jsg Exp $	*/
/*	$NetBSD: isareg.h,v 1.5 1995/04/17 12:09:13 cgd Exp $	*/

/*-
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
 *	@(#)isa.h	5.7 (Berkeley) 5/9/91
 */

/*
 * ISA Bus conventions
 */

/*
 * Input / Output Port Assignments
 */

#ifndef IO_ISABEGIN
#define	IO_ISABEGIN	0x000		/* 0x000 - Beginning of I/O Registers */

		/* CPU Board */
#define	IO_DMA1		0x000		/* 8237A DMA Controller #1 */
#define	IO_ICU1		0x020		/* 8259A Interrupt Controller #1 */
#define	IO_PMP1		0x026		/* 82347 Power Management Peripheral */
#define	IO_TIMER1	0x040		/* 8253 Timer #1 */
#define	IO_TIMER2	0x048		/* 8253 Timer #2 (EISA only) */
#define	IO_KBD		0x060		/* 8042 Keyboard */
#define	IO_PPI		0x061		/* Programmable Peripheral Interface */
#define	IO_RTC		0x070		/* RTC */
#define	IO_NMI		IO_RTC		/* NMI Control */
#define	IO_DMAPG	0x080		/* DMA Page Registers */
#define	IO_ICU2		0x0A0		/* 8259A Interrupt Controller #2 */
#define	IO_DMA2		0x0C0		/* 8237A DMA Controller #2 */
#define	IO_NPX		0x0F0		/* Numeric Coprocessor */

		/* Cards */
					/* 0x100 - 0x16F Open */

#define	IO_WD2		0x170		/* Secondary Fixed Disk Controller */
#define	IO_PMP2		0x178		/* 82347 Power Management Peripheral */

					/* 0x17A - 0x1EF Open */

#define	IO_WD1		0x1f0		/* Primary Fixed Disk Controller */
#define	IO_GAME		0x200		/* Game Controller */

					/* 0x208 - 0x237 Open */

#define	IO_BMS2		0x238		/* secondary InPort Bus Mouse */
#define	IO_BMS1		0x23c		/* primary InPort Bus Mouse */

					/* 0x240 - 0x277 Open */

#define	IO_LPT2		0x278		/* Parallel Port #2 */

					/* 0x280 - 0x2E7 Open */

#define	IO_COM4		0x2e8		/* COM4 i/o address */

					/* 0x2F0 - 0x2F7 Open */

#define	IO_COM2		0x2f8		/* COM2 i/o address */

					/* 0x300 - 0x32F Open */

#define	IO_BT0		0x330		/* bustek 742a default addr. */
#define	IO_AHA0		0x330		/* adaptec 1542 default addr. */
#define	IO_UHA0		0x330		/* ultrastore 14f default addr. */
#define	IO_BT1          0x334		/* bustek 742a default addr. */
#define	IO_AHA1         0x334		/* adaptec 1542 default addr. */

					/* 0x338 - 0x34F Open */

#define	IO_WDS		0x350		/* WD7000 scsi */

					/* 0x354 - 0x36F Open */

#define	IO_FD2		0x370		/* secondary base i/o address */
#define	IO_LPT1		0x378		/* Parallel Port #1 */

					/* 0x380 - 0x3AF Open */

#define	IO_MDA		0x3B0		/* Monochrome Adapter */
#define	IO_LPT3		0x3BC		/* Monochrome Adapter Printer Port */
#define	IO_VGA		0x3C0		/* E/VGA Ports */
#define	IO_CGA		0x3D0		/* CGA Ports */

					/* 0x3E0 - 0x3E7 Open */

#define	IO_COM3		0x3e8		/* COM3 i/o address */
#define	IO_FD1		0x3f0		/* primary base i/o address */
#define	IO_COM1		0x3f8		/* COM1 i/o address */

#define	IO_ISAEND	0x3FF		/* - 0x3FF End of I/O Registers */
#endif /* !IO_ISABEGIN */

/*
 * Input / Output Port Sizes - these are from several sources, and tend
 * to be the larger of what was found, ie COM ports can be 4, but some
 * boards do not fully decode the address, thus 8 ports are used.
 */

#ifndef	IO_ISASIZES
#define	IO_ISASIZES

#define	IO_COMSIZE	8	/* 8250, 16X50 com controllers */
#define	IO_CGASIZE	16	/* CGA controllers */
#define	IO_DMASIZE	16	/* 8237 DMA controllers */
#define	IO_DPGSIZE	32	/* 74LS612 DMA page registers */
#define	IO_FDCSIZE	8	/* Nec765 floppy controllers */
#define	IO_WDCSIZE	8	/* WD compatible disk controller */
#define	IO_GAMSIZE	16	/* AT compatible game controller */
#define	IO_ICUSIZE	16	/* 8259A interrupt controllers */
#define	IO_KBDSIZE	16	/* 8042 Keyboard controllers */
#define	IO_LPTSIZE	8	/* LPT controllers */
#define	IO_MDASIZE	16	/* Monochrome display controller */
#define	IO_RTCSIZE	16	/* CMOS real time clock, NMI con */
#define	IO_TMRSIZE	16	/* 8253 programmable timers */
#define	IO_NPXSIZE	16	/* 80387/80487 NPX registers */
#define	IO_VGASIZE	16	/* VGA controllers */
#define	IO_PMPSIZE	2	/* 82347 Power Management Peripheral */
#endif /* !IO_ISASIZES */

/*
 * Input / Output Memory Physical Addresses
 */

#ifndef	IOM_BEGIN
#define	IOM_BEGIN	0x0a0000		/* Start of I/O Memory "hole" */
#define	IOM_END		0x100000		/* End of I/O Memory "hole" */
#define	IOM_SIZE	(IOM_END - IOM_BEGIN)
#endif /* !IOM_BEGIN */
