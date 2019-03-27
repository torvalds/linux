/*	$FreeBSD$	*/
/*	$OpenBSD: ebusreg.h,v 1.4 2001/10/01 18:08:04 jason Exp $	*/
/*	$NetBSD: ebusreg.h,v 1.8 2008/05/29 14:51:27 mrg Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1999 Matthew R. Green
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * UltraSPARC `ebus'
 *
 * The `ebus' bus is designed to plug traditional PC-ISA devices into
 * an SPARC system with as few costs as possible, without sacrificing
 * to performance.  Typically, it is implemented in the PCIO IC from
 * SME, which also implements a `hme-compatible' PCI network device
 * (`network').  The ebus has 4 DMA channels, similar to the DMA seen
 * in the ESP SCSI DMA.
 *
 * Typical UltraSPARC systems have a NatSemi SuperIO IC to provide
 * serial ports for the keyboard and mouse (`se'), floppy disk
 * controller (`fdthree'), parallel port controller (`bpp') connected
 * to the ebus, and a PCI-IDE controller (connected directly to the
 * PCI bus, of course), as well as a Siemens Nixdorf SAB82532 dual
 * channel serial controller (`su' providing ttya and ttyb), an MK48T59
 * EEPROM/clock controller (also where the idprom, including the
 * ethernet address, is located), the audio system (`SUNW,CS4231', same
 * as other UltraSPARC and some SPARC systems), and other various
 * internal devices found on traditional SPARC systems such as the
 * `power', `flashprom', etc., devices.
 *
 * The ebus uses an interrupt mapping scheme similar to PCI, though
 * the actual structures are different.
 */

/* EBUS dma registers */
#define	EBDMA_DCSR		0x0		/* control/status */
#define	EBDMA_DADDR		0x4		/* DMA address */
#define	EBDMA_DCNT		0x8		/* DMA count */

/* EBUS DMA control/status (EBDMA_DCSR) */
#define	EBDCSR_INT		0x00000001	/* interrupt pending */
#define	EBDCSR_ERR		0x00000002	/* error pending */
#define	EBDCSR_DRAIN		0x00000004	/* drain */
#define	EBDCSR_INTEN		0x00000010	/* interrupt enable */
#define	EBDCSR_RESET		0x00000080	/* reset */
#define	EBDCSR_WRITE		0x00000100	/* write */
#define	EBDCSR_DMAEN		0x00000200	/* dma enable */
#define	EBDCSR_CYC		0x00000400	/* cyc pending */
#define	EBDCSR_DIAGRD		0x00000800	/* diagnostic read done */
#define	EBDCSR_DIAGWR		0x00001000	/* diagnostic write done */
#define	EBDCSR_CNTEN		0x00002000	/* count enable */
#define	EBDCSR_TC		0x00004000	/* terminal count */
#define	EBDCSR_CSRDRNDIS	0x00010000	/* disable csr drain */
#define	EBDCSR_BURSTMASK	0x000c0000	/* burst size mask */
#define	EBDCSR_BURST_1		0x00080000	/* burst 1 */
#define	EBDCSR_BURST_4		0x00000000	/* burst 4 */
#define	EBDCSR_BURST_8		0x00040000	/* burst 8 */
#define	EBDCSR_BURST_16		0x000c0000	/* burst 16 */
#define	EBDCSR_DIAGEN		0x00100000	/* enable diagnostics */
#define	EBDCSR_ERRDIS		0x00400000	/* disable error pending */
#define	EBDCSR_TCIDIS		0x00800000	/* disable TCI */
#define	EBDCSR_NEXTEN		0x01000000	/* enable next */
#define	EBDCSR_DMAON		0x02000000	/* dma on */
#define	EBDCSR_A_LOADED		0x04000000	/* address loaded */
#define	EBDCSR_NA_LOADED	0x08000000	/* next address loaded */
#define	EBDCSR_DEVMASK		0xf0000000	/* device id mask */
