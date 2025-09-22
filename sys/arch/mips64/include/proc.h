/*	$OpenBSD: proc.h,v 1.11 2017/04/13 03:52:25 guenther Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
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
 *	@(#)proc.h	8.1 (Berkeley) 6/10/93
 */

#ifndef	_MIPS64_PROC_H_
#define	_MIPS64_PROC_H_

/*
 * Machine-dependent part of the proc structure.
 */
struct mdproc {
	struct trapframe *md_regs;	/* registers on current frame */
	volatile int md_astpending;	/* AST pending for this process */
	int	md_flags;		/* machine-dependent flags */
	vaddr_t	md_uarea;		/* allocated uarea virtual addr */
	void	*md_tcb;		/* user-space thread-control-block */

	/* ptrace fields */
	vaddr_t	md_ss_addr;		/* single step address */
	uint32_t md_ss_instr;		/* saved single step instruction */

	/* fpu emulation fields */
	vaddr_t	md_fppgva;		/* vaddr of the branch emulation page */
	vaddr_t	md_fpbranchva;		/* vaddr of fp branch destination */
	vaddr_t	md_fpslotva;		/* initial vaddr of delay slot */

	int32_t	md_obsolete[10];	/* Were RM7000-specific data. */
};

/*
 * Values for md_flags.
 * MDP_FPUSED has two meanings: if the floating point hardware (coprocessor
 * #1) is available, it means it has been used; if there is no floating
 * point hardware, it means the process is currently running a duplicated
 * delay slot, created by the branch emulation logic.
 */
#define	MDP_FPUSED	0x00000001	/* floating point coprocessor used */
#define	MDP_PERF	0x00010000	/* Performance counter used */
#define	MDP_WATCH1	0x00020000	/* Watch register 1 used */
#define	MDP_WATCH2	0x00040000	/* Watch register 1 used */
#define	MDP_FORKSAVE	0x0000ffff	/* Flags to save when forking */

#endif	/* !_MIPS64_PROC_H_ */
