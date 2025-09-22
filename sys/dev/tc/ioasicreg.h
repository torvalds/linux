/* $OpenBSD: ioasicreg.h,v 1.3 2017/10/13 08:58:42 mpi Exp $ */
/* $NetBSD: ioasicreg.h,v 1.6 2000/07/17 02:18:17 thorpej Exp $ */

/* 
 * Copyright (c) 1991,1990,1989,1994,1995 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * The Mach Operating System project at Carnegie-Mellon University,
 * Ralph Campbell and Rick Macklem.
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
 *	@(#)asic.h	8.1 (Berkeley) 6/10/93
 */

/*
 * Slot definitions
 */

#define IOASIC_SLOT_0_START		0x000000
#define IOASIC_SLOT_1_START		0x040000
#define IOASIC_SLOT_2_START		0x080000
#define IOASIC_SLOT_3_START		0x0c0000
#define IOASIC_SLOT_4_START		0x100000
#define IOASIC_SLOT_5_START		0x140000
#define IOASIC_SLOT_6_START		0x180000
#define IOASIC_SLOT_7_START		0x1c0000
#define IOASIC_SLOT_8_START		0x200000
#define IOASIC_SLOT_9_START		0x240000
#define IOASIC_SLOT_10_START		0x280000
#define IOASIC_SLOT_11_START		0x2c0000
#define IOASIC_SLOT_12_START		0x300000
#define IOASIC_SLOT_13_START		0x340000
#define IOASIC_SLOT_14_START		0x380000
#define IOASIC_SLOT_15_START		0x3c0000
#define IOASIC_SLOTS_END		0x3fffff

/*
 *  Register offsets (slot 1)
 */

#define IOASIC_SCSI_DMAPTR		IOASIC_SLOT_1_START+0x000
#define IOASIC_SCSI_NEXTPTR		IOASIC_SLOT_1_START+0x010
#define IOASIC_LANCE_DMAPTR		IOASIC_SLOT_1_START+0x020
#define IOASIC_SCC_T1_DMAPTR		IOASIC_SLOT_1_START+0x030
#define IOASIC_SCC_R1_DMAPTR		IOASIC_SLOT_1_START+0x040
#define IOASIC_SCC_T2_DMAPTR		IOASIC_SLOT_1_START+0x050
#define IOASIC_SCC_R2_DMAPTR		IOASIC_SLOT_1_START+0x060
#define IOASIC_FLOPPY_DMAPTR		IOASIC_SLOT_1_START+0x070
#define IOASIC_ISDN_X_DMAPTR		IOASIC_SLOT_1_START+0x080
#define IOASIC_ISDN_X_NEXTPTR		IOASIC_SLOT_1_START+0x090
#define IOASIC_ISDN_R_DMAPTR		IOASIC_SLOT_1_START+0x0a0
#define IOASIC_ISDN_R_NEXTPTR		IOASIC_SLOT_1_START+0x0b0
#define IOASIC_BUFF0			IOASIC_SLOT_1_START+0x0c0
#define IOASIC_BUFF1			IOASIC_SLOT_1_START+0x0d0
#define IOASIC_BUFF2			IOASIC_SLOT_1_START+0x0e0
#define IOASIC_BUFF3			IOASIC_SLOT_1_START+0x0f0
#define IOASIC_CSR			IOASIC_SLOT_1_START+0x100
#define IOASIC_INTR			IOASIC_SLOT_1_START+0x110
#define IOASIC_IMSK			IOASIC_SLOT_1_START+0x120
#define IOASIC_CURADDR			IOASIC_SLOT_1_START+0x130
#define IOASIC_ISDN_X_DATA		IOASIC_SLOT_1_START+0x140
#define IOASIC_ISDN_R_DATA		IOASIC_SLOT_1_START+0x150
#define IOASIC_LANCE_DECODE		IOASIC_SLOT_1_START+0x160
#define IOASIC_SCSI_DECODE		IOASIC_SLOT_1_START+0x170
#define IOASIC_SCC0_DECODE		IOASIC_SLOT_1_START+0x180
#define IOASIC_SCC1_DECODE		IOASIC_SLOT_1_START+0x190
#define IOASIC_FLOPPY_DECODE		IOASIC_SLOT_1_START+0x1a0
#define IOASIC_SCSI_SCR			IOASIC_SLOT_1_START+0x1b0
#define IOASIC_SCSI_SDR0		IOASIC_SLOT_1_START+0x1c0
#define IOASIC_SCSI_SDR1		IOASIC_SLOT_1_START+0x1d0
#define IOASIC_CTR			IOASIC_SLOT_1_START+0x1e0 /*3max+/3000*/

/* System Status and control Register (SSR). */
#define IOASIC_CSR_DMAEN_T1		0x80000000	/* rw */
#define IOASIC_CSR_DMAEN_R1		0x40000000	/* rw */
#define IOASIC_CSR_DMAEN_T2		0x20000000	/* rw */
#define IOASIC_CSR_DMAEN_R2		0x10000000	/* rw */
#define IOASIC_CSR_FASTMODE		0x08000000	/* rw - 3000 */
#define IOASIC_CSR_xxx			0x07800000	/* reserved - 3000 */
#define IOASIC_CSR_DS_xxx		0x0f800000	/* reserved - DS */
#define IOASIC_CSR_FLOPPY_DIR		0x00400000	/* rw - maxine */
#define IOASIC_CSR_DMAEN_FLOPPY		0x00200000	/* rw - maxine */
#define IOASIC_CSR_DMAEN_ISDN_T		0x00100000	/* rw */
#define IOASIC_CSR_DMAEN_ISDN_R		0x00080000	/* rw */
#define IOASIC_CSR_SCSI_DIR		0x00040000	/* rw - DS */
#define IOASIC_CSR_DMAEN_SCSI		0x00020000	/* rw - DS */
#define IOASIC_CSR_DMAEN_LANCE		0x00010000	/* rw - DS */
/* low 16 bits are rw gp outputs */
#define IOASIC_CSR_DIAGDN		0x00008000	/* rw */
#define IOASIC_CSR_TXDIS_2		0x00004000	/* rw - 3min,3max+ */
#define IOASIC_CSR_TXDIS_1		0x00002000	/* rw - 3min,3max+ */
#define IOASIC_CSR_ETHERNET_UTP		0x00002000	/* rw - 3000 but 300 */
#define IOASIC_CSR_ISDN_ENABLE		0x00001000	/* rw - 3000/maxine */
#define IOASIC_CSR_SCC_ENABLE		0x00000800	/* rw */
#define IOASIC_CSR_RTC_ENABLE		0x00000400	/* rw */
#define IOASIC_CSR_SCSI_ENABLE		0x00000200	/* rw - DS */
#define IOASIC_CSR_LANCE_ENABLE		0x00000100	/* rw */

/* System Interrupt Register (and Interrupt Mask Register). */
#define IOASIC_INTR_T1_PAGE_END		0x80000000	/* rz */
#define IOASIC_INTR_T1_READ_E		0x40000000	/* rz */
#define IOASIC_INTR_R1_HALF_PAGE	0x20000000	/* rz */
#define IOASIC_INTR_R1_DMA_OVRUN	0x10000000	/* rz */
#define IOASIC_INTR_T2_PAGE_END		0x08000000	/* rz */
#define IOASIC_INTR_T2_READ_E		0x04000000	/* rz */
#define IOASIC_INTR_R2_HALF_PAGE	0x02000000	/* rz */
#define IOASIC_INTR_R2_DMA_OVRUN	0x01000000	/* rz */
#define IOASIC_INTR_FLOPPY_DMA_E	0x00800000	/* rz - maxine */
#define IOASIC_INTR_ISDN_TXLOAD		0x00400000	/* rz - 3000/maxine */
#define IOASIC_INTR_ISDN_RXLOAD		0x00200000	/* rz - 3000/maxine */
#define IOASIC_INTR_ISDN_OVRUN		0x00100000	/* rz - 3000/maxine */
#define IOASIC_INTR_SCSI_PTR_LOAD	0x00080000	/* rz - DS */
#define IOASIC_INTR_SCSI_OVRUN		0x00040000	/* rz - DS */
#define IOASIC_INTR_SCSI_READ_E		0x00020000	/* rz - DS */
#define IOASIC_INTR_LANCE_READ_E	0x00010000	/* rz - DS */

/* low 16 bits are model-dependent; see also model specific *.h */
#define IOASIC_INTR_NVR_JUMPER		0x00004000	/* ro */
#define IOASIC_INTR_ISDN		0x00002000	/* ro - 3000 */
#define IOASIC_INTR_NRMOD_JUMPER	0x00000400	/* ro */
#define IOASIC_INTR_SEC_CON		0x00000200	/* ro */
#define IOASIC_INTR_SCSI		0x00000200	/* ro - DS */
#define IOASIC_INTR_LANCE		0x00000100	/* ro */
#define IOASIC_INTR_SCC_1		0x00000080	/* ro */
#define IOASIC_INTR_SCC_0		0x00000040	/* ro */
#define IOASIC_INTR_ALT_CON		0x00000008	/* ro - 3000/500 */
#define IOASIC_INTR_300_OPT1		0x00000008	/* ro - 3000/300 */
#define IOASIC_INTR_300_OPT0		0x00000004	/* ro - 3000/300 */

/* DMA pointer registers (SCSI, Comm, ...) */

#define	IOASIC_DMA_ADDR(p) \
    ((((p) << 3) & ~0x1f) | (((p) >> 29) & 0x1f))
#define	IOASIC_DMA_BLOCKSIZE		0x1000

/* For the LANCE DMA pointer register initialization the above suffices */

/* More SCSI DMA registers */

#define IOASIC_SCR_STATUS		0x00000004
#define IOASIC_SCR_WORD			0x00000003

/* Various Decode registers */

#define IOASIC_DECODE_HW_ADDRESS	0x000003f0
#define IOASIC_DECODE_CHIP_SELECT	0x0000000f

/*
 * And slot assignments.
 */
#define IOASIC_SYS_ETHER_ADDRESS(base)	((base) + IOASIC_SLOT_2_START)
#define IOASIC_SYS_LANCE(base)		((base) + IOASIC_SLOT_3_START)
