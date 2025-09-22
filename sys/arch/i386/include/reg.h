/*	$OpenBSD: reg.h,v 1.7 2011/03/23 16:54:35 pirofti Exp $	*/
/*	$NetBSD: reg.h,v 1.14 1995/10/11 04:20:24 mycroft Exp $	*/

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

/*
 * Location of the users' stored
 * registers within appropriate frame of 'trap' and 'syscall', relative to
 * base of stack frame.
 * Normal usage is u.u_ar0[XX] in kernel.
 */

/* When referenced during a trap/exception, registers are at these offsets */

#define	tES	(0)
#define	tDS	(1)
#define	tEDI	(2)
#define	tESI	(3)
#define	tEBP	(4)
#define	tEBX	(5)
#define	tEDX	(6)
#define	tECX	(7)
#define	tEAX	(8)

#define	tEIP	(11)
#define	tCS	(12)
#define	tEFLAGS	(13)
#define	tESP	(14)
#define	tSS	(15)

/*
 * Registers accessible to ptrace(2) syscall for debugger
 * The machine-dependent code for PT_{SET,GET}REGS needs to
 * use whichever order, defined above, is correct, so that it
 * is all invisible to the user.
 */
struct reg {
	int	r_eax;
	int	r_ecx;
	int	r_edx;
	int	r_ebx;
	int	r_esp;
	int	r_ebp;
	int	r_esi;
	int	r_edi;
	int	r_eip;
	int	r_eflags;
	int	r_cs;
	int	r_ss;
	int	r_ds;
	int	r_es;
	int	r_fs;
	int	r_gs;
};

struct fpreg {
	/*
	 * XXX
	 * Fill this in with real info.
	 */
	char	__data[116];
};

struct xmmregs {
	char	__data[512];
};

#endif /* !_MACHINE_REG_H_ */
