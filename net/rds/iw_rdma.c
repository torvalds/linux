/*
 * Copyright (c) 2006 Oracle.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/ratelimit.h>

#include "rds.h"
#include "iw.h"


/*
 * This is stored as mr->r_trans_private.
 */
struct rds_iw_mr {
	struct rds_iw_device	*device;
	struct rds_iw_mr_pool	*pool;
	struct rdma_cm_id	*cm_id;

	struct ib_mr	*mr;
	struct ib_fast_reg_page_list *page_list;

	struct rds_iw_mapping	mapping;
	unsigned char		remap_count;
};

/*
 * Our own little MR pool
 */
struct rds_iw_mr_pool {
	struct rds_iw_device	*device;		/* back ptr to the device that owns us */

	struct mutex		flush_lock;		/* serialize fmr invalidate */
	struct work_struct	flush_worker;		/* flush worker */

	spinlock_t		list_lock;		/* protect variables below */
	atomic_t		item_count;		/* total # of MRs */
	atomic_t		dirty_count;		/* # dirty of MRs */
	struct list_head	dirty_list;		/* dirty mappings */
	struct list_head	clean_list;		/* unused & unamapped MRs */
	atomic_t		free_pinned;		/* memory pinned by free MRs */
	unsigned long		max_message_size;	/* in pages */
	unsigned long		max_items;
	unsigned long		max_items_soft;
	unsigned long		max_free_pinned;
	int			max_pages;
};

static int rds_iw_flush_mr_pool(struct rds_iw_mr_pool *pool, int free_all);
static void rds_iw_mr_pool_flush_worker(struct work_struct *work);
static int rds_iw_init_fastreg(struct rds_iw_mr_pool *pool, struct rds_iw_mr *ibmr);
static int rds_iw_map_fastreg(struct rds_iw_mr_pool *pool,
			  struct rds_iw_mr *ibmr,
			  struct scatterlist *sg, unsigned int nents);
static void rds_iw_free_fastreg(struct rds_iw_mr_pool *pool, struct rds_iw_mr *ibmr);
static unsigned int rds_iw_unmap_fastreg_list(struct rds_iw_mr_pool *pool,
			struct list_head *unmap_list,
			struct list_head *kill_list,
			int *unpinned);
static void rds_iw_destroy_fastreg(struct rds_iw_mr_pool *pool, struct rds_iw_mr *ibmr);

static int rds_iw_get_device(struct sockaddr_in *src, struct sockaddr_in *dst,
			     struct rds_iw_device **rds_iwdev,
			     struct rdma_cm_id **cm_id)
{
	struct rds_iw_device *iwdev;
	struct rds_iw_cm_id *i_cm_id;

	*rds_iwdev = NULL;
	*cm_id = NULL;

	list_for_each_entry(iwdev, &rds_iw_devices, list) {
		spin_lock_irq(&iwdev->spinlock);
		list_for_each_entry(i_cm_id, &iwdev->cm_id_list, list) {
			struct sockaddr_in *src_addr, *dst_addr;

			src_addr = (struct sockaddr_in *)&i_cm_id->cm_id->route.addr.src_addr;
			dst_addr = (struct sockaddr_in *)&i_cm_id->cm_id->route.addr.dst_addr;

			rdsdebug("local ipaddr = %x port %d, "
				 "remote ipaddr = %x port %d"
				 "..looking for %x port %d, "
				 "remote ipaddr = %x port %d\n",
				src_addr->sin_addr.s_addr,
				src_addr->sin_port,
				dst_addr->sin_addr.s_addr,
				dst_addr->sin_port,
				src->sin_addr.s_addr,
				src->sin_port,
				dst->sin_addr.s_addr,
				dst->sin_port);
#ifdef WORKING_TUPLE_DETECTION
			if (src_addr->sin_addr.s_addr == src->sin_addr.s_addr &&
			    src_addr->sin_port == src->sin_port &&
			    dst_addr->sin_addr.s_addr == dst->sin_addr.s_addr &&
			    dst_addr->sin_port == dst->sin_port) {
#else
			/* FIXME - needs to compare the local and remote
			 * ipaddr/port tuple, but the ipaddr is the only
			 * available information in the rds_sock (as the rest are
			 * zero'ed.  It doesn't appear to be properly populated
			 * during connection setup...
			 */
			if (src_addr->sin_addr.s_addr == src->sin_addr.s_addr) {
#endif
				spin_unlock_irq(&iwdev->spinlock);
				*rds_iwdev = iwdev;
				*cm_id = i_cm_id->cm_id;
				return 0;
			}
		}
		spin_unlock_irq(&iwdev->spinlock);
	}

	return 1;
}

static int rds_iw_add_cm_id(struct rds_iw_device *rds_iwdev, struct rdma_cm_id *cm_id)
{
	struct rds_iw_cm_id *i_cm_id;

	i_cm_id = kmalloc(sizeof *i_cm_id, GFP_KERNEL);
	if (!i_cm_id)
		return -ENOMEM;

	i_cm_id->cm_id = cm_id;

	spin_lock_irq(&rds_iwdev->spinlock);
	list_add_tail(&i_cm_id->list, &rds_iwdev->cm_id_list);
	spin_unlock_irq(&rds_iwdev->spinlock);

	return 0;
}

static void rds_iw_remove_cm_id(struct rds_iw_device *rds_iwdev,
				struct rdma_cm_id *cm_id)
{
	struct rds_iw_cm_id *i_cm_id;

	spin_lock_irq(&rds_iwdev->spinlock);
	list_for_each_entry(i_cm_id, &rds_iwdev->cm_id_list, list) {
		if (i_cm_id->cm_id == cm_id) {
			list_del(&i_cm_id->list);
			kfree(i_cm_id);
			break;
		}
	}
	spin_unlock_irq(&rds_iwdev->spinlock);
}


int rds_iw_update_cm_id(struct rds_iw_device *rds_iwdev, struct rdma_cm_id *cm_id)
{
	struct sockaddr_in *src_addr, *dst_addr;
	struct rds_iw_device *rds_iwdev_old;
	struct rdma_cm_id *pcm_id;
	int rc;

	src_addr = (struct sockaddr_in *)&cm_id->route.addr.src_addr;
	dst_addr = (struct sockaddr_in *)&cm_id->route.addr.dst_addr;

	rc = rds_iw_get_device(src_addr, dst_addr, &rds_iwdev_old, &pcm_id);
	if (rc)
		rds_iw_remove_cm_id(rds_iwdev, cm_id);

	return rds_iw_add_cm_id(rds_iwdev, cm_id);
}

void rds_iw_add_conn(struct rds_iw_device *rds_iwdev, struct rds_connection *conn)
{
	struct rds_iw_connection *ic = conn->c_transport_data;

	/* conn was previously on the nodev_conns_list */
	spin_lock_irq(&iw_nodev_conns_lock);
	BUG_ON(list_empty(&iw_nodev_conns));
	BUG_ON(list_empty(&ic->iw_node));
	list_del(&ic->iw_node);

	spin_lock(&rds_iwdev->spinlock);
	list_add_tail(&ic->iw_node, &rds_iwdev->conn_list);
	spin_unlock(&rds_iwdev->spinlock);
	spin_unlock_irq(&iw_nodev_conns_lock);

	ic->rds_iwdev = rds_iwdev;
}

void rds_iw_remove_conn(struct rds_iw_device *rds_iwdev, struct rds_connection *conn)
{
	struct rds_iw_connection *ic = conn->c_transport_data;

	/* place conn on nodev_conns_list */
	spin_lock(&iw_nodev_conns_lock);

	spin_lock_irq(&rds_iwdev->spinlock);
	BUG_ON(list_empty(&ic->iw_node));
	list_del(&ic->iw_node);
	spin_unlock_irq(&rds_iwdev->spinlock);

	list_add_tail(&ic->iw_node, &iw_nodev_conns);

	spin_unlock(&iw_nodev_conns_lock);

	rds_iw_remove_cm_id(ic->rds_iwdev, ic->i_cm_id);
	ic->rds_iwdev = NULL;
}

void __rds_iw_destroy_conns(struct list_head *list, spinlock_t *list_lock)
{
	struct rds_iw_connection *ic, *_ic;
	LIST_HEAD(tmp_list);

	/* avoid calling conn_destroy with irqs off */
	spin_lock_irq(list_lock);
	list_splice(list, &tmp_list);
	INIT_LIST_HEAD(list);
	spin_unlock_irq(list_lock);

	list_for_each_entry_safe(ic, _ic, &tmp_list, iw_node)
		rds_conn_destroy(ic->conn);
}

static void rds_iw_set_scatterlist(struct rds_iw_scatterlist *sg,
		struct scatterlist *list, unsigned int sg_len)
{
	sg->list = list;
	sg->len = sg_len;
	sg->dma_len = 0;
	sg->dma_npages = 0;
	sg->bytes = 0;
}

static u64 *rds_iw_map_scatterlist(struct rds_iw_device *rds_iwdev,
			struct rds_iw_scatterlist *sg)
{
	struct ib_device *dev = rds_iwdev->dev;
	u64 *dma_pages = NULL;
	int i, j, ret;

	WARN_ON(sg->dma_len);

	sg->dma_len = ib_dma_map_sg(dev, sg->list, sg->len, DMA_BIDIRECTIONAL);
	if (unlikely(!sg->dma_len)) {
		printk(KERN_WARNING "RDS/IW: dma_map_sg failed!\n");
		return ERR_PTR(-EBUSY);
	}

	sg->bytes = 0;
	sg->dma_npages = 0;

	ret = -EINVAL;
	for (i = 0; i < sg->dma_len; ++i) {
		unsigned int dma_len = ib_sg_dma_len(dev, &sg->list[i]);
		u64 dma_addr = ib_sg_dma_address(dev, &sg->list[i]);
		u64 end_addr;

		sg->bytes += dma_len;

		end_addr = dma_addr + dma_len;
		if (dma_addr & PAGE_MASK) {
			if (i > 0)
				goto out_unmap;
			dma_addr &= ~PAGE_MASK;
		}
		if (end_addr & PAGE_MASK) {
			if (i < sg->dma_len - 1)
				goto out_unmap;
			end_addr = (end_addr + PAGE_MASK) & ~PAGE_MASK;
		}

		sg->dma_npages += (end_addr - dma_addr) >> PAGE_SHIFT;
	}

	/* Now gather the dma addrs into one list */
	if (sg->dma_npages > fastreg_message_size)
		goto out_unmap;

	dma_pages = kmalloc(sizeof(u64) * sg->dma_npages, GFP_ATOMIC);
	if (!dma_pages) {
		ret = -ENOMEM;
		goto out_unmap;
	}

	for (i = j = 0; i < sg->dma_len; ++i) {
		unsigned int dma_len = ib_sg_dma_len(dev, &sg->list[i]);
		u64 dma_addr = ib_sg_dma_address(dev, &sg->list[i]);
		u64 end_addr;

		end_addr = dma_addr + dma_len;
		dma_addr &= ~PAGE_MASK;
		for (; dma_addr < end_addr; dma_addr += PAGE_SIZE)
			dma_pages[j++] = dma_addr;
		BUG_ON(j > sg->dma_npages);
	}

	return dma_pages;

out_unmap:
	ib_dma_unmap_sg(rds_iwdev->dev, sg->list, sg->len, DMA_BIDIRECTIONAL);
	sg->dma_len = 0;
	kfree(dma_pages);
	return ERR_PTR(ret);
}


struct rds_iw_mr_pool *rds_iw_create_mr_pool(struct rds_iw_device *rds_iwdev)
{
	struct rds_iw_mr_pool *pool;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool) {
		printk(KERN_WARNING "RDS/IW: rds_iw_create_mr_pool alloc error\n");
		return ERR_PTR(-ENOMEM);
	}

	pool->device = rds_iwdev;
	INIT_LIST_HEAD(&pool->dirty_list);
	INIT_LIST_HEAD(&pool->clean_list);
	mutex_init(&pool->flush_lock);
	spin_lock_init(&pool->list_lock);
	INIT_WORK(&pool->flush_worker, rds_iw_mr_pool_flush_worker);

	pool->max_message_size = fastreg_message_size;
	pool->max_items = fastreg_pool_size;
	pool->max_free_pinned = pool->max_items * pool->max_message_size / 4;
	pool->max_pages = fastreg_message_size;

	/* We never allow more than max_items MRs to be allocated.
	 * When we exceed more than max_items_soft, we start freeing
	 * items more aggressively.
	 * Make sure that max_items > max_items_soft > max_items / 2
	 */
	pool->max_items_soft = pool->max_items * 3 / 4;

	return pool;
}

void rds_iw_get_mr_info(struct rds_iw_device *rds_iwdev, struct rds_info_rdma_connection *iinfo)
{
	struct rds_iw_mr_pool *pool = rds_iwdev->mr_pool;

	iinfo->rdma_mr_max = pool->max_items;
	iinfo->rdma_mr_size = pool->max_pages;
}

void rds_iw_destroy_mr_pool(struct rds_iw_mr_pool *pool)
{
	flush_workqueue(rds_wq);
	rds_iw_flush_mr_pool(pool, 1);
	BUG_ON(atomic_read(&pool->item_count));
	BUG_ON(atomic_read(&pool->free_pinned));
	kfree(pool);
}

static inline struct rds_iw_mr *rds_iw_reuse_fmr(struct rds_iw_mr_pool *pool)
{
	struct rds_iw_mr *ibmr = NULL;
	unsigned long flags;

	spin_lock_irqsave(&pool->list_lock, flags);
	if (!list_empty(&pool->clean_list)) {
		ibmr = list_entry(pool->clean_list.next, struct rds_iw_mr, mapping.m_list);
		list_del_init(&ibmr->mapping.m_list);
	}
	spin_unlock_irqrestore(&pool->list_lock, flags);

	return ibmr;
}

static struct rds_iw_mr *rds_iw_alloc_mr(struct rds_iw_device *rds_iwdev)
{
	struct rds_iw_mr_pool *pool = rds_iwdev->mr_pool;
	struct rds_iw_mr *ibmr = NULL;
	int err = 0, iter = 0;

	while (1) {
		ibmr = rds_iw_reuse_fmr(pool);
		if (ibmr)
			return ibmr;

		/* No clean MRs - now we have the choice of either
		 * allocating a fresh MR up to the limit imposed by the
		 * driver, or flush any dirty unused MRs.
		 * We try to avoid stalling in the send path if possible,
		 * so we allocate as long as we're allowed to.
		 *
		 * We're fussy with enforcing the FMR limit, though. If the driver
		 * tells us we can't use more than N fmrs, we shouldn't start
		 * arguing with it */
		if (atomic_inc_return(&pool->item_count) <= pool->max_items)
			break;

		atomic_dec(&pool->item_count);

		if (++iter > 2) {
			rds_iw_stats_inc(s_iw_rdma_mr_pool_depleted);
			return ERR_PTR(-EAGAIN);
		}

		/* We do have some empty MRs. Flush them out. */
		rds_iw_stats_inc(s_iw_rdma_mr_pool_wait);
		rds_iw_flush_mr_pool(pool, 0);
	}

	ibmr = kzalloc(sizeof(*ibmr), GFP_KERNEL);
	if (!ibmr) {
		err = -ENOMEM;
		goto out_no_cigar;
	}

	spin_lock_init(&ibmr->mapping.m_lock);
	INIT_LIST_HEAD(&ibmr->mapping.m_list);
	ibmr->mapping.m_mr = ibmr;

	err = rds_iw_init_fastreg(pool, ibmr);
	if (err)
		goto out_no_cigar;

	rds_iw_stats_inc(s_iw_rdma_mr_alloc);
	return ibmr;

out_no_cigar:
	if (ibmr) {
		rds_iw_destroy_fastreg(pool, ibmr);
		kfree(ibmr);
	}
	atomic_dec(&pool->item_count);
	return ERR_PTR(err);
}

void rds_iw_sync_mr(void *trans_private, int direction)
{
	struct rds_iw_mr *ibmr = trans_private;
	struct rds_iw_device *rds_iwdev = ibmr->device;

	switch (direction) {
	case DMA_FROM_DEVICE:
		ib_dma_sync_sg_for_cpu(rds_iwdev->dev, ibmr->mapping.m_sg.list,
			ibmr->mapping.m_sg.dma_len, DMA_BIDIRECTIONAL);
		break;
	case DMA_TO_DEVICE:
		ib_dma_sync_sg_for_device(rds_iwdev->dev, ibmr->mapping.m_sg.list,
			ibmr->mapping.m_sg.dma_len, DMA_BIDIRECTIONAL);
		break;
	}
}

/*
 * Flush our pool of MRs.
 * At a minimum, all currently unused MRs are unmapped.
 * If the number of MRs allocated exceeds the limit, we also try
 * to free as many MRs as needed to get back to this limit.
 */
static int rds_iw_flush_mr_pool(struct rds_iw_mr_pool *pool, int free_all)
{
	struct rds_iw_mr *ibmr, *next;
	LIST_HEAD(unmap_list);
	LIST_HEAD(kill_list);
	unsigned long flags;
	unsigned int nfreed = 0, ncleaned = 0, unpinned = 0;
	int ret = 0;

	rds_iw_stats_inc(s_iw_rdma_mr_pool_flush);

	mutex_lock(&pool->flush_lock);

	spin_lock_irqsave(&pool->list_lock, flags);
	/* Get the list of all mappings to be destroyed */
	list_splice_init(&pool->dirty_list, &unmap_list);
	if (free_all)
		list_splice_init(&pool->clean_list, &kill_list);
	spin_unlock_irqrestore(&pool->list_lock, flags);

	/* Batched invalidate of dirty MRs.
	 * For FMR based MRs, the mappings on the unmap list are
	 * actually members of an ibmr (ibmr->mapping). They either
	 * migrate to the kill_list, or have been cleaned and should be
	 * moved to the clean_list.
	 * For fastregs, they will be dynamically allocated, and
	 * will be destroyed by the unmap function.
	 */
	if (!list_empty(&unmap_list)) {
		ncleaned = rds_iw_unmap_fastreg_list(pool, &unmap_list,
						     &kill_list, &unpinned);
		/* If we've been asked to destroy all MRs, move those
		 * that were simply cleaned to the kill list */
		if (free_all)
			list_splice_init(&unmap_list, &kill_list);
	}

	/* Destroy any MRs that are past their best before date */
	list_for_each_entry_safe(ibmr, next, &kill_list, mapping.m_list) {
		rds_iw_stats_inc(s_iw_rdma_mr_free);
		list_del(&ibmr->mapping.m_list);
		rds_iw_destroy_fastreg(pool, ibmr);
		kfree(ibmr);
		nfreed++;
	}

	/* Anything that remains are laundered ibmrs, which we can add
	 * back to the clean list. */
	if (!list_empty(&unmap_list)) {
		spin_lock_irqsave(&pool->list_lock, flags);
		list_splice(&unmap_list, &pool->clean_list);
		spin_unlock_irqrestore(&pool->list_lock, flags);
	}

	atomic_sub(unpinned, &pool->free_pinned);
	atomic_sub(ncleaned, &pool->dirty_count);
	atomic_sub(nfreed, &pool->item_count);

	mutex_unlock(&pool->flush_lock);
	return ret;
}

static void rds_iw_mr_pool_flush_worker(struct work_struct *work)
{
	struct rds_iw_mr_pool *pool = container_of(work, struct rds_iw_mr_pool, flush_worker);

	rds_iw_flush_mr_pool(pool, 0);
}

void rds_iw_free_mr(void *trans_private, int invalidate)
{
	struct rds_iw_mr *ibmr = trans_private;
	struct rds_iw_mr_pool *pool = ibmr->device->mr_pool;

	rdsdebug("RDS/IW: free_mr nents %u\n", ibmr->mapping.m_sg.len);
	if (!pool)
		return;

	/* Return it to the pool's free list */
	rds_iw_free_fastreg(pool, ibmr);

	/* If we've pinned too many pages, request a flush */
	if (atomic_read(&pool->free_pinned) >= pool->max_free_pinned ||
	    atomic_read(&pool->dirty_count) >= pool->max_items / 10)
		queue_work(rds_wq, &pool->flush_worker);

	if (invalidate) {
		if (likely(!in_interrupt())) {
			rds_iw_flush_mr_pool(pool, 0);
		} else {
			/* We get here if the user created a MR marked
			 * as use_once and invalidate at the same time. */
			queue_work(rds_wq, &pool->flush_worker);
		}
	}
}

void rds_iw_flush_mrs(void)
{
	struct rds_iw_device *rds_iwdev;

	list_for_each_entry(rds_iwdev, &rds_iw_devices, list) {
		struct rds_iw_mr_pool *pool = rds_iwdev->mr_pool;

		if (pool)
			rds_iw_flush_mr_pool(pool, 0);
	}
}

void *rds_iw_get_mr(struct scatterlist *sg, unsigned long nents,
		    struct rds_sock *rs, u32 *key_ret)
{
	struct rds_iw_device *rds_iwdev;
	struct rds_iw_mr *ibmr = NULL;
	struct rdma_cm_id *cm_id;
	struct sockaddr_in src = {
		.sin_addr.s_addr = rs->rs_bound_addr,
		.sin_port = rs->rs_bound_port,
	};
	struct sockaddr_in dst = {
		.sin_addr.s_addr = rs->rs_conn_addr,
		.sin_port = rs->rs_conn_port,
	};
	int ret;

	ret = rds_iw_get_device(&src, &dst, &rds_iwdev, &cm_id);
	if (ret || !cm_id) {
		ret = -ENODEV;
		goto out;
	}

	if (!rds_iwdev->mr_pool) {
		ret = -ENODEV;
		goto out;
	}

	ibmr = rds_iw_alloc_mr(rds_iwdev);
	if (IS_ERR(ibmr))
		return ibmr;

	ibmr->cm_id = cm_id;
	ibmr->device = rds_iwdev;

	ret = rds_iw_map_fastreg(rds_iwdev->mr_pool, ibmr, sg, nents);
	if (ret == 0)
		*key_ret = ibmr->mr->rkey;
	else
		printk(KERN_WARNING "RDS/IW: failed to map mr (errno=%d)\n", ret);

out:
	if (ret) {
		if (ibmr)
			rds_iw_free_mr(ibmr, 0);
		ibmr = ERR_PTR(ret);
	}
	return ibmr;
}

/*
 * iWARP fastreg handling
 *
 * The life cycle of a fastreg registration is a bit different from
 * FMRs.
 * The idea behind fastreg is to have one MR, to which we bind different
 * mappings over time. To avoid stalling on the expensive map and invalidate
 * operations, these operations are pipelined on the same send queue on
 * which we want to send the message containing the r_key.
 *
 * This creates a bit of a problem for us, as we do not have the destination
 * IP in GET_MR, so the connection must be setup prior to the GET_MR call for
 * RDMA to be correctly setup.  If a fastreg request is present, rds_iw_xmit
 * will try to queue a LOCAL_INV (if needed) and a FAST_REG_MR work request
 * before queuing the SEND. When completions for these arrive, they are
 * dispatched to the MR has a bit set showing that RDMa can be performed.
 *
 * There is another interesting aspect that's related to invalidation.
 * The application can request that a mapping is invalidated in FREE_MR.
 * The expectation there is that this invalidation step includes ALL
 * PREVIOUSLY FREED MRs.
 */
static int rds_iw_init_fastreg(struct rds_iw_mr_pool *pool,
				struct rds_iw_mr *ibmr)
{
	struct rds_iw_device *rds_iwdev = pool->device;
	struct ib_fast_reg_page_list *page_list = NULL;
	struct ib_mr *mr;
	int err;

	mr = ib_alloc_fast_reg_mr(rds_iwdev->pd, pool->max_message_size);
	if (IS_ERR(mr)) {
		err = PTR_ERR(mr);

		printk(KERN_WARNING "RDS/IW: ib_alloc_fast_reg_mr failed (err=%d)\n", err);
		return err;
	}

	/* FIXME - this is overkill, but mapping->m_sg.dma_len/mapping->m_sg.dma_npages
	 * is not filled in.
	 */
	page_list = ib_alloc_fast_reg_page_list(rds_iwdev->dev, pool->max_message_size);
	if (IS_ERR(page_list)) {
		err = PTR_ERR(page_list);

		printk(KERN_WARNING "RDS/IW: ib_alloc_fast_reg_page_list failed (err=%d)\n", err);
		ib_dereg_mr(mr);
		return err;
	}

	ibmr->page_list = page_list;
	ibmr->mr = mr;
	return 0;
}

static int rds_iw_rdma_build_fastreg(struct rds_iw_mapping *mapping)
{
	struct rds_iw_mr *ibmr = mapping->m_mr;
	struct ib_send_wr f_wr, *failed_wr;
	int ret;

	/*
	 * Perform a WR for the fast_reg_mr. Each individual page
	 * in the sg list is added to the fast reg page list and placed
	 * inside the fast_reg_mr WR.  The key used is a rolling 8bit
	 * counter, which should guarantee uniqueness.
	 */
	ib_update_fast_reg_key(ibmr->mr, ibmr->remap_count++);
	mapping->m_rkey = ibmr->mr->rkey;

	memset(&f_wr, 0, sizeof(f_wr));
	f_wr.wr_id = RDS_IW_FAST_REG_WR_ID;
	f_wr.opcode = IB_WR_FAST_REG_MR;
	f_wr.wr.fast_reg.length = mapping->m_sg.bytes;
	f_wr.wr.fast_reg.rkey = mapping->m_rkey;
	f_wr.wr.fast_reg.page_list = ibmr->page_list;
	f_wr.wr.fast_reg.page_list_len = mapping->m_sg.dma_len;
	f_wr.wr.fast_reg.page_shift = PAGE_SHIFT;
	f_wr.wr.fast_reg.access_flags = IB_ACCESS_LOCAL_WRITE |
				IB_ACCESS_REMOTE_READ |
				IB_ACCESS_REMOTE_WRITE;
	f_wr.wr.fast_reg.iova_start = 0;
	f_wr.send_flags = IB_SEND_SIGNALED;

	failed_wr = &f_wr;
	ret = ib_post_send(ibmr->cm_id->qp, &f_wr, &failed_wr);
	BUG_ON(failed_wr != &f_wr);
	if (ret)
		printk_ratelimited(KERN_WARNING "RDS/IW: %s:%d ib_post_send returned %d\n",
			__func__, __LINE__, ret);
	return ret;
}

static int rds_iw_rdma_fastreg_inv(struct rds_iw_mr *ibmr)
{
	struct ib_send_wr s_wr, *failed_wr;
	int ret = 0;

	if (!ibmr->cm_id->qp || !ibmr->mr)
		goto out;

	memset(&s_wr, 0, sizeof(s_wr));
	s_wr.wr_id = RDS_IW_LOCAL_INV_WR_ID;
	s_wr.opcode = IB_WR_LOCAL_INV;
	s_wr.ex.invalidate_rkey = ibmr->mr->rkey;
	s_wr.send_flags = IB_SEND_SIGNALED;

	failed_wr = &s_wr;
	ret = ib_post_send(ibmr->cm_id->qp, &s_wr, &failed_wr);
	if (ret) {
		printk_ratelimited(KERN_WARNING "RDS/IW: %s:%d ib_post_send returned %d\n",
			__func__, __LINE__, ret);
		goto out;
	}
out:
	return ret;
}

static int rds_iw_map_fastreg(struct rds_iw_mr_pool *pool,
			struct rds_iw_mr *ibmr,
			struct scatterlist *sg,
			unsigned int sg_len)
{
	struct rds_iw_device *rds_iwdev = pool->device;
	struct rds_iw_mapping *mapping = &ibmr->mapping;
	u64 *dma_pages;
	int i, ret = 0;

	rds_iw_set_scatterlist(&mapping->m_sg, sg, sg_len);

	dma_pages = rds_iw_map_scatterlist(rds_iwdev, &mapping->m_sg);
	if (IS_ERR(dma_pages)) {
		ret = PTR_ERR(dma_pages);
		dma_pages = NULL;
		goto out;
	}

	if (mapping->m_sg.dma_len > pool->max_message_size) {
		ret = -EMSGSIZE;
		goto out;
	}

	for (i = 0; i < mapping->m_sg.dma_npages; ++i)
		ibmr->page_list->page_list[i] = dma_pages[i];

	ret = rds_iw_rdma_build_fastreg(mapping);
	if (ret)
		goto out;

	rds_iw_stats_inc(s_iw_rdma_mr_used);

out:
	kfree(dma_pages);

	return ret;
}

/*
 * "Free" a fastreg MR.
 */
static void rds_iw_free_fastreg(struct rds_iw_mr_pool *pool,
		struct rds_iw_mr *ibmr)
{
	unsigned long flags;
	int ret;

	if (!ibmr->mapping.m_sg.dma_len)
		return;

	ret = rds_iw_rdma_fastreg_inv(ibmr);
	if (ret)
		return;

	/* Try to post the LOCAL_INV WR to the queue. */
	spin_lock_irqsave(&pool->list_lock, flags);

	list_add_tail(&ibmr->mapping.m_list, &pool->dirty_list);
	atomic_add(ibmr->mapping.m_sg.len, &pool->free_pinned);
	atomic_inc(&pool->dirty_count);

	spin_unlock_irqrestore(&pool->list_lock, flags);
}

static unsigned int rds_iw_unmap_fastreg_list(struct rds_iw_mr_pool *pool,
				struct list_head *unmap_list,
				struct list_head *kill_list,
				int *unpinned)
{
	struct rds_iw_mapping *mapping, *next;
	unsigned int ncleaned = 0;
	LIST_HEAD(laundered);

	/* Batched invalidation of fastreg MRs.
	 * Why do we do it this way, even though we could pipeline unmap
	 * and remap? The reason is the application semantics - when the
	 * application requests an invalidation of MRs, it expects all
	 * previously released R_Keys to become invalid.
	 *
	 * If we implement MR reuse naively, we risk memory corruption
	 * (this has actually been observed). So the default behavior
	 * requires that a MR goes through an explicit unmap operation before
	 * we can reuse it again.
	 *
	 * We could probably improve on this a little, by allowing immediate
	 * reuse of a MR on the same socket (eg you could add small
	 * cache of unused MRs to strct rds_socket - GET_MR could grab one
	 * of these without requiring an explicit invalidate).
	 */
	while (!list_empty(unmap_list)) {
		unsigned long flags;

		spin_lock_irqsave(&pool->list_lock, flags);
		list_for_each_entry_safe(mapping, next, unmap_list, m_list) {
			*unpinned += mapping->m_sg.len;
			list_move(&mapping->m_list, &laundered);
			ncleaned++;
		}
		spin_unlock_irqrestore(&pool->list_lock, flags);
	}

	/* Move all laundered mappings back to the unmap list.
	 * We do not kill any WRs right now - it doesn't seem the
	 * fastreg API has a max_remap limit. */
	list_splice_init(&laundered, unmap_list);

	return ncleaned;
}

static void rds_iw_destroy_fastreg(struct rds_iw_mr_pool *pool,
		struct rds_iw_mr *ibmr)
{
	if (ibmr->page_list)
		ib_free_fast_reg_page_list(ibmr->page_list);
	if (ibmr->mr)
		ib_dereg_mr(ibmr->mr);
}
