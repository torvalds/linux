/*	$OpenBSD: osiopreg.h,v 1.5 2005/11/21 21:52:47 miod Exp $	*/
/*	$NetBSD: osiopreg.h,v 1.1 2001/04/30 04:47:51 tsutsui Exp $	*/

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
 * NCR 53C710 SCSI interface hardware description.
 *
 * From the Mach scsi driver for the 53C710 and amiga siop driver
 */

/* byte lane definitions */
#if BYTE_ORDER == LITTLE_ENDIAN
#define BL0	0
#define BL1	1
#define BL2	2
#define BL3	3
#else
#define BL0	3
#define BL1	2
#define BL2	1
#define BL3	0
#endif

#define OSIOP_SCNTL0	(0x00+BL0)	/* rw: SCSI control reg 0 */
#define OSIOP_SCNTL1	(0x00+BL1)	/* rw: SCSI control reg 1 */
#define OSIOP_SDID	(0x00+BL2)	/* rw: SCSI destination ID */
#define OSIOP_SIEN	(0x00+BL3)	/* rw: SCSI interrupt enable */

#define OSIOP_SCID	(0x04+BL0)	/* rw: SCSI Chip ID reg */
#define OSIOP_SXFER	(0x04+BL1)	/* rw: SCSI Transfer reg */
#define OSIOP_SODL	(0x04+BL2)	/* rw: SCSI Output Data Latch */
#define OSIOP_SOCL	(0x04+BL3)	/* rw: SCSI Output Control Latch */

#define OSIOP_SFBR	(0x08+BL0)	/* ro: SCSI First Byte Received */
#define OSIOP_SIDL	(0x08+BL1)	/* ro: SCSI Input Data Latch */
#define OSIOP_SBDL	(0x08+BL2)	/* ro: SCSI Bus Data Lines */
#define OSIOP_SBCL	(0x08+BL3)	/* rw: SCSI Bus Control Lines */

#define OSIOP_DSTAT	(0x0c+BL0)	/* ro: DMA status */
#define OSIOP_SSTAT0	(0x0c+BL1)	/* ro: SCSI status reg 0 */
#define OSIOP_SSTAT1	(0x0c+BL2)	/* ro: SCSI status reg 1 */
#define OSIOP_SSTAT2	(0x0c+BL3)	/* ro: SCSI status reg 2 */

#define OSIOP_DSA	0x10		/* rw: Data Structure Address */

#define OSIOP_CTEST0	(0x14+BL0)	/* ro: Chip test register 0 */
#define OSIOP_CTEST1	(0x14+BL1)	/* ro: Chip test register 1 */
#define OSIOP_CTEST2	(0x14+BL2)	/* ro: Chip test register 2 */
#define OSIOP_CTEST3	(0x14+BL3)	/* ro: Chip test register 3 */

#define OSIOP_CTEST4	(0x18+BL0)	/* rw: Chip test register 4 */
#define OSIOP_CTEST5	(0x18+BL1)	/* rw: Chip test register 5 */
#define OSIOP_CTEST6	(0x18+BL2)	/* rw: Chip test register 6 */
#define OSIOP_CTEST7	(0x18+BL3)	/* rw: Chip test register 7 */

#define OSIOP_TEMP	0x1c		/* rw: Temporary Stack reg */

#define OSIOP_DFIFO	(0x20+BL0)	/* rw: DMA FIFO */
#define OSIOP_ISTAT	(0x20+BL1)	/* rw: Interrupt Status reg */
#define OSIOP_CTEST8	(0x20+BL2)	/* rw: Chip test register 8 */
#define OSIOP_LCRC	(0x20+BL3)	/* rw: LCRC value */

#define OSIOP_DBC	0x24		/* rw: DMA Counter reg (longword) */
#define OSIOP_DBC0	(0x24+BL0)	/* rw: DMA Byte Counter reg 0 */
#define OSIOP_DBC1	(0x24+BL1)	/* rw: DMA Byte Counter reg 1 */
#define OSIOP_DBC2	(0x24+BL2)	/* rw: DMA Byte Counter reg 2 */
#define OSIOP_DCMD	(0x24+BL3)	/* rw: DMA Command Register */

#define OSIOP_DNAD	0x28		/* rw: DMA Next Data Address */

#define OSIOP_DSP	0x2c		/* rw: DMA SCRIPTS Pointer reg */

#define OSIOP_DSPS	0x30		/* rw: DMA SCRIPTS Pointer Save reg */

#define OSIOP_SCRATCH	0x34		/* rw: Scratch register */

#define OSIOP_DMODE	(0x38+BL0)	/* rw: DMA Mode reg */
#define OSIOP_DIEN	(0x38+BL1)	/* rw: DMA Interrupt Enable */
#define OSIOP_DWT	(0x38+BL2)	/* rw: DMA Watchdog Timer */
#define OSIOP_DCNTL	(0x38+BL3)	/* rw: DMA Control reg */

#define OSIOP_ADDER	0x3c		/* ro: Adder Sum Output */

#define OSIOP_NREGS	0x40


/*
 * Register defines
 */

/* Scsi control register 0 (scntl0) */

#define OSIOP_SCNTL0_ARB	0xc0	/* Arbitration mode */
#define  OSIOP_ARB_SIMPLE	0x00
#define  OSIOP_ARB_FULL		0xc0
#define OSIOP_SCNTL0_START	0x20	/* Start Sequence */
#define OSIOP_SCNTL0_WATN	0x10	/* (Select) With ATN */
#define OSIOP_SCNTL0_EPC	0x08	/* Enable Parity Checking */
#define OSIOP_SCNTL0_EPG	0x04	/* Enable Parity Generation */
#define OSIOP_SCNTL0_AAP	0x02	/* Assert ATN on Parity Error */
#define OSIOP_SCNTL0_TRG	0x01	/* Target Mode */

/* Scsi control register 1 (scntl1) */

#define OSIOP_SCNTL1_EXC	0x80	/* Extra Clock Cycle of data setup */
#define OSIOP_SCNTL1_ADB	0x40	/* Assert Data Bus */
#define OSIOP_SCNTL1_ESR	0x20	/* Enable Selection/Reselection */
#define OSIOP_SCNTL1_CON	0x10	/* Connected */
#define OSIOP_SCNTL1_RST	0x08	/* Assert RST */
#define OSIOP_SCNTL1_AESP	0x04	/* Assert even SCSI parity */
#define OSIOP_SCNTL1_PAR	0x04	/* Force bad Parity */
#define OSIOP_SCNTL1_RES0	0x02	/* Reserved */
#define OSIOP_SCNTL1_RES1	0x01	/* Reserved */

/* Scsi interrupt enable register (sien) */

#define OSIOP_SIEN_M_A		0x80	/* Phase Mismatch or ATN active */
#define OSIOP_SIEN_FCMP		0x40	/* Function Complete */
#define OSIOP_SIEN_STO		0x20	/* (Re)Selection timeout */
#define OSIOP_SIEN_SEL		0x10	/* (Re)Selected */
#define OSIOP_SIEN_SGE		0x08	/* SCSI Gross Error */
#define OSIOP_SIEN_UDC		0x04	/* Unexpected Disconnect */
#define OSIOP_SIEN_RST		0x02	/* RST asserted */
#define OSIOP_SIEN_PAR		0x01	/* Parity Error */

/* Scsi chip ID (scid) */

#define OSIOP_SCID_VALUE(i)	(1 << (i))

/* Scsi transfer register (sxfer) */

#define OSIOP_SXFER_DHP		0x80	/* Disable Halt on Parity error/
					   ATN asserted */
#define OSIOP_SXFER_TP		0x70	/* Synch Transfer Period */
					/* see specs for formulas:
						Period = TCP * (4 + XFERP )
						TCP = 1 + CLK + 1..2;
					 */
#define OSIOP_SXFER_MO		0x0f	/* Synch Max Offset */
#define  OSIOP_MAX_OFFSET	8

/* Scsi output data latch register (sodl) */

/* Scsi output control latch register (socl) */

#define OSIOP_REQ		0x80	/* SCSI signal <x> asserted */
#define OSIOP_ACK		0x40
#define OSIOP_BSY		0x20
#define OSIOP_SEL		0x10
#define OSIOP_ATN		0x08
#define OSIOP_MSG		0x04
#define OSIOP_CD		0x02
#define OSIOP_IO		0x01

#define OSIOP_PHASE(x)		((x) & (OSIOP_MSG|OSIOP_CD|OSIOP_IO))
#define DATA_OUT_PHASE		0x00
#define DATA_IN_PHASE		OSIOP_IO
#define COMMAND_PHASE		OSIOP_CD
#define STATUS_PHASE		(OSIOP_CD|OSIOP_IO)
#define MSG_OUT_PHASE		(OSIOP_MSG|OSIOP_CD)
#define MSG_IN_PHASE		(OSIOP_MSG|OSIOP_CD|OSIOP_IO)

/* Scsi first byte received register (sfbr) */

/* Scsi input data latch register (sidl) */

/* Scsi bus data lines register (sbdl) */

/* Scsi bus control lines register (sbcl).  Same as socl */

#define OSIOP_SBCL_SSCF1	0x02	/* wo */
#define OSIOP_SBCL_SSCF0	0x01	/* wo */

/* DMA status register (dstat) */

#define OSIOP_DSTAT_DFE		0x80	/* DMA FIFO empty */
#define OSIOP_DSTAT_RES		0x40
#define OSIOP_DSTAT_BF		0x20	/* Bus fault */
#define OSIOP_DSTAT_ABRT	0x10	/* Aborted */
#define OSIOP_DSTAT_SSI		0x08	/* SCRIPT Single Step */
#define OSIOP_DSTAT_SIR		0x04	/* SCRIPT Interrupt Instruction */
#define OSIOP_DSTAT_WTD		0x02	/* Watchdog Timeout Detected */
#define OSIOP_DSTAT_IID		0x01	/* Invalid Instruction Detected */

/* Scsi status register 0 (sstat0) */

#define OSIOP_SSTAT0_M_A	0x80	/* Phase Mismatch or ATN active */
#define OSIOP_SSTAT0_FCMP	0x40	/* Function Complete */
#define OSIOP_SSTAT0_STO	0x20	/* (Re)Selection timeout */
#define OSIOP_SSTAT0_SEL	0x10	/* (Re)Selected */
#define OSIOP_SSTAT0_SGE	0x08	/* SCSI Gross Error */
#define OSIOP_SSTAT0_UDC	0x04	/* Unexpected Disconnect */
#define OSIOP_SSTAT0_RST	0x02	/* RST asserted */
#define OSIOP_SSTAT0_PAR	0x01	/* Parity Error */

/* Scsi status register 1 (sstat1) */

#define OSIOP_SSTAT1_ILF	0x80	/* Input latch (sidl) full */
#define OSIOP_SSTAT1_ORF	0x40	/* output reg (sodr) full */
#define OSIOP_SSTAT1_OLF	0x20	/* output latch (sodl) full */
#define OSIOP_SSTAT1_AIP	0x10	/* Arbitration in progress */
#define OSIOP_SSTAT1_LOA	0x08	/* Lost arbitration */
#define OSIOP_SSTAT1_WOA	0x04	/* Won arbitration */
#define OSIOP_SSTAT1_RST	0x02	/* SCSI RST current value */
#define OSIOP_SSTAT1_SDP	0x01	/* SCSI SDP current value */

/* Scsi status register 2 (sstat2) */

#define OSIOP_SSTAT2_FF		0xf0	/* SCSI FIFO flags (bytecount) */
#define  OSIOP_SCSI_FIFO_DEEP	8
#define OSIOP_SSTAT2_SDP	0x08	/* Latched (on REQ) SCSI SDP */
#define OSIOP_SSTAT2_MSG	0x04	/* Latched SCSI phase */
#define OSIOP_SSTAT2_CD		0x02
#define OSIOP_SSTAT2_IO		0x01

/* Chip test register 0 (ctest0) */

#define OSIOP_CTEST0_RES0	0x80
#define OSIOP_CTEST0_BTD	0x40	/* Byte-to-byte Timer Disable */
#define OSIOP_CTEST0_GRP	0x20	/* Generate Receive Parity */
#define OSIOP_CTEST0_EAN	0x10	/* Enable Active Negation */
#define OSIOP_CTEST0_HSC	0x08	/* Halt SCSI clock */
#define OSIOP_CTEST0_ERF	0x04	/* Extend REQ/ACK Filtering */
#define OSIOP_CTEST0_RES1	0x02
#define OSIOP_CTEST0_DDIR	0x01	/* Xfer direction (1-> from SCSI bus) */


/* Chip test register 1 (ctest1) */

#define OSIOP_CTEST1_FMT	0xf0	/* Byte empty in DMA FIFO bottom
					   (high->byte3) */
#define OSIOP_CTEST1_FFL	0x0f	/* Byte full in DMA FIFO top, same */

/* Chip test register 2 (ctest2) */

#define OSIOP_CTEST2_RES	0x80
#define OSIOP_CTEST2_SIGP	0x40	/* Signal process */
#define OSIOP_CTEST2_SOFF	0x20	/* Synch Offset compare
					   (1-> zero Init, max Tgt */
#define OSIOP_CTEST2_SFP	0x10	/* SCSI FIFO Parity */
#define OSIOP_CTEST2_DFP	0x08	/* DMA FIFO Parity */
#define OSIOP_CTEST2_TEOP	0x04	/* True EOP (a-la 5380) */
#define OSIOP_CTEST2_DREQ	0x02	/* DREQ status */
#define OSIOP_CTEST2_DACK	0x01	/* DACK status */

/* Chip test register 3 (ctest3) read-only, top of SCSI FIFO */

/* Chip test register 4 (ctest4) */

#define OSIOP_CTEST4_MUX	0x80	/* Host bus multiplex mode */
#define OSIOP_CTEST4_ZMOD	0x40	/* High-impedance outputs */
#define OSIOP_CTEST4_SZM	0x20	/* ditto, SCSI "outputs" */
#define OSIOP_CTEST4_SLBE	0x10	/* SCSI loopback enable */
#define OSIOP_CTEST4_SFWR	0x08	/* SCSI FIFO write enable (from sodl) */
#define OSIOP_CTEST4_FBL	0x07	/* DMA FIFO Byte Lane select
					   (from ctest6) 4->0, .. 7->3 */

/* Chip test register 5 (ctest5) */

#define OSIOP_CTEST5_ADCK	0x80	/* Clock Address Incrementor */
#define OSIOP_CTEST5_BBCK	0x40	/* Clock Byte counter */
#define OSIOP_CTEST5_ROFF	0x20	/* Reset SCSI offset */
#define OSIOP_CTEST5_MASR	0x10	/* Master set/reset pulses
					   (of bits 3-0) */
#define OSIOP_CTEST5_DDIR	0x08	/* (re)set internal DMA direction */
#define OSIOP_CTEST5_EOP	0x04	/* (re)set internal EOP */
#define OSIOP_CTEST5_DREQ	0x02	/* (re)set internal REQ */
#define OSIOP_CTEST5_DACK	0x01	/* (re)set internal ACK */

/* Chip test register 6 (ctest6)  DMA FIFO access */

/* Chip test register 7 (ctest7) */

#define OSIOP_CTEST7_CDIS	0x80	/* Cache burst disable */
#define OSIOP_CTEST7_SC1	0x40	/* Snoop control 1 */
#define OSIOP_CTEST7_SC0	0x20	/* Snoop control 0 */
#define OSIOP_CTEST7_STD	0x10	/* Selection timeout disable */
#define OSIOP_CTEST7_DFP	0x08	/* DMA FIFO parity bit */
#define OSIOP_CTEST7_EVP	0x04	/* Even parity (to host bus) */
#define OSIOP_CTEST7_TT1	0x02	/* Transfer type bit */
#define OSIOP_CTEST7_DIFF	0x01	/* Differential mode */

/* DMA FIFO register (dfifo) */

#define OSIOP_DFIFO_FLF		0x80	/* Flush (spill) DMA FIFO */
#define OSIOP_DFIFO_BO		0x7f	/* FIFO byte offset counter */

/* Interrupt status register (istat) */

#define OSIOP_ISTAT_ABRT	0x80	/* Abort operation */
#define OSIOP_ISTAT_RST		0x40	/* Software reset */
#define OSIOP_ISTAT_SIGP	0x20	/* Signal process */
#define OSIOP_ISTAT_RES		0x10
#define OSIOP_ISTAT_CON		0x08	/* Connected */
#define OSIOP_ISTAT_RES1	0x04
#define OSIOP_ISTAT_SIP		0x02	/* SCSI Interrupt pending */
#define OSIOP_ISTAT_DIP		0x01	/* DMA Interrupt pending */

/* Chip test register 8 (ctest8) */

#define OSIOP_CTEST8_V		0xf0	/* Chip revision level */
#define OSIOP_CTEST8_FLF	0x08	/* Flush DMA FIFO */
#define OSIOP_CTEST8_CLF	0x04	/* Clear DMA and SCSI FIFOs */
#define OSIOP_CTEST8_FM		0x02	/* Fetch pin mode */
#define OSIOP_CTEST8_SM		0x01	/* Snoop pins mode */

/* DMA Mode register (dmode) */

#define OSIOP_DMODE_BL_MASK	0xc0	/* DMA burst length */
#define  OSIOP_DMODE_BL8	0xc0	/* 8 bytes */
#define  OSIOP_DMODE_BL4	0x80	/* 4 bytes */
#define  OSIOP_DMODE_BL2	0x40	/* 2 bytes */
#define  OSIOP_DMODE_BL1	0x00	/* 1 byte */
#define OSIOP_DMODE_FC		0x30	/* Function code */
#define OSIOP_DMODE_PD		0x08	/* Program/data */
#define OSIOP_DMODE_FAM		0x04	/* fixed address mode */
#define OSIOP_DMODE_U0		0x02	/* User programmable transfer type */
#define OSIOP_DMODE_MAN		0x01	/* SCRIPTS in Manual start mode */

/* DMA interrupt enable register (dien) */

#define OSIOP_DIEN_RES		0xc0
#define OSIOP_DIEN_BF		0x20	/* On Bus Fault */
#define OSIOP_DIEN_ABRT		0x10	/* On Abort */
#define OSIOP_DIEN_SSI		0x08	/* On SCRIPTS sstep */
#define OSIOP_DIEN_SIR		0x04	/* On SCRIPTS intr instruction */
#define OSIOP_DIEN_WTD		0x02	/* On watchdog timeout */
#define OSIOP_DIEN_IID		0x01	/* On illegal instruction detected */

/* DMA control register (dcntl) */

#define OSIOP_DCNTL_CF_MASK	0xc0	/* Clock frequency dividers: */
#define  OSIOP_DCNTL_CF_2	0x00	/*  0 --> 37.51..50.00 MHz, div=2 */
#define  OSIOP_DCNTL_CF_1_5	0x40	/*  1 --> 25.01..37.50 MHz, div=1.5 */
#define  OSIOP_DCNTL_CF_1	0x80	/*  2 --> 16.67..25.00 MHz, div=1 */
#define  OSIOP_DCNTL_CF_3	0xc0	/*  3 --> 50.01..66.67 MHz, div=3 */
#define OSIOP_DCNTL_EA		0x20	/* Enable ACK */
#define OSIOP_DCNTL_SSM		0x10	/* Single step mode */
#define OSIOP_DCNTL_LLM		0x08	/* Enable SCSI Low-level mode */
#define OSIOP_DCNTL_STD		0x04	/* Start DMA operation */
#define OSIOP_DCNTL_FA		0x02	/* Fast arbitration */
#define OSIOP_DCNTL_COM		0x01	/* 53C700 Compatibility */
