/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2003 Peter Wemm
 * Copyright (c) 1982, 1987, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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
#include <sys/exec.h>
#include <sys/fcntl.h>
#include <sys/imgact.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/mman.h>
#include <sys/namei.h>
#include <sys/pioctl.h>
#include <sys/proc.h>
#include <sys/procfs.h>
#include <sys/resourcevar.h>
#include <sys/systm.h>
#include <sys/signalvar.h>
#include <sys/stat.h>
#include <sys/sx.h>
#include <sys/syscall.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/vnode.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>

#include <compat/freebsd32/freebsd32_signal.h>
#include <compat/freebsd32/freebsd32_util.h>
#include <compat/freebsd32/freebsd32_proto.h>
#include <compat/freebsd32/freebsd32.h>
#include <compat/ia32/ia32_signal.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/specialreg.h>
#include <machine/frame.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/cpufunc.h>
#include <machine/trap.h>

#ifdef COMPAT_FREEBSD4
static void freebsd4_ia32_sendsig(sig_t, ksiginfo_t *, sigset_t *);
#endif

#define	CS_SECURE(cs)		(ISPL(cs) == SEL_UPL)
#define	EFL_SECURE(ef, oef)	((((ef) ^ (oef)) & ~PSL_USERCHANGE) == 0)

static void
ia32_get_fpcontext(struct thread *td, struct ia32_mcontext *mcp,
    char *xfpusave, size_t xfpusave_len)
{
	size_t max_len, len;

	/*
	 * XXX Format of 64bit and 32bit FXSAVE areas differs. FXSAVE
	 * in 32bit mode saves %cs and %ds, while on 64bit it saves
	 * 64bit instruction and data pointers. Ignore the difference
	 * for now, it should be irrelevant for most applications.
	 */
	mcp->mc_ownedfp = fpugetregs(td);
	bcopy(get_pcb_user_save_td(td), &mcp->mc_fpstate[0],
	    sizeof(mcp->mc_fpstate));
	mcp->mc_fpformat = fpuformat();
	if (!use_xsave || xfpusave_len == 0)
		return;
	max_len = cpu_max_ext_state_size - sizeof(struct savefpu);
	len = xfpusave_len;
	if (len > max_len) {
		len = max_len;
		bzero(xfpusave + max_len, len - max_len);
	}
	mcp->mc_flags |= _MC_IA32_HASFPXSTATE;
	mcp->mc_xfpustate_len = len;
	bcopy(get_pcb_user_save_td(td) + 1, xfpusave, len);
}

static int
ia32_set_fpcontext(struct thread *td, struct ia32_mcontext *mcp,
    char *xfpustate, size_t xfpustate_len)
{
	int error;

	if (mcp->mc_fpformat == _MC_FPFMT_NODEV)
		return (0);
	else if (mcp->mc_fpformat != _MC_FPFMT_XMM)
		return (EINVAL);
	else if (mcp->mc_ownedfp == _MC_FPOWNED_NONE) {
		/* We don't care what state is left in the FPU or PCB. */
		fpstate_drop(td);
		error = 0;
	} else if (mcp->mc_ownedfp == _MC_FPOWNED_FPU ||
	    mcp->mc_ownedfp == _MC_FPOWNED_PCB) {
		error = fpusetregs(td, (struct savefpu *)&mcp->mc_fpstate,
		    xfpustate, xfpustate_len);
	} else
		return (EINVAL);
	return (error);
}

/*
 * Get machine context.
 */
static int
ia32_get_mcontext(struct thread *td, struct ia32_mcontext *mcp, int flags)
{
	struct pcb *pcb;
	struct trapframe *tp;

	pcb = td->td_pcb;
	tp = td->td_frame;

	PROC_LOCK(curthread->td_proc);
	mcp->mc_onstack = sigonstack(tp->tf_rsp);
	PROC_UNLOCK(curthread->td_proc);
	/* Entry into kernel always sets TF_HASSEGS */
	mcp->mc_gs = tp->tf_gs;
	mcp->mc_fs = tp->tf_fs;
	mcp->mc_es = tp->tf_es;
	mcp->mc_ds = tp->tf_ds;
	mcp->mc_edi = tp->tf_rdi;
	mcp->mc_esi = tp->tf_rsi;
	mcp->mc_ebp = tp->tf_rbp;
	mcp->mc_isp = tp->tf_rsp;
	mcp->mc_eflags = tp->tf_rflags;
	if (flags & GET_MC_CLEAR_RET) {
		mcp->mc_eax = 0;
		mcp->mc_edx = 0;
		mcp->mc_eflags &= ~PSL_C;
	} else {
		mcp->mc_eax = tp->tf_rax;
		mcp->mc_edx = tp->tf_rdx;
	}
	mcp->mc_ebx = tp->tf_rbx;
	mcp->mc_ecx = tp->tf_rcx;
	mcp->mc_eip = tp->tf_rip;
	mcp->mc_cs = tp->tf_cs;
	mcp->mc_esp = tp->tf_rsp;
	mcp->mc_ss = tp->tf_ss;
	mcp->mc_len = sizeof(*mcp);
	mcp->mc_flags = tp->tf_flags;
	ia32_get_fpcontext(td, mcp, NULL, 0);
	mcp->mc_fsbase = pcb->pcb_fsbase;
	mcp->mc_gsbase = pcb->pcb_gsbase;
	mcp->mc_xfpustate = 0;
	mcp->mc_xfpustate_len = 0;
	bzero(mcp->mc_spare2, sizeof(mcp->mc_spare2));
	return (0);
}

/*
 * Set machine context.
 *
 * However, we don't set any but the user modifiable flags, and we won't
 * touch the cs selector.
 */
static int
ia32_set_mcontext(struct thread *td, struct ia32_mcontext *mcp)
{
	struct trapframe *tp;
	char *xfpustate;
	long rflags;
	int ret;

	tp = td->td_frame;
	if (mcp->mc_len != sizeof(*mcp))
		return (EINVAL);
	rflags = (mcp->mc_eflags & PSL_USERCHANGE) |
	    (tp->tf_rflags & ~PSL_USERCHANGE);
	if (mcp->mc_flags & _MC_IA32_HASFPXSTATE) {
		if (mcp->mc_xfpustate_len > cpu_max_ext_state_size -
		    sizeof(struct savefpu))
			return (EINVAL);
		xfpustate = __builtin_alloca(mcp->mc_xfpustate_len);
		ret = copyin(PTRIN(mcp->mc_xfpustate), xfpustate,
		    mcp->mc_xfpustate_len);
		if (ret != 0)
			return (ret);
	} else
		xfpustate = NULL;
	ret = ia32_set_fpcontext(td, mcp, xfpustate, mcp->mc_xfpustate_len);
	if (ret != 0)
		return (ret);
	tp->tf_gs = mcp->mc_gs;
	tp->tf_fs = mcp->mc_fs;
	tp->tf_es = mcp->mc_es;
	tp->tf_ds = mcp->mc_ds;
	tp->tf_flags = TF_HASSEGS;
	tp->tf_rdi = mcp->mc_edi;
	tp->tf_rsi = mcp->mc_esi;
	tp->tf_rbp = mcp->mc_ebp;
	tp->tf_rbx = mcp->mc_ebx;
	tp->tf_rdx = mcp->mc_edx;
	tp->tf_rcx = mcp->mc_ecx;
	tp->tf_rax = mcp->mc_eax;
	/* trapno, err */
	tp->tf_rip = mcp->mc_eip;
	tp->tf_rflags = rflags;
	tp->tf_rsp = mcp->mc_esp;
	tp->tf_ss = mcp->mc_ss;
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	return (0);
}

/*
 * The first two fields of a ucontext_t are the signal mask and
 * the machine context.  The next field is uc_link; we want to
 * avoid destroying the link when copying out contexts.
 */
#define	UC_COPY_SIZE	offsetof(struct ia32_ucontext, uc_link)

int
freebsd32_getcontext(struct thread *td, struct freebsd32_getcontext_args *uap)
{
	struct ia32_ucontext uc;
	int ret;

	if (uap->ucp == NULL)
		ret = EINVAL;
	else {
		bzero(&uc, sizeof(uc));
		ia32_get_mcontext(td, &uc.uc_mcontext, GET_MC_CLEAR_RET);
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
	struct ia32_ucontext uc;
	int ret;

	if (uap->ucp == NULL)
		ret = EINVAL;
	else {
		ret = copyin(uap->ucp, &uc, UC_COPY_SIZE);
		if (ret == 0) {
			ret = ia32_set_mcontext(td, &uc.uc_mcontext);
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
	struct ia32_ucontext uc;
	int ret;

	if (uap->oucp == NULL || uap->ucp == NULL)
		ret = EINVAL;
	else {
		bzero(&uc, sizeof(uc));
		ia32_get_mcontext(td, &uc.uc_mcontext, GET_MC_CLEAR_RET);
		PROC_LOCK(td->td_proc);
		uc.uc_sigmask = td->td_sigmask;
		PROC_UNLOCK(td->td_proc);
		ret = copyout(&uc, uap->oucp, UC_COPY_SIZE);
		if (ret == 0) {
			ret = copyin(uap->ucp, &uc, UC_COPY_SIZE);
			if (ret == 0) {
				ret = ia32_set_mcontext(td, &uc.uc_mcontext);
				if (ret == 0) {
					kern_sigprocmask(td, SIG_SETMASK,
					    &uc.uc_sigmask, NULL, 0);
				}
			}
		}
	}
	return (ret == 0 ? EJUSTRETURN : ret);
}

/*
 * Send an interrupt to process.
 *
 * Stack is set up to allow sigcode stored
 * at top to call routine, followed by kcall
 * to sigreturn routine below.  After sigreturn
 * resets the signal mask, the stack, and the
 * frame pointer, it returns to the user
 * specified pc, psl.
 */

#ifdef COMPAT_43
static void
ia32_osendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask)
{
	struct ia32_sigframe3 sf, *fp;
	struct proc *p;
	struct thread *td;
	struct sigacts *psp;
	struct trapframe *regs;
	int sig;
	int oonstack;

	td = curthread;
	p = td->td_proc;
	PROC_LOCK_ASSERT(p, MA_OWNED);
	sig = ksi->ksi_signo;
	psp = p->p_sigacts;
	mtx_assert(&psp->ps_mtx, MA_OWNED);
	regs = td->td_frame;
	oonstack = sigonstack(regs->tf_rsp);

	/* Allocate space for the signal handler context. */
	if ((td->td_pflags & TDP_ALTSTACK) && !oonstack &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		fp = (struct ia32_sigframe3 *)((uintptr_t)td->td_sigstk.ss_sp +
		    td->td_sigstk.ss_size - sizeof(sf));
		td->td_sigstk.ss_flags |= SS_ONSTACK;
	} else
		fp = (struct ia32_sigframe3 *)regs->tf_rsp - 1;

	/* Build the argument list for the signal handler. */
	sf.sf_signum = sig;
	sf.sf_scp = (register_t)&fp->sf_siginfo.si_sc;
	bzero(&sf.sf_siginfo, sizeof(sf.sf_siginfo));
	if (SIGISMEMBER(psp->ps_siginfo, sig)) {
		/* Signal handler installed with SA_SIGINFO. */
		sf.sf_arg2 = (register_t)&fp->sf_siginfo;
		sf.sf_siginfo.si_signo = sig;
		sf.sf_siginfo.si_code = ksi->ksi_code;
		sf.sf_ah = (uintptr_t)catcher;
		sf.sf_addr = 0;
	} else {
		/* Old FreeBSD-style arguments. */
		sf.sf_arg2 = ksi->ksi_code;
		sf.sf_addr = (register_t)ksi->ksi_addr;
		sf.sf_ah = (uintptr_t)catcher;
	}
	mtx_unlock(&psp->ps_mtx);
	PROC_UNLOCK(p);

	/* Save most if not all of trap frame. */
	sf.sf_siginfo.si_sc.sc_eax = regs->tf_rax;
	sf.sf_siginfo.si_sc.sc_ebx = regs->tf_rbx;
	sf.sf_siginfo.si_sc.sc_ecx = regs->tf_rcx;
	sf.sf_siginfo.si_sc.sc_edx = regs->tf_rdx;
	sf.sf_siginfo.si_sc.sc_esi = regs->tf_rsi;
	sf.sf_siginfo.si_sc.sc_edi = regs->tf_rdi;
	sf.sf_siginfo.si_sc.sc_cs = regs->tf_cs;
	sf.sf_siginfo.si_sc.sc_ds = regs->tf_ds;
	sf.sf_siginfo.si_sc.sc_ss = regs->tf_ss;
	sf.sf_siginfo.si_sc.sc_es = regs->tf_es;
	sf.sf_siginfo.si_sc.sc_fs = regs->tf_fs;
	sf.sf_siginfo.si_sc.sc_gs = regs->tf_gs;
	sf.sf_siginfo.si_sc.sc_isp = regs->tf_rsp;

	/* Build the signal context to be used by osigreturn(). */
	sf.sf_siginfo.si_sc.sc_onstack = (oonstack) ? 1 : 0;
	SIG2OSIG(*mask, sf.sf_siginfo.si_sc.sc_mask);
	sf.sf_siginfo.si_sc.sc_esp = regs->tf_rsp;
	sf.sf_siginfo.si_sc.sc_ebp = regs->tf_rbp;
	sf.sf_siginfo.si_sc.sc_eip = regs->tf_rip;
	sf.sf_siginfo.si_sc.sc_eflags = regs->tf_rflags;
	sf.sf_siginfo.si_sc.sc_trapno = regs->tf_trapno;
	sf.sf_siginfo.si_sc.sc_err = regs->tf_err;

	/*
	 * Copy the sigframe out to the user's stack.
	 */
	if (copyout(&sf, fp, sizeof(*fp)) != 0) {
#ifdef DEBUG
		printf("process %ld has trashed its stack\n", (long)p->p_pid);
#endif
		PROC_LOCK(p);
		sigexit(td, SIGILL);
	}

	regs->tf_rsp = (uintptr_t)fp;
	regs->tf_rip = p->p_sysent->sv_psstrings - sz_ia32_osigcode;
	regs->tf_rflags &= ~(PSL_T | PSL_D);
	regs->tf_cs = _ucode32sel;
	regs->tf_ds = _udatasel;
	regs->tf_es = _udatasel;
	regs->tf_fs = _udatasel;
	regs->tf_ss = _udatasel;
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	PROC_LOCK(p);
	mtx_lock(&psp->ps_mtx);
}
#endif

#ifdef COMPAT_FREEBSD4
static void
freebsd4_ia32_sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask)
{
	struct ia32_sigframe4 sf, *sfp;
	struct siginfo32 siginfo;
	struct proc *p;
	struct thread *td;
	struct sigacts *psp;
	struct trapframe *regs;
	int oonstack;
	int sig;

	td = curthread;
	p = td->td_proc;
	siginfo_to_siginfo32(&ksi->ksi_info, &siginfo);

	PROC_LOCK_ASSERT(p, MA_OWNED);
	sig = siginfo.si_signo;
	psp = p->p_sigacts;
	mtx_assert(&psp->ps_mtx, MA_OWNED);
	regs = td->td_frame;
	oonstack = sigonstack(regs->tf_rsp);

	/* Save user context. */
	bzero(&sf, sizeof(sf));
	sf.sf_uc.uc_sigmask = *mask;
	sf.sf_uc.uc_stack.ss_sp = (uintptr_t)td->td_sigstk.ss_sp;
	sf.sf_uc.uc_stack.ss_size = td->td_sigstk.ss_size;
	sf.sf_uc.uc_stack.ss_flags = (td->td_pflags & TDP_ALTSTACK)
	    ? ((oonstack) ? SS_ONSTACK : 0) : SS_DISABLE;
	sf.sf_uc.uc_mcontext.mc_onstack = (oonstack) ? 1 : 0;
	sf.sf_uc.uc_mcontext.mc_edi = regs->tf_rdi;
	sf.sf_uc.uc_mcontext.mc_esi = regs->tf_rsi;
	sf.sf_uc.uc_mcontext.mc_ebp = regs->tf_rbp;
	sf.sf_uc.uc_mcontext.mc_isp = regs->tf_rsp; /* XXX */
	sf.sf_uc.uc_mcontext.mc_ebx = regs->tf_rbx;
	sf.sf_uc.uc_mcontext.mc_edx = regs->tf_rdx;
	sf.sf_uc.uc_mcontext.mc_ecx = regs->tf_rcx;
	sf.sf_uc.uc_mcontext.mc_eax = regs->tf_rax;
	sf.sf_uc.uc_mcontext.mc_trapno = regs->tf_trapno;
	sf.sf_uc.uc_mcontext.mc_err = regs->tf_err;
	sf.sf_uc.uc_mcontext.mc_eip = regs->tf_rip;
	sf.sf_uc.uc_mcontext.mc_cs = regs->tf_cs;
	sf.sf_uc.uc_mcontext.mc_eflags = regs->tf_rflags;
	sf.sf_uc.uc_mcontext.mc_esp = regs->tf_rsp;
	sf.sf_uc.uc_mcontext.mc_ss = regs->tf_ss;
	sf.sf_uc.uc_mcontext.mc_ds = regs->tf_ds;
	sf.sf_uc.uc_mcontext.mc_es = regs->tf_es;
	sf.sf_uc.uc_mcontext.mc_fs = regs->tf_fs;
	sf.sf_uc.uc_mcontext.mc_gs = regs->tf_gs;
	bzero(sf.sf_uc.uc_mcontext.mc_fpregs,
	    sizeof(sf.sf_uc.uc_mcontext.mc_fpregs));
	bzero(sf.sf_uc.uc_mcontext.__spare__,
	    sizeof(sf.sf_uc.uc_mcontext.__spare__));
	bzero(sf.sf_uc.__spare__, sizeof(sf.sf_uc.__spare__));

	/* Allocate space for the signal handler context. */
	if ((td->td_pflags & TDP_ALTSTACK) != 0 && !oonstack &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		sfp = (struct ia32_sigframe4 *)((uintptr_t)td->td_sigstk.ss_sp +
		    td->td_sigstk.ss_size - sizeof(sf));
	} else
		sfp = (struct ia32_sigframe4 *)regs->tf_rsp - 1;
	PROC_UNLOCK(p);

	/* Build the argument list for the signal handler. */
	sf.sf_signum = sig;
	sf.sf_ucontext = (register_t)&sfp->sf_uc;
	bzero(&sf.sf_si, sizeof(sf.sf_si));
	if (SIGISMEMBER(psp->ps_siginfo, sig)) {
		/* Signal handler installed with SA_SIGINFO. */
		sf.sf_siginfo = (u_int32_t)(uintptr_t)&sfp->sf_si;
		sf.sf_ah = (u_int32_t)(uintptr_t)catcher;

		/* Fill in POSIX parts */
		sf.sf_si = siginfo;
		sf.sf_si.si_signo = sig;
	} else {
		/* Old FreeBSD-style arguments. */
		sf.sf_siginfo = siginfo.si_code;
		sf.sf_addr = (u_int32_t)siginfo.si_addr;
		sf.sf_ah = (u_int32_t)(uintptr_t)catcher;
	}
	mtx_unlock(&psp->ps_mtx);

	/*
	 * Copy the sigframe out to the user's stack.
	 */
	if (copyout(&sf, sfp, sizeof(*sfp)) != 0) {
#ifdef DEBUG
		printf("process %ld has trashed its stack\n", (long)p->p_pid);
#endif
		PROC_LOCK(p);
		sigexit(td, SIGILL);
	}

	regs->tf_rsp = (uintptr_t)sfp;
	regs->tf_rip = p->p_sysent->sv_sigcode_base + sz_ia32_sigcode -
	    sz_freebsd4_ia32_sigcode;
	regs->tf_rflags &= ~(PSL_T | PSL_D);
	regs->tf_cs = _ucode32sel;
	regs->tf_ss = _udatasel;
	regs->tf_ds = _udatasel;
	regs->tf_es = _udatasel;
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	/* leave user %fs and %gs untouched */
	PROC_LOCK(p);
	mtx_lock(&psp->ps_mtx);
}
#endif	/* COMPAT_FREEBSD4 */

void
ia32_sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask)
{
	struct ia32_sigframe sf, *sfp;
	struct siginfo32 siginfo;
	struct proc *p;
	struct thread *td;
	struct sigacts *psp;
	char *sp;
	struct trapframe *regs;
	char *xfpusave;
	size_t xfpusave_len;
	int oonstack;
	int sig;

	siginfo_to_siginfo32(&ksi->ksi_info, &siginfo);
	td = curthread;
	p = td->td_proc;
	PROC_LOCK_ASSERT(p, MA_OWNED);
	sig = siginfo.si_signo;
	psp = p->p_sigacts;
#ifdef COMPAT_FREEBSD4
	if (SIGISMEMBER(psp->ps_freebsd4, sig)) {
		freebsd4_ia32_sendsig(catcher, ksi, mask);
		return;
	}
#endif
#ifdef COMPAT_43
	if (SIGISMEMBER(psp->ps_osigset, sig)) {
		ia32_osendsig(catcher, ksi, mask);
		return;
	}
#endif
	mtx_assert(&psp->ps_mtx, MA_OWNED);
	regs = td->td_frame;
	oonstack = sigonstack(regs->tf_rsp);

	if (cpu_max_ext_state_size > sizeof(struct savefpu) && use_xsave) {
		xfpusave_len = cpu_max_ext_state_size - sizeof(struct savefpu);
		xfpusave = __builtin_alloca(xfpusave_len);
	} else {
		xfpusave_len = 0;
		xfpusave = NULL;
	}

	/* Save user context. */
	bzero(&sf, sizeof(sf));
	sf.sf_uc.uc_sigmask = *mask;
	sf.sf_uc.uc_stack.ss_sp = (uintptr_t)td->td_sigstk.ss_sp;
	sf.sf_uc.uc_stack.ss_size = td->td_sigstk.ss_size;
	sf.sf_uc.uc_stack.ss_flags = (td->td_pflags & TDP_ALTSTACK)
	    ? ((oonstack) ? SS_ONSTACK : 0) : SS_DISABLE;
	sf.sf_uc.uc_mcontext.mc_onstack = (oonstack) ? 1 : 0;
	sf.sf_uc.uc_mcontext.mc_edi = regs->tf_rdi;
	sf.sf_uc.uc_mcontext.mc_esi = regs->tf_rsi;
	sf.sf_uc.uc_mcontext.mc_ebp = regs->tf_rbp;
	sf.sf_uc.uc_mcontext.mc_isp = regs->tf_rsp; /* XXX */
	sf.sf_uc.uc_mcontext.mc_ebx = regs->tf_rbx;
	sf.sf_uc.uc_mcontext.mc_edx = regs->tf_rdx;
	sf.sf_uc.uc_mcontext.mc_ecx = regs->tf_rcx;
	sf.sf_uc.uc_mcontext.mc_eax = regs->tf_rax;
	sf.sf_uc.uc_mcontext.mc_trapno = regs->tf_trapno;
	sf.sf_uc.uc_mcontext.mc_err = regs->tf_err;
	sf.sf_uc.uc_mcontext.mc_eip = regs->tf_rip;
	sf.sf_uc.uc_mcontext.mc_cs = regs->tf_cs;
	sf.sf_uc.uc_mcontext.mc_eflags = regs->tf_rflags;
	sf.sf_uc.uc_mcontext.mc_esp = regs->tf_rsp;
	sf.sf_uc.uc_mcontext.mc_ss = regs->tf_ss;
	sf.sf_uc.uc_mcontext.mc_ds = regs->tf_ds;
	sf.sf_uc.uc_mcontext.mc_es = regs->tf_es;
	sf.sf_uc.uc_mcontext.mc_fs = regs->tf_fs;
	sf.sf_uc.uc_mcontext.mc_gs = regs->tf_gs;
	sf.sf_uc.uc_mcontext.mc_len = sizeof(sf.sf_uc.uc_mcontext); /* magic */
	ia32_get_fpcontext(td, &sf.sf_uc.uc_mcontext, xfpusave, xfpusave_len);
	fpstate_drop(td);
	sf.sf_uc.uc_mcontext.mc_fsbase = td->td_pcb->pcb_fsbase;
	sf.sf_uc.uc_mcontext.mc_gsbase = td->td_pcb->pcb_gsbase;

	/* Allocate space for the signal handler context. */
	if ((td->td_pflags & TDP_ALTSTACK) != 0 && !oonstack &&
	    SIGISMEMBER(psp->ps_sigonstack, sig))
		sp = (char *)td->td_sigstk.ss_sp + td->td_sigstk.ss_size;
	else
		sp = (char *)regs->tf_rsp;
	if (xfpusave != NULL) {
		sp -= xfpusave_len;
		sp = (char *)((unsigned long)sp & ~0x3Ful);
		sf.sf_uc.uc_mcontext.mc_xfpustate = (register_t)sp;
	}
	sp -= sizeof(sf);
	/* Align to 16 bytes. */
	sfp = (struct ia32_sigframe *)((uintptr_t)sp & ~0xF);
	PROC_UNLOCK(p);

	/* Build the argument list for the signal handler. */
	sf.sf_signum = sig;
	sf.sf_ucontext = (register_t)&sfp->sf_uc;
	bzero(&sf.sf_si, sizeof(sf.sf_si));
	if (SIGISMEMBER(psp->ps_siginfo, sig)) {
		/* Signal handler installed with SA_SIGINFO. */
		sf.sf_siginfo = (u_int32_t)(uintptr_t)&sfp->sf_si;
		sf.sf_ah = (u_int32_t)(uintptr_t)catcher;

		/* Fill in POSIX parts */
		sf.sf_si = siginfo;
		sf.sf_si.si_signo = sig;
	} else {
		/* Old FreeBSD-style arguments. */
		sf.sf_siginfo = siginfo.si_code;
		sf.sf_addr = (u_int32_t)siginfo.si_addr;
		sf.sf_ah = (u_int32_t)(uintptr_t)catcher;
	}
	mtx_unlock(&psp->ps_mtx);

	/*
	 * Copy the sigframe out to the user's stack.
	 */
	if (copyout(&sf, sfp, sizeof(*sfp)) != 0 ||
	    (xfpusave != NULL && copyout(xfpusave,
	    PTRIN(sf.sf_uc.uc_mcontext.mc_xfpustate), xfpusave_len)
	    != 0)) {
#ifdef DEBUG
		printf("process %ld has trashed its stack\n", (long)p->p_pid);
#endif
		PROC_LOCK(p);
		sigexit(td, SIGILL);
	}

	regs->tf_rsp = (uintptr_t)sfp;
	regs->tf_rip = p->p_sysent->sv_sigcode_base;
	regs->tf_rflags &= ~(PSL_T | PSL_D);
	regs->tf_cs = _ucode32sel;
	regs->tf_ss = _udatasel;
	regs->tf_ds = _udatasel;
	regs->tf_es = _udatasel;
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	/* XXXKIB leave user %fs and %gs untouched */
	PROC_LOCK(p);
	mtx_lock(&psp->ps_mtx);
}

/*
 * System call to cleanup state after a signal
 * has been taken.  Reset signal mask and
 * stack state from context left by sendsig (above).
 * Return to previous pc and psl as specified by
 * context left by sendsig. Check carefully to
 * make sure that the user has not modified the
 * state to gain improper privileges.
 */

#ifdef COMPAT_43
int
ofreebsd32_sigreturn(struct thread *td, struct ofreebsd32_sigreturn_args *uap)
{
	struct ia32_sigcontext3 sc, *scp;
	struct trapframe *regs;
	int eflags, error;
	ksiginfo_t ksi;

	regs = td->td_frame;
	error = copyin(uap->sigcntxp, &sc, sizeof(sc));
	if (error != 0)
		return (error);
	scp = &sc;
	eflags = scp->sc_eflags;
	if (!EFL_SECURE(eflags, regs->tf_rflags)) {
		return (EINVAL);
	}
	if (!CS_SECURE(scp->sc_cs)) {
		ksiginfo_init_trap(&ksi);
		ksi.ksi_signo = SIGBUS;
		ksi.ksi_code = BUS_OBJERR;
		ksi.ksi_trapno = T_PROTFLT;
		ksi.ksi_addr = (void *)regs->tf_rip;
		trapsignal(td, &ksi);
		return (EINVAL);
	}
	regs->tf_ds = scp->sc_ds;
	regs->tf_es = scp->sc_es;
	regs->tf_fs = scp->sc_fs;
	regs->tf_gs = scp->sc_gs;

	regs->tf_rax = scp->sc_eax;
	regs->tf_rbx = scp->sc_ebx;
	regs->tf_rcx = scp->sc_ecx;
	regs->tf_rdx = scp->sc_edx;
	regs->tf_rsi = scp->sc_esi;
	regs->tf_rdi = scp->sc_edi;
	regs->tf_cs = scp->sc_cs;
	regs->tf_ss = scp->sc_ss;
	regs->tf_rbp = scp->sc_ebp;
	regs->tf_rsp = scp->sc_esp;
	regs->tf_rip = scp->sc_eip;
	regs->tf_rflags = eflags;

	if (scp->sc_onstack & 1)
		td->td_sigstk.ss_flags |= SS_ONSTACK;
	else
		td->td_sigstk.ss_flags &= ~SS_ONSTACK;

	kern_sigprocmask(td, SIG_SETMASK, (sigset_t *)&scp->sc_mask, NULL,
	    SIGPROCMASK_OLD);
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	return (EJUSTRETURN);
}
#endif

#ifdef COMPAT_FREEBSD4
/*
 * MPSAFE
 */
int
freebsd4_freebsd32_sigreturn(td, uap)
	struct thread *td;
	struct freebsd4_freebsd32_sigreturn_args /* {
		const struct freebsd4_freebsd32_ucontext *sigcntxp;
	} */ *uap;
{
	struct ia32_ucontext4 uc;
	struct trapframe *regs;
	struct ia32_ucontext4 *ucp;
	int cs, eflags, error;
	ksiginfo_t ksi;

	error = copyin(uap->sigcntxp, &uc, sizeof(uc));
	if (error != 0)
		return (error);
	ucp = &uc;
	regs = td->td_frame;
	eflags = ucp->uc_mcontext.mc_eflags;
	/*
	 * Don't allow users to change privileged or reserved flags.
	 */
	if (!EFL_SECURE(eflags, regs->tf_rflags)) {
		uprintf("pid %d (%s): freebsd4_freebsd32_sigreturn eflags = 0x%x\n",
		    td->td_proc->p_pid, td->td_name, eflags);
		return (EINVAL);
	}

	/*
	 * Don't allow users to load a valid privileged %cs.  Let the
	 * hardware check for invalid selectors, excess privilege in
	 * other selectors, invalid %eip's and invalid %esp's.
	 */
	cs = ucp->uc_mcontext.mc_cs;
	if (!CS_SECURE(cs)) {
		uprintf("pid %d (%s): freebsd4_sigreturn cs = 0x%x\n",
		    td->td_proc->p_pid, td->td_name, cs);
		ksiginfo_init_trap(&ksi);
		ksi.ksi_signo = SIGBUS;
		ksi.ksi_code = BUS_OBJERR;
		ksi.ksi_trapno = T_PROTFLT;
		ksi.ksi_addr = (void *)regs->tf_rip;
		trapsignal(td, &ksi);
		return (EINVAL);
	}

	regs->tf_rdi = ucp->uc_mcontext.mc_edi;
	regs->tf_rsi = ucp->uc_mcontext.mc_esi;
	regs->tf_rbp = ucp->uc_mcontext.mc_ebp;
	regs->tf_rbx = ucp->uc_mcontext.mc_ebx;
	regs->tf_rdx = ucp->uc_mcontext.mc_edx;
	regs->tf_rcx = ucp->uc_mcontext.mc_ecx;
	regs->tf_rax = ucp->uc_mcontext.mc_eax;
	regs->tf_trapno = ucp->uc_mcontext.mc_trapno;
	regs->tf_err = ucp->uc_mcontext.mc_err;
	regs->tf_rip = ucp->uc_mcontext.mc_eip;
	regs->tf_cs = cs;
	regs->tf_rflags = ucp->uc_mcontext.mc_eflags;
	regs->tf_rsp = ucp->uc_mcontext.mc_esp;
	regs->tf_ss = ucp->uc_mcontext.mc_ss;
	regs->tf_ds = ucp->uc_mcontext.mc_ds;
	regs->tf_es = ucp->uc_mcontext.mc_es;
	regs->tf_fs = ucp->uc_mcontext.mc_fs;
	regs->tf_gs = ucp->uc_mcontext.mc_gs;

	kern_sigprocmask(td, SIG_SETMASK, &ucp->uc_sigmask, NULL, 0);
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	return (EJUSTRETURN);
}
#endif	/* COMPAT_FREEBSD4 */

/*
 * MPSAFE
 */
int
freebsd32_sigreturn(td, uap)
	struct thread *td;
	struct freebsd32_sigreturn_args /* {
		const struct freebsd32_ucontext *sigcntxp;
	} */ *uap;
{
	struct ia32_ucontext uc;
	struct trapframe *regs;
	struct ia32_ucontext *ucp;
	char *xfpustate;
	size_t xfpustate_len;
	int cs, eflags, error, ret;
	ksiginfo_t ksi;

	error = copyin(uap->sigcntxp, &uc, sizeof(uc));
	if (error != 0)
		return (error);
	ucp = &uc;
	regs = td->td_frame;
	eflags = ucp->uc_mcontext.mc_eflags;
	/*
	 * Don't allow users to change privileged or reserved flags.
	 */
	if (!EFL_SECURE(eflags, regs->tf_rflags)) {
		uprintf("pid %d (%s): freebsd32_sigreturn eflags = 0x%x\n",
		    td->td_proc->p_pid, td->td_name, eflags);
		return (EINVAL);
	}

	/*
	 * Don't allow users to load a valid privileged %cs.  Let the
	 * hardware check for invalid selectors, excess privilege in
	 * other selectors, invalid %eip's and invalid %esp's.
	 */
	cs = ucp->uc_mcontext.mc_cs;
	if (!CS_SECURE(cs)) {
		uprintf("pid %d (%s): sigreturn cs = 0x%x\n",
		    td->td_proc->p_pid, td->td_name, cs);
		ksiginfo_init_trap(&ksi);
		ksi.ksi_signo = SIGBUS;
		ksi.ksi_code = BUS_OBJERR;
		ksi.ksi_trapno = T_PROTFLT;
		ksi.ksi_addr = (void *)regs->tf_rip;
		trapsignal(td, &ksi);
		return (EINVAL);
	}

	if ((ucp->uc_mcontext.mc_flags & _MC_HASFPXSTATE) != 0) {
		xfpustate_len = uc.uc_mcontext.mc_xfpustate_len;
		if (xfpustate_len > cpu_max_ext_state_size -
		    sizeof(struct savefpu)) {
			uprintf("pid %d (%s): sigreturn xfpusave_len = 0x%zx\n",
			    td->td_proc->p_pid, td->td_name, xfpustate_len);
			return (EINVAL);
		}
		xfpustate = __builtin_alloca(xfpustate_len);
		error = copyin(PTRIN(ucp->uc_mcontext.mc_xfpustate),
		    xfpustate, xfpustate_len);
		if (error != 0) {
			uprintf(
	"pid %d (%s): sigreturn copying xfpustate failed\n",
			    td->td_proc->p_pid, td->td_name);
			return (error);
		}
	} else {
		xfpustate = NULL;
		xfpustate_len = 0;
	}
	ret = ia32_set_fpcontext(td, &ucp->uc_mcontext, xfpustate,
	    xfpustate_len);
	if (ret != 0) {
		uprintf("pid %d (%s): sigreturn set_fpcontext err %d\n",
		    td->td_proc->p_pid, td->td_name, ret);
		return (ret);
	}

	regs->tf_rdi = ucp->uc_mcontext.mc_edi;
	regs->tf_rsi = ucp->uc_mcontext.mc_esi;
	regs->tf_rbp = ucp->uc_mcontext.mc_ebp;
	regs->tf_rbx = ucp->uc_mcontext.mc_ebx;
	regs->tf_rdx = ucp->uc_mcontext.mc_edx;
	regs->tf_rcx = ucp->uc_mcontext.mc_ecx;
	regs->tf_rax = ucp->uc_mcontext.mc_eax;
	regs->tf_trapno = ucp->uc_mcontext.mc_trapno;
	regs->tf_err = ucp->uc_mcontext.mc_err;
	regs->tf_rip = ucp->uc_mcontext.mc_eip;
	regs->tf_cs = cs;
	regs->tf_rflags = ucp->uc_mcontext.mc_eflags;
	regs->tf_rsp = ucp->uc_mcontext.mc_esp;
	regs->tf_ss = ucp->uc_mcontext.mc_ss;
	regs->tf_ds = ucp->uc_mcontext.mc_ds;
	regs->tf_es = ucp->uc_mcontext.mc_es;
	regs->tf_fs = ucp->uc_mcontext.mc_fs;
	regs->tf_gs = ucp->uc_mcontext.mc_gs;
	regs->tf_flags = TF_HASSEGS;

	kern_sigprocmask(td, SIG_SETMASK, &ucp->uc_sigmask, NULL, 0);
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	return (EJUSTRETURN);
}

/*
 * Clear registers on exec
 */
void
ia32_setregs(struct thread *td, struct image_params *imgp, u_long stack)
{
	struct trapframe *regs;
	struct pcb *pcb;
	register_t saved_rflags;

	regs = td->td_frame;
	pcb = td->td_pcb;

	if (td->td_proc->p_md.md_ldt != NULL)
		user_ldt_free(td);
#ifdef COMPAT_43
	setup_lcall_gate();
#endif

	pcb->pcb_fsbase = 0;
	pcb->pcb_gsbase = 0;
	pcb->pcb_initial_fpucw = __INITIAL_FPUCW_I386__;

	saved_rflags = regs->tf_rflags & PSL_T;
	bzero((char *)regs, sizeof(struct trapframe));
	regs->tf_rip = imgp->entry_addr;
	regs->tf_rsp = stack;
	regs->tf_rflags = PSL_USER | saved_rflags;
	regs->tf_ss = _udatasel;
	regs->tf_cs = _ucode32sel;
	regs->tf_rbx = imgp->ps_strings;
	regs->tf_ds = _udatasel;
	regs->tf_es = _udatasel;
	regs->tf_fs = _ufssel;
	regs->tf_gs = _ugssel;
	regs->tf_flags = TF_HASSEGS;

	fpstate_drop(td);

	/* Return via doreti so that we can change to a different %cs */
	set_pcb_flags(pcb, PCB_32BIT | PCB_FULL_IRET);
}
