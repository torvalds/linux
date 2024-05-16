/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  include/linux/eventfd.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_EVENTFD_H
#define _LINUX_EVENTFD_H

#include <linux/wait.h>
#include <linux/err.h>
#include <linux/percpu-defs.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <uapi/linux/eventfd.h>

/*
 * CAREFUL: Check include/uapi/asm-generic/fcntl.h when defining
 * new flags, since they might collide with O_* ones. We want
 * to re-use O_* flags that couldn't possibly have a meaning
 * from eventfd, in order to leave a free define-space for
 * shared O_* flags.
 */
#define EFD_SHARED_FCNTL_FLAGS (O_CLOEXEC | O_NONBLOCK)
#define EFD_FLAGS_SET (EFD_SHARED_FCNTL_FLAGS | EFD_SEMAPHORE)

struct eventfd_ctx;
struct file;

#ifdef CONFIG_EVENTFD

void eventfd_ctx_put(struct eventfd_ctx *ctx);
struct file *eventfd_fget(int fd);
struct eventfd_ctx *eventfd_ctx_fdget(int fd);
struct eventfd_ctx *eventfd_ctx_fileget(struct file *file);
void eventfd_signal_mask(struct eventfd_ctx *ctx, __poll_t mask);
int eventfd_ctx_remove_wait_queue(struct eventfd_ctx *ctx, wait_queue_entry_t *wait,
				  __u64 *cnt);
void eventfd_ctx_do_read(struct eventfd_ctx *ctx, __u64 *cnt);

static inline bool eventfd_signal_allowed(void)
{
	return !current->in_eventfd;
}

#else /* CONFIG_EVENTFD */

/*
 * Ugly ugly ugly error layer to support modules that uses eventfd but
 * pretend to work in !CONFIG_EVENTFD configurations. Namely, AIO.
 */

static inline struct eventfd_ctx *eventfd_ctx_fdget(int fd)
{
	return ERR_PTR(-ENOSYS);
}

static inline void eventfd_signal_mask(struct eventfd_ctx *ctx, __poll_t mask)
{
}

static inline void eventfd_ctx_put(struct eventfd_ctx *ctx)
{

}

static inline int eventfd_ctx_remove_wait_queue(struct eventfd_ctx *ctx,
						wait_queue_entry_t *wait, __u64 *cnt)
{
	return -ENOSYS;
}

static inline bool eventfd_signal_allowed(void)
{
	return true;
}

static inline void eventfd_ctx_do_read(struct eventfd_ctx *ctx, __u64 *cnt)
{

}

#endif

static inline void eventfd_signal(struct eventfd_ctx *ctx)
{
	eventfd_signal_mask(ctx, 0);
}

#endif /* _LINUX_EVENTFD_H */

