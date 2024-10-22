// SPDX-License-Identifier: GPL-2.0+
/*
 * 2002-10-15  Posix Clocks & timers
 *                           by George Anzinger george@mvista.com
 *			     Copyright (C) 2002 2003 by MontaVista Software.
 *
 * 2004-06-01  Fix CLOCK_REALTIME clock/timer TIMER_ABSTIME bug.
 *			     Copyright (C) 2004 Boris Hu
 *
 * These are all the functions necessary to implement POSIX clocks & timers
 */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/sched/task.h>

#include <linux/uaccess.h>
#include <linux/list.h>
#include <linux/init.h>
#include <linux/compiler.h>
#include <linux/hash.h>
#include <linux/posix-clock.h>
#include <linux/posix-timers.h>
#include <linux/syscalls.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/export.h>
#include <linux/hashtable.h>
#include <linux/compat.h>
#include <linux/nospec.h>
#include <linux/time_namespace.h>

#include "timekeeping.h"
#include "posix-timers.h"

static struct kmem_cache *posix_timers_cache;

/*
 * Timers are managed in a hash table for lockless lookup. The hash key is
 * constructed from current::signal and the timer ID and the timer is
 * matched against current::signal and the timer ID when walking the hash
 * bucket list.
 *
 * This allows checkpoint/restore to reconstruct the exact timer IDs for
 * a process.
 */
static DEFINE_HASHTABLE(posix_timers_hashtable, 9);
static DEFINE_SPINLOCK(hash_lock);

static const struct k_clock * const posix_clocks[];
static const struct k_clock *clockid_to_kclock(const clockid_t id);
static const struct k_clock clock_realtime, clock_monotonic;

/* SIGEV_THREAD_ID cannot share a bit with the other SIGEV values. */
#if SIGEV_THREAD_ID != (SIGEV_THREAD_ID & \
			~(SIGEV_SIGNAL | SIGEV_NONE | SIGEV_THREAD))
#error "SIGEV_THREAD_ID must not share bit with other SIGEV values!"
#endif

static struct k_itimer *__lock_timer(timer_t timer_id, unsigned long *flags);

#define lock_timer(tid, flags)						   \
({	struct k_itimer *__timr;					   \
	__cond_lock(&__timr->it_lock, __timr = __lock_timer(tid, flags));  \
	__timr;								   \
})

static int hash(struct signal_struct *sig, unsigned int nr)
{
	return hash_32(hash32_ptr(sig) ^ nr, HASH_BITS(posix_timers_hashtable));
}

static struct k_itimer *__posix_timers_find(struct hlist_head *head,
					    struct signal_struct *sig,
					    timer_t id)
{
	struct k_itimer *timer;

	hlist_for_each_entry_rcu(timer, head, t_hash, lockdep_is_held(&hash_lock)) {
		/* timer->it_signal can be set concurrently */
		if ((READ_ONCE(timer->it_signal) == sig) && (timer->it_id == id))
			return timer;
	}
	return NULL;
}

static struct k_itimer *posix_timer_by_id(timer_t id)
{
	struct signal_struct *sig = current->signal;
	struct hlist_head *head = &posix_timers_hashtable[hash(sig, id)];

	return __posix_timers_find(head, sig, id);
}

static int posix_timer_add(struct k_itimer *timer)
{
	struct signal_struct *sig = current->signal;
	struct hlist_head *head;
	unsigned int cnt, id;

	/*
	 * FIXME: Replace this by a per signal struct xarray once there is
	 * a plan to handle the resulting CRIU regression gracefully.
	 */
	for (cnt = 0; cnt <= INT_MAX; cnt++) {
		spin_lock(&hash_lock);
		id = sig->next_posix_timer_id;

		/* Write the next ID back. Clamp it to the positive space */
		sig->next_posix_timer_id = (id + 1) & INT_MAX;

		head = &posix_timers_hashtable[hash(sig, id)];
		if (!__posix_timers_find(head, sig, id)) {
			hlist_add_head_rcu(&timer->t_hash, head);
			spin_unlock(&hash_lock);
			return id;
		}
		spin_unlock(&hash_lock);
	}
	/* POSIX return code when no timer ID could be allocated */
	return -EAGAIN;
}

static inline void unlock_timer(struct k_itimer *timr, unsigned long flags)
{
	spin_unlock_irqrestore(&timr->it_lock, flags);
}

static int posix_get_realtime_timespec(clockid_t which_clock, struct timespec64 *tp)
{
	ktime_get_real_ts64(tp);
	return 0;
}

static ktime_t posix_get_realtime_ktime(clockid_t which_clock)
{
	return ktime_get_real();
}

static int posix_clock_realtime_set(const clockid_t which_clock,
				    const struct timespec64 *tp)
{
	return do_sys_settimeofday64(tp, NULL);
}

static int posix_clock_realtime_adj(const clockid_t which_clock,
				    struct __kernel_timex *t)
{
	return do_adjtimex(t);
}

static int posix_get_monotonic_timespec(clockid_t which_clock, struct timespec64 *tp)
{
	ktime_get_ts64(tp);
	timens_add_monotonic(tp);
	return 0;
}

static ktime_t posix_get_monotonic_ktime(clockid_t which_clock)
{
	return ktime_get();
}

static int posix_get_monotonic_raw(clockid_t which_clock, struct timespec64 *tp)
{
	ktime_get_raw_ts64(tp);
	timens_add_monotonic(tp);
	return 0;
}

static int posix_get_realtime_coarse(clockid_t which_clock, struct timespec64 *tp)
{
	ktime_get_coarse_real_ts64(tp);
	return 0;
}

static int posix_get_monotonic_coarse(clockid_t which_clock,
						struct timespec64 *tp)
{
	ktime_get_coarse_ts64(tp);
	timens_add_monotonic(tp);
	return 0;
}

static int posix_get_coarse_res(const clockid_t which_clock, struct timespec64 *tp)
{
	*tp = ktime_to_timespec64(KTIME_LOW_RES);
	return 0;
}

static int posix_get_boottime_timespec(const clockid_t which_clock, struct timespec64 *tp)
{
	ktime_get_boottime_ts64(tp);
	timens_add_boottime(tp);
	return 0;
}

static ktime_t posix_get_boottime_ktime(const clockid_t which_clock)
{
	return ktime_get_boottime();
}

static int posix_get_tai_timespec(clockid_t which_clock, struct timespec64 *tp)
{
	ktime_get_clocktai_ts64(tp);
	return 0;
}

static ktime_t posix_get_tai_ktime(clockid_t which_clock)
{
	return ktime_get_clocktai();
}

static int posix_get_hrtimer_res(clockid_t which_clock, struct timespec64 *tp)
{
	tp->tv_sec = 0;
	tp->tv_nsec = hrtimer_resolution;
	return 0;
}

static __init int init_posix_timers(void)
{
	posix_timers_cache = kmem_cache_create("posix_timers_cache",
					sizeof(struct k_itimer), 0,
					SLAB_PANIC | SLAB_ACCOUNT, NULL);
	return 0;
}
__initcall(init_posix_timers);

/*
 * The siginfo si_overrun field and the return value of timer_getoverrun(2)
 * are of type int. Clamp the overrun value to INT_MAX
 */
static inline int timer_overrun_to_int(struct k_itimer *timr, int baseval)
{
	s64 sum = timr->it_overrun_last + (s64)baseval;

	return sum > (s64)INT_MAX ? INT_MAX : (int)sum;
}

static void common_hrtimer_rearm(struct k_itimer *timr)
{
	struct hrtimer *timer = &timr->it.real.timer;

	timr->it_overrun += hrtimer_forward(timer, timer->base->get_time(),
					    timr->it_interval);
	hrtimer_restart(timer);
}

/*
 * This function is called from the signal delivery code if
 * info->si_sys_private is not zero, which indicates that the timer has to
 * be rearmed. Restart the timer and update info::si_overrun.
 */
void posixtimer_rearm(struct kernel_siginfo *info)
{
	struct k_itimer *timr;
	unsigned long flags;

	timr = lock_timer(info->si_tid, &flags);
	if (!timr)
		return;

	if (timr->it_interval && timr->it_requeue_pending == info->si_sys_private) {
		timr->kclock->timer_rearm(timr);

		timr->it_active = 1;
		timr->it_overrun_last = timr->it_overrun;
		timr->it_overrun = -1LL;
		++timr->it_requeue_pending;

		info->si_overrun = timer_overrun_to_int(timr, info->si_overrun);
	}

	unlock_timer(timr, flags);
}

int posix_timer_queue_signal(struct k_itimer *timr)
{
	int ret, si_private = 0;
	enum pid_type type;

	lockdep_assert_held(&timr->it_lock);

	timr->it_active = 0;
	if (timr->it_interval)
		si_private = ++timr->it_requeue_pending;

	/*
	 * FIXME: if ->sigq is queued we can race with
	 * dequeue_signal()->posixtimer_rearm().
	 *
	 * If dequeue_signal() sees the "right" value of
	 * si_sys_private it calls posixtimer_rearm().
	 * We re-queue ->sigq and drop ->it_lock().
	 * posixtimer_rearm() locks the timer
	 * and re-schedules it while ->sigq is pending.
	 * Not really bad, but not that we want.
	 */
	timr->sigq->info.si_sys_private = si_private;

	type = !(timr->it_sigev_notify & SIGEV_THREAD_ID) ? PIDTYPE_TGID : PIDTYPE_PID;
	ret = send_sigqueue(timr->sigq, timr->it_pid, type);
	/* If we failed to send the signal the timer stops. */
	return ret > 0;
}

/*
 * This function gets called when a POSIX.1b interval timer expires from
 * the HRTIMER interrupt (soft interrupt on RT kernels).
 *
 * Handles CLOCK_REALTIME, CLOCK_MONOTONIC, CLOCK_BOOTTIME and CLOCK_TAI
 * based timers.
 */
static enum hrtimer_restart posix_timer_fn(struct hrtimer *timer)
{
	struct k_itimer *timr = container_of(timer, struct k_itimer, it.real.timer);
	enum hrtimer_restart ret = HRTIMER_NORESTART;
	unsigned long flags;

	spin_lock_irqsave(&timr->it_lock, flags);

	if (posix_timer_queue_signal(timr)) {
		/*
		 * The signal was not queued due to SIG_IGN. As a
		 * consequence the timer is not going to be rearmed from
		 * the signal delivery path. But as a real signal handler
		 * can be installed later the timer must be rearmed here.
		 */
		if (timr->it_interval != 0) {
			ktime_t now = hrtimer_cb_get_time(timer);

			/*
			 * FIXME: What we really want, is to stop this
			 * timer completely and restart it in case the
			 * SIG_IGN is removed. This is a non trivial
			 * change to the signal handling code.
			 *
			 * For now let timers with an interval less than a
			 * jiffy expire every jiffy and recheck for a
			 * valid signal handler.
			 *
			 * This avoids interrupt starvation in case of a
			 * very small interval, which would expire the
			 * timer immediately again.
			 *
			 * Moving now ahead of time by one jiffy tricks
			 * hrtimer_forward() to expire the timer later,
			 * while it still maintains the overrun accuracy
			 * for the price of a slight inconsistency in the
			 * timer_gettime() case. This is at least better
			 * than a timer storm.
			 *
			 * Only required when high resolution timers are
			 * enabled as the periodic tick based timers are
			 * automatically aligned to the next tick.
			 */
			if (IS_ENABLED(CONFIG_HIGH_RES_TIMERS)) {
				ktime_t kj = TICK_NSEC;

				if (timr->it_interval < kj)
					now = ktime_add(now, kj);
			}

			timr->it_overrun += hrtimer_forward(timer, now, timr->it_interval);
			ret = HRTIMER_RESTART;
			++timr->it_requeue_pending;
			timr->it_active = 1;
		}
	}

	unlock_timer(timr, flags);
	return ret;
}

static struct pid *good_sigevent(sigevent_t * event)
{
	struct pid *pid = task_tgid(current);
	struct task_struct *rtn;

	switch (event->sigev_notify) {
	case SIGEV_SIGNAL | SIGEV_THREAD_ID:
		pid = find_vpid(event->sigev_notify_thread_id);
		rtn = pid_task(pid, PIDTYPE_PID);
		if (!rtn || !same_thread_group(rtn, current))
			return NULL;
		fallthrough;
	case SIGEV_SIGNAL:
	case SIGEV_THREAD:
		if (event->sigev_signo <= 0 || event->sigev_signo > SIGRTMAX)
			return NULL;
		fallthrough;
	case SIGEV_NONE:
		return pid;
	default:
		return NULL;
	}
}

static struct k_itimer * alloc_posix_timer(void)
{
	struct k_itimer *tmr = kmem_cache_zalloc(posix_timers_cache, GFP_KERNEL);

	if (!tmr)
		return tmr;
	if (unlikely(!(tmr->sigq = sigqueue_alloc()))) {
		kmem_cache_free(posix_timers_cache, tmr);
		return NULL;
	}
	clear_siginfo(&tmr->sigq->info);
	return tmr;
}

static void k_itimer_rcu_free(struct rcu_head *head)
{
	struct k_itimer *tmr = container_of(head, struct k_itimer, rcu);

	kmem_cache_free(posix_timers_cache, tmr);
}

static void posix_timer_free(struct k_itimer *tmr)
{
	put_pid(tmr->it_pid);
	sigqueue_free(tmr->sigq);
	call_rcu(&tmr->rcu, k_itimer_rcu_free);
}

static void posix_timer_unhash_and_free(struct k_itimer *tmr)
{
	spin_lock(&hash_lock);
	hlist_del_rcu(&tmr->t_hash);
	spin_unlock(&hash_lock);
	posix_timer_free(tmr);
}

static int common_timer_create(struct k_itimer *new_timer)
{
	hrtimer_init(&new_timer->it.real.timer, new_timer->it_clock, 0);
	return 0;
}

/* Create a POSIX.1b interval timer. */
static int do_timer_create(clockid_t which_clock, struct sigevent *event,
			   timer_t __user *created_timer_id)
{
	const struct k_clock *kc = clockid_to_kclock(which_clock);
	struct k_itimer *new_timer;
	int error, new_timer_id;

	if (!kc)
		return -EINVAL;
	if (!kc->timer_create)
		return -EOPNOTSUPP;

	new_timer = alloc_posix_timer();
	if (unlikely(!new_timer))
		return -EAGAIN;

	spin_lock_init(&new_timer->it_lock);

	/*
	 * Add the timer to the hash table. The timer is not yet valid
	 * because new_timer::it_signal is still NULL. The timer id is also
	 * not yet visible to user space.
	 */
	new_timer_id = posix_timer_add(new_timer);
	if (new_timer_id < 0) {
		posix_timer_free(new_timer);
		return new_timer_id;
	}

	new_timer->it_id = (timer_t) new_timer_id;
	new_timer->it_clock = which_clock;
	new_timer->kclock = kc;
	new_timer->it_overrun = -1LL;

	if (event) {
		rcu_read_lock();
		new_timer->it_pid = get_pid(good_sigevent(event));
		rcu_read_unlock();
		if (!new_timer->it_pid) {
			error = -EINVAL;
			goto out;
		}
		new_timer->it_sigev_notify     = event->sigev_notify;
		new_timer->sigq->info.si_signo = event->sigev_signo;
		new_timer->sigq->info.si_value = event->sigev_value;
	} else {
		new_timer->it_sigev_notify     = SIGEV_SIGNAL;
		new_timer->sigq->info.si_signo = SIGALRM;
		memset(&new_timer->sigq->info.si_value, 0, sizeof(sigval_t));
		new_timer->sigq->info.si_value.sival_int = new_timer->it_id;
		new_timer->it_pid = get_pid(task_tgid(current));
	}

	new_timer->sigq->info.si_tid   = new_timer->it_id;
	new_timer->sigq->info.si_code  = SI_TIMER;

	if (copy_to_user(created_timer_id, &new_timer_id, sizeof (new_timer_id))) {
		error = -EFAULT;
		goto out;
	}
	/*
	 * After succesful copy out, the timer ID is visible to user space
	 * now but not yet valid because new_timer::signal is still NULL.
	 *
	 * Complete the initialization with the clock specific create
	 * callback.
	 */
	error = kc->timer_create(new_timer);
	if (error)
		goto out;

	spin_lock_irq(&current->sighand->siglock);
	/* This makes the timer valid in the hash table */
	WRITE_ONCE(new_timer->it_signal, current->signal);
	hlist_add_head(&new_timer->list, &current->signal->posix_timers);
	spin_unlock_irq(&current->sighand->siglock);
	/*
	 * After unlocking sighand::siglock @new_timer is subject to
	 * concurrent removal and cannot be touched anymore
	 */
	return 0;
out:
	posix_timer_unhash_and_free(new_timer);
	return error;
}

SYSCALL_DEFINE3(timer_create, const clockid_t, which_clock,
		struct sigevent __user *, timer_event_spec,
		timer_t __user *, created_timer_id)
{
	if (timer_event_spec) {
		sigevent_t event;

		if (copy_from_user(&event, timer_event_spec, sizeof (event)))
			return -EFAULT;
		return do_timer_create(which_clock, &event, created_timer_id);
	}
	return do_timer_create(which_clock, NULL, created_timer_id);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE3(timer_create, clockid_t, which_clock,
		       struct compat_sigevent __user *, timer_event_spec,
		       timer_t __user *, created_timer_id)
{
	if (timer_event_spec) {
		sigevent_t event;

		if (get_compat_sigevent(&event, timer_event_spec))
			return -EFAULT;
		return do_timer_create(which_clock, &event, created_timer_id);
	}
	return do_timer_create(which_clock, NULL, created_timer_id);
}
#endif

static struct k_itimer *__lock_timer(timer_t timer_id, unsigned long *flags)
{
	struct k_itimer *timr;

	/*
	 * timer_t could be any type >= int and we want to make sure any
	 * @timer_id outside positive int range fails lookup.
	 */
	if ((unsigned long long)timer_id > INT_MAX)
		return NULL;

	/*
	 * The hash lookup and the timers are RCU protected.
	 *
	 * Timers are added to the hash in invalid state where
	 * timr::it_signal == NULL. timer::it_signal is only set after the
	 * rest of the initialization succeeded.
	 *
	 * Timer destruction happens in steps:
	 *  1) Set timr::it_signal to NULL with timr::it_lock held
	 *  2) Release timr::it_lock
	 *  3) Remove from the hash under hash_lock
	 *  4) Call RCU for removal after the grace period
	 *
	 * Holding rcu_read_lock() accross the lookup ensures that
	 * the timer cannot be freed.
	 *
	 * The lookup validates locklessly that timr::it_signal ==
	 * current::it_signal and timr::it_id == @timer_id. timr::it_id
	 * can't change, but timr::it_signal becomes NULL during
	 * destruction.
	 */
	rcu_read_lock();
	timr = posix_timer_by_id(timer_id);
	if (timr) {
		spin_lock_irqsave(&timr->it_lock, *flags);
		/*
		 * Validate under timr::it_lock that timr::it_signal is
		 * still valid. Pairs with #1 above.
		 */
		if (timr->it_signal == current->signal) {
			rcu_read_unlock();
			return timr;
		}
		spin_unlock_irqrestore(&timr->it_lock, *flags);
	}
	rcu_read_unlock();

	return NULL;
}

static ktime_t common_hrtimer_remaining(struct k_itimer *timr, ktime_t now)
{
	struct hrtimer *timer = &timr->it.real.timer;

	return __hrtimer_expires_remaining_adjusted(timer, now);
}

static s64 common_hrtimer_forward(struct k_itimer *timr, ktime_t now)
{
	struct hrtimer *timer = &timr->it.real.timer;

	return hrtimer_forward(timer, now, timr->it_interval);
}

/*
 * Get the time remaining on a POSIX.1b interval timer.
 *
 * Two issues to handle here:
 *
 *  1) The timer has a requeue pending. The return value must appear as
 *     if the timer has been requeued right now.
 *
 *  2) The timer is a SIGEV_NONE timer. These timers are never enqueued
 *     into the hrtimer queue and therefore never expired. Emulate expiry
 *     here taking #1 into account.
 */
void common_timer_get(struct k_itimer *timr, struct itimerspec64 *cur_setting)
{
	const struct k_clock *kc = timr->kclock;
	ktime_t now, remaining, iv;
	bool sig_none;

	sig_none = timr->it_sigev_notify == SIGEV_NONE;
	iv = timr->it_interval;

	/* interval timer ? */
	if (iv) {
		cur_setting->it_interval = ktime_to_timespec64(iv);
	} else if (!timr->it_active) {
		/*
		 * SIGEV_NONE oneshot timers are never queued and therefore
		 * timr->it_active is always false. The check below
		 * vs. remaining time will handle this case.
		 *
		 * For all other timers there is nothing to update here, so
		 * return.
		 */
		if (!sig_none)
			return;
	}

	now = kc->clock_get_ktime(timr->it_clock);

	/*
	 * If this is an interval timer and either has requeue pending or
	 * is a SIGEV_NONE timer move the expiry time forward by intervals,
	 * so expiry is > now.
	 */
	if (iv && (timr->it_requeue_pending & REQUEUE_PENDING || sig_none))
		timr->it_overrun += kc->timer_forward(timr, now);

	remaining = kc->timer_remaining(timr, now);
	/*
	 * As @now is retrieved before a possible timer_forward() and
	 * cannot be reevaluated by the compiler @remaining is based on the
	 * same @now value. Therefore @remaining is consistent vs. @now.
	 *
	 * Consequently all interval timers, i.e. @iv > 0, cannot have a
	 * remaining time <= 0 because timer_forward() guarantees to move
	 * them forward so that the next timer expiry is > @now.
	 */
	if (remaining <= 0) {
		/*
		 * A single shot SIGEV_NONE timer must return 0, when it is
		 * expired! Timers which have a real signal delivery mode
		 * must return a remaining time greater than 0 because the
		 * signal has not yet been delivered.
		 */
		if (!sig_none)
			cur_setting->it_value.tv_nsec = 1;
	} else {
		cur_setting->it_value = ktime_to_timespec64(remaining);
	}
}

static int do_timer_gettime(timer_t timer_id,  struct itimerspec64 *setting)
{
	const struct k_clock *kc;
	struct k_itimer *timr;
	unsigned long flags;
	int ret = 0;

	timr = lock_timer(timer_id, &flags);
	if (!timr)
		return -EINVAL;

	memset(setting, 0, sizeof(*setting));
	kc = timr->kclock;
	if (WARN_ON_ONCE(!kc || !kc->timer_get))
		ret = -EINVAL;
	else
		kc->timer_get(timr, setting);

	unlock_timer(timr, flags);
	return ret;
}

/* Get the time remaining on a POSIX.1b interval timer. */
SYSCALL_DEFINE2(timer_gettime, timer_t, timer_id,
		struct __kernel_itimerspec __user *, setting)
{
	struct itimerspec64 cur_setting;

	int ret = do_timer_gettime(timer_id, &cur_setting);
	if (!ret) {
		if (put_itimerspec64(&cur_setting, setting))
			ret = -EFAULT;
	}
	return ret;
}

#ifdef CONFIG_COMPAT_32BIT_TIME

SYSCALL_DEFINE2(timer_gettime32, timer_t, timer_id,
		struct old_itimerspec32 __user *, setting)
{
	struct itimerspec64 cur_setting;

	int ret = do_timer_gettime(timer_id, &cur_setting);
	if (!ret) {
		if (put_old_itimerspec32(&cur_setting, setting))
			ret = -EFAULT;
	}
	return ret;
}

#endif

/**
 * sys_timer_getoverrun - Get the number of overruns of a POSIX.1b interval timer
 * @timer_id:	The timer ID which identifies the timer
 *
 * The "overrun count" of a timer is one plus the number of expiration
 * intervals which have elapsed between the first expiry, which queues the
 * signal and the actual signal delivery. On signal delivery the "overrun
 * count" is calculated and cached, so it can be returned directly here.
 *
 * As this is relative to the last queued signal the returned overrun count
 * is meaningless outside of the signal delivery path and even there it
 * does not accurately reflect the current state when user space evaluates
 * it.
 *
 * Returns:
 *	-EINVAL		@timer_id is invalid
 *	1..INT_MAX	The number of overruns related to the last delivered signal
 */
SYSCALL_DEFINE1(timer_getoverrun, timer_t, timer_id)
{
	struct k_itimer *timr;
	unsigned long flags;
	int overrun;

	timr = lock_timer(timer_id, &flags);
	if (!timr)
		return -EINVAL;

	overrun = timer_overrun_to_int(timr, 0);
	unlock_timer(timr, flags);

	return overrun;
}

static void common_hrtimer_arm(struct k_itimer *timr, ktime_t expires,
			       bool absolute, bool sigev_none)
{
	struct hrtimer *timer = &timr->it.real.timer;
	enum hrtimer_mode mode;

	mode = absolute ? HRTIMER_MODE_ABS : HRTIMER_MODE_REL;
	/*
	 * Posix magic: Relative CLOCK_REALTIME timers are not affected by
	 * clock modifications, so they become CLOCK_MONOTONIC based under the
	 * hood. See hrtimer_init(). Update timr->kclock, so the generic
	 * functions which use timr->kclock->clock_get_*() work.
	 *
	 * Note: it_clock stays unmodified, because the next timer_set() might
	 * use ABSTIME, so it needs to switch back.
	 */
	if (timr->it_clock == CLOCK_REALTIME)
		timr->kclock = absolute ? &clock_realtime : &clock_monotonic;

	hrtimer_init(&timr->it.real.timer, timr->it_clock, mode);
	timr->it.real.timer.function = posix_timer_fn;

	if (!absolute)
		expires = ktime_add_safe(expires, timer->base->get_time());
	hrtimer_set_expires(timer, expires);

	if (!sigev_none)
		hrtimer_start_expires(timer, HRTIMER_MODE_ABS);
}

static int common_hrtimer_try_to_cancel(struct k_itimer *timr)
{
	return hrtimer_try_to_cancel(&timr->it.real.timer);
}

static void common_timer_wait_running(struct k_itimer *timer)
{
	hrtimer_cancel_wait_running(&timer->it.real.timer);
}

/*
 * On PREEMPT_RT this prevents priority inversion and a potential livelock
 * against the ksoftirqd thread in case that ksoftirqd gets preempted while
 * executing a hrtimer callback.
 *
 * See the comments in hrtimer_cancel_wait_running(). For PREEMPT_RT=n this
 * just results in a cpu_relax().
 *
 * For POSIX CPU timers with CONFIG_POSIX_CPU_TIMERS_TASK_WORK=n this is
 * just a cpu_relax(). With CONFIG_POSIX_CPU_TIMERS_TASK_WORK=y this
 * prevents spinning on an eventually scheduled out task and a livelock
 * when the task which tries to delete or disarm the timer has preempted
 * the task which runs the expiry in task work context.
 */
static struct k_itimer *timer_wait_running(struct k_itimer *timer,
					   unsigned long *flags)
{
	const struct k_clock *kc = READ_ONCE(timer->kclock);
	timer_t timer_id = READ_ONCE(timer->it_id);

	/* Prevent kfree(timer) after dropping the lock */
	rcu_read_lock();
	unlock_timer(timer, *flags);

	/*
	 * kc->timer_wait_running() might drop RCU lock. So @timer
	 * cannot be touched anymore after the function returns!
	 */
	if (!WARN_ON_ONCE(!kc->timer_wait_running))
		kc->timer_wait_running(timer);

	rcu_read_unlock();
	/* Relock the timer. It might be not longer hashed. */
	return lock_timer(timer_id, flags);
}

/*
 * Set up the new interval and reset the signal delivery data
 */
void posix_timer_set_common(struct k_itimer *timer, struct itimerspec64 *new_setting)
{
	if (new_setting->it_value.tv_sec || new_setting->it_value.tv_nsec)
		timer->it_interval = timespec64_to_ktime(new_setting->it_interval);
	else
		timer->it_interval = 0;

	/* Prevent reloading in case there is a signal pending */
	timer->it_requeue_pending = (timer->it_requeue_pending + 2) & ~REQUEUE_PENDING;
	/* Reset overrun accounting */
	timer->it_overrun_last = 0;
	timer->it_overrun = -1LL;
}

/* Set a POSIX.1b interval timer. */
int common_timer_set(struct k_itimer *timr, int flags,
		     struct itimerspec64 *new_setting,
		     struct itimerspec64 *old_setting)
{
	const struct k_clock *kc = timr->kclock;
	bool sigev_none;
	ktime_t expires;

	if (old_setting)
		common_timer_get(timr, old_setting);

	/* Prevent rearming by clearing the interval */
	timr->it_interval = 0;
	/*
	 * Careful here. On SMP systems the timer expiry function could be
	 * active and spinning on timr->it_lock.
	 */
	if (kc->timer_try_to_cancel(timr) < 0)
		return TIMER_RETRY;

	timr->it_active = 0;
	posix_timer_set_common(timr, new_setting);

	/* Keep timer disarmed when it_value is zero */
	if (!new_setting->it_value.tv_sec && !new_setting->it_value.tv_nsec)
		return 0;

	expires = timespec64_to_ktime(new_setting->it_value);
	if (flags & TIMER_ABSTIME)
		expires = timens_ktime_to_host(timr->it_clock, expires);
	sigev_none = timr->it_sigev_notify == SIGEV_NONE;

	kc->timer_arm(timr, expires, flags & TIMER_ABSTIME, sigev_none);
	timr->it_active = !sigev_none;
	return 0;
}

static int do_timer_settime(timer_t timer_id, int tmr_flags,
			    struct itimerspec64 *new_spec64,
			    struct itimerspec64 *old_spec64)
{
	const struct k_clock *kc;
	struct k_itimer *timr;
	unsigned long flags;
	int error;

	if (!timespec64_valid(&new_spec64->it_interval) ||
	    !timespec64_valid(&new_spec64->it_value))
		return -EINVAL;

	if (old_spec64)
		memset(old_spec64, 0, sizeof(*old_spec64));

	timr = lock_timer(timer_id, &flags);
retry:
	if (!timr)
		return -EINVAL;

	if (old_spec64)
		old_spec64->it_interval = ktime_to_timespec64(timr->it_interval);

	kc = timr->kclock;
	if (WARN_ON_ONCE(!kc || !kc->timer_set))
		error = -EINVAL;
	else
		error = kc->timer_set(timr, tmr_flags, new_spec64, old_spec64);

	if (error == TIMER_RETRY) {
		// We already got the old time...
		old_spec64 = NULL;
		/* Unlocks and relocks the timer if it still exists */
		timr = timer_wait_running(timr, &flags);
		goto retry;
	}
	unlock_timer(timr, flags);

	return error;
}

/* Set a POSIX.1b interval timer */
SYSCALL_DEFINE4(timer_settime, timer_t, timer_id, int, flags,
		const struct __kernel_itimerspec __user *, new_setting,
		struct __kernel_itimerspec __user *, old_setting)
{
	struct itimerspec64 new_spec, old_spec, *rtn;
	int error = 0;

	if (!new_setting)
		return -EINVAL;

	if (get_itimerspec64(&new_spec, new_setting))
		return -EFAULT;

	rtn = old_setting ? &old_spec : NULL;
	error = do_timer_settime(timer_id, flags, &new_spec, rtn);
	if (!error && old_setting) {
		if (put_itimerspec64(&old_spec, old_setting))
			error = -EFAULT;
	}
	return error;
}

#ifdef CONFIG_COMPAT_32BIT_TIME
SYSCALL_DEFINE4(timer_settime32, timer_t, timer_id, int, flags,
		struct old_itimerspec32 __user *, new,
		struct old_itimerspec32 __user *, old)
{
	struct itimerspec64 new_spec, old_spec;
	struct itimerspec64 *rtn = old ? &old_spec : NULL;
	int error = 0;

	if (!new)
		return -EINVAL;
	if (get_old_itimerspec32(&new_spec, new))
		return -EFAULT;

	error = do_timer_settime(timer_id, flags, &new_spec, rtn);
	if (!error && old) {
		if (put_old_itimerspec32(&old_spec, old))
			error = -EFAULT;
	}
	return error;
}
#endif

int common_timer_del(struct k_itimer *timer)
{
	const struct k_clock *kc = timer->kclock;

	timer->it_interval = 0;
	if (kc->timer_try_to_cancel(timer) < 0)
		return TIMER_RETRY;
	timer->it_active = 0;
	return 0;
}

static inline int timer_delete_hook(struct k_itimer *timer)
{
	const struct k_clock *kc = timer->kclock;

	if (WARN_ON_ONCE(!kc || !kc->timer_del))
		return -EINVAL;
	return kc->timer_del(timer);
}

/* Delete a POSIX.1b interval timer. */
SYSCALL_DEFINE1(timer_delete, timer_t, timer_id)
{
	struct k_itimer *timer;
	unsigned long flags;

	timer = lock_timer(timer_id, &flags);

retry_delete:
	if (!timer)
		return -EINVAL;

	if (unlikely(timer_delete_hook(timer) == TIMER_RETRY)) {
		/* Unlocks and relocks the timer if it still exists */
		timer = timer_wait_running(timer, &flags);
		goto retry_delete;
	}

	spin_lock(&current->sighand->siglock);
	hlist_del(&timer->list);
	spin_unlock(&current->sighand->siglock);
	/*
	 * A concurrent lookup could check timer::it_signal lockless. It
	 * will reevaluate with timer::it_lock held and observe the NULL.
	 */
	WRITE_ONCE(timer->it_signal, NULL);

	unlock_timer(timer, flags);
	posix_timer_unhash_and_free(timer);
	return 0;
}

/*
 * Delete a timer if it is armed, remove it from the hash and schedule it
 * for RCU freeing.
 */
static void itimer_delete(struct k_itimer *timer)
{
	unsigned long flags;

	/*
	 * irqsave is required to make timer_wait_running() work.
	 */
	spin_lock_irqsave(&timer->it_lock, flags);

retry_delete:
	/*
	 * Even if the timer is not longer accessible from other tasks
	 * it still might be armed and queued in the underlying timer
	 * mechanism. Worse, that timer mechanism might run the expiry
	 * function concurrently.
	 */
	if (timer_delete_hook(timer) == TIMER_RETRY) {
		/*
		 * Timer is expired concurrently, prevent livelocks
		 * and pointless spinning on RT.
		 *
		 * timer_wait_running() drops timer::it_lock, which opens
		 * the possibility for another task to delete the timer.
		 *
		 * That's not possible here because this is invoked from
		 * do_exit() only for the last thread of the thread group.
		 * So no other task can access and delete that timer.
		 */
		if (WARN_ON_ONCE(timer_wait_running(timer, &flags) != timer))
			return;

		goto retry_delete;
	}
	hlist_del(&timer->list);

	/*
	 * Setting timer::it_signal to NULL is technically not required
	 * here as nothing can access the timer anymore legitimately via
	 * the hash table. Set it to NULL nevertheless so that all deletion
	 * paths are consistent.
	 */
	WRITE_ONCE(timer->it_signal, NULL);

	spin_unlock_irqrestore(&timer->it_lock, flags);
	posix_timer_unhash_and_free(timer);
}

/*
 * Invoked from do_exit() when the last thread of a thread group exits.
 * At that point no other task can access the timers of the dying
 * task anymore.
 */
void exit_itimers(struct task_struct *tsk)
{
	struct hlist_head timers;

	if (hlist_empty(&tsk->signal->posix_timers))
		return;

	/* Protect against concurrent read via /proc/$PID/timers */
	spin_lock_irq(&tsk->sighand->siglock);
	hlist_move_list(&tsk->signal->posix_timers, &timers);
	spin_unlock_irq(&tsk->sighand->siglock);

	/* The timers are not longer accessible via tsk::signal */
	while (!hlist_empty(&timers))
		itimer_delete(hlist_entry(timers.first, struct k_itimer, list));
}

SYSCALL_DEFINE2(clock_settime, const clockid_t, which_clock,
		const struct __kernel_timespec __user *, tp)
{
	const struct k_clock *kc = clockid_to_kclock(which_clock);
	struct timespec64 new_tp;

	if (!kc || !kc->clock_set)
		return -EINVAL;

	if (get_timespec64(&new_tp, tp))
		return -EFAULT;

	/*
	 * Permission checks have to be done inside the clock specific
	 * setter callback.
	 */
	return kc->clock_set(which_clock, &new_tp);
}

SYSCALL_DEFINE2(clock_gettime, const clockid_t, which_clock,
		struct __kernel_timespec __user *, tp)
{
	const struct k_clock *kc = clockid_to_kclock(which_clock);
	struct timespec64 kernel_tp;
	int error;

	if (!kc)
		return -EINVAL;

	error = kc->clock_get_timespec(which_clock, &kernel_tp);

	if (!error && put_timespec64(&kernel_tp, tp))
		error = -EFAULT;

	return error;
}

int do_clock_adjtime(const clockid_t which_clock, struct __kernel_timex * ktx)
{
	const struct k_clock *kc = clockid_to_kclock(which_clock);

	if (!kc)
		return -EINVAL;
	if (!kc->clock_adj)
		return -EOPNOTSUPP;

	return kc->clock_adj(which_clock, ktx);
}

SYSCALL_DEFINE2(clock_adjtime, const clockid_t, which_clock,
		struct __kernel_timex __user *, utx)
{
	struct __kernel_timex ktx;
	int err;

	if (copy_from_user(&ktx, utx, sizeof(ktx)))
		return -EFAULT;

	err = do_clock_adjtime(which_clock, &ktx);

	if (err >= 0 && copy_to_user(utx, &ktx, sizeof(ktx)))
		return -EFAULT;

	return err;
}

/**
 * sys_clock_getres - Get the resolution of a clock
 * @which_clock:	The clock to get the resolution for
 * @tp:			Pointer to a a user space timespec64 for storage
 *
 * POSIX defines:
 *
 * "The clock_getres() function shall return the resolution of any
 * clock. Clock resolutions are implementation-defined and cannot be set by
 * a process. If the argument res is not NULL, the resolution of the
 * specified clock shall be stored in the location pointed to by res. If
 * res is NULL, the clock resolution is not returned. If the time argument
 * of clock_settime() is not a multiple of res, then the value is truncated
 * to a multiple of res."
 *
 * Due to the various hardware constraints the real resolution can vary
 * wildly and even change during runtime when the underlying devices are
 * replaced. The kernel also can use hardware devices with different
 * resolutions for reading the time and for arming timers.
 *
 * The kernel therefore deviates from the POSIX spec in various aspects:
 *
 * 1) The resolution returned to user space
 *
 *    For CLOCK_REALTIME, CLOCK_MONOTONIC, CLOCK_BOOTTIME, CLOCK_TAI,
 *    CLOCK_REALTIME_ALARM, CLOCK_BOOTTIME_ALAREM and CLOCK_MONOTONIC_RAW
 *    the kernel differentiates only two cases:
 *
 *    I)  Low resolution mode:
 *
 *	  When high resolution timers are disabled at compile or runtime
 *	  the resolution returned is nanoseconds per tick, which represents
 *	  the precision at which timers expire.
 *
 *    II) High resolution mode:
 *
 *	  When high resolution timers are enabled the resolution returned
 *	  is always one nanosecond independent of the actual resolution of
 *	  the underlying hardware devices.
 *
 *	  For CLOCK_*_ALARM the actual resolution depends on system
 *	  state. When system is running the resolution is the same as the
 *	  resolution of the other clocks. During suspend the actual
 *	  resolution is the resolution of the underlying RTC device which
 *	  might be way less precise than the clockevent device used during
 *	  running state.
 *
 *   For CLOCK_REALTIME_COARSE and CLOCK_MONOTONIC_COARSE the resolution
 *   returned is always nanoseconds per tick.
 *
 *   For CLOCK_PROCESS_CPUTIME and CLOCK_THREAD_CPUTIME the resolution
 *   returned is always one nanosecond under the assumption that the
 *   underlying scheduler clock has a better resolution than nanoseconds
 *   per tick.
 *
 *   For dynamic POSIX clocks (PTP devices) the resolution returned is
 *   always one nanosecond.
 *
 * 2) Affect on sys_clock_settime()
 *
 *    The kernel does not truncate the time which is handed in to
 *    sys_clock_settime(). The kernel internal timekeeping is always using
 *    nanoseconds precision independent of the clocksource device which is
 *    used to read the time from. The resolution of that device only
 *    affects the presicion of the time returned by sys_clock_gettime().
 *
 * Returns:
 *	0		Success. @tp contains the resolution
 *	-EINVAL		@which_clock is not a valid clock ID
 *	-EFAULT		Copying the resolution to @tp faulted
 *	-ENODEV		Dynamic POSIX clock is not backed by a device
 *	-EOPNOTSUPP	Dynamic POSIX clock does not support getres()
 */
SYSCALL_DEFINE2(clock_getres, const clockid_t, which_clock,
		struct __kernel_timespec __user *, tp)
{
	const struct k_clock *kc = clockid_to_kclock(which_clock);
	struct timespec64 rtn_tp;
	int error;

	if (!kc)
		return -EINVAL;

	error = kc->clock_getres(which_clock, &rtn_tp);

	if (!error && tp && put_timespec64(&rtn_tp, tp))
		error = -EFAULT;

	return error;
}

#ifdef CONFIG_COMPAT_32BIT_TIME

SYSCALL_DEFINE2(clock_settime32, clockid_t, which_clock,
		struct old_timespec32 __user *, tp)
{
	const struct k_clock *kc = clockid_to_kclock(which_clock);
	struct timespec64 ts;

	if (!kc || !kc->clock_set)
		return -EINVAL;

	if (get_old_timespec32(&ts, tp))
		return -EFAULT;

	return kc->clock_set(which_clock, &ts);
}

SYSCALL_DEFINE2(clock_gettime32, clockid_t, which_clock,
		struct old_timespec32 __user *, tp)
{
	const struct k_clock *kc = clockid_to_kclock(which_clock);
	struct timespec64 ts;
	int err;

	if (!kc)
		return -EINVAL;

	err = kc->clock_get_timespec(which_clock, &ts);

	if (!err && put_old_timespec32(&ts, tp))
		err = -EFAULT;

	return err;
}

SYSCALL_DEFINE2(clock_adjtime32, clockid_t, which_clock,
		struct old_timex32 __user *, utp)
{
	struct __kernel_timex ktx;
	int err;

	err = get_old_timex32(&ktx, utp);
	if (err)
		return err;

	err = do_clock_adjtime(which_clock, &ktx);

	if (err >= 0 && put_old_timex32(utp, &ktx))
		return -EFAULT;

	return err;
}

SYSCALL_DEFINE2(clock_getres_time32, clockid_t, which_clock,
		struct old_timespec32 __user *, tp)
{
	const struct k_clock *kc = clockid_to_kclock(which_clock);
	struct timespec64 ts;
	int err;

	if (!kc)
		return -EINVAL;

	err = kc->clock_getres(which_clock, &ts);
	if (!err && tp && put_old_timespec32(&ts, tp))
		return -EFAULT;

	return err;
}

#endif

/*
 * sys_clock_nanosleep() for CLOCK_REALTIME and CLOCK_TAI
 */
static int common_nsleep(const clockid_t which_clock, int flags,
			 const struct timespec64 *rqtp)
{
	ktime_t texp = timespec64_to_ktime(*rqtp);

	return hrtimer_nanosleep(texp, flags & TIMER_ABSTIME ?
				 HRTIMER_MODE_ABS : HRTIMER_MODE_REL,
				 which_clock);
}

/*
 * sys_clock_nanosleep() for CLOCK_MONOTONIC and CLOCK_BOOTTIME
 *
 * Absolute nanosleeps for these clocks are time-namespace adjusted.
 */
static int common_nsleep_timens(const clockid_t which_clock, int flags,
				const struct timespec64 *rqtp)
{
	ktime_t texp = timespec64_to_ktime(*rqtp);

	if (flags & TIMER_ABSTIME)
		texp = timens_ktime_to_host(which_clock, texp);

	return hrtimer_nanosleep(texp, flags & TIMER_ABSTIME ?
				 HRTIMER_MODE_ABS : HRTIMER_MODE_REL,
				 which_clock);
}

SYSCALL_DEFINE4(clock_nanosleep, const clockid_t, which_clock, int, flags,
		const struct __kernel_timespec __user *, rqtp,
		struct __kernel_timespec __user *, rmtp)
{
	const struct k_clock *kc = clockid_to_kclock(which_clock);
	struct timespec64 t;

	if (!kc)
		return -EINVAL;
	if (!kc->nsleep)
		return -EOPNOTSUPP;

	if (get_timespec64(&t, rqtp))
		return -EFAULT;

	if (!timespec64_valid(&t))
		return -EINVAL;
	if (flags & TIMER_ABSTIME)
		rmtp = NULL;
	current->restart_block.fn = do_no_restart_syscall;
	current->restart_block.nanosleep.type = rmtp ? TT_NATIVE : TT_NONE;
	current->restart_block.nanosleep.rmtp = rmtp;

	return kc->nsleep(which_clock, flags, &t);
}

#ifdef CONFIG_COMPAT_32BIT_TIME

SYSCALL_DEFINE4(clock_nanosleep_time32, clockid_t, which_clock, int, flags,
		struct old_timespec32 __user *, rqtp,
		struct old_timespec32 __user *, rmtp)
{
	const struct k_clock *kc = clockid_to_kclock(which_clock);
	struct timespec64 t;

	if (!kc)
		return -EINVAL;
	if (!kc->nsleep)
		return -EOPNOTSUPP;

	if (get_old_timespec32(&t, rqtp))
		return -EFAULT;

	if (!timespec64_valid(&t))
		return -EINVAL;
	if (flags & TIMER_ABSTIME)
		rmtp = NULL;
	current->restart_block.fn = do_no_restart_syscall;
	current->restart_block.nanosleep.type = rmtp ? TT_COMPAT : TT_NONE;
	current->restart_block.nanosleep.compat_rmtp = rmtp;

	return kc->nsleep(which_clock, flags, &t);
}

#endif

static const struct k_clock clock_realtime = {
	.clock_getres		= posix_get_hrtimer_res,
	.clock_get_timespec	= posix_get_realtime_timespec,
	.clock_get_ktime	= posix_get_realtime_ktime,
	.clock_set		= posix_clock_realtime_set,
	.clock_adj		= posix_clock_realtime_adj,
	.nsleep			= common_nsleep,
	.timer_create		= common_timer_create,
	.timer_set		= common_timer_set,
	.timer_get		= common_timer_get,
	.timer_del		= common_timer_del,
	.timer_rearm		= common_hrtimer_rearm,
	.timer_forward		= common_hrtimer_forward,
	.timer_remaining	= common_hrtimer_remaining,
	.timer_try_to_cancel	= common_hrtimer_try_to_cancel,
	.timer_wait_running	= common_timer_wait_running,
	.timer_arm		= common_hrtimer_arm,
};

static const struct k_clock clock_monotonic = {
	.clock_getres		= posix_get_hrtimer_res,
	.clock_get_timespec	= posix_get_monotonic_timespec,
	.clock_get_ktime	= posix_get_monotonic_ktime,
	.nsleep			= common_nsleep_timens,
	.timer_create		= common_timer_create,
	.timer_set		= common_timer_set,
	.timer_get		= common_timer_get,
	.timer_del		= common_timer_del,
	.timer_rearm		= common_hrtimer_rearm,
	.timer_forward		= common_hrtimer_forward,
	.timer_remaining	= common_hrtimer_remaining,
	.timer_try_to_cancel	= common_hrtimer_try_to_cancel,
	.timer_wait_running	= common_timer_wait_running,
	.timer_arm		= common_hrtimer_arm,
};

static const struct k_clock clock_monotonic_raw = {
	.clock_getres		= posix_get_hrtimer_res,
	.clock_get_timespec	= posix_get_monotonic_raw,
};

static const struct k_clock clock_realtime_coarse = {
	.clock_getres		= posix_get_coarse_res,
	.clock_get_timespec	= posix_get_realtime_coarse,
};

static const struct k_clock clock_monotonic_coarse = {
	.clock_getres		= posix_get_coarse_res,
	.clock_get_timespec	= posix_get_monotonic_coarse,
};

static const struct k_clock clock_tai = {
	.clock_getres		= posix_get_hrtimer_res,
	.clock_get_ktime	= posix_get_tai_ktime,
	.clock_get_timespec	= posix_get_tai_timespec,
	.nsleep			= common_nsleep,
	.timer_create		= common_timer_create,
	.timer_set		= common_timer_set,
	.timer_get		= common_timer_get,
	.timer_del		= common_timer_del,
	.timer_rearm		= common_hrtimer_rearm,
	.timer_forward		= common_hrtimer_forward,
	.timer_remaining	= common_hrtimer_remaining,
	.timer_try_to_cancel	= common_hrtimer_try_to_cancel,
	.timer_wait_running	= common_timer_wait_running,
	.timer_arm		= common_hrtimer_arm,
};

static const struct k_clock clock_boottime = {
	.clock_getres		= posix_get_hrtimer_res,
	.clock_get_ktime	= posix_get_boottime_ktime,
	.clock_get_timespec	= posix_get_boottime_timespec,
	.nsleep			= common_nsleep_timens,
	.timer_create		= common_timer_create,
	.timer_set		= common_timer_set,
	.timer_get		= common_timer_get,
	.timer_del		= common_timer_del,
	.timer_rearm		= common_hrtimer_rearm,
	.timer_forward		= common_hrtimer_forward,
	.timer_remaining	= common_hrtimer_remaining,
	.timer_try_to_cancel	= common_hrtimer_try_to_cancel,
	.timer_wait_running	= common_timer_wait_running,
	.timer_arm		= common_hrtimer_arm,
};

static const struct k_clock * const posix_clocks[] = {
	[CLOCK_REALTIME]		= &clock_realtime,
	[CLOCK_MONOTONIC]		= &clock_monotonic,
	[CLOCK_PROCESS_CPUTIME_ID]	= &clock_process,
	[CLOCK_THREAD_CPUTIME_ID]	= &clock_thread,
	[CLOCK_MONOTONIC_RAW]		= &clock_monotonic_raw,
	[CLOCK_REALTIME_COARSE]		= &clock_realtime_coarse,
	[CLOCK_MONOTONIC_COARSE]	= &clock_monotonic_coarse,
	[CLOCK_BOOTTIME]		= &clock_boottime,
	[CLOCK_REALTIME_ALARM]		= &alarm_clock,
	[CLOCK_BOOTTIME_ALARM]		= &alarm_clock,
	[CLOCK_TAI]			= &clock_tai,
};

static const struct k_clock *clockid_to_kclock(const clockid_t id)
{
	clockid_t idx = id;

	if (id < 0) {
		return (id & CLOCKFD_MASK) == CLOCKFD ?
			&clock_posix_dynamic : &clock_posix_cpu;
	}

	if (id >= ARRAY_SIZE(posix_clocks))
		return NULL;

	return posix_clocks[array_index_nospec(idx, ARRAY_SIZE(posix_clocks))];
}
