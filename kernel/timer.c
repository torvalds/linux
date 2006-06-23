/*
 *  linux/kernel/timer.c
 *
 *  Kernel internal timers, kernel timekeeping, basic process system calls
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-01-28  Modified by Finn Arne Gangstad to make timers scale better.
 *
 *  1997-09-10  Updated NTP code according to technical memorandum Jan '96
 *              "A Kernel Model for Precision Timekeeping" by Dave Mills
 *  1998-12-24  Fixed a xtime SMP race (we need the xtime_lock rw spinlock to
 *              serialize accesses to xtime/lost_ticks).
 *                              Copyright (C) 1998  Andrea Arcangeli
 *  1999-03-10  Improved NTP compatibility by Ulrich Windl
 *  2002-05-31	Move sys_sysinfo here and make its locking sane, Robert Love
 *  2000-10-05  Implemented scalable SMP per-CPU timer handling.
 *                              Copyright (C) 2000, 2001, 2002  Ingo Molnar
 *              Designed by David S. Miller, Alexey Kuznetsov and Ingo Molnar
 */

#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/notifier.h>
#include <linux/thread_info.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/posix-timers.h>
#include <linux/cpu.h>
#include <linux/syscalls.h>
#include <linux/delay.h>

#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <asm/div64.h>
#include <asm/timex.h>
#include <asm/io.h>

#ifdef CONFIG_TIME_INTERPOLATION
static void time_interpolator_update(long delta_nsec);
#else
#define time_interpolator_update(x)
#endif

u64 jiffies_64 __cacheline_aligned_in_smp = INITIAL_JIFFIES;

EXPORT_SYMBOL(jiffies_64);

/*
 * per-CPU timer vector definitions:
 */
#define TVN_BITS (CONFIG_BASE_SMALL ? 4 : 6)
#define TVR_BITS (CONFIG_BASE_SMALL ? 6 : 8)
#define TVN_SIZE (1 << TVN_BITS)
#define TVR_SIZE (1 << TVR_BITS)
#define TVN_MASK (TVN_SIZE - 1)
#define TVR_MASK (TVR_SIZE - 1)

typedef struct tvec_s {
	struct list_head vec[TVN_SIZE];
} tvec_t;

typedef struct tvec_root_s {
	struct list_head vec[TVR_SIZE];
} tvec_root_t;

struct tvec_t_base_s {
	spinlock_t lock;
	struct timer_list *running_timer;
	unsigned long timer_jiffies;
	tvec_root_t tv1;
	tvec_t tv2;
	tvec_t tv3;
	tvec_t tv4;
	tvec_t tv5;
} ____cacheline_aligned_in_smp;

typedef struct tvec_t_base_s tvec_base_t;

tvec_base_t boot_tvec_bases;
EXPORT_SYMBOL(boot_tvec_bases);
static DEFINE_PER_CPU(tvec_base_t *, tvec_bases) = { &boot_tvec_bases };

static inline void set_running_timer(tvec_base_t *base,
					struct timer_list *timer)
{
#ifdef CONFIG_SMP
	base->running_timer = timer;
#endif
}

static void internal_add_timer(tvec_base_t *base, struct timer_list *timer)
{
	unsigned long expires = timer->expires;
	unsigned long idx = expires - base->timer_jiffies;
	struct list_head *vec;

	if (idx < TVR_SIZE) {
		int i = expires & TVR_MASK;
		vec = base->tv1.vec + i;
	} else if (idx < 1 << (TVR_BITS + TVN_BITS)) {
		int i = (expires >> TVR_BITS) & TVN_MASK;
		vec = base->tv2.vec + i;
	} else if (idx < 1 << (TVR_BITS + 2 * TVN_BITS)) {
		int i = (expires >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
		vec = base->tv3.vec + i;
	} else if (idx < 1 << (TVR_BITS + 3 * TVN_BITS)) {
		int i = (expires >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
		vec = base->tv4.vec + i;
	} else if ((signed long) idx < 0) {
		/*
		 * Can happen if you add a timer with expires == jiffies,
		 * or you set a timer to go off in the past
		 */
		vec = base->tv1.vec + (base->timer_jiffies & TVR_MASK);
	} else {
		int i;
		/* If the timeout is larger than 0xffffffff on 64-bit
		 * architectures then we use the maximum timeout:
		 */
		if (idx > 0xffffffffUL) {
			idx = 0xffffffffUL;
			expires = idx + base->timer_jiffies;
		}
		i = (expires >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;
		vec = base->tv5.vec + i;
	}
	/*
	 * Timers are FIFO:
	 */
	list_add_tail(&timer->entry, vec);
}

/***
 * init_timer - initialize a timer.
 * @timer: the timer to be initialized
 *
 * init_timer() must be done to a timer prior calling *any* of the
 * other timer functions.
 */
void fastcall init_timer(struct timer_list *timer)
{
	timer->entry.next = NULL;
	timer->base = per_cpu(tvec_bases, raw_smp_processor_id());
}
EXPORT_SYMBOL(init_timer);

static inline void detach_timer(struct timer_list *timer,
					int clear_pending)
{
	struct list_head *entry = &timer->entry;

	__list_del(entry->prev, entry->next);
	if (clear_pending)
		entry->next = NULL;
	entry->prev = LIST_POISON2;
}

/*
 * We are using hashed locking: holding per_cpu(tvec_bases).lock
 * means that all timers which are tied to this base via timer->base are
 * locked, and the base itself is locked too.
 *
 * So __run_timers/migrate_timers can safely modify all timers which could
 * be found on ->tvX lists.
 *
 * When the timer's base is locked, and the timer removed from list, it is
 * possible to set timer->base = NULL and drop the lock: the timer remains
 * locked.
 */
static tvec_base_t *lock_timer_base(struct timer_list *timer,
					unsigned long *flags)
{
	tvec_base_t *base;

	for (;;) {
		base = timer->base;
		if (likely(base != NULL)) {
			spin_lock_irqsave(&base->lock, *flags);
			if (likely(base == timer->base))
				return base;
			/* The timer has migrated to another CPU */
			spin_unlock_irqrestore(&base->lock, *flags);
		}
		cpu_relax();
	}
}

int __mod_timer(struct timer_list *timer, unsigned long expires)
{
	tvec_base_t *base, *new_base;
	unsigned long flags;
	int ret = 0;

	BUG_ON(!timer->function);

	base = lock_timer_base(timer, &flags);

	if (timer_pending(timer)) {
		detach_timer(timer, 0);
		ret = 1;
	}

	new_base = __get_cpu_var(tvec_bases);

	if (base != new_base) {
		/*
		 * We are trying to schedule the timer on the local CPU.
		 * However we can't change timer's base while it is running,
		 * otherwise del_timer_sync() can't detect that the timer's
		 * handler yet has not finished. This also guarantees that
		 * the timer is serialized wrt itself.
		 */
		if (likely(base->running_timer != timer)) {
			/* See the comment in lock_timer_base() */
			timer->base = NULL;
			spin_unlock(&base->lock);
			base = new_base;
			spin_lock(&base->lock);
			timer->base = base;
		}
	}

	timer->expires = expires;
	internal_add_timer(base, timer);
	spin_unlock_irqrestore(&base->lock, flags);

	return ret;
}

EXPORT_SYMBOL(__mod_timer);

/***
 * add_timer_on - start a timer on a particular CPU
 * @timer: the timer to be added
 * @cpu: the CPU to start it on
 *
 * This is not very scalable on SMP. Double adds are not possible.
 */
void add_timer_on(struct timer_list *timer, int cpu)
{
	tvec_base_t *base = per_cpu(tvec_bases, cpu);
  	unsigned long flags;

  	BUG_ON(timer_pending(timer) || !timer->function);
	spin_lock_irqsave(&base->lock, flags);
	timer->base = base;
	internal_add_timer(base, timer);
	spin_unlock_irqrestore(&base->lock, flags);
}


/***
 * mod_timer - modify a timer's timeout
 * @timer: the timer to be modified
 *
 * mod_timer is a more efficient way to update the expire field of an
 * active timer (if the timer is inactive it will be activated)
 *
 * mod_timer(timer, expires) is equivalent to:
 *
 *     del_timer(timer); timer->expires = expires; add_timer(timer);
 *
 * Note that if there are multiple unserialized concurrent users of the
 * same timer, then mod_timer() is the only safe way to modify the timeout,
 * since add_timer() cannot modify an already running timer.
 *
 * The function returns whether it has modified a pending timer or not.
 * (ie. mod_timer() of an inactive timer returns 0, mod_timer() of an
 * active timer returns 1.)
 */
int mod_timer(struct timer_list *timer, unsigned long expires)
{
	BUG_ON(!timer->function);

	/*
	 * This is a common optimization triggered by the
	 * networking code - if the timer is re-modified
	 * to be the same thing then just return:
	 */
	if (timer->expires == expires && timer_pending(timer))
		return 1;

	return __mod_timer(timer, expires);
}

EXPORT_SYMBOL(mod_timer);

/***
 * del_timer - deactive a timer.
 * @timer: the timer to be deactivated
 *
 * del_timer() deactivates a timer - this works on both active and inactive
 * timers.
 *
 * The function returns whether it has deactivated a pending timer or not.
 * (ie. del_timer() of an inactive timer returns 0, del_timer() of an
 * active timer returns 1.)
 */
int del_timer(struct timer_list *timer)
{
	tvec_base_t *base;
	unsigned long flags;
	int ret = 0;

	if (timer_pending(timer)) {
		base = lock_timer_base(timer, &flags);
		if (timer_pending(timer)) {
			detach_timer(timer, 1);
			ret = 1;
		}
		spin_unlock_irqrestore(&base->lock, flags);
	}

	return ret;
}

EXPORT_SYMBOL(del_timer);

#ifdef CONFIG_SMP
/*
 * This function tries to deactivate a timer. Upon successful (ret >= 0)
 * exit the timer is not queued and the handler is not running on any CPU.
 *
 * It must not be called from interrupt contexts.
 */
int try_to_del_timer_sync(struct timer_list *timer)
{
	tvec_base_t *base;
	unsigned long flags;
	int ret = -1;

	base = lock_timer_base(timer, &flags);

	if (base->running_timer == timer)
		goto out;

	ret = 0;
	if (timer_pending(timer)) {
		detach_timer(timer, 1);
		ret = 1;
	}
out:
	spin_unlock_irqrestore(&base->lock, flags);

	return ret;
}

/***
 * del_timer_sync - deactivate a timer and wait for the handler to finish.
 * @timer: the timer to be deactivated
 *
 * This function only differs from del_timer() on SMP: besides deactivating
 * the timer it also makes sure the handler has finished executing on other
 * CPUs.
 *
 * Synchronization rules: callers must prevent restarting of the timer,
 * otherwise this function is meaningless. It must not be called from
 * interrupt contexts. The caller must not hold locks which would prevent
 * completion of the timer's handler. The timer's handler must not call
 * add_timer_on(). Upon exit the timer is not queued and the handler is
 * not running on any CPU.
 *
 * The function returns whether it has deactivated a pending timer or not.
 */
int del_timer_sync(struct timer_list *timer)
{
	for (;;) {
		int ret = try_to_del_timer_sync(timer);
		if (ret >= 0)
			return ret;
	}
}

EXPORT_SYMBOL(del_timer_sync);
#endif

static int cascade(tvec_base_t *base, tvec_t *tv, int index)
{
	/* cascade all the timers from tv up one level */
	struct timer_list *timer, *tmp;
	struct list_head tv_list;

	list_replace_init(tv->vec + index, &tv_list);

	/*
	 * We are removing _all_ timers from the list, so we
	 * don't have to detach them individually.
	 */
	list_for_each_entry_safe(timer, tmp, &tv_list, entry) {
		BUG_ON(timer->base != base);
		internal_add_timer(base, timer);
	}

	return index;
}

/***
 * __run_timers - run all expired timers (if any) on this CPU.
 * @base: the timer vector to be processed.
 *
 * This function cascades all vectors and executes all expired timer
 * vectors.
 */
#define INDEX(N) (base->timer_jiffies >> (TVR_BITS + N * TVN_BITS)) & TVN_MASK

static inline void __run_timers(tvec_base_t *base)
{
	struct timer_list *timer;

	spin_lock_irq(&base->lock);
	while (time_after_eq(jiffies, base->timer_jiffies)) {
		struct list_head work_list;
		struct list_head *head = &work_list;
 		int index = base->timer_jiffies & TVR_MASK;

		/*
		 * Cascade timers:
		 */
		if (!index &&
			(!cascade(base, &base->tv2, INDEX(0))) &&
				(!cascade(base, &base->tv3, INDEX(1))) &&
					!cascade(base, &base->tv4, INDEX(2)))
			cascade(base, &base->tv5, INDEX(3));
		++base->timer_jiffies;
		list_replace_init(base->tv1.vec + index, &work_list);
		while (!list_empty(head)) {
			void (*fn)(unsigned long);
			unsigned long data;

			timer = list_entry(head->next,struct timer_list,entry);
 			fn = timer->function;
 			data = timer->data;

			set_running_timer(base, timer);
			detach_timer(timer, 1);
			spin_unlock_irq(&base->lock);
			{
				int preempt_count = preempt_count();
				fn(data);
				if (preempt_count != preempt_count()) {
					printk(KERN_WARNING "huh, entered %p "
					       "with preempt_count %08x, exited"
					       " with %08x?\n",
					       fn, preempt_count,
					       preempt_count());
					BUG();
				}
			}
			spin_lock_irq(&base->lock);
		}
	}
	set_running_timer(base, NULL);
	spin_unlock_irq(&base->lock);
}

#ifdef CONFIG_NO_IDLE_HZ
/*
 * Find out when the next timer event is due to happen. This
 * is used on S/390 to stop all activity when a cpus is idle.
 * This functions needs to be called disabled.
 */
unsigned long next_timer_interrupt(void)
{
	tvec_base_t *base;
	struct list_head *list;
	struct timer_list *nte;
	unsigned long expires;
	unsigned long hr_expires = MAX_JIFFY_OFFSET;
	ktime_t hr_delta;
	tvec_t *varray[4];
	int i, j;

	hr_delta = hrtimer_get_next_event();
	if (hr_delta.tv64 != KTIME_MAX) {
		struct timespec tsdelta;
		tsdelta = ktime_to_timespec(hr_delta);
		hr_expires = timespec_to_jiffies(&tsdelta);
		if (hr_expires < 3)
			return hr_expires + jiffies;
	}
	hr_expires += jiffies;

	base = __get_cpu_var(tvec_bases);
	spin_lock(&base->lock);
	expires = base->timer_jiffies + (LONG_MAX >> 1);
	list = NULL;

	/* Look for timer events in tv1. */
	j = base->timer_jiffies & TVR_MASK;
	do {
		list_for_each_entry(nte, base->tv1.vec + j, entry) {
			expires = nte->expires;
			if (j < (base->timer_jiffies & TVR_MASK))
				list = base->tv2.vec + (INDEX(0));
			goto found;
		}
		j = (j + 1) & TVR_MASK;
	} while (j != (base->timer_jiffies & TVR_MASK));

	/* Check tv2-tv5. */
	varray[0] = &base->tv2;
	varray[1] = &base->tv3;
	varray[2] = &base->tv4;
	varray[3] = &base->tv5;
	for (i = 0; i < 4; i++) {
		j = INDEX(i);
		do {
			if (list_empty(varray[i]->vec + j)) {
				j = (j + 1) & TVN_MASK;
				continue;
			}
			list_for_each_entry(nte, varray[i]->vec + j, entry)
				if (time_before(nte->expires, expires))
					expires = nte->expires;
			if (j < (INDEX(i)) && i < 3)
				list = varray[i + 1]->vec + (INDEX(i + 1));
			goto found;
		} while (j != (INDEX(i)));
	}
found:
	if (list) {
		/*
		 * The search wrapped. We need to look at the next list
		 * from next tv element that would cascade into tv element
		 * where we found the timer element.
		 */
		list_for_each_entry(nte, list, entry) {
			if (time_before(nte->expires, expires))
				expires = nte->expires;
		}
	}
	spin_unlock(&base->lock);

	/*
	 * It can happen that other CPUs service timer IRQs and increment
	 * jiffies, but we have not yet got a local timer tick to process
	 * the timer wheels.  In that case, the expiry time can be before
	 * jiffies, but since the high-resolution timer here is relative to
	 * jiffies, the default expression when high-resolution timers are
	 * not active,
	 *
	 *   time_before(MAX_JIFFY_OFFSET + jiffies, expires)
	 *
	 * would falsely evaluate to true.  If that is the case, just
	 * return jiffies so that we can immediately fire the local timer
	 */
	if (time_before(expires, jiffies))
		return jiffies;

	if (time_before(hr_expires, expires))
		return hr_expires;

	return expires;
}
#endif

/******************************************************************/

/*
 * Timekeeping variables
 */
unsigned long tick_usec = TICK_USEC; 		/* USER_HZ period (usec) */
unsigned long tick_nsec = TICK_NSEC;		/* ACTHZ period (nsec) */

/* 
 * The current time 
 * wall_to_monotonic is what we need to add to xtime (or xtime corrected 
 * for sub jiffie times) to get to monotonic time.  Monotonic is pegged
 * at zero at system boot time, so wall_to_monotonic will be negative,
 * however, we will ALWAYS keep the tv_nsec part positive so we can use
 * the usual normalization.
 */
struct timespec xtime __attribute__ ((aligned (16)));
struct timespec wall_to_monotonic __attribute__ ((aligned (16)));

EXPORT_SYMBOL(xtime);

/* Don't completely fail for HZ > 500.  */
int tickadj = 500/HZ ? : 1;		/* microsecs */


/*
 * phase-lock loop variables
 */
/* TIME_ERROR prevents overwriting the CMOS clock */
int time_state = TIME_OK;		/* clock synchronization status	*/
int time_status = STA_UNSYNC;		/* clock status bits		*/
long time_offset;			/* time adjustment (us)		*/
long time_constant = 2;			/* pll time constant		*/
long time_tolerance = MAXFREQ;		/* frequency tolerance (ppm)	*/
long time_precision = 1;		/* clock precision (us)		*/
long time_maxerror = NTP_PHASE_LIMIT;	/* maximum error (us)		*/
long time_esterror = NTP_PHASE_LIMIT;	/* estimated error (us)		*/
static long time_phase;			/* phase offset (scaled us)	*/
long time_freq = (((NSEC_PER_SEC + HZ/2) % HZ - HZ/2) << SHIFT_USEC) / NSEC_PER_USEC;
					/* frequency offset (scaled ppm)*/
static long time_adj;			/* tick adjust (scaled 1 / HZ)	*/
long time_reftime;			/* time at last adjustment (s)	*/
long time_adjust;
long time_next_adjust;

/*
 * this routine handles the overflow of the microsecond field
 *
 * The tricky bits of code to handle the accurate clock support
 * were provided by Dave Mills (Mills@UDEL.EDU) of NTP fame.
 * They were originally developed for SUN and DEC kernels.
 * All the kudos should go to Dave for this stuff.
 *
 */
static void second_overflow(void)
{
	long ltemp;

	/* Bump the maxerror field */
	time_maxerror += time_tolerance >> SHIFT_USEC;
	if (time_maxerror > NTP_PHASE_LIMIT) {
		time_maxerror = NTP_PHASE_LIMIT;
		time_status |= STA_UNSYNC;
	}

	/*
	 * Leap second processing. If in leap-insert state at the end of the
	 * day, the system clock is set back one second; if in leap-delete
	 * state, the system clock is set ahead one second. The microtime()
	 * routine or external clock driver will insure that reported time is
	 * always monotonic. The ugly divides should be replaced.
	 */
	switch (time_state) {
	case TIME_OK:
		if (time_status & STA_INS)
			time_state = TIME_INS;
		else if (time_status & STA_DEL)
			time_state = TIME_DEL;
		break;
	case TIME_INS:
		if (xtime.tv_sec % 86400 == 0) {
			xtime.tv_sec--;
			wall_to_monotonic.tv_sec++;
			/*
			 * The timer interpolator will make time change
			 * gradually instead of an immediate jump by one second
			 */
			time_interpolator_update(-NSEC_PER_SEC);
			time_state = TIME_OOP;
			clock_was_set();
			printk(KERN_NOTICE "Clock: inserting leap second "
					"23:59:60 UTC\n");
		}
		break;
	case TIME_DEL:
		if ((xtime.tv_sec + 1) % 86400 == 0) {
			xtime.tv_sec++;
			wall_to_monotonic.tv_sec--;
			/*
			 * Use of time interpolator for a gradual change of
			 * time
			 */
			time_interpolator_update(NSEC_PER_SEC);
			time_state = TIME_WAIT;
			clock_was_set();
			printk(KERN_NOTICE "Clock: deleting leap second "
					"23:59:59 UTC\n");
		}
		break;
	case TIME_OOP:
		time_state = TIME_WAIT;
		break;
	case TIME_WAIT:
		if (!(time_status & (STA_INS | STA_DEL)))
		time_state = TIME_OK;
	}

	/*
	 * Compute the phase adjustment for the next second. In PLL mode, the
	 * offset is reduced by a fixed factor times the time constant. In FLL
	 * mode the offset is used directly. In either mode, the maximum phase
	 * adjustment for each second is clamped so as to spread the adjustment
	 * over not more than the number of seconds between updates.
	 */
	ltemp = time_offset;
	if (!(time_status & STA_FLL))
		ltemp = shift_right(ltemp, SHIFT_KG + time_constant);
	ltemp = min(ltemp, (MAXPHASE / MINSEC) << SHIFT_UPDATE);
	ltemp = max(ltemp, -(MAXPHASE / MINSEC) << SHIFT_UPDATE);
	time_offset -= ltemp;
	time_adj = ltemp << (SHIFT_SCALE - SHIFT_HZ - SHIFT_UPDATE);

	/*
	 * Compute the frequency estimate and additional phase adjustment due
	 * to frequency error for the next second.
	 */
	ltemp = time_freq;
	time_adj += shift_right(ltemp,(SHIFT_USEC + SHIFT_HZ - SHIFT_SCALE));

#if HZ == 100
	/*
	 * Compensate for (HZ==100) != (1 << SHIFT_HZ).  Add 25% and 3.125% to
	 * get 128.125; => only 0.125% error (p. 14)
	 */
	time_adj += shift_right(time_adj, 2) + shift_right(time_adj, 5);
#endif
#if HZ == 250
	/*
	 * Compensate for (HZ==250) != (1 << SHIFT_HZ).  Add 1.5625% and
	 * 0.78125% to get 255.85938; => only 0.05% error (p. 14)
	 */
	time_adj += shift_right(time_adj, 6) + shift_right(time_adj, 7);
#endif
#if HZ == 1000
	/*
	 * Compensate for (HZ==1000) != (1 << SHIFT_HZ).  Add 1.5625% and
	 * 0.78125% to get 1023.4375; => only 0.05% error (p. 14)
	 */
	time_adj += shift_right(time_adj, 6) + shift_right(time_adj, 7);
#endif
}

/*
 * Returns how many microseconds we need to add to xtime this tick
 * in doing an adjustment requested with adjtime.
 */
static long adjtime_adjustment(void)
{
	long time_adjust_step;

	time_adjust_step = time_adjust;
	if (time_adjust_step) {
		/*
		 * We are doing an adjtime thing.  Prepare time_adjust_step to
		 * be within bounds.  Note that a positive time_adjust means we
		 * want the clock to run faster.
		 *
		 * Limit the amount of the step to be in the range
		 * -tickadj .. +tickadj
		 */
		time_adjust_step = min(time_adjust_step, (long)tickadj);
		time_adjust_step = max(time_adjust_step, (long)-tickadj);
	}
	return time_adjust_step;
}

/* in the NTP reference this is called "hardclock()" */
static void update_wall_time_one_tick(void)
{
	long time_adjust_step, delta_nsec;

	time_adjust_step = adjtime_adjustment();
	if (time_adjust_step)
		/* Reduce by this step the amount of time left  */
		time_adjust -= time_adjust_step;
	delta_nsec = tick_nsec + time_adjust_step * 1000;
	/*
	 * Advance the phase, once it gets to one microsecond, then
	 * advance the tick more.
	 */
	time_phase += time_adj;
	if ((time_phase >= FINENSEC) || (time_phase <= -FINENSEC)) {
		long ltemp = shift_right(time_phase, (SHIFT_SCALE - 10));
		time_phase -= ltemp << (SHIFT_SCALE - 10);
		delta_nsec += ltemp;
	}
	xtime.tv_nsec += delta_nsec;
	time_interpolator_update(delta_nsec);

	/* Changes by adjtime() do not take effect till next tick. */
	if (time_next_adjust != 0) {
		time_adjust = time_next_adjust;
		time_next_adjust = 0;
	}
}

/*
 * Return how long ticks are at the moment, that is, how much time
 * update_wall_time_one_tick will add to xtime next time we call it
 * (assuming no calls to do_adjtimex in the meantime).
 * The return value is in fixed-point nanoseconds with SHIFT_SCALE-10
 * bits to the right of the binary point.
 * This function has no side-effects.
 */
u64 current_tick_length(void)
{
	long delta_nsec;

	delta_nsec = tick_nsec + adjtime_adjustment() * 1000;
	return ((u64) delta_nsec << (SHIFT_SCALE - 10)) + time_adj;
}

/*
 * Using a loop looks inefficient, but "ticks" is
 * usually just one (we shouldn't be losing ticks,
 * we're doing this this way mainly for interrupt
 * latency reasons, not because we think we'll
 * have lots of lost timer ticks
 */
static void update_wall_time(unsigned long ticks)
{
	do {
		ticks--;
		update_wall_time_one_tick();
		if (xtime.tv_nsec >= 1000000000) {
			xtime.tv_nsec -= 1000000000;
			xtime.tv_sec++;
			second_overflow();
		}
	} while (ticks);
}

/*
 * Called from the timer interrupt handler to charge one tick to the current 
 * process.  user_tick is 1 if the tick is user time, 0 for system.
 */
void update_process_times(int user_tick)
{
	struct task_struct *p = current;
	int cpu = smp_processor_id();

	/* Note: this timer irq context must be accounted for as well. */
	if (user_tick)
		account_user_time(p, jiffies_to_cputime(1));
	else
		account_system_time(p, HARDIRQ_OFFSET, jiffies_to_cputime(1));
	run_local_timers();
	if (rcu_pending(cpu))
		rcu_check_callbacks(cpu, user_tick);
	scheduler_tick();
 	run_posix_cpu_timers(p);
}

/*
 * Nr of active tasks - counted in fixed-point numbers
 */
static unsigned long count_active_tasks(void)
{
	return nr_active() * FIXED_1;
}

/*
 * Hmm.. Changed this, as the GNU make sources (load.c) seems to
 * imply that avenrun[] is the standard name for this kind of thing.
 * Nothing else seems to be standardized: the fractional size etc
 * all seem to differ on different machines.
 *
 * Requires xtime_lock to access.
 */
unsigned long avenrun[3];

EXPORT_SYMBOL(avenrun);

/*
 * calc_load - given tick count, update the avenrun load estimates.
 * This is called while holding a write_lock on xtime_lock.
 */
static inline void calc_load(unsigned long ticks)
{
	unsigned long active_tasks; /* fixed-point */
	static int count = LOAD_FREQ;

	count -= ticks;
	if (count < 0) {
		count += LOAD_FREQ;
		active_tasks = count_active_tasks();
		CALC_LOAD(avenrun[0], EXP_1, active_tasks);
		CALC_LOAD(avenrun[1], EXP_5, active_tasks);
		CALC_LOAD(avenrun[2], EXP_15, active_tasks);
	}
}

/* jiffies at the most recent update of wall time */
unsigned long wall_jiffies = INITIAL_JIFFIES;

/*
 * This read-write spinlock protects us from races in SMP while
 * playing with xtime and avenrun.
 */
#ifndef ARCH_HAVE_XTIME_LOCK
seqlock_t xtime_lock __cacheline_aligned_in_smp = SEQLOCK_UNLOCKED;

EXPORT_SYMBOL(xtime_lock);
#endif

/*
 * This function runs timers and the timer-tq in bottom half context.
 */
static void run_timer_softirq(struct softirq_action *h)
{
	tvec_base_t *base = __get_cpu_var(tvec_bases);

 	hrtimer_run_queues();
	if (time_after_eq(jiffies, base->timer_jiffies))
		__run_timers(base);
}

/*
 * Called by the local, per-CPU timer interrupt on SMP.
 */
void run_local_timers(void)
{
	raise_softirq(TIMER_SOFTIRQ);
	softlockup_tick();
}

/*
 * Called by the timer interrupt. xtime_lock must already be taken
 * by the timer IRQ!
 */
static inline void update_times(void)
{
	unsigned long ticks;

	ticks = jiffies - wall_jiffies;
	if (ticks) {
		wall_jiffies += ticks;
		update_wall_time(ticks);
	}
	calc_load(ticks);
}
  
/*
 * The 64-bit jiffies value is not atomic - you MUST NOT read it
 * without sampling the sequence number in xtime_lock.
 * jiffies is defined in the linker script...
 */

void do_timer(struct pt_regs *regs)
{
	jiffies_64++;
	/* prevent loading jiffies before storing new jiffies_64 value. */
	barrier();
	update_times();
}

#ifdef __ARCH_WANT_SYS_ALARM

/*
 * For backwards compatibility?  This can be done in libc so Alpha
 * and all newer ports shouldn't need it.
 */
asmlinkage unsigned long sys_alarm(unsigned int seconds)
{
	return alarm_setitimer(seconds);
}

#endif

#ifndef __alpha__

/*
 * The Alpha uses getxpid, getxuid, and getxgid instead.  Maybe this
 * should be moved into arch/i386 instead?
 */

/**
 * sys_getpid - return the thread group id of the current process
 *
 * Note, despite the name, this returns the tgid not the pid.  The tgid and
 * the pid are identical unless CLONE_THREAD was specified on clone() in
 * which case the tgid is the same in all threads of the same group.
 *
 * This is SMP safe as current->tgid does not change.
 */
asmlinkage long sys_getpid(void)
{
	return current->tgid;
}

/*
 * Accessing ->group_leader->real_parent is not SMP-safe, it could
 * change from under us. However, rather than getting any lock
 * we can use an optimistic algorithm: get the parent
 * pid, and go back and check that the parent is still
 * the same. If it has changed (which is extremely unlikely
 * indeed), we just try again..
 *
 * NOTE! This depends on the fact that even if we _do_
 * get an old value of "parent", we can happily dereference
 * the pointer (it was and remains a dereferencable kernel pointer
 * no matter what): we just can't necessarily trust the result
 * until we know that the parent pointer is valid.
 *
 * NOTE2: ->group_leader never changes from under us.
 */
asmlinkage long sys_getppid(void)
{
	int pid;
	struct task_struct *me = current;
	struct task_struct *parent;

	parent = me->group_leader->real_parent;
	for (;;) {
		pid = parent->tgid;
#if defined(CONFIG_SMP) || defined(CONFIG_PREEMPT)
{
		struct task_struct *old = parent;

		/*
		 * Make sure we read the pid before re-reading the
		 * parent pointer:
		 */
		smp_rmb();
		parent = me->group_leader->real_parent;
		if (old != parent)
			continue;
}
#endif
		break;
	}
	return pid;
}

asmlinkage long sys_getuid(void)
{
	/* Only we change this so SMP safe */
	return current->uid;
}

asmlinkage long sys_geteuid(void)
{
	/* Only we change this so SMP safe */
	return current->euid;
}

asmlinkage long sys_getgid(void)
{
	/* Only we change this so SMP safe */
	return current->gid;
}

asmlinkage long sys_getegid(void)
{
	/* Only we change this so SMP safe */
	return  current->egid;
}

#endif

static void process_timeout(unsigned long __data)
{
	wake_up_process((task_t *)__data);
}

/**
 * schedule_timeout - sleep until timeout
 * @timeout: timeout value in jiffies
 *
 * Make the current task sleep until @timeout jiffies have
 * elapsed. The routine will return immediately unless
 * the current task state has been set (see set_current_state()).
 *
 * You can set the task state as follows -
 *
 * %TASK_UNINTERRUPTIBLE - at least @timeout jiffies are guaranteed to
 * pass before the routine returns. The routine will return 0
 *
 * %TASK_INTERRUPTIBLE - the routine may return early if a signal is
 * delivered to the current task. In this case the remaining time
 * in jiffies will be returned, or 0 if the timer expired in time
 *
 * The current task state is guaranteed to be TASK_RUNNING when this
 * routine returns.
 *
 * Specifying a @timeout value of %MAX_SCHEDULE_TIMEOUT will schedule
 * the CPU away without a bound on the timeout. In this case the return
 * value will be %MAX_SCHEDULE_TIMEOUT.
 *
 * In all cases the return value is guaranteed to be non-negative.
 */
fastcall signed long __sched schedule_timeout(signed long timeout)
{
	struct timer_list timer;
	unsigned long expire;

	switch (timeout)
	{
	case MAX_SCHEDULE_TIMEOUT:
		/*
		 * These two special cases are useful to be comfortable
		 * in the caller. Nothing more. We could take
		 * MAX_SCHEDULE_TIMEOUT from one of the negative value
		 * but I' d like to return a valid offset (>=0) to allow
		 * the caller to do everything it want with the retval.
		 */
		schedule();
		goto out;
	default:
		/*
		 * Another bit of PARANOID. Note that the retval will be
		 * 0 since no piece of kernel is supposed to do a check
		 * for a negative retval of schedule_timeout() (since it
		 * should never happens anyway). You just have the printk()
		 * that will tell you if something is gone wrong and where.
		 */
		if (timeout < 0)
		{
			printk(KERN_ERR "schedule_timeout: wrong timeout "
				"value %lx from %p\n", timeout,
				__builtin_return_address(0));
			current->state = TASK_RUNNING;
			goto out;
		}
	}

	expire = timeout + jiffies;

	setup_timer(&timer, process_timeout, (unsigned long)current);
	__mod_timer(&timer, expire);
	schedule();
	del_singleshot_timer_sync(&timer);

	timeout = expire - jiffies;

 out:
	return timeout < 0 ? 0 : timeout;
}
EXPORT_SYMBOL(schedule_timeout);

/*
 * We can use __set_current_state() here because schedule_timeout() calls
 * schedule() unconditionally.
 */
signed long __sched schedule_timeout_interruptible(signed long timeout)
{
	__set_current_state(TASK_INTERRUPTIBLE);
	return schedule_timeout(timeout);
}
EXPORT_SYMBOL(schedule_timeout_interruptible);

signed long __sched schedule_timeout_uninterruptible(signed long timeout)
{
	__set_current_state(TASK_UNINTERRUPTIBLE);
	return schedule_timeout(timeout);
}
EXPORT_SYMBOL(schedule_timeout_uninterruptible);

/* Thread ID - the internal kernel "pid" */
asmlinkage long sys_gettid(void)
{
	return current->pid;
}

/*
 * sys_sysinfo - fill in sysinfo struct
 */ 
asmlinkage long sys_sysinfo(struct sysinfo __user *info)
{
	struct sysinfo val;
	unsigned long mem_total, sav_total;
	unsigned int mem_unit, bitcount;
	unsigned long seq;

	memset((char *)&val, 0, sizeof(struct sysinfo));

	do {
		struct timespec tp;
		seq = read_seqbegin(&xtime_lock);

		/*
		 * This is annoying.  The below is the same thing
		 * posix_get_clock_monotonic() does, but it wants to
		 * take the lock which we want to cover the loads stuff
		 * too.
		 */

		getnstimeofday(&tp);
		tp.tv_sec += wall_to_monotonic.tv_sec;
		tp.tv_nsec += wall_to_monotonic.tv_nsec;
		if (tp.tv_nsec - NSEC_PER_SEC >= 0) {
			tp.tv_nsec = tp.tv_nsec - NSEC_PER_SEC;
			tp.tv_sec++;
		}
		val.uptime = tp.tv_sec + (tp.tv_nsec ? 1 : 0);

		val.loads[0] = avenrun[0] << (SI_LOAD_SHIFT - FSHIFT);
		val.loads[1] = avenrun[1] << (SI_LOAD_SHIFT - FSHIFT);
		val.loads[2] = avenrun[2] << (SI_LOAD_SHIFT - FSHIFT);

		val.procs = nr_threads;
	} while (read_seqretry(&xtime_lock, seq));

	si_meminfo(&val);
	si_swapinfo(&val);

	/*
	 * If the sum of all the available memory (i.e. ram + swap)
	 * is less than can be stored in a 32 bit unsigned long then
	 * we can be binary compatible with 2.2.x kernels.  If not,
	 * well, in that case 2.2.x was broken anyways...
	 *
	 *  -Erik Andersen <andersee@debian.org>
	 */

	mem_total = val.totalram + val.totalswap;
	if (mem_total < val.totalram || mem_total < val.totalswap)
		goto out;
	bitcount = 0;
	mem_unit = val.mem_unit;
	while (mem_unit > 1) {
		bitcount++;
		mem_unit >>= 1;
		sav_total = mem_total;
		mem_total <<= 1;
		if (mem_total < sav_total)
			goto out;
	}

	/*
	 * If mem_total did not overflow, multiply all memory values by
	 * val.mem_unit and set it to 1.  This leaves things compatible
	 * with 2.2.x, and also retains compatibility with earlier 2.4.x
	 * kernels...
	 */

	val.mem_unit = 1;
	val.totalram <<= bitcount;
	val.freeram <<= bitcount;
	val.sharedram <<= bitcount;
	val.bufferram <<= bitcount;
	val.totalswap <<= bitcount;
	val.freeswap <<= bitcount;
	val.totalhigh <<= bitcount;
	val.freehigh <<= bitcount;

 out:
	if (copy_to_user(info, &val, sizeof(struct sysinfo)))
		return -EFAULT;

	return 0;
}

static int __devinit init_timers_cpu(int cpu)
{
	int j;
	tvec_base_t *base;
	static char __devinitdata tvec_base_done[NR_CPUS];

	if (!tvec_base_done[cpu]) {
		static char boot_done;

		if (boot_done) {
			/*
			 * The APs use this path later in boot
			 */
			base = kmalloc_node(sizeof(*base), GFP_KERNEL,
						cpu_to_node(cpu));
			if (!base)
				return -ENOMEM;
			memset(base, 0, sizeof(*base));
			per_cpu(tvec_bases, cpu) = base;
		} else {
			/*
			 * This is for the boot CPU - we use compile-time
			 * static initialisation because per-cpu memory isn't
			 * ready yet and because the memory allocators are not
			 * initialised either.
			 */
			boot_done = 1;
			base = &boot_tvec_bases;
		}
		tvec_base_done[cpu] = 1;
	} else {
		base = per_cpu(tvec_bases, cpu);
	}

	spin_lock_init(&base->lock);
	for (j = 0; j < TVN_SIZE; j++) {
		INIT_LIST_HEAD(base->tv5.vec + j);
		INIT_LIST_HEAD(base->tv4.vec + j);
		INIT_LIST_HEAD(base->tv3.vec + j);
		INIT_LIST_HEAD(base->tv2.vec + j);
	}
	for (j = 0; j < TVR_SIZE; j++)
		INIT_LIST_HEAD(base->tv1.vec + j);

	base->timer_jiffies = jiffies;
	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static void migrate_timer_list(tvec_base_t *new_base, struct list_head *head)
{
	struct timer_list *timer;

	while (!list_empty(head)) {
		timer = list_entry(head->next, struct timer_list, entry);
		detach_timer(timer, 0);
		timer->base = new_base;
		internal_add_timer(new_base, timer);
	}
}

static void __devinit migrate_timers(int cpu)
{
	tvec_base_t *old_base;
	tvec_base_t *new_base;
	int i;

	BUG_ON(cpu_online(cpu));
	old_base = per_cpu(tvec_bases, cpu);
	new_base = get_cpu_var(tvec_bases);

	local_irq_disable();
	spin_lock(&new_base->lock);
	spin_lock(&old_base->lock);

	BUG_ON(old_base->running_timer);

	for (i = 0; i < TVR_SIZE; i++)
		migrate_timer_list(new_base, old_base->tv1.vec + i);
	for (i = 0; i < TVN_SIZE; i++) {
		migrate_timer_list(new_base, old_base->tv2.vec + i);
		migrate_timer_list(new_base, old_base->tv3.vec + i);
		migrate_timer_list(new_base, old_base->tv4.vec + i);
		migrate_timer_list(new_base, old_base->tv5.vec + i);
	}

	spin_unlock(&old_base->lock);
	spin_unlock(&new_base->lock);
	local_irq_enable();
	put_cpu_var(tvec_bases);
}
#endif /* CONFIG_HOTPLUG_CPU */

static int timer_cpu_notify(struct notifier_block *self,
				unsigned long action, void *hcpu)
{
	long cpu = (long)hcpu;
	switch(action) {
	case CPU_UP_PREPARE:
		if (init_timers_cpu(cpu) < 0)
			return NOTIFY_BAD;
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_DEAD:
		migrate_timers(cpu);
		break;
#endif
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block timers_nb = {
	.notifier_call	= timer_cpu_notify,
};


void __init init_timers(void)
{
	timer_cpu_notify(&timers_nb, (unsigned long)CPU_UP_PREPARE,
				(void *)(long)smp_processor_id());
	register_cpu_notifier(&timers_nb);
	open_softirq(TIMER_SOFTIRQ, run_timer_softirq, NULL);
}

#ifdef CONFIG_TIME_INTERPOLATION

struct time_interpolator *time_interpolator __read_mostly;
static struct time_interpolator *time_interpolator_list __read_mostly;
static DEFINE_SPINLOCK(time_interpolator_lock);

static inline u64 time_interpolator_get_cycles(unsigned int src)
{
	unsigned long (*x)(void);

	switch (src)
	{
		case TIME_SOURCE_FUNCTION:
			x = time_interpolator->addr;
			return x();

		case TIME_SOURCE_MMIO64	:
			return readq_relaxed((void __iomem *)time_interpolator->addr);

		case TIME_SOURCE_MMIO32	:
			return readl_relaxed((void __iomem *)time_interpolator->addr);

		default: return get_cycles();
	}
}

static inline u64 time_interpolator_get_counter(int writelock)
{
	unsigned int src = time_interpolator->source;

	if (time_interpolator->jitter)
	{
		u64 lcycle;
		u64 now;

		do {
			lcycle = time_interpolator->last_cycle;
			now = time_interpolator_get_cycles(src);
			if (lcycle && time_after(lcycle, now))
				return lcycle;

			/* When holding the xtime write lock, there's no need
			 * to add the overhead of the cmpxchg.  Readers are
			 * force to retry until the write lock is released.
			 */
			if (writelock) {
				time_interpolator->last_cycle = now;
				return now;
			}
			/* Keep track of the last timer value returned. The use of cmpxchg here
			 * will cause contention in an SMP environment.
			 */
		} while (unlikely(cmpxchg(&time_interpolator->last_cycle, lcycle, now) != lcycle));
		return now;
	}
	else
		return time_interpolator_get_cycles(src);
}

void time_interpolator_reset(void)
{
	time_interpolator->offset = 0;
	time_interpolator->last_counter = time_interpolator_get_counter(1);
}

#define GET_TI_NSECS(count,i) (((((count) - i->last_counter) & (i)->mask) * (i)->nsec_per_cyc) >> (i)->shift)

unsigned long time_interpolator_get_offset(void)
{
	/* If we do not have a time interpolator set up then just return zero */
	if (!time_interpolator)
		return 0;

	return time_interpolator->offset +
		GET_TI_NSECS(time_interpolator_get_counter(0), time_interpolator);
}

#define INTERPOLATOR_ADJUST 65536
#define INTERPOLATOR_MAX_SKIP 10*INTERPOLATOR_ADJUST

static void time_interpolator_update(long delta_nsec)
{
	u64 counter;
	unsigned long offset;

	/* If there is no time interpolator set up then do nothing */
	if (!time_interpolator)
		return;

	/*
	 * The interpolator compensates for late ticks by accumulating the late
	 * time in time_interpolator->offset. A tick earlier than expected will
	 * lead to a reset of the offset and a corresponding jump of the clock
	 * forward. Again this only works if the interpolator clock is running
	 * slightly slower than the regular clock and the tuning logic insures
	 * that.
	 */

	counter = time_interpolator_get_counter(1);
	offset = time_interpolator->offset +
			GET_TI_NSECS(counter, time_interpolator);

	if (delta_nsec < 0 || (unsigned long) delta_nsec < offset)
		time_interpolator->offset = offset - delta_nsec;
	else {
		time_interpolator->skips++;
		time_interpolator->ns_skipped += delta_nsec - offset;
		time_interpolator->offset = 0;
	}
	time_interpolator->last_counter = counter;

	/* Tuning logic for time interpolator invoked every minute or so.
	 * Decrease interpolator clock speed if no skips occurred and an offset is carried.
	 * Increase interpolator clock speed if we skip too much time.
	 */
	if (jiffies % INTERPOLATOR_ADJUST == 0)
	{
		if (time_interpolator->skips == 0 && time_interpolator->offset > tick_nsec)
			time_interpolator->nsec_per_cyc--;
		if (time_interpolator->ns_skipped > INTERPOLATOR_MAX_SKIP && time_interpolator->offset == 0)
			time_interpolator->nsec_per_cyc++;
		time_interpolator->skips = 0;
		time_interpolator->ns_skipped = 0;
	}
}

static inline int
is_better_time_interpolator(struct time_interpolator *new)
{
	if (!time_interpolator)
		return 1;
	return new->frequency > 2*time_interpolator->frequency ||
	    (unsigned long)new->drift < (unsigned long)time_interpolator->drift;
}

void
register_time_interpolator(struct time_interpolator *ti)
{
	unsigned long flags;

	/* Sanity check */
	BUG_ON(ti->frequency == 0 || ti->mask == 0);

	ti->nsec_per_cyc = ((u64)NSEC_PER_SEC << ti->shift) / ti->frequency;
	spin_lock(&time_interpolator_lock);
	write_seqlock_irqsave(&xtime_lock, flags);
	if (is_better_time_interpolator(ti)) {
		time_interpolator = ti;
		time_interpolator_reset();
	}
	write_sequnlock_irqrestore(&xtime_lock, flags);

	ti->next = time_interpolator_list;
	time_interpolator_list = ti;
	spin_unlock(&time_interpolator_lock);
}

void
unregister_time_interpolator(struct time_interpolator *ti)
{
	struct time_interpolator *curr, **prev;
	unsigned long flags;

	spin_lock(&time_interpolator_lock);
	prev = &time_interpolator_list;
	for (curr = *prev; curr; curr = curr->next) {
		if (curr == ti) {
			*prev = curr->next;
			break;
		}
		prev = &curr->next;
	}

	write_seqlock_irqsave(&xtime_lock, flags);
	if (ti == time_interpolator) {
		/* we lost the best time-interpolator: */
		time_interpolator = NULL;
		/* find the next-best interpolator */
		for (curr = time_interpolator_list; curr; curr = curr->next)
			if (is_better_time_interpolator(curr))
				time_interpolator = curr;
		time_interpolator_reset();
	}
	write_sequnlock_irqrestore(&xtime_lock, flags);
	spin_unlock(&time_interpolator_lock);
}
#endif /* CONFIG_TIME_INTERPOLATION */

/**
 * msleep - sleep safely even with waitqueue interruptions
 * @msecs: Time in milliseconds to sleep for
 */
void msleep(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs) + 1;

	while (timeout)
		timeout = schedule_timeout_uninterruptible(timeout);
}

EXPORT_SYMBOL(msleep);

/**
 * msleep_interruptible - sleep waiting for signals
 * @msecs: Time in milliseconds to sleep for
 */
unsigned long msleep_interruptible(unsigned int msecs)
{
	unsigned long timeout = msecs_to_jiffies(msecs) + 1;

	while (timeout && !signal_pending(current))
		timeout = schedule_timeout_interruptible(timeout);
	return jiffies_to_msecs(timeout);
}

EXPORT_SYMBOL(msleep_interruptible);
