// SPDX-License-Identifier: GPL-2.0
/*
 * Basic worker thread pool for io_uring
 *
 * Copyright (C) 2019 Jens Axboe
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/mmu_context.h>
#include <linux/sched/mm.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/rculist_nulls.h>

#include "io-wq.h"

#define WORKER_IDLE_TIMEOUT	(5 * HZ)

enum {
	IO_WORKER_F_UP		= 1,	/* up and active */
	IO_WORKER_F_RUNNING	= 2,	/* account as running */
	IO_WORKER_F_FREE	= 4,	/* worker on free list */
	IO_WORKER_F_EXITING	= 8,	/* worker exiting */
	IO_WORKER_F_FIXED	= 16,	/* static idle worker */
};

enum {
	IO_WQ_BIT_EXIT		= 0,	/* wq exiting */
	IO_WQ_BIT_CANCEL	= 1,	/* cancel work on list */
};

enum {
	IO_WQE_FLAG_STALLED	= 1,	/* stalled on hash */
};

/*
 * One for each thread in a wqe pool
 */
struct io_worker {
	refcount_t ref;
	unsigned flags;
	struct hlist_nulls_node nulls_node;
	struct task_struct *task;
	wait_queue_head_t wait;
	struct io_wqe *wqe;
	struct io_wq_work *cur_work;

	struct rcu_head rcu;
	struct mm_struct *mm;
	struct files_struct *restore_files;
};

struct io_wq_nulls_list {
	struct hlist_nulls_head head;
	unsigned long nulls;
};

#if BITS_PER_LONG == 64
#define IO_WQ_HASH_ORDER	6
#else
#define IO_WQ_HASH_ORDER	5
#endif

/*
 * Per-node worker thread pool
 */
struct io_wqe {
	struct {
		spinlock_t lock;
		struct list_head work_list;
		unsigned long hash_map;
		unsigned flags;
	} ____cacheline_aligned_in_smp;

	int node;
	unsigned nr_workers;
	unsigned max_workers;
	atomic_t nr_running;

	struct io_wq_nulls_list free_list;
	struct io_wq_nulls_list busy_list;

	struct io_wq *wq;
};

/*
 * Per io_wq state
  */
struct io_wq {
	struct io_wqe **wqes;
	unsigned long state;
	unsigned nr_wqes;

	struct task_struct *manager;
	struct mm_struct *mm;
	refcount_t refs;
	struct completion done;
};

static void io_wq_free_worker(struct rcu_head *head)
{
	struct io_worker *worker = container_of(head, struct io_worker, rcu);

	kfree(worker);
}

static bool io_worker_get(struct io_worker *worker)
{
	return refcount_inc_not_zero(&worker->ref);
}

static void io_worker_release(struct io_worker *worker)
{
	if (refcount_dec_and_test(&worker->ref))
		wake_up_process(worker->task);
}

/*
 * Note: drops the wqe->lock if returning true! The caller must re-acquire
 * the lock in that case. Some callers need to restart handling if this
 * happens, so we can't just re-acquire the lock on behalf of the caller.
 */
static bool __io_worker_unuse(struct io_wqe *wqe, struct io_worker *worker)
{
	bool dropped_lock = false;

	if (current->files != worker->restore_files) {
		__acquire(&wqe->lock);
		spin_unlock_irq(&wqe->lock);
		dropped_lock = true;

		task_lock(current);
		current->files = worker->restore_files;
		task_unlock(current);
	}

	/*
	 * If we have an active mm, we need to drop the wq lock before unusing
	 * it. If we do, return true and let the caller retry the idle loop.
	 */
	if (worker->mm) {
		if (!dropped_lock) {
			__acquire(&wqe->lock);
			spin_unlock_irq(&wqe->lock);
			dropped_lock = true;
		}
		__set_current_state(TASK_RUNNING);
		set_fs(KERNEL_DS);
		unuse_mm(worker->mm);
		mmput(worker->mm);
		worker->mm = NULL;
	}

	return dropped_lock;
}

static void io_worker_exit(struct io_worker *worker)
{
	struct io_wqe *wqe = worker->wqe;
	bool all_done = false;

	/*
	 * If we're not at zero, someone else is holding a brief reference
	 * to the worker. Wait for that to go away.
	 */
	set_current_state(TASK_INTERRUPTIBLE);
	if (!refcount_dec_and_test(&worker->ref))
		schedule();
	__set_current_state(TASK_RUNNING);

	preempt_disable();
	current->flags &= ~PF_IO_WORKER;
	if (worker->flags & IO_WORKER_F_RUNNING)
		atomic_dec(&wqe->nr_running);
	worker->flags = 0;
	preempt_enable();

	spin_lock_irq(&wqe->lock);
	hlist_nulls_del_rcu(&worker->nulls_node);
	if (__io_worker_unuse(wqe, worker)) {
		__release(&wqe->lock);
		spin_lock_irq(&wqe->lock);
	}
	wqe->nr_workers--;
	all_done = !wqe->nr_workers;
	spin_unlock_irq(&wqe->lock);

	/* all workers gone, wq exit can proceed */
	if (all_done && refcount_dec_and_test(&wqe->wq->refs))
		complete(&wqe->wq->done);

	call_rcu(&worker->rcu, io_wq_free_worker);
}

static void io_worker_start(struct io_wqe *wqe, struct io_worker *worker)
{
	allow_kernel_signal(SIGINT);

	current->flags |= PF_IO_WORKER;

	worker->flags |= (IO_WORKER_F_UP | IO_WORKER_F_RUNNING);
	worker->restore_files = current->files;
	atomic_inc(&wqe->nr_running);
}

/*
 * Worker will start processing some work. Move it to the busy list, if
 * it's currently on the freelist
 */
static void __io_worker_busy(struct io_wqe *wqe, struct io_worker *worker,
			     struct io_wq_work *work)
	__must_hold(wqe->lock)
{
	if (worker->flags & IO_WORKER_F_FREE) {
		worker->flags &= ~IO_WORKER_F_FREE;
		hlist_nulls_del_init_rcu(&worker->nulls_node);
		hlist_nulls_add_head_rcu(&worker->nulls_node,
						&wqe->busy_list.head);
	}
	worker->cur_work = work;
}

/*
 * No work, worker going to sleep. Move to freelist, and unuse mm if we
 * have one attached. Dropping the mm may potentially sleep, so we drop
 * the lock in that case and return success. Since the caller has to
 * retry the loop in that case (we changed task state), we don't regrab
 * the lock if we return success.
 */
static bool __io_worker_idle(struct io_wqe *wqe, struct io_worker *worker)
	__must_hold(wqe->lock)
{
	if (!(worker->flags & IO_WORKER_F_FREE)) {
		worker->flags |= IO_WORKER_F_FREE;
		hlist_nulls_del_init_rcu(&worker->nulls_node);
		hlist_nulls_add_head_rcu(&worker->nulls_node,
						&wqe->free_list.head);
	}

	return __io_worker_unuse(wqe, worker);
}

static struct io_wq_work *io_get_next_work(struct io_wqe *wqe, unsigned *hash)
	__must_hold(wqe->lock)
{
	struct io_wq_work *work;

	list_for_each_entry(work, &wqe->work_list, list) {
		/* not hashed, can run anytime */
		if (!(work->flags & IO_WQ_WORK_HASHED)) {
			list_del(&work->list);
			return work;
		}

		/* hashed, can run if not already running */
		*hash = work->flags >> IO_WQ_HASH_SHIFT;
		if (!(wqe->hash_map & BIT_ULL(*hash))) {
			wqe->hash_map |= BIT_ULL(*hash);
			list_del(&work->list);
			return work;
		}
	}

	return NULL;
}

static void io_worker_handle_work(struct io_worker *worker)
	__releases(wqe->lock)
{
	struct io_wq_work *work, *old_work;
	struct io_wqe *wqe = worker->wqe;
	struct io_wq *wq = wqe->wq;

	do {
		unsigned hash = -1U;

		/*
		 * Signals are either sent to cancel specific work, or to just
		 * cancel all work items. For the former, ->cur_work must
		 * match. ->cur_work is NULL at this point, since we haven't
		 * assigned any work, so it's safe to flush signals for that
		 * case. For the latter case of cancelling all work, the caller
		 * wil have set IO_WQ_BIT_CANCEL.
		 */
		if (signal_pending(current))
			flush_signals(current);

		/*
		 * If we got some work, mark us as busy. If we didn't, but
		 * the list isn't empty, it means we stalled on hashed work.
		 * Mark us stalled so we don't keep looking for work when we
		 * can't make progress, any work completion or insertion will
		 * clear the stalled flag.
		 */
		work = io_get_next_work(wqe, &hash);
		if (work)
			__io_worker_busy(wqe, worker, work);
		else if (!list_empty(&wqe->work_list))
			wqe->flags |= IO_WQE_FLAG_STALLED;

		spin_unlock_irq(&wqe->lock);
		if (!work)
			break;
next:
		if ((work->flags & IO_WQ_WORK_NEEDS_FILES) &&
		    current->files != work->files) {
			task_lock(current);
			current->files = work->files;
			task_unlock(current);
		}
		if ((work->flags & IO_WQ_WORK_NEEDS_USER) && !worker->mm &&
		    wq->mm && mmget_not_zero(wq->mm)) {
			use_mm(wq->mm);
			set_fs(USER_DS);
			worker->mm = wq->mm;
		}
		if (test_bit(IO_WQ_BIT_CANCEL, &wq->state))
			work->flags |= IO_WQ_WORK_CANCEL;
		if (worker->mm)
			work->flags |= IO_WQ_WORK_HAS_MM;

		old_work = work;
		work->func(&work);

		spin_lock_irq(&wqe->lock);
		worker->cur_work = NULL;
		if (hash != -1U) {
			wqe->hash_map &= ~BIT_ULL(hash);
			wqe->flags &= ~IO_WQE_FLAG_STALLED;
		}
		if (work && work != old_work) {
			spin_unlock_irq(&wqe->lock);
			/* dependent work not hashed */
			hash = -1U;
			goto next;
		}
	} while (1);
}

static inline bool io_wqe_run_queue(struct io_wqe *wqe)
	__must_hold(wqe->lock)
{
	if (!list_empty_careful(&wqe->work_list) &&
	    !(wqe->flags & IO_WQE_FLAG_STALLED))
		return true;
	return false;
}

static int io_wqe_worker(void *data)
{
	struct io_worker *worker = data;
	struct io_wqe *wqe = worker->wqe;
	struct io_wq *wq = wqe->wq;
	DEFINE_WAIT(wait);

	io_worker_start(wqe, worker);

	while (!test_bit(IO_WQ_BIT_EXIT, &wq->state)) {
		prepare_to_wait(&worker->wait, &wait, TASK_INTERRUPTIBLE);

		spin_lock_irq(&wqe->lock);
		if (io_wqe_run_queue(wqe)) {
			__set_current_state(TASK_RUNNING);
			io_worker_handle_work(worker);
			continue;
		}
		/* drops the lock on success, retry */
		if (__io_worker_idle(wqe, worker)) {
			__release(&wqe->lock);
			continue;
		}
		spin_unlock_irq(&wqe->lock);
		if (signal_pending(current))
			flush_signals(current);
		if (schedule_timeout(WORKER_IDLE_TIMEOUT))
			continue;
		/* timed out, exit unless we're the fixed worker */
		if (test_bit(IO_WQ_BIT_EXIT, &wq->state) ||
		    !(worker->flags & IO_WORKER_F_FIXED))
			break;
	}

	finish_wait(&worker->wait, &wait);

	if (test_bit(IO_WQ_BIT_EXIT, &wq->state)) {
		spin_lock_irq(&wqe->lock);
		if (!list_empty(&wqe->work_list))
			io_worker_handle_work(worker);
		else
			spin_unlock_irq(&wqe->lock);
	}

	io_worker_exit(worker);
	return 0;
}

/*
 * Check head of free list for an available worker. If one isn't available,
 * caller must wake up the wq manager to create one.
 */
static bool io_wqe_activate_free_worker(struct io_wqe *wqe)
	__must_hold(RCU)
{
	struct hlist_nulls_node *n;
	struct io_worker *worker;

	n = rcu_dereference(hlist_nulls_first_rcu(&wqe->free_list.head));
	if (is_a_nulls(n))
		return false;

	worker = hlist_nulls_entry(n, struct io_worker, nulls_node);
	if (io_worker_get(worker)) {
		wake_up(&worker->wait);
		io_worker_release(worker);
		return true;
	}

	return false;
}

/*
 * We need a worker. If we find a free one, we're good. If not, and we're
 * below the max number of workers, wake up the manager to create one.
 */
static void io_wqe_wake_worker(struct io_wqe *wqe)
{
	bool ret;

	rcu_read_lock();
	ret = io_wqe_activate_free_worker(wqe);
	rcu_read_unlock();

	if (!ret && wqe->nr_workers < wqe->max_workers)
		wake_up_process(wqe->wq->manager);
}

/*
 * Called when a worker is scheduled in. Mark us as currently running.
 */
void io_wq_worker_running(struct task_struct *tsk)
{
	struct io_worker *worker = kthread_data(tsk);
	struct io_wqe *wqe = worker->wqe;

	if (!(worker->flags & IO_WORKER_F_UP))
		return;
	if (worker->flags & IO_WORKER_F_RUNNING)
		return;
	worker->flags |= IO_WORKER_F_RUNNING;
	atomic_inc(&wqe->nr_running);
}

/*
 * Called when worker is going to sleep. If there are no workers currently
 * running and we have work pending, wake up a free one or have the manager
 * set one up.
 */
void io_wq_worker_sleeping(struct task_struct *tsk)
{
	struct io_worker *worker = kthread_data(tsk);
	struct io_wqe *wqe = worker->wqe;

	if (!(worker->flags & IO_WORKER_F_UP))
		return;
	if (!(worker->flags & IO_WORKER_F_RUNNING))
		return;

	worker->flags &= ~IO_WORKER_F_RUNNING;

	spin_lock_irq(&wqe->lock);
	if (atomic_dec_and_test(&wqe->nr_running) && io_wqe_run_queue(wqe))
		io_wqe_wake_worker(wqe);
	spin_unlock_irq(&wqe->lock);
}

static void create_io_worker(struct io_wq *wq, struct io_wqe *wqe)
{
	struct io_worker *worker;

	worker = kcalloc_node(1, sizeof(*worker), GFP_KERNEL, wqe->node);
	if (!worker)
		return;

	refcount_set(&worker->ref, 1);
	worker->nulls_node.pprev = NULL;
	init_waitqueue_head(&worker->wait);
	worker->wqe = wqe;

	worker->task = kthread_create_on_node(io_wqe_worker, worker, wqe->node,
						"io_wqe_worker-%d", wqe->node);
	if (IS_ERR(worker->task)) {
		kfree(worker);
		return;
	}

	spin_lock_irq(&wqe->lock);
	hlist_nulls_add_head_rcu(&worker->nulls_node, &wqe->free_list.head);
	worker->flags |= IO_WORKER_F_FREE;
	if (!wqe->nr_workers)
		worker->flags |= IO_WORKER_F_FIXED;
	wqe->nr_workers++;
	spin_unlock_irq(&wqe->lock);

	wake_up_process(worker->task);
}

static inline bool io_wqe_need_new_worker(struct io_wqe *wqe)
	__must_hold(wqe->lock)
{
	if (!wqe->nr_workers)
		return true;
	if (hlist_nulls_empty(&wqe->free_list.head) &&
	    wqe->nr_workers < wqe->max_workers && io_wqe_run_queue(wqe))
		return true;

	return false;
}

/*
 * Manager thread. Tasked with creating new workers, if we need them.
 */
static int io_wq_manager(void *data)
{
	struct io_wq *wq = data;

	while (!kthread_should_stop()) {
		int i;

		for (i = 0; i < wq->nr_wqes; i++) {
			struct io_wqe *wqe = wq->wqes[i];
			bool fork_worker = false;

			spin_lock_irq(&wqe->lock);
			fork_worker = io_wqe_need_new_worker(wqe);
			spin_unlock_irq(&wqe->lock);
			if (fork_worker)
				create_io_worker(wq, wqe);
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}

	return 0;
}

static void io_wqe_enqueue(struct io_wqe *wqe, struct io_wq_work *work)
{
	unsigned long flags;

	spin_lock_irqsave(&wqe->lock, flags);
	list_add_tail(&work->list, &wqe->work_list);
	wqe->flags &= ~IO_WQE_FLAG_STALLED;
	spin_unlock_irqrestore(&wqe->lock, flags);

	if (!atomic_read(&wqe->nr_running))
		io_wqe_wake_worker(wqe);
}

void io_wq_enqueue(struct io_wq *wq, struct io_wq_work *work)
{
	struct io_wqe *wqe = wq->wqes[numa_node_id()];

	io_wqe_enqueue(wqe, work);
}

/*
 * Enqueue work, hashed by some key. Work items that hash to the same value
 * will not be done in parallel. Used to limit concurrent writes, generally
 * hashed by inode.
 */
void io_wq_enqueue_hashed(struct io_wq *wq, struct io_wq_work *work, void *val)
{
	struct io_wqe *wqe = wq->wqes[numa_node_id()];
	unsigned bit;


	bit = hash_ptr(val, IO_WQ_HASH_ORDER);
	work->flags |= (IO_WQ_WORK_HASHED | (bit << IO_WQ_HASH_SHIFT));
	io_wqe_enqueue(wqe, work);
}

static bool io_wqe_worker_send_sig(struct io_worker *worker, void *data)
{
	send_sig(SIGINT, worker->task, 1);
	return false;
}

/*
 * Iterate the passed in list and call the specific function for each
 * worker that isn't exiting
 */
static bool io_wq_for_each_worker(struct io_wqe *wqe,
				  struct io_wq_nulls_list *list,
				  bool (*func)(struct io_worker *, void *),
				  void *data)
{
	struct hlist_nulls_node *n;
	struct io_worker *worker;
	bool ret = false;

restart:
	hlist_nulls_for_each_entry_rcu(worker, n, &list->head, nulls_node) {
		if (io_worker_get(worker)) {
			ret = func(worker, data);
			io_worker_release(worker);
			if (ret)
				break;
		}
	}
	if (!ret && get_nulls_value(n) != list->nulls)
		goto restart;
	return ret;
}

void io_wq_cancel_all(struct io_wq *wq)
{
	int i;

	set_bit(IO_WQ_BIT_CANCEL, &wq->state);

	/*
	 * Browse both lists, as there's a gap between handing work off
	 * to a worker and the worker putting itself on the busy_list
	 */
	rcu_read_lock();
	for (i = 0; i < wq->nr_wqes; i++) {
		struct io_wqe *wqe = wq->wqes[i];

		io_wq_for_each_worker(wqe, &wqe->busy_list,
					io_wqe_worker_send_sig, NULL);
		io_wq_for_each_worker(wqe, &wqe->free_list,
					io_wqe_worker_send_sig, NULL);
	}
	rcu_read_unlock();
}

struct io_cb_cancel_data {
	struct io_wqe *wqe;
	work_cancel_fn *cancel;
	void *caller_data;
};

static bool io_work_cancel(struct io_worker *worker, void *cancel_data)
{
	struct io_cb_cancel_data *data = cancel_data;
	struct io_wqe *wqe = data->wqe;
	bool ret = false;

	/*
	 * Hold the lock to avoid ->cur_work going out of scope, caller
	 * may deference the passed in work.
	 */
	spin_lock_irq(&wqe->lock);
	if (worker->cur_work &&
	    data->cancel(worker->cur_work, data->caller_data)) {
		send_sig(SIGINT, worker->task, 1);
		ret = true;
	}
	spin_unlock_irq(&wqe->lock);

	return ret;
}

static enum io_wq_cancel io_wqe_cancel_cb_work(struct io_wqe *wqe,
					       work_cancel_fn *cancel,
					       void *cancel_data)
{
	struct io_cb_cancel_data data = {
		.wqe = wqe,
		.cancel = cancel,
		.caller_data = cancel_data,
	};
	struct io_wq_work *work;
	bool found = false;

	spin_lock_irq(&wqe->lock);
	list_for_each_entry(work, &wqe->work_list, list) {
		if (cancel(work, cancel_data)) {
			list_del(&work->list);
			found = true;
			break;
		}
	}
	spin_unlock_irq(&wqe->lock);

	if (found) {
		work->flags |= IO_WQ_WORK_CANCEL;
		work->func(&work);
		return IO_WQ_CANCEL_OK;
	}

	rcu_read_lock();
	found = io_wq_for_each_worker(wqe, &wqe->free_list, io_work_cancel,
					&data);
	if (found)
		goto done;

	found = io_wq_for_each_worker(wqe, &wqe->busy_list, io_work_cancel,
					&data);
done:
	rcu_read_unlock();
	return found ? IO_WQ_CANCEL_RUNNING : IO_WQ_CANCEL_NOTFOUND;
}

enum io_wq_cancel io_wq_cancel_cb(struct io_wq *wq, work_cancel_fn *cancel,
				  void *data)
{
	enum io_wq_cancel ret = IO_WQ_CANCEL_NOTFOUND;
	int i;

	for (i = 0; i < wq->nr_wqes; i++) {
		struct io_wqe *wqe = wq->wqes[i];

		ret = io_wqe_cancel_cb_work(wqe, cancel, data);
		if (ret != IO_WQ_CANCEL_NOTFOUND)
			break;
	}

	return ret;
}

static bool io_wq_worker_cancel(struct io_worker *worker, void *data)
{
	struct io_wq_work *work = data;

	if (worker->cur_work == work) {
		send_sig(SIGINT, worker->task, 1);
		return true;
	}

	return false;
}

static enum io_wq_cancel io_wqe_cancel_work(struct io_wqe *wqe,
					    struct io_wq_work *cwork)
{
	struct io_wq_work *work;
	bool found = false;

	cwork->flags |= IO_WQ_WORK_CANCEL;

	/*
	 * First check pending list, if we're lucky we can just remove it
	 * from there. CANCEL_OK means that the work is returned as-new,
	 * no completion will be posted for it.
	 */
	spin_lock_irq(&wqe->lock);
	list_for_each_entry(work, &wqe->work_list, list) {
		if (work == cwork) {
			list_del(&work->list);
			found = true;
			break;
		}
	}
	spin_unlock_irq(&wqe->lock);

	if (found) {
		work->flags |= IO_WQ_WORK_CANCEL;
		work->func(&work);
		return IO_WQ_CANCEL_OK;
	}

	/*
	 * Now check if a free (going busy) or busy worker has the work
	 * currently running. If we find it there, we'll return CANCEL_RUNNING
	 * as an indication that we attempte to signal cancellation. The
	 * completion will run normally in this case.
	 */
	rcu_read_lock();
	found = io_wq_for_each_worker(wqe, &wqe->free_list, io_wq_worker_cancel,
					cwork);
	if (found)
		goto done;

	found = io_wq_for_each_worker(wqe, &wqe->busy_list, io_wq_worker_cancel,
					cwork);
done:
	rcu_read_unlock();
	return found ? IO_WQ_CANCEL_RUNNING : IO_WQ_CANCEL_NOTFOUND;
}

enum io_wq_cancel io_wq_cancel_work(struct io_wq *wq, struct io_wq_work *cwork)
{
	enum io_wq_cancel ret = IO_WQ_CANCEL_NOTFOUND;
	int i;

	for (i = 0; i < wq->nr_wqes; i++) {
		struct io_wqe *wqe = wq->wqes[i];

		ret = io_wqe_cancel_work(wqe, cwork);
		if (ret != IO_WQ_CANCEL_NOTFOUND)
			break;
	}

	return ret;
}

struct io_wq_flush_data {
	struct io_wq_work work;
	struct completion done;
};

static void io_wq_flush_func(struct io_wq_work **workptr)
{
	struct io_wq_work *work = *workptr;
	struct io_wq_flush_data *data;

	data = container_of(work, struct io_wq_flush_data, work);
	complete(&data->done);
}

/*
 * Doesn't wait for previously queued work to finish. When this completes,
 * it just means that previously queued work was started.
 */
void io_wq_flush(struct io_wq *wq)
{
	struct io_wq_flush_data data;
	int i;

	for (i = 0; i < wq->nr_wqes; i++) {
		struct io_wqe *wqe = wq->wqes[i];

		init_completion(&data.done);
		INIT_IO_WORK(&data.work, io_wq_flush_func);
		io_wqe_enqueue(wqe, &data.work);
		wait_for_completion(&data.done);
	}
}

struct io_wq *io_wq_create(unsigned concurrency, struct mm_struct *mm)
{
	int ret = -ENOMEM, i, node;
	struct io_wq *wq;

	wq = kcalloc(1, sizeof(*wq), GFP_KERNEL);
	if (!wq)
		return ERR_PTR(-ENOMEM);

	wq->nr_wqes = num_online_nodes();
	wq->wqes = kcalloc(wq->nr_wqes, sizeof(struct io_wqe *), GFP_KERNEL);
	if (!wq->wqes) {
		kfree(wq);
		return ERR_PTR(-ENOMEM);
	}

	i = 0;
	refcount_set(&wq->refs, wq->nr_wqes);
	for_each_online_node(node) {
		struct io_wqe *wqe;

		wqe = kcalloc_node(1, sizeof(struct io_wqe), GFP_KERNEL, node);
		if (!wqe)
			break;
		wq->wqes[i] = wqe;
		wqe->node = node;
		wqe->max_workers = concurrency;
		wqe->node = node;
		wqe->wq = wq;
		spin_lock_init(&wqe->lock);
		INIT_LIST_HEAD(&wqe->work_list);
		INIT_HLIST_NULLS_HEAD(&wqe->free_list.head, 0);
		wqe->free_list.nulls = 0;
		INIT_HLIST_NULLS_HEAD(&wqe->busy_list.head, 1);
		wqe->busy_list.nulls = 1;
		atomic_set(&wqe->nr_running, 0);

		i++;
	}

	init_completion(&wq->done);

	if (i != wq->nr_wqes)
		goto err;

	/* caller must have already done mmgrab() on this mm */
	wq->mm = mm;

	wq->manager = kthread_create(io_wq_manager, wq, "io_wq_manager");
	if (!IS_ERR(wq->manager)) {
		wake_up_process(wq->manager);
		return wq;
	}

	ret = PTR_ERR(wq->manager);
	wq->manager = NULL;
err:
	complete(&wq->done);
	io_wq_destroy(wq);
	return ERR_PTR(ret);
}

static bool io_wq_worker_wake(struct io_worker *worker, void *data)
{
	wake_up_process(worker->task);
	return false;
}

void io_wq_destroy(struct io_wq *wq)
{
	int i;

	if (wq->manager) {
		set_bit(IO_WQ_BIT_EXIT, &wq->state);
		kthread_stop(wq->manager);
	}

	rcu_read_lock();
	for (i = 0; i < wq->nr_wqes; i++) {
		struct io_wqe *wqe = wq->wqes[i];

		if (!wqe)
			continue;
		io_wq_for_each_worker(wqe, &wqe->free_list, io_wq_worker_wake,
						NULL);
		io_wq_for_each_worker(wqe, &wqe->busy_list, io_wq_worker_wake,
						NULL);
	}
	rcu_read_unlock();

	wait_for_completion(&wq->done);

	for (i = 0; i < wq->nr_wqes; i++)
		kfree(wq->wqes[i]);
	kfree(wq->wqes);
	kfree(wq);
}
