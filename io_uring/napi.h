/* SPDX-License-Identifier: GPL-2.0 */

#ifndef IOU_NAPI_H
#define IOU_NAPI_H

#include <linux/kernel.h>
#include <linux/io_uring.h>
#include <net/busy_poll.h>

#ifdef CONFIG_NET_RX_BUSY_POLL

void io_napi_init(struct io_ring_ctx *ctx);
void io_napi_free(struct io_ring_ctx *ctx);

int io_register_napi(struct io_ring_ctx *ctx, void __user *arg);
int io_unregister_napi(struct io_ring_ctx *ctx, void __user *arg);

void __io_napi_add(struct io_ring_ctx *ctx, struct socket *sock);

void __io_napi_adjust_timeout(struct io_ring_ctx *ctx,
		struct io_wait_queue *iowq, ktime_t to_wait);
void __io_napi_busy_loop(struct io_ring_ctx *ctx, struct io_wait_queue *iowq);
int io_napi_sqpoll_busy_poll(struct io_ring_ctx *ctx);

static inline bool io_napi(struct io_ring_ctx *ctx)
{
	return !list_empty(&ctx->napi_list);
}

static inline void io_napi_adjust_timeout(struct io_ring_ctx *ctx,
					  struct io_wait_queue *iowq,
					  ktime_t to_wait)
{
	if (!io_napi(ctx))
		return;
	__io_napi_adjust_timeout(ctx, iowq, to_wait);
}

static inline void io_napi_busy_loop(struct io_ring_ctx *ctx,
				     struct io_wait_queue *iowq)
{
	if (!io_napi(ctx))
		return;
	__io_napi_busy_loop(ctx, iowq);
}

/*
 * io_napi_add() - Add napi id to the busy poll list
 * @req: pointer to io_kiocb request
 *
 * Add the napi id of the socket to the napi busy poll list and hash table.
 */
static inline void io_napi_add(struct io_kiocb *req)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct socket *sock;

	if (!READ_ONCE(ctx->napi_enabled))
		return;

	sock = sock_from_file(req->file);
	if (sock)
		__io_napi_add(ctx, sock);
}

#else

static inline void io_napi_init(struct io_ring_ctx *ctx)
{
}
static inline void io_napi_free(struct io_ring_ctx *ctx)
{
}
static inline int io_register_napi(struct io_ring_ctx *ctx, void __user *arg)
{
	return -EOPNOTSUPP;
}
static inline int io_unregister_napi(struct io_ring_ctx *ctx, void __user *arg)
{
	return -EOPNOTSUPP;
}
static inline bool io_napi(struct io_ring_ctx *ctx)
{
	return false;
}
static inline void io_napi_add(struct io_kiocb *req)
{
}
static inline void io_napi_adjust_timeout(struct io_ring_ctx *ctx,
					  struct io_wait_queue *iowq,
					  ktime_t to_wait)
{
}
static inline void io_napi_busy_loop(struct io_ring_ctx *ctx,
				     struct io_wait_queue *iowq)
{
}
static inline int io_napi_sqpoll_busy_poll(struct io_ring_ctx *ctx)
{
	return 0;
}
#endif /* CONFIG_NET_RX_BUSY_POLL */

#endif
