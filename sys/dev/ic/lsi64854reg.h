/*	$OpenBSD: lsi64854reg.h,v 1.4 2008/06/26 05:42:15 ray Exp $	*/
/*	$NetBSD: lsi64854reg.h,v 1.5 2001/03/29 02:58:39 petrov Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Paul Kranenburg.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * LSI 64854 DMA engine. Contains three independent channels
 * designed to interface with (a) a NCR539X SCSI controller,
 * (b) a AM7990 Ethernet controller, (c) Parallel port hardware..
 */

/*
 * Register offsets to bus handle.
 */
#define L64854_REG_CSR		0		/* Control bits */
#define L64854_REG_ADDR		4		/* DMA Address */
#define L64854_REG_CNT		8		/* DMA count */
#define L64854_REG_CNT_MASK	0x00ffffff	/*   only 24 bits */
#define L64854_REG_ENBAR	12		/* ENET Base register */
#define L64854_REG_TEST		12		/* SCSI Test register */
#define L64854_REG_HCR		16		/* PP Hardware Configuration */
#define L64854_REG_OCR		18		/* PP Operation Configuration */
#define L64854_REG_DR		20		/* PP Data register */
#define L64854_REG_TCR		21		/* PP Transfer Control */
#define L64854_REG_OR		22		/* PP Output register */
#define L64854_REG_IR		23		/* PP Input register */
#define L64854_REG_ICR		24		/* PP Interrupt Control */


/*
 * Control bits common to all three channels.
 */
#define L64854_INT_PEND	0x00000001	/* Interrupt pending */
#define L64854_ERR_PEND	0x00000002	/* Error pending */
#define L64854_DRAINING	0x0000000c	/* FIFO draining */
#define L64854_INT_EN	0x00000010	/* Interrupt enable */
#define L64854_INVALIDATE	0x00000020	/* Invalidate FIFO */
#define L64854_SLAVE_ERR	0x00000040	/* Slave access size error */
#define L64854_RESET	0x00000080	/* Reset device */
#define L64854_WRITE	0x00000100	/* 1: xfer to memory */
#define L64854_EN_DMA	0x00000200	/* enable DMA transfers */

#define L64854_BURST_SIZE	0x000c0000	/* Read/write burst size */
#define  L64854_BURST_0		0x00080000	/*   no bursts (SCSI-only) */
#define  L64854_BURST_16	0x00000000	/*   16-byte bursts */
#define  L64854_BURST_32    	0x00040000	/*   32-byte bursts */
#define  L64854_BURST_64	0x000c0000	/*   64-byte bursts (fas) */

#define L64854_RST_FAS366	0x08000000	/* FAS366 hardware reset */

#define L64854_DEVID		0xf0000000	/* device ID bits */

/*
 * SCSI DMA control bits.
 */
#define D_INT_PEND	L64854_INT_PEND	/* interrupt pending */
#define D_ERR_PEND	L64854_ERR_PEND	/* error pending */
#define D_DRAINING	L64854_DRAINING	/* fifo draining */
#define D_INT_EN	L64854_INT_EN	/* interrupt enable */
#define D_INVALIDATE	L64854_INVALIDATE/* invalidate fifo */
#define D_SLAVE_ERR	L64854_SLAVE_ERR/* slave access size error */
#define D_RESET		L64854_RESET	/* reset scsi */
#define D_WRITE		L64854_WRITE	/* 1 = dev -> mem */
#define D_EN_DMA	L64854_EN_DMA	/* enable DMA requests */
#define D_EN_CNT	0x00002000	/* enable byte counter */
#define D_TC		0x00004000	/* terminal count */
#define D_WIDE_EN	0x00008000	/* enable wide mode SBUS DMA (fas) */
#define D_DSBL_CSR_DRN	0x00010000	/* disable fifo drain on csr */
#define D_DSBL_SCSI_DRN	0x00020000	/* disable fifo drain on reg */

#define D_DIAG		0x00100000	/* disable fifo drain on addr */
#define D_TWO_CYCLE	0x00200000	/* 2 clocks per transfer */
#define D_FASTER	0x00400000	/* 3 clocks per transfer */
#define D_TCI_DIS	0x00800000	/* disable intr on D_TC */
#define D_EN_NEXT	0x01000000	/* enable auto next address */
#define D_DMA_ON	0x02000000	/* enable dma from scsi XXX */
#define D_DSBL_PARITY_CHK \
			0x02000000	/* disable checking for parity on bus (default 1:fas) */
#define D_A_LOADED	0x04000000	/* address loaded */
#define D_NA_LOADED	0x08000000	/* next address loaded */
#define D_HW_RESET_FAS366 \
			0x08000000	/* hardware reset FAS366 (fas) */
#define D_DEV_ID	L64854_DEVID	/* device ID */
#define  DMAREV_0	0x00000000	/* Sunray DMA */
#define  DMAREV_ESC	0x40000000	/*  DMA ESC array */
#define  DMAREV_1	0x80000000	/* 'DMA' */
#define  DMAREV_PLUS	0x90000000	/* 'DMA+' */
#define  DMAREV_2	0xa0000000	/* 'DMA2' */
#define  DMAREV_HME     0xb0000000 	/* 'HME'  */

/*
 * revisions 0,1 and ESC have different bits.
 */
#define D_ESC_DRAIN	0x00000040	/* rev0,1,esc: drain fifo */
#define D_ESC_R_PEND	0x00000400	/* rev0,1: request pending */
#define D_ESC_BURST	0x00000800	/* DMA ESC: 16 byte bursts */
#define D_ESC_AUTODRAIN	0x00040000	/* DMA ESC: Auto-drain */

#define DDMACSR_BITS	"\020"				\
	"\01INT\02ERR\03DR1\04DR2\05IEN"		\
	"\07SLVERR\010RST\011WRITE\012ENDMA"		\
	"\016ENCNT\017TC\021DSBL_CSR_DRN"		\
	"\022DSBL_SCSI_DRN\026TWOCYCLE"			\
	"\027FASTER\030TCIDIS\031ENNXT\032DMAON"	\
	"\033ALOADED\034NALOADED"


/*
 * ENET DMA control bits.
 */
#define E_INT_PEND	L64854_INT_PEND	/* interrupt pending */
#define E_ERR_PEND	L64854_ERR_PEND	/* error pending */
#define E_DRAINING	L64854_DRAINING	/* fifo draining */
#define E_INT_EN	L64854_INT_EN	/* interrupt enable */
#define E_INVALIDATE	L64854_INVALIDATE/* invalidate fifo */
#define E_SLAVE_ERR	L64854_SLAVE_ERR/* slave access size error */
#define E_RESET		L64854_RESET	/* reset ENET */
#define E_reserved1	0x00000300	/* */
#define E_DRAIN		0x00000400	/* force Ecache drain */
#define E_DSBL_WR_DRN	0x00000800	/* disable Ecache drain on .. */
#define E_DSBL_RD_DRN	0x00001000	/* disable Ecache drain on .. */
#define E_reserved2	0x00006000	/* */
#define E_ILACC		0x00008000	/* ... */
#define E_DSBL_BUF_WR	0x00010000	/* no buffering of slave writes */
#define E_DSBL_WR_INVAL	0x00020000	/* no Ecache invalidate on slave writes */

#define E_reserved3	0x00100000	/* */
#define E_LOOP_TEST	0x00200000	/* loopback mode */
#define E_TP_AUI	0x00400000	/* 1 for TP, 0 for AUI */
#define E_reserved4	0x0c800000	/* */
#define E_DEV_ID	L64854_DEVID	/* ID bits */

#define EDMACSR_BITS	"\020"				\
	"\01INT\02ERR\05IEN"				\
	"\07SLVERR\010RST\011WRITE\013DRAIN"		\
	"\014DSBL_WR_DRN\015DSBL_RD_DRN\020ILACC"	\
	"\021DSBL_BUF_WR\022DSBL_WR_INVAL"		\
	"\026LOOPTEST\027TP"

/*
 * PP DMA control bits.
 */
#define P_INT_PEND	L64854_INT_PEND	/* interrupt pending */
#define P_ERR_PEND	L64854_ERR_PEND	/* error pending */
#define P_DRAINING	L64854_DRAINING	/* fifo draining */
#define P_INT_EN	L64854_INT_EN	/* interrupt enable */
#define P_INVALIDATE	L64854_INVALIDATE/* invalidate fifo */
#define P_SLAVE_ERR	L64854_SLAVE_ERR/* slave access size error */
#define P_RESET		L64854_RESET	/* reset PP */
#define P_WRITE		L64854_WRITE	/* 1: xfer to memory */
#define P_EN_DMA	L64854_EN_DMA	/* enable DMA transfers */
#define P_reserved1	0x00001c00	/* */
#define P_EN_CNT	0x00002000	/* enable counter */
#define P_TC		0x00004000	/* terminal count */
#define P_reserved2	0x00038000	/* */

#define P_DIAG		0x00100000	/* ... */
#define P_reserved3	0x00600000	/* */
#define P_TCI_DIS	0x00800000	/* no interrupt on terminal count */
#define P_EN_NEXT	0x01000000	/* enable DMA chaining */
#define P_DMA_ON	0x02000000	/* DMA xfers enabled */
#define P_A_LOADED	0x04000000	/* addr and byte count valid */
#define P_NA_LOADED	0x08000000	/* next addr & count valid but not used */
#define P_DEV_ID	L64854_DEVID	/* ID bits */

#define PDMACSR_BITS	"\020"				\
	"\01INT\02ERR\05IEN"				\
	"\07SLVERR\010RST\011WRITE\012ENDMA"		\
	"\016ENCNT\017TC\025DIAG\030TCIDIS"		\
	"\031ENNXT\032DMAON\033ALOADED\034NALOADED"
