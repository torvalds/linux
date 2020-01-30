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

#include "timekeeping.h"
#include "posix-timers.h"

/*
 * Management arrays for POSIX timers. Timers are now kept in static hash table
 * with 512 entries.
 * Timer ids are allocated by local routine, which selects proper hash head by
 * key, constructed from current->signal address and per signal struct counter.
 * This keeps timer ids unique per process, but now they can intersect between
 * processes.
 */

/*
 * Lets keep our timers in a slab cache :-)
 */
static struct kmem_cache *posix_timers_cache;

static DEFINE_HASHTABLE(posix_timers_hashtable, 9);
static DEFINE_SPINLOCK(hash_lock);

static const struct k_clock * const posix_clocks[];
static const struct k_clock *clockid_to_kclock(const clockid_t id);
static const struct k_clock clock_realtime, clock_monotonic;

/*
 * we assume that the new SIGEV_THREAD_ID shares no bits with the other
 * SIGEV values.  Here we put out an error if this assumption fails.
 */
#if SIGEV_THREAD_ID != (SIGEV_THREAD_ID & \
                       ~(SIGEV_SIGNAL | SIGEV_NONE | SIGEV_THREAD))
#error "SIGEV_THREAD_ID must not share bit with other SIGEV values!"
#endif

/*
 * The timer ID is turned into a timer address by idr_find().
 * Verifying a valid ID consists of:
 *
 * a) checking that idr_find() returns other than -1.
 * b) checking that the timer id matches the one in the timer itself.
 * c) that the timer owner is in the callers thread group.
 */

/*
 * CLOCKs: The POSIX standard calls for a couple of clocks and allows us
 *	    to implement others.  This structure defines the various
 *	    clocks.
 *
 * RESOLUTION: Clock resolution is used to round up timer and interval
 *	    times, NOT to report clock times, which are reported with as
 *	    much resolution as the system can muster.  In some cases this
 *	    resolution may depend on the underlying clock hardware and
 *	    may not be quantifiable until run time, and only then is the
 *	    necessary code is written.	The standard says we should say
 *	    something about this issue in the documentation...
 *
 * FUNCTIONS: The CLOCKs structure defines possible functions to
 *	    handle various clock functions.
 *
 *	    The standard POSIX timer management code assumes the
 *	    following: 1.) The k_itimer struct (sched.h) is used for
 *	    the timer.  2.) The list, it_lock, it_clock, it_id and
 *	    it_pid fields are not modified by timer code.
 *
 * Permissions: It is assumed that the clock_settime() function defined
 *	    for each clock will take care of permission checks.	 Some
 *	    clocks may be set able by any user (i.e. local process
 *	    clocks) others not.	 Currently the only set able clock we
 *	    have is CLOCK_REALTIME and its high res counter part, both of
 *	    which we beg off on and pass to do_sys_settimeofday().
 */
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

	hlist_for_each_entry_rcu(timer, head, t_hash) {
		if ((timer->it_signal == sig) && (timer->it_id == id))
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
	int first_free_id = sig->posix_timer_id;
	struct hlist_head *head;
	int ret = -ENOENT;

	do {
		spin_lock(&hash_lock);
		head = &posix_timers_hashtable[hash(sig, sig->posix_timer_id)];
		if (!__posix_timers_find(head, sig, sig->posix_timer_id)) {
			hlist_add_head_rcu(&timer->t_hash, head);
			ret = sig->posix_timer_id;
		}
		if (++sig->posix_timer_id < 0)
			sig->posix_timer_id = 0;
		if ((sig->posix_timer_id == first_free_id) && (ret == -ENOENT))
			/* Loop over all possible ids completed */
			ret = -EAGAIN;
		spin_unlock(&hash_lock);
	} while (ret == -ENOENT);
	return ret;
}

static inline void unlock_timer(struct k_itimer *timr, unsigned long flags)
{
	spin_unlock_irqrestore(&timr->it_lock, flags);
}

/* Get clock_realtime */
static int posix_clock_realtime_get(clockid_t which_clock, struct timespec64 *tp)
{
	ktime_get_real_ts64(tp);
	return 0;
}

/* Set clock_realtime */
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

/*
 * Get monotonic time for posix timers
 */
static int posix_ktime_get_ts(clockid_t which_clock, struct timespec64 *tp)
{
	ktime_get_ts64(tp);
	return 0;
}

/*
 * Get monotonic-raw time for posix timers
 */
static int posix_get_monotonic_raw(clockid_t which_clock, struct timespec64 *tp)
{
	ktime_get_raw_ts64(tp);
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
	return 0;
}

static int posix_get_coarse_res(const clockid_t which_clock, struct timespec64 *tp)
{
	*tp = ktime_to_timespec64(KTIME_LOW_RES);
	return 0;
}

static int posix_get_boottime(const clockid_t which_clock, struct timespec64 *tp)
{
	ktime_get_boottime_ts64(tp);
	return 0;
}

static int posix_get_tai(clockid_t which_clock, struct timespec64 *tp)
{
	ktime_get_clocktai_ts64(tp);
	return 0;
}

static int posix_get_hrtimer_res(clockid_t which_clock, struct timespec64 *tp)
{
	tp->tv_sec = 0;
	tp->tv_nsec = hrtimer_resolution;
	return 0;
}

/*
 * Initialize everything, well, just everything in Posix clocks/timers ;)
 */
static __init int init_posix_timers(void)
{
	posix_timers_cache = kmem_cache_create("posix_timers_cache",
					sizeof (struct k_itimer), 0, SLAB_PANIC,
					NULL);
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
 * This function is exported for use by the signal deliver code.  It is
 * called just prior to the info block being released and passes that
 * block to us.  It's function is to update the overrun entry AND to
 * restart the timer.  It should only be called if the timer is to be
 * restarted (i.e. we have flagged this in the sys_private entry of the
 * info block).
 *
 * To protect against the timer going away while the interrupt is queued,
 * we require that the it_requeue_pending flag be set.
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

int posix_timer_event(struct k_itimer *timr, int si_private)
{
	enum pid_type type;
	int ret = -1;
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
 * This function gets called when a POSIX.1b interval timer expires.  It
 * is used as a callback from the kernel internal timer.  The
 * run_timer_list code ALWAYS calls with interrupts on.

 * This code is for CLOCK_REALTIME* and CLOCK_MONOTONIC* timers.
 */
static enum hrtimer_restart posix_timer_fn(struct hrtimer *timer)
{
	struct k_itimer *timr;
	unsigned long flags;
	int si_private = 0;
	enum hrtimer_restart ret = HRTIMER_NORESTART;

	timr = container_of(timer, struct k_itimer, it.real.timer);
	spin_lock_irqsave(&timr->it_lock, flags);

	timr->it_active = 0;
	if (timr->it_interval != 0)
		si_private = ++timr->it_requeue_pending;

	if (posix_timer_event(timr, si_private)) {
		/*
		 * signal was not sent because of sig_ignor
		 * we will not get a call back to restart it AND
		 * it should be restarted.
		 */
		if (timr->it_interval != 0) {
			ktime_t now = hrtimer_cb_get_time(timer);

			/*
			 * FIXME: What we really want, is to stop this
			 * timer completely and restart it in case the
			 * SIG_IGN is removed. This is a non trivial
			 * change which involves sighand locking
			 * (sigh !), which we don't want to do late in
			 * the release cycle.
			 *
			 * For now we just let timers with an interval
			 * less than a jiffie expire every jiffie to
			 * avoid softirq starvation in case of SIG_IGN
			 * and a very small interval, which would put
			 * the timer right back on the softirq pending
			 * list. By moving now ahead of time we trick
			 * hrtimer_forward() to expire the timer
			 * later, while we still maintain the overrun
			 * accuracy, but have some inconsistency in
			 * the timer_gettime() case. This is at least
			 * better than a starved softirq. A more
			 * complex fix which solves also another related
			 * inconsistency is already in the pipeline.
			 */
#ifdef CONFIG_HIGH_RES_TIMERS
			{
				ktime_t kj = NSEC_PER_SEC / HZ;

				if (timr->it_interval < kj)
					now = ktime_add(now, kj);
			}
#endif
			timr->it_overrun += hrtimer_forward(timer, now,
							    timr->it_interval);
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
		/* FALLTHRU */
	case SIGEV_SIGNAL:
	case SIGEV_THREAD:
		if (event->sigev_signo <= 0 || event->sigev_signo > SIGRTMAX)
			return NULL;
		/* FALLTHRU */
	case SIGEV_NONE:
		return pid;
	default:
		return NULL;
	}
}

static struct k_itimer * alloc_posix_timer(void)
{
	struct k_itimer *tmr;
	tmr = kmem_cache_zalloc(posix_timers_cache, GFP_KERNEL);
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

#define IT_ID_SET	1
#define IT_ID_NOT_SET	0
static void release_posix_timer(struct k_itimer *tmr, int it_id_set)
{
	if (it_id_set) {
		unsigned long flags;
		spin_lock_irqsave(&hash_lock, flags);
		hlist_del_rcu(&tmr->t_hash);
		spin_unlock_irqrestore(&hash_lock, flags);
	}
	put_pid(tmr->it_pid);
	sigqueue_free(tmr->sigq);
	call_rcu(&tmr->rcu, k_itimer_rcu_free);
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
	int it_id_set = IT_ID_NOT_SET;

	if (!kc)
		return -EINVAL;
	if (!kc->timer_create)
		return -EOPNOTSUPP;

	new_timer = alloc_posix_timer();
	if (unlikely(!new_timer))
		return -EAGAIN;

	spin_lock_init(&new_timer->it_lock);
	new_timer_id = posix_timer_add(new_timer);
	if (new_timer_id < 0) {
		error = new_timer_id;
		goto out;
	}

	it_id_set = IT_ID_SET;
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

	if (copy_to_user(created_timer_id,
			 &new_timer_id, sizeof (new_timer_id))) {
		error = -EFAULT;
		goto out;
	}

	error = kc->timer_create(new_timer);
	if (error)
		goto out;

	spin_lock_irq(&current->sighand->siglock);
	new_timer->it_signal = current->signal;
	list_add(&new_timer->list, &current->signal->posix_timers);
	spin_unlock_irq(&current->sighand->siglock);

	return 0;
	/*
	 * In the case of the timer belonging to another task, after
	 * the task is unlocked, the timer is owned by the other task
	 * and may cease to exist at any time.  Don't use or modify
	 * new_timer after the unlock call.
	 */
out:
	release_posix_timer(new_timer, it_id_set);
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

/*
 * Locking issues: We need to protect the result of the id look up until
 * we get the timer locked down so it is not deleted under us.  The
 * removal is done under the idr spinlock so we use that here to bridge
 * the find to the timer lock.  To avoid a dead lock, the timer id MUST
 * be release with out holding the timer lock.
 */
static struct k_itimer *__lock_timer(timer_t timer_id, unsigned long *flags)
{
	struct k_itimer *timr;

	/*
	 * timer_t could be any type >= int and we want to make sure any
	 * @timer_id outside positive int range fails lookup.
	 */
	if ((unsigned long long)timer_id > INT_MAX)
		return NULL;

	rcu_read_lock();
	timr = posix_timer_by_id(timer_id);
	if (timr) {
		spin_lock_irqsave(&timr->it_lock, *flags);
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
 * Get the time remaining on a POSIX.1b interval timer.  This function
 * is ALWAYS called with spin_lock_irq on the timer, thus it must not
 * mess with irq.
 *
 * We have a couple of messes to clean up here.  First there is the case
 * of a timer that has a requeue pending.  These timers should appear to
 * be in the timer list with an expiry as if we were to requeue them
 * now.
 *
 * The second issue is the SIGEV_NONE timer which may be active but is
 * not really ever put in the timer list (to save system resources).
 * This timer may be expired, and if so, we will do it here.  Otherwise
 * it is the same as a requeue pending timer WRT to what we should
 * report.
 */
void common_timer_get(struct k_itimer *timr, struct itimerspec64 *cur_setting)
{
	const struct k_clock *kc = timr->kclock;
	ktime_t now, remaining, iv;
	struct timespec64 ts64;
	bool sig_none;

	sig_none = timr->it_sigev_notify == SIGEV_NONE;
	iv = timr->it_interval;

	/* interval timer ? */
	if (iv) {
		cur_setting->it_interval = ktime_to_timespec64(iv);
	} else if (!timr->it_active) {
		/*
		 * SIGEV_NONE oneshot timers are never queued. Check them
		 * below.
		 */
		if (!sig_none)
			return;
	}

	/*
	 * The timespec64 based conversion is suboptimal, but it's not
	 * worth to implement yet another callback.
	 */
	kc->clock_get(timr->it_clock, &ts64);
	now = timespec64_to_ktime(ts64);

	/*
	 * When a requeue is pending or this is a SIGEV_NONE timer move the
	 * expiry time forward by intervals, so expiry is > now.
	 */
	if (iv && (timr->it_requeue_pending & REQUEUE_PENDING || sig_none))
		timr->it_overrun += kc->timer_forward(timr, now);

	remaining = kc->timer_remaining(timr, now);
	/* Return 0 only, when the timer is expired and not pending */
	if (remaining <= 0) {
		/*
		 * A single shot SIGEV_NONE timer must return 0, when
		 * it is expired !
		 */
		if (!sig_none)
			cur_setting->it_value.tv_nsec = 1;
	} else {
		cur_setting->it_value = ktime_to_timespec64(remaining);
	}
}

/* Get the time remaining on a POSIX.1b interval timer. */
static int do_timer_gettime(timer_t timer_id,  struct itimerspec64 *setting)
{
	struct k_itimer *timr;
	const struct k_clock *kc;
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

/*
 * Get the number of overruns of a POSIX.1b interval timer.  This is to
 * be the overrun of the timer last delivered.  At the same time we are
 * accumulating overruns on the next timer.  The overrun is frozen when
 * the signal is delivered, either at the notify time (if the info block
 * is not queued) or at the actual delivery time (as we are informed by
 * the call back to posixtimer_rearm().  So all we need to do is
 * to pick up the frozen overrun.
 */
SYSCALL_DEFINE1(timer_getoverrun, timer_t, timer_id)
{
	struct k_itimer *timr;
	int overrun;
	unsigned long flags;

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
	 * functions which use timr->kclock->clock_get() work.
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
 * On PREEMPT_RT this prevent priority inversion against softirq kthread in
 * case it gets preempted while executing a timer callback. See comments in
 * hrtimer_cancel_wait_running. For PREEMPT_RT=n this just results in a
 * cpu_relax().
 */
static struct k_itimer *timer_wait_running(struct k_itimer *timer,
					   unsigned long *flags)
{
	const struct k_clock *kc = READ_ONCE(timer->kclock);
	timer_t timer_id = READ_ONCE(timer->it_id);

	/* Prevent kfree(timer) after dropping the lock */
	rcu_read_lock();
	unlock_timer(timer, *flags);

	if (!WARN_ON_ONCE(!kc->timer_wait_running))
		kc->timer_wait_running(timer);

	rcu_read_unlock();
	/* Relock the timer. It might be not longer hashed. */
	return lock_timer(timer_id, flags);
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
	timr->it_requeue_pending = (timr->it_requeue_pending + 2) &
		~REQUEUE_PENDING;
	timr->it_overrun_last = 0;

	/* Switch off the timer when it_value is zero */
	if (!new_setting->it_value.tv_sec && !new_setting->it_value.tv_nsec)
		return 0;

	timr->it_interval = timespec64_to_ktime(new_setting->it_interval);
	expires = timespec64_to_ktime(new_setting->it_value);
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
	int error = 0;

	if (!timespec64_valid(&new_spec64->it_interval) ||
	    !timespec64_valid(&new_spec64->it_value))
		return -EINVAL;

	if (old_spec64)
		memset(old_spec64, 0, sizeof(*old_spec64));

	timr = lock_timer(timer_id, &flags);
retry:
	if (!timr)
		return -EINVAL;

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
	struct itimerspec64 new_spec, old_spec;
	struct itimerspec64 *rtn = old_setting ? &old_spec : NULL;
	int error = 0;

	if (!new_setting)
		return -EINVAL;

	if (get_itimerspec64(&new_spec, new_setting))
		return -EFAULT;

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
	list_del(&timer->list);
	spin_unlock(&current->sighand->siglock);
	/*
	 * This keeps any tasks waiting on the spin lock from thinking
	 * they got something (see the lock code above).
	 */
	timer->it_signal = NULL;

	unlock_timer(timer, flags);
	release_posix_timer(timer, IT_ID_SET);
	return 0;
}

/*
 * return timer owned by the process, used by exit_itimers
 */
static void itimer_delete(struct k_itimer *timer)
{
retry_delete:
	spin_lock_irq(&timer->it_lock);

	if (timer_delete_hook(timer) == TIMER_RETRY) {
		spin_unlock_irq(&timer->it_lock);
		goto retry_delete;
	}
	list_del(&timer->list);

	spin_unlock_irq(&timer->it_lock);
	release_posix_timer(timer, IT_ID_SET);
}

/*
 * This is called by do_exit or de_thread, only when there are no more
 * references to the shared signal_struct.
 */
void exit_itimers(struct signal_struct *sig)
{
	struct k_itimer *tmr;

	while (!list_empty(&sig->posix_timers)) {
		tmr = list_entry(sig->posix_timers.next, struct k_itimer, list);
		itimer_delete(tmr);
	}
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

	error = kc->clock_get(which_clock, &kernel_tp);

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

	err = kc->clock_get(which_clock, &ts);

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

	if (err >= 0)
		err = put_old_timex32(utp, &ktx);

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
 * nanosleep for monotonic and realtime clocks
 */
static int common_nsleep(const clockid_t which_clock, int flags,
			 const struct timespec64 *rqtp)
{
	return hrtimer_nanosleep(rqtp, flags & TIMER_ABSTIME ?
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
	current->restart_block.nanosleep.type = rmtp ? TT_COMPAT : TT_NONE;
	current->restart_block.nanosleep.compat_rmtp = rmtp;

	return kc->nsleep(which_clock, flags, &t);
}

#endif

static const struct k_clock clock_realtime = {
	.clock_getres		= posix_get_hrtimer_res,
	.clock_get		= posix_clock_realtime_get,
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
	.clock_get		= posix_ktime_get_ts,
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

static const struct k_clock clock_monotonic_raw = {
	.clock_getres		= posix_get_hrtimer_res,
	.clock_get		= posix_get_monotonic_raw,
};

static const struct k_clock clock_realtime_coarse = {
	.clock_getres		= posix_get_coarse_res,
	.clock_get		= posix_get_realtime_coarse,
};

static const struct k_clock clock_monotonic_coarse = {
	.clock_getres		= posix_get_coarse_res,
	.clock_get		= posix_get_monotonic_coarse,
};

static const struct k_clock clock_tai = {
	.clock_getres		= posix_get_hrtimer_res,
	.clock_get		= posix_get_tai,
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
	.clock_get		= posix_get_boottime,
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
