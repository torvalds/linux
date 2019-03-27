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

/* Linux/i386-specific system call handling. */

#include <sys/ptrace.h>

#include <machine/reg.h>
#include <machine/psl.h>

#include <stdbool.h>
#include <stdio.h>
#include <sysdecode.h>

#include "truss.h"

static int
i386_linux_fetch_args(struct trussinfo *trussinfo, u_int narg)
{
	struct reg regs;
	struct current_syscall *cs;
	lwpid_t tid;

	tid = trussinfo->curthread->tid;
	cs = &trussinfo->curthread->cs;
	if (ptrace(PT_GETREGS, tid, (caddr_t)&regs, 0) < 0) {
		fprintf(trussinfo->outfile, "-- CANNOT READ REGISTERS --\n");
		return (-1);
	}

	/*
	 * Linux passes syscall arguments in registers, not
	 * on the stack.  Fortunately, we've got access to the
	 * register set.  Note that we don't bother checking the
	 * number of arguments.	And what does linux do for syscalls
	 * that have more than five arguments?
	 */
	switch (narg) {
	default:
		cs->args[5] = regs.r_ebp;	/* Unconfirmed */
	case 5:
		cs->args[4] = regs.r_edi;
	case 4:
		cs->args[3] = regs.r_esi;
	case 3:
		cs->args[2] = regs.r_edx;
	case 2:
		cs->args[1] = regs.r_ecx;
	case 1:
		cs->args[0] = regs.r_ebx;
	}

	return (0);
}

static int
i386_linux_fetch_retval(struct trussinfo *trussinfo, long *retval, int *errorp)
{
	struct reg regs;
	lwpid_t tid;

	tid = trussinfo->curthread->tid;
	if (ptrace(PT_GETREGS, tid, (caddr_t)&regs, 0) < 0) {
		fprintf(trussinfo->outfile, "-- CANNOT READ REGISTERS --\n");
		return (-1);
	}

	retval[0] = regs.r_eax;
	retval[1] = regs.r_edx;
	*errorp = !!(regs.r_eflags & PSL_C);
	return (0);
}

static struct procabi i386_linux = {
	"Linux ELF",
	SYSDECODE_ABI_LINUX,
	i386_linux_fetch_args,
	i386_linux_fetch_retval,
	STAILQ_HEAD_INITIALIZER(i386_linux.extra_syscalls),
	{ NULL }
};

PROCABI(i386_linux);
