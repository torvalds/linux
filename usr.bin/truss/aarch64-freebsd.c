/*
 * Copyright (c) 2015 The FreeBSD Foundation
 *
 * Portions of this software were developed by Konstantin Belousov
 * under sponsorship from the FreeBSD Foundation.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* FreeBSD/arm64-specific system call handling. */

#include <sys/ptrace.h>
#include <sys/syscall.h>

#include <machine/reg.h>
#include <machine/armreg.h>
#include <machine/ucontext.h>

#include <stdbool.h>
#include <stdio.h>
#include <sysdecode.h>

#include "truss.h"

static int
aarch64_fetch_args(struct trussinfo *trussinfo, u_int narg)
{
	struct reg regs;
	struct current_syscall *cs;
	lwpid_t tid;
	u_int i, reg, syscall_num;

	tid = trussinfo->curthread->tid;
	cs = &trussinfo->curthread->cs;
	if (ptrace(PT_GETREGS, tid, (caddr_t)&regs, 0) < 0) {
		fprintf(trussinfo->outfile, "-- CANNOT READ REGISTERS --\n");
		return (-1);
	}

	/*
	 * FreeBSD has two special kinds of system call redirections --
	 * SYS_syscall, and SYS___syscall.  The former is the old syscall()
	 * routine, basically; the latter is for quad-aligned arguments.
	 *
	 * The system call argument count and code from ptrace() already
	 * account for these, but we need to skip over the first argument.
	 */
	syscall_num = regs.x[8];
	if (syscall_num == SYS_syscall || syscall_num == SYS___syscall) {
		reg = 1;
		syscall_num = regs.x[0];
	} else {
		reg = 0;
	}

	for (i = 0; i < narg && reg < 8; i++, reg++)
		cs->args[i] = regs.x[reg];
	return (0);
}

static int
aarch64_fetch_retval(struct trussinfo *trussinfo, long *retval, int *errorp)
{
	struct reg regs;
	lwpid_t tid;

	tid = trussinfo->curthread->tid;
	if (ptrace(PT_GETREGS, tid, (caddr_t)&regs, 0) < 0) {
		fprintf(trussinfo->outfile, "-- CANNOT READ REGISTERS --\n");
		return (-1);
	}

	retval[0] = regs.x[0];
	retval[1] = regs.x[1];
	*errorp = !!(regs.spsr & PSR_C);
	return (0);
}

static struct procabi aarch64_freebsd = {
	"FreeBSD ELF64",
	SYSDECODE_ABI_FREEBSD,
	aarch64_fetch_args,
	aarch64_fetch_retval,
	STAILQ_HEAD_INITIALIZER(aarch64_freebsd.extra_syscalls),
	{ NULL }
};

PROCABI(aarch64_freebsd);
