/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_IO_URING_H
#define _LINUX_IO_URING_H

#include <linux/sched.h>
#include <linux/xarray.h>
#include <linux/percpu-refcount.h>

struct io_uring_task {
	/* submission side */
	struct xarray		xa;
	struct wait_queue_head	wait;
	struct file		*last;
	atomic_long_t		req_issue;

	/* completion side */
	bool			in_idle ____cacheline_aligned_in_smp;
	atomic_long_t		req_complete;
};

#if defined(CONFIG_IO_URING)
void __io_uring_task_cancel(void);
void __io_uring_files_cancel(struct files_struct *files);
void __io_uring_free(struct task_struct *tsk);

static inline void io_uring_task_cancel(void)
{
	if (current->io_uring && !xa_empty(&current->io_uring->xa))
		__io_uring_task_cancel();
}
static inline void io_uring_files_cancel(struct files_struct *files)
{
	if (current->io_uring && !xa_empty(&current->io_uring->xa))
		__io_uring_files_cancel(files);
}
static inline void io_uring_free(struct task_struct *tsk)
{
	if (tsk->io_uring)
		__io_uring_free(tsk);
}
#else
static inline void io_uring_task_cancel(void)
{
}
static inline void io_uring_files_cancel(struct files_struct *files)
{
}
static inline void io_uring_free(struct task_struct *tsk)
{
}
#endif

#endif
