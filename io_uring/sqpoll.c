// SPDX-License-Identifier: GPL-2.0
/*
 * Contains the core associated with submission side polling of the SQ
 * ring, offloading submissions from the application to a kernel thread.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/audit.h>
#include <linux/security.h>
#include <linux/cpuset.h>
#include <linux/io_uring.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "napi.h"
#include "sqpoll.h"

#define IORING_SQPOLL_CAP_ENTRIES_VALUE 8
#define IORING_TW_CAP_ENTRIES_VALUE	8

enum {
	IO_SQ_THREAD_SHOULD_STOP = 0,
	IO_SQ_THREAD_SHOULD_PARK,
};

void io_sq_thread_unpark(struct io_sq_data *sqd)
	__releases(&sqd->lock)
{
	WARN_ON_ONCE(sqd->thread == current);

	/*
	 * Do the dance but not conditional clear_bit() because it'd race with
	 * other threads incrementing park_pending and setting the bit.
	 */
	clear_bit(IO_SQ_THREAD_SHOULD_PARK, &sqd->state);
	if (atomic_dec_return(&sqd->park_pending))
		set_bit(IO_SQ_THREAD_SHOULD_PARK, &sqd->state);
	mutex_unlock(&sqd->lock);
}

void io_sq_thread_park(struct io_sq_data *sqd)
	__acquires(&sqd->lock)
{
	WARN_ON_ONCE(data_race(sqd->thread) == current);

	atomic_inc(&sqd->park_pending);
	set_bit(IO_SQ_THREAD_SHOULD_PARK, &sqd->state);
	mutex_lock(&sqd->lock);
	if (sqd->thread)
		wake_up_process(sqd->thread);
}

void io_sq_thread_stop(struct io_sq_data *sqd)
{
	WARN_ON_ONCE(sqd->thread == current);
	WARN_ON_ONCE(test_bit(IO_SQ_THREAD_SHOULD_STOP, &sqd->state));

	set_bit(IO_SQ_THREAD_SHOULD_STOP, &sqd->state);
	mutex_lock(&sqd->lock);
	if (sqd->thread)
		wake_up_process(sqd->thread);
	mutex_unlock(&sqd->lock);
	wait_for_completion(&sqd->exited);
}

void io_put_sq_data(struct io_sq_data *sqd)
{
	if (refcount_dec_and_test(&sqd->refs)) {
		WARN_ON_ONCE(atomic_read(&sqd->park_pending));

		io_sq_thread_stop(sqd);
		kfree(sqd);
	}
}

static __cold void io_sqd_update_thread_idle(struct io_sq_data *sqd)
{
	struct io_ring_ctx *ctx;
	unsigned sq_thread_idle = 0;

	list_for_each_entry(ctx, &sqd->ctx_list, sqd_list)
		sq_thread_idle = max(sq_thread_idle, ctx->sq_thread_idle);
	sqd->sq_thread_idle = sq_thread_idle;
}

void io_sq_thread_finish(struct io_ring_ctx *ctx)
{
	struct io_sq_data *sqd = ctx->sq_data;

	if (sqd) {
		io_sq_thread_park(sqd);
		list_del_init(&ctx->sqd_list);
		io_sqd_update_thread_idle(sqd);
		io_sq_thread_unpark(sqd);

		io_put_sq_data(sqd);
		ctx->sq_data = NULL;
	}
}

static struct io_sq_data *io_attach_sq_data(struct io_uring_params *p)
{
	struct io_ring_ctx *ctx_attach;
	struct io_sq_data *sqd;
	struct fd f;

	f = fdget(p->wq_fd);
	if (!fd_file(f))
		return ERR_PTR(-ENXIO);
	if (!io_is_uring_fops(fd_file(f))) {
		fdput(f);
		return ERR_PTR(-EINVAL);
	}

	ctx_attach = fd_file(f)->private_data;
	sqd = ctx_attach->sq_data;
	if (!sqd) {
		fdput(f);
		return ERR_PTR(-EINVAL);
	}
	if (sqd->task_tgid != current->tgid) {
		fdput(f);
		return ERR_PTR(-EPERM);
	}

	refcount_inc(&sqd->refs);
	fdput(f);
	return sqd;
}

static struct io_sq_data *io_get_sq_data(struct io_uring_params *p,
					 bool *attached)
{
	struct io_sq_data *sqd;

	*attached = false;
	if (p->flags & IORING_SETUP_ATTACH_WQ) {
		sqd = io_attach_sq_data(p);
		if (!IS_ERR(sqd)) {
			*attached = true;
			return sqd;
		}
		/* fall through for EPERM case, setup new sqd/task */
		if (PTR_ERR(sqd) != -EPERM)
			return sqd;
	}

	sqd = kzalloc(sizeof(*sqd), GFP_KERNEL);
	if (!sqd)
		return ERR_PTR(-ENOMEM);

	atomic_set(&sqd->park_pending, 0);
	refcount_set(&sqd->refs, 1);
	INIT_LIST_HEAD(&sqd->ctx_list);
	mutex_init(&sqd->lock);
	init_waitqueue_head(&sqd->wait);
	init_completion(&sqd->exited);
	return sqd;
}

static inline bool io_sqd_events_pending(struct io_sq_data *sqd)
{
	return READ_ONCE(sqd->state);
}

static int __io_sq_thread(struct io_ring_ctx *ctx, bool cap_entries)
{
	unsigned int to_submit;
	int ret = 0;

	to_submit = io_sqring_entries(ctx);
	/* if we're handling multiple rings, cap submit size for fairness */
	if (cap_entries && to_submit > IORING_SQPOLL_CAP_ENTRIES_VALUE)
		to_submit = IORING_SQPOLL_CAP_ENTRIES_VALUE;

	if (to_submit || !wq_list_empty(&ctx->iopoll_list)) {
		const struct cred *creds = NULL;

		if (ctx->sq_creds != current_cred())
			creds = override_creds(ctx->sq_creds);

		mutex_lock(&ctx->uring_lock);
		if (!wq_list_empty(&ctx->iopoll_list))
			io_do_iopoll(ctx, true);

		/*
		 * Don't submit if refs are dying, good for io_uring_register(),
		 * but also it is relied upon by io_ring_exit_work()
		 */
		if (to_submit && likely(!percpu_ref_is_dying(&ctx->refs)) &&
		    !(ctx->flags & IORING_SETUP_R_DISABLED))
			ret = io_submit_sqes(ctx, to_submit);
		mutex_unlock(&ctx->uring_lock);

		if (to_submit && wq_has_sleeper(&ctx->sqo_sq_wait))
			wake_up(&ctx->sqo_sq_wait);
		if (creds)
			revert_creds(creds);
	}

	return ret;
}

static bool io_sqd_handle_event(struct io_sq_data *sqd)
{
	bool did_sig = false;
	struct ksignal ksig;

	if (test_bit(IO_SQ_THREAD_SHOULD_PARK, &sqd->state) ||
	    signal_pending(current)) {
		mutex_unlock(&sqd->lock);
		if (signal_pending(current))
			did_sig = get_signal(&ksig);
		cond_resched();
		mutex_lock(&sqd->lock);
		sqd->sq_cpu = raw_smp_processor_id();
	}
	return did_sig || test_bit(IO_SQ_THREAD_SHOULD_STOP, &sqd->state);
}

/*
 * Run task_work, processing the retry_list first. The retry_list holds
 * entries that we passed on in the previous run, if we had more task_work
 * than we were asked to process. Newly queued task_work isn't run until the
 * retry list has been fully processed.
 */
static unsigned int io_sq_tw(struct llist_node **retry_list, int max_entries)
{
	struct io_uring_task *tctx = current->io_uring;
	unsigned int count = 0;

	if (*retry_list) {
		*retry_list = io_handle_tw_list(*retry_list, &count, max_entries);
		if (count >= max_entries)
			goto out;
		max_entries -= count;
	}
	*retry_list = tctx_task_work_run(tctx, max_entries, &count);
out:
	if (task_work_pending(current))
		task_work_run();
	return count;
}

static bool io_sq_tw_pending(struct llist_node *retry_list)
{
	struct io_uring_task *tctx = current->io_uring;

	return retry_list || !llist_empty(&tctx->task_list);
}

static void io_sq_update_worktime(struct io_sq_data *sqd, struct rusage *start)
{
	struct rusage end;

	getrusage(current, RUSAGE_SELF, &end);
	end.ru_stime.tv_sec -= start->ru_stime.tv_sec;
	end.ru_stime.tv_usec -= start->ru_stime.tv_usec;

	sqd->work_time += end.ru_stime.tv_usec + end.ru_stime.tv_sec * 1000000;
}

static int io_sq_thread(void *data)
{
	struct llist_node *retry_list = NULL;
	struct io_sq_data *sqd = data;
	struct io_ring_ctx *ctx;
	struct rusage start;
	unsigned long timeout = 0;
	char buf[TASK_COMM_LEN];
	DEFINE_WAIT(wait);

	/* offload context creation failed, just exit */
	if (!current->io_uring) {
		mutex_lock(&sqd->lock);
		sqd->thread = NULL;
		mutex_unlock(&sqd->lock);
		goto err_out;
	}

	snprintf(buf, sizeof(buf), "iou-sqp-%d", sqd->task_pid);
	set_task_comm(current, buf);

	/* reset to our pid after we've set task_comm, for fdinfo */
	sqd->task_pid = current->pid;

	if (sqd->sq_cpu != -1) {
		set_cpus_allowed_ptr(current, cpumask_of(sqd->sq_cpu));
	} else {
		set_cpus_allowed_ptr(current, cpu_online_mask);
		sqd->sq_cpu = raw_smp_processor_id();
	}

	/*
	 * Force audit context to get setup, in case we do prep side async
	 * operations that would trigger an audit call before any issue side
	 * audit has been done.
	 */
	audit_uring_entry(IORING_OP_NOP);
	audit_uring_exit(true, 0);

	mutex_lock(&sqd->lock);
	while (1) {
		bool cap_entries, sqt_spin = false;

		if (io_sqd_events_pending(sqd) || signal_pending(current)) {
			if (io_sqd_handle_event(sqd))
				break;
			timeout = jiffies + sqd->sq_thread_idle;
		}

		cap_entries = !list_is_singular(&sqd->ctx_list);
		getrusage(current, RUSAGE_SELF, &start);
		list_for_each_entry(ctx, &sqd->ctx_list, sqd_list) {
			int ret = __io_sq_thread(ctx, cap_entries);

			if (!sqt_spin && (ret > 0 || !wq_list_empty(&ctx->iopoll_list)))
				sqt_spin = true;
		}
		if (io_sq_tw(&retry_list, IORING_TW_CAP_ENTRIES_VALUE))
			sqt_spin = true;

		list_for_each_entry(ctx, &sqd->ctx_list, sqd_list)
			if (io_napi(ctx))
				io_napi_sqpoll_busy_poll(ctx);

		if (sqt_spin || !time_after(jiffies, timeout)) {
			if (sqt_spin) {
				io_sq_update_worktime(sqd, &start);
				timeout = jiffies + sqd->sq_thread_idle;
			}
			if (unlikely(need_resched())) {
				mutex_unlock(&sqd->lock);
				cond_resched();
				mutex_lock(&sqd->lock);
				sqd->sq_cpu = raw_smp_processor_id();
			}
			continue;
		}

		prepare_to_wait(&sqd->wait, &wait, TASK_INTERRUPTIBLE);
		if (!io_sqd_events_pending(sqd) && !io_sq_tw_pending(retry_list)) {
			bool needs_sched = true;

			list_for_each_entry(ctx, &sqd->ctx_list, sqd_list) {
				atomic_or(IORING_SQ_NEED_WAKEUP,
						&ctx->rings->sq_flags);
				if ((ctx->flags & IORING_SETUP_IOPOLL) &&
				    !wq_list_empty(&ctx->iopoll_list)) {
					needs_sched = false;
					break;
				}

				/*
				 * Ensure the store of the wakeup flag is not
				 * reordered with the load of the SQ tail
				 */
				smp_mb__after_atomic();

				if (io_sqring_entries(ctx)) {
					needs_sched = false;
					break;
				}
			}

			if (needs_sched) {
				mutex_unlock(&sqd->lock);
				schedule();
				mutex_lock(&sqd->lock);
				sqd->sq_cpu = raw_smp_processor_id();
			}
			list_for_each_entry(ctx, &sqd->ctx_list, sqd_list)
				atomic_andnot(IORING_SQ_NEED_WAKEUP,
						&ctx->rings->sq_flags);
		}

		finish_wait(&sqd->wait, &wait);
		timeout = jiffies + sqd->sq_thread_idle;
	}

	if (retry_list)
		io_sq_tw(&retry_list, UINT_MAX);

	io_uring_cancel_generic(true, sqd);
	sqd->thread = NULL;
	list_for_each_entry(ctx, &sqd->ctx_list, sqd_list)
		atomic_or(IORING_SQ_NEED_WAKEUP, &ctx->rings->sq_flags);
	io_run_task_work();
	mutex_unlock(&sqd->lock);
err_out:
	complete(&sqd->exited);
	do_exit(0);
}

void io_sqpoll_wait_sq(struct io_ring_ctx *ctx)
{
	DEFINE_WAIT(wait);

	do {
		if (!io_sqring_full(ctx))
			break;
		prepare_to_wait(&ctx->sqo_sq_wait, &wait, TASK_INTERRUPTIBLE);

		if (!io_sqring_full(ctx))
			break;
		schedule();
	} while (!signal_pending(current));

	finish_wait(&ctx->sqo_sq_wait, &wait);
}

__cold int io_sq_offload_create(struct io_ring_ctx *ctx,
				struct io_uring_params *p)
{
	struct task_struct *task_to_put = NULL;
	int ret;

	/* Retain compatibility with failing for an invalid attach attempt */
	if ((ctx->flags & (IORING_SETUP_ATTACH_WQ | IORING_SETUP_SQPOLL)) ==
				IORING_SETUP_ATTACH_WQ) {
		struct fd f;

		f = fdget(p->wq_fd);
		if (!fd_file(f))
			return -ENXIO;
		if (!io_is_uring_fops(fd_file(f))) {
			fdput(f);
			return -EINVAL;
		}
		fdput(f);
	}
	if (ctx->flags & IORING_SETUP_SQPOLL) {
		struct task_struct *tsk;
		struct io_sq_data *sqd;
		bool attached;

		ret = security_uring_sqpoll();
		if (ret)
			return ret;

		sqd = io_get_sq_data(p, &attached);
		if (IS_ERR(sqd)) {
			ret = PTR_ERR(sqd);
			goto err;
		}

		ctx->sq_creds = get_current_cred();
		ctx->sq_data = sqd;
		ctx->sq_thread_idle = msecs_to_jiffies(p->sq_thread_idle);
		if (!ctx->sq_thread_idle)
			ctx->sq_thread_idle = HZ;

		io_sq_thread_park(sqd);
		list_add(&ctx->sqd_list, &sqd->ctx_list);
		io_sqd_update_thread_idle(sqd);
		/* don't attach to a dying SQPOLL thread, would be racy */
		ret = (attached && !sqd->thread) ? -ENXIO : 0;
		io_sq_thread_unpark(sqd);

		if (ret < 0)
			goto err;
		if (attached)
			return 0;

		if (p->flags & IORING_SETUP_SQ_AFF) {
			cpumask_var_t allowed_mask;
			int cpu = p->sq_thread_cpu;

			ret = -EINVAL;
			if (cpu >= nr_cpu_ids || !cpu_online(cpu))
				goto err_sqpoll;
			ret = -ENOMEM;
			if (!alloc_cpumask_var(&allowed_mask, GFP_KERNEL))
				goto err_sqpoll;
			ret = -EINVAL;
			cpuset_cpus_allowed(current, allowed_mask);
			if (!cpumask_test_cpu(cpu, allowed_mask)) {
				free_cpumask_var(allowed_mask);
				goto err_sqpoll;
			}
			free_cpumask_var(allowed_mask);
			sqd->sq_cpu = cpu;
		} else {
			sqd->sq_cpu = -1;
		}

		sqd->task_pid = current->pid;
		sqd->task_tgid = current->tgid;
		tsk = create_io_thread(io_sq_thread, sqd, NUMA_NO_NODE);
		if (IS_ERR(tsk)) {
			ret = PTR_ERR(tsk);
			goto err_sqpoll;
		}

		sqd->thread = tsk;
		task_to_put = get_task_struct(tsk);
		ret = io_uring_alloc_task_context(tsk, ctx);
		wake_up_new_task(tsk);
		if (ret)
			goto err;
	} else if (p->flags & IORING_SETUP_SQ_AFF) {
		/* Can't have SQ_AFF without SQPOLL */
		ret = -EINVAL;
		goto err;
	}

	if (task_to_put)
		put_task_struct(task_to_put);
	return 0;
err_sqpoll:
	complete(&ctx->sq_data->exited);
err:
	io_sq_thread_finish(ctx);
	if (task_to_put)
		put_task_struct(task_to_put);
	return ret;
}

__cold int io_sqpoll_wq_cpu_affinity(struct io_ring_ctx *ctx,
				     cpumask_var_t mask)
{
	struct io_sq_data *sqd = ctx->sq_data;
	int ret = -EINVAL;

	if (sqd) {
		io_sq_thread_park(sqd);
		/* Don't set affinity for a dying thread */
		if (sqd->thread)
			ret = io_wq_cpu_affinity(sqd->thread->io_uring, mask);
		io_sq_thread_unpark(sqd);
	}

	return ret;
}
