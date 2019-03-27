/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009-2013 Chelsio, Inc. All rights reserved.
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
/* Crude resource management */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"

#ifdef TCP_OFFLOAD
#include <linux/spinlock.h>
#include "iw_cxgbe.h"

static int c4iw_init_qid_table(struct c4iw_rdev *rdev)
{
	u32 i;

	if (c4iw_id_table_alloc(&rdev->resource.qid_table,
				rdev->adap->vres.qp.start,
				rdev->adap->vres.qp.size,
				rdev->adap->vres.qp.size, 0)) {
		printf("%s: return ENOMEM\n", __func__);
		return -ENOMEM;
	}

	for (i = rdev->adap->vres.qp.start;
		i < rdev->adap->vres.qp.start + rdev->adap->vres.qp.size; i++)
		if (!(i & rdev->qpmask))
			c4iw_id_free(&rdev->resource.qid_table, i);
	return 0;
}

/* nr_* must be power of 2 */
int c4iw_init_resource(struct c4iw_rdev *rdev, u32 nr_tpt, u32 nr_pdid)
{
	int err = 0;
	err = c4iw_id_table_alloc(&rdev->resource.tpt_table, 0, nr_tpt, 1,
					C4IW_ID_TABLE_F_RANDOM);
	if (err)
		goto tpt_err;
	err = c4iw_init_qid_table(rdev);
	if (err)
		goto qid_err;
	err = c4iw_id_table_alloc(&rdev->resource.pdid_table, 0,
					nr_pdid, 1, 0);
	if (err)
		goto pdid_err;
	return 0;
 pdid_err:
	c4iw_id_table_free(&rdev->resource.qid_table);
 qid_err:
	c4iw_id_table_free(&rdev->resource.tpt_table);
 tpt_err:
	return -ENOMEM;
}

/*
 * returns 0 if no resource available
 */
u32 c4iw_get_resource(struct c4iw_id_table *id_table)
{
	u32 entry;
	entry = c4iw_id_alloc(id_table);
	if (entry == (u32)(-1)) {
		return 0;
	}
	return entry;
}

void c4iw_put_resource(struct c4iw_id_table *id_table, u32 entry)
{
	CTR2(KTR_IW_CXGBE, "%s entry 0x%x", __func__, entry);
	c4iw_id_free(id_table, entry);
}

u32 c4iw_get_cqid(struct c4iw_rdev *rdev, struct c4iw_dev_ucontext *uctx)
{
	struct c4iw_qid_list *entry;
	u32 qid;
	int i;

	mutex_lock(&uctx->lock);
	if (!list_empty(&uctx->cqids)) {
		entry = list_entry(uctx->cqids.next, struct c4iw_qid_list,
				   entry);
		list_del(&entry->entry);
		qid = entry->qid;
		kfree(entry);
	} else {
		qid = c4iw_get_resource(&rdev->resource.qid_table);
		if (!qid)
			goto out;
		mutex_lock(&rdev->stats.lock);
		rdev->stats.qid.cur += rdev->qpmask + 1;
		mutex_unlock(&rdev->stats.lock);
		for (i = qid+1; i & rdev->qpmask; i++) {
			entry = kmalloc(sizeof *entry, GFP_KERNEL);
			if (!entry)
				goto out;
			entry->qid = i;
			list_add_tail(&entry->entry, &uctx->cqids);
		}

		/*
		 * now put the same ids on the qp list since they all
		 * map to the same db/gts page.
		 */
		entry = kmalloc(sizeof *entry, GFP_KERNEL);
		if (!entry)
			goto out;
		entry->qid = qid;
		list_add_tail(&entry->entry, &uctx->qpids);
		for (i = qid+1; i & rdev->qpmask; i++) {
			entry = kmalloc(sizeof *entry, GFP_KERNEL);
			if (!entry)
				goto out;
			entry->qid = i;
			list_add_tail(&entry->entry, &uctx->qpids);
		}
	}
out:
	mutex_unlock(&uctx->lock);
	CTR2(KTR_IW_CXGBE, "%s: qid 0x%x", __func__, qid);
	mutex_lock(&rdev->stats.lock);
	if (rdev->stats.qid.cur > rdev->stats.qid.max)
		rdev->stats.qid.max = rdev->stats.qid.cur;
	mutex_unlock(&rdev->stats.lock);
	return qid;
}

void c4iw_put_cqid(struct c4iw_rdev *rdev, u32 qid,
		   struct c4iw_dev_ucontext *uctx)
{
	struct c4iw_qid_list *entry;

	entry = kmalloc(sizeof *entry, GFP_KERNEL);
	if (!entry)
		return;
	CTR2(KTR_IW_CXGBE, "%s qid 0x%x", __func__, qid);
	entry->qid = qid;
	mutex_lock(&uctx->lock);
	list_add_tail(&entry->entry, &uctx->cqids);
	mutex_unlock(&uctx->lock);
}

u32 c4iw_get_qpid(struct c4iw_rdev *rdev, struct c4iw_dev_ucontext *uctx)
{
	struct c4iw_qid_list *entry;
	u32 qid;
	int i;

	mutex_lock(&uctx->lock);
	if (!list_empty(&uctx->qpids)) {
		entry = list_entry(uctx->qpids.next, struct c4iw_qid_list,
				   entry);
		list_del(&entry->entry);
		qid = entry->qid;
		kfree(entry);
	} else {
		qid = c4iw_get_resource(&rdev->resource.qid_table);
		if (!qid)
			goto out;
		mutex_lock(&rdev->stats.lock);
		rdev->stats.qid.cur += rdev->qpmask + 1;
		mutex_unlock(&rdev->stats.lock);
		for (i = qid+1; i & rdev->qpmask; i++) {
			entry = kmalloc(sizeof *entry, GFP_KERNEL);
			if (!entry)
				goto out;
			entry->qid = i;
			list_add_tail(&entry->entry, &uctx->qpids);
		}

		/*
		 * now put the same ids on the cq list since they all
		 * map to the same db/gts page.
		 */
		entry = kmalloc(sizeof *entry, GFP_KERNEL);
		if (!entry)
			goto out;
		entry->qid = qid;
		list_add_tail(&entry->entry, &uctx->cqids);
		for (i = qid; i & rdev->qpmask; i++) {
			entry = kmalloc(sizeof *entry, GFP_KERNEL);
			if (!entry)
				goto out;
			entry->qid = i;
			list_add_tail(&entry->entry, &uctx->cqids);
		}
	}
out:
	mutex_unlock(&uctx->lock);
	CTR2(KTR_IW_CXGBE, "%s qid 0x%x", __func__, qid);
	mutex_lock(&rdev->stats.lock);
	if (rdev->stats.qid.cur > rdev->stats.qid.max)
		rdev->stats.qid.max = rdev->stats.qid.cur;
	mutex_unlock(&rdev->stats.lock);
	return qid;
}

void c4iw_put_qpid(struct c4iw_rdev *rdev, u32 qid,
		   struct c4iw_dev_ucontext *uctx)
{
	struct c4iw_qid_list *entry;

	entry = kmalloc(sizeof *entry, GFP_KERNEL);
	if (!entry)
		return;
	CTR2(KTR_IW_CXGBE, "%s qid 0x%x", __func__, qid);
	entry->qid = qid;
	mutex_lock(&uctx->lock);
	list_add_tail(&entry->entry, &uctx->qpids);
	mutex_unlock(&uctx->lock);
}

void c4iw_destroy_resource(struct c4iw_resource *rscp)
{
	c4iw_id_table_free(&rscp->tpt_table);
	c4iw_id_table_free(&rscp->qid_table);
	c4iw_id_table_free(&rscp->pdid_table);
}

/* PBL Memory Manager. */

#define MIN_PBL_SHIFT 5			/* 32B == min PBL size (4 entries) */

u32 c4iw_pblpool_alloc(struct c4iw_rdev *rdev, int size)
{
	unsigned long addr;

	vmem_xalloc(rdev->pbl_arena, roundup(size, (1 << MIN_PBL_SHIFT)),
			4, 0, 0, VMEM_ADDR_MIN, VMEM_ADDR_MAX,
			M_FIRSTFIT|M_NOWAIT, &addr);
	CTR3(KTR_IW_CXGBE, "%s addr 0x%x size %d", __func__, (u32)addr, size);
	mutex_lock(&rdev->stats.lock);
	if (addr) {
		rdev->stats.pbl.cur += roundup(size, 1 << MIN_PBL_SHIFT);
		if (rdev->stats.pbl.cur > rdev->stats.pbl.max)
			rdev->stats.pbl.max = rdev->stats.pbl.cur;
	} else
		rdev->stats.pbl.fail++;
	mutex_unlock(&rdev->stats.lock);
	return (u32)addr;
}

void c4iw_pblpool_free(struct c4iw_rdev *rdev, u32 addr, int size)
{
	CTR3(KTR_IW_CXGBE, "%s addr 0x%x size %d", __func__, addr, size);
	mutex_lock(&rdev->stats.lock);
	rdev->stats.pbl.cur -= roundup(size, 1 << MIN_PBL_SHIFT);
	mutex_unlock(&rdev->stats.lock);
	vmem_xfree(rdev->pbl_arena, addr, roundup(size,(1 << MIN_PBL_SHIFT)));
}

int c4iw_pblpool_create(struct c4iw_rdev *rdev)
{
	rdev->pbl_arena = vmem_create("PBL_MEM_POOL",
					rdev->adap->vres.pbl.start,
					rdev->adap->vres.pbl.size,
					1, 0, M_FIRSTFIT| M_NOWAIT);
	if (!rdev->pbl_arena)
		return -ENOMEM;

	return 0;
}

void c4iw_pblpool_destroy(struct c4iw_rdev *rdev)
{
	vmem_destroy(rdev->pbl_arena);
}

/* RQT Memory Manager. */

#define MIN_RQT_SHIFT 10	/* 1KB == min RQT size (16 entries) */

u32 c4iw_rqtpool_alloc(struct c4iw_rdev *rdev, int size)
{
	unsigned long addr;

	vmem_xalloc(rdev->rqt_arena,
			roundup((size << 6),(1 << MIN_RQT_SHIFT)),
			4, 0, 0, VMEM_ADDR_MIN, VMEM_ADDR_MAX,
			M_FIRSTFIT|M_NOWAIT, &addr);
	CTR3(KTR_IW_CXGBE, "%s addr 0x%x size %d", __func__, (u32)addr,
	    size << 6);
	if (!addr)
		printf("%s: Out of RQT memory\n",
		       device_get_nameunit(rdev->adap->dev));
	mutex_lock(&rdev->stats.lock);
	if (addr) {
		rdev->stats.rqt.cur += roundup(size << 6, 1 << MIN_RQT_SHIFT);
		if (rdev->stats.rqt.cur > rdev->stats.rqt.max)
			rdev->stats.rqt.max = rdev->stats.rqt.cur;
	} else
		rdev->stats.rqt.fail++;
	mutex_unlock(&rdev->stats.lock);
	return (u32)addr;
}

void c4iw_rqtpool_free(struct c4iw_rdev *rdev, u32 addr, int size)
{
	CTR3(KTR_IW_CXGBE, "%s addr 0x%x size %d", __func__, addr, size << 6);
	mutex_lock(&rdev->stats.lock);
	rdev->stats.rqt.cur -= roundup(size << 6, 1 << MIN_RQT_SHIFT);
	mutex_unlock(&rdev->stats.lock);
	vmem_xfree(rdev->rqt_arena, addr,
		       roundup((size << 6),(1 << MIN_RQT_SHIFT)));
}

int c4iw_rqtpool_create(struct c4iw_rdev *rdev)
{
	rdev->rqt_arena = vmem_create("RQT_MEM_POOL",
					rdev->adap->vres.rq.start,
					rdev->adap->vres.rq.size,
					1, 0, M_FIRSTFIT| M_NOWAIT);
	if (!rdev->rqt_arena)
		return -ENOMEM;

	return 0;
}

void c4iw_rqtpool_destroy(struct c4iw_rdev *rdev)
{
	vmem_destroy(rdev->rqt_arena);
}
#endif
