#ifndef _LINUX_KTHREAD_H
#define _LINUX_KTHREAD_H
/* Simple interface for creating and stopping kernel threads without mess. */
#include <linux/err.h>
#include <linux/sched.h>

struct task_struct *kthread_create(int (*threadfn)(void *data),
				   void *data,
				   const char namefmt[], ...)
	__attribute__((format(printf, 3, 4)));

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
	.lock = SPIN_LOCK_UNLOCKED,					\
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

static inline void init_kthread_worker(struct kthread_worker *worker)
{
	*worker = (struct kthread_worker)KTHREAD_WORKER_INIT(*worker);
}

static inline void init_kthread_work(struct kthread_work *work,
				     kthread_work_func_t fn)
{
	*work = (struct kthread_work)KTHREAD_WORK_INIT(*work, fn);
}

int kthread_worker_fn(void *worker_ptr);

bool queue_kthread_work(struct kthread_worker *worker,
			struct kthread_work *work);
void flush_kthread_work(struct kthread_work *work);
void flush_kthread_worker(struct kthread_worker *worker);

#endif /* _LINUX_KTHREAD_H */
