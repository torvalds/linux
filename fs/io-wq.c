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
#include <linux/rculist_nulls.h>
#include <linux/cpu.h>
#include <linux/tracehook.h>

#include "../kernel/sched/sched.h"
#include "io-wq.h"

#define WORKER_IDLE_TIMEOUT	(5 * HZ)

enum {
	IO_WORKER_F_UP		= 1,	/* up and active */
	IO_WORKER_F_RUNNING	= 2,	/* account as running */
	IO_WORKER_F_FREE	= 4,	/* worker on free list */
	IO_WORKER_F_FIXED	= 8,	/* static idle worker */
	IO_WORKER_F_BOUND	= 16,	/* is doing bounded work */
};

enum {
	IO_WQ_BIT_EXIT		= 0,	/* wq exiting */
	IO_WQ_BIT_ERROR		= 1,	/* error on setup */
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

	const struct cred *cur_creds;
	const struct cred *saved_creds;

	struct rcu_head rcu;
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
		raw_spinlock_t lock;
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

	struct hlist_node cpuhp_node;

	pid_t task_pid;
};

static enum cpuhp_state io_wq_online;

static bool io_worker_get(struct io_worker *worker)
{
	return refcount_inc_not_zero(&worker->ref);
}

static void io_worker_release(struct io_worker *worker)
{
	if (refcount_dec_and_test(&worker->ref))
		wake_up_process(worker->task);
}

static inline struct io_wqe_acct *io_work_get_acct(struct io_wqe *wqe,
						   struct io_wq_work *work)
{
	if (work->flags & IO_WQ_WORK_UNBOUND)
		return &wqe->acct[IO_WQ_ACCT_UNBOUND];

	return &wqe->acct[IO_WQ_ACCT_BOUND];
}

static inline struct io_wqe_acct *io_wqe_get_acct(struct io_worker *worker)
{
	struct io_wqe *wqe = worker->wqe;

	if (worker->flags & IO_WORKER_F_BOUND)
		return &wqe->acct[IO_WQ_ACCT_BOUND];

	return &wqe->acct[IO_WQ_ACCT_UNBOUND];
}

static void io_worker_exit(struct io_worker *worker)
{
	struct io_wqe *wqe = worker->wqe;
	struct io_wqe_acct *acct = io_wqe_get_acct(worker);
	unsigned flags;

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
	flags = worker->flags;
	worker->flags = 0;
	if (flags & IO_WORKER_F_RUNNING)
		atomic_dec(&acct->nr_running);
	if (!(flags & IO_WORKER_F_BOUND))
		atomic_dec(&wqe->wq->user->processes);
	worker->flags = 0;
	preempt_enable();

	if (worker->saved_creds) {
		revert_creds(worker->saved_creds);
		worker->cur_creds = worker->saved_creds = NULL;
	}

	raw_spin_lock_irq(&wqe->lock);
	if (flags & IO_WORKER_F_FREE)
		hlist_nulls_del_rcu(&worker->nulls_node);
	list_del_rcu(&worker->all_list);
	acct->nr_workers--;
	raw_spin_unlock_irq(&wqe->lock);

	kfree_rcu(worker, rcu);
	if (refcount_dec_and_test(&wqe->wq->refs))
		complete(&wqe->wq->done);
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

static void io_wqe_inc_running(struct io_worker *worker)
{
	struct io_wqe_acct *acct = io_wqe_get_acct(worker);

	atomic_inc(&acct->nr_running);
}

static void io_wqe_dec_running(struct io_worker *worker)
	__must_hold(wqe->lock)
{
	struct io_wqe_acct *acct = io_wqe_get_acct(worker);
	struct io_wqe *wqe = worker->wqe;

	if (atomic_dec_and_test(&acct->nr_running) && io_wqe_run_queue(wqe))
		io_wqe_wake_worker(wqe, acct);
}

static void io_worker_start(struct io_worker *worker)
{
	worker->flags |= (IO_WORKER_F_UP | IO_WORKER_F_RUNNING);
	io_wqe_inc_running(worker);
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
		io_wqe_dec_running(worker);
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
		io_wqe_inc_running(worker);
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
	if (worker->saved_creds) {
		revert_creds(worker->saved_creds);
		worker->cur_creds = worker->saved_creds = NULL;
	}
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

static void io_flush_signals(void)
{
	if (unlikely(test_tsk_thread_flag(current, TIF_NOTIFY_SIGNAL))) {
		if (current->task_works)
			task_work_run();
		clear_tsk_thread_flag(current, TIF_NOTIFY_SIGNAL);
	}
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

static void io_assign_current_work(struct io_worker *worker,
				   struct io_wq_work *work)
{
	if (work) {
		io_flush_signals();
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

		raw_spin_unlock_irq(&wqe->lock);
		if (!work)
			break;
		io_assign_current_work(worker, work);

		/* handle a whole dependent link */
		do {
			struct io_wq_work *next_hashed, *linked;
			unsigned int hash = io_get_work_hash(work);

			next_hashed = wq_next_work(work);
			if (work->creds && worker->cur_creds != work->creds)
				io_wq_switch_creds(worker, work);
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
				raw_spin_lock_irq(&wqe->lock);
				wqe->hash_map &= ~BIT_ULL(hash);
				wqe->flags &= ~IO_WQE_FLAG_STALLED;
				/* skip unnecessary unlock-lock wqe->lock */
				if (!work)
					goto get_next;
				raw_spin_unlock_irq(&wqe->lock);
			}
		} while (work);

		raw_spin_lock_irq(&wqe->lock);
	} while (1);
}

static int io_wqe_worker(void *data)
{
	struct io_worker *worker = data;
	struct io_wqe *wqe = worker->wqe;
	struct io_wq *wq = wqe->wq;

	io_worker_start(worker);

	while (!test_bit(IO_WQ_BIT_EXIT, &wq->state)) {
		set_current_state(TASK_INTERRUPTIBLE);
loop:
		raw_spin_lock_irq(&wqe->lock);
		if (io_wqe_run_queue(wqe)) {
			__set_current_state(TASK_RUNNING);
			io_worker_handle_work(worker);
			goto loop;
		}
		__io_worker_idle(wqe, worker);
		raw_spin_unlock_irq(&wqe->lock);
		io_flush_signals();
		if (schedule_timeout(WORKER_IDLE_TIMEOUT))
			continue;
		if (fatal_signal_pending(current))
			break;
		/* timed out, exit unless we're the fixed worker */
		if (test_bit(IO_WQ_BIT_EXIT, &wq->state) ||
		    !(worker->flags & IO_WORKER_F_FIXED))
			break;
	}

	if (test_bit(IO_WQ_BIT_EXIT, &wq->state)) {
		raw_spin_lock_irq(&wqe->lock);
		if (!wq_list_empty(&wqe->work_list))
			io_worker_handle_work(worker);
		else
			raw_spin_unlock_irq(&wqe->lock);
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
 * running and we have work pending, wake up a free one or have the manager
 * set one up.
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

	raw_spin_lock_irq(&worker->wqe->lock);
	io_wqe_dec_running(worker);
	raw_spin_unlock_irq(&worker->wqe->lock);
}

static int task_thread(void *data, int index)
{
	struct io_worker *worker = data;
	struct io_wqe *wqe = worker->wqe;
	struct io_wqe_acct *acct = &wqe->acct[index];
	struct io_wq *wq = wqe->wq;
	char buf[TASK_COMM_LEN];

	sprintf(buf, "iou-wrk-%d", wq->task_pid);
	set_task_comm(current, buf);

	current->pf_io_worker = worker;
	worker->task = current;

	set_cpus_allowed_ptr(current, cpumask_of_node(wqe->node));
	current->flags |= PF_NO_SETAFFINITY;

	raw_spin_lock_irq(&wqe->lock);
	hlist_nulls_add_head_rcu(&worker->nulls_node, &wqe->free_list);
	list_add_tail_rcu(&worker->all_list, &wqe->all_list);
	worker->flags |= IO_WORKER_F_FREE;
	if (index == IO_WQ_ACCT_BOUND)
		worker->flags |= IO_WORKER_F_BOUND;
	if (!acct->nr_workers && (worker->flags & IO_WORKER_F_BOUND))
		worker->flags |= IO_WORKER_F_FIXED;
	acct->nr_workers++;
	raw_spin_unlock_irq(&wqe->lock);

	if (index == IO_WQ_ACCT_UNBOUND)
		atomic_inc(&wq->user->processes);

	io_wqe_worker(data);
	do_exit(0);
}

static int task_thread_bound(void *data)
{
	return task_thread(data, IO_WQ_ACCT_BOUND);
}

static int task_thread_unbound(void *data)
{
	return task_thread(data, IO_WQ_ACCT_UNBOUND);
}

static pid_t fork_thread(int (*fn)(void *), void *arg)
{
	unsigned long flags = CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD|
				CLONE_IO|SIGCHLD;
	struct kernel_clone_args args = {
		.flags		= ((lower_32_bits(flags) | CLONE_VM |
				    CLONE_UNTRACED) & ~CSIGNAL),
		.exit_signal	= (lower_32_bits(flags) & CSIGNAL),
		.stack		= (unsigned long)fn,
		.stack_size	= (unsigned long)arg,
	};

	return kernel_clone(&args);
}

static bool create_io_worker(struct io_wq *wq, struct io_wqe *wqe, int index)
{
	struct io_worker *worker;
	pid_t pid;

	worker = kzalloc_node(sizeof(*worker), GFP_KERNEL, wqe->node);
	if (!worker)
		return false;

	refcount_set(&worker->ref, 1);
	worker->nulls_node.pprev = NULL;
	worker->wqe = wqe;
	spin_lock_init(&worker->lock);

	if (index == IO_WQ_ACCT_BOUND)
		pid = fork_thread(task_thread_bound, worker);
	else
		pid = fork_thread(task_thread_unbound, worker);
	if (pid < 0) {
		kfree(worker);
		return false;
	}
	refcount_inc(&wq->refs);
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
	wake_up_process(worker->task);
	return false;
}

/*
 * Manager thread. Tasked with creating new workers, if we need them.
 */
static int io_wq_manager(void *data)
{
	struct io_wq *wq = data;
	char buf[TASK_COMM_LEN];
	int node;

	sprintf(buf, "iou-mgr-%d", wq->task_pid);
	set_task_comm(current, buf);
	current->flags |= PF_IO_WORKER;
	wq->manager = current;

	complete(&wq->done);

	while (!test_bit(IO_WQ_BIT_EXIT, &wq->state)) {
		for_each_node(node) {
			struct io_wqe *wqe = wq->wqes[node];
			bool fork_worker[2] = { false, false };

			if (!node_online(node))
				continue;

			raw_spin_lock_irq(&wqe->lock);
			if (io_wqe_need_worker(wqe, IO_WQ_ACCT_BOUND))
				fork_worker[IO_WQ_ACCT_BOUND] = true;
			if (io_wqe_need_worker(wqe, IO_WQ_ACCT_UNBOUND))
				fork_worker[IO_WQ_ACCT_UNBOUND] = true;
			raw_spin_unlock_irq(&wqe->lock);
			if (fork_worker[IO_WQ_ACCT_BOUND])
				create_io_worker(wq, wqe, IO_WQ_ACCT_BOUND);
			if (fork_worker[IO_WQ_ACCT_UNBOUND])
				create_io_worker(wq, wqe, IO_WQ_ACCT_UNBOUND);
		}
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
		if (fatal_signal_pending(current))
			set_bit(IO_WQ_BIT_EXIT, &wq->state);
	}

	if (refcount_dec_and_test(&wq->refs)) {
		complete(&wq->done);
		do_exit(0);
	}
	/* if ERROR is set and we get here, we have workers to wake */
	if (test_bit(IO_WQ_BIT_ERROR, &wq->state)) {
		rcu_read_lock();
		for_each_node(node)
			io_wq_for_each_worker(wq->wqes[node], io_wq_worker_wake, NULL);
		rcu_read_unlock();
	}
	do_exit(0);
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
		work->flags |= IO_WQ_WORK_CANCEL;
		wq->do_work(work);
		work = wq->free_work(work);
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
	raw_spin_lock_irqsave(&wqe->lock, flags);
	io_wqe_insert_work(wqe, work);
	wqe->flags &= ~IO_WQE_FLAG_STALLED;
	raw_spin_unlock_irqrestore(&wqe->lock, flags);

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
	    match->fn(worker->cur_work, match->data)) {
		set_notify_signal(worker->task);
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
	raw_spin_lock_irqsave(&wqe->lock, flags);
	wq_list_for_each(node, prev, &wqe->work_list) {
		work = container_of(node, struct io_wq_work, list);
		if (!match->fn(work, match->data))
			continue;
		io_wqe_remove_pending(wqe, work, prev);
		raw_spin_unlock_irqrestore(&wqe->lock, flags);
		io_run_cancel(work, wqe);
		match->nr_pending++;
		if (!match->cancel_all)
			return;

		/* not safe to continue after unlock */
		goto retry;
	}
	raw_spin_unlock_irqrestore(&wqe->lock, flags);
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
	if (!wq->wqes)
		goto err_wq;

	ret = cpuhp_state_add_instance_nocalls(io_wq_online, &wq->cpuhp_node);
	if (ret)
		goto err_wqes;

	wq->free_work = data->free_work;
	wq->do_work = data->do_work;

	/* caller must already hold a reference to this */
	wq->user = data->user;

	ret = -ENOMEM;
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
		raw_spin_lock_init(&wqe->lock);
		INIT_WQ_LIST(&wqe->work_list);
		INIT_HLIST_NULLS_HEAD(&wqe->free_list, 0);
		INIT_LIST_HEAD(&wqe->all_list);
	}

	wq->task_pid = current->pid;
	init_completion(&wq->done);
	refcount_set(&wq->refs, 1);

	current->flags |= PF_IO_WORKER;
	ret = fork_thread(io_wq_manager, wq);
	current->flags &= ~PF_IO_WORKER;
	if (ret >= 0) {
		wait_for_completion(&wq->done);
		reinit_completion(&wq->done);
		return wq;
	}

	if (refcount_dec_and_test(&wq->refs))
		complete(&wq->done);
err:
	cpuhp_state_remove_instance_nocalls(io_wq_online, &wq->cpuhp_node);
	for_each_node(node)
		kfree(wq->wqes[node]);
err_wqes:
	kfree(wq->wqes);
err_wq:
	kfree(wq);
	return ERR_PTR(ret);
}

void io_wq_destroy(struct io_wq *wq)
{
	int node;

	cpuhp_state_remove_instance_nocalls(io_wq_online, &wq->cpuhp_node);

	set_bit(IO_WQ_BIT_EXIT, &wq->state);
	if (wq->manager)
		wake_up_process(wq->manager);

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

static bool io_wq_worker_affinity(struct io_worker *worker, void *data)
{
	struct task_struct *task = worker->task;
	struct rq_flags rf;
	struct rq *rq;

	rq = task_rq_lock(task, &rf);
	do_set_cpus_allowed(task, cpumask_of_node(worker->wqe->node));
	task->flags |= PF_NO_SETAFFINITY;
	task_rq_unlock(rq, task, &rf);
	return false;
}

static int io_wq_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct io_wq *wq = hlist_entry_safe(node, struct io_wq, cpuhp_node);
	int i;

	rcu_read_lock();
	for_each_node(i)
		io_wq_for_each_worker(wq->wqes[i], io_wq_worker_affinity, NULL);
	rcu_read_unlock();
	return 0;
}

static __init int io_wq_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, "io-wq/online",
					io_wq_cpu_online, NULL);
	if (ret < 0)
		return ret;
	io_wq_online = ret;
	return 0;
}
subsys_initcall(io_wq_init);
