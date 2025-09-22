/*	$OpenBSD: comreg.h,v 1.21 2022/01/11 11:51:14 uaa Exp $	*/
/*	$NetBSD: comreg.h,v 1.8 1996/02/05 23:01:50 scottr Exp $	*/

/*
 * Copyright (c) 1997 - 1998, Jason Downs.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*-
 * Copyright (c) 1991 The Regents of the University of California.
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
 *	@(#)comreg.h	7.2 (Berkeley) 5/9/91
 */

#include <dev/ic/ns16550reg.h>

#ifndef COM_FREQ		/* allow to be set externally */
#define	COM_FREQ	1843200	/* 16-bit baud rate divisor */
#endif
#define	COM_TOLERANCE	30	/* baud rate tolerance, in 0.1% units */

/* interrupt enable register */
#define	IER_ERXRDY	0x1	/* Enable receiver interrupt */
#define	IER_ETXRDY	0x2	/* Enable transmitter empty interrupt */
#define	IER_ERLS	0x4	/* Enable line status interrupt */
#define	IER_EMSC	0x8	/* Enable modem status interrupt */
#define IER_SLEEP	0x10	/* Enable sleep mode */
/* PXA2X0's ns16550 ports have extra bits in this register */
#define	IER_ERXTOUT	0x10	/* Enable rx timeout interrupt */
#define	IER_EUART	0x40	/* Enable UART */

/* interrupt identification register */
#define	IIR_IMASK	0xf
#define	IIR_RXTOUT	0xc
#define	IIR_RLS		0x6	/* Line status change */
#define	IIR_RXRDY	0x4	/* Receiver ready */
#define	IIR_TXRDY	0x2	/* Transmitter ready */
#define	IIR_MLSC	0x0	/* Modem status */
#define	IIR_NOPEND	0x1	/* No pending interrupts */
#define	IIR_FIFO_MASK	0xc0	/* set if FIFOs are enabled */

/* fifo control register */
#define	FIFO_ENABLE	0x01	/* Turn the FIFO on */
#define	FIFO_RCV_RST	0x02	/* Reset RX FIFO */
#define	FIFO_XMT_RST	0x04	/* Reset TX FIFO */
#define	FIFO_DMA_MODE	0x08
#define	FIFO_TRIGGER_1	0x00	/* Trigger RXRDY intr on 1 character */
#define	FIFO_TRIGGER_4	0x40	/* ibid 4 */
#define	FIFO_TRIGGER_8	0x80	/* ibid 8 */
#define	FIFO_TRIGGER_14	0xc0	/* ibid 14 */
/* ST16650 fifo control register */
#define FIFO_RCV_TRIGGER_8	0x00
#define FIFO_RCV_TRIGGER_16	0x40
#define FIFO_RCV_TRIGGER_24	0x80
#define FIFO_RCV_TRIGGER_28	0xc0
#define FIFO_XMT_TRIGGER_16	0x00
#define FIFO_XMT_TRIGGER_8	0x10
#define FIFO_XMT_TRIGGER_24	0x20
#define FIFO_XMT_TRIGGER_30	0x30
/* XR16850 fifo control register */
#define FIFO_RCV3_TRIGGER_8	FIFO_RCV_TRIGGER_8
#define FIFO_RCV3_TRIGGER_16	FIFO_RCV_TRIGGER_16
#define FIFO_RCV3_TRIGGER_56	0x80
#define FIFO_RCV3_TRIGGER_60	0xc0
#define FIFO_XMT3_TRIGGER_8	0x00
#define FIFO_XMT3_TRIGGER_16	0x10
#define FIFO_XMT3_TRIGGER_32	0x20
#define FIFO_XMT3_TRIGGER_56	0x30
/* TI16750 fifo control register */
#define FIFO_ENABLE_64BYTE	0x20

/* line control register */
#define	LCR_DLAB	0x80	/* Divisor latch access enable */
#define	LCR_SBREAK	0x40	/* Break Control */
#define	LCR_PZERO	0x38	/* Space parity */
#define	LCR_PONE	0x28	/* Mark parity */
#define	LCR_PEVEN	0x18	/* Even parity */
#define	LCR_PODD	0x08	/* Odd parity */
#define	LCR_PNONE	0x00	/* No parity */
#define	LCR_PENAB	0x08	/* XXX - low order bit of all parity */
#define	LCR_STOPB	0x04	/* 2 stop bits per serial word */
#define	LCR_8BITS	0x03	/* 8 bits per serial word */
#define	LCR_7BITS	0x02	/* 7 bits */
#define	LCR_6BITS	0x01	/* 6 bits */
#define	LCR_5BITS	0x00	/* 5 bits */
#define LCR_EFR		0xbf	/* ST16650/XR16850/OX16C950 EFR access enable */

/* modem control register */
#define	MCR_AFE		0x20	/* auto flow control */
#define	MCR_LOOPBACK	0x10	/* Loop test: echos from TX to RX */
#define	MCR_IENABLE	0x08	/* Out2: enables UART interrupts */
#define	MCR_DRS		0x04	/* Out1: resets some internal modems */
#define	MCR_RTS		0x02	/* Request To Send */
#define	MCR_DTR		0x01	/* Data Terminal Ready */

/* line status register */
#define	LSR_RCV_FIFO	0x80
#define	LSR_TSRE	0x40	/* Transmitter empty: byte sent */
#define	LSR_TXRDY	0x20	/* Transmitter buffer empty */
#define	LSR_BI		0x10	/* Break detected */
#define	LSR_FE		0x08	/* Framing error: bad stop bit */
#define	LSR_PE		0x04	/* Parity error */
#define	LSR_OE		0x02	/* Overrun, lost incoming byte */
#define	LSR_RXRDY	0x01	/* Byte ready in Receive Buffer */
#define	LSR_RCV_MASK	0x1f	/* Mask for incoming data or error */

/* modem status register */
/* All deltas are from the last read of the MSR. */
#define	MSR_DCD		0x80	/* Current Data Carrier Detect */
#define	MSR_RI		0x40	/* Current Ring Indicator */
#define	MSR_DSR		0x20	/* Current Data Set Ready */
#define	MSR_CTS		0x10	/* Current Clear to Send */
#define	MSR_DDCD	0x08	/* DCD has changed state */
#define	MSR_TERI	0x04	/* RI has toggled low to high */
#define	MSR_DDSR	0x02	/* DSR has changed state */
#define	MSR_DCTS	0x01	/* CTS has changed state */

/* enhanced features register */
#define EFR_ECB		0x10	/* enhanced control bit */
#define EFR_SCD		0x20	/* special character detect */
#define EFR_RTS		0x40	/* RTS flow control */
#define EFR_CTS		0x80	/* CTS flow control */

/* enhanced FIFO control register */
#define FCTL_MODE	0x80
#define FCTL_SWAP	0x40
#define FCTL_RS485	0x08
#define FCTL_IrRxInv	0x04
#define FCTL_TRIGGER2	0x10
#define FCTL_TRIGGER3	0x20

/* infrared selection register */
#define ISR_XMITIR	0x01	/* transmitter SIR enable */
#define ISR_RCVEIR	0x02	/* receiver SIR enable */
#define ISR_XMODE	0x04	/* 1.6us transmit pulse width */
#define ISR_TXPL	0x08	/* negative transmit data polarity */
#define ISR_RXPL	0x10	/* negative receive data polarity */

/* component parameter register (Synopsys DesignWare APB UART) */
#define	CPR_FIFO_MODE(x)	(((x) >> 16) & 0xff)

#define	COM_NPORTS	8

/* Exar XR17V35X */
#define UART_EXAR_INT0	0x80
#define UART_EXAR_SLEEP	0x8b	/* Sleep mode */
#define UART_EXAR_DVID	0x8d	/* Device identification */

/*
 * WARNING: Serial console is assumed to be at COM1 address
 */
#ifndef CONADDR
#define	CONADDR	(0x3f8)
#else
#define CONADDR_OVERRIDE
#endif
