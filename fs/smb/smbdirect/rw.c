// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2017, Microsoft Corporation.
 *   Copyright (C) 2018, LG Electronics.
 *   Copyright (c) 2025, Stefan Metzmacher
 */

#include "internal.h"

static int smbdirect_connection_wait_for_rw_credits(struct smbdirect_socket *sc,
						    int credits)
{
	return smbdirect_socket_wait_for_credits(sc,
						 SMBDIRECT_SOCKET_CONNECTED,
						 -ENOTCONN,
						 &sc->rw_io.credits.wait_queue,
						 &sc->rw_io.credits.count,
						 credits);
}

static int smbdirect_connection_calc_rw_credits(struct smbdirect_socket *sc,
						const void *buf,
						size_t len)
{
	return DIV_ROUND_UP(smbdirect_get_buf_page_count(buf, len),
			    sc->rw_io.credits.num_pages);
}

static int smbdirect_connection_rdma_get_sg_list(void *buf,
						 size_t size,
						 struct scatterlist *sg_list,
						 size_t nentries)
{
	bool high = is_vmalloc_addr(buf);
	struct page *page;
	size_t offset, len;
	int i = 0;

	if (size == 0 || nentries < smbdirect_get_buf_page_count(buf, size))
		return -EINVAL;

	offset = offset_in_page(buf);
	buf -= offset;
	while (size > 0) {
		len = min_t(size_t, PAGE_SIZE - offset, size);
		if (high)
			page = vmalloc_to_page(buf);
		else
			page = kmap_to_page(buf);

		if (!sg_list)
			return -EINVAL;
		sg_set_page(sg_list, page, len, offset);
		sg_list = sg_next(sg_list);

		buf += PAGE_SIZE;
		size -= len;
		offset = 0;
		i++;
	}

	return i;
}

static void smbdirect_connection_rw_io_free(struct smbdirect_rw_io *msg,
					    enum dma_data_direction dir)
{
	struct smbdirect_socket *sc = msg->socket;

	rdma_rw_ctx_destroy(&msg->rdma_ctx,
			    sc->ib.qp,
			    sc->ib.qp->port,
			    msg->sgt.sgl,
			    msg->sgt.nents,
			    dir);
	sg_free_table_chained(&msg->sgt, SG_CHUNK_SIZE);
	kfree(msg);
}

static void smbdirect_connection_rdma_rw_done(struct ib_cq *cq, struct ib_wc *wc,
					      enum dma_data_direction dir)
{
	struct smbdirect_rw_io *msg =
		container_of(wc->wr_cqe, struct smbdirect_rw_io, cqe);
	struct smbdirect_socket *sc = msg->socket;

	if (wc->status != IB_WC_SUCCESS) {
		msg->error = -EIO;
		pr_err("read/write error. opcode = %d, status = %s(%d)\n",
		       wc->opcode, ib_wc_status_msg(wc->status), wc->status);
		if (wc->status != IB_WC_WR_FLUSH_ERR)
			smbdirect_socket_schedule_cleanup(sc, msg->error);
	}

	complete(msg->completion);
}

static void smbdirect_connection_rdma_read_done(struct ib_cq *cq, struct ib_wc *wc)
{
	smbdirect_connection_rdma_rw_done(cq, wc, DMA_FROM_DEVICE);
}

static void smbdirect_connection_rdma_write_done(struct ib_cq *cq, struct ib_wc *wc)
{
	smbdirect_connection_rdma_rw_done(cq, wc, DMA_TO_DEVICE);
}

int smbdirect_connection_rdma_xmit(struct smbdirect_socket *sc,
				   void *buf, size_t buf_len,
				   struct smbdirect_buffer_descriptor_v1 *desc,
				   size_t desc_len,
				   bool is_read)
{
	const struct smbdirect_socket_parameters *sp = &sc->parameters;
	enum dma_data_direction direction = is_read ? DMA_FROM_DEVICE : DMA_TO_DEVICE;
	struct smbdirect_rw_io *msg, *next_msg;
	size_t i;
	int ret;
	DECLARE_COMPLETION_ONSTACK(completion);
	struct ib_send_wr *first_wr;
	LIST_HEAD(msg_list);
	u8 *desc_buf;
	int credits_needed;
	size_t desc_buf_len, desc_num = 0;

	if (sc->status != SMBDIRECT_SOCKET_CONNECTED)
		return -ENOTCONN;

	if (buf_len > sp->max_read_write_size)
		return -EINVAL;

	/* calculate needed credits */
	credits_needed = 0;
	desc_buf = buf;
	for (i = 0; i < desc_len / sizeof(*desc); i++) {
		if (!buf_len)
			break;

		desc_buf_len = le32_to_cpu(desc[i].length);
		if (!desc_buf_len)
			return -EINVAL;

		if (desc_buf_len > buf_len) {
			desc_buf_len = buf_len;
			desc[i].length = cpu_to_le32(desc_buf_len);
			buf_len = 0;
		}

		credits_needed += smbdirect_connection_calc_rw_credits(sc,
								       desc_buf,
								       desc_buf_len);
		desc_buf += desc_buf_len;
		buf_len -= desc_buf_len;
		desc_num++;
	}

	smbdirect_log_rdma_rw(sc, SMBDIRECT_LOG_INFO,
		"RDMA %s, len %zu, needed credits %d\n",
		str_read_write(is_read), buf_len, credits_needed);

	ret = smbdirect_connection_wait_for_rw_credits(sc, credits_needed);
	if (ret < 0)
		return ret;

	/* build rdma_rw_ctx for each descriptor */
	desc_buf = buf;
	for (i = 0; i < desc_num; i++) {
		size_t page_count;

		msg = kzalloc_flex(*msg, sg_list, SG_CHUNK_SIZE,
				   sc->rw_io.mem.gfp_mask);
		if (!msg) {
			ret = -ENOMEM;
			goto out;
		}

		desc_buf_len = le32_to_cpu(desc[i].length);
		page_count = smbdirect_get_buf_page_count(desc_buf, desc_buf_len);

		msg->socket = sc;
		msg->cqe.done = is_read ?
			smbdirect_connection_rdma_read_done :
			smbdirect_connection_rdma_write_done;
		msg->completion = &completion;

		msg->sgt.sgl = &msg->sg_list[0];
		ret = sg_alloc_table_chained(&msg->sgt,
					     page_count,
					     msg->sg_list,
					     SG_CHUNK_SIZE);
		if (ret) {
			ret = -ENOMEM;
			goto free_msg;
		}

		ret = smbdirect_connection_rdma_get_sg_list(desc_buf,
							    desc_buf_len,
							    msg->sgt.sgl,
							    msg->sgt.orig_nents);
		if (ret < 0)
			goto free_table;

		ret = rdma_rw_ctx_init(&msg->rdma_ctx,
				       sc->ib.qp,
				       sc->ib.qp->port,
				       msg->sgt.sgl,
				       page_count,
				       0,
				       le64_to_cpu(desc[i].offset),
				       le32_to_cpu(desc[i].token),
				       direction);
		if (ret < 0) {
			pr_err("failed to init rdma_rw_ctx: %d\n", ret);
			goto free_table;
		}

		list_add_tail(&msg->list, &msg_list);
		desc_buf += desc_buf_len;
	}

	/* concatenate work requests of rdma_rw_ctxs */
	first_wr = NULL;
	list_for_each_entry_reverse(msg, &msg_list, list) {
		first_wr = rdma_rw_ctx_wrs(&msg->rdma_ctx,
					   sc->ib.qp,
					   sc->ib.qp->port,
					   &msg->cqe,
					   first_wr);
	}

	ret = ib_post_send(sc->ib.qp, first_wr, NULL);
	if (ret) {
		pr_err("failed to post send wr for RDMA R/W: %d\n", ret);
		goto out;
	}

	msg = list_last_entry(&msg_list, struct smbdirect_rw_io, list);
	wait_for_completion(&completion);
	ret = msg->error;
out:
	list_for_each_entry_safe(msg, next_msg, &msg_list, list) {
		list_del(&msg->list);
		smbdirect_connection_rw_io_free(msg, direction);
	}
	atomic_add(credits_needed, &sc->rw_io.credits.count);
	wake_up(&sc->rw_io.credits.wait_queue);
	return ret;

free_table:
	sg_free_table_chained(&msg->sgt, SG_CHUNK_SIZE);
free_msg:
	kfree(msg);
	goto out;
}
EXPORT_SYMBOL_GPL(smbdirect_connection_rdma_xmit);
