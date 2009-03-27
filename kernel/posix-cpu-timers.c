/*
 * Implement CPU time clocks for the POSIX clock interface.
 */

#include <linux/sched.h>
#include <linux/posix-timers.h>
#include <linux/errno.h>
#include <linux/math64.h>
#include <asm/uaccess.h>
#include <linux/kernel_stat.h>

/*
 * Called after updating RLIMIT_CPU to set timer expiration if necessary.
 */
void update_rlimit_cpu(unsigned long rlim_new)
{
	cputime_t cputime;

	cputime = secs_to_cputime(rlim_new);
	if (cputime_eq(current->signal->it_prof_expires, cputime_zero) ||
	    cputime_gt(current->signal->it_prof_expires, cputime)) {
		spin_lock_irq(&current->sighand->siglock);
		set_process_cpu_timer(current, CPUCLOCK_PROF, &cputime, NULL);
		spin_unlock_irq(&current->sighand->siglock);
	}
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

	read_lock(&tasklist_lock);
	p = find_task_by_vpid(pid);
	if (!p || !(CPUCLOCK_PERTHREAD(which_clock) ?
		   same_thread_group(p, current) : thread_group_leader(p))) {
		error = -EINVAL;
	}
	read_unlock(&tasklist_lock);

	return error;
}

static inline union cpu_time_count
timespec_to_sample(const clockid_t which_clock, const struct timespec *tp)
{
	union cpu_time_count ret;
	ret.sched = 0;		/* high half always zero when .cpu used */
	if (CPUCLOCK_WHICH(which_clock) == CPUCLOCK_SCHED) {
		ret.sched = (unsigned long long)tp->tv_sec * NSEC_PER_SEC + tp->tv_nsec;
	} else {
		ret.cpu = timespec_to_cputime(tp);
	}
	return ret;
}

static void sample_to_timespec(const clockid_t which_clock,
			       union cpu_time_count cpu,
			       struct timespec *tp)
{
	if (CPUCLOCK_WHICH(which_clock) == CPUCLOCK_SCHED)
		*tp = ns_to_timespec(cpu.sched);
	else
		cputime_to_timespec(cpu.cpu, tp);
}

static inline int cpu_time_before(const clockid_t which_clock,
				  union cpu_time_count now,
				  union cpu_time_count then)
{
	if (CPUCLOCK_WHICH(which_clock) == CPUCLOCK_SCHED) {
		return now.sched < then.sched;
	}  else {
		return cputime_lt(now.cpu, then.cpu);
	}
}
static inline void cpu_time_add(const clockid_t which_clock,
				union cpu_time_count *acc,
			        union cpu_time_count val)
{
	if (CPUCLOCK_WHICH(which_clock) == CPUCLOCK_SCHED) {
		acc->sched += val.sched;
	}  else {
		acc->cpu = cputime_add(acc->cpu, val.cpu);
	}
}
static inline union cpu_time_count cpu_time_sub(const clockid_t which_clock,
						union cpu_time_count a,
						union cpu_time_count b)
{
	if (CPUCLOCK_WHICH(which_clock) == CPUCLOCK_SCHED) {
		a.sched -= b.sched;
	}  else {
		a.cpu = cputime_sub(a.cpu, b.cpu);
	}
	return a;
}

/*
 * Divide and limit the result to res >= 1
 *
 * This is necessary to prevent signal delivery starvation, when the result of
 * the division would be rounded down to 0.
 */
static inline cputime_t cputime_div_non_zero(cputime_t time, unsigned long div)
{
	cputime_t res = cputime_div(time, div);

	return max_t(cputime_t, res, 1);
}

/*
 * Update expiry time from increment, and increase overrun count,
 * given the current clock sample.
 */
static void bump_cpu_timer(struct k_itimer *timer,
				  union cpu_time_count now)
{
	int i;

	if (timer->it.cpu.incr.sched == 0)
		return;

	if (CPUCLOCK_WHICH(timer->it_clock) == CPUCLOCK_SCHED) {
		unsigned long long delta, incr;

		if (now.sched < timer->it.cpu.expires.sched)
			return;
		incr = timer->it.cpu.incr.sched;
		delta = now.sched + incr - timer->it.cpu.expires.sched;
		/* Don't use (incr*2 < delta), incr*2 might overflow. */
		for (i = 0; incr < delta - incr; i++)
			incr = incr << 1;
		for (; i >= 0; incr >>= 1, i--) {
			if (delta < incr)
				continue;
			timer->it.cpu.expires.sched += incr;
			timer->it_overrun += 1 << i;
			delta -= incr;
		}
	} else {
		cputime_t delta, incr;

		if (cputime_lt(now.cpu, timer->it.cpu.expires.cpu))
			return;
		incr = timer->it.cpu.incr.cpu;
		delta = cputime_sub(cputime_add(now.cpu, incr),
				    timer->it.cpu.expires.cpu);
		/* Don't use (incr*2 < delta), incr*2 might overflow. */
		for (i = 0; cputime_lt(incr, cputime_sub(delta, incr)); i++)
			     incr = cputime_add(incr, incr);
		for (; i >= 0; incr = cputime_halve(incr), i--) {
			if (cputime_lt(delta, incr))
				continue;
			timer->it.cpu.expires.cpu =
				cputime_add(timer->it.cpu.expires.cpu, incr);
			timer->it_overrun += 1 << i;
			delta = cputime_sub(delta, incr);
		}
	}
}

static inline cputime_t prof_ticks(struct task_struct *p)
{
	return cputime_add(p->utime, p->stime);
}
static inline cputime_t virt_ticks(struct task_struct *p)
{
	return p->utime;
}

int posix_cpu_clock_getres(const clockid_t which_clock, struct timespec *tp)
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

int posix_cpu_clock_set(const clockid_t which_clock, const struct timespec *tp)
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
static int cpu_clock_sample(const clockid_t which_clock, struct task_struct *p,
			    union cpu_time_count *cpu)
{
	switch (CPUCLOCK_WHICH(which_clock)) {
	default:
		return -EINVAL;
	case CPUCLOCK_PROF:
		cpu->cpu = prof_ticks(p);
		break;
	case CPUCLOCK_VIRT:
		cpu->cpu = virt_ticks(p);
		break;
	case CPUCLOCK_SCHED:
		cpu->sched = p->se.sum_exec_runtime + task_delta_exec(p);
		break;
	}
	return 0;
}

void thread_group_cputime(struct task_struct *tsk, struct task_cputime *times)
{
	struct sighand_struct *sighand;
	struct signal_struct *sig;
	struct task_struct *t;

	*times = INIT_CPUTIME;

	rcu_read_lock();
	sighand = rcu_dereference(tsk->sighand);
	if (!sighand)
		goto out;

	sig = tsk->signal;

	t = tsk;
	do {
		times->utime = cputime_add(times->utime, t->utime);
		times->stime = cputime_add(times->stime, t->stime);
		times->sum_exec_runtime += t->se.sum_exec_runtime;

		t = next_thread(t);
	} while (t != tsk);

	times->utime = cputime_add(times->utime, sig->utime);
	times->stime = cputime_add(times->stime, sig->stime);
	times->sum_exec_runtime += sig->sum_sched_runtime;
out:
	rcu_read_unlock();
}

static void update_gt_cputime(struct task_cputime *a, struct task_cputime *b)
{
	if (cputime_gt(b->utime, a->utime))
		a->utime = b->utime;

	if (cputime_gt(b->stime, a->stime))
		a->stime = b->stime;

	if (b->sum_exec_runtime > a->sum_exec_runtime)
		a->sum_exec_runtime = b->sum_exec_runtime;
}

void thread_group_cputimer(struct task_struct *tsk, struct task_cputime *times)
{
	struct thread_group_cputimer *cputimer = &tsk->signal->cputimer;
	struct task_cputime sum;
	unsigned long flags;

	spin_lock_irqsave(&cputimer->lock, flags);
	if (!cputimer->running) {
		cputimer->running = 1;
		/*
		 * The POSIX timer interface allows for absolute time expiry
		 * values through the TIMER_ABSTIME flag, therefore we have
		 * to synchronize the timer to the clock every time we start
		 * it.
		 */
		thread_group_cputime(tsk, &sum);
		update_gt_cputime(&cputimer->cputime, &sum);
	}
	*times = cputimer->cputime;
	spin_unlock_irqrestore(&cputimer->lock, flags);
}

/*
 * Sample a process (thread group) clock for the given group_leader task.
 * Must be called with tasklist_lock held for reading.
 */
static int cpu_clock_sample_group(const clockid_t which_clock,
				  struct task_struct *p,
				  union cpu_time_count *cpu)
{
	struct task_cputime cputime;

	thread_group_cputime(p, &cputime);
	switch (CPUCLOCK_WHICH(which_clock)) {
	default:
		return -EINVAL;
	case CPUCLOCK_PROF:
		cpu->cpu = cputime_add(cputime.utime, cputime.stime);
		break;
	case CPUCLOCK_VIRT:
		cpu->cpu = cputime.utime;
		break;
	case CPUCLOCK_SCHED:
		cpu->sched = cputime.sum_exec_runtime + task_delta_exec(p);
		break;
	}
	return 0;
}


int posix_cpu_clock_get(const clockid_t which_clock, struct timespec *tp)
{
	const pid_t pid = CPUCLOCK_PID(which_clock);
	int error = -EINVAL;
	union cpu_time_count rtn;

	if (pid == 0) {
		/*
		 * Special case constant value for our own clocks.
		 * We don't have to do any lookup to find ourselves.
		 */
		if (CPUCLOCK_PERTHREAD(which_clock)) {
			/*
			 * Sampling just ourselves we can do with no locking.
			 */
			error = cpu_clock_sample(which_clock,
						 current, &rtn);
		} else {
			read_lock(&tasklist_lock);
			error = cpu_clock_sample_group(which_clock,
						       current, &rtn);
			read_unlock(&tasklist_lock);
		}
	} else {
		/*
		 * Find the given PID, and validate that the caller
		 * should be able to see it.
		 */
		struct task_struct *p;
		rcu_read_lock();
		p = find_task_by_vpid(pid);
		if (p) {
			if (CPUCLOCK_PERTHREAD(which_clock)) {
				if (same_thread_group(p, current)) {
					error = cpu_clock_sample(which_clock,
								 p, &rtn);
				}
			} else {
				read_lock(&tasklist_lock);
				if (thread_group_leader(p) && p->signal) {
					error =
					    cpu_clock_sample_group(which_clock,
							           p, &rtn);
				}
				read_unlock(&tasklist_lock);
			}
		}
		rcu_read_unlock();
	}

	if (error)
		return error;
	sample_to_timespec(which_clock, rtn, tp);
	return 0;
}


/*
 * Validate the clockid_t for a new CPU-clock timer, and initialize the timer.
 * This is called from sys_timer_create with the new timer already locked.
 */
int posix_cpu_timer_create(struct k_itimer *new_timer)
{
	int ret = 0;
	const pid_t pid = CPUCLOCK_PID(new_timer->it_clock);
	struct task_struct *p;

	if (CPUCLOCK_WHICH(new_timer->it_clock) >= CPUCLOCK_MAX)
		return -EINVAL;

	INIT_LIST_HEAD(&new_timer->it.cpu.entry);
	new_timer->it.cpu.incr.sched = 0;
	new_timer->it.cpu.expires.sched = 0;

	read_lock(&tasklist_lock);
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
			if (p && !thread_group_leader(p))
				p = NULL;
		}
	}
	new_timer->it.cpu.task = p;
	if (p) {
		get_task_struct(p);
	} else {
		ret = -EINVAL;
	}
	read_unlock(&tasklist_lock);

	return ret;
}

/*
 * Clean up a CPU-clock timer that is about to be destroyed.
 * This is called from timer deletion with the timer already locked.
 * If we return TIMER_RETRY, it's necessary to release the timer's lock
 * and try again.  (This happens when the timer is in the middle of firing.)
 */
int posix_cpu_timer_del(struct k_itimer *timer)
{
	struct task_struct *p = timer->it.cpu.task;
	int ret = 0;

	if (likely(p != NULL)) {
		read_lock(&tasklist_lock);
		if (unlikely(p->signal == NULL)) {
			/*
			 * We raced with the reaping of the task.
			 * The deletion should have cleared us off the list.
			 */
			BUG_ON(!list_empty(&timer->it.cpu.entry));
		} else {
			spin_lock(&p->sighand->siglock);
			if (timer->it.cpu.firing)
				ret = TIMER_RETRY;
			else
				list_del(&timer->it.cpu.entry);
			spin_unlock(&p->sighand->siglock);
		}
		read_unlock(&tasklist_lock);

		if (!ret)
			put_task_struct(p);
	}

	return ret;
}

/*
 * Clean out CPU timers still ticking when a thread exited.  The task
 * pointer is cleared, and the expiry time is replaced with the residual
 * time for later timer_gettime calls to return.
 * This must be called with the siglock held.
 */
static void cleanup_timers(struct list_head *head,
			   cputime_t utime, cputime_t stime,
			   unsigned long long sum_exec_runtime)
{
	struct cpu_timer_list *timer, *next;
	cputime_t ptime = cputime_add(utime, stime);

	list_for_each_entry_safe(timer, next, head, entry) {
		list_del_init(&timer->entry);
		if (cputime_lt(timer->expires.cpu, ptime)) {
			timer->expires.cpu = cputime_zero;
		} else {
			timer->expires.cpu = cputime_sub(timer->expires.cpu,
							 ptime);
		}
	}

	++head;
	list_for_each_entry_safe(timer, next, head, entry) {
		list_del_init(&timer->entry);
		if (cputime_lt(timer->expires.cpu, utime)) {
			timer->expires.cpu = cputime_zero;
		} else {
			timer->expires.cpu = cputime_sub(timer->expires.cpu,
							 utime);
		}
	}

	++head;
	list_for_each_entry_safe(timer, next, head, entry) {
		list_del_init(&timer->entry);
		if (timer->expires.sched < sum_exec_runtime) {
			timer->expires.sched = 0;
		} else {
			timer->expires.sched -= sum_exec_runtime;
		}
	}
}

/*
 * These are both called with the siglock held, when the current thread
 * is being reaped.  When the final (leader) thread in the group is reaped,
 * posix_cpu_timers_exit_group will be called after posix_cpu_timers_exit.
 */
void posix_cpu_timers_exit(struct task_struct *tsk)
{
	cleanup_timers(tsk->cpu_timers,
		       tsk->utime, tsk->stime, tsk->se.sum_exec_runtime);

}
void posix_cpu_timers_exit_group(struct task_struct *tsk)
{
	struct task_cputime cputime;

	thread_group_cputimer(tsk, &cputime);
	cleanup_timers(tsk->signal->cpu_timers,
		       cputime.utime, cputime.stime, cputime.sum_exec_runtime);
}

static void clear_dead_task(struct k_itimer *timer, union cpu_time_count now)
{
	/*
	 * That's all for this thread or process.
	 * We leave our residual in expires to be reported.
	 */
	put_task_struct(timer->it.cpu.task);
	timer->it.cpu.task = NULL;
	timer->it.cpu.expires = cpu_time_sub(timer->it_clock,
					     timer->it.cpu.expires,
					     now);
}

/*
 * Insert the timer on the appropriate list before any timers that
 * expire later.  This must be called with the tasklist_lock held
 * for reading, and interrupts disabled.
 */
static void arm_timer(struct k_itimer *timer, union cpu_time_count now)
{
	struct task_struct *p = timer->it.cpu.task;
	struct list_head *head, *listpos;
	struct cpu_timer_list *const nt = &timer->it.cpu;
	struct cpu_timer_list *next;
	unsigned long i;

	head = (CPUCLOCK_PERTHREAD(timer->it_clock) ?
		p->cpu_timers : p->signal->cpu_timers);
	head += CPUCLOCK_WHICH(timer->it_clock);

	BUG_ON(!irqs_disabled());
	spin_lock(&p->sighand->siglock);

	listpos = head;
	if (CPUCLOCK_WHICH(timer->it_clock) == CPUCLOCK_SCHED) {
		list_for_each_entry(next, head, entry) {
			if (next->expires.sched > nt->expires.sched)
				break;
			listpos = &next->entry;
		}
	} else {
		list_for_each_entry(next, head, entry) {
			if (cputime_gt(next->expires.cpu, nt->expires.cpu))
				break;
			listpos = &next->entry;
		}
	}
	list_add(&nt->entry, listpos);

	if (listpos == head) {
		/*
		 * We are the new earliest-expiring timer.
		 * If we are a thread timer, there can always
		 * be a process timer telling us to stop earlier.
		 */

		if (CPUCLOCK_PERTHREAD(timer->it_clock)) {
			switch (CPUCLOCK_WHICH(timer->it_clock)) {
			default:
				BUG();
			case CPUCLOCK_PROF:
				if (cputime_eq(p->cputime_expires.prof_exp,
					       cputime_zero) ||
				    cputime_gt(p->cputime_expires.prof_exp,
					       nt->expires.cpu))
					p->cputime_expires.prof_exp =
						nt->expires.cpu;
				break;
			case CPUCLOCK_VIRT:
				if (cputime_eq(p->cputime_expires.virt_exp,
					       cputime_zero) ||
				    cputime_gt(p->cputime_expires.virt_exp,
					       nt->expires.cpu))
					p->cputime_expires.virt_exp =
						nt->expires.cpu;
				break;
			case CPUCLOCK_SCHED:
				if (p->cputime_expires.sched_exp == 0 ||
				    p->cputime_expires.sched_exp >
							nt->expires.sched)
					p->cputime_expires.sched_exp =
						nt->expires.sched;
				break;
			}
		} else {
			/*
			 * For a process timer, set the cached expiration time.
			 */
			switch (CPUCLOCK_WHICH(timer->it_clock)) {
			default:
				BUG();
			case CPUCLOCK_VIRT:
				if (!cputime_eq(p->signal->it_virt_expires,
						cputime_zero) &&
				    cputime_lt(p->signal->it_virt_expires,
					       timer->it.cpu.expires.cpu))
					break;
				p->signal->cputime_expires.virt_exp =
					timer->it.cpu.expires.cpu;
				break;
			case CPUCLOCK_PROF:
				if (!cputime_eq(p->signal->it_prof_expires,
						cputime_zero) &&
				    cputime_lt(p->signal->it_prof_expires,
					       timer->it.cpu.expires.cpu))
					break;
				i = p->signal->rlim[RLIMIT_CPU].rlim_cur;
				if (i != RLIM_INFINITY &&
				    i <= cputime_to_secs(timer->it.cpu.expires.cpu))
					break;
				p->signal->cputime_expires.prof_exp =
					timer->it.cpu.expires.cpu;
				break;
			case CPUCLOCK_SCHED:
				p->signal->cputime_expires.sched_exp =
					timer->it.cpu.expires.sched;
				break;
			}
		}
	}

	spin_unlock(&p->sighand->siglock);
}

/*
 * The timer is locked, fire it and arrange for its reload.
 */
static void cpu_timer_fire(struct k_itimer *timer)
{
	if (unlikely(timer->sigq == NULL)) {
		/*
		 * This a special case for clock_nanosleep,
		 * not a normal timer from sys_timer_create.
		 */
		wake_up_process(timer->it_process);
		timer->it.cpu.expires.sched = 0;
	} else if (timer->it.cpu.incr.sched == 0) {
		/*
		 * One-shot timer.  Clear it as soon as it's fired.
		 */
		posix_timer_event(timer, 0);
		timer->it.cpu.expires.sched = 0;
	} else if (posix_timer_event(timer, ++timer->it_requeue_pending)) {
		/*
		 * The signal did not get queued because the signal
		 * was ignored, so we won't get any callback to
		 * reload the timer.  But we need to keep it
		 * ticking in case the signal is deliverable next time.
		 */
		posix_cpu_timer_schedule(timer);
	}
}

/*
 * Sample a process (thread group) timer for the given group_leader task.
 * Must be called with tasklist_lock held for reading.
 */
static int cpu_timer_sample_group(const clockid_t which_clock,
				  struct task_struct *p,
				  union cpu_time_count *cpu)
{
	struct task_cputime cputime;

	thread_group_cputimer(p, &cputime);
	switch (CPUCLOCK_WHICH(which_clock)) {
	default:
		return -EINVAL;
	case CPUCLOCK_PROF:
		cpu->cpu = cputime_add(cputime.utime, cputime.stime);
		break;
	case CPUCLOCK_VIRT:
		cpu->cpu = cputime.utime;
		break;
	case CPUCLOCK_SCHED:
		cpu->sched = cputime.sum_exec_runtime + task_delta_exec(p);
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
int posix_cpu_timer_set(struct k_itimer *timer, int flags,
			struct itimerspec *new, struct itimerspec *old)
{
	struct task_struct *p = timer->it.cpu.task;
	union cpu_time_count old_expires, new_expires, val;
	int ret;

	if (unlikely(p == NULL)) {
		/*
		 * Timer refers to a dead task's clock.
		 */
		return -ESRCH;
	}

	new_expires = timespec_to_sample(timer->it_clock, &new->it_value);

	read_lock(&tasklist_lock);
	/*
	 * We need the tasklist_lock to protect against reaping that
	 * clears p->signal.  If p has just been reaped, we can no
	 * longer get any information about it at all.
	 */
	if (unlikely(p->signal == NULL)) {
		read_unlock(&tasklist_lock);
		put_task_struct(p);
		timer->it.cpu.task = NULL;
		return -ESRCH;
	}

	/*
	 * Disarm any old timer after extracting its expiry time.
	 */
	BUG_ON(!irqs_disabled());

	ret = 0;
	spin_lock(&p->sighand->siglock);
	old_expires = timer->it.cpu.expires;
	if (unlikely(timer->it.cpu.firing)) {
		timer->it.cpu.firing = -1;
		ret = TIMER_RETRY;
	} else
		list_del_init(&timer->it.cpu.entry);
	spin_unlock(&p->sighand->siglock);

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
		if (old_expires.sched == 0) {
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
			if (cpu_time_before(timer->it_clock, val,
					    timer->it.cpu.expires)) {
				old_expires = cpu_time_sub(
					timer->it_clock,
					timer->it.cpu.expires, val);
				sample_to_timespec(timer->it_clock,
						   old_expires,
						   &old->it_value);
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
		read_unlock(&tasklist_lock);
		goto out;
	}

	if (new_expires.sched != 0 && !(flags & TIMER_ABSTIME)) {
		cpu_time_add(timer->it_clock, &new_expires, val);
	}

	/*
	 * Install the new expiry time (or zero).
	 * For a timer with no notification action, we don't actually
	 * arm the timer (we'll just fake it for timer_gettime).
	 */
	timer->it.cpu.expires = new_expires;
	if (new_expires.sched != 0 &&
	    (timer->it_sigev_notify & ~SIGEV_THREAD_ID) != SIGEV_NONE &&
	    cpu_time_before(timer->it_clock, val, new_expires)) {
		arm_timer(timer, val);
	}

	read_unlock(&tasklist_lock);

	/*
	 * Install the new reload setting, and
	 * set up the signal and overrun bookkeeping.
	 */
	timer->it.cpu.incr = timespec_to_sample(timer->it_clock,
						&new->it_interval);

	/*
	 * This acts as a modification timestamp for the timer,
	 * so any automatic reload attempt will punt on seeing
	 * that we have reset the timer manually.
	 */
	timer->it_requeue_pending = (timer->it_requeue_pending + 2) &
		~REQUEUE_PENDING;
	timer->it_overrun_last = 0;
	timer->it_overrun = -1;

	if (new_expires.sched != 0 &&
	    (timer->it_sigev_notify & ~SIGEV_THREAD_ID) != SIGEV_NONE &&
	    !cpu_time_before(timer->it_clock, val, new_expires)) {
		/*
		 * The designated time already passed, so we notify
		 * immediately, even if the thread never runs to
		 * accumulate more time on this clock.
		 */
		cpu_timer_fire(timer);
	}

	ret = 0;
 out:
	if (old) {
		sample_to_timespec(timer->it_clock,
				   timer->it.cpu.incr, &old->it_interval);
	}
	return ret;
}

void posix_cpu_timer_get(struct k_itimer *timer, struct itimerspec *itp)
{
	union cpu_time_count now;
	struct task_struct *p = timer->it.cpu.task;
	int clear_dead;

	/*
	 * Easy part: convert the reload time.
	 */
	sample_to_timespec(timer->it_clock,
			   timer->it.cpu.incr, &itp->it_interval);

	if (timer->it.cpu.expires.sched == 0) {	/* Timer not armed at all.  */
		itp->it_value.tv_sec = itp->it_value.tv_nsec = 0;
		return;
	}

	if (unlikely(p == NULL)) {
		/*
		 * This task already died and the timer will never fire.
		 * In this case, expires is actually the dead value.
		 */
	dead:
		sample_to_timespec(timer->it_clock, timer->it.cpu.expires,
				   &itp->it_value);
		return;
	}

	/*
	 * Sample the clock to take the difference with the expiry time.
	 */
	if (CPUCLOCK_PERTHREAD(timer->it_clock)) {
		cpu_clock_sample(timer->it_clock, p, &now);
		clear_dead = p->exit_state;
	} else {
		read_lock(&tasklist_lock);
		if (unlikely(p->signal == NULL)) {
			/*
			 * The process has been reaped.
			 * We can't even collect a sample any more.
			 * Call the timer disarmed, nothing else to do.
			 */
			put_task_struct(p);
			timer->it.cpu.task = NULL;
			timer->it.cpu.expires.sched = 0;
			read_unlock(&tasklist_lock);
			goto dead;
		} else {
			cpu_timer_sample_group(timer->it_clock, p, &now);
			clear_dead = (unlikely(p->exit_state) &&
				      thread_group_empty(p));
		}
		read_unlock(&tasklist_lock);
	}

	if ((timer->it_sigev_notify & ~SIGEV_THREAD_ID) == SIGEV_NONE) {
		if (timer->it.cpu.incr.sched == 0 &&
		    cpu_time_before(timer->it_clock,
				    timer->it.cpu.expires, now)) {
			/*
			 * Do-nothing timer expired and has no reload,
			 * so it's as if it was never set.
			 */
			timer->it.cpu.expires.sched = 0;
			itp->it_value.tv_sec = itp->it_value.tv_nsec = 0;
			return;
		}
		/*
		 * Account for any expirations and reloads that should
		 * have happened.
		 */
		bump_cpu_timer(timer, now);
	}

	if (unlikely(clear_dead)) {
		/*
		 * We've noticed that the thread is dead, but
		 * not yet reaped.  Take this opportunity to
		 * drop our task ref.
		 */
		clear_dead_task(timer, now);
		goto dead;
	}

	if (cpu_time_before(timer->it_clock, now, timer->it.cpu.expires)) {
		sample_to_timespec(timer->it_clock,
				   cpu_time_sub(timer->it_clock,
						timer->it.cpu.expires, now),
				   &itp->it_value);
	} else {
		/*
		 * The timer should have expired already, but the firing
		 * hasn't taken place yet.  Say it's just about to expire.
		 */
		itp->it_value.tv_nsec = 1;
		itp->it_value.tv_sec = 0;
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
	int maxfire;
	struct list_head *timers = tsk->cpu_timers;
	struct signal_struct *const sig = tsk->signal;

	maxfire = 20;
	tsk->cputime_expires.prof_exp = cputime_zero;
	while (!list_empty(timers)) {
		struct cpu_timer_list *t = list_first_entry(timers,
						      struct cpu_timer_list,
						      entry);
		if (!--maxfire || cputime_lt(prof_ticks(tsk), t->expires.cpu)) {
			tsk->cputime_expires.prof_exp = t->expires.cpu;
			break;
		}
		t->firing = 1;
		list_move_tail(&t->entry, firing);
	}

	++timers;
	maxfire = 20;
	tsk->cputime_expires.virt_exp = cputime_zero;
	while (!list_empty(timers)) {
		struct cpu_timer_list *t = list_first_entry(timers,
						      struct cpu_timer_list,
						      entry);
		if (!--maxfire || cputime_lt(virt_ticks(tsk), t->expires.cpu)) {
			tsk->cputime_expires.virt_exp = t->expires.cpu;
			break;
		}
		t->firing = 1;
		list_move_tail(&t->entry, firing);
	}

	++timers;
	maxfire = 20;
	tsk->cputime_expires.sched_exp = 0;
	while (!list_empty(timers)) {
		struct cpu_timer_list *t = list_first_entry(timers,
						      struct cpu_timer_list,
						      entry);
		if (!--maxfire || tsk->se.sum_exec_runtime < t->expires.sched) {
			tsk->cputime_expires.sched_exp = t->expires.sched;
			break;
		}
		t->firing = 1;
		list_move_tail(&t->entry, firing);
	}

	/*
	 * Check for the special case thread timers.
	 */
	if (sig->rlim[RLIMIT_RTTIME].rlim_cur != RLIM_INFINITY) {
		unsigned long hard = sig->rlim[RLIMIT_RTTIME].rlim_max;
		unsigned long *soft = &sig->rlim[RLIMIT_RTTIME].rlim_cur;

		if (hard != RLIM_INFINITY &&
		    tsk->rt.timeout > DIV_ROUND_UP(hard, USEC_PER_SEC/HZ)) {
			/*
			 * At the hard limit, we just die.
			 * No need to calculate anything else now.
			 */
			__group_send_sig_info(SIGKILL, SEND_SIG_PRIV, tsk);
			return;
		}
		if (tsk->rt.timeout > DIV_ROUND_UP(*soft, USEC_PER_SEC/HZ)) {
			/*
			 * At the soft limit, send a SIGXCPU every second.
			 */
			if (sig->rlim[RLIMIT_RTTIME].rlim_cur
			    < sig->rlim[RLIMIT_RTTIME].rlim_max) {
				sig->rlim[RLIMIT_RTTIME].rlim_cur +=
								USEC_PER_SEC;
			}
			printk(KERN_INFO
				"RT Watchdog Timeout: %s[%d]\n",
				tsk->comm, task_pid_nr(tsk));
			__group_send_sig_info(SIGXCPU, SEND_SIG_PRIV, tsk);
		}
	}
}

static void stop_process_timers(struct task_struct *tsk)
{
	struct thread_group_cputimer *cputimer = &tsk->signal->cputimer;
	unsigned long flags;

	if (!cputimer->running)
		return;

	spin_lock_irqsave(&cputimer->lock, flags);
	cputimer->running = 0;
	spin_unlock_irqrestore(&cputimer->lock, flags);
}

/*
 * Check for any per-thread CPU timers that have fired and move them
 * off the tsk->*_timers list onto the firing list.  Per-thread timers
 * have already been taken off.
 */
static void check_process_timers(struct task_struct *tsk,
				 struct list_head *firing)
{
	int maxfire;
	struct signal_struct *const sig = tsk->signal;
	cputime_t utime, ptime, virt_expires, prof_expires;
	unsigned long long sum_sched_runtime, sched_expires;
	struct list_head *timers = sig->cpu_timers;
	struct task_cputime cputime;

	/*
	 * Don't sample the current process CPU clocks if there are no timers.
	 */
	if (list_empty(&timers[CPUCLOCK_PROF]) &&
	    cputime_eq(sig->it_prof_expires, cputime_zero) &&
	    sig->rlim[RLIMIT_CPU].rlim_cur == RLIM_INFINITY &&
	    list_empty(&timers[CPUCLOCK_VIRT]) &&
	    cputime_eq(sig->it_virt_expires, cputime_zero) &&
	    list_empty(&timers[CPUCLOCK_SCHED])) {
		stop_process_timers(tsk);
		return;
	}

	/*
	 * Collect the current process totals.
	 */
	thread_group_cputimer(tsk, &cputime);
	utime = cputime.utime;
	ptime = cputime_add(utime, cputime.stime);
	sum_sched_runtime = cputime.sum_exec_runtime;
	maxfire = 20;
	prof_expires = cputime_zero;
	while (!list_empty(timers)) {
		struct cpu_timer_list *tl = list_first_entry(timers,
						      struct cpu_timer_list,
						      entry);
		if (!--maxfire || cputime_lt(ptime, tl->expires.cpu)) {
			prof_expires = tl->expires.cpu;
			break;
		}
		tl->firing = 1;
		list_move_tail(&tl->entry, firing);
	}

	++timers;
	maxfire = 20;
	virt_expires = cputime_zero;
	while (!list_empty(timers)) {
		struct cpu_timer_list *tl = list_first_entry(timers,
						      struct cpu_timer_list,
						      entry);
		if (!--maxfire || cputime_lt(utime, tl->expires.cpu)) {
			virt_expires = tl->expires.cpu;
			break;
		}
		tl->firing = 1;
		list_move_tail(&tl->entry, firing);
	}

	++timers;
	maxfire = 20;
	sched_expires = 0;
	while (!list_empty(timers)) {
		struct cpu_timer_list *tl = list_first_entry(timers,
						      struct cpu_timer_list,
						      entry);
		if (!--maxfire || sum_sched_runtime < tl->expires.sched) {
			sched_expires = tl->expires.sched;
			break;
		}
		tl->firing = 1;
		list_move_tail(&tl->entry, firing);
	}

	/*
	 * Check for the special case process timers.
	 */
	if (!cputime_eq(sig->it_prof_expires, cputime_zero)) {
		if (cputime_ge(ptime, sig->it_prof_expires)) {
			/* ITIMER_PROF fires and reloads.  */
			sig->it_prof_expires = sig->it_prof_incr;
			if (!cputime_eq(sig->it_prof_expires, cputime_zero)) {
				sig->it_prof_expires = cputime_add(
					sig->it_prof_expires, ptime);
			}
			__group_send_sig_info(SIGPROF, SEND_SIG_PRIV, tsk);
		}
		if (!cputime_eq(sig->it_prof_expires, cputime_zero) &&
		    (cputime_eq(prof_expires, cputime_zero) ||
		     cputime_lt(sig->it_prof_expires, prof_expires))) {
			prof_expires = sig->it_prof_expires;
		}
	}
	if (!cputime_eq(sig->it_virt_expires, cputime_zero)) {
		if (cputime_ge(utime, sig->it_virt_expires)) {
			/* ITIMER_VIRTUAL fires and reloads.  */
			sig->it_virt_expires = sig->it_virt_incr;
			if (!cputime_eq(sig->it_virt_expires, cputime_zero)) {
				sig->it_virt_expires = cputime_add(
					sig->it_virt_expires, utime);
			}
			__group_send_sig_info(SIGVTALRM, SEND_SIG_PRIV, tsk);
		}
		if (!cputime_eq(sig->it_virt_expires, cputime_zero) &&
		    (cputime_eq(virt_expires, cputime_zero) ||
		     cputime_lt(sig->it_virt_expires, virt_expires))) {
			virt_expires = sig->it_virt_expires;
		}
	}
	if (sig->rlim[RLIMIT_CPU].rlim_cur != RLIM_INFINITY) {
		unsigned long psecs = cputime_to_secs(ptime);
		cputime_t x;
		if (psecs >= sig->rlim[RLIMIT_CPU].rlim_max) {
			/*
			 * At the hard limit, we just die.
			 * No need to calculate anything else now.
			 */
			__group_send_sig_info(SIGKILL, SEND_SIG_PRIV, tsk);
			return;
		}
		if (psecs >= sig->rlim[RLIMIT_CPU].rlim_cur) {
			/*
			 * At the soft limit, send a SIGXCPU every second.
			 */
			__group_send_sig_info(SIGXCPU, SEND_SIG_PRIV, tsk);
			if (sig->rlim[RLIMIT_CPU].rlim_cur
			    < sig->rlim[RLIMIT_CPU].rlim_max) {
				sig->rlim[RLIMIT_CPU].rlim_cur++;
			}
		}
		x = secs_to_cputime(sig->rlim[RLIMIT_CPU].rlim_cur);
		if (cputime_eq(prof_expires, cputime_zero) ||
		    cputime_lt(x, prof_expires)) {
			prof_expires = x;
		}
	}

	if (!cputime_eq(prof_expires, cputime_zero) &&
	    (cputime_eq(sig->cputime_expires.prof_exp, cputime_zero) ||
	     cputime_gt(sig->cputime_expires.prof_exp, prof_expires)))
		sig->cputime_expires.prof_exp = prof_expires;
	if (!cputime_eq(virt_expires, cputime_zero) &&
	    (cputime_eq(sig->cputime_expires.virt_exp, cputime_zero) ||
	     cputime_gt(sig->cputime_expires.virt_exp, virt_expires)))
		sig->cputime_expires.virt_exp = virt_expires;
	if (sched_expires != 0 &&
	    (sig->cputime_expires.sched_exp == 0 ||
	     sig->cputime_expires.sched_exp > sched_expires))
		sig->cputime_expires.sched_exp = sched_expires;
}

/*
 * This is called from the signal code (via do_schedule_next_timer)
 * when the last timer signal was delivered and we have to reload the timer.
 */
void posix_cpu_timer_schedule(struct k_itimer *timer)
{
	struct task_struct *p = timer->it.cpu.task;
	union cpu_time_count now;

	if (unlikely(p == NULL))
		/*
		 * The task was cleaned up already, no future firings.
		 */
		goto out;

	/*
	 * Fetch the current sample and update the timer's expiry time.
	 */
	if (CPUCLOCK_PERTHREAD(timer->it_clock)) {
		cpu_clock_sample(timer->it_clock, p, &now);
		bump_cpu_timer(timer, now);
		if (unlikely(p->exit_state)) {
			clear_dead_task(timer, now);
			goto out;
		}
		read_lock(&tasklist_lock); /* arm_timer needs it.  */
	} else {
		read_lock(&tasklist_lock);
		if (unlikely(p->signal == NULL)) {
			/*
			 * The process has been reaped.
			 * We can't even collect a sample any more.
			 */
			put_task_struct(p);
			timer->it.cpu.task = p = NULL;
			timer->it.cpu.expires.sched = 0;
			goto out_unlock;
		} else if (unlikely(p->exit_state) && thread_group_empty(p)) {
			/*
			 * We've noticed that the thread is dead, but
			 * not yet reaped.  Take this opportunity to
			 * drop our task ref.
			 */
			clear_dead_task(timer, now);
			goto out_unlock;
		}
		cpu_timer_sample_group(timer->it_clock, p, &now);
		bump_cpu_timer(timer, now);
		/* Leave the tasklist_lock locked for the call below.  */
	}

	/*
	 * Now re-arm for the new expiry time.
	 */
	arm_timer(timer, now);

out_unlock:
	read_unlock(&tasklist_lock);

out:
	timer->it_overrun_last = timer->it_overrun;
	timer->it_overrun = -1;
	++timer->it_requeue_pending;
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
	if (cputime_eq(cputime->utime, cputime_zero) &&
	    cputime_eq(cputime->stime, cputime_zero) &&
	    cputime->sum_exec_runtime == 0)
		return 1;
	return 0;
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
	if (!cputime_eq(expires->utime, cputime_zero) &&
	    cputime_ge(sample->utime, expires->utime))
		return 1;
	if (!cputime_eq(expires->stime, cputime_zero) &&
	    cputime_ge(cputime_add(sample->utime, sample->stime),
		       expires->stime))
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

	/* tsk == current, ensure it is safe to use ->signal/sighand */
	if (unlikely(tsk->exit_state))
		return 0;

	if (!task_cputime_zero(&tsk->cputime_expires)) {
		struct task_cputime task_sample = {
			.utime = tsk->utime,
			.stime = tsk->stime,
			.sum_exec_runtime = tsk->se.sum_exec_runtime
		};

		if (task_cputime_expired(&task_sample, &tsk->cputime_expires))
			return 1;
	}

	sig = tsk->signal;
	if (!task_cputime_zero(&sig->cputime_expires)) {
		struct task_cputime group_sample;

		thread_group_cputimer(tsk, &group_sample);
		if (task_cputime_expired(&group_sample, &sig->cputime_expires))
			return 1;
	}

	return sig->rlim[RLIMIT_CPU].rlim_cur != RLIM_INFINITY;
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

	BUG_ON(!irqs_disabled());

	/*
	 * The fast path checks that there are no expired thread or thread
	 * group timers.  If that's so, just return.
	 */
	if (!fastpath_timer_check(tsk))
		return;

	spin_lock(&tsk->sighand->siglock);
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
	spin_unlock(&tsk->sighand->siglock);

	/*
	 * Now that all the timers on our list have the firing flag,
	 * noone will touch their list entries but us.  We'll take
	 * each timer's lock before clearing its firing flag, so no
	 * timer call will interfere.
	 */
	list_for_each_entry_safe(timer, next, &firing, it.cpu.entry) {
		int firing;
		spin_lock(&timer->it_lock);
		list_del_init(&timer->it.cpu.entry);
		firing = timer->it.cpu.firing;
		timer->it.cpu.firing = 0;
		/*
		 * The firing flag is -1 if we collided with a reset
		 * of the timer, which already reported this
		 * almost-firing as an overrun.  So don't generate an event.
		 */
		if (likely(firing >= 0)) {
			cpu_timer_fire(timer);
		}
		spin_unlock(&timer->it_lock);
	}
}

/*
 * Set one of the process-wide special case CPU timers.
 * The tsk->sighand->siglock must be held by the caller.
 * The *newval argument is relative and we update it to be absolute, *oldval
 * is absolute and we update it to be relative.
 */
void set_process_cpu_timer(struct task_struct *tsk, unsigned int clock_idx,
			   cputime_t *newval, cputime_t *oldval)
{
	union cpu_time_count now;
	struct list_head *head;

	BUG_ON(clock_idx == CPUCLOCK_SCHED);
	cpu_timer_sample_group(clock_idx, tsk, &now);

	if (oldval) {
		if (!cputime_eq(*oldval, cputime_zero)) {
			if (cputime_le(*oldval, now.cpu)) {
				/* Just about to fire. */
				*oldval = jiffies_to_cputime(1);
			} else {
				*oldval = cputime_sub(*oldval, now.cpu);
			}
		}

		if (cputime_eq(*newval, cputime_zero))
			return;
		*newval = cputime_add(*newval, now.cpu);

		/*
		 * If the RLIMIT_CPU timer will expire before the
		 * ITIMER_PROF timer, we have nothing else to do.
		 */
		if (tsk->signal->rlim[RLIMIT_CPU].rlim_cur
		    < cputime_to_secs(*newval))
			return;
	}

	/*
	 * Check whether there are any process timers already set to fire
	 * before this one.  If so, we don't have anything more to do.
	 */
	head = &tsk->signal->cpu_timers[clock_idx];
	if (list_empty(head) ||
	    cputime_ge(list_first_entry(head,
				  struct cpu_timer_list, entry)->expires.cpu,
		       *newval)) {
		switch (clock_idx) {
		case CPUCLOCK_PROF:
			tsk->signal->cputime_expires.prof_exp = *newval;
			break;
		case CPUCLOCK_VIRT:
			tsk->signal->cputime_expires.virt_exp = *newval;
			break;
		}
	}
}

static int do_cpu_nanosleep(const clockid_t which_clock, int flags,
			    struct timespec *rqtp, struct itimerspec *it)
{
	struct k_itimer timer;
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
		static struct itimerspec zero_it;

		memset(it, 0, sizeof *it);
		it->it_value = *rqtp;

		spin_lock_irq(&timer.it_lock);
		error = posix_cpu_timer_set(&timer, flags, it, NULL);
		if (error) {
			spin_unlock_irq(&timer.it_lock);
			return error;
		}

		while (!signal_pending(current)) {
			if (timer.it.cpu.expires.sched == 0) {
				/*
				 * Our timer fired and was reset.
				 */
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
		sample_to_timespec(which_clock, timer.it.cpu.expires, rqtp);
		posix_cpu_timer_set(&timer, 0, &zero_it, it);
		spin_unlock_irq(&timer.it_lock);

		if ((it->it_value.tv_sec | it->it_value.tv_nsec) == 0) {
			/*
			 * It actually did fire already.
			 */
			return 0;
		}

		error = -ERESTART_RESTARTBLOCK;
	}

	return error;
}

int posix_cpu_nsleep(const clockid_t which_clock, int flags,
		     struct timespec *rqtp, struct timespec __user *rmtp)
{
	struct restart_block *restart_block =
	    &current_thread_info()->restart_block;
	struct itimerspec it;
	int error;

	/*
	 * Diagnose required errors first.
	 */
	if (CPUCLOCK_PERTHREAD(which_clock) &&
	    (CPUCLOCK_PID(which_clock) == 0 ||
	     CPUCLOCK_PID(which_clock) == current->pid))
		return -EINVAL;

	error = do_cpu_nanosleep(which_clock, flags, rqtp, &it);

	if (error == -ERESTART_RESTARTBLOCK) {

	       	if (flags & TIMER_ABSTIME)
			return -ERESTARTNOHAND;
		/*
	 	 * Report back to the user the time still remaining.
	 	 */
		if (rmtp != NULL && copy_to_user(rmtp, &it.it_value, sizeof *rmtp))
			return -EFAULT;

		restart_block->fn = posix_cpu_nsleep_restart;
		restart_block->arg0 = which_clock;
		restart_block->arg1 = (unsigned long) rmtp;
		restart_block->arg2 = rqtp->tv_sec;
		restart_block->arg3 = rqtp->tv_nsec;
	}
	return error;
}

long posix_cpu_nsleep_restart(struct restart_block *restart_block)
{
	clockid_t which_clock = restart_block->arg0;
	struct timespec __user *rmtp;
	struct timespec t;
	struct itimerspec it;
	int error;

	rmtp = (struct timespec __user *) restart_block->arg1;
	t.tv_sec = restart_block->arg2;
	t.tv_nsec = restart_block->arg3;

	restart_block->fn = do_no_restart_syscall;
	error = do_cpu_nanosleep(which_clock, TIMER_ABSTIME, &t, &it);

	if (error == -ERESTART_RESTARTBLOCK) {
		/*
	 	 * Report back to the user the time still remaining.
	 	 */
		if (rmtp != NULL && copy_to_user(rmtp, &it.it_value, sizeof *rmtp))
			return -EFAULT;

		restart_block->fn = posix_cpu_nsleep_restart;
		restart_block->arg0 = which_clock;
		restart_block->arg1 = (unsigned long) rmtp;
		restart_block->arg2 = t.tv_sec;
		restart_block->arg3 = t.tv_nsec;
	}
	return error;

}


#define PROCESS_CLOCK	MAKE_PROCESS_CPUCLOCK(0, CPUCLOCK_SCHED)
#define THREAD_CLOCK	MAKE_THREAD_CPUCLOCK(0, CPUCLOCK_SCHED)

static int process_cpu_clock_getres(const clockid_t which_clock,
				    struct timespec *tp)
{
	return posix_cpu_clock_getres(PROCESS_CLOCK, tp);
}
static int process_cpu_clock_get(const clockid_t which_clock,
				 struct timespec *tp)
{
	return posix_cpu_clock_get(PROCESS_CLOCK, tp);
}
static int process_cpu_timer_create(struct k_itimer *timer)
{
	timer->it_clock = PROCESS_CLOCK;
	return posix_cpu_timer_create(timer);
}
static int process_cpu_nsleep(const clockid_t which_clock, int flags,
			      struct timespec *rqtp,
			      struct timespec __user *rmtp)
{
	return posix_cpu_nsleep(PROCESS_CLOCK, flags, rqtp, rmtp);
}
static long process_cpu_nsleep_restart(struct restart_block *restart_block)
{
	return -EINVAL;
}
static int thread_cpu_clock_getres(const clockid_t which_clock,
				   struct timespec *tp)
{
	return posix_cpu_clock_getres(THREAD_CLOCK, tp);
}
static int thread_cpu_clock_get(const clockid_t which_clock,
				struct timespec *tp)
{
	return posix_cpu_clock_get(THREAD_CLOCK, tp);
}
static int thread_cpu_timer_create(struct k_itimer *timer)
{
	timer->it_clock = THREAD_CLOCK;
	return posix_cpu_timer_create(timer);
}
static int thread_cpu_nsleep(const clockid_t which_clock, int flags,
			      struct timespec *rqtp, struct timespec __user *rmtp)
{
	return -EINVAL;
}
static long thread_cpu_nsleep_restart(struct restart_block *restart_block)
{
	return -EINVAL;
}

static __init int init_posix_cpu_timers(void)
{
	struct k_clock process = {
		.clock_getres = process_cpu_clock_getres,
		.clock_get = process_cpu_clock_get,
		.clock_set = do_posix_clock_nosettime,
		.timer_create = process_cpu_timer_create,
		.nsleep = process_cpu_nsleep,
		.nsleep_restart = process_cpu_nsleep_restart,
	};
	struct k_clock thread = {
		.clock_getres = thread_cpu_clock_getres,
		.clock_get = thread_cpu_clock_get,
		.clock_set = do_posix_clock_nosettime,
		.timer_create = thread_cpu_timer_create,
		.nsleep = thread_cpu_nsleep,
		.nsleep_restart = thread_cpu_nsleep_restart,
	};

	register_posix_clock(CLOCK_PROCESS_CPUTIME_ID, &process);
	register_posix_clock(CLOCK_THREAD_CPUTIME_ID, &thread);

	return 0;
}
__initcall(init_posix_cpu_timers);
