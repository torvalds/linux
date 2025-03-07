// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/io_uring/cmd.h>
#include <linux/io_uring/net.h>
#include <linux/security.h>
#include <linux/nospec.h>
#include <net/sock.h>

#include <uapi/linux/io_uring.h>
#include <asm/ioctls.h>

#include "io_uring.h"
#include "alloc_cache.h"
#include "rsrc.h"
#include "uring_cmd.h"

static void io_req_uring_cleanup(struct io_kiocb *req, unsigned int issue_flags)
{
	struct io_uring_cmd *ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);
	struct io_uring_cmd_data *cache = req->async_data;

	if (cache->op_data) {
		kfree(cache->op_data);
		cache->op_data = NULL;
	}

	if (issue_flags & IO_URING_F_UNLOCKED)
		return;
	if (io_alloc_cache_put(&req->ctx->uring_cache, cache)) {
		ioucmd->sqe = NULL;
		req->async_data = NULL;
		req->flags &= ~REQ_F_ASYNC_DATA;
	}
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

	if (io_should_terminate_tw())
		flags |= IO_URING_F_TASK_DEAD;

	/* task_work executor checks the deffered list completion */
	ioucmd->task_work_cb(ioucmd, flags);
}

void __io_uring_cmd_do_in_task(struct io_uring_cmd *ioucmd,
			void (*task_work_cb)(struct io_uring_cmd *, unsigned),
			unsigned flags)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);

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
void io_uring_cmd_done(struct io_uring_cmd *ioucmd, ssize_t ret, u64 res2,
		       unsigned issue_flags)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);

	io_uring_cmd_del_cancelable(ioucmd, issue_flags);

	if (ret < 0)
		req_set_fail(req);

	io_req_set_res(req, ret, 0);
	if (req->ctx->flags & IORING_SETUP_CQE32)
		io_req_set_cqe32_extra(req, res2, 0);
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
EXPORT_SYMBOL_GPL(io_uring_cmd_done);

static int io_uring_cmd_prep_setup(struct io_kiocb *req,
				   const struct io_uring_sqe *sqe)
{
	struct io_uring_cmd *ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);
	struct io_uring_cmd_data *cache;

	cache = io_uring_alloc_async_data(&req->ctx->uring_cache, req);
	if (!cache)
		return -ENOMEM;
	cache->op_data = NULL;

	/*
	 * Unconditionally cache the SQE for now - this is only needed for
	 * requests that go async, but prep handlers must ensure that any
	 * sqe data is stable beyond prep. Since uring_cmd is special in
	 * that it doesn't read in per-op data, play it safe and ensure that
	 * any SQE data is stable beyond prep. This can later get relaxed.
	 */
	memcpy(cache->sqes, sqe, uring_sqe_size(req->ctx));
	ioucmd->sqe = cache->sqes;
	return 0;
}

int io_uring_cmd_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe)
{
	struct io_uring_cmd *ioucmd = io_kiocb_to_cmd(req, struct io_uring_cmd);

	if (sqe->__pad1)
		return -EINVAL;

	ioucmd->flags = READ_ONCE(sqe->uring_cmd_flags);
	if (ioucmd->flags & ~IORING_URING_CMD_MASK)
		return -EINVAL;

	if (ioucmd->flags & IORING_URING_CMD_FIXED)
		req->buf_index = READ_ONCE(sqe->buf_index);

	ioucmd->cmd_op = READ_ONCE(sqe->cmd_op);

	return io_uring_cmd_prep_setup(req, sqe);
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
	if (ctx->flags & IORING_SETUP_CQE32)
		issue_flags |= IO_URING_F_CQE32;
	if (io_is_compat(ctx))
		issue_flags |= IO_URING_F_COMPAT;
	if (ctx->flags & IORING_SETUP_IOPOLL) {
		if (!file->f_op->uring_cmd_iopoll)
			return -EOPNOTSUPP;
		issue_flags |= IO_URING_F_IOPOLL;
		req->iopoll_completed = 0;
	}

	ret = file->f_op->uring_cmd(ioucmd, issue_flags);
	if (ret == -EAGAIN || ret == -EIOCBQUEUED)
		return ret;
	if (ret < 0)
		req_set_fail(req);
	io_req_uring_cleanup(req, issue_flags);
	io_req_set_res(req, ret, 0);
	return IOU_OK;
}

int io_uring_cmd_import_fixed(u64 ubuf, unsigned long len, int rw,
			      struct iov_iter *iter,
			      struct io_uring_cmd *ioucmd,
			      unsigned int issue_flags)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);

	return io_import_reg_buf(req, iter, ubuf, len, rw, issue_flags);
}
EXPORT_SYMBOL_GPL(io_uring_cmd_import_fixed);

void io_uring_cmd_issue_blocking(struct io_uring_cmd *ioucmd)
{
	struct io_kiocb *req = cmd_to_io_kiocb(ioucmd);

	io_req_queue_iowq(req);
}

static inline int io_uring_cmd_getsockopt(struct socket *sock,
					  struct io_uring_cmd *cmd,
					  unsigned int issue_flags)
{
	bool compat = !!(issue_flags & IO_URING_F_COMPAT);
	int optlen, optname, level, err;
	void __user *optval;

	level = READ_ONCE(cmd->sqe->level);
	if (level != SOL_SOCKET)
		return -EOPNOTSUPP;

	optval = u64_to_user_ptr(READ_ONCE(cmd->sqe->optval));
	optname = READ_ONCE(cmd->sqe->optname);
	optlen = READ_ONCE(cmd->sqe->optlen);

	err = do_sock_getsockopt(sock, compat, level, optname,
				 USER_SOCKPTR(optval),
				 KERNEL_SOCKPTR(&optlen));
	if (err)
		return err;

	/* On success, return optlen */
	return optlen;
}

static inline int io_uring_cmd_setsockopt(struct socket *sock,
					  struct io_uring_cmd *cmd,
					  unsigned int issue_flags)
{
	bool compat = !!(issue_flags & IO_URING_F_COMPAT);
	int optname, optlen, level;
	void __user *optval;
	sockptr_t optval_s;

	optval = u64_to_user_ptr(READ_ONCE(cmd->sqe->optval));
	optname = READ_ONCE(cmd->sqe->optname);
	optlen = READ_ONCE(cmd->sqe->optlen);
	level = READ_ONCE(cmd->sqe->level);
	optval_s = USER_SOCKPTR(optval);

	return do_sock_setsockopt(sock, compat, level, optname, optval_s,
				  optlen);
}

#if defined(CONFIG_NET)
int io_uring_cmd_sock(struct io_uring_cmd *cmd, unsigned int issue_flags)
{
	struct socket *sock = cmd->file->private_data;
	struct sock *sk = sock->sk;
	struct proto *prot = READ_ONCE(sk->sk_prot);
	int ret, arg = 0;

	if (!prot || !prot->ioctl)
		return -EOPNOTSUPP;

	switch (cmd->cmd_op) {
	case SOCKET_URING_OP_SIOCINQ:
		ret = prot->ioctl(sk, SIOCINQ, &arg);
		if (ret)
			return ret;
		return arg;
	case SOCKET_URING_OP_SIOCOUTQ:
		ret = prot->ioctl(sk, SIOCOUTQ, &arg);
		if (ret)
			return ret;
		return arg;
	case SOCKET_URING_OP_GETSOCKOPT:
		return io_uring_cmd_getsockopt(sock, cmd, issue_flags);
	case SOCKET_URING_OP_SETSOCKOPT:
		return io_uring_cmd_setsockopt(sock, cmd, issue_flags);
	default:
		return -EOPNOTSUPP;
	}
}
EXPORT_SYMBOL_GPL(io_uring_cmd_sock);
#endif
