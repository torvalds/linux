// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/dma-map-ops.h>
#include <linux/mm.h>
#include <linux/nospec.h>
#include <linux/io_uring.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>

#include <net/page_pool/helpers.h>
#include <net/page_pool/memory_provider.h>
#include <net/netlink.h>

#include <trace/events/page_pool.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "kbuf.h"
#include "memmap.h"
#include "zcrx.h"
#include "rsrc.h"

#define IO_DMA_ATTR (DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

static void __io_zcrx_unmap_area(struct io_zcrx_ifq *ifq,
				 struct io_zcrx_area *area, int nr_mapped)
{
	int i;

	for (i = 0; i < nr_mapped; i++) {
		struct net_iov *niov = &area->nia.niovs[i];
		dma_addr_t dma;

		dma = page_pool_get_dma_addr_netmem(net_iov_to_netmem(niov));
		dma_unmap_page_attrs(ifq->dev, dma, PAGE_SIZE,
				     DMA_FROM_DEVICE, IO_DMA_ATTR);
		net_mp_niov_set_dma_addr(niov, 0);
	}
}

static void io_zcrx_unmap_area(struct io_zcrx_ifq *ifq, struct io_zcrx_area *area)
{
	if (area->is_mapped)
		__io_zcrx_unmap_area(ifq, area, area->nia.num_niovs);
}

static int io_zcrx_map_area(struct io_zcrx_ifq *ifq, struct io_zcrx_area *area)
{
	int i;

	for (i = 0; i < area->nia.num_niovs; i++) {
		struct net_iov *niov = &area->nia.niovs[i];
		dma_addr_t dma;

		dma = dma_map_page_attrs(ifq->dev, area->pages[i], 0, PAGE_SIZE,
					 DMA_FROM_DEVICE, IO_DMA_ATTR);
		if (dma_mapping_error(ifq->dev, dma))
			break;
		if (net_mp_niov_set_dma_addr(niov, dma)) {
			dma_unmap_page_attrs(ifq->dev, dma, PAGE_SIZE,
					     DMA_FROM_DEVICE, IO_DMA_ATTR);
			break;
		}
	}

	if (i != area->nia.num_niovs) {
		__io_zcrx_unmap_area(ifq, area, i);
		return -EINVAL;
	}

	area->is_mapped = true;
	return 0;
}

static void io_zcrx_sync_for_device(const struct page_pool *pool,
				    struct net_iov *niov)
{
#if defined(CONFIG_HAS_DMA) && defined(CONFIG_DMA_NEED_SYNC)
	dma_addr_t dma_addr;

	if (!dma_dev_need_sync(pool->p.dev))
		return;

	dma_addr = page_pool_get_dma_addr_netmem(net_iov_to_netmem(niov));
	__dma_sync_single_for_device(pool->p.dev, dma_addr + pool->p.offset,
				     PAGE_SIZE, pool->p.dma_dir);
#endif
}

#define IO_RQ_MAX_ENTRIES		32768

__maybe_unused
static const struct memory_provider_ops io_uring_pp_zc_ops;

static inline struct io_zcrx_area *io_zcrx_iov_to_area(const struct net_iov *niov)
{
	struct net_iov_area *owner = net_iov_owner(niov);

	return container_of(owner, struct io_zcrx_area, nia);
}

static inline atomic_t *io_get_user_counter(struct net_iov *niov)
{
	struct io_zcrx_area *area = io_zcrx_iov_to_area(niov);

	return &area->user_refs[net_iov_idx(niov)];
}

static bool io_zcrx_put_niov_uref(struct net_iov *niov)
{
	atomic_t *uref = io_get_user_counter(niov);

	if (unlikely(!atomic_read(uref)))
		return false;
	atomic_dec(uref);
	return true;
}

static int io_allocate_rbuf_ring(struct io_zcrx_ifq *ifq,
				 struct io_uring_zcrx_ifq_reg *reg,
				 struct io_uring_region_desc *rd)
{
	size_t off, size;
	void *ptr;
	int ret;

	off = sizeof(struct io_uring);
	size = off + sizeof(struct io_uring_zcrx_rqe) * reg->rq_entries;
	if (size > rd->size)
		return -EINVAL;

	ret = io_create_region_mmap_safe(ifq->ctx, &ifq->ctx->zcrx_region, rd,
					 IORING_MAP_OFF_ZCRX_REGION);
	if (ret < 0)
		return ret;

	ptr = io_region_get_ptr(&ifq->ctx->zcrx_region);
	ifq->rq_ring = (struct io_uring *)ptr;
	ifq->rqes = (struct io_uring_zcrx_rqe *)(ptr + off);
	return 0;
}

static void io_free_rbuf_ring(struct io_zcrx_ifq *ifq)
{
	io_free_region(ifq->ctx, &ifq->ctx->zcrx_region);
	ifq->rq_ring = NULL;
	ifq->rqes = NULL;
}

static void io_zcrx_free_area(struct io_zcrx_area *area)
{
	io_zcrx_unmap_area(area->ifq, area);

	kvfree(area->freelist);
	kvfree(area->nia.niovs);
	kvfree(area->user_refs);
	if (area->pages) {
		unpin_user_pages(area->pages, area->nia.num_niovs);
		kvfree(area->pages);
	}
	kfree(area);
}

static int io_zcrx_create_area(struct io_zcrx_ifq *ifq,
			       struct io_zcrx_area **res,
			       struct io_uring_zcrx_area_reg *area_reg)
{
	struct io_zcrx_area *area;
	int i, ret, nr_pages;
	struct iovec iov;

	if (area_reg->flags || area_reg->rq_area_token)
		return -EINVAL;
	if (area_reg->__resv1 || area_reg->__resv2[0] || area_reg->__resv2[1])
		return -EINVAL;
	if (area_reg->addr & ~PAGE_MASK || area_reg->len & ~PAGE_MASK)
		return -EINVAL;

	iov.iov_base = u64_to_user_ptr(area_reg->addr);
	iov.iov_len = area_reg->len;
	ret = io_buffer_validate(&iov);
	if (ret)
		return ret;

	ret = -ENOMEM;
	area = kzalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		goto err;

	area->pages = io_pin_pages((unsigned long)area_reg->addr, area_reg->len,
				   &nr_pages);
	if (IS_ERR(area->pages)) {
		ret = PTR_ERR(area->pages);
		area->pages = NULL;
		goto err;
	}
	area->nia.num_niovs = nr_pages;

	area->nia.niovs = kvmalloc_array(nr_pages, sizeof(area->nia.niovs[0]),
					 GFP_KERNEL | __GFP_ZERO);
	if (!area->nia.niovs)
		goto err;

	area->freelist = kvmalloc_array(nr_pages, sizeof(area->freelist[0]),
					GFP_KERNEL | __GFP_ZERO);
	if (!area->freelist)
		goto err;

	for (i = 0; i < nr_pages; i++)
		area->freelist[i] = i;

	area->user_refs = kvmalloc_array(nr_pages, sizeof(area->user_refs[0]),
					GFP_KERNEL | __GFP_ZERO);
	if (!area->user_refs)
		goto err;

	for (i = 0; i < nr_pages; i++) {
		struct net_iov *niov = &area->nia.niovs[i];

		niov->owner = &area->nia;
		area->freelist[i] = i;
		atomic_set(&area->user_refs[i], 0);
	}

	area->free_count = nr_pages;
	area->ifq = ifq;
	/* we're only supporting one area per ifq for now */
	area->area_id = 0;
	area_reg->rq_area_token = (u64)area->area_id << IORING_ZCRX_AREA_SHIFT;
	spin_lock_init(&area->freelist_lock);
	*res = area;
	return 0;
err:
	if (area)
		io_zcrx_free_area(area);
	return ret;
}

static struct io_zcrx_ifq *io_zcrx_ifq_alloc(struct io_ring_ctx *ctx)
{
	struct io_zcrx_ifq *ifq;

	ifq = kzalloc(sizeof(*ifq), GFP_KERNEL);
	if (!ifq)
		return NULL;

	ifq->if_rxq = -1;
	ifq->ctx = ctx;
	spin_lock_init(&ifq->lock);
	spin_lock_init(&ifq->rq_lock);
	return ifq;
}

static void io_zcrx_drop_netdev(struct io_zcrx_ifq *ifq)
{
	spin_lock(&ifq->lock);
	if (ifq->netdev) {
		netdev_put(ifq->netdev, &ifq->netdev_tracker);
		ifq->netdev = NULL;
	}
	spin_unlock(&ifq->lock);
}

static void io_zcrx_ifq_free(struct io_zcrx_ifq *ifq)
{
	io_zcrx_drop_netdev(ifq);

	if (ifq->area)
		io_zcrx_free_area(ifq->area);
	if (ifq->dev)
		put_device(ifq->dev);

	io_free_rbuf_ring(ifq);
	kfree(ifq);
}

int io_register_zcrx_ifq(struct io_ring_ctx *ctx,
			  struct io_uring_zcrx_ifq_reg __user *arg)
{
	struct io_uring_zcrx_area_reg area;
	struct io_uring_zcrx_ifq_reg reg;
	struct io_uring_region_desc rd;
	struct io_zcrx_ifq *ifq;
	int ret;

	/*
	 * 1. Interface queue allocation.
	 * 2. It can observe data destined for sockets of other tasks.
	 */
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	/* mandatory io_uring features for zc rx */
	if (!(ctx->flags & IORING_SETUP_DEFER_TASKRUN &&
	      ctx->flags & IORING_SETUP_CQE32))
		return -EINVAL;
	if (ctx->ifq)
		return -EBUSY;
	if (copy_from_user(&reg, arg, sizeof(reg)))
		return -EFAULT;
	if (copy_from_user(&rd, u64_to_user_ptr(reg.region_ptr), sizeof(rd)))
		return -EFAULT;
	if (memchr_inv(&reg.__resv, 0, sizeof(reg.__resv)))
		return -EINVAL;
	if (reg.if_rxq == -1 || !reg.rq_entries || reg.flags)
		return -EINVAL;
	if (reg.rq_entries > IO_RQ_MAX_ENTRIES) {
		if (!(ctx->flags & IORING_SETUP_CLAMP))
			return -EINVAL;
		reg.rq_entries = IO_RQ_MAX_ENTRIES;
	}
	reg.rq_entries = roundup_pow_of_two(reg.rq_entries);

	if (copy_from_user(&area, u64_to_user_ptr(reg.area_ptr), sizeof(area)))
		return -EFAULT;

	ifq = io_zcrx_ifq_alloc(ctx);
	if (!ifq)
		return -ENOMEM;

	ret = io_allocate_rbuf_ring(ifq, &reg, &rd);
	if (ret)
		goto err;

	ret = io_zcrx_create_area(ifq, &ifq->area, &area);
	if (ret)
		goto err;

	ifq->rq_entries = reg.rq_entries;
	ifq->if_rxq = reg.if_rxq;

	ret = -ENODEV;
	ifq->netdev = netdev_get_by_index(current->nsproxy->net_ns, reg.if_idx,
					  &ifq->netdev_tracker, GFP_KERNEL);
	if (!ifq->netdev)
		goto err;

	ifq->dev = ifq->netdev->dev.parent;
	if (!ifq->dev)
		return -EOPNOTSUPP;
	get_device(ifq->dev);

	ret = io_zcrx_map_area(ifq, ifq->area);
	if (ret)
		goto err;

	reg.offsets.rqes = sizeof(struct io_uring);
	reg.offsets.head = offsetof(struct io_uring, head);
	reg.offsets.tail = offsetof(struct io_uring, tail);

	if (copy_to_user(arg, &reg, sizeof(reg)) ||
	    copy_to_user(u64_to_user_ptr(reg.region_ptr), &rd, sizeof(rd))) {
		ret = -EFAULT;
		goto err;
	}
	if (copy_to_user(u64_to_user_ptr(reg.area_ptr), &area, sizeof(area))) {
		ret = -EFAULT;
		goto err;
	}
	ctx->ifq = ifq;
	return 0;
err:
	io_zcrx_ifq_free(ifq);
	return ret;
}

void io_unregister_zcrx_ifqs(struct io_ring_ctx *ctx)
{
	struct io_zcrx_ifq *ifq = ctx->ifq;

	lockdep_assert_held(&ctx->uring_lock);

	if (!ifq)
		return;

	ctx->ifq = NULL;
	io_zcrx_ifq_free(ifq);
}

static struct net_iov *__io_zcrx_get_free_niov(struct io_zcrx_area *area)
{
	unsigned niov_idx;

	lockdep_assert_held(&area->freelist_lock);

	niov_idx = area->freelist[--area->free_count];
	return &area->nia.niovs[niov_idx];
}

static void io_zcrx_return_niov_freelist(struct net_iov *niov)
{
	struct io_zcrx_area *area = io_zcrx_iov_to_area(niov);

	spin_lock_bh(&area->freelist_lock);
	area->freelist[area->free_count++] = net_iov_idx(niov);
	spin_unlock_bh(&area->freelist_lock);
}

static void io_zcrx_return_niov(struct net_iov *niov)
{
	netmem_ref netmem = net_iov_to_netmem(niov);

	page_pool_put_unrefed_netmem(niov->pp, netmem, -1, false);
}

static void io_zcrx_scrub(struct io_zcrx_ifq *ifq)
{
	struct io_zcrx_area *area = ifq->area;
	int i;

	if (!area)
		return;

	/* Reclaim back all buffers given to the user space. */
	for (i = 0; i < area->nia.num_niovs; i++) {
		struct net_iov *niov = &area->nia.niovs[i];
		int nr;

		if (!atomic_read(io_get_user_counter(niov)))
			continue;
		nr = atomic_xchg(io_get_user_counter(niov), 0);
		if (nr && !page_pool_unref_netmem(net_iov_to_netmem(niov), nr))
			io_zcrx_return_niov(niov);
	}
}

void io_shutdown_zcrx_ifqs(struct io_ring_ctx *ctx)
{
	lockdep_assert_held(&ctx->uring_lock);

	if (ctx->ifq)
		io_zcrx_scrub(ctx->ifq);
}

static inline u32 io_zcrx_rqring_entries(struct io_zcrx_ifq *ifq)
{
	u32 entries;

	entries = smp_load_acquire(&ifq->rq_ring->tail) - ifq->cached_rq_head;
	return min(entries, ifq->rq_entries);
}

static struct io_uring_zcrx_rqe *io_zcrx_get_rqe(struct io_zcrx_ifq *ifq,
						 unsigned mask)
{
	unsigned int idx = ifq->cached_rq_head++ & mask;

	return &ifq->rqes[idx];
}

static void io_zcrx_ring_refill(struct page_pool *pp,
				struct io_zcrx_ifq *ifq)
{
	unsigned int mask = ifq->rq_entries - 1;
	unsigned int entries;
	netmem_ref netmem;

	spin_lock_bh(&ifq->rq_lock);

	entries = io_zcrx_rqring_entries(ifq);
	entries = min_t(unsigned, entries, PP_ALLOC_CACHE_REFILL - pp->alloc.count);
	if (unlikely(!entries)) {
		spin_unlock_bh(&ifq->rq_lock);
		return;
	}

	do {
		struct io_uring_zcrx_rqe *rqe = io_zcrx_get_rqe(ifq, mask);
		struct io_zcrx_area *area;
		struct net_iov *niov;
		unsigned niov_idx, area_idx;

		area_idx = rqe->off >> IORING_ZCRX_AREA_SHIFT;
		niov_idx = (rqe->off & ~IORING_ZCRX_AREA_MASK) >> PAGE_SHIFT;

		if (unlikely(rqe->__pad || area_idx))
			continue;
		area = ifq->area;

		if (unlikely(niov_idx >= area->nia.num_niovs))
			continue;
		niov_idx = array_index_nospec(niov_idx, area->nia.num_niovs);

		niov = &area->nia.niovs[niov_idx];
		if (!io_zcrx_put_niov_uref(niov))
			continue;

		netmem = net_iov_to_netmem(niov);
		if (page_pool_unref_netmem(netmem, 1) != 0)
			continue;

		if (unlikely(niov->pp != pp)) {
			io_zcrx_return_niov(niov);
			continue;
		}

		io_zcrx_sync_for_device(pp, niov);
		net_mp_netmem_place_in_cache(pp, netmem);
	} while (--entries);

	smp_store_release(&ifq->rq_ring->head, ifq->cached_rq_head);
	spin_unlock_bh(&ifq->rq_lock);
}

static void io_zcrx_refill_slow(struct page_pool *pp, struct io_zcrx_ifq *ifq)
{
	struct io_zcrx_area *area = ifq->area;

	spin_lock_bh(&area->freelist_lock);
	while (area->free_count && pp->alloc.count < PP_ALLOC_CACHE_REFILL) {
		struct net_iov *niov = __io_zcrx_get_free_niov(area);
		netmem_ref netmem = net_iov_to_netmem(niov);

		net_mp_niov_set_page_pool(pp, niov);
		io_zcrx_sync_for_device(pp, niov);
		net_mp_netmem_place_in_cache(pp, netmem);
	}
	spin_unlock_bh(&area->freelist_lock);
}

static netmem_ref io_pp_zc_alloc_netmems(struct page_pool *pp, gfp_t gfp)
{
	struct io_zcrx_ifq *ifq = pp->mp_priv;

	/* pp should already be ensuring that */
	if (unlikely(pp->alloc.count))
		goto out_return;

	io_zcrx_ring_refill(pp, ifq);
	if (likely(pp->alloc.count))
		goto out_return;

	io_zcrx_refill_slow(pp, ifq);
	if (!pp->alloc.count)
		return 0;
out_return:
	return pp->alloc.cache[--pp->alloc.count];
}

static bool io_pp_zc_release_netmem(struct page_pool *pp, netmem_ref netmem)
{
	struct net_iov *niov;

	if (WARN_ON_ONCE(!netmem_is_net_iov(netmem)))
		return false;

	niov = netmem_to_net_iov(netmem);
	net_mp_niov_clear_page_pool(niov);
	io_zcrx_return_niov_freelist(niov);
	return false;
}

static int io_pp_zc_init(struct page_pool *pp)
{
	struct io_zcrx_ifq *ifq = pp->mp_priv;

	if (WARN_ON_ONCE(!ifq))
		return -EINVAL;
	if (WARN_ON_ONCE(ifq->dev != pp->p.dev))
		return -EINVAL;
	if (WARN_ON_ONCE(!pp->dma_map))
		return -EOPNOTSUPP;
	if (pp->p.order != 0)
		return -EOPNOTSUPP;
	if (pp->p.dma_dir != DMA_FROM_DEVICE)
		return -EOPNOTSUPP;

	percpu_ref_get(&ifq->ctx->refs);
	return 0;
}

static void io_pp_zc_destroy(struct page_pool *pp)
{
	struct io_zcrx_ifq *ifq = pp->mp_priv;
	struct io_zcrx_area *area = ifq->area;

	if (WARN_ON_ONCE(area->free_count != area->nia.num_niovs))
		return;
	percpu_ref_put(&ifq->ctx->refs);
}

static int io_pp_nl_fill(void *mp_priv, struct sk_buff *rsp,
			 struct netdev_rx_queue *rxq)
{
	struct nlattr *nest;
	int type;

	type = rxq ? NETDEV_A_QUEUE_IO_URING : NETDEV_A_PAGE_POOL_IO_URING;
	nest = nla_nest_start(rsp, type);
	if (!nest)
		return -EMSGSIZE;
	nla_nest_end(rsp, nest);

	return 0;
}

static void io_pp_uninstall(void *mp_priv, struct netdev_rx_queue *rxq)
{
	struct pp_memory_provider_params *p = &rxq->mp_params;
	struct io_zcrx_ifq *ifq = mp_priv;

	io_zcrx_drop_netdev(ifq);
	p->mp_ops = NULL;
	p->mp_priv = NULL;
}

static const struct memory_provider_ops io_uring_pp_zc_ops = {
	.alloc_netmems		= io_pp_zc_alloc_netmems,
	.release_netmem		= io_pp_zc_release_netmem,
	.init			= io_pp_zc_init,
	.destroy		= io_pp_zc_destroy,
	.nl_fill		= io_pp_nl_fill,
	.uninstall		= io_pp_uninstall,
};
