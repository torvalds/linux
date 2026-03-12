// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_TW_H
#define IOU_TW_H

#include <linux/sched.h>
#include <linux/percpu-refcount.h>
#include <linux/io_uring_types.h>

#define IO_LOCAL_TW_DEFAULT_MAX		20

/*
 * Terminate the request if either of these conditions are true:
 *
 * 1) It's being executed by the original task, but that task is marked
 *    with PF_EXITING as it's exiting.
 * 2) PF_KTHREAD is set, in which case the invoker of the task_work is
 *    our fallback task_work.
 * 3) The ring has been closed and is going away.
 */
static inline bool io_should_terminate_tw(struct io_ring_ctx *ctx)
{
	return (current->flags & (PF_EXITING | PF_KTHREAD)) || percpu_ref_is_dying(&ctx->refs);
}

void io_req_task_work_add_remote(struct io_kiocb *req, unsigned flags);
struct llist_node *io_handle_tw_list(struct llist_node *node, unsigned int *count, unsigned int max_entries);
void tctx_task_work(struct callback_head *cb);
int io_run_local_work(struct io_ring_ctx *ctx, int min_events, int max_events);
int io_run_task_work_sig(struct io_ring_ctx *ctx);

__cold void io_fallback_req_func(struct work_struct *work);
__cold void io_move_task_work_from_local(struct io_ring_ctx *ctx);
int io_run_local_work_locked(struct io_ring_ctx *ctx, int min_events);

void io_req_local_work_add(struct io_kiocb *req, unsigned flags);
void io_req_normal_work_add(struct io_kiocb *req);
struct llist_node *tctx_task_work_run(struct io_uring_task *tctx, unsigned int max_entries, unsigned int *count);

static inline void __io_req_task_work_add(struct io_kiocb *req, unsigned flags)
{
	if (req->ctx->flags & IORING_SETUP_DEFER_TASKRUN)
		io_req_local_work_add(req, flags);
	else
		io_req_normal_work_add(req);
}

static inline void io_req_task_work_add(struct io_kiocb *req)
{
	__io_req_task_work_add(req, 0);
}

static inline int io_run_task_work(void)
{
	bool ret = false;

	/*
	 * Always check-and-clear the task_work notification signal. With how
	 * signaling works for task_work, we can find it set with nothing to
	 * run. We need to clear it for that case, like get_signal() does.
	 */
	if (test_thread_flag(TIF_NOTIFY_SIGNAL))
		clear_notify_signal();
	/*
	 * PF_IO_WORKER never returns to userspace, so check here if we have
	 * notify work that needs processing.
	 */
	if (current->flags & PF_IO_WORKER) {
		if (test_thread_flag(TIF_NOTIFY_RESUME)) {
			__set_current_state(TASK_RUNNING);
			resume_user_mode_work(NULL);
		}
		if (current->io_uring) {
			unsigned int count = 0;

			__set_current_state(TASK_RUNNING);
			tctx_task_work_run(current->io_uring, UINT_MAX, &count);
			if (count)
				ret = true;
		}
	}
	if (task_work_pending(current)) {
		__set_current_state(TASK_RUNNING);
		task_work_run();
		ret = true;
	}

	return ret;
}

static inline bool io_local_work_pending(struct io_ring_ctx *ctx)
{
	return !llist_empty(&ctx->work_llist) || !llist_empty(&ctx->retry_llist);
}

static inline bool io_task_work_pending(struct io_ring_ctx *ctx)
{
	return task_work_pending(current) || io_local_work_pending(ctx);
}

static inline void io_tw_lock(struct io_ring_ctx *ctx, io_tw_token_t tw)
{
	lockdep_assert_held(&ctx->uring_lock);
}

static inline bool io_allowed_defer_tw_run(struct io_ring_ctx *ctx)
{
	return likely(ctx->submitter_task == current);
}

static inline bool io_allowed_run_tw(struct io_ring_ctx *ctx)
{
	return likely(!(ctx->flags & IORING_SETUP_DEFER_TASKRUN) ||
		      ctx->submitter_task == current);
}

#endif
