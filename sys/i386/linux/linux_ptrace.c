/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Alexander Kabaev
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

#include "opt_cpu.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <sys/syscallsubr.h>
#include <sys/systm.h>

#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/reg.h>

#include <i386/linux/linux.h>
#include <i386/linux/linux_proto.h>
#include <compat/linux/linux_signal.h>

/*
 *   Linux ptrace requests numbers. Mostly identical to FreeBSD,
 *   except for MD ones and PT_ATTACH/PT_DETACH.
 */
#define	PTRACE_TRACEME		0
#define	PTRACE_PEEKTEXT		1
#define	PTRACE_PEEKDATA		2
#define	PTRACE_PEEKUSR		3
#define	PTRACE_POKETEXT		4
#define	PTRACE_POKEDATA		5
#define	PTRACE_POKEUSR		6
#define	PTRACE_CONT		7
#define	PTRACE_KILL		8
#define	PTRACE_SINGLESTEP	9

#define PTRACE_ATTACH		16
#define PTRACE_DETACH		17

#define	LINUX_PTRACE_SYSCALL	24

#define PTRACE_GETREGS		12
#define PTRACE_SETREGS		13
#define PTRACE_GETFPREGS	14
#define PTRACE_SETFPREGS	15
#define PTRACE_GETFPXREGS	18
#define PTRACE_SETFPXREGS	19

#define PTRACE_SETOPTIONS	21

/*
 * Linux keeps debug registers at the following
 * offset in the user struct
 */
#define LINUX_DBREG_OFFSET	252
#define LINUX_DBREG_SIZE	(8*sizeof(l_int))

static __inline int
map_signum(int signum)
{

	signum = linux_to_bsd_signal(signum);
	return ((signum == SIGSTOP)? 0 : signum);
}

struct linux_pt_reg {
	l_long	ebx;
	l_long	ecx;
	l_long	edx;
	l_long	esi;
	l_long	edi;
	l_long	ebp;
	l_long	eax;
	l_int	xds;
	l_int	xes;
	l_int	xfs;
	l_int	xgs;
	l_long	orig_eax;
	l_long	eip;
	l_int	xcs;
	l_long	eflags;
	l_long	esp;
	l_int	xss;
};

/*
 *   Translate i386 ptrace registers between Linux and FreeBSD formats.
 *   The translation is pretty straighforward, for all registers, but
 *   orig_eax on Linux side and r_trapno and r_err in FreeBSD
 */
static void
map_regs_to_linux(struct reg *bsd_r, struct linux_pt_reg *linux_r)
{
	linux_r->ebx = bsd_r->r_ebx;
	linux_r->ecx = bsd_r->r_ecx;
	linux_r->edx = bsd_r->r_edx;
	linux_r->esi = bsd_r->r_esi;
	linux_r->edi = bsd_r->r_edi;
	linux_r->ebp = bsd_r->r_ebp;
	linux_r->eax = bsd_r->r_eax;
	linux_r->xds = bsd_r->r_ds;
	linux_r->xes = bsd_r->r_es;
	linux_r->xfs = bsd_r->r_fs;
	linux_r->xgs = bsd_r->r_gs;
	linux_r->orig_eax = bsd_r->r_eax;
	linux_r->eip = bsd_r->r_eip;
	linux_r->xcs = bsd_r->r_cs;
	linux_r->eflags = bsd_r->r_eflags;
	linux_r->esp = bsd_r->r_esp;
	linux_r->xss = bsd_r->r_ss;
}

static void
map_regs_from_linux(struct reg *bsd_r, struct linux_pt_reg *linux_r)
{
	bsd_r->r_ebx = linux_r->ebx;
	bsd_r->r_ecx = linux_r->ecx;
	bsd_r->r_edx = linux_r->edx;
	bsd_r->r_esi = linux_r->esi;
	bsd_r->r_edi = linux_r->edi;
	bsd_r->r_ebp = linux_r->ebp;
	bsd_r->r_eax = linux_r->eax;
	bsd_r->r_ds  = linux_r->xds;
	bsd_r->r_es  = linux_r->xes;
	bsd_r->r_fs  = linux_r->xfs;
	bsd_r->r_gs  = linux_r->xgs;
	bsd_r->r_eip = linux_r->eip;
	bsd_r->r_cs  = linux_r->xcs;
	bsd_r->r_eflags = linux_r->eflags;
	bsd_r->r_esp = linux_r->esp;
	bsd_r->r_ss = linux_r->xss;
}

struct linux_pt_fpreg {
	l_long cwd;
	l_long swd;
	l_long twd;
	l_long fip;
	l_long fcs;
	l_long foo;
	l_long fos;
	l_long st_space[2*10];
};

static void
map_fpregs_to_linux(struct fpreg *bsd_r, struct linux_pt_fpreg *linux_r)
{
	linux_r->cwd = bsd_r->fpr_env[0];
	linux_r->swd = bsd_r->fpr_env[1];
	linux_r->twd = bsd_r->fpr_env[2];
	linux_r->fip = bsd_r->fpr_env[3];
	linux_r->fcs = bsd_r->fpr_env[4];
	linux_r->foo = bsd_r->fpr_env[5];
	linux_r->fos = bsd_r->fpr_env[6];
	bcopy(bsd_r->fpr_acc, linux_r->st_space, sizeof(linux_r->st_space));
}

static void
map_fpregs_from_linux(struct fpreg *bsd_r, struct linux_pt_fpreg *linux_r)
{
	bsd_r->fpr_env[0] = linux_r->cwd;
	bsd_r->fpr_env[1] = linux_r->swd;
	bsd_r->fpr_env[2] = linux_r->twd;
	bsd_r->fpr_env[3] = linux_r->fip;
	bsd_r->fpr_env[4] = linux_r->fcs;
	bsd_r->fpr_env[5] = linux_r->foo;
	bsd_r->fpr_env[6] = linux_r->fos;
	bcopy(bsd_r->fpr_acc, linux_r->st_space, sizeof(bsd_r->fpr_acc));
}

struct linux_pt_fpxreg {
	l_ushort	cwd;
	l_ushort	swd;
	l_ushort	twd;
	l_ushort	fop;
	l_long		fip;
	l_long		fcs;
	l_long		foo;
	l_long		fos;
	l_long		mxcsr;
	l_long		reserved;
	l_long		st_space[32];
	l_long		xmm_space[32];
	l_long		padding[56];
};

static int
linux_proc_read_fpxregs(struct thread *td, struct linux_pt_fpxreg *fpxregs)
{

	PROC_LOCK_ASSERT(td->td_proc, MA_OWNED);
	if (cpu_fxsr == 0 || (td->td_proc->p_flag & P_INMEM) == 0)
		return (EIO);
	bcopy(&get_pcb_user_save_td(td)->sv_xmm, fpxregs, sizeof(*fpxregs));
	return (0);
}

static int
linux_proc_write_fpxregs(struct thread *td, struct linux_pt_fpxreg *fpxregs)
{

	PROC_LOCK_ASSERT(td->td_proc, MA_OWNED);
	if (cpu_fxsr == 0 || (td->td_proc->p_flag & P_INMEM) == 0)
		return (EIO);
	bcopy(fpxregs, &get_pcb_user_save_td(td)->sv_xmm, sizeof(*fpxregs));
	return (0);
}

int
linux_ptrace(struct thread *td, struct linux_ptrace_args *uap)
{
	union {
		struct linux_pt_reg	reg;
		struct linux_pt_fpreg	fpreg;
		struct linux_pt_fpxreg	fpxreg;
	} r;
	union {
		struct reg		bsd_reg;
		struct fpreg		bsd_fpreg;
		struct dbreg		bsd_dbreg;
	} u;
	void *addr;
	pid_t pid;
	int error, req;

	error = 0;

	/* by default, just copy data intact */
	req  = uap->req;
	pid  = (pid_t)uap->pid;
	addr = (void *)uap->addr;

	switch (req) {
	case PTRACE_TRACEME:
	case PTRACE_POKETEXT:
	case PTRACE_POKEDATA:
	case PTRACE_KILL:
		error = kern_ptrace(td, req, pid, addr, uap->data);
		break;
	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA: {
		/* need to preserve return value */
		int rval = td->td_retval[0];
		error = kern_ptrace(td, req, pid, addr, 0);
		if (error == 0)
			error = copyout(td->td_retval, (void *)uap->data,
			    sizeof(l_int));
		td->td_retval[0] = rval;
		break;
	}
	case PTRACE_DETACH:
		error = kern_ptrace(td, PT_DETACH, pid, (void *)1,
		     map_signum(uap->data));
		break;
	case PTRACE_SINGLESTEP:
	case PTRACE_CONT:
		error = kern_ptrace(td, req, pid, (void *)1,
		     map_signum(uap->data));
		break;
	case PTRACE_ATTACH:
		error = kern_ptrace(td, PT_ATTACH, pid, addr, uap->data);
		break;
	case PTRACE_GETREGS:
		/* Linux is using data where FreeBSD is using addr */
		error = kern_ptrace(td, PT_GETREGS, pid, &u.bsd_reg, 0);
		if (error == 0) {
			map_regs_to_linux(&u.bsd_reg, &r.reg);
			error = copyout(&r.reg, (void *)uap->data,
			    sizeof(r.reg));
		}
		break;
	case PTRACE_SETREGS:
		/* Linux is using data where FreeBSD is using addr */
		error = copyin((void *)uap->data, &r.reg, sizeof(r.reg));
		if (error == 0) {
			map_regs_from_linux(&u.bsd_reg, &r.reg);
			error = kern_ptrace(td, PT_SETREGS, pid, &u.bsd_reg, 0);
		}
		break;
	case PTRACE_GETFPREGS:
		/* Linux is using data where FreeBSD is using addr */
		error = kern_ptrace(td, PT_GETFPREGS, pid, &u.bsd_fpreg, 0);
		if (error == 0) {
			map_fpregs_to_linux(&u.bsd_fpreg, &r.fpreg);
			error = copyout(&r.fpreg, (void *)uap->data,
			    sizeof(r.fpreg));
		}
		break;
	case PTRACE_SETFPREGS:
		/* Linux is using data where FreeBSD is using addr */
		error = copyin((void *)uap->data, &r.fpreg, sizeof(r.fpreg));
		if (error == 0) {
			map_fpregs_from_linux(&u.bsd_fpreg, &r.fpreg);
			error = kern_ptrace(td, PT_SETFPREGS, pid,
			    &u.bsd_fpreg, 0);
		}
		break;
	case PTRACE_SETFPXREGS:
		error = copyin((void *)uap->data, &r.fpxreg, sizeof(r.fpxreg));
		if (error)
			break;
		/* FALL THROUGH */
	case PTRACE_GETFPXREGS: {
		struct proc *p;
		struct thread *td2;

		if (sizeof(struct linux_pt_fpxreg) != sizeof(struct savexmm)) {
			static int once = 0;
			if (!once) {
				printf("linux: savexmm != linux_pt_fpxreg\n");
				once = 1;
			}
			error = EIO;
			break;
		}

		if ((p = pfind(uap->pid)) == NULL) {
			error = ESRCH;
			break;
		}

		/* Exiting processes can't be debugged. */
		if ((p->p_flag & P_WEXIT) != 0) {
			error = ESRCH;
			goto fail;
		}

		if ((error = p_candebug(td, p)) != 0)
			goto fail;

		/* System processes can't be debugged. */
		if ((p->p_flag & P_SYSTEM) != 0) {
			error = EINVAL;
			goto fail;
		}

		/* not being traced... */
		if ((p->p_flag & P_TRACED) == 0) {
			error = EPERM;
			goto fail;
		}

		/* not being traced by YOU */
		if (p->p_pptr != td->td_proc) {
			error = EBUSY;
			goto fail;
		}

		/* not currently stopped */
		if (!P_SHOULDSTOP(p) || (p->p_flag & P_WAITED) == 0) {
			error = EBUSY;
			goto fail;
		}

		if (req == PTRACE_GETFPXREGS) {
			_PHOLD(p);	/* may block */
			td2 = FIRST_THREAD_IN_PROC(p);
			error = linux_proc_read_fpxregs(td2, &r.fpxreg);
			_PRELE(p);
			PROC_UNLOCK(p);
			if (error == 0)
				error = copyout(&r.fpxreg, (void *)uap->data,
				    sizeof(r.fpxreg));
		} else {
			/* clear dangerous bits exactly as Linux does*/
			r.fpxreg.mxcsr &= 0xffbf;
			_PHOLD(p);	/* may block */
			td2 = FIRST_THREAD_IN_PROC(p);
			error = linux_proc_write_fpxregs(td2, &r.fpxreg);
			_PRELE(p);
			PROC_UNLOCK(p);
		}
		break;

	fail:
		PROC_UNLOCK(p);
		break;
	}
	case PTRACE_PEEKUSR:
	case PTRACE_POKEUSR: {
		error = EIO;

		/* check addr for alignment */
		if (uap->addr < 0 || uap->addr & (sizeof(l_int) - 1))
			break;
		/*
		 * Allow Linux programs to access register values in
		 * user struct. We simulate this through PT_GET/SETREGS
		 * as necessary.
		 */
		if (uap->addr < sizeof(struct linux_pt_reg)) {
			error = kern_ptrace(td, PT_GETREGS, pid, &u.bsd_reg, 0);
			if (error != 0)
				break;

			map_regs_to_linux(&u.bsd_reg, &r.reg);
			if (req == PTRACE_PEEKUSR) {
				error = copyout((char *)&r.reg + uap->addr,
				    (void *)uap->data, sizeof(l_int));
				break;
			}

			*(l_int *)((char *)&r.reg + uap->addr) =
			    (l_int)uap->data;

			map_regs_from_linux(&u.bsd_reg, &r.reg);
			error = kern_ptrace(td, PT_SETREGS, pid, &u.bsd_reg, 0);
		}

		/*
		 * Simulate debug registers access
		 */
		if (uap->addr >= LINUX_DBREG_OFFSET &&
		    uap->addr <= LINUX_DBREG_OFFSET + LINUX_DBREG_SIZE) {
			error = kern_ptrace(td, PT_GETDBREGS, pid, &u.bsd_dbreg,
			    0);
			if (error != 0)
				break;

			uap->addr -= LINUX_DBREG_OFFSET;
			if (req == PTRACE_PEEKUSR) {
				error = copyout((char *)&u.bsd_dbreg +
				    uap->addr, (void *)uap->data,
				    sizeof(l_int));
				break;
			}

			*(l_int *)((char *)&u.bsd_dbreg + uap->addr) =
			     uap->data;
			error = kern_ptrace(td, PT_SETDBREGS, pid,
			    &u.bsd_dbreg, 0);
		}

		break;
	}
	case LINUX_PTRACE_SYSCALL:
		/* fall through */
	default:
		printf("linux: ptrace(%u, ...) not implemented\n",
		    (unsigned int)uap->req);
		error = EINVAL;
		break;
	}

	return (error);
}
