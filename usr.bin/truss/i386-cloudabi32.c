/*-
 * Copyright (c) 2015-2017 Nuxi, https://nuxi.nl/
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
i386_cloudabi32_fetch_args(struct trussinfo *trussinfo, unsigned int narg)
{
	struct current_syscall *cs;
	struct ptrace_io_desc iorequest;
	struct reg regs;
	lwpid_t tid;

	if (narg > 0) {
		/* Fetch registers, containing the stack pointer. */
		tid = trussinfo->curthread->tid;
		if (ptrace(PT_GETREGS, tid, (caddr_t)&regs, 0) == -1) {
			fprintf(trussinfo->outfile,
			    "-- CANNOT READ REGISTERS --\n");
			return (-1);
		}

		/* Fetch arguments. */
		cs = &trussinfo->curthread->cs;
		iorequest.piod_op = PIOD_READ_D;
		iorequest.piod_offs = (void **)regs.r_esp + 1;
		iorequest.piod_addr = cs->args;
		iorequest.piod_len = sizeof(cs->args[0]) * narg;
		if (ptrace(PT_IO, tid, (caddr_t)&iorequest, 0) == -1 ||
		    iorequest.piod_len == 0)
			return (-1);
	}
	return (0);
}

static int
i386_cloudabi32_fetch_retval(struct trussinfo *trussinfo, long *retval,
    int *errorp)
{
	struct reg regs;
	lwpid_t tid;

	tid = trussinfo->curthread->tid;
	if (ptrace(PT_GETREGS, tid, (caddr_t)&regs, 0) == -1) {
		fprintf(trussinfo->outfile, "-- CANNOT READ REGISTERS --\n");
		return (-1);
	}

	retval[0] = regs.r_eax;
	retval[1] = regs.r_edx;
	*errorp = (regs.r_eflags & PSL_C) != 0;
	return (0);
}

static struct procabi i386_cloudabi32 = {
	"CloudABI ELF32",
	SYSDECODE_ABI_CLOUDABI32,
	i386_cloudabi32_fetch_args,
	i386_cloudabi32_fetch_retval,
	STAILQ_HEAD_INITIALIZER(i386_cloudabi32.extra_syscalls),
	{ NULL }
};

PROCABI(i386_cloudabi32);
