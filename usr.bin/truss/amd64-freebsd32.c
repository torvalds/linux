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

/* FreeBSD/amd64-freebsd32-specific system call handling. */

#include <sys/ptrace.h>
#include <sys/syscall.h>

#include <machine/reg.h>
#include <machine/psl.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysdecode.h>

#include "truss.h"

static int
amd64_freebsd32_fetch_args(struct trussinfo *trussinfo, u_int narg)
{
	struct ptrace_io_desc iorequest;
	struct reg regs;
	struct current_syscall *cs;
	unsigned int args32[narg];
	unsigned long parm_offset;
	lwpid_t tid;
	u_int i;

	tid = trussinfo->curthread->tid;
	cs = &trussinfo->curthread->cs;
	if (ptrace(PT_GETREGS, tid, (caddr_t)&regs, 0) < 0) {
		fprintf(trussinfo->outfile, "-- CANNOT READ REGISTERS --\n");
		return (-1);
	}
	parm_offset = regs.r_rsp + sizeof(int);

	/*
	 * FreeBSD has two special kinds of system call redirections --
	 * SYS_syscall, and SYS___syscall.  The former is the old syscall()
	 * routine, basically; the latter is for quad-aligned arguments.
	 *
	 * The system call argument count and code from ptrace() already
	 * account for these, but we need to skip over the first argument.
	 */
	switch (regs.r_rax) {
	case SYS_syscall:
		parm_offset += sizeof(int);
		break;
	case SYS___syscall:
		parm_offset += sizeof(quad_t);
		break;
	}

	iorequest.piod_op = PIOD_READ_D;
	iorequest.piod_offs = (void *)parm_offset;
	iorequest.piod_addr = args32;
	iorequest.piod_len = sizeof(args32);
	ptrace(PT_IO, tid, (caddr_t)&iorequest, 0);
	if (iorequest.piod_len == 0) {
		return (-1);
	}

	for (i = 0; i < narg; i++)
		 cs->args[i] = args32[i];
	return (0);
}

static int
amd64_freebsd32_fetch_retval(struct trussinfo *trussinfo, long *retval,
    int *errorp)
{
	struct reg regs;
	lwpid_t tid;

	tid = trussinfo->curthread->tid;
	if (ptrace(PT_GETREGS, tid, (caddr_t)&regs, 0) < 0) {
		fprintf(trussinfo->outfile, "-- CANNOT READ REGISTERS --\n");
		return (-1);
	}

	retval[0] = regs.r_rax & 0xffffffff;
	retval[1] = regs.r_rdx & 0xffffffff;
	*errorp = !!(regs.r_rflags & PSL_C);
	return (0);
}

static struct procabi amd64_freebsd32 = {
	"FreeBSD ELF32",
	SYSDECODE_ABI_FREEBSD32,
	amd64_freebsd32_fetch_args,
	amd64_freebsd32_fetch_retval,
	STAILQ_HEAD_INITIALIZER(amd64_freebsd32.extra_syscalls),
	{ NULL }
};

PROCABI(amd64_freebsd32);

static struct procabi amd64_freebsd32_aout = {
	"FreeBSD a.out",
	SYSDECODE_ABI_FREEBSD32,
	amd64_freebsd32_fetch_args,
	amd64_freebsd32_fetch_retval,
	STAILQ_HEAD_INITIALIZER(amd64_freebsd32.extra_syscalls),
	{ NULL }
};

PROCABI(amd64_freebsd32_aout);
