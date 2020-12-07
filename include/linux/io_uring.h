/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_IO_URING_H
#define _LINUX_IO_URING_H

#include <linux/sched.h>
#include <linux/xarray.h>

struct io_identity {
	struct files_struct		*files;
	struct mm_struct		*mm;
#ifdef CONFIG_BLK_CGROUP
	struct cgroup_subsys_state	*blkcg_css;
#endif
	const struct cred		*creds;
	struct nsproxy			*nsproxy;
	struct fs_struct		*fs;
	unsigned long			fsize;
#ifdef CONFIG_AUDIT
	kuid_t				loginuid;
	unsigned int			sessionid;
#endif
	refcount_t			count;
};

struct io_uring_task {
	/* submission side */
	struct xarray		xa;
	struct wait_queue_head	wait;
	struct file		*last;
	struct percpu_counter	inflight;
	struct io_identity	__identity;
	struct io_identity	*identity;
	atomic_t		in_idle;
	bool			sqpoll;
};

#if defined(CONFIG_IO_URING)
struct sock *io_uring_get_socket(struct file *file);
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
