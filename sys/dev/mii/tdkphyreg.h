/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2000,2001 Jonathan Chen.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification, immediately at the beginning of the file.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Register definitions for TDK 78Q2120
 */

#ifndef	_DEV_MII_TDKPHYREG_H_
#define	_DEV_MII_TDKPHYREG_H_

#define	MII_VENDOR	16
#define	VENDOR_RXCC	0x0001
#define	VENDOR_PCSBP	0x0002
#define	VENDOR_RVSPOL	0x0010
#define	VENDOR_NOAPOL	0x0020
#define	VENDOR_GPIO0DIR	0x0040
#define	VENDOR_GPIO0DAT	0x0080
#define	VENDOR_GPIO1DIR	0x0100
#define	VENDOR_GPIO1DAT	0x0200
#define	VENDOR_10BTLOOP	0x0400
#define	VENDOR_NOSQE	0x0800
#define	VENDOR_TXHIM	0x1000
#define	VENDOR_INTLVL	0x4000
#define	VENDOR_RPTR	0x8000

#define	MII_INT		17
#define	INT_STAT_MASK	0x00ff
#define	INT_STAT_ACOMP	0x0001
#define	INT_STAT_RFAULT	0x0002
#define	INT_STAT_LSCHG	0x0004
#define	INT_STAT_LPACK	0x0008
#define	INT_STAT_PDF	0x0010
#define	INT_STAT_PRX	0x0020
#define	INT_STAT_RXERR	0x0040
#define	INT_STAT_JABBER	0x0080
#define	INT_CTRL_MASK	0xff00
#define	INT_CTRL_ACOMP	0x0100
#define	INT_CTRL_RFAULT	0x0200
#define	INT_CTRL_LSCHG	0x0400
#define	INT_CTRL_LPACK	0x0800
#define	INT_CTRL_PDF	0x1000
#define	INT_CTRL_PRX	0x2000
#define	INT_CTRL_RXERR	0x4000
#define	INT_CTRL_JABBER	0x8000


#define	MII_DIAG	18
#define	DIAG_RLOCK	0x0100
#define	DIAG_RPASS	0x0200
#define	DIAG_RATE_100	0x0400
#define	DIAG_DUPLEX	0x0800
#define	DIAG_NEGFAIL	0x1000


#endif
