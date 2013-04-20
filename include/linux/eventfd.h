/*
 *  include/linux/eventfd.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_EVENTFD_H
#define _LINUX_EVENTFD_H

#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/wait.h>

/*
 * CAREFUL: Check include/uapi/asm-generic/fcntl.h when defining
 * new flags, since they might collide with O_* ones. We want
 * to re-use O_* flags that couldn't possibly have a meaning
 * from eventfd, in order to leave a free define-space for
 * shared O_* flags.
 */
#define EFD_SEMAPHORE (1 << 0)
#define EFD_CLOEXEC O_CLOEXEC
#define EFD_NONBLOCK O_NONBLOCK

#define EFD_SHARED_FCNTL_FLAGS (O_CLOEXEC | O_NONBLOCK)
#define EFD_FLAGS_SET (EFD_SHARED_FCNTL_FLAGS | EFD_SEMAPHORE)

#ifdef CONFIG_EVENTFD

struct file *eventfd_file_create(unsigned int count, int flags);
struct eventfd_ctx *eventfd_ctx_get(struct eventfd_ctx *ctx);
void eventfd_ctx_put(struct eventfd_ctx *ctx);
struct file *eventfd_fget(int fd);
struct eventfd_ctx *eventfd_ctx_fdget(int fd);
struct eventfd_ctx *eventfd_ctx_fileget(struct file *file);
__u64 eventfd_signal(struct eventfd_ctx *ctx, __u64 n);
ssize_t eventfd_ctx_read(struct eventfd_ctx *ctx, int no_wait, __u64 *cnt);
int eventfd_ctx_remove_wait_queue(struct eventfd_ctx *ctx, wait_queue_t *wait,
				  __u64 *cnt);

#else /* CONFIG_EVENTFD */

/*
 * Ugly ugly ugly error layer to support modules that uses eventfd but
 * pretend to work in !CONFIG_EVENTFD configurations. Namely, AIO.
 */
static inline struct file *eventfd_file_create(unsigned int count, int flags)
{
	return ERR_PTR(-ENOSYS);
}

static inline struct eventfd_ctx *eventfd_ctx_fdget(int fd)
{
	return ERR_PTR(-ENOSYS);
}

static inline int eventfd_signal(struct eventfd_ctx *ctx, int n)
{
	return -ENOSYS;
}

static inline void eventfd_ctx_put(struct eventfd_ctx *ctx)
{

}

static inline ssize_t eventfd_ctx_read(struct eventfd_ctx *ctx, int no_wait,
				       __u64 *cnt)
{
	return -ENOSYS;
}

static inline int eventfd_ctx_remove_wait_queue(struct eventfd_ctx *ctx,
						wait_queue_t *wait, __u64 *cnt)
{
	return -ENOSYS;
}

#endif

#endif /* _LINUX_EVENTFD_H */

