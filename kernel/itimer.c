/*
 * linux/kernel/itimer.c
 *
 * Copyright (C) 1992 Darren Senn
 */

/* These are all the functions necessary to implement itimers */

#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/syscalls.h>
#include <linux/time.h>
#include <linux/posix-timers.h>
#include <linux/hrtimer.h>

#include <asm/uaccess.h>

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
	ktime_t rem = hrtimer_get_remaining(timer);

	/*
	 * Racy but safe: if the itimer expires after the above
	 * hrtimer_get_remtime() call but before this condition
	 * then we return 0 - which is correct.
	 */
	if (hrtimer_active(timer)) {
		if (rem.tv64 <= 0)
			rem.tv64 = NSEC_PER_USEC;
	} else
		rem.tv64 = 0;

	return ktime_to_timeval(rem);
}

int do_getitimer(int which, struct itimerval *value)
{
	struct task_struct *tsk = current;
	cputime_t cinterval, cval;

	switch (which) {
	case ITIMER_REAL:
		spin_lock_irq(&tsk->sighand->siglock);
		value->it_value = itimer_get_remtime(&tsk->signal->real_timer);
		value->it_interval =
			ktime_to_timeval(tsk->signal->it_real_incr);
		spin_unlock_irq(&tsk->sighand->siglock);
		break;
	case ITIMER_VIRTUAL:
		read_lock(&tasklist_lock);
		spin_lock_irq(&tsk->sighand->siglock);
		cval = tsk->signal->it_virt_expires;
		cinterval = tsk->signal->it_virt_incr;
		if (!cputime_eq(cval, cputime_zero)) {
			struct task_struct *t = tsk;
			cputime_t utime = tsk->signal->utime;
			do {
				utime = cputime_add(utime, t->utime);
				t = next_thread(t);
			} while (t != tsk);
			if (cputime_le(cval, utime)) { /* about to fire */
				cval = jiffies_to_cputime(1);
			} else {
				cval = cputime_sub(cval, utime);
			}
		}
		spin_unlock_irq(&tsk->sighand->siglock);
		read_unlock(&tasklist_lock);
		cputime_to_timeval(cval, &value->it_value);
		cputime_to_timeval(cinterval, &value->it_interval);
		break;
	case ITIMER_PROF:
		read_lock(&tasklist_lock);
		spin_lock_irq(&tsk->sighand->siglock);
		cval = tsk->signal->it_prof_expires;
		cinterval = tsk->signal->it_prof_incr;
		if (!cputime_eq(cval, cputime_zero)) {
			struct task_struct *t = tsk;
			cputime_t ptime = cputime_add(tsk->signal->utime,
						      tsk->signal->stime);
			do {
				ptime = cputime_add(ptime,
						    cputime_add(t->utime,
								t->stime));
				t = next_thread(t);
			} while (t != tsk);
			if (cputime_le(cval, ptime)) { /* about to fire */
				cval = jiffies_to_cputime(1);
			} else {
				cval = cputime_sub(cval, ptime);
			}
		}
		spin_unlock_irq(&tsk->sighand->siglock);
		read_unlock(&tasklist_lock);
		cputime_to_timeval(cval, &value->it_value);
		cputime_to_timeval(cinterval, &value->it_interval);
		break;
	default:
		return(-EINVAL);
	}
	return 0;
}

asmlinkage long sys_getitimer(int which, struct itimerval __user *value)
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


/*
 * The timer is automagically restarted, when interval != 0
 */
int it_real_fn(struct hrtimer *timer)
{
	struct signal_struct *sig =
	    container_of(timer, struct signal_struct, real_timer);

	send_group_sig_info(SIGALRM, SEND_SIG_PRIV, sig->tsk);

	if (sig->it_real_incr.tv64 != 0) {
		hrtimer_forward(timer, timer->base->softirq_time,
				sig->it_real_incr);
		return HRTIMER_RESTART;
	}
	return HRTIMER_NORESTART;
}

/*
 * We do not care about correctness. We just sanitize the values so
 * the ktime_t operations which expect normalized values do not
 * break. This converts negative values to long timeouts similar to
 * the code in kernel versions < 2.6.16
 *
 * Print a limited number of warning messages when an invalid timeval
 * is detected.
 */
static void fixup_timeval(struct timeval *tv, int interval)
{
	static int warnlimit = 10;
	unsigned long tmp;

	if (warnlimit > 0) {
		warnlimit--;
		printk(KERN_WARNING
		       "setitimer: %s (pid = %d) provided "
		       "invalid timeval %s: tv_sec = %ld tv_usec = %ld\n",
		       current->comm, current->pid,
		       interval ? "it_interval" : "it_value",
		       tv->tv_sec, (long) tv->tv_usec);
	}

	tmp = tv->tv_usec;
	if (tmp >= USEC_PER_SEC) {
		tv->tv_usec = tmp % USEC_PER_SEC;
		tv->tv_sec += tmp / USEC_PER_SEC;
	}

	tmp = tv->tv_sec;
	if (tmp > LONG_MAX)
		tv->tv_sec = LONG_MAX;
}

/*
 * Returns true if the timeval is in canonical form
 */
#define timeval_valid(t) \
	(((t)->tv_sec >= 0) && (((unsigned long) (t)->tv_usec) < USEC_PER_SEC))

/*
 * Check for invalid timevals, sanitize them and print a limited
 * number of warnings.
 */
static void check_itimerval(struct itimerval *value) {

	if (unlikely(!timeval_valid(&value->it_value)))
		fixup_timeval(&value->it_value, 0);

	if (unlikely(!timeval_valid(&value->it_interval)))
		fixup_timeval(&value->it_interval, 1);
}

int do_setitimer(int which, struct itimerval *value, struct itimerval *ovalue)
{
	struct task_struct *tsk = current;
	struct hrtimer *timer;
	ktime_t expires;
	cputime_t cval, cinterval, nval, ninterval;

	/*
	 * Validate the timevals in value.
	 *
	 * Note: Although the spec requires that invalid values shall
	 * return -EINVAL, we just fixup the value and print a limited
	 * number of warnings in order not to break users of this
	 * historical misfeature.
	 *
	 * Scheduled for replacement in March 2007
	 */
	check_itimerval(value);

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
		tsk->signal->it_real_incr =
			timeval_to_ktime(value->it_interval);
		expires = timeval_to_ktime(value->it_value);
		if (expires.tv64 != 0)
			hrtimer_start(timer, expires, HRTIMER_REL);
		spin_unlock_irq(&tsk->sighand->siglock);
		break;
	case ITIMER_VIRTUAL:
		nval = timeval_to_cputime(&value->it_value);
		ninterval = timeval_to_cputime(&value->it_interval);
		read_lock(&tasklist_lock);
		spin_lock_irq(&tsk->sighand->siglock);
		cval = tsk->signal->it_virt_expires;
		cinterval = tsk->signal->it_virt_incr;
		if (!cputime_eq(cval, cputime_zero) ||
		    !cputime_eq(nval, cputime_zero)) {
			if (cputime_gt(nval, cputime_zero))
				nval = cputime_add(nval,
						   jiffies_to_cputime(1));
			set_process_cpu_timer(tsk, CPUCLOCK_VIRT,
					      &nval, &cval);
		}
		tsk->signal->it_virt_expires = nval;
		tsk->signal->it_virt_incr = ninterval;
		spin_unlock_irq(&tsk->sighand->siglock);
		read_unlock(&tasklist_lock);
		if (ovalue) {
			cputime_to_timeval(cval, &ovalue->it_value);
			cputime_to_timeval(cinterval, &ovalue->it_interval);
		}
		break;
	case ITIMER_PROF:
		nval = timeval_to_cputime(&value->it_value);
		ninterval = timeval_to_cputime(&value->it_interval);
		read_lock(&tasklist_lock);
		spin_lock_irq(&tsk->sighand->siglock);
		cval = tsk->signal->it_prof_expires;
		cinterval = tsk->signal->it_prof_incr;
		if (!cputime_eq(cval, cputime_zero) ||
		    !cputime_eq(nval, cputime_zero)) {
			if (cputime_gt(nval, cputime_zero))
				nval = cputime_add(nval,
						   jiffies_to_cputime(1));
			set_process_cpu_timer(tsk, CPUCLOCK_PROF,
					      &nval, &cval);
		}
		tsk->signal->it_prof_expires = nval;
		tsk->signal->it_prof_incr = ninterval;
		spin_unlock_irq(&tsk->sighand->siglock);
		read_unlock(&tasklist_lock);
		if (ovalue) {
			cputime_to_timeval(cval, &ovalue->it_value);
			cputime_to_timeval(cinterval, &ovalue->it_interval);
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

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
unsigned int alarm_setitimer(unsigned int seconds)
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

asmlinkage long sys_setitimer(int which,
			      struct itimerval __user *value,
			      struct itimerval __user *ovalue)
{
	struct itimerval set_buffer, get_buffer;
	int error;

	if (value) {
		if(copy_from_user(&set_buffer, value, sizeof(set_buffer)))
			return -EFAULT;
	} else
		memset((char *) &set_buffer, 0, sizeof(set_buffer));

	error = do_setitimer(which, &set_buffer, ovalue ? &get_buffer : NULL);
	if (error || !ovalue)
		return error;

	if (copy_to_user(ovalue, &get_buffer, sizeof(get_buffer)))
		return -EFAULT; 
	return 0;
}
