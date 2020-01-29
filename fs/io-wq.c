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
	IO_WORKER_F_BOUND	= 32,	/* is doing bounded work */
};

enum {
	IO_WQ_BIT_EXIT		= 0,	/* wq exiting */
	IO_WQ_BIT_CANCEL	= 1,	/* cancel work on list */
	IO_WQ_BIT_ERROR		= 2,	/* error on setup */
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
	struct list_head all_list;
	struct task_struct *task;
	struct io_wqe *wqe;

	struct io_wq_work *cur_work;
	spinlock_t lock;

	struct rcu_head rcu;
	struct mm_struct *mm;
	const struct cred *cur_creds;
	const struct cred *saved_creds;
	struct files_struct *restore_files;
};

#if BITS_PER_LONG == 64
#define IO_WQ_HASH_ORDER	6
#else
#define IO_WQ_HASH_ORDER	5
#endif

struct io_wqe_acct {
	unsigned nr_workers;
	unsigned max_workers;
	atomic_t nr_running;
};

enum {
	IO_WQ_ACCT_BOUND,
	IO_WQ_ACCT_UNBOUND,
};

/*
 * Per-node worker thread pool
 */
struct io_wqe {
	struct {
		spinlock_t lock;
		struct io_wq_work_list work_list;
		unsigned long hash_map;
		unsigned flags;
	} ____cacheline_aligned_in_smp;

	int node;
	struct io_wqe_acct acct[2];

	struct hlist_nulls_head free_list;
	struct list_head all_list;

	struct io_wq *wq;
};

/*
 * Per io_wq state
  */
struct io_wq {
	struct io_wqe **wqes;
	unsigned long state;

	get_work_fn *get_work;
	put_work_fn *put_work;

	struct task_struct *manager;
	struct user_struct *user;
	refcount_t refs;
	struct completion done;

	refcount_t use_refs;
};

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

	if (worker->saved_creds) {
		revert_creds(worker->saved_creds);
		worker->cur_creds = worker->saved_creds = NULL;
	}

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

static inline struct io_wqe_acct *io_work_get_acct(struct io_wqe *wqe,
						   struct io_wq_work *work)
{
	if (work->flags & IO_WQ_WORK_UNBOUND)
		return &wqe->acct[IO_WQ_ACCT_UNBOUND];

	return &wqe->acct[IO_WQ_ACCT_BOUND];
}

static inline struct io_wqe_acct *io_wqe_get_acct(struct io_wqe *wqe,
						  struct io_worker *worker)
{
	if (worker->flags & IO_WORKER_F_BOUND)
		return &wqe->acct[IO_WQ_ACCT_BOUND];

	return &wqe->acct[IO_WQ_ACCT_UNBOUND];
}

static void io_worker_exit(struct io_worker *worker)
{
	struct io_wqe *wqe = worker->wqe;
	struct io_wqe_acct *acct = io_wqe_get_acct(wqe, worker);
	unsigned nr_workers;

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
		atomic_dec(&acct->nr_running);
	if (!(worker->flags & IO_WORKER_F_BOUND))
		atomic_dec(&wqe->wq->user->processes);
	worker->flags = 0;
	preempt_enable();

	spin_lock_irq(&wqe->lock);
	hlist_nulls_del_rcu(&worker->nulls_node);
	list_del_rcu(&worker->all_list);
	if (__io_worker_unuse(wqe, worker)) {
		__release(&wqe->lock);
		spin_lock_irq(&wqe->lock);
	}
	acct->nr_workers--;
	nr_workers = wqe->acct[IO_WQ_ACCT_BOUND].nr_workers +
			wqe->acct[IO_WQ_ACCT_UNBOUND].nr_workers;
	spin_unlock_irq(&wqe->lock);

	/* all workers gone, wq exit can proceed */
	if (!nr_workers && refcount_dec_and_test(&wqe->wq->refs))
		complete(&wqe->wq->done);

	kfree_rcu(worker, rcu);
}

static inline bool io_wqe_run_queue(struct io_wqe *wqe)
	__must_hold(wqe->lock)
{
	if (!wq_list_empty(&wqe->work_list) &&
	    !(wqe->flags & IO_WQE_FLAG_STALLED))
		return true;
	return false;
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

	n = rcu_dereference(hlist_nulls_first_rcu(&wqe->free_list));
	if (is_a_nulls(n))
		return false;

	worker = hlist_nulls_entry(n, struct io_worker, nulls_node);
	if (io_worker_get(worker)) {
		wake_up_process(worker->task);
		io_worker_release(worker);
		return true;
	}

	return false;
}

/*
 * We need a worker. If we find a free one, we're good. If not, and we're
 * below the max number of workers, wake up the manager to create one.
 */
static void io_wqe_wake_worker(struct io_wqe *wqe, struct io_wqe_acct *acct)
{
	bool ret;

	/*
	 * Most likely an attempt to queue unbounded work on an io_wq that
	 * wasn't setup with any unbounded workers.
	 */
	WARN_ON_ONCE(!acct->max_workers);

	rcu_read_lock();
	ret = io_wqe_activate_free_worker(wqe);
	rcu_read_unlock();

	if (!ret && acct->nr_workers < acct->max_workers)
		wake_up_process(wqe->wq->manager);
}

static void io_wqe_inc_running(struct io_wqe *wqe, struct io_worker *worker)
{
	struct io_wqe_acct *acct = io_wqe_get_acct(wqe, worker);

	atomic_inc(&acct->nr_running);
}

static void io_wqe_dec_running(struct io_wqe *wqe, struct io_worker *worker)
	__must_hold(wqe->lock)
{
	struct io_wqe_acct *acct = io_wqe_get_acct(wqe, worker);

	if (atomic_dec_and_test(&acct->nr_running) && io_wqe_run_queue(wqe))
		io_wqe_wake_worker(wqe, acct);
}

static void io_worker_start(struct io_wqe *wqe, struct io_worker *worker)
{
	allow_kernel_signal(SIGINT);

	current->flags |= PF_IO_WORKER;

	worker->flags |= (IO_WORKER_F_UP | IO_WORKER_F_RUNNING);
	worker->restore_files = current->files;
	io_wqe_inc_running(wqe, worker);
}

/*
 * Worker will start processing some work. Move it to the busy list, if
 * it's currently on the freelist
 */
static void __io_worker_busy(struct io_wqe *wqe, struct io_worker *worker,
			     struct io_wq_work *work)
	__must_hold(wqe->lock)
{
	bool worker_bound, work_bound;

	if (worker->flags & IO_WORKER_F_FREE) {
		worker->flags &= ~IO_WORKER_F_FREE;
		hlist_nulls_del_init_rcu(&worker->nulls_node);
	}

	/*
	 * If worker is moving from bound to unbound (or vice versa), then
	 * ensure we update the running accounting.
	 */
	worker_bound = (worker->flags & IO_WORKER_F_BOUND) != 0;
	work_bound = (work->flags & IO_WQ_WORK_UNBOUND) == 0;
	if (worker_bound != work_bound) {
		io_wqe_dec_running(wqe, worker);
		if (work_bound) {
			worker->flags |= IO_WORKER_F_BOUND;
			wqe->acct[IO_WQ_ACCT_UNBOUND].nr_workers--;
			wqe->acct[IO_WQ_ACCT_BOUND].nr_workers++;
			atomic_dec(&wqe->wq->user->processes);
		} else {
			worker->flags &= ~IO_WORKER_F_BOUND;
			wqe->acct[IO_WQ_ACCT_UNBOUND].nr_workers++;
			wqe->acct[IO_WQ_ACCT_BOUND].nr_workers--;
			atomic_inc(&wqe->wq->user->processes);
		}
		io_wqe_inc_running(wqe, worker);
	 }
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
		hlist_nulls_add_head_rcu(&worker->nulls_node, &wqe->free_list);
	}

	return __io_worker_unuse(wqe, worker);
}

static struct io_wq_work *io_get_next_work(struct io_wqe *wqe, unsigned *hash)
	__must_hold(wqe->lock)
{
	struct io_wq_work_node *node, *prev;
	struct io_wq_work *work;

	wq_list_for_each(node, prev, &wqe->work_list) {
		work = container_of(node, struct io_wq_work, list);

		/* not hashed, can run anytime */
		if (!(work->flags & IO_WQ_WORK_HASHED)) {
			wq_node_del(&wqe->work_list, node, prev);
			return work;
		}

		/* hashed, can run if not already running */
		*hash = work->flags >> IO_WQ_HASH_SHIFT;
		if (!(wqe->hash_map & BIT_ULL(*hash))) {
			wqe->hash_map |= BIT_ULL(*hash);
			wq_node_del(&wqe->work_list, node, prev);
			return work;
		}
	}

	return NULL;
}

static void io_wq_switch_mm(struct io_worker *worker, struct io_wq_work *work)
{
	if (worker->mm) {
		unuse_mm(worker->mm);
		mmput(worker->mm);
		worker->mm = NULL;
	}
	if (!work->mm) {
		set_fs(KERNEL_DS);
		return;
	}
	if (mmget_not_zero(work->mm)) {
		use_mm(work->mm);
		if (!worker->mm)
			set_fs(USER_DS);
		worker->mm = work->mm;
		/* hang on to this mm */
		work->mm = NULL;
		return;
	}

	/* failed grabbing mm, ensure work gets cancelled */
	work->flags |= IO_WQ_WORK_CANCEL;
}

static void io_wq_switch_creds(struct io_worker *worker,
			       struct io_wq_work *work)
{
	const struct cred *old_creds = override_creds(work->creds);

	worker->cur_creds = work->creds;
	if (worker->saved_creds)
		put_cred(old_creds); /* creds set by previous switch */
	else
		worker->saved_creds = old_creds;
}

static void io_worker_handle_work(struct io_worker *worker)
	__releases(wqe->lock)
{
	struct io_wq_work *work, *old_work = NULL, *put_work = NULL;
	struct io_wqe *wqe = worker->wqe;
	struct io_wq *wq = wqe->wq;

	do {
		unsigned hash = -1U;

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
		else if (!wq_list_empty(&wqe->work_list))
			wqe->flags |= IO_WQE_FLAG_STALLED;

		spin_unlock_irq(&wqe->lock);
		if (put_work && wq->put_work)
			wq->put_work(old_work);
		if (!work)
			break;
next:
		/* flush any pending signals before assigning new work */
		if (signal_pending(current))
			flush_signals(current);

		cond_resched();

		spin_lock_irq(&worker->lock);
		worker->cur_work = work;
		spin_unlock_irq(&worker->lock);

		if (work->flags & IO_WQ_WORK_CB)
			work->func(&work);

		if (work->files && current->files != work->files) {
			task_lock(current);
			current->files = work->files;
			task_unlock(current);
		}
		if (work->mm != worker->mm)
			io_wq_switch_mm(worker, work);
		if (worker->cur_creds != work->creds)
			io_wq_switch_creds(worker, work);
		/*
		 * OK to set IO_WQ_WORK_CANCEL even for uncancellable work,
		 * the worker function will do the right thing.
		 */
		if (test_bit(IO_WQ_BIT_CANCEL, &wq->state))
			work->flags |= IO_WQ_WORK_CANCEL;
		if (worker->mm)
			work->flags |= IO_WQ_WORK_HAS_MM;

		if (wq->get_work && !(work->flags & IO_WQ_WORK_INTERNAL)) {
			put_work = work;
			wq->get_work(work);
		}

		old_work = work;
		work->func(&work);

		spin_lock_irq(&worker->lock);
		worker->cur_work = NULL;
		spin_unlock_irq(&worker->lock);

		spin_lock_irq(&wqe->lock);

		if (hash != -1U) {
			wqe->hash_map &= ~BIT_ULL(hash);
			wqe->flags &= ~IO_WQE_FLAG_STALLED;
		}
		if (work && work != old_work) {
			spin_unlock_irq(&wqe->lock);

			if (put_work && wq->put_work) {
				wq->put_work(put_work);
				put_work = NULL;
			}

			/* dependent work not hashed */
			hash = -1U;
			goto next;
		}
	} while (1);
}

static inline void io_worker_spin_for_work(struct io_wqe *wqe)
{
	int i = 0;

	while (++i < 1000) {
		if (io_wqe_run_queue(wqe))
			break;
		if (need_resched())
			break;
		cpu_relax();
	}
}

static int io_wqe_worker(void *data)
{
	struct io_worker *worker = data;
	struct io_wqe *wqe = worker->wqe;
	struct io_wq *wq = wqe->wq;
	bool did_work;

	io_worker_start(wqe, worker);

	did_work = false;
	while (!test_bit(IO_WQ_BIT_EXIT, &wq->state)) {
		set_current_state(TASK_INTERRUPTIBLE);
loop:
		if (did_work)
			io_worker_spin_for_work(wqe);
		spin_lock_irq(&wqe->lock);
		if (io_wqe_run_queue(wqe)) {
			__set_current_state(TASK_RUNNING);
			io_worker_handle_work(worker);
			did_work = true;
			goto loop;
		}
		did_work = false;
		/* drops the lock on success, retry */
		if (__io_worker_idle(wqe, worker)) {
			__release(&wqe->lock);
			goto loop;
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

	if (test_bit(IO_WQ_BIT_EXIT, &wq->state)) {
		spin_lock_irq(&wqe->lock);
		if (!wq_list_empty(&wqe->work_list))
			io_worker_handle_work(worker);
		else
			spin_unlock_irq(&wqe->lock);
	}

	io_worker_exit(worker);
	return 0;
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
	io_wqe_inc_running(wqe, worker);
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
	io_wqe_dec_running(wqe, worker);
	spin_unlock_irq(&wqe->lock);
}

static bool create_io_worker(struct io_wq *wq, struct io_wqe *wqe, int index)
{
	struct io_wqe_acct *acct =&wqe->acct[index];
	struct io_worker *worker;

	worker = kzalloc_node(sizeof(*worker), GFP_KERNEL, wqe->node);
	if (!worker)
		return false;

	refcount_set(&worker->ref, 1);
	worker->nulls_node.pprev = NULL;
	worker->wqe = wqe;
	spin_lock_init(&worker->lock);

	worker->task = kthread_create_on_node(io_wqe_worker, worker, wqe->node,
				"io_wqe_worker-%d/%d", index, wqe->node);
	if (IS_ERR(worker->task)) {
		kfree(worker);
		return false;
	}

	spin_lock_irq(&wqe->lock);
	hlist_nulls_add_head_rcu(&worker->nulls_node, &wqe->free_list);
	list_add_tail_rcu(&worker->all_list, &wqe->all_list);
	worker->flags |= IO_WORKER_F_FREE;
	if (index == IO_WQ_ACCT_BOUND)
		worker->flags |= IO_WORKER_F_BOUND;
	if (!acct->nr_workers && (worker->flags & IO_WORKER_F_BOUND))
		worker->flags |= IO_WORKER_F_FIXED;
	acct->nr_workers++;
	spin_unlock_irq(&wqe->lock);

	if (index == IO_WQ_ACCT_UNBOUND)
		atomic_inc(&wq->user->processes);

	wake_up_process(worker->task);
	return true;
}

static inline bool io_wqe_need_worker(struct io_wqe *wqe, int index)
	__must_hold(wqe->lock)
{
	struct io_wqe_acct *acct = &wqe->acct[index];

	/* if we have available workers or no work, no need */
	if (!hlist_nulls_empty(&wqe->free_list) || !io_wqe_run_queue(wqe))
		return false;
	return acct->nr_workers < acct->max_workers;
}

/*
 * Manager thread. Tasked with creating new workers, if we need them.
 */
static int io_wq_manager(void *data)
{
	struct io_wq *wq = data;
	int workers_to_create = num_possible_nodes();
	int node;

	/* create fixed workers */
	refcount_set(&wq->refs, workers_to_create);
	for_each_node(node) {
		if (!create_io_worker(wq, wq->wqes[node], IO_WQ_ACCT_BOUND))
			goto err;
		workers_to_create--;
	}

	complete(&wq->done);

	while (!kthread_should_stop()) {
		for_each_node(node) {
			struct io_wqe *wqe = wq->wqes[node];
			bool fork_worker[2] = { false, false };

			spin_lock_irq(&wqe->lock);
			if (io_wqe_need_worker(wqe, IO_WQ_ACCT_BOUND))
				fork_worker[IO_WQ_ACCT_BOUND] = true;
			if (io_wqe_need_worker(wqe, IO_WQ_ACCT_UNBOUND))
				fork_worker[IO_WQ_ACCT_UNBOUND] = true;
			spin_unlock_irq(&wqe->lock);
			if (fork_worker[IO_WQ_ACCT_BOUND])
				create_io_worker(wq, wqe, IO_WQ_ACCT_BOUND);
			if (fork_worker[IO_WQ_ACCT_UNBOUND])
				create_io_worker(wq, wqe, IO_WQ_ACCT_UNBOUND);
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}

	return 0;
err:
	set_bit(IO_WQ_BIT_ERROR, &wq->state);
	set_bit(IO_WQ_BIT_EXIT, &wq->state);
	if (refcount_sub_and_test(workers_to_create, &wq->refs))
		complete(&wq->done);
	return 0;
}

static bool io_wq_can_queue(struct io_wqe *wqe, struct io_wqe_acct *acct,
			    struct io_wq_work *work)
{
	bool free_worker;

	if (!(work->flags & IO_WQ_WORK_UNBOUND))
		return true;
	if (atomic_read(&acct->nr_running))
		return true;

	rcu_read_lock();
	free_worker = !hlist_nulls_empty(&wqe->free_list);
	rcu_read_unlock();
	if (free_worker)
		return true;

	if (atomic_read(&wqe->wq->user->processes) >= acct->max_workers &&
	    !(capable(CAP_SYS_RESOURCE) || capable(CAP_SYS_ADMIN)))
		return false;

	return true;
}

static void io_wqe_enqueue(struct io_wqe *wqe, struct io_wq_work *work)
{
	struct io_wqe_acct *acct = io_work_get_acct(wqe, work);
	int work_flags;
	unsigned long flags;

	/*
	 * Do early check to see if we need a new unbound worker, and if we do,
	 * if we're allowed to do so. This isn't 100% accurate as there's a
	 * gap between this check and incrementing the value, but that's OK.
	 * It's close enough to not be an issue, fork() has the same delay.
	 */
	if (unlikely(!io_wq_can_queue(wqe, acct, work))) {
		work->flags |= IO_WQ_WORK_CANCEL;
		work->func(&work);
		return;
	}

	work_flags = work->flags;
	spin_lock_irqsave(&wqe->lock, flags);
	wq_list_add_tail(&work->list, &wqe->work_list);
	wqe->flags &= ~IO_WQE_FLAG_STALLED;
	spin_unlock_irqrestore(&wqe->lock, flags);

	if ((work_flags & IO_WQ_WORK_CONCURRENT) ||
	    !atomic_read(&acct->nr_running))
		io_wqe_wake_worker(wqe, acct);
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
				  bool (*func)(struct io_worker *, void *),
				  void *data)
{
	struct io_worker *worker;
	bool ret = false;

	list_for_each_entry_rcu(worker, &wqe->all_list, all_list) {
		if (io_worker_get(worker)) {
			ret = func(worker, data);
			io_worker_release(worker);
			if (ret)
				break;
		}
	}

	return ret;
}

void io_wq_cancel_all(struct io_wq *wq)
{
	int node;

	set_bit(IO_WQ_BIT_CANCEL, &wq->state);

	rcu_read_lock();
	for_each_node(node) {
		struct io_wqe *wqe = wq->wqes[node];

		io_wq_for_each_worker(wqe, io_wqe_worker_send_sig, NULL);
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
	unsigned long flags;
	bool ret = false;

	/*
	 * Hold the lock to avoid ->cur_work going out of scope, caller
	 * may dereference the passed in work.
	 */
	spin_lock_irqsave(&worker->lock, flags);
	if (worker->cur_work &&
	    !(worker->cur_work->flags & IO_WQ_WORK_NO_CANCEL) &&
	    data->cancel(worker->cur_work, data->caller_data)) {
		send_sig(SIGINT, worker->task, 1);
		ret = true;
	}
	spin_unlock_irqrestore(&worker->lock, flags);

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
	struct io_wq_work_node *node, *prev;
	struct io_wq_work *work;
	unsigned long flags;
	bool found = false;

	spin_lock_irqsave(&wqe->lock, flags);
	wq_list_for_each(node, prev, &wqe->work_list) {
		work = container_of(node, struct io_wq_work, list);

		if (cancel(work, cancel_data)) {
			wq_node_del(&wqe->work_list, node, prev);
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&wqe->lock, flags);

	if (found) {
		work->flags |= IO_WQ_WORK_CANCEL;
		work->func(&work);
		return IO_WQ_CANCEL_OK;
	}

	rcu_read_lock();
	found = io_wq_for_each_worker(wqe, io_work_cancel, &data);
	rcu_read_unlock();
	return found ? IO_WQ_CANCEL_RUNNING : IO_WQ_CANCEL_NOTFOUND;
}

enum io_wq_cancel io_wq_cancel_cb(struct io_wq *wq, work_cancel_fn *cancel,
				  void *data)
{
	enum io_wq_cancel ret = IO_WQ_CANCEL_NOTFOUND;
	int node;

	for_each_node(node) {
		struct io_wqe *wqe = wq->wqes[node];

		ret = io_wqe_cancel_cb_work(wqe, cancel, data);
		if (ret != IO_WQ_CANCEL_NOTFOUND)
			break;
	}

	return ret;
}

static bool io_wq_worker_cancel(struct io_worker *worker, void *data)
{
	struct io_wq_work *work = data;
	unsigned long flags;
	bool ret = false;

	if (worker->cur_work != work)
		return false;

	spin_lock_irqsave(&worker->lock, flags);
	if (worker->cur_work == work &&
	    !(worker->cur_work->flags & IO_WQ_WORK_NO_CANCEL)) {
		send_sig(SIGINT, worker->task, 1);
		ret = true;
	}
	spin_unlock_irqrestore(&worker->lock, flags);

	return ret;
}

static enum io_wq_cancel io_wqe_cancel_work(struct io_wqe *wqe,
					    struct io_wq_work *cwork)
{
	struct io_wq_work_node *node, *prev;
	struct io_wq_work *work;
	unsigned long flags;
	bool found = false;

	cwork->flags |= IO_WQ_WORK_CANCEL;

	/*
	 * First check pending list, if we're lucky we can just remove it
	 * from there. CANCEL_OK means that the work is returned as-new,
	 * no completion will be posted for it.
	 */
	spin_lock_irqsave(&wqe->lock, flags);
	wq_list_for_each(node, prev, &wqe->work_list) {
		work = container_of(node, struct io_wq_work, list);

		if (work == cwork) {
			wq_node_del(&wqe->work_list, node, prev);
			found = true;
			break;
		}
	}
	spin_unlock_irqrestore(&wqe->lock, flags);

	if (found) {
		work->flags |= IO_WQ_WORK_CANCEL;
		work->func(&work);
		return IO_WQ_CANCEL_OK;
	}

	/*
	 * Now check if a free (going busy) or busy worker has the work
	 * currently running. If we find it there, we'll return CANCEL_RUNNING
	 * as an indication that we attempt to signal cancellation. The
	 * completion will run normally in this case.
	 */
	rcu_read_lock();
	found = io_wq_for_each_worker(wqe, io_wq_worker_cancel, cwork);
	rcu_read_unlock();
	return found ? IO_WQ_CANCEL_RUNNING : IO_WQ_CANCEL_NOTFOUND;
}

enum io_wq_cancel io_wq_cancel_work(struct io_wq *wq, struct io_wq_work *cwork)
{
	enum io_wq_cancel ret = IO_WQ_CANCEL_NOTFOUND;
	int node;

	for_each_node(node) {
		struct io_wqe *wqe = wq->wqes[node];

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
	int node;

	for_each_node(node) {
		struct io_wqe *wqe = wq->wqes[node];

		init_completion(&data.done);
		INIT_IO_WORK(&data.work, io_wq_flush_func);
		data.work.flags |= IO_WQ_WORK_INTERNAL;
		io_wqe_enqueue(wqe, &data.work);
		wait_for_completion(&data.done);
	}
}

struct io_wq *io_wq_create(unsigned bounded, struct io_wq_data *data)
{
	int ret = -ENOMEM, node;
	struct io_wq *wq;

	wq = kzalloc(sizeof(*wq), GFP_KERNEL);
	if (!wq)
		return ERR_PTR(-ENOMEM);

	wq->wqes = kcalloc(nr_node_ids, sizeof(struct io_wqe *), GFP_KERNEL);
	if (!wq->wqes) {
		kfree(wq);
		return ERR_PTR(-ENOMEM);
	}

	wq->get_work = data->get_work;
	wq->put_work = data->put_work;

	/* caller must already hold a reference to this */
	wq->user = data->user;

	for_each_node(node) {
		struct io_wqe *wqe;

		wqe = kzalloc_node(sizeof(struct io_wqe), GFP_KERNEL, node);
		if (!wqe)
			goto err;
		wq->wqes[node] = wqe;
		wqe->node = node;
		wqe->acct[IO_WQ_ACCT_BOUND].max_workers = bounded;
		atomic_set(&wqe->acct[IO_WQ_ACCT_BOUND].nr_running, 0);
		if (wq->user) {
			wqe->acct[IO_WQ_ACCT_UNBOUND].max_workers =
					task_rlimit(current, RLIMIT_NPROC);
		}
		atomic_set(&wqe->acct[IO_WQ_ACCT_UNBOUND].nr_running, 0);
		wqe->node = node;
		wqe->wq = wq;
		spin_lock_init(&wqe->lock);
		INIT_WQ_LIST(&wqe->work_list);
		INIT_HLIST_NULLS_HEAD(&wqe->free_list, 0);
		INIT_LIST_HEAD(&wqe->all_list);
	}

	init_completion(&wq->done);

	wq->manager = kthread_create(io_wq_manager, wq, "io_wq_manager");
	if (!IS_ERR(wq->manager)) {
		wake_up_process(wq->manager);
		wait_for_completion(&wq->done);
		if (test_bit(IO_WQ_BIT_ERROR, &wq->state)) {
			ret = -ENOMEM;
			goto err;
		}
		refcount_set(&wq->use_refs, 1);
		reinit_completion(&wq->done);
		return wq;
	}

	ret = PTR_ERR(wq->manager);
	complete(&wq->done);
err:
	for_each_node(node)
		kfree(wq->wqes[node]);
	kfree(wq->wqes);
	kfree(wq);
	return ERR_PTR(ret);
}

bool io_wq_get(struct io_wq *wq, struct io_wq_data *data)
{
	if (data->get_work != wq->get_work || data->put_work != wq->put_work)
		return false;

	return refcount_inc_not_zero(&wq->use_refs);
}

static bool io_wq_worker_wake(struct io_worker *worker, void *data)
{
	wake_up_process(worker->task);
	return false;
}

static void __io_wq_destroy(struct io_wq *wq)
{
	int node;

	set_bit(IO_WQ_BIT_EXIT, &wq->state);
	if (wq->manager)
		kthread_stop(wq->manager);

	rcu_read_lock();
	for_each_node(node)
		io_wq_for_each_worker(wq->wqes[node], io_wq_worker_wake, NULL);
	rcu_read_unlock();

	wait_for_completion(&wq->done);

	for_each_node(node)
		kfree(wq->wqes[node]);
	kfree(wq->wqes);
	kfree(wq);
}

void io_wq_destroy(struct io_wq *wq)
{
	if (refcount_dec_and_test(&wq->use_refs))
		__io_wq_destroy(wq);
}
