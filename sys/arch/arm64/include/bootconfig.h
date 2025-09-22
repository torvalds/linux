/*	$OpenBSD: bootconfig.h,v 1.4 2023/12/05 05:27:26 jsg Exp $	*/
/*	$NetBSD: bootconfig.h,v 1.2 2001/06/21 22:08:28 chris Exp $	*/

/*-
 * Copyright (c) 2013 Andrew Turner <andrew@freebsd.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: head/sys/arm64/include/machdep.h 281494 2015-04-13 14:43:10Z andrew $
 */

#ifndef _MACHINE_BOOTCONFIG_H_
#define	_MACHINE_BOOTCONFIG_H_

struct arm64_bootparams {
	vaddr_t		modulep;
	vaddr_t		kern_l1pt;	/* L1 page table for the kernel */
	uint64_t	kern_delta;
	vaddr_t		kern_stack;
	void		*arg0; // passed to kernel in R0
	void		*arg1; // passed to kernel in R1
	void		*arg2; // passed to kernel in R2
};

void initarm(struct arm64_bootparams *);

#endif /* _MACHINE_BOOTCONFIG_H_ */
