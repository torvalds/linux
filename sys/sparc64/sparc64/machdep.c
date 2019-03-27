/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001 Jake Burkholder.
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
 *
 *	from: @(#)machdep.c	7.4 (Berkeley) 6/3/91
 *	from: FreeBSD: src/sys/i386/i386/machdep.c,v 1.477 2001/08/27
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_ddb.h"
#include "opt_kstack_pages.h"

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/bio.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/cons.h>
#include <sys/eventhandler.h>
#include <sys/exec.h>
#include <sys/imgact.h>
#include <sys/interrupt.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/linker.h>
#include <sys/lock.h>
#include <sys/msgbuf.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/ptrace.h>
#include <sys/reboot.h>
#include <sys/rwlock.h>
#include <sys/signalvar.h>
#include <sys/smp.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/timetc.h>
#include <sys/ucontext.h>
#include <sys/vmmeter.h>

#include <dev/ofw/openfirm.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_pager.h>
#include <vm/vm_param.h>

#include <ddb/ddb.h>

#include <machine/bus.h>
#include <machine/cache.h>
#include <machine/cmt.h>
#include <machine/cpu.h>
#include <machine/fireplane.h>
#include <machine/fp.h>
#include <machine/fsr.h>
#include <machine/intr_machdep.h>
#include <machine/jbus.h>
#include <machine/md_var.h>
#include <machine/metadata.h>
#include <machine/ofw_machdep.h>
#include <machine/ofw_mem.h>
#include <machine/pcb.h>
#include <machine/pmap.h>
#include <machine/pstate.h>
#include <machine/reg.h>
#include <machine/sigframe.h>
#include <machine/smp.h>
#include <machine/tick.h>
#include <machine/tlb.h>
#include <machine/tstate.h>
#include <machine/upa.h>
#include <machine/ver.h>

typedef int ofw_vec_t(void *);

int dtlb_slots;
int itlb_slots;
struct tlb_entry *kernel_tlbs;
int kernel_tlb_slots;

int cold = 1;
long Maxmem;
long realmem;

void *dpcpu0;
char pcpu0[PCPU_PAGES * PAGE_SIZE];
struct pcpu dummy_pcpu[MAXCPU];
struct trapframe frame0;

vm_offset_t kstack0;
vm_paddr_t kstack0_phys;

struct kva_md_info kmi;

u_long ofw_vec;
u_long ofw_tba;
u_int tba_taken_over;

char sparc64_model[32];

static int cpu_use_vis = 1;

cpu_block_copy_t *cpu_block_copy;
cpu_block_zero_t *cpu_block_zero;

static phandle_t find_bsp(phandle_t node, uint32_t bspid, u_int cpu_impl);
void sparc64_init(caddr_t mdp, u_long o1, u_long o2, u_long o3,
    ofw_vec_t *vec);
static void sparc64_shutdown_final(void *dummy, int howto);

static void cpu_startup(void *arg);
SYSINIT(cpu, SI_SUB_CPU, SI_ORDER_FIRST, cpu_startup, NULL);

CTASSERT((1 << INT_SHIFT) == sizeof(int));
CTASSERT((1 << PTR_SHIFT) == sizeof(char *));

CTASSERT(sizeof(struct reg) == 256);
CTASSERT(sizeof(struct fpreg) == 272);
CTASSERT(sizeof(struct __mcontext) == 512);

CTASSERT((sizeof(struct pcb) & (64 - 1)) == 0);
CTASSERT((offsetof(struct pcb, pcb_kfp) & (64 - 1)) == 0);
CTASSERT((offsetof(struct pcb, pcb_ufp) & (64 - 1)) == 0);
CTASSERT(sizeof(struct pcb) <= ((KSTACK_PAGES * PAGE_SIZE) / 8));

CTASSERT(sizeof(struct pcpu) <= ((PCPU_PAGES * PAGE_SIZE) / 2));

static void
cpu_startup(void *arg)
{
	vm_paddr_t physsz;
	int i;

	physsz = 0;
	for (i = 0; i < sparc64_nmemreg; i++)
		physsz += sparc64_memreg[i].mr_size;
	printf("real memory  = %lu (%lu MB)\n", physsz,
	    physsz / (1024 * 1024));
	realmem = (long)physsz / PAGE_SIZE;

	vm_ksubmap_init(&kmi);

	bufinit();
	vm_pager_bufferinit();

	EVENTHANDLER_REGISTER(shutdown_final, sparc64_shutdown_final, NULL,
	    SHUTDOWN_PRI_LAST);

	printf("avail memory = %lu (%lu MB)\n", vm_free_count() * PAGE_SIZE,
	    vm_free_count() / ((1024 * 1024) / PAGE_SIZE));

	if (bootverbose)
		printf("machine: %s\n", sparc64_model);

	cpu_identify(rdpr(ver), PCPU_GET(clock), curcpu);
}

void
cpu_pcpu_init(struct pcpu *pcpu, int cpuid, size_t size)
{
	struct intr_request *ir;
	int i;

	pcpu->pc_irtail = &pcpu->pc_irhead;
	for (i = 0; i < IR_FREE; i++) {
		ir = &pcpu->pc_irpool[i];
		ir->ir_next = pcpu->pc_irfree;
		pcpu->pc_irfree = ir;
	}
}

void
spinlock_enter(void)
{
	struct thread *td;
	register_t pil;

	td = curthread;
	if (td->td_md.md_spinlock_count == 0) {
		pil = rdpr(pil);
		wrpr(pil, 0, PIL_TICK);
		td->td_md.md_spinlock_count = 1;
		td->td_md.md_saved_pil = pil;
	} else
		td->td_md.md_spinlock_count++;
	critical_enter();
}

void
spinlock_exit(void)
{
	struct thread *td;
	register_t pil;

	td = curthread;
	critical_exit();
	pil = td->td_md.md_saved_pil;
	td->td_md.md_spinlock_count--;
	if (td->td_md.md_spinlock_count == 0)
		wrpr(pil, pil, 0);
}

static phandle_t
find_bsp(phandle_t node, uint32_t bspid, u_int cpu_impl)
{
	char type[sizeof("cpu")];
	phandle_t child;
	uint32_t portid;

	for (; node != 0; node = OF_peer(node)) {
		child = OF_child(node);
		if (child > 0) {
			child = find_bsp(child, bspid, cpu_impl);
			if (child > 0)
				return (child);
		} else {
			if (OF_getprop(node, "device_type", type,
			    sizeof(type)) <= 0)
				continue;
			if (strcmp(type, "cpu") != 0)
				continue;
			if (OF_getprop(node, cpu_portid_prop(cpu_impl),
			    &portid, sizeof(portid)) <= 0)
				continue;
			if (portid == bspid)
				return (node);
		}
	}
	return (0);
}

const char *
cpu_portid_prop(u_int cpu_impl)
{

	switch (cpu_impl) {
	case CPU_IMPL_SPARC64:
	case CPU_IMPL_SPARC64V:
	case CPU_IMPL_ULTRASPARCI:
	case CPU_IMPL_ULTRASPARCII:
	case CPU_IMPL_ULTRASPARCIIi:
	case CPU_IMPL_ULTRASPARCIIe:
		return ("upa-portid");
	case CPU_IMPL_ULTRASPARCIII:
	case CPU_IMPL_ULTRASPARCIIIp:
	case CPU_IMPL_ULTRASPARCIIIi:
	case CPU_IMPL_ULTRASPARCIIIip:
		return ("portid");
	case CPU_IMPL_ULTRASPARCIV:
	case CPU_IMPL_ULTRASPARCIVp:
		return ("cpuid");
	default:
		return ("");
	}
}

uint32_t
cpu_get_mid(u_int cpu_impl)
{

	switch (cpu_impl) {
	case CPU_IMPL_SPARC64:
	case CPU_IMPL_SPARC64V:
	case CPU_IMPL_ULTRASPARCI:
	case CPU_IMPL_ULTRASPARCII:
	case CPU_IMPL_ULTRASPARCIIi:
	case CPU_IMPL_ULTRASPARCIIe:
		return (UPA_CR_GET_MID(ldxa(0, ASI_UPA_CONFIG_REG)));
	case CPU_IMPL_ULTRASPARCIII:
	case CPU_IMPL_ULTRASPARCIIIp:
		return (FIREPLANE_CR_GET_AID(ldxa(AA_FIREPLANE_CONFIG,
		    ASI_FIREPLANE_CONFIG_REG)));
	case CPU_IMPL_ULTRASPARCIIIi:
	case CPU_IMPL_ULTRASPARCIIIip:
		return (JBUS_CR_GET_JID(ldxa(0, ASI_JBUS_CONFIG_REG)));
	case CPU_IMPL_ULTRASPARCIV:
	case CPU_IMPL_ULTRASPARCIVp:
		return (INTR_ID_GET_ID(ldxa(AA_INTR_ID, ASI_INTR_ID)));
	default:
		return (0);
	}
}

void
sparc64_init(caddr_t mdp, u_long o1, u_long o2, u_long o3, ofw_vec_t *vec)
{
	char *env;
	struct pcpu *pc;
	vm_offset_t end;
	vm_offset_t va;
	caddr_t kmdp;
	phandle_t root;
	u_int cpu_impl;

	end = 0;
	kmdp = NULL;

	/*
	 * Find out what kind of CPU we have first, for anything that changes
	 * behaviour.
	 */
	cpu_impl = VER_IMPL(rdpr(ver));

	/*
	 * Do CPU-specific initialization.
	 */
	if (cpu_impl >= CPU_IMPL_ULTRASPARCIII)
		cheetah_init(cpu_impl);
	else if (cpu_impl == CPU_IMPL_SPARC64V)
		zeus_init(cpu_impl);

	/*
	 * Clear (S)TICK timer (including NPT).
	 */
	tick_clear(cpu_impl);

	/*
	 * UltraSparc II[e,i] based systems come up with the tick interrupt
	 * enabled and a handler that resets the tick counter, causing DELAY()
	 * to not work properly when used early in boot.
	 * UltraSPARC III based systems come up with the system tick interrupt
	 * enabled, causing an interrupt storm on startup since they are not
	 * handled.
	 */
	tick_stop(cpu_impl);

	/*
	 * Set up Open Firmware entry points.
	 */
	ofw_tba = rdpr(tba);
	ofw_vec = (u_long)vec;

	/*
	 * Parse metadata if present and fetch parameters.  Must be before the
	 * console is inited so cninit() gets the right value of boothowto.
	 */
	if (mdp != NULL) {
		preload_metadata = mdp;
		kmdp = preload_search_by_type("elf kernel");
		if (kmdp != NULL) {
			boothowto = MD_FETCH(kmdp, MODINFOMD_HOWTO, int);
			init_static_kenv(MD_FETCH(kmdp, MODINFOMD_ENVP, char *),
			    0);
			end = MD_FETCH(kmdp, MODINFOMD_KERNEND, vm_offset_t);
			kernel_tlb_slots = MD_FETCH(kmdp, MODINFOMD_DTLB_SLOTS,
			    int);
			kernel_tlbs = (void *)preload_search_info(kmdp,
			    MODINFO_METADATA | MODINFOMD_DTLB);
		}
	}

	init_param1();

	/*
	 * Initialize Open Firmware (needed for console).
	 */
	OF_install(OFW_STD_DIRECT, 0);
	OF_init(ofw_entry);

	/*
	 * Prime our per-CPU data page for use.  Note, we are using it for
	 * our stack, so don't pass the real size (PAGE_SIZE) to pcpu_init
	 * or it'll zero it out from under us.
	 */
	pc = (struct pcpu *)(pcpu0 + (PCPU_PAGES * PAGE_SIZE)) - 1;
	pcpu_init(pc, 0, sizeof(struct pcpu));
	pc->pc_addr = (vm_offset_t)pcpu0;
	pc->pc_impl = cpu_impl;
	pc->pc_mid = cpu_get_mid(cpu_impl);
	pc->pc_tlb_ctx = TLB_CTX_USER_MIN;
	pc->pc_tlb_ctx_min = TLB_CTX_USER_MIN;
	pc->pc_tlb_ctx_max = TLB_CTX_USER_MAX;

	/*
	 * Determine the OFW node and frequency of the BSP (and ensure the
	 * BSP is in the device tree in the first place).
	 */
	root = OF_peer(0);
	pc->pc_node = find_bsp(root, pc->pc_mid, cpu_impl);
	if (pc->pc_node == 0)
		OF_panic("%s: cannot find boot CPU node", __func__);
	if (OF_getprop(pc->pc_node, "clock-frequency", &pc->pc_clock,
	    sizeof(pc->pc_clock)) <= 0)
		OF_panic("%s: cannot determine boot CPU clock", __func__);

	/*
	 * Panic if there is no metadata.  Most likely the kernel was booted
	 * directly, instead of through loader(8).
	 */
	if (mdp == NULL || kmdp == NULL || end == 0 ||
	    kernel_tlb_slots == 0 || kernel_tlbs == NULL)
		OF_panic("%s: missing loader metadata.\nThis probably means "
		    "you are not using loader(8).", __func__);

	/*
	 * Work around the broken loader behavior of not demapping no
	 * longer used kernel TLB slots when unloading the kernel or
	 * modules.
	 */
	for (va = KERNBASE + (kernel_tlb_slots - 1) * PAGE_SIZE_4M;
	    va >= roundup2(end, PAGE_SIZE_4M); va -= PAGE_SIZE_4M) {
		if (bootverbose)
			OF_printf("demapping unused kernel TLB slot "
			    "(va %#lx - %#lx)\n", va, va + PAGE_SIZE_4M - 1);
		stxa(TLB_DEMAP_VA(va) | TLB_DEMAP_PRIMARY | TLB_DEMAP_PAGE,
		    ASI_DMMU_DEMAP, 0);
		stxa(TLB_DEMAP_VA(va) | TLB_DEMAP_PRIMARY | TLB_DEMAP_PAGE,
		    ASI_IMMU_DEMAP, 0);
		flush(KERNBASE);
		kernel_tlb_slots--;
	}

	/*
	 * Determine the TLB slot maxima, which are expected to be
	 * equal across all CPUs.
	 * NB: for cheetah-class CPUs, these properties only refer
	 * to the t16s.
	 */
	if (OF_getprop(pc->pc_node, "#dtlb-entries", &dtlb_slots,
	    sizeof(dtlb_slots)) == -1)
		OF_panic("%s: cannot determine number of dTLB slots",
		    __func__);
	if (OF_getprop(pc->pc_node, "#itlb-entries", &itlb_slots,
	    sizeof(itlb_slots)) == -1)
		OF_panic("%s: cannot determine number of iTLB slots",
		    __func__);

	/*
	 * Initialize and enable the caches.  Note that this may include
	 * applying workarounds.
	 */
	cache_init(pc);
	cache_enable(cpu_impl);
	uma_set_align(pc->pc_cache.dc_linesize - 1);

	cpu_block_copy = bcopy;
	cpu_block_zero = bzero;
	getenv_int("machdep.use_vis", &cpu_use_vis);
	if (cpu_use_vis) {
		switch (cpu_impl) {
		case CPU_IMPL_SPARC64:
		case CPU_IMPL_ULTRASPARCI:
		case CPU_IMPL_ULTRASPARCII:
		case CPU_IMPL_ULTRASPARCIIi:
		case CPU_IMPL_ULTRASPARCIIe:
		case CPU_IMPL_ULTRASPARCIII:	/* NB: we've disabled P$. */
		case CPU_IMPL_ULTRASPARCIIIp:
		case CPU_IMPL_ULTRASPARCIIIi:
		case CPU_IMPL_ULTRASPARCIV:
		case CPU_IMPL_ULTRASPARCIVp:
		case CPU_IMPL_ULTRASPARCIIIip:
			cpu_block_copy = spitfire_block_copy;
			cpu_block_zero = spitfire_block_zero;
			break;
		case CPU_IMPL_SPARC64V:
			cpu_block_copy = zeus_block_copy;
			cpu_block_zero = zeus_block_zero;
			break;
		}
	}

#ifdef SMP
	mp_init();
#endif

	/*
	 * Initialize virtual memory and calculate physmem.
	 */
	pmap_bootstrap(cpu_impl);

	/*
	 * Initialize tunables.
	 */
	init_param2(physmem);
	env = kern_getenv("kernelname");
	if (env != NULL) {
		strlcpy(kernelname, env, sizeof(kernelname));
		freeenv(env);
	}

	/*
	 * Initialize the interrupt tables.
	 */
	intr_init1();

	/*
	 * Initialize proc0, set kstack0, frame0, curthread and curpcb.
	 */
	proc_linkup0(&proc0, &thread0);
	proc0.p_md.md_sigtramp = NULL;
	proc0.p_md.md_utrap = NULL;
	thread0.td_kstack = kstack0;
	thread0.td_kstack_pages = KSTACK_PAGES;
	thread0.td_pcb = (struct pcb *)
	    (thread0.td_kstack + KSTACK_PAGES * PAGE_SIZE) - 1;
	frame0.tf_tstate = TSTATE_IE | TSTATE_PEF | TSTATE_PRIV;
	thread0.td_frame = &frame0;
	pc->pc_curthread = &thread0;
	pc->pc_curpcb = thread0.td_pcb;

	/*
	 * Initialize global registers.
	 */
	cpu_setregs(pc);

	/*
	 * Take over the trap table via the PROM.  Using the PROM for this
	 * is necessary in order to set obp-control-relinquished to true
	 * within the PROM so obtaining /virtual-memory/translations doesn't
	 * trigger a fatal reset error or worse things further down the road.
	 * XXX it should be possible to use this solely instead of writing
	 * %tba in cpu_setregs().  Doing so causes a hang however.
	 *
	 * NB: the low-level console drivers require a working DELAY() and
	 * some compiler optimizations may cause the curthread accesses of
	 * mutex(9) to be factored out even if the latter aren't actually
	 * called.  Both of these require PCPU_REG to be set.  However, we
	 * can't set PCPU_REG without also taking over the trap table or the
	 * firmware will overwrite it.
	 */
	sun4u_set_traptable(tl0_base);

	/*
	 * Initialize the dynamic per-CPU area for the BSP and the message
	 * buffer (after setting the trap table).
	 */
	dpcpu_init(dpcpu0, 0);
	msgbufinit(msgbufp, msgbufsize);

	/*
	 * Initialize mutexes.
	 */
	mutex_init();

	/*
	 * Initialize console now that we have a reasonable set of system
	 * services.
	 */
	cninit();

	/*
	 * Finish the interrupt initialization now that mutexes work and
	 * enable them.
	 */
	intr_init2();
	wrpr(pil, 0, 0);
	wrpr(pstate, 0, PSTATE_KERNEL);

	OF_getprop(root, "name", sparc64_model, sizeof(sparc64_model) - 1);

	kdb_init();

#ifdef KDB
	if (boothowto & RB_KDB)
		kdb_enter(KDB_WHY_BOOTFLAGS, "Boot flags requested debugger");
#endif
}

void
sendsig(sig_t catcher, ksiginfo_t *ksi, sigset_t *mask)
{
	struct trapframe *tf;
	struct sigframe *sfp;
	struct sigacts *psp;
	struct sigframe sf;
	struct thread *td;
	struct frame *fp;
	struct proc *p;
	u_long sp;
	int oonstack;
	int sig;

	oonstack = 0;
	td = curthread;
	p = td->td_proc;
	PROC_LOCK_ASSERT(p, MA_OWNED);
	sig = ksi->ksi_signo;
	psp = p->p_sigacts;
	mtx_assert(&psp->ps_mtx, MA_OWNED);
	tf = td->td_frame;
	sp = tf->tf_sp + SPOFF;
	oonstack = sigonstack(sp);

	CTR4(KTR_SIG, "sendsig: td=%p (%s) catcher=%p sig=%d", td, p->p_comm,
	    catcher, sig);

	/* Make sure we have a signal trampoline to return to. */
	if (p->p_md.md_sigtramp == NULL) {
		/*
		 * No signal trampoline... kill the process.
		 */
		CTR0(KTR_SIG, "sendsig: no sigtramp");
		printf("sendsig: %s is too old, rebuild it\n", p->p_comm);
		sigexit(td, sig);
		/* NOTREACHED */
	}

	/* Save user context. */
	bzero(&sf, sizeof(sf));
	get_mcontext(td, &sf.sf_uc.uc_mcontext, 0);
	sf.sf_uc.uc_sigmask = *mask;
	sf.sf_uc.uc_stack = td->td_sigstk;
	sf.sf_uc.uc_stack.ss_flags = (td->td_pflags & TDP_ALTSTACK) ?
	    ((oonstack) ? SS_ONSTACK : 0) : SS_DISABLE;

	/* Allocate and validate space for the signal handler context. */
	if ((td->td_pflags & TDP_ALTSTACK) != 0 && !oonstack &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		sfp = (struct sigframe *)((uintptr_t)td->td_sigstk.ss_sp +
		    td->td_sigstk.ss_size - sizeof(struct sigframe));
	} else
		sfp = (struct sigframe *)sp - 1;
	mtx_unlock(&psp->ps_mtx);
	PROC_UNLOCK(p);

	fp = (struct frame *)sfp - 1;

	/* Build the argument list for the signal handler. */
	tf->tf_out[0] = sig;
	tf->tf_out[2] = (register_t)&sfp->sf_uc;
	tf->tf_out[4] = (register_t)catcher;
	if (SIGISMEMBER(psp->ps_siginfo, sig)) {
		/* Signal handler installed with SA_SIGINFO. */
		tf->tf_out[1] = (register_t)&sfp->sf_si;

		/* Fill in POSIX parts. */
		sf.sf_si = ksi->ksi_info;
		sf.sf_si.si_signo = sig; /* maybe a translated signal */
	} else {
		/* Old FreeBSD-style arguments. */
		tf->tf_out[1] = ksi->ksi_code;
		tf->tf_out[3] = (register_t)ksi->ksi_addr;
	}

	/* Copy the sigframe out to the user's stack. */
	if (rwindow_save(td) != 0 || copyout(&sf, sfp, sizeof(*sfp)) != 0 ||
	    suword(&fp->fr_in[6], tf->tf_out[6]) != 0) {
		/*
		 * Something is wrong with the stack pointer.
		 * ...Kill the process.
		 */
		CTR2(KTR_SIG, "sendsig: sigexit td=%p sfp=%p", td, sfp);
		PROC_LOCK(p);
		sigexit(td, SIGILL);
		/* NOTREACHED */
	}

	tf->tf_tpc = (u_long)p->p_md.md_sigtramp;
	tf->tf_tnpc = tf->tf_tpc + 4;
	tf->tf_sp = (u_long)fp - SPOFF;

	CTR3(KTR_SIG, "sendsig: return td=%p pc=%#lx sp=%#lx", td, tf->tf_tpc,
	    tf->tf_sp);

	PROC_LOCK(p);
	mtx_lock(&psp->ps_mtx);
}

#ifndef	_SYS_SYSPROTO_H_
struct sigreturn_args {
	ucontext_t *ucp;
};
#endif

/*
 * MPSAFE
 */
int
sys_sigreturn(struct thread *td, struct sigreturn_args *uap)
{
	struct proc *p;
	mcontext_t *mc;
	ucontext_t uc;
	int error;

	p = td->td_proc;
	if (rwindow_save(td)) {
		PROC_LOCK(p);
		sigexit(td, SIGILL);
	}

	CTR2(KTR_SIG, "sigreturn: td=%p ucp=%p", td, uap->sigcntxp);
	if (copyin(uap->sigcntxp, &uc, sizeof(uc)) != 0) {
		CTR1(KTR_SIG, "sigreturn: efault td=%p", td);
		return (EFAULT);
	}

	mc = &uc.uc_mcontext;
	error = set_mcontext(td, mc);
	if (error != 0)
		return (error);

	kern_sigprocmask(td, SIG_SETMASK, &uc.uc_sigmask, NULL, 0);

	CTR4(KTR_SIG, "sigreturn: return td=%p pc=%#lx sp=%#lx tstate=%#lx",
	    td, mc->_mc_tpc, mc->_mc_sp, mc->_mc_tstate);
	return (EJUSTRETURN);
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

	pcb->pcb_pc = tf->tf_tpc;
	pcb->pcb_sp = tf->tf_sp;
}

int
get_mcontext(struct thread *td, mcontext_t *mc, int flags)
{
	struct trapframe *tf;
	struct pcb *pcb;

	tf = td->td_frame;
	pcb = td->td_pcb;
	/*
	 * Copy the registers which will be restored by tl0_ret() from the
	 * trapframe.
	 * Note that we skip %g7 which is used as the userland TLS register
	 * and %wstate.
	 */
	mc->_mc_flags = _MC_VERSION;
	mc->mc_global[1] = tf->tf_global[1];
	mc->mc_global[2] = tf->tf_global[2];
	mc->mc_global[3] = tf->tf_global[3];
	mc->mc_global[4] = tf->tf_global[4];
	mc->mc_global[5] = tf->tf_global[5];
	mc->mc_global[6] = tf->tf_global[6];
	if (flags & GET_MC_CLEAR_RET) {
		mc->mc_out[0] = 0;
		mc->mc_out[1] = 0;
	} else {
		mc->mc_out[0] = tf->tf_out[0];
		mc->mc_out[1] = tf->tf_out[1];
	}
	mc->mc_out[2] = tf->tf_out[2];
	mc->mc_out[3] = tf->tf_out[3];
	mc->mc_out[4] = tf->tf_out[4];
	mc->mc_out[5] = tf->tf_out[5];
	mc->mc_out[6] = tf->tf_out[6];
	mc->mc_out[7] = tf->tf_out[7];
	mc->_mc_fprs = tf->tf_fprs;
	mc->_mc_fsr = tf->tf_fsr;
	mc->_mc_gsr = tf->tf_gsr;
	mc->_mc_tnpc = tf->tf_tnpc;
	mc->_mc_tpc = tf->tf_tpc;
	mc->_mc_tstate = tf->tf_tstate;
	mc->_mc_y = tf->tf_y;
	critical_enter();
	if ((tf->tf_fprs & FPRS_FEF) != 0) {
		savefpctx(pcb->pcb_ufp);
		tf->tf_fprs &= ~FPRS_FEF;
		pcb->pcb_flags |= PCB_FEF;
	}
	if ((pcb->pcb_flags & PCB_FEF) != 0) {
		bcopy(pcb->pcb_ufp, mc->mc_fp, sizeof(mc->mc_fp));
		mc->_mc_fprs |= FPRS_FEF;
	}
	critical_exit();
	return (0);
}

int
set_mcontext(struct thread *td, mcontext_t *mc)
{
	struct trapframe *tf;
	struct pcb *pcb;

	if (!TSTATE_SECURE(mc->_mc_tstate) ||
	    (mc->_mc_flags & ((1L << _MC_VERSION_BITS) - 1)) != _MC_VERSION)
		return (EINVAL);
	tf = td->td_frame;
	pcb = td->td_pcb;
	/* Make sure the windows are spilled first. */
	flushw();
	/*
	 * Copy the registers which will be restored by tl0_ret() to the
	 * trapframe.
	 * Note that we skip %g7 which is used as the userland TLS register
	 * and %wstate.
	 */
	tf->tf_global[1] = mc->mc_global[1];
	tf->tf_global[2] = mc->mc_global[2];
	tf->tf_global[3] = mc->mc_global[3];
	tf->tf_global[4] = mc->mc_global[4];
	tf->tf_global[5] = mc->mc_global[5];
	tf->tf_global[6] = mc->mc_global[6];
	tf->tf_out[0] = mc->mc_out[0];
	tf->tf_out[1] = mc->mc_out[1];
	tf->tf_out[2] = mc->mc_out[2];
	tf->tf_out[3] = mc->mc_out[3];
	tf->tf_out[4] = mc->mc_out[4];
	tf->tf_out[5] = mc->mc_out[5];
	tf->tf_out[6] = mc->mc_out[6];
	tf->tf_out[7] = mc->mc_out[7];
	tf->tf_fprs = mc->_mc_fprs;
	tf->tf_fsr = mc->_mc_fsr;
	tf->tf_gsr = mc->_mc_gsr;
	tf->tf_tnpc = mc->_mc_tnpc;
	tf->tf_tpc = mc->_mc_tpc;
	tf->tf_tstate = mc->_mc_tstate;
	tf->tf_y = mc->_mc_y;
	if ((mc->_mc_fprs & FPRS_FEF) != 0) {
		tf->tf_fprs = 0;
		bcopy(mc->mc_fp, pcb->pcb_ufp, sizeof(pcb->pcb_ufp));
		pcb->pcb_flags |= PCB_FEF;
	}
	return (0);
}

/*
 * Exit the kernel and execute a firmware call that will not return, as
 * specified by the arguments.
 */
void
cpu_shutdown(void *args)
{

#ifdef SMP
	cpu_mp_shutdown();
#endif
	ofw_exit(args);
}

/*
 * Flush the D-cache for non-DMA I/O so that the I-cache can
 * be made coherent later.
 */
void
cpu_flush_dcache(void *ptr, size_t len)
{

	/* TBD */
}

/* Get current clock frequency for the given CPU ID. */
int
cpu_est_clockrate(int cpu_id, uint64_t *rate)
{
	struct pcpu *pc;

	pc = pcpu_find(cpu_id);
	if (pc == NULL || rate == NULL)
		return (EINVAL);
	*rate = pc->pc_clock;
	return (0);
}

/*
 * Duplicate OF_exit() with a different firmware call function that restores
 * the trap table, otherwise a RED state exception is triggered in at least
 * some firmware versions.
 */
void
cpu_halt(void)
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
	} args = {
		(cell_t)"exit",
		0,
		0
	};

	cpu_shutdown(&args);
}

static void
sparc64_shutdown_final(void *dummy, int howto)
{
	static struct {
		cell_t name;
		cell_t nargs;
		cell_t nreturns;
	} args = {
		(cell_t)"SUNW,power-off",
		0,
		0
	};

	/* Turn the power off? */
	if ((howto & RB_POWEROFF) != 0)
		cpu_shutdown(&args);
	/* In case of halt, return to the firmware. */
	if ((howto & RB_HALT) != 0)
		cpu_halt();
}

void
cpu_idle(int busy)
{

	/* Insert code to halt (until next interrupt) for the idle loop. */
}

int
cpu_idle_wakeup(int cpu)
{

	return (1);
}

int
ptrace_set_pc(struct thread *td, u_long addr)
{

	td->td_frame->tf_tpc = addr;
	td->td_frame->tf_tnpc = addr + 4;
	return (0);
}

int
ptrace_single_step(struct thread *td)
{

	/* TODO; */
	return (0);
}

int
ptrace_clear_single_step(struct thread *td)
{

	/* TODO; */
	return (0);
}

void
exec_setregs(struct thread *td, struct image_params *imgp, u_long stack)
{
	struct trapframe *tf;
	struct pcb *pcb;
	struct proc *p;
	u_long sp;

	/* XXX no cpu_exec */
	p = td->td_proc;
	p->p_md.md_sigtramp = NULL;
	if (p->p_md.md_utrap != NULL) {
		utrap_free(p->p_md.md_utrap);
		p->p_md.md_utrap = NULL;
	}

	pcb = td->td_pcb;
	tf = td->td_frame;
	sp = rounddown(stack, 16);
	bzero(pcb, sizeof(*pcb));
	bzero(tf, sizeof(*tf));
	tf->tf_out[0] = stack;
	tf->tf_out[3] = p->p_sysent->sv_psstrings;
	tf->tf_out[6] = sp - SPOFF - sizeof(struct frame);
	tf->tf_tnpc = imgp->entry_addr + 4;
	tf->tf_tpc = imgp->entry_addr;
	/*
	 * While we could adhere to the memory model indicated in the ELF
	 * header, it turns out that just always using TSO performs best.
	 */
	tf->tf_tstate = TSTATE_IE | TSTATE_PEF | TSTATE_MM_TSO;
}

int
fill_regs(struct thread *td, struct reg *regs)
{

	bcopy(td->td_frame, regs, sizeof(*regs));
	return (0);
}

int
set_regs(struct thread *td, struct reg *regs)
{
	struct trapframe *tf;

	if (!TSTATE_SECURE(regs->r_tstate))
		return (EINVAL);
	tf = td->td_frame;
	regs->r_wstate = tf->tf_wstate;
	bcopy(regs, tf, sizeof(*regs));
	return (0);
}

int
fill_dbregs(struct thread *td, struct dbreg *dbregs)
{

	return (ENOSYS);
}

int
set_dbregs(struct thread *td, struct dbreg *dbregs)
{

	return (ENOSYS);
}

int
fill_fpregs(struct thread *td, struct fpreg *fpregs)
{
	struct trapframe *tf;
	struct pcb *pcb;

	pcb = td->td_pcb;
	tf = td->td_frame;
	bcopy(pcb->pcb_ufp, fpregs->fr_regs, sizeof(fpregs->fr_regs));
	fpregs->fr_fsr = tf->tf_fsr;
	fpregs->fr_gsr = tf->tf_gsr;
	fpregs->fr_pad[0] = 0;
	return (0);
}

int
set_fpregs(struct thread *td, struct fpreg *fpregs)
{
	struct trapframe *tf;
	struct pcb *pcb;

	pcb = td->td_pcb;
	tf = td->td_frame;
	tf->tf_fprs &= ~FPRS_FEF;
	bcopy(fpregs->fr_regs, pcb->pcb_ufp, sizeof(pcb->pcb_ufp));
	tf->tf_fsr = fpregs->fr_fsr;
	tf->tf_gsr = fpregs->fr_gsr;
	return (0);
}

struct md_utrap *
utrap_alloc(void)
{
	struct md_utrap *ut;

	ut = malloc(sizeof(struct md_utrap), M_SUBPROC, M_WAITOK | M_ZERO);
	ut->ut_refcnt = 1;
	return (ut);
}

void
utrap_free(struct md_utrap *ut)
{
	int refcnt;

	if (ut == NULL)
		return;
	mtx_pool_lock(mtxpool_sleep, ut);
	ut->ut_refcnt--;
	refcnt = ut->ut_refcnt;
	mtx_pool_unlock(mtxpool_sleep, ut);
	if (refcnt == 0)
		free(ut, M_SUBPROC);
}

struct md_utrap *
utrap_hold(struct md_utrap *ut)
{

	if (ut == NULL)
		return (NULL);
	mtx_pool_lock(mtxpool_sleep, ut);
	ut->ut_refcnt++;
	mtx_pool_unlock(mtxpool_sleep, ut);
	return (ut);
}
