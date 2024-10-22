// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/eventfd.h>
#include <linux/eventpoll.h>
#include <linux/io_uring.h>
#include <linux/io_uring_types.h>

#include "io-wq.h"
#include "eventfd.h"

struct io_ev_fd {
	struct eventfd_ctx	*cq_ev_fd;
	unsigned int		eventfd_async: 1;
	struct rcu_head		rcu;
	refcount_t		refs;
	atomic_t		ops;
};

enum {
	IO_EVENTFD_OP_SIGNAL_BIT,
};

static void io_eventfd_free(struct rcu_head *rcu)
{
	struct io_ev_fd *ev_fd = container_of(rcu, struct io_ev_fd, rcu);

	eventfd_ctx_put(ev_fd->cq_ev_fd);
	kfree(ev_fd);
}

static void io_eventfd_do_signal(struct rcu_head *rcu)
{
	struct io_ev_fd *ev_fd = container_of(rcu, struct io_ev_fd, rcu);

	eventfd_signal_mask(ev_fd->cq_ev_fd, EPOLL_URING_WAKE);

	if (refcount_dec_and_test(&ev_fd->refs))
		io_eventfd_free(rcu);
}

void io_eventfd_signal(struct io_ring_ctx *ctx)
{
	struct io_ev_fd *ev_fd = NULL;

	if (READ_ONCE(ctx->rings->cq_flags) & IORING_CQ_EVENTFD_DISABLED)
		return;

	guard(rcu)();

	/*
	 * rcu_dereference ctx->io_ev_fd once and use it for both for checking
	 * and eventfd_signal
	 */
	ev_fd = rcu_dereference(ctx->io_ev_fd);

	/*
	 * Check again if ev_fd exists incase an io_eventfd_unregister call
	 * completed between the NULL check of ctx->io_ev_fd at the start of
	 * the function and rcu_read_lock.
	 */
	if (unlikely(!ev_fd))
		return;
	if (!refcount_inc_not_zero(&ev_fd->refs))
		return;
	if (ev_fd->eventfd_async && !io_wq_current_is_worker())
		goto out;

	if (likely(eventfd_signal_allowed())) {
		eventfd_signal_mask(ev_fd->cq_ev_fd, EPOLL_URING_WAKE);
	} else {
		if (!atomic_fetch_or(BIT(IO_EVENTFD_OP_SIGNAL_BIT), &ev_fd->ops)) {
			call_rcu_hurry(&ev_fd->rcu, io_eventfd_do_signal);
			return;
		}
	}
out:
	if (refcount_dec_and_test(&ev_fd->refs))
		call_rcu(&ev_fd->rcu, io_eventfd_free);
}

void io_eventfd_flush_signal(struct io_ring_ctx *ctx)
{
	bool skip;

	spin_lock(&ctx->completion_lock);

	/*
	 * Eventfd should only get triggered when at least one event has been
	 * posted. Some applications rely on the eventfd notification count
	 * only changing IFF a new CQE has been added to the CQ ring. There's
	 * no depedency on 1:1 relationship between how many times this
	 * function is called (and hence the eventfd count) and number of CQEs
	 * posted to the CQ ring.
	 */
	skip = ctx->cached_cq_tail == ctx->evfd_last_cq_tail;
	ctx->evfd_last_cq_tail = ctx->cached_cq_tail;
	spin_unlock(&ctx->completion_lock);
	if (skip)
		return;

	io_eventfd_signal(ctx);
}

int io_eventfd_register(struct io_ring_ctx *ctx, void __user *arg,
			unsigned int eventfd_async)
{
	struct io_ev_fd *ev_fd;
	__s32 __user *fds = arg;
	int fd;

	ev_fd = rcu_dereference_protected(ctx->io_ev_fd,
					lockdep_is_held(&ctx->uring_lock));
	if (ev_fd)
		return -EBUSY;

	if (copy_from_user(&fd, fds, sizeof(*fds)))
		return -EFAULT;

	ev_fd = kmalloc(sizeof(*ev_fd), GFP_KERNEL);
	if (!ev_fd)
		return -ENOMEM;

	ev_fd->cq_ev_fd = eventfd_ctx_fdget(fd);
	if (IS_ERR(ev_fd->cq_ev_fd)) {
		int ret = PTR_ERR(ev_fd->cq_ev_fd);

		kfree(ev_fd);
		return ret;
	}

	spin_lock(&ctx->completion_lock);
	ctx->evfd_last_cq_tail = ctx->cached_cq_tail;
	spin_unlock(&ctx->completion_lock);

	ev_fd->eventfd_async = eventfd_async;
	ctx->has_evfd = true;
	refcount_set(&ev_fd->refs, 1);
	atomic_set(&ev_fd->ops, 0);
	rcu_assign_pointer(ctx->io_ev_fd, ev_fd);
	return 0;
}

int io_eventfd_unregister(struct io_ring_ctx *ctx)
{
	struct io_ev_fd *ev_fd;

	ev_fd = rcu_dereference_protected(ctx->io_ev_fd,
					lockdep_is_held(&ctx->uring_lock));
	if (ev_fd) {
		ctx->has_evfd = false;
		rcu_assign_pointer(ctx->io_ev_fd, NULL);
		if (refcount_dec_and_test(&ev_fd->refs))
			call_rcu(&ev_fd->rcu, io_eventfd_free);
		return 0;
	}

	return -ENXIO;
}
