/*
 * linux/kernel/itimer.c
 *
 * Copyright (C) 1992 Darren Senn
 */

/* These are all the functions necessary to implement itimers */

#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/syscalls.h>
#include <linux/time.h>
#include <linux/sched/signal.h>
#include <linux/sched/cputime.h>
#include <linux/posix-timers.h>
#include <linux/hrtimer.h>
#include <trace/events/timer.h>
#include <linux/compat.h>

#include <linux/uaccess.h>

/**
 * itimer_get_remtime - get remaining time for the timer
 *
 * @timer: the timer to read
 *
 * Returns the delta between the expiry time and now, which can be
 * less than zero or 1usec for an pending expired timer
 */
static struct timeval itimer_get_remtime(struct hrtimer *timer)
{
	ktime_t rem = __hrtimer_get_remaining(timer, true);

	/*
	 * Racy but safe: if the itimer expires after the above
	 * hrtimer_get_remtime() call but before this condition
	 * then we return 0 - which is correct.
	 */
	if (hrtimer_active(timer)) {
		if (rem <= 0)
			rem = NSEC_PER_USEC;
	} else
		rem = 0;

	return ktime_to_timeval(rem);
}

static void get_cpu_itimer(struct task_struct *tsk, unsigned int clock_id,
			   struct itimerval *const value)
{
	u64 val, interval;
	struct cpu_itimer *it = &tsk->signal->it[clock_id];

	spin_lock_irq(&tsk->sighand->siglock);

	val = it->expires;
	interval = it->incr;
	if (val) {
		struct task_cputime cputime;
		u64 t;

		thread_group_cputimer(tsk, &cputime);
		if (clock_id == CPUCLOCK_PROF)
			t = cputime.utime + cputime.stime;
		else
			/* CPUCLOCK_VIRT */
			t = cputime.utime;

		if (val < t)
			/* about to fire */
			val = TICK_NSEC;
		else
			val -= t;
	}

	spin_unlock_irq(&tsk->sighand->siglock);

	value->it_value = ns_to_timeval(val);
	value->it_interval = ns_to_timeval(interval);
}

int do_getitimer(int which, struct itimerval *value)
{
	struct task_struct *tsk = current;

	switch (which) {
	case ITIMER_REAL:
		spin_lock_irq(&tsk->sighand->siglock);
		value->it_value = itimer_get_remtime(&tsk->signal->real_timer);
		value->it_interval =
			ktime_to_timeval(tsk->signal->it_real_incr);
		spin_unlock_irq(&tsk->sighand->siglock);
		break;
	case ITIMER_VIRTUAL:
		get_cpu_itimer(tsk, CPUCLOCK_VIRT, value);
		break;
	case ITIMER_PROF:
		get_cpu_itimer(tsk, CPUCLOCK_PROF, value);
		break;
	default:
		return(-EINVAL);
	}
	return 0;
}

SYSCALL_DEFINE2(getitimer, int, which, struct itimerval __user *, value)
{
	int error = -EFAULT;
	struct itimerval get_buffer;

	if (value) {
		error = do_getitimer(which, &get_buffer);
		if (!error &&
		    copy_to_user(value, &get_buffer, sizeof(get_buffer)))
			error = -EFAULT;
	}
	return error;
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE2(getitimer, int, which,
		       struct compat_itimerval __user *, it)
{
	struct itimerval kit;
	int error = do_getitimer(which, &kit);

	if (!error && put_compat_itimerval(it, &kit))
		error = -EFAULT;
	return error;
}
#endif


/*
 * The timer is automagically restarted, when interval != 0
 */
enum hrtimer_restart it_real_fn(struct hrtimer *timer)
{
	struct signal_struct *sig =
		container_of(timer, struct signal_struct, real_timer);

	trace_itimer_expire(ITIMER_REAL, sig->leader_pid, 0);
	kill_pid_info(SIGALRM, SEND_SIG_PRIV, sig->leader_pid);

	return HRTIMER_NORESTART;
}

static void set_cpu_itimer(struct task_struct *tsk, unsigned int clock_id,
			   const struct itimerval *const value,
			   struct itimerval *const ovalue)
{
	u64 oval, nval, ointerval, ninterval;
	struct cpu_itimer *it = &tsk->signal->it[clock_id];

	/*
	 * Use the to_ktime conversion because that clamps the maximum
	 * value to KTIME_MAX and avoid multiplication overflows.
	 */
	nval = ktime_to_ns(timeval_to_ktime(value->it_value));
	ninterval = ktime_to_ns(timeval_to_ktime(value->it_interval));

	spin_lock_irq(&tsk->sighand->siglock);

	oval = it->expires;
	ointerval = it->incr;
	if (oval || nval) {
		if (nval > 0)
			nval += TICK_NSEC;
		set_process_cpu_timer(tsk, clock_id, &nval, &oval);
	}
	it->expires = nval;
	it->incr = ninterval;
	trace_itimer_state(clock_id == CPUCLOCK_VIRT ?
			   ITIMER_VIRTUAL : ITIMER_PROF, value, nval);

	spin_unlock_irq(&tsk->sighand->siglock);

	if (ovalue) {
		ovalue->it_value = ns_to_timeval(oval);
		ovalue->it_interval = ns_to_timeval(ointerval);
	}
}

/*
 * Returns true if the timeval is in canonical form
 */
#define timeval_valid(t) \
	(((t)->tv_sec >= 0) && (((unsigned long) (t)->tv_usec) < USEC_PER_SEC))

int do_setitimer(int which, struct itimerval *value, struct itimerval *ovalue)
{
	struct task_struct *tsk = current;
	struct hrtimer *timer;
	ktime_t expires;

	/*
	 * Validate the timevals in value.
	 */
	if (!timeval_valid(&value->it_value) ||
	    !timeval_valid(&value->it_interval))
		return -EINVAL;

	switch (which) {
	case ITIMER_REAL:
again:
		spin_lock_irq(&tsk->sighand->siglock);
		timer = &tsk->signal->real_timer;
		if (ovalue) {
			ovalue->it_value = itimer_get_remtime(timer);
			ovalue->it_interval
				= ktime_to_timeval(tsk->signal->it_real_incr);
		}
		/* We are sharing ->siglock with it_real_fn() */
		if (hrtimer_try_to_cancel(timer) < 0) {
			spin_unlock_irq(&tsk->sighand->siglock);
			goto again;
		}
		expires = timeval_to_ktime(value->it_value);
		if (expires != 0) {
			tsk->signal->it_real_incr =
				timeval_to_ktime(value->it_interval);
			hrtimer_start(timer, expires, HRTIMER_MODE_REL);
		} else
			tsk->signal->it_real_incr = 0;

		trace_itimer_state(ITIMER_REAL, value, 0);
		spin_unlock_irq(&tsk->sighand->siglock);
		break;
	case ITIMER_VIRTUAL:
		set_cpu_itimer(tsk, CPUCLOCK_VIRT, value, ovalue);
		break;
	case ITIMER_PROF:
		set_cpu_itimer(tsk, CPUCLOCK_PROF, value, ovalue);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

#ifdef __ARCH_WANT_SYS_ALARM

/**
 * alarm_setitimer - set alarm in seconds
 *
 * @seconds:	number of seconds until alarm
 *		0 disables the alarm
 *
 * Returns the remaining time in seconds of a pending timer or 0 when
 * the timer is not active.
 *
 * On 32 bit machines the seconds value is limited to (INT_MAX/2) to avoid
 * negative timeval settings which would cause immediate expiry.
 */
static unsigned int alarm_setitimer(unsigned int seconds)
{
	struct itimerval it_new, it_old;

#if BITS_PER_LONG < 64
	if (seconds > INT_MAX)
		seconds = INT_MAX;
#endif
	it_new.it_value.tv_sec = seconds;
	it_new.it_value.tv_usec = 0;
	it_new.it_interval.tv_sec = it_new.it_interval.tv_usec = 0;

	do_setitimer(ITIMER_REAL, &it_new, &it_old);

	/*
	 * We can't return 0 if we have an alarm pending ...  And we'd
	 * better return too much than too little anyway
	 */
	if ((!it_old.it_value.tv_sec && it_old.it_value.tv_usec) ||
	      it_old.it_value.tv_usec >= 500000)
		it_old.it_value.tv_sec++;

	return it_old.it_value.tv_sec;
}

/*
 * For backwards compatibility?  This can be done in libc so Alpha
 * and all newer ports shouldn't need it.
 */
SYSCALL_DEFINE1(alarm, unsigned int, seconds)
{
	return alarm_setitimer(seconds);
}

#endif

SYSCALL_DEFINE3(setitimer, int, which, struct itimerval __user *, value,
		struct itimerval __user *, ovalue)
{
	struct itimerval set_buffer, get_buffer;
	int error;

	if (value) {
		if(copy_from_user(&set_buffer, value, sizeof(set_buffer)))
			return -EFAULT;
	} else {
		memset(&set_buffer, 0, sizeof(set_buffer));
		printk_once(KERN_WARNING "%s calls setitimer() with new_value NULL pointer."
			    " Misfeature support will be removed\n",
			    current->comm);
	}

	error = do_setitimer(which, &set_buffer, ovalue ? &get_buffer : NULL);
	if (error || !ovalue)
		return error;

	if (copy_to_user(ovalue, &get_buffer, sizeof(get_buffer)))
		return -EFAULT;
	return 0;
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE3(setitimer, int, which,
		       struct compat_itimerval __user *, in,
		       struct compat_itimerval __user *, out)
{
	struct itimerval kin, kout;
	int error;

	if (in) {
		if (get_compat_itimerval(&kin, in))
			return -EFAULT;
	} else {
		memset(&kin, 0, sizeof(kin));
	}

	error = do_setitimer(which, &kin, out ? &kout : NULL);
	if (error || !out)
		return error;
	if (put_compat_itimerval(out, &kout))
		return -EFAULT;
	return 0;
}
#endif
