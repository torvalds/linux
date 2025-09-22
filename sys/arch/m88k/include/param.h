/*	$OpenBSD: param.h,v 1.22 2023/12/14 13:26:49 claudio Exp $ */

/*
 * Copyright (c) 1999 Steve Murphree, Jr.
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1986, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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

#ifndef	_M88K_PARAM_H_
#define	_M88K_PARAM_H_

#ifdef _KERNEL
#ifndef _LOCORE
#include <machine/cpu.h>
#endif	/* _LOCORE */
#endif

#define	_MACHINE_ARCH  m88k
#define	MACHINE_ARCH   "m88k"
#define	MID_MACHINE    MID_M88K

#define	PAGE_SHIFT	12
#define	PAGE_SIZE	(1 << PAGE_SHIFT)
#define	PAGE_MASK	(PAGE_SIZE - 1)

#define	NPTEPG		(PAGE_SIZE / (sizeof(pt_entry_t)))

#ifdef _KERNEL

#define	NBPG		PAGE_SIZE
#define	PGSHIFT		PAGE_SHIFT
#define	PGOFSET		PAGE_MASK

#define	UPAGES		2			/* pages of u-area */
#define	USPACE		(UPAGES * PAGE_SIZE)	/* total size of u-area */
#define	USPACE_ALIGN	0			/* u-area alignment 0-none */

#define	NMBCLUSTERS	(8 * 1024)		/* max cluster allocation */

#ifndef MSGBUFSIZE
#define	MSGBUFSIZE	PAGE_SIZE
#endif

/*
 * Get interrupt glue.
 */
#include <machine/intr.h>

#define	DELAY(x)	delay(x)

#if !defined(_LOCORE)
extern void delay(int);
extern int cputyp;
#endif

/*
 * Values for the cputyp variable.
 */
#define CPU_88100	0x00
#define CPU_88110	0x01

#ifdef M88100
#ifdef M88110
#define	CPU_IS88100	(cputyp == CPU_88100)
#define	CPU_IS88110	(cputyp != CPU_88100)
#else
#define	CPU_IS88100	1
#define	CPU_IS88110	0
#endif
#else
#define	CPU_IS88100	0
#define	CPU_IS88110	1
#endif

#endif	/* _KERNEL */

#endif /* _M88K_PARAM_H_ */
