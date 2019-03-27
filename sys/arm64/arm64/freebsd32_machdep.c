/*-
 * Copyright (c) 2018 Olivier Houchard
 * Copyright (c) 2017 Nuxi, https://nuxi.nl/
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

#include <sys/types.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/syscallsubr.h>
#include <sys/ktr.h>
#include <sys/sysent.h>
#include <machine/armreg.h>
#ifdef VFP
#include <machine/vfp.h>
#endif
#include <compat/freebsd32/freebsd32_proto.h>
#include <compat/freebsd32/freebsd32_signal.h>

extern void freebsd32_sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask);

/*
 * The first two fields of a ucontext_t are the signal mask and the machine
 * context.  The next field is uc_link; we want to avoid destroying the link
 * when copying out contexts.
 */
#define UC32_COPY_SIZE  offsetof(ucontext32_t, uc_link)

#ifdef VFP
static void get_fpcontext32(struct thread *td, mcontext32_vfp_t *);
#endif

/*
 * Stubs for machine dependent 32-bits system calls.
 */

int
freebsd32_sysarch(struct thread *td, struct freebsd32_sysarch_args *uap)
{
	int error;

#define ARM_SYNC_ICACHE		0
#define ARM_DRAIN_WRITEBUF	1
#define ARM_SET_TP		2
#define ARM_GET_TP		3
#define ARM_GET_VFPSTATE	4

	switch(uap->op) {
	case ARM_SET_TP:
		WRITE_SPECIALREG(TPIDR_EL0, uap->parms);
		WRITE_SPECIALREG(TPIDRRO_EL0, uap->parms);
		return 0;
	case ARM_SYNC_ICACHE:
		{
			struct {
				uint32_t addr;
				uint32_t size;
			} args;

			if ((error = copyin(uap->parms, &args, sizeof(args))) != 0)
				return (error);
			if ((uint64_t)args.addr + (uint64_t)args.size > 0xffffffff)
				return (EINVAL);
			cpu_icache_sync_range_checked(args.addr, args.size);
			return 0;
		}
	case ARM_GET_VFPSTATE:
		{
			mcontext32_vfp_t mcontext_vfp;

			struct {
				uint32_t mc_vfp_size;
				uint32_t mc_vfp;
			} args;
			if ((error = copyin(uap->parms, &args, sizeof(args))) != 0)
				return (error);
			if (args.mc_vfp_size != sizeof(mcontext_vfp))
				return (EINVAL);
#ifdef VFP
			get_fpcontext32(td, &mcontext_vfp);
#else
			bzero(&mcontext_vfp, sizeof(mcontext_vfp));
#endif
			error = copyout(&mcontext_vfp,
				(void *)(uintptr_t)args.mc_vfp,
				sizeof(mcontext_vfp));
			return error;
		}
	}

	return (EINVAL);
}



#ifdef VFP
static void
get_fpcontext32(struct thread *td, mcontext32_vfp_t *mcp)
{
	struct pcb *curpcb;

	critical_enter();
	curpcb = curthread->td_pcb;

	if ((curpcb->pcb_fpflags & PCB_FP_STARTED) != 0) {
		/*
		 * If we have just been running VFP instructions we will
		 * need to save the state to memcpy it below.
		 */
		vfp_save_state(td, curpcb);

		KASSERT(curpcb->pcb_fpusaved == &curpcb->pcb_fpustate,
				("Called get_fpcontext while the kernel is using the VFP"));
		KASSERT((curpcb->pcb_fpflags & ~PCB_FP_USERMASK) == 0,
				("Non-userspace FPU flags set in get_fpcontext"));
		memcpy(mcp->mcv_reg, curpcb->pcb_fpustate.vfp_regs,
				sizeof(mcp->mcv_reg));
		mcp->mcv_fpscr = VFP_FPSCR_FROM_SRCR(curpcb->pcb_fpustate.vfp_fpcr,
				curpcb->pcb_fpustate.vfp_fpsr);
	}
 critical_exit();
}

static void
set_fpcontext32(struct thread *td, mcontext32_vfp_t *mcp)
{
	struct pcb *pcb;

	critical_enter();
	pcb = td->td_pcb;
	if (td == curthread)
		vfp_discard(td);
	memcpy(pcb->pcb_fpustate.vfp_regs, mcp->mcv_reg,
			sizeof(pcb->pcb_fpustate.vfp_regs));
	pcb->pcb_fpustate.vfp_fpsr = VFP_FPSR_FROM_FPSCR(mcp->mcv_fpscr);
	pcb->pcb_fpustate.vfp_fpcr = VFP_FPSR_FROM_FPSCR(mcp->mcv_fpscr);
	critical_exit();
}
#endif
static void
get_mcontext32(struct thread *td, mcontext32_t *mcp, int flags)
{
	struct pcb *pcb;
	struct trapframe *tf;
	int i;

	pcb = td->td_pcb;
	tf = td->td_frame;

	if ((flags & GET_MC_CLEAR_RET) != 0) {
		mcp->mc_gregset[0] = 0;
		mcp->mc_gregset[16] = tf->tf_spsr & ~PSR_C;
	} else {
		mcp->mc_gregset[0] = tf->tf_x[0];
		mcp->mc_gregset[16] = tf->tf_spsr;
	}
	for (i = 1; i < 15; i++)
		mcp->mc_gregset[i] = tf->tf_x[i];
	mcp->mc_gregset[15] = tf->tf_elr;

	mcp->mc_vfp_size = 0;
	mcp->mc_vfp_ptr = 0;

	memset(mcp->mc_spare, 0, sizeof(mcp->mc_spare));
}

static int
set_mcontext32(struct thread *td, mcontext32_t *mcp)
{
	struct trapframe *tf;
	mcontext32_vfp_t mc_vfp;
	int i;

	tf = td->td_frame;

	for (i = 0; i < 15; i++)
		tf->tf_x[i] = mcp->mc_gregset[i];
	tf->tf_elr = mcp->mc_gregset[15];
	tf->tf_spsr = mcp->mc_gregset[16];
#ifdef VFP
	if (mcp->mc_vfp_size == sizeof(mc_vfp) && mcp->mc_vfp_ptr != 0) {
		if (copyin((void *)(uintptr_t)mcp->mc_vfp_ptr, &mc_vfp,
					sizeof(mc_vfp)) != 0)
			return (EFAULT);
		set_fpcontext32(td, &mc_vfp);
	}
#endif

	return (0);
}

#define UC_COPY_SIZE	offsetof(ucontext32_t, uc_link)

int
freebsd32_getcontext(struct thread *td, struct freebsd32_getcontext_args *uap)
{
	ucontext32_t uc;
	int ret;

	if (uap->ucp == NULL)
		ret = EINVAL;
	else {
		memset(&uc, 0, sizeof(uc));
		get_mcontext32(td, &uc.uc_mcontext, GET_MC_CLEAR_RET);
		PROC_LOCK(td->td_proc);
		uc.uc_sigmask = td->td_sigmask;
		PROC_UNLOCK(td->td_proc);
		ret = copyout(&uc, uap->ucp, UC_COPY_SIZE);
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
		ret = copyin(uap->ucp, &uc, UC_COPY_SIZE);
		if (ret == 0) {
			ret = set_mcontext32(td, &uc.uc_mcontext);
			if (ret == 0)
				kern_sigprocmask(td, SIG_SETMASK, &uc.uc_sigmask,
						NULL, 0);
		}
	}
	return (ret);
}

int
freebsd32_sigreturn(struct thread *td, struct freebsd32_sigreturn_args *uap)
{
	ucontext32_t uc;
	int error;

	if (uap == NULL)
		return (EFAULT);
	if (copyin(uap->sigcntxp, &uc, sizeof(uc)))
		return (EFAULT);
	error = set_mcontext32(td, &uc.uc_mcontext);
	if (error != 0)
		return (0);

	/* Restore signal mask. */
	kern_sigprocmask(td, SIG_SETMASK, &uc.uc_sigmask, NULL, 0);

	return (EJUSTRETURN);

}

int
freebsd32_swapcontext(struct thread *td, struct freebsd32_swapcontext_args *uap)
{
	ucontext32_t uc;
	int ret;

	if (uap->oucp == NULL || uap->ucp == NULL)
		ret = EINVAL;
	else {
		get_mcontext32(td, &uc.uc_mcontext, GET_MC_CLEAR_RET);
		PROC_LOCK(td->td_proc);
		uc.uc_sigmask = td->td_sigmask;
		PROC_UNLOCK(td->td_proc);
		ret = copyout(&uc, uap->oucp, UC32_COPY_SIZE);
		if (ret == 0) {
			ret = copyin(uap->ucp, &uc, UC32_COPY_SIZE);
			if (ret == 0) {
				ret = set_mcontext32(td, &uc.uc_mcontext);
				kern_sigprocmask(td, SIG_SETMASK,
						&uc.uc_sigmask, NULL, 0);
			}
		}
	}
	return (ret);
}

void
freebsd32_sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask)
{
	struct thread *td;
	struct proc *p;
	struct trapframe *tf;
	struct sigframe32 *fp, frame;
	struct sigacts *psp;
	struct siginfo32 siginfo;
	struct sysentvec *sysent;
	int onstack;
	int sig;
	int code;

	siginfo_to_siginfo32(&ksi->ksi_info, &siginfo);
	td = curthread;
	p = td->td_proc;
	PROC_LOCK_ASSERT(p, MA_OWNED);
	sig = ksi->ksi_signo;
	code = ksi->ksi_code;
	psp = p->p_sigacts;
	mtx_assert(&psp->ps_mtx, MA_OWNED);
	tf = td->td_frame;
	onstack = sigonstack(tf->tf_x[13]);

	CTR4(KTR_SIG, "sendsig: td=%p (%s) catcher=%p sig=%d", td, p->p_comm,
	    catcher, sig);

	/* Allocate and validate space for the signal handler context. */
	if ((td->td_pflags & TDP_ALTSTACK) != 0 && !(onstack) &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		fp = (struct sigframe32 *)((uintptr_t)td->td_sigstk.ss_sp +
		    td->td_sigstk.ss_size);
#if defined(COMPAT_43)
		td->td_sigstk.ss_flags |= SS_ONSTACK;
#endif
	} else
		fp = (struct sigframe32 *)td->td_frame->tf_x[13];

	/* make room on the stack */
	fp--;

	/* make the stack aligned */
	fp = (struct sigframe32 *)((unsigned long)(fp) &~ (8 - 1));
	/* Populate the siginfo frame. */
	get_mcontext32(td, &frame.sf_uc.uc_mcontext, 0);
#ifdef VFP
	get_fpcontext32(td, &frame.sf_vfp);
	frame.sf_uc.uc_mcontext.mc_vfp_size = sizeof(fp->sf_vfp);
	frame.sf_uc.uc_mcontext.mc_vfp_ptr = (uint32_t)(uintptr_t)&fp->sf_vfp;
#else
	frame.sf_uc.uc_mcontext.mc_vfp_size = 0;
	frame.sf_uc.uc_mcontext.mc_vfp_ptr = (uint32_t)NULL;
#endif
	frame.sf_si = siginfo;
	frame.sf_uc.uc_sigmask = *mask;
	frame.sf_uc.uc_stack.ss_flags = (td->td_pflags & TDP_ALTSTACK )
	    ? ((onstack) ? SS_ONSTACK : 0) : SS_DISABLE;
	frame.sf_uc.uc_stack.ss_sp = (uintptr_t)td->td_sigstk.ss_sp;
	frame.sf_uc.uc_stack.ss_size = td->td_sigstk.ss_size;

	mtx_unlock(&psp->ps_mtx);
	PROC_UNLOCK(td->td_proc);

	/* Copy the sigframe out to the user's stack. */
	if (copyout(&frame, fp, sizeof(*fp)) != 0) {
		/* Process has trashed its stack. Kill it. */
		CTR2(KTR_SIG, "sendsig: sigexit td=%p fp=%p", td, fp);
		PROC_LOCK(p);
		sigexit(td, SIGILL);
	}

	/*
	 * Build context to run handler in.  We invoke the handler
	 * directly, only returning via the trampoline.  Note the
	 * trampoline version numbers are coordinated with machine-
	 * dependent code in libc.
	 */

	tf->tf_x[0] = sig;
	tf->tf_x[1] = (register_t)&fp->sf_si;
	tf->tf_x[2] = (register_t)&fp->sf_uc;

	/* the trampoline uses r5 as the uc address */
	tf->tf_x[5] = (register_t)&fp->sf_uc;
	tf->tf_elr = (register_t)catcher;
	tf->tf_x[13] = (register_t)fp;
	sysent = p->p_sysent;
	if (sysent->sv_sigcode_base != 0)
		tf->tf_x[14] = (register_t)sysent->sv_sigcode_base;
	else
		tf->tf_x[14] = (register_t)(sysent->sv_psstrings -
		    *(sysent->sv_szsigcode));
	/* Set the mode to enter in the signal handler */
	if ((register_t)catcher & 1)
		tf->tf_spsr |= PSR_T;
	else
		tf->tf_spsr &= ~PSR_T;

	CTR3(KTR_SIG, "sendsig: return td=%p pc=%#x sp=%#x", td, tf->tf_x[14],
	    tf->tf_x[13]);

	PROC_LOCK(p);
	mtx_lock(&psp->ps_mtx);

}
