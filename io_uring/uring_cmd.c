// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/io_uring/cmd.h>
#include <linux/security.h>
#include <linux/nospec.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "alloc_cache.h"
#include "rsrc.h"
#include "kbuf.h"
#include "uring_cmd.h"
#include "poll.h"

void io_cmd_cache_free(const void *entry)
{
	struct io_async_cmd *ac = (struct io_async_cmd *)entry;

	io_vec_free(&ac->vec);
	kfree(ac);
}

static void io_req_uring_cleanup(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_uring_cmd *ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);
	struct io_async_cmd *ac = req->async_data;

	if (issue_flags & IO_URING_F_UNLOCKED)
		return;

	io_alloc_cache_vec_kasan(&ac->vec);
	if (ac->vec.nr > IO_VEC_CACHE_SOFT_CAP)
		io_vec_free(&ac->vec);

	if (io_alloc_cache_put(&req->ctx->cmd_cache, ac)) {
		ioucmd->sqe = NULL;
		io_req_async_data_clear(req, REQ_F_NEED_CLEANUP);
	}
}

void io_uring_cmd_cleanup(struct io_kiocb *req)
{
	io_req_uring_cleanup(req, 0);
}

bool io_uring_try_cancel_uring_cmd(struct io_ring_ctx *ctx,
				   struct io_uring_task *tctx, bool cancel_all)
{
	struct hlist_node *tmp;
	struct io_kiocb *req;
	bool ret = false;

	lockdep_assert_held(&ctx->uring_lock);

	hlist_for_each_entry_safe(req, tmp, &ctx->cancelable_uring_cmd,
			hash_node) {
		struct io_uring_cmd *cmd = io_kiocb_to_cmd(req,
				struct io_uring_cmd);
		struct file *file = req->file;

		if (!cancel_all && req->tctx != tctx)
			continue;

		if (cmd->flags & IORING_URING_CMD_CANCELABLE) {
			file->f_op->uring_cmd(cmd, IO_URING_F_CANCEL |
						   IO_URING_F_COMPLETE_DEFER);
			ret = true;
		}
	}
	io_submit_flush_completions(ctx);
	return ret;
}

static void io_uring_cmd_del_cancelable(struct io_uring_cmd *cmd,
		unsigned int issue_flags)
{
	struct io_kiocb *req = cmd_to_io_kiocb(cmd);
	struct io_ring_ctx *ctx = req->ctx;

	if (!(cmd->flags & IORING_URING_CMD_CANCELABLE))
		return;

	cmd->flags &= ~IORING_URING_CMD_CANCELABLE;
	io_ring_submit_lock(ctx, issue_flags);
	hlist_del(&req->hash_node);
	io_ring_submit_unlock(ctx, issue_flags);
}

/*
 * Mark this command as concelable, then io_uring_try_cancel_uring_cmd()
 * will try to cancel this issued command by sending ->uring_cmd() with
 * issue_flags of IO_URING_F_CANCEL.
 *
 * The command is guaranteed to not be done when calling ->uring_cmd()
 * with IO_URING_F_CANCEL, but it is driver's responsibility to deal
 * with race between io_uring canceling and normal completion.
 */
void io_uring_cmd_mark_cancelable(struct io_uring_cmd *cmd,
		unsigned int issue_flags)
{
	struct io_kiocb *req = cmd_to_io_kiocb(cmd);
	struct io_ring_ctx *ctx = req->ctx;

	if (!(cmd->flags & IORING_URING_CMD_CANCELABLE)) {
		cmd->flags |= IORING_URING_CMD_CANCELABLE;
		io_ring_submit_lock(ctx, issue_flags);
		hlist_add_head(&req->hash_node, &ctx->cancelable_uring_cmd);
		io_ring_submit_unlock(ctx, issue_flags);
	}
}
EXPORT_SYMBOL_GPL(io_uring_cmd_mark_cancelable);

static void io_uring_cmd_work(struct io_kiocb *req, io_tw_token_t tw)
{
	struct io_uring_cmd *ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);
	unsigned int flags = IO_URING_F_COMPLETE_DEFER;

	if (io_should_terminate_tw(req->ctx))
		flags |= IO_URING_F_TASK_DEAD;

	/* task_work executor checks the deffered list completion */
	ioucmd->task_work_cb(ioucmd, flags);
}

void __io_uring_cmd_do_in_task(struct io_uring_cmd *ioucmd,
			io_uring_cmd_tw_t task_work_cb,
			unsigned flags)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);

	if (WARN_ON_ONCE(req->flags & REQ_F_APOLL_MULTISHOT))
		return;

	ioucmd->task_work_cb = task_work_cb;
	req->io_task_work.func = io_uring_cmd_work;
	__io_req_task_work_add(req, flags);
}
EXPORT_SYMBOL_GPL(__io_uring_cmd_do_in_task);

static inline void io_req_set_cqe32_extra(struct io_kiocb *req,
					  u64 extra1, u64 extra2)
{
	req->big_cqe.extra1 = extra1;
	req->big_cqe.extra2 = extra2;
}

/*
 * Called by consumers of io_uring_cmd, if they originally returned
 * -EIOCBQUEUED upon receiving the command.
 */
void __io_uring_cmd_done(struct io_uring_cmd *ioucmd, s32 ret, u64 res2,
		       unsigned issue_flags, bool is_cqe32)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);

	if (WARN_ON_ONCE(req->flags & REQ_F_APOLL_MULTISHOT))
		return;

	io_uring_cmd_del_cancelable(ioucmd, issue_flags);

	if (ret < 0)
		req_set_fail(req);

	io_req_set_res(req, ret, 0);
	if (is_cqe32) {
		if (req->ctx->flags & IORING_SETUP_CQE_MIXED)
			req->cqe.flags |= IORING_CQE_F_32;
		io_req_set_cqe32_extra(req, res2, 0);
	}
	io_req_uring_cleanup(req, issue_flags);
	if (req->ctx->flags & IORING_SETUP_IOPOLL) {
		/* order with io_iopoll_req_issued() checking ->iopoll_complete */
		smp_store_release(&req->iopoll_completed, 1);
	} else if (issue_flags & IO_URING_F_COMPLETE_DEFER) {
		if (WARN_ON_ONCE(issue_flags & IO_URING_F_UNLOCKED))
			return;
		io_req_complete_defer(req);
	} else {
		req->io_task_work.func = io_req_task_complete;
		io_req_task_work_add(req);
	}
}
EXPORT_SYMBOL_GPL(__io_uring_cmd_done);

int io_uring_cmd_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_uring_cmd *ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);
	struct io_async_cmd *ac;

	if (sqe->__pad1)
		return -EINVAL;

	ioucmd->flags = READ_ONCE(sqe->uring_cmd_flags);
	if (ioucmd->flags & ~IORING_URING_CMD_MASK)
		return -EINVAL;

	if (ioucmd->flags & IORING_URING_CMD_FIXED) {
		if (ioucmd->flags & IORING_URING_CMD_MULTISHOT)
			return -EINVAL;
		req->buf_index = READ_ONCE(sqe->buf_index);
	}

	if (!!(ioucmd->flags & IORING_URING_CMD_MULTISHOT) !=
	    !!(req->flags & REQ_F_BUFFER_SELECT))
		return -EINVAL;

	ioucmd->cmd_op = READ_ONCE(sqe->cmd_op);

	ac = io_uring_alloc_async_data(&req->ctx->cmd_cache, req);
	if (!ac)
		return -ENOMEM;
	ioucmd->sqe = sqe;
	return 0;
}

void io_uring_cmd_sqe_copy(struct io_kiocb *req)
{
	struct io_uring_cmd *ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);
	struct io_async_cmd *ac = req->async_data;

	/* Should not happen, as REQ_F_SQE_COPIED covers this */
	if (WARN_ON_ONCE(ioucmd->sqe == ac->sqes))
		return;
	memcpy(ac->sqes, ioucmd->sqe, uring_sqe_size(req->ctx));
	ioucmd->sqe = ac->sqes;
}

int io_uring_cmd(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_uring_cmd *ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);
	struct io_ring_ctx *ctx = req->ctx;
	struct file *file = req->file;
	int ret;

	if (!file->f_op->uring_cmd)
		return -EOPNOTSUPP;

	ret = security_uring_cmd(ioucmd);
	if (ret)
		return ret;

	if (ctx->flags & IORING_SETUP_SQE128)
		issue_flags |= IO_URING_F_SQE128;
	if (ctx->flags & (IORING_SETUP_CQE32 | IORING_SETUP_CQE_MIXED))
		issue_flags |= IO_URING_F_CQE32;
	if (io_is_compat(ctx))
		issue_flags |= IO_URING_F_COMPAT;
	if (ctx->flags & IORING_SETUP_IOPOLL) {
		if (!file->f_op->uring_cmd_iopoll)
			return -EOPNOTSUPP;
		issue_flags |= IO_URING_F_IOPOLL;
		req->iopoll_completed = 0;
		if (ctx->flags & IORING_SETUP_HYBRID_IOPOLL) {
			/* make sure every req only blocks once */
			req->flags &= ~REQ_F_IOPOLL_STATE;
			req->iopoll_start = ktime_get_ns();
		}
	}

	ret = file->f_op->uring_cmd(ioucmd, issue_flags);
	if (ioucmd->flags & IORING_URING_CMD_MULTISHOT) {
		if (ret >= 0)
			return IOU_ISSUE_SKIP_COMPLETE;
	}
	if (ret == -EAGAIN) {
		ioucmd->flags |= IORING_URING_CMD_REISSUE;
		return ret;
	}
	if (ret == -EIOCBQUEUED)
		return ret;
	if (ret < 0)
		req_set_fail(req);
	io_req_uring_cleanup(req, issue_flags);
	io_req_set_res(req, ret, 0);
	return IOU_COMPLETE;
}

int io_uring_cmd_import_fixed(u64 ubuf, unsigned long len, int rw,
			      struct iov_iter *iter,
			      struct io_uring_cmd *ioucmd,
			      unsigned int issue_flags)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);

	if (WARN_ON_ONCE(!(ioucmd->flags & IORING_URING_CMD_FIXED)))
		return -EINVAL;

	return io_import_reg_buf(req, iter, ubuf, len, rw, issue_flags);
}
EXPORT_SYMBOL_GPL(io_uring_cmd_import_fixed);

int io_uring_cmd_import_fixed_vec(struct io_uring_cmd *ioucmd,
				  const struct iovec __user *uvec,
				  size_t uvec_segs,
				  int ddir, struct iov_iter *iter,
				  unsigned issue_flags)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);
	struct io_async_cmd *ac = req->async_data;
	int ret;

	if (WARN_ON_ONCE(!(ioucmd->flags & IORING_URING_CMD_FIXED)))
		return -EINVAL;

	ret = io_prep_reg_iovec(req, &ac->vec, uvec, uvec_segs);
	if (ret)
		return ret;

	return io_import_reg_vec(ddir, iter, req, &ac->vec, uvec_segs,
				 issue_flags);
}
EXPORT_SYMBOL_GPL(io_uring_cmd_import_fixed_vec);

void io_uring_cmd_issue_blocking(struct io_uring_cmd *ioucmd)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);

	io_req_queue_iowq(req);
}

int io_cmd_poll_multishot(struct io_uring_cmd *cmd,
			  unsigned int issue_flags, __poll_t mask)
{
	struct io_kiocb *req = cmd_to_io_kiocb(cmd);
	int ret;

	if (likely(req->flags & REQ_F_APOLL_MULTISHOT))
		return 0;

	req->flags |= REQ_F_APOLL_MULTISHOT;
	mask &= ~EPOLLONESHOT;

	ret = io_arm_apoll(req, issue_flags, mask);
	return ret == IO_APOLL_OK ? -EIOCBQUEUED : -ECANCELED;
}

bool io_uring_cmd_post_mshot_cqe32(struct io_uring_cmd *cmd,
				   unsigned int issue_flags,
				   struct io_uring_cqe cqe[2])
{
	struct io_kiocb *req = cmd_to_io_kiocb(cmd);

	if (WARN_ON_ONCE(!(issue_flags & IO_URING_F_MULTISHOT)))
		return false;
	return io_req_post_cqe32(req, cqe);
}

/*
 * Work with io_uring_mshot_cmd_post_cqe() together for committing the
 * provided buffer upfront
 */
struct io_br_sel io_uring_cmd_buffer_select(struct io_uring_cmd *ioucmd,
					    unsigned buf_group, size_t *len,
					    unsigned int issue_flags)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);

	if (!(ioucmd->flags & IORING_URING_CMD_MULTISHOT))
		return (struct io_br_sel) { .val = -EINVAL };

	if (WARN_ON_ONCE(!io_do_buffer_select(req)))
		return (struct io_br_sel) { .val = -EINVAL };

	return io_buffer_select(req, len, buf_group, issue_flags);
}
EXPORT_SYMBOL_GPL(io_uring_cmd_buffer_select);

/*
 * Return true if this multishot uring_cmd needs to be completed, otherwise
 * the event CQE is posted successfully.
 *
 * This function must use `struct io_br_sel` returned from
 * io_uring_cmd_buffer_select() for committing the buffer in the same
 * uring_cmd submission context.
 */
bool io_uring_mshot_cmd_post_cqe(struct io_uring_cmd *ioucmd,
				 struct io_br_sel *sel, unsigned int issue_flags)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);
	unsigned int cflags = 0;

	if (!(ioucmd->flags & IORING_URING_CMD_MULTISHOT))
		return true;

	if (sel->val > 0) {
		cflags = io_put_kbuf(req, sel->val, sel->buf_list);
		if (io_req_post_cqe(req, sel->val, cflags | IORING_CQE_F_MORE))
			return false;
	}

	io_kbuf_recycle(req, sel->buf_list, issue_flags);
	if (sel->val < 0)
		req_set_fail(req);
	io_req_set_res(req, sel->val, cflags);
	return true;
}
EXPORT_SYMBOL_GPL(io_uring_mshot_cmd_post_cqe);
