/*	$OpenBSD: umct.h,v 1.6 2025/07/06 01:54:12 jsg Exp $	*/
/*	$NetBSD: umct.h,v 1.1 2001/03/28 18:42:13 ichiro Exp $	*/
/*
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Ichiro FUKUHARA (ichiro@ichiro.org).
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
 * Vendor Request Interface
 */
#define UMCT_SET_REQUEST	0x40
#define UMCT_GET_REQUEST	0xc0

#define REQ_SET_BAUD_RATE	5	/* Set Baud Rate Divisor */
#define LENGTH_BAUD_RATE	4

#define REQ_GET_MSR		2	/* Get Modem Status Register (MSR) */
#define LENGTH_GET_MSR		1

#define REQ_GET_LCR		6	/* Get Line Control Register (LCR) */
#define LENGTH_GET_LCR		1

#define REQ_SET_LCR		7	/* Set Line Control Register (LCR) */
#define LENGTH_SET_LCR		1

#define REQ_SET_MCR		10	/* Set Modem Control Register (MCR) */
#define LENGTH_SET_MCR		1

#define REQ_UNKNOWN1		11	/* Unknown functionality */
#define LENGTH_UNKNOWN1		1

#define REQ_SET_CTS		12	/* Apparently controls CTS */
#define LENGTH_SET_CTS		1

/*
 * Baud rate (divisor)
 */
#define UMCT_BAUD_RATE(b)	(115200/b)

/*
 * Line Control Register (LCR)
 */
#define LCR_SET_BREAK		0x40
#define LCR_PARITY_EVEN		0x18
#define LCR_PARITY_ODD		0x08
#define LCR_PARITY_NONE		0x00
#define LCR_DATA_BITS_5		0x00
#define LCR_DATA_BITS_6		0x01
#define LCR_DATA_BITS_7		0x02
#define LCR_DATA_BITS_8		0x03
#define LCR_STOP_BITS_2		0x04
#define LCR_STOP_BITS_1		0x00

/*
 * Modem Control Register (MCR)
 */
#define MCR_NONE		0x8
#define MCR_RTS			0xa
#define MCR_DTR			0x9

/*
 * Modem Status Register (MSR)
 */
#define MSR_CD			0x80	/* Current CD */
#define MSR_RI			0x40	/* Current RI */
#define MSR_DSR			0x20	/* Current DSR */
#define MSR_CTS			0x10	/* Current CTS */
#define MSR_DCD			0x08	/* Delta CD */
#define MSR_DRI			0x04	/* Delta RI */
#define MSR_DDSR		0x02	/* Delta DSR */
#define MSR_DCTS		0x01	/* Delta CTS */

/*
 * Line Status Register (LSR)
 */
#define LSR_ERR			0x80	/* OE | PE | FE | BI */
#define LSR_TEMT		0x40	/* transmit register empty */
#define LSR_THRE		0x20	/* transmit holding register empty */
#define LSR_BI			0x10	/* break indicator */
#define LSR_FE			0x08	/* framing error */
#define LSR_PE			0x04	/* parity error */
#define LSR_OE			0x02	/* overrun error */
#define LSR_DR			0x01	/* receive data ready */
