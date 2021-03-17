// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2018 Oracle.  All rights reserved.
 *
 * Use the core R/W API to move RPC-over-RDMA Read and Write chunks.
 */

#include <rdma/rw.h>

#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/rpc_rdma.h>
#include <linux/sunrpc/svc_rdma.h>

#include "xprt_rdma.h"
#include <trace/events/rpcrdma.h>

static void svc_rdma_write_done(struct ib_cq *cq, struct ib_wc *wc);
static void svc_rdma_wc_read_done(struct ib_cq *cq, struct ib_wc *wc);

/* Each R/W context contains state for one chain of RDMA Read or
 * Write Work Requests.
 *
 * Each WR chain handles a single contiguous server-side buffer,
 * because scatterlist entries after the first have to start on
 * page alignment. xdr_buf iovecs cannot guarantee alignment.
 *
 * Each WR chain handles only one R_key. Each RPC-over-RDMA segment
 * from a client may contain a unique R_key, so each WR chain moves
 * up to one segment at a time.
 *
 * The scatterlist makes this data structure over 4KB in size. To
 * make it less likely to fail, and to handle the allocation for
 * smaller I/O requests without disabling bottom-halves, these
 * contexts are created on demand, but cached and reused until the
 * controlling svcxprt_rdma is destroyed.
 */
struct svc_rdma_rw_ctxt {
	struct list_head	rw_list;
	struct rdma_rw_ctx	rw_ctx;
	unsigned int		rw_nents;
	struct sg_table		rw_sg_table;
	struct scatterlist	rw_first_sgl[];
};

static inline struct svc_rdma_rw_ctxt *
svc_rdma_next_ctxt(struct list_head *list)
{
	return list_first_entry_or_null(list, struct svc_rdma_rw_ctxt,
					rw_list);
}

static struct svc_rdma_rw_ctxt *
svc_rdma_get_rw_ctxt(struct svcxprt_rdma *rdma, unsigned int sges)
{
	struct svc_rdma_rw_ctxt *ctxt;

	spin_lock(&rdma->sc_rw_ctxt_lock);

	ctxt = svc_rdma_next_ctxt(&rdma->sc_rw_ctxts);
	if (ctxt) {
		list_del(&ctxt->rw_list);
		spin_unlock(&rdma->sc_rw_ctxt_lock);
	} else {
		spin_unlock(&rdma->sc_rw_ctxt_lock);
		ctxt = kmalloc(struct_size(ctxt, rw_first_sgl, SG_CHUNK_SIZE),
			       GFP_KERNEL);
		if (!ctxt)
			goto out_noctx;
		INIT_LIST_HEAD(&ctxt->rw_list);
	}

	ctxt->rw_sg_table.sgl = ctxt->rw_first_sgl;
	if (sg_alloc_table_chained(&ctxt->rw_sg_table, sges,
				   ctxt->rw_sg_table.sgl,
				   SG_CHUNK_SIZE))
		goto out_free;
	return ctxt;

out_free:
	kfree(ctxt);
out_noctx:
	trace_svcrdma_no_rwctx_err(rdma, sges);
	return NULL;
}

static void svc_rdma_put_rw_ctxt(struct svcxprt_rdma *rdma,
				 struct svc_rdma_rw_ctxt *ctxt)
{
	sg_free_table_chained(&ctxt->rw_sg_table, SG_CHUNK_SIZE);

	spin_lock(&rdma->sc_rw_ctxt_lock);
	list_add(&ctxt->rw_list, &rdma->sc_rw_ctxts);
	spin_unlock(&rdma->sc_rw_ctxt_lock);
}

/**
 * svc_rdma_destroy_rw_ctxts - Free accumulated R/W contexts
 * @rdma: transport about to be destroyed
 *
 */
void svc_rdma_destroy_rw_ctxts(struct svcxprt_rdma *rdma)
{
	struct svc_rdma_rw_ctxt *ctxt;

	while ((ctxt = svc_rdma_next_ctxt(&rdma->sc_rw_ctxts)) != NULL) {
		list_del(&ctxt->rw_list);
		kfree(ctxt);
	}
}

/**
 * svc_rdma_rw_ctx_init - Prepare a R/W context for I/O
 * @rdma: controlling transport instance
 * @ctxt: R/W context to prepare
 * @offset: RDMA offset
 * @handle: RDMA tag/handle
 * @direction: I/O direction
 *
 * Returns on success, the number of WQEs that will be needed
 * on the workqueue, or a negative errno.
 */
static int svc_rdma_rw_ctx_init(struct svcxprt_rdma *rdma,
				struct svc_rdma_rw_ctxt *ctxt,
				u64 offset, u32 handle,
				enum dma_data_direction direction)
{
	int ret;

	ret = rdma_rw_ctx_init(&ctxt->rw_ctx, rdma->sc_qp, rdma->sc_port_num,
			       ctxt->rw_sg_table.sgl, ctxt->rw_nents,
			       0, offset, handle, direction);
	if (unlikely(ret < 0)) {
		svc_rdma_put_rw_ctxt(rdma, ctxt);
		trace_svcrdma_dma_map_rw_err(rdma, ctxt->rw_nents, ret);
	}
	return ret;
}

/* A chunk context tracks all I/O for moving one Read or Write
 * chunk. This is a set of rdma_rw's that handle data movement
 * for all segments of one chunk.
 *
 * These are small, acquired with a single allocator call, and
 * no more than one is needed per chunk. They are allocated on
 * demand, and not cached.
 */
struct svc_rdma_chunk_ctxt {
	struct rpc_rdma_cid	cc_cid;
	struct ib_cqe		cc_cqe;
	struct svcxprt_rdma	*cc_rdma;
	struct list_head	cc_rwctxts;
	int			cc_sqecount;
};

static void svc_rdma_cc_cid_init(struct svcxprt_rdma *rdma,
				 struct rpc_rdma_cid *cid)
{
	cid->ci_queue_id = rdma->sc_sq_cq->res.id;
	cid->ci_completion_id = atomic_inc_return(&rdma->sc_completion_ids);
}

static void svc_rdma_cc_init(struct svcxprt_rdma *rdma,
			     struct svc_rdma_chunk_ctxt *cc)
{
	svc_rdma_cc_cid_init(rdma, &cc->cc_cid);
	cc->cc_rdma = rdma;

	INIT_LIST_HEAD(&cc->cc_rwctxts);
	cc->cc_sqecount = 0;
}

static void svc_rdma_cc_release(struct svc_rdma_chunk_ctxt *cc,
				enum dma_data_direction dir)
{
	struct svcxprt_rdma *rdma = cc->cc_rdma;
	struct svc_rdma_rw_ctxt *ctxt;

	while ((ctxt = svc_rdma_next_ctxt(&cc->cc_rwctxts)) != NULL) {
		list_del(&ctxt->rw_list);

		rdma_rw_ctx_destroy(&ctxt->rw_ctx, rdma->sc_qp,
				    rdma->sc_port_num, ctxt->rw_sg_table.sgl,
				    ctxt->rw_nents, dir);
		svc_rdma_put_rw_ctxt(rdma, ctxt);
	}
}

/* State for sending a Write or Reply chunk.
 *  - Tracks progress of writing one chunk over all its segments
 *  - Stores arguments for the SGL constructor functions
 */
struct svc_rdma_write_info {
	/* write state of this chunk */
	unsigned int		wi_seg_off;
	unsigned int		wi_seg_no;
	unsigned int		wi_nsegs;
	__be32			*wi_segs;

	/* SGL constructor arguments */
	struct xdr_buf		*wi_xdr;
	unsigned char		*wi_base;
	unsigned int		wi_next_off;

	struct svc_rdma_chunk_ctxt	wi_cc;
};

static struct svc_rdma_write_info *
svc_rdma_write_info_alloc(struct svcxprt_rdma *rdma, __be32 *chunk)
{
	struct svc_rdma_write_info *info;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return info;

	info->wi_seg_off = 0;
	info->wi_seg_no = 0;
	info->wi_nsegs = be32_to_cpup(++chunk);
	info->wi_segs = ++chunk;
	svc_rdma_cc_init(rdma, &info->wi_cc);
	info->wi_cc.cc_cqe.done = svc_rdma_write_done;
	return info;
}

static void svc_rdma_write_info_free(struct svc_rdma_write_info *info)
{
	svc_rdma_cc_release(&info->wi_cc, DMA_TO_DEVICE);
	kfree(info);
}

/**
 * svc_rdma_write_done - Write chunk completion
 * @cq: controlling Completion Queue
 * @wc: Work Completion
 *
 * Pages under I/O are freed by a subsequent Send completion.
 */
static void svc_rdma_write_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct svc_rdma_chunk_ctxt *cc =
			container_of(cqe, struct svc_rdma_chunk_ctxt, cc_cqe);
	struct svcxprt_rdma *rdma = cc->cc_rdma;
	struct svc_rdma_write_info *info =
			container_of(cc, struct svc_rdma_write_info, wi_cc);

	trace_svcrdma_wc_write(wc, &cc->cc_cid);

	atomic_add(cc->cc_sqecount, &rdma->sc_sq_avail);
	wake_up(&rdma->sc_send_wait);

	if (unlikely(wc->status != IB_WC_SUCCESS))
		set_bit(XPT_CLOSE, &rdma->sc_xprt.xpt_flags);

	svc_rdma_write_info_free(info);
}

/* State for pulling a Read chunk.
 */
struct svc_rdma_read_info {
	struct svc_rdma_recv_ctxt	*ri_readctxt;
	unsigned int			ri_position;
	unsigned int			ri_pageno;
	unsigned int			ri_pageoff;
	unsigned int			ri_chunklen;

	struct svc_rdma_chunk_ctxt	ri_cc;
};

static struct svc_rdma_read_info *
svc_rdma_read_info_alloc(struct svcxprt_rdma *rdma)
{
	struct svc_rdma_read_info *info;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return info;

	svc_rdma_cc_init(rdma, &info->ri_cc);
	info->ri_cc.cc_cqe.done = svc_rdma_wc_read_done;
	return info;
}

static void svc_rdma_read_info_free(struct svc_rdma_read_info *info)
{
	svc_rdma_cc_release(&info->ri_cc, DMA_FROM_DEVICE);
	kfree(info);
}

/**
 * svc_rdma_wc_read_done - Handle completion of an RDMA Read ctx
 * @cq: controlling Completion Queue
 * @wc: Work Completion
 *
 */
static void svc_rdma_wc_read_done(struct ib_cq *cq, struct ib_wc *wc)
{
	struct ib_cqe *cqe = wc->wr_cqe;
	struct svc_rdma_chunk_ctxt *cc =
			container_of(cqe, struct svc_rdma_chunk_ctxt, cc_cqe);
	struct svcxprt_rdma *rdma = cc->cc_rdma;
	struct svc_rdma_read_info *info =
			container_of(cc, struct svc_rdma_read_info, ri_cc);

	trace_svcrdma_wc_read(wc, &cc->cc_cid);

	atomic_add(cc->cc_sqecount, &rdma->sc_sq_avail);
	wake_up(&rdma->sc_send_wait);

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		set_bit(XPT_CLOSE, &rdma->sc_xprt.xpt_flags);
		svc_rdma_recv_ctxt_put(rdma, info->ri_readctxt);
	} else {
		spin_lock(&rdma->sc_rq_dto_lock);
		list_add_tail(&info->ri_readctxt->rc_list,
			      &rdma->sc_read_complete_q);
		/* Note the unlock pairs with the smp_rmb in svc_xprt_ready: */
		set_bit(XPT_DATA, &rdma->sc_xprt.xpt_flags);
		spin_unlock(&rdma->sc_rq_dto_lock);

		svc_xprt_enqueue(&rdma->sc_xprt);
	}

	svc_rdma_read_info_free(info);
}

/* This function sleeps when the transport's Send Queue is congested.
 *
 * Assumptions:
 * - If ib_post_send() succeeds, only one completion is expected,
 *   even if one or more WRs are flushed. This is true when posting
 *   an rdma_rw_ctx or when posting a single signaled WR.
 */
static int svc_rdma_post_chunk_ctxt(struct svc_rdma_chunk_ctxt *cc)
{
	struct svcxprt_rdma *rdma = cc->cc_rdma;
	struct svc_xprt *xprt = &rdma->sc_xprt;
	struct ib_send_wr *first_wr;
	const struct ib_send_wr *bad_wr;
	struct list_head *tmp;
	struct ib_cqe *cqe;
	int ret;

	if (cc->cc_sqecount > rdma->sc_sq_depth)
		return -EINVAL;

	first_wr = NULL;
	cqe = &cc->cc_cqe;
	list_for_each(tmp, &cc->cc_rwctxts) {
		struct svc_rdma_rw_ctxt *ctxt;

		ctxt = list_entry(tmp, struct svc_rdma_rw_ctxt, rw_list);
		first_wr = rdma_rw_ctx_wrs(&ctxt->rw_ctx, rdma->sc_qp,
					   rdma->sc_port_num, cqe, first_wr);
		cqe = NULL;
	}

	do {
		if (atomic_sub_return(cc->cc_sqecount,
				      &rdma->sc_sq_avail) > 0) {
			trace_svcrdma_post_chunk(&cc->cc_cid, cc->cc_sqecount);
			ret = ib_post_send(rdma->sc_qp, first_wr, &bad_wr);
			if (ret)
				break;
			return 0;
		}

		trace_svcrdma_sq_full(rdma);
		atomic_add(cc->cc_sqecount, &rdma->sc_sq_avail);
		wait_event(rdma->sc_send_wait,
			   atomic_read(&rdma->sc_sq_avail) > cc->cc_sqecount);
		trace_svcrdma_sq_retry(rdma);
	} while (1);

	trace_svcrdma_sq_post_err(rdma, ret);
	set_bit(XPT_CLOSE, &xprt->xpt_flags);

	/* If even one was posted, there will be a completion. */
	if (bad_wr != first_wr)
		return 0;

	atomic_add(cc->cc_sqecount, &rdma->sc_sq_avail);
	wake_up(&rdma->sc_send_wait);
	return -ENOTCONN;
}

/* Build and DMA-map an SGL that covers one kvec in an xdr_buf
 */
static void svc_rdma_vec_to_sg(struct svc_rdma_write_info *info,
			       unsigned int len,
			       struct svc_rdma_rw_ctxt *ctxt)
{
	struct scatterlist *sg = ctxt->rw_sg_table.sgl;

	sg_set_buf(&sg[0], info->wi_base, len);
	info->wi_base += len;

	ctxt->rw_nents = 1;
}

/* Build and DMA-map an SGL that covers part of an xdr_buf's pagelist.
 */
static void svc_rdma_pagelist_to_sg(struct svc_rdma_write_info *info,
				    unsigned int remaining,
				    struct svc_rdma_rw_ctxt *ctxt)
{
	unsigned int sge_no, sge_bytes, page_off, page_no;
	struct xdr_buf *xdr = info->wi_xdr;
	struct scatterlist *sg;
	struct page **page;

	page_off = info->wi_next_off + xdr->page_base;
	page_no = page_off >> PAGE_SHIFT;
	page_off = offset_in_page(page_off);
	page = xdr->pages + page_no;
	info->wi_next_off += remaining;
	sg = ctxt->rw_sg_table.sgl;
	sge_no = 0;
	do {
		sge_bytes = min_t(unsigned int, remaining,
				  PAGE_SIZE - page_off);
		sg_set_page(sg, *page, sge_bytes, page_off);

		remaining -= sge_bytes;
		sg = sg_next(sg);
		page_off = 0;
		sge_no++;
		page++;
	} while (remaining);

	ctxt->rw_nents = sge_no;
}

/* Construct RDMA Write WRs to send a portion of an xdr_buf containing
 * an RPC Reply.
 */
static int
svc_rdma_build_writes(struct svc_rdma_write_info *info,
		      void (*constructor)(struct svc_rdma_write_info *info,
					  unsigned int len,
					  struct svc_rdma_rw_ctxt *ctxt),
		      unsigned int remaining)
{
	struct svc_rdma_chunk_ctxt *cc = &info->wi_cc;
	struct svcxprt_rdma *rdma = cc->cc_rdma;
	struct svc_rdma_rw_ctxt *ctxt;
	__be32 *seg;
	int ret;

	seg = info->wi_segs + info->wi_seg_no * rpcrdma_segment_maxsz;
	do {
		unsigned int write_len;
		u32 handle, length;
		u64 offset;

		if (info->wi_seg_no >= info->wi_nsegs)
			goto out_overflow;

		xdr_decode_rdma_segment(seg, &handle, &length, &offset);
		offset += info->wi_seg_off;

		write_len = min(remaining, length - info->wi_seg_off);
		ctxt = svc_rdma_get_rw_ctxt(rdma,
					    (write_len >> PAGE_SHIFT) + 2);
		if (!ctxt)
			return -ENOMEM;

		constructor(info, write_len, ctxt);
		ret = svc_rdma_rw_ctx_init(rdma, ctxt, offset, handle,
					   DMA_TO_DEVICE);
		if (ret < 0)
			return -EIO;

		trace_svcrdma_send_wseg(handle, write_len, offset);

		list_add(&ctxt->rw_list, &cc->cc_rwctxts);
		cc->cc_sqecount += ret;
		if (write_len == length - info->wi_seg_off) {
			seg += 4;
			info->wi_seg_no++;
			info->wi_seg_off = 0;
		} else {
			info->wi_seg_off += write_len;
		}
		remaining -= write_len;
	} while (remaining);

	return 0;

out_overflow:
	trace_svcrdma_small_wrch_err(rdma, remaining, info->wi_seg_no,
				     info->wi_nsegs);
	return -E2BIG;
}

/* Send one of an xdr_buf's kvecs by itself. To send a Reply
 * chunk, the whole RPC Reply is written back to the client.
 * This function writes either the head or tail of the xdr_buf
 * containing the Reply.
 */
static int svc_rdma_send_xdr_kvec(struct svc_rdma_write_info *info,
				  struct kvec *vec)
{
	info->wi_base = vec->iov_base;
	return svc_rdma_build_writes(info, svc_rdma_vec_to_sg,
				     vec->iov_len);
}

/* Send an xdr_buf's page list by itself. A Write chunk is just
 * the page list. A Reply chunk is @xdr's head, page list, and
 * tail. This function is shared between the two types of chunk.
 */
static int svc_rdma_send_xdr_pagelist(struct svc_rdma_write_info *info,
				      struct xdr_buf *xdr,
				      unsigned int offset,
				      unsigned long length)
{
	info->wi_xdr = xdr;
	info->wi_next_off = offset - xdr->head[0].iov_len;
	return svc_rdma_build_writes(info, svc_rdma_pagelist_to_sg,
				     length);
}

/**
 * svc_rdma_send_write_chunk - Write all segments in a Write chunk
 * @rdma: controlling RDMA transport
 * @wr_ch: Write chunk provided by client
 * @xdr: xdr_buf containing the data payload
 * @offset: payload's byte offset in @xdr
 * @length: size of payload, in bytes
 *
 * Returns a non-negative number of bytes the chunk consumed, or
 *	%-E2BIG if the payload was larger than the Write chunk,
 *	%-EINVAL if client provided too many segments,
 *	%-ENOMEM if rdma_rw context pool was exhausted,
 *	%-ENOTCONN if posting failed (connection is lost),
 *	%-EIO if rdma_rw initialization failed (DMA mapping, etc).
 */
int svc_rdma_send_write_chunk(struct svcxprt_rdma *rdma, __be32 *wr_ch,
			      struct xdr_buf *xdr,
			      unsigned int offset, unsigned long length)
{
	struct svc_rdma_write_info *info;
	int ret;

	if (!length)
		return 0;

	info = svc_rdma_write_info_alloc(rdma, wr_ch);
	if (!info)
		return -ENOMEM;

	ret = svc_rdma_send_xdr_pagelist(info, xdr, offset, length);
	if (ret < 0)
		goto out_err;

	ret = svc_rdma_post_chunk_ctxt(&info->wi_cc);
	if (ret < 0)
		goto out_err;

	trace_svcrdma_send_write_chunk(xdr->page_len);
	return length;

out_err:
	svc_rdma_write_info_free(info);
	return ret;
}

/**
 * svc_rdma_send_reply_chunk - Write all segments in the Reply chunk
 * @rdma: controlling RDMA transport
 * @rctxt: Write and Reply chunks from client
 * @xdr: xdr_buf containing an RPC Reply
 *
 * Returns a non-negative number of bytes the chunk consumed, or
 *	%-E2BIG if the payload was larger than the Reply chunk,
 *	%-EINVAL if client provided too many segments,
 *	%-ENOMEM if rdma_rw context pool was exhausted,
 *	%-ENOTCONN if posting failed (connection is lost),
 *	%-EIO if rdma_rw initialization failed (DMA mapping, etc).
 */
int svc_rdma_send_reply_chunk(struct svcxprt_rdma *rdma,
			      const struct svc_rdma_recv_ctxt *rctxt,
			      struct xdr_buf *xdr)
{
	struct svc_rdma_write_info *info;
	int consumed, ret;

	info = svc_rdma_write_info_alloc(rdma, rctxt->rc_reply_chunk);
	if (!info)
		return -ENOMEM;

	ret = svc_rdma_send_xdr_kvec(info, &xdr->head[0]);
	if (ret < 0)
		goto out_err;
	consumed = xdr->head[0].iov_len;

	/* Send the page list in the Reply chunk only if the
	 * client did not provide Write chunks.
	 */
	if (!rctxt->rc_write_list && xdr->page_len) {
		ret = svc_rdma_send_xdr_pagelist(info, xdr,
						 xdr->head[0].iov_len,
						 xdr->page_len);
		if (ret < 0)
			goto out_err;
		consumed += xdr->page_len;
	}

	if (xdr->tail[0].iov_len) {
		ret = svc_rdma_send_xdr_kvec(info, &xdr->tail[0]);
		if (ret < 0)
			goto out_err;
		consumed += xdr->tail[0].iov_len;
	}

	ret = svc_rdma_post_chunk_ctxt(&info->wi_cc);
	if (ret < 0)
		goto out_err;

	trace_svcrdma_send_reply_chunk(consumed);
	return consumed;

out_err:
	svc_rdma_write_info_free(info);
	return ret;
}

static int svc_rdma_build_read_segment(struct svc_rdma_read_info *info,
				       struct svc_rqst *rqstp,
				       u32 rkey, u32 len, u64 offset)
{
	struct svc_rdma_recv_ctxt *head = info->ri_readctxt;
	struct svc_rdma_chunk_ctxt *cc = &info->ri_cc;
	struct svc_rdma_rw_ctxt *ctxt;
	unsigned int sge_no, seg_len;
	struct scatterlist *sg;
	int ret;

	sge_no = PAGE_ALIGN(info->ri_pageoff + len) >> PAGE_SHIFT;
	ctxt = svc_rdma_get_rw_ctxt(cc->cc_rdma, sge_no);
	if (!ctxt)
		return -ENOMEM;
	ctxt->rw_nents = sge_no;

	sg = ctxt->rw_sg_table.sgl;
	for (sge_no = 0; sge_no < ctxt->rw_nents; sge_no++) {
		seg_len = min_t(unsigned int, len,
				PAGE_SIZE - info->ri_pageoff);

		head->rc_arg.pages[info->ri_pageno] =
			rqstp->rq_pages[info->ri_pageno];
		if (!info->ri_pageoff)
			head->rc_page_count++;

		sg_set_page(sg, rqstp->rq_pages[info->ri_pageno],
			    seg_len, info->ri_pageoff);
		sg = sg_next(sg);

		info->ri_pageoff += seg_len;
		if (info->ri_pageoff == PAGE_SIZE) {
			info->ri_pageno++;
			info->ri_pageoff = 0;
		}
		len -= seg_len;

		/* Safety check */
		if (len &&
		    &rqstp->rq_pages[info->ri_pageno + 1] > rqstp->rq_page_end)
			goto out_overrun;
	}

	ret = svc_rdma_rw_ctx_init(cc->cc_rdma, ctxt, offset, rkey,
				   DMA_FROM_DEVICE);
	if (ret < 0)
		return -EIO;

	list_add(&ctxt->rw_list, &cc->cc_rwctxts);
	cc->cc_sqecount += ret;
	return 0;

out_overrun:
	trace_svcrdma_page_overrun_err(cc->cc_rdma, rqstp, info->ri_pageno);
	return -EINVAL;
}

/* Walk the segments in the Read chunk starting at @p and construct
 * RDMA Read operations to pull the chunk to the server.
 */
static int svc_rdma_build_read_chunk(struct svc_rqst *rqstp,
				     struct svc_rdma_read_info *info,
				     __be32 *p)
{
	int ret;

	ret = -EINVAL;
	info->ri_chunklen = 0;
	while (*p++ != xdr_zero && be32_to_cpup(p++) == info->ri_position) {
		u32 handle, length;
		u64 offset;

		p = xdr_decode_rdma_segment(p, &handle, &length, &offset);
		ret = svc_rdma_build_read_segment(info, rqstp, handle, length,
						  offset);
		if (ret < 0)
			break;

		trace_svcrdma_send_rseg(handle, length, offset);
		info->ri_chunklen += length;
	}

	return ret;
}

/* Construct RDMA Reads to pull over a normal Read chunk. The chunk
 * data lands in the page list of head->rc_arg.pages.
 *
 * Currently NFSD does not look at the head->rc_arg.tail[0] iovec.
 * Therefore, XDR round-up of the Read chunk and trailing
 * inline content must both be added at the end of the pagelist.
 */
static int svc_rdma_build_normal_read_chunk(struct svc_rqst *rqstp,
					    struct svc_rdma_read_info *info,
					    __be32 *p)
{
	struct svc_rdma_recv_ctxt *head = info->ri_readctxt;
	int ret;

	ret = svc_rdma_build_read_chunk(rqstp, info, p);
	if (ret < 0)
		goto out;

	trace_svcrdma_send_read_chunk(info->ri_chunklen, info->ri_position);

	head->rc_hdr_count = 0;

	/* Split the Receive buffer between the head and tail
	 * buffers at Read chunk's position. XDR roundup of the
	 * chunk is not included in either the pagelist or in
	 * the tail.
	 */
	head->rc_arg.tail[0].iov_base =
		head->rc_arg.head[0].iov_base + info->ri_position;
	head->rc_arg.tail[0].iov_len =
		head->rc_arg.head[0].iov_len - info->ri_position;
	head->rc_arg.head[0].iov_len = info->ri_position;

	/* Read chunk may need XDR roundup (see RFC 8166, s. 3.4.5.2).
	 *
	 * If the client already rounded up the chunk length, the
	 * length does not change. Otherwise, the length of the page
	 * list is increased to include XDR round-up.
	 *
	 * Currently these chunks always start at page offset 0,
	 * thus the rounded-up length never crosses a page boundary.
	 */
	info->ri_chunklen = XDR_QUADLEN(info->ri_chunklen) << 2;

	head->rc_arg.page_len = info->ri_chunklen;
	head->rc_arg.len += info->ri_chunklen;
	head->rc_arg.buflen += info->ri_chunklen;

out:
	return ret;
}

/* Construct RDMA Reads to pull over a Position Zero Read chunk.
 * The start of the data lands in the first page just after
 * the Transport header, and the rest lands in the page list of
 * head->rc_arg.pages.
 *
 * Assumptions:
 *	- A PZRC has an XDR-aligned length (no implicit round-up).
 *	- There can be no trailing inline content (IOW, we assume
 *	  a PZRC is never sent in an RDMA_MSG message, though it's
 *	  allowed by spec).
 */
static int svc_rdma_build_pz_read_chunk(struct svc_rqst *rqstp,
					struct svc_rdma_read_info *info,
					__be32 *p)
{
	struct svc_rdma_recv_ctxt *head = info->ri_readctxt;
	int ret;

	ret = svc_rdma_build_read_chunk(rqstp, info, p);
	if (ret < 0)
		goto out;

	trace_svcrdma_send_pzr(info->ri_chunklen);

	head->rc_arg.len += info->ri_chunklen;
	head->rc_arg.buflen += info->ri_chunklen;

	head->rc_hdr_count = 1;
	head->rc_arg.head[0].iov_base = page_address(head->rc_pages[0]);
	head->rc_arg.head[0].iov_len = min_t(size_t, PAGE_SIZE,
					     info->ri_chunklen);

	head->rc_arg.page_len = info->ri_chunklen -
				head->rc_arg.head[0].iov_len;

out:
	return ret;
}

/* Pages under I/O have been copied to head->rc_pages. Ensure they
 * are not released by svc_xprt_release() until the I/O is complete.
 *
 * This has to be done after all Read WRs are constructed to properly
 * handle a page that is part of I/O on behalf of two different RDMA
 * segments.
 *
 * Do this only if I/O has been posted. Otherwise, we do indeed want
 * svc_xprt_release() to clean things up properly.
 */
static void svc_rdma_save_io_pages(struct svc_rqst *rqstp,
				   const unsigned int start,
				   const unsigned int num_pages)
{
	unsigned int i;

	for (i = start; i < num_pages + start; i++)
		rqstp->rq_pages[i] = NULL;
}

/**
 * svc_rdma_recv_read_chunk - Pull a Read chunk from the client
 * @rdma: controlling RDMA transport
 * @rqstp: set of pages to use as Read sink buffers
 * @head: pages under I/O collect here
 * @p: pointer to start of Read chunk
 *
 * Returns:
 *	%0 if all needed RDMA Reads were posted successfully,
 *	%-EINVAL if client provided too many segments,
 *	%-ENOMEM if rdma_rw context pool was exhausted,
 *	%-ENOTCONN if posting failed (connection is lost),
 *	%-EIO if rdma_rw initialization failed (DMA mapping, etc).
 *
 * Assumptions:
 * - All Read segments in @p have the same Position value.
 */
int svc_rdma_recv_read_chunk(struct svcxprt_rdma *rdma, struct svc_rqst *rqstp,
			     struct svc_rdma_recv_ctxt *head, __be32 *p)
{
	struct svc_rdma_read_info *info;
	int ret;

	/* The request (with page list) is constructed in
	 * head->rc_arg. Pages involved with RDMA Read I/O are
	 * transferred there.
	 */
	head->rc_arg.head[0] = rqstp->rq_arg.head[0];
	head->rc_arg.tail[0] = rqstp->rq_arg.tail[0];
	head->rc_arg.pages = head->rc_pages;
	head->rc_arg.page_base = 0;
	head->rc_arg.page_len = 0;
	head->rc_arg.len = rqstp->rq_arg.len;
	head->rc_arg.buflen = rqstp->rq_arg.buflen;

	info = svc_rdma_read_info_alloc(rdma);
	if (!info)
		return -ENOMEM;
	info->ri_readctxt = head;
	info->ri_pageno = 0;
	info->ri_pageoff = 0;

	info->ri_position = be32_to_cpup(p + 1);
	if (info->ri_position)
		ret = svc_rdma_build_normal_read_chunk(rqstp, info, p);
	else
		ret = svc_rdma_build_pz_read_chunk(rqstp, info, p);
	if (ret < 0)
		goto out_err;

	ret = svc_rdma_post_chunk_ctxt(&info->ri_cc);
	if (ret < 0)
		goto out_err;
	svc_rdma_save_io_pages(rqstp, 0, head->rc_page_count);
	return 0;

out_err:
	svc_rdma_read_info_free(info);
	return ret;
}
