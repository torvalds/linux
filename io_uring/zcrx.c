// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/dma-map-ops.h>
#include <linux/mm.h>
#include <linux/nospec.h>
#include <linux/io_uring.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/skbuff_ref.h>

#include <net/page_pool/helpers.h>
#include <net/page_pool/memory_provider.h>
#include <net/netlink.h>
#include <net/netdev_queues.h>
#include <net/netdev_rx_queue.h>
#include <net/tcp.h>
#include <net/rps.h>

#include <trace/events/page_pool.h>

#include <uapi/linux/io_uring.h>

#include "io_uring.h"
#include "kbuf.h"
#include "memmap.h"
#include "zcrx.h"
#include "rsrc.h"

#define IO_ZCRX_AREA_SUPPORTED_FLAGS	(IORING_ZCRX_AREA_DMABUF)

#define IO_DMA_ATTR (DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

static inline struct io_zcrx_ifq *io_pp_to_ifq(struct page_pool *pp)
{
	return pp->mp_priv;
}

static inline struct io_zcrx_area *io_zcrx_iov_to_area(const struct net_iov *niov)
{
	struct net_iov_area *owner = net_iov_owner(niov);

	return container_of(owner, struct io_zcrx_area, nia);
}

static inline struct page *io_zcrx_iov_page(const struct net_iov *niov)
{
	struct io_zcrx_area *area = io_zcrx_iov_to_area(niov);
	unsigned niov_pages_shift;

	lockdep_assert(!area->mem.is_dmabuf);

	niov_pages_shift = area->ifq->niov_shift - PAGE_SHIFT;
	return area->mem.pages[net_iov_idx(niov) << niov_pages_shift];
}

static int io_populate_area_dma(struct io_zcrx_ifq *ifq,
				struct io_zcrx_area *area)
{
	unsigned niov_size = 1U << ifq->niov_shift;
	struct sg_table *sgt = area->mem.sgt;
	struct scatterlist *sg;
	unsigned i, niov_idx = 0;

	for_each_sgtable_dma_sg(sgt, sg, i) {
		dma_addr_t dma = sg_dma_address(sg);
		unsigned long sg_len = sg_dma_len(sg);

		if (WARN_ON_ONCE(sg_len % niov_size))
			return -EINVAL;

		while (sg_len && niov_idx < area->nia.num_niovs) {
			struct net_iov *niov = &area->nia.niovs[niov_idx];

			if (net_mp_niov_set_dma_addr(niov, dma))
				return -EFAULT;
			sg_len -= niov_size;
			dma += niov_size;
			niov_idx++;
		}
	}

	if (WARN_ON_ONCE(niov_idx != area->nia.num_niovs))
		return -EFAULT;
	return 0;
}

static void io_release_dmabuf(struct io_zcrx_mem *mem)
{
	if (!IS_ENABLED(CONFIG_DMA_SHARED_BUFFER))
		return;

	if (mem->sgt)
		dma_buf_unmap_attachment_unlocked(mem->attach, mem->sgt,
						  DMA_FROM_DEVICE);
	if (mem->attach)
		dma_buf_detach(mem->dmabuf, mem->attach);
	if (mem->dmabuf)
		dma_buf_put(mem->dmabuf);

	mem->sgt = NULL;
	mem->attach = NULL;
	mem->dmabuf = NULL;
}

static int io_import_dmabuf(struct io_zcrx_ifq *ifq,
			    struct io_zcrx_mem *mem,
			    struct io_uring_zcrx_area_reg *area_reg)
{
	unsigned long off = (unsigned long)area_reg->addr;
	unsigned long len = (unsigned long)area_reg->len;
	unsigned long total_size = 0;
	struct scatterlist *sg;
	int dmabuf_fd = area_reg->dmabuf_fd;
	int i, ret;

	if (off)
		return -EINVAL;
	if (WARN_ON_ONCE(!ifq->dev))
		return -EFAULT;
	if (!IS_ENABLED(CONFIG_DMA_SHARED_BUFFER))
		return -EINVAL;

	mem->is_dmabuf = true;
	mem->dmabuf = dma_buf_get(dmabuf_fd);
	if (IS_ERR(mem->dmabuf)) {
		ret = PTR_ERR(mem->dmabuf);
		mem->dmabuf = NULL;
		goto err;
	}

	mem->attach = dma_buf_attach(mem->dmabuf, ifq->dev);
	if (IS_ERR(mem->attach)) {
		ret = PTR_ERR(mem->attach);
		mem->attach = NULL;
		goto err;
	}

	mem->sgt = dma_buf_map_attachment_unlocked(mem->attach, DMA_FROM_DEVICE);
	if (IS_ERR(mem->sgt)) {
		ret = PTR_ERR(mem->sgt);
		mem->sgt = NULL;
		goto err;
	}

	for_each_sgtable_dma_sg(mem->sgt, sg, i)
		total_size += sg_dma_len(sg);

	if (total_size != len) {
		ret = -EINVAL;
		goto err;
	}

	mem->size = len;
	return 0;
err:
	io_release_dmabuf(mem);
	return ret;
}

static unsigned long io_count_account_pages(struct page **pages, unsigned nr_pages)
{
	struct folio *last_folio = NULL;
	unsigned long res = 0;
	int i;

	for (i = 0; i < nr_pages; i++) {
		struct folio *folio = page_folio(pages[i]);

		if (folio == last_folio)
			continue;
		last_folio = folio;
		res += 1UL << folio_order(folio);
	}
	return res;
}

static int io_import_umem(struct io_zcrx_ifq *ifq,
			  struct io_zcrx_mem *mem,
			  struct io_uring_zcrx_area_reg *area_reg)
{
	struct page **pages;
	int nr_pages, ret;

	if (area_reg->dmabuf_fd)
		return -EINVAL;
	if (!area_reg->addr)
		return -EFAULT;
	pages = io_pin_pages((unsigned long)area_reg->addr, area_reg->len,
				   &nr_pages);
	if (IS_ERR(pages))
		return PTR_ERR(pages);

	ret = sg_alloc_table_from_pages(&mem->page_sg_table, pages, nr_pages,
					0, nr_pages << PAGE_SHIFT,
					GFP_KERNEL_ACCOUNT);
	if (ret) {
		unpin_user_pages(pages, nr_pages);
		return ret;
	}

	mem->account_pages = io_count_account_pages(pages, nr_pages);
	ret = io_account_mem(ifq->ctx, mem->account_pages);
	if (ret < 0)
		mem->account_pages = 0;

	mem->sgt = &mem->page_sg_table;
	mem->pages = pages;
	mem->nr_folios = nr_pages;
	mem->size = area_reg->len;
	return ret;
}

static void io_release_area_mem(struct io_zcrx_mem *mem)
{
	if (mem->is_dmabuf) {
		io_release_dmabuf(mem);
		return;
	}
	if (mem->pages) {
		unpin_user_pages(mem->pages, mem->nr_folios);
		sg_free_table(mem->sgt);
		mem->sgt = NULL;
		kvfree(mem->pages);
	}
}

static int io_import_area(struct io_zcrx_ifq *ifq,
			  struct io_zcrx_mem *mem,
			  struct io_uring_zcrx_area_reg *area_reg)
{
	int ret;

	if (area_reg->flags & ~IO_ZCRX_AREA_SUPPORTED_FLAGS)
		return -EINVAL;
	if (area_reg->rq_area_token)
		return -EINVAL;
	if (area_reg->__resv2[0] || area_reg->__resv2[1])
		return -EINVAL;

	ret = io_validate_user_buf_range(area_reg->addr, area_reg->len);
	if (ret)
		return ret;
	if (area_reg->addr & ~PAGE_MASK || area_reg->len & ~PAGE_MASK)
		return -EINVAL;

	if (area_reg->flags & IORING_ZCRX_AREA_DMABUF)
		return io_import_dmabuf(ifq, mem, area_reg);
	return io_import_umem(ifq, mem, area_reg);
}

static void io_zcrx_unmap_area(struct io_zcrx_ifq *ifq,
				struct io_zcrx_area *area)
{
	int i;

	guard(mutex)(&ifq->pp_lock);
	if (!area->is_mapped)
		return;
	area->is_mapped = false;

	for (i = 0; i < area->nia.num_niovs; i++)
		net_mp_niov_set_dma_addr(&area->nia.niovs[i], 0);

	if (area->mem.is_dmabuf) {
		io_release_dmabuf(&area->mem);
	} else {
		dma_unmap_sgtable(ifq->dev, &area->mem.page_sg_table,
				  DMA_FROM_DEVICE, IO_DMA_ATTR);
	}
}

static int io_zcrx_map_area(struct io_zcrx_ifq *ifq, struct io_zcrx_area *area)
{
	int ret;

	guard(mutex)(&ifq->pp_lock);
	if (area->is_mapped)
		return 0;

	if (!area->mem.is_dmabuf) {
		ret = dma_map_sgtable(ifq->dev, &area->mem.page_sg_table,
				      DMA_FROM_DEVICE, IO_DMA_ATTR);
		if (ret < 0)
			return ret;
	}

	ret = io_populate_area_dma(ifq, area);
	if (ret == 0)
		area->is_mapped = true;
	return ret;
}

static void io_zcrx_sync_for_device(struct page_pool *pool,
				    struct net_iov *niov)
{
#if defined(CONFIG_HAS_DMA) && defined(CONFIG_DMA_NEED_SYNC)
	dma_addr_t dma_addr;

	unsigned niov_size;

	if (!dma_dev_need_sync(pool->p.dev))
		return;

	niov_size = 1U << io_pp_to_ifq(pool)->niov_shift;
	dma_addr = page_pool_get_dma_addr_netmem(net_iov_to_netmem(niov));
	__dma_sync_single_for_device(pool->p.dev, dma_addr + pool->p.offset,
				     niov_size, pool->p.dma_dir);
#endif
}

#define IO_RQ_MAX_ENTRIES		32768

#define IO_SKBS_PER_CALL_LIMIT	20

struct io_zcrx_args {
	struct io_kiocb		*req;
	struct io_zcrx_ifq	*ifq;
	struct socket		*sock;
	unsigned		nr_skbs;
};

static const struct memory_provider_ops io_uring_pp_zc_ops;

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

static void io_zcrx_get_niov_uref(struct net_iov *niov)
{
	atomic_inc(io_get_user_counter(niov));
}

static int io_allocate_rbuf_ring(struct io_zcrx_ifq *ifq,
				 struct io_uring_zcrx_ifq_reg *reg,
				 struct io_uring_region_desc *rd,
				 u32 id)
{
	u64 mmap_offset;
	size_t off, size;
	void *ptr;
	int ret;

	off = ALIGN(sizeof(struct io_uring), L1_CACHE_BYTES);
	size = off + sizeof(struct io_uring_zcrx_rqe) * reg->rq_entries;
	if (size > rd->size)
		return -EINVAL;

	mmap_offset = IORING_MAP_OFF_ZCRX_REGION;
	mmap_offset += id << IORING_OFF_PBUF_SHIFT;

	ret = io_create_region(ifq->ctx, &ifq->region, rd, mmap_offset);
	if (ret < 0)
		return ret;

	ptr = io_region_get_ptr(&ifq->region);
	ifq->rq_ring = (struct io_uring *)ptr;
	ifq->rqes = (struct io_uring_zcrx_rqe *)(ptr + off);

	reg->offsets.head = offsetof(struct io_uring, head);
	reg->offsets.tail = offsetof(struct io_uring, tail);
	reg->offsets.rqes = off;
	return 0;
}

static void io_free_rbuf_ring(struct io_zcrx_ifq *ifq)
{
	io_free_region(ifq->ctx, &ifq->region);
	ifq->rq_ring = NULL;
	ifq->rqes = NULL;
}

static void io_zcrx_free_area(struct io_zcrx_area *area)
{
	io_zcrx_unmap_area(area->ifq, area);
	io_release_area_mem(&area->mem);

	if (area->mem.account_pages)
		io_unaccount_mem(area->ifq->ctx, area->mem.account_pages);

	kvfree(area->freelist);
	kvfree(area->nia.niovs);
	kvfree(area->user_refs);
	kfree(area);
}

static int io_zcrx_append_area(struct io_zcrx_ifq *ifq,
				struct io_zcrx_area *area)
{
	if (ifq->area)
		return -EINVAL;
	ifq->area = area;
	return 0;
}

static int io_zcrx_create_area(struct io_zcrx_ifq *ifq,
			       struct io_uring_zcrx_area_reg *area_reg)
{
	struct io_zcrx_area *area;
	unsigned nr_iovs;
	int i, ret;

	ret = -ENOMEM;
	area = kzalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		goto err;
	area->ifq = ifq;

	ret = io_import_area(ifq, &area->mem, area_reg);
	if (ret)
		goto err;

	ifq->niov_shift = PAGE_SHIFT;
	nr_iovs = area->mem.size >> ifq->niov_shift;
	area->nia.num_niovs = nr_iovs;

	ret = -ENOMEM;
	area->nia.niovs = kvmalloc_array(nr_iovs, sizeof(area->nia.niovs[0]),
					 GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!area->nia.niovs)
		goto err;

	area->freelist = kvmalloc_array(nr_iovs, sizeof(area->freelist[0]),
					GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!area->freelist)
		goto err;

	area->user_refs = kvmalloc_array(nr_iovs, sizeof(area->user_refs[0]),
					GFP_KERNEL_ACCOUNT | __GFP_ZERO);
	if (!area->user_refs)
		goto err;

	for (i = 0; i < nr_iovs; i++) {
		struct net_iov *niov = &area->nia.niovs[i];

		niov->owner = &area->nia;
		area->freelist[i] = i;
		atomic_set(&area->user_refs[i], 0);
		niov->type = NET_IOV_IOURING;
	}

	area->free_count = nr_iovs;
	/* we're only supporting one area per ifq for now */
	area->area_id = 0;
	area_reg->rq_area_token = (u64)area->area_id << IORING_ZCRX_AREA_SHIFT;
	spin_lock_init(&area->freelist_lock);

	ret = io_zcrx_append_area(ifq, area);
	if (!ret)
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
	spin_lock_init(&ifq->rq_lock);
	mutex_init(&ifq->pp_lock);
	return ifq;
}

static void io_zcrx_drop_netdev(struct io_zcrx_ifq *ifq)
{
	guard(mutex)(&ifq->pp_lock);

	if (!ifq->netdev)
		return;
	netdev_put(ifq->netdev, &ifq->netdev_tracker);
	ifq->netdev = NULL;
}

static void io_close_queue(struct io_zcrx_ifq *ifq)
{
	struct net_device *netdev;
	netdevice_tracker netdev_tracker;
	struct pp_memory_provider_params p = {
		.mp_ops = &io_uring_pp_zc_ops,
		.mp_priv = ifq,
	};

	if (ifq->if_rxq == -1)
		return;

	scoped_guard(mutex, &ifq->pp_lock) {
		netdev = ifq->netdev;
		netdev_tracker = ifq->netdev_tracker;
		ifq->netdev = NULL;
	}

	if (netdev) {
		net_mp_close_rxq(netdev, ifq->if_rxq, &p);
		netdev_put(netdev, &netdev_tracker);
	}
	ifq->if_rxq = -1;
}

static void io_zcrx_ifq_free(struct io_zcrx_ifq *ifq)
{
	io_close_queue(ifq);

	if (ifq->area)
		io_zcrx_free_area(ifq->area);
	if (ifq->dev)
		put_device(ifq->dev);

	io_free_rbuf_ring(ifq);
	mutex_destroy(&ifq->pp_lock);
	kfree(ifq);
}

struct io_mapped_region *io_zcrx_get_region(struct io_ring_ctx *ctx,
					    unsigned int id)
{
	struct io_zcrx_ifq *ifq = xa_load(&ctx->zcrx_ctxs, id);

	lockdep_assert_held(&ctx->mmap_lock);

	return ifq ? &ifq->region : NULL;
}

int io_register_zcrx_ifq(struct io_ring_ctx *ctx,
			  struct io_uring_zcrx_ifq_reg __user *arg)
{
	struct pp_memory_provider_params mp_param = {};
	struct io_uring_zcrx_area_reg area;
	struct io_uring_zcrx_ifq_reg reg;
	struct io_uring_region_desc rd;
	struct io_zcrx_ifq *ifq;
	int ret;
	u32 id;

	/*
	 * 1. Interface queue allocation.
	 * 2. It can observe data destined for sockets of other tasks.
	 */
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	/* mandatory io_uring features for zc rx */
	if (!(ctx->flags & IORING_SETUP_DEFER_TASKRUN))
		return -EINVAL;
	if (!(ctx->flags & (IORING_SETUP_CQE32|IORING_SETUP_CQE_MIXED)))
		return -EINVAL;
	if (copy_from_user(&reg, arg, sizeof(reg)))
		return -EFAULT;
	if (copy_from_user(&rd, u64_to_user_ptr(reg.region_ptr), sizeof(rd)))
		return -EFAULT;
	if (!mem_is_zero(&reg.__resv, sizeof(reg.__resv)) ||
	    reg.__resv2 || reg.zcrx_id)
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
	ifq->rq_entries = reg.rq_entries;

	scoped_guard(mutex, &ctx->mmap_lock) {
		/* preallocate id */
		ret = xa_alloc(&ctx->zcrx_ctxs, &id, NULL, xa_limit_31b, GFP_KERNEL);
		if (ret)
			goto ifq_free;
	}

	ret = io_allocate_rbuf_ring(ifq, &reg, &rd, id);
	if (ret)
		goto err;

	ifq->netdev = netdev_get_by_index(current->nsproxy->net_ns, reg.if_idx,
					  &ifq->netdev_tracker, GFP_KERNEL);
	if (!ifq->netdev) {
		ret = -ENODEV;
		goto err;
	}

	ifq->dev = netdev_queue_get_dma_dev(ifq->netdev, reg.if_rxq);
	if (!ifq->dev) {
		ret = -EOPNOTSUPP;
		goto err;
	}
	get_device(ifq->dev);

	ret = io_zcrx_create_area(ifq, &area);
	if (ret)
		goto err;

	mp_param.mp_ops = &io_uring_pp_zc_ops;
	mp_param.mp_priv = ifq;
	ret = net_mp_open_rxq(ifq->netdev, reg.if_rxq, &mp_param);
	if (ret)
		goto err;
	ifq->if_rxq = reg.if_rxq;

	reg.zcrx_id = id;

	scoped_guard(mutex, &ctx->mmap_lock) {
		/* publish ifq */
		ret = -ENOMEM;
		if (xa_store(&ctx->zcrx_ctxs, id, ifq, GFP_KERNEL))
			goto err;
	}

	if (copy_to_user(arg, &reg, sizeof(reg)) ||
	    copy_to_user(u64_to_user_ptr(reg.region_ptr), &rd, sizeof(rd)) ||
	    copy_to_user(u64_to_user_ptr(reg.area_ptr), &area, sizeof(area))) {
		ret = -EFAULT;
		goto err;
	}
	return 0;
err:
	scoped_guard(mutex, &ctx->mmap_lock)
		xa_erase(&ctx->zcrx_ctxs, id);
ifq_free:
	io_zcrx_ifq_free(ifq);
	return ret;
}

void io_unregister_zcrx_ifqs(struct io_ring_ctx *ctx)
{
	struct io_zcrx_ifq *ifq;

	lockdep_assert_held(&ctx->uring_lock);

	while (1) {
		scoped_guard(mutex, &ctx->mmap_lock) {
			unsigned long id = 0;

			ifq = xa_find(&ctx->zcrx_ctxs, &id, ULONG_MAX, XA_PRESENT);
			if (ifq)
				xa_erase(&ctx->zcrx_ctxs, id);
		}
		if (!ifq)
			break;
		io_zcrx_ifq_free(ifq);
	}

	xa_destroy(&ctx->zcrx_ctxs);
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

	if (!niov->pp) {
		/* copy fallback allocated niovs */
		io_zcrx_return_niov_freelist(niov);
		return;
	}
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
	struct io_zcrx_ifq *ifq;
	unsigned long index;

	lockdep_assert_held(&ctx->uring_lock);

	xa_for_each(&ctx->zcrx_ctxs, index, ifq) {
		io_zcrx_scrub(ifq);
		io_close_queue(ifq);
	}
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

static inline bool io_parse_rqe(struct io_uring_zcrx_rqe *rqe,
				struct io_zcrx_ifq *ifq,
				struct net_iov **ret_niov)
{
	unsigned niov_idx, area_idx;
	struct io_zcrx_area *area;

	area_idx = rqe->off >> IORING_ZCRX_AREA_SHIFT;
	niov_idx = (rqe->off & ~IORING_ZCRX_AREA_MASK) >> ifq->niov_shift;

	if (unlikely(rqe->__pad || area_idx))
		return false;
	area = ifq->area;

	if (unlikely(niov_idx >= area->nia.num_niovs))
		return false;
	niov_idx = array_index_nospec(niov_idx, area->nia.num_niovs);

	*ret_niov = &area->nia.niovs[niov_idx];
	return true;
}

static void io_zcrx_ring_refill(struct page_pool *pp,
				struct io_zcrx_ifq *ifq)
{
	unsigned int mask = ifq->rq_entries - 1;
	unsigned int entries;

	guard(spinlock_bh)(&ifq->rq_lock);

	entries = io_zcrx_rqring_entries(ifq);
	entries = min_t(unsigned, entries, PP_ALLOC_CACHE_REFILL);
	if (unlikely(!entries))
		return;

	do {
		struct io_uring_zcrx_rqe *rqe = io_zcrx_get_rqe(ifq, mask);
		struct net_iov *niov;
		netmem_ref netmem;

		if (!io_parse_rqe(rqe, ifq, &niov))
			continue;
		if (!io_zcrx_put_niov_uref(niov))
			continue;

		netmem = net_iov_to_netmem(niov);
		if (!page_pool_unref_and_test(netmem))
			continue;

		if (unlikely(niov->pp != pp)) {
			io_zcrx_return_niov(niov);
			continue;
		}

		io_zcrx_sync_for_device(pp, niov);
		net_mp_netmem_place_in_cache(pp, netmem);
	} while (--entries);

	smp_store_release(&ifq->rq_ring->head, ifq->cached_rq_head);
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
	struct io_zcrx_ifq *ifq = io_pp_to_ifq(pp);

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
	struct io_zcrx_ifq *ifq = io_pp_to_ifq(pp);
	int ret;

	if (WARN_ON_ONCE(!ifq))
		return -EINVAL;
	if (WARN_ON_ONCE(ifq->dev != pp->p.dev))
		return -EINVAL;
	if (WARN_ON_ONCE(!pp->dma_map))
		return -EOPNOTSUPP;
	if (pp->p.order + PAGE_SHIFT != ifq->niov_shift)
		return -EINVAL;
	if (pp->p.dma_dir != DMA_FROM_DEVICE)
		return -EOPNOTSUPP;

	ret = io_zcrx_map_area(ifq, ifq->area);
	if (ret)
		return ret;

	percpu_ref_get(&ifq->ctx->refs);
	return 0;
}

static void io_pp_zc_destroy(struct page_pool *pp)
{
	struct io_zcrx_ifq *ifq = io_pp_to_ifq(pp);

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
	if (ifq->area)
		io_zcrx_unmap_area(ifq, ifq->area);

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

static bool io_zcrx_queue_cqe(struct io_kiocb *req, struct net_iov *niov,
			      struct io_zcrx_ifq *ifq, int off, int len)
{
	struct io_ring_ctx *ctx = req->ctx;
	struct io_uring_zcrx_cqe *rcqe;
	struct io_zcrx_area *area;
	struct io_uring_cqe *cqe;
	u64 offset;

	if (!io_defer_get_uncommited_cqe(ctx, &cqe))
		return false;

	cqe->user_data = req->cqe.user_data;
	cqe->res = len;
	cqe->flags = IORING_CQE_F_MORE;
	if (ctx->flags & IORING_SETUP_CQE_MIXED)
		cqe->flags |= IORING_CQE_F_32;

	area = io_zcrx_iov_to_area(niov);
	offset = off + (net_iov_idx(niov) << ifq->niov_shift);
	rcqe = (struct io_uring_zcrx_cqe *)(cqe + 1);
	rcqe->off = offset + ((u64)area->area_id << IORING_ZCRX_AREA_SHIFT);
	rcqe->__pad = 0;
	return true;
}

static struct net_iov *io_alloc_fallback_niov(struct io_zcrx_ifq *ifq)
{
	struct io_zcrx_area *area = ifq->area;
	struct net_iov *niov = NULL;

	if (area->mem.is_dmabuf)
		return NULL;

	spin_lock_bh(&area->freelist_lock);
	if (area->free_count)
		niov = __io_zcrx_get_free_niov(area);
	spin_unlock_bh(&area->freelist_lock);

	if (niov)
		page_pool_fragment_netmem(net_iov_to_netmem(niov), 1);
	return niov;
}

struct io_copy_cache {
	struct page		*page;
	unsigned long		offset;
	size_t			size;
};

static ssize_t io_copy_page(struct io_copy_cache *cc, struct page *src_page,
			    unsigned int src_offset, size_t len)
{
	size_t copied = 0;

	len = min(len, cc->size);

	while (len) {
		void *src_addr, *dst_addr;
		struct page *dst_page = cc->page;
		unsigned dst_offset = cc->offset;
		size_t n = len;

		if (folio_test_partial_kmap(page_folio(dst_page)) ||
		    folio_test_partial_kmap(page_folio(src_page))) {
			dst_page += dst_offset / PAGE_SIZE;
			dst_offset = offset_in_page(dst_offset);
			src_page += src_offset / PAGE_SIZE;
			src_offset = offset_in_page(src_offset);
			n = min(PAGE_SIZE - src_offset, PAGE_SIZE - dst_offset);
			n = min(n, len);
		}

		dst_addr = kmap_local_page(dst_page) + dst_offset;
		src_addr = kmap_local_page(src_page) + src_offset;

		memcpy(dst_addr, src_addr, n);

		kunmap_local(src_addr);
		kunmap_local(dst_addr);

		cc->size -= n;
		cc->offset += n;
		src_offset += n;
		len -= n;
		copied += n;
	}
	return copied;
}

static ssize_t io_zcrx_copy_chunk(struct io_kiocb *req, struct io_zcrx_ifq *ifq,
				  struct page *src_page, unsigned int src_offset,
				  size_t len)
{
	size_t copied = 0;
	int ret = 0;

	while (len) {
		struct io_copy_cache cc;
		struct net_iov *niov;
		size_t n;

		niov = io_alloc_fallback_niov(ifq);
		if (!niov) {
			ret = -ENOMEM;
			break;
		}

		cc.page = io_zcrx_iov_page(niov);
		cc.offset = 0;
		cc.size = PAGE_SIZE;

		n = io_copy_page(&cc, src_page, src_offset, len);

		if (!io_zcrx_queue_cqe(req, niov, ifq, 0, n)) {
			io_zcrx_return_niov(niov);
			ret = -ENOSPC;
			break;
		}

		io_zcrx_get_niov_uref(niov);
		src_offset += n;
		len -= n;
		copied += n;
	}

	return copied ? copied : ret;
}

static int io_zcrx_copy_frag(struct io_kiocb *req, struct io_zcrx_ifq *ifq,
			     const skb_frag_t *frag, int off, int len)
{
	struct page *page = skb_frag_page(frag);

	return io_zcrx_copy_chunk(req, ifq, page, off + skb_frag_off(frag), len);
}

static int io_zcrx_recv_frag(struct io_kiocb *req, struct io_zcrx_ifq *ifq,
			     const skb_frag_t *frag, int off, int len)
{
	struct net_iov *niov;

	if (unlikely(!skb_frag_is_net_iov(frag)))
		return io_zcrx_copy_frag(req, ifq, frag, off, len);

	niov = netmem_to_net_iov(frag->netmem);
	if (!niov->pp || niov->pp->mp_ops != &io_uring_pp_zc_ops ||
	    io_pp_to_ifq(niov->pp) != ifq)
		return -EFAULT;

	if (!io_zcrx_queue_cqe(req, niov, ifq, off + skb_frag_off(frag), len))
		return -ENOSPC;

	/*
	 * Prevent it from being recycled while user is accessing it.
	 * It has to be done before grabbing a user reference.
	 */
	page_pool_ref_netmem(net_iov_to_netmem(niov));
	io_zcrx_get_niov_uref(niov);
	return len;
}

static int
io_zcrx_recv_skb(read_descriptor_t *desc, struct sk_buff *skb,
		 unsigned int offset, size_t len)
{
	struct io_zcrx_args *args = desc->arg.data;
	struct io_zcrx_ifq *ifq = args->ifq;
	struct io_kiocb *req = args->req;
	struct sk_buff *frag_iter;
	unsigned start, start_off = offset;
	int i, copy, end, off;
	int ret = 0;

	len = min_t(size_t, len, desc->count);
	/*
	 * __tcp_read_sock() always calls io_zcrx_recv_skb one last time, even
	 * if desc->count is already 0. This is caused by the if (offset + 1 !=
	 * skb->len) check. Return early in this case to break out of
	 * __tcp_read_sock().
	 */
	if (!len)
		return 0;
	if (unlikely(args->nr_skbs++ > IO_SKBS_PER_CALL_LIMIT))
		return -EAGAIN;

	if (unlikely(offset < skb_headlen(skb))) {
		ssize_t copied;
		size_t to_copy;

		to_copy = min_t(size_t, skb_headlen(skb) - offset, len);
		copied = io_zcrx_copy_chunk(req, ifq, virt_to_page(skb->data),
					    offset_in_page(skb->data) + offset,
					    to_copy);
		if (copied < 0) {
			ret = copied;
			goto out;
		}
		offset += copied;
		len -= copied;
		if (!len)
			goto out;
		if (offset != skb_headlen(skb))
			goto out;
	}

	start = skb_headlen(skb);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *frag;

		if (WARN_ON(start > offset + len))
			return -EFAULT;

		frag = &skb_shinfo(skb)->frags[i];
		end = start + skb_frag_size(frag);

		if (offset < end) {
			copy = end - offset;
			if (copy > len)
				copy = len;

			off = offset - start;
			ret = io_zcrx_recv_frag(req, ifq, frag, off, copy);
			if (ret < 0)
				goto out;

			offset += ret;
			len -= ret;
			if (len == 0 || ret != copy)
				goto out;
		}
		start = end;
	}

	skb_walk_frags(skb, frag_iter) {
		if (WARN_ON(start > offset + len))
			return -EFAULT;

		end = start + frag_iter->len;
		if (offset < end) {
			size_t count;

			copy = end - offset;
			if (copy > len)
				copy = len;

			off = offset - start;
			count = desc->count;
			ret = io_zcrx_recv_skb(desc, frag_iter, off, copy);
			desc->count = count;
			if (ret < 0)
				goto out;

			offset += ret;
			len -= ret;
			if (len == 0 || ret != copy)
				goto out;
		}
		start = end;
	}

out:
	if (offset == start_off)
		return ret;
	desc->count -= (offset - start_off);
	return offset - start_off;
}

static int io_zcrx_tcp_recvmsg(struct io_kiocb *req, struct io_zcrx_ifq *ifq,
				struct sock *sk, int flags,
				unsigned issue_flags, unsigned int *outlen)
{
	unsigned int len = *outlen;
	struct io_zcrx_args args = {
		.req = req,
		.ifq = ifq,
		.sock = sk->sk_socket,
	};
	read_descriptor_t rd_desc = {
		.count = len ? len : UINT_MAX,
		.arg.data = &args,
	};
	int ret;

	lock_sock(sk);
	ret = tcp_read_sock(sk, &rd_desc, io_zcrx_recv_skb);
	if (len && ret > 0)
		*outlen = len - ret;
	if (ret <= 0) {
		if (ret < 0 || sock_flag(sk, SOCK_DONE))
			goto out;
		if (sk->sk_err)
			ret = sock_error(sk);
		else if (sk->sk_shutdown & RCV_SHUTDOWN)
			goto out;
		else if (sk->sk_state == TCP_CLOSE)
			ret = -ENOTCONN;
		else
			ret = -EAGAIN;
	} else if (unlikely(args.nr_skbs > IO_SKBS_PER_CALL_LIMIT) &&
		   (issue_flags & IO_URING_F_MULTISHOT)) {
		ret = IOU_REQUEUE;
	} else if (sock_flag(sk, SOCK_DONE)) {
		/* Make it to retry until it finally gets 0. */
		if (issue_flags & IO_URING_F_MULTISHOT)
			ret = IOU_REQUEUE;
		else
			ret = -EAGAIN;
	}
out:
	release_sock(sk);
	return ret;
}

int io_zcrx_recv(struct io_kiocb *req, struct io_zcrx_ifq *ifq,
		 struct socket *sock, unsigned int flags,
		 unsigned issue_flags, unsigned int *len)
{
	struct sock *sk = sock->sk;
	const struct proto *prot = READ_ONCE(sk->sk_prot);

	if (prot->recvmsg != tcp_recvmsg)
		return -EPROTONOSUPPORT;

	sock_rps_record_flow(sk);
	return io_zcrx_tcp_recvmsg(req, ifq, sk, flags, issue_flags, len);
}
