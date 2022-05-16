// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * tsacct.c - System accounting over taskstats interface
 *
 * Copyright (C) Jay Lan,	<jlan@sgi.com>
 */

#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/sched/cputime.h>
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
	u64 utime, stime, utimescaled, stimescaled;
	u64 delta;
	time64_t btime;

	BUILD_BUG_ON(TS_COMM_LEN < TASK_COMM_LEN);

	/* calculate task elapsed time in nsec */
	delta = ktime_get_ns() - tsk->start_time;
	/* Convert to micro seconds */
	do_div(delta, NSEC_PER_USEC);
	stats->ac_etime = delta;
	/* Convert to seconds for btime (note y2106 limit) */
	btime = ktime_get_real_seconds() - div_u64(delta, USEC_PER_SEC);
	stats->ac_btime = clamp_t(time64_t, btime, 0, U32_MAX);
	stats->ac_btime64 = btime;

	if (tsk->flags & PF_EXITING)
		stats->ac_exitcode = tsk->exit_code;
	if (thread_group_leader(tsk) && (tsk->flags & PF_FORKNOEXEC))
		stats->ac_flag |= AFORK;
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
	stats->ac_utime = div_u64(utime, NSEC_PER_USEC);
	stats->ac_stime = div_u64(stime, NSEC_PER_USEC);

	task_cputime_scaled(tsk, &utimescaled, &stimescaled);
	stats->ac_utimescaled = div_u64(utimescaled, NSEC_PER_USEC);
	stats->ac_stimescaled = div_u64(stimescaled, NSEC_PER_USEC);

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

	/* convert pages-nsec/1024 to Mbyte-usec, see __acct_update_integrals */
	stats->coremem = p->acct_rss_mem1 * PAGE_SIZE;
	do_div(stats->coremem, 1000 * KB);
	stats->virtmem = p->acct_vm_mem1 * PAGE_SIZE;
	do_div(stats->virtmem, 1000 * KB);
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
				    u64 utime, u64 stime)
{
	u64 time, delta;

	if (!likely(tsk->mm))
		return;

	time = stime + utime;
	delta = time - tsk->acct_timexpd;

	if (delta < TICK_NSEC)
		return;

	tsk->acct_timexpd = time;
	/*
	 * Divide by 1024 to avoid overflow, and to avoid division.
	 * The final unit reported to userspace is Mbyte-usecs,
	 * the rest of the math is done in xacct_add_tsk.
	 */
	tsk->acct_rss_mem1 += delta * get_mm_rss(tsk->mm) >> 10;
	tsk->acct_vm_mem1 += delta * tsk->mm->total_vm >> 10;
}

/**
 * acct_update_integrals - update mm integral fields in task_struct
 * @tsk: task_struct for accounting
 */
void acct_update_integrals(struct task_struct *tsk)
{
	u64 utime, stime;
	unsigned long flags;

	local_irq_save(flags);
	task_cputime(tsk, &utime, &stime);
	__acct_update_integrals(tsk, utime, stime);
	local_irq_restore(flags);
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
