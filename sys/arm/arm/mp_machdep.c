/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 Semihalf.
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
#include "opt_ddb.h"

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/pcpu.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/ktr.h>
#include <sys/malloc.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>

#include <machine/armreg.h>
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/debug_monitor.h>
#include <machine/smp.h>
#include <machine/pcb.h>
#include <machine/physmem.h>
#include <machine/intr.h>
#include <machine/vmparam.h>
#ifdef VFP
#include <machine/vfp.h>
#endif
#ifdef CPU_MV_PJ4B
#include <arm/mv/mvwin.h>
#endif

extern struct pcpu __pcpu[];
/* used to hold the AP's until we are ready to release them */
struct mtx ap_boot_mtx;
struct pcb stoppcbs[MAXCPU];

/* # of Applications processors */
volatile int mp_naps;

/* Set to 1 once we're ready to let the APs out of the pen. */
volatile int aps_ready = 0;

void set_stackptrs(int cpu);

/* Temporary variables for init_secondary()  */
void *dpcpu[MAXCPU - 1];

/* Determine if we running MP machine */
int
cpu_mp_probe(void)
{

	KASSERT(mp_ncpus != 0, ("cpu_mp_probe: mp_ncpus is unset"));

	CPU_SETOF(0, &all_cpus);

	return (mp_ncpus > 1);
}

/* Start Application Processor via platform specific function */
static int
check_ap(void)
{
	uint32_t ms;

	for (ms = 0; ms < 2000; ++ms) {
		if ((mp_naps + 1) == mp_ncpus)
			return (0);		/* success */
		else
			DELAY(1000);
	}

	return (-2);
}

extern unsigned char _end[];

/* Initialize and fire up non-boot processors */
void
cpu_mp_start(void)
{
	int error, i;

	mtx_init(&ap_boot_mtx, "ap boot", NULL, MTX_SPIN);

	/* Reserve memory for application processors */
	for(i = 0; i < (mp_ncpus - 1); i++)
		dpcpu[i] = (void *)kmem_malloc(DPCPU_SIZE, M_WAITOK | M_ZERO);

	dcache_wbinv_poc_all();

	/* Initialize boot code and start up processors */
	platform_mp_start_ap();

	/*  Check if ap's started properly */
	error = check_ap();
	if (error)
		printf("WARNING: Some AP's failed to start\n");
	else
		for (i = 1; i < mp_ncpus; i++)
			CPU_SET(i, &all_cpus);
}

/* Introduce rest of cores to the world */
void
cpu_mp_announce(void)
{

}

extern vm_paddr_t pmap_pa;
void
init_secondary(int cpu)
{
	struct pcpu *pc;
	uint32_t loop_counter;

	pmap_set_tex();
	cpuinfo_reinit_mmu(pmap_kern_ttb);
	cpu_setup();

	/* Provide stack pointers for other processor modes. */
	set_stackptrs(cpu);

	enable_interrupts(PSR_A);
	pc = &__pcpu[cpu];

	/*
	 * pcpu_init() updates queue, so it should not be executed in parallel
	 * on several cores
	 */
	while(mp_naps < (cpu - 1))
		;

	pcpu_init(pc, cpu, sizeof(struct pcpu));
	dpcpu_init(dpcpu[cpu - 1], cpu);
#if __ARM_ARCH >= 6 && defined(DDB)
	dbg_monitor_init_secondary();
#endif
	/* Signal our startup to BSP */
	atomic_add_rel_32(&mp_naps, 1);

	/* Spin until the BSP releases the APs */
	while (!atomic_load_acq_int(&aps_ready)) {
#if __ARM_ARCH >= 7
		__asm __volatile("wfe");
#endif
	}

	/* Initialize curthread */
	KASSERT(PCPU_GET(idlethread) != NULL, ("no idle thread"));
	pc->pc_curthread = pc->pc_idlethread;
	pc->pc_curpcb = pc->pc_idlethread->td_pcb;
	set_curthread(pc->pc_idlethread);
#ifdef VFP
	vfp_init();
#endif

	/* Configure the interrupt controller */
	intr_pic_init_secondary();

	/* Apply possible BP hardening */
	cpuinfo_init_bp_hardening();

	mtx_lock_spin(&ap_boot_mtx);

	atomic_add_rel_32(&smp_cpus, 1);

	if (smp_cpus == mp_ncpus) {
		/* enable IPI's, tlb shootdown, freezes etc */
		atomic_store_rel_int(&smp_started, 1);
	}

	mtx_unlock_spin(&ap_boot_mtx);

	enable_interrupts(PSR_I);

	loop_counter = 0;
	while (smp_started == 0) {
		DELAY(100);
		loop_counter++;
		if (loop_counter == 1000)
			CTR0(KTR_SMP, "AP still wait for smp_started");
	}
	/* Start per-CPU event timers. */
	cpu_initclocks_ap();

	CTR0(KTR_SMP, "go into scheduler");

	/* Enter the scheduler */
	sched_throw(NULL);

	panic("scheduler returned us to %s", __func__);
	/* NOTREACHED */
}

static void
ipi_rendezvous(void *dummy __unused)
{

	CTR0(KTR_SMP, "IPI_RENDEZVOUS");
	smp_rendezvous_action();
}

static void
ipi_ast(void *dummy __unused)
{

	CTR0(KTR_SMP, "IPI_AST");
}

static void
ipi_stop(void *dummy __unused)
{
	u_int cpu;

	/*
	 * IPI_STOP_HARD is mapped to IPI_STOP.
	 */
	CTR0(KTR_SMP, "IPI_STOP or IPI_STOP_HARD");

	cpu = PCPU_GET(cpuid);
	savectx(&stoppcbs[cpu]);

	/*
	 * CPUs are stopped when entering the debugger and at
	 * system shutdown, both events which can precede a
	 * panic dump.  For the dump to be correct, all caches
	 * must be flushed and invalidated, but on ARM there's
	 * no way to broadcast a wbinv_all to other cores.
	 * Instead, we have each core do the local wbinv_all as
	 * part of stopping the core.  The core requesting the
	 * stop will do the l2 cache flush after all other cores
	 * have done their l1 flushes and stopped.
	 */
	dcache_wbinv_poc_all();

	/* Indicate we are stopped */
	CPU_SET_ATOMIC(cpu, &stopped_cpus);

	/* Wait for restart */
	while (!CPU_ISSET(cpu, &started_cpus))
		cpu_spinwait();

	CPU_CLR_ATOMIC(cpu, &started_cpus);
	CPU_CLR_ATOMIC(cpu, &stopped_cpus);
#ifdef DDB
	dbg_resume_dbreg();
#endif
	CTR0(KTR_SMP, "IPI_STOP (restart)");
}

static void
ipi_preempt(void *arg)
{
	struct trapframe *oldframe;
	struct thread *td;

	critical_enter();
	td = curthread;
	td->td_intr_nesting_level++;
	oldframe = td->td_intr_frame;
	td->td_intr_frame = (struct trapframe *)arg;

	CTR1(KTR_SMP, "%s: IPI_PREEMPT", __func__);
	sched_preempt(td);

	td->td_intr_frame = oldframe;
	td->td_intr_nesting_level--;
	critical_exit();
}

static void
ipi_hardclock(void *arg)
{
	struct trapframe *oldframe;
	struct thread *td;

	critical_enter();
	td = curthread;
	td->td_intr_nesting_level++;
	oldframe = td->td_intr_frame;
	td->td_intr_frame = (struct trapframe *)arg;

	CTR1(KTR_SMP, "%s: IPI_HARDCLOCK", __func__);
	hardclockintr();

	td->td_intr_frame = oldframe;
	td->td_intr_nesting_level--;
	critical_exit();
}

static void
release_aps(void *dummy __unused)
{
	uint32_t loop_counter;

	if (mp_ncpus == 1)
		return;

	intr_pic_ipi_setup(IPI_RENDEZVOUS, "rendezvous", ipi_rendezvous, NULL);
	intr_pic_ipi_setup(IPI_AST, "ast", ipi_ast, NULL);
	intr_pic_ipi_setup(IPI_STOP, "stop", ipi_stop, NULL);
	intr_pic_ipi_setup(IPI_PREEMPT, "preempt", ipi_preempt, NULL);
	intr_pic_ipi_setup(IPI_HARDCLOCK, "hardclock", ipi_hardclock, NULL);

	atomic_store_rel_int(&aps_ready, 1);
	/* Wake the other threads up */
	dsb();
	sev();

	printf("Release APs\n");

	for (loop_counter = 0; loop_counter < 2000; loop_counter++) {
		if (smp_started)
			return;
		DELAY(1000);
	}
	printf("AP's not started\n");
}

SYSINIT(start_aps, SI_SUB_SMP, SI_ORDER_FIRST, release_aps, NULL);

struct cpu_group *
cpu_topo(void)
{

	return (smp_topo_1level(CG_SHARE_L2, mp_ncpus, 0));
}

void
cpu_mp_setmaxid(void)
{

	platform_mp_setmaxid();
}

/* Sending IPI */
void
ipi_all_but_self(u_int ipi)
{
	cpuset_t other_cpus;

	other_cpus = all_cpus;
	CPU_CLR(PCPU_GET(cpuid), &other_cpus);
	CTR2(KTR_SMP, "%s: ipi: %x", __func__, ipi);
	intr_ipi_send(other_cpus, ipi);
}

void
ipi_cpu(int cpu, u_int ipi)
{
	cpuset_t cpus;

	CPU_ZERO(&cpus);
	CPU_SET(cpu, &cpus);

	CTR3(KTR_SMP, "%s: cpu: %d, ipi: %x", __func__, cpu, ipi);
	intr_ipi_send(cpus, ipi);
}

void
ipi_selected(cpuset_t cpus, u_int ipi)
{

	CTR2(KTR_SMP, "%s: ipi: %x", __func__, ipi);
	intr_ipi_send(cpus, ipi);
}
