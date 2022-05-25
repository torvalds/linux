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

struct file *io_file_get_normal(struct io_kiocb *req, int fd);
struct file *io_file_get_fixed(struct io_kiocb *req, int fd,
			       unsigned issue_flags);
int io_fixed_fd_install(struct io_kiocb *req, unsigned int issue_flags,
			struct file *file, unsigned int file_slot);

int io_rsrc_node_switch_start(struct io_ring_ctx *ctx);
int io_queue_rsrc_removal(struct io_rsrc_data *data, unsigned idx,
			  struct io_rsrc_node *node, void *rsrc);
void io_rsrc_node_switch(struct io_ring_ctx *ctx,
			 struct io_rsrc_data *data_to_kill);
bool io_is_uring_fops(struct file *file);

#endif
