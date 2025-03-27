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
#include <linux/compat.h>
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/jhash.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/memblock.h>
#include <linux/nospec.h>
#include <linux/posix-clock.h>
#include <linux/posix-timers.h>
#include <linux/prctl.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/time.h>
#include <linux/time_namespace.h>
#include <linux/uaccess.h>

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
struct timer_hash_bucket {
	spinlock_t		lock;
	struct hlist_head	head;
};

static struct {
	struct timer_hash_bucket	*buckets;
	unsigned long			mask;
} __timer_data __ro_after_init __aligned(2*sizeof(long));

#define timer_buckets	(__timer_data.buckets)
#define timer_hashmask	(__timer_data.mask)

static const struct k_clock * const posix_clocks[];
static const struct k_clock *clockid_to_kclock(const clockid_t id);
static const struct k_clock clock_realtime, clock_monotonic;

#define TIMER_ANY_ID		INT_MIN

/* SIGEV_THREAD_ID cannot share a bit with the other SIGEV values. */
#if SIGEV_THREAD_ID != (SIGEV_THREAD_ID & \
			~(SIGEV_SIGNAL | SIGEV_NONE | SIGEV_THREAD))
#error "SIGEV_THREAD_ID must not share bit with other SIGEV values!"
#endif

static struct k_itimer *__lock_timer(timer_t timer_id);

#define lock_timer(tid)							\
({	struct k_itimer *__timr;					\
	__cond_lock(&__timr->it_lock, __timr = __lock_timer(tid));	\
	__timr;								\
})

static inline void unlock_timer(struct k_itimer *timr)
{
	if (likely((timr)))
		spin_unlock_irq(&timr->it_lock);
}

#define scoped_timer_get_or_fail(_id)					\
	scoped_cond_guard(lock_timer, return -EINVAL, _id)

#define scoped_timer				(scope)

DEFINE_CLASS(lock_timer, struct k_itimer *, unlock_timer(_T), __lock_timer(id), timer_t id);
DEFINE_CLASS_IS_COND_GUARD(lock_timer);

static struct timer_hash_bucket *hash_bucket(struct signal_struct *sig, unsigned int nr)
{
	return &timer_buckets[jhash2((u32 *)&sig, sizeof(sig) / sizeof(u32), nr) & timer_hashmask];
}

static struct k_itimer *posix_timer_by_id(timer_t id)
{
	struct signal_struct *sig = current->signal;
	struct timer_hash_bucket *bucket = hash_bucket(sig, id);
	struct k_itimer *timer;

	hlist_for_each_entry_rcu(timer, &bucket->head, t_hash) {
		/* timer->it_signal can be set concurrently */
		if ((READ_ONCE(timer->it_signal) == sig) && (timer->it_id == id))
			return timer;
	}
	return NULL;
}

static inline struct signal_struct *posix_sig_owner(const struct k_itimer *timer)
{
	unsigned long val = (unsigned long)timer->it_signal;

	/*
	 * Mask out bit 0, which acts as invalid marker to prevent
	 * posix_timer_by_id() detecting it as valid.
	 */
	return (struct signal_struct *)(val & ~1UL);
}

static bool posix_timer_hashed(struct timer_hash_bucket *bucket, struct signal_struct *sig,
			       timer_t id)
{
	struct hlist_head *head = &bucket->head;
	struct k_itimer *timer;

	hlist_for_each_entry_rcu(timer, head, t_hash, lockdep_is_held(&bucket->lock)) {
		if ((posix_sig_owner(timer) == sig) && (timer->it_id == id))
			return true;
	}
	return false;
}

static bool posix_timer_add_at(struct k_itimer *timer, struct signal_struct *sig, unsigned int id)
{
	struct timer_hash_bucket *bucket = hash_bucket(sig, id);

	scoped_guard (spinlock, &bucket->lock) {
		/*
		 * Validate under the lock as this could have raced against
		 * another thread ending up with the same ID, which is
		 * highly unlikely, but possible.
		 */
		if (!posix_timer_hashed(bucket, sig, id)) {
			/*
			 * Set the timer ID and the signal pointer to make
			 * it identifiable in the hash table. The signal
			 * pointer has bit 0 set to indicate that it is not
			 * yet fully initialized. posix_timer_hashed()
			 * masks this bit out, but the syscall lookup fails
			 * to match due to it being set. This guarantees
			 * that there can't be duplicate timer IDs handed
			 * out.
			 */
			timer->it_id = (timer_t)id;
			timer->it_signal = (struct signal_struct *)((unsigned long)sig | 1UL);
			hlist_add_head_rcu(&timer->t_hash, &bucket->head);
			return true;
		}
	}
	return false;
}

static int posix_timer_add(struct k_itimer *timer, int req_id)
{
	struct signal_struct *sig = current->signal;

	if (unlikely(req_id != TIMER_ANY_ID)) {
		if (!posix_timer_add_at(timer, sig, req_id))
			return -EBUSY;

		/*
		 * Move the ID counter past the requested ID, so that after
		 * switching back to normal mode the IDs are outside of the
		 * exact allocated region. That avoids ID collisions on the
		 * next regular timer_create() invocations.
		 */
		atomic_set(&sig->next_posix_timer_id, req_id + 1);
		return req_id;
	}

	for (unsigned int cnt = 0; cnt <= INT_MAX; cnt++) {
		/* Get the next timer ID and clamp it to positive space */
		unsigned int id = atomic_fetch_inc(&sig->next_posix_timer_id) & INT_MAX;

		if (posix_timer_add_at(timer, sig, id))
			return id;
		cond_resched();
	}
	/* POSIX return code when no timer ID could be allocated */
	return -EAGAIN;
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
	posix_timers_cache = kmem_cache_create("posix_timers_cache", sizeof(struct k_itimer),
					       __alignof__(struct k_itimer), SLAB_ACCOUNT, NULL);
	return 0;
}
__initcall(init_posix_timers);

/*
 * The siginfo si_overrun field and the return value of timer_getoverrun(2)
 * are of type int. Clamp the overrun value to INT_MAX
 */
static inline int timer_overrun_to_int(struct k_itimer *timr)
{
	if (timr->it_overrun_last > (s64)INT_MAX)
		return INT_MAX;

	return (int)timr->it_overrun_last;
}

static void common_hrtimer_rearm(struct k_itimer *timr)
{
	struct hrtimer *timer = &timr->it.real.timer;

	timr->it_overrun += hrtimer_forward(timer, timer->base->get_time(),
					    timr->it_interval);
	hrtimer_restart(timer);
}

static bool __posixtimer_deliver_signal(struct kernel_siginfo *info, struct k_itimer *timr)
{
	guard(spinlock)(&timr->it_lock);

	/*
	 * Check if the timer is still alive or whether it got modified
	 * since the signal was queued. In either case, don't rearm and
	 * drop the signal.
	 */
	if (timr->it_signal_seq != timr->it_sigqueue_seq || WARN_ON_ONCE(!posixtimer_valid(timr)))
		return false;

	if (!timr->it_interval || WARN_ON_ONCE(timr->it_status != POSIX_TIMER_REQUEUE_PENDING))
		return true;

	timr->kclock->timer_rearm(timr);
	timr->it_status = POSIX_TIMER_ARMED;
	timr->it_overrun_last = timr->it_overrun;
	timr->it_overrun = -1LL;
	++timr->it_signal_seq;
	info->si_overrun = timer_overrun_to_int(timr);
	return true;
}

/*
 * This function is called from the signal delivery code. It decides
 * whether the signal should be dropped and rearms interval timers.  The
 * timer can be unconditionally accessed as there is a reference held on
 * it.
 */
bool posixtimer_deliver_signal(struct kernel_siginfo *info, struct sigqueue *timer_sigq)
{
	struct k_itimer *timr = container_of(timer_sigq, struct k_itimer, sigq);
	bool ret;

	/*
	 * Release siglock to ensure proper locking order versus
	 * timr::it_lock. Keep interrupts disabled.
	 */
	spin_unlock(&current->sighand->siglock);

	ret = __posixtimer_deliver_signal(info, timr);

	/* Drop the reference which was acquired when the signal was queued */
	posixtimer_putref(timr);

	spin_lock(&current->sighand->siglock);
	return ret;
}

void posix_timer_queue_signal(struct k_itimer *timr)
{
	lockdep_assert_held(&timr->it_lock);

	if (!posixtimer_valid(timr))
		return;

	timr->it_status = timr->it_interval ? POSIX_TIMER_REQUEUE_PENDING : POSIX_TIMER_DISARMED;
	posixtimer_send_sigqueue(timr);
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

	guard(spinlock_irqsave)(&timr->it_lock);
	posix_timer_queue_signal(timr);
	return HRTIMER_NORESTART;
}

long posixtimer_create_prctl(unsigned long ctrl)
{
	switch (ctrl) {
	case PR_TIMER_CREATE_RESTORE_IDS_OFF:
		current->signal->timer_create_restore_ids = 0;
		return 0;
	case PR_TIMER_CREATE_RESTORE_IDS_ON:
		current->signal->timer_create_restore_ids = 1;
		return 0;
	case PR_TIMER_CREATE_RESTORE_IDS_GET:
		return current->signal->timer_create_restore_ids;
	}
	return -EINVAL;
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

static struct k_itimer *alloc_posix_timer(void)
{
	struct k_itimer *tmr;

	if (unlikely(!posix_timers_cache))
		return NULL;

	tmr = kmem_cache_zalloc(posix_timers_cache, GFP_KERNEL);
	if (!tmr)
		return tmr;

	if (unlikely(!posixtimer_init_sigqueue(&tmr->sigq))) {
		kmem_cache_free(posix_timers_cache, tmr);
		return NULL;
	}
	rcuref_init(&tmr->rcuref, 1);
	return tmr;
}

void posixtimer_free_timer(struct k_itimer *tmr)
{
	put_pid(tmr->it_pid);
	if (tmr->sigq.ucounts)
		dec_rlimit_put_ucounts(tmr->sigq.ucounts, UCOUNT_RLIMIT_SIGPENDING);
	kfree_rcu(tmr, rcu);
}

static void posix_timer_unhash_and_free(struct k_itimer *tmr)
{
	struct timer_hash_bucket *bucket = hash_bucket(posix_sig_owner(tmr), tmr->it_id);

	scoped_guard (spinlock, &bucket->lock)
		hlist_del_rcu(&tmr->t_hash);
	posixtimer_putref(tmr);
}

static int common_timer_create(struct k_itimer *new_timer)
{
	hrtimer_setup(&new_timer->it.real.timer, posix_timer_fn, new_timer->it_clock, 0);
	return 0;
}

/* Create a POSIX.1b interval timer. */
static int do_timer_create(clockid_t which_clock, struct sigevent *event,
			   timer_t __user *created_timer_id)
{
	const struct k_clock *kc = clockid_to_kclock(which_clock);
	timer_t req_id = TIMER_ANY_ID;
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

	/* Special case for CRIU to restore timers with a given timer ID. */
	if (unlikely(current->signal->timer_create_restore_ids)) {
		if (copy_from_user(&req_id, created_timer_id, sizeof(req_id)))
			return -EFAULT;
		/* Valid IDs are 0..INT_MAX */
		if ((unsigned int)req_id > INT_MAX)
			return -EINVAL;
	}

	/*
	 * Add the timer to the hash table. The timer is not yet valid
	 * after insertion, but has a unique ID allocated.
	 */
	new_timer_id = posix_timer_add(new_timer, req_id);
	if (new_timer_id < 0) {
		posixtimer_free_timer(new_timer);
		return new_timer_id;
	}

	new_timer->it_clock = which_clock;
	new_timer->kclock = kc;
	new_timer->it_overrun = -1LL;

	if (event) {
		scoped_guard (rcu)
			new_timer->it_pid = get_pid(good_sigevent(event));
		if (!new_timer->it_pid) {
			error = -EINVAL;
			goto out;
		}
		new_timer->it_sigev_notify     = event->sigev_notify;
		new_timer->sigq.info.si_signo = event->sigev_signo;
		new_timer->sigq.info.si_value = event->sigev_value;
	} else {
		new_timer->it_sigev_notify     = SIGEV_SIGNAL;
		new_timer->sigq.info.si_signo = SIGALRM;
		new_timer->sigq.info.si_value.sival_int = new_timer->it_id;
		new_timer->it_pid = get_pid(task_tgid(current));
	}

	if (new_timer->it_sigev_notify & SIGEV_THREAD_ID)
		new_timer->it_pid_type = PIDTYPE_PID;
	else
		new_timer->it_pid_type = PIDTYPE_TGID;

	new_timer->sigq.info.si_tid = new_timer->it_id;
	new_timer->sigq.info.si_code = SI_TIMER;

	if (copy_to_user(created_timer_id, &new_timer_id, sizeof (new_timer_id))) {
		error = -EFAULT;
		goto out;
	}
	/*
	 * After succesful copy out, the timer ID is visible to user space
	 * now but not yet valid because new_timer::signal low order bit is 1.
	 *
	 * Complete the initialization with the clock specific create
	 * callback.
	 */
	error = kc->timer_create(new_timer);
	if (error)
		goto out;

	/*
	 * timer::it_lock ensures that __lock_timer() observes a fully
	 * initialized timer when it observes a valid timer::it_signal.
	 *
	 * sighand::siglock is required to protect signal::posix_timers.
	 */
	scoped_guard (spinlock_irq, &new_timer->it_lock) {
		guard(spinlock)(&current->sighand->siglock);
		/*
		 * new_timer::it_signal contains the signal pointer with
		 * bit 0 set, which makes it invalid for syscall operations.
		 * Store the unmodified signal pointer to make it valid.
		 */
		WRITE_ONCE(new_timer->it_signal, current->signal);
		hlist_add_head_rcu(&new_timer->list, &current->signal->posix_timers);
	}
	/*
	 * After unlocking @new_timer is subject to concurrent removal and
	 * cannot be touched anymore
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

static struct k_itimer *__lock_timer(timer_t timer_id)
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
	 * timr::it_signal is marked invalid. timer::it_signal is only set
	 * after the rest of the initialization succeeded.
	 *
	 * Timer destruction happens in steps:
	 *  1) Set timr::it_signal marked invalid with timr::it_lock held
	 *  2) Release timr::it_lock
	 *  3) Remove from the hash under hash_lock
	 *  4) Put the reference count.
	 *
	 * The reference count might not drop to zero if timr::sigq is
	 * queued. In that case the signal delivery or flush will put the
	 * last reference count.
	 *
	 * When the reference count reaches zero, the timer is scheduled
	 * for RCU removal after the grace period.
	 *
	 * Holding rcu_read_lock() across the lookup ensures that
	 * the timer cannot be freed.
	 *
	 * The lookup validates locklessly that timr::it_signal ==
	 * current::it_signal and timr::it_id == @timer_id. timr::it_id
	 * can't change, but timr::it_signal can become invalid during
	 * destruction, which makes the locked check fail.
	 */
	guard(rcu)();
	timr = posix_timer_by_id(timer_id);
	if (timr) {
		spin_lock_irq(&timr->it_lock);
		/*
		 * Validate under timr::it_lock that timr::it_signal is
		 * still valid. Pairs with #1 above.
		 */
		if (timr->it_signal == current->signal)
			return timr;
		spin_unlock_irq(&timr->it_lock);
	}
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
	} else if (timr->it_status == POSIX_TIMER_DISARMED) {
		/*
		 * SIGEV_NONE oneshot timers are never queued and therefore
		 * timr->it_status is always DISARMED. The check below
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
	if (iv && timr->it_status != POSIX_TIMER_ARMED)
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
	memset(setting, 0, sizeof(*setting));
	scoped_timer_get_or_fail(timer_id)
		scoped_timer->kclock->timer_get(scoped_timer, setting);
	return 0;
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
	scoped_timer_get_or_fail(timer_id)
		return timer_overrun_to_int(scoped_timer);
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
	 * hood. See hrtimer_setup(). Update timr->kclock, so the generic
	 * functions which use timr->kclock->clock_get_*() work.
	 *
	 * Note: it_clock stays unmodified, because the next timer_set() might
	 * use ABSTIME, so it needs to switch back.
	 */
	if (timr->it_clock == CLOCK_REALTIME)
		timr->kclock = absolute ? &clock_realtime : &clock_monotonic;

	hrtimer_setup(&timr->it.real.timer, posix_timer_fn, timr->it_clock, mode);

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
static void timer_wait_running(struct k_itimer *timer)
{
	/*
	 * kc->timer_wait_running() might drop RCU lock. So @timer
	 * cannot be touched anymore after the function returns!
	 */
	timer->kclock->timer_wait_running(timer);
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

	/*
	 * Careful here. On SMP systems the timer expiry function could be
	 * active and spinning on timr->it_lock.
	 */
	if (kc->timer_try_to_cancel(timr) < 0)
		return TIMER_RETRY;

	timr->it_status = POSIX_TIMER_DISARMED;
	posix_timer_set_common(timr, new_setting);

	/* Keep timer disarmed when it_value is zero */
	if (!new_setting->it_value.tv_sec && !new_setting->it_value.tv_nsec)
		return 0;

	expires = timespec64_to_ktime(new_setting->it_value);
	if (flags & TIMER_ABSTIME)
		expires = timens_ktime_to_host(timr->it_clock, expires);
	sigev_none = timr->it_sigev_notify == SIGEV_NONE;

	kc->timer_arm(timr, expires, flags & TIMER_ABSTIME, sigev_none);
	if (!sigev_none)
		timr->it_status = POSIX_TIMER_ARMED;
	return 0;
}

static int do_timer_settime(timer_t timer_id, int tmr_flags, struct itimerspec64 *new_spec64,
			    struct itimerspec64 *old_spec64)
{
	if (!timespec64_valid(&new_spec64->it_interval) ||
	    !timespec64_valid(&new_spec64->it_value))
		return -EINVAL;

	if (old_spec64)
		memset(old_spec64, 0, sizeof(*old_spec64));

	for (; ; old_spec64 = NULL) {
		struct k_itimer *timr;

		scoped_timer_get_or_fail(timer_id) {
			timr = scoped_timer;

			if (old_spec64)
				old_spec64->it_interval = ktime_to_timespec64(timr->it_interval);

			/* Prevent signal delivery and rearming. */
			timr->it_signal_seq++;

			int ret = timr->kclock->timer_set(timr, tmr_flags, new_spec64, old_spec64);
			if (ret != TIMER_RETRY)
				return ret;

			/* Protect the timer from being freed when leaving the lock scope */
			rcu_read_lock();
		}
		timer_wait_running(timr);
		rcu_read_unlock();
	}
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

	if (kc->timer_try_to_cancel(timer) < 0)
		return TIMER_RETRY;
	timer->it_status = POSIX_TIMER_DISARMED;
	return 0;
}

/*
 * If the deleted timer is on the ignored list, remove it and
 * drop the associated reference.
 */
static inline void posix_timer_cleanup_ignored(struct k_itimer *tmr)
{
	if (!hlist_unhashed(&tmr->ignored_list)) {
		hlist_del_init(&tmr->ignored_list);
		posixtimer_putref(tmr);
	}
}

static void posix_timer_delete(struct k_itimer *timer)
{
	/*
	 * Invalidate the timer, remove it from the linked list and remove
	 * it from the ignored list if pending.
	 *
	 * The invalidation must be written with siglock held so that the
	 * signal code observes the invalidated timer::it_signal in
	 * do_sigaction(), which prevents it from moving a pending signal
	 * of a deleted timer to the ignore list.
	 *
	 * The invalidation also prevents signal queueing, signal delivery
	 * and therefore rearming from the signal delivery path.
	 *
	 * A concurrent lookup can still find the timer in the hash, but it
	 * will check timer::it_signal with timer::it_lock held and observe
	 * bit 0 set, which invalidates it. That also prevents the timer ID
	 * from being handed out before this timer is completely gone.
	 */
	timer->it_signal_seq++;

	scoped_guard (spinlock, &current->sighand->siglock) {
		unsigned long sig = (unsigned long)timer->it_signal | 1UL;

		WRITE_ONCE(timer->it_signal, (struct signal_struct *)sig);
		hlist_del_rcu(&timer->list);
		posix_timer_cleanup_ignored(timer);
	}

	while (timer->kclock->timer_del(timer) == TIMER_RETRY) {
		guard(rcu)();
		spin_unlock_irq(&timer->it_lock);
		timer_wait_running(timer);
		spin_lock_irq(&timer->it_lock);
	}
}

/* Delete a POSIX.1b interval timer. */
SYSCALL_DEFINE1(timer_delete, timer_t, timer_id)
{
	struct k_itimer *timer;

	scoped_timer_get_or_fail(timer_id) {
		timer = scoped_timer;
		posix_timer_delete(timer);
	}
	/* Remove it from the hash, which frees up the timer ID */
	posix_timer_unhash_and_free(timer);
	return 0;
}

/*
 * Invoked from do_exit() when the last thread of a thread group exits.
 * At that point no other task can access the timers of the dying
 * task anymore.
 */
void exit_itimers(struct task_struct *tsk)
{
	struct hlist_head timers;
	struct hlist_node *next;
	struct k_itimer *timer;

	/* Clear restore mode for exec() */
	tsk->signal->timer_create_restore_ids = 0;

	if (hlist_empty(&tsk->signal->posix_timers))
		return;

	/* Protect against concurrent read via /proc/$PID/timers */
	scoped_guard (spinlock_irq, &tsk->sighand->siglock)
		hlist_move_list(&tsk->signal->posix_timers, &timers);

	/* The timers are not longer accessible via tsk::signal */
	hlist_for_each_entry_safe(timer, next, &timers, list) {
		scoped_guard (spinlock_irq, &timer->it_lock)
			posix_timer_delete(timer);
		posix_timer_unhash_and_free(timer);
		cond_resched();
	}

	/*
	 * There should be no timers on the ignored list. itimer_delete() has
	 * mopped them up.
	 */
	if (!WARN_ON_ONCE(!hlist_empty(&tsk->signal->ignored_posix_timers)))
		return;

	hlist_move_list(&tsk->signal->ignored_posix_timers, &timers);
	while (!hlist_empty(&timers)) {
		posix_timer_cleanup_ignored(hlist_entry(timers.first, struct k_itimer,
							ignored_list));
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

static int __init posixtimer_init(void)
{
	unsigned long i, size;
	unsigned int shift;

	if (IS_ENABLED(CONFIG_BASE_SMALL))
		size = 512;
	else
		size = roundup_pow_of_two(512 * num_possible_cpus());

	timer_buckets = alloc_large_system_hash("posixtimers", sizeof(*timer_buckets),
						size, 0, 0, &shift, NULL, size, size);
	size = 1UL << shift;
	timer_hashmask = size - 1;

	for (i = 0; i < size; i++) {
		spin_lock_init(&timer_buckets[i].lock);
		INIT_HLIST_HEAD(&timer_buckets[i].head);
	}
	return 0;
}
core_initcall(posixtimer_init);
