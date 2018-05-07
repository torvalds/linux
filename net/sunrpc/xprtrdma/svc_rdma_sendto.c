// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2016 Oracle. All rights reserved.
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

/* Operation
 *
 * The main entry point is svc_rdma_sendto. This is called by the
 * RPC server when an RPC Reply is ready to be transmitted to a client.
 *
 * The passed-in svc_rqst contains a struct xdr_buf which holds an
 * XDR-encoded RPC Reply message. sendto must construct the RPC-over-RDMA
 * transport header, post all Write WRs needed for this Reply, then post
 * a Send WR conveying the transport header and the RPC message itself to
 * the client.
 *
 * svc_rdma_sendto must fully transmit the Reply before returning, as
 * the svc_rqst will be recycled as soon as sendto returns. Remaining
 * resources referred to by the svc_rqst are also recycled at that time.
 * Therefore any resources that must remain longer must be detached
 * from the svc_rqst and released later.
 *
 * Page Management
 *
 * The I/O that performs Reply transmission is asynchronous, and may
 * complete well after sendto returns. Thus pages under I/O must be
 * removed from the svc_rqst before sendto returns.
 *
 * The logic here depends on Send Queue and completion ordering. Since
 * the Send WR is always posted last, it will always complete last. Thus
 * when it completes, it is guaranteed that all previous Write WRs have
 * also completed.
 *
 * Write WRs are constructed and posted. Each Write segment gets its own
 * svc_rdma_rw_ctxt, allowing the Write completion handler to find and
 * DMA-unmap the pages under I/O for that Write segment. The Write
 * completion handler does not release any pages.
 *
 * When the Send WR is constructed, it also gets its own svc_rdma_op_ctxt.
 * The ownership of all of the Reply's pages are transferred into that
 * ctxt, the Send WR is posted, and sendto returns.
 *
 * The svc_rdma_op_ctxt is presented when the Send WR completes. The
 * Send completion handler finally releases the Reply's pages.
 *
 * This mechanism also assumes that completions on the transport's Send
 * Completion Queue do not run in parallel. Otherwise a Write completion
 * and Send completion running at the same time could release pages that
 * are still DMA-mapped.
 *
 * Error Handling
 *
 * - If the Send WR is posted successfully, it will either complete
 *   successfully, or get flushed. Either way, the Send completion
 *   handler releases the Reply's pages.
 * - If the Send WR cannot be not posted, the forward path releases
 *   the Reply's pages.
 *
 * This handles the case, without the use of page reference counting,
 * where two different Write segments send portions of the same page.
 */

#include <linux/sunrpc/debug.h>
#include <linux/sunrpc/rpc_rdma.h>
#include <linux/spinlock.h>
#include <asm/unaligned.h>
#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>
#include <linux/sunrpc/svc_rdma.h>

#define RPCDBG_FACILITY	RPCDBG_SVCXPRT

static u32 xdr_padsize(u32 len)
{
	return (len & 3) ? (4 - (len & 3)) : 0;
}

/* Returns length of transport header, in bytes.
 */
static unsigned int svc_rdma_reply_hdr_len(__be32 *rdma_resp)
{
	unsigned int nsegs;
	__be32 *p;

	p = rdma_resp;

	/* RPC-over-RDMA V1 replies never have a Read list. */
	p += rpcrdma_fixed_maxsz + 1;

	/* Skip Write list. */
	while (*p++ != xdr_zero) {
		nsegs = be32_to_cpup(p++);
		p += nsegs * rpcrdma_segment_maxsz;
	}

	/* Skip Reply chunk. */
	if (*p++ != xdr_zero) {
		nsegs = be32_to_cpup(p++);
		p += nsegs * rpcrdma_segment_maxsz;
	}

	return (unsigned long)p - (unsigned long)rdma_resp;
}

/* One Write chunk is copied from Call transport header to Reply
 * transport header. Each segment's length field is updated to
 * reflect number of bytes consumed in the segment.
 *
 * Returns number of segments in this chunk.
 */
static unsigned int xdr_encode_write_chunk(__be32 *dst, __be32 *src,
					   unsigned int remaining)
{
	unsigned int i, nsegs;
	u32 seg_len;

	/* Write list discriminator */
	*dst++ = *src++;

	/* number of segments in this chunk */
	nsegs = be32_to_cpup(src);
	*dst++ = *src++;

	for (i = nsegs; i; i--) {
		/* segment's RDMA handle */
		*dst++ = *src++;

		/* bytes returned in this segment */
		seg_len = be32_to_cpu(*src);
		if (remaining >= seg_len) {
			/* entire segment was consumed */
			*dst = *src;
			remaining -= seg_len;
		} else {
			/* segment only partly filled */
			*dst = cpu_to_be32(remaining);
			remaining = 0;
		}
		dst++; src++;

		/* segment's RDMA offset */
		*dst++ = *src++;
		*dst++ = *src++;
	}

	return nsegs;
}

/* The client provided a Write list in the Call message. Fill in
 * the segments in the first Write chunk in the Reply's transport
 * header with the number of bytes consumed in each segment.
 * Remaining chunks are returned unused.
 *
 * Assumptions:
 *  - Client has provided only one Write chunk
 */
static void svc_rdma_xdr_encode_write_list(__be32 *rdma_resp, __be32 *wr_ch,
					   unsigned int consumed)
{
	unsigned int nsegs;
	__be32 *p, *q;

	/* RPC-over-RDMA V1 replies never have a Read list. */
	p = rdma_resp + rpcrdma_fixed_maxsz + 1;

	q = wr_ch;
	while (*q != xdr_zero) {
		nsegs = xdr_encode_write_chunk(p, q, consumed);
		q += 2 + nsegs * rpcrdma_segment_maxsz;
		p += 2 + nsegs * rpcrdma_segment_maxsz;
		consumed = 0;
	}

	/* Terminate Write list */
	*p++ = xdr_zero;

	/* Reply chunk discriminator; may be replaced later */
	*p = xdr_zero;
}

/* The client provided a Reply chunk in the Call message. Fill in
 * the segments in the Reply chunk in the Reply message with the
 * number of bytes consumed in each segment.
 *
 * Assumptions:
 * - Reply can always fit in the provided Reply chunk
 */
static void svc_rdma_xdr_encode_reply_chunk(__be32 *rdma_resp, __be32 *rp_ch,
					    unsigned int consumed)
{
	__be32 *p;

	/* Find the Reply chunk in the Reply's xprt header.
	 * RPC-over-RDMA V1 replies never have a Read list.
	 */
	p = rdma_resp + rpcrdma_fixed_maxsz + 1;

	/* Skip past Write list */
	while (*p++ != xdr_zero)
		p += 1 + be32_to_cpup(p) * rpcrdma_segment_maxsz;

	xdr_encode_write_chunk(p, rp_ch, consumed);
}

/* Parse the RPC Call's transport header.
 */
static void svc_rdma_get_write_arrays(__be32 *rdma_argp,
				      __be32 **write, __be32 **reply)
{
	__be32 *p;

	p = rdma_argp + rpcrdma_fixed_maxsz;

	/* Read list */
	while (*p++ != xdr_zero)
		p += 5;

	/* Write list */
	if (*p != xdr_zero) {
		*write = p;
		while (*p++ != xdr_zero)
			p += 1 + be32_to_cpu(*p) * 4;
	} else {
		*write = NULL;
		p++;
	}

	/* Reply chunk */
	if (*p != xdr_zero)
		*reply = p;
	else
		*reply = NULL;
}

/* RPC-over-RDMA Version One private extension: Remote Invalidation.
 * Responder's choice: requester signals it can handle Send With
 * Invalidate, and responder chooses one rkey to invalidate.
 *
 * Find a candidate rkey to invalidate when sending a reply.  Picks the
 * first R_key it finds in the chunk lists.
 *
 * Returns zero if RPC's chunk lists are empty.
 */
static u32 svc_rdma_get_inv_rkey(__be32 *rdma_argp,
				 __be32 *wr_lst, __be32 *rp_ch)
{
	__be32 *p;

	p = rdma_argp + rpcrdma_fixed_maxsz;
	if (*p != xdr_zero)
		p += 2;
	else if (wr_lst && be32_to_cpup(wr_lst + 1))
		p = wr_lst + 2;
	else if (rp_ch && be32_to_cpup(rp_ch + 1))
		p = rp_ch + 2;
	else
		return 0;
	return be32_to_cpup(p);
}

/* ib_dma_map_page() is used here because svc_rdma_dma_unmap()
 * is used during completion to DMA-unmap this memory, and
 * it uses ib_dma_unmap_page() exclusively.
 */
static int svc_rdma_dma_map_buf(struct svcxprt_rdma *rdma,
				struct svc_rdma_op_ctxt *ctxt,
				unsigned int sge_no,
				unsigned char *base,
				unsigned int len)
{
	unsigned long offset = (unsigned long)base & ~PAGE_MASK;
	struct ib_device *dev = rdma->sc_cm_id->device;
	dma_addr_t dma_addr;

	dma_addr = ib_dma_map_page(dev, virt_to_page(base),
				   offset, len, DMA_TO_DEVICE);
	if (ib_dma_mapping_error(dev, dma_addr))
		goto out_maperr;

	ctxt->sge[sge_no].addr = dma_addr;
	ctxt->sge[sge_no].length = len;
	ctxt->sge[sge_no].lkey = rdma->sc_pd->local_dma_lkey;
	svc_rdma_count_mappings(rdma, ctxt);
	return 0;

out_maperr:
	pr_err("svcrdma: failed to map buffer\n");
	return -EIO;
}

static int svc_rdma_dma_map_page(struct svcxprt_rdma *rdma,
				 struct svc_rdma_op_ctxt *ctxt,
				 unsigned int sge_no,
				 struct page *page,
				 unsigned int offset,
				 unsigned int len)
{
	struct ib_device *dev = rdma->sc_cm_id->device;
	dma_addr_t dma_addr;

	dma_addr = ib_dma_map_page(dev, page, offset, len, DMA_TO_DEVICE);
	if (ib_dma_mapping_error(dev, dma_addr))
		goto out_maperr;

	ctxt->sge[sge_no].addr = dma_addr;
	ctxt->sge[sge_no].length = len;
	ctxt->sge[sge_no].lkey = rdma->sc_pd->local_dma_lkey;
	svc_rdma_count_mappings(rdma, ctxt);
	return 0;

out_maperr:
	pr_err("svcrdma: failed to map page\n");
	return -EIO;
}

/**
 * svc_rdma_map_reply_hdr - DMA map the transport header buffer
 * @rdma: controlling transport
 * @ctxt: op_ctxt for the Send WR
 * @rdma_resp: buffer containing transport header
 * @len: length of transport header
 *
 * Returns:
 *	%0 if the header is DMA mapped,
 *	%-EIO if DMA mapping failed.
 */
int svc_rdma_map_reply_hdr(struct svcxprt_rdma *rdma,
			   struct svc_rdma_op_ctxt *ctxt,
			   __be32 *rdma_resp,
			   unsigned int len)
{
	ctxt->direction = DMA_TO_DEVICE;
	ctxt->pages[0] = virt_to_page(rdma_resp);
	ctxt->count = 1;
	return svc_rdma_dma_map_page(rdma, ctxt, 0, ctxt->pages[0], 0, len);
}

/* Load the xdr_buf into the ctxt's sge array, and DMA map each
 * element as it is added.
 *
 * Returns the number of sge elements loaded on success, or
 * a negative errno on failure.
 */
static int svc_rdma_map_reply_msg(struct svcxprt_rdma *rdma,
				  struct svc_rdma_op_ctxt *ctxt,
				  struct xdr_buf *xdr, __be32 *wr_lst)
{
	unsigned int len, sge_no, remaining, page_off;
	struct page **ppages;
	unsigned char *base;
	u32 xdr_pad;
	int ret;

	sge_no = 1;

	ret = svc_rdma_dma_map_buf(rdma, ctxt, sge_no++,
				   xdr->head[0].iov_base,
				   xdr->head[0].iov_len);
	if (ret < 0)
		return ret;

	/* If a Write chunk is present, the xdr_buf's page list
	 * is not included inline. However the Upper Layer may
	 * have added XDR padding in the tail buffer, and that
	 * should not be included inline.
	 */
	if (wr_lst) {
		base = xdr->tail[0].iov_base;
		len = xdr->tail[0].iov_len;
		xdr_pad = xdr_padsize(xdr->page_len);

		if (len && xdr_pad) {
			base += xdr_pad;
			len -= xdr_pad;
		}

		goto tail;
	}

	ppages = xdr->pages + (xdr->page_base >> PAGE_SHIFT);
	page_off = xdr->page_base & ~PAGE_MASK;
	remaining = xdr->page_len;
	while (remaining) {
		len = min_t(u32, PAGE_SIZE - page_off, remaining);

		ret = svc_rdma_dma_map_page(rdma, ctxt, sge_no++,
					    *ppages++, page_off, len);
		if (ret < 0)
			return ret;

		remaining -= len;
		page_off = 0;
	}

	base = xdr->tail[0].iov_base;
	len = xdr->tail[0].iov_len;
tail:
	if (len) {
		ret = svc_rdma_dma_map_buf(rdma, ctxt, sge_no++, base, len);
		if (ret < 0)
			return ret;
	}

	return sge_no - 1;
}

/* The svc_rqst and all resources it owns are released as soon as
 * svc_rdma_sendto returns. Transfer pages under I/O to the ctxt
 * so they are released by the Send completion handler.
 */
static void svc_rdma_save_io_pages(struct svc_rqst *rqstp,
				   struct svc_rdma_op_ctxt *ctxt)
{
	int i, pages = rqstp->rq_next_page - rqstp->rq_respages;

	ctxt->count += pages;
	for (i = 0; i < pages; i++) {
		ctxt->pages[i + 1] = rqstp->rq_respages[i];
		rqstp->rq_respages[i] = NULL;
	}
	rqstp->rq_next_page = rqstp->rq_respages + 1;
}

/**
 * svc_rdma_post_send_wr - Set up and post one Send Work Request
 * @rdma: controlling transport
 * @ctxt: op_ctxt for transmitting the Send WR
 * @num_sge: number of SGEs to send
 * @inv_rkey: R_key argument to Send With Invalidate, or zero
 *
 * Returns:
 *	%0 if the Send* was posted successfully,
 *	%-ENOTCONN if the connection was lost or dropped,
 *	%-EINVAL if there was a problem with the Send we built,
 *	%-ENOMEM if ib_post_send failed.
 */
int svc_rdma_post_send_wr(struct svcxprt_rdma *rdma,
			  struct svc_rdma_op_ctxt *ctxt, int num_sge,
			  u32 inv_rkey)
{
	struct ib_send_wr *send_wr = &ctxt->send_wr;

	dprintk("svcrdma: posting Send WR with %u sge(s)\n", num_sge);

	send_wr->next = NULL;
	ctxt->cqe.done = svc_rdma_wc_send;
	send_wr->wr_cqe = &ctxt->cqe;
	send_wr->sg_list = ctxt->sge;
	send_wr->num_sge = num_sge;
	send_wr->send_flags = IB_SEND_SIGNALED;
	if (inv_rkey) {
		send_wr->opcode = IB_WR_SEND_WITH_INV;
		send_wr->ex.invalidate_rkey = inv_rkey;
	} else {
		send_wr->opcode = IB_WR_SEND;
	}

	return svc_rdma_send(rdma, send_wr);
}

/* Prepare the portion of the RPC Reply that will be transmitted
 * via RDMA Send. The RPC-over-RDMA transport header is prepared
 * in sge[0], and the RPC xdr_buf is prepared in following sges.
 *
 * Depending on whether a Write list or Reply chunk is present,
 * the server may send all, a portion of, or none of the xdr_buf.
 * In the latter case, only the transport header (sge[0]) is
 * transmitted.
 *
 * RDMA Send is the last step of transmitting an RPC reply. Pages
 * involved in the earlier RDMA Writes are here transferred out
 * of the rqstp and into the ctxt's page array. These pages are
 * DMA unmapped by each Write completion, but the subsequent Send
 * completion finally releases these pages.
 *
 * Assumptions:
 * - The Reply's transport header will never be larger than a page.
 */
static int svc_rdma_send_reply_msg(struct svcxprt_rdma *rdma,
				   __be32 *rdma_argp, __be32 *rdma_resp,
				   struct svc_rqst *rqstp,
				   __be32 *wr_lst, __be32 *rp_ch)
{
	struct svc_rdma_op_ctxt *ctxt;
	u32 inv_rkey;
	int ret;

	dprintk("svcrdma: sending %s reply: head=%zu, pagelen=%u, tail=%zu\n",
		(rp_ch ? "RDMA_NOMSG" : "RDMA_MSG"),
		rqstp->rq_res.head[0].iov_len,
		rqstp->rq_res.page_len,
		rqstp->rq_res.tail[0].iov_len);

	ctxt = svc_rdma_get_context(rdma);

	ret = svc_rdma_map_reply_hdr(rdma, ctxt, rdma_resp,
				     svc_rdma_reply_hdr_len(rdma_resp));
	if (ret < 0)
		goto err;

	if (!rp_ch) {
		ret = svc_rdma_map_reply_msg(rdma, ctxt,
					     &rqstp->rq_res, wr_lst);
		if (ret < 0)
			goto err;
	}

	svc_rdma_save_io_pages(rqstp, ctxt);

	inv_rkey = 0;
	if (rdma->sc_snd_w_inv)
		inv_rkey = svc_rdma_get_inv_rkey(rdma_argp, wr_lst, rp_ch);
	ret = svc_rdma_post_send_wr(rdma, ctxt, 1 + ret, inv_rkey);
	if (ret)
		goto err;

	return 0;

err:
	svc_rdma_unmap_dma(ctxt);
	svc_rdma_put_context(ctxt, 1);
	return ret;
}

/* Given the client-provided Write and Reply chunks, the server was not
 * able to form a complete reply. Return an RDMA_ERROR message so the
 * client can retire this RPC transaction. As above, the Send completion
 * routine releases payload pages that were part of a previous RDMA Write.
 *
 * Remote Invalidation is skipped for simplicity.
 */
static int svc_rdma_send_error_msg(struct svcxprt_rdma *rdma,
				   __be32 *rdma_resp, struct svc_rqst *rqstp)
{
	struct svc_rdma_op_ctxt *ctxt;
	__be32 *p;
	int ret;

	ctxt = svc_rdma_get_context(rdma);

	/* Replace the original transport header with an
	 * RDMA_ERROR response. XID etc are preserved.
	 */
	p = rdma_resp + 3;
	*p++ = rdma_error;
	*p   = err_chunk;

	ret = svc_rdma_map_reply_hdr(rdma, ctxt, rdma_resp, 20);
	if (ret < 0)
		goto err;

	svc_rdma_save_io_pages(rqstp, ctxt);

	ret = svc_rdma_post_send_wr(rdma, ctxt, 1 + ret, 0);
	if (ret)
		goto err;

	return 0;

err:
	pr_err("svcrdma: failed to post Send WR (%d)\n", ret);
	svc_rdma_unmap_dma(ctxt);
	svc_rdma_put_context(ctxt, 1);
	return ret;
}

void svc_rdma_prep_reply_hdr(struct svc_rqst *rqstp)
{
}

/**
 * svc_rdma_sendto - Transmit an RPC reply
 * @rqstp: processed RPC request, reply XDR already in ::rq_res
 *
 * Any resources still associated with @rqstp are released upon return.
 * If no reply message was possible, the connection is closed.
 *
 * Returns:
 *	%0 if an RPC reply has been successfully posted,
 *	%-ENOMEM if a resource shortage occurred (connection is lost),
 *	%-ENOTCONN if posting failed (connection is lost).
 */
int svc_rdma_sendto(struct svc_rqst *rqstp)
{
	struct svc_xprt *xprt = rqstp->rq_xprt;
	struct svcxprt_rdma *rdma =
		container_of(xprt, struct svcxprt_rdma, sc_xprt);
	__be32 *p, *rdma_argp, *rdma_resp, *wr_lst, *rp_ch;
	struct xdr_buf *xdr = &rqstp->rq_res;
	struct page *res_page;
	int ret;

	/* Find the call's chunk lists to decide how to send the reply.
	 * Receive places the Call's xprt header at the start of page 0.
	 */
	rdma_argp = page_address(rqstp->rq_pages[0]);
	svc_rdma_get_write_arrays(rdma_argp, &wr_lst, &rp_ch);

	dprintk("svcrdma: preparing response for XID 0x%08x\n",
		be32_to_cpup(rdma_argp));

	/* Create the RDMA response header. xprt->xpt_mutex,
	 * acquired in svc_send(), serializes RPC replies. The
	 * code path below that inserts the credit grant value
	 * into each transport header runs only inside this
	 * critical section.
	 */
	ret = -ENOMEM;
	res_page = alloc_page(GFP_KERNEL);
	if (!res_page)
		goto err0;
	rdma_resp = page_address(res_page);

	p = rdma_resp;
	*p++ = *rdma_argp;
	*p++ = *(rdma_argp + 1);
	*p++ = rdma->sc_fc_credits;
	*p++ = rp_ch ? rdma_nomsg : rdma_msg;

	/* Start with empty chunks */
	*p++ = xdr_zero;
	*p++ = xdr_zero;
	*p   = xdr_zero;

	if (wr_lst) {
		/* XXX: Presume the client sent only one Write chunk */
		ret = svc_rdma_send_write_chunk(rdma, wr_lst, xdr);
		if (ret < 0)
			goto err2;
		svc_rdma_xdr_encode_write_list(rdma_resp, wr_lst, ret);
	}
	if (rp_ch) {
		ret = svc_rdma_send_reply_chunk(rdma, rp_ch, wr_lst, xdr);
		if (ret < 0)
			goto err2;
		svc_rdma_xdr_encode_reply_chunk(rdma_resp, rp_ch, ret);
	}

	ret = svc_rdma_send_reply_msg(rdma, rdma_argp, rdma_resp, rqstp,
				      wr_lst, rp_ch);
	if (ret < 0)
		goto err0;
	return 0;

 err2:
	if (ret != -E2BIG && ret != -EINVAL)
		goto err1;

	ret = svc_rdma_send_error_msg(rdma, rdma_resp, rqstp);
	if (ret < 0)
		goto err0;
	return 0;

 err1:
	put_page(res_page);
 err0:
	pr_err("svcrdma: Could not send reply, err=%d. Closing transport.\n",
	       ret);
	set_bit(XPT_CLOSE, &xprt->xpt_flags);
	return -ENOTCONN;
}
