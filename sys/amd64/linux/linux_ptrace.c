/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2017 Edward Tomasz Napierala <trasz@FreeBSD.org>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/syscallsubr.h>

#include <machine/pcb.h>
#include <machine/reg.h>

#include <amd64/linux/linux.h>
#include <amd64/linux/linux_proto.h>
#include <compat/linux/linux_signal.h>

#define	LINUX_PTRACE_TRACEME		0
#define	LINUX_PTRACE_PEEKTEXT		1
#define	LINUX_PTRACE_PEEKDATA		2
#define	LINUX_PTRACE_PEEKUSER		3
#define	LINUX_PTRACE_POKETEXT		4
#define	LINUX_PTRACE_POKEDATA		5
#define	LINUX_PTRACE_POKEUSER		6
#define	LINUX_PTRACE_CONT		7
#define	LINUX_PTRACE_KILL		8
#define	LINUX_PTRACE_SINGLESTEP		9
#define	LINUX_PTRACE_GETREGS		12
#define	LINUX_PTRACE_SETREGS		13
#define	LINUX_PTRACE_GETFPREGS		14
#define	LINUX_PTRACE_SETFPREGS		15
#define	LINUX_PTRACE_ATTACH		16
#define	LINUX_PTRACE_DETACH		17
#define	LINUX_PTRACE_SYSCALL		24
#define	LINUX_PTRACE_SETOPTIONS		0x4200
#define	LINUX_PTRACE_GETREGSET		0x4204
#define	LINUX_PTRACE_SEIZE		0x4206

#define	LINUX_PTRACE_O_TRACESYSGOOD	1
#define	LINUX_PTRACE_O_TRACEFORK	2
#define	LINUX_PTRACE_O_TRACEVFORK	4
#define	LINUX_PTRACE_O_TRACECLONE	8
#define	LINUX_PTRACE_O_TRACEEXEC	16
#define	LINUX_PTRACE_O_TRACEVFORKDONE	32
#define	LINUX_PTRACE_O_TRACEEXIT	64
#define	LINUX_PTRACE_O_TRACESECCOMP	128
#define	LINUX_PTRACE_O_EXITKILL		1048576
#define	LINUX_PTRACE_O_SUSPEND_SECCOMP	2097152

#define	LINUX_NT_PRSTATUS		1

#define	LINUX_PTRACE_O_MASK	(LINUX_PTRACE_O_TRACESYSGOOD |	\
    LINUX_PTRACE_O_TRACEFORK | LINUX_PTRACE_O_TRACEVFORK |	\
    LINUX_PTRACE_O_TRACECLONE | LINUX_PTRACE_O_TRACEEXEC |	\
    LINUX_PTRACE_O_TRACEVFORKDONE | LINUX_PTRACE_O_TRACEEXIT |	\
    LINUX_PTRACE_O_TRACESECCOMP | LINUX_PTRACE_O_EXITKILL |	\
    LINUX_PTRACE_O_SUSPEND_SECCOMP)

static int
map_signum(int lsig, int *bsigp)
{
	int bsig;

	if (lsig == 0) {
		*bsigp = 0;
		return (0);
	}

	if (lsig < 0 || lsig > LINUX_SIGRTMAX)
		return (EINVAL);

	bsig = linux_to_bsd_signal(lsig);
	if (bsig == SIGSTOP)
		bsig = 0;

	*bsigp = bsig;
	return (0);
}

struct linux_pt_reg {
	l_ulong	r15;
	l_ulong	r14;
	l_ulong	r13;
	l_ulong	r12;
	l_ulong	rbp;
	l_ulong	rbx;
	l_ulong	r11;
	l_ulong	r10;
	l_ulong	r9;
	l_ulong	r8;
	l_ulong	rax;
	l_ulong	rcx;
	l_ulong	rdx;
	l_ulong	rsi;
	l_ulong	rdi;
	l_ulong	orig_rax;
	l_ulong	rip;
	l_ulong	cs;
	l_ulong	eflags;
	l_ulong	rsp;
	l_ulong	ss;
};

/*
 * Translate amd64 ptrace registers between Linux and FreeBSD formats.
 * The translation is pretty straighforward, for all registers but
 * orig_rax on Linux side and r_trapno and r_err in FreeBSD.
 */
static void
map_regs_to_linux(struct reg *b_reg, struct linux_pt_reg *l_reg)
{

	l_reg->r15 = b_reg->r_r15;
	l_reg->r14 = b_reg->r_r14;
	l_reg->r13 = b_reg->r_r13;
	l_reg->r12 = b_reg->r_r12;
	l_reg->rbp = b_reg->r_rbp;
	l_reg->rbx = b_reg->r_rbx;
	l_reg->r11 = b_reg->r_r11;
	l_reg->r10 = b_reg->r_r10;
	l_reg->r9 = b_reg->r_r9;
	l_reg->r8 = b_reg->r_r8;
	l_reg->rax = b_reg->r_rax;
	l_reg->rcx = b_reg->r_rcx;
	l_reg->rdx = b_reg->r_rdx;
	l_reg->rsi = b_reg->r_rsi;
	l_reg->rdi = b_reg->r_rdi;
	l_reg->orig_rax = b_reg->r_rax;
	l_reg->rip = b_reg->r_rip;
	l_reg->cs = b_reg->r_cs;
	l_reg->eflags = b_reg->r_rflags;
	l_reg->rsp = b_reg->r_rsp;
	l_reg->ss = b_reg->r_ss;
}

static void
map_regs_from_linux(struct reg *b_reg, struct linux_pt_reg *l_reg)
{
	b_reg->r_r15 = l_reg->r15;
	b_reg->r_r14 = l_reg->r14;
	b_reg->r_r13 = l_reg->r13;
	b_reg->r_r12 = l_reg->r12;
	b_reg->r_r11 = l_reg->r11;
	b_reg->r_r10 = l_reg->r10;
	b_reg->r_r9 = l_reg->r9;
	b_reg->r_r8 = l_reg->r8;
	b_reg->r_rdi = l_reg->rdi;
	b_reg->r_rsi = l_reg->rsi;
	b_reg->r_rbp = l_reg->rbp;
	b_reg->r_rbx = l_reg->rbx;
	b_reg->r_rdx = l_reg->rdx;
	b_reg->r_rcx = l_reg->rcx;
	b_reg->r_rax = l_reg->rax;

	/*
	 * XXX: Are zeroes the right thing to put here?
	 */
	b_reg->r_trapno = 0;
	b_reg->r_fs = 0;
	b_reg->r_gs = 0;
	b_reg->r_err = 0;
	b_reg->r_es = 0;
	b_reg->r_ds = 0;

	b_reg->r_rip = l_reg->rip;
	b_reg->r_cs = l_reg->cs;
	b_reg->r_rflags = l_reg->eflags;
	b_reg->r_rsp = l_reg->rsp;
	b_reg->r_ss = l_reg->ss;
}

static int
linux_ptrace_peek(struct thread *td, pid_t pid, void *addr, void *data)
{
	int error;

	error = kern_ptrace(td, PT_READ_I, pid, addr, 0);
	if (error == 0)
		error = copyout(td->td_retval, data, sizeof(l_int));
	td->td_retval[0] = error;

	return (error);
}

static int
linux_ptrace_setoptions(struct thread *td, pid_t pid, l_ulong data)
{
	int mask;

	mask = 0;

	if (data & ~LINUX_PTRACE_O_MASK) {
		printf("%s: unknown ptrace option %lx set; "
		    "returning EINVAL\n",
		    __func__, data & ~LINUX_PTRACE_O_MASK);
		return (EINVAL);
	}

	/*
	 * PTRACE_O_EXITKILL is ignored, we do that by default.
	 */

	if (data & LINUX_PTRACE_O_TRACESYSGOOD) {
		printf("%s: PTRACE_O_TRACESYSGOOD not implemented; "
		    "returning EINVAL\n", __func__);
		return (EINVAL);
	}

	if (data & LINUX_PTRACE_O_TRACEFORK)
		mask |= PTRACE_FORK;

	if (data & LINUX_PTRACE_O_TRACEVFORK)
		mask |= PTRACE_VFORK;

	if (data & LINUX_PTRACE_O_TRACECLONE)
		mask |= PTRACE_VFORK;

	if (data & LINUX_PTRACE_O_TRACEEXEC)
		mask |= PTRACE_EXEC;

	if (data & LINUX_PTRACE_O_TRACEVFORKDONE)
		mask |= PTRACE_VFORK; /* XXX: Close enough? */

	if (data & LINUX_PTRACE_O_TRACEEXIT) {
		printf("%s: PTRACE_O_TRACEEXIT not implemented; "
		    "returning EINVAL\n", __func__);
		return (EINVAL);
	}

	return (kern_ptrace(td, PT_SET_EVENT_MASK, pid, &mask, sizeof(mask)));
}

static int
linux_ptrace_getregs(struct thread *td, pid_t pid, void *data)
{
	struct ptrace_lwpinfo lwpinfo;
	struct reg b_reg;
	struct linux_pt_reg l_reg;
	int error;

	error = kern_ptrace(td, PT_GETREGS, pid, &b_reg, 0);
	if (error != 0)
		return (error);

	map_regs_to_linux(&b_reg, &l_reg);

	/*
	 * The strace(1) utility depends on RAX being set to -ENOSYS
	 * on syscall entry.
	 */
	error = kern_ptrace(td, PT_LWPINFO, pid, &lwpinfo, sizeof(lwpinfo));
	if (error != 0) {
		printf("%s: PT_LWPINFO failed with error %d\n", __func__, error);
		return (error);
	}
	if (lwpinfo.pl_flags & PL_FLAG_SCE)
		l_reg.rax = -38; // XXX: Don't hardcode?

	error = copyout(&l_reg, (void *)data, sizeof(l_reg));
	return (error);
}

static int
linux_ptrace_setregs(struct thread *td, pid_t pid, void *data)
{
	struct reg b_reg;
	struct linux_pt_reg l_reg;
	int error;

	error = copyin(data, &l_reg, sizeof(l_reg));
	if (error != 0)
		return (error);
	map_regs_from_linux(&b_reg, &l_reg);
	error = kern_ptrace(td, PT_SETREGS, pid, &b_reg, 0);
	return (error);
}

static int
linux_ptrace_getregset(struct thread *td, pid_t pid, l_ulong addr, l_ulong data)
{

	switch (addr) {
	case LINUX_NT_PRSTATUS:
		printf("%s: NT_PRSTATUS not implemented; returning EINVAL\n",
		    __func__);
		return (EINVAL);
	default:
		printf("%s: PTRACE_GETREGSET request %ld not implemented; "
		    "returning EINVAL\n", __func__, addr);
		return (EINVAL);
	}
}

static int
linux_ptrace_seize(struct thread *td, pid_t pid, l_ulong addr, l_ulong data)
{

	printf("%s: PTRACE_SEIZE not implemented; returning EINVAL\n", __func__);
	return (EINVAL);
}

int
linux_ptrace(struct thread *td, struct linux_ptrace_args *uap)
{
	void *addr;
	pid_t pid;
	int error, sig;

	pid  = (pid_t)uap->pid;
	addr = (void *)uap->addr;

	switch (uap->req) {
	case LINUX_PTRACE_TRACEME:
		error = kern_ptrace(td, PT_TRACE_ME, 0, 0, 0);
		break;
	case LINUX_PTRACE_PEEKTEXT:
	case LINUX_PTRACE_PEEKDATA:
		error = linux_ptrace_peek(td, pid, addr, (void *)uap->data);
		if (error != 0)
			return (error);
		/*
		 * Linux expects this syscall to read 64 bits, not 32.
		 */
		error = linux_ptrace_peek(td, pid,
		    (void *)(uap->addr + 4), (void *)(uap->data + 4));
		break;
	case LINUX_PTRACE_POKETEXT:
		error = kern_ptrace(td, PT_WRITE_I, pid, addr, uap->data);
		break;
	case LINUX_PTRACE_POKEDATA:
		error = kern_ptrace(td, PT_WRITE_D, pid, addr, uap->data);
		break;
	case LINUX_PTRACE_CONT:
		error = map_signum(uap->data, &sig);
		if (error != 0)
			break;
		error = kern_ptrace(td, PT_CONTINUE, pid, (void *)1, sig);
		break;
	case LINUX_PTRACE_KILL:
		error = kern_ptrace(td, PT_KILL, pid, addr, uap->data);
		break;
	case LINUX_PTRACE_SINGLESTEP:
		error = map_signum(uap->data, &sig);
		if (error != 0)
			break;
		error = kern_ptrace(td, PT_STEP, pid, (void *)1, sig);
		break;
	case LINUX_PTRACE_GETREGS:
		error = linux_ptrace_getregs(td, pid, (void *)uap->data);
		break;
	case LINUX_PTRACE_SETREGS:
		error = linux_ptrace_setregs(td, pid, (void *)uap->data);
		break;
	case LINUX_PTRACE_ATTACH:
		error = kern_ptrace(td, PT_ATTACH, pid, addr, uap->data);
		break;
	case LINUX_PTRACE_DETACH:
		error = map_signum(uap->data, &sig);
		if (error != 0)
			break;
		error = kern_ptrace(td, PT_DETACH, pid, (void *)1, sig);
		break;
	case LINUX_PTRACE_SYSCALL:
		error = map_signum(uap->data, &sig);
		if (error != 0)
			break;
		error = kern_ptrace(td, PT_SYSCALL, pid, (void *)1, sig);
		break;
	case LINUX_PTRACE_SETOPTIONS:
		error = linux_ptrace_setoptions(td, pid, uap->data);
		break;
	case LINUX_PTRACE_GETREGSET:
		error = linux_ptrace_getregset(td, pid, uap->addr, uap->data);
		break;
	case LINUX_PTRACE_SEIZE:
		error = linux_ptrace_seize(td, pid, uap->addr, uap->data);
		break;
	default:
		printf("%s: ptrace(%ld, ...) not implemented; returning EINVAL\n",
		    __func__, uap->req);
		error = EINVAL;
		break;
	}

	return (error);
}
