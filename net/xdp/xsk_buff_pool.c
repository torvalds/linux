// SPDX-License-Identifier: GPL-2.0

#include <net/xsk_buff_pool.h>
#include <net/xdp_sock.h>
#include <net/xdp_sock_drv.h>

#include "xsk_queue.h"
#include "xdp_umem.h"
#include "xsk.h"

void xp_add_xsk(struct xsk_buff_pool *pool, struct xdp_sock *xs)
{
	unsigned long flags;

	if (!xs->tx)
		return;

	spin_lock_irqsave(&pool->xsk_tx_list_lock, flags);
	list_add_rcu(&xs->tx_list, &pool->xsk_tx_list);
	spin_unlock_irqrestore(&pool->xsk_tx_list_lock, flags);
}

void xp_del_xsk(struct xsk_buff_pool *pool, struct xdp_sock *xs)
{
	unsigned long flags;

	if (!xs->tx)
		return;

	spin_lock_irqsave(&pool->xsk_tx_list_lock, flags);
	list_del_rcu(&xs->tx_list);
	spin_unlock_irqrestore(&pool->xsk_tx_list_lock, flags);
}

void xp_destroy(struct xsk_buff_pool *pool)
{
	if (!pool)
		return;

	kvfree(pool->heads);
	kvfree(pool);
}

struct xsk_buff_pool *xp_create_and_assign_umem(struct xdp_sock *xs,
						struct xdp_umem *umem)
{
	bool unaligned = umem->flags & XDP_UMEM_UNALIGNED_CHUNK_FLAG;
	struct xsk_buff_pool *pool;
	struct xdp_buff_xsk *xskb;
	u32 i, entries;

	entries = unaligned ? umem->chunks : 0;
	pool = kvzalloc(struct_size(pool, free_heads, entries),	GFP_KERNEL);
	if (!pool)
		goto out;

	pool->heads = kvcalloc(umem->chunks, sizeof(*pool->heads), GFP_KERNEL);
	if (!pool->heads)
		goto out;

	pool->chunk_mask = ~((u64)umem->chunk_size - 1);
	pool->addrs_cnt = umem->size;
	pool->heads_cnt = umem->chunks;
	pool->free_heads_cnt = umem->chunks;
	pool->headroom = umem->headroom;
	pool->chunk_size = umem->chunk_size;
	pool->chunk_shift = ffs(umem->chunk_size) - 1;
	pool->unaligned = unaligned;
	pool->frame_len = umem->chunk_size - umem->headroom -
		XDP_PACKET_HEADROOM;
	pool->umem = umem;
	pool->addrs = umem->addrs;
	INIT_LIST_HEAD(&pool->free_list);
	INIT_LIST_HEAD(&pool->xsk_tx_list);
	spin_lock_init(&pool->xsk_tx_list_lock);
	spin_lock_init(&pool->cq_lock);
	refcount_set(&pool->users, 1);

	pool->fq = xs->fq_tmp;
	pool->cq = xs->cq_tmp;

	for (i = 0; i < pool->free_heads_cnt; i++) {
		xskb = &pool->heads[i];
		xskb->pool = pool;
		xskb->xdp.frame_sz = umem->chunk_size - umem->headroom;
		if (pool->unaligned)
			pool->free_heads[i] = xskb;
		else
			xp_init_xskb_addr(xskb, pool, i * pool->chunk_size);
	}

	return pool;

out:
	xp_destroy(pool);
	return NULL;
}

void xp_set_rxq_info(struct xsk_buff_pool *pool, struct xdp_rxq_info *rxq)
{
	u32 i;

	for (i = 0; i < pool->heads_cnt; i++)
		pool->heads[i].xdp.rxq = rxq;
}
EXPORT_SYMBOL(xp_set_rxq_info);

static void xp_disable_drv_zc(struct xsk_buff_pool *pool)
{
	struct netdev_bpf bpf;
	int err;

	ASSERT_RTNL();

	if (pool->umem->zc) {
		bpf.command = XDP_SETUP_XSK_POOL;
		bpf.xsk.pool = NULL;
		bpf.xsk.queue_id = pool->queue_id;

		err = pool->netdev->netdev_ops->ndo_bpf(pool->netdev, &bpf);

		if (err)
			WARN(1, "Failed to disable zero-copy!\n");
	}
}

int xp_assign_dev(struct xsk_buff_pool *pool,
		  struct net_device *netdev, u16 queue_id, u16 flags)
{
	bool force_zc, force_copy;
	struct netdev_bpf bpf;
	int err = 0;

	ASSERT_RTNL();

	force_zc = flags & XDP_ZEROCOPY;
	force_copy = flags & XDP_COPY;

	if (force_zc && force_copy)
		return -EINVAL;

	if (xsk_get_pool_from_qid(netdev, queue_id))
		return -EBUSY;

	pool->netdev = netdev;
	pool->queue_id = queue_id;
	err = xsk_reg_pool_at_qid(netdev, pool, queue_id);
	if (err)
		return err;

	if (flags & XDP_USE_NEED_WAKEUP)
		pool->uses_need_wakeup = true;
	/* Tx needs to be explicitly woken up the first time.  Also
	 * for supporting drivers that do not implement this
	 * feature. They will always have to call sendto() or poll().
	 */
	pool->cached_need_wakeup = XDP_WAKEUP_TX;

	dev_hold(netdev);

	if (force_copy)
		/* For copy-mode, we are done. */
		return 0;

	if (!netdev->netdev_ops->ndo_bpf ||
	    !netdev->netdev_ops->ndo_xsk_wakeup) {
		err = -EOPNOTSUPP;
		goto err_unreg_pool;
	}

	bpf.command = XDP_SETUP_XSK_POOL;
	bpf.xsk.pool = pool;
	bpf.xsk.queue_id = queue_id;

	err = netdev->netdev_ops->ndo_bpf(netdev, &bpf);
	if (err)
		goto err_unreg_pool;

	if (!pool->dma_pages) {
		WARN(1, "Driver did not DMA map zero-copy buffers");
		err = -EINVAL;
		goto err_unreg_xsk;
	}
	pool->umem->zc = true;
	return 0;

err_unreg_xsk:
	xp_disable_drv_zc(pool);
err_unreg_pool:
	if (!force_zc)
		err = 0; /* fallback to copy mode */
	if (err) {
		xsk_clear_pool_at_qid(netdev, queue_id);
		dev_put(netdev);
	}
	return err;
}

int xp_assign_dev_shared(struct xsk_buff_pool *pool, struct xdp_umem *umem,
			 struct net_device *dev, u16 queue_id)
{
	u16 flags;

	/* One fill and completion ring required for each queue id. */
	if (!pool->fq || !pool->cq)
		return -EINVAL;

	flags = umem->zc ? XDP_ZEROCOPY : XDP_COPY;
	if (pool->uses_need_wakeup)
		flags |= XDP_USE_NEED_WAKEUP;

	return xp_assign_dev(pool, dev, queue_id, flags);
}

void xp_clear_dev(struct xsk_buff_pool *pool)
{
	if (!pool->netdev)
		return;

	xp_disable_drv_zc(pool);
	xsk_clear_pool_at_qid(pool->netdev, pool->queue_id);
	dev_put(pool->netdev);
	pool->netdev = NULL;
}

static void xp_release_deferred(struct work_struct *work)
{
	struct xsk_buff_pool *pool = container_of(work, struct xsk_buff_pool,
						  work);

	rtnl_lock();
	xp_clear_dev(pool);
	rtnl_unlock();

	if (pool->fq) {
		xskq_destroy(pool->fq);
		pool->fq = NULL;
	}

	if (pool->cq) {
		xskq_destroy(pool->cq);
		pool->cq = NULL;
	}

	xdp_put_umem(pool->umem, false);
	xp_destroy(pool);
}

void xp_get_pool(struct xsk_buff_pool *pool)
{
	refcount_inc(&pool->users);
}

bool xp_put_pool(struct xsk_buff_pool *pool)
{
	if (!pool)
		return false;

	if (refcount_dec_and_test(&pool->users)) {
		INIT_WORK(&pool->work, xp_release_deferred);
		schedule_work(&pool->work);
		return true;
	}

	return false;
}

static struct xsk_dma_map *xp_find_dma_map(struct xsk_buff_pool *pool)
{
	struct xsk_dma_map *dma_map;

	list_for_each_entry(dma_map, &pool->umem->xsk_dma_list, list) {
		if (dma_map->netdev == pool->netdev)
			return dma_map;
	}

	return NULL;
}

static struct xsk_dma_map *xp_create_dma_map(struct device *dev, struct net_device *netdev,
					     u32 nr_pages, struct xdp_umem *umem)
{
	struct xsk_dma_map *dma_map;

	dma_map = kzalloc(sizeof(*dma_map), GFP_KERNEL);
	if (!dma_map)
		return NULL;

	dma_map->dma_pages = kvcalloc(nr_pages, sizeof(*dma_map->dma_pages), GFP_KERNEL);
	if (!dma_map->dma_pages) {
		kfree(dma_map);
		return NULL;
	}

	dma_map->netdev = netdev;
	dma_map->dev = dev;
	dma_map->dma_need_sync = false;
	dma_map->dma_pages_cnt = nr_pages;
	refcount_set(&dma_map->users, 1);
	list_add(&dma_map->list, &umem->xsk_dma_list);
	return dma_map;
}

static void xp_destroy_dma_map(struct xsk_dma_map *dma_map)
{
	list_del(&dma_map->list);
	kvfree(dma_map->dma_pages);
	kfree(dma_map);
}

static void __xp_dma_unmap(struct xsk_dma_map *dma_map, unsigned long attrs)
{
	dma_addr_t *dma;
	u32 i;

	for (i = 0; i < dma_map->dma_pages_cnt; i++) {
		dma = &dma_map->dma_pages[i];
		if (*dma) {
			dma_unmap_page_attrs(dma_map->dev, *dma, PAGE_SIZE,
					     DMA_BIDIRECTIONAL, attrs);
			*dma = 0;
		}
	}

	xp_destroy_dma_map(dma_map);
}

void xp_dma_unmap(struct xsk_buff_pool *pool, unsigned long attrs)
{
	struct xsk_dma_map *dma_map;

	if (pool->dma_pages_cnt == 0)
		return;

	dma_map = xp_find_dma_map(pool);
	if (!dma_map) {
		WARN(1, "Could not find dma_map for device");
		return;
	}

	if (!refcount_dec_and_test(&dma_map->users))
		return;

	__xp_dma_unmap(dma_map, attrs);
	kvfree(pool->dma_pages);
	pool->dma_pages_cnt = 0;
	pool->dev = NULL;
}
EXPORT_SYMBOL(xp_dma_unmap);

static void xp_check_dma_contiguity(struct xsk_dma_map *dma_map)
{
	u32 i;

	for (i = 0; i < dma_map->dma_pages_cnt - 1; i++) {
		if (dma_map->dma_pages[i] + PAGE_SIZE == dma_map->dma_pages[i + 1])
			dma_map->dma_pages[i] |= XSK_NEXT_PG_CONTIG_MASK;
		else
			dma_map->dma_pages[i] &= ~XSK_NEXT_PG_CONTIG_MASK;
	}
}

static int xp_init_dma_info(struct xsk_buff_pool *pool, struct xsk_dma_map *dma_map)
{
	pool->dma_pages = kvcalloc(dma_map->dma_pages_cnt, sizeof(*pool->dma_pages), GFP_KERNEL);
	if (!pool->dma_pages)
		return -ENOMEM;

	pool->dev = dma_map->dev;
	pool->dma_pages_cnt = dma_map->dma_pages_cnt;
	pool->dma_need_sync = dma_map->dma_need_sync;
	memcpy(pool->dma_pages, dma_map->dma_pages,
	       pool->dma_pages_cnt * sizeof(*pool->dma_pages));

	return 0;
}

int xp_dma_map(struct xsk_buff_pool *pool, struct device *dev,
	       unsigned long attrs, struct page **pages, u32 nr_pages)
{
	struct xsk_dma_map *dma_map;
	dma_addr_t dma;
	int err;
	u32 i;

	dma_map = xp_find_dma_map(pool);
	if (dma_map) {
		err = xp_init_dma_info(pool, dma_map);
		if (err)
			return err;

		refcount_inc(&dma_map->users);
		return 0;
	}

	dma_map = xp_create_dma_map(dev, pool->netdev, nr_pages, pool->umem);
	if (!dma_map)
		return -ENOMEM;

	for (i = 0; i < dma_map->dma_pages_cnt; i++) {
		dma = dma_map_page_attrs(dev, pages[i], 0, PAGE_SIZE,
					 DMA_BIDIRECTIONAL, attrs);
		if (dma_mapping_error(dev, dma)) {
			__xp_dma_unmap(dma_map, attrs);
			return -ENOMEM;
		}
		if (dma_need_sync(dev, dma))
			dma_map->dma_need_sync = true;
		dma_map->dma_pages[i] = dma;
	}

	if (pool->unaligned)
		xp_check_dma_contiguity(dma_map);
	else
		for (i = 0; i < pool->heads_cnt; i++) {
			struct xdp_buff_xsk *xskb = &pool->heads[i];

			xp_init_xskb_dma(xskb, pool, dma_map->dma_pages, xskb->orig_addr);
		}

	err = xp_init_dma_info(pool, dma_map);
	if (err) {
		__xp_dma_unmap(dma_map, attrs);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL(xp_dma_map);

static bool xp_addr_crosses_non_contig_pg(struct xsk_buff_pool *pool,
					  u64 addr)
{
	return xp_desc_crosses_non_contig_pg(pool, addr, pool->chunk_size);
}

static bool xp_check_unaligned(struct xsk_buff_pool *pool, u64 *addr)
{
	*addr = xp_unaligned_extract_addr(*addr);
	if (*addr >= pool->addrs_cnt ||
	    *addr + pool->chunk_size > pool->addrs_cnt ||
	    xp_addr_crosses_non_contig_pg(pool, *addr))
		return false;
	return true;
}

static bool xp_check_aligned(struct xsk_buff_pool *pool, u64 *addr)
{
	*addr = xp_aligned_extract_addr(pool, *addr);
	return *addr < pool->addrs_cnt;
}

static struct xdp_buff_xsk *__xp_alloc(struct xsk_buff_pool *pool)
{
	struct xdp_buff_xsk *xskb;
	u64 addr;
	bool ok;

	if (pool->free_heads_cnt == 0)
		return NULL;

	for (;;) {
		if (!xskq_cons_peek_addr_unchecked(pool->fq, &addr)) {
			pool->fq->queue_empty_descs++;
			return NULL;
		}

		ok = pool->unaligned ? xp_check_unaligned(pool, &addr) :
		     xp_check_aligned(pool, &addr);
		if (!ok) {
			pool->fq->invalid_descs++;
			xskq_cons_release(pool->fq);
			continue;
		}
		break;
	}

	if (pool->unaligned) {
		xskb = pool->free_heads[--pool->free_heads_cnt];
		xp_init_xskb_addr(xskb, pool, addr);
		if (pool->dma_pages_cnt)
			xp_init_xskb_dma(xskb, pool, pool->dma_pages, addr);
	} else {
		xskb = &pool->heads[xp_aligned_extract_idx(pool, addr)];
	}

	xskq_cons_release(pool->fq);
	return xskb;
}

struct xdp_buff *xp_alloc(struct xsk_buff_pool *pool)
{
	struct xdp_buff_xsk *xskb;

	if (!pool->free_list_cnt) {
		xskb = __xp_alloc(pool);
		if (!xskb)
			return NULL;
	} else {
		pool->free_list_cnt--;
		xskb = list_first_entry(&pool->free_list, struct xdp_buff_xsk,
					free_list_node);
		list_del_init(&xskb->free_list_node);
	}

	xskb->xdp.data = xskb->xdp.data_hard_start + XDP_PACKET_HEADROOM;
	xskb->xdp.data_meta = xskb->xdp.data;

	if (pool->dma_need_sync) {
		dma_sync_single_range_for_device(pool->dev, xskb->dma, 0,
						 pool->frame_len,
						 DMA_BIDIRECTIONAL);
	}
	return &xskb->xdp;
}
EXPORT_SYMBOL(xp_alloc);

static u32 xp_alloc_new_from_fq(struct xsk_buff_pool *pool, struct xdp_buff **xdp, u32 max)
{
	u32 i, cached_cons, nb_entries;

	if (max > pool->free_heads_cnt)
		max = pool->free_heads_cnt;
	max = xskq_cons_nb_entries(pool->fq, max);

	cached_cons = pool->fq->cached_cons;
	nb_entries = max;
	i = max;
	while (i--) {
		struct xdp_buff_xsk *xskb;
		u64 addr;
		bool ok;

		__xskq_cons_read_addr_unchecked(pool->fq, cached_cons++, &addr);

		ok = pool->unaligned ? xp_check_unaligned(pool, &addr) :
			xp_check_aligned(pool, &addr);
		if (unlikely(!ok)) {
			pool->fq->invalid_descs++;
			nb_entries--;
			continue;
		}

		if (pool->unaligned) {
			xskb = pool->free_heads[--pool->free_heads_cnt];
			xp_init_xskb_addr(xskb, pool, addr);
			if (pool->dma_pages_cnt)
				xp_init_xskb_dma(xskb, pool, pool->dma_pages, addr);
		} else {
			xskb = &pool->heads[xp_aligned_extract_idx(pool, addr)];
		}

		*xdp = &xskb->xdp;
		xdp++;
	}

	xskq_cons_release_n(pool->fq, max);
	return nb_entries;
}

static u32 xp_alloc_reused(struct xsk_buff_pool *pool, struct xdp_buff **xdp, u32 nb_entries)
{
	struct xdp_buff_xsk *xskb;
	u32 i;

	nb_entries = min_t(u32, nb_entries, pool->free_list_cnt);

	i = nb_entries;
	while (i--) {
		xskb = list_first_entry(&pool->free_list, struct xdp_buff_xsk, free_list_node);
		list_del_init(&xskb->free_list_node);

		*xdp = &xskb->xdp;
		xdp++;
	}
	pool->free_list_cnt -= nb_entries;

	return nb_entries;
}

u32 xp_alloc_batch(struct xsk_buff_pool *pool, struct xdp_buff **xdp, u32 max)
{
	u32 nb_entries1 = 0, nb_entries2;

	if (unlikely(pool->dma_need_sync)) {
		/* Slow path */
		*xdp = xp_alloc(pool);
		return !!*xdp;
	}

	if (unlikely(pool->free_list_cnt)) {
		nb_entries1 = xp_alloc_reused(pool, xdp, max);
		if (nb_entries1 == max)
			return nb_entries1;

		max -= nb_entries1;
		xdp += nb_entries1;
	}

	nb_entries2 = xp_alloc_new_from_fq(pool, xdp, max);
	if (!nb_entries2)
		pool->fq->queue_empty_descs++;

	return nb_entries1 + nb_entries2;
}
EXPORT_SYMBOL(xp_alloc_batch);

bool xp_can_alloc(struct xsk_buff_pool *pool, u32 count)
{
	if (pool->free_list_cnt >= count)
		return true;
	return xskq_cons_has_entries(pool->fq, count - pool->free_list_cnt);
}
EXPORT_SYMBOL(xp_can_alloc);

void xp_free(struct xdp_buff_xsk *xskb)
{
	if (!list_empty(&xskb->free_list_node))
		return;

	xskb->pool->free_list_cnt++;
	list_add(&xskb->free_list_node, &xskb->pool->free_list);
}
EXPORT_SYMBOL(xp_free);

void *xp_raw_get_data(struct xsk_buff_pool *pool, u64 addr)
{
	addr = pool->unaligned ? xp_unaligned_add_offset_to_addr(addr) : addr;
	return pool->addrs + addr;
}
EXPORT_SYMBOL(xp_raw_get_data);

dma_addr_t xp_raw_get_dma(struct xsk_buff_pool *pool, u64 addr)
{
	addr = pool->unaligned ? xp_unaligned_add_offset_to_addr(addr) : addr;
	return (pool->dma_pages[addr >> PAGE_SHIFT] &
		~XSK_NEXT_PG_CONTIG_MASK) +
		(addr & ~PAGE_MASK);
}
EXPORT_SYMBOL(xp_raw_get_dma);

void xp_dma_sync_for_cpu_slow(struct xdp_buff_xsk *xskb)
{
	dma_sync_single_range_for_cpu(xskb->pool->dev, xskb->dma, 0,
				      xskb->pool->frame_len, DMA_BIDIRECTIONAL);
}
EXPORT_SYMBOL(xp_dma_sync_for_cpu_slow);

void xp_dma_sync_for_device_slow(struct xsk_buff_pool *pool, dma_addr_t dma,
				 size_t size)
{
	dma_sync_single_range_for_device(pool->dev, dma, 0,
					 size, DMA_BIDIRECTIONAL);
}
EXPORT_SYMBOL(xp_dma_sync_for_device_slow);
