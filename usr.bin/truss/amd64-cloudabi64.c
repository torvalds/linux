/*-
 * Copyright (c) 2015 Nuxi, https://nuxi.nl/
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

#include <sys/param.h>
#include <sys/ptrace.h>

#include <machine/psl.h>

#include <stdbool.h>
#include <stdio.h>
#include <sysdecode.h>

#include "truss.h"

static int
amd64_cloudabi64_fetch_args(struct trussinfo *trussinfo, unsigned int narg)
{
	struct current_syscall *cs;
	struct reg regs;
	lwpid_t tid;

	tid = trussinfo->curthread->tid;
	if (ptrace(PT_GETREGS, tid, (caddr_t)&regs, 0) == -1) {
		fprintf(trussinfo->outfile, "-- CANNOT READ REGISTERS --\n");
		return (-1);
	}

	cs = &trussinfo->curthread->cs;
	if (narg >= 1)
		cs->args[0] = regs.r_rdi;
	if (narg >= 2)
		cs->args[1] = regs.r_rsi;
	if (narg >= 3)
		cs->args[2] = regs.r_rdx;
	if (narg >= 4)
		cs->args[3] = regs.r_rcx;
	if (narg >= 5)
		cs->args[4] = regs.r_r8;
	if (narg >= 6)
		cs->args[5] = regs.r_r9;
	return (0);
}

static int
amd64_cloudabi64_fetch_retval(struct trussinfo *trussinfo, long *retval,
    int *errorp)
{
	struct reg regs;
	lwpid_t tid;

	tid = trussinfo->curthread->tid;
	if (ptrace(PT_GETREGS, tid, (caddr_t)&regs, 0) == -1) {
		fprintf(trussinfo->outfile, "-- CANNOT READ REGISTERS --\n");
		return (-1);
	}

	retval[0] = regs.r_rax;
	retval[1] = regs.r_rdx;
	*errorp = (regs.r_rflags & PSL_C) != 0;
	return (0);
}

static struct procabi amd64_cloudabi64 = {
	"CloudABI ELF64",
	SYSDECODE_ABI_CLOUDABI64,
	amd64_cloudabi64_fetch_args,
	amd64_cloudabi64_fetch_retval,
	STAILQ_HEAD_INITIALIZER(amd64_cloudabi64.extra_syscalls),
	{ NULL }
};

PROCABI(amd64_cloudabi64);
