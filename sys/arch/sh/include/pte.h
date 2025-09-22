/*	$OpenBSD: pte.h,v 1.2 2008/06/26 05:42:13 ray Exp $	*/
/*	$NetBSD: pte.h,v 1.11 2006/03/04 01:55:03 uwe Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
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

#ifndef _SH_PTE_H_
#define	_SH_PTE_H_

/*
 * OpenBSD/sh PTE format.
 *
 * [Hardware bit]
 * SH3
 *	  PPN   V   PR  SZ C  D  SH
 *	[28:10][8][6:5][4][3][2][1]
 *
 * SH4
 *	        V  SZ  PR  SZ C  D  SH WT
 *	[28:10][8][7][6:5][4][3][2][1][0]
 *
 * [Software bit]
 *   [31]   - PMAP_WIRED bit (not hardware wired entry)
 *   [11:9] - SH4 PCMCIA Assistant bit. (space attribute bit only)
 */

/*
 * Hardware bits
 */
#define	PG_PPN			0x1ffff000	/* Physical page number mask */
#define	PG_V			0x00000100	/* Valid */
#define	PG_PR_MASK		0x00000060	/* Page protection mask */
#define	PG_PR_URW		0x00000060	/* kernel/user read/write */
#define	PG_PR_URO		0x00000040	/* kernel/user read only */
#define	PG_PR_KRW		0x00000020	/* kernel read/write */
#define	PG_PR_KRO		0x00000000	/* kernel read only */
#define	PG_4K			0x00000010	/* page size 4KB */
#define	PG_C			0x00000008	/* Cacheable */
#define	PG_D			0x00000004	/* Dirty */
#define	PG_SH			0x00000002	/* Share status */
#define	PG_WT			0x00000001	/* Write-through (SH4 only) */

#define	PG_HW_BITS		0x1ffff17e	/* [28:12][8][6:1] */

/*
 * Software bits
 */
#define	_PG_WIRED		0x80000000

/* SH4 PCMCIA MMU support bits */
/* PTEA SA (Space Attribute bit) */
#define	_PG_PCMCIA		0x00000e00	/* [11:9] */
#define	_PG_PCMCIA_SHIFT	9
#define	_PG_PCMCIA_NONE		0x00000000	/* Non PCMCIA space */
#define	_PG_PCMCIA_IO		0x00000200	/* IOIS16 signal */
#define	_PG_PCMCIA_IO8		0x00000400	/* 8 bit I/O  */
#define	_PG_PCMCIA_IO16		0x00000600	/* 16 bit I/O  */
#define	_PG_PCMCIA_MEM8		0x00000800	/* 8 bit common memory */
#define	_PG_PCMCIA_MEM16	0x00000a00	/* 16 bit common memory */
#define	_PG_PCMCIA_ATTR8	0x00000c00	/* 8 bit attribute */
#define	_PG_PCMCIA_ATTR16	0x00000e00	/* 16 bit attribute */

#ifndef _LOCORE
typedef uint32_t pt_entry_t;
#endif /* _LOCORE */
#endif /* !_SH_PTE_H_ */
