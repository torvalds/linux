/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_IO_URING_H
#define _LINUX_IO_URING_H

#include <linux/sched.h>
#include <linux/xarray.h>

struct io_wq_work_node {
	struct io_wq_work_node *next;
};

struct io_wq_work_list {
	struct io_wq_work_node *first;
	struct io_wq_work_node *last;
};

struct io_uring_task {
	/* submission side */
	struct xarray		xa;
	struct wait_queue_head	wait;
	struct file		*last;
	void			*io_wq;
	struct percpu_counter	inflight;
	atomic_t		in_idle;
	bool			sqpoll;

	spinlock_t		task_lock;
	struct io_wq_work_list	task_list;
	unsigned long		task_state;
	struct callback_head	task_work;
};

#if defined(CONFIG_IO_URING)
struct sock *io_uring_get_socket(struct file *file);
void __io_uring_task_cancel(void);
void __io_uring_files_cancel(struct files_struct *files);
void __io_uring_free(struct task_struct *tsk);

static inline void io_uring_task_cancel(void)
{
	if (current->io_uring)
		__io_uring_task_cancel();
}
static inline void io_uring_files_cancel(struct files_struct *files)
{
	if (current->io_uring)
		__io_uring_files_cancel(files);
}
static inline void io_uring_free(struct task_struct *tsk)
{
	if (tsk->io_uring)
		__io_uring_free(tsk);
}
#else
static inline struct sock *io_uring_get_socket(struct file *file)
{
	return NULL;
}
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
