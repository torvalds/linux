/*
 * Copyright 2006 Peter Grehan <grehan@freebsd.org>
 * Copyright 2005 Orlando Bassotto <orlando@break.net>
 * Copyright 1998 Sean Eric Fagan
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

/* FreeBSD/powerpc-specific system call handling. */

#include <sys/ptrace.h>
#include <sys/syscall.h>

#include <machine/reg.h>
#include <machine/frame.h>

#include <stdbool.h>
#include <stdio.h>
#include <sysdecode.h>

#include "truss.h"

static int
powerpc_fetch_args(struct trussinfo *trussinfo, u_int narg)
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
	 * account for these, but we need to skip over the first argument.
	 */
	reg = 0;
	switch (regs.fixreg[0]) {
	case SYS_syscall:
		reg += 1;
		break;
	case SYS___syscall:
		reg += 2;
		break;
	}

	for (i = 0; i < narg && reg < NARGREG; i++, reg++) {
		cs->args[i] = regs.fixreg[FIRSTARG + reg];
	}
	if (narg > i) {
		iorequest.piod_op = PIOD_READ_D;
		iorequest.piod_offs = (void *)(regs.fixreg[1] + 8);
		iorequest.piod_addr = &cs->args[i];
		iorequest.piod_len = (narg - i) * sizeof(cs->args[0]);
		ptrace(PT_IO, tid, (caddr_t)&iorequest, 0);
		if (iorequest.piod_len == 0)
			return (-1);
	}

	return (0);
}

static int
powerpc_fetch_retval(struct trussinfo *trussinfo, long *retval, int *errorp)
{
	struct reg regs;
	lwpid_t tid;

	tid = trussinfo->curthread->tid;
	if (ptrace(PT_GETREGS, tid, (caddr_t)&regs, 0) < 0) {
		fprintf(trussinfo->outfile, "-- CANNOT READ REGISTERS --\n");
		return (-1);
	}

	/* XXX: Does not have fixup for __syscall(). */
	retval[0] = regs.fixreg[3];
	retval[1] = regs.fixreg[4];
	*errorp = !!(regs.cr & 0x10000000);
	return (0);
}

static struct procabi powerpc_freebsd = {
	"FreeBSD ELF32",
	SYSDECODE_ABI_FREEBSD,
	powerpc_fetch_args,
	powerpc_fetch_retval,
	STAILQ_HEAD_INITIALIZER(powerpc_freebsd.extra_syscalls),
	{ NULL }
};

PROCABI(powerpc_freebsd);
