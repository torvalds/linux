/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Juli Mallett <jmallett@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

/*
 * Based on nwhitehorn's COMPAT_FREEBSD32 support code for PowerPC64.
 */

#define __ELF_WORD_SIZE 32

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sysent.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/namei.h>
#include <sys/fcntl.h>
#include <sys/sysent.h>
#include <sys/imgact_elf.h>
#include <sys/syscall.h>
#include <sys/syscallsubr.h>
#include <sys/sysproto.h>
#include <sys/signalvar.h>
#include <sys/vnode.h>
#include <sys/linker.h>

#include <vm/vm.h>
#include <vm/vm_param.h>

#include <machine/md_var.h>
#include <machine/reg.h>
#include <machine/sigframe.h>
#include <machine/sysarch.h>
#include <machine/tls.h>

#include <compat/freebsd32/freebsd32_signal.h>
#include <compat/freebsd32/freebsd32_util.h>
#include <compat/freebsd32/freebsd32_proto.h>

static void freebsd32_exec_setregs(struct thread *, struct image_params *, u_long);
static int get_mcontext32(struct thread *, mcontext32_t *, int);
static int set_mcontext32(struct thread *, mcontext32_t *);
static void freebsd32_sendsig(sig_t, ksiginfo_t *, sigset_t *);

extern const char *freebsd32_syscallnames[];

struct sysentvec elf32_freebsd_sysvec = {
	.sv_size	= SYS_MAXSYSCALL,
	.sv_table	= freebsd32_sysent,
	.sv_errsize	= 0,
	.sv_errtbl	= NULL,
	.sv_transtrap	= NULL,
	.sv_fixup	= __elfN(freebsd_fixup),
	.sv_sendsig	= freebsd32_sendsig,
	.sv_sigcode	= sigcode32,
	.sv_szsigcode	= &szsigcode32,
	.sv_name	= "FreeBSD ELF32",
	.sv_coredump	= __elfN(coredump),
	.sv_imgact_try	= NULL,
	.sv_minsigstksz	= MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= ((vm_offset_t)0x80000000),
	.sv_usrstack	= FREEBSD32_USRSTACK,
	.sv_psstrings	= FREEBSD32_PS_STRINGS,
	.sv_stackprot	= VM_PROT_ALL,
	.sv_copyout_strings = freebsd32_copyout_strings,
	.sv_setregs	= freebsd32_exec_setregs,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_FREEBSD | SV_ILP32,
	.sv_set_syscall_retval = cpu_set_syscall_retval,
	.sv_fetch_syscall_args = cpu_fetch_syscall_args,
	.sv_syscallnames = freebsd32_syscallnames,
	.sv_schedtail	= NULL,
	.sv_thread_detach = NULL,
	.sv_trap	= NULL,
};
INIT_SYSENTVEC(elf32_sysvec, &elf32_freebsd_sysvec);

static Elf32_Brandinfo freebsd_brand_info = {
	.brand		= ELFOSABI_FREEBSD,
	.machine	= EM_MIPS,
	.compat_3_brand	= "FreeBSD",
	.emul_path	= NULL,
	.interp_path	= "/libexec/ld-elf.so.1",
	.sysvec		= &elf32_freebsd_sysvec,
	.interp_newpath	= "/libexec/ld-elf32.so.1",
	.brand_note	= &elf32_freebsd_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

SYSINIT(elf32, SI_SUB_EXEC, SI_ORDER_FIRST,
    (sysinit_cfunc_t) elf32_insert_brand_entry,
    &freebsd_brand_info);

static void
freebsd32_exec_setregs(struct thread *td, struct image_params *imgp, u_long stack)
{
	exec_setregs(td, imgp, stack);

	/*
	 * See comment in exec_setregs about running 32-bit binaries with 64-bit
	 * registers.
	 */
	td->td_frame->sp -= 65536;

	/*
	 * Clear extended address space bit for userland.
	 */
	td->td_frame->sr &= ~MIPS_SR_UX;

	td->td_md.md_tls_tcb_offset = TLS_TP_OFFSET + TLS_TCB_SIZE32;
}

int
set_regs32(struct thread *td, struct reg32 *regs)
{
	struct reg r;
	unsigned i;

	for (i = 0; i < NUMSAVEREGS; i++)
		r.r_regs[i] = regs->r_regs[i];

	return (set_regs(td, &r));
}

int
fill_regs32(struct thread *td, struct reg32 *regs)
{
	struct reg r;
	unsigned i;
	int error;

	error = fill_regs(td, &r);
	if (error != 0)
		return (error);

	for (i = 0; i < NUMSAVEREGS; i++)
		regs->r_regs[i] = r.r_regs[i];

	return (0);
}

int
set_fpregs32(struct thread *td, struct fpreg32 *fpregs)
{
	struct fpreg fp;
	unsigned i;

	for (i = 0; i < NUMFPREGS; i++)
		fp.r_regs[i] = fpregs->r_regs[i];

	return (set_fpregs(td, &fp));
}

int
fill_fpregs32(struct thread *td, struct fpreg32 *fpregs)
{
	struct fpreg fp;
	unsigned i;
	int error;

	error = fill_fpregs(td, &fp);
	if (error != 0)
		return (error);

	for (i = 0; i < NUMFPREGS; i++)
		fpregs->r_regs[i] = fp.r_regs[i];

	return (0);
}

static int
get_mcontext32(struct thread *td, mcontext32_t *mcp, int flags)
{
	mcontext_t mcp64;
	unsigned i;
	int error;

	error = get_mcontext(td, &mcp64, flags);
	if (error != 0)
		return (error);

	mcp->mc_onstack = mcp64.mc_onstack;
	mcp->mc_pc = mcp64.mc_pc;
	for (i = 0; i < 32; i++)
		mcp->mc_regs[i] = mcp64.mc_regs[i];
	mcp->sr = mcp64.sr;
	mcp->mullo = mcp64.mullo;
	mcp->mulhi = mcp64.mulhi;
	mcp->mc_fpused = mcp64.mc_fpused;
	for (i = 0; i < 33; i++)
		mcp->mc_fpregs[i] = mcp64.mc_fpregs[i];
	mcp->mc_fpc_eir = mcp64.mc_fpc_eir;
	mcp->mc_tls = (int32_t)(intptr_t)mcp64.mc_tls;

	return (0);
}

static int
set_mcontext32(struct thread *td, mcontext32_t *mcp)
{
	mcontext_t mcp64;
	unsigned i;

	mcp64.mc_onstack = mcp->mc_onstack;
	mcp64.mc_pc = mcp->mc_pc;
	for (i = 0; i < 32; i++)
		mcp64.mc_regs[i] = mcp->mc_regs[i];
	mcp64.sr = mcp->sr;
	mcp64.mullo = mcp->mullo;
	mcp64.mulhi = mcp->mulhi;
	mcp64.mc_fpused = mcp->mc_fpused;
	for (i = 0; i < 33; i++)
		mcp64.mc_fpregs[i] = mcp->mc_fpregs[i];
	mcp64.mc_fpc_eir = mcp->mc_fpc_eir;
	mcp64.mc_tls = (void *)(intptr_t)mcp->mc_tls;

	return (set_mcontext(td, &mcp64));
}

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

#if 0
	CTR3(KTR_SIG, "sigreturn: return td=%p pc=%#x sp=%#x",
	     td, uc.uc_mcontext.mc_srr0, uc.uc_mcontext.mc_gpr[1]);
#endif

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

#define	UCONTEXT_MAGIC	0xACEDBADE

/*
 * Send an interrupt to process.
 *
 * Stack is set up to allow sigcode stored
 * at top to call routine, followed by kcall
 * to sigreturn routine below.	After sigreturn
 * resets the signal mask, the stack, and the
 * frame pointer, it returns to the user
 * specified pc, psl.
 */
static void
freebsd32_sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask)
{
	struct proc *p;
	struct thread *td;
	struct fpreg32 fpregs;
	struct reg32 regs;
	struct sigacts *psp;
	struct sigframe32 sf, *sfp;
	int sig;
	int oonstack;
	unsigned i;

	td = curthread;
	p = td->td_proc;
	PROC_LOCK_ASSERT(p, MA_OWNED);
	sig = ksi->ksi_signo;
	psp = p->p_sigacts;
	mtx_assert(&psp->ps_mtx, MA_OWNED);

	fill_regs32(td, &regs);
	oonstack = sigonstack(td->td_frame->sp);

	/* save user context */
	bzero(&sf, sizeof sf);
	sf.sf_uc.uc_sigmask = *mask;
	sf.sf_uc.uc_stack.ss_sp = (int32_t)(intptr_t)td->td_sigstk.ss_sp;
	sf.sf_uc.uc_stack.ss_size = td->td_sigstk.ss_size;
	sf.sf_uc.uc_stack.ss_flags = td->td_sigstk.ss_flags;
	sf.sf_uc.uc_mcontext.mc_onstack = (oonstack) ? 1 : 0;
	sf.sf_uc.uc_mcontext.mc_pc = regs.r_regs[PC];
	sf.sf_uc.uc_mcontext.mullo = regs.r_regs[MULLO];
	sf.sf_uc.uc_mcontext.mulhi = regs.r_regs[MULHI];
	sf.sf_uc.uc_mcontext.mc_tls = (int32_t)(intptr_t)td->td_md.md_tls;
	sf.sf_uc.uc_mcontext.mc_regs[0] = UCONTEXT_MAGIC;  /* magic number */
	for (i = 1; i < 32; i++)
		sf.sf_uc.uc_mcontext.mc_regs[i] = regs.r_regs[i];
	sf.sf_uc.uc_mcontext.mc_fpused = td->td_md.md_flags & MDTD_FPUSED;
	if (sf.sf_uc.uc_mcontext.mc_fpused) {
		/* if FPU has current state, save it first */
		if (td == PCPU_GET(fpcurthread))
			MipsSaveCurFPState(td);
		fill_fpregs32(td, &fpregs);
		for (i = 0; i < 33; i++)
			sf.sf_uc.uc_mcontext.mc_fpregs[i] = fpregs.r_regs[i];
	}

	/* Allocate and validate space for the signal handler context. */
	if ((td->td_pflags & TDP_ALTSTACK) != 0 && !oonstack &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		sfp = (struct sigframe32 *)(((uintptr_t)td->td_sigstk.ss_sp +
		    td->td_sigstk.ss_size - sizeof(struct sigframe32))
		    & ~(sizeof(__int64_t) - 1));
	} else
		sfp = (struct sigframe32 *)((vm_offset_t)(td->td_frame->sp - 
		    sizeof(struct sigframe32)) & ~(sizeof(__int64_t) - 1));

	/* Build the argument list for the signal handler. */
	td->td_frame->a0 = sig;
	td->td_frame->a2 = (register_t)(intptr_t)&sfp->sf_uc;
	if (SIGISMEMBER(psp->ps_siginfo, sig)) {
		/* Signal handler installed with SA_SIGINFO. */
		td->td_frame->a1 = (register_t)(intptr_t)&sfp->sf_si;
		/* sf.sf_ahu.sf_action = (__siginfohandler_t *)catcher; */

		/* fill siginfo structure */
		sf.sf_si.si_signo = sig;
		sf.sf_si.si_code = ksi->ksi_code;
		sf.sf_si.si_addr = td->td_frame->badvaddr;
	} else {
		/* Old FreeBSD-style arguments. */
		td->td_frame->a1 = ksi->ksi_code;
		td->td_frame->a3 = td->td_frame->badvaddr;
		/* sf.sf_ahu.sf_handler = catcher; */
	}

	mtx_unlock(&psp->ps_mtx);
	PROC_UNLOCK(p);

	/*
	 * Copy the sigframe out to the user's stack.
	 */
	if (copyout(&sf, sfp, sizeof(struct sigframe32)) != 0) {
		/*
		 * Something is wrong with the stack pointer.
		 * ...Kill the process.
		 */
		PROC_LOCK(p);
		sigexit(td, SIGILL);
	}

	td->td_frame->pc = (register_t)(intptr_t)catcher;
	td->td_frame->t9 = (register_t)(intptr_t)catcher;
	td->td_frame->sp = (register_t)(intptr_t)sfp;
	/*
	 * Signal trampoline code is at base of user stack.
	 */
	td->td_frame->ra = (register_t)(intptr_t)FREEBSD32_PS_STRINGS - *(p->p_sysent->sv_szsigcode);
	PROC_LOCK(p);
	mtx_lock(&psp->ps_mtx);
}

int
freebsd32_sysarch(struct thread *td, struct freebsd32_sysarch_args *uap)
{
	int error;
	int32_t tlsbase;

	switch (uap->op) {
	case MIPS_SET_TLS:
		td->td_md.md_tls = (void *)(intptr_t)uap->parms;
		return (0);
	case MIPS_GET_TLS: 
		tlsbase = (int32_t)(intptr_t)td->td_md.md_tls;
		error = copyout(&tlsbase, uap->parms, sizeof(tlsbase));
		return (error);
	default:
		break;
	}
	return (EINVAL);
}

void
elf32_dump_thread(struct thread *td __unused, void *dst __unused,
    size_t *off __unused)
{
}
