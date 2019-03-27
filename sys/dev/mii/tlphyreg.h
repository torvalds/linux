/*	$NetBSD: tlphyreg.h,v 1.1 1998/08/10 23:59:58 thorpej Exp $	*/
 
/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 1997 Manuel Bouyer.  All rights reserved.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _DEV_MII_TLPHYREG_H_
#define	_DEV_MII_TLPHYREG_H_

/*
 * Registers for the TI ThunderLAN internal PHY.
 */

#define	MII_TLPHY_ID	0x10	/* ThunderLAN PHY ID */
#define	ID_10BASETAUI	0x0001	/* 10baseT/AUI PHY */

#define	MII_TLPHY_CTRL	0x11	/* Control regiseter */
#define	CTRL_ILINK	0x8000	/* Ignore link */
#define	CTRL_SWPOL	0x4000	/* swap polarity */
#define	CTRL_AUISEL	0x2000	/* Select AUI */
#define	CTRL_SQEEN	0x1000	/* Enable SQE */
#define	CTRL_NFEW	0x0004	/* Not far end wrap */
#define	CTRL_INTEN	0x0002	/* Interrupts enable */
#define	CTRL_TINT	0x0001	/* Test Interrupts */

#define	MII_TLPHY_ST	0x12	/* Status register */
#define	ST_MII_INT	0x8000	/* MII interrupt */
#define	ST_PHOK		0x4000	/* Power high OK */
#define	ST_POLOK	0x2000	/* Polarity OK */
#define	ST_TPE		0x1000	/* Twisted pair energy */

#endif /* _DEV_MII_TLPHYREG_H_ */
