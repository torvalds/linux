/*
 * Copyright (c) 2003-2007 Network Appliance, Inc. All rights reserved.
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
 */

/*
 * rpc_rdma.c
 *
 * This file contains the guts of the RPC RDMA protocol, and
 * does marshaling/unmarshaling, etc. It is also where interfacing
 * to the Linux RPC framework lives.
 */

#include "xprt_rdma.h"

#include <linux/highmem.h>

#if IS_ENABLED(CONFIG_SUNRPC_DEBUG)
# define RPCDBG_FACILITY	RPCDBG_TRANS
#endif

static const char transfertypes[][12] = {
	"inline",	/* no chunks */
	"read list",	/* some argument via rdma read */
	"*read list",	/* entire request via rdma read */
	"write list",	/* some result via rdma write */
	"reply chunk"	/* entire reply via rdma write */
};

/* Returns size of largest RPC-over-RDMA header in a Call message
 *
 * The largest Call header contains a full-size Read list and a
 * minimal Reply chunk.
 */
static unsigned int rpcrdma_max_call_header_size(unsigned int maxsegs)
{
	unsigned int size;

	/* Fixed header fields and list discriminators */
	size = RPCRDMA_HDRLEN_MIN;

	/* Maximum Read list size */
	maxsegs += 2;	/* segment for head and tail buffers */
	size = maxsegs * sizeof(struct rpcrdma_read_chunk);

	/* Minimal Read chunk size */
	size += sizeof(__be32);	/* segment count */
	size += sizeof(struct rpcrdma_segment);
	size += sizeof(__be32);	/* list discriminator */

	dprintk("RPC:       %s: max call header size = %u\n",
		__func__, size);
	return size;
}

/* Returns size of largest RPC-over-RDMA header in a Reply message
 *
 * There is only one Write list or one Reply chunk per Reply
 * message.  The larger list is the Write list.
 */
static unsigned int rpcrdma_max_reply_header_size(unsigned int maxsegs)
{
	unsigned int size;

	/* Fixed header fields and list discriminators */
	size = RPCRDMA_HDRLEN_MIN;

	/* Maximum Write list size */
	maxsegs += 2;	/* segment for head and tail buffers */
	size = sizeof(__be32);		/* segment count */
	size += maxsegs * sizeof(struct rpcrdma_segment);
	size += sizeof(__be32);	/* list discriminator */

	dprintk("RPC:       %s: max reply header size = %u\n",
		__func__, size);
	return size;
}

void rpcrdma_set_max_header_sizes(struct rpcrdma_xprt *r_xprt)
{
	struct rpcrdma_create_data_internal *cdata = &r_xprt->rx_data;
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;
	unsigned int maxsegs = ia->ri_max_segs;

	ia->ri_max_inline_write = cdata->inline_wsize -
				  rpcrdma_max_call_header_size(maxsegs);
	ia->ri_max_inline_read = cdata->inline_rsize -
				 rpcrdma_max_reply_header_size(maxsegs);
}

/* The client can send a request inline as long as the RPCRDMA header
 * plus the RPC call fit under the transport's inline limit. If the
 * combined call message size exceeds that limit, the client must use
 * a Read chunk for this operation.
 *
 * A Read chunk is also required if sending the RPC call inline would
 * exceed this device's max_sge limit.
 */
static bool rpcrdma_args_inline(struct rpcrdma_xprt *r_xprt,
				struct rpc_rqst *rqst)
{
	struct xdr_buf *xdr = &rqst->rq_snd_buf;
	unsigned int count, remaining, offset;

	if (xdr->len > r_xprt->rx_ia.ri_max_inline_write)
		return false;

	if (xdr->page_len) {
		remaining = xdr->page_len;
		offset = xdr->page_base & ~PAGE_MASK;
		count = 0;
		while (remaining) {
			remaining -= min_t(unsigned int,
					   PAGE_SIZE - offset, remaining);
			offset = 0;
			if (++count > r_xprt->rx_ia.ri_max_send_sges)
				return false;
		}
	}

	return true;
}

/* The client can't know how large the actual reply will be. Thus it
 * plans for the largest possible reply for that particular ULP
 * operation. If the maximum combined reply message size exceeds that
 * limit, the client must provide a write list or a reply chunk for
 * this request.
 */
static bool rpcrdma_results_inline(struct rpcrdma_xprt *r_xprt,
				   struct rpc_rqst *rqst)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;

	return rqst->rq_rcv_buf.buflen <= ia->ri_max_inline_read;
}

/* Split "vec" on page boundaries into segments. FMR registers pages,
 * not a byte range. Other modes coalesce these segments into a single
 * MR when they can.
 */
static int
rpcrdma_convert_kvec(struct kvec *vec, struct rpcrdma_mr_seg *seg, int n)
{
	size_t page_offset;
	u32 remaining;
	char *base;

	base = vec->iov_base;
	page_offset = offset_in_page(base);
	remaining = vec->iov_len;
	while (remaining && n < RPCRDMA_MAX_SEGS) {
		seg[n].mr_page = NULL;
		seg[n].mr_offset = base;
		seg[n].mr_len = min_t(u32, PAGE_SIZE - page_offset, remaining);
		remaining -= seg[n].mr_len;
		base += seg[n].mr_len;
		++n;
		page_offset = 0;
	}
	return n;
}

/*
 * Chunk assembly from upper layer xdr_buf.
 *
 * Prepare the passed-in xdr_buf into representation as RPC/RDMA chunk
 * elements. Segments are then coalesced when registered, if possible
 * within the selected memreg mode.
 *
 * Returns positive number of segments converted, or a negative errno.
 */

static int
rpcrdma_convert_iovs(struct rpcrdma_xprt *r_xprt, struct xdr_buf *xdrbuf,
		     unsigned int pos, enum rpcrdma_chunktype type,
		     struct rpcrdma_mr_seg *seg)
{
	int len, n, p, page_base;
	struct page **ppages;

	n = 0;
	if (pos == 0) {
		n = rpcrdma_convert_kvec(&xdrbuf->head[0], seg, n);
		if (n == RPCRDMA_MAX_SEGS)
			goto out_overflow;
	}

	len = xdrbuf->page_len;
	ppages = xdrbuf->pages + (xdrbuf->page_base >> PAGE_SHIFT);
	page_base = xdrbuf->page_base & ~PAGE_MASK;
	p = 0;
	while (len && n < RPCRDMA_MAX_SEGS) {
		if (!ppages[p]) {
			/* alloc the pagelist for receiving buffer */
			ppages[p] = alloc_page(GFP_ATOMIC);
			if (!ppages[p])
				return -EAGAIN;
		}
		seg[n].mr_page = ppages[p];
		seg[n].mr_offset = (void *)(unsigned long) page_base;
		seg[n].mr_len = min_t(u32, PAGE_SIZE - page_base, len);
		if (seg[n].mr_len > PAGE_SIZE)
			goto out_overflow;
		len -= seg[n].mr_len;
		++n;
		++p;
		page_base = 0;	/* page offset only applies to first page */
	}

	/* Message overflows the seg array */
	if (len && n == RPCRDMA_MAX_SEGS)
		goto out_overflow;

	/* When encoding a Read chunk, the tail iovec contains an
	 * XDR pad and may be omitted.
	 */
	if (type == rpcrdma_readch && r_xprt->rx_ia.ri_implicit_roundup)
		return n;

	/* When encoding a Write chunk, some servers need to see an
	 * extra segment for non-XDR-aligned Write chunks. The upper
	 * layer provides space in the tail iovec that may be used
	 * for this purpose.
	 */
	if (type == rpcrdma_writech && r_xprt->rx_ia.ri_implicit_roundup)
		return n;

	if (xdrbuf->tail[0].iov_len) {
		n = rpcrdma_convert_kvec(&xdrbuf->tail[0], seg, n);
		if (n == RPCRDMA_MAX_SEGS)
			goto out_overflow;
	}

	return n;

out_overflow:
	pr_err("rpcrdma: segment array overflow\n");
	return -EIO;
}

static inline __be32 *
xdr_encode_rdma_segment(__be32 *iptr, struct rpcrdma_mw *mw)
{
	*iptr++ = cpu_to_be32(mw->mw_handle);
	*iptr++ = cpu_to_be32(mw->mw_length);
	return xdr_encode_hyper(iptr, mw->mw_offset);
}

/* XDR-encode the Read list. Supports encoding a list of read
 * segments that belong to a single read chunk.
 *
 * Encoding key for single-list chunks (HLOO = Handle32 Length32 Offset64):
 *
 *  Read chunklist (a linked list):
 *   N elements, position P (same P for all chunks of same arg!):
 *    1 - PHLOO - 1 - PHLOO - ... - 1 - PHLOO - 0
 *
 * Returns a pointer to the XDR word in the RDMA header following
 * the end of the Read list, or an error pointer.
 */
static __be32 *
rpcrdma_encode_read_list(struct rpcrdma_xprt *r_xprt,
			 struct rpcrdma_req *req, struct rpc_rqst *rqst,
			 __be32 *iptr, enum rpcrdma_chunktype rtype)
{
	struct rpcrdma_mr_seg *seg;
	struct rpcrdma_mw *mw;
	unsigned int pos;
	int n, nsegs;

	if (rtype == rpcrdma_noch) {
		*iptr++ = xdr_zero;	/* item not present */
		return iptr;
	}

	pos = rqst->rq_snd_buf.head[0].iov_len;
	if (rtype == rpcrdma_areadch)
		pos = 0;
	seg = req->rl_segments;
	nsegs = rpcrdma_convert_iovs(r_xprt, &rqst->rq_snd_buf, pos,
				     rtype, seg);
	if (nsegs < 0)
		return ERR_PTR(nsegs);

	do {
		n = r_xprt->rx_ia.ri_ops->ro_map(r_xprt, seg, nsegs,
						 false, &mw);
		if (n < 0)
			return ERR_PTR(n);
		list_add(&mw->mw_list, &req->rl_registered);

		*iptr++ = xdr_one;	/* item present */

		/* All read segments in this chunk
		 * have the same "position".
		 */
		*iptr++ = cpu_to_be32(pos);
		iptr = xdr_encode_rdma_segment(iptr, mw);

		dprintk("RPC: %5u %s: pos %u %u@0x%016llx:0x%08x (%s)\n",
			rqst->rq_task->tk_pid, __func__, pos,
			mw->mw_length, (unsigned long long)mw->mw_offset,
			mw->mw_handle, n < nsegs ? "more" : "last");

		r_xprt->rx_stats.read_chunk_count++;
		seg += n;
		nsegs -= n;
	} while (nsegs);

	/* Finish Read list */
	*iptr++ = xdr_zero;	/* Next item not present */
	return iptr;
}

/* XDR-encode the Write list. Supports encoding a list containing
 * one array of plain segments that belong to a single write chunk.
 *
 * Encoding key for single-list chunks (HLOO = Handle32 Length32 Offset64):
 *
 *  Write chunklist (a list of (one) counted array):
 *   N elements:
 *    1 - N - HLOO - HLOO - ... - HLOO - 0
 *
 * Returns a pointer to the XDR word in the RDMA header following
 * the end of the Write list, or an error pointer.
 */
static __be32 *
rpcrdma_encode_write_list(struct rpcrdma_xprt *r_xprt, struct rpcrdma_req *req,
			  struct rpc_rqst *rqst, __be32 *iptr,
			  enum rpcrdma_chunktype wtype)
{
	struct rpcrdma_mr_seg *seg;
	struct rpcrdma_mw *mw;
	int n, nsegs, nchunks;
	__be32 *segcount;

	if (wtype != rpcrdma_writech) {
		*iptr++ = xdr_zero;	/* no Write list present */
		return iptr;
	}

	seg = req->rl_segments;
	nsegs = rpcrdma_convert_iovs(r_xprt, &rqst->rq_rcv_buf,
				     rqst->rq_rcv_buf.head[0].iov_len,
				     wtype, seg);
	if (nsegs < 0)
		return ERR_PTR(nsegs);

	*iptr++ = xdr_one;	/* Write list present */
	segcount = iptr++;	/* save location of segment count */

	nchunks = 0;
	do {
		n = r_xprt->rx_ia.ri_ops->ro_map(r_xprt, seg, nsegs,
						 true, &mw);
		if (n < 0)
			return ERR_PTR(n);
		list_add(&mw->mw_list, &req->rl_registered);

		iptr = xdr_encode_rdma_segment(iptr, mw);

		dprintk("RPC: %5u %s: %u@0x016%llx:0x%08x (%s)\n",
			rqst->rq_task->tk_pid, __func__,
			mw->mw_length, (unsigned long long)mw->mw_offset,
			mw->mw_handle, n < nsegs ? "more" : "last");

		r_xprt->rx_stats.write_chunk_count++;
		r_xprt->rx_stats.total_rdma_request += seg->mr_len;
		nchunks++;
		seg   += n;
		nsegs -= n;
	} while (nsegs);

	/* Update count of segments in this Write chunk */
	*segcount = cpu_to_be32(nchunks);

	/* Finish Write list */
	*iptr++ = xdr_zero;	/* Next item not present */
	return iptr;
}

/* XDR-encode the Reply chunk. Supports encoding an array of plain
 * segments that belong to a single write (reply) chunk.
 *
 * Encoding key for single-list chunks (HLOO = Handle32 Length32 Offset64):
 *
 *  Reply chunk (a counted array):
 *   N elements:
 *    1 - N - HLOO - HLOO - ... - HLOO
 *
 * Returns a pointer to the XDR word in the RDMA header following
 * the end of the Reply chunk, or an error pointer.
 */
static __be32 *
rpcrdma_encode_reply_chunk(struct rpcrdma_xprt *r_xprt,
			   struct rpcrdma_req *req, struct rpc_rqst *rqst,
			   __be32 *iptr, enum rpcrdma_chunktype wtype)
{
	struct rpcrdma_mr_seg *seg;
	struct rpcrdma_mw *mw;
	int n, nsegs, nchunks;
	__be32 *segcount;

	if (wtype != rpcrdma_replych) {
		*iptr++ = xdr_zero;	/* no Reply chunk present */
		return iptr;
	}

	seg = req->rl_segments;
	nsegs = rpcrdma_convert_iovs(r_xprt, &rqst->rq_rcv_buf, 0, wtype, seg);
	if (nsegs < 0)
		return ERR_PTR(nsegs);

	*iptr++ = xdr_one;	/* Reply chunk present */
	segcount = iptr++;	/* save location of segment count */

	nchunks = 0;
	do {
		n = r_xprt->rx_ia.ri_ops->ro_map(r_xprt, seg, nsegs,
						 true, &mw);
		if (n < 0)
			return ERR_PTR(n);
		list_add(&mw->mw_list, &req->rl_registered);

		iptr = xdr_encode_rdma_segment(iptr, mw);

		dprintk("RPC: %5u %s: %u@0x%016llx:0x%08x (%s)\n",
			rqst->rq_task->tk_pid, __func__,
			mw->mw_length, (unsigned long long)mw->mw_offset,
			mw->mw_handle, n < nsegs ? "more" : "last");

		r_xprt->rx_stats.reply_chunk_count++;
		r_xprt->rx_stats.total_rdma_request += seg->mr_len;
		nchunks++;
		seg   += n;
		nsegs -= n;
	} while (nsegs);

	/* Update count of segments in the Reply chunk */
	*segcount = cpu_to_be32(nchunks);

	return iptr;
}

/* Prepare the RPC-over-RDMA header SGE.
 */
static bool
rpcrdma_prepare_hdr_sge(struct rpcrdma_ia *ia, struct rpcrdma_req *req,
			u32 len)
{
	struct rpcrdma_regbuf *rb = req->rl_rdmabuf;
	struct ib_sge *sge = &req->rl_send_sge[0];

	if (unlikely(!rpcrdma_regbuf_is_mapped(rb))) {
		if (!__rpcrdma_dma_map_regbuf(ia, rb))
			return false;
		sge->addr = rdmab_addr(rb);
		sge->lkey = rdmab_lkey(rb);
	}
	sge->length = len;

	ib_dma_sync_single_for_device(ia->ri_device, sge->addr,
				      sge->length, DMA_TO_DEVICE);
	req->rl_send_wr.num_sge++;
	return true;
}

/* Prepare the Send SGEs. The head and tail iovec, and each entry
 * in the page list, gets its own SGE.
 */
static bool
rpcrdma_prepare_msg_sges(struct rpcrdma_ia *ia, struct rpcrdma_req *req,
			 struct xdr_buf *xdr, enum rpcrdma_chunktype rtype)
{
	unsigned int sge_no, page_base, len, remaining;
	struct rpcrdma_regbuf *rb = req->rl_sendbuf;
	struct ib_device *device = ia->ri_device;
	struct ib_sge *sge = req->rl_send_sge;
	u32 lkey = ia->ri_pd->local_dma_lkey;
	struct page *page, **ppages;

	/* The head iovec is straightforward, as it is already
	 * DMA-mapped. Sync the content that has changed.
	 */
	if (!rpcrdma_dma_map_regbuf(ia, rb))
		return false;
	sge_no = 1;
	sge[sge_no].addr = rdmab_addr(rb);
	sge[sge_no].length = xdr->head[0].iov_len;
	sge[sge_no].lkey = rdmab_lkey(rb);
	ib_dma_sync_single_for_device(device, sge[sge_no].addr,
				      sge[sge_no].length, DMA_TO_DEVICE);

	/* If there is a Read chunk, the page list is being handled
	 * via explicit RDMA, and thus is skipped here. However, the
	 * tail iovec may include an XDR pad for the page list, as
	 * well as additional content, and may not reside in the
	 * same page as the head iovec.
	 */
	if (rtype == rpcrdma_readch) {
		len = xdr->tail[0].iov_len;

		/* Do not include the tail if it is only an XDR pad */
		if (len < 4)
			goto out;

		page = virt_to_page(xdr->tail[0].iov_base);
		page_base = (unsigned long)xdr->tail[0].iov_base & ~PAGE_MASK;

		/* If the content in the page list is an odd length,
		 * xdr_write_pages() has added a pad at the beginning
		 * of the tail iovec. Force the tail's non-pad content
		 * to land at the next XDR position in the Send message.
		 */
		page_base += len & 3;
		len -= len & 3;
		goto map_tail;
	}

	/* If there is a page list present, temporarily DMA map
	 * and prepare an SGE for each page to be sent.
	 */
	if (xdr->page_len) {
		ppages = xdr->pages + (xdr->page_base >> PAGE_SHIFT);
		page_base = xdr->page_base & ~PAGE_MASK;
		remaining = xdr->page_len;
		while (remaining) {
			sge_no++;
			if (sge_no > RPCRDMA_MAX_SEND_SGES - 2)
				goto out_mapping_overflow;

			len = min_t(u32, PAGE_SIZE - page_base, remaining);
			sge[sge_no].addr = ib_dma_map_page(device, *ppages,
							   page_base, len,
							   DMA_TO_DEVICE);
			if (ib_dma_mapping_error(device, sge[sge_no].addr))
				goto out_mapping_err;
			sge[sge_no].length = len;
			sge[sge_no].lkey = lkey;

			req->rl_mapped_sges++;
			ppages++;
			remaining -= len;
			page_base = 0;
		}
	}

	/* The tail iovec is not always constructed in the same
	 * page where the head iovec resides (see, for example,
	 * gss_wrap_req_priv). To neatly accommodate that case,
	 * DMA map it separately.
	 */
	if (xdr->tail[0].iov_len) {
		page = virt_to_page(xdr->tail[0].iov_base);
		page_base = (unsigned long)xdr->tail[0].iov_base & ~PAGE_MASK;
		len = xdr->tail[0].iov_len;

map_tail:
		sge_no++;
		sge[sge_no].addr = ib_dma_map_page(device, page,
						   page_base, len,
						   DMA_TO_DEVICE);
		if (ib_dma_mapping_error(device, sge[sge_no].addr))
			goto out_mapping_err;
		sge[sge_no].length = len;
		sge[sge_no].lkey = lkey;
		req->rl_mapped_sges++;
	}

out:
	req->rl_send_wr.num_sge = sge_no + 1;
	return true;

out_mapping_overflow:
	pr_err("rpcrdma: too many Send SGEs (%u)\n", sge_no);
	return false;

out_mapping_err:
	pr_err("rpcrdma: Send mapping error\n");
	return false;
}

bool
rpcrdma_prepare_send_sges(struct rpcrdma_ia *ia, struct rpcrdma_req *req,
			  u32 hdrlen, struct xdr_buf *xdr,
			  enum rpcrdma_chunktype rtype)
{
	req->rl_send_wr.num_sge = 0;
	req->rl_mapped_sges = 0;

	if (!rpcrdma_prepare_hdr_sge(ia, req, hdrlen))
		goto out_map;

	if (rtype != rpcrdma_areadch)
		if (!rpcrdma_prepare_msg_sges(ia, req, xdr, rtype))
			goto out_map;

	return true;

out_map:
	pr_err("rpcrdma: failed to DMA map a Send buffer\n");
	return false;
}

void
rpcrdma_unmap_sges(struct rpcrdma_ia *ia, struct rpcrdma_req *req)
{
	struct ib_device *device = ia->ri_device;
	struct ib_sge *sge;
	int count;

	sge = &req->rl_send_sge[2];
	for (count = req->rl_mapped_sges; count--; sge++)
		ib_dma_unmap_page(device, sge->addr, sge->length,
				  DMA_TO_DEVICE);
	req->rl_mapped_sges = 0;
}

/*
 * Marshal a request: the primary job of this routine is to choose
 * the transfer modes. See comments below.
 *
 * Returns zero on success, otherwise a negative errno.
 */

int
rpcrdma_marshal_req(struct rpc_rqst *rqst)
{
	struct rpc_xprt *xprt = rqst->rq_xprt;
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);
	enum rpcrdma_chunktype rtype, wtype;
	struct rpcrdma_msg *headerp;
	bool ddp_allowed;
	ssize_t hdrlen;
	size_t rpclen;
	__be32 *iptr;

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
	if (test_bit(RPC_BC_PA_IN_USE, &rqst->rq_bc_pa_state))
		return rpcrdma_bc_marshal_reply(rqst);
#endif

	headerp = rdmab_to_msg(req->rl_rdmabuf);
	/* don't byte-swap XID, it's already done in request */
	headerp->rm_xid = rqst->rq_xid;
	headerp->rm_vers = rpcrdma_version;
	headerp->rm_credit = cpu_to_be32(r_xprt->rx_buf.rb_max_requests);
	headerp->rm_type = rdma_msg;

	/* When the ULP employs a GSS flavor that guarantees integrity
	 * or privacy, direct data placement of individual data items
	 * is not allowed.
	 */
	ddp_allowed = !(rqst->rq_cred->cr_auth->au_flags &
						RPCAUTH_AUTH_DATATOUCH);

	/*
	 * Chunks needed for results?
	 *
	 * o If the expected result is under the inline threshold, all ops
	 *   return as inline.
	 * o Large read ops return data as write chunk(s), header as
	 *   inline.
	 * o Large non-read ops return as a single reply chunk.
	 */
	if (rpcrdma_results_inline(r_xprt, rqst))
		wtype = rpcrdma_noch;
	else if (ddp_allowed && rqst->rq_rcv_buf.flags & XDRBUF_READ)
		wtype = rpcrdma_writech;
	else
		wtype = rpcrdma_replych;

	/*
	 * Chunks needed for arguments?
	 *
	 * o If the total request is under the inline threshold, all ops
	 *   are sent as inline.
	 * o Large write ops transmit data as read chunk(s), header as
	 *   inline.
	 * o Large non-write ops are sent with the entire message as a
	 *   single read chunk (protocol 0-position special case).
	 *
	 * This assumes that the upper layer does not present a request
	 * that both has a data payload, and whose non-data arguments
	 * by themselves are larger than the inline threshold.
	 */
	if (rpcrdma_args_inline(r_xprt, rqst)) {
		rtype = rpcrdma_noch;
		rpclen = rqst->rq_snd_buf.len;
	} else if (ddp_allowed && rqst->rq_snd_buf.flags & XDRBUF_WRITE) {
		rtype = rpcrdma_readch;
		rpclen = rqst->rq_snd_buf.head[0].iov_len +
			 rqst->rq_snd_buf.tail[0].iov_len;
	} else {
		r_xprt->rx_stats.nomsg_call_count++;
		headerp->rm_type = htonl(RDMA_NOMSG);
		rtype = rpcrdma_areadch;
		rpclen = 0;
	}

	/* This implementation supports the following combinations
	 * of chunk lists in one RPC-over-RDMA Call message:
	 *
	 *   - Read list
	 *   - Write list
	 *   - Reply chunk
	 *   - Read list + Reply chunk
	 *
	 * It might not yet support the following combinations:
	 *
	 *   - Read list + Write list
	 *
	 * It does not support the following combinations:
	 *
	 *   - Write list + Reply chunk
	 *   - Read list + Write list + Reply chunk
	 *
	 * This implementation supports only a single chunk in each
	 * Read or Write list. Thus for example the client cannot
	 * send a Call message with a Position Zero Read chunk and a
	 * regular Read chunk at the same time.
	 */
	iptr = headerp->rm_body.rm_chunks;
	iptr = rpcrdma_encode_read_list(r_xprt, req, rqst, iptr, rtype);
	if (IS_ERR(iptr))
		goto out_unmap;
	iptr = rpcrdma_encode_write_list(r_xprt, req, rqst, iptr, wtype);
	if (IS_ERR(iptr))
		goto out_unmap;
	iptr = rpcrdma_encode_reply_chunk(r_xprt, req, rqst, iptr, wtype);
	if (IS_ERR(iptr))
		goto out_unmap;
	hdrlen = (unsigned char *)iptr - (unsigned char *)headerp;

	dprintk("RPC: %5u %s: %s/%s: hdrlen %zd rpclen %zd\n",
		rqst->rq_task->tk_pid, __func__,
		transfertypes[rtype], transfertypes[wtype],
		hdrlen, rpclen);

	if (!rpcrdma_prepare_send_sges(&r_xprt->rx_ia, req, hdrlen,
				       &rqst->rq_snd_buf, rtype)) {
		iptr = ERR_PTR(-EIO);
		goto out_unmap;
	}
	return 0;

out_unmap:
	r_xprt->rx_ia.ri_ops->ro_unmap_safe(r_xprt, req, false);
	return PTR_ERR(iptr);
}

/*
 * Chase down a received write or reply chunklist to get length
 * RDMA'd by server. See map at rpcrdma_create_chunks()! :-)
 */
static int
rpcrdma_count_chunks(struct rpcrdma_rep *rep, int wrchunk, __be32 **iptrp)
{
	unsigned int i, total_len;
	struct rpcrdma_write_chunk *cur_wchunk;
	char *base = (char *)rdmab_to_msg(rep->rr_rdmabuf);

	i = be32_to_cpu(**iptrp);
	cur_wchunk = (struct rpcrdma_write_chunk *) (*iptrp + 1);
	total_len = 0;
	while (i--) {
		struct rpcrdma_segment *seg = &cur_wchunk->wc_target;
		ifdebug(FACILITY) {
			u64 off;
			xdr_decode_hyper((__be32 *)&seg->rs_offset, &off);
			dprintk("RPC:       %s: chunk %d@0x%llx:0x%x\n",
				__func__,
				be32_to_cpu(seg->rs_length),
				(unsigned long long)off,
				be32_to_cpu(seg->rs_handle));
		}
		total_len += be32_to_cpu(seg->rs_length);
		++cur_wchunk;
	}
	/* check and adjust for properly terminated write chunk */
	if (wrchunk) {
		__be32 *w = (__be32 *) cur_wchunk;
		if (*w++ != xdr_zero)
			return -1;
		cur_wchunk = (struct rpcrdma_write_chunk *) w;
	}
	if ((char *)cur_wchunk > base + rep->rr_len)
		return -1;

	*iptrp = (__be32 *) cur_wchunk;
	return total_len;
}

/**
 * rpcrdma_inline_fixup - Scatter inline received data into rqst's iovecs
 * @rqst: controlling RPC request
 * @srcp: points to RPC message payload in receive buffer
 * @copy_len: remaining length of receive buffer content
 * @pad: Write chunk pad bytes needed (zero for pure inline)
 *
 * The upper layer has set the maximum number of bytes it can
 * receive in each component of rq_rcv_buf. These values are set in
 * the head.iov_len, page_len, tail.iov_len, and buflen fields.
 *
 * Unlike the TCP equivalent (xdr_partial_copy_from_skb), in
 * many cases this function simply updates iov_base pointers in
 * rq_rcv_buf to point directly to the received reply data, to
 * avoid copying reply data.
 *
 * Returns the count of bytes which had to be memcopied.
 */
static unsigned long
rpcrdma_inline_fixup(struct rpc_rqst *rqst, char *srcp, int copy_len, int pad)
{
	unsigned long fixup_copy_count;
	int i, npages, curlen;
	char *destp;
	struct page **ppages;
	int page_base;

	/* The head iovec is redirected to the RPC reply message
	 * in the receive buffer, to avoid a memcopy.
	 */
	rqst->rq_rcv_buf.head[0].iov_base = srcp;
	rqst->rq_private_buf.head[0].iov_base = srcp;

	/* The contents of the receive buffer that follow
	 * head.iov_len bytes are copied into the page list.
	 */
	curlen = rqst->rq_rcv_buf.head[0].iov_len;
	if (curlen > copy_len)
		curlen = copy_len;
	dprintk("RPC:       %s: srcp 0x%p len %d hdrlen %d\n",
		__func__, srcp, copy_len, curlen);
	srcp += curlen;
	copy_len -= curlen;

	page_base = rqst->rq_rcv_buf.page_base;
	ppages = rqst->rq_rcv_buf.pages + (page_base >> PAGE_SHIFT);
	page_base &= ~PAGE_MASK;
	fixup_copy_count = 0;
	if (copy_len && rqst->rq_rcv_buf.page_len) {
		int pagelist_len;

		pagelist_len = rqst->rq_rcv_buf.page_len;
		if (pagelist_len > copy_len)
			pagelist_len = copy_len;
		npages = PAGE_ALIGN(page_base + pagelist_len) >> PAGE_SHIFT;
		for (i = 0; i < npages; i++) {
			curlen = PAGE_SIZE - page_base;
			if (curlen > pagelist_len)
				curlen = pagelist_len;

			dprintk("RPC:       %s: page %d"
				" srcp 0x%p len %d curlen %d\n",
				__func__, i, srcp, copy_len, curlen);
			destp = kmap_atomic(ppages[i]);
			memcpy(destp + page_base, srcp, curlen);
			flush_dcache_page(ppages[i]);
			kunmap_atomic(destp);
			srcp += curlen;
			copy_len -= curlen;
			fixup_copy_count += curlen;
			pagelist_len -= curlen;
			if (!pagelist_len)
				break;
			page_base = 0;
		}

		/* Implicit padding for the last segment in a Write
		 * chunk is inserted inline at the front of the tail
		 * iovec. The upper layer ignores the content of
		 * the pad. Simply ensure inline content in the tail
		 * that follows the Write chunk is properly aligned.
		 */
		if (pad)
			srcp -= pad;
	}

	/* The tail iovec is redirected to the remaining data
	 * in the receive buffer, to avoid a memcopy.
	 */
	if (copy_len || pad) {
		rqst->rq_rcv_buf.tail[0].iov_base = srcp;
		rqst->rq_private_buf.tail[0].iov_base = srcp;
	}

	return fixup_copy_count;
}

void
rpcrdma_connect_worker(struct work_struct *work)
{
	struct rpcrdma_ep *ep =
		container_of(work, struct rpcrdma_ep, rep_connect_worker.work);
	struct rpcrdma_xprt *r_xprt =
		container_of(ep, struct rpcrdma_xprt, rx_ep);
	struct rpc_xprt *xprt = &r_xprt->rx_xprt;

	spin_lock_bh(&xprt->transport_lock);
	if (++xprt->connect_cookie == 0)	/* maintain a reserved value */
		++xprt->connect_cookie;
	if (ep->rep_connected > 0) {
		if (!xprt_test_and_set_connected(xprt))
			xprt_wake_pending_tasks(xprt, 0);
	} else {
		if (xprt_test_and_clear_connected(xprt))
			xprt_wake_pending_tasks(xprt, -ENOTCONN);
	}
	spin_unlock_bh(&xprt->transport_lock);
}

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
/* By convention, backchannel calls arrive via rdma_msg type
 * messages, and never populate the chunk lists. This makes
 * the RPC/RDMA header small and fixed in size, so it is
 * straightforward to check the RPC header's direction field.
 */
static bool
rpcrdma_is_bcall(struct rpcrdma_msg *headerp)
{
	__be32 *p = (__be32 *)headerp;

	if (headerp->rm_type != rdma_msg)
		return false;
	if (headerp->rm_body.rm_chunks[0] != xdr_zero)
		return false;
	if (headerp->rm_body.rm_chunks[1] != xdr_zero)
		return false;
	if (headerp->rm_body.rm_chunks[2] != xdr_zero)
		return false;

	/* sanity */
	if (p[7] != headerp->rm_xid)
		return false;
	/* call direction */
	if (p[8] != cpu_to_be32(RPC_CALL))
		return false;

	return true;
}
#endif	/* CONFIG_SUNRPC_BACKCHANNEL */

/*
 * This function is called when an async event is posted to
 * the connection which changes the connection state. All it
 * does at this point is mark the connection up/down, the rpc
 * timers do the rest.
 */
void
rpcrdma_conn_func(struct rpcrdma_ep *ep)
{
	schedule_delayed_work(&ep->rep_connect_worker, 0);
}

/* Process received RPC/RDMA messages.
 *
 * Errors must result in the RPC task either being awakened, or
 * allowed to timeout, to discover the errors at that time.
 */
void
rpcrdma_reply_handler(struct work_struct *work)
{
	struct rpcrdma_rep *rep =
			container_of(work, struct rpcrdma_rep, rr_work);
	struct rpcrdma_msg *headerp;
	struct rpcrdma_req *req;
	struct rpc_rqst *rqst;
	struct rpcrdma_xprt *r_xprt = rep->rr_rxprt;
	struct rpc_xprt *xprt = &r_xprt->rx_xprt;
	__be32 *iptr;
	int rdmalen, status, rmerr;
	unsigned long cwnd;

	dprintk("RPC:       %s: incoming rep %p\n", __func__, rep);

	if (rep->rr_len == RPCRDMA_BAD_LEN)
		goto out_badstatus;
	if (rep->rr_len < RPCRDMA_HDRLEN_ERR)
		goto out_shortreply;

	headerp = rdmab_to_msg(rep->rr_rdmabuf);
#if defined(CONFIG_SUNRPC_BACKCHANNEL)
	if (rpcrdma_is_bcall(headerp))
		goto out_bcall;
#endif

	/* Match incoming rpcrdma_rep to an rpcrdma_req to
	 * get context for handling any incoming chunks.
	 */
	spin_lock_bh(&xprt->transport_lock);
	rqst = xprt_lookup_rqst(xprt, headerp->rm_xid);
	if (!rqst)
		goto out_nomatch;

	req = rpcr_to_rdmar(rqst);
	if (req->rl_reply)
		goto out_duplicate;

	/* Sanity checking has passed. We are now committed
	 * to complete this transaction.
	 */
	list_del_init(&rqst->rq_list);
	spin_unlock_bh(&xprt->transport_lock);
	dprintk("RPC:       %s: reply %p completes request %p (xid 0x%08x)\n",
		__func__, rep, req, be32_to_cpu(headerp->rm_xid));

	/* from here on, the reply is no longer an orphan */
	req->rl_reply = rep;
	xprt->reestablish_timeout = 0;

	if (headerp->rm_vers != rpcrdma_version)
		goto out_badversion;

	/* check for expected message types */
	/* The order of some of these tests is important. */
	switch (headerp->rm_type) {
	case rdma_msg:
		/* never expect read chunks */
		/* never expect reply chunks (two ways to check) */
		/* never expect write chunks without having offered RDMA */
		if (headerp->rm_body.rm_chunks[0] != xdr_zero ||
		    (headerp->rm_body.rm_chunks[1] == xdr_zero &&
		     headerp->rm_body.rm_chunks[2] != xdr_zero) ||
		    (headerp->rm_body.rm_chunks[1] != xdr_zero &&
		     list_empty(&req->rl_registered)))
			goto badheader;
		if (headerp->rm_body.rm_chunks[1] != xdr_zero) {
			/* count any expected write chunks in read reply */
			/* start at write chunk array count */
			iptr = &headerp->rm_body.rm_chunks[2];
			rdmalen = rpcrdma_count_chunks(rep, 1, &iptr);
			/* check for validity, and no reply chunk after */
			if (rdmalen < 0 || *iptr++ != xdr_zero)
				goto badheader;
			rep->rr_len -=
			    ((unsigned char *)iptr - (unsigned char *)headerp);
			status = rep->rr_len + rdmalen;
			r_xprt->rx_stats.total_rdma_reply += rdmalen;
			/* special case - last chunk may omit padding */
			if (rdmalen &= 3) {
				rdmalen = 4 - rdmalen;
				status += rdmalen;
			}
		} else {
			/* else ordinary inline */
			rdmalen = 0;
			iptr = (__be32 *)((unsigned char *)headerp +
							RPCRDMA_HDRLEN_MIN);
			rep->rr_len -= RPCRDMA_HDRLEN_MIN;
			status = rep->rr_len;
		}

		r_xprt->rx_stats.fixup_copy_count +=
			rpcrdma_inline_fixup(rqst, (char *)iptr, rep->rr_len,
					     rdmalen);
		break;

	case rdma_nomsg:
		/* never expect read or write chunks, always reply chunks */
		if (headerp->rm_body.rm_chunks[0] != xdr_zero ||
		    headerp->rm_body.rm_chunks[1] != xdr_zero ||
		    headerp->rm_body.rm_chunks[2] != xdr_one ||
		    list_empty(&req->rl_registered))
			goto badheader;
		iptr = (__be32 *)((unsigned char *)headerp +
							RPCRDMA_HDRLEN_MIN);
		rdmalen = rpcrdma_count_chunks(rep, 0, &iptr);
		if (rdmalen < 0)
			goto badheader;
		r_xprt->rx_stats.total_rdma_reply += rdmalen;
		/* Reply chunk buffer already is the reply vector - no fixup. */
		status = rdmalen;
		break;

	case rdma_error:
		goto out_rdmaerr;

badheader:
	default:
		dprintk("RPC: %5u %s: invalid rpcrdma reply (type %u)\n",
			rqst->rq_task->tk_pid, __func__,
			be32_to_cpu(headerp->rm_type));
		status = -EIO;
		r_xprt->rx_stats.bad_reply_count++;
		break;
	}

out:
	/* Invalidate and flush the data payloads before waking the
	 * waiting application. This guarantees the memory region is
	 * properly fenced from the server before the application
	 * accesses the data. It also ensures proper send flow
	 * control: waking the next RPC waits until this RPC has
	 * relinquished all its Send Queue entries.
	 */
	if (!list_empty(&req->rl_registered))
		r_xprt->rx_ia.ri_ops->ro_unmap_sync(r_xprt, req);

	spin_lock_bh(&xprt->transport_lock);
	cwnd = xprt->cwnd;
	xprt->cwnd = atomic_read(&r_xprt->rx_buf.rb_credits) << RPC_CWNDSHIFT;
	if (xprt->cwnd > cwnd)
		xprt_release_rqst_cong(rqst->rq_task);

	xprt_complete_rqst(rqst->rq_task, status);
	spin_unlock_bh(&xprt->transport_lock);
	dprintk("RPC:       %s: xprt_complete_rqst(0x%p, 0x%p, %d)\n",
			__func__, xprt, rqst, status);
	return;

out_badstatus:
	rpcrdma_recv_buffer_put(rep);
	if (r_xprt->rx_ep.rep_connected == 1) {
		r_xprt->rx_ep.rep_connected = -EIO;
		rpcrdma_conn_func(&r_xprt->rx_ep);
	}
	return;

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
out_bcall:
	rpcrdma_bc_receive_call(r_xprt, rep);
	return;
#endif

/* If the incoming reply terminated a pending RPC, the next
 * RPC call will post a replacement receive buffer as it is
 * being marshaled.
 */
out_badversion:
	dprintk("RPC:       %s: invalid version %d\n",
		__func__, be32_to_cpu(headerp->rm_vers));
	status = -EIO;
	r_xprt->rx_stats.bad_reply_count++;
	goto out;

out_rdmaerr:
	rmerr = be32_to_cpu(headerp->rm_body.rm_error.rm_err);
	switch (rmerr) {
	case ERR_VERS:
		pr_err("%s: server reports header version error (%u-%u)\n",
		       __func__,
		       be32_to_cpu(headerp->rm_body.rm_error.rm_vers_low),
		       be32_to_cpu(headerp->rm_body.rm_error.rm_vers_high));
		break;
	case ERR_CHUNK:
		pr_err("%s: server reports header decoding error\n",
		       __func__);
		break;
	default:
		pr_err("%s: server reports unknown error %d\n",
		       __func__, rmerr);
	}
	status = -EREMOTEIO;
	r_xprt->rx_stats.bad_reply_count++;
	goto out;

/* If no pending RPC transaction was matched, post a replacement
 * receive buffer before returning.
 */
out_shortreply:
	dprintk("RPC:       %s: short/invalid reply\n", __func__);
	goto repost;

out_nomatch:
	spin_unlock_bh(&xprt->transport_lock);
	dprintk("RPC:       %s: no match for incoming xid 0x%08x len %d\n",
		__func__, be32_to_cpu(headerp->rm_xid),
		rep->rr_len);
	goto repost;

out_duplicate:
	spin_unlock_bh(&xprt->transport_lock);
	dprintk("RPC:       %s: "
		"duplicate reply %p to RPC request %p: xid 0x%08x\n",
		__func__, rep, req, be32_to_cpu(headerp->rm_xid));

repost:
	r_xprt->rx_stats.bad_reply_count++;
	if (rpcrdma_ep_post_recv(&r_xprt->rx_ia, rep))
		rpcrdma_recv_buffer_put(rep);
}
