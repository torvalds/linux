/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003 Peter Wemm.
 * Copyright (c) 1992 Terrence R. Lambert.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *
 *	from: @(#)machdep.c	7.4 (Berkeley) 6/3/91
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_atpic.h"
#include "opt_cpu.h"
#include "opt_ddb.h"
#include "opt_inet.h"
#include "opt_isa.h"
#include "opt_kstack_pages.h"
#include "opt_maxmem.h"
#include "opt_mp_watchdog.h"
#include "opt_pci.h"
#include "opt_platform.h"
#include "opt_sched.h"

#include <sys/param.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/cons.h>
#include <sys/cpu.h>
#include <sys/efi.h>
#include <sys/eventhandler.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/memrange.h>
#include <sys/msgbuf.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/ptrace.h>
#include <sys/reboot.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/signalvar.h>
#ifdef SMP
#include <sys/smp.h>
#endif
#include <sys/syscallsubr.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/ucontext.h>
#include <sys/vmmeter.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vm_param.h>
#include <vm/vm_phys.h>

#ifdef DDB
#ifndef KDB
#error KDB must be enabled in order for DDB to work!
#endif
#include <ddb/ddb.h>
#include <ddb/db_sym.h>
#endif

#include <net/netisr.h>

#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/cputypes.h>
#include <machine/frame.h>
#include <machine/intr_machdep.h>
#include <x86/mca.h>
#include <machine/md_var.h>
#include <machine/metadata.h>
#include <machine/mp_watchdog.h>
#include <machine/pc/bios.h>
#include <machine/pcb.h>
#include <machine/proc.h>
#include <machine/reg.h>
#include <machine/sigframe.h>
#include <machine/specialreg.h>
#include <machine/trap.h>
#include <machine/tss.h>
#include <x86/ucode.h>
#include <x86/ifunc.h>
#ifdef SMP
#include <machine/smp.h>
#endif
#ifdef FDT
#include <x86/fdt.h>
#endif

#ifdef DEV_ATPIC
#include <x86/isa/icu.h>
#else
#include <x86/apicvar.h>
#endif

#include <isa/isareg.h>
#include <isa/rtc.h>
#include <x86/init.h>

/* Sanity check for __curthread() */
CTASSERT(offsetof(struct pcpu, pc_curthread) == 0);

/*
 * The PTI trampoline stack needs enough space for a hardware trapframe and a
 * couple of scratch registers, as well as the trapframe left behind after an
 * iret fault.
 */
CTASSERT(PC_PTI_STACK_SZ * sizeof(register_t) >= 2 * sizeof(struct pti_frame) -
    offsetof(struct pti_frame, pti_rip));

extern u_int64_t hammer_time(u_int64_t, u_int64_t);

#define	CS_SECURE(cs)		(ISPL(cs) == SEL_UPL)
#define	EFL_SECURE(ef, oef)	((((ef) ^ (oef)) & ~PSL_USERCHANGE) == 0)

static void cpu_startup(void *);
static void get_fpcontext(struct thread *td, mcontext_t *mcp,
    char *xfpusave, size_t xfpusave_len);
static int  set_fpcontext(struct thread *td, mcontext_t *mcp,
    char *xfpustate, size_t xfpustate_len);
SYSINIT(cpu, SI_SUB_CPU, SI_ORDER_FIRST, cpu_startup, NULL);

/* Preload data parse function */
static caddr_t native_parse_preload_data(u_int64_t);

/* Native function to fetch and parse the e820 map */
static void native_parse_memmap(caddr_t, vm_paddr_t *, int *);

/* Default init_ops implementation. */
struct init_ops init_ops = {
	.parse_preload_data =	native_parse_preload_data,
	.early_clock_source_init =	i8254_init,
	.early_delay =			i8254_delay,
	.parse_memmap =			native_parse_memmap,
#ifdef SMP
	.mp_bootaddress =		mp_bootaddress,
	.start_all_aps =		native_start_all_aps,
#endif
#ifdef DEV_PCI
	.msi_init =			msi_init,
#endif
};

/*
 * Physical address of the EFI System Table. Stashed from the metadata hints
 * passed into the kernel and used by the EFI code to call runtime services.
 */
vm_paddr_t efi_systbl_phys;

/* Intel ICH registers */
#define ICH_PMBASE	0x400
#define ICH_SMI_EN	ICH_PMBASE + 0x30

int	_udatasel, _ucodesel, _ucode32sel, _ufssel, _ugssel;

int cold = 1;

long Maxmem = 0;
long realmem = 0;

/*
 * The number of PHYSMAP entries must be one less than the number of
 * PHYSSEG entries because the PHYSMAP entry that spans the largest
 * physical address that is accessible by ISA DMA is split into two
 * PHYSSEG entries.
 */
#define	PHYSMAP_SIZE	(2 * (VM_PHYSSEG_MAX - 1))

vm_paddr_t phys_avail[PHYSMAP_SIZE + 2];
vm_paddr_t dump_avail[PHYSMAP_SIZE + 2];

/* must be 2 less so 0 0 can signal end of chunks */
#define	PHYS_AVAIL_ARRAY_END (nitems(phys_avail) - 2)
#define	DUMP_AVAIL_ARRAY_END (nitems(dump_avail) - 2)

struct kva_md_info kmi;

static struct trapframe proc0_tf;
struct region_descriptor r_gdt, r_idt;

struct pcpu __pcpu[MAXCPU];

struct mtx icu_lock;

struct mem_range_softc mem_range_softc;

struct mtx dt_lock;	/* lock for GDT and LDT */

void (*vmm_resume_p)(void);

static void
cpu_startup(dummy)
	void *dummy;
{
	uintmax_t memsize;
	char *sysenv;

	/*
	 * On MacBooks, we need to disallow the legacy USB circuit to
	 * generate an SMI# because this can cause several problems,
	 * namely: incorrect CPU frequency detection and failure to
	 * start the APs.
	 * We do this by disabling a bit in the SMI_EN (SMI Control and
	 * Enable register) of the Intel ICH LPC Interface Bridge. 
	 */
	sysenv = kern_getenv("smbios.system.product");
	if (sysenv != NULL) {
		if (strncmp(sysenv, "MacBook1,1", 10) == 0 ||
		    strncmp(sysenv, "MacBook3,1", 10) == 0 ||
		    strncmp(sysenv, "MacBook4,1", 10) == 0 ||
		    strncmp(sysenv, "MacBookPro1,1", 13) == 0 ||
		    strncmp(sysenv, "MacBookPro1,2", 13) == 0 ||
		    strncmp(sysenv, "MacBookPro3,1", 13) == 0 ||
		    strncmp(sysenv, "MacBookPro4,1", 13) == 0 ||
		    strncmp(sysenv, "Macmini1,1", 10) == 0) {
			if (bootverbose)
				printf("Disabling LEGACY_USB_EN bit on "
				    "Intel ICH.\n");
			outl(ICH_SMI_EN, inl(ICH_SMI_EN) & ~0x8);
		}
		freeenv(sysenv);
	}

	/*
	 * Good {morning,afternoon,evening,night}.
	 */
	startrtclock();
	printcpuinfo();

	/*
	 * Display physical memory if SMBIOS reports reasonable amount.
	 */
	memsize = 0;
	sysenv = kern_getenv("smbios.memory.enabled");
	if (sysenv != NULL) {
		memsize = (uintmax_t)strtoul(sysenv, (char **)NULL, 10) << 10;
		freeenv(sysenv);
	}
	if (memsize < ptoa((uintmax_t)vm_free_count()))
		memsize = ptoa((uintmax_t)Maxmem);
	printf("real memory  = %ju (%ju MB)\n", memsize, memsize >> 20);
	realmem = atop(memsize);

	/*
	 * Display any holes after the first chunk of extended memory.
	 */
	if (bootverbose) {
		int indx;

		printf("Physical memory chunk(s):\n");
		for (indx = 0; phys_avail[indx + 1] != 0; indx += 2) {
			vm_paddr_t size;

			size = phys_avail[indx + 1] - phys_avail[indx];
			printf(
			    "0x%016jx - 0x%016jx, %ju bytes (%ju pages)\n",
			    (uintmax_t)phys_avail[indx],
			    (uintmax_t)phys_avail[indx + 1] - 1,
			    (uintmax_t)size, (uintmax_t)size / PAGE_SIZE);
		}
	}

	vm_ksubmap_init(&kmi);

	printf("avail memory = %ju (%ju MB)\n",
	    ptoa((uintmax_t)vm_free_count()),
	    ptoa((uintmax_t)vm_free_count()) / 1048576);
#ifdef DEV_PCI
	if (bootverbose && intel_graphics_stolen_base != 0)
		printf("intel stolen mem: base %#jx size %ju MB\n",
		    (uintmax_t)intel_graphics_stolen_base,
		    (uintmax_t)intel_graphics_stolen_size / 1024 / 1024);
#endif

	/*
	 * Set up buffers, so they can be used to read disk labels.
	 */
	bufinit();
	vm_pager_bufferinit();

	cpu_setregs();
}

/*
 * Send an interrupt to process.
 *
 * Stack is set up to allow sigcode stored
 * at top to call routine, followed by call
 * to sigreturn routine below.  After sigreturn
 * resets the signal mask, the stack, and the
 * frame pointer, it returns to the user
 * specified pc, psl.
 */
void
sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask)
{
	struct sigframe sf, *sfp;
	struct pcb *pcb;
	struct proc *p;
	struct thread *td;
	struct sigacts *psp;
	char *sp;
	struct trapframe *regs;
	char *xfpusave;
	size_t xfpusave_len;
	int sig;
	int oonstack;

	td = curthread;
	pcb = td->td_pcb;
	p = td->td_proc;
	PROC_LOCK_ASSERT(p, MA_OWNED);
	sig = ksi->ksi_signo;
	psp = p->p_sigacts;
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
	sf.sf_uc.uc_stack = td->td_sigstk;
	sf.sf_uc.uc_stack.ss_flags = (td->td_pflags & TDP_ALTSTACK)
	    ? ((oonstack) ? SS_ONSTACK : 0) : SS_DISABLE;
	sf.sf_uc.uc_mcontext.mc_onstack = (oonstack) ? 1 : 0;
	bcopy(regs, &sf.sf_uc.uc_mcontext.mc_rdi, sizeof(*regs));
	sf.sf_uc.uc_mcontext.mc_len = sizeof(sf.sf_uc.uc_mcontext); /* magic */
	get_fpcontext(td, &sf.sf_uc.uc_mcontext, xfpusave, xfpusave_len);
	fpstate_drop(td);
	update_pcb_bases(pcb);
	sf.sf_uc.uc_mcontext.mc_fsbase = pcb->pcb_fsbase;
	sf.sf_uc.uc_mcontext.mc_gsbase = pcb->pcb_gsbase;
	bzero(sf.sf_uc.uc_mcontext.mc_spare,
	    sizeof(sf.sf_uc.uc_mcontext.mc_spare));

	/* Allocate space for the signal handler context. */
	if ((td->td_pflags & TDP_ALTSTACK) != 0 && !oonstack &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		sp = (char *)td->td_sigstk.ss_sp + td->td_sigstk.ss_size;
#if defined(COMPAT_43)
		td->td_sigstk.ss_flags |= SS_ONSTACK;
#endif
	} else
		sp = (char *)regs->tf_rsp - 128;
	if (xfpusave != NULL) {
		sp -= xfpusave_len;
		sp = (char *)((unsigned long)sp & ~0x3Ful);
		sf.sf_uc.uc_mcontext.mc_xfpustate = (register_t)sp;
	}
	sp -= sizeof(struct sigframe);
	/* Align to 16 bytes. */
	sfp = (struct sigframe *)((unsigned long)sp & ~0xFul);

	/* Build the argument list for the signal handler. */
	regs->tf_rdi = sig;			/* arg 1 in %rdi */
	regs->tf_rdx = (register_t)&sfp->sf_uc;	/* arg 3 in %rdx */
	bzero(&sf.sf_si, sizeof(sf.sf_si));
	if (SIGISMEMBER(psp->ps_siginfo, sig)) {
		/* Signal handler installed with SA_SIGINFO. */
		regs->tf_rsi = (register_t)&sfp->sf_si;	/* arg 2 in %rsi */
		sf.sf_ahu.sf_action = (__siginfohandler_t *)catcher;

		/* Fill in POSIX parts */
		sf.sf_si = ksi->ksi_info;
		sf.sf_si.si_signo = sig; /* maybe a translated signal */
		regs->tf_rcx = (register_t)ksi->ksi_addr; /* arg 4 in %rcx */
	} else {
		/* Old FreeBSD-style arguments. */
		regs->tf_rsi = ksi->ksi_code;	/* arg 2 in %rsi */
		regs->tf_rcx = (register_t)ksi->ksi_addr; /* arg 4 in %rcx */
		sf.sf_ahu.sf_handler = catcher;
	}
	mtx_unlock(&psp->ps_mtx);
	PROC_UNLOCK(p);

	/*
	 * Copy the sigframe out to the user's stack.
	 */
	if (copyout(&sf, sfp, sizeof(*sfp)) != 0 ||
	    (xfpusave != NULL && copyout(xfpusave,
	    (void *)sf.sf_uc.uc_mcontext.mc_xfpustate, xfpusave_len)
	    != 0)) {
#ifdef DEBUG
		printf("process %ld has trashed its stack\n", (long)p->p_pid);
#endif
		PROC_LOCK(p);
		sigexit(td, SIGILL);
	}

	regs->tf_rsp = (long)sfp;
	regs->tf_rip = p->p_sysent->sv_sigcode_base;
	regs->tf_rflags &= ~(PSL_T | PSL_D);
	regs->tf_cs = _ucodesel;
	regs->tf_ds = _udatasel;
	regs->tf_ss = _udatasel;
	regs->tf_es = _udatasel;
	regs->tf_fs = _ufssel;
	regs->tf_gs = _ugssel;
	regs->tf_flags = TF_HASSEGS;
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
 *
 * MPSAFE
 */
int
sys_sigreturn(td, uap)
	struct thread *td;
	struct sigreturn_args /* {
		const struct __ucontext *sigcntxp;
	} */ *uap;
{
	ucontext_t uc;
	struct pcb *pcb;
	struct proc *p;
	struct trapframe *regs;
	ucontext_t *ucp;
	char *xfpustate;
	size_t xfpustate_len;
	long rflags;
	int cs, error, ret;
	ksiginfo_t ksi;

	pcb = td->td_pcb;
	p = td->td_proc;

	error = copyin(uap->sigcntxp, &uc, sizeof(uc));
	if (error != 0) {
		uprintf("pid %d (%s): sigreturn copyin failed\n",
		    p->p_pid, td->td_name);
		return (error);
	}
	ucp = &uc;
	if ((ucp->uc_mcontext.mc_flags & ~_MC_FLAG_MASK) != 0) {
		uprintf("pid %d (%s): sigreturn mc_flags %x\n", p->p_pid,
		    td->td_name, ucp->uc_mcontext.mc_flags);
		return (EINVAL);
	}
	regs = td->td_frame;
	rflags = ucp->uc_mcontext.mc_rflags;
	/*
	 * Don't allow users to change privileged or reserved flags.
	 */
	if (!EFL_SECURE(rflags, regs->tf_rflags)) {
		uprintf("pid %d (%s): sigreturn rflags = 0x%lx\n", p->p_pid,
		    td->td_name, rflags);
		return (EINVAL);
	}

	/*
	 * Don't allow users to load a valid privileged %cs.  Let the
	 * hardware check for invalid selectors, excess privilege in
	 * other selectors, invalid %eip's and invalid %esp's.
	 */
	cs = ucp->uc_mcontext.mc_cs;
	if (!CS_SECURE(cs)) {
		uprintf("pid %d (%s): sigreturn cs = 0x%x\n", p->p_pid,
		    td->td_name, cs);
		ksiginfo_init_trap(&ksi);
		ksi.ksi_signo = SIGBUS;
		ksi.ksi_code = BUS_OBJERR;
		ksi.ksi_trapno = T_PROTFLT;
		ksi.ksi_addr = (void *)regs->tf_rip;
		trapsignal(td, &ksi);
		return (EINVAL);
	}

	if ((uc.uc_mcontext.mc_flags & _MC_HASFPXSTATE) != 0) {
		xfpustate_len = uc.uc_mcontext.mc_xfpustate_len;
		if (xfpustate_len > cpu_max_ext_state_size -
		    sizeof(struct savefpu)) {
			uprintf("pid %d (%s): sigreturn xfpusave_len = 0x%zx\n",
			    p->p_pid, td->td_name, xfpustate_len);
			return (EINVAL);
		}
		xfpustate = __builtin_alloca(xfpustate_len);
		error = copyin((const void *)uc.uc_mcontext.mc_xfpustate,
		    xfpustate, xfpustate_len);
		if (error != 0) {
			uprintf(
	"pid %d (%s): sigreturn copying xfpustate failed\n",
			    p->p_pid, td->td_name);
			return (error);
		}
	} else {
		xfpustate = NULL;
		xfpustate_len = 0;
	}
	ret = set_fpcontext(td, &ucp->uc_mcontext, xfpustate, xfpustate_len);
	if (ret != 0) {
		uprintf("pid %d (%s): sigreturn set_fpcontext err %d\n",
		    p->p_pid, td->td_name, ret);
		return (ret);
	}
	bcopy(&ucp->uc_mcontext.mc_rdi, regs, sizeof(*regs));
	update_pcb_bases(pcb);
	pcb->pcb_fsbase = ucp->uc_mcontext.mc_fsbase;
	pcb->pcb_gsbase = ucp->uc_mcontext.mc_gsbase;

#if defined(COMPAT_43)
	if (ucp->uc_mcontext.mc_onstack & 1)
		td->td_sigstk.ss_flags |= SS_ONSTACK;
	else
		td->td_sigstk.ss_flags &= ~SS_ONSTACK;
#endif

	kern_sigprocmask(td, SIG_SETMASK, &ucp->uc_sigmask, NULL, 0);
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
 * Reset registers to default values on exec.
 */
void
exec_setregs(struct thread *td, struct image_params *imgp, u_long stack)
{
	struct trapframe *regs;
	struct pcb *pcb;
	register_t saved_rflags;

	regs = td->td_frame;
	pcb = td->td_pcb;

	if (td->td_proc->p_md.md_ldt != NULL)
		user_ldt_free(td);

	update_pcb_bases(pcb);
	pcb->pcb_fsbase = 0;
	pcb->pcb_gsbase = 0;
	clear_pcb_flags(pcb, PCB_32BIT);
	pcb->pcb_initial_fpucw = __INITIAL_FPUCW__;

	saved_rflags = regs->tf_rflags & PSL_T;
	bzero((char *)regs, sizeof(struct trapframe));
	regs->tf_rip = imgp->entry_addr;
	regs->tf_rsp = ((stack - 8) & ~0xFul) + 8;
	regs->tf_rdi = stack;		/* argv */
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

void
cpu_setregs(void)
{
	register_t cr0;

	cr0 = rcr0();
	/*
	 * CR0_MP, CR0_NE and CR0_TS are also set by npx_probe() for the
	 * BSP.  See the comments there about why we set them.
	 */
	cr0 |= CR0_MP | CR0_NE | CR0_TS | CR0_WP | CR0_AM;
	load_cr0(cr0);
}

/*
 * Initialize amd64 and configure to run kernel
 */

/*
 * Initialize segments & interrupt table
 */

struct user_segment_descriptor gdt[NGDT * MAXCPU];/* global descriptor tables */
static struct gate_descriptor idt0[NIDT];
struct gate_descriptor *idt = &idt0[0];	/* interrupt descriptor table */

static char dblfault_stack[PAGE_SIZE] __aligned(16);
static char mce0_stack[PAGE_SIZE] __aligned(16);
static char nmi0_stack[PAGE_SIZE] __aligned(16);
static char dbg0_stack[PAGE_SIZE] __aligned(16);
CTASSERT(sizeof(struct nmi_pcpu) == 16);

struct amd64tss common_tss[MAXCPU];

/*
 * Software prototypes -- in more palatable form.
 *
 * Keep GUFS32, GUGS32, GUCODE32 and GUDATA at the same
 * slots as corresponding segments for i386 kernel.
 */
struct soft_segment_descriptor gdt_segs[] = {
/* GNULL_SEL	0 Null Descriptor */
{	.ssd_base = 0x0,
	.ssd_limit = 0x0,
	.ssd_type = 0,
	.ssd_dpl = 0,
	.ssd_p = 0,
	.ssd_long = 0,
	.ssd_def32 = 0,
	.ssd_gran = 0		},
/* GNULL2_SEL	1 Null Descriptor */
{	.ssd_base = 0x0,
	.ssd_limit = 0x0,
	.ssd_type = 0,
	.ssd_dpl = 0,
	.ssd_p = 0,
	.ssd_long = 0,
	.ssd_def32 = 0,
	.ssd_gran = 0		},
/* GUFS32_SEL	2 32 bit %gs Descriptor for user */
{	.ssd_base = 0x0,
	.ssd_limit = 0xfffff,
	.ssd_type = SDT_MEMRWA,
	.ssd_dpl = SEL_UPL,
	.ssd_p = 1,
	.ssd_long = 0,
	.ssd_def32 = 1,
	.ssd_gran = 1		},
/* GUGS32_SEL	3 32 bit %fs Descriptor for user */
{	.ssd_base = 0x0,
	.ssd_limit = 0xfffff,
	.ssd_type = SDT_MEMRWA,
	.ssd_dpl = SEL_UPL,
	.ssd_p = 1,
	.ssd_long = 0,
	.ssd_def32 = 1,
	.ssd_gran = 1		},
/* GCODE_SEL	4 Code Descriptor for kernel */
{	.ssd_base = 0x0,
	.ssd_limit = 0xfffff,
	.ssd_type = SDT_MEMERA,
	.ssd_dpl = SEL_KPL,
	.ssd_p = 1,
	.ssd_long = 1,
	.ssd_def32 = 0,
	.ssd_gran = 1		},
/* GDATA_SEL	5 Data Descriptor for kernel */
{	.ssd_base = 0x0,
	.ssd_limit = 0xfffff,
	.ssd_type = SDT_MEMRWA,
	.ssd_dpl = SEL_KPL,
	.ssd_p = 1,
	.ssd_long = 1,
	.ssd_def32 = 0,
	.ssd_gran = 1		},
/* GUCODE32_SEL	6 32 bit Code Descriptor for user */
{	.ssd_base = 0x0,
	.ssd_limit = 0xfffff,
	.ssd_type = SDT_MEMERA,
	.ssd_dpl = SEL_UPL,
	.ssd_p = 1,
	.ssd_long = 0,
	.ssd_def32 = 1,
	.ssd_gran = 1		},
/* GUDATA_SEL	7 32/64 bit Data Descriptor for user */
{	.ssd_base = 0x0,
	.ssd_limit = 0xfffff,
	.ssd_type = SDT_MEMRWA,
	.ssd_dpl = SEL_UPL,
	.ssd_p = 1,
	.ssd_long = 0,
	.ssd_def32 = 1,
	.ssd_gran = 1		},
/* GUCODE_SEL	8 64 bit Code Descriptor for user */
{	.ssd_base = 0x0,
	.ssd_limit = 0xfffff,
	.ssd_type = SDT_MEMERA,
	.ssd_dpl = SEL_UPL,
	.ssd_p = 1,
	.ssd_long = 1,
	.ssd_def32 = 0,
	.ssd_gran = 1		},
/* GPROC0_SEL	9 Proc 0 Tss Descriptor */
{	.ssd_base = 0x0,
	.ssd_limit = sizeof(struct amd64tss) + IOPERM_BITMAP_SIZE - 1,
	.ssd_type = SDT_SYSTSS,
	.ssd_dpl = SEL_KPL,
	.ssd_p = 1,
	.ssd_long = 0,
	.ssd_def32 = 0,
	.ssd_gran = 0		},
/* Actually, the TSS is a system descriptor which is double size */
{	.ssd_base = 0x0,
	.ssd_limit = 0x0,
	.ssd_type = 0,
	.ssd_dpl = 0,
	.ssd_p = 0,
	.ssd_long = 0,
	.ssd_def32 = 0,
	.ssd_gran = 0		},
/* GUSERLDT_SEL	11 LDT Descriptor */
{	.ssd_base = 0x0,
	.ssd_limit = 0x0,
	.ssd_type = 0,
	.ssd_dpl = 0,
	.ssd_p = 0,
	.ssd_long = 0,
	.ssd_def32 = 0,
	.ssd_gran = 0		},
/* GUSERLDT_SEL	12 LDT Descriptor, double size */
{	.ssd_base = 0x0,
	.ssd_limit = 0x0,
	.ssd_type = 0,
	.ssd_dpl = 0,
	.ssd_p = 0,
	.ssd_long = 0,
	.ssd_def32 = 0,
	.ssd_gran = 0		},
};

void
setidt(int idx, inthand_t *func, int typ, int dpl, int ist)
{
	struct gate_descriptor *ip;

	ip = idt + idx;
	ip->gd_looffset = (uintptr_t)func;
	ip->gd_selector = GSEL(GCODE_SEL, SEL_KPL);
	ip->gd_ist = ist;
	ip->gd_xx = 0;
	ip->gd_type = typ;
	ip->gd_dpl = dpl;
	ip->gd_p = 1;
	ip->gd_hioffset = ((uintptr_t)func)>>16 ;
}

extern inthand_t
	IDTVEC(div), IDTVEC(dbg), IDTVEC(nmi), IDTVEC(bpt), IDTVEC(ofl),
	IDTVEC(bnd), IDTVEC(ill), IDTVEC(dna), IDTVEC(fpusegm),
	IDTVEC(tss), IDTVEC(missing), IDTVEC(stk), IDTVEC(prot),
	IDTVEC(page), IDTVEC(mchk), IDTVEC(rsvd), IDTVEC(fpu), IDTVEC(align),
	IDTVEC(xmm), IDTVEC(dblfault),
	IDTVEC(div_pti), IDTVEC(bpt_pti),
	IDTVEC(ofl_pti), IDTVEC(bnd_pti), IDTVEC(ill_pti), IDTVEC(dna_pti),
	IDTVEC(fpusegm_pti), IDTVEC(tss_pti), IDTVEC(missing_pti),
	IDTVEC(stk_pti), IDTVEC(prot_pti), IDTVEC(page_pti),
	IDTVEC(rsvd_pti), IDTVEC(fpu_pti), IDTVEC(align_pti),
	IDTVEC(xmm_pti),
#ifdef KDTRACE_HOOKS
	IDTVEC(dtrace_ret), IDTVEC(dtrace_ret_pti),
#endif
#ifdef XENHVM
	IDTVEC(xen_intr_upcall), IDTVEC(xen_intr_upcall_pti),
#endif
	IDTVEC(fast_syscall), IDTVEC(fast_syscall32),
	IDTVEC(fast_syscall_pti);

#ifdef DDB
/*
 * Display the index and function name of any IDT entries that don't use
 * the default 'rsvd' entry point.
 */
DB_SHOW_COMMAND(idt, db_show_idt)
{
	struct gate_descriptor *ip;
	int idx;
	uintptr_t func;

	ip = idt;
	for (idx = 0; idx < NIDT && !db_pager_quit; idx++) {
		func = ((long)ip->gd_hioffset << 16 | ip->gd_looffset);
		if (func != (uintptr_t)&IDTVEC(rsvd)) {
			db_printf("%3d\t", idx);
			db_printsym(func, DB_STGY_PROC);
			db_printf("\n");
		}
		ip++;
	}
}

/* Show privileged registers. */
DB_SHOW_COMMAND(sysregs, db_show_sysregs)
{
	struct {
		uint16_t limit;
		uint64_t base;
	} __packed idtr, gdtr;
	uint16_t ldt, tr;

	__asm __volatile("sidt %0" : "=m" (idtr));
	db_printf("idtr\t0x%016lx/%04x\n",
	    (u_long)idtr.base, (u_int)idtr.limit);
	__asm __volatile("sgdt %0" : "=m" (gdtr));
	db_printf("gdtr\t0x%016lx/%04x\n",
	    (u_long)gdtr.base, (u_int)gdtr.limit);
	__asm __volatile("sldt %0" : "=r" (ldt));
	db_printf("ldtr\t0x%04x\n", ldt);
	__asm __volatile("str %0" : "=r" (tr));
	db_printf("tr\t0x%04x\n", tr);
	db_printf("cr0\t0x%016lx\n", rcr0());
	db_printf("cr2\t0x%016lx\n", rcr2());
	db_printf("cr3\t0x%016lx\n", rcr3());
	db_printf("cr4\t0x%016lx\n", rcr4());
	if (rcr4() & CR4_XSAVE)
		db_printf("xcr0\t0x%016lx\n", rxcr(0));
	db_printf("EFER\t0x%016lx\n", rdmsr(MSR_EFER));
	if (cpu_feature2 & (CPUID2_VMX | CPUID2_SMX))
		db_printf("FEATURES_CTL\t%016lx\n",
		    rdmsr(MSR_IA32_FEATURE_CONTROL));
	db_printf("DEBUG_CTL\t0x%016lx\n", rdmsr(MSR_DEBUGCTLMSR));
	db_printf("PAT\t0x%016lx\n", rdmsr(MSR_PAT));
	db_printf("GSBASE\t0x%016lx\n", rdmsr(MSR_GSBASE));
}

DB_SHOW_COMMAND(dbregs, db_show_dbregs)
{

	db_printf("dr0\t0x%016lx\n", rdr0());
	db_printf("dr1\t0x%016lx\n", rdr1());
	db_printf("dr2\t0x%016lx\n", rdr2());
	db_printf("dr3\t0x%016lx\n", rdr3());
	db_printf("dr6\t0x%016lx\n", rdr6());
	db_printf("dr7\t0x%016lx\n", rdr7());	
}
#endif

void
sdtossd(sd, ssd)
	struct user_segment_descriptor *sd;
	struct soft_segment_descriptor *ssd;
{

	ssd->ssd_base  = (sd->sd_hibase << 24) | sd->sd_lobase;
	ssd->ssd_limit = (sd->sd_hilimit << 16) | sd->sd_lolimit;
	ssd->ssd_type  = sd->sd_type;
	ssd->ssd_dpl   = sd->sd_dpl;
	ssd->ssd_p     = sd->sd_p;
	ssd->ssd_long  = sd->sd_long;
	ssd->ssd_def32 = sd->sd_def32;
	ssd->ssd_gran  = sd->sd_gran;
}

void
ssdtosd(ssd, sd)
	struct soft_segment_descriptor *ssd;
	struct user_segment_descriptor *sd;
{

	sd->sd_lobase = (ssd->ssd_base) & 0xffffff;
	sd->sd_hibase = (ssd->ssd_base >> 24) & 0xff;
	sd->sd_lolimit = (ssd->ssd_limit) & 0xffff;
	sd->sd_hilimit = (ssd->ssd_limit >> 16) & 0xf;
	sd->sd_type  = ssd->ssd_type;
	sd->sd_dpl   = ssd->ssd_dpl;
	sd->sd_p     = ssd->ssd_p;
	sd->sd_long  = ssd->ssd_long;
	sd->sd_def32 = ssd->ssd_def32;
	sd->sd_gran  = ssd->ssd_gran;
}

void
ssdtosyssd(ssd, sd)
	struct soft_segment_descriptor *ssd;
	struct system_segment_descriptor *sd;
{

	sd->sd_lobase = (ssd->ssd_base) & 0xffffff;
	sd->sd_hibase = (ssd->ssd_base >> 24) & 0xfffffffffful;
	sd->sd_lolimit = (ssd->ssd_limit) & 0xffff;
	sd->sd_hilimit = (ssd->ssd_limit >> 16) & 0xf;
	sd->sd_type  = ssd->ssd_type;
	sd->sd_dpl   = ssd->ssd_dpl;
	sd->sd_p     = ssd->ssd_p;
	sd->sd_gran  = ssd->ssd_gran;
}

#if !defined(DEV_ATPIC) && defined(DEV_ISA)
#include <isa/isavar.h>
#include <isa/isareg.h>
/*
 * Return a bitmap of the current interrupt requests.  This is 8259-specific
 * and is only suitable for use at probe time.
 * This is only here to pacify sio.  It is NOT FATAL if this doesn't work.
 * It shouldn't be here.  There should probably be an APIC centric
 * implementation in the apic driver code, if at all.
 */
intrmask_t
isa_irq_pending(void)
{
	u_char irr1;
	u_char irr2;

	irr1 = inb(IO_ICU1);
	irr2 = inb(IO_ICU2);
	return ((irr2 << 8) | irr1);
}
#endif

u_int basemem;

static int
add_physmap_entry(uint64_t base, uint64_t length, vm_paddr_t *physmap,
    int *physmap_idxp)
{
	int i, insert_idx, physmap_idx;

	physmap_idx = *physmap_idxp;

	if (length == 0)
		return (1);

	/*
	 * Find insertion point while checking for overlap.  Start off by
	 * assuming the new entry will be added to the end.
	 *
	 * NB: physmap_idx points to the next free slot.
	 */
	insert_idx = physmap_idx;
	for (i = 0; i <= physmap_idx; i += 2) {
		if (base < physmap[i + 1]) {
			if (base + length <= physmap[i]) {
				insert_idx = i;
				break;
			}
			if (boothowto & RB_VERBOSE)
				printf(
		    "Overlapping memory regions, ignoring second region\n");
			return (1);
		}
	}

	/* See if we can prepend to the next entry. */
	if (insert_idx <= physmap_idx && base + length == physmap[insert_idx]) {
		physmap[insert_idx] = base;
		return (1);
	}

	/* See if we can append to the previous entry. */
	if (insert_idx > 0 && base == physmap[insert_idx - 1]) {
		physmap[insert_idx - 1] += length;
		return (1);
	}

	physmap_idx += 2;
	*physmap_idxp = physmap_idx;
	if (physmap_idx == PHYSMAP_SIZE) {
		printf(
		"Too many segments in the physical address map, giving up\n");
		return (0);
	}

	/*
	 * Move the last 'N' entries down to make room for the new
	 * entry if needed.
	 */
	for (i = (physmap_idx - 2); i > insert_idx; i -= 2) {
		physmap[i] = physmap[i - 2];
		physmap[i + 1] = physmap[i - 1];
	}

	/* Insert the new entry. */
	physmap[insert_idx] = base;
	physmap[insert_idx + 1] = base + length;
	return (1);
}

void
bios_add_smap_entries(struct bios_smap *smapbase, u_int32_t smapsize,
                      vm_paddr_t *physmap, int *physmap_idx)
{
	struct bios_smap *smap, *smapend;

	smapend = (struct bios_smap *)((uintptr_t)smapbase + smapsize);

	for (smap = smapbase; smap < smapend; smap++) {
		if (boothowto & RB_VERBOSE)
			printf("SMAP type=%02x base=%016lx len=%016lx\n",
			    smap->type, smap->base, smap->length);

		if (smap->type != SMAP_TYPE_MEMORY)
			continue;

		if (!add_physmap_entry(smap->base, smap->length, physmap,
		    physmap_idx))
			break;
	}
}

static void
add_efi_map_entries(struct efi_map_header *efihdr, vm_paddr_t *physmap,
    int *physmap_idx)
{
	struct efi_md *map, *p;
	const char *type;
	size_t efisz;
	int ndesc, i;

	static const char *types[] = {
		"Reserved",
		"LoaderCode",
		"LoaderData",
		"BootServicesCode",
		"BootServicesData",
		"RuntimeServicesCode",
		"RuntimeServicesData",
		"ConventionalMemory",
		"UnusableMemory",
		"ACPIReclaimMemory",
		"ACPIMemoryNVS",
		"MemoryMappedIO",
		"MemoryMappedIOPortSpace",
		"PalCode",
		"PersistentMemory"
	};

	/*
	 * Memory map data provided by UEFI via the GetMemoryMap
	 * Boot Services API.
	 */
	efisz = (sizeof(struct efi_map_header) + 0xf) & ~0xf;
	map = (struct efi_md *)((uint8_t *)efihdr + efisz);

	if (efihdr->descriptor_size == 0)
		return;
	ndesc = efihdr->memory_size / efihdr->descriptor_size;

	if (boothowto & RB_VERBOSE)
		printf("%23s %12s %12s %8s %4s\n",
		    "Type", "Physical", "Virtual", "#Pages", "Attr");

	for (i = 0, p = map; i < ndesc; i++,
	    p = efi_next_descriptor(p, efihdr->descriptor_size)) {
		if (boothowto & RB_VERBOSE) {
			if (p->md_type < nitems(types))
				type = types[p->md_type];
			else
				type = "<INVALID>";
			printf("%23s %012lx %12p %08lx ", type, p->md_phys,
			    p->md_virt, p->md_pages);
			if (p->md_attr & EFI_MD_ATTR_UC)
				printf("UC ");
			if (p->md_attr & EFI_MD_ATTR_WC)
				printf("WC ");
			if (p->md_attr & EFI_MD_ATTR_WT)
				printf("WT ");
			if (p->md_attr & EFI_MD_ATTR_WB)
				printf("WB ");
			if (p->md_attr & EFI_MD_ATTR_UCE)
				printf("UCE ");
			if (p->md_attr & EFI_MD_ATTR_WP)
				printf("WP ");
			if (p->md_attr & EFI_MD_ATTR_RP)
				printf("RP ");
			if (p->md_attr & EFI_MD_ATTR_XP)
				printf("XP ");
			if (p->md_attr & EFI_MD_ATTR_NV)
				printf("NV ");
			if (p->md_attr & EFI_MD_ATTR_MORE_RELIABLE)
				printf("MORE_RELIABLE ");
			if (p->md_attr & EFI_MD_ATTR_RO)
				printf("RO ");
			if (p->md_attr & EFI_MD_ATTR_RT)
				printf("RUNTIME");
			printf("\n");
		}

		switch (p->md_type) {
		case EFI_MD_TYPE_CODE:
		case EFI_MD_TYPE_DATA:
		case EFI_MD_TYPE_BS_CODE:
		case EFI_MD_TYPE_BS_DATA:
		case EFI_MD_TYPE_FREE:
			/*
			 * We're allowed to use any entry with these types.
			 */
			break;
		default:
			continue;
		}

		if (!add_physmap_entry(p->md_phys, (p->md_pages * PAGE_SIZE),
		    physmap, physmap_idx))
			break;
	}
}

static char bootmethod[16] = "";
SYSCTL_STRING(_machdep, OID_AUTO, bootmethod, CTLFLAG_RD, bootmethod, 0,
    "System firmware boot method");

static void
native_parse_memmap(caddr_t kmdp, vm_paddr_t *physmap, int *physmap_idx)
{
	struct bios_smap *smap;
	struct efi_map_header *efihdr;
	u_int32_t size;

	/*
	 * Memory map from INT 15:E820.
	 *
	 * subr_module.c says:
	 * "Consumer may safely assume that size value precedes data."
	 * ie: an int32_t immediately precedes smap.
	 */

	efihdr = (struct efi_map_header *)preload_search_info(kmdp,
	    MODINFO_METADATA | MODINFOMD_EFI_MAP);
	smap = (struct bios_smap *)preload_search_info(kmdp,
	    MODINFO_METADATA | MODINFOMD_SMAP);
	if (efihdr == NULL && smap == NULL)
		panic("No BIOS smap or EFI map info from loader!");

	if (efihdr != NULL) {
		add_efi_map_entries(efihdr, physmap, physmap_idx);
		strlcpy(bootmethod, "UEFI", sizeof(bootmethod));
	} else {
		size = *((u_int32_t *)smap - 1);
		bios_add_smap_entries(smap, size, physmap, physmap_idx);
		strlcpy(bootmethod, "BIOS", sizeof(bootmethod));
	}
}

#define	PAGES_PER_GB	(1024 * 1024 * 1024 / PAGE_SIZE)

/*
 * Populate the (physmap) array with base/bound pairs describing the
 * available physical memory in the system, then test this memory and
 * build the phys_avail array describing the actually-available memory.
 *
 * Total memory size may be set by the kernel environment variable
 * hw.physmem or the compile-time define MAXMEM.
 *
 * XXX first should be vm_paddr_t.
 */
static void
getmemsize(caddr_t kmdp, u_int64_t first)
{
	int i, physmap_idx, pa_indx, da_indx;
	vm_paddr_t pa, physmap[PHYSMAP_SIZE];
	u_long physmem_start, physmem_tunable, memtest;
	pt_entry_t *pte;
	quad_t dcons_addr, dcons_size;
	int page_counter;

	/*
	 * Tell the physical memory allocator about pages used to store
	 * the kernel and preloaded data.  See kmem_bootstrap_free().
	 */
	vm_phys_add_seg((vm_paddr_t)kernphys, trunc_page(first));

	bzero(physmap, sizeof(physmap));
	physmap_idx = 0;

	init_ops.parse_memmap(kmdp, physmap, &physmap_idx);
	physmap_idx -= 2;

	/*
	 * Find the 'base memory' segment for SMP
	 */
	basemem = 0;
	for (i = 0; i <= physmap_idx; i += 2) {
		if (physmap[i] <= 0xA0000) {
			basemem = physmap[i + 1] / 1024;
			break;
		}
	}
	if (basemem == 0 || basemem > 640) {
		if (bootverbose)
			printf(
		"Memory map doesn't contain a basemem segment, faking it");
		basemem = 640;
	}

	/*
	 * Maxmem isn't the "maximum memory", it's one larger than the
	 * highest page of the physical address space.  It should be
	 * called something like "Maxphyspage".  We may adjust this
	 * based on ``hw.physmem'' and the results of the memory test.
	 */
	Maxmem = atop(physmap[physmap_idx + 1]);

#ifdef MAXMEM
	Maxmem = MAXMEM / 4;
#endif

	if (TUNABLE_ULONG_FETCH("hw.physmem", &physmem_tunable))
		Maxmem = atop(physmem_tunable);

	/*
	 * The boot memory test is disabled by default, as it takes a
	 * significant amount of time on large-memory systems, and is
	 * unfriendly to virtual machines as it unnecessarily touches all
	 * pages.
	 *
	 * A general name is used as the code may be extended to support
	 * additional tests beyond the current "page present" test.
	 */
	memtest = 0;
	TUNABLE_ULONG_FETCH("hw.memtest.tests", &memtest);

	/*
	 * Don't allow MAXMEM or hw.physmem to extend the amount of memory
	 * in the system.
	 */
	if (Maxmem > atop(physmap[physmap_idx + 1]))
		Maxmem = atop(physmap[physmap_idx + 1]);

	if (atop(physmap[physmap_idx + 1]) != Maxmem &&
	    (boothowto & RB_VERBOSE))
		printf("Physical memory use set to %ldK\n", Maxmem * 4);

	/*
	 * Make hole for "AP -> long mode" bootstrap code.  The
	 * mp_bootaddress vector is only available when the kernel
	 * is configured to support APs and APs for the system start
	 * in real mode mode (e.g. SMP bare metal).
	 */
	if (init_ops.mp_bootaddress)
		init_ops.mp_bootaddress(physmap, &physmap_idx);

	/* call pmap initialization to make new kernel address space */
	pmap_bootstrap(&first);

	/*
	 * Size up each available chunk of physical memory.
	 *
	 * XXX Some BIOSes corrupt low 64KB between suspend and resume.
	 * By default, mask off the first 16 pages unless we appear to be
	 * running in a VM.
	 */
	physmem_start = (vm_guest > VM_GUEST_NO ? 1 : 16) << PAGE_SHIFT;
	TUNABLE_ULONG_FETCH("hw.physmem.start", &physmem_start);
	if (physmap[0] < physmem_start) {
		if (physmem_start < PAGE_SIZE)
			physmap[0] = PAGE_SIZE;
		else if (physmem_start >= physmap[1])
			physmap[0] = round_page(physmap[1] - PAGE_SIZE);
		else
			physmap[0] = round_page(physmem_start);
	}
	pa_indx = 0;
	da_indx = 1;
	phys_avail[pa_indx++] = physmap[0];
	phys_avail[pa_indx] = physmap[0];
	dump_avail[da_indx] = physmap[0];
	pte = CMAP1;

	/*
	 * Get dcons buffer address
	 */
	if (getenv_quad("dcons.addr", &dcons_addr) == 0 ||
	    getenv_quad("dcons.size", &dcons_size) == 0)
		dcons_addr = 0;

	/*
	 * physmap is in bytes, so when converting to page boundaries,
	 * round up the start address and round down the end address.
	 */
	page_counter = 0;
	if (memtest != 0)
		printf("Testing system memory");
	for (i = 0; i <= physmap_idx; i += 2) {
		vm_paddr_t end;

		end = ptoa((vm_paddr_t)Maxmem);
		if (physmap[i + 1] < end)
			end = trunc_page(physmap[i + 1]);
		for (pa = round_page(physmap[i]); pa < end; pa += PAGE_SIZE) {
			int tmp, page_bad, full;
			int *ptr = (int *)CADDR1;

			full = FALSE;
			/*
			 * block out kernel memory as not available.
			 */
			if (pa >= (vm_paddr_t)kernphys && pa < first)
				goto do_dump_avail;

			/*
			 * block out dcons buffer
			 */
			if (dcons_addr > 0
			    && pa >= trunc_page(dcons_addr)
			    && pa < dcons_addr + dcons_size)
				goto do_dump_avail;

			page_bad = FALSE;
			if (memtest == 0)
				goto skip_memtest;

			/*
			 * Print a "." every GB to show we're making
			 * progress.
			 */
			page_counter++;
			if ((page_counter % PAGES_PER_GB) == 0)
				printf(".");

			/*
			 * map page into kernel: valid, read/write,non-cacheable
			 */
			*pte = pa | PG_V | PG_RW | PG_NC_PWT | PG_NC_PCD;
			invltlb();

			tmp = *(int *)ptr;
			/*
			 * Test for alternating 1's and 0's
			 */
			*(volatile int *)ptr = 0xaaaaaaaa;
			if (*(volatile int *)ptr != 0xaaaaaaaa)
				page_bad = TRUE;
			/*
			 * Test for alternating 0's and 1's
			 */
			*(volatile int *)ptr = 0x55555555;
			if (*(volatile int *)ptr != 0x55555555)
				page_bad = TRUE;
			/*
			 * Test for all 1's
			 */
			*(volatile int *)ptr = 0xffffffff;
			if (*(volatile int *)ptr != 0xffffffff)
				page_bad = TRUE;
			/*
			 * Test for all 0's
			 */
			*(volatile int *)ptr = 0x0;
			if (*(volatile int *)ptr != 0x0)
				page_bad = TRUE;
			/*
			 * Restore original value.
			 */
			*(int *)ptr = tmp;

skip_memtest:
			/*
			 * Adjust array of valid/good pages.
			 */
			if (page_bad == TRUE)
				continue;
			/*
			 * If this good page is a continuation of the
			 * previous set of good pages, then just increase
			 * the end pointer. Otherwise start a new chunk.
			 * Note that "end" points one higher than end,
			 * making the range >= start and < end.
			 * If we're also doing a speculative memory
			 * test and we at or past the end, bump up Maxmem
			 * so that we keep going. The first bad page
			 * will terminate the loop.
			 */
			if (phys_avail[pa_indx] == pa) {
				phys_avail[pa_indx] += PAGE_SIZE;
			} else {
				pa_indx++;
				if (pa_indx == PHYS_AVAIL_ARRAY_END) {
					printf(
		"Too many holes in the physical address space, giving up\n");
					pa_indx--;
					full = TRUE;
					goto do_dump_avail;
				}
				phys_avail[pa_indx++] = pa;	/* start */
				phys_avail[pa_indx] = pa + PAGE_SIZE; /* end */
			}
			physmem++;
do_dump_avail:
			if (dump_avail[da_indx] == pa) {
				dump_avail[da_indx] += PAGE_SIZE;
			} else {
				da_indx++;
				if (da_indx == DUMP_AVAIL_ARRAY_END) {
					da_indx--;
					goto do_next;
				}
				dump_avail[da_indx++] = pa; /* start */
				dump_avail[da_indx] = pa + PAGE_SIZE; /* end */
			}
do_next:
			if (full)
				break;
		}
	}
	*pte = 0;
	invltlb();
	if (memtest != 0)
		printf("\n");

	/*
	 * XXX
	 * The last chunk must contain at least one page plus the message
	 * buffer to avoid complicating other code (message buffer address
	 * calculation, etc.).
	 */
	while (phys_avail[pa_indx - 1] + PAGE_SIZE +
	    round_page(msgbufsize) >= phys_avail[pa_indx]) {
		physmem -= atop(phys_avail[pa_indx] - phys_avail[pa_indx - 1]);
		phys_avail[pa_indx--] = 0;
		phys_avail[pa_indx--] = 0;
	}

	Maxmem = atop(phys_avail[pa_indx]);

	/* Trim off space for the message buffer. */
	phys_avail[pa_indx] -= round_page(msgbufsize);

	/* Map the message buffer. */
	msgbufp = (struct msgbuf *)PHYS_TO_DMAP(phys_avail[pa_indx]);
}

static caddr_t
native_parse_preload_data(u_int64_t modulep)
{
	caddr_t kmdp;
	char *envp;
#ifdef DDB
	vm_offset_t ksym_start;
	vm_offset_t ksym_end;
#endif

	preload_metadata = (caddr_t)(uintptr_t)(modulep + KERNBASE);
	preload_bootstrap_relocate(KERNBASE);
	kmdp = preload_search_by_type("elf kernel");
	if (kmdp == NULL)
		kmdp = preload_search_by_type("elf64 kernel");
	boothowto = MD_FETCH(kmdp, MODINFOMD_HOWTO, int);
	envp = MD_FETCH(kmdp, MODINFOMD_ENVP, char *);
	if (envp != NULL)
		envp += KERNBASE;
	init_static_kenv(envp, 0);
#ifdef DDB
	ksym_start = MD_FETCH(kmdp, MODINFOMD_SSYM, uintptr_t);
	ksym_end = MD_FETCH(kmdp, MODINFOMD_ESYM, uintptr_t);
	db_fetch_ksymtab(ksym_start, ksym_end);
#endif
	efi_systbl_phys = MD_FETCH(kmdp, MODINFOMD_FW_HANDLE, vm_paddr_t);

	return (kmdp);
}

static void
amd64_kdb_init(void)
{
	kdb_init();
#ifdef KDB
	if (boothowto & RB_KDB)
		kdb_enter(KDB_WHY_BOOTFLAGS, "Boot flags requested debugger");
#endif
}

/* Set up the fast syscall stuff */
void
amd64_conf_fast_syscall(void)
{
	uint64_t msr;

	msr = rdmsr(MSR_EFER) | EFER_SCE;
	wrmsr(MSR_EFER, msr);
	wrmsr(MSR_LSTAR, pti ? (u_int64_t)IDTVEC(fast_syscall_pti) :
	    (u_int64_t)IDTVEC(fast_syscall));
	wrmsr(MSR_CSTAR, (u_int64_t)IDTVEC(fast_syscall32));
	msr = ((u_int64_t)GSEL(GCODE_SEL, SEL_KPL) << 32) |
	    ((u_int64_t)GSEL(GUCODE32_SEL, SEL_UPL) << 48);
	wrmsr(MSR_STAR, msr);
	wrmsr(MSR_SF_MASK, PSL_NT | PSL_T | PSL_I | PSL_C | PSL_D | PSL_AC);
}

u_int64_t
hammer_time(u_int64_t modulep, u_int64_t physfree)
{
	caddr_t kmdp;
	int gsel_tss, x;
	struct pcpu *pc;
	struct nmi_pcpu *np;
	struct xstate_hdr *xhdr;
	u_int64_t rsp0;
	char *env;
	size_t kstack0_sz;
	int late_console;

	TSRAW(&thread0, TS_ENTER, __func__, NULL);

	kmdp = init_ops.parse_preload_data(modulep);

	physfree += ucode_load_bsp(physfree + KERNBASE);
	physfree = roundup2(physfree, PAGE_SIZE);

	identify_cpu1();
	identify_hypervisor();
	identify_cpu_fixup_bsp();
	identify_cpu2();
	initializecpucache();

	/*
	 * Check for pti, pcid, and invpcid before ifuncs are
	 * resolved, to correctly select the implementation for
	 * pmap_activate_sw_mode().
	 */
	pti = pti_get_default();
	TUNABLE_INT_FETCH("vm.pmap.pti", &pti);
	TUNABLE_INT_FETCH("vm.pmap.pcid_enabled", &pmap_pcid_enabled);
	if ((cpu_feature2 & CPUID2_PCID) != 0 && pmap_pcid_enabled) {
		invpcid_works = (cpu_stdext_feature &
		    CPUID_STDEXT_INVPCID) != 0;
	} else {
		pmap_pcid_enabled = 0;
	}

	link_elf_ireloc(kmdp);

	/*
	 * This may be done better later if it gets more high level
	 * components in it. If so just link td->td_proc here.
	 */
	proc_linkup0(&proc0, &thread0);

	/* Init basic tunables, hz etc */
	init_param1();

	thread0.td_kstack = physfree + KERNBASE;
	thread0.td_kstack_pages = kstack_pages;
	kstack0_sz = thread0.td_kstack_pages * PAGE_SIZE;
	bzero((void *)thread0.td_kstack, kstack0_sz);
	physfree += kstack0_sz;

	/*
	 * make gdt memory segments
	 */
	for (x = 0; x < NGDT; x++) {
		if (x != GPROC0_SEL && x != (GPROC0_SEL + 1) &&
		    x != GUSERLDT_SEL && x != (GUSERLDT_SEL) + 1)
			ssdtosd(&gdt_segs[x], &gdt[x]);
	}
	gdt_segs[GPROC0_SEL].ssd_base = (uintptr_t)&common_tss[0];
	ssdtosyssd(&gdt_segs[GPROC0_SEL],
	    (struct system_segment_descriptor *)&gdt[GPROC0_SEL]);

	r_gdt.rd_limit = NGDT * sizeof(gdt[0]) - 1;
	r_gdt.rd_base =  (long) gdt;
	lgdt(&r_gdt);
	pc = &__pcpu[0];

	wrmsr(MSR_FSBASE, 0);		/* User value */
	wrmsr(MSR_GSBASE, (u_int64_t)pc);
	wrmsr(MSR_KGSBASE, 0);		/* User value while in the kernel */

	pcpu_init(pc, 0, sizeof(struct pcpu));
	dpcpu_init((void *)(physfree + KERNBASE), 0);
	physfree += DPCPU_SIZE;
	PCPU_SET(prvspace, pc);
	PCPU_SET(curthread, &thread0);
	/* Non-late cninit() and printf() can be moved up to here. */
	PCPU_SET(tssp, &common_tss[0]);
	PCPU_SET(commontssp, &common_tss[0]);
	PCPU_SET(tss, (struct system_segment_descriptor *)&gdt[GPROC0_SEL]);
	PCPU_SET(ldt, (struct system_segment_descriptor *)&gdt[GUSERLDT_SEL]);
	PCPU_SET(fs32p, &gdt[GUFS32_SEL]);
	PCPU_SET(gs32p, &gdt[GUGS32_SEL]);

	/*
	 * Initialize mutexes.
	 *
	 * icu_lock: in order to allow an interrupt to occur in a critical
	 * 	     section, to set pcpu->ipending (etc...) properly, we
	 *	     must be able to get the icu lock, so it can't be
	 *	     under witness.
	 */
	mutex_init();
	mtx_init(&icu_lock, "icu", NULL, MTX_SPIN | MTX_NOWITNESS);
	mtx_init(&dt_lock, "descriptor tables", NULL, MTX_DEF);

	/* exceptions */
	for (x = 0; x < NIDT; x++)
		setidt(x, pti ? &IDTVEC(rsvd_pti) : &IDTVEC(rsvd), SDT_SYSIGT,
		    SEL_KPL, 0);
	setidt(IDT_DE, pti ? &IDTVEC(div_pti) : &IDTVEC(div), SDT_SYSIGT,
	    SEL_KPL, 0);
	setidt(IDT_DB, &IDTVEC(dbg), SDT_SYSIGT, SEL_KPL, 4);
	setidt(IDT_NMI, &IDTVEC(nmi),  SDT_SYSIGT, SEL_KPL, 2);
	setidt(IDT_BP, pti ? &IDTVEC(bpt_pti) : &IDTVEC(bpt), SDT_SYSIGT,
	    SEL_UPL, 0);
	setidt(IDT_OF, pti ? &IDTVEC(ofl_pti) : &IDTVEC(ofl), SDT_SYSIGT,
	    SEL_UPL, 0);
	setidt(IDT_BR, pti ? &IDTVEC(bnd_pti) : &IDTVEC(bnd), SDT_SYSIGT,
	    SEL_KPL, 0);
	setidt(IDT_UD, pti ? &IDTVEC(ill_pti) : &IDTVEC(ill), SDT_SYSIGT,
	    SEL_KPL, 0);
	setidt(IDT_NM, pti ? &IDTVEC(dna_pti) : &IDTVEC(dna), SDT_SYSIGT,
	    SEL_KPL, 0);
	setidt(IDT_DF, &IDTVEC(dblfault), SDT_SYSIGT, SEL_KPL, 1);
	setidt(IDT_FPUGP, pti ? &IDTVEC(fpusegm_pti) : &IDTVEC(fpusegm),
	    SDT_SYSIGT, SEL_KPL, 0);
	setidt(IDT_TS, pti ? &IDTVEC(tss_pti) : &IDTVEC(tss), SDT_SYSIGT,
	    SEL_KPL, 0);
	setidt(IDT_NP, pti ? &IDTVEC(missing_pti) : &IDTVEC(missing),
	    SDT_SYSIGT, SEL_KPL, 0);
	setidt(IDT_SS, pti ? &IDTVEC(stk_pti) : &IDTVEC(stk), SDT_SYSIGT,
	    SEL_KPL, 0);
	setidt(IDT_GP, pti ? &IDTVEC(prot_pti) : &IDTVEC(prot), SDT_SYSIGT,
	    SEL_KPL, 0);
	setidt(IDT_PF, pti ? &IDTVEC(page_pti) : &IDTVEC(page), SDT_SYSIGT,
	    SEL_KPL, 0);
	setidt(IDT_MF, pti ? &IDTVEC(fpu_pti) : &IDTVEC(fpu), SDT_SYSIGT,
	    SEL_KPL, 0);
	setidt(IDT_AC, pti ? &IDTVEC(align_pti) : &IDTVEC(align), SDT_SYSIGT,
	    SEL_KPL, 0);
	setidt(IDT_MC, &IDTVEC(mchk), SDT_SYSIGT, SEL_KPL, 3);
	setidt(IDT_XF, pti ? &IDTVEC(xmm_pti) : &IDTVEC(xmm), SDT_SYSIGT,
	    SEL_KPL, 0);
#ifdef KDTRACE_HOOKS
	setidt(IDT_DTRACE_RET, pti ? &IDTVEC(dtrace_ret_pti) :
	    &IDTVEC(dtrace_ret), SDT_SYSIGT, SEL_UPL, 0);
#endif
#ifdef XENHVM
	setidt(IDT_EVTCHN, pti ? &IDTVEC(xen_intr_upcall_pti) :
	    &IDTVEC(xen_intr_upcall), SDT_SYSIGT, SEL_KPL, 0);
#endif
	r_idt.rd_limit = sizeof(idt0) - 1;
	r_idt.rd_base = (long) idt;
	lidt(&r_idt);

	/*
	 * Initialize the clock before the console so that console
	 * initialization can use DELAY().
	 */
	clock_init();

	/*
	 * Use vt(4) by default for UEFI boot (during the sc(4)/vt(4)
	 * transition).
	 * Once bootblocks have updated, we can test directly for
	 * efi_systbl != NULL here...
	 */
	if (preload_search_info(kmdp, MODINFO_METADATA | MODINFOMD_EFI_MAP)
	    != NULL)
		vty_set_preferred(VTY_VT);

	TUNABLE_INT_FETCH("hw.ibrs_disable", &hw_ibrs_disable);
	TUNABLE_INT_FETCH("hw.spec_store_bypass_disable", &hw_ssb_disable);
	TUNABLE_INT_FETCH("machdep.syscall_ret_l1d_flush",
	    &syscall_ret_l1d_flush_mode);

	finishidentcpu();	/* Final stage of CPU initialization */
	initializecpu();	/* Initialize CPU registers */

	/* doublefault stack space, runs on ist1 */
	common_tss[0].tss_ist1 = (long)&dblfault_stack[sizeof(dblfault_stack)];

	/*
	 * NMI stack, runs on ist2.  The pcpu pointer is stored just
	 * above the start of the ist2 stack.
	 */
	np = ((struct nmi_pcpu *) &nmi0_stack[sizeof(nmi0_stack)]) - 1;
	np->np_pcpu = (register_t) pc;
	common_tss[0].tss_ist2 = (long) np;

	/*
	 * MC# stack, runs on ist3.  The pcpu pointer is stored just
	 * above the start of the ist3 stack.
	 */
	np = ((struct nmi_pcpu *) &mce0_stack[sizeof(mce0_stack)]) - 1;
	np->np_pcpu = (register_t) pc;
	common_tss[0].tss_ist3 = (long) np;

	/*
	 * DB# stack, runs on ist4.
	 */
	np = ((struct nmi_pcpu *) &dbg0_stack[sizeof(dbg0_stack)]) - 1;
	np->np_pcpu = (register_t) pc;
	common_tss[0].tss_ist4 = (long) np;
	
	/* Set the IO permission bitmap (empty due to tss seg limit) */
	common_tss[0].tss_iobase = sizeof(struct amd64tss) + IOPERM_BITMAP_SIZE;

	gsel_tss = GSEL(GPROC0_SEL, SEL_KPL);
	ltr(gsel_tss);

	amd64_conf_fast_syscall();

	/*
	 * Temporary forge some valid pointer to PCB, for exception
	 * handlers.  It is reinitialized properly below after FPU is
	 * set up.  Also set up td_critnest to short-cut the page
	 * fault handler.
	 */
	cpu_max_ext_state_size = sizeof(struct savefpu);
	thread0.td_pcb = get_pcb_td(&thread0);
	thread0.td_critnest = 1;

	/*
	 * The console and kdb should be initialized even earlier than here,
	 * but some console drivers don't work until after getmemsize().
	 * Default to late console initialization to support these drivers.
	 * This loses mainly printf()s in getmemsize() and early debugging.
	 */
	late_console = 1;
	TUNABLE_INT_FETCH("debug.late_console", &late_console);
	if (!late_console) {
		cninit();
		amd64_kdb_init();
	}

	getmemsize(kmdp, physfree);
	init_param2(physmem);

	/* now running on new page tables, configured,and u/iom is accessible */

#ifdef DEV_PCI
        /* This call might adjust phys_avail[]. */
        pci_early_quirks();
#endif

	if (late_console)
		cninit();

#ifdef DEV_ISA
#ifdef DEV_ATPIC
	elcr_probe();
	atpic_startup();
#else
	/* Reset and mask the atpics and leave them shut down. */
	atpic_reset();

	/*
	 * Point the ICU spurious interrupt vectors at the APIC spurious
	 * interrupt handler.
	 */
	setidt(IDT_IO_INTS + 7, IDTVEC(spuriousint), SDT_SYSIGT, SEL_KPL, 0);
	setidt(IDT_IO_INTS + 15, IDTVEC(spuriousint), SDT_SYSIGT, SEL_KPL, 0);
#endif
#else
#error "have you forgotten the isa device?";
#endif

	if (late_console)
		amd64_kdb_init();

	msgbufinit(msgbufp, msgbufsize);
	fpuinit();

	/*
	 * Set up thread0 pcb after fpuinit calculated pcb + fpu save
	 * area size.  Zero out the extended state header in fpu save
	 * area.
	 */
	thread0.td_pcb = get_pcb_td(&thread0);
	thread0.td_pcb->pcb_save = get_pcb_user_save_td(&thread0);
	bzero(get_pcb_user_save_td(&thread0), cpu_max_ext_state_size);
	if (use_xsave) {
		xhdr = (struct xstate_hdr *)(get_pcb_user_save_td(&thread0) +
		    1);
		xhdr->xstate_bv = xsave_mask;
	}
	/* make an initial tss so cpu can get interrupt stack on syscall! */
	rsp0 = (vm_offset_t)thread0.td_pcb;
	/* Ensure the stack is aligned to 16 bytes */
	rsp0 &= ~0xFul;
	common_tss[0].tss_rsp0 = rsp0;
	PCPU_SET(rsp0, rsp0);
	PCPU_SET(pti_rsp0, ((vm_offset_t)PCPU_PTR(pti_stack) +
	    PC_PTI_STACK_SZ * sizeof(uint64_t)) & ~0xful);
	PCPU_SET(curpcb, thread0.td_pcb);

	/* transfer to user mode */

	_ucodesel = GSEL(GUCODE_SEL, SEL_UPL);
	_udatasel = GSEL(GUDATA_SEL, SEL_UPL);
	_ucode32sel = GSEL(GUCODE32_SEL, SEL_UPL);
	_ufssel = GSEL(GUFS32_SEL, SEL_UPL);
	_ugssel = GSEL(GUGS32_SEL, SEL_UPL);

	load_ds(_udatasel);
	load_es(_udatasel);
	load_fs(_ufssel);

	/* setup proc 0's pcb */
	thread0.td_pcb->pcb_flags = 0;
	thread0.td_frame = &proc0_tf;

        env = kern_getenv("kernelname");
	if (env != NULL)
		strlcpy(kernelname, env, sizeof(kernelname));

	cpu_probe_amdc1e();

#ifdef FDT
	x86_init_fdt();
#endif
	thread0.td_critnest = 0;

	TSEXIT();

	/* Location of kernel stack for locore */
	return ((u_int64_t)thread0.td_pcb);
}

void
cpu_pcpu_init(struct pcpu *pcpu, int cpuid, size_t size)
{

	pcpu->pc_acpi_id = 0xffffffff;
}

static int
smap_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	struct bios_smap *smapbase;
	struct bios_smap_xattr smap;
	caddr_t kmdp;
	uint32_t *smapattr;
	int count, error, i;

	/* Retrieve the system memory map from the loader. */
	kmdp = preload_search_by_type("elf kernel");
	if (kmdp == NULL)
		kmdp = preload_search_by_type("elf64 kernel");
	smapbase = (struct bios_smap *)preload_search_info(kmdp,
	    MODINFO_METADATA | MODINFOMD_SMAP);
	if (smapbase == NULL)
		return (0);
	smapattr = (uint32_t *)preload_search_info(kmdp,
	    MODINFO_METADATA | MODINFOMD_SMAP_XATTR);
	count = *((uint32_t *)smapbase - 1) / sizeof(*smapbase);
	error = 0;
	for (i = 0; i < count; i++) {
		smap.base = smapbase[i].base;
		smap.length = smapbase[i].length;
		smap.type = smapbase[i].type;
		if (smapattr != NULL)
			smap.xattr = smapattr[i];
		else
			smap.xattr = 0;
		error = SYSCTL_OUT(req, &smap, sizeof(smap));
	}
	return (error);
}
SYSCTL_PROC(_machdep, OID_AUTO, smap, CTLTYPE_OPAQUE|CTLFLAG_RD, NULL, 0,
    smap_sysctl_handler, "S,bios_smap_xattr", "Raw BIOS SMAP data");

static int
efi_map_sysctl_handler(SYSCTL_HANDLER_ARGS)
{
	struct efi_map_header *efihdr;
	caddr_t kmdp;
	uint32_t efisize;

	kmdp = preload_search_by_type("elf kernel");
	if (kmdp == NULL)
		kmdp = preload_search_by_type("elf64 kernel");
	efihdr = (struct efi_map_header *)preload_search_info(kmdp,
	    MODINFO_METADATA | MODINFOMD_EFI_MAP);
	if (efihdr == NULL)
		return (0);
	efisize = *((uint32_t *)efihdr - 1);
	return (SYSCTL_OUT(req, efihdr, efisize));
}
SYSCTL_PROC(_machdep, OID_AUTO, efi_map, CTLTYPE_OPAQUE|CTLFLAG_RD, NULL, 0,
    efi_map_sysctl_handler, "S,efi_map_header", "Raw EFI Memory Map");

void
spinlock_enter(void)
{
	struct thread *td;
	register_t flags;

	td = curthread;
	if (td->td_md.md_spinlock_count == 0) {
		flags = intr_disable();
		td->td_md.md_spinlock_count = 1;
		td->td_md.md_saved_flags = flags;
		critical_enter();
	} else
		td->td_md.md_spinlock_count++;
}

void
spinlock_exit(void)
{
	struct thread *td;
	register_t flags;

	td = curthread;
	flags = td->td_md.md_saved_flags;
	td->td_md.md_spinlock_count--;
	if (td->td_md.md_spinlock_count == 0) {
		critical_exit();
		intr_restore(flags);
	}
}

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

	pcb->pcb_r12 = tf->tf_r12;
	pcb->pcb_r13 = tf->tf_r13;
	pcb->pcb_r14 = tf->tf_r14;
	pcb->pcb_r15 = tf->tf_r15;
	pcb->pcb_rbp = tf->tf_rbp;
	pcb->pcb_rbx = tf->tf_rbx;
	pcb->pcb_rip = tf->tf_rip;
	pcb->pcb_rsp = tf->tf_rsp;
}

int
ptrace_set_pc(struct thread *td, unsigned long addr)
{

	td->td_frame->tf_rip = addr;
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	return (0);
}

int
ptrace_single_step(struct thread *td)
{

	PROC_LOCK_ASSERT(td->td_proc, MA_OWNED);
	if ((td->td_frame->tf_rflags & PSL_T) == 0) {
		td->td_frame->tf_rflags |= PSL_T;
		td->td_dbgflags |= TDB_STEP;
	}
	return (0);
}

int
ptrace_clear_single_step(struct thread *td)
{

	PROC_LOCK_ASSERT(td->td_proc, MA_OWNED);
	td->td_frame->tf_rflags &= ~PSL_T;
	td->td_dbgflags &= ~TDB_STEP;
	return (0);
}

int
fill_regs(struct thread *td, struct reg *regs)
{
	struct trapframe *tp;

	tp = td->td_frame;
	return (fill_frame_regs(tp, regs));
}

int
fill_frame_regs(struct trapframe *tp, struct reg *regs)
{

	regs->r_r15 = tp->tf_r15;
	regs->r_r14 = tp->tf_r14;
	regs->r_r13 = tp->tf_r13;
	regs->r_r12 = tp->tf_r12;
	regs->r_r11 = tp->tf_r11;
	regs->r_r10 = tp->tf_r10;
	regs->r_r9  = tp->tf_r9;
	regs->r_r8  = tp->tf_r8;
	regs->r_rdi = tp->tf_rdi;
	regs->r_rsi = tp->tf_rsi;
	regs->r_rbp = tp->tf_rbp;
	regs->r_rbx = tp->tf_rbx;
	regs->r_rdx = tp->tf_rdx;
	regs->r_rcx = tp->tf_rcx;
	regs->r_rax = tp->tf_rax;
	regs->r_rip = tp->tf_rip;
	regs->r_cs = tp->tf_cs;
	regs->r_rflags = tp->tf_rflags;
	regs->r_rsp = tp->tf_rsp;
	regs->r_ss = tp->tf_ss;
	if (tp->tf_flags & TF_HASSEGS) {
		regs->r_ds = tp->tf_ds;
		regs->r_es = tp->tf_es;
		regs->r_fs = tp->tf_fs;
		regs->r_gs = tp->tf_gs;
	} else {
		regs->r_ds = 0;
		regs->r_es = 0;
		regs->r_fs = 0;
		regs->r_gs = 0;
	}
	regs->r_err = 0;
	regs->r_trapno = 0;
	return (0);
}

int
set_regs(struct thread *td, struct reg *regs)
{
	struct trapframe *tp;
	register_t rflags;

	tp = td->td_frame;
	rflags = regs->r_rflags & 0xffffffff;
	if (!EFL_SECURE(rflags, tp->tf_rflags) || !CS_SECURE(regs->r_cs))
		return (EINVAL);
	tp->tf_r15 = regs->r_r15;
	tp->tf_r14 = regs->r_r14;
	tp->tf_r13 = regs->r_r13;
	tp->tf_r12 = regs->r_r12;
	tp->tf_r11 = regs->r_r11;
	tp->tf_r10 = regs->r_r10;
	tp->tf_r9  = regs->r_r9;
	tp->tf_r8  = regs->r_r8;
	tp->tf_rdi = regs->r_rdi;
	tp->tf_rsi = regs->r_rsi;
	tp->tf_rbp = regs->r_rbp;
	tp->tf_rbx = regs->r_rbx;
	tp->tf_rdx = regs->r_rdx;
	tp->tf_rcx = regs->r_rcx;
	tp->tf_rax = regs->r_rax;
	tp->tf_rip = regs->r_rip;
	tp->tf_cs = regs->r_cs;
	tp->tf_rflags = rflags;
	tp->tf_rsp = regs->r_rsp;
	tp->tf_ss = regs->r_ss;
	if (0) {	/* XXXKIB */
		tp->tf_ds = regs->r_ds;
		tp->tf_es = regs->r_es;
		tp->tf_fs = regs->r_fs;
		tp->tf_gs = regs->r_gs;
		tp->tf_flags = TF_HASSEGS;
	}
	set_pcb_flags(td->td_pcb, PCB_FULL_IRET);
	return (0);
}

/* XXX check all this stuff! */
/* externalize from sv_xmm */
static void
fill_fpregs_xmm(struct savefpu *sv_xmm, struct fpreg *fpregs)
{
	struct envxmm *penv_fpreg = (struct envxmm *)&fpregs->fpr_env;
	struct envxmm *penv_xmm = &sv_xmm->sv_env;
	int i;

	/* pcb -> fpregs */
	bzero(fpregs, sizeof(*fpregs));

	/* FPU control/status */
	penv_fpreg->en_cw = penv_xmm->en_cw;
	penv_fpreg->en_sw = penv_xmm->en_sw;
	penv_fpreg->en_tw = penv_xmm->en_tw;
	penv_fpreg->en_opcode = penv_xmm->en_opcode;
	penv_fpreg->en_rip = penv_xmm->en_rip;
	penv_fpreg->en_rdp = penv_xmm->en_rdp;
	penv_fpreg->en_mxcsr = penv_xmm->en_mxcsr;
	penv_fpreg->en_mxcsr_mask = penv_xmm->en_mxcsr_mask;

	/* FPU registers */
	for (i = 0; i < 8; ++i)
		bcopy(sv_xmm->sv_fp[i].fp_acc.fp_bytes, fpregs->fpr_acc[i], 10);

	/* SSE registers */
	for (i = 0; i < 16; ++i)
		bcopy(sv_xmm->sv_xmm[i].xmm_bytes, fpregs->fpr_xacc[i], 16);
}

/* internalize from fpregs into sv_xmm */
static void
set_fpregs_xmm(struct fpreg *fpregs, struct savefpu *sv_xmm)
{
	struct envxmm *penv_xmm = &sv_xmm->sv_env;
	struct envxmm *penv_fpreg = (struct envxmm *)&fpregs->fpr_env;
	int i;

	/* fpregs -> pcb */
	/* FPU control/status */
	penv_xmm->en_cw = penv_fpreg->en_cw;
	penv_xmm->en_sw = penv_fpreg->en_sw;
	penv_xmm->en_tw = penv_fpreg->en_tw;
	penv_xmm->en_opcode = penv_fpreg->en_opcode;
	penv_xmm->en_rip = penv_fpreg->en_rip;
	penv_xmm->en_rdp = penv_fpreg->en_rdp;
	penv_xmm->en_mxcsr = penv_fpreg->en_mxcsr;
	penv_xmm->en_mxcsr_mask = penv_fpreg->en_mxcsr_mask & cpu_mxcsr_mask;

	/* FPU registers */
	for (i = 0; i < 8; ++i)
		bcopy(fpregs->fpr_acc[i], sv_xmm->sv_fp[i].fp_acc.fp_bytes, 10);

	/* SSE registers */
	for (i = 0; i < 16; ++i)
		bcopy(fpregs->fpr_xacc[i], sv_xmm->sv_xmm[i].xmm_bytes, 16);
}

/* externalize from td->pcb */
int
fill_fpregs(struct thread *td, struct fpreg *fpregs)
{

	KASSERT(td == curthread || TD_IS_SUSPENDED(td) ||
	    P_SHOULDSTOP(td->td_proc),
	    ("not suspended thread %p", td));
	fpugetregs(td);
	fill_fpregs_xmm(get_pcb_user_save_td(td), fpregs);
	return (0);
}

/* internalize to td->pcb */
int
set_fpregs(struct thread *td, struct fpreg *fpregs)
{

	critical_enter();
	set_fpregs_xmm(fpregs, get_pcb_user_save_td(td));
	fpuuserinited(td);
	critical_exit();
	return (0);
}

/*
 * Get machine context.
 */
int
get_mcontext(struct thread *td, mcontext_t *mcp, int flags)
{
	struct pcb *pcb;
	struct trapframe *tp;

	pcb = td->td_pcb;
	tp = td->td_frame;
	PROC_LOCK(curthread->td_proc);
	mcp->mc_onstack = sigonstack(tp->tf_rsp);
	PROC_UNLOCK(curthread->td_proc);
	mcp->mc_r15 = tp->tf_r15;
	mcp->mc_r14 = tp->tf_r14;
	mcp->mc_r13 = tp->tf_r13;
	mcp->mc_r12 = tp->tf_r12;
	mcp->mc_r11 = tp->tf_r11;
	mcp->mc_r10 = tp->tf_r10;
	mcp->mc_r9  = tp->tf_r9;
	mcp->mc_r8  = tp->tf_r8;
	mcp->mc_rdi = tp->tf_rdi;
	mcp->mc_rsi = tp->tf_rsi;
	mcp->mc_rbp = tp->tf_rbp;
	mcp->mc_rbx = tp->tf_rbx;
	mcp->mc_rcx = tp->tf_rcx;
	mcp->mc_rflags = tp->tf_rflags;
	if (flags & GET_MC_CLEAR_RET) {
		mcp->mc_rax = 0;
		mcp->mc_rdx = 0;
		mcp->mc_rflags &= ~PSL_C;
	} else {
		mcp->mc_rax = tp->tf_rax;
		mcp->mc_rdx = tp->tf_rdx;
	}
	mcp->mc_rip = tp->tf_rip;
	mcp->mc_cs = tp->tf_cs;
	mcp->mc_rsp = tp->tf_rsp;
	mcp->mc_ss = tp->tf_ss;
	mcp->mc_ds = tp->tf_ds;
	mcp->mc_es = tp->tf_es;
	mcp->mc_fs = tp->tf_fs;
	mcp->mc_gs = tp->tf_gs;
	mcp->mc_flags = tp->tf_flags;
	mcp->mc_len = sizeof(*mcp);
	get_fpcontext(td, mcp, NULL, 0);
	update_pcb_bases(pcb);
	mcp->mc_fsbase = pcb->pcb_fsbase;
	mcp->mc_gsbase = pcb->pcb_gsbase;
	mcp->mc_xfpustate = 0;
	mcp->mc_xfpustate_len = 0;
	bzero(mcp->mc_spare, sizeof(mcp->mc_spare));
	return (0);
}

/*
 * Set machine context.
 *
 * However, we don't set any but the user modifiable flags, and we won't
 * touch the cs selector.
 */
int
set_mcontext(struct thread *td, mcontext_t *mcp)
{
	struct pcb *pcb;
	struct trapframe *tp;
	char *xfpustate;
	long rflags;
	int ret;

	pcb = td->td_pcb;
	tp = td->td_frame;
	if (mcp->mc_len != sizeof(*mcp) ||
	    (mcp->mc_flags & ~_MC_FLAG_MASK) != 0)
		return (EINVAL);
	rflags = (mcp->mc_rflags & PSL_USERCHANGE) |
	    (tp->tf_rflags & ~PSL_USERCHANGE);
	if (mcp->mc_flags & _MC_HASFPXSTATE) {
		if (mcp->mc_xfpustate_len > cpu_max_ext_state_size -
		    sizeof(struct savefpu))
			return (EINVAL);
		xfpustate = __builtin_alloca(mcp->mc_xfpustate_len);
		ret = copyin((void *)mcp->mc_xfpustate, xfpustate,
		    mcp->mc_xfpustate_len);
		if (ret != 0)
			return (ret);
	} else
		xfpustate = NULL;
	ret = set_fpcontext(td, mcp, xfpustate, mcp->mc_xfpustate_len);
	if (ret != 0)
		return (ret);
	tp->tf_r15 = mcp->mc_r15;
	tp->tf_r14 = mcp->mc_r14;
	tp->tf_r13 = mcp->mc_r13;
	tp->tf_r12 = mcp->mc_r12;
	tp->tf_r11 = mcp->mc_r11;
	tp->tf_r10 = mcp->mc_r10;
	tp->tf_r9  = mcp->mc_r9;
	tp->tf_r8  = mcp->mc_r8;
	tp->tf_rdi = mcp->mc_rdi;
	tp->tf_rsi = mcp->mc_rsi;
	tp->tf_rbp = mcp->mc_rbp;
	tp->tf_rbx = mcp->mc_rbx;
	tp->tf_rdx = mcp->mc_rdx;
	tp->tf_rcx = mcp->mc_rcx;
	tp->tf_rax = mcp->mc_rax;
	tp->tf_rip = mcp->mc_rip;
	tp->tf_rflags = rflags;
	tp->tf_rsp = mcp->mc_rsp;
	tp->tf_ss = mcp->mc_ss;
	tp->tf_flags = mcp->mc_flags;
	if (tp->tf_flags & TF_HASSEGS) {
		tp->tf_ds = mcp->mc_ds;
		tp->tf_es = mcp->mc_es;
		tp->tf_fs = mcp->mc_fs;
		tp->tf_gs = mcp->mc_gs;
	}
	set_pcb_flags(pcb, PCB_FULL_IRET);
	if (mcp->mc_flags & _MC_HASBASES) {
		pcb->pcb_fsbase = mcp->mc_fsbase;
		pcb->pcb_gsbase = mcp->mc_gsbase;
	}
	return (0);
}

static void
get_fpcontext(struct thread *td, mcontext_t *mcp, char *xfpusave,
    size_t xfpusave_len)
{
	size_t max_len, len;

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
	mcp->mc_flags |= _MC_HASFPXSTATE;
	mcp->mc_xfpustate_len = len;
	bcopy(get_pcb_user_save_td(td) + 1, xfpusave, len);
}

static int
set_fpcontext(struct thread *td, mcontext_t *mcp, char *xfpustate,
    size_t xfpustate_len)
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

void
fpstate_drop(struct thread *td)
{

	KASSERT(PCB_USER_FPU(td->td_pcb), ("fpstate_drop: kernel-owned fpu"));
	critical_enter();
	if (PCPU_GET(fpcurthread) == td)
		fpudrop();
	/*
	 * XXX force a full drop of the fpu.  The above only drops it if we
	 * owned it.
	 *
	 * XXX I don't much like fpugetuserregs()'s semantics of doing a full
	 * drop.  Dropping only to the pcb matches fnsave's behaviour.
	 * We only need to drop to !PCB_INITDONE in sendsig().  But
	 * sendsig() is the only caller of fpugetuserregs()... perhaps we just
	 * have too many layers.
	 */
	clear_pcb_flags(curthread->td_pcb,
	    PCB_FPUINITDONE | PCB_USERFPUINITDONE);
	critical_exit();
}

int
fill_dbregs(struct thread *td, struct dbreg *dbregs)
{
	struct pcb *pcb;

	if (td == NULL) {
		dbregs->dr[0] = rdr0();
		dbregs->dr[1] = rdr1();
		dbregs->dr[2] = rdr2();
		dbregs->dr[3] = rdr3();
		dbregs->dr[6] = rdr6();
		dbregs->dr[7] = rdr7();
	} else {
		pcb = td->td_pcb;
		dbregs->dr[0] = pcb->pcb_dr0;
		dbregs->dr[1] = pcb->pcb_dr1;
		dbregs->dr[2] = pcb->pcb_dr2;
		dbregs->dr[3] = pcb->pcb_dr3;
		dbregs->dr[6] = pcb->pcb_dr6;
		dbregs->dr[7] = pcb->pcb_dr7;
	}
	dbregs->dr[4] = 0;
	dbregs->dr[5] = 0;
	dbregs->dr[8] = 0;
	dbregs->dr[9] = 0;
	dbregs->dr[10] = 0;
	dbregs->dr[11] = 0;
	dbregs->dr[12] = 0;
	dbregs->dr[13] = 0;
	dbregs->dr[14] = 0;
	dbregs->dr[15] = 0;
	return (0);
}

int
set_dbregs(struct thread *td, struct dbreg *dbregs)
{
	struct pcb *pcb;
	int i;

	if (td == NULL) {
		load_dr0(dbregs->dr[0]);
		load_dr1(dbregs->dr[1]);
		load_dr2(dbregs->dr[2]);
		load_dr3(dbregs->dr[3]);
		load_dr6(dbregs->dr[6]);
		load_dr7(dbregs->dr[7]);
	} else {
		/*
		 * Don't let an illegal value for dr7 get set.  Specifically,
		 * check for undefined settings.  Setting these bit patterns
		 * result in undefined behaviour and can lead to an unexpected
		 * TRCTRAP or a general protection fault right here.
		 * Upper bits of dr6 and dr7 must not be set
		 */
		for (i = 0; i < 4; i++) {
			if (DBREG_DR7_ACCESS(dbregs->dr[7], i) == 0x02)
				return (EINVAL);
			if (td->td_frame->tf_cs == _ucode32sel &&
			    DBREG_DR7_LEN(dbregs->dr[7], i) == DBREG_DR7_LEN_8)
				return (EINVAL);
		}
		if ((dbregs->dr[6] & 0xffffffff00000000ul) != 0 ||
		    (dbregs->dr[7] & 0xffffffff00000000ul) != 0)
			return (EINVAL);

		pcb = td->td_pcb;

		/*
		 * Don't let a process set a breakpoint that is not within the
		 * process's address space.  If a process could do this, it
		 * could halt the system by setting a breakpoint in the kernel
		 * (if ddb was enabled).  Thus, we need to check to make sure
		 * that no breakpoints are being enabled for addresses outside
		 * process's address space.
		 *
		 * XXX - what about when the watched area of the user's
		 * address space is written into from within the kernel
		 * ... wouldn't that still cause a breakpoint to be generated
		 * from within kernel mode?
		 */

		if (DBREG_DR7_ENABLED(dbregs->dr[7], 0)) {
			/* dr0 is enabled */
			if (dbregs->dr[0] >= VM_MAXUSER_ADDRESS)
				return (EINVAL);
		}
		if (DBREG_DR7_ENABLED(dbregs->dr[7], 1)) {
			/* dr1 is enabled */
			if (dbregs->dr[1] >= VM_MAXUSER_ADDRESS)
				return (EINVAL);
		}
		if (DBREG_DR7_ENABLED(dbregs->dr[7], 2)) {
			/* dr2 is enabled */
			if (dbregs->dr[2] >= VM_MAXUSER_ADDRESS)
				return (EINVAL);
		}
		if (DBREG_DR7_ENABLED(dbregs->dr[7], 3)) {
			/* dr3 is enabled */
			if (dbregs->dr[3] >= VM_MAXUSER_ADDRESS)
				return (EINVAL);
		}

		pcb->pcb_dr0 = dbregs->dr[0];
		pcb->pcb_dr1 = dbregs->dr[1];
		pcb->pcb_dr2 = dbregs->dr[2];
		pcb->pcb_dr3 = dbregs->dr[3];
		pcb->pcb_dr6 = dbregs->dr[6];
		pcb->pcb_dr7 = dbregs->dr[7];

		set_pcb_flags(pcb, PCB_DBREGS);
	}

	return (0);
}

void
reset_dbregs(void)
{

	load_dr7(0);	/* Turn off the control bits first */
	load_dr0(0);
	load_dr1(0);
	load_dr2(0);
	load_dr3(0);
	load_dr6(0);
}

/*
 * Return > 0 if a hardware breakpoint has been hit, and the
 * breakpoint was in user space.  Return 0, otherwise.
 */
int
user_dbreg_trap(register_t dr6)
{
        u_int64_t dr7;
        u_int64_t bp;       /* breakpoint bits extracted from dr6 */
        int nbp;            /* number of breakpoints that triggered */
        caddr_t addr[4];    /* breakpoint addresses */
        int i;

        bp = dr6 & DBREG_DR6_BMASK;
        if (bp == 0) {
                /*
                 * None of the breakpoint bits are set meaning this
                 * trap was not caused by any of the debug registers
                 */
                return 0;
        }

        dr7 = rdr7();
        if ((dr7 & 0x000000ff) == 0) {
                /*
                 * all GE and LE bits in the dr7 register are zero,
                 * thus the trap couldn't have been caused by the
                 * hardware debug registers
                 */
                return 0;
        }

        nbp = 0;

        /*
         * at least one of the breakpoints were hit, check to see
         * which ones and if any of them are user space addresses
         */

        if (bp & 0x01) {
                addr[nbp++] = (caddr_t)rdr0();
        }
        if (bp & 0x02) {
                addr[nbp++] = (caddr_t)rdr1();
        }
        if (bp & 0x04) {
                addr[nbp++] = (caddr_t)rdr2();
        }
        if (bp & 0x08) {
                addr[nbp++] = (caddr_t)rdr3();
        }

        for (i = 0; i < nbp; i++) {
                if (addr[i] < (caddr_t)VM_MAXUSER_ADDRESS) {
                        /*
                         * addr[i] is in user space
                         */
                        return nbp;
                }
        }

        /*
         * None of the breakpoints are in user space.
         */
        return 0;
}

/*
 * The pcb_flags is only modified by current thread, or by other threads
 * when current thread is stopped.  However, current thread may change it
 * from the interrupt context in cpu_switch(), or in the trap handler.
 * When we read-modify-write pcb_flags from C sources, compiler may generate
 * code that is not atomic regarding the interrupt handler.  If a trap or
 * interrupt happens and any flag is modified from the handler, it can be
 * clobbered with the cached value later.  Therefore, we implement setting
 * and clearing flags with single-instruction functions, which do not race
 * with possible modification of the flags from the trap or interrupt context,
 * because traps and interrupts are executed only on instruction boundary.
 */
void
set_pcb_flags_raw(struct pcb *pcb, const u_int flags)
{

	__asm __volatile("orl %1,%0"
	    : "=m" (pcb->pcb_flags) : "ir" (flags), "m" (pcb->pcb_flags)
	    : "cc", "memory");

}

/*
 * The support for RDFSBASE, WRFSBASE and similar instructions for %gs
 * base requires that kernel saves MSR_FSBASE and MSR_{K,}GSBASE into
 * pcb if user space modified the bases.  We must save on the context
 * switch or if the return to usermode happens through the doreti.
 *
 * Tracking of both events is performed by the pcb flag PCB_FULL_IRET,
 * which have a consequence that the base MSRs must be saved each time
 * the PCB_FULL_IRET flag is set.  We disable interrupts to sync with
 * context switches.
 */
static void
set_pcb_flags_fsgsbase(struct pcb *pcb, const u_int flags)
{
	register_t r;

	if (curpcb == pcb &&
	    (flags & PCB_FULL_IRET) != 0 &&
	    (pcb->pcb_flags & PCB_FULL_IRET) == 0) {
		r = intr_disable();
		if ((pcb->pcb_flags & PCB_FULL_IRET) == 0) {
			if (rfs() == _ufssel)
				pcb->pcb_fsbase = rdfsbase();
			if (rgs() == _ugssel)
				pcb->pcb_gsbase = rdmsr(MSR_KGSBASE);
		}
		set_pcb_flags_raw(pcb, flags);
		intr_restore(r);
	} else {
		set_pcb_flags_raw(pcb, flags);
	}
}

DEFINE_IFUNC(, void, set_pcb_flags, (struct pcb *, const u_int), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_FSGSBASE) != 0 ?
	    set_pcb_flags_fsgsbase : set_pcb_flags_raw);
}

void
clear_pcb_flags(struct pcb *pcb, const u_int flags)
{

	__asm __volatile("andl %1,%0"
	    : "=m" (pcb->pcb_flags) : "ir" (~flags), "m" (pcb->pcb_flags)
	    : "cc", "memory");
}

#ifdef KDB

/*
 * Provide inb() and outb() as functions.  They are normally only available as
 * inline functions, thus cannot be called from the debugger.
 */

/* silence compiler warnings */
u_char inb_(u_short);
void outb_(u_short, u_char);

u_char
inb_(u_short port)
{
	return inb(port);
}

void
outb_(u_short port, u_char data)
{
	outb(port, data);
}

#endif /* KDB */

#undef memset
#undef memmove
#undef memcpy

void	*memset_std(void *buf, int c, size_t len);
void	*memset_erms(void *buf, int c, size_t len);
DEFINE_IFUNC(, void *, memset, (void *, int, size_t), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_ERMS) != 0 ?
	    memset_erms : memset_std);
}

void    *memmove_std(void * _Nonnull dst, const void * _Nonnull src,
	    size_t len);
void    *memmove_erms(void * _Nonnull dst, const void * _Nonnull src,
	    size_t len);
DEFINE_IFUNC(, void *, memmove, (void * _Nonnull, const void * _Nonnull,
    size_t), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_ERMS) != 0 ?
	    memmove_erms : memmove_std);
}

void    *memcpy_std(void * _Nonnull dst, const void * _Nonnull src,
	    size_t len);
void    *memcpy_erms(void * _Nonnull dst, const void * _Nonnull src,
	    size_t len);
DEFINE_IFUNC(, void *, memcpy, (void * _Nonnull, const void * _Nonnull,size_t),
    static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_ERMS) != 0 ?
	    memcpy_erms : memcpy_std);
}

void	pagezero_std(void *addr);
void	pagezero_erms(void *addr);
DEFINE_IFUNC(, void , pagezero, (void *), static)
{

	return ((cpu_stdext_feature & CPUID_STDEXT_ERMS) != 0 ?
	    pagezero_erms : pagezero_std);
}
