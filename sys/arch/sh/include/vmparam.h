/*	$OpenBSD: vmparam.h,v 1.10 2014/01/30 18:16:41 miod Exp $	*/
/*	$NetBSD: vmparam.h,v 1.17 2006/03/04 01:55:03 uwe Exp $	*/

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

#ifndef _SH_VMPARAM_H_
#define	_SH_VMPARAM_H_
#include <sys/queue.h>

/* Virtual address map. */
#define	VM_MIN_ADDRESS		((vaddr_t)PAGE_SIZE)
#define	VM_MAXUSER_ADDRESS	((vaddr_t)0x7ffff000)
#define	VM_MAX_ADDRESS		((vaddr_t)0x7ffff000)
#define	VM_MIN_KERNEL_ADDRESS	((vaddr_t)0xc0000000)
#define	VM_MAX_KERNEL_ADDRESS	((vaddr_t)0xe0000000)

/* map PIE below 4MB (non-pie link address) to avoid mmap pressure */
#define VM_PIE_MIN_ADDR		PAGE_SIZE
#define VM_PIE_MAX_ADDR		0x400000UL

/* top of stack */
#define	USRSTACK		VM_MAXUSER_ADDRESS

/* Virtual memory resource limit. */
#define	MAXTSIZ			(64 * 1024 * 1024)	/* max text size */
#ifndef MAXDSIZ
#define	MAXDSIZ			(512 * 1024 * 1024)	/* max data size */
#endif
#ifndef BRKSIZ
#define	BRKSIZ			MAXDSIZ			/* heap gap size */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ			(32 * 1024 * 1024)	/* max stack size */
#endif

/* initial data size limit */
#ifndef DFLDSIZ
#define	DFLDSIZ			(128 * 1024 * 1024)
#endif
/* initial stack size limit */
#ifndef	DFLSSIZ
#define	DFLSSIZ			(2 * 1024 * 1024)
#endif

#define	STACKGAP_RANDOM		(256 * 1024)

/*
 * Size of shared memory map
 */
#ifndef SHMMAXPGS
#define	SHMMAXPGS		1024
#endif

/* Size of user raw I/O map */
#ifndef USRIOSIZE
#define	USRIOSIZE		(MAXBSIZE / PAGE_SIZE * 8)
#endif

#define	VM_PHYS_SIZE		(USRIOSIZE * PAGE_SIZE)

#endif /* !_SH_VMPARAM_H_ */
