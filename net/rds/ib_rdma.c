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
#include <linux/rculist.h>

#include "rds.h"
#include "ib.h"
#include "xlist.h"

static DEFINE_PER_CPU(unsigned long, clean_list_grace);
#define CLEAN_LIST_BUSY_BIT 0

/*
 * This is stored as mr->r_trans_private.
 */
struct rds_ib_mr {
	struct rds_ib_device	*device;
	struct rds_ib_mr_pool	*pool;
	struct ib_fmr		*fmr;

	struct xlist_head	xlist;

	/* unmap_list is for freeing */
	struct list_head	unmap_list;
	unsigned int		remap_count;

	struct scatterlist	*sg;
	unsigned int		sg_len;
	u64			*dma;
	int			sg_dma_len;
};

/*
 * Our own little FMR pool
 */
struct rds_ib_mr_pool {
	struct mutex		flush_lock;		/* serialize fmr invalidate */
	struct delayed_work	flush_worker;		/* flush worker */

	atomic_t		item_count;		/* total # of MRs */
	atomic_t		dirty_count;		/* # dirty of MRs */

	struct xlist_head	drop_list;		/* MRs that have reached their max_maps limit */
	struct xlist_head	free_list;		/* unused MRs */
	struct xlist_head	clean_list;		/* global unused & unamapped MRs */
	wait_queue_head_t	flush_wait;

	atomic_t		free_pinned;		/* memory pinned by free MRs */
	unsigned long		max_items;
	unsigned long		max_items_soft;
	unsigned long		max_free_pinned;
	struct ib_fmr_attr	fmr_attr;
};

static int rds_ib_flush_mr_pool(struct rds_ib_mr_pool *pool, int free_all, struct rds_ib_mr **);
static void rds_ib_teardown_mr(struct rds_ib_mr *ibmr);
static void rds_ib_mr_pool_flush_worker(struct work_struct *work);

static struct rds_ib_device *rds_ib_get_device(__be32 ipaddr)
{
	struct rds_ib_device *rds_ibdev;
	struct rds_ib_ipaddr *i_ipaddr;

	rcu_read_lock();
	list_for_each_entry_rcu(rds_ibdev, &rds_ib_devices, list) {
		list_for_each_entry_rcu(i_ipaddr, &rds_ibdev->ipaddr_list, list) {
			if (i_ipaddr->ipaddr == ipaddr) {
				atomic_inc(&rds_ibdev->refcount);
				rcu_read_unlock();
				return rds_ibdev;
			}
		}
	}
	rcu_read_unlock();

	return NULL;
}

static int rds_ib_add_ipaddr(struct rds_ib_device *rds_ibdev, __be32 ipaddr)
{
	struct rds_ib_ipaddr *i_ipaddr;

	i_ipaddr = kmalloc(sizeof *i_ipaddr, GFP_KERNEL);
	if (!i_ipaddr)
		return -ENOMEM;

	i_ipaddr->ipaddr = ipaddr;

	spin_lock_irq(&rds_ibdev->spinlock);
	list_add_tail_rcu(&i_ipaddr->list, &rds_ibdev->ipaddr_list);
	spin_unlock_irq(&rds_ibdev->spinlock);

	return 0;
}

static void rds_ib_remove_ipaddr(struct rds_ib_device *rds_ibdev, __be32 ipaddr)
{
	struct rds_ib_ipaddr *i_ipaddr;
	struct rds_ib_ipaddr *to_free = NULL;


	spin_lock_irq(&rds_ibdev->spinlock);
	list_for_each_entry_rcu(i_ipaddr, &rds_ibdev->ipaddr_list, list) {
		if (i_ipaddr->ipaddr == ipaddr) {
			list_del_rcu(&i_ipaddr->list);
			to_free = i_ipaddr;
			break;
		}
	}
	spin_unlock_irq(&rds_ibdev->spinlock);

	if (to_free) {
		synchronize_rcu();
		kfree(to_free);
	}
}

int rds_ib_update_ipaddr(struct rds_ib_device *rds_ibdev, __be32 ipaddr)
{
	struct rds_ib_device *rds_ibdev_old;

	rds_ibdev_old = rds_ib_get_device(ipaddr);
	if (rds_ibdev_old) {
		rds_ib_remove_ipaddr(rds_ibdev_old, ipaddr);
		rds_ib_dev_put(rds_ibdev_old);
	}

	return rds_ib_add_ipaddr(rds_ibdev, ipaddr);
}

void rds_ib_add_conn(struct rds_ib_device *rds_ibdev, struct rds_connection *conn)
{
	struct rds_ib_connection *ic = conn->c_transport_data;

	/* conn was previously on the nodev_conns_list */
	spin_lock_irq(&ib_nodev_conns_lock);
	BUG_ON(list_empty(&ib_nodev_conns));
	BUG_ON(list_empty(&ic->ib_node));
	list_del(&ic->ib_node);

	spin_lock(&rds_ibdev->spinlock);
	list_add_tail(&ic->ib_node, &rds_ibdev->conn_list);
	spin_unlock(&rds_ibdev->spinlock);
	spin_unlock_irq(&ib_nodev_conns_lock);

	ic->rds_ibdev = rds_ibdev;
	atomic_inc(&rds_ibdev->refcount);
}

void rds_ib_remove_conn(struct rds_ib_device *rds_ibdev, struct rds_connection *conn)
{
	struct rds_ib_connection *ic = conn->c_transport_data;

	/* place conn on nodev_conns_list */
	spin_lock(&ib_nodev_conns_lock);

	spin_lock_irq(&rds_ibdev->spinlock);
	BUG_ON(list_empty(&ic->ib_node));
	list_del(&ic->ib_node);
	spin_unlock_irq(&rds_ibdev->spinlock);

	list_add_tail(&ic->ib_node, &ib_nodev_conns);

	spin_unlock(&ib_nodev_conns_lock);

	ic->rds_ibdev = NULL;
	rds_ib_dev_put(rds_ibdev);
}

void rds_ib_destroy_nodev_conns(void)
{
	struct rds_ib_connection *ic, *_ic;
	LIST_HEAD(tmp_list);

	/* avoid calling conn_destroy with irqs off */
	spin_lock_irq(&ib_nodev_conns_lock);
	list_splice(&ib_nodev_conns, &tmp_list);
	spin_unlock_irq(&ib_nodev_conns_lock);

	list_for_each_entry_safe(ic, _ic, &tmp_list, ib_node)
		rds_conn_destroy(ic->conn);
}

struct rds_ib_mr_pool *rds_ib_create_mr_pool(struct rds_ib_device *rds_ibdev)
{
	struct rds_ib_mr_pool *pool;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	INIT_XLIST_HEAD(&pool->free_list);
	INIT_XLIST_HEAD(&pool->drop_list);
	INIT_XLIST_HEAD(&pool->clean_list);
	mutex_init(&pool->flush_lock);
	init_waitqueue_head(&pool->flush_wait);
	INIT_DELAYED_WORK(&pool->flush_worker, rds_ib_mr_pool_flush_worker);

	pool->fmr_attr.max_pages = fmr_message_size;
	pool->fmr_attr.max_maps = rds_ibdev->fmr_max_remaps;
	pool->fmr_attr.page_shift = PAGE_SHIFT;
	pool->max_free_pinned = rds_ibdev->max_fmrs * fmr_message_size / 4;

	/* We never allow more than max_items MRs to be allocated.
	 * When we exceed more than max_items_soft, we start freeing
	 * items more aggressively.
	 * Make sure that max_items > max_items_soft > max_items / 2
	 */
	pool->max_items_soft = rds_ibdev->max_fmrs * 3 / 4;
	pool->max_items = rds_ibdev->max_fmrs;

	return pool;
}

void rds_ib_get_mr_info(struct rds_ib_device *rds_ibdev, struct rds_info_rdma_connection *iinfo)
{
	struct rds_ib_mr_pool *pool = rds_ibdev->mr_pool;

	iinfo->rdma_mr_max = pool->max_items;
	iinfo->rdma_mr_size = pool->fmr_attr.max_pages;
}

void rds_ib_destroy_mr_pool(struct rds_ib_mr_pool *pool)
{
	cancel_delayed_work_sync(&pool->flush_worker);
	rds_ib_flush_mr_pool(pool, 1, NULL);
	WARN_ON(atomic_read(&pool->item_count));
	WARN_ON(atomic_read(&pool->free_pinned));
	kfree(pool);
}

static void refill_local(struct rds_ib_mr_pool *pool, struct xlist_head *xl,
			 struct rds_ib_mr **ibmr_ret)
{
	struct xlist_head *ibmr_xl;
	ibmr_xl = xlist_del_head_fast(xl);
	*ibmr_ret = list_entry(ibmr_xl, struct rds_ib_mr, xlist);
}

static inline struct rds_ib_mr *rds_ib_reuse_fmr(struct rds_ib_mr_pool *pool)
{
	struct rds_ib_mr *ibmr = NULL;
	struct xlist_head *ret;
	unsigned long *flag;

	preempt_disable();
	flag = &__get_cpu_var(clean_list_grace);
	set_bit(CLEAN_LIST_BUSY_BIT, flag);
	ret = xlist_del_head(&pool->clean_list);
	if (ret)
		ibmr = list_entry(ret, struct rds_ib_mr, xlist);

	clear_bit(CLEAN_LIST_BUSY_BIT, flag);
	preempt_enable();
	return ibmr;
}

static inline void wait_clean_list_grace(void)
{
	int cpu;
	unsigned long *flag;

	for_each_online_cpu(cpu) {
		flag = &per_cpu(clean_list_grace, cpu);
		while (test_bit(CLEAN_LIST_BUSY_BIT, flag))
			cpu_relax();
	}
}

static struct rds_ib_mr *rds_ib_alloc_fmr(struct rds_ib_device *rds_ibdev)
{
	struct rds_ib_mr_pool *pool = rds_ibdev->mr_pool;
	struct rds_ib_mr *ibmr = NULL;
	int err = 0, iter = 0;

	if (atomic_read(&pool->dirty_count) >= pool->max_items / 10)
		schedule_delayed_work(&pool->flush_worker, 10);

	while (1) {
		ibmr = rds_ib_reuse_fmr(pool);
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
			rds_ib_stats_inc(s_ib_rdma_mr_pool_depleted);
			return ERR_PTR(-EAGAIN);
		}

		/* We do have some empty MRs. Flush them out. */
		rds_ib_stats_inc(s_ib_rdma_mr_pool_wait);
		rds_ib_flush_mr_pool(pool, 0, &ibmr);
		if (ibmr)
			return ibmr;
	}

	ibmr = kzalloc_node(sizeof(*ibmr), GFP_KERNEL, rdsibdev_to_node(rds_ibdev));
	if (!ibmr) {
		err = -ENOMEM;
		goto out_no_cigar;
	}

	memset(ibmr, 0, sizeof(*ibmr));

	ibmr->fmr = ib_alloc_fmr(rds_ibdev->pd,
			(IB_ACCESS_LOCAL_WRITE |
			 IB_ACCESS_REMOTE_READ |
			 IB_ACCESS_REMOTE_WRITE|
			 IB_ACCESS_REMOTE_ATOMIC),
			&pool->fmr_attr);
	if (IS_ERR(ibmr->fmr)) {
		err = PTR_ERR(ibmr->fmr);
		ibmr->fmr = NULL;
		printk(KERN_WARNING "RDS/IB: ib_alloc_fmr failed (err=%d)\n", err);
		goto out_no_cigar;
	}

	rds_ib_stats_inc(s_ib_rdma_mr_alloc);
	return ibmr;

out_no_cigar:
	if (ibmr) {
		if (ibmr->fmr)
			ib_dealloc_fmr(ibmr->fmr);
		kfree(ibmr);
	}
	atomic_dec(&pool->item_count);
	return ERR_PTR(err);
}

static int rds_ib_map_fmr(struct rds_ib_device *rds_ibdev, struct rds_ib_mr *ibmr,
	       struct scatterlist *sg, unsigned int nents)
{
	struct ib_device *dev = rds_ibdev->dev;
	struct scatterlist *scat = sg;
	u64 io_addr = 0;
	u64 *dma_pages;
	u32 len;
	int page_cnt, sg_dma_len;
	int i, j;
	int ret;

	sg_dma_len = ib_dma_map_sg(dev, sg, nents,
				 DMA_BIDIRECTIONAL);
	if (unlikely(!sg_dma_len)) {
		printk(KERN_WARNING "RDS/IB: dma_map_sg failed!\n");
		return -EBUSY;
	}

	len = 0;
	page_cnt = 0;

	for (i = 0; i < sg_dma_len; ++i) {
		unsigned int dma_len = ib_sg_dma_len(dev, &scat[i]);
		u64 dma_addr = ib_sg_dma_address(dev, &scat[i]);

		if (dma_addr & ~PAGE_MASK) {
			if (i > 0)
				return -EINVAL;
			else
				++page_cnt;
		}
		if ((dma_addr + dma_len) & ~PAGE_MASK) {
			if (i < sg_dma_len - 1)
				return -EINVAL;
			else
				++page_cnt;
		}

		len += dma_len;
	}

	page_cnt += len >> PAGE_SHIFT;
	if (page_cnt > fmr_message_size)
		return -EINVAL;

	dma_pages = kmalloc_node(sizeof(u64) * page_cnt, GFP_ATOMIC,
				 rdsibdev_to_node(rds_ibdev));
	if (!dma_pages)
		return -ENOMEM;

	page_cnt = 0;
	for (i = 0; i < sg_dma_len; ++i) {
		unsigned int dma_len = ib_sg_dma_len(dev, &scat[i]);
		u64 dma_addr = ib_sg_dma_address(dev, &scat[i]);

		for (j = 0; j < dma_len; j += PAGE_SIZE)
			dma_pages[page_cnt++] =
				(dma_addr & PAGE_MASK) + j;
	}

	ret = ib_map_phys_fmr(ibmr->fmr,
				   dma_pages, page_cnt, io_addr);
	if (ret)
		goto out;

	/* Success - we successfully remapped the MR, so we can
	 * safely tear down the old mapping. */
	rds_ib_teardown_mr(ibmr);

	ibmr->sg = scat;
	ibmr->sg_len = nents;
	ibmr->sg_dma_len = sg_dma_len;
	ibmr->remap_count++;

	rds_ib_stats_inc(s_ib_rdma_mr_used);
	ret = 0;

out:
	kfree(dma_pages);

	return ret;
}

void rds_ib_sync_mr(void *trans_private, int direction)
{
	struct rds_ib_mr *ibmr = trans_private;
	struct rds_ib_device *rds_ibdev = ibmr->device;

	switch (direction) {
	case DMA_FROM_DEVICE:
		ib_dma_sync_sg_for_cpu(rds_ibdev->dev, ibmr->sg,
			ibmr->sg_dma_len, DMA_BIDIRECTIONAL);
		break;
	case DMA_TO_DEVICE:
		ib_dma_sync_sg_for_device(rds_ibdev->dev, ibmr->sg,
			ibmr->sg_dma_len, DMA_BIDIRECTIONAL);
		break;
	}
}

static void __rds_ib_teardown_mr(struct rds_ib_mr *ibmr)
{
	struct rds_ib_device *rds_ibdev = ibmr->device;

	if (ibmr->sg_dma_len) {
		ib_dma_unmap_sg(rds_ibdev->dev,
				ibmr->sg, ibmr->sg_len,
				DMA_BIDIRECTIONAL);
		ibmr->sg_dma_len = 0;
	}

	/* Release the s/g list */
	if (ibmr->sg_len) {
		unsigned int i;

		for (i = 0; i < ibmr->sg_len; ++i) {
			struct page *page = sg_page(&ibmr->sg[i]);

			/* FIXME we need a way to tell a r/w MR
			 * from a r/o MR */
			BUG_ON(irqs_disabled());
			set_page_dirty(page);
			put_page(page);
		}
		kfree(ibmr->sg);

		ibmr->sg = NULL;
		ibmr->sg_len = 0;
	}
}

static void rds_ib_teardown_mr(struct rds_ib_mr *ibmr)
{
	unsigned int pinned = ibmr->sg_len;

	__rds_ib_teardown_mr(ibmr);
	if (pinned) {
		struct rds_ib_device *rds_ibdev = ibmr->device;
		struct rds_ib_mr_pool *pool = rds_ibdev->mr_pool;

		atomic_sub(pinned, &pool->free_pinned);
	}
}

static inline unsigned int rds_ib_flush_goal(struct rds_ib_mr_pool *pool, int free_all)
{
	unsigned int item_count;

	item_count = atomic_read(&pool->item_count);
	if (free_all)
		return item_count;

	return 0;
}

/*
 * given an xlist of mrs, put them all into the list_head for more processing
 */
static void xlist_append_to_list(struct xlist_head *xlist, struct list_head *list)
{
	struct rds_ib_mr *ibmr;
	struct xlist_head splice;
	struct xlist_head *cur;
	struct xlist_head *next;

	splice.next = NULL;
	xlist_splice(xlist, &splice);
	cur = splice.next;
	while (cur) {
		next = cur->next;
		ibmr = list_entry(cur, struct rds_ib_mr, xlist);
		list_add_tail(&ibmr->unmap_list, list);
		cur = next;
	}
}

/*
 * this takes a list head of mrs and turns it into an xlist of clusters.
 * each cluster has an xlist of MR_CLUSTER_SIZE mrs that are ready for
 * reuse.
 */
static void list_append_to_xlist(struct rds_ib_mr_pool *pool,
				struct list_head *list, struct xlist_head *xlist,
				struct xlist_head **tail_ret)
{
	struct rds_ib_mr *ibmr;
	struct xlist_head *cur_mr = xlist;
	struct xlist_head *tail_mr = NULL;

	list_for_each_entry(ibmr, list, unmap_list) {
		tail_mr = &ibmr->xlist;
		tail_mr->next = NULL;
		cur_mr->next = tail_mr;
		cur_mr = tail_mr;
	}
	*tail_ret = tail_mr;
}

/*
 * Flush our pool of MRs.
 * At a minimum, all currently unused MRs are unmapped.
 * If the number of MRs allocated exceeds the limit, we also try
 * to free as many MRs as needed to get back to this limit.
 */
static int rds_ib_flush_mr_pool(struct rds_ib_mr_pool *pool,
			        int free_all, struct rds_ib_mr **ibmr_ret)
{
	struct rds_ib_mr *ibmr, *next;
	struct xlist_head clean_xlist;
	struct xlist_head *clean_tail;
	LIST_HEAD(unmap_list);
	LIST_HEAD(fmr_list);
	unsigned long unpinned = 0;
	unsigned int nfreed = 0, ncleaned = 0, free_goal;
	int ret = 0;

	rds_ib_stats_inc(s_ib_rdma_mr_pool_flush);

	if (ibmr_ret) {
		DEFINE_WAIT(wait);
		while(!mutex_trylock(&pool->flush_lock)) {
			ibmr = rds_ib_reuse_fmr(pool);
			if (ibmr) {
				*ibmr_ret = ibmr;
				finish_wait(&pool->flush_wait, &wait);
				goto out_nolock;
			}

			prepare_to_wait(&pool->flush_wait, &wait,
					TASK_UNINTERRUPTIBLE);
			if (xlist_empty(&pool->clean_list))
				schedule();

			ibmr = rds_ib_reuse_fmr(pool);
			if (ibmr) {
				*ibmr_ret = ibmr;
				finish_wait(&pool->flush_wait, &wait);
				goto out_nolock;
			}
		}
		finish_wait(&pool->flush_wait, &wait);
	} else
		mutex_lock(&pool->flush_lock);

	if (ibmr_ret) {
		ibmr = rds_ib_reuse_fmr(pool);
		if (ibmr) {
			*ibmr_ret = ibmr;
			goto out;
		}
	}

	/* Get the list of all MRs to be dropped. Ordering matters -
	 * we want to put drop_list ahead of free_list.
	 */
	xlist_append_to_list(&pool->drop_list, &unmap_list);
	xlist_append_to_list(&pool->free_list, &unmap_list);
	if (free_all)
		xlist_append_to_list(&pool->clean_list, &unmap_list);

	free_goal = rds_ib_flush_goal(pool, free_all);

	if (list_empty(&unmap_list))
		goto out;

	/* String all ib_mr's onto one list and hand them to ib_unmap_fmr */
	list_for_each_entry(ibmr, &unmap_list, unmap_list)
		list_add(&ibmr->fmr->list, &fmr_list);

	ret = ib_unmap_fmr(&fmr_list);
	if (ret)
		printk(KERN_WARNING "RDS/IB: ib_unmap_fmr failed (err=%d)\n", ret);

	/* Now we can destroy the DMA mapping and unpin any pages */
	list_for_each_entry_safe(ibmr, next, &unmap_list, unmap_list) {
		unpinned += ibmr->sg_len;
		__rds_ib_teardown_mr(ibmr);
		if (nfreed < free_goal || ibmr->remap_count >= pool->fmr_attr.max_maps) {
			rds_ib_stats_inc(s_ib_rdma_mr_free);
			list_del(&ibmr->unmap_list);
			ib_dealloc_fmr(ibmr->fmr);
			kfree(ibmr);
			nfreed++;
		}
		ncleaned++;
	}

	if (!list_empty(&unmap_list)) {
		/* we have to make sure that none of the things we're about
		 * to put on the clean list would race with other cpus trying
		 * to pull items off.  The xlist would explode if we managed to
		 * remove something from the clean list and then add it back again
		 * while another CPU was spinning on that same item in xlist_del_head.
		 *
		 * This is pretty unlikely, but just in case  wait for an xlist grace period
		 * here before adding anything back into the clean list.
		 */
		wait_clean_list_grace();

		list_append_to_xlist(pool, &unmap_list, &clean_xlist, &clean_tail);
		if (ibmr_ret)
			refill_local(pool, &clean_xlist, ibmr_ret);

		/* refill_local may have emptied our list */
		if (!xlist_empty(&clean_xlist))
			xlist_add(clean_xlist.next, clean_tail, &pool->clean_list);

	}

	atomic_sub(unpinned, &pool->free_pinned);
	atomic_sub(ncleaned, &pool->dirty_count);
	atomic_sub(nfreed, &pool->item_count);

out:
	mutex_unlock(&pool->flush_lock);
	if (waitqueue_active(&pool->flush_wait))
		wake_up(&pool->flush_wait);
out_nolock:
	return ret;
}

static void rds_ib_mr_pool_flush_worker(struct work_struct *work)
{
	struct rds_ib_mr_pool *pool = container_of(work, struct rds_ib_mr_pool, flush_worker.work);

	rds_ib_flush_mr_pool(pool, 0, NULL);
}

void rds_ib_free_mr(void *trans_private, int invalidate)
{
	struct rds_ib_mr *ibmr = trans_private;
	struct rds_ib_device *rds_ibdev = ibmr->device;
	struct rds_ib_mr_pool *pool = rds_ibdev->mr_pool;

	rdsdebug("RDS/IB: free_mr nents %u\n", ibmr->sg_len);

	/* Return it to the pool's free list */
	if (ibmr->remap_count >= pool->fmr_attr.max_maps)
		xlist_add(&ibmr->xlist, &ibmr->xlist, &pool->drop_list);
	else
		xlist_add(&ibmr->xlist, &ibmr->xlist, &pool->free_list);

	atomic_add(ibmr->sg_len, &pool->free_pinned);
	atomic_inc(&pool->dirty_count);

	/* If we've pinned too many pages, request a flush */
	if (atomic_read(&pool->free_pinned) >= pool->max_free_pinned ||
	    atomic_read(&pool->dirty_count) >= pool->max_items / 10)
		schedule_delayed_work(&pool->flush_worker, 10);

	if (invalidate) {
		if (likely(!in_interrupt())) {
			rds_ib_flush_mr_pool(pool, 0, NULL);
		} else {
			/* We get here if the user created a MR marked
			 * as use_once and invalidate at the same time. */
			schedule_delayed_work(&pool->flush_worker, 10);
		}
	}

	rds_ib_dev_put(rds_ibdev);
}

void rds_ib_flush_mrs(void)
{
	struct rds_ib_device *rds_ibdev;

	down_read(&rds_ib_devices_lock);
	list_for_each_entry(rds_ibdev, &rds_ib_devices, list) {
		struct rds_ib_mr_pool *pool = rds_ibdev->mr_pool;

		if (pool)
			rds_ib_flush_mr_pool(pool, 0, NULL);
	}
	up_read(&rds_ib_devices_lock);
}

void *rds_ib_get_mr(struct scatterlist *sg, unsigned long nents,
		    struct rds_sock *rs, u32 *key_ret)
{
	struct rds_ib_device *rds_ibdev;
	struct rds_ib_mr *ibmr = NULL;
	int ret;

	rds_ibdev = rds_ib_get_device(rs->rs_bound_addr);
	if (!rds_ibdev) {
		ret = -ENODEV;
		goto out;
	}

	if (!rds_ibdev->mr_pool) {
		ret = -ENODEV;
		goto out;
	}

	ibmr = rds_ib_alloc_fmr(rds_ibdev);
	if (IS_ERR(ibmr))
		return ibmr;

	ret = rds_ib_map_fmr(rds_ibdev, ibmr, sg, nents);
	if (ret == 0)
		*key_ret = ibmr->fmr->rkey;
	else
		printk(KERN_WARNING "RDS/IB: map_fmr failed (errno=%d)\n", ret);

	ibmr->device = rds_ibdev;
	rds_ibdev = NULL;

 out:
	if (ret) {
		if (ibmr)
			rds_ib_free_mr(ibmr, 0);
		ibmr = ERR_PTR(ret);
	}
	if (rds_ibdev)
		rds_ib_dev_put(rds_ibdev);
	return ibmr;
}

