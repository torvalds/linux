/*	$OpenBSD: sched_bsd.c,v 1.104 2025/08/01 10:53:23 claudio Exp $	*/
/*	$NetBSD: kern_synch.c,v 1.37 1996/04/22 01:38:37 christos Exp $	*/

/*-
 * Copyright (c) 1982, 1986, 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)kern_synch.c	8.6 (Berkeley) 1/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/clockintr.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/resourcevar.h>
#include <uvm/uvm_extern.h>
#include <sys/sched.h>
#include <sys/timeout.h>
#include <sys/smr.h>
#include <sys/tracepoint.h>

#ifdef KTRACE
#include <sys/ktrace.h>
#endif

uint64_t roundrobin_period;	/* [I] roundrobin period (ns) */
int	lbolt;			/* once a second sleep address */

struct mutex sched_lock;

void			update_loadavg(void *);
void			schedcpu(void *);
uint32_t		decay_aftersleep(uint32_t, uint32_t);

extern struct cpuset sched_idle_cpus;
extern struct cpuset sched_all_cpus;

/*
 * constants for averages over 1, 5, and 15 minutes when sampling at
 * 5 second intervals.
 */
static const fixpt_t cexp[3] = {
	0.9200444146293232 * FSCALE,	/* exp(-1/12) */
	0.9834714538216174 * FSCALE,	/* exp(-1/60) */
	0.9944598480048967 * FSCALE,	/* exp(-1/180) */
};

struct loadavg averunnable;

/*
 * Force switch among equal priority processes every 100ms.
 */
void
roundrobin(struct clockrequest *cr, void *cf, void *arg)
{
	uint64_t count;
	struct cpu_info *ci = curcpu();
	struct schedstate_percpu *spc = &ci->ci_schedstate;

	count = clockrequest_advance(cr, roundrobin_period);

	if (ci->ci_curproc != NULL) {
		if (spc->spc_schedflags & SPCF_SEENRR || count >= 2) {
			/*
			 * The process has already been through a roundrobin
			 * without switching and may be hogging the CPU.
			 * Indicate that the process should yield.
			 */
			atomic_setbits_int(&spc->spc_schedflags,
			    SPCF_SEENRR | SPCF_SHOULDYIELD);
		} else {
			atomic_setbits_int(&spc->spc_schedflags,
			    SPCF_SEENRR);
		}
	}

	if (spc->spc_nrun || spc->spc_schedflags & SPCF_SHOULDYIELD)
		need_resched(ci);
}



/*
 * update_loadav: compute a tenex style load average of a quantity on
 * 1, 5, and 15 minute intervals.
 */
void
update_loadavg(void *unused)
{
	static struct timeout to = TIMEOUT_INITIALIZER(update_loadavg, NULL);
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;
	struct cpuset set;
	u_int i, nrun;

	cpuset_complement(&set, &sched_idle_cpus, &sched_all_cpus);
	nrun = cpuset_cardinality(&set);
	CPU_INFO_FOREACH(cii, ci) {
		nrun += ci->ci_schedstate.spc_nrun;
	}

	for (i = 0; i < 3; i++) {
		averunnable.ldavg[i] = (cexp[i] * averunnable.ldavg[i] +
		    nrun * FSCALE * (FSCALE - cexp[i])) >> FSHIFT;
	}

	timeout_add_sec(&to, 5);
}

/*
 * Constants for digital decay and forget:
 *	90% of (p_estcpu) usage in 5 * loadav time
 *	95% of (p_pctcpu) usage in 60 seconds (load insensitive)
 *          Note that, as ps(1) mentions, this can let percentages
 *          total over 100% (I've seen 137.9% for 3 processes).
 *
 * Note that p_estcpu and p_cpticks are updated independently.
 *
 * We wish to decay away 90% of p_estcpu in (5 * loadavg) seconds.
 * That is, the system wants to compute a value of decay such
 * that the following for loop:
 * 	for (i = 0; i < (5 * loadavg); i++)
 * 		p_estcpu *= decay;
 * will compute
 * 	p_estcpu *= 0.1;
 * for all values of loadavg:
 *
 * Mathematically this loop can be expressed by saying:
 * 	decay ** (5 * loadavg) ~= .1
 *
 * The system computes decay as:
 * 	decay = (2 * loadavg) / (2 * loadavg + 1)
 *
 * We wish to prove that the system's computation of decay
 * will always fulfill the equation:
 * 	decay ** (5 * loadavg) ~= .1
 *
 * If we compute b as:
 * 	b = 2 * loadavg
 * then
 * 	decay = b / (b + 1)
 *
 * We now need to prove two things:
 *	1) Given factor ** (5 * loadavg) ~= .1, prove factor == b/(b+1)
 *	2) Given b/(b+1) ** power ~= .1, prove power == (5 * loadavg)
 *
 * Facts:
 *         For x close to zero, exp(x) =~ 1 + x, since
 *              exp(x) = 0! + x**1/1! + x**2/2! + ... .
 *              therefore exp(-1/b) =~ 1 - (1/b) = (b-1)/b.
 *         For x close to zero, ln(1+x) =~ x, since
 *              ln(1+x) = x - x**2/2 + x**3/3 - ...     -1 < x < 1
 *              therefore ln(b/(b+1)) = ln(1 - 1/(b+1)) =~ -1/(b+1).
 *         ln(.1) =~ -2.30
 *
 * Proof of (1):
 *    Solve (factor)**(power) =~ .1 given power (5*loadav):
 *	solving for factor,
 *      ln(factor) =~ (-2.30/5*loadav), or
 *      factor =~ exp(-1/((5/2.30)*loadav)) =~ exp(-1/(2*loadav)) =
 *          exp(-1/b) =~ (b-1)/b =~ b/(b+1).                    QED
 *
 * Proof of (2):
 *    Solve (factor)**(power) =~ .1 given factor == (b/(b+1)):
 *	solving for power,
 *      power*ln(b/(b+1)) =~ -2.30, or
 *      power =~ 2.3 * (b + 1) = 4.6*loadav + 2.3 =~ 5*loadav.  QED
 *
 * Actual power values for the implemented algorithm are as follows:
 *      loadav: 1       2       3       4
 *      power:  5.68    10.32   14.94   19.55
 */

/* calculations for digital decay to forget 90% of usage in 5*loadav sec */
#define	loadfactor(loadav)	(2 * (loadav))
#define	decay_cpu(loadfac, cpu)	(((loadfac) * (cpu)) / ((loadfac) + FSCALE))

/* decay 95% of `p_pctcpu' in 60 seconds; see CCPU_SHIFT before changing */
fixpt_t	ccpu = 0.95122942450071400909 * FSCALE;		/* exp(-1/20) */

/*
 * If `ccpu' is not equal to `exp(-1/20)' and you still want to use the
 * faster/more-accurate formula, you'll have to estimate CCPU_SHIFT below
 * and possibly adjust FSHIFT in "param.h" so that (FSHIFT >= CCPU_SHIFT).
 *
 * To estimate CCPU_SHIFT for exp(-1/20), the following formula was used:
 *	1 - exp(-1/20) ~= 0.0487 ~= 0.0488 == 1 (fixed pt, *11* bits).
 *
 * If you don't want to bother with the faster/more-accurate formula, you
 * can set CCPU_SHIFT to (FSHIFT + 1) which will use a slower/less-accurate
 * (more general) method of calculating the %age of CPU used by a process.
 */
#define	CCPU_SHIFT	11

/*
 * Recompute process priorities, every second.
 */
void
schedcpu(void *unused)
{
	static struct timeout to = TIMEOUT_INITIALIZER(schedcpu, NULL);
	fixpt_t loadfac = loadfactor(averunnable.ldavg[0]), pctcpu;
	struct proc *p;
	unsigned int newcpu, cpt;

	LIST_FOREACH(p, &allproc, p_list) {
		/*
		 * Idle threads are never placed on the runqueue,
		 * therefore computing their priority is pointless.
		 */
		if (p->p_cpu != NULL &&
		    p->p_cpu->ci_schedstate.spc_idleproc == p)
			continue;
		/*
		 * Increment sleep time (if sleeping). We ignore overflow.
		 */
		if (p->p_stat == SSLEEP || p->p_stat == SSTOP)
			p->p_slptime++;
		pctcpu = (p->p_pctcpu * ccpu) >> FSHIFT;
		/*
		 * If the process has slept the entire second,
		 * stop recalculating its priority until it wakes up.
		 */
		if (p->p_slptime > 1) {
			p->p_pctcpu = pctcpu;
			continue;
		}
		SCHED_LOCK();
		/*
		 * p_pctcpu is only for diagnostic tools such as ps.
		 */
		cpt = READ_ONCE(p->p_cpticks);
#if	(FSHIFT >= CCPU_SHIFT)
		pctcpu += (stathz == 100) ?
		    (cpt - p->p_cpticks2) << (FSHIFT - CCPU_SHIFT) :
		    100 * ((cpt - p->p_cpticks2)
				<< (FSHIFT - CCPU_SHIFT)) / stathz;
#else
		pctcpu += ((FSCALE - ccpu) *
		    ((cpt - p->p_cpticks2) * FSCALE / stathz)) >> FSHIFT;
#endif
		p->p_pctcpu = pctcpu;
		p->p_cpticks2 = cpt;
		newcpu = (u_int) decay_cpu(loadfac, p->p_estcpu);
		setpriority(p, newcpu, p->p_p->ps_nice);

		if (p->p_stat == SRUN &&
		    (p->p_runpri / SCHED_PPQ) != (p->p_usrpri / SCHED_PPQ)) {
			remrunqueue(p);
			setrunqueue(p->p_cpu, p, p->p_usrpri);
		}
		SCHED_UNLOCK();
	}
	wakeup(&lbolt);
	timeout_add_sec(&to, 1);
}

/*
 * Recalculate the priority of a process after it has slept for a while.
 * For all load averages >= 1 and max p_estcpu of 255, sleeping for at
 * least six times the loadfactor will decay p_estcpu to zero.
 */
uint32_t
decay_aftersleep(uint32_t estcpu, uint32_t slptime)
{
	fixpt_t loadfac = loadfactor(averunnable.ldavg[0]);
	uint32_t newcpu;

	if (slptime > 5 * loadfac)
		newcpu = 0;
	else {
		newcpu = estcpu;
		slptime--;	/* the first time was done in schedcpu */
		while (newcpu && --slptime)
			newcpu = decay_cpu(loadfac, newcpu);

	}

	return (newcpu);
}

/*
 * General yield call.  Puts the current process back on its run queue and
 * performs a voluntary context switch.
 */
void
yield(void)
{
	struct proc *p = curproc;

	SCHED_LOCK();
	setrunqueue(p->p_cpu, p, p->p_usrpri);
	p->p_ru.ru_nvcsw++;
	mi_switch();
}

/*
 * General preemption call.  Puts the current process back on its run queue
 * and performs an involuntary context switch.  If a process is supplied,
 * we switch to that process.  Otherwise, we use the normal process selection
 * criteria.
 */
void
preempt(void)
{
	struct proc *p = curproc;

	SCHED_LOCK();
	setrunqueue(p->p_cpu, p, p->p_usrpri);
	p->p_ru.ru_nivcsw++;
	mi_switch();
}

void
mi_switch(void)
{
	struct schedstate_percpu *spc = &curcpu()->ci_schedstate;
	struct proc *p = curproc;
	struct proc *nextproc;
	int oldipl;
#ifdef MULTIPROCESSOR
	int hold_count;
#endif

	KASSERT(p->p_stat != SONPROC);

	SCHED_ASSERT_LOCKED();

#ifdef MULTIPROCESSOR
	/*
	 * Release the kernel_lock, as we are about to yield the CPU.
	 */
	if (_kernel_lock_held())
		hold_count = __mp_release_all(&kernel_lock);
	else
		hold_count = 0;
#endif

	/* Update thread runtime */
	tuagg_add_runtime();

	/* Stop any optional clock interrupts. */
	if (ISSET(spc->spc_schedflags, SPCF_ITIMER)) {
		atomic_clearbits_int(&spc->spc_schedflags, SPCF_ITIMER);
		clockintr_cancel(&spc->spc_itimer);
	}
	if (ISSET(spc->spc_schedflags, SPCF_PROFCLOCK)) {
		atomic_clearbits_int(&spc->spc_schedflags, SPCF_PROFCLOCK);
		clockintr_cancel(&spc->spc_profclock);
	}

	/*
	 * Process is about to yield the CPU; clear the appropriate
	 * scheduling flags.
	 */
	atomic_clearbits_int(&spc->spc_schedflags, SPCF_SWITCHCLEAR);

	nextproc = sched_chooseproc();

	/* preserve old IPL level so we can switch back to that */
	oldipl = MUTEX_OLDIPL(&sched_lock);

	if (p != nextproc) {
		uvmexp.swtch++;
		TRACEPOINT(sched, off__cpu, nextproc->p_tid + THREAD_PID_OFFSET,
		    nextproc->p_p->ps_pid);
		cpu_switchto(p, nextproc);
		TRACEPOINT(sched, on__cpu, NULL);
	} else {
		TRACEPOINT(sched, remain__cpu, NULL);
		p->p_stat = SONPROC;
	}

	clear_resched(curcpu());

	SCHED_ASSERT_LOCKED();

	/* Restore proc's IPL. */
	MUTEX_OLDIPL(&sched_lock) = oldipl;
	SCHED_UNLOCK();

	SCHED_ASSERT_UNLOCKED();

	assertwaitok();
	smr_idle();

	/*
	 * We're running again; record our new start time.  We might
	 * be running on a new CPU now, so refetch the schedstate_percpu
	 * pointer.
	 */
	KASSERT(p->p_cpu == curcpu());
	spc = &p->p_cpu->ci_schedstate;

	/* Start any optional clock interrupts needed by the thread. */
	if (ISSET(p->p_p->ps_flags, PS_ITIMER)) {
		atomic_setbits_int(&spc->spc_schedflags, SPCF_ITIMER);
		clockintr_advance(&spc->spc_itimer, hardclock_period);
	}
	if (ISSET(p->p_p->ps_flags, PS_PROFIL)) {
		atomic_setbits_int(&spc->spc_schedflags, SPCF_PROFCLOCK);
		clockintr_advance(&spc->spc_profclock, profclock_period);
	}

	nanouptime(&spc->spc_runtime);

#ifdef MULTIPROCESSOR
	/*
	 * Reacquire the kernel_lock now.  We do this after we've
	 * released the scheduler lock to avoid deadlock, and before
	 * we reacquire the interlock and the scheduler lock.
	 */
	if (hold_count)
		__mp_acquire_count(&kernel_lock, hold_count);
#endif
}

/*
 * Change process state to be runnable,
 * placing it on the run queue.
 */
void
setrunnable(struct proc *p)
{
	struct process *pr = p->p_p;
	u_char prio;

	SCHED_ASSERT_LOCKED();

	switch (p->p_stat) {
	case 0:
	case SRUN:
	case SONPROC:
	case SDEAD:
	case SIDL:
	default:
		panic("setrunnable");
	case SSTOP:
		prio = p->p_usrpri;
		TRACEPOINT(sched, unstop, p->p_tid + THREAD_PID_OFFSET,
		    p->p_p->ps_pid, CPU_INFO_UNIT(p->p_cpu));

		/* If not yet stopped or asleep, unstop but don't add to runq */
		if (ISSET(p->p_flag, P_INSCHED)) {
			if (p->p_wchan != NULL)
				p->p_stat = SSLEEP;
			else
				p->p_stat = SONPROC;
			return;
		}
		setrunqueue(NULL, p, prio);
		break;
	case SSLEEP:
		prio = p->p_slppri;

		TRACEPOINT(sched, wakeup, p->p_tid + THREAD_PID_OFFSET,
		    p->p_p->ps_pid, CPU_INFO_UNIT(p->p_cpu));
		/* if not yet asleep, don't add to runqueue */
		if (ISSET(p->p_flag, P_INSCHED))
			return;
		setrunqueue(NULL, p, prio);
		break;
	}
	if (p->p_slptime > 1) {
		uint32_t newcpu;

		newcpu = decay_aftersleep(p->p_estcpu, p->p_slptime);
		setpriority(p, newcpu, pr->ps_nice);
	}
	p->p_slptime = 0;
}

/*
 * Compute the priority of a process.
 */
void
setpriority(struct proc *p, uint32_t newcpu, uint8_t nice)
{
	unsigned int newprio;

	newprio = min((PUSER + newcpu + NICE_WEIGHT * (nice - NZERO)), MAXPRI);

	SCHED_ASSERT_LOCKED();
	p->p_estcpu = newcpu;
	p->p_usrpri = newprio;
}

/*
 * We adjust the priority of the current process.  The priority of a process
 * gets worse as it accumulates CPU time.  The cpu usage estimator (p_estcpu)
 * is increased here.  The formula for computing priorities (in kern_synch.c)
 * will compute a different value each time p_estcpu increases. This can
 * cause a switch, but unless the priority crosses a PPQ boundary the actual
 * queue will not change.  The cpu usage estimator ramps up quite quickly
 * when the process is running (linearly), and decays away exponentially, at
 * a rate which is proportionally slower when the system is busy.  The basic
 * principle is that the system will 90% forget that the process used a lot
 * of CPU time in 5 * loadav seconds.  This causes the system to favor
 * processes which haven't run much recently, and to round-robin among other
 * processes.
 */
void
schedclock(struct proc *p)
{
	struct cpu_info *ci = curcpu();
	struct schedstate_percpu *spc = &ci->ci_schedstate;
	uint32_t newcpu;

	if (p == spc->spc_idleproc || spc->spc_spinning)
		return;

	SCHED_LOCK();
	newcpu = ESTCPULIM(p->p_estcpu + 1);
	setpriority(p, newcpu, p->p_p->ps_nice);
	SCHED_UNLOCK();
}

void (*cpu_setperf)(int);

#define PERFPOL_MANUAL 0
#define PERFPOL_AUTO 1
#define PERFPOL_HIGH 2
int perflevel = 100;
int perfpolicy_on_ac = PERFPOL_HIGH;
int perfpolicy_on_battery = PERFPOL_AUTO;

#ifndef SMALL_KERNEL
/*
 * The code below handles CPU throttling.
 */
#include <sys/sysctl.h>

void setperf_auto(void *);
struct timeout setperf_to = TIMEOUT_INITIALIZER(setperf_auto, NULL);
extern int hw_power;

static inline int
perfpolicy_dynamic(void)
{
	return (perfpolicy_on_ac == PERFPOL_AUTO ||
	    perfpolicy_on_battery == PERFPOL_AUTO);
}

static inline int
current_perfpolicy(void)
{
	return (hw_power) ? perfpolicy_on_ac : perfpolicy_on_battery;
}

void
setperf_auto(void *v)
{
	static uint64_t *idleticks, *totalticks;
	static int downbeats;
	int i, j = 0;
	int speedup = 0;
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;
	uint64_t idle, total, allidle = 0, alltotal = 0;
	unsigned int gen;

	if (!perfpolicy_dynamic())
		return;

	if (cpu_setperf == NULL)
		return;

	if (current_perfpolicy() == PERFPOL_HIGH) {
		speedup = 1;
		goto faster;
	}

	if (!idleticks)
		if (!(idleticks = mallocarray(ncpusfound, sizeof(*idleticks),
		    M_DEVBUF, M_NOWAIT | M_ZERO)))
			return;
	if (!totalticks)
		if (!(totalticks = mallocarray(ncpusfound, sizeof(*totalticks),
		    M_DEVBUF, M_NOWAIT | M_ZERO))) {
			free(idleticks, M_DEVBUF,
			    sizeof(*idleticks) * ncpusfound);
			return;
		}
	CPU_INFO_FOREACH(cii, ci) {
		struct schedstate_percpu *spc;

		if (!cpu_is_online(ci))
			continue;

		spc = &ci->ci_schedstate;
		pc_cons_enter(&spc->spc_cp_time_lock, &gen);
		do {
			total = 0;
			for (i = 0; i < CPUSTATES; i++) {
				total += spc->spc_cp_time[i];
			}
			idle = spc->spc_cp_time[CP_IDLE];
		} while (pc_cons_leave(&spc->spc_cp_time_lock, &gen) != 0);

		total -= totalticks[j];
		idle -= idleticks[j];
		if (idle < total / 3)
			speedup = 1;
		alltotal += total;
		allidle += idle;
		idleticks[j] += idle;
		totalticks[j] += total;
		j++;
	}
	if (allidle < alltotal / 2)
		speedup = 1;
	if (speedup && downbeats < 5)
		downbeats++;

	if (speedup && perflevel != 100) {
faster:
		perflevel = 100;
		cpu_setperf(perflevel);
	} else if (!speedup && perflevel != 0 && --downbeats <= 0) {
		perflevel = 0;
		cpu_setperf(perflevel);
	}

	timeout_add_msec(&setperf_to, 100);
}

int
sysctl_hwsetperf(void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
	int err;

	if (!cpu_setperf)
		return EOPNOTSUPP;

	if (perfpolicy_on_ac != PERFPOL_MANUAL)
		return sysctl_rdint(oldp, oldlenp, newp, perflevel);

	err = sysctl_int_bounded(oldp, oldlenp, newp, newlen,
	    &perflevel, 0, 100);
	if (err)
		return err;

	if (newp != NULL)
		cpu_setperf(perflevel);

	return 0;
}

int
sysctl_hwperfpolicy(void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
	char policy[32];
	char *policy_on_battery;
	int err, perfpolicy;

	if (!cpu_setperf)
		return EOPNOTSUPP;

	switch (current_perfpolicy()) {
	case PERFPOL_MANUAL:
		strlcpy(policy, "manual", sizeof(policy));
		break;
	case PERFPOL_AUTO:
		strlcpy(policy, "auto", sizeof(policy));
		break;
	case PERFPOL_HIGH:
		strlcpy(policy, "high", sizeof(policy));
		break;
	default:
		strlcpy(policy, "unknown", sizeof(policy));
		break;
	}

	if (newp == NULL)
		return sysctl_rdstring(oldp, oldlenp, newp, policy);

	err = sysctl_string(oldp, oldlenp, newp, newlen, policy, sizeof(policy));
	if (err)
		return err;

	policy_on_battery = strchr(policy, ',');
	if (policy_on_battery != NULL) {
		*policy_on_battery = '\0';
		policy_on_battery++;
	}

	if (strcmp(policy, "manual") == 0)
		perfpolicy = PERFPOL_MANUAL;
	else if (strcmp(policy, "auto") == 0)
		perfpolicy = PERFPOL_AUTO;
	else if (strcmp(policy, "high") == 0)
		perfpolicy = PERFPOL_HIGH;
	else
		return EINVAL;

	if (policy_on_battery == NULL)
		perfpolicy_on_battery = perfpolicy_on_ac = perfpolicy;
	else {
		if (strcmp(policy_on_battery, "manual") == 0 ||
		    perfpolicy == PERFPOL_MANUAL) {
			/* Not handled */
			return EINVAL;
		}
		if (strcmp(policy_on_battery, "auto") == 0)
			perfpolicy_on_battery = PERFPOL_AUTO;
		else if (strcmp(policy_on_battery, "high") == 0)
			perfpolicy_on_battery = PERFPOL_HIGH;
		else
			return EINVAL;
		perfpolicy_on_ac = perfpolicy;
	}

	if (current_perfpolicy() == PERFPOL_HIGH) {
		perflevel = 100;
		cpu_setperf(perflevel);
	}

	if (perfpolicy_dynamic())
		timeout_add_msec(&setperf_to, 200);

	return 0;
}
#endif

/*
 * Start the scheduler's periodic timeouts.
 */
void
scheduler_start(void)
{
	schedcpu(NULL);
	update_loadavg(NULL);

#ifndef SMALL_KERNEL
	if (perfpolicy_dynamic())
		timeout_add_msec(&setperf_to, 200);
#endif
}

