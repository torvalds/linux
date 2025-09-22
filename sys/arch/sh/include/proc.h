/*	$OpenBSD: proc.h,v 1.4 2017/04/13 03:52:25 guenther Exp $	*/
/*	$NetBSD: proc.h,v 1.10 2005/12/11 12:18:58 christos Exp $	*/

/*
 * Copyright (c) 2002 The NetBSD Foundation, Inc. All rights reserved.
 * Copyright (c) 1991 Regents of the University of California.
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
 *
 *	@(#)proc.h	7.1 (Berkeley) 5/15/91
 */

#ifndef _SH_PROC_H_
#define	_SH_PROC_H_

/*
 * Machine-dependent part of the proc structure for sh.
 */

#include <machine/param.h>

/* Kernel stack PTE */
struct md_upte {
	uint32_t addr;
	uint32_t data;
};

struct mdproc {
	struct trapframe *md_regs;	/* user context */
	struct pcb *md_pcb;		/* pcb access address */
	volatile int md_astpending;	/* AST pending on return to userland */
	int md_flags;			/* machine-dependent flags */
	/* u-area PTE: *2 .. SH4 data/address data array access */
	struct md_upte md_upte[UPAGES * 2];
};

/* md_flags */
#define	MDP_USEDFPU	0x0001	/* has used the FPU */
#define	MDP_STEP	0x0002	/* single step */

#ifdef _KERNEL
#ifndef _LOCORE
extern void sh_proc0_init(void);
extern struct md_upte *curupte;	/* SH3 wired u-area hack */
#endif /* _LOCORE */
#endif /* _KERNEL */
#endif /* !_SH_PROC_H_ */
