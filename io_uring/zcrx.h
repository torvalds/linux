// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_ZC_RX_H
#define IOU_ZC_RX_H

#include <linux/io_uring_types.h>
#include <net/page_pool/types.h>

struct io_zcrx_area {
	struct net_iov_area	nia;
	struct io_zcrx_ifq	*ifq;

	u16			area_id;
	struct page		**pages;

	/* freelist */
	spinlock_t		freelist_lock ____cacheline_aligned_in_smp;
	u32			free_count;
	u32			*freelist;
};

struct io_zcrx_ifq {
	struct io_ring_ctx		*ctx;
	struct io_zcrx_area		*area;

	struct io_uring			*rq_ring;
	struct io_uring_zcrx_rqe	*rqes;
	u32				rq_entries;

	u32				if_rxq;
};

#if defined(CONFIG_IO_URING_ZCRX)
int io_register_zcrx_ifq(struct io_ring_ctx *ctx,
			 struct io_uring_zcrx_ifq_reg __user *arg);
void io_unregister_zcrx_ifqs(struct io_ring_ctx *ctx);
void io_shutdown_zcrx_ifqs(struct io_ring_ctx *ctx);
#else
static inline int io_register_zcrx_ifq(struct io_ring_ctx *ctx,
					struct io_uring_zcrx_ifq_reg __user *arg)
{
	return -EOPNOTSUPP;
}
static inline void io_unregister_zcrx_ifqs(struct io_ring_ctx *ctx)
{
}
static inline void io_shutdown_zcrx_ifqs(struct io_ring_ctx *ctx)
{
}
#endif

#endif
