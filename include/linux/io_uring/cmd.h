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

#if defined(CONFIG_IO_URING)
int io_uring_cmd_import_fixed(u64 ubuf, unsigned long len, int rw,
			      struct iov_iter *iter, void *ioucmd);
void io_uring_cmd_done(struct io_uring_cmd *cmd, ssize_t ret, ssize_t res2,
			unsigned issue_flags);
void __io_uring_cmd_do_in_task(struct io_uring_cmd *ioucmd,
			    void (*task_work_cb)(struct io_uring_cmd *, unsigned),
			    unsigned flags);

void io_uring_cmd_mark_cancelable(struct io_uring_cmd *cmd,
		unsigned int issue_flags);

#else
static inline int io_uring_cmd_import_fixed(u64 ubuf, unsigned long len, int rw,
			      struct iov_iter *iter, void *ioucmd)
{
	return -EOPNOTSUPP;
}
static inline void io_uring_cmd_done(struct io_uring_cmd *cmd, ssize_t ret,
		ssize_t ret2, unsigned issue_flags)
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
#endif

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
