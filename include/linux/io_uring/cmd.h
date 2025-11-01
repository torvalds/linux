/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_IO_URING_CMD_H
#define _LINUX_IO_URING_CMD_H

#include <uapi/linux/io_uring.h>
#include <linux/io_uring_types.h>
#include <linux/blk-mq.h>

/* only top 8 bits of sqe->uring_cmd_flags for kernel internal use */
#define IORING_URING_CMD_CANCELABLE	(1U << 30)
/* io_uring_cmd is being issued again */
#define IORING_URING_CMD_REISSUE	(1U << 31)

typedef void (*io_uring_cmd_tw_t)(struct io_uring_cmd *cmd,
				  unsigned issue_flags);

struct io_uring_cmd {
	struct file	*file;
	const struct io_uring_sqe *sqe;
	/* callback to defer completions to task context */
	io_uring_cmd_tw_t task_work_cb;
	u32		cmd_op;
	u32		flags;
	u8		pdu[32]; /* available inline for free use */
};

static inline const void *io_uring_sqe_cmd(const struct io_uring_sqe *sqe)
{
	return sqe->cmd;
}

static inline void io_uring_cmd_private_sz_check(size_t cmd_sz)
{
	BUILD_BUG_ON(cmd_sz > sizeof_field(struct io_uring_cmd, pdu));
}
#define io_uring_cmd_to_pdu(cmd, pdu_type) ( \
	io_uring_cmd_private_sz_check(sizeof(pdu_type)), \
	((pdu_type *)&(cmd)->pdu) \
)

#if defined(CONFIG_IO_URING)
int io_uring_cmd_import_fixed(u64 ubuf, unsigned long len, int rw,
			      struct iov_iter *iter,
			      struct io_uring_cmd *ioucmd,
			      unsigned int issue_flags);
int io_uring_cmd_import_fixed_vec(struct io_uring_cmd *ioucmd,
				  const struct iovec __user *uvec,
				  size_t uvec_segs,
				  int ddir, struct iov_iter *iter,
				  unsigned issue_flags);

/*
 * Completes the request, i.e. posts an io_uring CQE and deallocates @ioucmd
 * and the corresponding io_uring request.
 *
 * Note: the caller should never hard code @issue_flags and is only allowed
 * to pass the mask provided by the core io_uring code.
 */
void __io_uring_cmd_done(struct io_uring_cmd *cmd, s32 ret, u64 res2,
			 unsigned issue_flags, bool is_cqe32);

void __io_uring_cmd_do_in_task(struct io_uring_cmd *ioucmd,
			    io_uring_cmd_tw_t task_work_cb,
			    unsigned flags);

/*
 * Note: the caller should never hard code @issue_flags and only use the
 * mask provided by the core io_uring code.
 */
void io_uring_cmd_mark_cancelable(struct io_uring_cmd *cmd,
		unsigned int issue_flags);

/* Execute the request from a blocking context */
void io_uring_cmd_issue_blocking(struct io_uring_cmd *ioucmd);

/*
 * Select a buffer from the provided buffer group for multishot uring_cmd.
 * Returns the selected buffer address and size.
 */
struct io_br_sel io_uring_cmd_buffer_select(struct io_uring_cmd *ioucmd,
					    unsigned buf_group, size_t *len,
					    unsigned int issue_flags);

/*
 * Complete a multishot uring_cmd event. This will post a CQE to the completion
 * queue and update the provided buffer.
 */
bool io_uring_mshot_cmd_post_cqe(struct io_uring_cmd *ioucmd,
				 struct io_br_sel *sel, unsigned int issue_flags);

#else
static inline int
io_uring_cmd_import_fixed(u64 ubuf, unsigned long len, int rw,
			  struct iov_iter *iter, struct io_uring_cmd *ioucmd,
			  unsigned int issue_flags)
{
	return -EOPNOTSUPP;
}
static inline int io_uring_cmd_import_fixed_vec(struct io_uring_cmd *ioucmd,
						const struct iovec __user *uvec,
						size_t uvec_segs,
						int ddir, struct iov_iter *iter,
						unsigned issue_flags)
{
	return -EOPNOTSUPP;
}
static inline void __io_uring_cmd_done(struct io_uring_cmd *cmd, s32 ret,
		u64 ret2, unsigned issue_flags, bool is_cqe32)
{
}
static inline void __io_uring_cmd_do_in_task(struct io_uring_cmd *ioucmd,
			    io_uring_cmd_tw_t task_work_cb, unsigned flags)
{
}
static inline void io_uring_cmd_mark_cancelable(struct io_uring_cmd *cmd,
		unsigned int issue_flags)
{
}
static inline void io_uring_cmd_issue_blocking(struct io_uring_cmd *ioucmd)
{
}
static inline struct io_br_sel
io_uring_cmd_buffer_select(struct io_uring_cmd *ioucmd, unsigned buf_group,
			   size_t *len, unsigned int issue_flags)
{
	return (struct io_br_sel) { .val = -EOPNOTSUPP };
}
static inline bool io_uring_mshot_cmd_post_cqe(struct io_uring_cmd *ioucmd,
				struct io_br_sel *sel, unsigned int issue_flags)
{
	return true;
}
#endif

/* users must follow the IOU_F_TWQ_LAZY_WAKE semantics */
static inline void io_uring_cmd_do_in_task_lazy(struct io_uring_cmd *ioucmd,
			io_uring_cmd_tw_t task_work_cb)
{
	__io_uring_cmd_do_in_task(ioucmd, task_work_cb, IOU_F_TWQ_LAZY_WAKE);
}

static inline void io_uring_cmd_complete_in_task(struct io_uring_cmd *ioucmd,
			io_uring_cmd_tw_t task_work_cb)
{
	__io_uring_cmd_do_in_task(ioucmd, task_work_cb, 0);
}

static inline struct task_struct *io_uring_cmd_get_task(struct io_uring_cmd *cmd)
{
	return cmd_to_io_kiocb(cmd)->tctx->task;
}

/*
 * Return uring_cmd's context reference as its context handle for driver to
 * track per-context resource, such as registered kernel IO buffer
 */
static inline void *io_uring_cmd_ctx_handle(struct io_uring_cmd *cmd)
{
	return cmd_to_io_kiocb(cmd)->ctx;
}

static inline void io_uring_cmd_done(struct io_uring_cmd *ioucmd, s32 ret,
				     unsigned issue_flags)
{
	return __io_uring_cmd_done(ioucmd, ret, 0, issue_flags, false);
}

static inline void io_uring_cmd_done32(struct io_uring_cmd *ioucmd, s32 ret,
				       u64 res2, unsigned issue_flags)
{
	return __io_uring_cmd_done(ioucmd, ret, res2, issue_flags, true);
}

int io_buffer_register_bvec(struct io_uring_cmd *cmd, struct request *rq,
			    void (*release)(void *), unsigned int index,
			    unsigned int issue_flags);
int io_buffer_unregister_bvec(struct io_uring_cmd *cmd, unsigned int index,
			      unsigned int issue_flags);

#endif /* _LINUX_IO_URING_CMD_H */
