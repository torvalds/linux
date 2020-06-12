// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (c) 2016-2018 Oracle. All rights reserved.
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
 * When the Send WR is constructed, it also gets its own svc_rdma_send_ctxt.
 * The ownership of all of the Reply's pages are transferred into that
 * ctxt, the Send WR is posted, and sendto returns.
 *
 * The svc_rdma_send_ctxt is presented when the Send WR completes. The
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

#include <linux/spinlock.h>
#include <asm/unaligned.h>

#include <rdma/ib_verbs.h>
#include <rdma/rdma_cm.h>

#include <linux/sunrpc/debug.h>
#include <linux/sunrpc/rpc_rdma.h>
#include <linux/sunrpc/svc_rdma.h>

#include "xprt_rdma.h"
#include <trace/events/rpcrdma.h>

#define RPCDBG_FACILITY	RPCDBG_SVCXPRT

static void svc_rdma_wc_send(struct ib_cq *cq, struct ib_wc *wc);

static inline struct svc_rdma_send_ctxt *
svc_rdma_next_send_ctxt(struct list_head *list)
{
	return list_first_entry_or_null(list, struct svc_rdma_send_ctxt,
					sc_list);
}

static struct svc_rdma_send_ctxt *
svc_rdma_send_ctxt_alloc(struct svcxprt_rdma *rdma)
{
	struct svc_rdma_send_ctxt *ctxt;
	dma_addr_t addr;
	void *buffer;
	size_t size;
	int i;

	size = sizeof(*ctxt);
	size += rdma->sc_max_send_sges * sizeof(struct ib_sge);
	ctxt = kmalloc(size, GFP_KERNEL);
	if (!ctxt)
		goto fail0;
	buffer = kmalloc(rdma->sc_max_req_size, GFP_KERNEL);
	if (!buffer)
		goto fail1;
	addr = ib_dma_map_single(rdma->sc_pd->device, buffer,
				 rdma->sc_max_req_size, DMA_TO_DEVICE);
	if (ib_dma_mapping_error(rdma->sc_pd->device, addr))
		goto fail2;

	ctxt->sc_send_wr.next = NULL;
	ctxt->sc_send_wr.wr_cqe = &ctxt->sc_cqe;
	ctxt->sc_send_wr.sg_list = ctxt->sc_sges;
	ctxt->sc_send_wr.send_flags = IB_SEND_SIGNALED;
	ctxt->sc_cqe.done = svc_rdma_wc_send;
	ctxt->sc_xprt_buf = buffer;
	xdr_buf_init(&ctxt->sc_hdrbuf, ctxt->sc_xprt_buf,
		     rdma->sc_max_req_size);
	ctxt->sc_sges[0].addr = addr;

	for (i = 0; i < rdma->sc_max_send_sges; i++)
		ctxt->sc_sges[i].lkey = rdma->sc_pd->local_dma_lkey;
	return ctxt;

fail2:
	kfree(buffer);
fail1:
	kfree(ctxt);
fail0:
	return NULL;
}

/**
 * svc_rdma_send_ctxts_destroy - Release all send_ctxt's for an xprt
 * @rdma: svcxprt_rdma being torn down
 *
 */
void svc_rdma_send_ctxts_destroy(struct svcxprt_rdma *rdma)
{
	struct svc_rdma_send_ctxt *ctxt;

	while ((ctxt = svc_rdma_next_send_ctxt(&rdma->sc_send_ctxts))) {
		list_del(&ctxt->sc_list);
		ib_dma_unmap_single(rdma->sc_pd->device,
				    ctxt->sc_sges[0].addr,
				    rdma->sc_max_req_size,
				    DMA_TO_DEVICE);
		kfree(ctxt->sc_xprt_buf);
		kfree(ctxt);
	}
}

/**
 * svc_rdma_send_ctxt_get - Get a free send_ctxt
 * @rdma: controlling svcxprt_rdma
 *
 * Returns a ready-to-use send_ctxt, or NULL if none are
 * available and a fresh one cannot be allocated.
 */
struct svc_rdma_send_ctxt *svc_rdma_send_ctxt_get(struct svcxprt_rdma *rdma)
{
	struct svc_rdma_send_ctxt *ctxt;

	spin_lock(&rdma->sc_send_lock);
	ctxt = svc_rdma_next_send_ctxt(&rdma->sc_send_ctxts);
	if (!ctxt)
		goto out_empty;
	list_del(&ctxt->sc_list);
	spin_unlock(&rdma->sc_send_lock);

out:
	rpcrdma_set_xdrlen(&ctxt->sc_hdrbuf, 0);
	xdr_init_encode(&ctxt->sc_stream, &ctxt->sc_hdrbuf,
			ctxt->sc_xprt_buf, NULL);

	ctxt->sc_send_wr.num_sge = 0;
	ctxt->sc_cur_sge_no = 0;
	ctxt->sc_page_count = 0;
	return ctxt;

out_empty:
	spin_unlock(&rdma->sc_send_lock);
	ctxt = svc_rdma_send_ctxt_alloc(rdma);
	if (!ctxt)
		return NULL;
	goto out;
}

/**
 * svc_rdma_send_ctxt_put - Return send_ctxt to free list
 * @rdma: controlling svcxprt_rdma
 * @ctxt: object to return to the free list
 *
 * Pages left in sc_pages are DMA unmapped and released.
 */
void svc_rdma_send_ctxt_put(struct svcxprt_rdma *rdma,
			    struct svc_rdma_send_ctxt *ctxt)
{
	struct ib_device *device = rdma->sc_cm_id->device;
	unsigned int i;

	/* The first SGE contains the transport header, which
	 * remains mapped until @ctxt is destroyed.
	 */
	for (i = 1; i < ctxt->sc_send_wr.num_sge; i++) {
		ib_dma_unmap_page(device,
				  ctxt->sc_sges[i].addr,
				  ctxt->sc_sges[i].length,
				  DMA_TO_DEVICE);
		trace_svcrdma_dma_unmap_page(rdma,
					     ctxt->sc_sges[i].addr,
					     ctxt->sc_sges[i].length);
	}

	for (i = 0; i < ctxt->sc_page_count; ++i)
		put_page(ctxt->sc_pages[i]);

	spin_lock(&rdma->sc_send_lock);
	list_add(&ctxt->sc_list, &rdma->sc_send_ctxts);
	spin_unlock(&rdma->sc_send_lock);
}

/**
 * svc_rdma_wc_send - Invoked by RDMA provider for each polled Send WC
 * @cq: Completion Queue context
 * @wc: Work Completion object
 *
 * NB: The svc_xprt/svcxprt_rdma is pinned whenever it's possible that
 * the Send completion handler could be running.
 */
static void svc_rdma_wc_send(struct ib_cq *cq, struct ib_wc *wc)
{
	struct svcxprt_rdma *rdma = cq->cq_context;
	struct ib_cqe *cqe = wc->wr_cqe;
	struct svc_rdma_send_ctxt *ctxt;

	trace_svcrdma_wc_send(wc);

	atomic_inc(&rdma->sc_sq_avail);
	wake_up(&rdma->sc_send_wait);

	ctxt = container_of(cqe, struct svc_rdma_send_ctxt, sc_cqe);
	svc_rdma_send_ctxt_put(rdma, ctxt);

	if (unlikely(wc->status != IB_WC_SUCCESS)) {
		set_bit(XPT_CLOSE, &rdma->sc_xprt.xpt_flags);
		svc_xprt_enqueue(&rdma->sc_xprt);
	}

	svc_xprt_put(&rdma->sc_xprt);
}

/**
 * svc_rdma_send - Post a single Send WR
 * @rdma: transport on which to post the WR
 * @wr: prepared Send WR to post
 *
 * Returns zero the Send WR was posted successfully. Otherwise, a
 * negative errno is returned.
 */
int svc_rdma_send(struct svcxprt_rdma *rdma, struct ib_send_wr *wr)
{
	int ret;

	might_sleep();

	/* Sync the transport header buffer */
	ib_dma_sync_single_for_device(rdma->sc_pd->device,
				      wr->sg_list[0].addr,
				      wr->sg_list[0].length,
				      DMA_TO_DEVICE);

	/* If the SQ is full, wait until an SQ entry is available */
	while (1) {
		if ((atomic_dec_return(&rdma->sc_sq_avail) < 0)) {
			atomic_inc(&rdma_stat_sq_starve);
			trace_svcrdma_sq_full(rdma);
			atomic_inc(&rdma->sc_sq_avail);
			wait_event(rdma->sc_send_wait,
				   atomic_read(&rdma->sc_sq_avail) > 1);
			if (test_bit(XPT_CLOSE, &rdma->sc_xprt.xpt_flags))
				return -ENOTCONN;
			trace_svcrdma_sq_retry(rdma);
			continue;
		}

		svc_xprt_get(&rdma->sc_xprt);
		trace_svcrdma_post_send(wr);
		ret = ib_post_send(rdma->sc_qp, wr, NULL);
		if (ret)
			break;
		return 0;
	}

	trace_svcrdma_sq_post_err(rdma, ret);
	set_bit(XPT_CLOSE, &rdma->sc_xprt.xpt_flags);
	svc_xprt_put(&rdma->sc_xprt);
	wake_up(&rdma->sc_send_wait);
	return ret;
}

/**
 * svc_rdma_encode_read_list - Encode RPC Reply's Read chunk list
 * @sctxt: Send context for the RPC Reply
 *
 * Return values:
 *   On success, returns length in bytes of the Reply XDR buffer
 *   that was consumed by the Reply Read list
 *   %-EMSGSIZE on XDR buffer overflow
 */
static ssize_t svc_rdma_encode_read_list(struct svc_rdma_send_ctxt *sctxt)
{
	/* RPC-over-RDMA version 1 replies never have a Read list. */
	return xdr_stream_encode_item_absent(&sctxt->sc_stream);
}

/**
 * svc_rdma_encode_write_segment - Encode one Write segment
 * @src: matching Write chunk in the RPC Call header
 * @sctxt: Send context for the RPC Reply
 * @remaining: remaining bytes of the payload left in the Write chunk
 *
 * Return values:
 *   On success, returns length in bytes of the Reply XDR buffer
 *   that was consumed by the Write segment
 *   %-EMSGSIZE on XDR buffer overflow
 */
static ssize_t svc_rdma_encode_write_segment(__be32 *src,
					     struct svc_rdma_send_ctxt *sctxt,
					     unsigned int *remaining)
{
	__be32 *p;
	const size_t len = rpcrdma_segment_maxsz * sizeof(*p);
	u32 handle, length;
	u64 offset;

	p = xdr_reserve_space(&sctxt->sc_stream, len);
	if (!p)
		return -EMSGSIZE;

	handle = be32_to_cpup(src++);
	length = be32_to_cpup(src++);
	xdr_decode_hyper(src, &offset);

	*p++ = cpu_to_be32(handle);
	if (*remaining < length) {
		/* segment only partly filled */
		length = *remaining;
		*remaining = 0;
	} else {
		/* entire segment was consumed */
		*remaining -= length;
	}
	*p++ = cpu_to_be32(length);
	xdr_encode_hyper(p, offset);

	trace_svcrdma_encode_wseg(handle, length, offset);
	return len;
}

/**
 * svc_rdma_encode_write_chunk - Encode one Write chunk
 * @src: matching Write chunk in the RPC Call header
 * @sctxt: Send context for the RPC Reply
 * @remaining: size in bytes of the payload in the Write chunk
 *
 * Copy a Write chunk from the Call transport header to the
 * Reply transport header. Update each segment's length field
 * to reflect the number of bytes written in that segment.
 *
 * Return values:
 *   On success, returns length in bytes of the Reply XDR buffer
 *   that was consumed by the Write chunk
 *   %-EMSGSIZE on XDR buffer overflow
 */
static ssize_t svc_rdma_encode_write_chunk(__be32 *src,
					   struct svc_rdma_send_ctxt *sctxt,
					   unsigned int remaining)
{
	unsigned int i, nsegs;
	ssize_t len, ret;

	len = 0;
	trace_svcrdma_encode_write_chunk(remaining);

	src++;
	ret = xdr_stream_encode_item_present(&sctxt->sc_stream);
	if (ret < 0)
		return -EMSGSIZE;
	len += ret;

	nsegs = be32_to_cpup(src++);
	ret = xdr_stream_encode_u32(&sctxt->sc_stream, nsegs);
	if (ret < 0)
		return -EMSGSIZE;
	len += ret;

	for (i = nsegs; i; i--) {
		ret = svc_rdma_encode_write_segment(src, sctxt, &remaining);
		if (ret < 0)
			return -EMSGSIZE;
		src += rpcrdma_segment_maxsz;
		len += ret;
	}

	return len;
}

/**
 * svc_rdma_encode_write_list - Encode RPC Reply's Write chunk list
 * @rctxt: Reply context with information about the RPC Call
 * @sctxt: Send context for the RPC Reply
 * @length: size in bytes of the payload in the first Write chunk
 *
 * The client provides a Write chunk list in the Call message. Fill
 * in the segments in the first Write chunk in the Reply's transport
 * header with the number of bytes consumed in each segment.
 * Remaining chunks are returned unused.
 *
 * Assumptions:
 *  - Client has provided only one Write chunk
 *
 * Return values:
 *   On success, returns length in bytes of the Reply XDR buffer
 *   that was consumed by the Reply's Write list
 *   %-EMSGSIZE on XDR buffer overflow
 */
static ssize_t
svc_rdma_encode_write_list(const struct svc_rdma_recv_ctxt *rctxt,
			   struct svc_rdma_send_ctxt *sctxt,
			   unsigned int length)
{
	ssize_t len, ret;

	ret = svc_rdma_encode_write_chunk(rctxt->rc_write_list, sctxt, length);
	if (ret < 0)
		return ret;
	len = ret;

	/* Terminate the Write list */
	ret = xdr_stream_encode_item_absent(&sctxt->sc_stream);
	if (ret < 0)
		return ret;

	return len + ret;
}

/**
 * svc_rdma_encode_reply_chunk - Encode RPC Reply's Reply chunk
 * @rctxt: Reply context with information about the RPC Call
 * @sctxt: Send context for the RPC Reply
 * @length: size in bytes of the payload in the Reply chunk
 *
 * Assumptions:
 * - Reply can always fit in the client-provided Reply chunk
 *
 * Return values:
 *   On success, returns length in bytes of the Reply XDR buffer
 *   that was consumed by the Reply's Reply chunk
 *   %-EMSGSIZE on XDR buffer overflow
 */
static ssize_t
svc_rdma_encode_reply_chunk(const struct svc_rdma_recv_ctxt *rctxt,
			    struct svc_rdma_send_ctxt *sctxt,
			    unsigned int length)
{
	return svc_rdma_encode_write_chunk(rctxt->rc_reply_chunk, sctxt,
					   length);
}

static int svc_rdma_dma_map_page(struct svcxprt_rdma *rdma,
				 struct svc_rdma_send_ctxt *ctxt,
				 struct page *page,
				 unsigned long offset,
				 unsigned int len)
{
	struct ib_device *dev = rdma->sc_cm_id->device;
	dma_addr_t dma_addr;

	dma_addr = ib_dma_map_page(dev, page, offset, len, DMA_TO_DEVICE);
	trace_svcrdma_dma_map_page(rdma, dma_addr, len);
	if (ib_dma_mapping_error(dev, dma_addr))
		goto out_maperr;

	ctxt->sc_sges[ctxt->sc_cur_sge_no].addr = dma_addr;
	ctxt->sc_sges[ctxt->sc_cur_sge_no].length = len;
	ctxt->sc_send_wr.num_sge++;
	return 0;

out_maperr:
	return -EIO;
}

/* ib_dma_map_page() is used here because svc_rdma_dma_unmap()
 * handles DMA-unmap and it uses ib_dma_unmap_page() exclusively.
 */
static int svc_rdma_dma_map_buf(struct svcxprt_rdma *rdma,
				struct svc_rdma_send_ctxt *ctxt,
				unsigned char *base,
				unsigned int len)
{
	return svc_rdma_dma_map_page(rdma, ctxt, virt_to_page(base),
				     offset_in_page(base), len);
}

/**
 * svc_rdma_pull_up_needed - Determine whether to use pull-up
 * @rdma: controlling transport
 * @sctxt: send_ctxt for the Send WR
 * @rctxt: Write and Reply chunks provided by client
 * @xdr: xdr_buf containing RPC message to transmit
 *
 * Returns:
 *	%true if pull-up must be used
 *	%false otherwise
 */
static bool svc_rdma_pull_up_needed(struct svcxprt_rdma *rdma,
				    struct svc_rdma_send_ctxt *sctxt,
				    const struct svc_rdma_recv_ctxt *rctxt,
				    struct xdr_buf *xdr)
{
	int elements;

	/* For small messages, copying bytes is cheaper than DMA mapping.
	 */
	if (sctxt->sc_hdrbuf.len + xdr->len < RPCRDMA_PULLUP_THRESH)
		return true;

	/* Check whether the xdr_buf has more elements than can
	 * fit in a single RDMA Send.
	 */
	/* xdr->head */
	elements = 1;

	/* xdr->pages */
	if (!rctxt || !rctxt->rc_write_list) {
		unsigned int remaining;
		unsigned long pageoff;

		pageoff = xdr->page_base & ~PAGE_MASK;
		remaining = xdr->page_len;
		while (remaining) {
			++elements;
			remaining -= min_t(u32, PAGE_SIZE - pageoff,
					   remaining);
			pageoff = 0;
		}
	}

	/* xdr->tail */
	if (xdr->tail[0].iov_len)
		++elements;

	/* assume 1 SGE is needed for the transport header */
	return elements >= rdma->sc_max_send_sges;
}

/**
 * svc_rdma_pull_up_reply_msg - Copy Reply into a single buffer
 * @rdma: controlling transport
 * @sctxt: send_ctxt for the Send WR; xprt hdr is already prepared
 * @rctxt: Write and Reply chunks provided by client
 * @xdr: prepared xdr_buf containing RPC message
 *
 * The device is not capable of sending the reply directly.
 * Assemble the elements of @xdr into the transport header buffer.
 *
 * Returns zero on success, or a negative errno on failure.
 */
static int svc_rdma_pull_up_reply_msg(struct svcxprt_rdma *rdma,
				      struct svc_rdma_send_ctxt *sctxt,
				      const struct svc_rdma_recv_ctxt *rctxt,
				      const struct xdr_buf *xdr)
{
	unsigned char *dst, *tailbase;
	unsigned int taillen;

	dst = sctxt->sc_xprt_buf + sctxt->sc_hdrbuf.len;
	memcpy(dst, xdr->head[0].iov_base, xdr->head[0].iov_len);
	dst += xdr->head[0].iov_len;

	tailbase = xdr->tail[0].iov_base;
	taillen = xdr->tail[0].iov_len;
	if (rctxt && rctxt->rc_write_list) {
		u32 xdrpad;

		xdrpad = xdr_pad_size(xdr->page_len);
		if (taillen && xdrpad) {
			tailbase += xdrpad;
			taillen -= xdrpad;
		}
	} else {
		unsigned int len, remaining;
		unsigned long pageoff;
		struct page **ppages;

		ppages = xdr->pages + (xdr->page_base >> PAGE_SHIFT);
		pageoff = xdr->page_base & ~PAGE_MASK;
		remaining = xdr->page_len;
		while (remaining) {
			len = min_t(u32, PAGE_SIZE - pageoff, remaining);

			memcpy(dst, page_address(*ppages), len);
			remaining -= len;
			dst += len;
			pageoff = 0;
		}
	}

	if (taillen)
		memcpy(dst, tailbase, taillen);

	sctxt->sc_sges[0].length += xdr->len;
	trace_svcrdma_send_pullup(sctxt->sc_sges[0].length);
	return 0;
}

/* svc_rdma_map_reply_msg - DMA map the buffer holding RPC message
 * @rdma: controlling transport
 * @sctxt: send_ctxt for the Send WR
 * @rctxt: Write and Reply chunks provided by client
 * @xdr: prepared xdr_buf containing RPC message
 *
 * Load the xdr_buf into the ctxt's sge array, and DMA map each
 * element as it is added. The Send WR's num_sge field is set.
 *
 * Returns zero on success, or a negative errno on failure.
 */
int svc_rdma_map_reply_msg(struct svcxprt_rdma *rdma,
			   struct svc_rdma_send_ctxt *sctxt,
			   const struct svc_rdma_recv_ctxt *rctxt,
			   struct xdr_buf *xdr)
{
	unsigned int len, remaining;
	unsigned long page_off;
	struct page **ppages;
	unsigned char *base;
	u32 xdr_pad;
	int ret;

	/* Set up the (persistently-mapped) transport header SGE. */
	sctxt->sc_send_wr.num_sge = 1;
	sctxt->sc_sges[0].length = sctxt->sc_hdrbuf.len;

	/* If there is a Reply chunk, nothing follows the transport
	 * header, and we're done here.
	 */
	if (rctxt && rctxt->rc_reply_chunk)
		return 0;

	/* For pull-up, svc_rdma_send() will sync the transport header.
	 * No additional DMA mapping is necessary.
	 */
	if (svc_rdma_pull_up_needed(rdma, sctxt, rctxt, xdr))
		return svc_rdma_pull_up_reply_msg(rdma, sctxt, rctxt, xdr);

	++sctxt->sc_cur_sge_no;
	ret = svc_rdma_dma_map_buf(rdma, sctxt,
				   xdr->head[0].iov_base,
				   xdr->head[0].iov_len);
	if (ret < 0)
		return ret;

	/* If a Write chunk is present, the xdr_buf's page list
	 * is not included inline. However the Upper Layer may
	 * have added XDR padding in the tail buffer, and that
	 * should not be included inline.
	 */
	if (rctxt && rctxt->rc_write_list) {
		base = xdr->tail[0].iov_base;
		len = xdr->tail[0].iov_len;
		xdr_pad = xdr_pad_size(xdr->page_len);

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

		++sctxt->sc_cur_sge_no;
		ret = svc_rdma_dma_map_page(rdma, sctxt, *ppages++,
					    page_off, len);
		if (ret < 0)
			return ret;

		remaining -= len;
		page_off = 0;
	}

	base = xdr->tail[0].iov_base;
	len = xdr->tail[0].iov_len;
tail:
	if (len) {
		++sctxt->sc_cur_sge_no;
		ret = svc_rdma_dma_map_buf(rdma, sctxt, base, len);
		if (ret < 0)
			return ret;
	}

	return 0;
}

/* The svc_rqst and all resources it owns are released as soon as
 * svc_rdma_sendto returns. Transfer pages under I/O to the ctxt
 * so they are released by the Send completion handler.
 */
static void svc_rdma_save_io_pages(struct svc_rqst *rqstp,
				   struct svc_rdma_send_ctxt *ctxt)
{
	int i, pages = rqstp->rq_next_page - rqstp->rq_respages;

	ctxt->sc_page_count += pages;
	for (i = 0; i < pages; i++) {
		ctxt->sc_pages[i] = rqstp->rq_respages[i];
		rqstp->rq_respages[i] = NULL;
	}

	/* Prevent svc_xprt_release from releasing pages in rq_pages */
	rqstp->rq_next_page = rqstp->rq_respages;
}

/* Prepare the portion of the RPC Reply that will be transmitted
 * via RDMA Send. The RPC-over-RDMA transport header is prepared
 * in sc_sges[0], and the RPC xdr_buf is prepared in following sges.
 *
 * Depending on whether a Write list or Reply chunk is present,
 * the server may send all, a portion of, or none of the xdr_buf.
 * In the latter case, only the transport header (sc_sges[0]) is
 * transmitted.
 *
 * RDMA Send is the last step of transmitting an RPC reply. Pages
 * involved in the earlier RDMA Writes are here transferred out
 * of the rqstp and into the sctxt's page array. These pages are
 * DMA unmapped by each Write completion, but the subsequent Send
 * completion finally releases these pages.
 *
 * Assumptions:
 * - The Reply's transport header will never be larger than a page.
 */
static int svc_rdma_send_reply_msg(struct svcxprt_rdma *rdma,
				   struct svc_rdma_send_ctxt *sctxt,
				   const struct svc_rdma_recv_ctxt *rctxt,
				   struct svc_rqst *rqstp)
{
	int ret;

	ret = svc_rdma_map_reply_msg(rdma, sctxt, rctxt, &rqstp->rq_res);
	if (ret < 0)
		return ret;

	svc_rdma_save_io_pages(rqstp, sctxt);

	if (rctxt->rc_inv_rkey) {
		sctxt->sc_send_wr.opcode = IB_WR_SEND_WITH_INV;
		sctxt->sc_send_wr.ex.invalidate_rkey = rctxt->rc_inv_rkey;
	} else {
		sctxt->sc_send_wr.opcode = IB_WR_SEND;
	}
	return svc_rdma_send(rdma, &sctxt->sc_send_wr);
}

/* Given the client-provided Write and Reply chunks, the server was not
 * able to form a complete reply. Return an RDMA_ERROR message so the
 * client can retire this RPC transaction. As above, the Send completion
 * routine releases payload pages that were part of a previous RDMA Write.
 *
 * Remote Invalidation is skipped for simplicity.
 */
static int svc_rdma_send_error_msg(struct svcxprt_rdma *rdma,
				   struct svc_rdma_send_ctxt *ctxt,
				   struct svc_rqst *rqstp)
{
	struct svc_rdma_recv_ctxt *rctxt = rqstp->rq_xprt_ctxt;
	__be32 *rdma_argp = rctxt->rc_recv_buf;
	__be32 *p;

	rpcrdma_set_xdrlen(&ctxt->sc_hdrbuf, 0);
	xdr_init_encode(&ctxt->sc_stream, &ctxt->sc_hdrbuf, ctxt->sc_xprt_buf,
			NULL);

	p = xdr_reserve_space(&ctxt->sc_stream, RPCRDMA_HDRLEN_ERR);
	if (!p)
		return -ENOMSG;

	*p++ = *rdma_argp;
	*p++ = *(rdma_argp + 1);
	*p++ = rdma->sc_fc_credits;
	*p++ = rdma_error;
	*p   = err_chunk;
	trace_svcrdma_err_chunk(*rdma_argp);

	svc_rdma_save_io_pages(rqstp, ctxt);

	ctxt->sc_send_wr.num_sge = 1;
	ctxt->sc_send_wr.opcode = IB_WR_SEND;
	ctxt->sc_sges[0].length = ctxt->sc_hdrbuf.len;
	return svc_rdma_send(rdma, &ctxt->sc_send_wr);
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
	struct svc_rdma_recv_ctxt *rctxt = rqstp->rq_xprt_ctxt;
	__be32 *rdma_argp = rctxt->rc_recv_buf;
	__be32 *wr_lst = rctxt->rc_write_list;
	__be32 *rp_ch = rctxt->rc_reply_chunk;
	struct xdr_buf *xdr = &rqstp->rq_res;
	struct svc_rdma_send_ctxt *sctxt;
	__be32 *p;
	int ret;

	ret = -ENOTCONN;
	if (svc_xprt_is_dead(xprt))
		goto err0;

	ret = -ENOMEM;
	sctxt = svc_rdma_send_ctxt_get(rdma);
	if (!sctxt)
		goto err0;

	p = xdr_reserve_space(&sctxt->sc_stream,
			      rpcrdma_fixed_maxsz * sizeof(*p));
	if (!p)
		goto err0;
	*p++ = *rdma_argp;
	*p++ = *(rdma_argp + 1);
	*p++ = rdma->sc_fc_credits;
	*p   = rp_ch ? rdma_nomsg : rdma_msg;

	if (svc_rdma_encode_read_list(sctxt) < 0)
		goto err0;
	if (wr_lst) {
		/* XXX: Presume the client sent only one Write chunk */
		unsigned long offset;
		unsigned int length;

		if (rctxt->rc_read_payload_length) {
			offset = rctxt->rc_read_payload_offset;
			length = rctxt->rc_read_payload_length;
		} else {
			offset = xdr->head[0].iov_len;
			length = xdr->page_len;
		}
		ret = svc_rdma_send_write_chunk(rdma, wr_lst, xdr, offset,
						length);
		if (ret < 0)
			goto err2;
		if (svc_rdma_encode_write_list(rctxt, sctxt, length) < 0)
			goto err0;
	} else {
		if (xdr_stream_encode_item_absent(&sctxt->sc_stream) < 0)
			goto err0;
	}
	if (rp_ch) {
		ret = svc_rdma_send_reply_chunk(rdma, rctxt, &rqstp->rq_res);
		if (ret < 0)
			goto err2;
		if (svc_rdma_encode_reply_chunk(rctxt, sctxt, ret) < 0)
			goto err0;
	} else {
		if (xdr_stream_encode_item_absent(&sctxt->sc_stream) < 0)
			goto err0;
	}

	ret = svc_rdma_send_reply_msg(rdma, sctxt, rctxt, rqstp);
	if (ret < 0)
		goto err1;
	return 0;

 err2:
	if (ret != -E2BIG && ret != -EINVAL)
		goto err1;

	ret = svc_rdma_send_error_msg(rdma, sctxt, rqstp);
	if (ret < 0)
		goto err1;
	return 0;

 err1:
	svc_rdma_send_ctxt_put(rdma, sctxt);
 err0:
	trace_svcrdma_send_failed(rqstp, ret);
	set_bit(XPT_CLOSE, &xprt->xpt_flags);
	return -ENOTCONN;
}

/**
 * svc_rdma_read_payload - special processing for a READ payload
 * @rqstp: svc_rqst to operate on
 * @offset: payload's byte offset in @xdr
 * @length: size of payload, in bytes
 *
 * Returns zero on success.
 *
 * For the moment, just record the xdr_buf location of the READ
 * payload. svc_rdma_sendto will use that location later when
 * we actually send the payload.
 */
int svc_rdma_read_payload(struct svc_rqst *rqstp, unsigned int offset,
			  unsigned int length)
{
	struct svc_rdma_recv_ctxt *rctxt = rqstp->rq_xprt_ctxt;

	/* XXX: Just one READ payload slot for now, since our
	 * transport implementation currently supports only one
	 * Write chunk.
	 */
	rctxt->rc_read_payload_offset = offset;
	rctxt->rc_read_payload_length = length;

	return 0;
}
