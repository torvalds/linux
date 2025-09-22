/* 	$OpenBSD: vmparam.h,v 1.8 2023/11/12 16:37:28 kettenis Exp $	*/
/*	$NetBSD: vmparam.h,v 1.23 2003/05/22 05:47:07 thorpej Exp $	*/

/*
 * Copyright (c) 1988 The Regents of the University of California.
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

#ifndef	_MACHINE_VMPARAM_H_
#define	_MACHINE_VMPARAM_H_

#define	ARM_KERNEL_BASE		0xc0000000U

/* Allow armv7 to have bigger limits than generic arm, allow user to override */
#ifndef	MAXDSIZ
#define	MAXDSIZ		(2UL*1024*1024*1024)	/* max data size */
#endif
#ifndef BRKSIZ
#define	BRKSIZ		MAXDSIZ			/* heap gap size */
#endif
#ifndef	DFLSSIZ
#define	DFLSSIZ		(4*1024*1024)		/* initial stack size limit */
#endif
#ifndef	MAXSSIZ
#define	MAXSSIZ		(32*1024*1024)		/* max stack size */
#endif

#include <arm/vmparam.h>

#ifdef _KERNEL
/*
 * Address space constants
 */

/*
 * The line between user space and kernel space
 * Mappings >= KERNEL_BASE are constant across all processes
 */
#define	KERNEL_BASE		ARM_KERNEL_BASE

#define VM_KERNEL_SPACE_SIZE	0x20000000

/*
 * Override the default pager_map size, there's not enough KVA.
 */
#define PAGER_MAP_SIZE		(4 * 1024 * 1024)

/*
 * Size of User Raw I/O map
 */

#define USRIOSIZE       300

/* virtual sizes (bytes) for various kernel submaps */

#define VM_PHYS_SIZE		(USRIOSIZE*PAGE_SIZE)

/*
 * max number of non-contig chunks of physical RAM you can have
 */

#define	VM_PHYSSEG_MAX		32
#define	VM_PHYSSEG_STRAT	VM_PSTRAT_BSEARCH

/*
 * this indicates that we can't add RAM to the VM system after the
 * vm system is init'd.
 */

#define	VM_PHYSSEG_NOADD

#endif /* _KERNEL */

#endif	/* _MACHINE_VMPARAM_H_ */
