/*	$OpenBSD: oosiopreg.h,v 1.2 2010/04/20 20:21:56 miod Exp $	*/
/*	$NetBSD: oosiopreg.h,v 1.3 2003/11/02 11:07:45 wiz Exp $	*/

/*
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Van Jacobson of Lawrence Berkeley Laboratory.
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
 *	@(#)siopreg.h	7.3 (Berkeley) 2/5/91
 */

/*
 * NCR 53C700 SCSI interface hardware description.
 *
 * From the Mach scsi driver for the 53C700 and amiga siop driver
 */

#define	OOSIOP_SCNTL0	0x00	/* rw: SCSI control reg 0 */
#define	OOSIOP_SCNTL1	0x01	/* rw: SCSI control reg 1 */
#define	OOSIOP_SDID	0x02	/* rw: SCSI destination ID */
#define	OOSIOP_SIEN	0x03	/* rw: SCSI interrupt enable */
#define	OOSIOP_SCID	0x04	/* rw: SCSI Chip ID reg */
#define	OOSIOP_SXFER	0x05	/* rw: SCSI Transfer reg */
#define	OOSIOP_SODL	0x06	/* rw: SCSI Output Data Latch */
#define	OOSIOP_SOCL	0x07	/* rw: SCSI Output Control Latch */
#define	OOSIOP_SFBR	0x08	/* ro: SCSI First Byte Received */
#define	OOSIOP_SIDL	0x09	/* ro: SCSI Input Data Latch */
#define	OOSIOP_SBDL	0x0a	/* ro: SCSI Bus Data Lines */
#define	OOSIOP_SBCL	0x0b	/* rw: SCSI Bus Control Lines */
#define	OOSIOP_DSTAT	0x0c	/* ro: DMA status */
#define	OOSIOP_SSTAT0	0x0d	/* ro: SCSI status reg 0 */
#define	OOSIOP_SSTAT1	0x0e	/* ro: SCSI status reg 1 */
#define	OOSIOP_SSTAT2	0x0f	/* ro: SCSI status reg 2 */
#define	OOSIOP_SCRA0	0x10	/* rw: Scratch A */
#define	OOSIOP_SCRA1	0x11
#define	OOSIOP_SCRA2	0x12
#define	OOSIOP_SCRA3	0x13
#define	OOSIOP_CTEST0	0x14	/* ro: Chip test register 0 */
#define	OOSIOP_CTEST1	0x15	/* ro: Chip test register 1 */
#define	OOSIOP_CTEST2	0x16	/* ro: Chip test register 2 */
#define	OOSIOP_CTEST3	0x17	/* ro: Chip test register 3 */
#define	OOSIOP_CTEST4	0x18	/* rw: Chip test register 4 */
#define	OOSIOP_CTEST5	0x19	/* rw: Chip test register 5 */
#define	OOSIOP_CTEST6	0x1a	/* rw: Chip test register 6 */
#define	OOSIOP_CTEST7	0x1b	/* rw: Chip test register 7 */
#define	OOSIOP_TEMP	0x1c	/* rw: Temporary Stack reg */
#define	OOSIOP_DFIFO	0x20	/* rw: DMA FIFO */
#define	OOSIOP_ISTAT	0x21	/* rw: Interrupt Status reg */
#define	OOSIOP_CTEST8	0x22	/* rw: Chip test register 8 */
#define	OOSIOP_CTEST9	0x23	/* ro: Chip test register 9 */
#define	OOSIOP_DBC	0x24	/* rw: DMA Byte Counter reg */
#define	OOSIOP_DCMD	0x27	/* rw: DMA Command Register */
#define	OOSIOP_DNAD	0x28	/* rw: DMA Next Address */
#define	OOSIOP_DSP	0x2c	/* rw: DMA SCRIPTS Pointer reg */
#define	OOSIOP_DSPS	0x30	/* rw: DMA SCRIPTS Pointer Save reg */
#define	OOSIOP_DMODE	0x34	/* rw: DMA Mode reg */
#define	OOSIOP_RES35	0x35
#define	OOSIOP_RES36	0x36
#define	OOSIOP_RES37	0x37
#define	OOSIOP_RES38	0x38
#define	OOSIOP_DIEN	0x39	/* rw: DMA Interrupt Enable */
#define	OOSIOP_DWT	0x3a	/* rw: DMA Watchdog Timer */
#define	OOSIOP_DCNTL	0x3b	/* rw: DMA Control reg */
#define	OOSIOP_SCRB0	0x3c	/* rw: Scratch B */
#define	OOSIOP_SCRB1	0x3d
#define	OOSIOP_SCRB2	0x3e
#define	OOSIOP_SCRB3	0x3f

#define	OOSIOP_NREGS	0x40


/*
 * Register defines
 */

/* Scsi control register 0 (scntl0) */

#define	OOSIOP_SCNTL0_ARB	0xc0	/* Arbitration mode */
#define	 OOSIOP_ARB_SIMPLE	0x00
#define	 OOSIOP_ARB_FULL	0xc0
#define	OOSIOP_SCNTL0_START	0x20	/* Start Sequence */
#define	OOSIOP_SCNTL0_WATN	0x10	/* (Select) With ATN */
#define	OOSIOP_SCNTL0_EPC	0x08	/* Enable Parity Checking */
#define	OOSIOP_SCNTL0_EPG	0x04	/* Enable Parity Generation */
#define	OOSIOP_SCNTL0_AAP	0x02	/* Assert ATN on Parity Error */
#define	OOSIOP_SCNTL0_TRG	0x01	/* Target Mode */

/* Scsi control register 1 (scntl1) */

#define	OOSIOP_SCNTL1_EXC	0x80	/* Extra Clock Cycle of data setup */
#define	OOSIOP_SCNTL1_ADB	0x40	/* Assert Data Bus */
#define	OOSIOP_SCNTL1_ESR	0x20	/* Enable Selection/Reselection */
#define	OOSIOP_SCNTL1_CON	0x10	/* Connected */
#define	OOSIOP_SCNTL1_RST	0x08	/* Assert RST */
#define	OOSIOP_SCNTL1_AESP	0x04	/* Assert even SCSI parity */
#define	OOSIOP_SCNTL1_SND	0x02	/* Start Send operation */
#define	OOSIOP_SCNTL1_RCV	0x01	/* Start Receive operation */

/* Scsi interrupt enable register (sien) */

#define	OOSIOP_SIEN_M_A		0x80	/* Phase Mismatch or ATN active */
#define	OOSIOP_SIEN_FC		0x40	/* Function Complete */
#define	OOSIOP_SIEN_STO		0x20	/* (Re)Selection timeout */
#define	OOSIOP_SIEN_SEL		0x10	/* (Re)Selected */
#define	OOSIOP_SIEN_SGE		0x08	/* SCSI Gross Error */
#define	OOSIOP_SIEN_UDC		0x04	/* Unexpected Disconnect */
#define	OOSIOP_SIEN_RST		0x02	/* RST asserted */
#define	OOSIOP_SIEN_PAR		0x01	/* Parity Error */

/* Scsi chip ID (scid) */

#define	OOSIOP_SCID_VALUE(i)	(1 << i)

/* Scsi transfer register (sxfer) */

#define	OOSIOP_SXFER_DHP	0x80	/* Disable Halt on Parity error/
					   ATN asserted */
#define	OOSIOP_SXFER_TP		0x70	/* Synch Transfer Period */
					/* see specs for formulas:
						Period = TCP * (4 + XFERP )
						TCP = 1 + CLK + 1..2;
					 */
#define	OOSIOP_SXFER_MO		0x0f	/* Synch Max Offset */
#define	OOSIOP_MAX_OFFSET	8

/* Scsi output data latch register (sodl) */

/* Scsi output control latch register (socl) */

#define	OOSIOP_REQ		0x80	/* SCSI signal <x> asserted */
#define	OOSIOP_ACK		0x40
#define	OOSIOP_BSY		0x20
#define	OOSIOP_SEL		0x10
#define	OOSIOP_ATN		0x08
#define	OOSIOP_MSG		0x04
#define	OOSIOP_CD		0x02
#define	OOSIOP_IO		0x01

#define	OOSIOP_PHASE(socl)	SCSI_PHASE(socl)

/* Scsi first byte received register (sfbr) */

/* Scsi input data latch register (sidl) */

/* Scsi bus data lines register (sbdl) */

/* Scsi bus control lines register (sbcl).  Same as socl */

#define	OOSIOP_SBCL_SSCF1	0x02	/* wo */
#define	OOSIOP_SBCL_SSCF0	0x01	/* wo */

/* DMA status register (dstat) */

#define	OOSIOP_DSTAT_DFE	0x80	/* DMA FIFO empty */
#define	OOSIOP_DSTAT_ABRT	0x10	/* Aborted */
#define	OOSIOP_DSTAT_SSI	0x08	/* SCRIPT Single Step */
#define	OOSIOP_DSTAT_SIR	0x04	/* SCRIPT Interrupt Instruction */
#define	OOSIOP_DSTAT_WTD	0x02	/* Watchdog Timeout Detected */
#define	OOSIOP_DSTAT_IID	0x01	/* Invalid Instruction Detected */

/* Scsi status register 0 (sstat0) */

#define	OOSIOP_SSTAT0_M_A	0x80	/* Phase Mismatch or ATN active */
#define	OOSIOP_SSTAT0_FC	0x40	/* Function Complete */
#define	OOSIOP_SSTAT0_STO	0x20	/* (Re)Selection timeout */
#define	OOSIOP_SSTAT0_SEL	0x10	/* (Re)Selected */
#define	OOSIOP_SSTAT0_SGE	0x08	/* SCSI Gross Error */
#define	OOSIOP_SSTAT0_UDC	0x04	/* Unexpected Disconnect */
#define	OOSIOP_SSTAT0_RST	0x02	/* RST asserted */
#define	OOSIOP_SSTAT0_PAR	0x01	/* Parity Error */

/* Scsi status register 1 (sstat1) */

#define	OOSIOP_SSTAT1_ILF	0x80	/* Input latch (sidl) full */
#define	OOSIOP_SSTAT1_ORF	0x40	/* output reg (sodr) full */
#define	OOSIOP_SSTAT1_OLF	0x20	/* output latch (sodl) full */
#define	OOSIOP_SSTAT1_AIP	0x10	/* Arbitration in progress */
#define	OOSIOP_SSTAT1_LOA	0x08	/* Lost arbitration */
#define	OOSIOP_SSTAT1_WOA	0x04	/* Won arbitration */
#define	OOSIOP_SSTAT1_RST	0x02	/* SCSI RST current value */
#define	OOSIOP_SSTAT1_SDP	0x01	/* SCSI SDP current value */

/* Scsi status register 2 (sstat2) */

#define	OOSIOP_SSTAT2_FF	0xf0	/* SCSI FIFO flags (bytecount) */
#define	OOSIOP_SCSI_FIFO_DEEP	8
#define	OOSIOP_SSTAT2_SDP	0x08	/* Latched (on REQ) SCSI SDP */
#define	OOSIOP_SSTAT2_MSG	0x04	/* Latched SCSI phase */
#define	OOSIOP_SSTAT2_CD	0x02
#define	OOSIOP_SSTAT2_IO	0x01

/* Chip test register 0 (ctest0) */

#define	OOSIOP_CTEST0_RTRG	0x02	/* Real Target Mode */
#define	OOSIOP_CTEST0_DDIR	0x01	/* Xfer direction (1-> from SCSI bus) */

/* Chip test register 1 (ctest1) */

#define	OOSIOP_CTEST1_FMT	0xf0	/* Byte empty in DMA FIFO bottom
					   (high->byte3) */
#define	OOSIOP_CTEST1_FFL	0x0f	/* Byte full in DMA FIFO top, same */

/* Chip test register 2 (ctest2) */

#define	OOSIOP_CTEST2_SOFF	0x20	/* Synch Offset compare
					   (1-> zero Init, max Tgt) */
#define	OOSIOP_CTEST2_SFP	0x10	/* SCSI FIFO Parity */
#define	OOSIOP_CTEST2_DFP	0x08	/* DMA FIFO Parity */
#define	OOSIOP_CTEST2_TEOP	0x04	/* True EOP (a-la 5380) */
#define	OOSIOP_CTEST2_DREQ	0x02	/* DREQ status */
#define	OOSIOP_CTEST2_DACK	0x01	/* DACK status */

/* Chip test register 3 (ctest3) read-only, top of SCSI FIFO */

/* Chip test register 4 (ctest4) */

#define	OOSIOP_CTEST4_ZMOD	0x40	/* High-impedance outputs */
#define	OOSIOP_CTEST4_SZM	0x20	/* ditto, SCSI "outputs" */
#define	OOSIOP_CTEST4_SLBE	0x10	/* SCSI loopback enable */
#define	OOSIOP_CTEST4_SFWR	0x08	/* SCSI FIFO write enable (from sodl) */
#define	OOSIOP_CTEST4_FBL	0x07	/* DMA FIFO Byte Lane select
					   (from ctest6) 4->0, .. 7->3 */

/* Chip test register 5 (ctest5) */

#define	OOSIOP_CTEST5_ADCK	0x80	/* Clock Address Incrementor */
#define	OOSIOP_CTEST5_BBCK	0x40	/* Clock Byte counter */
#define	OOSIOP_CTEST5_ROFF	0x20	/* Reset SCSI offset */
#define	OOSIOP_CTEST5_MASR	0x10	/* Master set/reset pulses
					   (of bits 3-0) */
#define	OOSIOP_CTEST5_DDIR	0x08	/* (re)set internal DMA direction */
#define	OOSIOP_CTEST5_EOP	0x04	/* (re)set internal EOP */
#define	OOSIOP_CTEST5_DREQ	0x02	/* (re)set internal REQ */
#define	OOSIOP_CTEST5_DACK	0x01	/* (re)set internal ACK */

/* Chip test register 6 (ctest6)  DMA FIFO access */

/* Chip test register 7 (ctest7) */

#define	OOSIOP_CTEST7_STD	0x10	/* Selection timeout disable */
#define	OOSIOP_CTEST7_DFP	0x08	/* DMA FIFO parity bit */
#define	OOSIOP_CTEST7_EVP	0x04	/* Even parity (to host bus) */
#define	OOSIOP_CTEST7_DC	0x02	/* DC output signal low */
#define	OOSIOP_CTEST7_DIFF	0x01	/* Differential mode */

/* DMA FIFO register (dfifo) */

#define	OOSIOP_DFIFO_FLF	0x80	/* Flush (spill) DMA FIFO */
#define	OOSIOP_DFIFO_CLF	0x40	/* Clear DMA and SCSI FIFOs */
#define	OOSIOP_DFIFO_BO		0x3f	/* FIFO byte offset counter */

/* Interrupt status register (istat) */

#define	OOSIOP_ISTAT_ABRT	0x80	/* Abort operation */
#define	OOSIOP_ISTAT_CON	0x08	/* Connected */
#define	OOSIOP_ISTAT_PRE	0x04	/* Pointer register empty */
#define	OOSIOP_ISTAT_SIP	0x02	/* SCSI Interrupt pending */
#define	OOSIOP_ISTAT_DIP	0x01	/* DMA Interrupt pending */

/* Chip test register 8 (ctest8) */

/* DMA Byte Counter register (dbc) */
#define	OOSIOP_DBC_MAX		0x00ffffff

/* DMA Mode register (dmode) */

#define	OOSIOP_DMODE_BL_MASK	0xc0	/* 0->1 1->2 2->4 3->8 */
#define	OOSIOP_DMODE_BL_1	0x00
#define	OOSIOP_DMODE_BL_2	0x40
#define	OOSIOP_DMODE_BL_4	0x80
#define	OOSIOP_DMODE_BL_8	0xc0
#define	OOSIOP_DMODE_BW16	0x20	/* Bus Width is 16 bits */
#define	OOSIOP_DMODE_286	0x10	/* 286 mode */
#define	OOSIOP_DMODE_IO_M	0x08	/* xfer data to memory or I/O space */
#define	OOSIOP_DMODE_FAM	0x04	/* fixed address mode */
#define	OOSIOP_DMODE_PIPE	0x02	/* SCRIPTS in Pipeline mode */
#define	OOSIOP_DMODE_MAN	0x01	/* SCRIPTS in Manual start mode */

/* DMA interrupt enable register (dien) */

#define	OOSIOP_DIEN_BF		0x20	/* On Bus Fault */
#define	OOSIOP_DIEN_ABRT	0x10	/* On Abort */
#define	OOSIOP_DIEN_SSI		0x08	/* On SCRIPTS sstep */
#define	OOSIOP_DIEN_SIR		0x04	/* On SCRIPTS intr instruction */
#define	OOSIOP_DIEN_WTD		0x02	/* On watchdog timeout */
#define	OOSIOP_DIEN_IID		0x01	/* On illegal instruction detected */

/* DMA control register (dcntl) */

#define	OOSIOP_DCNTL_CF_MASK	0xc0	/* Clock frequency dividers: */
#define	OOSIOP_DCNTL_CF_2	0x00	/*  0 --> 37.51..50.00 MHz, div=2 */
#define	OOSIOP_DCNTL_CF_1_5	0x40	/*  1 --> 25.01..37.50 MHz, div=1.5 */
#define	OOSIOP_DCNTL_CF_1	0x80	/*  2 --> 16.67..25.00 MHz, div=1 */
#define	OOSIOP_DCNTL_CF_3	0xc0	/*  3 --> 50.01..66.67 MHz, div=3 */
#define	OOSIOP_DCNTL_S16	0x20	/* SCRIPTS fetches 16bits at a time */
#define	OOSIOP_DCNTL_SSM	0x10	/* Single step mode */
#define	OOSIOP_DCNTL_LLM	0x08	/* Enable SCSI Low-level mode */
#define	OOSIOP_DCNTL_STD	0x04	/* Start DMA operation */
#define	OOSIOP_DCNTL_RST	0x01	/* Software reset */
