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
#include <linux/sched/mm.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/rculist_nulls.h>
#include <linux/fs_struct.h>
#include <linux/task_work.h>

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
	struct nsproxy *restore_nsproxy;
	struct fs_struct *restore_fs;
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
	struct io_wq_work *hash_tail[IO_WQ_NR_HASH_BUCKETS];
};

/*
 * Per io_wq state
  */
struct io_wq {
	struct io_wqe **wqes;
	unsigned long state;

	free_work_fn *free_work;
	io_wq_work_fn *do_work;

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
		current->nsproxy = worker->restore_nsproxy;
		task_unlock(current);
	}

	if (current->fs != worker->restore_fs)
		current->fs = worker->restore_fs;

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
		kthread_unuse_mm(worker->mm);
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
	worker->restore_nsproxy = current->nsproxy;
	worker->restore_fs = current->fs;
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

static inline unsigned int io_get_work_hash(struct io_wq_work *work)
{
	return work->flags >> IO_WQ_HASH_SHIFT;
}

static struct io_wq_work *io_get_next_work(struct io_wqe *wqe)
	__must_hold(wqe->lock)
{
	struct io_wq_work_node *node, *prev;
	struct io_wq_work *work, *tail;
	unsigned int hash;

	wq_list_for_each(node, prev, &wqe->work_list) {
		work = container_of(node, struct io_wq_work, list);

		/* not hashed, can run anytime */
		if (!io_wq_is_hashed(work)) {
			wq_list_del(&wqe->work_list, node, prev);
			return work;
		}

		/* hashed, can run if not already running */
		hash = io_get_work_hash(work);
		if (!(wqe->hash_map & BIT(hash))) {
			wqe->hash_map |= BIT(hash);
			/* all items with this hash lie in [work, tail] */
			tail = wqe->hash_tail[hash];
			wqe->hash_tail[hash] = NULL;
			wq_list_cut(&wqe->work_list, &tail->list, prev);
			return work;
		}
	}

	return NULL;
}

static void io_wq_switch_mm(struct io_worker *worker, struct io_wq_work *work)
{
	if (worker->mm) {
		kthread_unuse_mm(worker->mm);
		mmput(worker->mm);
		worker->mm = NULL;
	}
	if (!work->mm)
		return;

	if (mmget_not_zero(work->mm)) {
		kthread_use_mm(work->mm);
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

static void io_impersonate_work(struct io_worker *worker,
				struct io_wq_work *work)
{
	if (work->files && current->files != work->files) {
		task_lock(current);
		current->files = work->files;
		current->nsproxy = work->nsproxy;
		task_unlock(current);
	}
	if (work->fs && current->fs != work->fs)
		current->fs = work->fs;
	if (work->mm != worker->mm)
		io_wq_switch_mm(worker, work);
	if (worker->cur_creds != work->creds)
		io_wq_switch_creds(worker, work);
	current->signal->rlim[RLIMIT_FSIZE].rlim_cur = work->fsize;
}

static void io_assign_current_work(struct io_worker *worker,
				   struct io_wq_work *work)
{
	if (work) {
		/* flush pending signals before assigning new work */
		if (signal_pending(current))
			flush_signals(current);
		cond_resched();
	}

	spin_lock_irq(&worker->lock);
	worker->cur_work = work;
	spin_unlock_irq(&worker->lock);
}

static void io_wqe_enqueue(struct io_wqe *wqe, struct io_wq_work *work);

static void io_worker_handle_work(struct io_worker *worker)
	__releases(wqe->lock)
{
	struct io_wqe *wqe = worker->wqe;
	struct io_wq *wq = wqe->wq;

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
		work = io_get_next_work(wqe);
		if (work)
			__io_worker_busy(wqe, worker, work);
		else if (!wq_list_empty(&wqe->work_list))
			wqe->flags |= IO_WQE_FLAG_STALLED;

		spin_unlock_irq(&wqe->lock);
		if (!work)
			break;
		io_assign_current_work(worker, work);

		/* handle a whole dependent link */
		do {
			struct io_wq_work *old_work, *next_hashed, *linked;
			unsigned int hash = io_get_work_hash(work);

			next_hashed = wq_next_work(work);
			io_impersonate_work(worker, work);
			/*
			 * OK to set IO_WQ_WORK_CANCEL even for uncancellable
			 * work, the worker function will do the right thing.
			 */
			if (test_bit(IO_WQ_BIT_CANCEL, &wq->state))
				work->flags |= IO_WQ_WORK_CANCEL;

			old_work = work;
			linked = wq->do_work(work);

			work = next_hashed;
			if (!work && linked && !io_wq_is_hashed(linked)) {
				work = linked;
				linked = NULL;
			}
			io_assign_current_work(worker, work);
			wq->free_work(old_work);

			if (linked)
				io_wqe_enqueue(wqe, linked);

			if (hash != -1U && !next_hashed) {
				spin_lock_irq(&wqe->lock);
				wqe->hash_map &= ~BIT_ULL(hash);
				wqe->flags &= ~IO_WQE_FLAG_STALLED;
				/* skip unnecessary unlock-lock wqe->lock */
				if (!work)
					goto get_next;
				spin_unlock_irq(&wqe->lock);
			}
		} while (work);

		spin_lock_irq(&wqe->lock);
	} while (1);
}

static int io_wqe_worker(void *data)
{
	struct io_worker *worker = data;
	struct io_wqe *wqe = worker->wqe;
	struct io_wq *wq = wqe->wq;

	io_worker_start(wqe, worker);

	while (!test_bit(IO_WQ_BIT_EXIT, &wq->state)) {
		set_current_state(TASK_INTERRUPTIBLE);
loop:
		spin_lock_irq(&wqe->lock);
		if (io_wqe_run_queue(wqe)) {
			__set_current_state(TASK_RUNNING);
			io_worker_handle_work(worker);
			goto loop;
		}
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
		if (!node_online(node))
			continue;
		if (!create_io_worker(wq, wq->wqes[node], IO_WQ_ACCT_BOUND))
			goto err;
		workers_to_create--;
	}

	while (workers_to_create--)
		refcount_dec(&wq->refs);

	complete(&wq->done);

	while (!kthread_should_stop()) {
		if (current->task_works)
			task_work_run();

		for_each_node(node) {
			struct io_wqe *wqe = wq->wqes[node];
			bool fork_worker[2] = { false, false };

			if (!node_online(node))
				continue;

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

	if (current->task_works)
		task_work_run();

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

static void io_run_cancel(struct io_wq_work *work, struct io_wqe *wqe)
{
	struct io_wq *wq = wqe->wq;

	do {
		struct io_wq_work *old_work = work;

		work->flags |= IO_WQ_WORK_CANCEL;
		work = wq->do_work(work);
		wq->free_work(old_work);
	} while (work);
}

static void io_wqe_insert_work(struct io_wqe *wqe, struct io_wq_work *work)
{
	unsigned int hash;
	struct io_wq_work *tail;

	if (!io_wq_is_hashed(work)) {
append:
		wq_list_add_tail(&work->list, &wqe->work_list);
		return;
	}

	hash = io_get_work_hash(work);
	tail = wqe->hash_tail[hash];
	wqe->hash_tail[hash] = work;
	if (!tail)
		goto append;

	wq_list_add_after(&work->list, &tail->list, &wqe->work_list);
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
		io_run_cancel(work, wqe);
		return;
	}

	work_flags = work->flags;
	spin_lock_irqsave(&wqe->lock, flags);
	io_wqe_insert_work(wqe, work);
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
 * Work items that hash to the same value will not be done in parallel.
 * Used to limit concurrent writes, generally hashed by inode.
 */
void io_wq_hash_work(struct io_wq_work *work, void *val)
{
	unsigned int bit;

	bit = hash_ptr(val, IO_WQ_HASH_ORDER);
	work->flags |= (IO_WQ_WORK_HASHED | (bit << IO_WQ_HASH_SHIFT));
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
	work_cancel_fn *fn;
	void *data;
	int nr_running;
	int nr_pending;
	bool cancel_all;
};

static bool io_wq_worker_cancel(struct io_worker *worker, void *data)
{
	struct io_cb_cancel_data *match = data;
	unsigned long flags;

	/*
	 * Hold the lock to avoid ->cur_work going out of scope, caller
	 * may dereference the passed in work.
	 */
	spin_lock_irqsave(&worker->lock, flags);
	if (worker->cur_work &&
	    !(worker->cur_work->flags & IO_WQ_WORK_NO_CANCEL) &&
	    match->fn(worker->cur_work, match->data)) {
		send_sig(SIGINT, worker->task, 1);
		match->nr_running++;
	}
	spin_unlock_irqrestore(&worker->lock, flags);

	return match->nr_running && !match->cancel_all;
}

static inline void io_wqe_remove_pending(struct io_wqe *wqe,
					 struct io_wq_work *work,
					 struct io_wq_work_node *prev)
{
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
	wq_list_del(&wqe->work_list, &work->list, prev);
}

static void io_wqe_cancel_pending_work(struct io_wqe *wqe,
				       struct io_cb_cancel_data *match)
{
	struct io_wq_work_node *node, *prev;
	struct io_wq_work *work;
	unsigned long flags;

retry:
	spin_lock_irqsave(&wqe->lock, flags);
	wq_list_for_each(node, prev, &wqe->work_list) {
		work = container_of(node, struct io_wq_work, list);
		if (!match->fn(work, match->data))
			continue;
		io_wqe_remove_pending(wqe, work, prev);
		spin_unlock_irqrestore(&wqe->lock, flags);
		io_run_cancel(work, wqe);
		match->nr_pending++;
		if (!match->cancel_all)
			return;

		/* not safe to continue after unlock */
		goto retry;
	}
	spin_unlock_irqrestore(&wqe->lock, flags);
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

static bool io_wq_io_cb_cancel_data(struct io_wq_work *work, void *data)
{
	return work == data;
}

enum io_wq_cancel io_wq_cancel_work(struct io_wq *wq, struct io_wq_work *cwork)
{
	return io_wq_cancel_cb(wq, io_wq_io_cb_cancel_data, (void *)cwork, false);
}

struct io_wq *io_wq_create(unsigned bounded, struct io_wq_data *data)
{
	int ret = -ENOMEM, node;
	struct io_wq *wq;

	if (WARN_ON_ONCE(!data->free_work || !data->do_work))
		return ERR_PTR(-EINVAL);

	wq = kzalloc(sizeof(*wq), GFP_KERNEL);
	if (!wq)
		return ERR_PTR(-ENOMEM);

	wq->wqes = kcalloc(nr_node_ids, sizeof(struct io_wqe *), GFP_KERNEL);
	if (!wq->wqes) {
		kfree(wq);
		return ERR_PTR(-ENOMEM);
	}

	wq->free_work = data->free_work;
	wq->do_work = data->do_work;

	/* caller must already hold a reference to this */
	wq->user = data->user;

	for_each_node(node) {
		struct io_wqe *wqe;
		int alloc_node = node;

		if (!node_online(alloc_node))
			alloc_node = NUMA_NO_NODE;
		wqe = kzalloc_node(sizeof(struct io_wqe), GFP_KERNEL, alloc_node);
		if (!wqe)
			goto err;
		wq->wqes[node] = wqe;
		wqe->node = alloc_node;
		wqe->acct[IO_WQ_ACCT_BOUND].max_workers = bounded;
		atomic_set(&wqe->acct[IO_WQ_ACCT_BOUND].nr_running, 0);
		if (wq->user) {
			wqe->acct[IO_WQ_ACCT_UNBOUND].max_workers =
					task_rlimit(current, RLIMIT_NPROC);
		}
		atomic_set(&wqe->acct[IO_WQ_ACCT_UNBOUND].nr_running, 0);
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
	if (data->free_work != wq->free_work || data->do_work != wq->do_work)
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

struct task_struct *io_wq_get_task(struct io_wq *wq)
{
	return wq->manager;
}
