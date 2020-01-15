/*
 * Copyright (c) 2006, 2018 Oracle and/or its affiliates. All rights reserved.
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
#include <linux/llist.h>

#include "rds_single_path.h"
#include "ib_mr.h"
#include "rds.h"

struct workqueue_struct *rds_ib_mr_wq;
struct rds_ib_dereg_odp_mr {
	struct work_struct work;
	struct ib_mr *mr;
};

static void rds_ib_odp_mr_worker(struct work_struct *work);

static struct rds_ib_device *rds_ib_get_device(__be32 ipaddr)
{
	struct rds_ib_device *rds_ibdev;
	struct rds_ib_ipaddr *i_ipaddr;

	rcu_read_lock();
	list_for_each_entry_rcu(rds_ibdev, &rds_ib_devices, list) {
		list_for_each_entry_rcu(i_ipaddr, &rds_ibdev->ipaddr_list, list) {
			if (i_ipaddr->ipaddr == ipaddr) {
				refcount_inc(&rds_ibdev->refcount);
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

	if (to_free)
		kfree_rcu(to_free, rcu);
}

int rds_ib_update_ipaddr(struct rds_ib_device *rds_ibdev,
			 struct in6_addr *ipaddr)
{
	struct rds_ib_device *rds_ibdev_old;

	rds_ibdev_old = rds_ib_get_device(ipaddr->s6_addr32[3]);
	if (!rds_ibdev_old)
		return rds_ib_add_ipaddr(rds_ibdev, ipaddr->s6_addr32[3]);

	if (rds_ibdev_old != rds_ibdev) {
		rds_ib_remove_ipaddr(rds_ibdev_old, ipaddr->s6_addr32[3]);
		rds_ib_dev_put(rds_ibdev_old);
		return rds_ib_add_ipaddr(rds_ibdev, ipaddr->s6_addr32[3]);
	}
	rds_ib_dev_put(rds_ibdev_old);

	return 0;
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
	refcount_inc(&rds_ibdev->refcount);
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

void rds_ib_get_mr_info(struct rds_ib_device *rds_ibdev, struct rds_info_rdma_connection *iinfo)
{
	struct rds_ib_mr_pool *pool_1m = rds_ibdev->mr_1m_pool;

	iinfo->rdma_mr_max = pool_1m->max_items;
	iinfo->rdma_mr_size = pool_1m->fmr_attr.max_pages;
}

#if IS_ENABLED(CONFIG_IPV6)
void rds6_ib_get_mr_info(struct rds_ib_device *rds_ibdev,
			 struct rds6_info_rdma_connection *iinfo6)
{
	struct rds_ib_mr_pool *pool_1m = rds_ibdev->mr_1m_pool;

	iinfo6->rdma_mr_max = pool_1m->max_items;
	iinfo6->rdma_mr_size = pool_1m->fmr_attr.max_pages;
}
#endif

struct rds_ib_mr *rds_ib_reuse_mr(struct rds_ib_mr_pool *pool)
{
	struct rds_ib_mr *ibmr = NULL;
	struct llist_node *ret;
	unsigned long flags;

	spin_lock_irqsave(&pool->clean_lock, flags);
	ret = llist_del_first(&pool->clean_list);
	spin_unlock_irqrestore(&pool->clean_lock, flags);
	if (ret) {
		ibmr = llist_entry(ret, struct rds_ib_mr, llnode);
		if (pool->pool_type == RDS_IB_MR_8K_POOL)
			rds_ib_stats_inc(s_ib_rdma_mr_8k_reused);
		else
			rds_ib_stats_inc(s_ib_rdma_mr_1m_reused);
	}

	return ibmr;
}

void rds_ib_sync_mr(void *trans_private, int direction)
{
	struct rds_ib_mr *ibmr = trans_private;
	struct rds_ib_device *rds_ibdev = ibmr->device;

	if (ibmr->odp)
		return;

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

void __rds_ib_teardown_mr(struct rds_ib_mr *ibmr)
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
			WARN_ON(!page->mapping && irqs_disabled());
			set_page_dirty(page);
			put_page(page);
		}
		kfree(ibmr->sg);

		ibmr->sg = NULL;
		ibmr->sg_len = 0;
	}
}

void rds_ib_teardown_mr(struct rds_ib_mr *ibmr)
{
	unsigned int pinned = ibmr->sg_len;

	__rds_ib_teardown_mr(ibmr);
	if (pinned) {
		struct rds_ib_mr_pool *pool = ibmr->pool;

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
 * given an llist of mrs, put them all into the list_head for more processing
 */
static unsigned int llist_append_to_list(struct llist_head *llist,
					 struct list_head *list)
{
	struct rds_ib_mr *ibmr;
	struct llist_node *node;
	struct llist_node *next;
	unsigned int count = 0;

	node = llist_del_all(llist);
	while (node) {
		next = node->next;
		ibmr = llist_entry(node, struct rds_ib_mr, llnode);
		list_add_tail(&ibmr->unmap_list, list);
		node = next;
		count++;
	}
	return count;
}

/*
 * this takes a list head of mrs and turns it into linked llist nodes
 * of clusters.  Each cluster has linked llist nodes of
 * MR_CLUSTER_SIZE mrs that are ready for reuse.
 */
static void list_to_llist_nodes(struct list_head *list,
				struct llist_node **nodes_head,
				struct llist_node **nodes_tail)
{
	struct rds_ib_mr *ibmr;
	struct llist_node *cur = NULL;
	struct llist_node **next = nodes_head;

	list_for_each_entry(ibmr, list, unmap_list) {
		cur = &ibmr->llnode;
		*next = cur;
		next = &cur->next;
	}
	*next = NULL;
	*nodes_tail = cur;
}

/*
 * Flush our pool of MRs.
 * At a minimum, all currently unused MRs are unmapped.
 * If the number of MRs allocated exceeds the limit, we also try
 * to free as many MRs as needed to get back to this limit.
 */
int rds_ib_flush_mr_pool(struct rds_ib_mr_pool *pool,
			 int free_all, struct rds_ib_mr **ibmr_ret)
{
	struct rds_ib_mr *ibmr;
	struct llist_node *clean_nodes;
	struct llist_node *clean_tail;
	LIST_HEAD(unmap_list);
	unsigned long unpinned = 0;
	unsigned int nfreed = 0, dirty_to_clean = 0, free_goal;

	if (pool->pool_type == RDS_IB_MR_8K_POOL)
		rds_ib_stats_inc(s_ib_rdma_mr_8k_pool_flush);
	else
		rds_ib_stats_inc(s_ib_rdma_mr_1m_pool_flush);

	if (ibmr_ret) {
		DEFINE_WAIT(wait);
		while (!mutex_trylock(&pool->flush_lock)) {
			ibmr = rds_ib_reuse_mr(pool);
			if (ibmr) {
				*ibmr_ret = ibmr;
				finish_wait(&pool->flush_wait, &wait);
				goto out_nolock;
			}

			prepare_to_wait(&pool->flush_wait, &wait,
					TASK_UNINTERRUPTIBLE);
			if (llist_empty(&pool->clean_list))
				schedule();

			ibmr = rds_ib_reuse_mr(pool);
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
		ibmr = rds_ib_reuse_mr(pool);
		if (ibmr) {
			*ibmr_ret = ibmr;
			goto out;
		}
	}

	/* Get the list of all MRs to be dropped. Ordering matters -
	 * we want to put drop_list ahead of free_list.
	 */
	dirty_to_clean = llist_append_to_list(&pool->drop_list, &unmap_list);
	dirty_to_clean += llist_append_to_list(&pool->free_list, &unmap_list);
	if (free_all) {
		unsigned long flags;

		spin_lock_irqsave(&pool->clean_lock, flags);
		llist_append_to_list(&pool->clean_list, &unmap_list);
		spin_unlock_irqrestore(&pool->clean_lock, flags);
	}

	free_goal = rds_ib_flush_goal(pool, free_all);

	if (list_empty(&unmap_list))
		goto out;

	if (pool->use_fastreg)
		rds_ib_unreg_frmr(&unmap_list, &nfreed, &unpinned, free_goal);
	else
		rds_ib_unreg_fmr(&unmap_list, &nfreed, &unpinned, free_goal);

	if (!list_empty(&unmap_list)) {
		unsigned long flags;

		list_to_llist_nodes(&unmap_list, &clean_nodes, &clean_tail);
		if (ibmr_ret) {
			*ibmr_ret = llist_entry(clean_nodes, struct rds_ib_mr, llnode);
			clean_nodes = clean_nodes->next;
		}
		/* more than one entry in llist nodes */
		if (clean_nodes) {
			spin_lock_irqsave(&pool->clean_lock, flags);
			llist_add_batch(clean_nodes, clean_tail,
					&pool->clean_list);
			spin_unlock_irqrestore(&pool->clean_lock, flags);
		}
	}

	atomic_sub(unpinned, &pool->free_pinned);
	atomic_sub(dirty_to_clean, &pool->dirty_count);
	atomic_sub(nfreed, &pool->item_count);

out:
	mutex_unlock(&pool->flush_lock);
	if (waitqueue_active(&pool->flush_wait))
		wake_up(&pool->flush_wait);
out_nolock:
	return 0;
}

struct rds_ib_mr *rds_ib_try_reuse_ibmr(struct rds_ib_mr_pool *pool)
{
	struct rds_ib_mr *ibmr = NULL;
	int iter = 0;

	while (1) {
		ibmr = rds_ib_reuse_mr(pool);
		if (ibmr)
			return ibmr;

		if (atomic_inc_return(&pool->item_count) <= pool->max_items)
			break;

		atomic_dec(&pool->item_count);

		if (++iter > 2) {
			if (pool->pool_type == RDS_IB_MR_8K_POOL)
				rds_ib_stats_inc(s_ib_rdma_mr_8k_pool_depleted);
			else
				rds_ib_stats_inc(s_ib_rdma_mr_1m_pool_depleted);
			break;
		}

		/* We do have some empty MRs. Flush them out. */
		if (pool->pool_type == RDS_IB_MR_8K_POOL)
			rds_ib_stats_inc(s_ib_rdma_mr_8k_pool_wait);
		else
			rds_ib_stats_inc(s_ib_rdma_mr_1m_pool_wait);

		rds_ib_flush_mr_pool(pool, 0, &ibmr);
		if (ibmr)
			return ibmr;
	}

	return NULL;
}

static void rds_ib_mr_pool_flush_worker(struct work_struct *work)
{
	struct rds_ib_mr_pool *pool = container_of(work, struct rds_ib_mr_pool, flush_worker.work);

	rds_ib_flush_mr_pool(pool, 0, NULL);
}

void rds_ib_free_mr(void *trans_private, int invalidate)
{
	struct rds_ib_mr *ibmr = trans_private;
	struct rds_ib_mr_pool *pool = ibmr->pool;
	struct rds_ib_device *rds_ibdev = ibmr->device;

	rdsdebug("RDS/IB: free_mr nents %u\n", ibmr->sg_len);

	if (ibmr->odp) {
		/* A MR created and marked as use_once. We use delayed work,
		 * because there is a change that we are in interrupt and can't
		 * call to ib_dereg_mr() directly.
		 */
		INIT_DELAYED_WORK(&ibmr->work, rds_ib_odp_mr_worker);
		queue_delayed_work(rds_ib_mr_wq, &ibmr->work, 0);
		return;
	}

	/* Return it to the pool's free list */
	if (rds_ibdev->use_fastreg)
		rds_ib_free_frmr_list(ibmr);
	else
		rds_ib_free_fmr_list(ibmr);

	atomic_add(ibmr->sg_len, &pool->free_pinned);
	atomic_inc(&pool->dirty_count);

	/* If we've pinned too many pages, request a flush */
	if (atomic_read(&pool->free_pinned) >= pool->max_free_pinned ||
	    atomic_read(&pool->dirty_count) >= pool->max_items / 5)
		queue_delayed_work(rds_ib_mr_wq, &pool->flush_worker, 10);

	if (invalidate) {
		if (likely(!in_interrupt())) {
			rds_ib_flush_mr_pool(pool, 0, NULL);
		} else {
			/* We get here if the user created a MR marked
			 * as use_once and invalidate at the same time.
			 */
			queue_delayed_work(rds_ib_mr_wq,
					   &pool->flush_worker, 10);
		}
	}

	rds_ib_dev_put(rds_ibdev);
}

void rds_ib_flush_mrs(void)
{
	struct rds_ib_device *rds_ibdev;

	down_read(&rds_ib_devices_lock);
	list_for_each_entry(rds_ibdev, &rds_ib_devices, list) {
		if (rds_ibdev->mr_8k_pool)
			rds_ib_flush_mr_pool(rds_ibdev->mr_8k_pool, 0, NULL);

		if (rds_ibdev->mr_1m_pool)
			rds_ib_flush_mr_pool(rds_ibdev->mr_1m_pool, 0, NULL);
	}
	up_read(&rds_ib_devices_lock);
}

u32 rds_ib_get_lkey(void *trans_private)
{
	struct rds_ib_mr *ibmr = trans_private;

	return ibmr->u.mr->lkey;
}

void *rds_ib_get_mr(struct scatterlist *sg, unsigned long nents,
		    struct rds_sock *rs, u32 *key_ret,
		    struct rds_connection *conn,
		    u64 start, u64 length, int need_odp)
{
	struct rds_ib_device *rds_ibdev;
	struct rds_ib_mr *ibmr = NULL;
	struct rds_ib_connection *ic = NULL;
	int ret;

	rds_ibdev = rds_ib_get_device(rs->rs_bound_addr.s6_addr32[3]);
	if (!rds_ibdev) {
		ret = -ENODEV;
		goto out;
	}

	if (need_odp == ODP_ZEROBASED || need_odp == ODP_VIRTUAL) {
		u64 virt_addr = need_odp == ODP_ZEROBASED ? 0 : start;
		int access_flags =
			(IB_ACCESS_LOCAL_WRITE | IB_ACCESS_REMOTE_READ |
			 IB_ACCESS_REMOTE_WRITE | IB_ACCESS_REMOTE_ATOMIC |
			 IB_ACCESS_ON_DEMAND);
		struct ib_sge sge = {};
		struct ib_mr *ib_mr;

		if (!rds_ibdev->odp_capable) {
			ret = -EOPNOTSUPP;
			goto out;
		}

		ib_mr = ib_reg_user_mr(rds_ibdev->pd, start, length, virt_addr,
				       access_flags);

		if (IS_ERR(ib_mr)) {
			rdsdebug("rds_ib_get_user_mr returned %d\n",
				 IS_ERR(ib_mr));
			ret = PTR_ERR(ib_mr);
			goto out;
		}
		if (key_ret)
			*key_ret = ib_mr->rkey;

		ibmr = kzalloc(sizeof(*ibmr), GFP_KERNEL);
		if (!ibmr) {
			ib_dereg_mr(ib_mr);
			ret = -ENOMEM;
			goto out;
		}
		ibmr->u.mr = ib_mr;
		ibmr->odp = 1;

		sge.addr = virt_addr;
		sge.length = length;
		sge.lkey = ib_mr->lkey;

		ib_advise_mr(rds_ibdev->pd,
			     IB_UVERBS_ADVISE_MR_ADVICE_PREFETCH_WRITE,
			     IB_UVERBS_ADVISE_MR_FLAG_FLUSH, &sge, 1);
		return ibmr;
	}

	if (conn)
		ic = conn->c_transport_data;

	if (!rds_ibdev->mr_8k_pool || !rds_ibdev->mr_1m_pool) {
		ret = -ENODEV;
		goto out;
	}

	if (rds_ibdev->use_fastreg)
		ibmr = rds_ib_reg_frmr(rds_ibdev, ic, sg, nents, key_ret);
	else
		ibmr = rds_ib_reg_fmr(rds_ibdev, sg, nents, key_ret);
	if (IS_ERR(ibmr)) {
		ret = PTR_ERR(ibmr);
		pr_warn("RDS/IB: rds_ib_get_mr failed (errno=%d)\n", ret);
	} else {
		return ibmr;
	}

 out:
	if (rds_ibdev)
		rds_ib_dev_put(rds_ibdev);

	return ERR_PTR(ret);
}

void rds_ib_destroy_mr_pool(struct rds_ib_mr_pool *pool)
{
	cancel_delayed_work_sync(&pool->flush_worker);
	rds_ib_flush_mr_pool(pool, 1, NULL);
	WARN_ON(atomic_read(&pool->item_count));
	WARN_ON(atomic_read(&pool->free_pinned));
	kfree(pool);
}

struct rds_ib_mr_pool *rds_ib_create_mr_pool(struct rds_ib_device *rds_ibdev,
					     int pool_type)
{
	struct rds_ib_mr_pool *pool;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool)
		return ERR_PTR(-ENOMEM);

	pool->pool_type = pool_type;
	init_llist_head(&pool->free_list);
	init_llist_head(&pool->drop_list);
	init_llist_head(&pool->clean_list);
	spin_lock_init(&pool->clean_lock);
	mutex_init(&pool->flush_lock);
	init_waitqueue_head(&pool->flush_wait);
	INIT_DELAYED_WORK(&pool->flush_worker, rds_ib_mr_pool_flush_worker);

	if (pool_type == RDS_IB_MR_1M_POOL) {
		/* +1 allows for unaligned MRs */
		pool->fmr_attr.max_pages = RDS_MR_1M_MSG_SIZE + 1;
		pool->max_items = rds_ibdev->max_1m_mrs;
	} else {
		/* pool_type == RDS_IB_MR_8K_POOL */
		pool->fmr_attr.max_pages = RDS_MR_8K_MSG_SIZE + 1;
		pool->max_items = rds_ibdev->max_8k_mrs;
	}

	pool->max_free_pinned = pool->max_items * pool->fmr_attr.max_pages / 4;
	pool->fmr_attr.max_maps = rds_ibdev->fmr_max_remaps;
	pool->fmr_attr.page_shift = PAGE_SHIFT;
	pool->max_items_soft = rds_ibdev->max_mrs * 3 / 4;
	pool->use_fastreg = rds_ibdev->use_fastreg;

	return pool;
}

int rds_ib_mr_init(void)
{
	rds_ib_mr_wq = alloc_workqueue("rds_mr_flushd", WQ_MEM_RECLAIM, 0);
	if (!rds_ib_mr_wq)
		return -ENOMEM;
	return 0;
}

/* By the time this is called all the IB devices should have been torn down and
 * had their pools freed.  As each pool is freed its work struct is waited on,
 * so the pool flushing work queue should be idle by the time we get here.
 */
void rds_ib_mr_exit(void)
{
	destroy_workqueue(rds_ib_mr_wq);
}

static void rds_ib_odp_mr_worker(struct work_struct  *work)
{
	struct rds_ib_mr *ibmr;

	ibmr = container_of(work, struct rds_ib_mr, work.work);
	ib_dereg_mr(ibmr->u.mr);
	kfree(ibmr);
}
