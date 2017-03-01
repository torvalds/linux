#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

#include <linux/list.h>
#include <linux/ktime.h>
#include <linux/stddef.h>
#include <linux/debugobjects.h>
#include <linux/stringify.h>

struct tvec_base;

struct timer_list {
	/*
	 * All fields that change during normal runtime grouped to the
	 * same cacheline
	 */
	struct hlist_node	entry;
	unsigned long		expires;
	void			(*function)(unsigned long);
	unsigned long		data;
	u32			flags;

#ifdef CONFIG_LOCKDEP
	struct lockdep_map	lockdep_map;
#endif
};

#ifdef CONFIG_LOCKDEP
/*
 * NB: because we have to copy the lockdep_map, setting the lockdep_map key
 * (second argument) here is required, otherwise it could be initialised to
 * the copy of the lockdep_map later! We use the pointer to and the string
 * "<file>:<line>" as the key resp. the name of the lockdep_map.
 */
#define __TIMER_LOCKDEP_MAP_INITIALIZER(_kn)				\
	.lockdep_map = STATIC_LOCKDEP_MAP_INIT(_kn, &_kn),
#else
#define __TIMER_LOCKDEP_MAP_INITIALIZER(_kn)
#endif

/*
 * A deferrable timer will work normally when the system is busy, but
 * will not cause a CPU to come out of idle just to service it; instead,
 * the timer will be serviced when the CPU eventually wakes up with a
 * subsequent non-deferrable timer.
 *
 * An irqsafe timer is executed with IRQ disabled and it's safe to wait for
 * the completion of the running instance from IRQ handlers, for example,
 * by calling del_timer_sync().
 *
 * Note: The irq disabled callback execution is a special case for
 * workqueue locking issues. It's not meant for executing random crap
 * with interrupts disabled. Abuse is monitored!
 */
#define TIMER_CPUMASK		0x0003FFFF
#define TIMER_MIGRATING		0x00040000
#define TIMER_BASEMASK		(TIMER_CPUMASK | TIMER_MIGRATING)
#define TIMER_DEFERRABLE	0x00080000
#define TIMER_PINNED		0x00100000
#define TIMER_IRQSAFE		0x00200000
#define TIMER_ARRAYSHIFT	22
#define TIMER_ARRAYMASK		0xFFC00000

#define TIMER_TRACE_FLAGMASK	(TIMER_MIGRATING | TIMER_DEFERRABLE | TIMER_PINNED | TIMER_IRQSAFE)

#define __TIMER_INITIALIZER(_function, _expires, _data, _flags) { \
		.entry = { .next = TIMER_ENTRY_STATIC },	\
		.function = (_function),			\
		.expires = (_expires),				\
		.data = (_data),				\
		.flags = (_flags),				\
		__TIMER_LOCKDEP_MAP_INITIALIZER(		\
			__FILE__ ":" __stringify(__LINE__))	\
	}

#define TIMER_INITIALIZER(_function, _expires, _data)		\
	__TIMER_INITIALIZER((_function), (_expires), (_data), 0)

#define TIMER_PINNED_INITIALIZER(_function, _expires, _data)	\
	__TIMER_INITIALIZER((_function), (_expires), (_data), TIMER_PINNED)

#define TIMER_DEFERRED_INITIALIZER(_function, _expires, _data)	\
	__TIMER_INITIALIZER((_function), (_expires), (_data), TIMER_DEFERRABLE)

#define TIMER_PINNED_DEFERRED_INITIALIZER(_function, _expires, _data)	\
	__TIMER_INITIALIZER((_function), (_expires), (_data), TIMER_DEFERRABLE | TIMER_PINNED)

#define DEFINE_TIMER(_name, _function, _expires, _data)		\
	struct timer_list _name =				\
		TIMER_INITIALIZER(_function, _expires, _data)

void init_timer_key(struct timer_list *timer, unsigned int flags,
		    const char *name, struct lock_class_key *key);

#ifdef CONFIG_DEBUG_OBJECTS_TIMERS
extern void init_timer_on_stack_key(struct timer_list *timer,
				    unsigned int flags, const char *name,
				    struct lock_class_key *key);
extern void destroy_timer_on_stack(struct timer_list *timer);
#else
static inline void destroy_timer_on_stack(struct timer_list *timer) { }
static inline void init_timer_on_stack_key(struct timer_list *timer,
					   unsigned int flags, const char *name,
					   struct lock_class_key *key)
{
	init_timer_key(timer, flags, name, key);
}
#endif

#ifdef CONFIG_LOCKDEP
#define __init_timer(_timer, _flags)					\
	do {								\
		static struct lock_class_key __key;			\
		init_timer_key((_timer), (_flags), #_timer, &__key);	\
	} while (0)

#define __init_timer_on_stack(_timer, _flags)				\
	do {								\
		static struct lock_class_key __key;			\
		init_timer_on_stack_key((_timer), (_flags), #_timer, &__key); \
	} while (0)
#else
#define __init_timer(_timer, _flags)					\
	init_timer_key((_timer), (_flags), NULL, NULL)
#define __init_timer_on_stack(_timer, _flags)				\
	init_timer_on_stack_key((_timer), (_flags), NULL, NULL)
#endif

#define init_timer(timer)						\
	__init_timer((timer), 0)
#define init_timer_pinned(timer)					\
	__init_timer((timer), TIMER_PINNED)
#define init_timer_deferrable(timer)					\
	__init_timer((timer), TIMER_DEFERRABLE)
#define init_timer_pinned_deferrable(timer)				\
	__init_timer((timer), TIMER_DEFERRABLE | TIMER_PINNED)
#define init_timer_on_stack(timer)					\
	__init_timer_on_stack((timer), 0)

#define __setup_timer(_timer, _fn, _data, _flags)			\
	do {								\
		__init_timer((_timer), (_flags));			\
		(_timer)->function = (_fn);				\
		(_timer)->data = (_data);				\
	} while (0)

#define __setup_timer_on_stack(_timer, _fn, _data, _flags)		\
	do {								\
		__init_timer_on_stack((_timer), (_flags));		\
		(_timer)->function = (_fn);				\
		(_timer)->data = (_data);				\
	} while (0)

#define setup_timer(timer, fn, data)					\
	__setup_timer((timer), (fn), (data), 0)
#define setup_pinned_timer(timer, fn, data)				\
	__setup_timer((timer), (fn), (data), TIMER_PINNED)
#define setup_deferrable_timer(timer, fn, data)				\
	__setup_timer((timer), (fn), (data), TIMER_DEFERRABLE)
#define setup_pinned_deferrable_timer(timer, fn, data)			\
	__setup_timer((timer), (fn), (data), TIMER_DEFERRABLE | TIMER_PINNED)
#define setup_timer_on_stack(timer, fn, data)				\
	__setup_timer_on_stack((timer), (fn), (data), 0)
#define setup_pinned_timer_on_stack(timer, fn, data)			\
	__setup_timer_on_stack((timer), (fn), (data), TIMER_PINNED)
#define setup_deferrable_timer_on_stack(timer, fn, data)		\
	__setup_timer_on_stack((timer), (fn), (data), TIMER_DEFERRABLE)
#define setup_pinned_deferrable_timer_on_stack(timer, fn, data)		\
	__setup_timer_on_stack((timer), (fn), (data), TIMER_DEFERRABLE | TIMER_PINNED)

/**
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
	return timer->entry.pprev != NULL;
}

extern void add_timer_on(struct timer_list *timer, int cpu);
extern int del_timer(struct timer_list * timer);
extern int mod_timer(struct timer_list *timer, unsigned long expires);
extern int mod_timer_pending(struct timer_list *timer, unsigned long expires);

/*
 * The jiffies value which is added to now, when there is no timer
 * in the timer wheel:
 */
#define NEXT_TIMER_MAX_DELTA	((1UL << 30) - 1)

extern void add_timer(struct timer_list *timer);

extern int try_to_del_timer_sync(struct timer_list *timer);

#ifdef CONFIG_SMP
  extern int del_timer_sync(struct timer_list *timer);
#else
# define del_timer_sync(t)		del_timer(t)
#endif

#define del_singleshot_timer_sync(t) del_timer_sync(t)

extern void init_timers(void);
extern void run_local_timers(void);
struct hrtimer;
extern enum hrtimer_restart it_real_fn(struct hrtimer *);

#if defined(CONFIG_SMP) && defined(CONFIG_NO_HZ_COMMON)
#include <linux/sysctl.h>

extern unsigned int sysctl_timer_migration;
int timer_migration_handler(struct ctl_table *table, int write,
			    void __user *buffer, size_t *lenp,
			    loff_t *ppos);
#endif

unsigned long __round_jiffies(unsigned long j, int cpu);
unsigned long __round_jiffies_relative(unsigned long j, int cpu);
unsigned long round_jiffies(unsigned long j);
unsigned long round_jiffies_relative(unsigned long j);

unsigned long __round_jiffies_up(unsigned long j, int cpu);
unsigned long __round_jiffies_up_relative(unsigned long j, int cpu);
unsigned long round_jiffies_up(unsigned long j);
unsigned long round_jiffies_up_relative(unsigned long j);

#ifdef CONFIG_HOTPLUG_CPU
int timers_dead_cpu(unsigned int cpu);
#else
#define timers_dead_cpu NULL
#endif

#endif
