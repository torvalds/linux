/*	$OpenBSD: frame.h,v 1.8 2024/11/05 21:47:00 miod Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 */
#ifndef	_POWERPC_FRAME_H_
#define	_POWERPC_FRAME_H_

/*
 * This is to ensure alignment of the stackpointer
 */
#define	FRAMELEN	roundup(sizeof(struct trapframe) + 8, 16)
#define	trapframe(p)	((struct trapframe *)((void *)(p)->p_addr + USPACE - FRAMELEN + 8))

struct switchframe {
	register_t sp;
	int fill;
	int user_sr;
	int cr;
	register_t fixreg2;
	register_t fixreg[19];		/* R13-R31 */
};

struct clockframe {
	register_t srr1;
	register_t srr0;
	int unused;
	int depth;
};

/*
 * Call frame for PowerPC used during fork.
 */
struct callframe {
	register_t sp;
	register_t lr;
	register_t r30;
	register_t r31;
};

struct sigframe {
	int sf_signum;
	siginfo_t *sf_sip;
	struct sigcontext sf_sc;
	siginfo_t sf_si;
};

struct fpsig {
	double f[14]; /* f0 - f13 are volatile */
	double fpscr;
};
#endif	/* _POWERPC_FRAME_H_ */
