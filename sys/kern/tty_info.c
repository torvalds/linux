/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Copyright (c) 2002 Networks Associates Technologies, Inc.
 * All rights reserved.
 *
 * Portions of this software were developed for the FreeBSD Project by
 * ThinkSec AS and NAI Labs, the Security Research Division of Network
 * Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
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

#include "opt_stack.h"

#include <sys/param.h>
#include <sys/cons.h>
#include <sys/kdb.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/resourcevar.h>
#include <sys/sbuf.h>
#include <sys/sched.h>
#include <sys/stack.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/tty.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>

/*
 * Returns 1 if p2 is "better" than p1
 *
 * The algorithm for picking the "interesting" process is thus:
 *
 *	1) Only foreground processes are eligible - implied.
 *	2) Runnable processes are favored over anything else.  The runner
 *	   with the highest cpu utilization is picked (p_estcpu).  Ties are
 *	   broken by picking the highest pid.
 *	3) The sleeper with the shortest sleep time is next.  With ties,
 *	   we pick out just "short-term" sleepers (P_SINTR == 0).
 *	4) Further ties are broken by picking the highest pid.
 */

#define TESTAB(a, b)    ((a)<<1 | (b))
#define ONLYA   2
#define ONLYB   1
#define BOTH    3

static int
proc_sum(struct proc *p, fixpt_t *estcpup)
{
	struct thread *td;
	int estcpu;
	int val;

	val = 0;
	estcpu = 0;
	FOREACH_THREAD_IN_PROC(p, td) {
		thread_lock(td);
		if (TD_ON_RUNQ(td) ||
		    TD_IS_RUNNING(td))
			val = 1;
		estcpu += sched_pctcpu(td);
		thread_unlock(td);
	}
	*estcpup = estcpu;

	return (val);
}

static int
thread_compare(struct thread *td, struct thread *td2)
{
	int runa, runb;
	int slpa, slpb;
	fixpt_t esta, estb;

	if (td == NULL)
		return (1);

	/*
	 * Fetch running stats, pctcpu usage, and interruptable flag.
	 */
	thread_lock(td);
	runa = TD_IS_RUNNING(td) | TD_ON_RUNQ(td);
	slpa = td->td_flags & TDF_SINTR;
	esta = sched_pctcpu(td);
	thread_unlock(td);
	thread_lock(td2);
	runb = TD_IS_RUNNING(td2) | TD_ON_RUNQ(td2);
	estb = sched_pctcpu(td2);
	slpb = td2->td_flags & TDF_SINTR;
	thread_unlock(td2);
	/*
	 * see if at least one of them is runnable
	 */
	switch (TESTAB(runa, runb)) {
	case ONLYA:
		return (0);
	case ONLYB:
		return (1);
	case BOTH:
		break;
	}
	/*
	 *  favor one with highest recent cpu utilization
	 */
	if (estb > esta)
		return (1);
	if (esta > estb)
		return (0);
	/*
	 * favor one sleeping in a non-interruptible sleep
	 */
	switch (TESTAB(slpa, slpb)) {
	case ONLYA:
		return (0);
	case ONLYB:
		return (1);
	case BOTH:
		break;
	}

	return (td < td2);
}

static int
proc_compare(struct proc *p1, struct proc *p2)
{

	int runa, runb;
	fixpt_t esta, estb;

	if (p1 == NULL)
		return (1);

	/*
	 * Fetch various stats about these processes.  After we drop the
	 * lock the information could be stale but the race is unimportant.
	 */
	PROC_LOCK(p1);
	runa = proc_sum(p1, &esta);
	PROC_UNLOCK(p1);
	PROC_LOCK(p2);
	runb = proc_sum(p2, &estb);
	PROC_UNLOCK(p2);

	/*
	 * see if at least one of them is runnable
	 */
	switch (TESTAB(runa, runb)) {
	case ONLYA:
		return (0);
	case ONLYB:
		return (1);
	case BOTH:
		break;
	}
	/*
	 *  favor one with highest recent cpu utilization
	 */
	if (estb > esta)
		return (1);
	if (esta > estb)
		return (0);
	/*
	 * weed out zombies
	 */
	switch (TESTAB(p1->p_state == PRS_ZOMBIE, p2->p_state == PRS_ZOMBIE)) {
	case ONLYA:
		return (1);
	case ONLYB:
		return (0);
	case BOTH:
		break;
	}

	return (p2->p_pid > p1->p_pid);		/* tie - return highest pid */
}

static int
sbuf_tty_drain(void *a, const char *d, int len)
{
	struct tty *tp;
	int rc;

	tp = a;

	if (kdb_active) {
		cnputsn(d, len);
		return (len);
	}
	if (tp != NULL && panicstr == NULL) {
		rc = tty_putstrn(tp, d, len);
		if (rc != 0)
			return (-ENXIO);
		return (len);
	}
	return (-ENXIO);
}

#ifdef STACK
static bool tty_info_kstacks = false;
SYSCTL_BOOL(_kern, OID_AUTO, tty_info_kstacks, CTLFLAG_RWTUN,
    &tty_info_kstacks, 0,
    "Enable printing kernel stack(9) traces on ^T (tty info)");
#endif

/*
 * Report on state of foreground process group.
 */
void
tty_info(struct tty *tp)
{
	struct timeval rtime, utime, stime;
#ifdef STACK
	struct stack stack;
	int sterr;
#endif
	struct proc *p, *ppick;
	struct thread *td, *tdpick;
	const char *stateprefix, *state;
	struct sbuf sb;
	long rss;
	int load, pctcpu;
	pid_t pid;
	char comm[MAXCOMLEN + 1];
	struct rusage ru;

	tty_lock_assert(tp, MA_OWNED);

	if (tty_checkoutq(tp) == 0)
		return;

	(void)sbuf_new(&sb, tp->t_prbuf, tp->t_prbufsz, SBUF_FIXEDLEN);
	sbuf_set_drain(&sb, sbuf_tty_drain, tp);

	/* Print load average. */
	load = (averunnable.ldavg[0] * 100 + FSCALE / 2) >> FSHIFT;
	sbuf_printf(&sb, "%sload: %d.%02d ", tp->t_column == 0 ? "" : "\n",
	    load / 100, load % 100);

	if (tp->t_session == NULL) {
		sbuf_printf(&sb, "not a controlling terminal\n");
		goto out;
	}
	if (tp->t_pgrp == NULL) {
		sbuf_printf(&sb, "no foreground process group\n");
		goto out;
	}
	PGRP_LOCK(tp->t_pgrp);
	if (LIST_EMPTY(&tp->t_pgrp->pg_members)) {
		PGRP_UNLOCK(tp->t_pgrp);
		sbuf_printf(&sb, "empty foreground process group\n");
		goto out;
	}

	/*
	 * Pick the most interesting process and copy some of its
	 * state for printing later.  This operation could rely on stale
	 * data as we can't hold the proc slock or thread locks over the
	 * whole list. However, we're guaranteed not to reference an exited
	 * thread or proc since we hold the tty locked.
	 */
	p = NULL;
	LIST_FOREACH(ppick, &tp->t_pgrp->pg_members, p_pglist)
		if (proc_compare(p, ppick))
			p = ppick;

	PROC_LOCK(p);
	PGRP_UNLOCK(tp->t_pgrp);
	td = NULL;
	FOREACH_THREAD_IN_PROC(p, tdpick)
		if (thread_compare(td, tdpick))
			td = tdpick;
	stateprefix = "";
	thread_lock(td);
	if (TD_IS_RUNNING(td))
		state = "running";
	else if (TD_ON_RUNQ(td) || TD_CAN_RUN(td))
		state = "runnable";
	else if (TD_IS_SLEEPING(td)) {
		/* XXX: If we're sleeping, are we ever not in a queue? */
		if (TD_ON_SLEEPQ(td))
			state = td->td_wmesg;
		else
			state = "sleeping without queue";
	} else if (TD_ON_LOCK(td)) {
		state = td->td_lockname;
		stateprefix = "*";
	} else if (TD_IS_SUSPENDED(td))
		state = "suspended";
	else if (TD_AWAITING_INTR(td))
		state = "intrwait";
	else if (p->p_state == PRS_ZOMBIE)
		state = "zombie";
	else
		state = "unknown";
	pctcpu = (sched_pctcpu(td) * 10000 + FSCALE / 2) >> FSHIFT;
#ifdef STACK
	if (tty_info_kstacks) {
		stack_zero(&stack);
		if (TD_IS_SWAPPED(td) || TD_IS_RUNNING(td))
			sterr = stack_save_td_running(&stack, td);
		else {
			stack_save_td(&stack, td);
			sterr = 0;
		}
	}
#endif
	thread_unlock(td);
	if (p->p_state == PRS_NEW || p->p_state == PRS_ZOMBIE)
		rss = 0;
	else
		rss = pgtok(vmspace_resident_count(p->p_vmspace));
	microuptime(&rtime);
	timevalsub(&rtime, &p->p_stats->p_start);
	rufetchcalc(p, &ru, &utime, &stime);
	pid = p->p_pid;
	strlcpy(comm, p->p_comm, sizeof comm);
	PROC_UNLOCK(p);

	/* Print command, pid, state, rtime, utime, stime, %cpu, and rss. */
	sbuf_printf(&sb,
	    " cmd: %s %d [%s%s] %ld.%02ldr %ld.%02ldu %ld.%02lds %d%% %ldk\n",
	    comm, pid, stateprefix, state,
	    (long)rtime.tv_sec, rtime.tv_usec / 10000,
	    (long)utime.tv_sec, utime.tv_usec / 10000,
	    (long)stime.tv_sec, stime.tv_usec / 10000,
	    pctcpu / 100, rss);

#ifdef STACK
	if (tty_info_kstacks && sterr == 0)
		stack_sbuf_print_flags(&sb, &stack, M_NOWAIT);
#endif

out:
	sbuf_finish(&sb);
	sbuf_delete(&sb);
}
