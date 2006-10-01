/*
 * tsacct.c - System accounting over taskstats interface
 *
 * Copyright (C) Jay Lan,	<jlan@sgi.com>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/tsacct_kern.h>
#include <linux/acct.h>


#define USEC_PER_TICK	(USEC_PER_SEC/HZ)
/*
 * fill in basic accounting fields
 */
void bacct_add_tsk(struct taskstats *stats, struct task_struct *tsk)
{
	struct timespec uptime, ts;
	s64 ac_etime;

	BUILD_BUG_ON(TS_COMM_LEN < TASK_COMM_LEN);

	/* calculate task elapsed time in timespec */
	do_posix_clock_monotonic_gettime(&uptime);
	ts = timespec_sub(uptime, current->group_leader->start_time);
	/* rebase elapsed time to usec */
	ac_etime = timespec_to_ns(&ts);
	do_div(ac_etime, NSEC_PER_USEC);
	stats->ac_etime = ac_etime;
	stats->ac_btime = xtime.tv_sec - ts.tv_sec;
	if (thread_group_leader(tsk)) {
		stats->ac_exitcode = tsk->exit_code;
		if (tsk->flags & PF_FORKNOEXEC)
			stats->ac_flag |= AFORK;
	}
	if (tsk->flags & PF_SUPERPRIV)
		stats->ac_flag |= ASU;
	if (tsk->flags & PF_DUMPCORE)
		stats->ac_flag |= ACORE;
	if (tsk->flags & PF_SIGNALED)
		stats->ac_flag |= AXSIG;
	stats->ac_nice	 = task_nice(tsk);
	stats->ac_sched	 = tsk->policy;
	stats->ac_uid	 = tsk->uid;
	stats->ac_gid	 = tsk->gid;
	stats->ac_pid	 = tsk->pid;
	stats->ac_ppid	 = (tsk->parent) ? tsk->parent->pid : 0;
	stats->ac_utime	 = cputime_to_msecs(tsk->utime) * USEC_PER_MSEC;
	stats->ac_stime	 = cputime_to_msecs(tsk->stime) * USEC_PER_MSEC;
	stats->ac_minflt = tsk->min_flt;
	stats->ac_majflt = tsk->maj_flt;
	/* Each process gets a minimum of one usec cpu time */
	if ((stats->ac_utime == 0) && (stats->ac_stime == 0)) {
		stats->ac_stime = 1;
	}

	strncpy(stats->ac_comm, tsk->comm, sizeof(stats->ac_comm));
}


#ifdef CONFIG_TASK_XACCT
/*
 * fill in extended accounting fields
 */
void xacct_add_tsk(struct taskstats *stats, struct task_struct *p)
{
	stats->acct_rss_mem1 = p->acct_rss_mem1;
	stats->acct_vm_mem1  = p->acct_vm_mem1;
	if (p->mm) {
		stats->hiwater_rss   = p->mm->hiwater_rss;
		stats->hiwater_vm    = p->mm->hiwater_vm;
	}
	stats->read_char	= p->rchar;
	stats->write_char	= p->wchar;
	stats->read_syscalls	= p->syscr;
	stats->write_syscalls	= p->syscw;
}
#endif
