#ifndef IOU_CORE_H
#define IOU_CORE_H

#include <linux/errno.h>
#include <linux/lockdep.h>
#include "io_uring_types.h"

enum {
	IOU_OK			= 0,
	IOU_ISSUE_SKIP_COMPLETE	= -EIOCBQUEUED,
};

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

static inline bool io_run_task_work(void)
{
	if (test_thread_flag(TIF_NOTIFY_SIGNAL) || task_work_pending(current)) {
		__set_current_state(TASK_RUNNING);
		clear_notify_signal();
		if (task_work_pending(current))
			task_work_run();
		return true;
	}

	return false;
}

void __io_req_complete(struct io_kiocb *req, unsigned issue_flags);
void io_req_complete_post(struct io_kiocb *req);
void __io_req_complete_post(struct io_kiocb *req);
bool io_fill_cqe_aux(struct io_ring_ctx *ctx, u64 user_data, s32 res,
		     u32 cflags);
void io_cqring_ev_posted(struct io_ring_ctx *ctx);
void __user *io_buffer_select(struct io_kiocb *req, size_t *len,
			      unsigned int issue_flags);
unsigned int io_put_kbuf(struct io_kiocb *req, unsigned issue_flags);

static inline bool io_do_buffer_select(struct io_kiocb *req)
{
	if (!(req->flags & REQ_F_BUFFER_SELECT))
		return false;
	return !(req->flags & (REQ_F_BUFFER_SELECTED|REQ_F_BUFFER_RING));
}

struct file *io_file_get_normal(struct io_kiocb *req, int fd);
struct file *io_file_get_fixed(struct io_kiocb *req, int fd,
			       unsigned issue_flags);
int io_fixed_fd_install(struct io_kiocb *req, unsigned int issue_flags,
			struct file *file, unsigned int file_slot);
int io_install_fixed_file(struct io_kiocb *req, struct file *file,
			  unsigned int issue_flags, u32 slot_index);

int io_rsrc_node_switch_start(struct io_ring_ctx *ctx);
int io_queue_rsrc_removal(struct io_rsrc_data *data, unsigned idx,
			  struct io_rsrc_node *node, void *rsrc);
void io_rsrc_node_switch(struct io_ring_ctx *ctx,
			 struct io_rsrc_data *data_to_kill);
bool io_is_uring_fops(struct file *file);
bool io_alloc_async_data(struct io_kiocb *req);
void io_req_task_work_add(struct io_kiocb *req);
void io_req_tw_post_queue(struct io_kiocb *req, s32 res, u32 cflags);
void io_req_task_complete(struct io_kiocb *req, bool *locked);
void io_req_task_queue_fail(struct io_kiocb *req, int ret);
void tctx_task_work(struct callback_head *cb);
int io_try_cancel(struct io_kiocb *req, struct io_cancel_data *cd);
__cold void io_uring_cancel_generic(bool cancel_all, struct io_sq_data *sqd);
int io_uring_alloc_task_context(struct task_struct *task,
				struct io_ring_ctx *ctx);

int io_submit_sqes(struct io_ring_ctx *ctx, unsigned int nr);
int io_do_iopoll(struct io_ring_ctx *ctx, bool force_nonspin);

struct io_wq_work *io_wq_free_work(struct io_wq_work *work);
void io_wq_submit_work(struct io_wq_work *work);

void io_free_req(struct io_kiocb *req);
void io_queue_next(struct io_kiocb *req);

#define io_for_each_link(pos, head) \
	for (pos = (head); pos; pos = pos->link)

#endif
