// SPDX-License-Identifier: GPL-2.0
#ifndef IOU_ZC_RX_H
#define IOU_ZC_RX_H

#include <linux/io_uring_types.h>
#include <linux/dma-buf.h>
#include <linux/socket.h>
#include <net/page_pool/types.h>
#include <net/net_trackers.h>

struct io_zcrx_mem {
	unsigned long			size;
	bool				is_dmabuf;

	struct page			**pages;
	unsigned long			nr_folios;
	struct sg_table			page_sg_table;
	unsigned long			account_pages;
	struct sg_table			*sgt;

	struct dma_buf_attachment	*attach;
	struct dma_buf			*dmabuf;
};

struct io_zcrx_area {
	struct net_iov_area	nia;
	struct io_zcrx_ifq	*ifq;
	atomic_t		*user_refs;

	bool			is_mapped;
	u16			area_id;

	/* freelist */
	spinlock_t		freelist_lock ____cacheline_aligned_in_smp;
	u32			free_count;
	u32			*freelist;

	struct io_zcrx_mem	mem;
};

struct io_zcrx_ifq {
	struct io_ring_ctx		*ctx;
	struct io_zcrx_area		*area;
	unsigned			niov_shift;

	spinlock_t			rq_lock ____cacheline_aligned_in_smp;
	struct io_uring			*rq_ring;
	struct io_uring_zcrx_rqe	*rqes;
	u32				cached_rq_head;
	u32				rq_entries;

	u32				if_rxq;
	struct device			*dev;
	struct net_device		*netdev;
	netdevice_tracker		netdev_tracker;

	/*
	 * Page pool and net configuration lock, can be taken deeper in the
	 * net stack.
	 */
	struct mutex			pp_lock;
	struct io_mapped_region		region;
};

#if defined(CONFIG_IO_URING_ZCRX)
int io_register_zcrx_ifq(struct io_ring_ctx *ctx,
			 struct io_uring_zcrx_ifq_reg __user *arg);
void io_unregister_zcrx_ifqs(struct io_ring_ctx *ctx);
void io_shutdown_zcrx_ifqs(struct io_ring_ctx *ctx);
int io_zcrx_recv(struct io_kiocb *req, struct io_zcrx_ifq *ifq,
		 struct socket *sock, unsigned int flags,
		 unsigned issue_flags, unsigned int *len);
struct io_mapped_region *io_zcrx_get_region(struct io_ring_ctx *ctx,
					    unsigned int id);
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
static inline int io_zcrx_recv(struct io_kiocb *req, struct io_zcrx_ifq *ifq,
			       struct socket *sock, unsigned int flags,
			       unsigned issue_flags, unsigned int *len)
{
	return -EOPNOTSUPP;
}
static inline struct io_mapped_region *io_zcrx_get_region(struct io_ring_ctx *ctx,
							  unsigned int id)
{
	return NULL;
}
#endif

int io_recvzc(struct io_kiocb *req, unsigned int issue_flags);
int io_recvzc_prep(struct io_kiocb *req, const struct io_uring_sqe *sqe);

#endif
