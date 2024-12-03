/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_IO_URING_CMD_H
#define _LINUX_IO_URING_CMD_H

#include <uapi/linux/io_uring.h>
#include <linux/io_uring_types.h>

/* only top 8 bits of sqe->uring_cmd_flags for kernel internal use */
#define IORING_URING_CMD_CANCELABLE	(1U << 30)

struct io_uring_cmd {
	struct file	*file;
	const struct io_uring_sqe *sqe;
	/* callback to defer completions to task context */
	void (*task_work_cb)(struct io_uring_cmd *cmd, unsigned);
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
			      struct iov_iter *iter, void *ioucmd);

/*
 * Completes the request, i.e. posts an io_uring CQE and deallocates @ioucmd
 * and the corresponding io_uring request.
 *
 * Note: the caller should never hard code @issue_flags and is only allowed
 * to pass the mask provided by the core io_uring code.
 */
void io_uring_cmd_done(struct io_uring_cmd *cmd, ssize_t ret, u64 res2,
			unsigned issue_flags);

void __io_uring_cmd_do_in_task(struct io_uring_cmd *ioucmd,
			    void (*task_work_cb)(struct io_uring_cmd *, unsigned),
			    unsigned flags);

/*
 * Note: the caller should never hard code @issue_flags and only use the
 * mask provided by the core io_uring code.
 */
void io_uring_cmd_mark_cancelable(struct io_uring_cmd *cmd,
		unsigned int issue_flags);

/* Execute the request from a blocking context */
void io_uring_cmd_issue_blocking(struct io_uring_cmd *ioucmd);

#else
static inline int io_uring_cmd_import_fixed(u64 ubuf, unsigned long len, int rw,
			      struct iov_iter *iter, void *ioucmd)
{
	return -EOPNOTSUPP;
}
static inline void io_uring_cmd_done(struct io_uring_cmd *cmd, ssize_t ret,
		u64 ret2, unsigned issue_flags)
{
}
static inline void __io_uring_cmd_do_in_task(struct io_uring_cmd *ioucmd,
			    void (*task_work_cb)(struct io_uring_cmd *, unsigned),
			    unsigned flags)
{
}
static inline void io_uring_cmd_mark_cancelable(struct io_uring_cmd *cmd,
		unsigned int issue_flags)
{
}
static inline void io_uring_cmd_issue_blocking(struct io_uring_cmd *ioucmd)
{
}
#endif

/*
 * Polled completions must ensure they are coming from a poll queue, and
 * hence are completed inside the usual poll handling loops.
 */
static inline void io_uring_cmd_iopoll_done(struct io_uring_cmd *ioucmd,
					    ssize_t ret, ssize_t res2)
{
	lockdep_assert(in_task());
	io_uring_cmd_done(ioucmd, ret, res2, 0);
}

/* users must follow the IOU_F_TWQ_LAZY_WAKE semantics */
static inline void io_uring_cmd_do_in_task_lazy(struct io_uring_cmd *ioucmd,
			void (*task_work_cb)(struct io_uring_cmd *, unsigned))
{
	__io_uring_cmd_do_in_task(ioucmd, task_work_cb, IOU_F_TWQ_LAZY_WAKE);
}

static inline void io_uring_cmd_complete_in_task(struct io_uring_cmd *ioucmd,
			void (*task_work_cb)(struct io_uring_cmd *, unsigned))
{
	__io_uring_cmd_do_in_task(ioucmd, task_work_cb, 0);
}

static inline struct task_struct *io_uring_cmd_get_task(struct io_uring_cmd *cmd)
{
	return cmd_to_io_kiocb(cmd)->task;
}

#endif /* _LINUX_IO_URING_CMD_H */
