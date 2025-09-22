/*	$OpenBSD: kern_sched.c,v 1.113 2025/06/12 20:37:58 deraadt Exp $	*/
/*
 * Copyright (c) 2007, 2008 Artur Grabowski <art@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>

#include <sys/sched.h>
#include <sys/proc.h>
#include <sys/kthread.h>
#include <sys/systm.h>
#include <sys/clockintr.h>
#include <sys/resourcevar.h>
#include <sys/task.h>
#include <sys/time.h>
#include <sys/smr.h>
#include <sys/tracepoint.h>

#include <uvm/uvm_extern.h>

void sched_kthreads_create(void *);

int sched_proc_to_cpu_cost(struct cpu_info *ci, struct proc *p);
struct proc *sched_steal_proc(struct cpu_info *);

/*
 * To help choosing which cpu should run which process we keep track
 * of cpus which are currently idle and which cpus have processes
 * queued.
 */
struct cpuset sched_idle_cpus;
struct cpuset sched_queued_cpus;
struct cpuset sched_all_cpus;

/*
 * Some general scheduler counters.
 */
uint64_t sched_nmigrations;	/* Cpu migration counter */
uint64_t sched_nomigrations;	/* Cpu no migration counter */
uint64_t sched_noidle;		/* Times we didn't pick the idle task */
uint64_t sched_stolen;		/* Times we stole proc from other cpus */
uint64_t sched_choose;		/* Times we chose a cpu */
uint64_t sched_wasidle;		/* Times we came out of idle */

/* Only schedule processes on sibling CPU threads when true. */
int sched_smt;

/*
 * A few notes about cpu_switchto that is implemented in MD code.
 *
 * cpu_switchto takes two arguments, the old proc and the proc
 * it should switch to. The new proc will never be NULL, so we always have
 * a saved state that we need to switch to. The old proc however can
 * be NULL if the process is exiting. NULL for the old proc simply
 * means "don't bother saving old state".
 *
 * cpu_switchto is supposed to atomically load the new state of the process
 * including the pcb, pmap and setting curproc, the p_cpu pointer in the
 * proc and p_stat to SONPROC. Atomically with respect to interrupts, other
 * cpus in the system must not depend on this state being consistent.
 * Therefore no locking is necessary in cpu_switchto other than blocking
 * interrupts during the context switch.
 */

/*
 * sched_init() is called in main() before calling sched_init_cpu(curcpu()).
 * Setup the bare minimum to allow things like setrunqueue() to work even
 * before the scheduler is actually started.
 */
void
sched_init(void)
{
	cpuset_add(&sched_all_cpus, curcpu());
}

/*
 * sched_init_cpu is called from main() for the boot cpu, then it's the
 * responsibility of the MD code to call it for all other cpus.
 */
void
sched_init_cpu(struct cpu_info *ci)
{
	struct schedstate_percpu *spc = &ci->ci_schedstate;
	int i;

	for (i = 0; i < SCHED_NQS; i++)
		TAILQ_INIT(&spc->spc_qs[i]);

	spc->spc_idleproc = NULL;

	clockintr_bind(&spc->spc_itimer, ci, itimer_update, NULL);
	clockintr_bind(&spc->spc_profclock, ci, profclock, NULL);
	clockintr_bind(&spc->spc_roundrobin, ci, roundrobin, NULL);
	clockintr_bind(&spc->spc_statclock, ci, statclock, NULL);

	kthread_create_deferred(sched_kthreads_create, ci);

	TAILQ_INIT(&spc->spc_deadproc);
	SIMPLEQ_INIT(&spc->spc_deferred);

	/*
	 * Slight hack here until the cpuset code handles cpu_info
	 * structures.
	 */
	cpuset_init_cpu(ci);
}

void
sched_kthreads_create(void *v)
{
	struct cpu_info *ci = v;
	struct schedstate_percpu *spc = &ci->ci_schedstate;
	static int num;

	if (fork1(&proc0, FORK_SHAREVM|FORK_SHAREFILES|FORK_NOZOMBIE|
	    FORK_SYSTEM|FORK_IDLE, sched_idle, ci, NULL,
	    &spc->spc_idleproc))
		panic("fork idle");

	/* Name it as specified. */
	snprintf(spc->spc_idleproc->p_p->ps_comm,
	    sizeof(spc->spc_idleproc->p_p->ps_comm),
	    "idle%d", num);

	num++;
}

void
sched_idle(void *v)
{
	struct schedstate_percpu *spc;
	struct proc *p = curproc;
	struct cpu_info *ci = v;

	KERNEL_UNLOCK();

	/*
	 * The idle thread is setup in fork1(). When the CPU hatches we
	 * enter here for the first time. The CPU is now ready to take
	 * work and so add it to sched_all_cpus when appropriate.
	 * After that just go away and properly reenter once idle.
	 */
#ifdef __HAVE_CPU_TOPOLOGY
	if (sched_smt || ci->ci_smt_id == 0)
		cpuset_add(&sched_all_cpus, ci);
#else
	cpuset_add(&sched_all_cpus, ci);
#endif
	spc = &ci->ci_schedstate;

	KASSERT(ci == curcpu());
	KASSERT(curproc == spc->spc_idleproc);
	KASSERT(p->p_cpu == ci);

	SCHED_LOCK();
	p->p_stat = SSLEEP;
	mi_switch();

	while (1) {
		while (spc->spc_whichqs != 0) {
			struct proc *dead;

			SCHED_LOCK();
			p->p_stat = SSLEEP;
			mi_switch();

			while ((dead = TAILQ_FIRST(&spc->spc_deadproc))) {
				TAILQ_REMOVE(&spc->spc_deadproc, dead, p_runq);
				exit2(dead);
			}
		}

		splassert(IPL_NONE);

		smr_idle();

		cpuset_add(&sched_idle_cpus, ci);
		cpu_idle_enter();
		while (spc->spc_whichqs == 0) {
#ifdef MULTIPROCESSOR
			if (spc->spc_schedflags & SPCF_SHOULDHALT &&
			    (spc->spc_schedflags & SPCF_HALTED) == 0) {
				cpuset_del(&sched_idle_cpus, ci);
				SCHED_LOCK();
				atomic_setbits_int(&spc->spc_schedflags,
				    spc->spc_whichqs ? 0 : SPCF_HALTED);
				SCHED_UNLOCK();
				wakeup(spc);
			}
#endif
			cpu_idle_cycle();
		}
		cpu_idle_leave();
		cpuset_del(&sched_idle_cpus, ci);
	}
}

/*
 * To free our address space we have to jump through a few hoops.
 * The freeing is done by the reaper, but until we have one reaper
 * per cpu, we have no way of putting this proc on the deadproc list
 * and waking up the reaper without risking having our address space and
 * stack torn from under us before we manage to switch to another proc.
 * Therefore we have a per-cpu list of dead processes where we put this
 * proc and have idle clean up that list and move it to the reaper list.
 */
void
sched_exit(struct proc *p)
{
	struct schedstate_percpu *spc = &curcpu()->ci_schedstate;

	TAILQ_INSERT_TAIL(&spc->spc_deadproc, p, p_runq);

	tuagg_add_runtime();

	KERNEL_ASSERT_LOCKED();
	sched_toidle();
}

void
sched_toidle(void)
{
	struct schedstate_percpu *spc = &curcpu()->ci_schedstate;
	struct proc *idle;

#ifdef MULTIPROCESSOR
	/* This process no longer needs to hold the kernel lock. */
	if (_kernel_lock_held())
		__mp_release_all(&kernel_lock);
#endif

	if (ISSET(spc->spc_schedflags, SPCF_ITIMER)) {
		atomic_clearbits_int(&spc->spc_schedflags, SPCF_ITIMER);
		clockintr_cancel(&spc->spc_itimer);
	}
	if (ISSET(spc->spc_schedflags, SPCF_PROFCLOCK)) {
		atomic_clearbits_int(&spc->spc_schedflags, SPCF_PROFCLOCK);
		clockintr_cancel(&spc->spc_profclock);
	}

	atomic_clearbits_int(&spc->spc_schedflags, SPCF_SWITCHCLEAR);

	SCHED_LOCK();
	idle = spc->spc_idleproc;
	idle->p_stat = SRUN;

	uvmexp.swtch++;
	if (curproc != NULL)
		TRACEPOINT(sched, off__cpu, idle->p_tid + THREAD_PID_OFFSET,
		    idle->p_p->ps_pid);
	cpu_switchto(NULL, idle);
	panic("cpu_switchto returned");
}

void
setrunqueue(struct cpu_info *ci, struct proc *p, uint8_t prio)
{
	struct schedstate_percpu *spc;
	int queue = prio >> 2;

	if (ci == NULL)
		ci = sched_choosecpu(p);

	KASSERT(ci != NULL);
	SCHED_ASSERT_LOCKED();
	KASSERT(p->p_wchan == NULL);
	KASSERT(!ISSET(p->p_flag, P_INSCHED));

	p->p_cpu = ci;
	p->p_stat = SRUN;
	p->p_runpri = prio;

	spc = &p->p_cpu->ci_schedstate;
	spc->spc_nrun++;
	TRACEPOINT(sched, enqueue, p->p_tid + THREAD_PID_OFFSET,
	    p->p_p->ps_pid);

	TAILQ_INSERT_TAIL(&spc->spc_qs[queue], p, p_runq);
	spc->spc_whichqs |= (1U << queue);
	cpuset_add(&sched_queued_cpus, p->p_cpu);

	if (cpuset_isset(&sched_idle_cpus, p->p_cpu))
		cpu_unidle(p->p_cpu);
	else if (prio < spc->spc_curpriority)
		need_resched(ci);
}

void
remrunqueue(struct proc *p)
{
	struct schedstate_percpu *spc;
	int queue = p->p_runpri >> 2;

	SCHED_ASSERT_LOCKED();
	spc = &p->p_cpu->ci_schedstate;
	spc->spc_nrun--;
	TRACEPOINT(sched, dequeue, p->p_tid + THREAD_PID_OFFSET,
	    p->p_p->ps_pid);

	TAILQ_REMOVE(&spc->spc_qs[queue], p, p_runq);
	if (TAILQ_EMPTY(&spc->spc_qs[queue])) {
		spc->spc_whichqs &= ~(1U << queue);
		if (spc->spc_whichqs == 0)
			cpuset_del(&sched_queued_cpus, p->p_cpu);
	}
}

struct proc *
sched_chooseproc(void)
{
	struct schedstate_percpu *spc = &curcpu()->ci_schedstate;
	struct proc *p;
	int queue;

	SCHED_ASSERT_LOCKED();

#ifdef MULTIPROCESSOR
	if (spc->spc_schedflags & SPCF_SHOULDHALT) {
		if (spc->spc_whichqs) {
			for (queue = 0; queue < SCHED_NQS; queue++) {
				while ((p = TAILQ_FIRST(&spc->spc_qs[queue]))) {
					remrunqueue(p);
					setrunqueue(NULL, p, p->p_runpri);
					if (p->p_cpu == curcpu()) {
						KASSERT(p->p_flag & P_CPUPEG);
						goto again;
					}
				}
			}
		}
		p = spc->spc_idleproc;
		if (p == NULL)
			panic("no idleproc set on CPU%d",
			    CPU_INFO_UNIT(curcpu()));
		p->p_stat = SRUN;
		KASSERT(p->p_wchan == NULL);
		return (p);
	}
again:
#endif

	if (spc->spc_whichqs) {
		queue = ffs(spc->spc_whichqs) - 1;
		p = TAILQ_FIRST(&spc->spc_qs[queue]);
		remrunqueue(p);
		sched_noidle++;
		if (p->p_stat != SRUN)
			panic("thread %d not in SRUN: %d", p->p_tid, p->p_stat);
	} else if ((p = sched_steal_proc(curcpu())) == NULL) {
		p = spc->spc_idleproc;
		if (p == NULL)
			panic("no idleproc set on CPU%d",
			    CPU_INFO_UNIT(curcpu()));
		p->p_stat = SRUN;
	} 

	KASSERT(p->p_wchan == NULL);
	KASSERT(!ISSET(p->p_flag, P_INSCHED));
	return (p);
}

struct cpu_info *
sched_choosecpu_fork(struct proc *parent, int flags)
{
#ifdef MULTIPROCESSOR
	struct cpu_info *choice = NULL;
	int run, best_run = INT_MAX;
	struct cpu_info *ci;
	struct cpuset set;

#if 0
	/*
	 * XXX
	 * Don't do this until we have a painless way to move the cpu in exec.
	 * Preferably when nuking the old pmap and getting a new one on a
	 * new cpu.
	 */
	/*
	 * PPWAIT forks are simple. We know that the parent will not
	 * run until we exec and choose another cpu, so we just steal its
	 * cpu.
	 */
	if (flags & FORK_PPWAIT)
		return (parent->p_cpu);
#endif

	/*
	 * Look at all cpus that are currently idle and have nothing queued.
	 * If there are none, pick the one with least queued procs first,
	 * then the one with lowest load average.
	 */
	cpuset_complement(&set, &sched_queued_cpus, &sched_idle_cpus);
	cpuset_intersection(&set, &set, &sched_all_cpus);
	if (cpuset_first(&set) == NULL)
		cpuset_copy(&set, &sched_all_cpus);

	while ((ci = cpuset_first(&set)) != NULL) {
		cpuset_del(&set, ci);

		run = ci->ci_schedstate.spc_nrun;

		if (choice == NULL || run < best_run) {
			choice = ci;
			best_run = run;
		}
	}

	if (choice != NULL)
		return (choice);
#endif
	return (curcpu());
}

struct cpu_info *
sched_choosecpu(struct proc *p)
{
#ifdef MULTIPROCESSOR
	struct cpu_info *choice = NULL;
	int last_cost = INT_MAX;
	struct cpu_info *ci;
	struct cpuset set;

	/*
	 * If pegged to a cpu, don't allow it to move.
	 */
	if (p->p_flag & P_CPUPEG)
		return (p->p_cpu);

	sched_choose++;

	/*
	 * Look at all cpus that are currently idle and have nothing queued.
	 * If there are none, pick the cheapest of those.
	 * (idle + queued could mean that the cpu is handling an interrupt
	 * at this moment and haven't had time to leave idle yet).
	 */
	cpuset_complement(&set, &sched_queued_cpus, &sched_idle_cpus);
	cpuset_intersection(&set, &set, &sched_all_cpus);

	/*
	 * First, just check if our current cpu is in that set, if it is,
	 * this is simple.
	 * Also, our cpu might not be idle, but if it's the current cpu
	 * and it has nothing else queued and we're curproc, take it.
	 */
	if (cpuset_isset(&set, p->p_cpu) ||
	    (p->p_cpu == curcpu() && p->p_cpu->ci_schedstate.spc_nrun == 0 &&
	    (p->p_cpu->ci_schedstate.spc_schedflags & SPCF_SHOULDHALT) == 0 &&
	    curproc == p)) {
		sched_wasidle++;
		return (p->p_cpu);
	}

	if (cpuset_first(&set) == NULL)
		cpuset_copy(&set, &sched_all_cpus);

	while ((ci = cpuset_first(&set)) != NULL) {
		int cost = sched_proc_to_cpu_cost(ci, p);

		if (choice == NULL || cost < last_cost) {
			choice = ci;
			last_cost = cost;
		}
		cpuset_del(&set, ci);
	}

	if (p->p_cpu != choice)
		sched_nmigrations++;
	else
		sched_nomigrations++;

	if (choice != NULL)
		return (choice);
#endif
	return (curcpu());
}

/*
 * Attempt to steal a proc from some cpu.
 */
struct proc *
sched_steal_proc(struct cpu_info *self)
{
	struct proc *best = NULL;
#ifdef MULTIPROCESSOR
	struct schedstate_percpu *spc;
	int bestcost = INT_MAX;
	struct cpu_info *ci;
	struct cpuset set;

	KASSERT((self->ci_schedstate.spc_schedflags & SPCF_SHOULDHALT) == 0);

	/* Don't steal if we don't want to schedule processes in this CPU. */
	if (!cpuset_isset(&sched_all_cpus, self))
		return (NULL);

	cpuset_copy(&set, &sched_queued_cpus);

	while ((ci = cpuset_first(&set)) != NULL) {
		struct proc *p;
		int queue;
		int cost;

		cpuset_del(&set, ci);

		spc = &ci->ci_schedstate;

		queue = ffs(spc->spc_whichqs) - 1;
		TAILQ_FOREACH(p, &spc->spc_qs[queue], p_runq) {
			if (p->p_flag & P_CPUPEG)
				continue;

			cost = sched_proc_to_cpu_cost(self, p);

			if (best == NULL || cost < bestcost) {
				best = p;
				bestcost = cost;
			}
		}
	}
	if (best == NULL)
		return (NULL);

	TRACEPOINT(sched, steal, best->p_tid + THREAD_PID_OFFSET,
	    best->p_p->ps_pid, CPU_INFO_UNIT(self));

	remrunqueue(best);
	best->p_cpu = self;

	sched_stolen++;
#endif
	return (best);
}

#ifdef MULTIPROCESSOR
/*
 * Base 2 logarithm of an int. returns 0 for 0 (yeye, I know).
 */
static int
log2(unsigned int i)
{
	int ret = 0;

	while (i >>= 1)
		ret++;

	return (ret);
}

/*
 * Calculate the cost of moving the proc to this cpu.
 * 
 * What we want is some guesstimate of how much "performance" it will
 * cost us to move the proc here. Not just for caches and TLBs and NUMA
 * memory, but also for the proc itself. A highly loaded cpu might not
 * be the best candidate for this proc since it won't get run.
 *
 * Just total guesstimates for now.
 */

int sched_cost_priority = 1;
int sched_cost_runnable = 3;
int sched_cost_resident = 1;
#endif

int
sched_proc_to_cpu_cost(struct cpu_info *ci, struct proc *p)
{
	int cost = 0;
#ifdef MULTIPROCESSOR
	struct schedstate_percpu *spc;
	int l2resident = 0;

	spc = &ci->ci_schedstate;

	/*
	 * First, account for the priority of the proc we want to move.
	 * More willing to move, the lower the priority of the destination
	 * and the higher the priority of the proc.
	 */
	if (!cpuset_isset(&sched_idle_cpus, ci)) {
		cost += (p->p_usrpri - spc->spc_curpriority) *
		    sched_cost_priority;
		cost += sched_cost_runnable;
	}
	if (cpuset_isset(&sched_queued_cpus, ci))
		cost += spc->spc_nrun * sched_cost_runnable;

	/*
	 * Try to avoid the primary cpu as it handles hardware interrupts.
	 *
	 * XXX Needs to be revisited when we distribute interrupts
	 * over cpus.
	 */
	if (CPU_IS_PRIMARY(ci))
		cost += sched_cost_runnable;

	/*
	 * If the proc is on this cpu already, lower the cost by how much
	 * it has been running and an estimate of its footprint.
	 */
	if (p->p_cpu == ci && p->p_slptime == 0) {
		l2resident =
		    log2(pmap_resident_count(p->p_vmspace->vm_map.pmap));
		cost -= l2resident * sched_cost_resident;
	}
#endif
	return (cost);
}

/*
 * Peg a proc to a cpu.
 */
void
sched_peg_curproc(struct cpu_info *ci)
{
	struct proc *p = curproc;

	SCHED_LOCK();
	atomic_setbits_int(&p->p_flag, P_CPUPEG);
	setrunqueue(ci, p, p->p_usrpri);
	p->p_ru.ru_nvcsw++;
	mi_switch();
}

void
sched_unpeg_curproc(void)
{
	struct proc *p = curproc;

	atomic_clearbits_int(&p->p_flag, P_CPUPEG);
}

#ifdef MULTIPROCESSOR

void
sched_start_secondary_cpus(void)
{
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;

	CPU_INFO_FOREACH(cii, ci) {
		struct schedstate_percpu *spc = &ci->ci_schedstate;

		if (CPU_IS_PRIMARY(ci) || !CPU_IS_RUNNING(ci))
			continue;
		atomic_clearbits_int(&spc->spc_schedflags,
		    SPCF_SHOULDHALT | SPCF_HALTED);
#ifdef __HAVE_CPU_TOPOLOGY
		if (!sched_smt && ci->ci_smt_id > 0)
			continue;
#endif
		cpuset_add(&sched_all_cpus, ci);
	}
}

void
sched_stop_secondary_cpus(void)
{
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;

	/*
	 * Make sure we stop the secondary CPUs.
	 */
	CPU_INFO_FOREACH(cii, ci) {
		struct schedstate_percpu *spc = &ci->ci_schedstate;

		if (CPU_IS_PRIMARY(ci) || !CPU_IS_RUNNING(ci))
			continue;
		cpuset_del(&sched_all_cpus, ci);
		atomic_setbits_int(&spc->spc_schedflags, SPCF_SHOULDHALT);
	}
	CPU_INFO_FOREACH(cii, ci) {
		struct schedstate_percpu *spc = &ci->ci_schedstate;

		if (CPU_IS_PRIMARY(ci) || !CPU_IS_RUNNING(ci))
			continue;
		while ((spc->spc_schedflags & SPCF_HALTED) == 0) {
			sleep_setup(spc, PZERO, "schedstate");
			sleep_finish(INFSLP,
			    (spc->spc_schedflags & SPCF_HALTED) == 0);
		}
	}
}

struct sched_barrier_state {
	struct cpu_info *ci;
	struct cond cond;
};

void
sched_barrier_task(void *arg)
{
	struct sched_barrier_state *sb = arg;
	struct cpu_info *ci = sb->ci;

	sched_peg_curproc(ci);
	cond_signal(&sb->cond);
	sched_unpeg_curproc();
}

void
sched_barrier(struct cpu_info *ci)
{
	struct sched_barrier_state sb;
	struct task task;
	CPU_INFO_ITERATOR cii;

	if (ci == NULL) {
		CPU_INFO_FOREACH(cii, ci) {
			if (CPU_IS_PRIMARY(ci))
				break;
		}
	}
	KASSERT(ci != NULL);

	if (ci == curcpu())
		return;

	sb.ci = ci;
	cond_init(&sb.cond);
	task_set(&task, sched_barrier_task, &sb);

	task_add(systqmp, &task);
	cond_wait(&sb.cond, "sbar");
}

#else

void
sched_barrier(struct cpu_info *ci)
{
}

#endif

/*
 * Functions to manipulate cpu sets.
 */
struct cpu_info *cpuset_infos[MAXCPUS];

void
cpuset_init_cpu(struct cpu_info *ci)
{
	cpuset_infos[CPU_INFO_UNIT(ci)] = ci;
}

void
cpuset_add(struct cpuset *cs, struct cpu_info *ci)
{
	unsigned int num = CPU_INFO_UNIT(ci);
	atomic_setbits_int(&cs->cs_set[num/32], (1U << (num % 32)));
}

void
cpuset_del(struct cpuset *cs, struct cpu_info *ci)
{
	unsigned int num = CPU_INFO_UNIT(ci);
	atomic_clearbits_int(&cs->cs_set[num/32], (1U << (num % 32)));
}

int
cpuset_isset(struct cpuset *cs, struct cpu_info *ci)
{
	unsigned int num = CPU_INFO_UNIT(ci);
	return (cs->cs_set[num/32] & (1U << (num % 32)));
}

void
cpuset_copy(struct cpuset *to, struct cpuset *from)
{
	memcpy(to, from, sizeof(*to));
}

struct cpu_info *
cpuset_first(struct cpuset *cs)
{
	int i;

	for (i = 0; i < CPUSET_ASIZE(ncpus); i++)
		if (cs->cs_set[i])
			return (cpuset_infos[i * 32 + ffs(cs->cs_set[i]) - 1]);

	return (NULL);
}

void
cpuset_intersection(struct cpuset *to, struct cpuset *a, struct cpuset *b)
{
	int i;

	for (i = 0; i < CPUSET_ASIZE(ncpus); i++)
		to->cs_set[i] = a->cs_set[i] & b->cs_set[i];
}

void
cpuset_complement(struct cpuset *to, struct cpuset *a, struct cpuset *b)
{
	int i;

	for (i = 0; i < CPUSET_ASIZE(ncpus); i++)
		to->cs_set[i] = b->cs_set[i] & ~a->cs_set[i];
}

int
cpuset_cardinality(struct cpuset *cs)
{
	int cardinality, i, n;

	cardinality = 0;

	for (i = 0; i < CPUSET_ASIZE(ncpus); i++)
		for (n = cs->cs_set[i]; n != 0; n &= n - 1)
			cardinality++;

	return (cardinality);
}

int
sysctl_hwncpuonline(void)
{
	return cpuset_cardinality(&sched_all_cpus);
}

int
cpu_is_online(struct cpu_info *ci)
{
	return cpuset_isset(&sched_all_cpus, ci);
}

#ifdef __HAVE_CPU_TOPOLOGY

#include <sys/sysctl.h>

#ifndef SMALL_KERNEL
int
sysctl_hwsmt(void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;
	int err, newsmt;

	newsmt = sched_smt;
	err = sysctl_int_bounded(oldp, oldlenp, newp, newlen, &newsmt, 0, 1);
	if (err)
		return err;
	if (newsmt == sched_smt)
		return 0;

	sched_smt = newsmt;
	CPU_INFO_FOREACH(cii, ci) {
		if (CPU_IS_PRIMARY(ci) || !CPU_IS_RUNNING(ci))
			continue;
		if (ci->ci_smt_id == 0)
			continue;
		if (sched_smt)
			cpuset_add(&sched_all_cpus, ci);
		else
			cpuset_del(&sched_all_cpus, ci);
	}

	return 0;
}
#endif /* SMALL_KERNEL */

#endif /* __HAVE_CPU_TOPOLOGY */
