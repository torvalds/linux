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
#include <linux/jiffies.h>
#include <linux/mm.h>

/*
 * fill in basic accounting fields
 */
void bacct_add_tsk(struct user_namespace *user_ns,
		   struct pid_namespace *pid_ns,
		   struct taskstats *stats, struct task_struct *tsk)
{
	const struct cred *tcred;
	struct timespec uptime, ts;
	cputime_t utime, stime, utimescaled, stimescaled;
	u64 ac_etime;

	BUILD_BUG_ON(TS_COMM_LEN < TASK_COMM_LEN);

	/* calculate task elapsed time in timespec */
	do_posix_clock_monotonic_gettime(&uptime);
	ts = timespec_sub(uptime, tsk->start_time);
	/* rebase elapsed time to usec (should never be negative) */
	ac_etime = timespec_to_ns(&ts);
	do_div(ac_etime, NSEC_PER_USEC);
	stats->ac_etime = ac_etime;
	stats->ac_btime = get_seconds() - ts.tv_sec;
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
	stats->ac_pid	 = task_pid_nr_ns(tsk, pid_ns);
	rcu_read_lock();
	tcred = __task_cred(tsk);
	stats->ac_uid	 = from_kuid_munged(user_ns, tcred->uid);
	stats->ac_gid	 = from_kgid_munged(user_ns, tcred->gid);
	stats->ac_ppid	 = pid_alive(tsk) ?
		task_tgid_nr_ns(rcu_dereference(tsk->real_parent), pid_ns) : 0;
	rcu_read_unlock();

	task_cputime(tsk, &utime, &stime);
	stats->ac_utime = cputime_to_usecs(utime);
	stats->ac_stime = cputime_to_usecs(stime);

	task_cputime_scaled(tsk, &utimescaled, &stimescaled);
	stats->ac_utimescaled = cputime_to_usecs(utimescaled);
	stats->ac_stimescaled = cputime_to_usecs(stimescaled);

	stats->ac_minflt = tsk->min_flt;
	stats->ac_majflt = tsk->maj_flt;

	strncpy(stats->ac_comm, tsk->comm, sizeof(stats->ac_comm));
}


#ifdef CONFIG_TASK_XACCT

#define KB 1024
#define MB (1024*KB)
#define KB_MASK (~(KB-1))
/*
 * fill in extended accounting fields
 */
void xacct_add_tsk(struct taskstats *stats, struct task_struct *p)
{
	struct mm_struct *mm;

	/* convert pages-usec to Mbyte-usec */
	stats->coremem = p->acct_rss_mem1 * PAGE_SIZE / MB;
	stats->virtmem = p->acct_vm_mem1 * PAGE_SIZE / MB;
	mm = get_task_mm(p);
	if (mm) {
		/* adjust to KB unit */
		stats->hiwater_rss   = get_mm_hiwater_rss(mm) * PAGE_SIZE / KB;
		stats->hiwater_vm    = get_mm_hiwater_vm(mm)  * PAGE_SIZE / KB;
		mmput(mm);
	}
	stats->read_char	= p->ioac.rchar & KB_MASK;
	stats->write_char	= p->ioac.wchar & KB_MASK;
	stats->read_syscalls	= p->ioac.syscr & KB_MASK;
	stats->write_syscalls	= p->ioac.syscw & KB_MASK;
#ifdef CONFIG_TASK_IO_ACCOUNTING
	stats->read_bytes	= p->ioac.read_bytes & KB_MASK;
	stats->write_bytes	= p->ioac.write_bytes & KB_MASK;
	stats->cancelled_write_bytes = p->ioac.cancelled_write_bytes & KB_MASK;
#else
	stats->read_bytes	= 0;
	stats->write_bytes	= 0;
	stats->cancelled_write_bytes = 0;
#endif
}
#undef KB
#undef MB

static void __acct_update_integrals(struct task_struct *tsk,
				    cputime_t utime, cputime_t stime)
{
	if (likely(tsk->mm)) {
		cputime_t time, dtime;
		struct timeval value;
		unsigned long flags;
		u64 delta;

		local_irq_save(flags);
		time = stime + utime;
		dtime = time - tsk->acct_timexpd;
		jiffies_to_timeval(cputime_to_jiffies(dtime), &value);
		delta = value.tv_sec;
		delta = delta * USEC_PER_SEC + value.tv_usec;

		if (delta == 0)
			goto out;
		tsk->acct_timexpd = time;
		tsk->acct_rss_mem1 += delta * get_mm_rss(tsk->mm);
		tsk->acct_vm_mem1 += delta * tsk->mm->total_vm;
	out:
		local_irq_restore(flags);
	}
}

/**
 * acct_update_integrals - update mm integral fields in task_struct
 * @tsk: task_struct for accounting
 */
void acct_update_integrals(struct task_struct *tsk)
{
	cputime_t utime, stime;

	task_cputime(tsk, &utime, &stime);
	__acct_update_integrals(tsk, utime, stime);
}

/**
 * acct_account_cputime - update mm integral after cputime update
 * @tsk: task_struct for accounting
 */
void acct_account_cputime(struct task_struct *tsk)
{
	__acct_update_integrals(tsk, tsk->utime, tsk->stime);
}

/**
 * acct_clear_integrals - clear the mm integral fields in task_struct
 * @tsk: task_struct whose accounting fields are cleared
 */
void acct_clear_integrals(struct task_struct *tsk)
{
	tsk->acct_timexpd = 0;
	tsk->acct_rss_mem1 = 0;
	tsk->acct_vm_mem1 = 0;
}
#endif
