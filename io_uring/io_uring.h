#ifndef IOU_CORE_H
#define IOU_CORE_H

#include <linux/errno.h>
#include <linux/lockdep.h>
#include <linux/resume_user_mode.h>
#include <linux/kasan.h>
#include <linux/poll.h>
#include <linux/io_uring_types.h>
#include <uapi/linux/eventpoll.h>
#include "alloc_cache.h"
#include "io-wq.h"
#include "slist.h"
#include "filetable.h"
#include "opdef.h"

#ifndef CREATE_TRACE_POINTS
#include <trace/events/io_uring.h>
#endif

enum {
	IOU_OK			= 0, /* deprecated, use IOU_COMPLETE */
	IOU_COMPLETE		= 0,

	IOU_ISSUE_SKIP_COMPLETE	= -EIOCBQUEUED,

	/*
	 * The request has more work to do and should be retried. io_uring will
	 * attempt to wait on the file for eligible opcodes, but otherwise
	 * it'll be handed to iowq for blocking execution. It works for normal
	 * requests as well as for the multi shot mode.
	 */
	IOU_RETRY		= -EAGAIN,

	/*
	 * Requeue the task_work to restart operations on this request. The
	 * actual value isn't important, should just be not an otherwise
	 * valid error code, yet less than -MAX_ERRNO and valid internally.
	 */
	IOU_REQUEUE		= -3072,
};

struct io_wait_queue {
	struct wait_queue_entry wq;
	struct io_ring_ctx *ctx;
	unsigned cq_tail;
	unsigned cq_min_tail;
	unsigned nr_timeouts;
	int hit_timeout;
	ktime_t min_timeout;
	ktime_t timeout;
	struct hrtimer t;

#ifdef CONFIG_NET_RX_BUSY_POLL
	ktime_t napi_busy_poll_dt;
	bool napi_prefer_busy_poll;
#endif
};

static inline bool io_should_wake(struct io_wait_queue *iowq)
{
	struct io_ring_ctx *ctx = iowq->ctx;
	int dist = READ_ONCE(ctx->rings->cq.tail) - (int) iowq->cq_tail;

	/*
	 * Wake up if we have enough events, or if a timeout occurred since we
	 * started waiting. For timeouts, we always want to return to userspace,
	 * regardless of event count.
	 */
	return dist >= 0 || atomic_read(&ctx->cq_timeouts) != iowq->nr_timeouts;
}

#define IORING_MAX_ENTRIES	32768
#define IORING_MAX_CQ_ENTRIES	(2 * IORING_MAX_ENTRIES)

unsigned long rings_size(unsigned int flags, unsigned int sq_entries,
			 unsigned int cq_entries, size_t *sq_offset);
int io_uring_fill_params(unsigned entries, struct io_uring_params *p);
bool io_cqe_cache_refill(struct io_ring_ctx *ctx, bool overflow);
int io_run_task_work_sig(struct io_ring_ctx *ctx);
void io_req_defer_failed(struct io_kiocb *req, s32 res);
bool io_post_aux_cqe(struct io_ring_ctx *ctx, u64 user_data, s32 res, u32 cflags);
void io_add_aux_cqe(struct io_ring_ctx *ctx, u64 user_data, s32 res, u32 cflags);
bool io_req_post_cqe(struct io_kiocb *req, s32 res, u32 cflags);
void __io_commit_cqring_flush(struct io_ring_ctx *ctx);

struct file *io_file_get_normal(struct io_kiocb *req, int fd);
struct file *io_file_get_fixed(struct io_kiocb *req, int fd,
			       unsigned issue_flags);

void __io_req_task_work_add(struct io_kiocb *req, unsigned flags);
void io_req_task_work_add_remote(struct io_kiocb *req, unsigned flags);
void io_req_task_queue(struct io_kiocb *req);
void io_req_task_complete(struct io_kiocb *req, io_tw_token_t tw);
void io_req_task_queue_fail(struct io_kiocb *req, int ret);
void io_req_task_submit(struct io_kiocb *req, io_tw_token_t tw);
struct llist_node *io_handle_tw_list(struct llist_node *node, unsigned int *count, unsigned int max_entries);
struct llist_node *tctx_task_work_run(struct io_uring_task *tctx, unsigned int max_entries, unsigned int *count);
void tctx_task_work(struct callback_head *cb);
__cold void io_uring_cancel_generic(bool cancel_all, struct io_sq_data *sqd);
int io_uring_alloc_task_context(struct task_struct *task,
				struct io_ring_ctx *ctx);

int io_ring_add_registered_file(struct io_uring_task *tctx, struct file *file,
				     int start, int end);
void io_req_queue_iowq(struct io_kiocb *req);

int io_poll_issue(struct io_kiocb *req, io_tw_token_t tw);
int io_submit_sqes(struct io_ring_ctx *ctx, unsigned int nr);
int io_do_iopoll(struct io_ring_ctx *ctx, bool force_nonspin);
void __io_submit_flush_completions(struct io_ring_ctx *ctx);

struct io_wq_work *io_wq_free_work(struct io_wq_work *work);
void io_wq_submit_work(struct io_wq_work *work);

void io_free_req(struct io_kiocb *req);
void io_queue_next(struct io_kiocb *req);
void io_task_refs_refill(struct io_uring_task *tctx);
bool __io_alloc_req_refill(struct io_ring_ctx *ctx);

bool io_match_task_safe(struct io_kiocb *head, struct io_uring_task *tctx,
			bool cancel_all);

void io_activate_pollwq(struct io_ring_ctx *ctx);

static inline void io_lockdep_assert_cq_locked(struct io_ring_ctx *ctx)
{
#if defined(CONFIG_PROVE_LOCKING)
	lockdep_assert(in_task());

	if (ctx->flags & IORING_SETUP_DEFER_TASKRUN)
		lockdep_assert_held(&ctx->uring_lock);

	if (ctx->flags & IORING_SETUP_IOPOLL) {
		lockdep_assert_held(&ctx->uring_lock);
	} else if (!ctx->task_complete) {
		lockdep_assert_held(&ctx->completion_lock);
	} else if (ctx->submitter_task) {
		/*
		 * ->submitter_task may be NULL and we can still post a CQE,
		 * if the ring has been setup with IORING_SETUP_R_DISABLED.
		 * Not from an SQE, as those cannot be submitted, but via
		 * updating tagged resources.
		 */
		if (!percpu_ref_is_dying(&ctx->refs))
			lockdep_assert(current == ctx->submitter_task);
	}
#endif
}

static inline bool io_is_compat(struct io_ring_ctx *ctx)
{
	return IS_ENABLED(CONFIG_COMPAT) && unlikely(ctx->compat);
}

static inline void io_req_task_work_add(struct io_kiocb *req)
{
	__io_req_task_work_add(req, 0);
}

static inline void io_submit_flush_completions(struct io_ring_ctx *ctx)
{
	if (!wq_list_empty(&ctx->submit_state.compl_reqs) ||
	    ctx->submit_state.cq_flush)
		__io_submit_flush_completions(ctx);
}

#define io_for_each_link(pos, head) \
	for (pos = (head); pos; pos = pos->link)

static inline bool io_get_cqe_overflow(struct io_ring_ctx *ctx,
					struct io_uring_cqe **ret,
					bool overflow)
{
	io_lockdep_assert_cq_locked(ctx);

	if (unlikely(ctx->cqe_cached >= ctx->cqe_sentinel)) {
		if (unlikely(!io_cqe_cache_refill(ctx, overflow)))
			return false;
	}
	*ret = ctx->cqe_cached;
	ctx->cached_cq_tail++;
	ctx->cqe_cached++;
	if (ctx->flags & IORING_SETUP_CQE32)
		ctx->cqe_cached++;
	return true;
}

static inline bool io_get_cqe(struct io_ring_ctx *ctx, struct io_uring_cqe **ret)
{
	return io_get_cqe_overflow(ctx, ret, false);
}

static inline bool io_defer_get_uncommited_cqe(struct io_ring_ctx *ctx,
					       struct io_uring_cqe **cqe_ret)
{
	io_lockdep_assert_cq_locked(ctx);

	ctx->cq_extra++;
	ctx->submit_state.cq_flush = true;
	return io_get_cqe(ctx, cqe_ret);
}

static __always_inline bool io_fill_cqe_req(struct io_ring_ctx *ctx,
					    struct io_kiocb *req)
{
	struct io_uring_cqe *cqe;

	/*
	 * If we can't get a cq entry, userspace overflowed the
	 * submission (by quite a lot). Increment the overflow count in
	 * the ring.
	 */
	if (unlikely(!io_get_cqe(ctx, &cqe)))
		return false;


	memcpy(cqe, &req->cqe, sizeof(*cqe));
	if (ctx->flags & IORING_SETUP_CQE32) {
		memcpy(cqe->big_cqe, &req->big_cqe, sizeof(*cqe));
		memset(&req->big_cqe, 0, sizeof(req->big_cqe));
	}

	if (trace_io_uring_complete_enabled())
		trace_io_uring_complete(req->ctx, req, cqe);
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

static inline void *io_uring_alloc_async_data(struct io_alloc_cache *cache,
					      struct io_kiocb *req)
{
	if (cache) {
		req->async_data = io_cache_alloc(cache, GFP_KERNEL);
	} else {
		const struct io_issue_def *def = &io_issue_defs[req->opcode];

		WARN_ON_ONCE(!def->async_size);
		req->async_data = kmalloc(def->async_size, GFP_KERNEL);
	}
	if (req->async_data)
		req->flags |= REQ_F_ASYNC_DATA;
	return req->async_data;
}

static inline bool req_has_async_data(struct io_kiocb *req)
{
	return req->flags & REQ_F_ASYNC_DATA;
}

static inline void io_put_file(struct io_kiocb *req)
{
	if (!(req->flags & REQ_F_FIXED_FILE) && req->file)
		fput(req->file);
}

static inline void io_ring_submit_unlock(struct io_ring_ctx *ctx,
					 unsigned issue_flags)
{
	lockdep_assert_held(&ctx->uring_lock);
	if (unlikely(issue_flags & IO_URING_F_UNLOCKED))
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
	if (unlikely(issue_flags & IO_URING_F_UNLOCKED))
		mutex_lock(&ctx->uring_lock);
	lockdep_assert_held(&ctx->uring_lock);
}

static inline void io_commit_cqring(struct io_ring_ctx *ctx)
{
	/* order cqe stores with ring update */
	smp_store_release(&ctx->rings->cq.tail, ctx->cached_cq_tail);
}

static inline void io_poll_wq_wake(struct io_ring_ctx *ctx)
{
	if (wq_has_sleeper(&ctx->poll_wq))
		__wake_up(&ctx->poll_wq, TASK_NORMAL, 0,
				poll_to_key(EPOLL_URING_WAKE | EPOLLIN));
}

static inline void io_cqring_wake(struct io_ring_ctx *ctx)
{
	/*
	 * Trigger waitqueue handler on all waiters on our waitqueue. This
	 * won't necessarily wake up all the tasks, io_should_wake() will make
	 * that decision.
	 *
	 * Pass in EPOLLIN|EPOLL_URING_WAKE as the poll wakeup key. The latter
	 * set in the mask so that if we recurse back into our own poll
	 * waitqueue handlers, we know we have a dependency between eventfd or
	 * epoll and should terminate multishot poll at that point.
	 */
	if (wq_has_sleeper(&ctx->cq_wait))
		__wake_up(&ctx->cq_wait, TASK_NORMAL, 0,
				poll_to_key(EPOLL_URING_WAKE | EPOLLIN));
}

static inline bool io_sqring_full(struct io_ring_ctx *ctx)
{
	struct io_rings *r = ctx->rings;

	/*
	 * SQPOLL must use the actual sqring head, as using the cached_sq_head
	 * is race prone if the SQPOLL thread has grabbed entries but not yet
	 * committed them to the ring. For !SQPOLL, this doesn't matter, but
	 * since this helper is just used for SQPOLL sqring waits (or POLLOUT),
	 * just read the actual sqring head unconditionally.
	 */
	return READ_ONCE(r->sq.tail) - READ_ONCE(r->sq.head) == ctx->sq_entries;
}

static inline unsigned int io_sqring_entries(struct io_ring_ctx *ctx)
{
	struct io_rings *rings = ctx->rings;
	unsigned int entries;

	/* make sure SQ entry isn't read before tail */
	entries = smp_load_acquire(&rings->sq.tail) - ctx->cached_sq_head;
	return min(entries, ctx->sq_entries);
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
	if (unlikely(ctx->off_timeout_used || ctx->drain_active ||
		     ctx->has_evfd || ctx->poll_activated))
		__io_commit_cqring_flush(ctx);
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

extern struct kmem_cache *req_cachep;

static inline struct io_kiocb *io_extract_req(struct io_ring_ctx *ctx)
{
	struct io_kiocb *req;

	req = container_of(ctx->submit_state.free_list.next, struct io_kiocb, comp_list);
	wq_stack_extract(&ctx->submit_state.free_list);
	return req;
}

static inline bool io_alloc_req(struct io_ring_ctx *ctx, struct io_kiocb **req)
{
	if (unlikely(io_req_cache_empty(ctx))) {
		if (!__io_alloc_req_refill(ctx))
			return false;
	}
	*req = io_extract_req(ctx);
	return true;
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

/*
 * Terminate the request if either of these conditions are true:
 *
 * 1) It's being executed by the original task, but that task is marked
 *    with PF_EXITING as it's exiting.
 * 2) PF_KTHREAD is set, in which case the invoker of the task_work is
 *    our fallback task_work.
 */
static inline bool io_should_terminate_tw(void)
{
	return current->flags & (PF_KTHREAD | PF_EXITING);
}

static inline void io_req_queue_tw_complete(struct io_kiocb *req, s32 res)
{
	io_req_set_res(req, res, 0);
	req->io_task_work.func = io_req_task_complete;
	io_req_task_work_add(req);
}

/*
 * IORING_SETUP_SQE128 contexts allocate twice the normal SQE size for each
 * slot.
 */
static inline size_t uring_sqe_size(struct io_ring_ctx *ctx)
{
	if (ctx->flags & IORING_SETUP_SQE128)
		return 2 * sizeof(struct io_uring_sqe);
	return sizeof(struct io_uring_sqe);
}

static inline bool io_file_can_poll(struct io_kiocb *req)
{
	if (req->flags & REQ_F_CAN_POLL)
		return true;
	if (req->file && file_can_poll(req->file)) {
		req->flags |= REQ_F_CAN_POLL;
		return true;
	}
	return false;
}

static inline ktime_t io_get_time(struct io_ring_ctx *ctx)
{
	if (ctx->clockid == CLOCK_MONOTONIC)
		return ktime_get();

	return ktime_get_with_offset(ctx->clock_offset);
}

enum {
	IO_CHECK_CQ_OVERFLOW_BIT,
	IO_CHECK_CQ_DROPPED_BIT,
};

static inline bool io_has_work(struct io_ring_ctx *ctx)
{
	return test_bit(IO_CHECK_CQ_OVERFLOW_BIT, &ctx->check_cq) ||
	       io_local_work_pending(ctx);
}
#endif
