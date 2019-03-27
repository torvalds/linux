/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001, John Baldwin <jhb@FreeBSD.org>.
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

/*
 * This module holds the global variables and machine independent functions
 * used for the kernel SMP support.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/ktr.h>
#include <sys/proc.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/pcpu.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>

#include <machine/cpu.h>
#include <machine/smp.h>

#include "opt_sched.h"

#ifdef SMP
MALLOC_DEFINE(M_TOPO, "toponodes", "SMP topology data");

volatile cpuset_t stopped_cpus;
volatile cpuset_t started_cpus;
volatile cpuset_t suspended_cpus;
cpuset_t hlt_cpus_mask;
cpuset_t logical_cpus_mask;

void (*cpustop_restartfunc)(void);
#endif

static int sysctl_kern_smp_active(SYSCTL_HANDLER_ARGS);

/* This is used in modules that need to work in both SMP and UP. */
cpuset_t all_cpus;

int mp_ncpus;
/* export this for libkvm consumers. */
int mp_maxcpus = MAXCPU;

volatile int smp_started;
u_int mp_maxid;

static SYSCTL_NODE(_kern, OID_AUTO, smp, CTLFLAG_RD|CTLFLAG_CAPRD, NULL,
    "Kernel SMP");

SYSCTL_INT(_kern_smp, OID_AUTO, maxid, CTLFLAG_RD|CTLFLAG_CAPRD, &mp_maxid, 0,
    "Max CPU ID.");

SYSCTL_INT(_kern_smp, OID_AUTO, maxcpus, CTLFLAG_RD|CTLFLAG_CAPRD, &mp_maxcpus,
    0, "Max number of CPUs that the system was compiled for.");

SYSCTL_PROC(_kern_smp, OID_AUTO, active, CTLFLAG_RD|CTLTYPE_INT|CTLFLAG_MPSAFE,
    NULL, 0, sysctl_kern_smp_active, "I",
    "Indicates system is running in SMP mode");

int smp_disabled = 0;	/* has smp been disabled? */
SYSCTL_INT(_kern_smp, OID_AUTO, disabled, CTLFLAG_RDTUN|CTLFLAG_CAPRD,
    &smp_disabled, 0, "SMP has been disabled from the loader");

int smp_cpus = 1;	/* how many cpu's running */
SYSCTL_INT(_kern_smp, OID_AUTO, cpus, CTLFLAG_RD|CTLFLAG_CAPRD, &smp_cpus, 0,
    "Number of CPUs online");

int smp_threads_per_core = 1;	/* how many SMT threads are running per core */
SYSCTL_INT(_kern_smp, OID_AUTO, threads_per_core, CTLFLAG_RD|CTLFLAG_CAPRD,
    &smp_threads_per_core, 0, "Number of SMT threads online per core");

int mp_ncores = -1;	/* how many physical cores running */
SYSCTL_INT(_kern_smp, OID_AUTO, cores, CTLFLAG_RD|CTLFLAG_CAPRD, &mp_ncores, 0,
    "Number of CPUs online");

int smp_topology = 0;	/* Which topology we're using. */
SYSCTL_INT(_kern_smp, OID_AUTO, topology, CTLFLAG_RDTUN, &smp_topology, 0,
    "Topology override setting; 0 is default provided by hardware.");

#ifdef SMP
/* Enable forwarding of a signal to a process running on a different CPU */
static int forward_signal_enabled = 1;
SYSCTL_INT(_kern_smp, OID_AUTO, forward_signal_enabled, CTLFLAG_RW,
	   &forward_signal_enabled, 0,
	   "Forwarding of a signal to a process on a different CPU");

/* Variables needed for SMP rendezvous. */
static volatile int smp_rv_ncpus;
static void (*volatile smp_rv_setup_func)(void *arg);
static void (*volatile smp_rv_action_func)(void *arg);
static void (*volatile smp_rv_teardown_func)(void *arg);
static void *volatile smp_rv_func_arg;
static volatile int smp_rv_waiters[4];

/* 
 * Shared mutex to restrict busywaits between smp_rendezvous() and
 * smp(_targeted)_tlb_shootdown().  A deadlock occurs if both of these
 * functions trigger at once and cause multiple CPUs to busywait with
 * interrupts disabled. 
 */
struct mtx smp_ipi_mtx;

/*
 * Let the MD SMP code initialize mp_maxid very early if it can.
 */
static void
mp_setmaxid(void *dummy)
{

	cpu_mp_setmaxid();

	KASSERT(mp_ncpus >= 1, ("%s: CPU count < 1", __func__));
	KASSERT(mp_ncpus > 1 || mp_maxid == 0,
	    ("%s: one CPU but mp_maxid is not zero", __func__));
	KASSERT(mp_maxid >= mp_ncpus - 1,
	    ("%s: counters out of sync: max %d, count %d", __func__,
		mp_maxid, mp_ncpus));
}
SYSINIT(cpu_mp_setmaxid, SI_SUB_TUNABLES, SI_ORDER_FIRST, mp_setmaxid, NULL);

/*
 * Call the MD SMP initialization code.
 */
static void
mp_start(void *dummy)
{

	mtx_init(&smp_ipi_mtx, "smp rendezvous", NULL, MTX_SPIN);

	/* Probe for MP hardware. */
	if (smp_disabled != 0 || cpu_mp_probe() == 0) {
		mp_ncores = 1;
		mp_ncpus = 1;
		CPU_SETOF(PCPU_GET(cpuid), &all_cpus);
		return;
	}

	cpu_mp_start();
	printf("FreeBSD/SMP: Multiprocessor System Detected: %d CPUs\n",
	    mp_ncpus);

	/* Provide a default for most architectures that don't have SMT/HTT. */
	if (mp_ncores < 0)
		mp_ncores = mp_ncpus;

	cpu_mp_announce();
}
SYSINIT(cpu_mp, SI_SUB_CPU, SI_ORDER_THIRD, mp_start, NULL);

void
forward_signal(struct thread *td)
{
	int id;

	/*
	 * signotify() has already set TDF_ASTPENDING and TDF_NEEDSIGCHECK on
	 * this thread, so all we need to do is poke it if it is currently
	 * executing so that it executes ast().
	 */
	THREAD_LOCK_ASSERT(td, MA_OWNED);
	KASSERT(TD_IS_RUNNING(td),
	    ("forward_signal: thread is not TDS_RUNNING"));

	CTR1(KTR_SMP, "forward_signal(%p)", td->td_proc);

	if (!smp_started || cold || panicstr)
		return;
	if (!forward_signal_enabled)
		return;

	/* No need to IPI ourself. */
	if (td == curthread)
		return;

	id = td->td_oncpu;
	if (id == NOCPU)
		return;
	ipi_cpu(id, IPI_AST);
}

/*
 * When called the executing CPU will send an IPI to all other CPUs
 *  requesting that they halt execution.
 *
 * Usually (but not necessarily) called with 'other_cpus' as its arg.
 *
 *  - Signals all CPUs in map to stop.
 *  - Waits for each to stop.
 *
 * Returns:
 *  -1: error
 *   0: NA
 *   1: ok
 *
 */
#if defined(__amd64__) || defined(__i386__)
#define	X86	1
#else
#define	X86	0
#endif
static int
generic_stop_cpus(cpuset_t map, u_int type)
{
#ifdef KTR
	char cpusetbuf[CPUSETBUFSIZ];
#endif
	static volatile u_int stopping_cpu = NOCPU;
	int i;
	volatile cpuset_t *cpus;

	KASSERT(
	    type == IPI_STOP || type == IPI_STOP_HARD
#if X86
	    || type == IPI_SUSPEND
#endif
	    , ("%s: invalid stop type", __func__));

	if (!smp_started)
		return (0);

	CTR2(KTR_SMP, "stop_cpus(%s) with %u type",
	    cpusetobj_strprint(cpusetbuf, &map), type);

#if X86
	/*
	 * When suspending, ensure there are are no IPIs in progress.
	 * IPIs that have been issued, but not yet delivered (e.g.
	 * not pending on a vCPU when running under virtualization)
	 * will be lost, violating FreeBSD's assumption of reliable
	 * IPI delivery.
	 */
	if (type == IPI_SUSPEND)
		mtx_lock_spin(&smp_ipi_mtx);
#endif

#if X86
	if (!nmi_is_broadcast || nmi_kdb_lock == 0) {
#endif
	if (stopping_cpu != PCPU_GET(cpuid))
		while (atomic_cmpset_int(&stopping_cpu, NOCPU,
		    PCPU_GET(cpuid)) == 0)
			while (stopping_cpu != NOCPU)
				cpu_spinwait(); /* spin */

	/* send the stop IPI to all CPUs in map */
	ipi_selected(map, type);
#if X86
	}
#endif

#if X86
	if (type == IPI_SUSPEND)
		cpus = &suspended_cpus;
	else
#endif
		cpus = &stopped_cpus;

	i = 0;
	while (!CPU_SUBSET(cpus, &map)) {
		/* spin */
		cpu_spinwait();
		i++;
		if (i == 100000000) {
			printf("timeout stopping cpus\n");
			break;
		}
	}

#if X86
	if (type == IPI_SUSPEND)
		mtx_unlock_spin(&smp_ipi_mtx);
#endif

	stopping_cpu = NOCPU;
	return (1);
}

int
stop_cpus(cpuset_t map)
{

	return (generic_stop_cpus(map, IPI_STOP));
}

int
stop_cpus_hard(cpuset_t map)
{

	return (generic_stop_cpus(map, IPI_STOP_HARD));
}

#if X86
int
suspend_cpus(cpuset_t map)
{

	return (generic_stop_cpus(map, IPI_SUSPEND));
}
#endif

/*
 * Called by a CPU to restart stopped CPUs. 
 *
 * Usually (but not necessarily) called with 'stopped_cpus' as its arg.
 *
 *  - Signals all CPUs in map to restart.
 *  - Waits for each to restart.
 *
 * Returns:
 *  -1: error
 *   0: NA
 *   1: ok
 */
static int
generic_restart_cpus(cpuset_t map, u_int type)
{
#ifdef KTR
	char cpusetbuf[CPUSETBUFSIZ];
#endif
	volatile cpuset_t *cpus;

	KASSERT(type == IPI_STOP || type == IPI_STOP_HARD
#if X86
	    || type == IPI_SUSPEND
#endif
	    , ("%s: invalid stop type", __func__));

	if (!smp_started)
		return (0);

	CTR1(KTR_SMP, "restart_cpus(%s)", cpusetobj_strprint(cpusetbuf, &map));

#if X86
	if (type == IPI_SUSPEND)
		cpus = &resuming_cpus;
	else
#endif
		cpus = &stopped_cpus;

	/* signal other cpus to restart */
#if X86
	if (type == IPI_SUSPEND)
		CPU_COPY_STORE_REL(&map, &toresume_cpus);
	else
#endif
		CPU_COPY_STORE_REL(&map, &started_cpus);

#if X86
	if (!nmi_is_broadcast || nmi_kdb_lock == 0) {
#endif
	/* wait for each to clear its bit */
	while (CPU_OVERLAP(cpus, &map))
		cpu_spinwait();
#if X86
	}
#endif

	return (1);
}

int
restart_cpus(cpuset_t map)
{

	return (generic_restart_cpus(map, IPI_STOP));
}

#if X86
int
resume_cpus(cpuset_t map)
{

	return (generic_restart_cpus(map, IPI_SUSPEND));
}
#endif
#undef X86

/*
 * All-CPU rendezvous.  CPUs are signalled, all execute the setup function 
 * (if specified), rendezvous, execute the action function (if specified),
 * rendezvous again, execute the teardown function (if specified), and then
 * resume.
 *
 * Note that the supplied external functions _must_ be reentrant and aware
 * that they are running in parallel and in an unknown lock context.
 */
void
smp_rendezvous_action(void)
{
	struct thread *td;
	void *local_func_arg;
	void (*local_setup_func)(void*);
	void (*local_action_func)(void*);
	void (*local_teardown_func)(void*);
#ifdef INVARIANTS
	int owepreempt;
#endif

	/* Ensure we have up-to-date values. */
	atomic_add_acq_int(&smp_rv_waiters[0], 1);
	while (smp_rv_waiters[0] < smp_rv_ncpus)
		cpu_spinwait();

	/* Fetch rendezvous parameters after acquire barrier. */
	local_func_arg = smp_rv_func_arg;
	local_setup_func = smp_rv_setup_func;
	local_action_func = smp_rv_action_func;
	local_teardown_func = smp_rv_teardown_func;

	/*
	 * Use a nested critical section to prevent any preemptions
	 * from occurring during a rendezvous action routine.
	 * Specifically, if a rendezvous handler is invoked via an IPI
	 * and the interrupted thread was in the critical_exit()
	 * function after setting td_critnest to 0 but before
	 * performing a deferred preemption, this routine can be
	 * invoked with td_critnest set to 0 and td_owepreempt true.
	 * In that case, a critical_exit() during the rendezvous
	 * action would trigger a preemption which is not permitted in
	 * a rendezvous action.  To fix this, wrap all of the
	 * rendezvous action handlers in a critical section.  We
	 * cannot use a regular critical section however as having
	 * critical_exit() preempt from this routine would also be
	 * problematic (the preemption must not occur before the IPI
	 * has been acknowledged via an EOI).  Instead, we
	 * intentionally ignore td_owepreempt when leaving the
	 * critical section.  This should be harmless because we do
	 * not permit rendezvous action routines to schedule threads,
	 * and thus td_owepreempt should never transition from 0 to 1
	 * during this routine.
	 */
	td = curthread;
	td->td_critnest++;
#ifdef INVARIANTS
	owepreempt = td->td_owepreempt;
#endif
	
	/*
	 * If requested, run a setup function before the main action
	 * function.  Ensure all CPUs have completed the setup
	 * function before moving on to the action function.
	 */
	if (local_setup_func != smp_no_rendezvous_barrier) {
		if (smp_rv_setup_func != NULL)
			smp_rv_setup_func(smp_rv_func_arg);
		atomic_add_int(&smp_rv_waiters[1], 1);
		while (smp_rv_waiters[1] < smp_rv_ncpus)
                	cpu_spinwait();
	}

	if (local_action_func != NULL)
		local_action_func(local_func_arg);

	if (local_teardown_func != smp_no_rendezvous_barrier) {
		/*
		 * Signal that the main action has been completed.  If a
		 * full exit rendezvous is requested, then all CPUs will
		 * wait here until all CPUs have finished the main action.
		 */
		atomic_add_int(&smp_rv_waiters[2], 1);
		while (smp_rv_waiters[2] < smp_rv_ncpus)
			cpu_spinwait();

		if (local_teardown_func != NULL)
			local_teardown_func(local_func_arg);
	}

	/*
	 * Signal that the rendezvous is fully completed by this CPU.
	 * This means that no member of smp_rv_* pseudo-structure will be
	 * accessed by this target CPU after this point; in particular,
	 * memory pointed by smp_rv_func_arg.
	 *
	 * The release semantic ensures that all accesses performed by
	 * the current CPU are visible when smp_rendezvous_cpus()
	 * returns, by synchronizing with the
	 * atomic_load_acq_int(&smp_rv_waiters[3]).
	 */
	atomic_add_rel_int(&smp_rv_waiters[3], 1);

	td->td_critnest--;
	KASSERT(owepreempt == td->td_owepreempt,
	    ("rendezvous action changed td_owepreempt"));
}

void
smp_rendezvous_cpus(cpuset_t map,
	void (* setup_func)(void *), 
	void (* action_func)(void *),
	void (* teardown_func)(void *),
	void *arg)
{
	int curcpumap, i, ncpus = 0;

	/* Look comments in the !SMP case. */
	if (!smp_started) {
		spinlock_enter();
		if (setup_func != NULL)
			setup_func(arg);
		if (action_func != NULL)
			action_func(arg);
		if (teardown_func != NULL)
			teardown_func(arg);
		spinlock_exit();
		return;
	}

	CPU_FOREACH(i) {
		if (CPU_ISSET(i, &map))
			ncpus++;
	}
	if (ncpus == 0)
		panic("ncpus is 0 with non-zero map");

	mtx_lock_spin(&smp_ipi_mtx);

	/* Pass rendezvous parameters via global variables. */
	smp_rv_ncpus = ncpus;
	smp_rv_setup_func = setup_func;
	smp_rv_action_func = action_func;
	smp_rv_teardown_func = teardown_func;
	smp_rv_func_arg = arg;
	smp_rv_waiters[1] = 0;
	smp_rv_waiters[2] = 0;
	smp_rv_waiters[3] = 0;
	atomic_store_rel_int(&smp_rv_waiters[0], 0);

	/*
	 * Signal other processors, which will enter the IPI with
	 * interrupts off.
	 */
	curcpumap = CPU_ISSET(curcpu, &map);
	CPU_CLR(curcpu, &map);
	ipi_selected(map, IPI_RENDEZVOUS);

	/* Check if the current CPU is in the map */
	if (curcpumap != 0)
		smp_rendezvous_action();

	/*
	 * Ensure that the master CPU waits for all the other
	 * CPUs to finish the rendezvous, so that smp_rv_*
	 * pseudo-structure and the arg are guaranteed to not
	 * be in use.
	 *
	 * Load acquire synchronizes with the release add in
	 * smp_rendezvous_action(), which ensures that our caller sees
	 * all memory actions done by the called functions on other
	 * CPUs.
	 */
	while (atomic_load_acq_int(&smp_rv_waiters[3]) < ncpus)
		cpu_spinwait();

	mtx_unlock_spin(&smp_ipi_mtx);
}

void
smp_rendezvous(void (* setup_func)(void *), 
	       void (* action_func)(void *),
	       void (* teardown_func)(void *),
	       void *arg)
{
	smp_rendezvous_cpus(all_cpus, setup_func, action_func, teardown_func, arg);
}

static struct cpu_group group[MAXCPU * MAX_CACHE_LEVELS + 1];

struct cpu_group *
smp_topo(void)
{
	char cpusetbuf[CPUSETBUFSIZ], cpusetbuf2[CPUSETBUFSIZ];
	struct cpu_group *top;

	/*
	 * Check for a fake topology request for debugging purposes.
	 */
	switch (smp_topology) {
	case 1:
		/* Dual core with no sharing.  */
		top = smp_topo_1level(CG_SHARE_NONE, 2, 0);
		break;
	case 2:
		/* No topology, all cpus are equal. */
		top = smp_topo_none();
		break;
	case 3:
		/* Dual core with shared L2.  */
		top = smp_topo_1level(CG_SHARE_L2, 2, 0);
		break;
	case 4:
		/* quad core, shared l3 among each package, private l2.  */
		top = smp_topo_1level(CG_SHARE_L3, 4, 0);
		break;
	case 5:
		/* quad core,  2 dualcore parts on each package share l2.  */
		top = smp_topo_2level(CG_SHARE_NONE, 2, CG_SHARE_L2, 2, 0);
		break;
	case 6:
		/* Single-core 2xHTT */
		top = smp_topo_1level(CG_SHARE_L1, 2, CG_FLAG_HTT);
		break;
	case 7:
		/* quad core with a shared l3, 8 threads sharing L2.  */
		top = smp_topo_2level(CG_SHARE_L3, 4, CG_SHARE_L2, 8,
		    CG_FLAG_SMT);
		break;
	default:
		/* Default, ask the system what it wants. */
		top = cpu_topo();
		break;
	}
	/*
	 * Verify the returned topology.
	 */
	if (top->cg_count != mp_ncpus)
		panic("Built bad topology at %p.  CPU count %d != %d",
		    top, top->cg_count, mp_ncpus);
	if (CPU_CMP(&top->cg_mask, &all_cpus))
		panic("Built bad topology at %p.  CPU mask (%s) != (%s)",
		    top, cpusetobj_strprint(cpusetbuf, &top->cg_mask),
		    cpusetobj_strprint(cpusetbuf2, &all_cpus));

	/*
	 * Collapse nonsense levels that may be created out of convenience by
	 * the MD layers.  They cause extra work in the search functions.
	 */
	while (top->cg_children == 1) {
		top = &top->cg_child[0];
		top->cg_parent = NULL;
	}
	return (top);
}

struct cpu_group *
smp_topo_alloc(u_int count)
{
	static u_int index;
	u_int curr;

	curr = index;
	index += count;
	return (&group[curr]);
}

struct cpu_group *
smp_topo_none(void)
{
	struct cpu_group *top;

	top = &group[0];
	top->cg_parent = NULL;
	top->cg_child = NULL;
	top->cg_mask = all_cpus;
	top->cg_count = mp_ncpus;
	top->cg_children = 0;
	top->cg_level = CG_SHARE_NONE;
	top->cg_flags = 0;
	
	return (top);
}

static int
smp_topo_addleaf(struct cpu_group *parent, struct cpu_group *child, int share,
    int count, int flags, int start)
{
	char cpusetbuf[CPUSETBUFSIZ], cpusetbuf2[CPUSETBUFSIZ];
	cpuset_t mask;
	int i;

	CPU_ZERO(&mask);
	for (i = 0; i < count; i++, start++)
		CPU_SET(start, &mask);
	child->cg_parent = parent;
	child->cg_child = NULL;
	child->cg_children = 0;
	child->cg_level = share;
	child->cg_count = count;
	child->cg_flags = flags;
	child->cg_mask = mask;
	parent->cg_children++;
	for (; parent != NULL; parent = parent->cg_parent) {
		if (CPU_OVERLAP(&parent->cg_mask, &child->cg_mask))
			panic("Duplicate children in %p.  mask (%s) child (%s)",
			    parent,
			    cpusetobj_strprint(cpusetbuf, &parent->cg_mask),
			    cpusetobj_strprint(cpusetbuf2, &child->cg_mask));
		CPU_OR(&parent->cg_mask, &child->cg_mask);
		parent->cg_count += child->cg_count;
	}

	return (start);
}

struct cpu_group *
smp_topo_1level(int share, int count, int flags)
{
	struct cpu_group *child;
	struct cpu_group *top;
	int packages;
	int cpu;
	int i;

	cpu = 0;
	top = &group[0];
	packages = mp_ncpus / count;
	top->cg_child = child = &group[1];
	top->cg_level = CG_SHARE_NONE;
	for (i = 0; i < packages; i++, child++)
		cpu = smp_topo_addleaf(top, child, share, count, flags, cpu);
	return (top);
}

struct cpu_group *
smp_topo_2level(int l2share, int l2count, int l1share, int l1count,
    int l1flags)
{
	struct cpu_group *top;
	struct cpu_group *l1g;
	struct cpu_group *l2g;
	int cpu;
	int i;
	int j;

	cpu = 0;
	top = &group[0];
	l2g = &group[1];
	top->cg_child = l2g;
	top->cg_level = CG_SHARE_NONE;
	top->cg_children = mp_ncpus / (l2count * l1count);
	l1g = l2g + top->cg_children;
	for (i = 0; i < top->cg_children; i++, l2g++) {
		l2g->cg_parent = top;
		l2g->cg_child = l1g;
		l2g->cg_level = l2share;
		for (j = 0; j < l2count; j++, l1g++)
			cpu = smp_topo_addleaf(l2g, l1g, l1share, l1count,
			    l1flags, cpu);
	}
	return (top);
}


struct cpu_group *
smp_topo_find(struct cpu_group *top, int cpu)
{
	struct cpu_group *cg;
	cpuset_t mask;
	int children;
	int i;

	CPU_SETOF(cpu, &mask);
	cg = top;
	for (;;) {
		if (!CPU_OVERLAP(&cg->cg_mask, &mask))
			return (NULL);
		if (cg->cg_children == 0)
			return (cg);
		children = cg->cg_children;
		for (i = 0, cg = cg->cg_child; i < children; cg++, i++)
			if (CPU_OVERLAP(&cg->cg_mask, &mask))
				break;
	}
	return (NULL);
}
#else /* !SMP */

void
smp_rendezvous_cpus(cpuset_t map,
	void (*setup_func)(void *), 
	void (*action_func)(void *),
	void (*teardown_func)(void *),
	void *arg)
{
	/*
	 * In the !SMP case we just need to ensure the same initial conditions
	 * as the SMP case.
	 */
	spinlock_enter();
	if (setup_func != NULL)
		setup_func(arg);
	if (action_func != NULL)
		action_func(arg);
	if (teardown_func != NULL)
		teardown_func(arg);
	spinlock_exit();
}

void
smp_rendezvous(void (*setup_func)(void *), 
	       void (*action_func)(void *),
	       void (*teardown_func)(void *),
	       void *arg)
{

	smp_rendezvous_cpus(all_cpus, setup_func, action_func, teardown_func,
	    arg);
}

/*
 * Provide dummy SMP support for UP kernels.  Modules that need to use SMP
 * APIs will still work using this dummy support.
 */
static void
mp_setvariables_for_up(void *dummy)
{
	mp_ncpus = 1;
	mp_ncores = 1;
	mp_maxid = PCPU_GET(cpuid);
	CPU_SETOF(mp_maxid, &all_cpus);
	KASSERT(PCPU_GET(cpuid) == 0, ("UP must have a CPU ID of zero"));
}
SYSINIT(cpu_mp_setvariables, SI_SUB_TUNABLES, SI_ORDER_FIRST,
    mp_setvariables_for_up, NULL);
#endif /* SMP */

void
smp_no_rendezvous_barrier(void *dummy)
{
#ifdef SMP
	KASSERT((!smp_started),("smp_no_rendezvous called and smp is started"));
#endif
}

/*
 * Wait for specified idle threads to switch once.  This ensures that even
 * preempted threads have cycled through the switch function once,
 * exiting their codepaths.  This allows us to change global pointers
 * with no other synchronization.
 */
int
quiesce_cpus(cpuset_t map, const char *wmesg, int prio)
{
	struct pcpu *pcpu;
	u_int gen[MAXCPU];
	int error;
	int cpu;

	error = 0;
	for (cpu = 0; cpu <= mp_maxid; cpu++) {
		if (!CPU_ISSET(cpu, &map) || CPU_ABSENT(cpu))
			continue;
		pcpu = pcpu_find(cpu);
		gen[cpu] = pcpu->pc_idlethread->td_generation;
	}
	for (cpu = 0; cpu <= mp_maxid; cpu++) {
		if (!CPU_ISSET(cpu, &map) || CPU_ABSENT(cpu))
			continue;
		pcpu = pcpu_find(cpu);
		thread_lock(curthread);
		sched_bind(curthread, cpu);
		thread_unlock(curthread);
		while (gen[cpu] == pcpu->pc_idlethread->td_generation) {
			error = tsleep(quiesce_cpus, prio, wmesg, 1);
			if (error != EWOULDBLOCK)
				goto out;
			error = 0;
		}
	}
out:
	thread_lock(curthread);
	sched_unbind(curthread);
	thread_unlock(curthread);

	return (error);
}

int
quiesce_all_cpus(const char *wmesg, int prio)
{

	return quiesce_cpus(all_cpus, wmesg, prio);
}

/* Extra care is taken with this sysctl because the data type is volatile */
static int
sysctl_kern_smp_active(SYSCTL_HANDLER_ARGS)
{
	int error, active;

	active = smp_started;
	error = SYSCTL_OUT(req, &active, sizeof(active));
	return (error);
}


#ifdef SMP
void
topo_init_node(struct topo_node *node)
{

	bzero(node, sizeof(*node));
	TAILQ_INIT(&node->children);
}

void
topo_init_root(struct topo_node *root)
{

	topo_init_node(root);
	root->type = TOPO_TYPE_SYSTEM;
}

/*
 * Add a child node with the given ID under the given parent.
 * Do nothing if there is already a child with that ID.
 */
struct topo_node *
topo_add_node_by_hwid(struct topo_node *parent, int hwid,
    topo_node_type type, uintptr_t subtype)
{
	struct topo_node *node;

	TAILQ_FOREACH_REVERSE(node, &parent->children,
	    topo_children, siblings) {
		if (node->hwid == hwid
		    && node->type == type && node->subtype == subtype) {
			return (node);
		}
	}

	node = malloc(sizeof(*node), M_TOPO, M_WAITOK);
	topo_init_node(node);
	node->parent = parent;
	node->hwid = hwid;
	node->type = type;
	node->subtype = subtype;
	TAILQ_INSERT_TAIL(&parent->children, node, siblings);
	parent->nchildren++;

	return (node);
}

/*
 * Find a child node with the given ID under the given parent.
 */
struct topo_node *
topo_find_node_by_hwid(struct topo_node *parent, int hwid,
    topo_node_type type, uintptr_t subtype)
{

	struct topo_node *node;

	TAILQ_FOREACH(node, &parent->children, siblings) {
		if (node->hwid == hwid
		    && node->type == type && node->subtype == subtype) {
			return (node);
		}
	}

	return (NULL);
}

/*
 * Given a node change the order of its parent's child nodes such
 * that the node becomes the firt child while preserving the cyclic
 * order of the children.  In other words, the given node is promoted
 * by rotation.
 */
void
topo_promote_child(struct topo_node *child)
{
	struct topo_node *next;
	struct topo_node *node;
	struct topo_node *parent;

	parent = child->parent;
	next = TAILQ_NEXT(child, siblings);
	TAILQ_REMOVE(&parent->children, child, siblings);
	TAILQ_INSERT_HEAD(&parent->children, child, siblings);

	while (next != NULL) {
		node = next;
		next = TAILQ_NEXT(node, siblings);
		TAILQ_REMOVE(&parent->children, node, siblings);
		TAILQ_INSERT_AFTER(&parent->children, child, node, siblings);
		child = node;
	}
}

/*
 * Iterate to the next node in the depth-first search (traversal) of
 * the topology tree.
 */
struct topo_node *
topo_next_node(struct topo_node *top, struct topo_node *node)
{
	struct topo_node *next;

	if ((next = TAILQ_FIRST(&node->children)) != NULL)
		return (next);

	if ((next = TAILQ_NEXT(node, siblings)) != NULL)
		return (next);

	while (node != top && (node = node->parent) != top)
		if ((next = TAILQ_NEXT(node, siblings)) != NULL)
			return (next);

	return (NULL);
}

/*
 * Iterate to the next node in the depth-first search of the topology tree,
 * but without descending below the current node.
 */
struct topo_node *
topo_next_nonchild_node(struct topo_node *top, struct topo_node *node)
{
	struct topo_node *next;

	if ((next = TAILQ_NEXT(node, siblings)) != NULL)
		return (next);

	while (node != top && (node = node->parent) != top)
		if ((next = TAILQ_NEXT(node, siblings)) != NULL)
			return (next);

	return (NULL);
}

/*
 * Assign the given ID to the given topology node that represents a logical
 * processor.
 */
void
topo_set_pu_id(struct topo_node *node, cpuid_t id)
{

	KASSERT(node->type == TOPO_TYPE_PU,
	    ("topo_set_pu_id: wrong node type: %u", node->type));
	KASSERT(CPU_EMPTY(&node->cpuset) && node->cpu_count == 0,
	    ("topo_set_pu_id: cpuset already not empty"));
	node->id = id;
	CPU_SET(id, &node->cpuset);
	node->cpu_count = 1;
	node->subtype = 1;

	while ((node = node->parent) != NULL) {
		KASSERT(!CPU_ISSET(id, &node->cpuset),
		    ("logical ID %u is already set in node %p", id, node));
		CPU_SET(id, &node->cpuset);
		node->cpu_count++;
	}
}

static struct topology_spec {
	topo_node_type	type;
	bool		match_subtype;
	uintptr_t	subtype;
} topology_level_table[TOPO_LEVEL_COUNT] = {
	[TOPO_LEVEL_PKG] = { .type = TOPO_TYPE_PKG, },
	[TOPO_LEVEL_GROUP] = { .type = TOPO_TYPE_GROUP, },
	[TOPO_LEVEL_CACHEGROUP] = {
		.type = TOPO_TYPE_CACHE,
		.match_subtype = true,
		.subtype = CG_SHARE_L3,
	},
	[TOPO_LEVEL_CORE] = { .type = TOPO_TYPE_CORE, },
	[TOPO_LEVEL_THREAD] = { .type = TOPO_TYPE_PU, },
};

static bool
topo_analyze_table(struct topo_node *root, int all, enum topo_level level,
    struct topo_analysis *results)
{
	struct topology_spec *spec;
	struct topo_node *node;
	int count;

	if (level >= TOPO_LEVEL_COUNT)
		return (true);

	spec = &topology_level_table[level];
	count = 0;
	node = topo_next_node(root, root);

	while (node != NULL) {
		if (node->type != spec->type ||
		    (spec->match_subtype && node->subtype != spec->subtype)) {
			node = topo_next_node(root, node);
			continue;
		}
		if (!all && CPU_EMPTY(&node->cpuset)) {
			node = topo_next_nonchild_node(root, node);
			continue;
		}

		count++;

		if (!topo_analyze_table(node, all, level + 1, results))
			return (false);

		node = topo_next_nonchild_node(root, node);
	}

	/* No explicit subgroups is essentially one subgroup. */
	if (count == 0) {
		count = 1;

		if (!topo_analyze_table(root, all, level + 1, results))
			return (false);
	}

	if (results->entities[level] == -1)
		results->entities[level] = count;
	else if (results->entities[level] != count)
		return (false);

	return (true);
}

/*
 * Check if the topology is uniform, that is, each package has the same number
 * of cores in it and each core has the same number of threads (logical
 * processors) in it.  If so, calculate the number of packages, the number of
 * groups per package, the number of cachegroups per group, and the number of
 * logical processors per cachegroup.  'all' parameter tells whether to include
 * administratively disabled logical processors into the analysis.
 */
int
topo_analyze(struct topo_node *topo_root, int all,
    struct topo_analysis *results)
{

	results->entities[TOPO_LEVEL_PKG] = -1;
	results->entities[TOPO_LEVEL_CORE] = -1;
	results->entities[TOPO_LEVEL_THREAD] = -1;
	results->entities[TOPO_LEVEL_GROUP] = -1;
	results->entities[TOPO_LEVEL_CACHEGROUP] = -1;

	if (!topo_analyze_table(topo_root, all, TOPO_LEVEL_PKG, results))
		return (0);

	KASSERT(results->entities[TOPO_LEVEL_PKG] > 0,
		("bug in topology or analysis"));

	return (1);
}

#endif /* SMP */

