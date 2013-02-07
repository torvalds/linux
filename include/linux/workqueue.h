/*
 * workqueue.h --- work queue handling for Linux.
 */

#ifndef _LINUX_WORKQUEUE_H
#define _LINUX_WORKQUEUE_H

#include <linux/timer.h>
#include <linux/linkage.h>
#include <linux/bitops.h>
#include <linux/lockdep.h>
#include <linux/threads.h>
#include <linux/atomic.h>

struct workqueue_struct;

struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);
void delayed_work_timer_fn(unsigned long __data);

/*
 * The first word is the work queue pointer and the flags rolled into
 * one
 */
#define work_data_bits(work) ((unsigned long *)(&(work)->data))

enum {
	WORK_STRUCT_PENDING_BIT	= 0,	/* work item is pending execution */
	WORK_STRUCT_DELAYED_BIT	= 1,	/* work item is delayed */
	WORK_STRUCT_CWQ_BIT	= 2,	/* data points to cwq */
	WORK_STRUCT_LINKED_BIT	= 3,	/* next work is linked to this one */
#ifdef CONFIG_DEBUG_OBJECTS_WORK
	WORK_STRUCT_STATIC_BIT	= 4,	/* static initializer (debugobjects) */
	WORK_STRUCT_COLOR_SHIFT	= 5,	/* color for workqueue flushing */
#else
	WORK_STRUCT_COLOR_SHIFT	= 4,	/* color for workqueue flushing */
#endif

	WORK_STRUCT_COLOR_BITS	= 4,

	WORK_STRUCT_PENDING	= 1 << WORK_STRUCT_PENDING_BIT,
	WORK_STRUCT_DELAYED	= 1 << WORK_STRUCT_DELAYED_BIT,
	WORK_STRUCT_CWQ		= 1 << WORK_STRUCT_CWQ_BIT,
	WORK_STRUCT_LINKED	= 1 << WORK_STRUCT_LINKED_BIT,
#ifdef CONFIG_DEBUG_OBJECTS_WORK
	WORK_STRUCT_STATIC	= 1 << WORK_STRUCT_STATIC_BIT,
#else
	WORK_STRUCT_STATIC	= 0,
#endif

	/*
	 * The last color is no color used for works which don't
	 * participate in workqueue flushing.
	 */
	WORK_NR_COLORS		= (1 << WORK_STRUCT_COLOR_BITS) - 1,
	WORK_NO_COLOR		= WORK_NR_COLORS,

	/* special cpu IDs */
	WORK_CPU_UNBOUND	= NR_CPUS,
	WORK_CPU_END		= NR_CPUS + 1,

	/*
	 * Reserve 7 bits off of cwq pointer w/ debugobjects turned
	 * off.  This makes cwqs aligned to 256 bytes and allows 15
	 * workqueue flush colors.
	 */
	WORK_STRUCT_FLAG_BITS	= WORK_STRUCT_COLOR_SHIFT +
				  WORK_STRUCT_COLOR_BITS,

	/* data contains off-queue information when !WORK_STRUCT_CWQ */
	WORK_OFFQ_FLAG_BASE	= WORK_STRUCT_FLAG_BITS,

	WORK_OFFQ_CANCELING	= (1 << WORK_OFFQ_FLAG_BASE),

	/*
	 * When a work item is off queue, its high bits point to the last
	 * pool it was on.  Cap at 31 bits and use the highest number to
	 * indicate that no pool is associated.
	 */
	WORK_OFFQ_FLAG_BITS	= 1,
	WORK_OFFQ_POOL_SHIFT	= WORK_OFFQ_FLAG_BASE + WORK_OFFQ_FLAG_BITS,
	WORK_OFFQ_LEFT		= BITS_PER_LONG - WORK_OFFQ_POOL_SHIFT,
	WORK_OFFQ_POOL_BITS	= WORK_OFFQ_LEFT <= 31 ? WORK_OFFQ_LEFT : 31,
	WORK_OFFQ_POOL_NONE	= (1LU << WORK_OFFQ_POOL_BITS) - 1,

	/* convenience constants */
	WORK_STRUCT_FLAG_MASK	= (1UL << WORK_STRUCT_FLAG_BITS) - 1,
	WORK_STRUCT_WQ_DATA_MASK = ~WORK_STRUCT_FLAG_MASK,
	WORK_STRUCT_NO_POOL	= (unsigned long)WORK_OFFQ_POOL_NONE << WORK_OFFQ_POOL_SHIFT,

	/* bit mask for work_busy() return values */
	WORK_BUSY_PENDING	= 1 << 0,
	WORK_BUSY_RUNNING	= 1 << 1,
};

struct work_struct {
	atomic_long_t data;
	struct list_head entry;
	work_func_t func;
#ifdef CONFIG_LOCKDEP
	struct lockdep_map lockdep_map;
#endif
};

#define WORK_DATA_INIT()	ATOMIC_LONG_INIT(WORK_STRUCT_NO_POOL)
#define WORK_DATA_STATIC_INIT()	\
	ATOMIC_LONG_INIT(WORK_STRUCT_NO_POOL | WORK_STRUCT_STATIC)

struct delayed_work {
	struct work_struct work;
	struct timer_list timer;

	/* target workqueue and CPU ->timer uses to queue ->work */
	struct workqueue_struct *wq;
	int cpu;
};

static inline struct delayed_work *to_delayed_work(struct work_struct *work)
{
	return container_of(work, struct delayed_work, work);
}

struct execute_work {
	struct work_struct work;
};

#ifdef CONFIG_LOCKDEP
/*
 * NB: because we have to copy the lockdep_map, setting _key
 * here is required, otherwise it could get initialised to the
 * copy of the lockdep_map!
 */
#define __WORK_INIT_LOCKDEP_MAP(n, k) \
	.lockdep_map = STATIC_LOCKDEP_MAP_INIT(n, k),
#else
#define __WORK_INIT_LOCKDEP_MAP(n, k)
#endif

#define __WORK_INITIALIZER(n, f) {					\
	.data = WORK_DATA_STATIC_INIT(),				\
	.entry	= { &(n).entry, &(n).entry },				\
	.func = (f),							\
	__WORK_INIT_LOCKDEP_MAP(#n, &(n))				\
	}

#define __DELAYED_WORK_INITIALIZER(n, f, tflags) {			\
	.work = __WORK_INITIALIZER((n).work, (f)),			\
	.timer = __TIMER_INITIALIZER(delayed_work_timer_fn,		\
				     0, (unsigned long)&(n),		\
				     (tflags) | TIMER_IRQSAFE),		\
	}

#define DECLARE_WORK(n, f)						\
	struct work_struct n = __WORK_INITIALIZER(n, f)

#define DECLARE_DELAYED_WORK(n, f)					\
	struct delayed_work n = __DELAYED_WORK_INITIALIZER(n, f, 0)

#define DECLARE_DEFERRABLE_WORK(n, f)					\
	struct delayed_work n = __DELAYED_WORK_INITIALIZER(n, f, TIMER_DEFERRABLE)

/*
 * initialize a work item's function pointer
 */
#define PREPARE_WORK(_work, _func)					\
	do {								\
		(_work)->func = (_func);				\
	} while (0)

#define PREPARE_DELAYED_WORK(_work, _func)				\
	PREPARE_WORK(&(_work)->work, (_func))

#ifdef CONFIG_DEBUG_OBJECTS_WORK
extern void __init_work(struct work_struct *work, int onstack);
extern void destroy_work_on_stack(struct work_struct *work);
static inline unsigned int work_static(struct work_struct *work)
{
	return *work_data_bits(work) & WORK_STRUCT_STATIC;
}
#else
static inline void __init_work(struct work_struct *work, int onstack) { }
static inline void destroy_work_on_stack(struct work_struct *work) { }
static inline unsigned int work_static(struct work_struct *work) { return 0; }
#endif

/*
 * initialize all of a work item in one go
 *
 * NOTE! No point in using "atomic_long_set()": using a direct
 * assignment of the work data initializer allows the compiler
 * to generate better code.
 */
#ifdef CONFIG_LOCKDEP
#define __INIT_WORK(_work, _func, _onstack)				\
	do {								\
		static struct lock_class_key __key;			\
									\
		__init_work((_work), _onstack);				\
		(_work)->data = (atomic_long_t) WORK_DATA_INIT();	\
		lockdep_init_map(&(_work)->lockdep_map, #_work, &__key, 0); \
		INIT_LIST_HEAD(&(_work)->entry);			\
		PREPARE_WORK((_work), (_func));				\
	} while (0)
#else
#define __INIT_WORK(_work, _func, _onstack)				\
	do {								\
		__init_work((_work), _onstack);				\
		(_work)->data = (atomic_long_t) WORK_DATA_INIT();	\
		INIT_LIST_HEAD(&(_work)->entry);			\
		PREPARE_WORK((_work), (_func));				\
	} while (0)
#endif

#define INIT_WORK(_work, _func)						\
	do {								\
		__INIT_WORK((_work), (_func), 0);			\
	} while (0)

#define INIT_WORK_ONSTACK(_work, _func)					\
	do {								\
		__INIT_WORK((_work), (_func), 1);			\
	} while (0)

#define __INIT_DELAYED_WORK(_work, _func, _tflags)			\
	do {								\
		INIT_WORK(&(_work)->work, (_func));			\
		__setup_timer(&(_work)->timer, delayed_work_timer_fn,	\
			      (unsigned long)(_work),			\
			      (_tflags) | TIMER_IRQSAFE);		\
	} while (0)

#define __INIT_DELAYED_WORK_ONSTACK(_work, _func, _tflags)		\
	do {								\
		INIT_WORK_ONSTACK(&(_work)->work, (_func));		\
		__setup_timer_on_stack(&(_work)->timer,			\
				       delayed_work_timer_fn,		\
				       (unsigned long)(_work),		\
				       (_tflags) | TIMER_IRQSAFE);	\
	} while (0)

#define INIT_DELAYED_WORK(_work, _func)					\
	__INIT_DELAYED_WORK(_work, _func, 0)

#define INIT_DELAYED_WORK_ONSTACK(_work, _func)				\
	__INIT_DELAYED_WORK_ONSTACK(_work, _func, 0)

#define INIT_DEFERRABLE_WORK(_work, _func)				\
	__INIT_DELAYED_WORK(_work, _func, TIMER_DEFERRABLE)

#define INIT_DEFERRABLE_WORK_ONSTACK(_work, _func)			\
	__INIT_DELAYED_WORK_ONSTACK(_work, _func, TIMER_DEFERRABLE)

/**
 * work_pending - Find out whether a work item is currently pending
 * @work: The work item in question
 */
#define work_pending(work) \
	test_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))

/**
 * delayed_work_pending - Find out whether a delayable work item is currently
 * pending
 * @work: The work item in question
 */
#define delayed_work_pending(w) \
	work_pending(&(w)->work)

/**
 * work_clear_pending - for internal use only, mark a work item as not pending
 * @work: The work item in question
 */
#define work_clear_pending(work) \
	clear_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(work))

/*
 * Workqueue flags and constants.  For details, please refer to
 * Documentation/workqueue.txt.
 */
enum {
	WQ_NON_REENTRANT	= 1 << 0, /* guarantee non-reentrance */
	WQ_UNBOUND		= 1 << 1, /* not bound to any cpu */
	WQ_FREEZABLE		= 1 << 2, /* freeze during suspend */
	WQ_MEM_RECLAIM		= 1 << 3, /* may be used for memory reclaim */
	WQ_HIGHPRI		= 1 << 4, /* high priority */
	WQ_CPU_INTENSIVE	= 1 << 5, /* cpu instensive workqueue */

	WQ_DRAINING		= 1 << 6, /* internal: workqueue is draining */
	WQ_RESCUER		= 1 << 7, /* internal: workqueue has rescuer */

	WQ_MAX_ACTIVE		= 512,	  /* I like 512, better ideas? */
	WQ_MAX_UNBOUND_PER_CPU	= 4,	  /* 4 * #cpus for unbound wq */
	WQ_DFL_ACTIVE		= WQ_MAX_ACTIVE / 2,
};

/* unbound wq's aren't per-cpu, scale max_active according to #cpus */
#define WQ_UNBOUND_MAX_ACTIVE	\
	max_t(int, WQ_MAX_ACTIVE, num_possible_cpus() * WQ_MAX_UNBOUND_PER_CPU)

/*
 * System-wide workqueues which are always present.
 *
 * system_wq is the one used by schedule[_delayed]_work[_on]().
 * Multi-CPU multi-threaded.  There are users which expect relatively
 * short queue flush time.  Don't queue works which can run for too
 * long.
 *
 * system_long_wq is similar to system_wq but may host long running
 * works.  Queue flushing might take relatively long.
 *
 * system_unbound_wq is unbound workqueue.  Workers are not bound to
 * any specific CPU, not concurrency managed, and all queued works are
 * executed immediately as long as max_active limit is not reached and
 * resources are available.
 *
 * system_freezable_wq is equivalent to system_wq except that it's
 * freezable.
 */
extern struct workqueue_struct *system_wq;
extern struct workqueue_struct *system_long_wq;
extern struct workqueue_struct *system_unbound_wq;
extern struct workqueue_struct *system_freezable_wq;

static inline struct workqueue_struct * __deprecated __system_nrt_wq(void)
{
	return system_wq;
}

static inline struct workqueue_struct * __deprecated __system_nrt_freezable_wq(void)
{
	return system_freezable_wq;
}

/* equivlalent to system_wq and system_freezable_wq, deprecated */
#define system_nrt_wq			__system_nrt_wq()
#define system_nrt_freezable_wq		__system_nrt_freezable_wq()

extern struct workqueue_struct *
__alloc_workqueue_key(const char *fmt, unsigned int flags, int max_active,
	struct lock_class_key *key, const char *lock_name, ...) __printf(1, 6);

/**
 * alloc_workqueue - allocate a workqueue
 * @fmt: printf format for the name of the workqueue
 * @flags: WQ_* flags
 * @max_active: max in-flight work items, 0 for default
 * @args: args for @fmt
 *
 * Allocate a workqueue with the specified parameters.  For detailed
 * information on WQ_* flags, please refer to Documentation/workqueue.txt.
 *
 * The __lock_name macro dance is to guarantee that single lock_class_key
 * doesn't end up with different namesm, which isn't allowed by lockdep.
 *
 * RETURNS:
 * Pointer to the allocated workqueue on success, %NULL on failure.
 */
#ifdef CONFIG_LOCKDEP
#define alloc_workqueue(fmt, flags, max_active, args...)		\
({									\
	static struct lock_class_key __key;				\
	const char *__lock_name;					\
									\
	if (__builtin_constant_p(fmt))					\
		__lock_name = (fmt);					\
	else								\
		__lock_name = #fmt;					\
									\
	__alloc_workqueue_key((fmt), (flags), (max_active),		\
			      &__key, __lock_name, ##args);		\
})
#else
#define alloc_workqueue(fmt, flags, max_active, args...)		\
	__alloc_workqueue_key((fmt), (flags), (max_active),		\
			      NULL, NULL, ##args)
#endif

/**
 * alloc_ordered_workqueue - allocate an ordered workqueue
 * @fmt: printf format for the name of the workqueue
 * @flags: WQ_* flags (only WQ_FREEZABLE and WQ_MEM_RECLAIM are meaningful)
 * @args: args for @fmt
 *
 * Allocate an ordered workqueue.  An ordered workqueue executes at
 * most one work item at any given time in the queued order.  They are
 * implemented as unbound workqueues with @max_active of one.
 *
 * RETURNS:
 * Pointer to the allocated workqueue on success, %NULL on failure.
 */
#define alloc_ordered_workqueue(fmt, flags, args...)			\
	alloc_workqueue(fmt, WQ_UNBOUND | (flags), 1, ##args)

#define create_workqueue(name)						\
	alloc_workqueue((name), WQ_MEM_RECLAIM, 1)
#define create_freezable_workqueue(name)				\
	alloc_workqueue((name), WQ_FREEZABLE | WQ_UNBOUND | WQ_MEM_RECLAIM, 1)
#define create_singlethread_workqueue(name)				\
	alloc_workqueue((name), WQ_UNBOUND | WQ_MEM_RECLAIM, 1)

extern void destroy_workqueue(struct workqueue_struct *wq);

extern bool queue_work_on(int cpu, struct workqueue_struct *wq,
			struct work_struct *work);
extern bool queue_work(struct workqueue_struct *wq, struct work_struct *work);
extern bool queue_delayed_work_on(int cpu, struct workqueue_struct *wq,
			struct delayed_work *work, unsigned long delay);
extern bool queue_delayed_work(struct workqueue_struct *wq,
			struct delayed_work *work, unsigned long delay);
extern bool mod_delayed_work_on(int cpu, struct workqueue_struct *wq,
			struct delayed_work *dwork, unsigned long delay);
extern bool mod_delayed_work(struct workqueue_struct *wq,
			struct delayed_work *dwork, unsigned long delay);

extern void flush_workqueue(struct workqueue_struct *wq);
extern void drain_workqueue(struct workqueue_struct *wq);
extern void flush_scheduled_work(void);

extern bool schedule_work_on(int cpu, struct work_struct *work);
extern bool schedule_work(struct work_struct *work);
extern bool schedule_delayed_work_on(int cpu, struct delayed_work *work,
				     unsigned long delay);
extern bool schedule_delayed_work(struct delayed_work *work,
				  unsigned long delay);
extern int schedule_on_each_cpu(work_func_t func);
extern int keventd_up(void);

int execute_in_process_context(work_func_t fn, struct execute_work *);

extern bool flush_work(struct work_struct *work);
extern bool cancel_work_sync(struct work_struct *work);

extern bool flush_delayed_work(struct delayed_work *dwork);
extern bool cancel_delayed_work(struct delayed_work *dwork);
extern bool cancel_delayed_work_sync(struct delayed_work *dwork);

extern void workqueue_set_max_active(struct workqueue_struct *wq,
				     int max_active);
extern bool workqueue_congested(unsigned int cpu, struct workqueue_struct *wq);
extern unsigned int work_busy(struct work_struct *work);

/*
 * Like above, but uses del_timer() instead of del_timer_sync(). This means,
 * if it returns 0 the timer function may be running and the queueing is in
 * progress.
 */
static inline bool __deprecated __cancel_delayed_work(struct delayed_work *work)
{
	bool ret;

	ret = del_timer(&work->timer);
	if (ret)
		work_clear_pending(&work->work);
	return ret;
}

/* used to be different but now identical to flush_work(), deprecated */
static inline bool __deprecated flush_work_sync(struct work_struct *work)
{
	return flush_work(work);
}

/* used to be different but now identical to flush_delayed_work(), deprecated */
static inline bool __deprecated flush_delayed_work_sync(struct delayed_work *dwork)
{
	return flush_delayed_work(dwork);
}

#ifndef CONFIG_SMP
static inline long work_on_cpu(unsigned int cpu, long (*fn)(void *), void *arg)
{
	return fn(arg);
}
#else
long work_on_cpu(unsigned int cpu, long (*fn)(void *), void *arg);
#endif /* CONFIG_SMP */

#ifdef CONFIG_FREEZER
extern void freeze_workqueues_begin(void);
extern bool freeze_workqueues_busy(void);
extern void thaw_workqueues(void);
#endif /* CONFIG_FREEZER */

#endif
