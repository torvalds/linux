#ifndef IOU_CORE_H
#define IOU_CORE_H

#include <linux/errno.h>
#include <linux/lockdep.h>
#include <linux/io_uring_types.h>
#include "io-wq.h"
#include "slist.h"
#include "filetable.h"

#ifndef CREATE_TRACE_POINTS
#include <trace/events/io_uring.h>
#endif

enum {
	IOU_OK			= 0,
	IOU_ISSUE_SKIP_COMPLETE	= -EIOCBQUEUED,

	/*
	 * Intended only when both IO_URING_F_MULTISHOT is passed
	 * to indicate to the poll runner that multishot should be
	 * removed and the result is set on req->cqe.res.
	 */
	IOU_STOP_MULTISHOT	= -ECANCELED,
};

struct io_uring_cqe *__io_get_cqe(struct io_ring_ctx *ctx, bool overflow);
bool io_req_cqe_overflow(struct io_kiocb *req);
int io_run_task_work_sig(struct io_ring_ctx *ctx);
int __io_run_local_work(struct io_ring_ctx *ctx, bool *locked);
int io_run_local_work(struct io_ring_ctx *ctx);
void io_req_complete_failed(struct io_kiocb *req, s32 res);
void __io_req_complete(struct io_kiocb *req, unsigned issue_flags);
void io_req_complete_post(struct io_kiocb *req);
void __io_req_complete_post(struct io_kiocb *req);
bool io_post_aux_cqe(struct io_ring_ctx *ctx, u64 user_data, s32 res, u32 cflags,
		     bool allow_overflow);
bool io_fill_cqe_aux(struct io_ring_ctx *ctx, u64 user_data, s32 res, u32 cflags,
		     bool allow_overflow);
void __io_commit_cqring_flush(struct io_ring_ctx *ctx);

struct page **io_pin_pages(unsigned long ubuf, unsigned long len, int *npages);

struct file *io_file_get_normal(struct io_kiocb *req, int fd);
struct file *io_file_get_fixed(struct io_kiocb *req, int fd,
			       unsigned issue_flags);

static inline bool io_req_ffs_set(struct io_kiocb *req)
{
	return req->flags & REQ_F_FIXED_FILE;
}

bool io_is_uring_fops(struct file *file);
bool io_alloc_async_data(struct io_kiocb *req);
void io_req_task_work_add(struct io_kiocb *req);
void io_req_tw_post_queue(struct io_kiocb *req, s32 res, u32 cflags);
void io_req_task_queue(struct io_kiocb *req);
void io_queue_iowq(struct io_kiocb *req, bool *dont_use);
void io_req_task_complete(struct io_kiocb *req, bool *locked);
void io_req_task_queue_fail(struct io_kiocb *req, int ret);
void io_req_task_submit(struct io_kiocb *req, bool *locked);
void tctx_task_work(struct callback_head *cb);
__cold void io_uring_cancel_generic(bool cancel_all, struct io_sq_data *sqd);
int io_uring_alloc_task_context(struct task_struct *task,
				struct io_ring_ctx *ctx);

int io_poll_issue(struct io_kiocb *req, bool *locked);
int io_submit_sqes(struct io_ring_ctx *ctx, unsigned int nr);
int io_do_iopoll(struct io_ring_ctx *ctx, bool force_nonspin);
void io_free_batch_list(struct io_ring_ctx *ctx, struct io_wq_work_node *node);
int io_req_prep_async(struct io_kiocb *req);

struct io_wq_work *io_wq_free_work(struct io_wq_work *work);
void io_wq_submit_work(struct io_wq_work *work);

void io_free_req(struct io_kiocb *req);
void io_queue_next(struct io_kiocb *req);
void __io_put_task(struct task_struct *task, int nr);
void io_task_refs_refill(struct io_uring_task *tctx);
bool __io_alloc_req_refill(struct io_ring_ctx *ctx);

bool io_match_task_safe(struct io_kiocb *head, struct task_struct *task,
			bool cancel_all);

#define io_for_each_link(pos, head) \
	for (pos = (head); pos; pos = pos->link)

static inline void io_cq_lock(struct io_ring_ctx *ctx)
	__acquires(ctx->completion_lock)
{
	spin_lock(&ctx->completion_lock);
}

void io_cq_unlock_post(struct io_ring_ctx *ctx);

static inline struct io_uring_cqe *io_get_cqe_overflow(struct io_ring_ctx *ctx,
						       bool overflow)
{
	if (likely(ctx->cqe_cached < ctx->cqe_sentinel)) {
		struct io_uring_cqe *cqe = ctx->cqe_cached;

		ctx->cached_cq_tail++;
		ctx->cqe_cached++;
		if (ctx->flags & IORING_SETUP_CQE32)
			ctx->cqe_cached++;
		return cqe;
	}

	return __io_get_cqe(ctx, overflow);
}

static inline struct io_uring_cqe *io_get_cqe(struct io_ring_ctx *ctx)
{
	return io_get_cqe_overflow(ctx, false);
}

static inline bool __io_fill_cqe_req(struct io_ring_ctx *ctx,
				     struct io_kiocb *req)
{
	struct io_uring_cqe *cqe;

	/*
	 * If we can't get a cq entry, userspace overflowed the
	 * submission (by quite a lot). Increment the overflow count in
	 * the ring.
	 */
	cqe = io_get_cqe(ctx);
	if (unlikely(!cqe))
		return io_req_cqe_overflow(req);

	trace_io_uring_complete(req->ctx, req, req->cqe.user_data,
				req->cqe.res, req->cqe.flags,
				(req->flags & REQ_F_CQE32_INIT) ? req->extra1 : 0,
				(req->flags & REQ_F_CQE32_INIT) ? req->extra2 : 0);

	memcpy(cqe, &req->cqe, sizeof(*cqe));

	if (ctx->flags & IORING_SETUP_CQE32) {
		u64 extra1 = 0, extra2 = 0;

		if (req->flags & REQ_F_CQE32_INIT) {
			extra1 = req->extra1;
			extra2 = req->extra2;
		}

		WRITE_ONCE(cqe->big_cqe[0], extra1);
		WRITE_ONCE(cqe->big_cqe[1], extra2);
	}
	return true;
}

static inline void req_set_fail(struct io_kiocb *req)
{
	req->flags |= REQ_F_FAIL;
	if (req->flags & REQ_F_CQE_SKIP) {
		req->flags &= ~REQ_F_CQE_SKIP;
		req->flags |= REQ_F_SKIP_LINK_CQES;
	}
}

static inline void io_req_set_res(struct io_kiocb *req, s32 res, u32 cflags)
{
	req->cqe.res = res;
	req->cqe.flags = cflags;
}

static inline bool req_has_async_data(struct io_kiocb *req)
{
	return req->flags & REQ_F_ASYNC_DATA;
}

static inline void io_put_file(struct file *file)
{
	if (file)
		fput(file);
}

static inline void io_ring_submit_unlock(struct io_ring_ctx *ctx,
					 unsigned issue_flags)
{
	lockdep_assert_held(&ctx->uring_lock);
	if (issue_flags & IO_URING_F_UNLOCKED)
		mutex_unlock(&ctx->uring_lock);
}

static inline void io_ring_submit_lock(struct io_ring_ctx *ctx,
				       unsigned issue_flags)
{
	/*
	 * "Normal" inline submissions always hold the uring_lock, since we
	 * grab it from the system call. Same is true for the SQPOLL offload.
	 * The only exception is when we've detached the request and issue it
	 * from an async worker thread, grab the lock for that case.
	 */
	if (issue_flags & IO_URING_F_UNLOCKED)
		mutex_lock(&ctx->uring_lock);
	lockdep_assert_held(&ctx->uring_lock);
}

static inline void io_commit_cqring(struct io_ring_ctx *ctx)
{
	/* order cqe stores with ring update */
	smp_store_release(&ctx->rings->cq.tail, ctx->cached_cq_tail);
}

/* requires smb_mb() prior, see wq_has_sleeper() */
static inline void __io_cqring_wake(struct io_ring_ctx *ctx)
{
	/*
	 * wake_up_all() may seem excessive, but io_wake_function() and
	 * io_should_wake() handle the termination of the loop and only
	 * wake as many waiters as we need to.
	 */
	if (waitqueue_active(&ctx->cq_wait))
		wake_up_all(&ctx->cq_wait);
}

static inline void io_cqring_wake(struct io_ring_ctx *ctx)
{
	smp_mb();
	__io_cqring_wake(ctx);
}

static inline bool io_sqring_full(struct io_ring_ctx *ctx)
{
	struct io_rings *r = ctx->rings;

	return READ_ONCE(r->sq.tail) - ctx->cached_sq_head == ctx->sq_entries;
}

static inline unsigned int io_sqring_entries(struct io_ring_ctx *ctx)
{
	struct io_rings *rings = ctx->rings;

	/* make sure SQ entry isn't read before tail */
	return smp_load_acquire(&rings->sq.tail) - ctx->cached_sq_head;
}

static inline int io_run_task_work(void)
{
	/*
	 * Always check-and-clear the task_work notification signal. With how
	 * signaling works for task_work, we can find it set with nothing to
	 * run. We need to clear it for that case, like get_signal() does.
	 */
	if (test_thread_flag(TIF_NOTIFY_SIGNAL))
		clear_notify_signal();
	if (task_work_pending(current)) {
		__set_current_state(TASK_RUNNING);
		task_work_run();
		return 1;
	}

	return 0;
}

static inline bool io_task_work_pending(struct io_ring_ctx *ctx)
{
	return test_thread_flag(TIF_NOTIFY_SIGNAL) ||
		!wq_list_empty(&ctx->work_llist);
}

static inline int io_run_task_work_ctx(struct io_ring_ctx *ctx)
{
	int ret = 0;
	int ret2;

	if (ctx->flags & IORING_SETUP_DEFER_TASKRUN)
		ret = io_run_local_work(ctx);

	/* want to run this after in case more is added */
	ret2 = io_run_task_work();

	/* Try propagate error in favour of if tasks were run,
	 * but still make sure to run them if requested
	 */
	if (ret >= 0)
		ret += ret2;

	return ret;
}

static inline int io_run_local_work_locked(struct io_ring_ctx *ctx)
{
	bool locked;
	int ret;

	if (llist_empty(&ctx->work_llist))
		return 0;

	locked = true;
	ret = __io_run_local_work(ctx, &locked);
	/* shouldn't happen! */
	if (WARN_ON_ONCE(!locked))
		mutex_lock(&ctx->uring_lock);
	return ret;
}

static inline void io_tw_lock(struct io_ring_ctx *ctx, bool *locked)
{
	if (!*locked) {
		mutex_lock(&ctx->uring_lock);
		*locked = true;
	}
}

/*
 * Don't complete immediately but use deferred completion infrastructure.
 * Protected by ->uring_lock and can only be used either with
 * IO_URING_F_COMPLETE_DEFER or inside a tw handler holding the mutex.
 */
static inline void io_req_complete_defer(struct io_kiocb *req)
	__must_hold(&req->ctx->uring_lock)
{
	struct io_submit_state *state = &req->ctx->submit_state;

	lockdep_assert_held(&req->ctx->uring_lock);

	wq_list_add_tail(&req->comp_list, &state->compl_reqs);
}

static inline void io_commit_cqring_flush(struct io_ring_ctx *ctx)
{
	if (unlikely(ctx->off_timeout_used || ctx->drain_active || ctx->has_evfd))
		__io_commit_cqring_flush(ctx);
}

/* must to be called somewhat shortly after putting a request */
static inline void io_put_task(struct task_struct *task, int nr)
{
	if (likely(task == current))
		task->io_uring->cached_refs += nr;
	else
		__io_put_task(task, nr);
}

static inline void io_get_task_refs(int nr)
{
	struct io_uring_task *tctx = current->io_uring;

	tctx->cached_refs -= nr;
	if (unlikely(tctx->cached_refs < 0))
		io_task_refs_refill(tctx);
}

static inline bool io_req_cache_empty(struct io_ring_ctx *ctx)
{
	return !ctx->submit_state.free_list.next;
}

static inline bool io_alloc_req_refill(struct io_ring_ctx *ctx)
{
	if (unlikely(io_req_cache_empty(ctx)))
		return __io_alloc_req_refill(ctx);
	return true;
}

static inline struct io_kiocb *io_alloc_req(struct io_ring_ctx *ctx)
{
	struct io_wq_work_node *node;

	node = wq_stack_extract(&ctx->submit_state.free_list);
	return container_of(node, struct io_kiocb, comp_list);
}

static inline bool io_allowed_run_tw(struct io_ring_ctx *ctx)
{
	return likely(!(ctx->flags & IORING_SETUP_DEFER_TASKRUN) ||
		      ctx->submitter_task == current);
}

#endif
