/*	$OpenBSD: rs5c313reg.h,v 1.2 2008/06/27 06:03:08 ray Exp $	*/
/*	$NetBSD: rs5c313reg.h,v 1.1 2006/09/07 01:12:00 uwe Exp $	*/

/*-
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
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
 * RICOH RS5C313 Real Time Clock
 */
#define	RS5C313_SEC1	0
#define	RS5C313_SEC10	1
#define	RS5C313_MIN1	2
#define	RS5C313_MIN10	3
#define	RS5C313_HOUR1	4
#define	RS5C313_HOUR10	5
#define	RS5C313_WDAY	6
#define	RS5C313_TINT	7
#define	RS5C313_DAY1	8
#define	RS5C313_DAY10	9
#define	RS5C313_MON1	10
#define	RS5C313_MON10	11
#define	RS5C313_YEAR1	12
#define	RS5C313_YEAR10	13
#define	RS5C313_CTRL	14
#define	RS5C313_TEST	15

/* TINT register */
#define	TINT_CT0		0x01
#define	TINT_CT1		0x02
#define	TINT_CT2		0x04
#define	TINT_CT3		0x08

/* CTRL register */
#define	CTRL_BSY		0x01	/* read */
#define	CTRL_ADJ		0x01	/* write */
#define	CTRL_XSTP		0x02	/* read */
#define	CTRL_WTEN		0x02	/* write */
#define	CTRL_24H		0x04	/* read/write */
#define	CTRL_CTFG		0x08	/* read/write */

#define	CTRL_BASE		CTRL_24H
