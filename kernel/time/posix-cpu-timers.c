// SPDX-License-Identifier: GPL-2.0
/*
 * Implement CPU time clocks for the POSIX clock interface.
 */

#include <linux/sched/signal.h>
#include <linux/sched/cputime.h>
#include <linux/posix-timers.h>
#include <linux/errno.h>
#include <linux/math64.h>
#include <linux/uaccess.h>
#include <linux/kernel_stat.h>
#include <trace/events/timer.h>
#include <linux/tick.h>
#include <linux/workqueue.h>
#include <linux/compat.h>
#include <linux/sched/deadline.h>

#include "posix-timers.h"

static void posix_cpu_timer_rearm(struct k_itimer *timer);

/*
 * Called after updating RLIMIT_CPU to run cpu timer and update
 * tsk->signal->cputime_expires expiration cache if necessary. Needs
 * siglock protection since other code may update expiration cache as
 * well.
 */
void update_rlimit_cpu(struct task_struct *task, unsigned long rlim_new)
{
	u64 nsecs = rlim_new * NSEC_PER_SEC;

	spin_lock_irq(&task->sighand->siglock);
	set_process_cpu_timer(task, CPUCLOCK_PROF, &nsecs, NULL);
	spin_unlock_irq(&task->sighand->siglock);
}

static int check_clock(const clockid_t which_clock)
{
	int error = 0;
	struct task_struct *p;
	const pid_t pid = CPUCLOCK_PID(which_clock);

	if (CPUCLOCK_WHICH(which_clock) >= CPUCLOCK_MAX)
		return -EINVAL;

	if (pid == 0)
		return 0;

	rcu_read_lock();
	p = find_task_by_vpid(pid);
	if (!p || !(CPUCLOCK_PERTHREAD(which_clock) ?
		   same_thread_group(p, current) : has_group_leader_pid(p))) {
		error = -EINVAL;
	}
	rcu_read_unlock();

	return error;
}

/*
 * Update expiry time from increment, and increase overrun count,
 * given the current clock sample.
 */
static void bump_cpu_timer(struct k_itimer *timer, u64 now)
{
	int i;
	u64 delta, incr;

	if (timer->it.cpu.incr == 0)
		return;

	if (now < timer->it.cpu.expires)
		return;

	incr = timer->it.cpu.incr;
	delta = now + incr - timer->it.cpu.expires;

	/* Don't use (incr*2 < delta), incr*2 might overflow. */
	for (i = 0; incr < delta - incr; i++)
		incr = incr << 1;

	for (; i >= 0; incr >>= 1, i--) {
		if (delta < incr)
			continue;

		timer->it.cpu.expires += incr;
		timer->it_overrun += 1LL << i;
		delta -= incr;
	}
}

/**
 * task_cputime_zero - Check a task_cputime struct for all zero fields.
 *
 * @cputime:	The struct to compare.
 *
 * Checks @cputime to see if all fields are zero.  Returns true if all fields
 * are zero, false if any field is nonzero.
 */
static inline int task_cputime_zero(const struct task_cputime *cputime)
{
	if (!cputime->utime && !cputime->stime && !cputime->sum_exec_runtime)
		return 1;
	return 0;
}

static inline u64 prof_ticks(struct task_struct *p)
{
	u64 utime, stime;

	task_cputime(p, &utime, &stime);

	return utime + stime;
}
static inline u64 virt_ticks(struct task_struct *p)
{
	u64 utime, stime;

	task_cputime(p, &utime, &stime);

	return utime;
}

static int
posix_cpu_clock_getres(const clockid_t which_clock, struct timespec64 *tp)
{
	int error = check_clock(which_clock);
	if (!error) {
		tp->tv_sec = 0;
		tp->tv_nsec = ((NSEC_PER_SEC + HZ - 1) / HZ);
		if (CPUCLOCK_WHICH(which_clock) == CPUCLOCK_SCHED) {
			/*
			 * If sched_clock is using a cycle counter, we
			 * don't have any idea of its true resolution
			 * exported, but it is much more than 1s/HZ.
			 */
			tp->tv_nsec = 1;
		}
	}
	return error;
}

static int
posix_cpu_clock_set(const clockid_t which_clock, const struct timespec64 *tp)
{
	/*
	 * You can never reset a CPU clock, but we check for other errors
	 * in the call before failing with EPERM.
	 */
	int error = check_clock(which_clock);
	if (error == 0) {
		error = -EPERM;
	}
	return error;
}


/*
 * Sample a per-thread clock for the given task.
 */
static int cpu_clock_sample(const clockid_t which_clock,
			    struct task_struct *p, u64 *sample)
{
	switch (CPUCLOCK_WHICH(which_clock)) {
	default:
		return -EINVAL;
	case CPUCLOCK_PROF:
		*sample = prof_ticks(p);
		break;
	case CPUCLOCK_VIRT:
		*sample = virt_ticks(p);
		break;
	case CPUCLOCK_SCHED:
		*sample = task_sched_runtime(p);
		break;
	}
	return 0;
}

/*
 * Set cputime to sum_cputime if sum_cputime > cputime. Use cmpxchg
 * to avoid race conditions with concurrent updates to cputime.
 */
static inline void __update_gt_cputime(atomic64_t *cputime, u64 sum_cputime)
{
	u64 curr_cputime;
retry:
	curr_cputime = atomic64_read(cputime);
	if (sum_cputime > curr_cputime) {
		if (atomic64_cmpxchg(cputime, curr_cputime, sum_cputime) != curr_cputime)
			goto retry;
	}
}

static void update_gt_cputime(struct task_cputime_atomic *cputime_atomic, struct task_cputime *sum)
{
	__update_gt_cputime(&cputime_atomic->utime, sum->utime);
	__update_gt_cputime(&cputime_atomic->stime, sum->stime);
	__update_gt_cputime(&cputime_atomic->sum_exec_runtime, sum->sum_exec_runtime);
}

/* Sample task_cputime_atomic values in "atomic_timers", store results in "times". */
static inline void sample_cputime_atomic(struct task_cputime *times,
					 struct task_cputime_atomic *atomic_times)
{
	times->utime = atomic64_read(&atomic_times->utime);
	times->stime = atomic64_read(&atomic_times->stime);
	times->sum_exec_runtime = atomic64_read(&atomic_times->sum_exec_runtime);
}

void thread_group_cputimer(struct task_struct *tsk, struct task_cputime *times)
{
	struct thread_group_cputimer *cputimer = &tsk->signal->cputimer;
	struct task_cputime sum;

	/* Check if cputimer isn't running. This is accessed without locking. */
	if (!READ_ONCE(cputimer->running)) {
		/*
		 * The POSIX timer interface allows for absolute time expiry
		 * values through the TIMER_ABSTIME flag, therefore we have
		 * to synchronize the timer to the clock every time we start it.
		 */
		thread_group_cputime(tsk, &sum);
		update_gt_cputime(&cputimer->cputime_atomic, &sum);

		/*
		 * We're setting cputimer->running without a lock. Ensure
		 * this only gets written to in one operation. We set
		 * running after update_gt_cputime() as a small optimization,
		 * but barriers are not required because update_gt_cputime()
		 * can handle concurrent updates.
		 */
		WRITE_ONCE(cputimer->running, true);
	}
	sample_cputime_atomic(times, &cputimer->cputime_atomic);
}

/*
 * Sample a process (thread group) clock for the given group_leader task.
 * Must be called with task sighand lock held for safe while_each_thread()
 * traversal.
 */
static int cpu_clock_sample_group(const clockid_t which_clock,
				  struct task_struct *p,
				  u64 *sample)
{
	struct task_cputime cputime;

	switch (CPUCLOCK_WHICH(which_clock)) {
	default:
		return -EINVAL;
	case CPUCLOCK_PROF:
		thread_group_cputime(p, &cputime);
		*sample = cputime.utime + cputime.stime;
		break;
	case CPUCLOCK_VIRT:
		thread_group_cputime(p, &cputime);
		*sample = cputime.utime;
		break;
	case CPUCLOCK_SCHED:
		thread_group_cputime(p, &cputime);
		*sample = cputime.sum_exec_runtime;
		break;
	}
	return 0;
}

static int posix_cpu_clock_get_task(struct task_struct *tsk,
				    const clockid_t which_clock,
				    struct timespec64 *tp)
{
	int err = -EINVAL;
	u64 rtn;

	if (CPUCLOCK_PERTHREAD(which_clock)) {
		if (same_thread_group(tsk, current))
			err = cpu_clock_sample(which_clock, tsk, &rtn);
	} else {
		if (tsk == current || thread_group_leader(tsk))
			err = cpu_clock_sample_group(which_clock, tsk, &rtn);
	}

	if (!err)
		*tp = ns_to_timespec64(rtn);

	return err;
}


static int posix_cpu_clock_get(const clockid_t which_clock, struct timespec64 *tp)
{
	const pid_t pid = CPUCLOCK_PID(which_clock);
	int err = -EINVAL;

	if (pid == 0) {
		/*
		 * Special case constant value for our own clocks.
		 * We don't have to do any lookup to find ourselves.
		 */
		err = posix_cpu_clock_get_task(current, which_clock, tp);
	} else {
		/*
		 * Find the given PID, and validate that the caller
		 * should be able to see it.
		 */
		struct task_struct *p;
		rcu_read_lock();
		p = find_task_by_vpid(pid);
		if (p)
			err = posix_cpu_clock_get_task(p, which_clock, tp);
		rcu_read_unlock();
	}

	return err;
}

/*
 * Validate the clockid_t for a new CPU-clock timer, and initialize the timer.
 * This is called from sys_timer_create() and do_cpu_nanosleep() with the
 * new timer already all-zeros initialized.
 */
static int posix_cpu_timer_create(struct k_itimer *new_timer)
{
	int ret = 0;
	const pid_t pid = CPUCLOCK_PID(new_timer->it_clock);
	struct task_struct *p;

	if (CPUCLOCK_WHICH(new_timer->it_clock) >= CPUCLOCK_MAX)
		return -EINVAL;

	new_timer->kclock = &clock_posix_cpu;

	INIT_LIST_HEAD(&new_timer->it.cpu.entry);

	rcu_read_lock();
	if (CPUCLOCK_PERTHREAD(new_timer->it_clock)) {
		if (pid == 0) {
			p = current;
		} else {
			p = find_task_by_vpid(pid);
			if (p && !same_thread_group(p, current))
				p = NULL;
		}
	} else {
		if (pid == 0) {
			p = current->group_leader;
		} else {
			p = find_task_by_vpid(pid);
			if (p && !has_group_leader_pid(p))
				p = NULL;
		}
	}
	new_timer->it.cpu.task = p;
	if (p) {
		get_task_struct(p);
	} else {
		ret = -EINVAL;
	}
	rcu_read_unlock();

	return ret;
}

/*
 * Clean up a CPU-clock timer that is about to be destroyed.
 * This is called from timer deletion with the timer already locked.
 * If we return TIMER_RETRY, it's necessary to release the timer's lock
 * and try again.  (This happens when the timer is in the middle of firing.)
 */
static int posix_cpu_timer_del(struct k_itimer *timer)
{
	int ret = 0;
	unsigned long flags;
	struct sighand_struct *sighand;
	struct task_struct *p = timer->it.cpu.task;

	WARN_ON_ONCE(p == NULL);

	/*
	 * Protect against sighand release/switch in exit/exec and process/
	 * thread timer list entry concurrent read/writes.
	 */
	sighand = lock_task_sighand(p, &flags);
	if (unlikely(sighand == NULL)) {
		/*
		 * We raced with the reaping of the task.
		 * The deletion should have cleared us off the list.
		 */
		WARN_ON_ONCE(!list_empty(&timer->it.cpu.entry));
	} else {
		if (timer->it.cpu.firing)
			ret = TIMER_RETRY;
		else
			list_del(&timer->it.cpu.entry);

		unlock_task_sighand(p, &flags);
	}

	if (!ret)
		put_task_struct(p);

	return ret;
}

static void cleanup_timers_list(struct list_head *head)
{
	struct cpu_timer_list *timer, *next;

	list_for_each_entry_safe(timer, next, head, entry)
		list_del_init(&timer->entry);
}

/*
 * Clean out CPU timers still ticking when a thread exited.  The task
 * pointer is cleared, and the expiry time is replaced with the residual
 * time for later timer_gettime calls to return.
 * This must be called with the siglock held.
 */
static void cleanup_timers(struct list_head *head)
{
	cleanup_timers_list(head);
	cleanup_timers_list(++head);
	cleanup_timers_list(++head);
}

/*
 * These are both called with the siglock held, when the current thread
 * is being reaped.  When the final (leader) thread in the group is reaped,
 * posix_cpu_timers_exit_group will be called after posix_cpu_timers_exit.
 */
void posix_cpu_timers_exit(struct task_struct *tsk)
{
	cleanup_timers(tsk->cpu_timers);
}
void posix_cpu_timers_exit_group(struct task_struct *tsk)
{
	cleanup_timers(tsk->signal->cpu_timers);
}

static inline int expires_gt(u64 expires, u64 new_exp)
{
	return expires == 0 || expires > new_exp;
}

/*
 * Insert the timer on the appropriate list before any timers that
 * expire later.  This must be called with the sighand lock held.
 */
static void arm_timer(struct k_itimer *timer)
{
	struct task_struct *p = timer->it.cpu.task;
	struct list_head *head, *listpos;
	struct task_cputime *cputime_expires;
	struct cpu_timer_list *const nt = &timer->it.cpu;
	struct cpu_timer_list *next;

	if (CPUCLOCK_PERTHREAD(timer->it_clock)) {
		head = p->cpu_timers;
		cputime_expires = &p->cputime_expires;
	} else {
		head = p->signal->cpu_timers;
		cputime_expires = &p->signal->cputime_expires;
	}
	head += CPUCLOCK_WHICH(timer->it_clock);

	listpos = head;
	list_for_each_entry(next, head, entry) {
		if (nt->expires < next->expires)
			break;
		listpos = &next->entry;
	}
	list_add(&nt->entry, listpos);

	if (listpos == head) {
		u64 exp = nt->expires;

		/*
		 * We are the new earliest-expiring POSIX 1.b timer, hence
		 * need to update expiration cache. Take into account that
		 * for process timers we share expiration cache with itimers
		 * and RLIMIT_CPU and for thread timers with RLIMIT_RTTIME.
		 */

		switch (CPUCLOCK_WHICH(timer->it_clock)) {
		case CPUCLOCK_PROF:
			if (expires_gt(cputime_expires->prof_exp, exp))
				cputime_expires->prof_exp = exp;
			break;
		case CPUCLOCK_VIRT:
			if (expires_gt(cputime_expires->virt_exp, exp))
				cputime_expires->virt_exp = exp;
			break;
		case CPUCLOCK_SCHED:
			if (expires_gt(cputime_expires->sched_exp, exp))
				cputime_expires->sched_exp = exp;
			break;
		}
		if (CPUCLOCK_PERTHREAD(timer->it_clock))
			tick_dep_set_task(p, TICK_DEP_BIT_POSIX_TIMER);
		else
			tick_dep_set_signal(p->signal, TICK_DEP_BIT_POSIX_TIMER);
	}
}

/*
 * The timer is locked, fire it and arrange for its reload.
 */
static void cpu_timer_fire(struct k_itimer *timer)
{
	if ((timer->it_sigev_notify & ~SIGEV_THREAD_ID) == SIGEV_NONE) {
		/*
		 * User don't want any signal.
		 */
		timer->it.cpu.expires = 0;
	} else if (unlikely(timer->sigq == NULL)) {
		/*
		 * This a special case for clock_nanosleep,
		 * not a normal timer from sys_timer_create.
		 */
		wake_up_process(timer->it_process);
		timer->it.cpu.expires = 0;
	} else if (timer->it.cpu.incr == 0) {
		/*
		 * One-shot timer.  Clear it as soon as it's fired.
		 */
		posix_timer_event(timer, 0);
		timer->it.cpu.expires = 0;
	} else if (posix_timer_event(timer, ++timer->it_requeue_pending)) {
		/*
		 * The signal did not get queued because the signal
		 * was ignored, so we won't get any callback to
		 * reload the timer.  But we need to keep it
		 * ticking in case the signal is deliverable next time.
		 */
		posix_cpu_timer_rearm(timer);
		++timer->it_requeue_pending;
	}
}

/*
 * Sample a process (thread group) timer for the given group_leader task.
 * Must be called with task sighand lock held for safe while_each_thread()
 * traversal.
 */
static int cpu_timer_sample_group(const clockid_t which_clock,
				  struct task_struct *p, u64 *sample)
{
	struct task_cputime cputime;

	thread_group_cputimer(p, &cputime);
	switch (CPUCLOCK_WHICH(which_clock)) {
	default:
		return -EINVAL;
	case CPUCLOCK_PROF:
		*sample = cputime.utime + cputime.stime;
		break;
	case CPUCLOCK_VIRT:
		*sample = cputime.utime;
		break;
	case CPUCLOCK_SCHED:
		*sample = cputime.sum_exec_runtime;
		break;
	}
	return 0;
}

/*
 * Guts of sys_timer_settime for CPU timers.
 * This is called with the timer locked and interrupts disabled.
 * If we return TIMER_RETRY, it's necessary to release the timer's lock
 * and try again.  (This happens when the timer is in the middle of firing.)
 */
static int posix_cpu_timer_set(struct k_itimer *timer, int timer_flags,
			       struct itimerspec64 *new, struct itimerspec64 *old)
{
	unsigned long flags;
	struct sighand_struct *sighand;
	struct task_struct *p = timer->it.cpu.task;
	u64 old_expires, new_expires, old_incr, val;
	int ret;

	WARN_ON_ONCE(p == NULL);

	/*
	 * Use the to_ktime conversion because that clamps the maximum
	 * value to KTIME_MAX and avoid multiplication overflows.
	 */
	new_expires = ktime_to_ns(timespec64_to_ktime(new->it_value));

	/*
	 * Protect against sighand release/switch in exit/exec and p->cpu_timers
	 * and p->signal->cpu_timers read/write in arm_timer()
	 */
	sighand = lock_task_sighand(p, &flags);
	/*
	 * If p has just been reaped, we can no
	 * longer get any information about it at all.
	 */
	if (unlikely(sighand == NULL)) {
		return -ESRCH;
	}

	/*
	 * Disarm any old timer after extracting its expiry time.
	 */

	ret = 0;
	old_incr = timer->it.cpu.incr;
	old_expires = timer->it.cpu.expires;
	if (unlikely(timer->it.cpu.firing)) {
		timer->it.cpu.firing = -1;
		ret = TIMER_RETRY;
	} else
		list_del_init(&timer->it.cpu.entry);

	/*
	 * We need to sample the current value to convert the new
	 * value from to relative and absolute, and to convert the
	 * old value from absolute to relative.  To set a process
	 * timer, we need a sample to balance the thread expiry
	 * times (in arm_timer).  With an absolute time, we must
	 * check if it's already passed.  In short, we need a sample.
	 */
	if (CPUCLOCK_PERTHREAD(timer->it_clock)) {
		cpu_clock_sample(timer->it_clock, p, &val);
	} else {
		cpu_timer_sample_group(timer->it_clock, p, &val);
	}

	if (old) {
		if (old_expires == 0) {
			old->it_value.tv_sec = 0;
			old->it_value.tv_nsec = 0;
		} else {
			/*
			 * Update the timer in case it has
			 * overrun already.  If it has,
			 * we'll report it as having overrun
			 * and with the next reloaded timer
			 * already ticking, though we are
			 * swallowing that pending
			 * notification here to install the
			 * new setting.
			 */
			bump_cpu_timer(timer, val);
			if (val < timer->it.cpu.expires) {
				old_expires = timer->it.cpu.expires - val;
				old->it_value = ns_to_timespec64(old_expires);
			} else {
				old->it_value.tv_nsec = 1;
				old->it_value.tv_sec = 0;
			}
		}
	}

	if (unlikely(ret)) {
		/*
		 * We are colliding with the timer actually firing.
		 * Punt after filling in the timer's old value, and
		 * disable this firing since we are already reporting
		 * it as an overrun (thanks to bump_cpu_timer above).
		 */
		unlock_task_sighand(p, &flags);
		goto out;
	}

	if (new_expires != 0 && !(timer_flags & TIMER_ABSTIME)) {
		new_expires += val;
	}

	/*
	 * Install the new expiry time (or zero).
	 * For a timer with no notification action, we don't actually
	 * arm the timer (we'll just fake it for timer_gettime).
	 */
	timer->it.cpu.expires = new_expires;
	if (new_expires != 0 && val < new_expires) {
		arm_timer(timer);
	}

	unlock_task_sighand(p, &flags);
	/*
	 * Install the new reload setting, and
	 * set up the signal and overrun bookkeeping.
	 */
	timer->it.cpu.incr = timespec64_to_ns(&new->it_interval);
	timer->it_interval = ns_to_ktime(timer->it.cpu.incr);

	/*
	 * This acts as a modification timestamp for the timer,
	 * so any automatic reload attempt will punt on seeing
	 * that we have reset the timer manually.
	 */
	timer->it_requeue_pending = (timer->it_requeue_pending + 2) &
		~REQUEUE_PENDING;
	timer->it_overrun_last = 0;
	timer->it_overrun = -1;

	if (new_expires != 0 && !(val < new_expires)) {
		/*
		 * The designated time already passed, so we notify
		 * immediately, even if the thread never runs to
		 * accumulate more time on this clock.
		 */
		cpu_timer_fire(timer);
	}

	ret = 0;
 out:
	if (old)
		old->it_interval = ns_to_timespec64(old_incr);

	return ret;
}

static void posix_cpu_timer_get(struct k_itimer *timer, struct itimerspec64 *itp)
{
	u64 now;
	struct task_struct *p = timer->it.cpu.task;

	WARN_ON_ONCE(p == NULL);

	/*
	 * Easy part: convert the reload time.
	 */
	itp->it_interval = ns_to_timespec64(timer->it.cpu.incr);

	if (!timer->it.cpu.expires)
		return;

	/*
	 * Sample the clock to take the difference with the expiry time.
	 */
	if (CPUCLOCK_PERTHREAD(timer->it_clock)) {
		cpu_clock_sample(timer->it_clock, p, &now);
	} else {
		struct sighand_struct *sighand;
		unsigned long flags;

		/*
		 * Protect against sighand release/switch in exit/exec and
		 * also make timer sampling safe if it ends up calling
		 * thread_group_cputime().
		 */
		sighand = lock_task_sighand(p, &flags);
		if (unlikely(sighand == NULL)) {
			/*
			 * The process has been reaped.
			 * We can't even collect a sample any more.
			 * Call the timer disarmed, nothing else to do.
			 */
			timer->it.cpu.expires = 0;
			return;
		} else {
			cpu_timer_sample_group(timer->it_clock, p, &now);
			unlock_task_sighand(p, &flags);
		}
	}

	if (now < timer->it.cpu.expires) {
		itp->it_value = ns_to_timespec64(timer->it.cpu.expires - now);
	} else {
		/*
		 * The timer should have expired already, but the firing
		 * hasn't taken place yet.  Say it's just about to expire.
		 */
		itp->it_value.tv_nsec = 1;
		itp->it_value.tv_sec = 0;
	}
}

static unsigned long long
check_timers_list(struct list_head *timers,
		  struct list_head *firing,
		  unsigned long long curr)
{
	int maxfire = 20;

	while (!list_empty(timers)) {
		struct cpu_timer_list *t;

		t = list_first_entry(timers, struct cpu_timer_list, entry);

		if (!--maxfire || curr < t->expires)
			return t->expires;

		t->firing = 1;
		list_move_tail(&t->entry, firing);
	}

	return 0;
}

static inline void check_dl_overrun(struct task_struct *tsk)
{
	if (tsk->dl.dl_overrun) {
		tsk->dl.dl_overrun = 0;
		__group_send_sig_info(SIGXCPU, SEND_SIG_PRIV, tsk);
	}
}

/*
 * Check for any per-thread CPU timers that have fired and move them off
 * the tsk->cpu_timers[N] list onto the firing list.  Here we update the
 * tsk->it_*_expires values to reflect the remaining thread CPU timers.
 */
static void check_thread_timers(struct task_struct *tsk,
				struct list_head *firing)
{
	struct list_head *timers = tsk->cpu_timers;
	struct task_cputime *tsk_expires = &tsk->cputime_expires;
	u64 expires;
	unsigned long soft;

	if (dl_task(tsk))
		check_dl_overrun(tsk);

	/*
	 * If cputime_expires is zero, then there are no active
	 * per thread CPU timers.
	 */
	if (task_cputime_zero(&tsk->cputime_expires))
		return;

	expires = check_timers_list(timers, firing, prof_ticks(tsk));
	tsk_expires->prof_exp = expires;

	expires = check_timers_list(++timers, firing, virt_ticks(tsk));
	tsk_expires->virt_exp = expires;

	tsk_expires->sched_exp = check_timers_list(++timers, firing,
						   tsk->se.sum_exec_runtime);

	/*
	 * Check for the special case thread timers.
	 */
	soft = task_rlimit(tsk, RLIMIT_RTTIME);
	if (soft != RLIM_INFINITY) {
		unsigned long hard = task_rlimit_max(tsk, RLIMIT_RTTIME);

		if (hard != RLIM_INFINITY &&
		    tsk->rt.timeout > DIV_ROUND_UP(hard, USEC_PER_SEC/HZ)) {
			/*
			 * At the hard limit, we just die.
			 * No need to calculate anything else now.
			 */
			if (print_fatal_signals) {
				pr_info("CPU Watchdog Timeout (hard): %s[%d]\n",
					tsk->comm, task_pid_nr(tsk));
			}
			__group_send_sig_info(SIGKILL, SEND_SIG_PRIV, tsk);
			return;
		}
		if (tsk->rt.timeout > DIV_ROUND_UP(soft, USEC_PER_SEC/HZ)) {
			/*
			 * At the soft limit, send a SIGXCPU every second.
			 */
			if (soft < hard) {
				soft += USEC_PER_SEC;
				tsk->signal->rlim[RLIMIT_RTTIME].rlim_cur =
					soft;
			}
			if (print_fatal_signals) {
				pr_info("RT Watchdog Timeout (soft): %s[%d]\n",
					tsk->comm, task_pid_nr(tsk));
			}
			__group_send_sig_info(SIGXCPU, SEND_SIG_PRIV, tsk);
		}
	}
	if (task_cputime_zero(tsk_expires))
		tick_dep_clear_task(tsk, TICK_DEP_BIT_POSIX_TIMER);
}

static inline void stop_process_timers(struct signal_struct *sig)
{
	struct thread_group_cputimer *cputimer = &sig->cputimer;

	/* Turn off cputimer->running. This is done without locking. */
	WRITE_ONCE(cputimer->running, false);
	tick_dep_clear_signal(sig, TICK_DEP_BIT_POSIX_TIMER);
}

static void check_cpu_itimer(struct task_struct *tsk, struct cpu_itimer *it,
			     u64 *expires, u64 cur_time, int signo)
{
	if (!it->expires)
		return;

	if (cur_time >= it->expires) {
		if (it->incr)
			it->expires += it->incr;
		else
			it->expires = 0;

		trace_itimer_expire(signo == SIGPROF ?
				    ITIMER_PROF : ITIMER_VIRTUAL,
				    task_tgid(tsk), cur_time);
		__group_send_sig_info(signo, SEND_SIG_PRIV, tsk);
	}

	if (it->expires && (!*expires || it->expires < *expires))
		*expires = it->expires;
}

/*
 * Check for any per-thread CPU timers that have fired and move them
 * off the tsk->*_timers list onto the firing list.  Per-thread timers
 * have already been taken off.
 */
static void check_process_timers(struct task_struct *tsk,
				 struct list_head *firing)
{
	struct signal_struct *const sig = tsk->signal;
	u64 utime, ptime, virt_expires, prof_expires;
	u64 sum_sched_runtime, sched_expires;
	struct list_head *timers = sig->cpu_timers;
	struct task_cputime cputime;
	unsigned long soft;

	if (dl_task(tsk))
		check_dl_overrun(tsk);

	/*
	 * If cputimer is not running, then there are no active
	 * process wide timers (POSIX 1.b, itimers, RLIMIT_CPU).
	 */
	if (!READ_ONCE(tsk->signal->cputimer.running))
		return;

        /*
	 * Signify that a thread is checking for process timers.
	 * Write access to this field is protected by the sighand lock.
	 */
	sig->cputimer.checking_timer = true;

	/*
	 * Collect the current process totals.
	 */
	thread_group_cputimer(tsk, &cputime);
	utime = cputime.utime;
	ptime = utime + cputime.stime;
	sum_sched_runtime = cputime.sum_exec_runtime;

	prof_expires = check_timers_list(timers, firing, ptime);
	virt_expires = check_timers_list(++timers, firing, utime);
	sched_expires = check_timers_list(++timers, firing, sum_sched_runtime);

	/*
	 * Check for the special case process timers.
	 */
	check_cpu_itimer(tsk, &sig->it[CPUCLOCK_PROF], &prof_expires, ptime,
			 SIGPROF);
	check_cpu_itimer(tsk, &sig->it[CPUCLOCK_VIRT], &virt_expires, utime,
			 SIGVTALRM);
	soft = task_rlimit(tsk, RLIMIT_CPU);
	if (soft != RLIM_INFINITY) {
		unsigned long psecs = div_u64(ptime, NSEC_PER_SEC);
		unsigned long hard = task_rlimit_max(tsk, RLIMIT_CPU);
		u64 x;
		if (psecs >= hard) {
			/*
			 * At the hard limit, we just die.
			 * No need to calculate anything else now.
			 */
			if (print_fatal_signals) {
				pr_info("RT Watchdog Timeout (hard): %s[%d]\n",
					tsk->comm, task_pid_nr(tsk));
			}
			__group_send_sig_info(SIGKILL, SEND_SIG_PRIV, tsk);
			return;
		}
		if (psecs >= soft) {
			/*
			 * At the soft limit, send a SIGXCPU every second.
			 */
			if (print_fatal_signals) {
				pr_info("CPU Watchdog Timeout (soft): %s[%d]\n",
					tsk->comm, task_pid_nr(tsk));
			}
			__group_send_sig_info(SIGXCPU, SEND_SIG_PRIV, tsk);
			if (soft < hard) {
				soft++;
				sig->rlim[RLIMIT_CPU].rlim_cur = soft;
			}
		}
		x = soft * NSEC_PER_SEC;
		if (!prof_expires || x < prof_expires)
			prof_expires = x;
	}

	sig->cputime_expires.prof_exp = prof_expires;
	sig->cputime_expires.virt_exp = virt_expires;
	sig->cputime_expires.sched_exp = sched_expires;
	if (task_cputime_zero(&sig->cputime_expires))
		stop_process_timers(sig);

	sig->cputimer.checking_timer = false;
}

/*
 * This is called from the signal code (via posixtimer_rearm)
 * when the last timer signal was delivered and we have to reload the timer.
 */
static void posix_cpu_timer_rearm(struct k_itimer *timer)
{
	struct sighand_struct *sighand;
	unsigned long flags;
	struct task_struct *p = timer->it.cpu.task;
	u64 now;

	WARN_ON_ONCE(p == NULL);

	/*
	 * Fetch the current sample and update the timer's expiry time.
	 */
	if (CPUCLOCK_PERTHREAD(timer->it_clock)) {
		cpu_clock_sample(timer->it_clock, p, &now);
		bump_cpu_timer(timer, now);
		if (unlikely(p->exit_state))
			return;

		/* Protect timer list r/w in arm_timer() */
		sighand = lock_task_sighand(p, &flags);
		if (!sighand)
			return;
	} else {
		/*
		 * Protect arm_timer() and timer sampling in case of call to
		 * thread_group_cputime().
		 */
		sighand = lock_task_sighand(p, &flags);
		if (unlikely(sighand == NULL)) {
			/*
			 * The process has been reaped.
			 * We can't even collect a sample any more.
			 */
			timer->it.cpu.expires = 0;
			return;
		} else if (unlikely(p->exit_state) && thread_group_empty(p)) {
			/* If the process is dying, no need to rearm */
			goto unlock;
		}
		cpu_timer_sample_group(timer->it_clock, p, &now);
		bump_cpu_timer(timer, now);
		/* Leave the sighand locked for the call below.  */
	}

	/*
	 * Now re-arm for the new expiry time.
	 */
	arm_timer(timer);
unlock:
	unlock_task_sighand(p, &flags);
}

/**
 * task_cputime_expired - Compare two task_cputime entities.
 *
 * @sample:	The task_cputime structure to be checked for expiration.
 * @expires:	Expiration times, against which @sample will be checked.
 *
 * Checks @sample against @expires to see if any field of @sample has expired.
 * Returns true if any field of the former is greater than the corresponding
 * field of the latter if the latter field is set.  Otherwise returns false.
 */
static inline int task_cputime_expired(const struct task_cputime *sample,
					const struct task_cputime *expires)
{
	if (expires->utime && sample->utime >= expires->utime)
		return 1;
	if (expires->stime && sample->utime + sample->stime >= expires->stime)
		return 1;
	if (expires->sum_exec_runtime != 0 &&
	    sample->sum_exec_runtime >= expires->sum_exec_runtime)
		return 1;
	return 0;
}

/**
 * fastpath_timer_check - POSIX CPU timers fast path.
 *
 * @tsk:	The task (thread) being checked.
 *
 * Check the task and thread group timers.  If both are zero (there are no
 * timers set) return false.  Otherwise snapshot the task and thread group
 * timers and compare them with the corresponding expiration times.  Return
 * true if a timer has expired, else return false.
 */
static inline int fastpath_timer_check(struct task_struct *tsk)
{
	struct signal_struct *sig;

	if (!task_cputime_zero(&tsk->cputime_expires)) {
		struct task_cputime task_sample;

		task_cputime(tsk, &task_sample.utime, &task_sample.stime);
		task_sample.sum_exec_runtime = tsk->se.sum_exec_runtime;
		if (task_cputime_expired(&task_sample, &tsk->cputime_expires))
			return 1;
	}

	sig = tsk->signal;
	/*
	 * Check if thread group timers expired when the cputimer is
	 * running and no other thread in the group is already checking
	 * for thread group cputimers. These fields are read without the
	 * sighand lock. However, this is fine because this is meant to
	 * be a fastpath heuristic to determine whether we should try to
	 * acquire the sighand lock to check/handle timers.
	 *
	 * In the worst case scenario, if 'running' or 'checking_timer' gets
	 * set but the current thread doesn't see the change yet, we'll wait
	 * until the next thread in the group gets a scheduler interrupt to
	 * handle the timer. This isn't an issue in practice because these
	 * types of delays with signals actually getting sent are expected.
	 */
	if (READ_ONCE(sig->cputimer.running) &&
	    !READ_ONCE(sig->cputimer.checking_timer)) {
		struct task_cputime group_sample;

		sample_cputime_atomic(&group_sample, &sig->cputimer.cputime_atomic);

		if (task_cputime_expired(&group_sample, &sig->cputime_expires))
			return 1;
	}

	if (dl_task(tsk) && tsk->dl.dl_overrun)
		return 1;

	return 0;
}

/*
 * This is called from the timer interrupt handler.  The irq handler has
 * already updated our counts.  We need to check if any timers fire now.
 * Interrupts are disabled.
 */
void run_posix_cpu_timers(struct task_struct *tsk)
{
	LIST_HEAD(firing);
	struct k_itimer *timer, *next;
	unsigned long flags;

	lockdep_assert_irqs_disabled();

	/*
	 * The fast path checks that there are no expired thread or thread
	 * group timers.  If that's so, just return.
	 */
	if (!fastpath_timer_check(tsk))
		return;

	if (!lock_task_sighand(tsk, &flags))
		return;
	/*
	 * Here we take off tsk->signal->cpu_timers[N] and
	 * tsk->cpu_timers[N] all the timers that are firing, and
	 * put them on the firing list.
	 */
	check_thread_timers(tsk, &firing);

	check_process_timers(tsk, &firing);

	/*
	 * We must release these locks before taking any timer's lock.
	 * There is a potential race with timer deletion here, as the
	 * siglock now protects our private firing list.  We have set
	 * the firing flag in each timer, so that a deletion attempt
	 * that gets the timer lock before we do will give it up and
	 * spin until we've taken care of that timer below.
	 */
	unlock_task_sighand(tsk, &flags);

	/*
	 * Now that all the timers on our list have the firing flag,
	 * no one will touch their list entries but us.  We'll take
	 * each timer's lock before clearing its firing flag, so no
	 * timer call will interfere.
	 */
	list_for_each_entry_safe(timer, next, &firing, it.cpu.entry) {
		int cpu_firing;

		spin_lock(&timer->it_lock);
		list_del_init(&timer->it.cpu.entry);
		cpu_firing = timer->it.cpu.firing;
		timer->it.cpu.firing = 0;
		/*
		 * The firing flag is -1 if we collided with a reset
		 * of the timer, which already reported this
		 * almost-firing as an overrun.  So don't generate an event.
		 */
		if (likely(cpu_firing >= 0))
			cpu_timer_fire(timer);
		spin_unlock(&timer->it_lock);
	}
}

/*
 * Set one of the process-wide special case CPU timers or RLIMIT_CPU.
 * The tsk->sighand->siglock must be held by the caller.
 */
void set_process_cpu_timer(struct task_struct *tsk, unsigned int clock_idx,
			   u64 *newval, u64 *oldval)
{
	u64 now;
	int ret;

	WARN_ON_ONCE(clock_idx == CPUCLOCK_SCHED);
	ret = cpu_timer_sample_group(clock_idx, tsk, &now);

	if (oldval && ret != -EINVAL) {
		/*
		 * We are setting itimer. The *oldval is absolute and we update
		 * it to be relative, *newval argument is relative and we update
		 * it to be absolute.
		 */
		if (*oldval) {
			if (*oldval <= now) {
				/* Just about to fire. */
				*oldval = TICK_NSEC;
			} else {
				*oldval -= now;
			}
		}

		if (!*newval)
			return;
		*newval += now;
	}

	/*
	 * Update expiration cache if we are the earliest timer, or eventually
	 * RLIMIT_CPU limit is earlier than prof_exp cpu timer expire.
	 */
	switch (clock_idx) {
	case CPUCLOCK_PROF:
		if (expires_gt(tsk->signal->cputime_expires.prof_exp, *newval))
			tsk->signal->cputime_expires.prof_exp = *newval;
		break;
	case CPUCLOCK_VIRT:
		if (expires_gt(tsk->signal->cputime_expires.virt_exp, *newval))
			tsk->signal->cputime_expires.virt_exp = *newval;
		break;
	}

	tick_dep_set_signal(tsk->signal, TICK_DEP_BIT_POSIX_TIMER);
}

static int do_cpu_nanosleep(const clockid_t which_clock, int flags,
			    const struct timespec64 *rqtp)
{
	struct itimerspec64 it;
	struct k_itimer timer;
	u64 expires;
	int error;

	/*
	 * Set up a temporary timer and then wait for it to go off.
	 */
	memset(&timer, 0, sizeof timer);
	spin_lock_init(&timer.it_lock);
	timer.it_clock = which_clock;
	timer.it_overrun = -1;
	error = posix_cpu_timer_create(&timer);
	timer.it_process = current;
	if (!error) {
		static struct itimerspec64 zero_it;
		struct restart_block *restart;

		memset(&it, 0, sizeof(it));
		it.it_value = *rqtp;

		spin_lock_irq(&timer.it_lock);
		error = posix_cpu_timer_set(&timer, flags, &it, NULL);
		if (error) {
			spin_unlock_irq(&timer.it_lock);
			return error;
		}

		while (!signal_pending(current)) {
			if (timer.it.cpu.expires == 0) {
				/*
				 * Our timer fired and was reset, below
				 * deletion can not fail.
				 */
				posix_cpu_timer_del(&timer);
				spin_unlock_irq(&timer.it_lock);
				return 0;
			}

			/*
			 * Block until cpu_timer_fire (or a signal) wakes us.
			 */
			__set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(&timer.it_lock);
			schedule();
			spin_lock_irq(&timer.it_lock);
		}

		/*
		 * We were interrupted by a signal.
		 */
		expires = timer.it.cpu.expires;
		error = posix_cpu_timer_set(&timer, 0, &zero_it, &it);
		if (!error) {
			/*
			 * Timer is now unarmed, deletion can not fail.
			 */
			posix_cpu_timer_del(&timer);
		}
		spin_unlock_irq(&timer.it_lock);

		while (error == TIMER_RETRY) {
			/*
			 * We need to handle case when timer was or is in the
			 * middle of firing. In other cases we already freed
			 * resources.
			 */
			spin_lock_irq(&timer.it_lock);
			error = posix_cpu_timer_del(&timer);
			spin_unlock_irq(&timer.it_lock);
		}

		if ((it.it_value.tv_sec | it.it_value.tv_nsec) == 0) {
			/*
			 * It actually did fire already.
			 */
			return 0;
		}

		error = -ERESTART_RESTARTBLOCK;
		/*
		 * Report back to the user the time still remaining.
		 */
		restart = &current->restart_block;
		restart->nanosleep.expires = expires;
		if (restart->nanosleep.type != TT_NONE)
			error = nanosleep_copyout(restart, &it.it_value);
	}

	return error;
}

static long posix_cpu_nsleep_restart(struct restart_block *restart_block);

static int posix_cpu_nsleep(const clockid_t which_clock, int flags,
			    const struct timespec64 *rqtp)
{
	struct restart_block *restart_block = &current->restart_block;
	int error;

	/*
	 * Diagnose required errors first.
	 */
	if (CPUCLOCK_PERTHREAD(which_clock) &&
	    (CPUCLOCK_PID(which_clock) == 0 ||
	     CPUCLOCK_PID(which_clock) == task_pid_vnr(current)))
		return -EINVAL;

	error = do_cpu_nanosleep(which_clock, flags, rqtp);

	if (error == -ERESTART_RESTARTBLOCK) {

		if (flags & TIMER_ABSTIME)
			return -ERESTARTNOHAND;

		restart_block->fn = posix_cpu_nsleep_restart;
		restart_block->nanosleep.clockid = which_clock;
	}
	return error;
}

static long posix_cpu_nsleep_restart(struct restart_block *restart_block)
{
	clockid_t which_clock = restart_block->nanosleep.clockid;
	struct timespec64 t;

	t = ns_to_timespec64(restart_block->nanosleep.expires);

	return do_cpu_nanosleep(which_clock, TIMER_ABSTIME, &t);
}

#define PROCESS_CLOCK	make_process_cpuclock(0, CPUCLOCK_SCHED)
#define THREAD_CLOCK	make_thread_cpuclock(0, CPUCLOCK_SCHED)

static int process_cpu_clock_getres(const clockid_t which_clock,
				    struct timespec64 *tp)
{
	return posix_cpu_clock_getres(PROCESS_CLOCK, tp);
}
static int process_cpu_clock_get(const clockid_t which_clock,
				 struct timespec64 *tp)
{
	return posix_cpu_clock_get(PROCESS_CLOCK, tp);
}
static int process_cpu_timer_create(struct k_itimer *timer)
{
	timer->it_clock = PROCESS_CLOCK;
	return posix_cpu_timer_create(timer);
}
static int process_cpu_nsleep(const clockid_t which_clock, int flags,
			      const struct timespec64 *rqtp)
{
	return posix_cpu_nsleep(PROCESS_CLOCK, flags, rqtp);
}
static int thread_cpu_clock_getres(const clockid_t which_clock,
				   struct timespec64 *tp)
{
	return posix_cpu_clock_getres(THREAD_CLOCK, tp);
}
static int thread_cpu_clock_get(const clockid_t which_clock,
				struct timespec64 *tp)
{
	return posix_cpu_clock_get(THREAD_CLOCK, tp);
}
static int thread_cpu_timer_create(struct k_itimer *timer)
{
	timer->it_clock = THREAD_CLOCK;
	return posix_cpu_timer_create(timer);
}

const struct k_clock clock_posix_cpu = {
	.clock_getres	= posix_cpu_clock_getres,
	.clock_set	= posix_cpu_clock_set,
	.clock_get	= posix_cpu_clock_get,
	.timer_create	= posix_cpu_timer_create,
	.nsleep		= posix_cpu_nsleep,
	.timer_set	= posix_cpu_timer_set,
	.timer_del	= posix_cpu_timer_del,
	.timer_get	= posix_cpu_timer_get,
	.timer_rearm	= posix_cpu_timer_rearm,
};

const struct k_clock clock_process = {
	.clock_getres	= process_cpu_clock_getres,
	.clock_get	= process_cpu_clock_get,
	.timer_create	= process_cpu_timer_create,
	.nsleep		= process_cpu_nsleep,
};

const struct k_clock clock_thread = {
	.clock_getres	= thread_cpu_clock_getres,
	.clock_get	= thread_cpu_clock_get,
	.timer_create	= thread_cpu_timer_create,
};
