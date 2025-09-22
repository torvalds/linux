/*	$OpenBSD: scifreg.h,v 1.2 2021/03/11 11:17:00 jsg Exp $	*/
/* $NetBSD: scifreg.h,v 1.10 2006/02/18 00:41:32 uwe Exp $ */

/*-
 * Copyright (C) 1999 SAITOH Masanobu.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Serial Communication Interface with FIFO (SCIF)
 */

#define SH3_SCIF0_BASE	0xa4000150
#define SH3_SCIF1_BASE	0xa4000140

#define SH4_SCIF_BASE	0xffe80000

#ifdef SH3

/* SH3 definitions */

#define	SCIF_SMR		0x0	/* serial mode */
#define	SCIF_BRR		0x2	/* bit rate */
#define	SCIF_SCR		0x4	/* serial control */
#define	SCIF_FTDR		0x6	/* transmit fifo data */
#define	SCIF_SSR		0x8	/* serial status */
#define	SCIF_FRDR		0xa	/* receive fifo data */
#define	SCIF_FCR		0xc	/* fifo control */
#define	SCIF_FDR		0xe	/* fifo data count set */

#define	SHREG_SCSMR2  (*(volatile uint8_t  *)(SH3_SCIF0_BASE + SCIF_SMR))
#define	SHREG_SCBRR2  (*(volatile uint8_t  *)(SH3_SCIF0_BASE + SCIF_BRR))
#define	SHREG_SCSCR2  (*(volatile uint8_t  *)(SH3_SCIF0_BASE + SCIF_SCR))
#define	SHREG_SCFTDR2 (*(volatile uint8_t  *)(SH3_SCIF0_BASE + SCIF_FTDR))
#define	SHREG_SCSSR2  (*(volatile uint16_t *)(SH3_SCIF0_BASE + SCIF_SSR))
#define	SHREG_SCFRDR2 (*(volatile uint8_t  *)(SH3_SCIF0_BASE + SCIF_FRDR))
#define	SHREG_SCFCR2  (*(volatile uint8_t  *)(SH3_SCIF0_BASE + SCIF_FCR))
#define	SHREG_SCFDR2  (*(volatile uint16_t *)(SH3_SCIF0_BASE + SCIF_FDR))

#else  /* !SH3 */

/* SH4 definitions */

#define	SCIF_SMR		0x00	/* serial mode */
#define	SCIF_BRR		0x04	/* bit rate */
#define	SCIF_SCR		0x08	/* serial control */
#define	SCIF_FTDR		0x0c	/* transmit fifo data */
#define	SCIF_SSR		0x10	/* serial status */
#define	SCIF_FRDR		0x14	/* receive fifo data */
#define	SCIF_FCR		0x18	/* fifo control */
#define	SCIF_FDR		0x1c	/* fifo data count set */

#define SCIF_SPTR		0x20	/* seial port */
#define SCIF_LSR		0x24	/* line status */

#define	SHREG_SCSMR2  (*(volatile uint16_t *)(SH4_SCIF_BASE + SCIF_SMR))
#define	SHREG_SCBRR2  (*(volatile uint8_t  *)(SH4_SCIF_BASE + SCIF_BRR))
#define	SHREG_SCSCR2  (*(volatile uint16_t *)(SH4_SCIF_BASE + SCIF_SCR))
#define	SHREG_SCFTDR2 (*(volatile uint8_t  *)(SH4_SCIF_BASE + SCIF_FTDR))
#define	SHREG_SCSSR2  (*(volatile uint16_t *)(SH4_SCIF_BASE + SCIF_SSR))
#define	SHREG_SCFRDR2 (*(volatile uint8_t  *)(SH4_SCIF_BASE + SCIF_FRDR))
#define	SHREG_SCFCR2  (*(volatile uint16_t *)(SH4_SCIF_BASE + SCIF_FCR))
#define	SHREG_SCFDR2  (*(volatile uint16_t *)(SH4_SCIF_BASE + SCIF_FDR))

#define	SHREG_SCSPTR2 (*(volatile uint16_t *)(SH4_SCIF_BASE + SCIF_SPTR))
#define	SHREG_SCLSR2  (*(volatile uint16_t *)(SH4_SCIF_BASE + SCIF_LSR))

/* alias */
#define	SHREG_SCSFDR2	SHREG_SCFTDR2
#define	SHREG_SCFSR2	SHREG_SCSSR2

#define	SCSPTR2_RTSIO		0x0080
#define	SCSPTR2_RTSDT		0x0040
#define	SCSPTR2_CTSIO		0x0020
#define	SCSPTR2_CTSDT		0x0010
#define	SCSPTR2_SCKIO		0x0008
#define	SCSPTR2_SCKDT		0x0004
#define	SCSPTR2_SPB2IO		0x0002
#define	SCSPTR2_SPB2DT		0x0001

#define SCLSR2_ORER		0x0001	/* overrun error */

#endif /* !SH3 */

/* SMR: serial mode */
#define SCSMR2_CHR		0x40	/* character width (set = 7bit) */
#define SCSMR2_PE		0x20	/* Parity Enable */
#define SCSMR2_O		0x10	/* parity mode Odd */
#define SCSMR2_STOP		0x08	/* STOP bit (set = 2 stop bits) */
#define	SCSMR2_CKS1		0x02	/* ClocK Select 1 */
#define	SCSMR2_CKS0		0x01	/* ClocK Select 0 */

/* SMR: serial mode (for IrDA) */
#define SCSMR2_IRMOD		0x80	/* IrDA mode */
#define SCSMR2_ICK3		0x40
#define SCSMR2_ICK2		0x20
#define SCSMR2_ICK1		0x10
#define SCSMR2_ICK0		0x08
#define SCSMR2_PSEL		0x04	/* Pulse width SELelect */

/* SCR: serial control */
#define	SCSCR2_TIE		0x80	/* Transmit Interrupt Enable */
#define	SCSCR2_RIE		0x40	/* Receive Interrupt Enable */
#define	SCSCR2_TE		0x20	/* Transmit Enable */
#define	SCSCR2_RE		0x10	/* Receive Enable */
#define	SCSCR2_CKE1		0x02	/* ClocK Enable 1 */
#define	SCSCR2_CKE0		0x01	/* ClocK Enable 0 (not in sh4) */

/* SSR: serial status */
#define	SCSSR2_ER		0x0080	/* ERror */
#define	SCSSR2_TEND		0x0040	/* Transmit END */
#define	SCSSR2_TDFE		0x0020	/* Transmit Data Fifo Empty */
#define	SCSSR2_BRK		0x0010	/* BReaK detection */
#define	SCSSR2_FER		0x0008	/* Framing ERror */
#define	SCSSR2_PER		0x0004	/* Parity ERror */
#define	SCSSR2_RDF		0x0002	/* Receive fifo Data Full */
#define	SCSSR2_DR		0x0001	/* Data Ready */

/* FCR: fifo control */
#define	SCFCR2_RTRG1		0x80	/* Receive TRiGger 1 */
#define	SCFCR2_RTRG0		0x40	/* Receive TRiGger 0 */
#define	SCFCR2_TTRG1		0x20	/* Transmit TRiGger 1 */
#define	SCFCR2_TTRG0		0x10	/* Transmit TRiGger 0 */
#define	SCFCR2_MCE		0x08	/* Modem Control Enable */
#define	SCFCR2_TFRST		0x04	/* Transmit Fifo register ReSeT */
#define	SCFCR2_RFRST		0x02	/* Receive Fifo register ReSeT */
#define	SCFCR2_LOOP		0x01	/* LOOP back test */

#define	FIFO_RCV_TRIGGER_1	0x00
#define	FIFO_RCV_TRIGGER_4	0x40
#define	FIFO_RCV_TRIGGER_8	0x80
#define	FIFO_RCV_TRIGGER_14	0xc0

#define	FIFO_XMT_TRIGGER_8	0x00
#define	FIFO_XMT_TRIGGER_4	0x10
#define	FIFO_XMT_TRIGGER_2	0x20
#define	FIFO_XMT_TRIGGER_1	0x30

/* FDR: fifo data count set */
#define	SCFDR2_TXCNT		0xff00	/* Tx CouNT */
#define	SCFDR2_RECVCNT		0x00ff	/* Rx CouNT */
#define	SCFDR2_TXF_FULL		0x1000	/* Tx FULL */
#define	SCFDR2_RXF_EPTY		0x0000	/* Rx EMPTY */
