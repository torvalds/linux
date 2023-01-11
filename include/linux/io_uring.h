/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _LINUX_IO_URING_H
#define _LINUX_IO_URING_H

#include <linux/sched.h>
#include <linux/xarray.h>

#ifdef __GENKSYMS__
/*
 * ANDROID ABI HACK
 *
 * In the 5.10.162 release, the io_uring code was synced with the version
 * that is in the 5.15.y kernel tree in order to resolve a huge number of
 * potential, and known, problems with the codebase.  This makes for a more
 * secure and easier-to-update-and-maintain 5.10.y kernel tree, so this is
 * a great thing, however this caused some issues when it comes to the
 * Android KABI preservation and checking tools.
 *
 * A number of the io_uring structures get used in other core kernel
 * structures, only as "opaque" pointers, so there is not any real ABI
 * breakage.  But, due to the visibility of the structures going away, the
 * CRC values of many scheduler variables and functions were changed.
 *
 * In order to preserve the CRC values, to prevent all device kernels to be
 * forced to rebuild for no reason whatsoever from a functional point of
 * view, we need to keep around the "old" io_uring structures for the CRC
 * calculation only.  This is done by the following definitions of struct
 * io_identity and struct io_uring_task which will only be visible when the
 * CRC calculation build happens, not in any functional kernel build.
 *
 * Yes, this all is a horrible hack, and these really are not the true
 * structures that any code uses, but so life is in the world of stable
 * apis...
 * The real structures are in io_uring/io_uring.c, see the ones there if
 * you need to touch or do anything with it.
 *
 * NEVER touch these structure definitions, they are fake and not valid code.
 */
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
#endif	/* ANDROID ABI HACK */


#if defined(CONFIG_IO_URING)
struct sock *io_uring_get_socket(struct file *file);
void __io_uring_cancel(bool cancel_all);
void __io_uring_free(struct task_struct *tsk);

static inline void io_uring_files_cancel(void)
{
	if (current->io_uring)
		__io_uring_cancel(false);
}
static inline void io_uring_task_cancel(void)
{
	if (current->io_uring)
		__io_uring_cancel(true);
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
static inline void io_uring_files_cancel(void)
{
}
static inline void io_uring_free(struct task_struct *tsk)
{
}
#endif

#endif
