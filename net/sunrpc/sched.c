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
#include <linux/spinlock.h>
#include <linux/mutex.h>

#include <linux/sunrpc/clnt.h>

#ifdef RPC_DEBUG
#define RPCDBG_FACILITY		RPCDBG_SCHED
#define RPC_TASK_MAGIC_ID	0xf00baa
#endif

/*
 * RPC slabs and memory pools
 */
#define RPC_BUFFER_MAXSIZE	(2048)
#define RPC_BUFFER_POOLSIZE	(8)
#define RPC_TASK_POOLSIZE	(8)
static struct kmem_cache	*rpc_task_slabp __read_mostly;
static struct kmem_cache	*rpc_buffer_slabp __read_mostly;
static mempool_t	*rpc_task_mempool __read_mostly;
static mempool_t	*rpc_buffer_mempool __read_mostly;

static void			rpc_async_schedule(struct work_struct *);
static void			 rpc_release_task(struct rpc_task *task);
static void __rpc_queue_timer_fn(unsigned long ptr);

/*
 * RPC tasks sit here while waiting for conditions to improve.
 */
static struct rpc_wait_queue delay_queue;

/*
 * rpciod-related stuff
 */
struct workqueue_struct *rpciod_workqueue;

/*
 * Disable the timer for a given RPC task. Should be called with
 * queue->lock and bh_disabled in order to avoid races within
 * rpc_run_timer().
 */
static void
__rpc_disable_timer(struct rpc_wait_queue *queue, struct rpc_task *task)
{
	if (task->tk_timeout == 0)
		return;
	dprintk("RPC: %5u disabling timer\n", task->tk_pid);
	task->tk_timeout = 0;
	list_del(&task->u.tk_wait.timer_list);
	if (list_empty(&queue->timer_list.list))
		del_timer(&queue->timer_list.timer);
}

static void
rpc_set_queue_timer(struct rpc_wait_queue *queue, unsigned long expires)
{
	queue->timer_list.expires = expires;
	mod_timer(&queue->timer_list.timer, expires);
}

/*
 * Set up a timer for the current task.
 */
static void
__rpc_add_timer(struct rpc_wait_queue *queue, struct rpc_task *task)
{
	if (!task->tk_timeout)
		return;

	dprintk("RPC: %5u setting alarm for %lu ms\n",
			task->tk_pid, task->tk_timeout * 1000 / HZ);

	task->u.tk_wait.expires = jiffies + task->tk_timeout;
	if (list_empty(&queue->timer_list.list) || time_before(task->u.tk_wait.expires, queue->timer_list.expires))
		rpc_set_queue_timer(queue, task->u.tk_wait.expires);
	list_add(&task->u.tk_wait.timer_list, &queue->timer_list.list);
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
		if (t->tk_owner == task->tk_owner) {
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
	task->tk_waitqueue = queue;
	queue->qlen++;
	rpc_set_queued(task);

	dprintk("RPC: %5u added to queue %p \"%s\"\n",
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
}

/*
 * Remove request from queue.
 * Note: must be called with spin lock held.
 */
static void __rpc_remove_wait_queue(struct rpc_wait_queue *queue, struct rpc_task *task)
{
	__rpc_disable_timer(queue, task);
	if (RPC_IS_PRIORITY(queue))
		__rpc_remove_wait_queue_priority(task);
	list_del(&task->u.tk_wait.list);
	queue->qlen--;
	dprintk("RPC: %5u removed from queue %p \"%s\"\n",
			task->tk_pid, queue, rpc_qname(queue));
}

static inline void rpc_set_waitqueue_priority(struct rpc_wait_queue *queue, int priority)
{
	queue->priority = priority;
	queue->count = 1 << (priority * 2);
}

static inline void rpc_set_waitqueue_owner(struct rpc_wait_queue *queue, pid_t pid)
{
	queue->owner = pid;
	queue->nr = RPC_BATCH_COUNT;
}

static inline void rpc_reset_waitqueue_priority(struct rpc_wait_queue *queue)
{
	rpc_set_waitqueue_priority(queue, queue->maxpriority);
	rpc_set_waitqueue_owner(queue, 0);
}

static void __rpc_init_priority_wait_queue(struct rpc_wait_queue *queue, const char *qname, unsigned char nr_queues)
{
	int i;

	spin_lock_init(&queue->lock);
	for (i = 0; i < ARRAY_SIZE(queue->tasks); i++)
		INIT_LIST_HEAD(&queue->tasks[i]);
	queue->maxpriority = nr_queues - 1;
	rpc_reset_waitqueue_priority(queue);
	queue->qlen = 0;
	setup_timer(&queue->timer_list.timer, __rpc_queue_timer_fn, (unsigned long)queue);
	INIT_LIST_HEAD(&queue->timer_list.list);
#ifdef RPC_DEBUG
	queue->name = qname;
#endif
}

void rpc_init_priority_wait_queue(struct rpc_wait_queue *queue, const char *qname)
{
	__rpc_init_priority_wait_queue(queue, qname, RPC_NR_PRIORITY);
}

void rpc_init_wait_queue(struct rpc_wait_queue *queue, const char *qname)
{
	__rpc_init_priority_wait_queue(queue, qname, 1);
}
EXPORT_SYMBOL_GPL(rpc_init_wait_queue);

void rpc_destroy_wait_queue(struct rpc_wait_queue *queue)
{
	del_timer_sync(&queue->timer_list.timer);
}
EXPORT_SYMBOL_GPL(rpc_destroy_wait_queue);

static int rpc_wait_bit_killable(void *word)
{
	if (fatal_signal_pending(current))
		return -ERESTARTSYS;
	schedule();
	return 0;
}

#ifdef RPC_DEBUG
static void rpc_task_set_debuginfo(struct rpc_task *task)
{
	static atomic_t rpc_pid;

	task->tk_magic = RPC_TASK_MAGIC_ID;
	task->tk_pid = atomic_inc_return(&rpc_pid);
}
#else
static inline void rpc_task_set_debuginfo(struct rpc_task *task)
{
}
#endif

static void rpc_set_active(struct rpc_task *task)
{
	struct rpc_clnt *clnt;
	if (test_and_set_bit(RPC_TASK_ACTIVE, &task->tk_runstate) != 0)
		return;
	rpc_task_set_debuginfo(task);
	/* Add to global list of all tasks */
	clnt = task->tk_client;
	if (clnt != NULL) {
		spin_lock(&clnt->cl_lock);
		list_add_tail(&task->tk_task, &clnt->cl_tasks);
		spin_unlock(&clnt->cl_lock);
	}
}

/*
 * Mark an RPC call as having completed by clearing the 'active' bit
 */
static void rpc_mark_complete_task(struct rpc_task *task)
{
	smp_mb__before_clear_bit();
	clear_bit(RPC_TASK_ACTIVE, &task->tk_runstate);
	smp_mb__after_clear_bit();
	wake_up_bit(&task->tk_runstate, RPC_TASK_ACTIVE);
}

/*
 * Allow callers to wait for completion of an RPC call
 */
int __rpc_wait_for_completion_task(struct rpc_task *task, int (*action)(void *))
{
	if (action == NULL)
		action = rpc_wait_bit_killable;
	return wait_on_bit(&task->tk_runstate, RPC_TASK_ACTIVE,
			action, TASK_KILLABLE);
}
EXPORT_SYMBOL_GPL(__rpc_wait_for_completion_task);

/*
 * Make an RPC task runnable.
 *
 * Note: If the task is ASYNC, this must be called with
 * the spinlock held to protect the wait queue operation.
 */
static void rpc_make_runnable(struct rpc_task *task)
{
	rpc_clear_queued(task);
	if (rpc_test_and_set_running(task))
		return;
	if (RPC_IS_ASYNC(task)) {
		int status;

		INIT_WORK(&task->u.tk_work, rpc_async_schedule);
		status = queue_work(rpciod_workqueue, &task->u.tk_work);
		if (status < 0) {
			printk(KERN_WARNING "RPC: failed to add task to queue: error: %d!\n", status);
			task->tk_status = status;
			return;
		}
	} else
		wake_up_bit(&task->tk_runstate, RPC_TASK_QUEUED);
}

/*
 * Prepare for sleeping on a wait queue.
 * By always appending tasks to the list we ensure FIFO behavior.
 * NB: An RPC task will only receive interrupt-driven events as long
 * as it's on a wait queue.
 */
static void __rpc_sleep_on(struct rpc_wait_queue *q, struct rpc_task *task,
			rpc_action action)
{
	dprintk("RPC: %5u sleep_on(queue \"%s\" time %lu)\n",
			task->tk_pid, rpc_qname(q), jiffies);

	if (!RPC_IS_ASYNC(task) && !RPC_IS_ACTIVATED(task)) {
		printk(KERN_ERR "RPC: Inactive synchronous task put to sleep!\n");
		return;
	}

	__rpc_add_wait_queue(q, task);

	BUG_ON(task->tk_callback != NULL);
	task->tk_callback = action;
	__rpc_add_timer(q, task);
}

void rpc_sleep_on(struct rpc_wait_queue *q, struct rpc_task *task,
				rpc_action action)
{
	/* Mark the task as being activated if so needed */
	rpc_set_active(task);

	/*
	 * Protect the queue operations.
	 */
	spin_lock_bh(&q->lock);
	__rpc_sleep_on(q, task, action);
	spin_unlock_bh(&q->lock);
}
EXPORT_SYMBOL_GPL(rpc_sleep_on);

/**
 * __rpc_do_wake_up_task - wake up a single rpc_task
 * @queue: wait queue
 * @task: task to be woken up
 *
 * Caller must hold queue->lock, and have cleared the task queued flag.
 */
static void __rpc_do_wake_up_task(struct rpc_wait_queue *queue, struct rpc_task *task)
{
	dprintk("RPC: %5u __rpc_wake_up_task (now %lu)\n",
			task->tk_pid, jiffies);

#ifdef RPC_DEBUG
	BUG_ON(task->tk_magic != RPC_TASK_MAGIC_ID);
#endif
	/* Has the task been executed yet? If not, we cannot wake it up! */
	if (!RPC_IS_ACTIVATED(task)) {
		printk(KERN_ERR "RPC: Inactive task (%p) being woken up!\n", task);
		return;
	}

	__rpc_remove_wait_queue(queue, task);

	rpc_make_runnable(task);

	dprintk("RPC:       __rpc_wake_up_task done\n");
}

/*
 * Wake up a queued task while the queue lock is being held
 */
static void rpc_wake_up_task_queue_locked(struct rpc_wait_queue *queue, struct rpc_task *task)
{
	if (RPC_IS_QUEUED(task) && task->tk_waitqueue == queue)
		__rpc_do_wake_up_task(queue, task);
}

/*
 * Wake up a task on a specific queue
 */
void rpc_wake_up_queued_task(struct rpc_wait_queue *queue, struct rpc_task *task)
{
	spin_lock_bh(&queue->lock);
	rpc_wake_up_task_queue_locked(queue, task);
	spin_unlock_bh(&queue->lock);
}
EXPORT_SYMBOL_GPL(rpc_wake_up_queued_task);

/*
 * Wake up the specified task
 */
static void rpc_wake_up_task(struct rpc_task *task)
{
	rpc_wake_up_queued_task(task->tk_waitqueue, task);
}

/*
 * Wake up the next task on a priority queue.
 */
static struct rpc_task * __rpc_wake_up_next_priority(struct rpc_wait_queue *queue)
{
	struct list_head *q;
	struct rpc_task *task;

	/*
	 * Service a batch of tasks from a single owner.
	 */
	q = &queue->tasks[queue->priority];
	if (!list_empty(q)) {
		task = list_entry(q->next, struct rpc_task, u.tk_wait.list);
		if (queue->owner == task->tk_owner) {
			if (--queue->nr)
				goto out;
			list_move_tail(&task->u.tk_wait.list, q);
		}
		/*
		 * Check if we need to switch queues.
		 */
		if (--queue->count)
			goto new_owner;
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
new_owner:
	rpc_set_waitqueue_owner(queue, task->tk_owner);
out:
	rpc_wake_up_task_queue_locked(queue, task);
	return task;
}

/*
 * Wake up the next task on the wait queue.
 */
struct rpc_task * rpc_wake_up_next(struct rpc_wait_queue *queue)
{
	struct rpc_task	*task = NULL;

	dprintk("RPC:       wake_up_next(%p \"%s\")\n",
			queue, rpc_qname(queue));
	spin_lock_bh(&queue->lock);
	if (RPC_IS_PRIORITY(queue))
		task = __rpc_wake_up_next_priority(queue);
	else {
		task_for_first(task, &queue->tasks[0])
			rpc_wake_up_task_queue_locked(queue, task);
	}
	spin_unlock_bh(&queue->lock);

	return task;
}
EXPORT_SYMBOL_GPL(rpc_wake_up_next);

/**
 * rpc_wake_up - wake up all rpc_tasks
 * @queue: rpc_wait_queue on which the tasks are sleeping
 *
 * Grabs queue->lock
 */
void rpc_wake_up(struct rpc_wait_queue *queue)
{
	struct rpc_task *task, *next;
	struct list_head *head;

	spin_lock_bh(&queue->lock);
	head = &queue->tasks[queue->maxpriority];
	for (;;) {
		list_for_each_entry_safe(task, next, head, u.tk_wait.list)
			rpc_wake_up_task_queue_locked(queue, task);
		if (head == &queue->tasks[0])
			break;
		head--;
	}
	spin_unlock_bh(&queue->lock);
}
EXPORT_SYMBOL_GPL(rpc_wake_up);

/**
 * rpc_wake_up_status - wake up all rpc_tasks and set their status value.
 * @queue: rpc_wait_queue on which the tasks are sleeping
 * @status: status value to set
 *
 * Grabs queue->lock
 */
void rpc_wake_up_status(struct rpc_wait_queue *queue, int status)
{
	struct rpc_task *task, *next;
	struct list_head *head;

	spin_lock_bh(&queue->lock);
	head = &queue->tasks[queue->maxpriority];
	for (;;) {
		list_for_each_entry_safe(task, next, head, u.tk_wait.list) {
			task->tk_status = status;
			rpc_wake_up_task_queue_locked(queue, task);
		}
		if (head == &queue->tasks[0])
			break;
		head--;
	}
	spin_unlock_bh(&queue->lock);
}
EXPORT_SYMBOL_GPL(rpc_wake_up_status);

static void __rpc_queue_timer_fn(unsigned long ptr)
{
	struct rpc_wait_queue *queue = (struct rpc_wait_queue *)ptr;
	struct rpc_task *task, *n;
	unsigned long expires, now, timeo;

	spin_lock(&queue->lock);
	expires = now = jiffies;
	list_for_each_entry_safe(task, n, &queue->timer_list.list, u.tk_wait.timer_list) {
		timeo = task->u.tk_wait.expires;
		if (time_after_eq(now, timeo)) {
			dprintk("RPC: %5u timeout\n", task->tk_pid);
			task->tk_status = -ETIMEDOUT;
			rpc_wake_up_task_queue_locked(queue, task);
			continue;
		}
		if (expires == now || time_after(expires, timeo))
			expires = timeo;
	}
	if (!list_empty(&queue->timer_list.list))
		rpc_set_queue_timer(queue, expires);
	spin_unlock(&queue->lock);
}

static void __rpc_atrun(struct rpc_task *task)
{
	task->tk_status = 0;
}

/*
 * Run a task at a later time
 */
void rpc_delay(struct rpc_task *task, unsigned long delay)
{
	task->tk_timeout = delay;
	rpc_sleep_on(&delay_queue, task, __rpc_atrun);
}
EXPORT_SYMBOL_GPL(rpc_delay);

/*
 * Helper to call task->tk_ops->rpc_call_prepare
 */
void rpc_prepare_task(struct rpc_task *task)
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
EXPORT_SYMBOL_GPL(rpc_exit_task);

void rpc_release_calldata(const struct rpc_call_ops *ops, void *calldata)
{
	if (ops->rpc_release != NULL)
		ops->rpc_release(calldata);
}

/*
 * This is the RPC `scheduler' (or rather, the finite state machine).
 */
static void __rpc_execute(struct rpc_task *task)
{
	struct rpc_wait_queue *queue;
	int task_is_async = RPC_IS_ASYNC(task);
	int status = 0;

	dprintk("RPC: %5u __rpc_execute flags=0x%x\n",
			task->tk_pid, task->tk_flags);

	BUG_ON(RPC_IS_QUEUED(task));

	for (;;) {

		/*
		 * Execute any pending callback.
		 */
		if (task->tk_callback) {
			void (*save_callback)(struct rpc_task *);

			/*
			 * We set tk_callback to NULL before calling it,
			 * in case it sets the tk_callback field itself:
			 */
			save_callback = task->tk_callback;
			task->tk_callback = NULL;
			save_callback(task);
		}

		/*
		 * Perform the next FSM step.
		 * tk_action may be NULL when the task has been killed
		 * by someone else.
		 */
		if (!RPC_IS_QUEUED(task)) {
			if (task->tk_action == NULL)
				break;
			task->tk_action(task);
		}

		/*
		 * Lockless check for whether task is sleeping or not.
		 */
		if (!RPC_IS_QUEUED(task))
			continue;
		/*
		 * The queue->lock protects against races with
		 * rpc_make_runnable().
		 *
		 * Note that once we clear RPC_TASK_RUNNING on an asynchronous
		 * rpc_task, rpc_make_runnable() can assign it to a
		 * different workqueue. We therefore cannot assume that the
		 * rpc_task pointer may still be dereferenced.
		 */
		queue = task->tk_waitqueue;
		spin_lock_bh(&queue->lock);
		if (!RPC_IS_QUEUED(task)) {
			spin_unlock_bh(&queue->lock);
			continue;
		}
		rpc_clear_running(task);
		spin_unlock_bh(&queue->lock);
		if (task_is_async)
			return;

		/* sync task: sleep here */
		dprintk("RPC: %5u sync task going to sleep\n", task->tk_pid);
		status = out_of_line_wait_on_bit(&task->tk_runstate,
				RPC_TASK_QUEUED, rpc_wait_bit_killable,
				TASK_KILLABLE);
		if (status == -ERESTARTSYS) {
			/*
			 * When a sync task receives a signal, it exits with
			 * -ERESTARTSYS. In order to catch any callbacks that
			 * clean up after sleeping on some queue, we don't
			 * break the loop here, but go around once more.
			 */
			dprintk("RPC: %5u got signal\n", task->tk_pid);
			task->tk_flags |= RPC_TASK_KILLED;
			rpc_exit(task, -ERESTARTSYS);
			rpc_wake_up_task(task);
		}
		rpc_set_running(task);
		dprintk("RPC: %5u sync task resuming\n", task->tk_pid);
	}

	dprintk("RPC: %5u return %d, status %d\n", task->tk_pid, status,
			task->tk_status);
	/* Release all resources associated with the task */
	rpc_release_task(task);
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
void rpc_execute(struct rpc_task *task)
{
	rpc_set_active(task);
	rpc_set_running(task);
	__rpc_execute(task);
}

static void rpc_async_schedule(struct work_struct *work)
{
	__rpc_execute(container_of(work, struct rpc_task, u.tk_work));
}

struct rpc_buffer {
	size_t	len;
	char	data[];
};

/**
 * rpc_malloc - allocate an RPC buffer
 * @task: RPC task that will use this buffer
 * @size: requested byte size
 *
 * To prevent rpciod from hanging, this allocator never sleeps,
 * returning NULL if the request cannot be serviced immediately.
 * The caller can arrange to sleep in a way that is safe for rpciod.
 *
 * Most requests are 'small' (under 2KiB) and can be serviced from a
 * mempool, ensuring that NFS reads and writes can always proceed,
 * and that there is good locality of reference for these buffers.
 *
 * In order to avoid memory starvation triggering more writebacks of
 * NFS requests, we avoid using GFP_KERNEL.
 */
void *rpc_malloc(struct rpc_task *task, size_t size)
{
	struct rpc_buffer *buf;
	gfp_t gfp = RPC_IS_SWAPPER(task) ? GFP_ATOMIC : GFP_NOWAIT;

	size += sizeof(struct rpc_buffer);
	if (size <= RPC_BUFFER_MAXSIZE)
		buf = mempool_alloc(rpc_buffer_mempool, gfp);
	else
		buf = kmalloc(size, gfp);

	if (!buf)
		return NULL;

	buf->len = size;
	dprintk("RPC: %5u allocated buffer of size %zu at %p\n",
			task->tk_pid, size, buf);
	return &buf->data;
}
EXPORT_SYMBOL_GPL(rpc_malloc);

/**
 * rpc_free - free buffer allocated via rpc_malloc
 * @buffer: buffer to free
 *
 */
void rpc_free(void *buffer)
{
	size_t size;
	struct rpc_buffer *buf;

	if (!buffer)
		return;

	buf = container_of(buffer, struct rpc_buffer, data);
	size = buf->len;

	dprintk("RPC:       freeing buffer of size %zu at %p\n",
			size, buf);

	if (size <= RPC_BUFFER_MAXSIZE)
		mempool_free(buf, rpc_buffer_mempool);
	else
		kfree(buf);
}
EXPORT_SYMBOL_GPL(rpc_free);

/*
 * Creation and deletion of RPC task structures
 */
static void rpc_init_task(struct rpc_task *task, const struct rpc_task_setup *task_setup_data)
{
	memset(task, 0, sizeof(*task));
	atomic_set(&task->tk_count, 1);
	task->tk_flags  = task_setup_data->flags;
	task->tk_ops = task_setup_data->callback_ops;
	task->tk_calldata = task_setup_data->callback_data;
	INIT_LIST_HEAD(&task->tk_task);

	/* Initialize retry counters */
	task->tk_garb_retry = 2;
	task->tk_cred_retry = 2;

	task->tk_priority = task_setup_data->priority - RPC_PRIORITY_LOW;
	task->tk_owner = current->tgid;

	/* Initialize workqueue for async tasks */
	task->tk_workqueue = task_setup_data->workqueue;

	task->tk_client = task_setup_data->rpc_client;
	if (task->tk_client != NULL) {
		kref_get(&task->tk_client->cl_kref);
		if (task->tk_client->cl_softrtry)
			task->tk_flags |= RPC_TASK_SOFT;
	}

	if (task->tk_ops->rpc_call_prepare != NULL)
		task->tk_action = rpc_prepare_task;

	if (task_setup_data->rpc_message != NULL) {
		task->tk_msg.rpc_proc = task_setup_data->rpc_message->rpc_proc;
		task->tk_msg.rpc_argp = task_setup_data->rpc_message->rpc_argp;
		task->tk_msg.rpc_resp = task_setup_data->rpc_message->rpc_resp;
		/* Bind the user cred */
		rpcauth_bindcred(task, task_setup_data->rpc_message->rpc_cred, task_setup_data->flags);
		if (task->tk_action == NULL)
			rpc_call_start(task);
	}

	/* starting timestamp */
	task->tk_start = jiffies;

	dprintk("RPC:       new task initialized, procpid %u\n",
				task_pid_nr(current));
}

static struct rpc_task *
rpc_alloc_task(void)
{
	return (struct rpc_task *)mempool_alloc(rpc_task_mempool, GFP_NOFS);
}

/*
 * Create a new task for the specified client.
 */
struct rpc_task *rpc_new_task(const struct rpc_task_setup *setup_data)
{
	struct rpc_task	*task = setup_data->task;
	unsigned short flags = 0;

	if (task == NULL) {
		task = rpc_alloc_task();
		if (task == NULL)
			goto out;
		flags = RPC_TASK_DYNAMIC;
	}

	rpc_init_task(task, setup_data);

	task->tk_flags |= flags;
	dprintk("RPC:       allocated task %p\n", task);
out:
	return task;
}

static void rpc_free_task(struct rpc_task *task)
{
	const struct rpc_call_ops *tk_ops = task->tk_ops;
	void *calldata = task->tk_calldata;

	if (task->tk_flags & RPC_TASK_DYNAMIC) {
		dprintk("RPC: %5u freeing task\n", task->tk_pid);
		mempool_free(task, rpc_task_mempool);
	}
	rpc_release_calldata(tk_ops, calldata);
}

static void rpc_async_release(struct work_struct *work)
{
	rpc_free_task(container_of(work, struct rpc_task, u.tk_work));
}

void rpc_put_task(struct rpc_task *task)
{
	if (!atomic_dec_and_test(&task->tk_count))
		return;
	/* Release resources */
	if (task->tk_rqstp)
		xprt_release(task);
	if (task->tk_msg.rpc_cred)
		rpcauth_unbindcred(task);
	if (task->tk_client) {
		rpc_release_client(task->tk_client);
		task->tk_client = NULL;
	}
	if (task->tk_workqueue != NULL) {
		INIT_WORK(&task->u.tk_work, rpc_async_release);
		queue_work(task->tk_workqueue, &task->u.tk_work);
	} else
		rpc_free_task(task);
}
EXPORT_SYMBOL_GPL(rpc_put_task);

static void rpc_release_task(struct rpc_task *task)
{
#ifdef RPC_DEBUG
	BUG_ON(task->tk_magic != RPC_TASK_MAGIC_ID);
#endif
	dprintk("RPC: %5u release task\n", task->tk_pid);

	if (!list_empty(&task->tk_task)) {
		struct rpc_clnt *clnt = task->tk_client;
		/* Remove from client task list */
		spin_lock(&clnt->cl_lock);
		list_del(&task->tk_task);
		spin_unlock(&clnt->cl_lock);
	}
	BUG_ON (RPC_IS_QUEUED(task));

#ifdef RPC_DEBUG
	task->tk_magic = 0;
#endif
	/* Wake up anyone who is waiting for task completion */
	rpc_mark_complete_task(task);

	rpc_put_task(task);
}

/*
 * Kill all tasks for the given client.
 * XXX: kill their descendants as well?
 */
void rpc_killall_tasks(struct rpc_clnt *clnt)
{
	struct rpc_task	*rovr;


	if (list_empty(&clnt->cl_tasks))
		return;
	dprintk("RPC:       killing all tasks for client %p\n", clnt);
	/*
	 * Spin lock all_tasks to prevent changes...
	 */
	spin_lock(&clnt->cl_lock);
	list_for_each_entry(rovr, &clnt->cl_tasks, tk_task) {
		if (! RPC_IS_ACTIVATED(rovr))
			continue;
		if (!(rovr->tk_flags & RPC_TASK_KILLED)) {
			rovr->tk_flags |= RPC_TASK_KILLED;
			rpc_exit(rovr, -EIO);
			rpc_wake_up_task(rovr);
		}
	}
	spin_unlock(&clnt->cl_lock);
}
EXPORT_SYMBOL_GPL(rpc_killall_tasks);

int rpciod_up(void)
{
	return try_module_get(THIS_MODULE) ? 0 : -EINVAL;
}

void rpciod_down(void)
{
	module_put(THIS_MODULE);
}

/*
 * Start up the rpciod workqueue.
 */
static int rpciod_start(void)
{
	struct workqueue_struct *wq;

	/*
	 * Create the rpciod thread and wait for it to start.
	 */
	dprintk("RPC:       creating workqueue rpciod\n");
	wq = create_workqueue("rpciod");
	rpciod_workqueue = wq;
	return rpciod_workqueue != NULL;
}

static void rpciod_stop(void)
{
	struct workqueue_struct *wq = NULL;

	if (rpciod_workqueue == NULL)
		return;
	dprintk("RPC:       destroying workqueue rpciod\n");

	wq = rpciod_workqueue;
	rpciod_workqueue = NULL;
	destroy_workqueue(wq);
}

void
rpc_destroy_mempool(void)
{
	rpciod_stop();
	if (rpc_buffer_mempool)
		mempool_destroy(rpc_buffer_mempool);
	if (rpc_task_mempool)
		mempool_destroy(rpc_task_mempool);
	if (rpc_task_slabp)
		kmem_cache_destroy(rpc_task_slabp);
	if (rpc_buffer_slabp)
		kmem_cache_destroy(rpc_buffer_slabp);
	rpc_destroy_wait_queue(&delay_queue);
}

int
rpc_init_mempool(void)
{
	/*
	 * The following is not strictly a mempool initialisation,
	 * but there is no harm in doing it here
	 */
	rpc_init_wait_queue(&delay_queue, "delayq");
	if (!rpciod_start())
		goto err_nomem;

	rpc_task_slabp = kmem_cache_create("rpc_tasks",
					     sizeof(struct rpc_task),
					     0, SLAB_HWCACHE_ALIGN,
					     NULL);
	if (!rpc_task_slabp)
		goto err_nomem;
	rpc_buffer_slabp = kmem_cache_create("rpc_buffers",
					     RPC_BUFFER_MAXSIZE,
					     0, SLAB_HWCACHE_ALIGN,
					     NULL);
	if (!rpc_buffer_slabp)
		goto err_nomem;
	rpc_task_mempool = mempool_create_slab_pool(RPC_TASK_POOLSIZE,
						    rpc_task_slabp);
	if (!rpc_task_mempool)
		goto err_nomem;
	rpc_buffer_mempool = mempool_create_slab_pool(RPC_BUFFER_POOLSIZE,
						      rpc_buffer_slabp);
	if (!rpc_buffer_mempool)
		goto err_nomem;
	return 0;
err_nomem:
	rpc_destroy_mempool();
	return -ENOMEM;
}
