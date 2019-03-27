/*	$NetBSD: arm32_machdep.c,v 1.44 2004/03/24 15:34:47 atatat Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2004 Olivier Houchard
 * Copyright (c) 1994-1998 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
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
 *	This product includes software developed by Mark Brinicombe
 *	for the NetBSD Project.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Machine dependent functions for kernel setup
 *
 * Created      : 17/09/94
 * Updated	: 18/04/01 updated for new wscons
 */

#include "opt_ddb.h"
#include "opt_kstack_pages.h"
#include "opt_platform.h"
#include "opt_sched.h"
#include "opt_timer.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/bus.h>
#include <sys/cons.h>
#include <sys/cpu.h>
#include <sys/devmap.h>
#include <sys/efi.h>
#include <sys/imgact.h>
#include <sys/kdb.h>
#include <sys/kernel.h>
#include <sys/linker.h>
#include <sys/msgbuf.h>
#include <sys/reboot.h>
#include <sys/rwlock.h>
#include <sys/sched.h>
#include <sys/syscallsubr.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/vmmeter.h>

#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>

#include <machine/asm.h>
#include <machine/debug_monitor.h>
#include <machine/machdep.h>
#include <machine/metadata.h>
#include <machine/pcb.h>
#include <machine/physmem.h>
#include <machine/platform.h>
#include <machine/sysarch.h>
#include <machine/undefined.h>
#include <machine/vfp.h>
#include <machine/vmparam.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <machine/ofw_machdep.h>
#endif

#ifdef DEBUG
#define	debugf(fmt, args...) printf(fmt, ##args)
#else
#define	debugf(fmt, args...)
#endif

#if defined(COMPAT_FREEBSD4) || defined(COMPAT_FREEBSD5) || \
    defined(COMPAT_FREEBSD6) || defined(COMPAT_FREEBSD7) || \
    defined(COMPAT_FREEBSD9)
#error FreeBSD/arm doesn't provide compatibility with releases prior to 10
#endif

#if __ARM_ARCH >= 6 && !defined(INTRNG)
#error armv6 requires INTRNG
#endif

#ifndef _ARM_ARCH_5E
#error FreeBSD requires ARMv5 or later
#endif

struct pcpu __pcpu[MAXCPU];
struct pcpu *pcpup = &__pcpu[0];

static struct trapframe proc0_tf;
uint32_t cpu_reset_address = 0;
int cold = 1;
vm_offset_t vector_page;

int (*_arm_memcpy)(void *, void *, int, int) = NULL;
int (*_arm_bzero)(void *, int, int) = NULL;
int _min_memcpy_size = 0;
int _min_bzero_size = 0;

extern int *end;

#ifdef FDT
vm_paddr_t pmap_pa;
#if __ARM_ARCH >= 6
vm_offset_t systempage;
vm_offset_t irqstack;
vm_offset_t undstack;
vm_offset_t abtstack;
#else
/*
 * This is the number of L2 page tables required for covering max
 * (hypothetical) memsize of 4GB and all kernel mappings (vectors, msgbuf,
 * stacks etc.), uprounded to be divisible by 4.
 */
#define KERNEL_PT_MAX	78
static struct pv_addr kernel_pt_table[KERNEL_PT_MAX];
struct pv_addr systempage;
static struct pv_addr msgbufpv;
struct pv_addr irqstack;
struct pv_addr undstack;
struct pv_addr abtstack;
static struct pv_addr kernelstack;
#endif /* __ARM_ARCH >= 6 */
#endif /* FDT */

#ifdef PLATFORM
static delay_func *delay_impl;
static void *delay_arg;
#endif

struct kva_md_info kmi;

/*
 * arm32_vector_init:
 *
 *	Initialize the vector page, and select whether or not to
 *	relocate the vectors.
 *
 *	NOTE: We expect the vector page to be mapped at its expected
 *	destination.
 */

extern unsigned int page0[], page0_data[];
void
arm_vector_init(vm_offset_t va, int which)
{
	unsigned int *vectors = (int *) va;
	unsigned int *vectors_data = vectors + (page0_data - page0);
	int vec;

	/*
	 * Loop through the vectors we're taking over, and copy the
	 * vector's insn and data word.
	 */
	for (vec = 0; vec < ARM_NVEC; vec++) {
		if ((which & (1 << vec)) == 0) {
			/* Don't want to take over this vector. */
			continue;
		}
		vectors[vec] = page0[vec];
		vectors_data[vec] = page0_data[vec];
	}

	/* Now sync the vectors. */
	icache_sync(va, (ARM_NVEC * 2) * sizeof(u_int));

	vector_page = va;
#if __ARM_ARCH < 6
	if (va == ARM_VECTORS_HIGH) {
		/*
		 * Enable high vectors in the system control reg (SCTLR).
		 *
		 * Assume the MD caller knows what it's doing here, and really
		 * does want the vector page relocated.
		 *
		 * Note: This has to be done here (and not just in
		 * cpu_setup()) because the vector page needs to be
		 * accessible *before* cpu_startup() is called.
		 * Think ddb(9) ...
		 */
		cpu_control(CPU_CONTROL_VECRELOC, CPU_CONTROL_VECRELOC);
	}
#endif
}

static void
cpu_startup(void *dummy)
{
	struct pcb *pcb = thread0.td_pcb;
	const unsigned int mbyte = 1024 * 1024;
#if __ARM_ARCH < 6 && !defined(ARM_CACHE_LOCK_ENABLE)
	vm_page_t m;
#endif

	identify_arm_cpu();

	vm_ksubmap_init(&kmi);

	/*
	 * Display the RAM layout.
	 */
	printf("real memory  = %ju (%ju MB)\n",
	    (uintmax_t)arm32_ptob(realmem),
	    (uintmax_t)arm32_ptob(realmem) / mbyte);
	printf("avail memory = %ju (%ju MB)\n",
	    (uintmax_t)arm32_ptob(vm_free_count()),
	    (uintmax_t)arm32_ptob(vm_free_count()) / mbyte);
	if (bootverbose) {
		arm_physmem_print_tables();
		devmap_print_table();
	}

	bufinit();
	vm_pager_bufferinit();
	pcb->pcb_regs.sf_sp = (u_int)thread0.td_kstack +
	    USPACE_SVC_STACK_TOP;
	pmap_set_pcb_pagedir(kernel_pmap, pcb);
#if __ARM_ARCH < 6
	vector_page_setprot(VM_PROT_READ);
	pmap_postinit();
#ifdef ARM_CACHE_LOCK_ENABLE
	pmap_kenter_user(ARM_TP_ADDRESS, ARM_TP_ADDRESS);
	arm_lock_cache_line(ARM_TP_ADDRESS);
#else
	m = vm_page_alloc(NULL, 0, VM_ALLOC_NOOBJ | VM_ALLOC_ZERO);
	pmap_kenter_user(ARM_TP_ADDRESS, VM_PAGE_TO_PHYS(m));
#endif
	*(uint32_t *)ARM_RAS_START = 0;
	*(uint32_t *)ARM_RAS_END = 0xffffffff;
#endif
}

SYSINIT(cpu, SI_SUB_CPU, SI_ORDER_FIRST, cpu_startup, NULL);

/*
 * Flush the D-cache for non-DMA I/O so that the I-cache can
 * be made coherent later.
 */
void
cpu_flush_dcache(void *ptr, size_t len)
{

	dcache_wb_poc((vm_offset_t)ptr, (vm_paddr_t)vtophys(ptr), len);
}

/* Get current clock frequency for the given cpu id. */
int
cpu_est_clockrate(int cpu_id, uint64_t *rate)
{
#if __ARM_ARCH >= 6
	struct pcpu *pc;

	pc = pcpu_find(cpu_id);
	if (pc == NULL || rate == NULL)
		return (EINVAL);

	if (pc->pc_clock == 0)
		return (EOPNOTSUPP);

	*rate = pc->pc_clock;

	return (0);
#else
	return (ENXIO);
#endif
}

void
cpu_idle(int busy)
{

	CTR2(KTR_SPARE2, "cpu_idle(%d) at %d", busy, curcpu);
	spinlock_enter();
#ifndef NO_EVENTTIMERS
	if (!busy)
		cpu_idleclock();
#endif
	if (!sched_runnable())
		cpu_sleep(0);
#ifndef NO_EVENTTIMERS
	if (!busy)
		cpu_activeclock();
#endif
	spinlock_exit();
	CTR2(KTR_SPARE2, "cpu_idle(%d) at %d done", busy, curcpu);
}

int
cpu_idle_wakeup(int cpu)
{

	return (0);
}

#ifdef NO_EVENTTIMERS
/*
 * Most ARM platforms don't need to do anything special to init their clocks
 * (they get intialized during normal device attachment), and by not defining a
 * cpu_initclocks() function they get this generic one.  Any platform that needs
 * to do something special can just provide their own implementation, which will
 * override this one due to the weak linkage.
 */
void
arm_generic_initclocks(void)
{
}
__weak_reference(arm_generic_initclocks, cpu_initclocks);

#else
void
cpu_initclocks(void)
{

#ifdef SMP
	if (PCPU_GET(cpuid) == 0)
		cpu_initclocks_bsp();
	else
		cpu_initclocks_ap();
#else
	cpu_initclocks_bsp();
#endif
}
#endif

#ifdef PLATFORM
void
arm_set_delay(delay_func *impl, void *arg)
{

	KASSERT(impl != NULL, ("No DELAY implementation"));
	delay_impl = impl;
	delay_arg = arg;
}

void
DELAY(int usec)
{

	TSENTER();
	delay_impl(usec, delay_arg);
	TSEXIT();
}
#endif

void
cpu_pcpu_init(struct pcpu *pcpu, int cpuid, size_t size)
{
}

void
spinlock_enter(void)
{
	struct thread *td;
	register_t cspr;

	td = curthread;
	if (td->td_md.md_spinlock_count == 0) {
		cspr = disable_interrupts(PSR_I | PSR_F);
		td->td_md.md_spinlock_count = 1;
		td->td_md.md_saved_cspr = cspr;
	} else
		td->td_md.md_spinlock_count++;
	critical_enter();
}

void
spinlock_exit(void)
{
	struct thread *td;
	register_t cspr;

	td = curthread;
	critical_exit();
	cspr = td->td_md.md_saved_cspr;
	td->td_md.md_spinlock_count--;
	if (td->td_md.md_spinlock_count == 0)
		restore_interrupts(cspr);
}

/*
 * Clear registers on exec
 */
void
exec_setregs(struct thread *td, struct image_params *imgp, u_long stack)
{
	struct trapframe *tf = td->td_frame;

	memset(tf, 0, sizeof(*tf));
	tf->tf_usr_sp = stack;
	tf->tf_usr_lr = imgp->entry_addr;
	tf->tf_svc_lr = 0x77777777;
	tf->tf_pc = imgp->entry_addr;
	tf->tf_spsr = PSR_USR32_MODE;
}


#ifdef VFP
/*
 * Get machine VFP context.
 */
void
get_vfpcontext(struct thread *td, mcontext_vfp_t *vfp)
{
	struct pcb *pcb;

	pcb = td->td_pcb;
	if (td == curthread) {
		critical_enter();
		vfp_store(&pcb->pcb_vfpstate, false);
		critical_exit();
	} else
		MPASS(TD_IS_SUSPENDED(td));
	memcpy(vfp->mcv_reg, pcb->pcb_vfpstate.reg,
	    sizeof(vfp->mcv_reg));
	vfp->mcv_fpscr = pcb->pcb_vfpstate.fpscr;
}

/*
 * Set machine VFP context.
 */
void
set_vfpcontext(struct thread *td, mcontext_vfp_t *vfp)
{
	struct pcb *pcb;

	pcb = td->td_pcb;
	if (td == curthread) {
		critical_enter();
		vfp_discard(td);
		critical_exit();
	} else
		MPASS(TD_IS_SUSPENDED(td));
	memcpy(pcb->pcb_vfpstate.reg, vfp->mcv_reg,
	    sizeof(pcb->pcb_vfpstate.reg));
	pcb->pcb_vfpstate.fpscr = vfp->mcv_fpscr;
}
#endif

int
arm_get_vfpstate(struct thread *td, void *args)
{
	int rv;
	struct arm_get_vfpstate_args ua;
	mcontext_vfp_t	mcontext_vfp;

	rv = copyin(args, &ua, sizeof(ua));
	if (rv != 0)
		return (rv);
	if (ua.mc_vfp_size != sizeof(mcontext_vfp_t))
		return (EINVAL);
#ifdef VFP
	get_vfpcontext(td, &mcontext_vfp);
#else
	bzero(&mcontext_vfp, sizeof(mcontext_vfp));
#endif

	rv = copyout(&mcontext_vfp, ua.mc_vfp,  sizeof(mcontext_vfp));
	if (rv != 0)
		return (rv);
	return (0);
}

/*
 * Get machine context.
 */
int
get_mcontext(struct thread *td, mcontext_t *mcp, int clear_ret)
{
	struct trapframe *tf = td->td_frame;
	__greg_t *gr = mcp->__gregs;

	if (clear_ret & GET_MC_CLEAR_RET) {
		gr[_REG_R0] = 0;
		gr[_REG_CPSR] = tf->tf_spsr & ~PSR_C;
	} else {
		gr[_REG_R0]   = tf->tf_r0;
		gr[_REG_CPSR] = tf->tf_spsr;
	}
	gr[_REG_R1]   = tf->tf_r1;
	gr[_REG_R2]   = tf->tf_r2;
	gr[_REG_R3]   = tf->tf_r3;
	gr[_REG_R4]   = tf->tf_r4;
	gr[_REG_R5]   = tf->tf_r5;
	gr[_REG_R6]   = tf->tf_r6;
	gr[_REG_R7]   = tf->tf_r7;
	gr[_REG_R8]   = tf->tf_r8;
	gr[_REG_R9]   = tf->tf_r9;
	gr[_REG_R10]  = tf->tf_r10;
	gr[_REG_R11]  = tf->tf_r11;
	gr[_REG_R12]  = tf->tf_r12;
	gr[_REG_SP]   = tf->tf_usr_sp;
	gr[_REG_LR]   = tf->tf_usr_lr;
	gr[_REG_PC]   = tf->tf_pc;

	mcp->mc_vfp_size = 0;
	mcp->mc_vfp_ptr = NULL;
	memset(&mcp->mc_spare, 0, sizeof(mcp->mc_spare));

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
	mcontext_vfp_t mc_vfp, *vfp;
	struct trapframe *tf = td->td_frame;
	const __greg_t *gr = mcp->__gregs;
	int spsr;

	/*
	 * Make sure the processor mode has not been tampered with and
	 * interrupts have not been disabled.
	 */
	spsr = gr[_REG_CPSR];
	if ((spsr & PSR_MODE) != PSR_USR32_MODE ||
	    (spsr & (PSR_I | PSR_F)) != 0)
		return (EINVAL);

#ifdef WITNESS
	if (mcp->mc_vfp_size != 0 && mcp->mc_vfp_size != sizeof(mc_vfp)) {
		printf("%s: %s: Malformed mc_vfp_size: %d (0x%08X)\n",
		    td->td_proc->p_comm, __func__,
		    mcp->mc_vfp_size, mcp->mc_vfp_size);
	} else if (mcp->mc_vfp_size != 0 && mcp->mc_vfp_ptr == NULL) {
		printf("%s: %s: c_vfp_size != 0 but mc_vfp_ptr == NULL\n",
		    td->td_proc->p_comm, __func__);
	}
#endif

	if (mcp->mc_vfp_size == sizeof(mc_vfp) && mcp->mc_vfp_ptr != NULL) {
		if (copyin(mcp->mc_vfp_ptr, &mc_vfp, sizeof(mc_vfp)) != 0)
			return (EFAULT);
		vfp = &mc_vfp;
	} else {
		vfp = NULL;
	}

	tf->tf_r0 = gr[_REG_R0];
	tf->tf_r1 = gr[_REG_R1];
	tf->tf_r2 = gr[_REG_R2];
	tf->tf_r3 = gr[_REG_R3];
	tf->tf_r4 = gr[_REG_R4];
	tf->tf_r5 = gr[_REG_R5];
	tf->tf_r6 = gr[_REG_R6];
	tf->tf_r7 = gr[_REG_R7];
	tf->tf_r8 = gr[_REG_R8];
	tf->tf_r9 = gr[_REG_R9];
	tf->tf_r10 = gr[_REG_R10];
	tf->tf_r11 = gr[_REG_R11];
	tf->tf_r12 = gr[_REG_R12];
	tf->tf_usr_sp = gr[_REG_SP];
	tf->tf_usr_lr = gr[_REG_LR];
	tf->tf_pc = gr[_REG_PC];
	tf->tf_spsr = gr[_REG_CPSR];
#ifdef VFP
	if (vfp != NULL)
		set_vfpcontext(td, vfp);
#endif
	return (0);
}

void
sendsig(catcher, ksi, mask)
	sig_t catcher;
	ksiginfo_t *ksi;
	sigset_t *mask;
{
	struct thread *td;
	struct proc *p;
	struct trapframe *tf;
	struct sigframe *fp, frame;
	struct sigacts *psp;
	struct sysentvec *sysent;
	int onstack;
	int sig;
	int code;

	td = curthread;
	p = td->td_proc;
	PROC_LOCK_ASSERT(p, MA_OWNED);
	sig = ksi->ksi_signo;
	code = ksi->ksi_code;
	psp = p->p_sigacts;
	mtx_assert(&psp->ps_mtx, MA_OWNED);
	tf = td->td_frame;
	onstack = sigonstack(tf->tf_usr_sp);

	CTR4(KTR_SIG, "sendsig: td=%p (%s) catcher=%p sig=%d", td, p->p_comm,
	    catcher, sig);

	/* Allocate and validate space for the signal handler context. */
	if ((td->td_pflags & TDP_ALTSTACK) != 0 && !(onstack) &&
	    SIGISMEMBER(psp->ps_sigonstack, sig)) {
		fp = (struct sigframe *)((uintptr_t)td->td_sigstk.ss_sp +
		    td->td_sigstk.ss_size);
#if defined(COMPAT_43)
		td->td_sigstk.ss_flags |= SS_ONSTACK;
#endif
	} else
		fp = (struct sigframe *)td->td_frame->tf_usr_sp;

	/* make room on the stack */
	fp--;

	/* make the stack aligned */
	fp = (struct sigframe *)STACKALIGN(fp);
	/* Populate the siginfo frame. */
	bzero(&frame, sizeof(frame));
	get_mcontext(td, &frame.sf_uc.uc_mcontext, 0);
#ifdef VFP
	get_vfpcontext(td, &frame.sf_vfp);
	frame.sf_uc.uc_mcontext.mc_vfp_size = sizeof(fp->sf_vfp);
	frame.sf_uc.uc_mcontext.mc_vfp_ptr = &fp->sf_vfp;
#else
	frame.sf_uc.uc_mcontext.mc_vfp_size = 0;
	frame.sf_uc.uc_mcontext.mc_vfp_ptr = NULL;
#endif
	frame.sf_si = ksi->ksi_info;
	frame.sf_uc.uc_sigmask = *mask;
	frame.sf_uc.uc_stack = td->td_sigstk;
	frame.sf_uc.uc_stack.ss_flags = (td->td_pflags & TDP_ALTSTACK) != 0 ?
	    (onstack ? SS_ONSTACK : 0) : SS_DISABLE;
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

	tf->tf_r0 = sig;
	tf->tf_r1 = (register_t)&fp->sf_si;
	tf->tf_r2 = (register_t)&fp->sf_uc;

	/* the trampoline uses r5 as the uc address */
	tf->tf_r5 = (register_t)&fp->sf_uc;
	tf->tf_pc = (register_t)catcher;
	tf->tf_usr_sp = (register_t)fp;
	sysent = p->p_sysent;
	if (sysent->sv_sigcode_base != 0)
		tf->tf_usr_lr = (register_t)sysent->sv_sigcode_base;
	else
		tf->tf_usr_lr = (register_t)(sysent->sv_psstrings -
		    *(sysent->sv_szsigcode));
	/* Set the mode to enter in the signal handler */
#if __ARM_ARCH >= 7
	if ((register_t)catcher & 1)
		tf->tf_spsr |= PSR_T;
	else
		tf->tf_spsr &= ~PSR_T;
#endif

	CTR3(KTR_SIG, "sendsig: return td=%p pc=%#x sp=%#x", td, tf->tf_usr_lr,
	    tf->tf_usr_sp);

	PROC_LOCK(p);
	mtx_lock(&psp->ps_mtx);
}

int
sys_sigreturn(td, uap)
	struct thread *td;
	struct sigreturn_args /* {
		const struct __ucontext *sigcntxp;
	} */ *uap;
{
	ucontext_t uc;
	int error;

	if (uap == NULL)
		return (EFAULT);
	if (copyin(uap->sigcntxp, &uc, sizeof(uc)))
		return (EFAULT);
	/* Restore register context. */
	error = set_mcontext(td, &uc.uc_mcontext);
	if (error != 0)
		return (error);

	/* Restore signal mask. */
	kern_sigprocmask(td, SIG_SETMASK, &uc.uc_sigmask, NULL, 0);

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
	pcb->pcb_regs.sf_r4 = tf->tf_r4;
	pcb->pcb_regs.sf_r5 = tf->tf_r5;
	pcb->pcb_regs.sf_r6 = tf->tf_r6;
	pcb->pcb_regs.sf_r7 = tf->tf_r7;
	pcb->pcb_regs.sf_r8 = tf->tf_r8;
	pcb->pcb_regs.sf_r9 = tf->tf_r9;
	pcb->pcb_regs.sf_r10 = tf->tf_r10;
	pcb->pcb_regs.sf_r11 = tf->tf_r11;
	pcb->pcb_regs.sf_r12 = tf->tf_r12;
	pcb->pcb_regs.sf_pc = tf->tf_pc;
	pcb->pcb_regs.sf_lr = tf->tf_usr_lr;
	pcb->pcb_regs.sf_sp = tf->tf_usr_sp;
}

void
pcpu0_init(void)
{
#if __ARM_ARCH >= 6
	set_curthread(&thread0);
#endif
	pcpu_init(pcpup, 0, sizeof(struct pcpu));
	PCPU_SET(curthread, &thread0);
}

/*
 * Initialize proc0
 */
void
init_proc0(vm_offset_t kstack)
{
	proc_linkup0(&proc0, &thread0);
	thread0.td_kstack = kstack;
	thread0.td_pcb = (struct pcb *)
		(thread0.td_kstack + kstack_pages * PAGE_SIZE) - 1;
	thread0.td_pcb->pcb_flags = 0;
	thread0.td_pcb->pcb_vfpcpu = -1;
	thread0.td_pcb->pcb_vfpstate.fpscr = VFPSCR_DN;
	thread0.td_frame = &proc0_tf;
	pcpup->pc_curpcb = thread0.td_pcb;
}

#if __ARM_ARCH >= 6
void
set_stackptrs(int cpu)
{

	set_stackptr(PSR_IRQ32_MODE,
	    irqstack + ((IRQ_STACK_SIZE * PAGE_SIZE) * (cpu + 1)));
	set_stackptr(PSR_ABT32_MODE,
	    abtstack + ((ABT_STACK_SIZE * PAGE_SIZE) * (cpu + 1)));
	set_stackptr(PSR_UND32_MODE,
	    undstack + ((UND_STACK_SIZE * PAGE_SIZE) * (cpu + 1)));
}
#else
void
set_stackptrs(int cpu)
{

	set_stackptr(PSR_IRQ32_MODE,
	    irqstack.pv_va + ((IRQ_STACK_SIZE * PAGE_SIZE) * (cpu + 1)));
	set_stackptr(PSR_ABT32_MODE,
	    abtstack.pv_va + ((ABT_STACK_SIZE * PAGE_SIZE) * (cpu + 1)));
	set_stackptr(PSR_UND32_MODE,
	    undstack.pv_va + ((UND_STACK_SIZE * PAGE_SIZE) * (cpu + 1)));
}
#endif

static void
arm_kdb_init(void)
{

	kdb_init();
#ifdef KDB
	if (boothowto & RB_KDB)
		kdb_enter(KDB_WHY_BOOTFLAGS, "Boot flags requested debugger");
#endif
}

#ifdef FDT
#if __ARM_ARCH < 6
void *
initarm(struct arm_boot_params *abp)
{
	struct mem_region mem_regions[FDT_MEM_REGIONS];
	struct pv_addr kernel_l1pt;
	struct pv_addr dpcpu;
	vm_offset_t dtbp, freemempos, l2_start, lastaddr;
	uint64_t memsize;
	uint32_t l2size;
	char *env;
	void *kmdp;
	u_int l1pagetable;
	int i, j, err_devmap, mem_regions_sz;

	lastaddr = parse_boot_param(abp);
	arm_physmem_kernaddr = abp->abp_physaddr;

	memsize = 0;

	cpuinfo_init();
	set_cpufuncs();

	/*
	 * Find the dtb passed in by the boot loader.
	 */
	kmdp = preload_search_by_type("elf kernel");
	if (kmdp != NULL)
		dtbp = MD_FETCH(kmdp, MODINFOMD_DTBP, vm_offset_t);
	else
		dtbp = (vm_offset_t)NULL;

#if defined(FDT_DTB_STATIC)
	/*
	 * In case the device tree blob was not retrieved (from metadata) try
	 * to use the statically embedded one.
	 */
	if (dtbp == (vm_offset_t)NULL)
		dtbp = (vm_offset_t)&fdt_static_dtb;
#endif

	if (OF_install(OFW_FDT, 0) == FALSE)
		panic("Cannot install FDT");

	if (OF_init((void *)dtbp) != 0)
		panic("OF_init failed with the found device tree");

	/* Grab physical memory regions information from device tree. */
	if (fdt_get_mem_regions(mem_regions, &mem_regions_sz, &memsize) != 0)
		panic("Cannot get physical memory regions");
	arm_physmem_hardware_regions(mem_regions, mem_regions_sz);

	/* Grab reserved memory regions information from device tree. */
	if (fdt_get_reserved_regions(mem_regions, &mem_regions_sz) == 0)
		arm_physmem_exclude_regions(mem_regions, mem_regions_sz,
		    EXFLAG_NODUMP | EXFLAG_NOALLOC);

	/* Platform-specific initialisation */
	platform_probe_and_attach();

	pcpu0_init();

	/* Do basic tuning, hz etc */
	init_param1();

	/* Calculate number of L2 tables needed for mapping vm_page_array */
	l2size = (memsize / PAGE_SIZE) * sizeof(struct vm_page);
	l2size = (l2size >> L1_S_SHIFT) + 1;

	/*
	 * Add one table for end of kernel map, one for stacks, msgbuf and
	 * L1 and L2 tables map,  one for vectors map and two for
	 * l2 structures from pmap_bootstrap.
	 */
	l2size += 5;

	/* Make it divisible by 4 */
	l2size = (l2size + 3) & ~3;

	freemempos = (lastaddr + PAGE_MASK) & ~PAGE_MASK;

	/* Define a macro to simplify memory allocation */
#define valloc_pages(var, np)						\
	alloc_pages((var).pv_va, (np));					\
	(var).pv_pa = (var).pv_va + (abp->abp_physaddr - KERNVIRTADDR);

#define alloc_pages(var, np)						\
	(var) = freemempos;						\
	freemempos += (np * PAGE_SIZE);					\
	memset((char *)(var), 0, ((np) * PAGE_SIZE));

	while (((freemempos - L1_TABLE_SIZE) & (L1_TABLE_SIZE - 1)) != 0)
		freemempos += PAGE_SIZE;
	valloc_pages(kernel_l1pt, L1_TABLE_SIZE / PAGE_SIZE);

	for (i = 0, j = 0; i < l2size; ++i) {
		if (!(i % (PAGE_SIZE / L2_TABLE_SIZE_REAL))) {
			valloc_pages(kernel_pt_table[i],
			    L2_TABLE_SIZE / PAGE_SIZE);
			j = i;
		} else {
			kernel_pt_table[i].pv_va = kernel_pt_table[j].pv_va +
			    L2_TABLE_SIZE_REAL * (i - j);
			kernel_pt_table[i].pv_pa =
			    kernel_pt_table[i].pv_va - KERNVIRTADDR +
			    abp->abp_physaddr;

		}
	}
	/*
	 * Allocate a page for the system page mapped to 0x00000000
	 * or 0xffff0000. This page will just contain the system vectors
	 * and can be shared by all processes.
	 */
	valloc_pages(systempage, 1);

	/* Allocate dynamic per-cpu area. */
	valloc_pages(dpcpu, DPCPU_SIZE / PAGE_SIZE);
	dpcpu_init((void *)dpcpu.pv_va, 0);

	/* Allocate stacks for all modes */
	valloc_pages(irqstack, IRQ_STACK_SIZE * MAXCPU);
	valloc_pages(abtstack, ABT_STACK_SIZE * MAXCPU);
	valloc_pages(undstack, UND_STACK_SIZE * MAXCPU);
	valloc_pages(kernelstack, kstack_pages * MAXCPU);
	valloc_pages(msgbufpv, round_page(msgbufsize) / PAGE_SIZE);

	/*
	 * Now we start construction of the L1 page table
	 * We start by mapping the L2 page tables into the L1.
	 * This means that we can replace L1 mappings later on if necessary
	 */
	l1pagetable = kernel_l1pt.pv_va;

	/*
	 * Try to map as much as possible of kernel text and data using
	 * 1MB section mapping and for the rest of initial kernel address
	 * space use L2 coarse tables.
	 *
	 * Link L2 tables for mapping remainder of kernel (modulo 1MB)
	 * and kernel structures
	 */
	l2_start = lastaddr & ~(L1_S_OFFSET);
	for (i = 0 ; i < l2size - 1; i++)
		pmap_link_l2pt(l1pagetable, l2_start + i * L1_S_SIZE,
		    &kernel_pt_table[i]);

	pmap_curmaxkvaddr = l2_start + (l2size - 1) * L1_S_SIZE;

	/* Map kernel code and data */
	pmap_map_chunk(l1pagetable, KERNVIRTADDR, abp->abp_physaddr,
	   (((uint32_t)(lastaddr) - KERNVIRTADDR) + PAGE_MASK) & ~PAGE_MASK,
	    VM_PROT_READ|VM_PROT_WRITE, PTE_CACHE);

	/* Map L1 directory and allocated L2 page tables */
	pmap_map_chunk(l1pagetable, kernel_l1pt.pv_va, kernel_l1pt.pv_pa,
	    L1_TABLE_SIZE, VM_PROT_READ|VM_PROT_WRITE, PTE_PAGETABLE);

	pmap_map_chunk(l1pagetable, kernel_pt_table[0].pv_va,
	    kernel_pt_table[0].pv_pa,
	    L2_TABLE_SIZE_REAL * l2size,
	    VM_PROT_READ|VM_PROT_WRITE, PTE_PAGETABLE);

	/* Map allocated DPCPU, stacks and msgbuf */
	pmap_map_chunk(l1pagetable, dpcpu.pv_va, dpcpu.pv_pa,
	    freemempos - dpcpu.pv_va,
	    VM_PROT_READ|VM_PROT_WRITE, PTE_CACHE);

	/* Link and map the vector page */
	pmap_link_l2pt(l1pagetable, ARM_VECTORS_HIGH,
	    &kernel_pt_table[l2size - 1]);
	pmap_map_entry(l1pagetable, ARM_VECTORS_HIGH, systempage.pv_pa,
	    VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE, PTE_CACHE);

	/* Establish static device mappings. */
	err_devmap = platform_devmap_init();
	devmap_bootstrap(l1pagetable, NULL);
	vm_max_kernel_address = platform_lastaddr();

	cpu_domains((DOMAIN_CLIENT << (PMAP_DOMAIN_KERNEL * 2)) | DOMAIN_CLIENT);
	pmap_pa = kernel_l1pt.pv_pa;
	cpu_setttb(kernel_l1pt.pv_pa);
	cpu_tlb_flushID();
	cpu_domains(DOMAIN_CLIENT << (PMAP_DOMAIN_KERNEL * 2));

	/*
	 * Now that proper page tables are installed, call cpu_setup() to enable
	 * instruction and data caches and other chip-specific features.
	 */
	cpu_setup();

	/*
	 * Only after the SOC registers block is mapped we can perform device
	 * tree fixups, as they may attempt to read parameters from hardware.
	 */
	OF_interpret("perform-fixup", 0);

	platform_gpio_init();

	cninit();

	debugf("initarm: console initialized\n");
	debugf(" arg1 kmdp = 0x%08x\n", (uint32_t)kmdp);
	debugf(" boothowto = 0x%08x\n", boothowto);
	debugf(" dtbp = 0x%08x\n", (uint32_t)dtbp);
	arm_print_kenv();

	env = kern_getenv("kernelname");
	if (env != NULL) {
		strlcpy(kernelname, env, sizeof(kernelname));
		freeenv(env);
	}

	if (err_devmap != 0)
		printf("WARNING: could not fully configure devmap, error=%d\n",
		    err_devmap);

	platform_late_init();

	/*
	 * Pages were allocated during the secondary bootstrap for the
	 * stacks for different CPU modes.
	 * We must now set the r13 registers in the different CPU modes to
	 * point to these stacks.
	 * Since the ARM stacks use STMFD etc. we must set r13 to the top end
	 * of the stack memory.
	 */
	cpu_control(CPU_CONTROL_MMU_ENABLE, CPU_CONTROL_MMU_ENABLE);

	set_stackptrs(0);

	/*
	 * We must now clean the cache again....
	 * Cleaning may be done by reading new data to displace any
	 * dirty data in the cache. This will have happened in cpu_setttb()
	 * but since we are boot strapping the addresses used for the read
	 * may have just been remapped and thus the cache could be out
	 * of sync. A re-clean after the switch will cure this.
	 * After booting there are no gross relocations of the kernel thus
	 * this problem will not occur after initarm().
	 */
	cpu_idcache_wbinv_all();

	undefined_init();

	init_proc0(kernelstack.pv_va);

	arm_vector_init(ARM_VECTORS_HIGH, ARM_VEC_ALL);
	pmap_bootstrap(freemempos, &kernel_l1pt);
	msgbufp = (void *)msgbufpv.pv_va;
	msgbufinit(msgbufp, msgbufsize);
	mutex_init();

	/*
	 * Exclude the kernel (and all the things we allocated which immediately
	 * follow the kernel) from the VM allocation pool but not from crash
	 * dumps.  virtual_avail is a global variable which tracks the kva we've
	 * "allocated" while setting up pmaps.
	 *
	 * Prepare the list of physical memory available to the vm subsystem.
	 */
	arm_physmem_exclude_region(abp->abp_physaddr,
	    (virtual_avail - KERNVIRTADDR), EXFLAG_NOALLOC);
	arm_physmem_init_kernel_globals();

	init_param2(physmem);
	dbg_monitor_init();
	arm_kdb_init();

	return ((void *)(kernelstack.pv_va + USPACE_SVC_STACK_TOP -
	    sizeof(struct pcb)));
}
#else /* __ARM_ARCH < 6 */
void *
initarm(struct arm_boot_params *abp)
{
	struct mem_region mem_regions[FDT_MEM_REGIONS];
	vm_paddr_t lastaddr;
	vm_offset_t dtbp, kernelstack, dpcpu;
	char *env;
	void *kmdp;
	int err_devmap, mem_regions_sz;
#ifdef EFI
	struct efi_map_header *efihdr;
#endif

	/* get last allocated physical address */
	arm_physmem_kernaddr = abp->abp_physaddr;
	lastaddr = parse_boot_param(abp) - KERNVIRTADDR + arm_physmem_kernaddr;

	set_cpufuncs();
	cpuinfo_init();

	/*
	 * Find the dtb passed in by the boot loader.
	 */
	kmdp = preload_search_by_type("elf kernel");
	dtbp = MD_FETCH(kmdp, MODINFOMD_DTBP, vm_offset_t);
#if defined(FDT_DTB_STATIC)
	/*
	 * In case the device tree blob was not retrieved (from metadata) try
	 * to use the statically embedded one.
	 */
	if (dtbp == (vm_offset_t)NULL)
		dtbp = (vm_offset_t)&fdt_static_dtb;
#endif

	if (OF_install(OFW_FDT, 0) == FALSE)
		panic("Cannot install FDT");

	if (OF_init((void *)dtbp) != 0)
		panic("OF_init failed with the found device tree");

#if defined(LINUX_BOOT_ABI)
	arm_parse_fdt_bootargs();
#endif

#ifdef EFI
	efihdr = (struct efi_map_header *)preload_search_info(kmdp,
	    MODINFO_METADATA | MODINFOMD_EFI_MAP);
	if (efihdr != NULL) {
		arm_add_efi_map_entries(efihdr, mem_regions, &mem_regions_sz);
	} else
#endif
	{
		/* Grab physical memory regions information from device tree. */
		if (fdt_get_mem_regions(mem_regions, &mem_regions_sz,NULL) != 0)
			panic("Cannot get physical memory regions");
	}
	arm_physmem_hardware_regions(mem_regions, mem_regions_sz);

	/* Grab reserved memory regions information from device tree. */
	if (fdt_get_reserved_regions(mem_regions, &mem_regions_sz) == 0)
		arm_physmem_exclude_regions(mem_regions, mem_regions_sz,
		    EXFLAG_NODUMP | EXFLAG_NOALLOC);

	/*
	 * Set TEX remapping registers.
	 * Setup kernel page tables and switch to kernel L1 page table.
	 */
	pmap_set_tex();
	pmap_bootstrap_prepare(lastaddr);

	/*
	 * If EARLY_PRINTF support is enabled, we need to re-establish the
	 * mapping after pmap_bootstrap_prepare() switches to new page tables.
	 * Note that we can only do the remapping if the VA is outside the
	 * kernel, now that we have real virtual (not VA=PA) mappings in effect.
	 * Early printf does not work between the time pmap_set_tex() does
	 * cp15_prrr_set() and this code remaps the VA.
	 */
#if defined(EARLY_PRINTF) && defined(SOCDEV_PA) && defined(SOCDEV_VA) && SOCDEV_VA < KERNBASE
	pmap_preboot_map_attr(SOCDEV_PA, SOCDEV_VA, 1024 * 1024, 
	    VM_PROT_READ | VM_PROT_WRITE, VM_MEMATTR_DEVICE);
#endif

	/*
	 * Now that proper page tables are installed, call cpu_setup() to enable
	 * instruction and data caches and other chip-specific features.
	 */
	cpu_setup();

	/* Platform-specific initialisation */
	platform_probe_and_attach();
	pcpu0_init();

	/* Do basic tuning, hz etc */
	init_param1();

	/*
	 * Allocate a page for the system page mapped to 0xffff0000
	 * This page will just contain the system vectors and can be
	 * shared by all processes.
	 */
	systempage = pmap_preboot_get_pages(1);

	/* Map the vector page. */
	pmap_preboot_map_pages(systempage, ARM_VECTORS_HIGH,  1);
	if (virtual_end >= ARM_VECTORS_HIGH)
		virtual_end = ARM_VECTORS_HIGH - 1;

	/* Allocate dynamic per-cpu area. */
	dpcpu = pmap_preboot_get_vpages(DPCPU_SIZE / PAGE_SIZE);
	dpcpu_init((void *)dpcpu, 0);

	/* Allocate stacks for all modes */
	irqstack    = pmap_preboot_get_vpages(IRQ_STACK_SIZE * MAXCPU);
	abtstack    = pmap_preboot_get_vpages(ABT_STACK_SIZE * MAXCPU);
	undstack    = pmap_preboot_get_vpages(UND_STACK_SIZE * MAXCPU );
	kernelstack = pmap_preboot_get_vpages(kstack_pages * MAXCPU);

	/* Allocate message buffer. */
	msgbufp = (void *)pmap_preboot_get_vpages(
	    round_page(msgbufsize) / PAGE_SIZE);

	/*
	 * Pages were allocated during the secondary bootstrap for the
	 * stacks for different CPU modes.
	 * We must now set the r13 registers in the different CPU modes to
	 * point to these stacks.
	 * Since the ARM stacks use STMFD etc. we must set r13 to the top end
	 * of the stack memory.
	 */
	set_stackptrs(0);
	mutex_init();

	/* Establish static device mappings. */
	err_devmap = platform_devmap_init();
	devmap_bootstrap(0, NULL);
	vm_max_kernel_address = platform_lastaddr();

	/*
	 * Only after the SOC registers block is mapped we can perform device
	 * tree fixups, as they may attempt to read parameters from hardware.
	 */
	OF_interpret("perform-fixup", 0);
	platform_gpio_init();
	cninit();

	/*
	 * If we made a mapping for EARLY_PRINTF after pmap_bootstrap_prepare(),
	 * undo it now that the normal console printf works.
	 */
#if defined(EARLY_PRINTF) && defined(SOCDEV_PA) && defined(SOCDEV_VA) && SOCDEV_VA < KERNBASE
	pmap_kremove(SOCDEV_VA);
#endif

	debugf("initarm: console initialized\n");
	debugf(" arg1 kmdp = 0x%08x\n", (uint32_t)kmdp);
	debugf(" boothowto = 0x%08x\n", boothowto);
	debugf(" dtbp = 0x%08x\n", (uint32_t)dtbp);
	debugf(" lastaddr1: 0x%08x\n", lastaddr);
	arm_print_kenv();

	env = kern_getenv("kernelname");
	if (env != NULL)
		strlcpy(kernelname, env, sizeof(kernelname));

	if (err_devmap != 0)
		printf("WARNING: could not fully configure devmap, error=%d\n",
		    err_devmap);

	platform_late_init();

	/*
	 * We must now clean the cache again....
	 * Cleaning may be done by reading new data to displace any
	 * dirty data in the cache. This will have happened in cpu_setttb()
	 * but since we are boot strapping the addresses used for the read
	 * may have just been remapped and thus the cache could be out
	 * of sync. A re-clean after the switch will cure this.
	 * After booting there are no gross relocations of the kernel thus
	 * this problem will not occur after initarm().
	 */
	/* Set stack for exception handlers */
	undefined_init();
	init_proc0(kernelstack);
	arm_vector_init(ARM_VECTORS_HIGH, ARM_VEC_ALL);
	enable_interrupts(PSR_A);
	pmap_bootstrap(0);

	/* Exclude the kernel (and all the things we allocated which immediately
	 * follow the kernel) from the VM allocation pool but not from crash
	 * dumps.  virtual_avail is a global variable which tracks the kva we've
	 * "allocated" while setting up pmaps.
	 *
	 * Prepare the list of physical memory available to the vm subsystem.
	 */
	arm_physmem_exclude_region(abp->abp_physaddr,
		pmap_preboot_get_pages(0) - abp->abp_physaddr, EXFLAG_NOALLOC);
	arm_physmem_init_kernel_globals();

	init_param2(physmem);
	/* Init message buffer. */
	msgbufinit(msgbufp, msgbufsize);
	dbg_monitor_init();
	arm_kdb_init();
	/* Apply possible BP hardening. */
	cpuinfo_init_bp_hardening();
	return ((void *)STACKALIGN(thread0.td_pcb));

}

#endif /* __ARM_ARCH < 6 */
#endif /* FDT */
