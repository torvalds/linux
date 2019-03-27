/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright 1997 Sean Eric Fagan
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
 *	This product includes software developed by Sean Eric Fagan
 * 4. Neither the name of the author may be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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

/* FreeBSD/amd64-specific system call handling. */

#include <sys/ptrace.h>
#include <sys/syscall.h>

#include <machine/reg.h>
#include <machine/psl.h>

#include <stdbool.h>
#include <stdio.h>
#include <sysdecode.h>

#include "truss.h"

static int
amd64_fetch_args(struct trussinfo *trussinfo, u_int narg)
{
	struct ptrace_io_desc iorequest;
	struct reg regs;
	struct current_syscall *cs;
	lwpid_t tid;
	u_int i, reg;

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
	 * account for these, but we need to skip over %rax if it contains
	 * either of these values.
	 */
	reg = 0;
	switch (regs.r_rax) {
	case SYS_syscall:
	case SYS___syscall:
		reg++;
		break;
	}

	for (i = 0; i < narg && reg < 6; i++, reg++) {
		switch (reg) {
		case 0: cs->args[i] = regs.r_rdi; break;
		case 1: cs->args[i] = regs.r_rsi; break;
		case 2: cs->args[i] = regs.r_rdx; break;
		case 3: cs->args[i] = regs.r_rcx; break;
		case 4: cs->args[i] = regs.r_r8; break;
		case 5: cs->args[i] = regs.r_r9; break;
		}
	}
	if (narg > i) {
		iorequest.piod_op = PIOD_READ_D;
		iorequest.piod_offs = (void *)(regs.r_rsp + sizeof(register_t));
		iorequest.piod_addr = &cs->args[i];
		iorequest.piod_len = (narg - i) * sizeof(register_t);
		ptrace(PT_IO, tid, (caddr_t)&iorequest, 0);
		if (iorequest.piod_len == 0)
			return (-1);
	}

	return (0);
}

static int
amd64_fetch_retval(struct trussinfo *trussinfo, long *retval, int *errorp)
{
	struct reg regs;
	lwpid_t tid;

	tid = trussinfo->curthread->tid;
	if (ptrace(PT_GETREGS, tid, (caddr_t)&regs, 0) < 0) {
		fprintf(trussinfo->outfile, "-- CANNOT READ REGISTERS --\n");
		return (-1);
	}

	retval[0] = regs.r_rax;
	retval[1] = regs.r_rdx;
	*errorp = !!(regs.r_rflags & PSL_C);
	return (0);
}

static struct procabi amd64_freebsd = {
	"FreeBSD ELF64",
	SYSDECODE_ABI_FREEBSD,
	amd64_fetch_args,
	amd64_fetch_retval,
	STAILQ_HEAD_INITIALIZER(amd64_freebsd.extra_syscalls),
	{ NULL }
};

PROCABI(amd64_freebsd);
