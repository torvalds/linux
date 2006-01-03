/*
 * linux/net/sunrpc/sched.c
 *
 * Scheduling for synchronous and asynchronous RPC requests.
 *
 * Copyright (C) 1996 Olaf Kirch, <okir@monad.swb.de>
 * 
 * TCP NFS related read + write fixes
 * (C) 1999 Dave Airlie, University of Limerick, Ireland <airlied@linux.ie>
 */

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/mempool.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprt.h>

#ifdef RPC_DEBUG
#define RPCDBG_FACILITY		RPCDBG_SCHED
#define RPC_TASK_MAGIC_ID	0xf00baa
static int			rpc_task_id;
#endif

/*
 * RPC slabs and memory pools
 */
#define RPC_BUFFER_MAXSIZE	(2048)
#define RPC_BUFFER_POOLSIZE	(8)
#define RPC_TASK_POOLSIZE	(8)
static kmem_cache_t	*rpc_task_slabp __read_mostly;
static kmem_cache_t	*rpc_buffer_slabp __read_mostly;
static mempool_t	*rpc_task_mempool __read_mostly;
static mempool_t	*rpc_buffer_mempool __read_mostly;

static void			__rpc_default_timer(struct rpc_task *task);
static void			rpciod_killall(void);
static void			rpc_async_schedule(void *);

/*
 * RPC tasks that create another task (e.g. for contacting the portmapper)
 * will wait on this queue for their child's completion
 */
static RPC_WAITQ(childq, "childq");

/*
 * RPC tasks sit here while waiting for conditions to improve.
 */
static RPC_WAITQ(delay_queue, "delayq");

/*
 * All RPC tasks are linked into this list
 */
static LIST_HEAD(all_tasks);

/*
 * rpciod-related stuff
 */
static DECLARE_MUTEX(rpciod_sema);
static unsigned int		rpciod_users;
static struct workqueue_struct *rpciod_workqueue;

/*
 * Spinlock for other critical sections of code.
 */
static DEFINE_SPINLOCK(rpc_sched_lock);

/*
 * Disable the timer for a given RPC task. Should be called with
 * queue->lock and bh_disabled in order to avoid races within
 * rpc_run_timer().
 */
static inline void
__rpc_disable_timer(struct rpc_task *task)
{
	dprintk("RPC: %4d disabling timer\n", task->tk_pid);
	task->tk_timeout_fn = NULL;
	task->tk_timeout = 0;
}

/*
 * Run a timeout function.
 * We use the callback in order to allow __rpc_wake_up_task()
 * and friends to disable the timer synchronously on SMP systems
 * without calling del_timer_sync(). The latter could cause a
 * deadlock if called while we're holding spinlocks...
 */
static void rpc_run_timer(struct rpc_task *task)
{
	void (*callback)(struct rpc_task *);

	callback = task->tk_timeout_fn;
	task->tk_timeout_fn = NULL;
	if (callback && RPC_IS_QUEUED(task)) {
		dprintk("RPC: %4d running timer\n", task->tk_pid);
		callback(task);
	}
	smp_mb__before_clear_bit();
	clear_bit(RPC_TASK_HAS_TIMER, &task->tk_runstate);
	smp_mb__after_clear_bit();
}

/*
 * Set up a timer for the current task.
 */
static inline void
__rpc_add_timer(struct rpc_task *task, rpc_action timer)
{
	if (!task->tk_timeout)
		return;

	dprintk("RPC: %4d setting alarm for %lu ms\n",
			task->tk_pid, task->tk_timeout * 1000 / HZ);

	if (timer)
		task->tk_timeout_fn = timer;
	else
		task->tk_timeout_fn = __rpc_default_timer;
	set_bit(RPC_TASK_HAS_TIMER, &task->tk_runstate);
	mod_timer(&task->tk_timer, jiffies + task->tk_timeout);
}

/*
 * Delete any timer for the current task. Because we use del_timer_sync(),
 * this function should never be called while holding queue->lock.
 */
static void
rpc_delete_timer(struct rpc_task *task)
{
	if (RPC_IS_QUEUED(task))
		return;
	if (test_and_clear_bit(RPC_TASK_HAS_TIMER, &task->tk_runstate)) {
		del_singleshot_timer_sync(&task->tk_timer);
		dprintk("RPC: %4d deleting timer\n", task->tk_pid);
	}
}

/*
 * Add new request to a priority queue.
 */
static void __rpc_add_wait_queue_priority(struct rpc_wait_queue *queue, struct rpc_task *task)
{
	struct list_head *q;
	struct rpc_task *t;

	INIT_LIST_HEAD(&task->u.tk_wait.links);
	q = &queue->tasks[task->tk_priority];
	if (unlikely(task->tk_priority > queue->maxpriority))
		q = &queue->tasks[queue->maxpriority];
	list_for_each_entry(t, q, u.tk_wait.list) {
		if (t->tk_cookie == task->tk_cookie) {
			list_add_tail(&task->u.tk_wait.list, &t->u.tk_wait.links);
			return;
		}
	}
	list_add_tail(&task->u.tk_wait.list, q);
}

/*
 * Add new request to wait queue.
 *
 * Swapper tasks always get inserted at the head of the queue.
 * This should avoid many nasty memory deadlocks and hopefully
 * improve overall performance.
 * Everyone else gets appended to the queue to ensure proper FIFO behavior.
 */
static void __rpc_add_wait_queue(struct rpc_wait_queue *queue, struct rpc_task *task)
{
	BUG_ON (RPC_IS_QUEUED(task));

	if (RPC_IS_PRIORITY(queue))
		__rpc_add_wait_queue_priority(queue, task);
	else if (RPC_IS_SWAPPER(task))
		list_add(&task->u.tk_wait.list, &queue->tasks[0]);
	else
		list_add_tail(&task->u.tk_wait.list, &queue->tasks[0]);
	task->u.tk_wait.rpc_waitq = queue;
	rpc_set_queued(task);

	dprintk("RPC: %4d added to queue %p \"%s\"\n",
				task->tk_pid, queue, rpc_qname(queue));
}

/*
 * Remove request from a priority queue.
 */
static void __rpc_remove_wait_queue_priority(struct rpc_task *task)
{
	struct rpc_task *t;

	if (!list_empty(&task->u.tk_wait.links)) {
		t = list_entry(task->u.tk_wait.links.next, struct rpc_task, u.tk_wait.list);
		list_move(&t->u.tk_wait.list, &task->u.tk_wait.list);
		list_splice_init(&task->u.tk_wait.links, &t->u.tk_wait.links);
	}
	list_del(&task->u.tk_wait.list);
}

/*
 * Remove request from queue.
 * Note: must be called with spin lock held.
 */
static void __rpc_remove_wait_queue(struct rpc_task *task)
{
	struct rpc_wait_queue *queue;
	queue = task->u.tk_wait.rpc_waitq;

	if (RPC_IS_PRIORITY(queue))
		__rpc_remove_wait_queue_priority(task);
	else
		list_del(&task->u.tk_wait.list);
	dprintk("RPC: %4d removed from queue %p \"%s\"\n",
				task->tk_pid, queue, rpc_qname(queue));
}

static inline void rpc_set_waitqueue_priority(struct rpc_wait_queue *queue, int priority)
{
	queue->priority = priority;
	queue->count = 1 << (priority * 2);
}

static inline void rpc_set_waitqueue_cookie(struct rpc_wait_queue *queue, unsigned long cookie)
{
	queue->cookie = cookie;
	queue->nr = RPC_BATCH_COUNT;
}

static inline void rpc_reset_waitqueue_priority(struct rpc_wait_queue *queue)
{
	rpc_set_waitqueue_priority(queue, queue->maxpriority);
	rpc_set_waitqueue_cookie(queue, 0);
}

static void __rpc_init_priority_wait_queue(struct rpc_wait_queue *queue, const char *qname, int maxprio)
{
	int i;

	spin_lock_init(&queue->lock);
	for (i = 0; i < ARRAY_SIZE(queue->tasks); i++)
		INIT_LIST_HEAD(&queue->tasks[i]);
	queue->maxpriority = maxprio;
	rpc_reset_waitqueue_priority(queue);
#ifdef RPC_DEBUG
	queue->name = qname;
#endif
}

void rpc_init_priority_wait_queue(struct rpc_wait_queue *queue, const char *qname)
{
	__rpc_init_priority_wait_queue(queue, qname, RPC_PRIORITY_HIGH);
}

void rpc_init_wait_queue(struct rpc_wait_queue *queue, const char *qname)
{
	__rpc_init_priority_wait_queue(queue, qname, 0);
}
EXPORT_SYMBOL(rpc_init_wait_queue);

static int rpc_wait_bit_interruptible(void *word)
{
	if (signal_pending(current))
		return -ERESTARTSYS;
	schedule();
	return 0;
}

/*
 * Mark an RPC call as having completed by clearing the 'active' bit
 */
static inline void rpc_mark_complete_task(struct rpc_task *task)
{
	rpc_clear_active(task);
	wake_up_bit(&task->tk_runstate, RPC_TASK_ACTIVE);
}

/*
 * Allow callers to wait for completion of an RPC call
 */
int __rpc_wait_for_completion_task(struct rpc_task *task, int (*action)(void *))
{
	if (action == NULL)
		action = rpc_wait_bit_interruptible;
	return wait_on_bit(&task->tk_runstate, RPC_TASK_ACTIVE,
			action, TASK_INTERRUPTIBLE);
}
EXPORT_SYMBOL(__rpc_wait_for_completion_task);

/*
 * Make an RPC task runnable.
 *
 * Note: If the task is ASYNC, this must be called with 
 * the spinlock held to protect the wait queue operation.
 */
static void rpc_make_runnable(struct rpc_task *task)
{
	int do_ret;

	BUG_ON(task->tk_timeout_fn);
	do_ret = rpc_test_and_set_running(task);
	rpc_clear_queued(task);
	if (do_ret)
		return;
	if (RPC_IS_ASYNC(task)) {
		int status;

		INIT_WORK(&task->u.tk_work, rpc_async_schedule, (void *)task);
		status = queue_work(task->tk_workqueue, &task->u.tk_work);
		if (status < 0) {
			printk(KERN_WARNING "RPC: failed to add task to queue: error: %d!\n", status);
			task->tk_status = status;
			return;
		}
	} else
		wake_up_bit(&task->tk_runstate, RPC_TASK_QUEUED);
}

/*
 * Place a newly initialized task on the workqueue.
 */
static inline void
rpc_schedule_run(struct rpc_task *task)
{
	rpc_set_active(task);
	rpc_make_runnable(task);
}

/*
 * Prepare for sleeping on a wait queue.
 * By always appending tasks to the list we ensure FIFO behavior.
 * NB: An RPC task will only receive interrupt-driven events as long
 * as it's on a wait queue.
 */
static void __rpc_sleep_on(struct rpc_wait_queue *q, struct rpc_task *task,
			rpc_action action, rpc_action timer)
{
	dprintk("RPC: %4d sleep_on(queue \"%s\" time %ld)\n", task->tk_pid,
				rpc_qname(q), jiffies);

	if (!RPC_IS_ASYNC(task) && !RPC_IS_ACTIVATED(task)) {
		printk(KERN_ERR "RPC: Inactive synchronous task put to sleep!\n");
		return;
	}

	/* Mark the task as being activated if so needed */
	rpc_set_active(task);

	__rpc_add_wait_queue(q, task);

	BUG_ON(task->tk_callback != NULL);
	task->tk_callback = action;
	__rpc_add_timer(task, timer);
}

void rpc_sleep_on(struct rpc_wait_queue *q, struct rpc_task *task,
				rpc_action action, rpc_action timer)
{
	/*
	 * Protect the queue operations.
	 */
	spin_lock_bh(&q->lock);
	__rpc_sleep_on(q, task, action, timer);
	spin_unlock_bh(&q->lock);
}

/**
 * __rpc_do_wake_up_task - wake up a single rpc_task
 * @task: task to be woken up
 *
 * Caller must hold queue->lock, and have cleared the task queued flag.
 */
static void __rpc_do_wake_up_task(struct rpc_task *task)
{
	dprintk("RPC: %4d __rpc_wake_up_task (now %ld)\n", task->tk_pid, jiffies);

#ifdef RPC_DEBUG
	BUG_ON(task->tk_magic != RPC_TASK_MAGIC_ID);
#endif
	/* Has the task been executed yet? If not, we cannot wake it up! */
	if (!RPC_IS_ACTIVATED(task)) {
		printk(KERN_ERR "RPC: Inactive task (%p) being woken up!\n", task);
		return;
	}

	__rpc_disable_timer(task);
	__rpc_remove_wait_queue(task);

	rpc_make_runnable(task);

	dprintk("RPC:      __rpc_wake_up_task done\n");
}

/*
 * Wake up the specified task
 */
static void __rpc_wake_up_task(struct rpc_task *task)
{
	if (rpc_start_wakeup(task)) {
		if (RPC_IS_QUEUED(task))
			__rpc_do_wake_up_task(task);
		rpc_finish_wakeup(task);
	}
}

/*
 * Default timeout handler if none specified by user
 */
static void
__rpc_default_timer(struct rpc_task *task)
{
	dprintk("RPC: %d timeout (default timer)\n", task->tk_pid);
	task->tk_status = -ETIMEDOUT;
	rpc_wake_up_task(task);
}

/*
 * Wake up the specified task
 */
void rpc_wake_up_task(struct rpc_task *task)
{
	if (rpc_start_wakeup(task)) {
		if (RPC_IS_QUEUED(task)) {
			struct rpc_wait_queue *queue = task->u.tk_wait.rpc_waitq;

			spin_lock_bh(&queue->lock);
			__rpc_do_wake_up_task(task);
			spin_unlock_bh(&queue->lock);
		}
		rpc_finish_wakeup(task);
	}
}

/*
 * Wake up the next task on a priority queue.
 */
static struct rpc_task * __rpc_wake_up_next_priority(struct rpc_wait_queue *queue)
{
	struct list_head *q;
	struct rpc_task *task;

	/*
	 * Service a batch of tasks from a single cookie.
	 */
	q = &queue->tasks[queue->priority];
	if (!list_empty(q)) {
		task = list_entry(q->next, struct rpc_task, u.tk_wait.list);
		if (queue->cookie == task->tk_cookie) {
			if (--queue->nr)
				goto out;
			list_move_tail(&task->u.tk_wait.list, q);
		}
		/*
		 * Check if we need to switch queues.
		 */
		if (--queue->count)
			goto new_cookie;
	}

	/*
	 * Service the next queue.
	 */
	do {
		if (q == &queue->tasks[0])
			q = &queue->tasks[queue->maxpriority];
		else
			q = q - 1;
		if (!list_empty(q)) {
			task = list_entry(q->next, struct rpc_task, u.tk_wait.list);
			goto new_queue;
		}
	} while (q != &queue->tasks[queue->priority]);

	rpc_reset_waitqueue_priority(queue);
	return NULL;

new_queue:
	rpc_set_waitqueue_priority(queue, (unsigned int)(q - &queue->tasks[0]));
new_cookie:
	rpc_set_waitqueue_cookie(queue, task->tk_cookie);
out:
	__rpc_wake_up_task(task);
	return task;
}

/*
 * Wake up the next task on the wait queue.
 */
struct rpc_task * rpc_wake_up_next(struct rpc_wait_queue *queue)
{
	struct rpc_task	*task = NULL;

	dprintk("RPC:      wake_up_next(%p \"%s\")\n", queue, rpc_qname(queue));
	spin_lock_bh(&queue->lock);
	if (RPC_IS_PRIORITY(queue))
		task = __rpc_wake_up_next_priority(queue);
	else {
		task_for_first(task, &queue->tasks[0])
			__rpc_wake_up_task(task);
	}
	spin_unlock_bh(&queue->lock);

	return task;
}

/**
 * rpc_wake_up - wake up all rpc_tasks
 * @queue: rpc_wait_queue on which the tasks are sleeping
 *
 * Grabs queue->lock
 */
void rpc_wake_up(struct rpc_wait_queue *queue)
{
	struct rpc_task *task;

	struct list_head *head;
	spin_lock_bh(&queue->lock);
	head = &queue->tasks[queue->maxpriority];
	for (;;) {
		while (!list_empty(head)) {
			task = list_entry(head->next, struct rpc_task, u.tk_wait.list);
			__rpc_wake_up_task(task);
		}
		if (head == &queue->tasks[0])
			break;
		head--;
	}
	spin_unlock_bh(&queue->lock);
}

/**
 * rpc_wake_up_status - wake up all rpc_tasks and set their status value.
 * @queue: rpc_wait_queue on which the tasks are sleeping
 * @status: status value to set
 *
 * Grabs queue->lock
 */
void rpc_wake_up_status(struct rpc_wait_queue *queue, int status)
{
	struct list_head *head;
	struct rpc_task *task;

	spin_lock_bh(&queue->lock);
	head = &queue->tasks[queue->maxpriority];
	for (;;) {
		while (!list_empty(head)) {
			task = list_entry(head->next, struct rpc_task, u.tk_wait.list);
			task->tk_status = status;
			__rpc_wake_up_task(task);
		}
		if (head == &queue->tasks[0])
			break;
		head--;
	}
	spin_unlock_bh(&queue->lock);
}

/*
 * Run a task at a later time
 */
static void	__rpc_atrun(struct rpc_task *);
void
rpc_delay(struct rpc_task *task, unsigned long delay)
{
	task->tk_timeout = delay;
	rpc_sleep_on(&delay_queue, task, NULL, __rpc_atrun);
}

static void
__rpc_atrun(struct rpc_task *task)
{
	task->tk_status = 0;
	rpc_wake_up_task(task);
}

/*
 * Helper to call task->tk_ops->rpc_call_prepare
 */
static void rpc_prepare_task(struct rpc_task *task)
{
	task->tk_ops->rpc_call_prepare(task, task->tk_calldata);
}

/*
 * Helper that calls task->tk_ops->rpc_call_done if it exists
 */
void rpc_exit_task(struct rpc_task *task)
{
	task->tk_action = NULL;
	if (task->tk_ops->rpc_call_done != NULL) {
		task->tk_ops->rpc_call_done(task, task->tk_calldata);
		if (task->tk_action != NULL) {
			WARN_ON(RPC_ASSASSINATED(task));
			/* Always release the RPC slot and buffer memory */
			xprt_release(task);
		}
	}
}
EXPORT_SYMBOL(rpc_exit_task);

/*
 * This is the RPC `scheduler' (or rather, the finite state machine).
 */
static int __rpc_execute(struct rpc_task *task)
{
	int		status = 0;

	dprintk("RPC: %4d rpc_execute flgs %x\n",
				task->tk_pid, task->tk_flags);

	BUG_ON(RPC_IS_QUEUED(task));

	for (;;) {
		/*
		 * Garbage collection of pending timers...
		 */
		rpc_delete_timer(task);

		/*
		 * Execute any pending callback.
		 */
		if (RPC_DO_CALLBACK(task)) {
			/* Define a callback save pointer */
			void (*save_callback)(struct rpc_task *);
	
			/* 
			 * If a callback exists, save it, reset it,
			 * call it.
			 * The save is needed to stop from resetting
			 * another callback set within the callback handler
			 * - Dave
			 */
			save_callback=task->tk_callback;
			task->tk_callback=NULL;
			lock_kernel();
			save_callback(task);
			unlock_kernel();
		}

		/*
		 * Perform the next FSM step.
		 * tk_action may be NULL when the task has been killed
		 * by someone else.
		 */
		if (!RPC_IS_QUEUED(task)) {
			if (task->tk_action == NULL)
				break;
			lock_kernel();
			task->tk_action(task);
			unlock_kernel();
		}

		/*
		 * Lockless check for whether task is sleeping or not.
		 */
		if (!RPC_IS_QUEUED(task))
			continue;
		rpc_clear_running(task);
		if (RPC_IS_ASYNC(task)) {
			/* Careful! we may have raced... */
			if (RPC_IS_QUEUED(task))
				return 0;
			if (rpc_test_and_set_running(task))
				return 0;
			continue;
		}

		/* sync task: sleep here */
		dprintk("RPC: %4d sync task going to sleep\n", task->tk_pid);
		/* Note: Caller should be using rpc_clnt_sigmask() */
		status = out_of_line_wait_on_bit(&task->tk_runstate,
				RPC_TASK_QUEUED, rpc_wait_bit_interruptible,
				TASK_INTERRUPTIBLE);
		if (status == -ERESTARTSYS) {
			/*
			 * When a sync task receives a signal, it exits with
			 * -ERESTARTSYS. In order to catch any callbacks that
			 * clean up after sleeping on some queue, we don't
			 * break the loop here, but go around once more.
			 */
			dprintk("RPC: %4d got signal\n", task->tk_pid);
			task->tk_flags |= RPC_TASK_KILLED;
			rpc_exit(task, -ERESTARTSYS);
			rpc_wake_up_task(task);
		}
		rpc_set_running(task);
		dprintk("RPC: %4d sync task resuming\n", task->tk_pid);
	}

	dprintk("RPC: %4d, return %d, status %d\n", task->tk_pid, status, task->tk_status);
	/* Wake up anyone who is waiting for task completion */
	rpc_mark_complete_task(task);
	/* Release all resources associated with the task */
	rpc_release_task(task);
	return status;
}

/*
 * User-visible entry point to the scheduler.
 *
 * This may be called recursively if e.g. an async NFS task updates
 * the attributes and finds that dirty pages must be flushed.
 * NOTE: Upon exit of this function the task is guaranteed to be
 *	 released. In particular note that tk_release() will have
 *	 been called, so your task memory may have been freed.
 */
int
rpc_execute(struct rpc_task *task)
{
	rpc_set_active(task);
	rpc_set_running(task);
	return __rpc_execute(task);
}

static void rpc_async_schedule(void *arg)
{
	__rpc_execute((struct rpc_task *)arg);
}

/**
 * rpc_malloc - allocate an RPC buffer
 * @task: RPC task that will use this buffer
 * @size: requested byte size
 *
 * We try to ensure that some NFS reads and writes can always proceed
 * by using a mempool when allocating 'small' buffers.
 * In order to avoid memory starvation triggering more writebacks of
 * NFS requests, we use GFP_NOFS rather than GFP_KERNEL.
 */
void * rpc_malloc(struct rpc_task *task, size_t size)
{
	struct rpc_rqst *req = task->tk_rqstp;
	gfp_t	gfp;

	if (task->tk_flags & RPC_TASK_SWAPPER)
		gfp = GFP_ATOMIC;
	else
		gfp = GFP_NOFS;

	if (size > RPC_BUFFER_MAXSIZE) {
		req->rq_buffer = kmalloc(size, gfp);
		if (req->rq_buffer)
			req->rq_bufsize = size;
	} else {
		req->rq_buffer = mempool_alloc(rpc_buffer_mempool, gfp);
		if (req->rq_buffer)
			req->rq_bufsize = RPC_BUFFER_MAXSIZE;
	}
	return req->rq_buffer;
}

/**
 * rpc_free - free buffer allocated via rpc_malloc
 * @task: RPC task with a buffer to be freed
 *
 */
void rpc_free(struct rpc_task *task)
{
	struct rpc_rqst *req = task->tk_rqstp;

	if (req->rq_buffer) {
		if (req->rq_bufsize == RPC_BUFFER_MAXSIZE)
			mempool_free(req->rq_buffer, rpc_buffer_mempool);
		else
			kfree(req->rq_buffer);
		req->rq_buffer = NULL;
		req->rq_bufsize = 0;
	}
}

/*
 * Creation and deletion of RPC task structures
 */
void rpc_init_task(struct rpc_task *task, struct rpc_clnt *clnt, int flags, const struct rpc_call_ops *tk_ops, void *calldata)
{
	memset(task, 0, sizeof(*task));
	init_timer(&task->tk_timer);
	task->tk_timer.data     = (unsigned long) task;
	task->tk_timer.function = (void (*)(unsigned long)) rpc_run_timer;
	atomic_set(&task->tk_count, 1);
	task->tk_client = clnt;
	task->tk_flags  = flags;
	task->tk_ops = tk_ops;
	if (tk_ops->rpc_call_prepare != NULL)
		task->tk_action = rpc_prepare_task;
	task->tk_calldata = calldata;

	/* Initialize retry counters */
	task->tk_garb_retry = 2;
	task->tk_cred_retry = 2;

	task->tk_priority = RPC_PRIORITY_NORMAL;
	task->tk_cookie = (unsigned long)current;

	/* Initialize workqueue for async tasks */
	task->tk_workqueue = rpciod_workqueue;

	if (clnt) {
		atomic_inc(&clnt->cl_users);
		if (clnt->cl_softrtry)
			task->tk_flags |= RPC_TASK_SOFT;
		if (!clnt->cl_intr)
			task->tk_flags |= RPC_TASK_NOINTR;
	}

#ifdef RPC_DEBUG
	task->tk_magic = RPC_TASK_MAGIC_ID;
	task->tk_pid = rpc_task_id++;
#endif
	/* Add to global list of all tasks */
	spin_lock(&rpc_sched_lock);
	list_add_tail(&task->tk_task, &all_tasks);
	spin_unlock(&rpc_sched_lock);

	BUG_ON(task->tk_ops == NULL);

	dprintk("RPC: %4d new task procpid %d\n", task->tk_pid,
				current->pid);
}

static struct rpc_task *
rpc_alloc_task(void)
{
	return (struct rpc_task *)mempool_alloc(rpc_task_mempool, GFP_NOFS);
}

static void rpc_free_task(struct rpc_task *task)
{
	dprintk("RPC: %4d freeing task\n", task->tk_pid);
	mempool_free(task, rpc_task_mempool);
}

/*
 * Create a new task for the specified client.  We have to
 * clean up after an allocation failure, as the client may
 * have specified "oneshot".
 */
struct rpc_task *rpc_new_task(struct rpc_clnt *clnt, int flags, const struct rpc_call_ops *tk_ops, void *calldata)
{
	struct rpc_task	*task;

	task = rpc_alloc_task();
	if (!task)
		goto cleanup;

	rpc_init_task(task, clnt, flags, tk_ops, calldata);

	dprintk("RPC: %4d allocated task\n", task->tk_pid);
	task->tk_flags |= RPC_TASK_DYNAMIC;
out:
	return task;

cleanup:
	/* Check whether to release the client */
	if (clnt) {
		printk("rpc_new_task: failed, users=%d, oneshot=%d\n",
			atomic_read(&clnt->cl_users), clnt->cl_oneshot);
		atomic_inc(&clnt->cl_users); /* pretend we were used ... */
		rpc_release_client(clnt);
	}
	goto out;
}

void rpc_release_task(struct rpc_task *task)
{
	const struct rpc_call_ops *tk_ops = task->tk_ops;
	void *calldata = task->tk_calldata;

#ifdef RPC_DEBUG
	BUG_ON(task->tk_magic != RPC_TASK_MAGIC_ID);
#endif
	if (!atomic_dec_and_test(&task->tk_count))
		return;
	dprintk("RPC: %4d release task\n", task->tk_pid);

	/* Remove from global task list */
	spin_lock(&rpc_sched_lock);
	list_del(&task->tk_task);
	spin_unlock(&rpc_sched_lock);

	BUG_ON (RPC_IS_QUEUED(task));

	/* Synchronously delete any running timer */
	rpc_delete_timer(task);

	/* Release resources */
	if (task->tk_rqstp)
		xprt_release(task);
	if (task->tk_msg.rpc_cred)
		rpcauth_unbindcred(task);
	if (task->tk_client) {
		rpc_release_client(task->tk_client);
		task->tk_client = NULL;
	}

#ifdef RPC_DEBUG
	task->tk_magic = 0;
#endif
	if (task->tk_flags & RPC_TASK_DYNAMIC)
		rpc_free_task(task);
	if (tk_ops->rpc_release)
		tk_ops->rpc_release(calldata);
}

/**
 * rpc_run_task - Allocate a new RPC task, then run rpc_execute against it
 * @clnt - pointer to RPC client
 * @flags - RPC flags
 * @ops - RPC call ops
 * @data - user call data
 */
struct rpc_task *rpc_run_task(struct rpc_clnt *clnt, int flags,
					const struct rpc_call_ops *ops,
					void *data)
{
	struct rpc_task *task;
	task = rpc_new_task(clnt, flags, ops, data);
	if (task == NULL)
		return ERR_PTR(-ENOMEM);
	atomic_inc(&task->tk_count);
	rpc_execute(task);
	return task;
}
EXPORT_SYMBOL(rpc_run_task);

/**
 * rpc_find_parent - find the parent of a child task.
 * @child: child task
 *
 * Checks that the parent task is still sleeping on the
 * queue 'childq'. If so returns a pointer to the parent.
 * Upon failure returns NULL.
 *
 * Caller must hold childq.lock
 */
static inline struct rpc_task *rpc_find_parent(struct rpc_task *child, struct rpc_task *parent)
{
	struct rpc_task	*task;
	struct list_head *le;

	task_for_each(task, le, &childq.tasks[0])
		if (task == parent)
			return parent;

	return NULL;
}

static void rpc_child_exit(struct rpc_task *child, void *calldata)
{
	struct rpc_task	*parent;

	spin_lock_bh(&childq.lock);
	if ((parent = rpc_find_parent(child, calldata)) != NULL) {
		parent->tk_status = child->tk_status;
		__rpc_wake_up_task(parent);
	}
	spin_unlock_bh(&childq.lock);
}

static const struct rpc_call_ops rpc_child_ops = {
	.rpc_call_done = rpc_child_exit,
};

/*
 * Note: rpc_new_task releases the client after a failure.
 */
struct rpc_task *
rpc_new_child(struct rpc_clnt *clnt, struct rpc_task *parent)
{
	struct rpc_task	*task;

	task = rpc_new_task(clnt, RPC_TASK_ASYNC | RPC_TASK_CHILD, &rpc_child_ops, parent);
	if (!task)
		goto fail;
	return task;

fail:
	parent->tk_status = -ENOMEM;
	return NULL;
}

void rpc_run_child(struct rpc_task *task, struct rpc_task *child, rpc_action func)
{
	spin_lock_bh(&childq.lock);
	/* N.B. Is it possible for the child to have already finished? */
	__rpc_sleep_on(&childq, task, func, NULL);
	rpc_schedule_run(child);
	spin_unlock_bh(&childq.lock);
}

/*
 * Kill all tasks for the given client.
 * XXX: kill their descendants as well?
 */
void rpc_killall_tasks(struct rpc_clnt *clnt)
{
	struct rpc_task	*rovr;
	struct list_head *le;

	dprintk("RPC:      killing all tasks for client %p\n", clnt);

	/*
	 * Spin lock all_tasks to prevent changes...
	 */
	spin_lock(&rpc_sched_lock);
	alltask_for_each(rovr, le, &all_tasks) {
		if (! RPC_IS_ACTIVATED(rovr))
			continue;
		if (!clnt || rovr->tk_client == clnt) {
			rovr->tk_flags |= RPC_TASK_KILLED;
			rpc_exit(rovr, -EIO);
			rpc_wake_up_task(rovr);
		}
	}
	spin_unlock(&rpc_sched_lock);
}

static DECLARE_MUTEX_LOCKED(rpciod_running);

static void rpciod_killall(void)
{
	unsigned long flags;

	while (!list_empty(&all_tasks)) {
		clear_thread_flag(TIF_SIGPENDING);
		rpc_killall_tasks(NULL);
		flush_workqueue(rpciod_workqueue);
		if (!list_empty(&all_tasks)) {
			dprintk("rpciod_killall: waiting for tasks to exit\n");
			yield();
		}
	}

	spin_lock_irqsave(&current->sighand->siglock, flags);
	recalc_sigpending();
	spin_unlock_irqrestore(&current->sighand->siglock, flags);
}

/*
 * Start up the rpciod process if it's not already running.
 */
int
rpciod_up(void)
{
	struct workqueue_struct *wq;
	int error = 0;

	down(&rpciod_sema);
	dprintk("rpciod_up: users %d\n", rpciod_users);
	rpciod_users++;
	if (rpciod_workqueue)
		goto out;
	/*
	 * If there's no pid, we should be the first user.
	 */
	if (rpciod_users > 1)
		printk(KERN_WARNING "rpciod_up: no workqueue, %d users??\n", rpciod_users);
	/*
	 * Create the rpciod thread and wait for it to start.
	 */
	error = -ENOMEM;
	wq = create_workqueue("rpciod");
	if (wq == NULL) {
		printk(KERN_WARNING "rpciod_up: create workqueue failed, error=%d\n", error);
		rpciod_users--;
		goto out;
	}
	rpciod_workqueue = wq;
	error = 0;
out:
	up(&rpciod_sema);
	return error;
}

void
rpciod_down(void)
{
	down(&rpciod_sema);
	dprintk("rpciod_down sema %d\n", rpciod_users);
	if (rpciod_users) {
		if (--rpciod_users)
			goto out;
	} else
		printk(KERN_WARNING "rpciod_down: no users??\n");

	if (!rpciod_workqueue) {
		dprintk("rpciod_down: Nothing to do!\n");
		goto out;
	}
	rpciod_killall();

	destroy_workqueue(rpciod_workqueue);
	rpciod_workqueue = NULL;
 out:
	up(&rpciod_sema);
}

#ifdef RPC_DEBUG
void rpc_show_tasks(void)
{
	struct list_head *le;
	struct rpc_task *t;

	spin_lock(&rpc_sched_lock);
	if (list_empty(&all_tasks)) {
		spin_unlock(&rpc_sched_lock);
		return;
	}
	printk("-pid- proc flgs status -client- -prog- --rqstp- -timeout "
		"-rpcwait -action- ---ops--\n");
	alltask_for_each(t, le, &all_tasks) {
		const char *rpc_waitq = "none";

		if (RPC_IS_QUEUED(t))
			rpc_waitq = rpc_qname(t->u.tk_wait.rpc_waitq);

		printk("%05d %04d %04x %06d %8p %6d %8p %08ld %8s %8p %8p\n",
			t->tk_pid,
			(t->tk_msg.rpc_proc ? t->tk_msg.rpc_proc->p_proc : -1),
			t->tk_flags, t->tk_status,
			t->tk_client,
			(t->tk_client ? t->tk_client->cl_prog : 0),
			t->tk_rqstp, t->tk_timeout,
			rpc_waitq,
			t->tk_action, t->tk_ops);
	}
	spin_unlock(&rpc_sched_lock);
}
#endif

void
rpc_destroy_mempool(void)
{
	if (rpc_buffer_mempool)
		mempool_destroy(rpc_buffer_mempool);
	if (rpc_task_mempool)
		mempool_destroy(rpc_task_mempool);
	if (rpc_task_slabp && kmem_cache_destroy(rpc_task_slabp))
		printk(KERN_INFO "rpc_task: not all structures were freed\n");
	if (rpc_buffer_slabp && kmem_cache_destroy(rpc_buffer_slabp))
		printk(KERN_INFO "rpc_buffers: not all structures were freed\n");
}

int
rpc_init_mempool(void)
{
	rpc_task_slabp = kmem_cache_create("rpc_tasks",
					     sizeof(struct rpc_task),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL, NULL);
	if (!rpc_task_slabp)
		goto err_nomem;
	rpc_buffer_slabp = kmem_cache_create("rpc_buffers",
					     RPC_BUFFER_MAXSIZE,
					     0, SLAB_HWCACHE_ALIGN,
					     NULL, NULL);
	if (!rpc_buffer_slabp)
		goto err_nomem;
	rpc_task_mempool = mempool_create(RPC_TASK_POOLSIZE,
					    mempool_alloc_slab,
					    mempool_free_slab,
					    rpc_task_slabp);
	if (!rpc_task_mempool)
		goto err_nomem;
	rpc_buffer_mempool = mempool_create(RPC_BUFFER_POOLSIZE,
					    mempool_alloc_slab,
					    mempool_free_slab,
					    rpc_buffer_slabp);
	if (!rpc_buffer_mempool)
		goto err_nomem;
	return 0;
err_nomem:
	rpc_destroy_mempool();
	return -ENOMEM;
}
