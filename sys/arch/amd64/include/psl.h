/*	$OpenBSD: psl.h,v 1.5 2018/07/09 19:20:29 guenther Exp $	*/
/*	$NetBSD: psl.h,v 1.1 2003/02/26 21:26:11 fvdl Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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
 *	@(#)psl.h	5.2 (Berkeley) 1/18/91
 */

#ifndef _MACHINE_PSL_H_
#define _MACHINE_PSL_H_

/*
 * 386 processor status longword.
 */
#define	PSL_C		0x00000001	/* carry flag */
#define	PSL_PF		0x00000004	/* parity flag */
#define	PSL_AF		0x00000010	/* auxiliary carry flag */
#define	PSL_Z		0x00000040	/* zero flag */
#define	PSL_N		0x00000080	/* sign flag */
#define	PSL_T		0x00000100	/* trap flag */
#define	PSL_I		0x00000200	/* interrupt enable flag */
#define	PSL_D		0x00000400	/* direction flag */
#define	PSL_V		0x00000800	/* overflow flag */
#define	PSL_IOPL	0x00003000	/* i/o privilege level */
#define	PSL_NT		0x00004000	/* nested task */
#define	PSL_RF		0x00010000	/* resume flag */
#define	PSL_VM		0x00020000	/* virtual 8086 mode */
#define	PSL_AC		0x00040000	/* alignment check flag */
#define	PSL_VIF		0x00080000	/* virtual interrupt enable flag */
#define	PSL_VIP		0x00100000	/* virtual interrupt pending flag */
#define	PSL_ID		0x00200000	/* identification flag */

#define	PSL_MBO		0x00000002	/* must be one bits */
#define	PSL_MBZ		0xffc08028	/* must be zero bits */

#define	PSL_USERSET	(PSL_MBO | PSL_I)
#define	PSL_USERSTATIC	(PSL_MBO | PSL_MBZ | PSL_I | PSL_IOPL | PSL_NT | PSL_VM | PSL_VIF | PSL_VIP)
#define PSL_USER (PSL_C | PSL_MBO | PSL_PF | PSL_AF | PSL_Z | PSL_N | PSL_V)

/*
 * ???
 */
#ifdef _KERNEL
#include <machine/intr.h>
#endif

#endif /* !_MACHINE_PSL_H_ */
