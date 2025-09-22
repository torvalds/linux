/*	$OpenBSD: reg.h,v 1.9 2024/03/29 21:14:31 miod Exp $	*/
/*	$NetBSD: reg.h,v 1.8 2001/06/19 12:59:16 wiz Exp $ */

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)reg.h	8.1 (Berkeley) 6/11/93
 */

#ifndef _MACHINE_REG_H_
#define	_MACHINE_REG_H_

/*
 * The v9 register window.  Each stack pointer (%o6 aka %sp) in each window
 * must ALWAYS point to some place at which it is safe to scribble on
 * 64 bytes.  (If not, your process gets mangled.)  Furthermore, each
 * stack pointer should be aligned on a 16-byte boundary (plus the BIAS)
 * for v9 stacks (the kernel as currently coded allows arbitrary alignment,
 * but with a hefty performance penalty).
 */
struct rwindow {
	int64_t	rw_local[8];		/* %l0..%l7 */
	int64_t	rw_in[8];		/* %i0..%i7 */
};

struct reg {
	int64_t	r_tstate;	/* tstate register */
	int64_t	r_pc;		/* return pc */
	int64_t	r_npc;		/* return npc */
	int	r_y;		/* %y register -- 32-bits */
	int64_t	r_global[8];	/* %g* registers in trap's caller */
	int64_t	r_out[8];	/* %o* registers in trap's caller */
	int64_t r_local[8];	/* %l* registers in trap's caller */
	int64_t r_in[8];	/* %i* registers in trap's caller */
};

/*
 * FP coprocessor registers.
 */

struct fpstate {
	u_int	fs_regs[64];		/* our view is 64 32-bit registers */
	int64_t	fs_fsr;			/* %fsr */
	int	fs_gsr;			/* graphics state reg */
};

/*
 * The actual FP registers are made accessible (c.f. ptrace(2)) through
 * a `struct fpreg'; <arch/sparc64/sparc64/process_machdep.c> relies on the
 * fact that `fpreg' is a prefix of `fpstate'.
 */
struct fpreg {
	u_int	fr_regs[64];		/* our view is 64 32-bit registers */
	int64_t	fr_fsr;			/* %fsr */
	int	fr_gsr;			/* graphics state reg */
};

#endif /* _MACHINE_REG_H_ */
