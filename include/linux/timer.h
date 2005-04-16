#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

#include <linux/config.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>

struct tvec_t_base_s;

struct timer_list {
	struct list_head entry;
	unsigned long expires;

	spinlock_t lock;
	unsigned long magic;

	void (*function)(unsigned long);
	unsigned long data;

	struct tvec_t_base_s *base;
};

#define TIMER_MAGIC	0x4b87ad6e

#define TIMER_INITIALIZER(_function, _expires, _data) {		\
		.function = (_function),			\
		.expires = (_expires),				\
		.data = (_data),				\
		.base = NULL,					\
		.magic = TIMER_MAGIC,				\
		.lock = SPIN_LOCK_UNLOCKED,			\
	}

/***
 * init_timer - initialize a timer.
 * @timer: the timer to be initialized
 *
 * init_timer() must be done to a timer prior calling *any* of the
 * other timer functions.
 */
static inline void init_timer(struct timer_list * timer)
{
	timer->base = NULL;
	timer->magic = TIMER_MAGIC;
	spin_lock_init(&timer->lock);
}

/***
 * timer_pending - is a timer pending?
 * @timer: the timer in question
 *
 * timer_pending will tell whether a given timer is currently pending,
 * or not. Callers must ensure serialization wrt. other operations done
 * to this timer, eg. interrupt contexts, or other CPUs on SMP.
 *
 * return value: 1 if the timer is pending, 0 if not.
 */
static inline int timer_pending(const struct timer_list * timer)
{
	return timer->base != NULL;
}

extern void add_timer_on(struct timer_list *timer, int cpu);
extern int del_timer(struct timer_list * timer);
extern int __mod_timer(struct timer_list *timer, unsigned long expires);
extern int mod_timer(struct timer_list *timer, unsigned long expires);

extern unsigned long next_timer_interrupt(void);

/***
 * add_timer - start a timer
 * @timer: the timer to be added
 *
 * The kernel will do a ->function(->data) callback from the
 * timer interrupt at the ->expired point in the future. The
 * current time is 'jiffies'.
 *
 * The timer's ->expired, ->function (and if the handler uses it, ->data)
 * fields must be set prior calling this function.
 *
 * Timers with an ->expired field in the past will be executed in the next
 * timer tick.
 */
static inline void add_timer(struct timer_list * timer)
{
	__mod_timer(timer, timer->expires);
}

#ifdef CONFIG_SMP
  extern int del_timer_sync(struct timer_list *timer);
  extern int del_singleshot_timer_sync(struct timer_list *timer);
#else
# define del_timer_sync(t) del_timer(t)
# define del_singleshot_timer_sync(t) del_timer(t)
#endif

extern void init_timers(void);
extern void run_local_timers(void);
extern void it_real_fn(unsigned long);

#endif
