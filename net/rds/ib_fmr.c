/*
 * Copyright (c) 2016 Oracle.  All rights reserved.
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
 */

#include "ib_mr.h"

struct rds_ib_mr *rds_ib_alloc_fmr(struct rds_ib_device *rds_ibdev, int npages)
{
	struct rds_ib_mr_pool *pool;
	struct rds_ib_mr *ibmr = NULL;
	struct rds_ib_fmr *fmr;
	int err = 0, iter = 0;

	if (npages <= RDS_MR_8K_MSG_SIZE)
		pool = rds_ibdev->mr_8k_pool;
	else
		pool = rds_ibdev->mr_1m_pool;

	if (atomic_read(&pool->dirty_count) >= pool->max_items / 10)
		queue_delayed_work(rds_ib_mr_wq, &pool->flush_worker, 10);

	/* Switch pools if one of the pool is reaching upper limit */
	if (atomic_read(&pool->dirty_count) >=  pool->max_items * 9 / 10) {
		if (pool->pool_type == RDS_IB_MR_8K_POOL)
			pool = rds_ibdev->mr_1m_pool;
		else
			pool = rds_ibdev->mr_8k_pool;
	}

	while (1) {
		ibmr = rds_ib_reuse_mr(pool);
		if (ibmr)
			return ibmr;

		/* No clean MRs - now we have the choice of either
		 * allocating a fresh MR up to the limit imposed by the
		 * driver, or flush any dirty unused MRs.
		 * We try to avoid stalling in the send path if possible,
		 * so we allocate as long as we're allowed to.
		 *
		 * We're fussy with enforcing the FMR limit, though. If the
		 * driver tells us we can't use more than N fmrs, we shouldn't
		 * start arguing with it
		 */
		if (atomic_inc_return(&pool->item_count) <= pool->max_items)
			break;

		atomic_dec(&pool->item_count);

		if (++iter > 2) {
			if (pool->pool_type == RDS_IB_MR_8K_POOL)
				rds_ib_stats_inc(s_ib_rdma_mr_8k_pool_depleted);
			else
				rds_ib_stats_inc(s_ib_rdma_mr_1m_pool_depleted);
			return ERR_PTR(-EAGAIN);
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

	ibmr = kzalloc_node(sizeof(*ibmr), GFP_KERNEL,
			    rdsibdev_to_node(rds_ibdev));
	if (!ibmr) {
		err = -ENOMEM;
		goto out_no_cigar;
	}

	fmr = &ibmr->u.fmr;
	fmr->fmr = ib_alloc_fmr(rds_ibdev->pd,
			(IB_ACCESS_LOCAL_WRITE |
			 IB_ACCESS_REMOTE_READ |
			 IB_ACCESS_REMOTE_WRITE |
			 IB_ACCESS_REMOTE_ATOMIC),
			&pool->fmr_attr);
	if (IS_ERR(fmr->fmr)) {
		err = PTR_ERR(fmr->fmr);
		fmr->fmr = NULL;
		pr_warn("RDS/IB: %s failed (err=%d)\n", __func__, err);
		goto out_no_cigar;
	}

	ibmr->pool = pool;
	if (pool->pool_type == RDS_IB_MR_8K_POOL)
		rds_ib_stats_inc(s_ib_rdma_mr_8k_alloc);
	else
		rds_ib_stats_inc(s_ib_rdma_mr_1m_alloc);

	return ibmr;

out_no_cigar:
	if (ibmr) {
		if (fmr->fmr)
			ib_dealloc_fmr(fmr->fmr);
		kfree(ibmr);
	}
	atomic_dec(&pool->item_count);
	return ERR_PTR(err);
}

int rds_ib_map_fmr(struct rds_ib_device *rds_ibdev, struct rds_ib_mr *ibmr,
		   struct scatterlist *sg, unsigned int nents)
{
	struct ib_device *dev = rds_ibdev->dev;
	struct rds_ib_fmr *fmr = &ibmr->u.fmr;
	struct scatterlist *scat = sg;
	u64 io_addr = 0;
	u64 *dma_pages;
	u32 len;
	int page_cnt, sg_dma_len;
	int i, j;
	int ret;

	sg_dma_len = ib_dma_map_sg(dev, sg, nents, DMA_BIDIRECTIONAL);
	if (unlikely(!sg_dma_len)) {
		pr_warn("RDS/IB: %s failed!\n", __func__);
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
	if (page_cnt > ibmr->pool->fmr_attr.max_pages)
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

	ret = ib_map_phys_fmr(fmr->fmr, dma_pages, page_cnt, io_addr);
	if (ret)
		goto out;

	/* Success - we successfully remapped the MR, so we can
	 * safely tear down the old mapping.
	 */
	rds_ib_teardown_mr(ibmr);

	ibmr->sg = scat;
	ibmr->sg_len = nents;
	ibmr->sg_dma_len = sg_dma_len;
	ibmr->remap_count++;

	if (ibmr->pool->pool_type == RDS_IB_MR_8K_POOL)
		rds_ib_stats_inc(s_ib_rdma_mr_8k_used);
	else
		rds_ib_stats_inc(s_ib_rdma_mr_1m_used);
	ret = 0;

out:
	kfree(dma_pages);

	return ret;
}
