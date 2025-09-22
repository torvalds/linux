/*	$OpenBSD: reg.h,v 1.2 2007/03/02 06:11:54 miod Exp $	*/
/*	$NetBSD: reg.h,v 1.5 2005/12/11 12:18:58 christos Exp $	*/

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

#ifndef _SH_REG_H_
#define	_SH_REG_H_

/*
 * Location of the users' stored
 * registers within appropriate frame of 'trap' and 'syscall', relative to
 * base of stack frame.
 *
 * XXX
 * The #defines aren't used in the kernel, but some user-level code still
 * expects them.
 */

/* When referenced during a trap/exception, registers are at these offsets */

#define	tSPC	(0)
#define	tSSR	(1)
#define	tPR	(2)
#define	tR14	(3)
#define	tR13	(4)
#define	tR12	(5)
#define	tR11	(6)
#define	tR10	(7)
#define	tR9	(8)

#define	tR8	(11)
#define	tR7	(12)
#define	tR6	(13)
#define	tR5	(14)
#define	tR4	(15)
#define	tR3	(16)
#define	tR2	(17)
#define	tR1	(18)
#define	tR0	(19)

/*
 * Registers accessible to ptrace(2) syscall for debugger
 * The machine-dependent code for PT_{SET,GET}REGS needs to
 * use whichever order, defined above, is correct, so that it
 * is all invisible to the user.
 */
struct reg {
	int r_spc;
	int r_ssr;
	int r_pr;
	int r_mach;
	int r_macl;
	int r_r15;
	int r_r14;
	int r_r13;
	int r_r12;
	int r_r11;
	int r_r10;
	int r_r9;
	int r_r8;
	int r_r7;
	int r_r6;
	int r_r5;
	int r_r4;
	int r_r3;
	int r_r2;
	int r_r1;
	int r_r0;
};

struct fpreg {
	int	fpr_fr0;
	int	fpr_fr1;
	int	fpr_fr2;
	int	fpr_fr3;
	int	fpr_fr4;
	int	fpr_fr5;
	int	fpr_fr6;
	int	fpr_fr7;
	int	fpr_fr8;
	int	fpr_fr9;
	int	fpr_fr10;
	int	fpr_fr11;
	int	fpr_fr12;
	int	fpr_fr13;
	int	fpr_fr14;
	int	fpr_fr15;
	int	fpr_xf0;
	int	fpr_xf1;
	int	fpr_xf2;
	int	fpr_xf3;
	int	fpr_xf4;
	int	fpr_xf5;
	int	fpr_xf6;
	int	fpr_xf7;
	int	fpr_xf8;
	int	fpr_xf9;
	int	fpr_xf10;
	int	fpr_xf11;
	int	fpr_xf12;
	int	fpr_xf13;
	int	fpr_xf14;
	int	fpr_xf15;
	int	fpr_fpul;
	int	fpr_fpscr;
};

#endif /* !_SH_REG_H_ */
