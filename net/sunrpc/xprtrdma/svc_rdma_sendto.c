/*
 * Copyright (c) 2014 Open Grid Computing, Inc. All rights reserved.
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

int svc_rdma_map_xdr(struct svcxprt_rdma *xprt,
		     struct xdr_buf *xdr,
		     struct svc_rdma_req_map *vec)
{
	int sge_no;
	u32 sge_bytes;
	u32 page_bytes;
	u32 page_off;
	int page_no;

	if (xdr->len !=
	    (xdr->head[0].iov_len + xdr->page_len + xdr->tail[0].iov_len)) {
		pr_err("svcrdma: %s: XDR buffer length error\n", __func__);
		return -EIO;
	}

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

	dprintk("svcrdma: %s: sge_no %d page_no %d "
		"page_base %u page_len %u head_len %zu tail_len %zu\n",
		__func__, sge_no, page_no, xdr->page_base, xdr->page_len,
		xdr->head[0].iov_len, xdr->tail[0].iov_len);

	vec->count = sge_no;
	return 0;
}

static dma_addr_t dma_map_xdr(struct svcxprt_rdma *xprt,
			      struct xdr_buf *xdr,
			      u32 xdr_off, size_t len, int dir)
{
	struct page *page;
	dma_addr_t dma_addr;
	if (xdr_off < xdr->head[0].iov_len) {
		/* This offset is in the head */
		xdr_off += (unsigned long)xdr->head[0].iov_base & ~PAGE_MASK;
		page = virt_to_page(xdr->head[0].iov_base);
	} else {
		xdr_off -= xdr->head[0].iov_len;
		if (xdr_off < xdr->page_len) {
			/* This offset is in the page list */
			xdr_off += xdr->page_base;
			page = xdr->pages[xdr_off >> PAGE_SHIFT];
			xdr_off &= ~PAGE_MASK;
		} else {
			/* This offset is in the tail */
			xdr_off -= xdr->page_len;
			xdr_off += (unsigned long)
				xdr->tail[0].iov_base & ~PAGE_MASK;
			page = virt_to_page(xdr->tail[0].iov_base);
		}
	}
	dma_addr = ib_dma_map_page(xprt->sc_cm_id->device, page, xdr_off,
				   min_t(size_t, PAGE_SIZE, len), dir);
	return dma_addr;
}

/* Returns the address of the first read chunk or <nul> if no read chunk
 * is present
 */
struct rpcrdma_read_chunk *
svc_rdma_get_read_chunk(struct rpcrdma_msg *rmsgp)
{
	struct rpcrdma_read_chunk *ch =
		(struct rpcrdma_read_chunk *)&rmsgp->rm_body.rm_chunks[0];

	if (ch->rc_discrim == xdr_zero)
		return NULL;
	return ch;
}

/* Returns the address of the first read write array element or <nul>
 * if no write array list is present
 */
static struct rpcrdma_write_array *
svc_rdma_get_write_array(struct rpcrdma_msg *rmsgp)
{
	if (rmsgp->rm_body.rm_chunks[0] != xdr_zero ||
	    rmsgp->rm_body.rm_chunks[1] == xdr_zero)
		return NULL;
	return (struct rpcrdma_write_array *)&rmsgp->rm_body.rm_chunks[1];
}

/* Returns the address of the first reply array element or <nul> if no
 * reply array is present
 */
static struct rpcrdma_write_array *
svc_rdma_get_reply_array(struct rpcrdma_msg *rmsgp)
{
	struct rpcrdma_read_chunk *rch;
	struct rpcrdma_write_array *wr_ary;
	struct rpcrdma_write_array *rp_ary;

	/* XXX: Need to fix when reply chunk may occur with read list
	 *	and/or write list.
	 */
	if (rmsgp->rm_body.rm_chunks[0] != xdr_zero ||
	    rmsgp->rm_body.rm_chunks[1] != xdr_zero)
		return NULL;

	rch = svc_rdma_get_read_chunk(rmsgp);
	if (rch) {
		while (rch->rc_discrim != xdr_zero)
			rch++;

		/* The reply chunk follows an empty write array located
		 * at 'rc_position' here. The reply array is at rc_target.
		 */
		rp_ary = (struct rpcrdma_write_array *)&rch->rc_target;
		goto found_it;
	}

	wr_ary = svc_rdma_get_write_array(rmsgp);
	if (wr_ary) {
		int chunk = be32_to_cpu(wr_ary->wc_nchunks);

		rp_ary = (struct rpcrdma_write_array *)
			 &wr_ary->wc_array[chunk].wc_target.rs_length;
		goto found_it;
	}

	/* No read list, no write list */
	rp_ary = (struct rpcrdma_write_array *)&rmsgp->rm_body.rm_chunks[2];

 found_it:
	if (rp_ary->wc_discrim == xdr_zero)
		return NULL;
	return rp_ary;
}

/* Assumptions:
 * - The specified write_len can be represented in sc_max_sge * PAGE_SIZE
 */
static int send_write(struct svcxprt_rdma *xprt, struct svc_rqst *rqstp,
		      u32 rmr, u64 to,
		      u32 xdr_off, int write_len,
		      struct svc_rdma_req_map *vec)
{
	struct ib_rdma_wr write_wr;
	struct ib_sge *sge;
	int xdr_sge_no;
	int sge_no;
	int sge_bytes;
	int sge_off;
	int bc;
	struct svc_rdma_op_ctxt *ctxt;

	if (vec->count > RPCSVC_MAXPAGES) {
		pr_err("svcrdma: Too many pages (%lu)\n", vec->count);
		return -EIO;
	}

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
		sge[sge_no].addr =
			dma_map_xdr(xprt, &rqstp->rq_res, xdr_off,
				    sge_bytes, DMA_TO_DEVICE);
		xdr_off += sge_bytes;
		if (ib_dma_mapping_error(xprt->sc_cm_id->device,
					 sge[sge_no].addr))
			goto err;
		atomic_inc(&xprt->sc_dma_used);
		sge[sge_no].lkey = xprt->sc_pd->local_dma_lkey;
		ctxt->count++;
		sge_off = 0;
		sge_no++;
		xdr_sge_no++;
		if (xdr_sge_no > vec->count) {
			pr_err("svcrdma: Too many sges (%d)\n", xdr_sge_no);
			goto err;
		}
		bc -= sge_bytes;
		if (sge_no == xprt->sc_max_sge)
			break;
	}

	/* Prepare WRITE WR */
	memset(&write_wr, 0, sizeof write_wr);
	ctxt->wr_op = IB_WR_RDMA_WRITE;
	write_wr.wr.wr_id = (unsigned long)ctxt;
	write_wr.wr.sg_list = &sge[0];
	write_wr.wr.num_sge = sge_no;
	write_wr.wr.opcode = IB_WR_RDMA_WRITE;
	write_wr.wr.send_flags = IB_SEND_SIGNALED;
	write_wr.rkey = rmr;
	write_wr.remote_addr = to;

	/* Post It */
	atomic_inc(&rdma_stat_write);
	if (svc_rdma_send(xprt, &write_wr.wr))
		goto err;
	return write_len - bc;
 err:
	svc_rdma_unmap_dma(ctxt);
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
	u32 xdr_off;
	int chunk_off;
	int chunk_no;
	int nchunks;
	struct rpcrdma_write_array *arg_ary;
	struct rpcrdma_write_array *res_ary;
	int ret;

	arg_ary = svc_rdma_get_write_array(rdma_argp);
	if (!arg_ary)
		return 0;
	res_ary = (struct rpcrdma_write_array *)
		&rdma_resp->rm_body.rm_chunks[1];

	/* Write chunks start at the pagelist */
	nchunks = be32_to_cpu(arg_ary->wc_nchunks);
	for (xdr_off = rqstp->rq_res.head[0].iov_len, chunk_no = 0;
	     xfer_len && chunk_no < nchunks;
	     chunk_no++) {
		struct rpcrdma_segment *arg_ch;
		u64 rs_offset;

		arg_ch = &arg_ary->wc_array[chunk_no].wc_target;
		write_len = min(xfer_len, be32_to_cpu(arg_ch->rs_length));

		/* Prepare the response chunk given the length actually
		 * written */
		xdr_decode_hyper((__be32 *)&arg_ch->rs_offset, &rs_offset);
		svc_rdma_xdr_encode_array_chunk(res_ary, chunk_no,
						arg_ch->rs_handle,
						arg_ch->rs_offset,
						write_len);
		chunk_off = 0;
		while (write_len) {
			ret = send_write(xprt, rqstp,
					 be32_to_cpu(arg_ch->rs_handle),
					 rs_offset + chunk_off,
					 xdr_off,
					 write_len,
					 vec);
			if (ret <= 0) {
				dprintk("svcrdma: RDMA_WRITE failed, ret=%d\n",
					ret);
				return -EIO;
			}
			chunk_off += ret;
			xdr_off += ret;
			xfer_len -= ret;
			write_len -= ret;
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
	u32 xdr_off;
	int chunk_no;
	int chunk_off;
	int nchunks;
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

	/* xdr offset starts at RPC message */
	nchunks = be32_to_cpu(arg_ary->wc_nchunks);
	for (xdr_off = 0, chunk_no = 0;
	     xfer_len && chunk_no < nchunks;
	     chunk_no++) {
		u64 rs_offset;
		ch = &arg_ary->wc_array[chunk_no].wc_target;
		write_len = min(xfer_len, be32_to_cpu(ch->rs_length));

		/* Prepare the reply chunk given the length actually
		 * written */
		xdr_decode_hyper((__be32 *)&ch->rs_offset, &rs_offset);
		svc_rdma_xdr_encode_array_chunk(res_ary, chunk_no,
						ch->rs_handle, ch->rs_offset,
						write_len);
		chunk_off = 0;
		while (write_len) {
			ret = send_write(xprt, rqstp,
					 be32_to_cpu(ch->rs_handle),
					 rs_offset + chunk_off,
					 xdr_off,
					 write_len,
					 vec);
			if (ret <= 0) {
				dprintk("svcrdma: RDMA_WRITE failed, ret=%d\n",
					ret);
				return -EIO;
			}
			chunk_off += ret;
			xdr_off += ret;
			xfer_len -= ret;
			write_len -= ret;
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
 * the XDR to include in this RDMA_SEND. NB: The offset of the payload
 * to send is zero in the XDR.
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
	u32 xdr_off;
	int sge_no;
	int sge_bytes;
	int page_no;
	int pages;
	int ret;

	/* Post a recv buffer to handle another request. */
	ret = svc_rdma_post_recv(rdma, GFP_KERNEL);
	if (ret) {
		printk(KERN_INFO
		       "svcrdma: could not post a receive buffer, err=%d."
		       "Closing transport %p.\n", ret, rdma);
		set_bit(XPT_CLOSE, &rdma->sc_xprt.xpt_flags);
		svc_rdma_put_context(ctxt, 0);
		return -ENOTCONN;
	}

	/* Prepare the context */
	ctxt->pages[0] = page;
	ctxt->count = 1;

	/* Prepare the SGE for the RPCRDMA Header */
	ctxt->sge[0].lkey = rdma->sc_pd->local_dma_lkey;
	ctxt->sge[0].length = svc_rdma_xdr_get_reply_hdr_len(rdma_resp);
	ctxt->sge[0].addr =
	    ib_dma_map_page(rdma->sc_cm_id->device, page, 0,
			    ctxt->sge[0].length, DMA_TO_DEVICE);
	if (ib_dma_mapping_error(rdma->sc_cm_id->device, ctxt->sge[0].addr))
		goto err;
	atomic_inc(&rdma->sc_dma_used);

	ctxt->direction = DMA_TO_DEVICE;

	/* Map the payload indicated by 'byte_count' */
	xdr_off = 0;
	for (sge_no = 1; byte_count && sge_no < vec->count; sge_no++) {
		sge_bytes = min_t(size_t, vec->sge[sge_no].iov_len, byte_count);
		byte_count -= sge_bytes;
		ctxt->sge[sge_no].addr =
			dma_map_xdr(rdma, &rqstp->rq_res, xdr_off,
				    sge_bytes, DMA_TO_DEVICE);
		xdr_off += sge_bytes;
		if (ib_dma_mapping_error(rdma->sc_cm_id->device,
					 ctxt->sge[sge_no].addr))
			goto err;
		atomic_inc(&rdma->sc_dma_used);
		ctxt->sge[sge_no].lkey = rdma->sc_pd->local_dma_lkey;
		ctxt->sge[sge_no].length = sge_bytes;
	}
	if (byte_count != 0) {
		pr_err("svcrdma: Could not map %d bytes\n", byte_count);
		goto err;
	}

	/* Save all respages in the ctxt and remove them from the
	 * respages array. They are our pages until the I/O
	 * completes.
	 */
	pages = rqstp->rq_next_page - rqstp->rq_respages;
	for (page_no = 0; page_no < pages; page_no++) {
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
	rqstp->rq_next_page = rqstp->rq_respages + 1;

	/* The loop above bumps sc_dma_used for each sge. The
	 * xdr_buf.tail gets a separate sge, but resides in the
	 * same page as xdr_buf.head. Don't count it twice.
	 */
	if (sge_no > ctxt->count)
		atomic_dec(&rdma->sc_dma_used);

	if (sge_no > rdma->sc_max_sge) {
		pr_err("svcrdma: Too many sges (%d)\n", sge_no);
		goto err;
	}
	memset(&send_wr, 0, sizeof send_wr);
	ctxt->wr_op = IB_WR_SEND;
	send_wr.wr_id = (unsigned long)ctxt;
	send_wr.sg_list = ctxt->sge;
	send_wr.num_sge = sge_no;
	send_wr.opcode = IB_WR_SEND;
	send_wr.send_flags =  IB_SEND_SIGNALED;

	ret = svc_rdma_send(rdma, &send_wr);
	if (ret)
		goto err;

	return 0;

 err:
	svc_rdma_unmap_dma(ctxt);
	svc_rdma_put_context(ctxt, 1);
	return -EIO;
}

void svc_rdma_prep_reply_hdr(struct svc_rqst *rqstp)
{
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

	/* Get the RDMA request header. The receive logic always
	 * places this at the start of page 0.
	 */
	rdma_argp = page_address(rqstp->rq_pages[0]);

	/* Build an req vec for the XDR */
	ctxt = svc_rdma_get_context(rdma);
	ctxt->direction = DMA_TO_DEVICE;
	vec = svc_rdma_get_req_map(rdma);
	ret = svc_rdma_map_xdr(rdma, &rqstp->rq_res, vec);
	if (ret)
		goto err0;
	inline_bytes = rqstp->rq_res.len;

	/* Create the RDMA response header */
	ret = -ENOMEM;
	res_page = alloc_page(GFP_KERNEL);
	if (!res_page)
		goto err0;
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
	svc_rdma_put_req_map(rdma, vec);
	dprintk("svcrdma: send_reply returns %d\n", ret);
	return ret;

 err1:
	put_page(res_page);
 err0:
	svc_rdma_put_req_map(rdma, vec);
	svc_rdma_put_context(ctxt, 0);
	return ret;
}
