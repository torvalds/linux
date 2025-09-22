/*	$OpenBSD: reg.h,v 1.7 2019/07/14 05:08:26 guenther Exp $	*/
/*	$NetBSD: reg.h,v 1.1 2003/04/26 18:39:47 fvdl Exp $	*/

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

#ifndef _MACHINE_REG_H_
#define _MACHINE_REG_H_

#include <machine/fpu.h>

/*
 * XXX
 * The #defines aren't used in the kernel, but some user-level code still
 * expects them.
 */

/* When referenced during a trap/exception, registers are at these offsets */

#define tRDI	0
#define tRSI	1
#define tRDX	2
#define tRCX	3
#define tR8	4
#define tR9	5
#define tR10	6
#define tR11	7
#define	tR12	8
#define	tR13	9
#define	tR14	10
#define	tR15	11
#define	tRBP	12
#define	tRBX	13
#define	tRAX	14
#define	tRSP	15
#define	tRIP	16
#define	tRFLAGS	17
#define	tCS	18
#define	tSS	19
#define	tDS	20
#define	tES	21
#define	tFS	22
#define	tGS	23

/*
 * Registers accessible to ptrace(2) syscall for debugger use.
 */
struct reg {
	int64_t	r_rdi;
	int64_t	r_rsi;
	int64_t	r_rdx;
	int64_t	r_rcx;
	int64_t r_r8;
	int64_t r_r9;
	int64_t r_r10;
	int64_t r_r11;
	int64_t r_r12;
	int64_t r_r13;
	int64_t r_r14;
	int64_t r_r15;
	int64_t	r_rbp;
	int64_t	r_rbx;
	int64_t	r_rax;
	int64_t	r_rsp;
	int64_t	r_rip;
	int64_t	r_rflags;
	int64_t	r_cs;
	int64_t	r_ss;
	int64_t	r_ds;
	int64_t	r_es;
	int64_t	r_fs;
	int64_t	r_gs;
};

struct fpreg {
	struct fxsave64 fxstate;
};

#define fp_fcw		fxstate.fx_fcw
#define fp_fsw		fxstate.fx_fsw
#define fp_ftw		fxstate.fx_ftw
#define fp_fop		fxstate.fx_fop
#define fp_rip		fxstate.fx_rip
#define fp_rdp		fxstate.fx_rdp
#define fp_mxcsr	fxstate.fx_mxcsr
#define fp_mxcsr_mask	fxstate.fx_mxcsr_mask
#define fp_st		fxstate.fx_st
#define fp_xmm		fxstate.fx_xmm

#ifdef _KERNEL
int check_context(const struct reg *, struct trapframe *);
#endif

#endif /* !_MACHINE_REG_H_ */
