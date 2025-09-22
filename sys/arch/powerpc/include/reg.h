/*	$OpenBSD: reg.h,v 1.11 2014/09/08 01:47:06 guenther Exp $	*/

/*-
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
 *
 *	@(#)reg.h	5.5 (Berkeley) 1/18/91
 */

#ifndef _POWERPC_REG_H_
#define _POWERPC_REG_H_

/*
 * Struct reg, used for ptrace and in signal contexts
 * Note that in signal contexts, it's represented as an array.
 * That array has to look exactly like 'struct reg' though.
 */

struct reg {
	u_int32_t gpr[32];
	u_int64_t fpr[32];
	u_int32_t pc;
	u_int32_t ps;
	u_int32_t cnd;
	u_int32_t lr;
	u_int32_t cnt;
	u_int32_t xer;
	u_int32_t mq;
};

struct fpreg {
	u_int64_t fpr[32];
	u_int32_t fpscr;
};

struct vreg {
        u_int32_t vreg[32][4];
	u_int64_t vscr;
	u_int32_t vrsave;
	u_int32_t pad;
};

#ifdef _KERNEL
void save_vec(struct proc *);
void enable_vec(struct proc *);
extern struct proc *ppc_vecproc;
extern struct pool ppc_vecpl;
#endif /* _KERNEL */
#endif /* !_POWERPC_REG_H_ */
