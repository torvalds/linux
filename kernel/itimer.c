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
int it_real_fn(void *data)
{
	struct task_struct *tsk = (struct task_struct *) data;

	send_group_sig_info(SIGALRM, SEND_SIG_PRIV, tsk);

	if (tsk->signal->it_real_incr.tv64 != 0) {
		hrtimer_forward(&tsk->signal->real_timer,
			       tsk->signal->it_real_incr);

		return HRTIMER_RESTART;
	}
	return HRTIMER_NORESTART;
}

int do_setitimer(int which, struct itimerval *value, struct itimerval *ovalue)
{
	struct task_struct *tsk = current;
	struct hrtimer *timer;
	ktime_t expires;
	cputime_t cval, cinterval, nval, ninterval;

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
