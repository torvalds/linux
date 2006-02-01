/*
 *  linux/kernel/hrtimer.c
 *
 *  Copyright(C) 2005, Thomas Gleixner <tglx@linutronix.de>
 *  Copyright(C) 2005, Red Hat, Inc., Ingo Molnar
 *
 *  High-resolution kernel timers
 *
 *  In contrast to the low-resolution timeout API implemented in
 *  kernel/timer.c, hrtimers provide finer resolution and accuracy
 *  depending on system configuration and capabilities.
 *
 *  These timers are currently used for:
 *   - itimers
 *   - POSIX timers
 *   - nanosleep
 *   - precise in-kernel timing
 *
 *  Started by: Thomas Gleixner and Ingo Molnar
 *
 *  Credits:
 *	based on kernel/timer.c
 *
 *	Help, testing, suggestions, bugfixes, improvements were
 *	provided by:
 *
 *	George Anzinger, Andrew Morton, Steven Rostedt, Roman Zippel
 *	et. al.
 *
 *  For licencing details see kernel-base/COPYING
 */

#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/hrtimer.h>
#include <linux/notifier.h>
#include <linux/syscalls.h>
#include <linux/interrupt.h>

#include <asm/uaccess.h>

/**
 * ktime_get - get the monotonic time in ktime_t format
 *
 * returns the time in ktime_t format
 */
static ktime_t ktime_get(void)
{
	struct timespec now;

	ktime_get_ts(&now);

	return timespec_to_ktime(now);
}

/**
 * ktime_get_real - get the real (wall-) time in ktime_t format
 *
 * returns the time in ktime_t format
 */
static ktime_t ktime_get_real(void)
{
	struct timespec now;

	getnstimeofday(&now);

	return timespec_to_ktime(now);
}

EXPORT_SYMBOL_GPL(ktime_get_real);

/*
 * The timer bases:
 *
 * Note: If we want to add new timer bases, we have to skip the two
 * clock ids captured by the cpu-timers. We do this by holding empty
 * entries rather than doing math adjustment of the clock ids.
 * This ensures that we capture erroneous accesses to these clock ids
 * rather than moving them into the range of valid clock id's.
 */

#define MAX_HRTIMER_BASES 2

static DEFINE_PER_CPU(struct hrtimer_base, hrtimer_bases[MAX_HRTIMER_BASES]) =
{
	{
		.index = CLOCK_REALTIME,
		.get_time = &ktime_get_real,
		.resolution = KTIME_REALTIME_RES,
	},
	{
		.index = CLOCK_MONOTONIC,
		.get_time = &ktime_get,
		.resolution = KTIME_MONOTONIC_RES,
	},
};

/**
 * ktime_get_ts - get the monotonic clock in timespec format
 *
 * @ts:		pointer to timespec variable
 *
 * The function calculates the monotonic clock from the realtime
 * clock and the wall_to_monotonic offset and stores the result
 * in normalized timespec format in the variable pointed to by ts.
 */
void ktime_get_ts(struct timespec *ts)
{
	struct timespec tomono;
	unsigned long seq;

	do {
		seq = read_seqbegin(&xtime_lock);
		getnstimeofday(ts);
		tomono = wall_to_monotonic;

	} while (read_seqretry(&xtime_lock, seq));

	set_normalized_timespec(ts, ts->tv_sec + tomono.tv_sec,
				ts->tv_nsec + tomono.tv_nsec);
}
EXPORT_SYMBOL_GPL(ktime_get_ts);

/*
 * Functions and macros which are different for UP/SMP systems are kept in a
 * single place
 */
#ifdef CONFIG_SMP

#define set_curr_timer(b, t)		do { (b)->curr_timer = (t); } while (0)

/*
 * We are using hashed locking: holding per_cpu(hrtimer_bases)[n].lock
 * means that all timers which are tied to this base via timer->base are
 * locked, and the base itself is locked too.
 *
 * So __run_timers/migrate_timers can safely modify all timers which could
 * be found on the lists/queues.
 *
 * When the timer's base is locked, and the timer removed from list, it is
 * possible to set timer->base = NULL and drop the lock: the timer remains
 * locked.
 */
static struct hrtimer_base *lock_hrtimer_base(const struct hrtimer *timer,
					      unsigned long *flags)
{
	struct hrtimer_base *base;

	for (;;) {
		base = timer->base;
		if (likely(base != NULL)) {
			spin_lock_irqsave(&base->lock, *flags);
			if (likely(base == timer->base))
				return base;
			/* The timer has migrated to another CPU: */
			spin_unlock_irqrestore(&base->lock, *flags);
		}
		cpu_relax();
	}
}

/*
 * Switch the timer base to the current CPU when possible.
 */
static inline struct hrtimer_base *
switch_hrtimer_base(struct hrtimer *timer, struct hrtimer_base *base)
{
	struct hrtimer_base *new_base;

	new_base = &__get_cpu_var(hrtimer_bases[base->index]);

	if (base != new_base) {
		/*
		 * We are trying to schedule the timer on the local CPU.
		 * However we can't change timer's base while it is running,
		 * so we keep it on the same CPU. No hassle vs. reprogramming
		 * the event source in the high resolution case. The softirq
		 * code will take care of this when the timer function has
		 * completed. There is no conflict as we hold the lock until
		 * the timer is enqueued.
		 */
		if (unlikely(base->curr_timer == timer))
			return base;

		/* See the comment in lock_timer_base() */
		timer->base = NULL;
		spin_unlock(&base->lock);
		spin_lock(&new_base->lock);
		timer->base = new_base;
	}
	return new_base;
}

#else /* CONFIG_SMP */

#define set_curr_timer(b, t)		do { } while (0)

static inline struct hrtimer_base *
lock_hrtimer_base(const struct hrtimer *timer, unsigned long *flags)
{
	struct hrtimer_base *base = timer->base;

	spin_lock_irqsave(&base->lock, *flags);

	return base;
}

#define switch_hrtimer_base(t, b)	(b)

#endif	/* !CONFIG_SMP */

/*
 * Functions for the union type storage format of ktime_t which are
 * too large for inlining:
 */
#if BITS_PER_LONG < 64
# ifndef CONFIG_KTIME_SCALAR
/**
 * ktime_add_ns - Add a scalar nanoseconds value to a ktime_t variable
 *
 * @kt:		addend
 * @nsec:	the scalar nsec value to add
 *
 * Returns the sum of kt and nsec in ktime_t format
 */
ktime_t ktime_add_ns(const ktime_t kt, u64 nsec)
{
	ktime_t tmp;

	if (likely(nsec < NSEC_PER_SEC)) {
		tmp.tv64 = nsec;
	} else {
		unsigned long rem = do_div(nsec, NSEC_PER_SEC);

		tmp = ktime_set((long)nsec, rem);
	}

	return ktime_add(kt, tmp);
}

#else /* CONFIG_KTIME_SCALAR */

# endif /* !CONFIG_KTIME_SCALAR */

/*
 * Divide a ktime value by a nanosecond value
 */
static unsigned long ktime_divns(const ktime_t kt, nsec_t div)
{
	u64 dclc, inc, dns;
	int sft = 0;

	dclc = dns = ktime_to_ns(kt);
	inc = div;
	/* Make sure the divisor is less than 2^32: */
	while (div >> 32) {
		sft++;
		div >>= 1;
	}
	dclc >>= sft;
	do_div(dclc, (unsigned long) div);

	return (unsigned long) dclc;
}

#else /* BITS_PER_LONG < 64 */
# define ktime_divns(kt, div)		(unsigned long)((kt).tv64 / (div))
#endif /* BITS_PER_LONG >= 64 */

/*
 * Counterpart to lock_timer_base above:
 */
static inline
void unlock_hrtimer_base(const struct hrtimer *timer, unsigned long *flags)
{
	spin_unlock_irqrestore(&timer->base->lock, *flags);
}

/**
 * hrtimer_forward - forward the timer expiry
 *
 * @timer:	hrtimer to forward
 * @interval:	the interval to forward
 *
 * Forward the timer expiry so it will expire in the future.
 * Returns the number of overruns.
 */
unsigned long
hrtimer_forward(struct hrtimer *timer, ktime_t interval)
{
	unsigned long orun = 1;
	ktime_t delta, now;

	now = timer->base->get_time();

	delta = ktime_sub(now, timer->expires);

	if (delta.tv64 < 0)
		return 0;

	if (interval.tv64 < timer->base->resolution.tv64)
		interval.tv64 = timer->base->resolution.tv64;

	if (unlikely(delta.tv64 >= interval.tv64)) {
		nsec_t incr = ktime_to_ns(interval);

		orun = ktime_divns(delta, incr);
		timer->expires = ktime_add_ns(timer->expires, incr * orun);
		if (timer->expires.tv64 > now.tv64)
			return orun;
		/*
		 * This (and the ktime_add() below) is the
		 * correction for exact:
		 */
		orun++;
	}
	timer->expires = ktime_add(timer->expires, interval);

	return orun;
}

/*
 * enqueue_hrtimer - internal function to (re)start a timer
 *
 * The timer is inserted in expiry order. Insertion into the
 * red black tree is O(log(n)). Must hold the base lock.
 */
static void enqueue_hrtimer(struct hrtimer *timer, struct hrtimer_base *base)
{
	struct rb_node **link = &base->active.rb_node;
	struct rb_node *parent = NULL;
	struct hrtimer *entry;

	/*
	 * Find the right place in the rbtree:
	 */
	while (*link) {
		parent = *link;
		entry = rb_entry(parent, struct hrtimer, node);
		/*
		 * We dont care about collisions. Nodes with
		 * the same expiry time stay together.
		 */
		if (timer->expires.tv64 < entry->expires.tv64)
			link = &(*link)->rb_left;
		else
			link = &(*link)->rb_right;
	}

	/*
	 * Insert the timer to the rbtree and check whether it
	 * replaces the first pending timer
	 */
	rb_link_node(&timer->node, parent, link);
	rb_insert_color(&timer->node, &base->active);

	timer->state = HRTIMER_PENDING;

	if (!base->first || timer->expires.tv64 <
	    rb_entry(base->first, struct hrtimer, node)->expires.tv64)
		base->first = &timer->node;
}

/*
 * __remove_hrtimer - internal function to remove a timer
 *
 * Caller must hold the base lock.
 */
static void __remove_hrtimer(struct hrtimer *timer, struct hrtimer_base *base)
{
	/*
	 * Remove the timer from the rbtree and replace the
	 * first entry pointer if necessary.
	 */
	if (base->first == &timer->node)
		base->first = rb_next(&timer->node);
	rb_erase(&timer->node, &base->active);
}

/*
 * remove hrtimer, called with base lock held
 */
static inline int
remove_hrtimer(struct hrtimer *timer, struct hrtimer_base *base)
{
	if (hrtimer_active(timer)) {
		__remove_hrtimer(timer, base);
		timer->state = HRTIMER_INACTIVE;
		return 1;
	}
	return 0;
}

/**
 * hrtimer_start - (re)start an relative timer on the current CPU
 *
 * @timer:	the timer to be added
 * @tim:	expiry time
 * @mode:	expiry mode: absolute (HRTIMER_ABS) or relative (HRTIMER_REL)
 *
 * Returns:
 *  0 on success
 *  1 when the timer was active
 */
int
hrtimer_start(struct hrtimer *timer, ktime_t tim, const enum hrtimer_mode mode)
{
	struct hrtimer_base *base, *new_base;
	unsigned long flags;
	int ret;

	base = lock_hrtimer_base(timer, &flags);

	/* Remove an active timer from the queue: */
	ret = remove_hrtimer(timer, base);

	/* Switch the timer base, if necessary: */
	new_base = switch_hrtimer_base(timer, base);

	if (mode == HRTIMER_REL)
		tim = ktime_add(tim, new_base->get_time());
	timer->expires = tim;

	enqueue_hrtimer(timer, new_base);

	unlock_hrtimer_base(timer, &flags);

	return ret;
}

/**
 * hrtimer_try_to_cancel - try to deactivate a timer
 *
 * @timer:	hrtimer to stop
 *
 * Returns:
 *  0 when the timer was not active
 *  1 when the timer was active
 * -1 when the timer is currently excuting the callback function and
 *    can not be stopped
 */
int hrtimer_try_to_cancel(struct hrtimer *timer)
{
	struct hrtimer_base *base;
	unsigned long flags;
	int ret = -1;

	base = lock_hrtimer_base(timer, &flags);

	if (base->curr_timer != timer)
		ret = remove_hrtimer(timer, base);

	unlock_hrtimer_base(timer, &flags);

	return ret;

}

/**
 * hrtimer_cancel - cancel a timer and wait for the handler to finish.
 *
 * @timer:	the timer to be cancelled
 *
 * Returns:
 *  0 when the timer was not active
 *  1 when the timer was active
 */
int hrtimer_cancel(struct hrtimer *timer)
{
	for (;;) {
		int ret = hrtimer_try_to_cancel(timer);

		if (ret >= 0)
			return ret;
	}
}

/**
 * hrtimer_get_remaining - get remaining time for the timer
 *
 * @timer:	the timer to read
 */
ktime_t hrtimer_get_remaining(const struct hrtimer *timer)
{
	struct hrtimer_base *base;
	unsigned long flags;
	ktime_t rem;

	base = lock_hrtimer_base(timer, &flags);
	rem = ktime_sub(timer->expires, timer->base->get_time());
	unlock_hrtimer_base(timer, &flags);

	return rem;
}

/**
 * hrtimer_init - initialize a timer to the given clock
 *
 * @timer:	the timer to be initialized
 * @clock_id:	the clock to be used
 * @mode:	timer mode abs/rel
 */
void hrtimer_init(struct hrtimer *timer, clockid_t clock_id,
		  enum hrtimer_mode mode)
{
	struct hrtimer_base *bases;

	memset(timer, 0, sizeof(struct hrtimer));

	bases = per_cpu(hrtimer_bases, raw_smp_processor_id());

	if (clock_id == CLOCK_REALTIME && mode != HRTIMER_ABS)
		clock_id = CLOCK_MONOTONIC;

	timer->base = &bases[clock_id];
}

/**
 * hrtimer_get_res - get the timer resolution for a clock
 *
 * @which_clock: which clock to query
 * @tp:		 pointer to timespec variable to store the resolution
 *
 * Store the resolution of the clock selected by which_clock in the
 * variable pointed to by tp.
 */
int hrtimer_get_res(const clockid_t which_clock, struct timespec *tp)
{
	struct hrtimer_base *bases;

	bases = per_cpu(hrtimer_bases, raw_smp_processor_id());
	*tp = ktime_to_timespec(bases[which_clock].resolution);

	return 0;
}

/*
 * Expire the per base hrtimer-queue:
 */
static inline void run_hrtimer_queue(struct hrtimer_base *base)
{
	ktime_t now = base->get_time();
	struct rb_node *node;

	spin_lock_irq(&base->lock);

	while ((node = base->first)) {
		struct hrtimer *timer;
		int (*fn)(void *);
		int restart;
		void *data;

		timer = rb_entry(node, struct hrtimer, node);
		if (now.tv64 <= timer->expires.tv64)
			break;

		fn = timer->function;
		data = timer->data;
		set_curr_timer(base, timer);
		timer->state = HRTIMER_RUNNING;
		__remove_hrtimer(timer, base);
		spin_unlock_irq(&base->lock);

		/*
		 * fn == NULL is special case for the simplest timer
		 * variant - wake up process and do not restart:
		 */
		if (!fn) {
			wake_up_process(data);
			restart = HRTIMER_NORESTART;
		} else
			restart = fn(data);

		spin_lock_irq(&base->lock);

		/* Another CPU has added back the timer */
		if (timer->state != HRTIMER_RUNNING)
			continue;

		if (restart == HRTIMER_RESTART)
			enqueue_hrtimer(timer, base);
		else
			timer->state = HRTIMER_EXPIRED;
	}
	set_curr_timer(base, NULL);
	spin_unlock_irq(&base->lock);
}

/*
 * Called from timer softirq every jiffy, expire hrtimers:
 */
void hrtimer_run_queues(void)
{
	struct hrtimer_base *base = __get_cpu_var(hrtimer_bases);
	int i;

	for (i = 0; i < MAX_HRTIMER_BASES; i++)
		run_hrtimer_queue(&base[i]);
}

/*
 * Sleep related functions:
 */

/**
 * schedule_hrtimer - sleep until timeout
 *
 * @timer:	hrtimer variable initialized with the correct clock base
 * @mode:	timeout value is abs/rel
 *
 * Make the current task sleep until @timeout is
 * elapsed.
 *
 * You can set the task state as follows -
 *
 * %TASK_UNINTERRUPTIBLE - at least @timeout is guaranteed to
 * pass before the routine returns. The routine will return 0
 *
 * %TASK_INTERRUPTIBLE - the routine may return early if a signal is
 * delivered to the current task. In this case the remaining time
 * will be returned
 *
 * The current task state is guaranteed to be TASK_RUNNING when this
 * routine returns.
 */
static ktime_t __sched
schedule_hrtimer(struct hrtimer *timer, const enum hrtimer_mode mode)
{
	/* fn stays NULL, meaning single-shot wakeup: */
	timer->data = current;

	hrtimer_start(timer, timer->expires, mode);

	schedule();
	hrtimer_cancel(timer);

	/* Return the remaining time: */
	if (timer->state != HRTIMER_EXPIRED)
		return ktime_sub(timer->expires, timer->base->get_time());
	else
		return (ktime_t) {.tv64 = 0 };
}

static inline ktime_t __sched
schedule_hrtimer_interruptible(struct hrtimer *timer,
			       const enum hrtimer_mode mode)
{
	set_current_state(TASK_INTERRUPTIBLE);

	return schedule_hrtimer(timer, mode);
}

static long __sched nanosleep_restart(struct restart_block *restart)
{
	struct timespec __user *rmtp;
	struct timespec tu;
	void *rfn_save = restart->fn;
	struct hrtimer timer;
	ktime_t rem;

	restart->fn = do_no_restart_syscall;

	hrtimer_init(&timer, (clockid_t) restart->arg3, HRTIMER_ABS);

	timer.expires.tv64 = ((u64)restart->arg1 << 32) | (u64) restart->arg0;

	rem = schedule_hrtimer_interruptible(&timer, HRTIMER_ABS);

	if (rem.tv64 <= 0)
		return 0;

	rmtp = (struct timespec __user *) restart->arg2;
	tu = ktime_to_timespec(rem);
	if (rmtp && copy_to_user(rmtp, &tu, sizeof(tu)))
		return -EFAULT;

	restart->fn = rfn_save;

	/* The other values in restart are already filled in */
	return -ERESTART_RESTARTBLOCK;
}

long hrtimer_nanosleep(struct timespec *rqtp, struct timespec __user *rmtp,
		       const enum hrtimer_mode mode, const clockid_t clockid)
{
	struct restart_block *restart;
	struct hrtimer timer;
	struct timespec tu;
	ktime_t rem;

	hrtimer_init(&timer, clockid, mode);

	timer.expires = timespec_to_ktime(*rqtp);

	rem = schedule_hrtimer_interruptible(&timer, mode);
	if (rem.tv64 <= 0)
		return 0;

	/* Absolute timers do not update the rmtp value and restart: */
	if (mode == HRTIMER_ABS)
		return -ERESTARTNOHAND;

	tu = ktime_to_timespec(rem);

	if (rmtp && copy_to_user(rmtp, &tu, sizeof(tu)))
		return -EFAULT;

	restart = &current_thread_info()->restart_block;
	restart->fn = nanosleep_restart;
	restart->arg0 = timer.expires.tv64 & 0xFFFFFFFF;
	restart->arg1 = timer.expires.tv64 >> 32;
	restart->arg2 = (unsigned long) rmtp;
	restart->arg3 = (unsigned long) timer.base->index;

	return -ERESTART_RESTARTBLOCK;
}

asmlinkage long
sys_nanosleep(struct timespec __user *rqtp, struct timespec __user *rmtp)
{
	struct timespec tu;

	if (copy_from_user(&tu, rqtp, sizeof(tu)))
		return -EFAULT;

	if (!timespec_valid(&tu))
		return -EINVAL;

	return hrtimer_nanosleep(&tu, rmtp, HRTIMER_REL, CLOCK_MONOTONIC);
}

/*
 * Functions related to boot-time initialization:
 */
static void __devinit init_hrtimers_cpu(int cpu)
{
	struct hrtimer_base *base = per_cpu(hrtimer_bases, cpu);
	int i;

	for (i = 0; i < MAX_HRTIMER_BASES; i++, base++)
		spin_lock_init(&base->lock);
}

#ifdef CONFIG_HOTPLUG_CPU

static void migrate_hrtimer_list(struct hrtimer_base *old_base,
				struct hrtimer_base *new_base)
{
	struct hrtimer *timer;
	struct rb_node *node;

	while ((node = rb_first(&old_base->active))) {
		timer = rb_entry(node, struct hrtimer, node);
		__remove_hrtimer(timer, old_base);
		timer->base = new_base;
		enqueue_hrtimer(timer, new_base);
	}
}

static void migrate_hrtimers(int cpu)
{
	struct hrtimer_base *old_base, *new_base;
	int i;

	BUG_ON(cpu_online(cpu));
	old_base = per_cpu(hrtimer_bases, cpu);
	new_base = get_cpu_var(hrtimer_bases);

	local_irq_disable();

	for (i = 0; i < MAX_HRTIMER_BASES; i++) {

		spin_lock(&new_base->lock);
		spin_lock(&old_base->lock);

		BUG_ON(old_base->curr_timer);

		migrate_hrtimer_list(old_base, new_base);

		spin_unlock(&old_base->lock);
		spin_unlock(&new_base->lock);
		old_base++;
		new_base++;
	}

	local_irq_enable();
	put_cpu_var(hrtimer_bases);
}
#endif /* CONFIG_HOTPLUG_CPU */

static int __devinit hrtimer_cpu_notify(struct notifier_block *self,
					unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;

	switch (action) {

	case CPU_UP_PREPARE:
		init_hrtimers_cpu(cpu);
		break;

#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
		migrate_hrtimers(cpu);
		break;
#endif

	default:
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block __devinitdata hrtimers_nb = {
	.notifier_call = hrtimer_cpu_notify,
};

void __init hrtimers_init(void)
{
	hrtimer_cpu_notify(&hrtimers_nb, (unsigned long)CPU_UP_PREPARE,
			  (void *)(long)smp_processor_id());
	register_cpu_notifier(&hrtimers_nb);
}

