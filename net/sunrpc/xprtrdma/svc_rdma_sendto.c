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
 * - pages[] is not physically or virtually contiguous and consists of
 *   PAGE_SIZE elements.
 *
 * Output:
 * SGE[0]              reserved for RCPRDMA header
 * SGE[1]              data from xdr->head[]
 * SGE[2..sge_count-2] data from xdr->pages[]
 * SGE[sge_count-1]    data from xdr->tail.
 *
 * The max SGE we need is the length of the XDR / pagesize + one for
 * head + one for tail + one for RPCRDMA header. Since RPCSVC_MAXPAGES
 * reserves a page for both the request and the reply header, and this
 * array is only concerned with the reply we are assured that we have
 * on extra page for the RPCRMDA header.
 */
static int fast_reg_xdr(struct svcxprt_rdma *xprt,
		 struct xdr_buf *xdr,
		 struct svc_rdma_req_map *vec)
{
	int sge_no;
	u32 sge_bytes;
	u32 page_bytes;
	u32 page_off;
	int page_no = 0;
	u8 *frva;
	struct svc_rdma_fastreg_mr *frmr;

	frmr = svc_rdma_get_frmr(xprt);
	if (IS_ERR(frmr))
		return -ENOMEM;
	vec->frmr = frmr;

	/* Skip the RPCRDMA header */
	sge_no = 1;

	/* Map the head. */
	frva = (void *)((unsigned long)(xdr->head[0].iov_base) & PAGE_MASK);
	vec->sge[sge_no].iov_base = xdr->head[0].iov_base;
	vec->sge[sge_no].iov_len = xdr->head[0].iov_len;
	vec->count = 2;
	sge_no++;

	/* Build the FRMR */
	frmr->kva = frva;
	frmr->direction = DMA_TO_DEVICE;
	frmr->access_flags = 0;
	frmr->map_len = PAGE_SIZE;
	frmr->page_list_len = 1;
	frmr->page_list->page_list[page_no] =
		ib_dma_map_single(xprt->sc_cm_id->device,
				  (void *)xdr->head[0].iov_base,
				  PAGE_SIZE, DMA_TO_DEVICE);
	if (ib_dma_mapping_error(xprt->sc_cm_id->device,
				 frmr->page_list->page_list[page_no]))
		goto fatal_err;
	atomic_inc(&xprt->sc_dma_used);

	page_off = xdr->page_base;
	page_bytes = xdr->page_len + page_off;
	if (!page_bytes)
		goto encode_tail;

	/* Map the pages */
	vec->sge[sge_no].iov_base = frva + frmr->map_len + page_off;
	vec->sge[sge_no].iov_len = page_bytes;
	sge_no++;
	while (page_bytes) {
		struct page *page;

		page = xdr->pages[page_no++];
		sge_bytes = min_t(u32, page_bytes, (PAGE_SIZE - page_off));
		page_bytes -= sge_bytes;

		frmr->page_list->page_list[page_no] =
			ib_dma_map_single(xprt->sc_cm_id->device,
					  page_address(page),
					  PAGE_SIZE, DMA_TO_DEVICE);
		if (ib_dma_mapping_error(xprt->sc_cm_id->device,
					 frmr->page_list->page_list[page_no]))
			goto fatal_err;

		atomic_inc(&xprt->sc_dma_used);
		page_off = 0; /* reset for next time through loop */
		frmr->map_len += PAGE_SIZE;
		frmr->page_list_len++;
	}
	vec->count++;

 encode_tail:
	/* Map tail */
	if (0 == xdr->tail[0].iov_len)
		goto done;

	vec->count++;
	vec->sge[sge_no].iov_len = xdr->tail[0].iov_len;

	if (((unsigned long)xdr->tail[0].iov_base & PAGE_MASK) ==
	    ((unsigned long)xdr->head[0].iov_base & PAGE_MASK)) {
		/*
		 * If head and tail use the same page, we don't need
		 * to map it again.
		 */
		vec->sge[sge_no].iov_base = xdr->tail[0].iov_base;
	} else {
		void *va;

		/* Map another page for the tail */
		page_off = (unsigned long)xdr->tail[0].iov_base & ~PAGE_MASK;
		va = (void *)((unsigned long)xdr->tail[0].iov_base & PAGE_MASK);
		vec->sge[sge_no].iov_base = frva + frmr->map_len + page_off;

		frmr->page_list->page_list[page_no] =
			ib_dma_map_single(xprt->sc_cm_id->device, va, PAGE_SIZE,
					  DMA_TO_DEVICE);
		if (ib_dma_mapping_error(xprt->sc_cm_id->device,
					 frmr->page_list->page_list[page_no]))
			goto fatal_err;
		atomic_inc(&xprt->sc_dma_used);
		frmr->map_len += PAGE_SIZE;
		frmr->page_list_len++;
	}

 done:
	if (svc_rdma_fastreg(xprt, frmr))
		goto fatal_err;

	return 0;

 fatal_err:
	printk("svcrdma: Error fast registering memory for xprt %p\n", xprt);
	vec->frmr = NULL;
	svc_rdma_put_frmr(xprt, frmr);
	return -EIO;
}

static int map_xdr(struct svcxprt_rdma *xprt,
		   struct xdr_buf *xdr,
		   struct svc_rdma_req_map *vec)
{
	int sge_no;
	u32 sge_bytes;
	u32 page_bytes;
	u32 page_off;
	int page_no;

	BUG_ON(xdr->len !=
	       (xdr->head[0].iov_len + xdr->page_len + xdr->tail[0].iov_len));

	if (xprt->sc_frmr_pg_list_len)
		return fast_reg_xdr(xprt, xdr, vec);

	/* Skip the first sge, this is for the RPCRDMA header */
	sge_no = 1;

	/* Head SGE */
	vec->sge[sge_no].iov_base = xdr->head[0].iov_base;
	vec->sge[sge_no].iov_len = xdr->head[0].iov_len;
	sge_no++;

	/* pages SGE */
	page_no = 0;
	page_bytes = xdr->page_len;
	page_off = xdr->page_base;
	while (page_bytes) {
		vec->sge[sge_no].iov_base =
			page_address(xdr->pages[page_no]) + page_off;
		sge_bytes = min_t(u32, page_bytes, (PAGE_SIZE - page_off));
		page_bytes -= sge_bytes;
		vec->sge[sge_no].iov_len = sge_bytes;

		sge_no++;
		page_no++;
		page_off = 0; /* reset for next time through loop */
	}

	/* Tail SGE */
	if (xdr->tail[0].iov_len) {
		vec->sge[sge_no].iov_base = xdr->tail[0].iov_base;
		vec->sge[sge_no].iov_len = xdr->tail[0].iov_len;
		sge_no++;
	}

	dprintk("svcrdma: map_xdr: sge_no %d page_no %d "
		"page_base %u page_len %u head_len %zu tail_len %zu\n",
		sge_no, page_no, xdr->page_base, xdr->page_len,
		xdr->head[0].iov_len, xdr->tail[0].iov_len);

	vec->count = sge_no;
	return 0;
}

/* Assumptions:
 * - We are using FRMR
 *     - or -
 * - The specified write_len can be represented in sc_max_sge * PAGE_SIZE
 */
static int send_write(struct svcxprt_rdma *xprt, struct svc_rqst *rqstp,
		      u32 rmr, u64 to,
		      u32 xdr_off, int write_len,
		      struct svc_rdma_req_map *vec)
{
	struct ib_send_wr write_wr;
	struct ib_sge *sge;
	int xdr_sge_no;
	int sge_no;
	int sge_bytes;
	int sge_off;
	int bc;
	struct svc_rdma_op_ctxt *ctxt;

	BUG_ON(vec->count > RPCSVC_MAXPAGES);
	dprintk("svcrdma: RDMA_WRITE rmr=%x, to=%llx, xdr_off=%d, "
		"write_len=%d, vec->sge=%p, vec->count=%lu\n",
		rmr, (unsigned long long)to, xdr_off,
		write_len, vec->sge, vec->count);

	ctxt = svc_rdma_get_context(xprt);
	ctxt->direction = DMA_TO_DEVICE;
	sge = ctxt->sge;

	/* Find the SGE associated with xdr_off */
	for (bc = xdr_off, xdr_sge_no = 1; bc && xdr_sge_no < vec->count;
	     xdr_sge_no++) {
		if (vec->sge[xdr_sge_no].iov_len > bc)
			break;
		bc -= vec->sge[xdr_sge_no].iov_len;
	}

	sge_off = bc;
	bc = write_len;
	sge_no = 0;

	/* Copy the remaining SGE */
	while (bc != 0) {
		sge_bytes = min_t(size_t,
			  bc, vec->sge[xdr_sge_no].iov_len-sge_off);
		sge[sge_no].length = sge_bytes;
		if (!vec->frmr) {
			sge[sge_no].addr =
				ib_dma_map_single(xprt->sc_cm_id->device,
						  (void *)
						  vec->sge[xdr_sge_no].iov_base + sge_off,
						  sge_bytes, DMA_TO_DEVICE);
			if (ib_dma_mapping_error(xprt->sc_cm_id->device,
						 sge[sge_no].addr))
				goto err;
			atomic_inc(&xprt->sc_dma_used);
			sge[sge_no].lkey = xprt->sc_dma_lkey;
		} else {
			sge[sge_no].addr = (unsigned long)
				vec->sge[xdr_sge_no].iov_base + sge_off;
			sge[sge_no].lkey = vec->frmr->mr->lkey;
		}
		ctxt->count++;
		ctxt->frmr = vec->frmr;
		sge_off = 0;
		sge_no++;
		xdr_sge_no++;
		BUG_ON(xdr_sge_no > vec->count);
		bc -= sge_bytes;
	}

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
	if (svc_rdma_send(xprt, &write_wr))
		goto err;
	return 0;
 err:
	svc_rdma_put_context(ctxt, 0);
	/* Fatal error, close transport */
	return -EIO;
}

static int send_write_chunks(struct svcxprt_rdma *xprt,
			     struct rpcrdma_msg *rdma_argp,
			     struct rpcrdma_msg *rdma_resp,
			     struct svc_rqst *rqstp,
			     struct svc_rdma_req_map *vec)
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

	if (vec->frmr)
		max_write = vec->frmr->map_len;
	else
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
					 vec);
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
			     struct svc_rdma_req_map *vec)
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

	if (vec->frmr)
		max_write = vec->frmr->map_len;
	else
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
					 vec);
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
		      struct svc_rdma_req_map *vec,
		      int byte_count)
{
	struct ib_send_wr send_wr;
	struct ib_send_wr inv_wr;
	int sge_no;
	int sge_bytes;
	int page_no;
	int ret;

	/* Post a recv buffer to handle another request. */
	ret = svc_rdma_post_recv(rdma);
	if (ret) {
		printk(KERN_INFO
		       "svcrdma: could not post a receive buffer, err=%d."
		       "Closing transport %p.\n", ret, rdma);
		set_bit(XPT_CLOSE, &rdma->sc_xprt.xpt_flags);
		svc_rdma_put_frmr(rdma, vec->frmr);
		svc_rdma_put_context(ctxt, 0);
		return -ENOTCONN;
	}

	/* Prepare the context */
	ctxt->pages[0] = page;
	ctxt->count = 1;
	ctxt->frmr = vec->frmr;
	if (vec->frmr)
		set_bit(RDMACTXT_F_FAST_UNREG, &ctxt->flags);
	else
		clear_bit(RDMACTXT_F_FAST_UNREG, &ctxt->flags);

	/* Prepare the SGE for the RPCRDMA Header */
	ctxt->sge[0].lkey = rdma->sc_dma_lkey;
	ctxt->sge[0].length = svc_rdma_xdr_get_reply_hdr_len(rdma_resp);
	ctxt->sge[0].addr =
		ib_dma_map_single(rdma->sc_cm_id->device, page_address(page),
				  ctxt->sge[0].length, DMA_TO_DEVICE);
	if (ib_dma_mapping_error(rdma->sc_cm_id->device, ctxt->sge[0].addr))
		goto err;
	atomic_inc(&rdma->sc_dma_used);

	ctxt->direction = DMA_TO_DEVICE;

	/* Determine how many of our SGE are to be transmitted */
	for (sge_no = 1; byte_count && sge_no < vec->count; sge_no++) {
		sge_bytes = min_t(size_t, vec->sge[sge_no].iov_len, byte_count);
		byte_count -= sge_bytes;
		if (!vec->frmr) {
			ctxt->sge[sge_no].addr =
				ib_dma_map_single(rdma->sc_cm_id->device,
						  vec->sge[sge_no].iov_base,
						  sge_bytes, DMA_TO_DEVICE);
			if (ib_dma_mapping_error(rdma->sc_cm_id->device,
						 ctxt->sge[sge_no].addr))
				goto err;
			atomic_inc(&rdma->sc_dma_used);
			ctxt->sge[sge_no].lkey = rdma->sc_dma_lkey;
		} else {
			ctxt->sge[sge_no].addr = (unsigned long)
				vec->sge[sge_no].iov_base;
			ctxt->sge[sge_no].lkey = vec->frmr->mr->lkey;
		}
		ctxt->sge[sge_no].length = sge_bytes;
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
		/*
		 * If there are more pages than SGE, terminate SGE
		 * list so that svc_rdma_unmap_dma doesn't attempt to
		 * unmap garbage.
		 */
		if (page_no+1 >= sge_no)
			ctxt->sge[page_no+1].length = 0;
	}
	BUG_ON(sge_no > rdma->sc_max_sge);
	memset(&send_wr, 0, sizeof send_wr);
	ctxt->wr_op = IB_WR_SEND;
	send_wr.wr_id = (unsigned long)ctxt;
	send_wr.sg_list = ctxt->sge;
	send_wr.num_sge = sge_no;
	send_wr.opcode = IB_WR_SEND;
	send_wr.send_flags =  IB_SEND_SIGNALED;
	if (vec->frmr) {
		/* Prepare INVALIDATE WR */
		memset(&inv_wr, 0, sizeof inv_wr);
		inv_wr.opcode = IB_WR_LOCAL_INV;
		inv_wr.send_flags = IB_SEND_SIGNALED;
		inv_wr.ex.invalidate_rkey =
			vec->frmr->mr->lkey;
		send_wr.next = &inv_wr;
	}

	ret = svc_rdma_send(rdma, &send_wr);
	if (ret)
		goto err;

	return 0;

 err:
	svc_rdma_unmap_dma(ctxt);
	svc_rdma_put_frmr(rdma, vec->frmr);
	svc_rdma_put_context(ctxt, 1);
	return -EIO;
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
	struct page *res_page;
	struct svc_rdma_op_ctxt *ctxt;
	struct svc_rdma_req_map *vec;

	dprintk("svcrdma: sending response for rqstp=%p\n", rqstp);

	/* Get the RDMA request header. */
	rdma_argp = xdr_start(&rqstp->rq_arg);

	/* Build an req vec for the XDR */
	ctxt = svc_rdma_get_context(rdma);
	ctxt->direction = DMA_TO_DEVICE;
	vec = svc_rdma_get_req_map();
	ret = map_xdr(rdma, &rqstp->rq_res, vec);
	if (ret)
		goto err0;
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
				rqstp, vec);
	if (ret < 0) {
		printk(KERN_ERR "svcrdma: failed to send write chunks, rc=%d\n",
		       ret);
		goto err1;
	}
	inline_bytes -= ret;

	/* Send any reply-list data and update resp reply-list */
	ret = send_reply_chunks(rdma, rdma_argp, rdma_resp,
				rqstp, vec);
	if (ret < 0) {
		printk(KERN_ERR "svcrdma: failed to send reply chunks, rc=%d\n",
		       ret);
		goto err1;
	}
	inline_bytes -= ret;

	ret = send_reply(rdma, rqstp, res_page, rdma_resp, ctxt, vec,
			 inline_bytes);
	svc_rdma_put_req_map(vec);
	dprintk("svcrdma: send_reply returns %d\n", ret);
	return ret;

 err1:
	put_page(res_page);
 err0:
	svc_rdma_put_req_map(vec);
	svc_rdma_put_context(ctxt, 0);
	return ret;
}
