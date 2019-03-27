/*-
 * SPDX-License-Identifier: BSD-4-Clause AND BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 *      This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*-
 * Copyright (C) 2001 Benno Rice
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *	$NetBSD: machdep.c,v 1.74.2.1 2000/11/01 16:13:48 tv Exp $
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_fpu_emu.h"

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/cons.h>
#include <sys/cpu.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/signalvar.h>
#include <sys/syscallsubr.h>
#include <sys/syscall.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/ucontext.h>
#include <sys/uio.h>

#include <machine/altivec.h>
#include <machine/cpu.h>
#include <machine/elf.h>
#include <machine/fpu.h>
#include <machine/pcb.h>
#include <machine/reg.h>
#include <machine/sigframe.h>
#include <machine/trap.h>
#include <machine/vmparam.h>

#include <vm/pmap.h>

#ifdef FPU_EMU
#include <powerpc/fpu/fpu_extern.h>
#endif

#ifdef COMPAT_FREEBSD32
#include <compat/freebsd32/freebsd32_signal.h>
#include <compat/freebsd32/freebsd32_util.h>
#include <compat/freebsd32/freebsd32_proto.h>

typedef struct __ucontext32 {
	sigset_t		uc_sigmask;
	mcontext32_t		uc_mcontext;
	uint32_t		uc_link;
	struct sigaltstack32    uc_stack;
	uint32_t		uc_flags;
	uint32_t		__spare__[4];
} ucontext32_t;

struct sigframe32 {
	ucontext32_t		sf_uc;
	struct siginfo32	sf_si;
};

static int	grab_mcontext32(struct thread *td, mcontext32_t *, int flags);
#endif

static int	grab_mcontext(struct thread *, mcontext_t *, int);

#ifdef __powerpc64__
extern struct sysentvec elf64_freebsd_sysvec_v2;
#endif

void
sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask)
{
	struct trapframe *tf;
	struct sigacts *psp;
	struct sigframe sf;
	struct thread *td;
	struct proc *p;
	#ifdef COMPAT_FREEBSD32
	struct siginfo32 siginfo32;
	struct sigframe32 sf32;
	#endif
	size_t sfpsize;
	caddr_t sfp, usfp;
	int oonstack, rndfsize;
	int sig;
	int code;

	td = curthread;
	p = td->td_proc;
	PROC_LOCK_ASSERT(p, MA_OWNED);

	psp = p->p_sigacts;
	mtx_assert(&psp->ps_mtx, MA_OWNED);
	tf = td->td_frame;
	oonstack = sigonstack(tf->fixreg[1]);

	/*
	 * Fill siginfo structure.
	 */
	ksi->ksi_info.si_signo = ksi->ksi_signo;
	ksi->ksi_info.si_addr =
	    (void *)((tf->exc == EXC_DSI || tf->exc == EXC_DSE) ? 
	    tf->dar : tf->srr0);

	#ifdef COMPAT_FREEBSD32
	if (SV_PROC_FLAG(p, SV_ILP32)) {
		siginfo_to_siginfo32(&ksi->ksi_info, &siginfo32);
		sig = siginfo32.si_signo;
		code = siginfo32.si_code;
		sfp = (caddr_t)&sf32;
		sfpsize = sizeof(sf32);
		rndfsize = roundup(sizeof(sf32), 16);

		/*
		 * Save user context
		 */

		memset(&sf32, 0, sizeof(sf32));
		grab_mcontext32(td, &sf32.sf_uc.uc_mcontext, 0);

		sf32.sf_uc.uc_sigmask = *mask;
		sf32.sf_uc.uc_stack.ss_sp = (uintptr_t)td->td_sigstk.ss_sp;
		sf32.sf_uc.uc_stack.ss_size = (uint32_t)td->td_sigstk.ss_size;
		sf32.sf_uc.uc_stack.ss_flags = (td->td_pflags & TDP_ALTSTACK)
		    ? ((oonstack) ? SS_ONSTACK : 0) : SS_DISABLE;

		sf32.sf_uc.uc_mcontext.mc_onstack = (oonstack) ? 1 : 0;
	} else {
	#endif
		sig = ksi->ksi_signo;
		code = ksi->ksi_code;
		sfp = (caddr_t)&sf;
		sfpsize = sizeof(sf);
		#ifdef __powerpc64__
		/*
		 * 64-bit PPC defines a 288 byte scratch region
		 * below the stack.
		 */
		rndfsize = 288 + roundup(sizeof(sf), 48);
		#else
		rndfsize = roundup(sizeof(sf), 16);
		#endif

		/*
		 * Save user context
		 */

		memset(&sf, 0, sizeof(sf));
		grab_mcontext(td, &sf.sf_uc.uc_mcontext, 0);

		sf.sf_uc.uc_sigmask = *mask;
		sf.sf_uc.uc_stack = td->td_sigstk;
		sf.sf_uc.uc_stack.ss_flags = (td->td_pflags & TDP_ALTSTACK)
		    ? ((oonstack) ? SS_ONSTACK : 0) : SS_DISABLE;

		sf.sf_uc.uc_mcontext.mc_onstack = (oonstack) ? 1 : 0;
	#ifdef COMPAT_FREEBSD32
	}
	#endif

	CTR4(KTR_SIG, "sendsig: td=%p (%s) catcher=%p sig=%d", td, p->p_comm,
	     catcher, sig);

	/*
	 * Allocate and validate space for the signal handler context.
	 */
	if ((td->td_pflags & TDP_ALTSTACK) != 0 && !oonstack &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		usfp = (void *)(((uintptr_t)td->td_sigstk.ss_sp +
		   td->td_sigstk.ss_size - rndfsize) & ~0xFul);
	} else {
		usfp = (void *)((tf->fixreg[1] - rndfsize) & ~0xFul);
	}

	/*
	 * Save the floating-point state, if necessary, then copy it.
	 */
	/* XXX */

	/*
	 * Set up the registers to return to sigcode.
	 *
	 *   r1/sp - sigframe ptr
	 *   lr    - sig function, dispatched to by blrl in trampoline
	 *   r3    - sig number
	 *   r4    - SIGINFO ? &siginfo : exception code
	 *   r5    - user context
	 *   srr0  - trampoline function addr
	 */
	tf->lr = (register_t)catcher;
	tf->fixreg[1] = (register_t)usfp;
	tf->fixreg[FIRSTARG] = sig;
	#ifdef COMPAT_FREEBSD32
	tf->fixreg[FIRSTARG+2] = (register_t)usfp +
	    ((SV_PROC_FLAG(p, SV_ILP32)) ?
	    offsetof(struct sigframe32, sf_uc) :
	    offsetof(struct sigframe, sf_uc));
	#else
	tf->fixreg[FIRSTARG+2] = (register_t)usfp +
	    offsetof(struct sigframe, sf_uc);
	#endif
	if (SIGISMEMBER(psp->ps_siginfo, sig)) {
		/*
		 * Signal handler installed with SA_SIGINFO.
		 */
		#ifdef COMPAT_FREEBSD32
		if (SV_PROC_FLAG(p, SV_ILP32)) {
			sf32.sf_si = siginfo32;
			tf->fixreg[FIRSTARG+1] = (register_t)usfp +
			    offsetof(struct sigframe32, sf_si);
			sf32.sf_si = siginfo32;
		} else  {
		#endif
			tf->fixreg[FIRSTARG+1] = (register_t)usfp +
			    offsetof(struct sigframe, sf_si);
			sf.sf_si = ksi->ksi_info;
		#ifdef COMPAT_FREEBSD32
		}
		#endif
	} else {
		/* Old FreeBSD-style arguments. */
		tf->fixreg[FIRSTARG+1] = code;
		tf->fixreg[FIRSTARG+3] = (tf->exc == EXC_DSI) ? 
		    tf->dar : tf->srr0;
	}
	mtx_unlock(&psp->ps_mtx);
	PROC_UNLOCK(p);

	tf->srr0 = (register_t)p->p_sysent->sv_sigcode_base;

	/*
	 * copy the frame out to userland.
	 */
	if (copyout(sfp, usfp, sfpsize) != 0) {
		/*
		 * Process has trashed its stack. Kill it.
		 */
		CTR2(KTR_SIG, "sendsig: sigexit td=%p sfp=%p", td, sfp);
		PROC_LOCK(p);
		sigexit(td, SIGILL);
	}

	CTR3(KTR_SIG, "sendsig: return td=%p pc=%#x sp=%#x", td,
	     tf->srr0, tf->fixreg[1]);

	PROC_LOCK(p);
	mtx_lock(&psp->ps_mtx);
}

int
sys_sigreturn(struct thread *td, struct sigreturn_args *uap)
{
	ucontext_t uc;
	int error;

	CTR2(KTR_SIG, "sigreturn: td=%p ucp=%p", td, uap->sigcntxp);

	if (copyin(uap->sigcntxp, &uc, sizeof(uc)) != 0) {
		CTR1(KTR_SIG, "sigreturn: efault td=%p", td);
		return (EFAULT);
	}

	error = set_mcontext(td, &uc.uc_mcontext);
	if (error != 0)
		return (error);

	kern_sigprocmask(td, SIG_SETMASK, &uc.uc_sigmask, NULL, 0);

	CTR3(KTR_SIG, "sigreturn: return td=%p pc=%#x sp=%#x",
	     td, uc.uc_mcontext.mc_srr0, uc.uc_mcontext.mc_gpr[1]);

	return (EJUSTRETURN);
}

#ifdef COMPAT_FREEBSD4
int
freebsd4_sigreturn(struct thread *td, struct freebsd4_sigreturn_args *uap)
{

	return sys_sigreturn(td, (struct sigreturn_args *)uap);
}
#endif

/*
 * Construct a PCB from a trapframe. This is called from kdb_trap() where
 * we want to start a backtrace from the function that caused us to enter
 * the debugger. We have the context in the trapframe, but base the trace
 * on the PCB. The PCB doesn't have to be perfect, as long as it contains
 * enough for a backtrace.
 */
void
makectx(struct trapframe *tf, struct pcb *pcb)
{

	pcb->pcb_lr = tf->srr0;
	pcb->pcb_sp = tf->fixreg[1];
}

/*
 * get_mcontext/sendsig helper routine that doesn't touch the
 * proc lock
 */
static int
grab_mcontext(struct thread *td, mcontext_t *mcp, int flags)
{
	struct pcb *pcb;
	int i;

	pcb = td->td_pcb;

	memset(mcp, 0, sizeof(mcontext_t));

	mcp->mc_vers = _MC_VERSION;
	mcp->mc_flags = 0;
	memcpy(&mcp->mc_frame, td->td_frame, sizeof(struct trapframe));
	if (flags & GET_MC_CLEAR_RET) {
		mcp->mc_gpr[3] = 0;
		mcp->mc_gpr[4] = 0;
	}

	/*
	 * This assumes that floating-point context is *not* lazy,
	 * so if the thread has used FP there would have been a
	 * FP-unavailable exception that would have set things up
	 * correctly.
	 */
	if (pcb->pcb_flags & PCB_FPREGS) {
		if (pcb->pcb_flags & PCB_FPU) {
			KASSERT(td == curthread,
				("get_mcontext: fp save not curthread"));
			critical_enter();
			save_fpu(td);
			critical_exit();
		}
		mcp->mc_flags |= _MC_FP_VALID;
		memcpy(&mcp->mc_fpscr, &pcb->pcb_fpu.fpscr, sizeof(double));
		for (i = 0; i < 32; i++)
			memcpy(&mcp->mc_fpreg[i], &pcb->pcb_fpu.fpr[i].fpr,
			    sizeof(double));
	}

	if (pcb->pcb_flags & PCB_VSX) {
		for (i = 0; i < 32; i++)
			memcpy(&mcp->mc_vsxfpreg[i],
			    &pcb->pcb_fpu.fpr[i].vsr[2], sizeof(double));
	}

	/*
	 * Repeat for Altivec context
	 */

	if (pcb->pcb_flags & PCB_VEC) {
		KASSERT(td == curthread,
			("get_mcontext: fp save not curthread"));
		critical_enter();
		save_vec(td);
		critical_exit();
		mcp->mc_flags |= _MC_AV_VALID;
		mcp->mc_vscr  = pcb->pcb_vec.vscr;
		mcp->mc_vrsave =  pcb->pcb_vec.vrsave;
		memcpy(mcp->mc_avec, pcb->pcb_vec.vr, sizeof(mcp->mc_avec));
	}

	mcp->mc_len = sizeof(*mcp);

	return (0);
}

int
get_mcontext(struct thread *td, mcontext_t *mcp, int flags)
{
	int error;

	error = grab_mcontext(td, mcp, flags);
	if (error == 0) {
		PROC_LOCK(curthread->td_proc);
		mcp->mc_onstack = sigonstack(td->td_frame->fixreg[1]);
		PROC_UNLOCK(curthread->td_proc);
	}

	return (error);
}

int
set_mcontext(struct thread *td, mcontext_t *mcp)
{
	struct pcb *pcb;
	struct trapframe *tf;
	register_t tls;
	int i;

	pcb = td->td_pcb;
	tf = td->td_frame;

	if (mcp->mc_vers != _MC_VERSION || mcp->mc_len != sizeof(*mcp))
		return (EINVAL);

	/*
	 * Don't let the user set privileged MSR bits
	 */
	if ((mcp->mc_srr1 & psl_userstatic) != (tf->srr1 & psl_userstatic)) {
		return (EINVAL);
	}

	/* Copy trapframe, preserving TLS pointer across context change */
	if (SV_PROC_FLAG(td->td_proc, SV_LP64))
		tls = tf->fixreg[13];
	else
		tls = tf->fixreg[2];
	memcpy(tf, mcp->mc_frame, sizeof(mcp->mc_frame));
	if (SV_PROC_FLAG(td->td_proc, SV_LP64))
		tf->fixreg[13] = tls;
	else
		tf->fixreg[2] = tls;

	/* Disable FPU */
	tf->srr1 &= ~PSL_FP;
	pcb->pcb_flags &= ~PCB_FPU;

	if (mcp->mc_flags & _MC_FP_VALID) {
		/* enable_fpu() will happen lazily on a fault */
		pcb->pcb_flags |= PCB_FPREGS;
		memcpy(&pcb->pcb_fpu.fpscr, &mcp->mc_fpscr, sizeof(double));
		bzero(pcb->pcb_fpu.fpr, sizeof(pcb->pcb_fpu.fpr));
		for (i = 0; i < 32; i++) {
			memcpy(&pcb->pcb_fpu.fpr[i].fpr, &mcp->mc_fpreg[i],
			    sizeof(double));
			memcpy(&pcb->pcb_fpu.fpr[i].vsr[2],
			    &mcp->mc_vsxfpreg[i], sizeof(double));
		}
	}

	if (mcp->mc_flags & _MC_AV_VALID) {
		if ((pcb->pcb_flags & PCB_VEC) != PCB_VEC) {
			critical_enter();
			enable_vec(td);
			critical_exit();
		}
		pcb->pcb_vec.vscr = mcp->mc_vscr;
		pcb->pcb_vec.vrsave = mcp->mc_vrsave;
		memcpy(pcb->pcb_vec.vr, mcp->mc_avec, sizeof(mcp->mc_avec));
	}

	return (0);
}

/*
 * Set set up registers on exec.
 */
void
exec_setregs(struct thread *td, struct image_params *imgp, u_long stack)
{
	struct trapframe	*tf;
	register_t		argc;

	tf = trapframe(td);
	bzero(tf, sizeof *tf);
	#ifdef __powerpc64__
	tf->fixreg[1] = -roundup(-stack + 48, 16);
	#else
	tf->fixreg[1] = -roundup(-stack + 8, 16);
	#endif

	/*
	 * Set up arguments for _start():
	 *	_start(argc, argv, envp, obj, cleanup, ps_strings);
	 *
	 * Notes:
	 *	- obj and cleanup are the auxilliary and termination
	 *	  vectors.  They are fixed up by ld.elf_so.
	 *	- ps_strings is a NetBSD extention, and will be
	 * 	  ignored by executables which are strictly
	 *	  compliant with the SVR4 ABI.
	 */

	/* Collect argc from the user stack */
	argc = fuword((void *)stack);

	tf->fixreg[3] = argc;
	tf->fixreg[4] = stack + sizeof(register_t);
	tf->fixreg[5] = stack + (2 + argc)*sizeof(register_t);
	tf->fixreg[6] = 0;				/* auxillary vector */
	tf->fixreg[7] = 0;				/* termination vector */
	tf->fixreg[8] = (register_t)imgp->ps_strings;	/* NetBSD extension */

	tf->srr0 = imgp->entry_addr;
	#ifdef __powerpc64__
	tf->fixreg[12] = imgp->entry_addr;
	#endif
	tf->srr1 = psl_userset | PSL_FE_DFLT;
	td->td_pcb->pcb_flags = 0;
}

#ifdef COMPAT_FREEBSD32
void
ppc32_setregs(struct thread *td, struct image_params *imgp, u_long stack)
{
	struct trapframe	*tf;
	uint32_t		argc;

	tf = trapframe(td);
	bzero(tf, sizeof *tf);
	tf->fixreg[1] = -roundup(-stack + 8, 16);

	argc = fuword32((void *)stack);

	tf->fixreg[3] = argc;
	tf->fixreg[4] = stack + sizeof(uint32_t);
	tf->fixreg[5] = stack + (2 + argc)*sizeof(uint32_t);
	tf->fixreg[6] = 0;				/* auxillary vector */
	tf->fixreg[7] = 0;				/* termination vector */
	tf->fixreg[8] = (register_t)imgp->ps_strings;	/* NetBSD extension */

	tf->srr0 = imgp->entry_addr;
	tf->srr1 = psl_userset32 | PSL_FE_DFLT;
	td->td_pcb->pcb_flags = 0;
}
#endif

int
fill_regs(struct thread *td, struct reg *regs)
{
	struct trapframe *tf;

	tf = td->td_frame;
	memcpy(regs, tf, sizeof(struct reg));

	return (0);
}

int
fill_dbregs(struct thread *td, struct dbreg *dbregs)
{
	/* No debug registers on PowerPC */
	return (ENOSYS);
}

int
fill_fpregs(struct thread *td, struct fpreg *fpregs)
{
	struct pcb *pcb;
	int i;

	pcb = td->td_pcb;

	if ((pcb->pcb_flags & PCB_FPREGS) == 0)
		memset(fpregs, 0, sizeof(struct fpreg));
	else {
		memcpy(&fpregs->fpscr, &pcb->pcb_fpu.fpscr, sizeof(double));
		for (i = 0; i < 32; i++)
			memcpy(&fpregs->fpreg[i], &pcb->pcb_fpu.fpr[i].fpr,
			    sizeof(double));
	}

	return (0);
}

int
set_regs(struct thread *td, struct reg *regs)
{
	struct trapframe *tf;

	tf = td->td_frame;
	memcpy(tf, regs, sizeof(struct reg));
	
	return (0);
}

int
set_dbregs(struct thread *td, struct dbreg *dbregs)
{
	/* No debug registers on PowerPC */
	return (ENOSYS);
}

int
set_fpregs(struct thread *td, struct fpreg *fpregs)
{
	struct pcb *pcb;
	int i;

	pcb = td->td_pcb;
	pcb->pcb_flags |= PCB_FPREGS;
	memcpy(&pcb->pcb_fpu.fpscr, &fpregs->fpscr, sizeof(double));
	for (i = 0; i < 32; i++) {
		memcpy(&pcb->pcb_fpu.fpr[i].fpr, &fpregs->fpreg[i],
		    sizeof(double));
	}

	return (0);
}

#ifdef COMPAT_FREEBSD32
int
set_regs32(struct thread *td, struct reg32 *regs)
{
	struct trapframe *tf;
	int i;

	tf = td->td_frame;
	for (i = 0; i < 32; i++)
		tf->fixreg[i] = regs->fixreg[i];
	tf->lr = regs->lr;
	tf->cr = regs->cr;
	tf->xer = regs->xer;
	tf->ctr = regs->ctr;
	tf->srr0 = regs->pc;

	return (0);
}

int
fill_regs32(struct thread *td, struct reg32 *regs)
{
	struct trapframe *tf;
	int i;

	tf = td->td_frame;
	for (i = 0; i < 32; i++)
		regs->fixreg[i] = tf->fixreg[i];
	regs->lr = tf->lr;
	regs->cr = tf->cr;
	regs->xer = tf->xer;
	regs->ctr = tf->ctr;
	regs->pc = tf->srr0;

	return (0);
}

static int
grab_mcontext32(struct thread *td, mcontext32_t *mcp, int flags)
{
	mcontext_t mcp64;
	int i, error;

	error = grab_mcontext(td, &mcp64, flags);
	if (error != 0)
		return (error);
	
	mcp->mc_vers = mcp64.mc_vers;
	mcp->mc_flags = mcp64.mc_flags;
	mcp->mc_onstack = mcp64.mc_onstack;
	mcp->mc_len = mcp64.mc_len;
	memcpy(mcp->mc_avec,mcp64.mc_avec,sizeof(mcp64.mc_avec));
	memcpy(mcp->mc_av,mcp64.mc_av,sizeof(mcp64.mc_av));
	for (i = 0; i < 42; i++)
		mcp->mc_frame[i] = mcp64.mc_frame[i];
	memcpy(mcp->mc_fpreg,mcp64.mc_fpreg,sizeof(mcp64.mc_fpreg));
	memcpy(mcp->mc_vsxfpreg,mcp64.mc_vsxfpreg,sizeof(mcp64.mc_vsxfpreg));

	return (0);
}

static int
get_mcontext32(struct thread *td, mcontext32_t *mcp, int flags)
{
	int error;

	error = grab_mcontext32(td, mcp, flags);
	if (error == 0) {
		PROC_LOCK(curthread->td_proc);
		mcp->mc_onstack = sigonstack(td->td_frame->fixreg[1]);
		PROC_UNLOCK(curthread->td_proc);
	}

	return (error);
}

static int
set_mcontext32(struct thread *td, mcontext32_t *mcp)
{
	mcontext_t mcp64;
	int i, error;

	mcp64.mc_vers = mcp->mc_vers;
	mcp64.mc_flags = mcp->mc_flags;
	mcp64.mc_onstack = mcp->mc_onstack;
	mcp64.mc_len = mcp->mc_len;
	memcpy(mcp64.mc_avec,mcp->mc_avec,sizeof(mcp64.mc_avec));
	memcpy(mcp64.mc_av,mcp->mc_av,sizeof(mcp64.mc_av));
	for (i = 0; i < 42; i++)
		mcp64.mc_frame[i] = mcp->mc_frame[i];
	mcp64.mc_srr1 |= (td->td_frame->srr1 & 0xFFFFFFFF00000000ULL);
	memcpy(mcp64.mc_fpreg,mcp->mc_fpreg,sizeof(mcp64.mc_fpreg));
	memcpy(mcp64.mc_vsxfpreg,mcp->mc_vsxfpreg,sizeof(mcp64.mc_vsxfpreg));

	error = set_mcontext(td, &mcp64);

	return (error);
}
#endif

#ifdef COMPAT_FREEBSD32
int
freebsd32_sigreturn(struct thread *td, struct freebsd32_sigreturn_args *uap)
{
	ucontext32_t uc;
	int error;

	CTR2(KTR_SIG, "sigreturn: td=%p ucp=%p", td, uap->sigcntxp);

	if (copyin(uap->sigcntxp, &uc, sizeof(uc)) != 0) {
		CTR1(KTR_SIG, "sigreturn: efault td=%p", td);
		return (EFAULT);
	}

	error = set_mcontext32(td, &uc.uc_mcontext);
	if (error != 0)
		return (error);

	kern_sigprocmask(td, SIG_SETMASK, &uc.uc_sigmask, NULL, 0);

	CTR3(KTR_SIG, "sigreturn: return td=%p pc=%#x sp=%#x",
	     td, uc.uc_mcontext.mc_srr0, uc.uc_mcontext.mc_gpr[1]);

	return (EJUSTRETURN);
}

/*
 * The first two fields of a ucontext_t are the signal mask and the machine
 * context.  The next field is uc_link; we want to avoid destroying the link
 * when copying out contexts.
 */
#define	UC32_COPY_SIZE	offsetof(ucontext32_t, uc_link)

int
freebsd32_getcontext(struct thread *td, struct freebsd32_getcontext_args *uap)
{
	ucontext32_t uc;
	int ret;

	if (uap->ucp == NULL)
		ret = EINVAL;
	else {
		bzero(&uc, sizeof(uc));
		get_mcontext32(td, &uc.uc_mcontext, GET_MC_CLEAR_RET);
		PROC_LOCK(td->td_proc);
		uc.uc_sigmask = td->td_sigmask;
		PROC_UNLOCK(td->td_proc);
		ret = copyout(&uc, uap->ucp, UC32_COPY_SIZE);
	}
	return (ret);
}

int
freebsd32_setcontext(struct thread *td, struct freebsd32_setcontext_args *uap)
{
	ucontext32_t uc;
	int ret;	

	if (uap->ucp == NULL)
		ret = EINVAL;
	else {
		ret = copyin(uap->ucp, &uc, UC32_COPY_SIZE);
		if (ret == 0) {
			ret = set_mcontext32(td, &uc.uc_mcontext);
			if (ret == 0) {
				kern_sigprocmask(td, SIG_SETMASK,
				    &uc.uc_sigmask, NULL, 0);
			}
		}
	}
	return (ret == 0 ? EJUSTRETURN : ret);
}

int
freebsd32_swapcontext(struct thread *td, struct freebsd32_swapcontext_args *uap)
{
	ucontext32_t uc;
	int ret;

	if (uap->oucp == NULL || uap->ucp == NULL)
		ret = EINVAL;
	else {
		bzero(&uc, sizeof(uc));
		get_mcontext32(td, &uc.uc_mcontext, GET_MC_CLEAR_RET);
		PROC_LOCK(td->td_proc);
		uc.uc_sigmask = td->td_sigmask;
		PROC_UNLOCK(td->td_proc);
		ret = copyout(&uc, uap->oucp, UC32_COPY_SIZE);
		if (ret == 0) {
			ret = copyin(uap->ucp, &uc, UC32_COPY_SIZE);
			if (ret == 0) {
				ret = set_mcontext32(td, &uc.uc_mcontext);
				if (ret == 0) {
					kern_sigprocmask(td, SIG_SETMASK,
					    &uc.uc_sigmask, NULL, 0);
				}
			}
		}
	}
	return (ret == 0 ? EJUSTRETURN : ret);
}

#endif

void
cpu_set_syscall_retval(struct thread *td, int error)
{
	struct proc *p;
	struct trapframe *tf;
	int fixup;

	if (error == EJUSTRETURN)
		return;

	p = td->td_proc;
	tf = td->td_frame;

	if (tf->fixreg[0] == SYS___syscall &&
	    (SV_PROC_FLAG(p, SV_ILP32))) {
		int code = tf->fixreg[FIRSTARG + 1];
		fixup = (
#if defined(COMPAT_FREEBSD6) && defined(SYS_freebsd6_lseek)
		    code != SYS_freebsd6_lseek &&
#endif
		    code != SYS_lseek) ?  1 : 0;
	} else
		fixup = 0;

	switch (error) {
	case 0:
		if (fixup) {
			/*
			 * 64-bit return, 32-bit syscall. Fixup byte order
			 */
			tf->fixreg[FIRSTARG] = 0;
			tf->fixreg[FIRSTARG + 1] = td->td_retval[0];
		} else {
			tf->fixreg[FIRSTARG] = td->td_retval[0];
			tf->fixreg[FIRSTARG + 1] = td->td_retval[1];
		}
		tf->cr &= ~0x10000000;		/* Unset summary overflow */
		break;
	case ERESTART:
		/*
		 * Set user's pc back to redo the system call.
		 */
		tf->srr0 -= 4;
		break;
	default:
		tf->fixreg[FIRSTARG] = SV_ABI_ERRNO(p, error);
		tf->cr |= 0x10000000;		/* Set summary overflow */
		break;
	}
}

/*
 * Threading functions
 */
void
cpu_thread_exit(struct thread *td)
{
}

void
cpu_thread_clean(struct thread *td)
{
}

void
cpu_thread_alloc(struct thread *td)
{
	struct pcb *pcb;

	pcb = (struct pcb *)((td->td_kstack + td->td_kstack_pages * PAGE_SIZE -
	    sizeof(struct pcb)) & ~0x2fUL);
	td->td_pcb = pcb;
	td->td_frame = (struct trapframe *)pcb - 1;
}

void
cpu_thread_free(struct thread *td)
{
}

int
cpu_set_user_tls(struct thread *td, void *tls_base)
{

	if (SV_PROC_FLAG(td->td_proc, SV_LP64))
		td->td_frame->fixreg[13] = (register_t)tls_base + 0x7010;
	else
		td->td_frame->fixreg[2] = (register_t)tls_base + 0x7008;
	return (0);
}

void
cpu_copy_thread(struct thread *td, struct thread *td0)
{
	struct pcb *pcb2;
	struct trapframe *tf;
	struct callframe *cf;

	pcb2 = td->td_pcb;

	/* Copy the upcall pcb */
	bcopy(td0->td_pcb, pcb2, sizeof(*pcb2));

	/* Create a stack for the new thread */
	tf = td->td_frame;
	bcopy(td0->td_frame, tf, sizeof(struct trapframe));
	tf->fixreg[FIRSTARG] = 0;
	tf->fixreg[FIRSTARG + 1] = 0;
	tf->cr &= ~0x10000000;

	/* Set registers for trampoline to user mode. */
	cf = (struct callframe *)tf - 1;
	memset(cf, 0, sizeof(struct callframe));
	cf->cf_func = (register_t)fork_return;
	cf->cf_arg0 = (register_t)td;
	cf->cf_arg1 = (register_t)tf;

	pcb2->pcb_sp = (register_t)cf;
	#if defined(__powerpc64__) && (!defined(_CALL_ELF) || _CALL_ELF == 1)
	pcb2->pcb_lr = ((register_t *)fork_trampoline)[0];
	pcb2->pcb_toc = ((register_t *)fork_trampoline)[1];
	#else
	pcb2->pcb_lr = (register_t)fork_trampoline;
	pcb2->pcb_context[0] = pcb2->pcb_lr;
	#endif
	pcb2->pcb_cpu.aim.usr_vsid = 0;
#ifdef __SPE__
	pcb2->pcb_vec.vscr = SPEFSCR_FINVE | SPEFSCR_FDBZE |
	    SPEFSCR_FUNFE | SPEFSCR_FOVFE;
#endif

	/* Setup to release spin count in fork_exit(). */
	td->td_md.md_spinlock_count = 1;
	td->td_md.md_saved_msr = psl_kernset;
}

void
cpu_set_upcall(struct thread *td, void (*entry)(void *), void *arg,
    stack_t *stack)
{
	struct trapframe *tf;
	uintptr_t sp;

	tf = td->td_frame;
	/* align stack and alloc space for frame ptr and saved LR */
	#ifdef __powerpc64__
	sp = ((uintptr_t)stack->ss_sp + stack->ss_size - 48) &
	    ~0x1f;
	#else
	sp = ((uintptr_t)stack->ss_sp + stack->ss_size - 8) &
	    ~0x1f;
	#endif
	bzero(tf, sizeof(struct trapframe));

	tf->fixreg[1] = (register_t)sp;
	tf->fixreg[3] = (register_t)arg;
	if (SV_PROC_FLAG(td->td_proc, SV_ILP32)) {
		tf->srr0 = (register_t)entry;
		#ifdef __powerpc64__
		tf->srr1 = psl_userset32 | PSL_FE_DFLT;
		#else
		tf->srr1 = psl_userset | PSL_FE_DFLT;
		#endif
	} else {
	    #ifdef __powerpc64__
		if (td->td_proc->p_sysent == &elf64_freebsd_sysvec_v2) {
			tf->srr0 = (register_t)entry;
			/* ELFv2 ABI requires that the global entry point be in r12. */
			tf->fixreg[12] = (register_t)entry;
		}
		else {
			register_t entry_desc[3];
			(void)copyin((void *)entry, entry_desc, sizeof(entry_desc));
			tf->srr0 = entry_desc[0];
			tf->fixreg[2] = entry_desc[1];
			tf->fixreg[11] = entry_desc[2];
		}
		tf->srr1 = psl_userset | PSL_FE_DFLT;
	    #endif
	}

	td->td_pcb->pcb_flags = 0;
#ifdef __SPE__
	td->td_pcb->pcb_vec.vscr = SPEFSCR_FINVE | SPEFSCR_FDBZE |
	    SPEFSCR_FUNFE | SPEFSCR_FOVFE;
#endif

	td->td_retval[0] = (register_t)entry;
	td->td_retval[1] = 0;
}

static int
emulate_mfspr(int spr, int reg, struct trapframe *frame){
	struct thread *td;

	td = curthread;

	if (spr == SPR_DSCR) {
		// If DSCR was never set, get the default DSCR
		if ((td->td_pcb->pcb_flags & PCB_CDSCR) == 0)
			td->td_pcb->pcb_dscr = mfspr(SPR_DSCR);

		frame->fixreg[reg] = td->td_pcb->pcb_dscr;
		frame->srr0 += 4;
		return 0;
	} else
		return SIGILL;
}

static int
emulate_mtspr(int spr, int reg, struct trapframe *frame){
	struct thread *td;

	td = curthread;

	if (spr == SPR_DSCR) {
		td->td_pcb->pcb_flags |= PCB_CDSCR;
		td->td_pcb->pcb_dscr = frame->fixreg[reg];
		frame->srr0 += 4;
		return 0;
	} else
		return SIGILL;
}

#define XFX 0xFC0007FF
int
ppc_instr_emulate(struct trapframe *frame, struct pcb *pcb)
{
	uint32_t instr;
	int reg, sig;
	int rs, spr;

	instr = fuword32((void *)frame->srr0);
	sig = SIGILL;

	if ((instr & 0xfc1fffff) == 0x7c1f42a6) {	/* mfpvr */
		reg = (instr & ~0xfc1fffff) >> 21;
		frame->fixreg[reg] = mfpvr();
		frame->srr0 += 4;
		return (0);
	} else if ((instr & XFX) == 0x7c0002a6) {	/* mfspr */
		rs = (instr &  0x3e00000) >> 21;
		spr = (instr & 0x1ff800) >> 16;
		return emulate_mfspr(spr, rs, frame);
	} else if ((instr & XFX) == 0x7c0003a6) {	/* mtspr */
		rs = (instr &  0x3e00000) >> 21;
		spr = (instr & 0x1ff800) >> 16;
		return emulate_mtspr(spr, rs, frame);
	} else if ((instr & 0xfc000ffe) == 0x7c0004ac) {	/* various sync */
		powerpc_sync(); /* Do a heavy-weight sync */
		frame->srr0 += 4;
		return (0);
	}

#ifdef FPU_EMU
	if (!(pcb->pcb_flags & PCB_FPREGS)) {
		bzero(&pcb->pcb_fpu, sizeof(pcb->pcb_fpu));
		pcb->pcb_flags |= PCB_FPREGS;
	}
	sig = fpu_emulate(frame, &pcb->pcb_fpu);
#endif
	if (sig == SIGILL) {
		if (pcb->pcb_lastill != frame->srr0) {
			/* Allow a second chance, in case of cache sync issues. */
			sig = 0;
			pmap_sync_icache(PCPU_GET(curpmap), frame->srr0, 4);
			pcb->pcb_lastill = frame->srr0;
		}
	}

	return (sig);
}

