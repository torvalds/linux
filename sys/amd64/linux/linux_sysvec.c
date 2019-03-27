/*-
 * Copyright (c) 2013 Dmitry Chagin
 * Copyright (c) 2004 Tim J. Robbins
 * Copyright (c) 2003 Peter Wemm
 * Copyright (c) 2002 Doug Rabson
 * Copyright (c) 1998-1999 Andrew Gallatin
 * Copyright (c) 1994-1996 Søren Schmidt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define	__ELF_WORD_SIZE	64

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/exec.h>
#include <sys/fcntl.h>
#include <sys/imgact.h>
#include <sys/imgact_elf.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/vnode.h>
#include <sys/eventhandler.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_param.h>

#include <machine/cpu.h>
#include <machine/md_var.h>
#include <machine/pcb.h>
#include <machine/specialreg.h>
#include <machine/trap.h>

#include <amd64/linux/linux.h>
#include <amd64/linux/linux_proto.h>
#include <compat/linux/linux_emul.h>
#include <compat/linux/linux_futex.h>
#include <compat/linux/linux_ioctl.h>
#include <compat/linux/linux_mib.h>
#include <compat/linux/linux_misc.h>
#include <compat/linux/linux_signal.h>
#include <compat/linux/linux_sysproto.h>
#include <compat/linux/linux_util.h>
#include <compat/linux/linux_vdso.h>

MODULE_VERSION(linux64, 1);

#if defined(DEBUG)
SYSCTL_PROC(_compat_linux, OID_AUTO, debug,
	    CTLTYPE_STRING | CTLFLAG_RW,
	    0, 0, linux_sysctl_debug, "A",
	    "Linux 64 debugging control");
#endif

/*
 * Allow the sendsig functions to use the ldebug() facility even though they
 * are not syscalls themselves.  Map them to syscall 0.  This is slightly less
 * bogus than using ldebug(sigreturn).
 */
#define	LINUX_SYS_linux_rt_sendsig	0

const char *linux_kplatform;
static int linux_szsigcode;
static vm_object_t linux_shared_page_obj;
static char *linux_shared_page_mapping;
extern char _binary_linux_locore_o_start;
extern char _binary_linux_locore_o_end;

extern struct sysent linux_sysent[LINUX_SYS_MAXSYSCALL];

SET_DECLARE(linux_ioctl_handler_set, struct linux_ioctl_handler);

static register_t * linux_copyout_strings(struct image_params *imgp);
static int	linux_fixup_elf(register_t **stack_base,
		    struct image_params *iparams);
static bool	linux_trans_osrel(const Elf_Note *note, int32_t *osrel);
static void	linux_vdso_install(void *param);
static void	linux_vdso_deinstall(void *param);
static void	linux_set_syscall_retval(struct thread *td, int error);
static int	linux_fetch_syscall_args(struct thread *td);
static void	linux_exec_setregs(struct thread *td, struct image_params *imgp,
		    u_long stack);
static int	linux_vsyscall(struct thread *td);

#define LINUX_T_UNKNOWN  255
static int _bsd_to_linux_trapcode[] = {
	LINUX_T_UNKNOWN,	/* 0 */
	6,			/* 1  T_PRIVINFLT */
	LINUX_T_UNKNOWN,	/* 2 */
	3,			/* 3  T_BPTFLT */
	LINUX_T_UNKNOWN,	/* 4 */
	LINUX_T_UNKNOWN,	/* 5 */
	16,			/* 6  T_ARITHTRAP */
	254,			/* 7  T_ASTFLT */
	LINUX_T_UNKNOWN,	/* 8 */
	13,			/* 9  T_PROTFLT */
	1,			/* 10 T_TRCTRAP */
	LINUX_T_UNKNOWN,	/* 11 */
	14,			/* 12 T_PAGEFLT */
	LINUX_T_UNKNOWN,	/* 13 */
	17,			/* 14 T_ALIGNFLT */
	LINUX_T_UNKNOWN,	/* 15 */
	LINUX_T_UNKNOWN,	/* 16 */
	LINUX_T_UNKNOWN,	/* 17 */
	0,			/* 18 T_DIVIDE */
	2,			/* 19 T_NMI */
	4,			/* 20 T_OFLOW */
	5,			/* 21 T_BOUND */
	7,			/* 22 T_DNA */
	8,			/* 23 T_DOUBLEFLT */
	9,			/* 24 T_FPOPFLT */
	10,			/* 25 T_TSSFLT */
	11,			/* 26 T_SEGNPFLT */
	12,			/* 27 T_STKFLT */
	18,			/* 28 T_MCHK */
	19,			/* 29 T_XMMFLT */
	15			/* 30 T_RESERVED */
};
#define bsd_to_linux_trapcode(code) \
    ((code)<nitems(_bsd_to_linux_trapcode)? \
     _bsd_to_linux_trapcode[(code)]: \
     LINUX_T_UNKNOWN)

LINUX_VDSO_SYM_INTPTR(linux_rt_sigcode);
LINUX_VDSO_SYM_CHAR(linux_platform);

/*
 * If FreeBSD & Linux have a difference of opinion about what a trap
 * means, deal with it here.
 *
 * MPSAFE
 */
static int
linux_translate_traps(int signal, int trap_code)
{

	if (signal != SIGBUS)
		return (signal);
	switch (trap_code) {
	case T_PROTFLT:
	case T_TSSFLT:
	case T_DOUBLEFLT:
	case T_PAGEFLT:
		return (SIGSEGV);
	default:
		return (signal);
	}
}

static int
linux_fetch_syscall_args(struct thread *td)
{
	struct proc *p;
	struct trapframe *frame;
	struct syscall_args *sa;

	p = td->td_proc;
	frame = td->td_frame;
	sa = &td->td_sa;

	sa->args[0] = frame->tf_rdi;
	sa->args[1] = frame->tf_rsi;
	sa->args[2] = frame->tf_rdx;
	sa->args[3] = frame->tf_rcx;
	sa->args[4] = frame->tf_r8;
	sa->args[5] = frame->tf_r9;
	sa->code = frame->tf_rax;

	if (sa->code >= p->p_sysent->sv_size)
		/* nosys */
		sa->callp = &p->p_sysent->sv_table[p->p_sysent->sv_size - 1];
	else
		sa->callp = &p->p_sysent->sv_table[sa->code];
	sa->narg = sa->callp->sy_narg;

	td->td_retval[0] = 0;
	return (0);
}

static void
linux_set_syscall_retval(struct thread *td, int error)
{
	struct trapframe *frame = td->td_frame;

	/*
	 * On Linux only %rcx and %r11 values are not preserved across
	 * the syscall.  So, do not clobber %rdx and %r10.
	 */
	td->td_retval[1] = frame->tf_rdx;
	if (error != EJUSTRETURN)
		frame->tf_r10 = frame->tf_rcx;

	cpu_set_syscall_retval(td, error);

	 /* Restore all registers. */
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
}

static int
linux_fixup_elf(register_t **stack_base, struct image_params *imgp)
{
	Elf_Auxargs *args;
	Elf_Auxinfo *argarray, *pos;
	Elf_Addr *auxbase, *base;
	struct ps_strings *arginfo;
	struct proc *p;
	int error, issetugid;

	p = imgp->proc;
	arginfo = (struct ps_strings *)p->p_sysent->sv_psstrings;

	KASSERT(curthread->td_proc == imgp->proc,
	    ("unsafe linux_fixup_elf(), should be curproc"));
	base = (Elf64_Addr *)*stack_base;
	args = (Elf64_Auxargs *)imgp->auxargs;
	auxbase = base + imgp->args->argc + 1 + imgp->args->envc + 1;
	argarray = pos = malloc(LINUX_AT_COUNT * sizeof(*pos), M_TEMP,
	    M_WAITOK | M_ZERO);

	issetugid = p->p_flag & P_SUGID ? 1 : 0;
	AUXARGS_ENTRY(pos, LINUX_AT_SYSINFO_EHDR,
	    imgp->proc->p_sysent->sv_shared_page_base);
	AUXARGS_ENTRY(pos, LINUX_AT_HWCAP, cpu_feature);
	AUXARGS_ENTRY(pos, LINUX_AT_CLKTCK, stclohz);
	AUXARGS_ENTRY(pos, AT_PHDR, args->phdr);
	AUXARGS_ENTRY(pos, AT_PHENT, args->phent);
	AUXARGS_ENTRY(pos, AT_PHNUM, args->phnum);
	AUXARGS_ENTRY(pos, AT_PAGESZ, args->pagesz);
	AUXARGS_ENTRY(pos, AT_BASE, args->base);
	AUXARGS_ENTRY(pos, AT_FLAGS, args->flags);
	AUXARGS_ENTRY(pos, AT_ENTRY, args->entry);
	AUXARGS_ENTRY(pos, AT_UID, imgp->proc->p_ucred->cr_ruid);
	AUXARGS_ENTRY(pos, AT_EUID, imgp->proc->p_ucred->cr_svuid);
	AUXARGS_ENTRY(pos, AT_GID, imgp->proc->p_ucred->cr_rgid);
	AUXARGS_ENTRY(pos, AT_EGID, imgp->proc->p_ucred->cr_svgid);
	AUXARGS_ENTRY(pos, LINUX_AT_SECURE, issetugid);
	AUXARGS_ENTRY(pos, LINUX_AT_PLATFORM, PTROUT(linux_platform));
	AUXARGS_ENTRY(pos, LINUX_AT_RANDOM, imgp->canary);
	if (imgp->execpathp != 0)
		AUXARGS_ENTRY(pos, LINUX_AT_EXECFN, imgp->execpathp);
	if (args->execfd != -1)
		AUXARGS_ENTRY(pos, AT_EXECFD, args->execfd);
	AUXARGS_ENTRY(pos, AT_NULL, 0);
	free(imgp->auxargs, M_TEMP);
	imgp->auxargs = NULL;
	KASSERT(pos - argarray <= LINUX_AT_COUNT, ("Too many auxargs"));

	error = copyout(argarray, auxbase, sizeof(*argarray) * LINUX_AT_COUNT);
	free(argarray, M_TEMP);
	if (error != 0)
		return (error);

	base--;
	if (suword(base, (uint64_t)imgp->args->argc) == -1)
		return (EFAULT);

	*stack_base = (register_t *)base;
	return (0);
}

/*
 * Copy strings out to the new process address space, constructing new arg
 * and env vector tables. Return a pointer to the base so that it can be used
 * as the initial stack pointer.
 */
static register_t *
linux_copyout_strings(struct image_params *imgp)
{
	int argc, envc;
	char **vectp;
	char *stringp, *destp;
	register_t *stack_base;
	struct ps_strings *arginfo;
	char canary[LINUX_AT_RANDOM_LEN];
	size_t execpath_len;
	struct proc *p;

	/* Calculate string base and vector table pointers. */
	if (imgp->execpath != NULL && imgp->auxargs != NULL)
		execpath_len = strlen(imgp->execpath) + 1;
	else
		execpath_len = 0;

	p = imgp->proc;
	arginfo = (struct ps_strings *)p->p_sysent->sv_psstrings;
	destp = (caddr_t)arginfo - SPARE_USRSPACE -
	    roundup(sizeof(canary), sizeof(char *)) -
	    roundup(execpath_len, sizeof(char *)) -
	    roundup(ARG_MAX - imgp->args->stringspace, sizeof(char *));

	if (execpath_len != 0) {
		imgp->execpathp = (uintptr_t)arginfo - execpath_len;
		copyout(imgp->execpath, (void *)imgp->execpathp, execpath_len);
	}

	/* Prepare the canary for SSP. */
	arc4rand(canary, sizeof(canary), 0);
	imgp->canary = (uintptr_t)arginfo -
	    roundup(execpath_len, sizeof(char *)) -
	    roundup(sizeof(canary), sizeof(char *));
	copyout(canary, (void *)imgp->canary, sizeof(canary));

	vectp = (char **)destp;
	if (imgp->auxargs) {
		/*
		 * Allocate room on the stack for the ELF auxargs
		 * array.  It has LINUX_AT_COUNT entries.
		 */
		vectp -= howmany(LINUX_AT_COUNT * sizeof(Elf64_Auxinfo),
		    sizeof(*vectp));
	}

	/*
	 * Allocate room for the argv[] and env vectors including the
	 * terminating NULL pointers.
	 */
	vectp -= imgp->args->argc + 1 + imgp->args->envc + 1;

	/* vectp also becomes our initial stack base. */
	stack_base = (register_t *)vectp;

	stringp = imgp->args->begin_argv;
	argc = imgp->args->argc;
	envc = imgp->args->envc;

	/* Copy out strings - arguments and environment. */
	copyout(stringp, destp, ARG_MAX - imgp->args->stringspace);

	/* Fill in "ps_strings" struct for ps, w, etc. */
	suword(&arginfo->ps_argvstr, (long)(intptr_t)vectp);
	suword(&arginfo->ps_nargvstr, argc);

	/* Fill in argument portion of vector table. */
	for (; argc > 0; --argc) {
		suword(vectp++, (long)(intptr_t)destp);
		while (*stringp++ != 0)
			destp++;
		destp++;
	}

	/* A null vector table pointer separates the argp's from the envp's. */
	suword(vectp++, 0);

	suword(&arginfo->ps_envstr, (long)(intptr_t)vectp);
	suword(&arginfo->ps_nenvstr, envc);

	/* Fill in environment portion of vector table. */
	for (; envc > 0; --envc) {
		suword(vectp++, (long)(intptr_t)destp);
		while (*stringp++ != 0)
			destp++;
		destp++;
	}

	/* The end of the vector table is a null pointer. */
	suword(vectp, 0);
	return (stack_base);
}

/*
 * Reset registers to default values on exec.
 */
static void
linux_exec_setregs(struct thread *td, struct image_params *imgp, u_long stack)
{
	struct trapframe *regs;
	struct pcb *pcb;
	register_t saved_rflags;

	regs = td->td_frame;
	pcb = td->td_pcb;

	if (td->td_proc->p_md.md_ldt != NULL)
		user_ldt_free(td);

	pcb->pcb_fsbase = 0;
	pcb->pcb_gsbase = 0;
	clear_pcb_flags(pcb, PCB_32BIT);
	pcb->pcb_initial_fpucw = __LINUX_NPXCW__;
	set_pcb_flags(pcb, PCB_FULL_IRET);

	saved_rflags = regs->tf_rflags & PSL_T;
	bzero((char *)regs, sizeof(struct trapframe));
	regs->tf_rip = imgp->entry_addr;
	regs->tf_rsp = stack;
	regs->tf_rflags = PSL_USER | saved_rflags;
	regs->tf_ss = _udatasel;
	regs->tf_cs = _ucodesel;
	regs->tf_ds = _udatasel;
	regs->tf_es = _udatasel;
	regs->tf_fs = _ufssel;
	regs->tf_gs = _ugssel;
	regs->tf_flags = TF_HASSEGS;

	/*
	 * Reset the hardware debug registers if they were in use.
	 * They won't have any meaning for the newly exec'd process.
	 */
	if (pcb->pcb_flags & PCB_DBREGS) {
		pcb->pcb_dr0 = 0;
		pcb->pcb_dr1 = 0;
		pcb->pcb_dr2 = 0;
		pcb->pcb_dr3 = 0;
		pcb->pcb_dr6 = 0;
		pcb->pcb_dr7 = 0;
		if (pcb == curpcb) {
			/*
			 * Clear the debug registers on the running
			 * CPU, otherwise they will end up affecting
			 * the next process we switch to.
			 */
			reset_dbregs();
		}
		clear_pcb_flags(pcb, PCB_DBREGS);
	}

	/*
	 * Drop the FP state if we hold it, so that the process gets a
	 * clean FP state if it uses the FPU again.
	 */
	fpstate_drop(td);
}

/*
 * Copied from amd64/amd64/machdep.c
 *
 * XXX fpu state need? don't think so
 */
int
linux_rt_sigreturn(struct thread *td, struct linux_rt_sigreturn_args *args)
{
	struct proc *p;
	struct l_ucontext uc;
	struct l_sigcontext *context;
	struct trapframe *regs;
	unsigned long rflags;
	int error;
	ksiginfo_t ksi;

	regs = td->td_frame;
	error = copyin((void *)regs->tf_rbx, &uc, sizeof(uc));
	if (error != 0)
		return (error);

	p = td->td_proc;
	context = &uc.uc_mcontext;
	rflags = context->sc_rflags;

	/*
	 * Don't allow users to change privileged or reserved flags.
	 */
	/*
	 * XXX do allow users to change the privileged flag PSL_RF.
	 * The cpu sets PSL_RF in tf_rflags for faults.  Debuggers
	 * should sometimes set it there too.  tf_rflags is kept in
	 * the signal context during signal handling and there is no
	 * other place to remember it, so the PSL_RF bit may be
	 * corrupted by the signal handler without us knowing.
	 * Corruption of the PSL_RF bit at worst causes one more or
	 * one less debugger trap, so allowing it is fairly harmless.
	 */

#define RFLAG_SECURE(ef, oef)     ((((ef) ^ (oef)) & ~PSL_USERCHANGE) == 0)
	if (!RFLAG_SECURE(rflags & ~PSL_RF, regs->tf_rflags & ~PSL_RF)) {
		printf("linux_rt_sigreturn: rflags = 0x%lx\n", rflags);
		return (EINVAL);
	}

	/*
	 * Don't allow users to load a valid privileged %cs.  Let the
	 * hardware check for invalid selectors, excess privilege in
	 * other selectors, invalid %eip's and invalid %esp's.
	 */
#define CS_SECURE(cs)           (ISPL(cs) == SEL_UPL)
	if (!CS_SECURE(context->sc_cs)) {
		printf("linux_rt_sigreturn: cs = 0x%x\n", context->sc_cs);
		ksiginfo_init_trap(&ksi);
		ksi.ksi_signo = SIGBUS;
		ksi.ksi_code = BUS_OBJERR;
		ksi.ksi_trapno = T_PROTFLT;
		ksi.ksi_addr = (void *)regs->tf_rip;
		trapsignal(td, &ksi);
		return (EINVAL);
	}

	PROC_LOCK(p);
	linux_to_bsd_sigset(&uc.uc_sigmask, &td->td_sigmask);
	SIG_CANTMASK(td->td_sigmask);
	signotify(td);
	PROC_UNLOCK(p);

	regs->tf_rdi    = context->sc_rdi;
	regs->tf_rsi    = context->sc_rsi;
	regs->tf_rdx    = context->sc_rdx;
	regs->tf_rbp    = context->sc_rbp;
	regs->tf_rbx    = context->sc_rbx;
	regs->tf_rcx    = context->sc_rcx;
	regs->tf_rax    = context->sc_rax;
	regs->tf_rip    = context->sc_rip;
	regs->tf_rsp    = context->sc_rsp;
	regs->tf_r8     = context->sc_r8;
	regs->tf_r9     = context->sc_r9;
	regs->tf_r10    = context->sc_r10;
	regs->tf_r11    = context->sc_r11;
	regs->tf_r12    = context->sc_r12;
	regs->tf_r13    = context->sc_r13;
	regs->tf_r14    = context->sc_r14;
	regs->tf_r15    = context->sc_r15;
	regs->tf_cs     = context->sc_cs;
	regs->tf_err    = context->sc_err;
	regs->tf_rflags = rflags;

	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	return (EJUSTRETURN);
}

/*
 * copied from amd64/amd64/machdep.c
 *
 * Send an interrupt to process.
 */
static void
linux_rt_sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask)
{
	struct l_rt_sigframe sf, *sfp;
	struct proc *p;
	struct thread *td;
	struct sigacts *psp;
	caddr_t sp;
	struct trapframe *regs;
	int sig, code;
	int oonstack;

	td = curthread;
	p = td->td_proc;
	PROC_LOCK_ASSERT(p, MA_OWNED);
	sig = ksi->ksi_signo;
	psp = p->p_sigacts;
	code = ksi->ksi_code;
	mtx_assert(&psp->ps_mtx, MA_OWNED);
	regs = td->td_frame;
	oonstack = sigonstack(regs->tf_rsp);

	LINUX_CTR4(rt_sendsig, "%p, %d, %p, %u",
	    catcher, sig, mask, code);

	/* Allocate space for the signal handler context. */
	if ((td->td_pflags & TDP_ALTSTACK) != 0 && !oonstack &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		sp = (caddr_t)td->td_sigstk.ss_sp + td->td_sigstk.ss_size -
		    sizeof(struct l_rt_sigframe);
	} else
		sp = (caddr_t)regs->tf_rsp - sizeof(struct l_rt_sigframe) - 128;
	/* Align to 16 bytes. */
	sfp = (struct l_rt_sigframe *)((unsigned long)sp & ~0xFul);
	mtx_unlock(&psp->ps_mtx);

	/* Translate the signal. */
	sig = bsd_to_linux_signal(sig);

	/* Save user context. */
	bzero(&sf, sizeof(sf));
	bsd_to_linux_sigset(mask, &sf.sf_sc.uc_sigmask);
	bsd_to_linux_sigset(mask, &sf.sf_sc.uc_mcontext.sc_mask);

	sf.sf_sc.uc_stack.ss_sp = PTROUT(td->td_sigstk.ss_sp);
	sf.sf_sc.uc_stack.ss_size = td->td_sigstk.ss_size;
	sf.sf_sc.uc_stack.ss_flags = (td->td_pflags & TDP_ALTSTACK)
	    ? ((oonstack) ? LINUX_SS_ONSTACK : 0) : LINUX_SS_DISABLE;
	PROC_UNLOCK(p);

	sf.sf_sc.uc_mcontext.sc_rdi    = regs->tf_rdi;
	sf.sf_sc.uc_mcontext.sc_rsi    = regs->tf_rsi;
	sf.sf_sc.uc_mcontext.sc_rdx    = regs->tf_rdx;
	sf.sf_sc.uc_mcontext.sc_rbp    = regs->tf_rbp;
	sf.sf_sc.uc_mcontext.sc_rbx    = regs->tf_rbx;
	sf.sf_sc.uc_mcontext.sc_rcx    = regs->tf_rcx;
	sf.sf_sc.uc_mcontext.sc_rax    = regs->tf_rax;
	sf.sf_sc.uc_mcontext.sc_rip    = regs->tf_rip;
	sf.sf_sc.uc_mcontext.sc_rsp    = regs->tf_rsp;
	sf.sf_sc.uc_mcontext.sc_r8     = regs->tf_r8;
	sf.sf_sc.uc_mcontext.sc_r9     = regs->tf_r9;
	sf.sf_sc.uc_mcontext.sc_r10    = regs->tf_r10;
	sf.sf_sc.uc_mcontext.sc_r11    = regs->tf_r11;
	sf.sf_sc.uc_mcontext.sc_r12    = regs->tf_r12;
	sf.sf_sc.uc_mcontext.sc_r13    = regs->tf_r13;
	sf.sf_sc.uc_mcontext.sc_r14    = regs->tf_r14;
	sf.sf_sc.uc_mcontext.sc_r15    = regs->tf_r15;
	sf.sf_sc.uc_mcontext.sc_cs     = regs->tf_cs;
	sf.sf_sc.uc_mcontext.sc_rflags = regs->tf_rflags;
	sf.sf_sc.uc_mcontext.sc_err    = regs->tf_err;
	sf.sf_sc.uc_mcontext.sc_trapno = bsd_to_linux_trapcode(code);
	sf.sf_sc.uc_mcontext.sc_cr2    = (register_t)ksi->ksi_addr;

	/* Build the argument list for the signal handler. */
	regs->tf_rdi = sig;			/* arg 1 in %rdi */
	regs->tf_rax = 0;
	regs->tf_rsi = (register_t)&sfp->sf_si;	/* arg 2 in %rsi */
	regs->tf_rdx = (register_t)&sfp->sf_sc;	/* arg 3 in %rdx */

	sf.sf_handler = catcher;
	/* Fill in POSIX parts. */
	ksiginfo_to_lsiginfo(ksi, &sf.sf_si, sig);

	/* Copy the sigframe out to the user's stack. */
	if (copyout(&sf, sfp, sizeof(*sfp)) != 0) {
#ifdef DEBUG
		printf("process %ld has trashed its stack\n", (long)p->p_pid);
#endif
		PROC_LOCK(p);
		sigexit(td, SIGILL);
	}

	regs->tf_rsp = (long)sfp;
	regs->tf_rip = linux_rt_sigcode;
	regs->tf_rflags &= ~(PSL_T | PSL_D);
	regs->tf_cs = _ucodesel;
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	PROC_LOCK(p);
	mtx_lock(&psp->ps_mtx);
}

#define	LINUX_VSYSCALL_START		(-10UL << 20)
#define	LINUX_VSYSCALL_SZ		1024

const unsigned long linux_vsyscall_vector[] = {
	LINUX_SYS_gettimeofday,
	LINUX_SYS_linux_time,
				/* getcpu not implemented */
};

static int
linux_vsyscall(struct thread *td)
{
	struct trapframe *frame;
	uint64_t retqaddr;
	int code, traced;
	int error;

	frame = td->td_frame;

	/* Check %rip for vsyscall area. */
	if (__predict_true(frame->tf_rip < LINUX_VSYSCALL_START))
		return (EINVAL);
	if ((frame->tf_rip & (LINUX_VSYSCALL_SZ - 1)) != 0)
		return (EINVAL);
	code = (frame->tf_rip - LINUX_VSYSCALL_START) / LINUX_VSYSCALL_SZ;
	if (code >= nitems(linux_vsyscall_vector))
		return (EINVAL);

	/*
	 * vsyscall called as callq *(%rax), so we must
	 * use return address from %rsp and also fixup %rsp.
	 */
	error = copyin((void *)frame->tf_rsp, &retqaddr, sizeof(retqaddr));
	if (error)
		return (error);

	frame->tf_rip = retqaddr;
	frame->tf_rax = linux_vsyscall_vector[code];
	frame->tf_rsp += 8;

	traced = (frame->tf_flags & PSL_T);

	amd64_syscall(td, traced);

	return (0);
}

struct sysentvec elf_linux_sysvec = {
	.sv_size	= LINUX_SYS_MAXSYSCALL,
	.sv_table	= linux_sysent,
	.sv_errsize	= ELAST + 1,
	.sv_errtbl	= linux_errtbl,
	.sv_transtrap	= linux_translate_traps,
	.sv_fixup	= linux_fixup_elf,
	.sv_sendsig	= linux_rt_sendsig,
	.sv_sigcode	= &_binary_linux_locore_o_start,
	.sv_szsigcode	= &linux_szsigcode,
	.sv_name	= "Linux ELF64",
	.sv_coredump	= elf64_coredump,
	.sv_imgact_try	= linux_exec_imgact_try,
	.sv_minsigstksz	= LINUX_MINSIGSTKSZ,
	.sv_minuser	= VM_MIN_ADDRESS,
	.sv_maxuser	= VM_MAXUSER_ADDRESS,
	.sv_usrstack	= USRSTACK,
	.sv_psstrings	= PS_STRINGS,
	.sv_stackprot	= VM_PROT_ALL,
	.sv_copyout_strings = linux_copyout_strings,
	.sv_setregs	= linux_exec_setregs,
	.sv_fixlimit	= NULL,
	.sv_maxssiz	= NULL,
	.sv_flags	= SV_ABI_LINUX | SV_LP64 | SV_SHP,
	.sv_set_syscall_retval = linux_set_syscall_retval,
	.sv_fetch_syscall_args = linux_fetch_syscall_args,
	.sv_syscallnames = NULL,
	.sv_shared_page_base = SHAREDPAGE,
	.sv_shared_page_len = PAGE_SIZE,
	.sv_schedtail	= linux_schedtail,
	.sv_thread_detach = linux_thread_detach,
	.sv_trap	= linux_vsyscall,
};

static void
linux_vdso_install(void *param)
{

	amd64_lower_shared_page(&elf_linux_sysvec);

	linux_szsigcode = (&_binary_linux_locore_o_end -
	    &_binary_linux_locore_o_start);

	if (linux_szsigcode > elf_linux_sysvec.sv_shared_page_len)
		panic("Linux invalid vdso size\n");

	__elfN(linux_vdso_fixup)(&elf_linux_sysvec);

	linux_shared_page_obj = __elfN(linux_shared_page_init)
	    (&linux_shared_page_mapping);

	__elfN(linux_vdso_reloc)(&elf_linux_sysvec);

	bcopy(elf_linux_sysvec.sv_sigcode, linux_shared_page_mapping,
	    linux_szsigcode);
	elf_linux_sysvec.sv_shared_page_obj = linux_shared_page_obj;

	linux_kplatform = linux_shared_page_mapping +
	    (linux_platform - (caddr_t)elf_linux_sysvec.sv_shared_page_base);
}
SYSINIT(elf_linux_vdso_init, SI_SUB_EXEC, SI_ORDER_ANY,
    linux_vdso_install, NULL);

static void
linux_vdso_deinstall(void *param)
{

	__elfN(linux_shared_page_fini)(linux_shared_page_obj);
}
SYSUNINIT(elf_linux_vdso_uninit, SI_SUB_EXEC, SI_ORDER_FIRST,
    linux_vdso_deinstall, NULL);

static char GNULINUX_ABI_VENDOR[] = "GNU";
static int GNULINUX_ABI_DESC = 0;

static bool
linux_trans_osrel(const Elf_Note *note, int32_t *osrel)
{
	const Elf32_Word *desc;
	uintptr_t p;

	p = (uintptr_t)(note + 1);
	p += roundup2(note->n_namesz, sizeof(Elf32_Addr));

	desc = (const Elf32_Word *)p;
	if (desc[0] != GNULINUX_ABI_DESC)
		return (false);

	/*
	 * For Linux we encode osrel using the Linux convention of
	 * 	(version << 16) | (major << 8) | (minor)
	 * See macro in linux_mib.h
	 */
	*osrel = LINUX_KERNVER(desc[1], desc[2], desc[3]);

	return (true);
}

static Elf_Brandnote linux64_brandnote = {
	.hdr.n_namesz	= sizeof(GNULINUX_ABI_VENDOR),
	.hdr.n_descsz	= 16,
	.hdr.n_type	= 1,
	.vendor		= GNULINUX_ABI_VENDOR,
	.flags		= BN_TRANSLATE_OSREL,
	.trans_osrel	= linux_trans_osrel
};

static Elf64_Brandinfo linux_glibc2brand = {
	.brand		= ELFOSABI_LINUX,
	.machine	= EM_X86_64,
	.compat_3_brand	= "Linux",
	.emul_path	= "/compat/linux",
	.interp_path	= "/lib64/ld-linux-x86-64.so.2",
	.sysvec		= &elf_linux_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &linux64_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

static Elf64_Brandinfo linux_glibc2brandshort = {
	.brand		= ELFOSABI_LINUX,
	.machine	= EM_X86_64,
	.compat_3_brand	= "Linux",
	.emul_path	= "/compat/linux",
	.interp_path	= "/lib64/ld-linux.so.2",
	.sysvec		= &elf_linux_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &linux64_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

static Elf64_Brandinfo linux_muslbrand = {
	.brand		= ELFOSABI_LINUX,
	.machine	= EM_X86_64,
	.compat_3_brand	= "Linux",
	.emul_path	= "/compat/linux",
	.interp_path	= "/lib/ld-musl-x86_64.so.1",
	.sysvec		= &elf_linux_sysvec,
	.interp_newpath	= NULL,
	.brand_note	= &linux64_brandnote,
	.flags		= BI_CAN_EXEC_DYN | BI_BRAND_NOTE
};

Elf64_Brandinfo *linux_brandlist[] = {
	&linux_glibc2brand,
	&linux_glibc2brandshort,
	&linux_muslbrand,
	NULL
};

static int
linux64_elf_modevent(module_t mod, int type, void *data)
{
	Elf64_Brandinfo **brandinfo;
	int error;
	struct linux_ioctl_handler **lihp;

	error = 0;

	switch(type) {
	case MOD_LOAD:
		for (brandinfo = &linux_brandlist[0]; *brandinfo != NULL;
		     ++brandinfo)
			if (elf64_insert_brand_entry(*brandinfo) < 0)
				error = EINVAL;
		if (error == 0) {
			SET_FOREACH(lihp, linux_ioctl_handler_set)
				linux_ioctl_register_handler(*lihp);
			LIST_INIT(&futex_list);
			mtx_init(&futex_mtx, "ftllk64", NULL, MTX_DEF);
			stclohz = (stathz ? stathz : hz);
			if (bootverbose)
				printf("Linux x86-64 ELF exec handler installed\n");
		} else
			printf("cannot insert Linux x86-64 ELF brand handler\n");
		break;
	case MOD_UNLOAD:
		for (brandinfo = &linux_brandlist[0]; *brandinfo != NULL;
		     ++brandinfo)
			if (elf64_brand_inuse(*brandinfo))
				error = EBUSY;
		if (error == 0) {
			for (brandinfo = &linux_brandlist[0];
			     *brandinfo != NULL; ++brandinfo)
				if (elf64_remove_brand_entry(*brandinfo) < 0)
					error = EINVAL;
		}
		if (error == 0) {
			SET_FOREACH(lihp, linux_ioctl_handler_set)
				linux_ioctl_unregister_handler(*lihp);
			mtx_destroy(&futex_mtx);
			if (bootverbose)
				printf("Linux ELF exec handler removed\n");
		} else
			printf("Could not deinstall ELF interpreter entry\n");
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (error);
}

static moduledata_t linux64_elf_mod = {
	"linux64elf",
	linux64_elf_modevent,
	0
};

DECLARE_MODULE_TIED(linux64elf, linux64_elf_mod, SI_SUB_EXEC, SI_ORDER_ANY);
MODULE_DEPEND(linux64elf, linux_common, 1, 1, 1);
FEATURE(linux64, "Linux 64bit support");
