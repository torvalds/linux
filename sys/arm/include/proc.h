/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *      from: @(#)proc.h        7.1 (Berkeley) 5/15/91
 *	from: FreeBSD: src/sys/i386/include/proc.h,v 1.11 2001/06/29
 * $FreeBSD$
 */

#ifndef	_MACHINE_PROC_H_
#define	_MACHINE_PROC_H_

#include <machine/utrap.h>

struct md_utrap {
	utrap_entry_t *ut_precise[UT_MAX];	/* must be first */
	int	ut_refcnt;
};

struct mdthread {
	int	md_spinlock_count;	/* (k) */
	register_t md_saved_cspr;	/* (k) */
	register_t md_spurflt_addr;     /* (k) Spurious page fault address. */
	int md_ptrace_instr;
	int md_ptrace_addr;
	int md_ptrace_instr_alt;
	int md_ptrace_addr_alt;
#if __ARM_ARCH < 6
	register_t md_tp;
	void *md_ras_start;
	void *md_ras_end;
#endif
};

struct mdproc {
	struct	md_utrap *md_utrap;
	void	*md_sigtramp;
};

#define	KINFO_PROC_SIZE 816

#define MAXARGS	8
/*
 * This holds the syscall state for a single system call.
 * As some syscall arguments may be 64-bit aligned we need to ensure the
 * args value is 64-bit aligned. The ABI will then ensure any 64-bit
 * arguments are already correctly aligned, even if they were passed in
 * via registers, we just need to make sure we copy them to an aligned
 * buffer.
 */
struct syscall_args {
	u_int code;
	struct sysent *callp;
	register_t args[MAXARGS];
	int narg;
	u_int nap;
} __aligned(8);

#endif /* !_MACHINE_PROC_H_ */
