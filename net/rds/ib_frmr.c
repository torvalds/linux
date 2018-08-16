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

static struct rds_ib_mr *rds_ib_alloc_frmr(struct rds_ib_device *rds_ibdev,
					   int npages)
{
	struct rds_ib_mr_pool *pool;
	struct rds_ib_mr *ibmr = NULL;
	struct rds_ib_frmr *frmr;
	int err = 0;

	if (npages <= RDS_MR_8K_MSG_SIZE)
		pool = rds_ibdev->mr_8k_pool;
	else
		pool = rds_ibdev->mr_1m_pool;

	ibmr = rds_ib_try_reuse_ibmr(pool);
	if (ibmr)
		return ibmr;

	ibmr = kzalloc_node(sizeof(*ibmr), GFP_KERNEL,
			    rdsibdev_to_node(rds_ibdev));
	if (!ibmr) {
		err = -ENOMEM;
		goto out_no_cigar;
	}

	frmr = &ibmr->u.frmr;
	frmr->mr = ib_alloc_mr(rds_ibdev->pd, IB_MR_TYPE_MEM_REG,
			 pool->fmr_attr.max_pages);
	if (IS_ERR(frmr->mr)) {
		pr_warn("RDS/IB: %s failed to allocate MR", __func__);
		err = PTR_ERR(frmr->mr);
		goto out_no_cigar;
	}

	ibmr->pool = pool;
	if (pool->pool_type == RDS_IB_MR_8K_POOL)
		rds_ib_stats_inc(s_ib_rdma_mr_8k_alloc);
	else
		rds_ib_stats_inc(s_ib_rdma_mr_1m_alloc);

	if (atomic_read(&pool->item_count) > pool->max_items_soft)
		pool->max_items_soft = pool->max_items;

	frmr->fr_state = FRMR_IS_FREE;
	return ibmr;

out_no_cigar:
	kfree(ibmr);
	atomic_dec(&pool->item_count);
	return ERR_PTR(err);
}

static void rds_ib_free_frmr(struct rds_ib_mr *ibmr, bool drop)
{
	struct rds_ib_mr_pool *pool = ibmr->pool;

	if (drop)
		llist_add(&ibmr->llnode, &pool->drop_list);
	else
		llist_add(&ibmr->llnode, &pool->free_list);
	atomic_add(ibmr->sg_len, &pool->free_pinned);
	atomic_inc(&pool->dirty_count);

	/* If we've pinned too many pages, request a flush */
	if (atomic_read(&pool->free_pinned) >= pool->max_free_pinned ||
	    atomic_read(&pool->dirty_count) >= pool->max_items / 5)
		queue_delayed_work(rds_ib_mr_wq, &pool->flush_worker, 10);
}

static int rds_ib_post_reg_frmr(struct rds_ib_mr *ibmr)
{
	struct rds_ib_frmr *frmr = &ibmr->u.frmr;
	struct ib_reg_wr reg_wr;
	int ret, off = 0;

	while (atomic_dec_return(&ibmr->ic->i_fastreg_wrs) <= 0) {
		atomic_inc(&ibmr->ic->i_fastreg_wrs);
		cpu_relax();
	}

	ret = ib_map_mr_sg_zbva(frmr->mr, ibmr->sg, ibmr->sg_len,
				&off, PAGE_SIZE);
	if (unlikely(ret != ibmr->sg_len))
		return ret < 0 ? ret : -EINVAL;

	/* Perform a WR for the fast_reg_mr. Each individual page
	 * in the sg list is added to the fast reg page list and placed
	 * inside the fast_reg_mr WR.  The key used is a rolling 8bit
	 * counter, which should guarantee uniqueness.
	 */
	ib_update_fast_reg_key(frmr->mr, ibmr->remap_count++);
	frmr->fr_state = FRMR_IS_INUSE;

	memset(&reg_wr, 0, sizeof(reg_wr));
	reg_wr.wr.wr_id = (unsigned long)(void *)ibmr;
	reg_wr.wr.opcode = IB_WR_REG_MR;
	reg_wr.wr.num_sge = 0;
	reg_wr.mr = frmr->mr;
	reg_wr.key = frmr->mr->rkey;
	reg_wr.access = IB_ACCESS_LOCAL_WRITE |
			IB_ACCESS_REMOTE_READ |
			IB_ACCESS_REMOTE_WRITE;
	reg_wr.wr.send_flags = IB_SEND_SIGNALED;

	ret = ib_post_send(ibmr->ic->i_cm_id->qp, &reg_wr.wr, NULL);
	if (unlikely(ret)) {
		/* Failure here can be because of -ENOMEM as well */
		frmr->fr_state = FRMR_IS_STALE;
		atomic_inc(&ibmr->ic->i_fastreg_wrs);
		if (printk_ratelimit())
			pr_warn("RDS/IB: %s returned error(%d)\n",
				__func__, ret);
	}
	return ret;
}

static int rds_ib_map_frmr(struct rds_ib_device *rds_ibdev,
			   struct rds_ib_mr_pool *pool,
			   struct rds_ib_mr *ibmr,
			   struct scatterlist *sg, unsigned int sg_len)
{
	struct ib_device *dev = rds_ibdev->dev;
	struct rds_ib_frmr *frmr = &ibmr->u.frmr;
	int i;
	u32 len;
	int ret = 0;

	/* We want to teardown old ibmr values here and fill it up with
	 * new sg values
	 */
	rds_ib_teardown_mr(ibmr);

	ibmr->sg = sg;
	ibmr->sg_len = sg_len;
	ibmr->sg_dma_len = 0;
	frmr->sg_byte_len = 0;
	WARN_ON(ibmr->sg_dma_len);
	ibmr->sg_dma_len = ib_dma_map_sg(dev, ibmr->sg, ibmr->sg_len,
					 DMA_BIDIRECTIONAL);
	if (unlikely(!ibmr->sg_dma_len)) {
		pr_warn("RDS/IB: %s failed!\n", __func__);
		return -EBUSY;
	}

	frmr->sg_byte_len = 0;
	frmr->dma_npages = 0;
	len = 0;

	ret = -EINVAL;
	for (i = 0; i < ibmr->sg_dma_len; ++i) {
		unsigned int dma_len = ib_sg_dma_len(dev, &ibmr->sg[i]);
		u64 dma_addr = ib_sg_dma_address(dev, &ibmr->sg[i]);

		frmr->sg_byte_len += dma_len;
		if (dma_addr & ~PAGE_MASK) {
			if (i > 0)
				goto out_unmap;
			else
				++frmr->dma_npages;
		}

		if ((dma_addr + dma_len) & ~PAGE_MASK) {
			if (i < ibmr->sg_dma_len - 1)
				goto out_unmap;
			else
				++frmr->dma_npages;
		}

		len += dma_len;
	}
	frmr->dma_npages += len >> PAGE_SHIFT;

	if (frmr->dma_npages > ibmr->pool->fmr_attr.max_pages) {
		ret = -EMSGSIZE;
		goto out_unmap;
	}

	ret = rds_ib_post_reg_frmr(ibmr);
	if (ret)
		goto out_unmap;

	if (ibmr->pool->pool_type == RDS_IB_MR_8K_POOL)
		rds_ib_stats_inc(s_ib_rdma_mr_8k_used);
	else
		rds_ib_stats_inc(s_ib_rdma_mr_1m_used);

	return ret;

out_unmap:
	ib_dma_unmap_sg(rds_ibdev->dev, ibmr->sg, ibmr->sg_len,
			DMA_BIDIRECTIONAL);
	ibmr->sg_dma_len = 0;
	return ret;
}

static int rds_ib_post_inv(struct rds_ib_mr *ibmr)
{
	struct ib_send_wr *s_wr;
	struct rds_ib_frmr *frmr = &ibmr->u.frmr;
	struct rdma_cm_id *i_cm_id = ibmr->ic->i_cm_id;
	int ret = -EINVAL;

	if (!i_cm_id || !i_cm_id->qp || !frmr->mr)
		goto out;

	if (frmr->fr_state != FRMR_IS_INUSE)
		goto out;

	while (atomic_dec_return(&ibmr->ic->i_fastunreg_wrs) <= 0) {
		atomic_inc(&ibmr->ic->i_fastunreg_wrs);
		cpu_relax();
	}

	frmr->fr_inv = true;
	s_wr = &frmr->fr_wr;

	memset(s_wr, 0, sizeof(*s_wr));
	s_wr->wr_id = (unsigned long)(void *)ibmr;
	s_wr->opcode = IB_WR_LOCAL_INV;
	s_wr->ex.invalidate_rkey = frmr->mr->rkey;
	s_wr->send_flags = IB_SEND_SIGNALED;

	ret = ib_post_send(i_cm_id->qp, s_wr, NULL);
	if (unlikely(ret)) {
		frmr->fr_state = FRMR_IS_STALE;
		frmr->fr_inv = false;
		atomic_inc(&ibmr->ic->i_fastunreg_wrs);
		pr_err("RDS/IB: %s returned error(%d)\n", __func__, ret);
		goto out;
	}
out:
	return ret;
}

void rds_ib_mr_cqe_handler(struct rds_ib_connection *ic, struct ib_wc *wc)
{
	struct rds_ib_mr *ibmr = (void *)(unsigned long)wc->wr_id;
	struct rds_ib_frmr *frmr = &ibmr->u.frmr;

	if (wc->status != IB_WC_SUCCESS) {
		frmr->fr_state = FRMR_IS_STALE;
		if (rds_conn_up(ic->conn))
			rds_ib_conn_error(ic->conn,
					  "frmr completion <%pI4,%pI4> status %u(%s), vendor_err 0x%x, disconnecting and reconnecting\n",
					  &ic->conn->c_laddr,
					  &ic->conn->c_faddr,
					  wc->status,
					  ib_wc_status_msg(wc->status),
					  wc->vendor_err);
	}

	if (frmr->fr_inv) {
		frmr->fr_state = FRMR_IS_FREE;
		frmr->fr_inv = false;
		atomic_inc(&ic->i_fastreg_wrs);
	} else {
		atomic_inc(&ic->i_fastunreg_wrs);
	}
}

void rds_ib_unreg_frmr(struct list_head *list, unsigned int *nfreed,
		       unsigned long *unpinned, unsigned int goal)
{
	struct rds_ib_mr *ibmr, *next;
	struct rds_ib_frmr *frmr;
	int ret = 0;
	unsigned int freed = *nfreed;

	/* String all ib_mr's onto one list and hand them to ib_unmap_fmr */
	list_for_each_entry(ibmr, list, unmap_list) {
		if (ibmr->sg_dma_len)
			ret |= rds_ib_post_inv(ibmr);
	}
	if (ret)
		pr_warn("RDS/IB: %s failed (err=%d)\n", __func__, ret);

	/* Now we can destroy the DMA mapping and unpin any pages */
	list_for_each_entry_safe(ibmr, next, list, unmap_list) {
		*unpinned += ibmr->sg_len;
		frmr = &ibmr->u.frmr;
		__rds_ib_teardown_mr(ibmr);
		if (freed < goal || frmr->fr_state == FRMR_IS_STALE) {
			/* Don't de-allocate if the MR is not free yet */
			if (frmr->fr_state == FRMR_IS_INUSE)
				continue;

			if (ibmr->pool->pool_type == RDS_IB_MR_8K_POOL)
				rds_ib_stats_inc(s_ib_rdma_mr_8k_free);
			else
				rds_ib_stats_inc(s_ib_rdma_mr_1m_free);
			list_del(&ibmr->unmap_list);
			if (frmr->mr)
				ib_dereg_mr(frmr->mr);
			kfree(ibmr);
			freed++;
		}
	}
	*nfreed = freed;
}

struct rds_ib_mr *rds_ib_reg_frmr(struct rds_ib_device *rds_ibdev,
				  struct rds_ib_connection *ic,
				  struct scatterlist *sg,
				  unsigned long nents, u32 *key)
{
	struct rds_ib_mr *ibmr = NULL;
	struct rds_ib_frmr *frmr;
	int ret;

	if (!ic) {
		/* TODO: Add FRWR support for RDS_GET_MR using proxy qp*/
		return ERR_PTR(-EOPNOTSUPP);
	}

	do {
		if (ibmr)
			rds_ib_free_frmr(ibmr, true);
		ibmr = rds_ib_alloc_frmr(rds_ibdev, nents);
		if (IS_ERR(ibmr))
			return ibmr;
		frmr = &ibmr->u.frmr;
	} while (frmr->fr_state != FRMR_IS_FREE);

	ibmr->ic = ic;
	ibmr->device = rds_ibdev;
	ret = rds_ib_map_frmr(rds_ibdev, ibmr->pool, ibmr, sg, nents);
	if (ret == 0) {
		*key = frmr->mr->rkey;
	} else {
		rds_ib_free_frmr(ibmr, false);
		ibmr = ERR_PTR(ret);
	}

	return ibmr;
}

void rds_ib_free_frmr_list(struct rds_ib_mr *ibmr)
{
	struct rds_ib_mr_pool *pool = ibmr->pool;
	struct rds_ib_frmr *frmr = &ibmr->u.frmr;

	if (frmr->fr_state == FRMR_IS_STALE)
		llist_add(&ibmr->llnode, &pool->drop_list);
	else
		llist_add(&ibmr->llnode, &pool->free_list);
}
