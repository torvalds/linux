/* $FreeBSD$ */
/*-
 * Copyright (c) 2015, Mellanox Technologies, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "icl_iser.h"

static struct fast_reg_descriptor *
iser_reg_desc_get(struct ib_conn *ib_conn)
{
	struct fast_reg_descriptor *desc;

	mtx_lock(&ib_conn->lock);
	desc = list_first_entry(&ib_conn->fastreg.pool,
				struct fast_reg_descriptor, list);
	list_del(&desc->list);
	mtx_unlock(&ib_conn->lock);

	return (desc);
}

static void
iser_reg_desc_put(struct ib_conn *ib_conn,
		  struct fast_reg_descriptor *desc)
{
	mtx_lock(&ib_conn->lock);
	list_add(&desc->list, &ib_conn->fastreg.pool);
	mtx_unlock(&ib_conn->lock);
}

#define IS_4K_ALIGNED(addr)	((((unsigned long)addr) & ~MASK_4K) == 0)

/**
 * iser_data_buf_aligned_len - Tries to determine the maximal correctly aligned
 * for RDMA sub-list of a scatter-gather list of memory buffers, and  returns
 * the number of entries which are aligned correctly. Supports the case where
 * consecutive SG elements are actually fragments of the same physcial page.
 */
static int
iser_data_buf_aligned_len(struct iser_data_buf *data, struct ib_device *ibdev)
{
	struct scatterlist *sg, *sgl, *next_sg = NULL;
	u64 start_addr, end_addr;
	int i, ret_len, start_check = 0;

	if (data->dma_nents == 1)
		return (1);

	sgl = data->sgl;
	start_addr  = ib_sg_dma_address(ibdev, sgl);

	for_each_sg(sgl, sg, data->dma_nents, i) {
		if (start_check && !IS_4K_ALIGNED(start_addr))
			break;

		next_sg = sg_next(sg);
		if (!next_sg)
			break;

		end_addr    = start_addr + ib_sg_dma_len(ibdev, sg);
		start_addr  = ib_sg_dma_address(ibdev, next_sg);

		if (end_addr == start_addr) {
			start_check = 0;
			continue;
		} else
			start_check = 1;

		if (!IS_4K_ALIGNED(end_addr))
			break;
	}
	ret_len = (next_sg) ? i : i+1;

	return (ret_len);
}

void
iser_dma_unmap_task_data(struct icl_iser_pdu *iser_pdu,
			 struct iser_data_buf *data,
			 enum dma_data_direction dir)
{
	struct ib_device *dev;

	dev = iser_pdu->iser_conn->ib_conn.device->ib_device;
	ib_dma_unmap_sg(dev, data->sgl, data->size, dir);
}

static int
iser_reg_dma(struct iser_device *device, struct iser_data_buf *mem,
	     struct iser_mem_reg *reg)
{
	struct scatterlist *sg = mem->sgl;

	reg->sge.lkey = device->mr->lkey;
	reg->rkey = device->mr->rkey;
	reg->sge.length = ib_sg_dma_len(device->ib_device, &sg[0]);
	reg->sge.addr = ib_sg_dma_address(device->ib_device, &sg[0]);

	return (0);
}

/**
 * TODO: This should be a verb
 * iser_ib_inc_rkey - increments the key portion of the given rkey. Can be used
 * for calculating a new rkey for type 2 memory windows.
 * @rkey - the rkey to increment.
 */
static inline u32
iser_ib_inc_rkey(u32 rkey)
{
	const u32 mask = 0x000000ff;

	return (((rkey + 1) & mask) | (rkey & ~mask));
}

static void
iser_inv_rkey(struct ib_send_wr *inv_wr, struct ib_mr *mr)
{
	u32 rkey;

	memset(inv_wr, 0, sizeof(*inv_wr));
	inv_wr->opcode = IB_WR_LOCAL_INV;
	inv_wr->wr_id = ISER_FASTREG_LI_WRID;
	inv_wr->ex.invalidate_rkey = mr->rkey;

	rkey = iser_ib_inc_rkey(mr->rkey);
	ib_update_fast_reg_key(mr, rkey);
}

static int
iser_fast_reg_mr(struct icl_iser_pdu *iser_pdu,
		 struct iser_data_buf *mem,
		 struct iser_reg_resources *rsc,
		 struct iser_mem_reg *reg)
{
	struct ib_conn *ib_conn = &iser_pdu->iser_conn->ib_conn;
	struct iser_device *device = ib_conn->device;
	struct ib_mr *mr = rsc->mr;
	struct ib_reg_wr fastreg_wr;
	struct ib_send_wr inv_wr;
	struct ib_send_wr *bad_wr, *wr = NULL;
	int ret, n;

	/* if there a single dma entry, dma mr suffices */
	if (mem->dma_nents == 1)
		return iser_reg_dma(device, mem, reg);

	if (!rsc->mr_valid) {
		iser_inv_rkey(&inv_wr, mr);
		wr = &inv_wr;
	}

	n = ib_map_mr_sg(mr, mem->sg, mem->size, NULL, SIZE_4K);
	if (unlikely(n != mem->size)) {
		ISER_ERR("failed to map sg (%d/%d)\n", n, mem->size);
		return n < 0 ? n : -EINVAL;
	}
	/* Prepare FASTREG WR */
	memset(&fastreg_wr, 0, sizeof(fastreg_wr));
	fastreg_wr.wr.opcode = IB_WR_REG_MR;
	fastreg_wr.wr.wr_id = ISER_FASTREG_LI_WRID;
	fastreg_wr.wr.num_sge = 0;
	fastreg_wr.mr = mr;
	fastreg_wr.key = mr->rkey;
	fastreg_wr.access = IB_ACCESS_LOCAL_WRITE  |
			    IB_ACCESS_REMOTE_WRITE |
			    IB_ACCESS_REMOTE_READ;

	if (!wr)
		wr = &fastreg_wr.wr;
	else
		wr->next = &fastreg_wr.wr;

	ret = ib_post_send(ib_conn->qp, wr, &bad_wr);
	if (ret) {
		ISER_ERR("fast registration failed, ret:%d", ret);
		return (ret);
	}
	rsc->mr_valid = 0;

	reg->sge.lkey = mr->lkey;
	reg->rkey = mr->rkey;
	reg->sge.addr = mr->iova;
	reg->sge.length = mr->length;

	return (ret);
}

/**
 * iser_reg_rdma_mem - Registers memory intended for RDMA,
 * using Fast Registration WR (if possible) obtaining rkey and va
 *
 * returns 0 on success, errno code on failure
 */
int
iser_reg_rdma_mem(struct icl_iser_pdu *iser_pdu,
		  enum iser_data_dir cmd_dir)
{
	struct ib_conn *ib_conn = &iser_pdu->iser_conn->ib_conn;
	struct iser_device   *device = ib_conn->device;
	struct ib_device     *ibdev = device->ib_device;
	struct iser_data_buf *mem = &iser_pdu->data[cmd_dir];
	struct iser_mem_reg *mem_reg = &iser_pdu->rdma_reg[cmd_dir];
	struct fast_reg_descriptor *desc = NULL;
	int err, aligned_len;

	aligned_len = iser_data_buf_aligned_len(mem, ibdev);
	if (aligned_len != mem->dma_nents) {
		ISER_ERR("bounce buffer is not supported");
		return 1;
	}

	if (mem->dma_nents != 1) {
		desc = iser_reg_desc_get(ib_conn);
		mem_reg->mem_h = desc;
	}

	err = iser_fast_reg_mr(iser_pdu, mem, desc ? &desc->rsc : NULL,
				       mem_reg);
	if (err)
		goto err_reg;

	return (0);

err_reg:
	if (desc)
		iser_reg_desc_put(ib_conn, desc);

	return (err);
}

void
iser_unreg_rdma_mem(struct icl_iser_pdu *iser_pdu,
		    enum iser_data_dir cmd_dir)
{
	struct iser_mem_reg *reg = &iser_pdu->rdma_reg[cmd_dir];

	if (!reg->mem_h)
		return;

	iser_reg_desc_put(&iser_pdu->iser_conn->ib_conn,
			  reg->mem_h);
	reg->mem_h = NULL;
}

int
iser_dma_map_task_data(struct icl_iser_pdu *iser_pdu,
		       struct iser_data_buf *data,
		       enum iser_data_dir iser_dir,
		       enum dma_data_direction dma_dir)
{
	struct ib_device *dev;

	iser_pdu->dir[iser_dir] = 1;
	dev = iser_pdu->iser_conn->ib_conn.device->ib_device;

	data->dma_nents = ib_dma_map_sg(dev, data->sgl, data->size, dma_dir);
	if (data->dma_nents == 0) {
		ISER_ERR("dma_map_sg failed");
		return (EINVAL);
	}

	return (0);
}
