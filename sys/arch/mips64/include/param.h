/*      $OpenBSD: param.h,v 1.37 2023/12/14 13:26:49 claudio Exp $ */

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and Ralph Campbell.
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

#ifndef	_MIPS64_PARAM_H_
#define	_MIPS64_PARAM_H_

#ifdef _KERNEL
#include <machine/cpu.h>
#endif

#define	PAGE_SIZE	(1 << PAGE_SHIFT)
#define	PAGE_MASK	(PAGE_SIZE - 1)

#ifdef _KERNEL

#ifndef KERNBASE
#define	KERNBASE	0xffffffff80000000L	/* start of kernel virtual */
#endif

#define	NBPG		PAGE_SIZE
#define	PGSHIFT		PAGE_SHIFT
#define	PGOFSET		PAGE_MASK

#define	USPACE		16384
#define	UPAGES		(USPACE >> PAGE_SHIFT)
#if PAGE_SHIFT > 12
#define	USPACE_ALIGN	0
#else
#define	USPACE_ALIGN	(2 * PAGE_SIZE)	/* align to an even TLB boundary */
#endif

#define	NMBCLUSTERS	(64 * 1024)		/* max cluster allocation */

#ifndef MSGBUFSIZE
#if PAGE_SHIFT > 12
#define	MSGBUFSIZE	PAGE_SIZE
#else
#define	MSGBUFSIZE	8192
#endif
#endif

#ifndef _LOCORE
#define	DELAY(n)	delay(n)
void delay(int);
#endif

#endif /* _KERNEL */

#endif /* _MIPS64_PARAM_H_ */
