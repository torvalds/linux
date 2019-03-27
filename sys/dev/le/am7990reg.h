/*	$NetBSD: am7990reg.h,v 1.11 2005/12/11 12:21:25 christos Exp $	*/

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Charles M. Hannum.
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

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
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
 *	@(#)if_lereg.h	8.1 (Berkeley) 6/10/93
 */

/* $FreeBSD$ */

#ifndef _DEV_LE_AM7990REG_H_
#define	_DEV_LE_AM7990REG_H_

/*
 * Receive message descriptor
 */
struct lermd {
	uint16_t rmd0;
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t  rmd1_bits;
	uint8_t  rmd1_hadr;
#else
	uint8_t  rmd1_hadr;
	uint8_t  rmd1_bits;
#endif
	int16_t	 rmd2;
	uint16_t rmd3;
} __packed;

/*
 * Transmit message descriptor
 */
struct letmd {
	uint16_t tmd0;
#if BYTE_ORDER == BIG_ENDIAN
	uint8_t  tmd1_bits;
	uint8_t  tmd1_hadr;
#else
	uint8_t  tmd1_hadr;
	uint8_t  tmd1_bits;
#endif
	int16_t	 tmd2;
	uint16_t tmd3;
} __packed;

/*
 * Initialization block
 */
struct leinit {
	uint16_t init_mode;		/* +0x0000 */
	uint16_t init_padr[3];		/* +0x0002 */
	uint16_t init_ladrf[4];		/* +0x0008 */
	uint16_t init_rdra;		/* +0x0010 */
	uint16_t init_rlen;		/* +0x0012 */
	uint16_t init_tdra;		/* +0x0014 */
	uint16_t init_tlen;		/* +0x0016 */
	int16_t	 pad0[4];		/* Pad to 16 shorts. */
} __packed;

/* Receive message descriptor 1 (rmd1_bits) */
#define	LE_R1_OWN	0x80		/* LANCE owns the packet */
#define	LE_R1_ERR	0x40		/* error summary */
#define	LE_R1_FRAM	0x20		/* framing error */
#define	LE_R1_OFLO	0x10		/* overflow error */
#define	LE_R1_CRC	0x08		/* CRC error */
#define	LE_R1_BUFF	0x04		/* buffer error */
#define	LE_R1_STP	0x02		/* start of packet */
#define	LE_R1_ENP	0x01		/* end of packet */

#define	LE_R1_BITS \
    "\20\10OWN\7ERR\6FRAM\5OFLO\4CRC\3BUFF\2STP\1ENP"

/* Transmit message descriptor 1 (tmd1_bits) */
#define	LE_T1_OWN	0x80		/* LANCE owns the packet */
#define	LE_T1_ERR	0x40		/* error summary */
#define	LE_T1_MORE	0x10		/* multiple collisions */
#define	LE_T1_ONE	0x08		/* single collision */
#define	LE_T1_DEF	0x04		/* deferred transmit */
#define	LE_T1_STP	0x02		/* start of packet */
#define	LE_T1_ENP	0x01		/* end of packet */

#define	LE_T1_BITS \
    "\20\10OWN\7ERR\6RES\5MORE\4ONE\3DEF\2STP\1ENP"

/* Transmit message descriptor 3 (tmd3) */
#define	LE_T3_BUFF	0x8000		/* buffer error */
#define	LE_T3_UFLO	0x4000		/* underflow error */
#define	LE_T3_LCOL	0x1000		/* late collision */
#define	LE_T3_LCAR	0x0800		/* loss of carrier */
#define	LE_T3_RTRY	0x0400		/* retry error */
#define	LE_T3_TDR_MASK	0x03ff		/* time domain reflectometry counter */

#define	LE_XMD2_ONES	0xf000

#define	LE_T3_BITS \
    "\20\20BUFF\17UFLO\16RES\15LCOL\14LCAR\13RTRY"

/*
 * PCnet-ISA defines which are not available on LANCE 7990.
 */

/* (ISA) Bus Configuration Registers */
#define	LE_BCR_MSRDA	0x0000
#define	LE_BCR_MSWRA	0x0001
#define	LE_BCR_MC	0x0002
#define	LE_BCR_LED1	0x0005
#define	LE_BCR_LED2	0x0006
#define	LE_BCR_LED3	0x0007

/* Bus configurations bits (MC) */
#define	LE_MC_EADISEL	0x0008		/* EADI selection */
#define	LE_MC_AWAKE	0x0004		/* auto-wake */
#define	LE_MC_ASEL	0x0002		/* auto selection */
#define	LE_MC_XMAUSEL	0x0001		/* external MAU selection */

/* LED bis (LED[123]) */
#define	LE_LED_LEDOUT	0x8000
#define	LE_LED_PSE	0x0080
#define	LE_LED_XMTE	0x0010
#define	LE_LED_PVPE	0x0008
#define	LE_LED_PCVE	0x0004
#define	LE_LED_JABE	0x0002
#define	LE_LED_COLE	0x0001

#endif /* !_DEV_LE_AM7990REG_H_ */
