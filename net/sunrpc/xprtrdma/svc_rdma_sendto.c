/*
 * Copyright (c) 2005-2006 Network Appliance, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of the Network Appliance, Inc. nor the names of
 *      its contributors may be used to endorse or promote products
 *      derived from this software without specific prior written
 *      permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Tom Tucker <tom@opengridcomputing.com>
 */

#include <linux/sunrpc/debug.h>
#include <linux/sunrpc/rpc_rdma.h>
#include <linux/spinlock.h>
#include <asm/unaligned.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <linux/sunrpc/svc_rdma.h>

#define RPCDBG_FACILITY	RPCDBG_SVCXPRT

/* Encode an XDR as an array of IB SGE
 *
 * Assumptions:
 * - head[0] is physically contiguous.
 * - tail[0] is physically contiguous.
 * - pages[] is not physically or virtually contigous and consists of
 *   PAGE_SIZE elements.
 *
 * Output:
 * SGE[0]              reserved for RCPRDMA header
 * SGE[1]              data from xdr->head[]
 * SGE[2..sge_count-2] data from xdr->pages[]
 * SGE[sge_count-1]    data from xdr->tail.
 *
 */
static struct ib_sge *xdr_to_sge(struct svcxprt_rdma *xprt,
				 struct xdr_buf *xdr,
				 struct ib_sge *sge,
				 int *sge_count)
{
	/* Max we need is the length of the XDR / pagesize + one for
	 * head + one for tail + one for RPCRDMA header
	 */
	int sge_max = (xdr->len+PAGE_SIZE-1) / PAGE_SIZE + 3;
	int sge_no;
	u32 byte_count = xdr->len;
	u32 sge_bytes;
	u32 page_bytes;
	int page_off;
	int page_no;

	/* Skip the first sge, this is for the RPCRDMA header */
	sge_no = 1;

	/* Head SGE */
	sge[sge_no].addr = ib_dma_map_single(xprt->sc_cm_id->device,
					     xdr->head[0].iov_base,
					     xdr->head[0].iov_len,
					     DMA_TO_DEVICE);
	sge_bytes = min_t(u32, byte_count, xdr->head[0].iov_len);
	byte_count -= sge_bytes;
	sge[sge_no].length = sge_bytes;
	sge[sge_no].lkey = xprt->sc_phys_mr->lkey;
	sge_no++;

	/* pages SGE */
	page_no = 0;
	page_bytes = xdr->page_len;
	page_off = xdr->page_base;
	while (byte_count && page_bytes) {
		sge_bytes = min_t(u32, byte_count, (PAGE_SIZE-page_off));
		sge[sge_no].addr =
			ib_dma_map_page(xprt->sc_cm_id->device,
					xdr->pages[page_no], page_off,
					sge_bytes, DMA_TO_DEVICE);
		sge_bytes = min(sge_bytes, page_bytes);
		byte_count -= sge_bytes;
		page_bytes -= sge_bytes;
		sge[sge_no].length = sge_bytes;
		sge[sge_no].lkey = xprt->sc_phys_mr->lkey;

		sge_no++;
		page_no++;
		page_off = 0; /* reset for next time through loop */
	}

	/* Tail SGE */
	if (byte_count && xdr->tail[0].iov_len) {
		sge[sge_no].addr =
			ib_dma_map_single(xprt->sc_cm_id->device,
					  xdr->tail[0].iov_base,
					  xdr->tail[0].iov_len,
					  DMA_TO_DEVICE);
		sge_bytes = min_t(u32, byte_count, xdr->tail[0].iov_len);
		byte_count -= sge_bytes;
		sge[sge_no].length = sge_bytes;
		sge[sge_no].lkey = xprt->sc_phys_mr->lkey;
		sge_no++;
	}

	BUG_ON(sge_no > sge_max);
	BUG_ON(byte_count != 0);

	*sge_count = sge_no;
	return sge;
}


/* Assumptions:
 * - The specified write_len can be represented in sc_max_sge * PAGE_SIZE
 */
static int send_write(struct svcxprt_rdma *xprt, struct svc_rqst *rqstp,
		      u32 rmr, u64 to,
		      u32 xdr_off, int write_len,
		      struct ib_sge *xdr_sge, int sge_count)
{
	struct svc_rdma_op_ctxt *tmp_sge_ctxt;
	struct ib_send_wr write_wr;
	struct ib_sge *sge;
	int xdr_sge_no;
	int sge_no;
	int sge_bytes;
	int sge_off;
	int bc;
	struct svc_rdma_op_ctxt *ctxt;
	int ret = 0;

	BUG_ON(sge_count > RPCSVC_MAXPAGES);
	dprintk("svcrdma: RDMA_WRITE rmr=%x, to=%llx, xdr_off=%d, "
		"write_len=%d, xdr_sge=%p, sge_count=%d\n",
		rmr, (unsigned long long)to, xdr_off,
		write_len, xdr_sge, sge_count);

	ctxt = svc_rdma_get_context(xprt);
	ctxt->count = 0;
	tmp_sge_ctxt = svc_rdma_get_context(xprt);
	sge = tmp_sge_ctxt->sge;

	/* Find the SGE associated with xdr_off */
	for (bc = xdr_off, xdr_sge_no = 1; bc && xdr_sge_no < sge_count;
	     xdr_sge_no++) {
		if (xdr_sge[xdr_sge_no].length > bc)
			break;
		bc -= xdr_sge[xdr_sge_no].length;
	}

	sge_off = bc;
	bc = write_len;
	sge_no = 0;

	/* Copy the remaining SGE */
	while (bc != 0 && xdr_sge_no < sge_count) {
		sge[sge_no].addr = xdr_sge[xdr_sge_no].addr + sge_off;
		sge[sge_no].lkey = xdr_sge[xdr_sge_no].lkey;
		sge_bytes = min((size_t)bc,
				(size_t)(xdr_sge[xdr_sge_no].length-sge_off));
		sge[sge_no].length = sge_bytes;

		sge_off = 0;
		sge_no++;
		xdr_sge_no++;
		bc -= sge_bytes;
	}

	BUG_ON(bc != 0);
	BUG_ON(xdr_sge_no > sge_count);

	/* Prepare WRITE WR */
	memset(&write_wr, 0, sizeof write_wr);
	ctxt->wr_op = IB_WR_RDMA_WRITE;
	write_wr.wr_id = (unsigned long)ctxt;
	write_wr.sg_list = &sge[0];
	write_wr.num_sge = sge_no;
	write_wr.opcode = IB_WR_RDMA_WRITE;
	write_wr.send_flags = IB_SEND_SIGNALED;
	write_wr.wr.rdma.rkey = rmr;
	write_wr.wr.rdma.remote_addr = to;

	/* Post It */
	atomic_inc(&rdma_stat_write);
	if (svc_rdma_send(xprt, &write_wr)) {
		svc_rdma_put_context(ctxt, 1);
		/* Fatal error, close transport */
		ret = -EIO;
	}
	svc_rdma_put_context(tmp_sge_ctxt, 0);
	return ret;
}

static int send_write_chunks(struct svcxprt_rdma *xprt,
			     struct rpcrdma_msg *rdma_argp,
			     struct rpcrdma_msg *rdma_resp,
			     struct svc_rqst *rqstp,
			     struct ib_sge *sge,
			     int sge_count)
{
	u32 xfer_len = rqstp->rq_res.page_len + rqstp->rq_res.tail[0].iov_len;
	int write_len;
	int max_write;
	u32 xdr_off;
	int chunk_off;
	int chunk_no;
	struct rpcrdma_write_array *arg_ary;
	struct rpcrdma_write_array *res_ary;
	int ret;

	arg_ary = svc_rdma_get_write_array(rdma_argp);
	if (!arg_ary)
		return 0;
	res_ary = (struct rpcrdma_write_array *)
		&rdma_resp->rm_body.rm_chunks[1];

	max_write = xprt->sc_max_sge * PAGE_SIZE;

	/* Write chunks start at the pagelist */
	for (xdr_off = rqstp->rq_res.head[0].iov_len, chunk_no = 0;
	     xfer_len && chunk_no < arg_ary->wc_nchunks;
	     chunk_no++) {
		struct rpcrdma_segment *arg_ch;
		u64 rs_offset;

		arg_ch = &arg_ary->wc_array[chunk_no].wc_target;
		write_len = min(xfer_len, arg_ch->rs_length);

		/* Prepare the response chunk given the length actually
		 * written */
		rs_offset = get_unaligned(&(arg_ch->rs_offset));
		svc_rdma_xdr_encode_array_chunk(res_ary, chunk_no,
					    arg_ch->rs_handle,
					    rs_offset,
					    write_len);
		chunk_off = 0;
		while (write_len) {
			int this_write;
			this_write = min(write_len, max_write);
			ret = send_write(xprt, rqstp,
					 arg_ch->rs_handle,
					 rs_offset + chunk_off,
					 xdr_off,
					 this_write,
					 sge,
					 sge_count);
			if (ret) {
				dprintk("svcrdma: RDMA_WRITE failed, ret=%d\n",
					ret);
				return -EIO;
			}
			chunk_off += this_write;
			xdr_off += this_write;
			xfer_len -= this_write;
			write_len -= this_write;
		}
	}
	/* Update the req with the number of chunks actually used */
	svc_rdma_xdr_encode_write_list(rdma_resp, chunk_no);

	return rqstp->rq_res.page_len + rqstp->rq_res.tail[0].iov_len;
}

static int send_reply_chunks(struct svcxprt_rdma *xprt,
			     struct rpcrdma_msg *rdma_argp,
			     struct rpcrdma_msg *rdma_resp,
			     struct svc_rqst *rqstp,
			     struct ib_sge *sge,
			     int sge_count)
{
	u32 xfer_len = rqstp->rq_res.len;
	int write_len;
	int max_write;
	u32 xdr_off;
	int chunk_no;
	int chunk_off;
	struct rpcrdma_segment *ch;
	struct rpcrdma_write_array *arg_ary;
	struct rpcrdma_write_array *res_ary;
	int ret;

	arg_ary = svc_rdma_get_reply_array(rdma_argp);
	if (!arg_ary)
		return 0;
	/* XXX: need to fix when reply lists occur with read-list and or
	 * write-list */
	res_ary = (struct rpcrdma_write_array *)
		&rdma_resp->rm_body.rm_chunks[2];

	max_write = xprt->sc_max_sge * PAGE_SIZE;

	/* xdr offset starts at RPC message */
	for (xdr_off = 0, chunk_no = 0;
	     xfer_len && chunk_no < arg_ary->wc_nchunks;
	     chunk_no++) {
		u64 rs_offset;
		ch = &arg_ary->wc_array[chunk_no].wc_target;
		write_len = min(xfer_len, ch->rs_length);


		/* Prepare the reply chunk given the length actually
		 * written */
		rs_offset = get_unaligned(&(ch->rs_offset));
		svc_rdma_xdr_encode_array_chunk(res_ary, chunk_no,
					    ch->rs_handle, rs_offset,
					    write_len);
		chunk_off = 0;
		while (write_len) {
			int this_write;

			this_write = min(write_len, max_write);
			ret = send_write(xprt, rqstp,
					 ch->rs_handle,
					 rs_offset + chunk_off,
					 xdr_off,
					 this_write,
					 sge,
					 sge_count);
			if (ret) {
				dprintk("svcrdma: RDMA_WRITE failed, ret=%d\n",
					ret);
				return -EIO;
			}
			chunk_off += this_write;
			xdr_off += this_write;
			xfer_len -= this_write;
			write_len -= this_write;
		}
	}
	/* Update the req with the number of chunks actually used */
	svc_rdma_xdr_encode_reply_array(res_ary, chunk_no);

	return rqstp->rq_res.len;
}

/* This function prepares the portion of the RPCRDMA message to be
 * sent in the RDMA_SEND. This function is called after data sent via
 * RDMA has already been transmitted. There are three cases:
 * - The RPCRDMA header, RPC header, and payload are all sent in a
 *   single RDMA_SEND. This is the "inline" case.
 * - The RPCRDMA header and some portion of the RPC header and data
 *   are sent via this RDMA_SEND and another portion of the data is
 *   sent via RDMA.
 * - The RPCRDMA header [NOMSG] is sent in this RDMA_SEND and the RPC
 *   header and data are all transmitted via RDMA.
 * In all three cases, this function prepares the RPCRDMA header in
 * sge[0], the 'type' parameter indicates the type to place in the
 * RPCRDMA header, and the 'byte_count' field indicates how much of
 * the XDR to include in this RDMA_SEND.
 */
static int send_reply(struct svcxprt_rdma *rdma,
		      struct svc_rqst *rqstp,
		      struct page *page,
		      struct rpcrdma_msg *rdma_resp,
		      struct svc_rdma_op_ctxt *ctxt,
		      int sge_count,
		      int byte_count)
{
	struct ib_send_wr send_wr;
	int sge_no;
	int sge_bytes;
	int page_no;
	int ret;

	/* Prepare the context */
	ctxt->pages[0] = page;
	ctxt->count = 1;

	/* Prepare the SGE for the RPCRDMA Header */
	ctxt->sge[0].addr =
		ib_dma_map_page(rdma->sc_cm_id->device,
				page, 0, PAGE_SIZE, DMA_TO_DEVICE);
	ctxt->direction = DMA_TO_DEVICE;
	ctxt->sge[0].length = svc_rdma_xdr_get_reply_hdr_len(rdma_resp);
	ctxt->sge[0].lkey = rdma->sc_phys_mr->lkey;

	/* Determine how many of our SGE are to be transmitted */
	for (sge_no = 1; byte_count && sge_no < sge_count; sge_no++) {
		sge_bytes = min((size_t)ctxt->sge[sge_no].length,
				(size_t)byte_count);
		byte_count -= sge_bytes;
	}
	BUG_ON(byte_count != 0);

	/* Save all respages in the ctxt and remove them from the
	 * respages array. They are our pages until the I/O
	 * completes.
	 */
	for (page_no = 0; page_no < rqstp->rq_resused; page_no++) {
		ctxt->pages[page_no+1] = rqstp->rq_respages[page_no];
		ctxt->count++;
		rqstp->rq_respages[page_no] = NULL;
	}

	BUG_ON(sge_no > rdma->sc_max_sge);
	memset(&send_wr, 0, sizeof send_wr);
	ctxt->wr_op = IB_WR_SEND;
	send_wr.wr_id = (unsigned long)ctxt;
	send_wr.sg_list = ctxt->sge;
	send_wr.num_sge = sge_no;
	send_wr.opcode = IB_WR_SEND;
	send_wr.send_flags =  IB_SEND_SIGNALED;

	ret = svc_rdma_send(rdma, &send_wr);
	if (ret)
		svc_rdma_put_context(ctxt, 1);

	return ret;
}

void svc_rdma_prep_reply_hdr(struct svc_rqst *rqstp)
{
}

/*
 * Return the start of an xdr buffer.
 */
static void *xdr_start(struct xdr_buf *xdr)
{
	return xdr->head[0].iov_base -
		(xdr->len -
		 xdr->page_len -
		 xdr->tail[0].iov_len -
		 xdr->head[0].iov_len);
}

int svc_rdma_sendto(struct svc_rqst *rqstp)
{
	struct svc_xprt *xprt = rqstp->rq_xprt;
	struct svcxprt_rdma *rdma =
		container_of(xprt, struct svcxprt_rdma, sc_xprt);
	struct rpcrdma_msg *rdma_argp;
	struct rpcrdma_msg *rdma_resp;
	struct rpcrdma_write_array *reply_ary;
	enum rpcrdma_proc reply_type;
	int ret;
	int inline_bytes;
	struct ib_sge *sge;
	int sge_count = 0;
	struct page *res_page;
	struct svc_rdma_op_ctxt *ctxt;

	dprintk("svcrdma: sending response for rqstp=%p\n", rqstp);

	/* Get the RDMA request header. */
	rdma_argp = xdr_start(&rqstp->rq_arg);

	/* Build an SGE for the XDR */
	ctxt = svc_rdma_get_context(rdma);
	ctxt->direction = DMA_TO_DEVICE;
	sge = xdr_to_sge(rdma, &rqstp->rq_res, ctxt->sge, &sge_count);

	inline_bytes = rqstp->rq_res.len;

	/* Create the RDMA response header */
	res_page = svc_rdma_get_page();
	rdma_resp = page_address(res_page);
	reply_ary = svc_rdma_get_reply_array(rdma_argp);
	if (reply_ary)
		reply_type = RDMA_NOMSG;
	else
		reply_type = RDMA_MSG;
	svc_rdma_xdr_encode_reply_header(rdma, rdma_argp,
					 rdma_resp, reply_type);

	/* Send any write-chunk data and build resp write-list */
	ret = send_write_chunks(rdma, rdma_argp, rdma_resp,
				rqstp, sge, sge_count);
	if (ret < 0) {
		printk(KERN_ERR "svcrdma: failed to send write chunks, rc=%d\n",
		       ret);
		goto error;
	}
	inline_bytes -= ret;

	/* Send any reply-list data and update resp reply-list */
	ret = send_reply_chunks(rdma, rdma_argp, rdma_resp,
				rqstp, sge, sge_count);
	if (ret < 0) {
		printk(KERN_ERR "svcrdma: failed to send reply chunks, rc=%d\n",
		       ret);
		goto error;
	}
	inline_bytes -= ret;

	ret = send_reply(rdma, rqstp, res_page, rdma_resp, ctxt, sge_count,
			 inline_bytes);
	dprintk("svcrdma: send_reply returns %d\n", ret);
	return ret;
 error:
	svc_rdma_put_context(ctxt, 0);
	put_page(res_page);
	return ret;
}
