#ifndef _LINUX_KTHREAD_H
#define _LINUX_KTHREAD_H
/* Simple interface for creating and stopping kernel threads without mess. */
#include <linux/err.h>
#include <linux/sched.h>

struct task_struct *kthread_create_on_node(int (*threadfn)(void *data),
					   void *data,
					   int node,
					   const char namefmt[], ...)
	__attribute__((format(printf, 4, 5)));

#define kthread_create(threadfn, data, namefmt, arg...) \
	kthread_create_on_node(threadfn, data, -1, namefmt, ##arg)


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

void kthread_bind(struct task_struct *k, unsigned int cpu);
int kthread_stop(struct task_struct *k);
int kthread_should_stop(void);
void *kthread_data(struct task_struct *k);

int kthreadd(void *unused);
extern struct task_struct *kthreadd_task;
extern int tsk_fork_get_node(struct task_struct *tsk);

/*
 * Simple work processor based on kthread.
 *
 * This provides easier way to make use of kthreads.  A kthread_work
 * can be queued and flushed using queue/flush_kthread_work()
 * respectively.  Queued kthread_works are processed by a kthread
 * running kthread_worker_fn().
 *
 * A kthread_work can't be freed while it is executing.
 */
struct kthread_work;
typedef void (*kthread_work_func_t)(struct kthread_work *work);

struct kthread_worker {
	spinlock_t		lock;
	struct list_head	work_list;
	struct task_struct	*task;
};

struct kthread_work {
	struct list_head	node;
	kthread_work_func_t	func;
	wait_queue_head_t	done;
	atomic_t		flushing;
	int			queue_seq;
	int			done_seq;
};

#define KTHREAD_WORKER_INIT(worker)	{				\
	.lock = __SPIN_LOCK_UNLOCKED((worker).lock),			\
	.work_list = LIST_HEAD_INIT((worker).work_list),		\
	}

#define KTHREAD_WORK_INIT(work, fn)	{				\
	.node = LIST_HEAD_INIT((work).node),				\
	.func = (fn),							\
	.done = __WAIT_QUEUE_HEAD_INITIALIZER((work).done),		\
	.flushing = ATOMIC_INIT(0),					\
	}

#define DEFINE_KTHREAD_WORKER(worker)					\
	struct kthread_worker worker = KTHREAD_WORKER_INIT(worker)

#define DEFINE_KTHREAD_WORK(work, fn)					\
	struct kthread_work work = KTHREAD_WORK_INIT(work, fn)

/*
 * kthread_worker.lock and kthread_work.done need their own lockdep class
 * keys if they are defined on stack with lockdep enabled.  Use the
 * following macros when defining them on stack.
 */
#ifdef CONFIG_LOCKDEP
# define KTHREAD_WORKER_INIT_ONSTACK(worker)				\
	({ init_kthread_worker(&worker); worker; })
# define DEFINE_KTHREAD_WORKER_ONSTACK(worker)				\
	struct kthread_worker worker = KTHREAD_WORKER_INIT_ONSTACK(worker)
# define KTHREAD_WORK_INIT_ONSTACK(work, fn)				\
	({ init_kthread_work((&work), fn); work; })
# define DEFINE_KTHREAD_WORK_ONSTACK(work, fn)				\
	struct kthread_work work = KTHREAD_WORK_INIT_ONSTACK(work, fn)
#else
# define DEFINE_KTHREAD_WORKER_ONSTACK(worker) DEFINE_KTHREAD_WORKER(worker)
# define DEFINE_KTHREAD_WORK_ONSTACK(work, fn) DEFINE_KTHREAD_WORK(work, fn)
#endif

extern void __init_kthread_worker(struct kthread_worker *worker,
			const char *name, struct lock_class_key *key);

#define init_kthread_worker(worker)					\
	do {								\
		static struct lock_class_key __key;			\
		__init_kthread_worker((worker), "("#worker")->lock", &__key); \
	} while (0)

#define init_kthread_work(work, fn)					\
	do {								\
		memset((work), 0, sizeof(struct kthread_work));		\
		INIT_LIST_HEAD(&(work)->node);				\
		(work)->func = (fn);					\
		init_waitqueue_head(&(work)->done);			\
	} while (0)

int kthread_worker_fn(void *worker_ptr);

bool queue_kthread_work(struct kthread_worker *worker,
			struct kthread_work *work);
void flush_kthread_work(struct kthread_work *work);
void flush_kthread_worker(struct kthread_worker *worker);

#endif /* _LINUX_KTHREAD_H */
