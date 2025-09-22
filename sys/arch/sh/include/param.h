/*	$OpenBSD: param.h,v 1.14 2023/12/14 13:26:49 claudio Exp $	*/

/*-
 * Copyright (c) 2002 The NetBSD Foundation, Inc. All rights reserved.
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
 */

#ifndef	_SH_PARAM_H_
#define	_SH_PARAM_H_

#define	_MACHINE_ARCH	sh
#define	MACHINE_ARCH	"sh"

#ifndef	MID_MACHINE
#define	MID_MACHINE	MID_SH3
#endif

#if defined(_KERNEL) && !defined(_LOCORE)
#include <sh/cpu.h>
#endif

/*
 * We use 4K pages on the sh3/sh4.  Override the PAGE_* definitions
 * to be compile-time constants.
 */
#define	PAGE_SHIFT		12
#define	PAGE_SIZE		(1 << PAGE_SHIFT)
#define	PAGE_MASK		(PAGE_SIZE - 1)

#ifdef _KERNEL

#define	NBPG			PAGE_SIZE
#define	PGSHIFT			PAGE_SHIFT
#define	PGOFSET			PAGE_MASK

#endif

/*
 * u-space.
 */
#define	UPAGES		3			/* pages of u-area */
#define	USPACE		(UPAGES * PAGE_SIZE)	/* total size of u-area */
#define	USPACE_ALIGN	(0)

#if UPAGES == 1
#error "too small u-area"
#elif UPAGES == 2
#define	P1_STACK	/* kernel stack is P1-area */
#else
#undef	P1_STACK	/* kernel stack is P3-area */
#endif

#ifdef _KERNEL

#define	NMBCLUSTERS	(8 * 1024)		/* max cluster allocation */

#ifndef MSGBUFSIZE
#define	MSGBUFSIZE	PAGE_SIZE		/* default message buffer size */
#endif

#endif /* _KERNEL */

#endif /* _SH_PARAM_H_ */
