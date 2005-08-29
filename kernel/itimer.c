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

#include <asm/uaccess.h>

static unsigned long it_real_value(struct signal_struct *sig)
{
	unsigned long val = 0;
	if (timer_pending(&sig->real_timer)) {
		val = sig->real_timer.expires - jiffies;

		/* look out for negative/zero itimer.. */
		if ((long) val <= 0)
			val = 1;
	}
	return val;
}

int do_getitimer(int which, struct itimerval *value)
{
	struct task_struct *tsk = current;
	unsigned long interval, val;
	cputime_t cinterval, cval;

	switch (which) {
	case ITIMER_REAL:
		spin_lock_irq(&tsk->sighand->siglock);
		interval = tsk->signal->it_real_incr;
		val = it_real_value(tsk->signal);
		spin_unlock_irq(&tsk->sighand->siglock);
		jiffies_to_timeval(val, &value->it_value);
		jiffies_to_timeval(interval, &value->it_interval);
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


void it_real_fn(unsigned long __data)
{
	struct task_struct * p = (struct task_struct *) __data;
	unsigned long inc = p->signal->it_real_incr;

	send_group_sig_info(SIGALRM, SEND_SIG_PRIV, p);

	/*
	 * Now restart the timer if necessary.  We don't need any locking
	 * here because do_setitimer makes sure we have finished running
	 * before it touches anything.
	 * Note, we KNOW we are (or should be) at a jiffie edge here so
	 * we don't need the +1 stuff.  Also, we want to use the prior
	 * expire value so as to not "slip" a jiffie if we are late.
	 * Deal with requesting a time prior to "now" here rather than
	 * in add_timer.
	 */
	if (!inc)
		return;
	while (time_before_eq(p->signal->real_timer.expires, jiffies))
		p->signal->real_timer.expires += inc;
	add_timer(&p->signal->real_timer);
}

int do_setitimer(int which, struct itimerval *value, struct itimerval *ovalue)
{
	struct task_struct *tsk = current;
 	unsigned long val, interval, expires;
	cputime_t cval, cinterval, nval, ninterval;

	switch (which) {
	case ITIMER_REAL:
again:
		spin_lock_irq(&tsk->sighand->siglock);
		interval = tsk->signal->it_real_incr;
		val = it_real_value(tsk->signal);
		/* We are sharing ->siglock with it_real_fn() */
		if (try_to_del_timer_sync(&tsk->signal->real_timer) < 0) {
			spin_unlock_irq(&tsk->sighand->siglock);
			goto again;
		}
		tsk->signal->it_real_incr =
			timeval_to_jiffies(&value->it_interval);
		expires = timeval_to_jiffies(&value->it_value);
		if (expires)
			mod_timer(&tsk->signal->real_timer,
				  jiffies + 1 + expires);
		spin_unlock_irq(&tsk->sighand->siglock);
		if (ovalue) {
			jiffies_to_timeval(val, &ovalue->it_value);
			jiffies_to_timeval(interval,
					   &ovalue->it_interval);
		}
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
