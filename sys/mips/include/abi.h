/*	$NetBSD: asm.h,v 1.29 2000/12/14 21:29:51 jeffs Exp $	*/

/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
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
 *	@(#)machAsmDefs.h	8.1 (Berkeley) 6/10/93
 *	JNPR: asm.h,v 1.10 2007/08/09 11:23:32 katta
 * $FreeBSD$
 */

/*
 * machAsmDefs.h --
 *
 *	Macros used when writing assembler programs.
 *
 *	Copyright (C) 1989 Digital Equipment Corporation.
 *	Permission to use, copy, modify, and distribute this software and
 *	its documentation for any purpose and without fee is hereby granted,
 *	provided that the above copyright notice appears in all copies.
 *	Digital Equipment Corporation makes no representations about the
 *	suitability of this software for any purpose.  It is provided "as is"
 *	without express or implied warranty.
 *
 * from: Header: /sprite/src/kernel/mach/ds3100.md/RCS/machAsmDefs.h,
 *	v 1.2 89/08/15 18:28:24 rab Exp  SPRITE (DECWRL)
 */

#ifndef _MACHINE_ABI_H_
#define	_MACHINE_ABI_H_

#if defined(__mips_o32)
#define	SZREG	4
#else
#define	SZREG	8
#endif

#if defined(__mips_o32) || defined(__mips_o64)
#define	STACK_ALIGN	8
#else
#define	STACK_ALIGN	16
#endif

/*
 *  standard callframe {
 *	register_t cf_pad[N];		o32/64 (N=0), n32 (N=1) n64 (N=1)
 *  	register_t cf_args[4];		arg0 - arg3 (only on o32 and o64)
 *  	register_t cf_gp;		global pointer (only on n32 and n64)
 *  	register_t cf_sp;		frame pointer
 *  	register_t cf_ra;		return address
 *  };
 */
#if defined(__mips_o32) || defined(__mips_o64)
#define	CALLFRAME_SIZ	(SZREG * (4 + 2))
#define	CALLFRAME_S0	0
#elif defined(__mips_n32) || defined(__mips_n64)
#define	CALLFRAME_SIZ	(SZREG * 4)
#define	CALLFRAME_S0	(CALLFRAME_SIZ - 4 * SZREG)
#endif
#ifndef _KERNEL
#define	CALLFRAME_GP	(CALLFRAME_SIZ - 3 * SZREG)
#endif
#define	CALLFRAME_SP	(CALLFRAME_SIZ - 2 * SZREG)
#define	CALLFRAME_RA	(CALLFRAME_SIZ - 1 * SZREG)

#endif /* !_MACHINE_ABI_H_ */
