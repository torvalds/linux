/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KTHREAD_H
#define _LINUX_KTHREAD_H
/* Simple interface for creating and stopping kernel threads without mess. */
#include <linux/err.h>
#include <linux/sched.h>

struct mm_struct;

__printf(4, 5)
struct task_struct *kthread_create_on_node(int (*threadfn)(void *data),
					   void *data,
					   int node,
					   const char namefmt[], ...);

/**
 * kthread_create - create a kthread on the current node
 * @threadfn: the function to run in the thread
 * @data: data pointer for @threadfn()
 * @namefmt: printf-style format string for the thread name
 * @arg: arguments for @namefmt.
 *
 * This macro will create a kthread on the current node, leaving it in
 * the stopped state.  This is just a helper for kthread_create_on_node();
 * see the documentation there for more details.
 */
#define kthread_create(threadfn, data, namefmt, arg...) \
	kthread_create_on_node(threadfn, data, NUMA_NO_NODE, namefmt, ##arg)


struct task_struct *kthread_create_on_cpu(int (*threadfn)(void *data),
					  void *data,
					  unsigned int cpu,
					  const char *namefmt);

void get_kthread_comm(char *buf, size_t buf_size, struct task_struct *tsk);
bool set_kthread_struct(struct task_struct *p);

void kthread_set_per_cpu(struct task_struct *k, int cpu);
bool kthread_is_per_cpu(struct task_struct *k);

/**
 * kthread_run - create and wake a thread.
 * @threadfn: the function to run until signal_pending(current).
 * @data: data ptr for @threadfn.
 * @namefmt: printf-style name for the thread.
 *
 * Description: Convenient wrapper for kthread_create() followed by
 * wake_up_process().  Returns the kthread or ERR_PTR(-ENOMEM).
 */
#define kthread_run(threadfn, data, namefmt, ...)			   \
({									   \
	struct task_struct *__k						   \
		= kthread_create(threadfn, data, namefmt, ## __VA_ARGS__); \
	if (!IS_ERR(__k))						   \
		wake_up_process(__k);					   \
	__k;								   \
})

/**
 * kthread_run_on_cpu - create and wake a cpu bound thread.
 * @threadfn: the function to run until signal_pending(current).
 * @data: data ptr for @threadfn.
 * @cpu: The cpu on which the thread should be bound,
 * @namefmt: printf-style name for the thread. Format is restricted
 *	     to "name.*%u". Code fills in cpu number.
 *
 * Description: Convenient wrapper for kthread_create_on_cpu()
 * followed by wake_up_process().  Returns the kthread or
 * ERR_PTR(-ENOMEM).
 */
static inline struct task_struct *
kthread_run_on_cpu(int (*threadfn)(void *data), void *data,
			unsigned int cpu, const char *namefmt)
{
	struct task_struct *p;

	p = kthread_create_on_cpu(threadfn, data, cpu, namefmt);
	if (!IS_ERR(p))
		wake_up_process(p);

	return p;
}

void free_kthread_struct(struct task_struct *k);
void kthread_bind(struct task_struct *k, unsigned int cpu);
void kthread_bind_mask(struct task_struct *k, const struct cpumask *mask);
int kthread_stop(struct task_struct *k);
bool kthread_should_stop(void);
bool kthread_should_park(void);
bool kthread_should_stop_or_park(void);
bool kthread_freezable_should_stop(bool *was_frozen);
void *kthread_func(struct task_struct *k);
void *kthread_data(struct task_struct *k);
void *kthread_probe_data(struct task_struct *k);
int kthread_park(struct task_struct *k);
void kthread_unpark(struct task_struct *k);
void kthread_parkme(void);
void kthread_exit(long result) __noreturn;
void kthread_complete_and_exit(struct completion *, long) __noreturn;

int kthreadd(void *unused);
extern struct task_struct *kthreadd_task;
extern int tsk_fork_get_node(struct task_struct *tsk);

/*
 * Simple work processor based on kthread.
 *
 * This provides easier way to make use of kthreads.  A kthread_work
 * can be queued and flushed using queue/kthread_flush_work()
 * respectively.  Queued kthread_works are processed by a kthread
 * running kthread_worker_fn().
 */
struct kthread_work;
typedef void (*kthread_work_func_t)(struct kthread_work *work);
void kthread_delayed_work_timer_fn(struct timer_list *t);

enum {
	KTW_FREEZABLE		= 1 << 0,	/* freeze during suspend */
};

struct kthread_worker {
	unsigned int		flags;
	raw_spinlock_t		lock;
	struct list_head	work_list;
	struct list_head	delayed_work_list;
	struct task_struct	*task;
	struct kthread_work	*current_work;
};

struct kthread_work {
	struct list_head	node;
	kthread_work_func_t	func;
	struct kthread_worker	*worker;
	/* Number of canceling calls that are running at the moment. */
	int			canceling;
};

struct kthread_delayed_work {
	struct kthread_work work;
	struct timer_list timer;
};

#define KTHREAD_WORK_INIT(work, fn)	{				\
	.node = LIST_HEAD_INIT((work).node),				\
	.func = (fn),							\
	}

#define KTHREAD_DELAYED_WORK_INIT(dwork, fn) {				\
	.work = KTHREAD_WORK_INIT((dwork).work, (fn)),			\
	.timer = __TIMER_INITIALIZER(kthread_delayed_work_timer_fn,\
				     TIMER_IRQSAFE),			\
	}

#define DEFINE_KTHREAD_WORK(work, fn)					\
	struct kthread_work work = KTHREAD_WORK_INIT(work, fn)

#define DEFINE_KTHREAD_DELAYED_WORK(dwork, fn)				\
	struct kthread_delayed_work dwork =				\
		KTHREAD_DELAYED_WORK_INIT(dwork, fn)

extern void __kthread_init_worker(struct kthread_worker *worker,
			const char *name, struct lock_class_key *key);

#define kthread_init_worker(worker)					\
	do {								\
		static struct lock_class_key __key;			\
		__kthread_init_worker((worker), "("#worker")->lock", &__key); \
	} while (0)

#define kthread_init_work(work, fn)					\
	do {								\
		memset((work), 0, sizeof(struct kthread_work));		\
		INIT_LIST_HEAD(&(work)->node);				\
		(work)->func = (fn);					\
	} while (0)

#define kthread_init_delayed_work(dwork, fn)				\
	do {								\
		kthread_init_work(&(dwork)->work, (fn));		\
		timer_setup(&(dwork)->timer,				\
			     kthread_delayed_work_timer_fn,		\
			     TIMER_IRQSAFE);				\
	} while (0)

int kthread_worker_fn(void *worker_ptr);

__printf(2, 3)
struct kthread_worker *
kthread_create_worker(unsigned int flags, const char namefmt[], ...);

__printf(3, 4) struct kthread_worker *
kthread_create_worker_on_cpu(int cpu, unsigned int flags,
			     const char namefmt[], ...);

bool kthread_queue_work(struct kthread_worker *worker,
			struct kthread_work *work);

bool kthread_queue_delayed_work(struct kthread_worker *worker,
				struct kthread_delayed_work *dwork,
				unsigned long delay);

bool kthread_mod_delayed_work(struct kthread_worker *worker,
			      struct kthread_delayed_work *dwork,
			      unsigned long delay);

void kthread_flush_work(struct kthread_work *work);
void kthread_flush_worker(struct kthread_worker *worker);

bool kthread_cancel_work_sync(struct kthread_work *work);
bool kthread_cancel_delayed_work_sync(struct kthread_delayed_work *work);

void kthread_destroy_worker(struct kthread_worker *worker);

void kthread_use_mm(struct mm_struct *mm);
void kthread_unuse_mm(struct mm_struct *mm);

struct cgroup_subsys_state;

#ifdef CONFIG_BLK_CGROUP
void kthread_associate_blkcg(struct cgroup_subsys_state *css);
struct cgroup_subsys_state *kthread_blkcg(void);
#else
static inline void kthread_associate_blkcg(struct cgroup_subsys_state *css) { }
#endif
#endif /* _LINUX_KTHREAD_H */
