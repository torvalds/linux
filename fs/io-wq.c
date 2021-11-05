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
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/rculist_nulls.h>
#include <linux/cpu.h>
#include <linux/tracehook.h>
#include <uapi/linux/io_uring.h>

#include "io-wq.h"

#define WORKER_IDLE_TIMEOUT	(5 * HZ)

enum {
	IO_WORKER_F_UP		= 1,	/* up and active */
	IO_WORKER_F_RUNNING	= 2,	/* account as running */
	IO_WORKER_F_FREE	= 4,	/* worker on free list */
	IO_WORKER_F_BOUND	= 8,	/* is doing bounded work */
};

enum {
	IO_WQ_BIT_EXIT		= 0,	/* wq exiting */
};

enum {
	IO_ACCT_STALLED_BIT	= 0,	/* stalled on hash */
};

/*
 * One for each thread in a wqe pool
 */
struct io_worker {
	refcount_t ref;
	unsigned flags;
	struct hlist_nulls_node nulls_node;
	struct list_head all_list;
	struct task_struct *task;
	struct io_wqe *wqe;

	struct io_wq_work *cur_work;
	spinlock_t lock;

	struct completion ref_done;

	unsigned long create_state;
	struct callback_head create_work;
	int create_index;

	union {
		struct rcu_head rcu;
		struct work_struct work;
	};
};

#if BITS_PER_LONG == 64
#define IO_WQ_HASH_ORDER	6
#else
#define IO_WQ_HASH_ORDER	5
#endif

#define IO_WQ_NR_HASH_BUCKETS	(1u << IO_WQ_HASH_ORDER)

struct io_wqe_acct {
	unsigned nr_workers;
	unsigned max_workers;
	int index;
	atomic_t nr_running;
	struct io_wq_work_list work_list;
	unsigned long flags;
};

enum {
	IO_WQ_ACCT_BOUND,
	IO_WQ_ACCT_UNBOUND,
	IO_WQ_ACCT_NR,
};

/*
 * Per-node worker thread pool
 */
struct io_wqe {
	raw_spinlock_t lock;
	struct io_wqe_acct acct[2];

	int node;

	struct hlist_nulls_head free_list;
	struct list_head all_list;

	struct wait_queue_entry wait;

	struct io_wq *wq;
	struct io_wq_work *hash_tail[IO_WQ_NR_HASH_BUCKETS];

	cpumask_var_t cpu_mask;
};

/*
 * Per io_wq state
  */
struct io_wq {
	unsigned long state;

	free_work_fn *free_work;
	io_wq_work_fn *do_work;

	struct io_wq_hash *hash;

	atomic_t worker_refs;
	struct completion worker_done;

	struct hlist_node cpuhp_node;

	struct task_struct *task;

	struct io_wqe *wqes[];
};

static enum cpuhp_state io_wq_online;

struct io_cb_cancel_data {
	work_cancel_fn *fn;
	void *data;
	int nr_running;
	int nr_pending;
	bool cancel_all;
};

static bool create_io_worker(struct io_wq *wq, struct io_wqe *wqe, int index);
static void io_wqe_dec_running(struct io_worker *worker);
static bool io_acct_cancel_pending_work(struct io_wqe *wqe,
					struct io_wqe_acct *acct,
					struct io_cb_cancel_data *match);

static bool io_worker_get(struct io_worker *worker)
{
	return refcount_inc_not_zero(&worker->ref);
}

static void io_worker_release(struct io_worker *worker)
{
	if (refcount_dec_and_test(&worker->ref))
		complete(&worker->ref_done);
}

static inline struct io_wqe_acct *io_get_acct(struct io_wqe *wqe, bool bound)
{
	return &wqe->acct[bound ? IO_WQ_ACCT_BOUND : IO_WQ_ACCT_UNBOUND];
}

static inline struct io_wqe_acct *io_work_get_acct(struct io_wqe *wqe,
						   struct io_wq_work *work)
{
	return io_get_acct(wqe, !(work->flags & IO_WQ_WORK_UNBOUND));
}

static inline struct io_wqe_acct *io_wqe_get_acct(struct io_worker *worker)
{
	return io_get_acct(worker->wqe, worker->flags & IO_WORKER_F_BOUND);
}

static void io_worker_ref_put(struct io_wq *wq)
{
	if (atomic_dec_and_test(&wq->worker_refs))
		complete(&wq->worker_done);
}

static void io_worker_exit(struct io_worker *worker)
{
	struct io_wqe *wqe = worker->wqe;

	if (refcount_dec_and_test(&worker->ref))
		complete(&worker->ref_done);
	wait_for_completion(&worker->ref_done);

	raw_spin_lock(&wqe->lock);
	if (worker->flags & IO_WORKER_F_FREE)
		hlist_nulls_del_rcu(&worker->nulls_node);
	list_del_rcu(&worker->all_list);
	preempt_disable();
	io_wqe_dec_running(worker);
	worker->flags = 0;
	current->flags &= ~PF_IO_WORKER;
	preempt_enable();
	raw_spin_unlock(&wqe->lock);

	kfree_rcu(worker, rcu);
	io_worker_ref_put(wqe->wq);
	do_exit(0);
}

static inline bool io_acct_run_queue(struct io_wqe_acct *acct)
{
	if (!wq_list_empty(&acct->work_list) &&
	    !test_bit(IO_ACCT_STALLED_BIT, &acct->flags))
		return true;
	return false;
}

/*
 * Check head of free list for an available worker. If one isn't available,
 * caller must create one.
 */
static bool io_wqe_activate_free_worker(struct io_wqe *wqe,
					struct io_wqe_acct *acct)
	__must_hold(RCU)
{
	struct hlist_nulls_node *n;
	struct io_worker *worker;

	/*
	 * Iterate free_list and see if we can find an idle worker to
	 * activate. If a given worker is on the free_list but in the process
	 * of exiting, keep trying.
	 */
	hlist_nulls_for_each_entry_rcu(worker, n, &wqe->free_list, nulls_node) {
		if (!io_worker_get(worker))
			continue;
		if (io_wqe_get_acct(worker) != acct) {
			io_worker_release(worker);
			continue;
		}
		if (wake_up_process(worker->task)) {
			io_worker_release(worker);
			return true;
		}
		io_worker_release(worker);
	}

	return false;
}

/*
 * We need a worker. If we find a free one, we're good. If not, and we're
 * below the max number of workers, create one.
 */
static bool io_wqe_create_worker(struct io_wqe *wqe, struct io_wqe_acct *acct)
{
	/*
	 * Most likely an attempt to queue unbounded work on an io_wq that
	 * wasn't setup with any unbounded workers.
	 */
	if (unlikely(!acct->max_workers))
		pr_warn_once("io-wq is not configured for unbound workers");

	raw_spin_lock(&wqe->lock);
	if (acct->nr_workers == acct->max_workers) {
		raw_spin_unlock(&wqe->lock);
		return true;
	}
	acct->nr_workers++;
	raw_spin_unlock(&wqe->lock);
	atomic_inc(&acct->nr_running);
	atomic_inc(&wqe->wq->worker_refs);
	return create_io_worker(wqe->wq, wqe, acct->index);
}

static void io_wqe_inc_running(struct io_worker *worker)
{
	struct io_wqe_acct *acct = io_wqe_get_acct(worker);

	atomic_inc(&acct->nr_running);
}

static void create_worker_cb(struct callback_head *cb)
{
	struct io_worker *worker;
	struct io_wq *wq;
	struct io_wqe *wqe;
	struct io_wqe_acct *acct;
	bool do_create = false;

	worker = container_of(cb, struct io_worker, create_work);
	wqe = worker->wqe;
	wq = wqe->wq;
	acct = &wqe->acct[worker->create_index];
	raw_spin_lock(&wqe->lock);
	if (acct->nr_workers < acct->max_workers) {
		acct->nr_workers++;
		do_create = true;
	}
	raw_spin_unlock(&wqe->lock);
	if (do_create) {
		create_io_worker(wq, wqe, worker->create_index);
	} else {
		atomic_dec(&acct->nr_running);
		io_worker_ref_put(wq);
	}
	clear_bit_unlock(0, &worker->create_state);
	io_worker_release(worker);
}

static bool io_queue_worker_create(struct io_worker *worker,
				   struct io_wqe_acct *acct,
				   task_work_func_t func)
{
	struct io_wqe *wqe = worker->wqe;
	struct io_wq *wq = wqe->wq;

	/* raced with exit, just ignore create call */
	if (test_bit(IO_WQ_BIT_EXIT, &wq->state))
		goto fail;
	if (!io_worker_get(worker))
		goto fail;
	/*
	 * create_state manages ownership of create_work/index. We should
	 * only need one entry per worker, as the worker going to sleep
	 * will trigger the condition, and waking will clear it once it
	 * runs the task_work.
	 */
	if (test_bit(0, &worker->create_state) ||
	    test_and_set_bit_lock(0, &worker->create_state))
		goto fail_release;

	init_task_work(&worker->create_work, func);
	worker->create_index = acct->index;
	if (!task_work_add(wq->task, &worker->create_work, TWA_SIGNAL))
		return true;
	clear_bit_unlock(0, &worker->create_state);
fail_release:
	io_worker_release(worker);
fail:
	atomic_dec(&acct->nr_running);
	io_worker_ref_put(wq);
	return false;
}

static void io_wqe_dec_running(struct io_worker *worker)
	__must_hold(wqe->lock)
{
	struct io_wqe_acct *acct = io_wqe_get_acct(worker);
	struct io_wqe *wqe = worker->wqe;

	if (!(worker->flags & IO_WORKER_F_UP))
		return;

	if (atomic_dec_and_test(&acct->nr_running) && io_acct_run_queue(acct)) {
		atomic_inc(&acct->nr_running);
		atomic_inc(&wqe->wq->worker_refs);
		io_queue_worker_create(worker, acct, create_worker_cb);
	}
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
	}
}

/*
 * No work, worker going to sleep. Move to freelist, and unuse mm if we
 * have one attached. Dropping the mm may potentially sleep, so we drop
 * the lock in that case and return success. Since the caller has to
 * retry the loop in that case (we changed task state), we don't regrab
 * the lock if we return success.
 */
static void __io_worker_idle(struct io_wqe *wqe, struct io_worker *worker)
	__must_hold(wqe->lock)
{
	if (!(worker->flags & IO_WORKER_F_FREE)) {
		worker->flags |= IO_WORKER_F_FREE;
		hlist_nulls_add_head_rcu(&worker->nulls_node, &wqe->free_list);
	}
}

static inline unsigned int io_get_work_hash(struct io_wq_work *work)
{
	return work->flags >> IO_WQ_HASH_SHIFT;
}

static void io_wait_on_hash(struct io_wqe *wqe, unsigned int hash)
{
	struct io_wq *wq = wqe->wq;

	spin_lock_irq(&wq->hash->wait.lock);
	if (list_empty(&wqe->wait.entry)) {
		__add_wait_queue(&wq->hash->wait, &wqe->wait);
		if (!test_bit(hash, &wq->hash->map)) {
			__set_current_state(TASK_RUNNING);
			list_del_init(&wqe->wait.entry);
		}
	}
	spin_unlock_irq(&wq->hash->wait.lock);
}

static struct io_wq_work *io_get_next_work(struct io_wqe_acct *acct,
					   struct io_worker *worker)
	__must_hold(wqe->lock)
{
	struct io_wq_work_node *node, *prev;
	struct io_wq_work *work, *tail;
	unsigned int stall_hash = -1U;
	struct io_wqe *wqe = worker->wqe;

	wq_list_for_each(node, prev, &acct->work_list) {
		unsigned int hash;

		work = container_of(node, struct io_wq_work, list);

		/* not hashed, can run anytime */
		if (!io_wq_is_hashed(work)) {
			wq_list_del(&acct->work_list, node, prev);
			return work;
		}

		hash = io_get_work_hash(work);
		/* all items with this hash lie in [work, tail] */
		tail = wqe->hash_tail[hash];

		/* hashed, can run if not already running */
		if (!test_and_set_bit(hash, &wqe->wq->hash->map)) {
			wqe->hash_tail[hash] = NULL;
			wq_list_cut(&acct->work_list, &tail->list, prev);
			return work;
		}
		if (stall_hash == -1U)
			stall_hash = hash;
		/* fast forward to a next hash, for-each will fix up @prev */
		node = &tail->list;
	}

	if (stall_hash != -1U) {
		/*
		 * Set this before dropping the lock to avoid racing with new
		 * work being added and clearing the stalled bit.
		 */
		set_bit(IO_ACCT_STALLED_BIT, &acct->flags);
		raw_spin_unlock(&wqe->lock);
		io_wait_on_hash(wqe, stall_hash);
		raw_spin_lock(&wqe->lock);
	}

	return NULL;
}

static bool io_flush_signals(void)
{
	if (unlikely(test_thread_flag(TIF_NOTIFY_SIGNAL))) {
		__set_current_state(TASK_RUNNING);
		tracehook_notify_signal();
		return true;
	}
	return false;
}

static void io_assign_current_work(struct io_worker *worker,
				   struct io_wq_work *work)
{
	if (work) {
		io_flush_signals();
		cond_resched();
	}

	spin_lock(&worker->lock);
	worker->cur_work = work;
	spin_unlock(&worker->lock);
}

static void io_wqe_enqueue(struct io_wqe *wqe, struct io_wq_work *work);

static void io_worker_handle_work(struct io_worker *worker)
	__releases(wqe->lock)
{
	struct io_wqe_acct *acct = io_wqe_get_acct(worker);
	struct io_wqe *wqe = worker->wqe;
	struct io_wq *wq = wqe->wq;
	bool do_kill = test_bit(IO_WQ_BIT_EXIT, &wq->state);

	do {
		struct io_wq_work *work;
get_next:
		/*
		 * If we got some work, mark us as busy. If we didn't, but
		 * the list isn't empty, it means we stalled on hashed work.
		 * Mark us stalled so we don't keep looking for work when we
		 * can't make progress, any work completion or insertion will
		 * clear the stalled flag.
		 */
		work = io_get_next_work(acct, worker);
		if (work)
			__io_worker_busy(wqe, worker, work);

		raw_spin_unlock(&wqe->lock);
		if (!work)
			break;
		io_assign_current_work(worker, work);
		__set_current_state(TASK_RUNNING);

		/* handle a whole dependent link */
		do {
			struct io_wq_work *next_hashed, *linked;
			unsigned int hash = io_get_work_hash(work);

			next_hashed = wq_next_work(work);

			if (unlikely(do_kill) && (work->flags & IO_WQ_WORK_UNBOUND))
				work->flags |= IO_WQ_WORK_CANCEL;
			wq->do_work(work);
			io_assign_current_work(worker, NULL);

			linked = wq->free_work(work);
			work = next_hashed;
			if (!work && linked && !io_wq_is_hashed(linked)) {
				work = linked;
				linked = NULL;
			}
			io_assign_current_work(worker, work);
			if (linked)
				io_wqe_enqueue(wqe, linked);

			if (hash != -1U && !next_hashed) {
				clear_bit(hash, &wq->hash->map);
				clear_bit(IO_ACCT_STALLED_BIT, &acct->flags);
				if (wq_has_sleeper(&wq->hash->wait))
					wake_up(&wq->hash->wait);
				raw_spin_lock(&wqe->lock);
				/* skip unnecessary unlock-lock wqe->lock */
				if (!work)
					goto get_next;
				raw_spin_unlock(&wqe->lock);
			}
		} while (work);

		raw_spin_lock(&wqe->lock);
	} while (1);
}

static int io_wqe_worker(void *data)
{
	struct io_worker *worker = data;
	struct io_wqe_acct *acct = io_wqe_get_acct(worker);
	struct io_wqe *wqe = worker->wqe;
	struct io_wq *wq = wqe->wq;
	bool last_timeout = false;
	char buf[TASK_COMM_LEN];

	worker->flags |= (IO_WORKER_F_UP | IO_WORKER_F_RUNNING);

	snprintf(buf, sizeof(buf), "iou-wrk-%d", wq->task->pid);
	set_task_comm(current, buf);

	while (!test_bit(IO_WQ_BIT_EXIT, &wq->state)) {
		long ret;

		set_current_state(TASK_INTERRUPTIBLE);
loop:
		raw_spin_lock(&wqe->lock);
		if (io_acct_run_queue(acct)) {
			io_worker_handle_work(worker);
			goto loop;
		}
		/* timed out, exit unless we're the last worker */
		if (last_timeout && acct->nr_workers > 1) {
			acct->nr_workers--;
			raw_spin_unlock(&wqe->lock);
			__set_current_state(TASK_RUNNING);
			break;
		}
		last_timeout = false;
		__io_worker_idle(wqe, worker);
		raw_spin_unlock(&wqe->lock);
		if (io_flush_signals())
			continue;
		ret = schedule_timeout(WORKER_IDLE_TIMEOUT);
		if (signal_pending(current)) {
			struct ksignal ksig;

			if (!get_signal(&ksig))
				continue;
			if (fatal_signal_pending(current) ||
			    signal_group_exit(current->signal))
				break;
			continue;
		}
		last_timeout = !ret;
	}

	if (test_bit(IO_WQ_BIT_EXIT, &wq->state)) {
		raw_spin_lock(&wqe->lock);
		io_worker_handle_work(worker);
	}

	io_worker_exit(worker);
	return 0;
}

/*
 * Called when a worker is scheduled in. Mark us as currently running.
 */
void io_wq_worker_running(struct task_struct *tsk)
{
	struct io_worker *worker = tsk->pf_io_worker;

	if (!worker)
		return;
	if (!(worker->flags & IO_WORKER_F_UP))
		return;
	if (worker->flags & IO_WORKER_F_RUNNING)
		return;
	worker->flags |= IO_WORKER_F_RUNNING;
	io_wqe_inc_running(worker);
}

/*
 * Called when worker is going to sleep. If there are no workers currently
 * running and we have work pending, wake up a free one or create a new one.
 */
void io_wq_worker_sleeping(struct task_struct *tsk)
{
	struct io_worker *worker = tsk->pf_io_worker;

	if (!worker)
		return;
	if (!(worker->flags & IO_WORKER_F_UP))
		return;
	if (!(worker->flags & IO_WORKER_F_RUNNING))
		return;

	worker->flags &= ~IO_WORKER_F_RUNNING;

	raw_spin_lock(&worker->wqe->lock);
	io_wqe_dec_running(worker);
	raw_spin_unlock(&worker->wqe->lock);
}

static void io_init_new_worker(struct io_wqe *wqe, struct io_worker *worker,
			       struct task_struct *tsk)
{
	tsk->pf_io_worker = worker;
	worker->task = tsk;
	set_cpus_allowed_ptr(tsk, wqe->cpu_mask);
	tsk->flags |= PF_NO_SETAFFINITY;

	raw_spin_lock(&wqe->lock);
	hlist_nulls_add_head_rcu(&worker->nulls_node, &wqe->free_list);
	list_add_tail_rcu(&worker->all_list, &wqe->all_list);
	worker->flags |= IO_WORKER_F_FREE;
	raw_spin_unlock(&wqe->lock);
	wake_up_new_task(tsk);
}

static bool io_wq_work_match_all(struct io_wq_work *work, void *data)
{
	return true;
}

static inline bool io_should_retry_thread(long err)
{
	switch (err) {
	case -EAGAIN:
	case -ERESTARTSYS:
	case -ERESTARTNOINTR:
	case -ERESTARTNOHAND:
		return true;
	default:
		return false;
	}
}

static void create_worker_cont(struct callback_head *cb)
{
	struct io_worker *worker;
	struct task_struct *tsk;
	struct io_wqe *wqe;

	worker = container_of(cb, struct io_worker, create_work);
	clear_bit_unlock(0, &worker->create_state);
	wqe = worker->wqe;
	tsk = create_io_thread(io_wqe_worker, worker, wqe->node);
	if (!IS_ERR(tsk)) {
		io_init_new_worker(wqe, worker, tsk);
		io_worker_release(worker);
		return;
	} else if (!io_should_retry_thread(PTR_ERR(tsk))) {
		struct io_wqe_acct *acct = io_wqe_get_acct(worker);

		atomic_dec(&acct->nr_running);
		raw_spin_lock(&wqe->lock);
		acct->nr_workers--;
		if (!acct->nr_workers) {
			struct io_cb_cancel_data match = {
				.fn		= io_wq_work_match_all,
				.cancel_all	= true,
			};

			while (io_acct_cancel_pending_work(wqe, acct, &match))
				raw_spin_lock(&wqe->lock);
		}
		raw_spin_unlock(&wqe->lock);
		io_worker_ref_put(wqe->wq);
		kfree(worker);
		return;
	}

	/* re-create attempts grab a new worker ref, drop the existing one */
	io_worker_release(worker);
	schedule_work(&worker->work);
}

static void io_workqueue_create(struct work_struct *work)
{
	struct io_worker *worker = container_of(work, struct io_worker, work);
	struct io_wqe_acct *acct = io_wqe_get_acct(worker);

	if (!io_queue_worker_create(worker, acct, create_worker_cont)) {
		clear_bit_unlock(0, &worker->create_state);
		io_worker_release(worker);
		kfree(worker);
	}
}

static bool create_io_worker(struct io_wq *wq, struct io_wqe *wqe, int index)
{
	struct io_wqe_acct *acct = &wqe->acct[index];
	struct io_worker *worker;
	struct task_struct *tsk;

	__set_current_state(TASK_RUNNING);

	worker = kzalloc_node(sizeof(*worker), GFP_KERNEL, wqe->node);
	if (!worker) {
fail:
		atomic_dec(&acct->nr_running);
		raw_spin_lock(&wqe->lock);
		acct->nr_workers--;
		raw_spin_unlock(&wqe->lock);
		io_worker_ref_put(wq);
		return false;
	}

	refcount_set(&worker->ref, 1);
	worker->wqe = wqe;
	spin_lock_init(&worker->lock);
	init_completion(&worker->ref_done);

	if (index == IO_WQ_ACCT_BOUND)
		worker->flags |= IO_WORKER_F_BOUND;

	tsk = create_io_thread(io_wqe_worker, worker, wqe->node);
	if (!IS_ERR(tsk)) {
		io_init_new_worker(wqe, worker, tsk);
	} else if (!io_should_retry_thread(PTR_ERR(tsk))) {
		kfree(worker);
		goto fail;
	} else {
		INIT_WORK(&worker->work, io_workqueue_create);
		schedule_work(&worker->work);
	}

	return true;
}

/*
 * Iterate the passed in list and call the specific function for each
 * worker that isn't exiting
 */
static bool io_wq_for_each_worker(struct io_wqe *wqe,
				  bool (*func)(struct io_worker *, void *),
				  void *data)
{
	struct io_worker *worker;
	bool ret = false;

	list_for_each_entry_rcu(worker, &wqe->all_list, all_list) {
		if (io_worker_get(worker)) {
			/* no task if node is/was offline */
			if (worker->task)
				ret = func(worker, data);
			io_worker_release(worker);
			if (ret)
				break;
		}
	}

	return ret;
}

static bool io_wq_worker_wake(struct io_worker *worker, void *data)
{
	set_notify_signal(worker->task);
	wake_up_process(worker->task);
	return false;
}

static void io_run_cancel(struct io_wq_work *work, struct io_wqe *wqe)
{
	struct io_wq *wq = wqe->wq;

	do {
		work->flags |= IO_WQ_WORK_CANCEL;
		wq->do_work(work);
		work = wq->free_work(work);
	} while (work);
}

static void io_wqe_insert_work(struct io_wqe *wqe, struct io_wq_work *work)
{
	struct io_wqe_acct *acct = io_work_get_acct(wqe, work);
	unsigned int hash;
	struct io_wq_work *tail;

	if (!io_wq_is_hashed(work)) {
append:
		wq_list_add_tail(&work->list, &acct->work_list);
		return;
	}

	hash = io_get_work_hash(work);
	tail = wqe->hash_tail[hash];
	wqe->hash_tail[hash] = work;
	if (!tail)
		goto append;

	wq_list_add_after(&work->list, &tail->list, &acct->work_list);
}

static bool io_wq_work_match_item(struct io_wq_work *work, void *data)
{
	return work == data;
}

static void io_wqe_enqueue(struct io_wqe *wqe, struct io_wq_work *work)
{
	struct io_wqe_acct *acct = io_work_get_acct(wqe, work);
	unsigned work_flags = work->flags;
	bool do_create;

	/*
	 * If io-wq is exiting for this task, or if the request has explicitly
	 * been marked as one that should not get executed, cancel it here.
	 */
	if (test_bit(IO_WQ_BIT_EXIT, &wqe->wq->state) ||
	    (work->flags & IO_WQ_WORK_CANCEL)) {
		io_run_cancel(work, wqe);
		return;
	}

	raw_spin_lock(&wqe->lock);
	io_wqe_insert_work(wqe, work);
	clear_bit(IO_ACCT_STALLED_BIT, &acct->flags);

	rcu_read_lock();
	do_create = !io_wqe_activate_free_worker(wqe, acct);
	rcu_read_unlock();

	raw_spin_unlock(&wqe->lock);

	if (do_create && ((work_flags & IO_WQ_WORK_CONCURRENT) ||
	    !atomic_read(&acct->nr_running))) {
		bool did_create;

		did_create = io_wqe_create_worker(wqe, acct);
		if (likely(did_create))
			return;

		raw_spin_lock(&wqe->lock);
		/* fatal condition, failed to create the first worker */
		if (!acct->nr_workers) {
			struct io_cb_cancel_data match = {
				.fn		= io_wq_work_match_item,
				.data		= work,
				.cancel_all	= false,
			};

			if (io_acct_cancel_pending_work(wqe, acct, &match))
				raw_spin_lock(&wqe->lock);
		}
		raw_spin_unlock(&wqe->lock);
	}
}

void io_wq_enqueue(struct io_wq *wq, struct io_wq_work *work)
{
	struct io_wqe *wqe = wq->wqes[numa_node_id()];

	io_wqe_enqueue(wqe, work);
}

/*
 * Work items that hash to the same value will not be done in parallel.
 * Used to limit concurrent writes, generally hashed by inode.
 */
void io_wq_hash_work(struct io_wq_work *work, void *val)
{
	unsigned int bit;

	bit = hash_ptr(val, IO_WQ_HASH_ORDER);
	work->flags |= (IO_WQ_WORK_HASHED | (bit << IO_WQ_HASH_SHIFT));
}

static bool io_wq_worker_cancel(struct io_worker *worker, void *data)
{
	struct io_cb_cancel_data *match = data;

	/*
	 * Hold the lock to avoid ->cur_work going out of scope, caller
	 * may dereference the passed in work.
	 */
	spin_lock(&worker->lock);
	if (worker->cur_work &&
	    match->fn(worker->cur_work, match->data)) {
		set_notify_signal(worker->task);
		match->nr_running++;
	}
	spin_unlock(&worker->lock);

	return match->nr_running && !match->cancel_all;
}

static inline void io_wqe_remove_pending(struct io_wqe *wqe,
					 struct io_wq_work *work,
					 struct io_wq_work_node *prev)
{
	struct io_wqe_acct *acct = io_work_get_acct(wqe, work);
	unsigned int hash = io_get_work_hash(work);
	struct io_wq_work *prev_work = NULL;

	if (io_wq_is_hashed(work) && work == wqe->hash_tail[hash]) {
		if (prev)
			prev_work = container_of(prev, struct io_wq_work, list);
		if (prev_work && io_get_work_hash(prev_work) == hash)
			wqe->hash_tail[hash] = prev_work;
		else
			wqe->hash_tail[hash] = NULL;
	}
	wq_list_del(&acct->work_list, &work->list, prev);
}

static bool io_acct_cancel_pending_work(struct io_wqe *wqe,
					struct io_wqe_acct *acct,
					struct io_cb_cancel_data *match)
	__releases(wqe->lock)
{
	struct io_wq_work_node *node, *prev;
	struct io_wq_work *work;

	wq_list_for_each(node, prev, &acct->work_list) {
		work = container_of(node, struct io_wq_work, list);
		if (!match->fn(work, match->data))
			continue;
		io_wqe_remove_pending(wqe, work, prev);
		raw_spin_unlock(&wqe->lock);
		io_run_cancel(work, wqe);
		match->nr_pending++;
		/* not safe to continue after unlock */
		return true;
	}

	return false;
}

static void io_wqe_cancel_pending_work(struct io_wqe *wqe,
				       struct io_cb_cancel_data *match)
{
	int i;
retry:
	raw_spin_lock(&wqe->lock);
	for (i = 0; i < IO_WQ_ACCT_NR; i++) {
		struct io_wqe_acct *acct = io_get_acct(wqe, i == 0);

		if (io_acct_cancel_pending_work(wqe, acct, match)) {
			if (match->cancel_all)
				goto retry;
			return;
		}
	}
	raw_spin_unlock(&wqe->lock);
}

static void io_wqe_cancel_running_work(struct io_wqe *wqe,
				       struct io_cb_cancel_data *match)
{
	rcu_read_lock();
	io_wq_for_each_worker(wqe, io_wq_worker_cancel, match);
	rcu_read_unlock();
}

enum io_wq_cancel io_wq_cancel_cb(struct io_wq *wq, work_cancel_fn *cancel,
				  void *data, bool cancel_all)
{
	struct io_cb_cancel_data match = {
		.fn		= cancel,
		.data		= data,
		.cancel_all	= cancel_all,
	};
	int node;

	/*
	 * First check pending list, if we're lucky we can just remove it
	 * from there. CANCEL_OK means that the work is returned as-new,
	 * no completion will be posted for it.
	 */
	for_each_node(node) {
		struct io_wqe *wqe = wq->wqes[node];

		io_wqe_cancel_pending_work(wqe, &match);
		if (match.nr_pending && !match.cancel_all)
			return IO_WQ_CANCEL_OK;
	}

	/*
	 * Now check if a free (going busy) or busy worker has the work
	 * currently running. If we find it there, we'll return CANCEL_RUNNING
	 * as an indication that we attempt to signal cancellation. The
	 * completion will run normally in this case.
	 */
	for_each_node(node) {
		struct io_wqe *wqe = wq->wqes[node];

		io_wqe_cancel_running_work(wqe, &match);
		if (match.nr_running && !match.cancel_all)
			return IO_WQ_CANCEL_RUNNING;
	}

	if (match.nr_running)
		return IO_WQ_CANCEL_RUNNING;
	if (match.nr_pending)
		return IO_WQ_CANCEL_OK;
	return IO_WQ_CANCEL_NOTFOUND;
}

static int io_wqe_hash_wake(struct wait_queue_entry *wait, unsigned mode,
			    int sync, void *key)
{
	struct io_wqe *wqe = container_of(wait, struct io_wqe, wait);
	int i;

	list_del_init(&wait->entry);

	rcu_read_lock();
	for (i = 0; i < IO_WQ_ACCT_NR; i++) {
		struct io_wqe_acct *acct = &wqe->acct[i];

		if (test_and_clear_bit(IO_ACCT_STALLED_BIT, &acct->flags))
			io_wqe_activate_free_worker(wqe, acct);
	}
	rcu_read_unlock();
	return 1;
}

struct io_wq *io_wq_create(unsigned bounded, struct io_wq_data *data)
{
	int ret, node, i;
	struct io_wq *wq;

	if (WARN_ON_ONCE(!data->free_work || !data->do_work))
		return ERR_PTR(-EINVAL);
	if (WARN_ON_ONCE(!bounded))
		return ERR_PTR(-EINVAL);

	wq = kzalloc(struct_size(wq, wqes, nr_node_ids), GFP_KERNEL);
	if (!wq)
		return ERR_PTR(-ENOMEM);
	ret = cpuhp_state_add_instance_nocalls(io_wq_online, &wq->cpuhp_node);
	if (ret)
		goto err_wq;

	refcount_inc(&data->hash->refs);
	wq->hash = data->hash;
	wq->free_work = data->free_work;
	wq->do_work = data->do_work;

	ret = -ENOMEM;
	for_each_node(node) {
		struct io_wqe *wqe;
		int alloc_node = node;

		if (!node_online(alloc_node))
			alloc_node = NUMA_NO_NODE;
		wqe = kzalloc_node(sizeof(struct io_wqe), GFP_KERNEL, alloc_node);
		if (!wqe)
			goto err;
		if (!alloc_cpumask_var(&wqe->cpu_mask, GFP_KERNEL))
			goto err;
		cpumask_copy(wqe->cpu_mask, cpumask_of_node(node));
		wq->wqes[node] = wqe;
		wqe->node = alloc_node;
		wqe->acct[IO_WQ_ACCT_BOUND].max_workers = bounded;
		wqe->acct[IO_WQ_ACCT_UNBOUND].max_workers =
					task_rlimit(current, RLIMIT_NPROC);
		INIT_LIST_HEAD(&wqe->wait.entry);
		wqe->wait.func = io_wqe_hash_wake;
		for (i = 0; i < IO_WQ_ACCT_NR; i++) {
			struct io_wqe_acct *acct = &wqe->acct[i];

			acct->index = i;
			atomic_set(&acct->nr_running, 0);
			INIT_WQ_LIST(&acct->work_list);
		}
		wqe->wq = wq;
		raw_spin_lock_init(&wqe->lock);
		INIT_HLIST_NULLS_HEAD(&wqe->free_list, 0);
		INIT_LIST_HEAD(&wqe->all_list);
	}

	wq->task = get_task_struct(data->task);
	atomic_set(&wq->worker_refs, 1);
	init_completion(&wq->worker_done);
	return wq;
err:
	io_wq_put_hash(data->hash);
	cpuhp_state_remove_instance_nocalls(io_wq_online, &wq->cpuhp_node);
	for_each_node(node) {
		if (!wq->wqes[node])
			continue;
		free_cpumask_var(wq->wqes[node]->cpu_mask);
		kfree(wq->wqes[node]);
	}
err_wq:
	kfree(wq);
	return ERR_PTR(ret);
}

static bool io_task_work_match(struct callback_head *cb, void *data)
{
	struct io_worker *worker;

	if (cb->func != create_worker_cb && cb->func != create_worker_cont)
		return false;
	worker = container_of(cb, struct io_worker, create_work);
	return worker->wqe->wq == data;
}

void io_wq_exit_start(struct io_wq *wq)
{
	set_bit(IO_WQ_BIT_EXIT, &wq->state);
}

static void io_wq_exit_workers(struct io_wq *wq)
{
	struct callback_head *cb;
	int node;

	if (!wq->task)
		return;

	while ((cb = task_work_cancel_match(wq->task, io_task_work_match, wq)) != NULL) {
		struct io_worker *worker;
		struct io_wqe_acct *acct;

		worker = container_of(cb, struct io_worker, create_work);
		acct = io_wqe_get_acct(worker);
		atomic_dec(&acct->nr_running);
		raw_spin_lock(&worker->wqe->lock);
		acct->nr_workers--;
		raw_spin_unlock(&worker->wqe->lock);
		io_worker_ref_put(wq);
		clear_bit_unlock(0, &worker->create_state);
		io_worker_release(worker);
	}

	rcu_read_lock();
	for_each_node(node) {
		struct io_wqe *wqe = wq->wqes[node];

		io_wq_for_each_worker(wqe, io_wq_worker_wake, NULL);
	}
	rcu_read_unlock();
	io_worker_ref_put(wq);
	wait_for_completion(&wq->worker_done);

	for_each_node(node) {
		spin_lock_irq(&wq->hash->wait.lock);
		list_del_init(&wq->wqes[node]->wait.entry);
		spin_unlock_irq(&wq->hash->wait.lock);
	}
	put_task_struct(wq->task);
	wq->task = NULL;
}

static void io_wq_destroy(struct io_wq *wq)
{
	int node;

	cpuhp_state_remove_instance_nocalls(io_wq_online, &wq->cpuhp_node);

	for_each_node(node) {
		struct io_wqe *wqe = wq->wqes[node];
		struct io_cb_cancel_data match = {
			.fn		= io_wq_work_match_all,
			.cancel_all	= true,
		};
		io_wqe_cancel_pending_work(wqe, &match);
		free_cpumask_var(wqe->cpu_mask);
		kfree(wqe);
	}
	io_wq_put_hash(wq->hash);
	kfree(wq);
}

void io_wq_put_and_exit(struct io_wq *wq)
{
	WARN_ON_ONCE(!test_bit(IO_WQ_BIT_EXIT, &wq->state));

	io_wq_exit_workers(wq);
	io_wq_destroy(wq);
}

struct online_data {
	unsigned int cpu;
	bool online;
};

static bool io_wq_worker_affinity(struct io_worker *worker, void *data)
{
	struct online_data *od = data;

	if (od->online)
		cpumask_set_cpu(od->cpu, worker->wqe->cpu_mask);
	else
		cpumask_clear_cpu(od->cpu, worker->wqe->cpu_mask);
	return false;
}

static int __io_wq_cpu_online(struct io_wq *wq, unsigned int cpu, bool online)
{
	struct online_data od = {
		.cpu = cpu,
		.online = online
	};
	int i;

	rcu_read_lock();
	for_each_node(i)
		io_wq_for_each_worker(wq->wqes[i], io_wq_worker_affinity, &od);
	rcu_read_unlock();
	return 0;
}

static int io_wq_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct io_wq *wq = hlist_entry_safe(node, struct io_wq, cpuhp_node);

	return __io_wq_cpu_online(wq, cpu, true);
}

static int io_wq_cpu_offline(unsigned int cpu, struct hlist_node *node)
{
	struct io_wq *wq = hlist_entry_safe(node, struct io_wq, cpuhp_node);

	return __io_wq_cpu_online(wq, cpu, false);
}

int io_wq_cpu_affinity(struct io_wq *wq, cpumask_var_t mask)
{
	int i;

	rcu_read_lock();
	for_each_node(i) {
		struct io_wqe *wqe = wq->wqes[i];

		if (mask)
			cpumask_copy(wqe->cpu_mask, mask);
		else
			cpumask_copy(wqe->cpu_mask, cpumask_of_node(i));
	}
	rcu_read_unlock();
	return 0;
}

/*
 * Set max number of unbounded workers, returns old value. If new_count is 0,
 * then just return the old value.
 */
int io_wq_max_workers(struct io_wq *wq, int *new_count)
{
	int i, node, prev = 0;

	BUILD_BUG_ON((int) IO_WQ_ACCT_BOUND   != (int) IO_WQ_BOUND);
	BUILD_BUG_ON((int) IO_WQ_ACCT_UNBOUND != (int) IO_WQ_UNBOUND);
	BUILD_BUG_ON((int) IO_WQ_ACCT_NR      != 2);

	for (i = 0; i < 2; i++) {
		if (new_count[i] > task_rlimit(current, RLIMIT_NPROC))
			new_count[i] = task_rlimit(current, RLIMIT_NPROC);
	}

	rcu_read_lock();
	for_each_node(node) {
		struct io_wqe_acct *acct;

		for (i = 0; i < IO_WQ_ACCT_NR; i++) {
			acct = &wq->wqes[node]->acct[i];
			prev = max_t(int, acct->max_workers, prev);
			if (new_count[i])
				acct->max_workers = new_count[i];
			new_count[i] = prev;
		}
	}
	rcu_read_unlock();
	return 0;
}

static __init int io_wq_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, "io-wq/online",
					io_wq_cpu_online, io_wq_cpu_offline);
	if (ret < 0)
		return ret;
	io_wq_online = ret;
	return 0;
}
subsys_initcall(io_wq_init);
