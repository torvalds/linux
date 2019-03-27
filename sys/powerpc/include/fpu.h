/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	$NetBSD: fpu.h,v 1.2 1999/12/07 15:14:56 danw Exp $
 * $FreeBSD$
 */

#ifndef	_MACHINE_FPU_H_
#define	_MACHINE_FPU_H_

#define	FPSCR_FX	0x80000000
#define	FPSCR_FEX	0x40000000
#define	FPSCR_VX	0x20000000
#define	FPSCR_OX	0x10000000
#define	FPSCR_UX	0x08000000
#define	FPSCR_ZX	0x04000000
#define	FPSCR_XX	0x02000000
#define	FPSCR_VXSNAN	0x01000000
#define	FPSCR_VXISI	0x00800000
#define	FPSCR_VXIDI	0x00400000
#define	FPSCR_VXZDZ	0x00200000
#define	FPSCR_VXIMZ	0x00100000
#define	FPSCR_VXVC	0x00080000
#define	FPSCR_FR	0x00040000
#define	FPSCR_FI	0x00020000
#define	FPSCR_FPRF	0x0001f000
#define	FPSCR_C		0x00010000
#define	FPSCR_FPCC	0x0000f000
#define	FPSCR_FL	0x00008000
#define	FPSCR_FG	0x00004000
#define	FPSCR_FE	0x00002000
#define	FPSCR_FU	0x00001000
#define	FPSCR_VXSOFT	0x00000400
#define	FPSCR_VXSQRT	0x00000200
#define	FPSCR_VXCVI	0x00000100
#define	FPSCR_VE	0x00000080
#define	FPSCR_OE	0x00000040
#define	FPSCR_UE	0x00000020
#define	FPSCR_ZE	0x00000010
#define	FPSCR_XE	0x00000008
#define	FPSCR_NI	0x00000004
#define	FPSCR_RN	0x00000003

#ifdef _KERNEL

void    enable_fpu(struct thread *);
void    save_fpu(struct thread *);
void    save_fpu_nodrop(struct thread *);

#endif /* _KERNEL */

#endif	/* _MACHINE_FPU_H_ */
