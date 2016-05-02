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

enum rpcrdma_chunktype {
	rpcrdma_noch = 0,
	rpcrdma_readch,
	rpcrdma_areadch,
	rpcrdma_writech,
	rpcrdma_replych
};

static const char transfertypes[][12] = {
	"pure inline",	/* no chunks */
	" read chunk",	/* some argument via rdma read */
	"*read chunk",	/* entire request via rdma read */
	"write chunk",	/* some result via rdma write */
	"reply chunk"	/* entire reply via rdma write */
};

/* Returns size of largest RPC-over-RDMA header in a Call message
 *
 * The client marshals only one chunk list per Call message.
 * The largest list is the Read list.
 */
static unsigned int rpcrdma_max_call_header_size(unsigned int maxsegs)
{
	unsigned int size;

	/* Fixed header fields and list discriminators */
	size = RPCRDMA_HDRLEN_MIN;

	/* Maximum Read list size */
	maxsegs += 2;	/* segment for head and tail buffers */
	size = maxsegs * sizeof(struct rpcrdma_read_chunk);

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

void rpcrdma_set_max_header_sizes(struct rpcrdma_ia *ia,
				  struct rpcrdma_create_data_internal *cdata,
				  unsigned int maxsegs)
{
	ia->ri_max_inline_write = cdata->inline_wsize -
				  rpcrdma_max_call_header_size(maxsegs);
	ia->ri_max_inline_read = cdata->inline_rsize -
				 rpcrdma_max_reply_header_size(maxsegs);
}

/* The client can send a request inline as long as the RPCRDMA header
 * plus the RPC call fit under the transport's inline limit. If the
 * combined call message size exceeds that limit, the client must use
 * the read chunk list for this operation.
 */
static bool rpcrdma_args_inline(struct rpcrdma_xprt *r_xprt,
				struct rpc_rqst *rqst)
{
	struct rpcrdma_ia *ia = &r_xprt->rx_ia;

	return rqst->rq_snd_buf.len <= ia->ri_max_inline_write;
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

static int
rpcrdma_tail_pullup(struct xdr_buf *buf)
{
	size_t tlen = buf->tail[0].iov_len;
	size_t skip = tlen & 3;

	/* Do not include the tail if it is only an XDR pad */
	if (tlen < 4)
		return 0;

	/* xdr_write_pages() adds a pad at the beginning of the tail
	 * if the content in "buf->pages" is unaligned. Force the
	 * tail's actual content to land at the next XDR position
	 * after the head instead.
	 */
	if (skip) {
		unsigned char *src, *dst;
		unsigned int count;

		src = buf->tail[0].iov_base;
		dst = buf->head[0].iov_base;
		dst += buf->head[0].iov_len;

		src += skip;
		tlen -= skip;

		dprintk("RPC:       %s: skip=%zu, memmove(%p, %p, %zu)\n",
			__func__, skip, dst, src, tlen);

		for (count = tlen; count; count--)
			*dst++ = *src++;
	}

	return tlen;
}

/* Split "vec" on page boundaries into segments. FMR registers pages,
 * not a byte range. Other modes coalesce these segments into a single
 * MR when they can.
 */
static int
rpcrdma_convert_kvec(struct kvec *vec, struct rpcrdma_mr_seg *seg,
		     int n, int nsegs)
{
	size_t page_offset;
	u32 remaining;
	char *base;

	base = vec->iov_base;
	page_offset = offset_in_page(base);
	remaining = vec->iov_len;
	while (remaining && n < nsegs) {
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
rpcrdma_convert_iovs(struct xdr_buf *xdrbuf, unsigned int pos,
	enum rpcrdma_chunktype type, struct rpcrdma_mr_seg *seg, int nsegs)
{
	int len, n = 0, p;
	int page_base;
	struct page **ppages;

	if (pos == 0) {
		n = rpcrdma_convert_kvec(&xdrbuf->head[0], seg, n, nsegs);
		if (n == nsegs)
			return -EIO;
	}

	len = xdrbuf->page_len;
	ppages = xdrbuf->pages + (xdrbuf->page_base >> PAGE_SHIFT);
	page_base = xdrbuf->page_base & ~PAGE_MASK;
	p = 0;
	while (len && n < nsegs) {
		if (!ppages[p]) {
			/* alloc the pagelist for receiving buffer */
			ppages[p] = alloc_page(GFP_ATOMIC);
			if (!ppages[p])
				return -ENOMEM;
		}
		seg[n].mr_page = ppages[p];
		seg[n].mr_offset = (void *)(unsigned long) page_base;
		seg[n].mr_len = min_t(u32, PAGE_SIZE - page_base, len);
		if (seg[n].mr_len > PAGE_SIZE)
			return -EIO;
		len -= seg[n].mr_len;
		++n;
		++p;
		page_base = 0;	/* page offset only applies to first page */
	}

	/* Message overflows the seg array */
	if (len && n == nsegs)
		return -EIO;

	/* When encoding the read list, the tail is always sent inline */
	if (type == rpcrdma_readch)
		return n;

	if (xdrbuf->tail[0].iov_len) {
		/* the rpcrdma protocol allows us to omit any trailing
		 * xdr pad bytes, saving the server an RDMA operation. */
		if (xdrbuf->tail[0].iov_len < 4 && xprt_rdma_pad_optimize)
			return n;
		n = rpcrdma_convert_kvec(&xdrbuf->tail[0], seg, n, nsegs);
		if (n == nsegs)
			return -EIO;
	}

	return n;
}

/*
 * Create read/write chunk lists, and reply chunks, for RDMA
 *
 *   Assume check against THRESHOLD has been done, and chunks are required.
 *   Assume only encoding one list entry for read|write chunks. The NFSv3
 *     protocol is simple enough to allow this as it only has a single "bulk
 *     result" in each procedure - complicated NFSv4 COMPOUNDs are not. (The
 *     RDMA/Sessions NFSv4 proposal addresses this for future v4 revs.)
 *
 * When used for a single reply chunk (which is a special write
 * chunk used for the entire reply, rather than just the data), it
 * is used primarily for READDIR and READLINK which would otherwise
 * be severely size-limited by a small rdma inline read max. The server
 * response will come back as an RDMA Write, followed by a message
 * of type RDMA_NOMSG carrying the xid and length. As a result, reply
 * chunks do not provide data alignment, however they do not require
 * "fixup" (moving the response to the upper layer buffer) either.
 *
 * Encoding key for single-list chunks (HLOO = Handle32 Length32 Offset64):
 *
 *  Read chunklist (a linked list):
 *   N elements, position P (same P for all chunks of same arg!):
 *    1 - PHLOO - 1 - PHLOO - ... - 1 - PHLOO - 0
 *
 *  Write chunklist (a list of (one) counted array):
 *   N elements:
 *    1 - N - HLOO - HLOO - ... - HLOO - 0
 *
 *  Reply chunk (a counted array):
 *   N elements:
 *    1 - N - HLOO - HLOO - ... - HLOO
 *
 * Returns positive RPC/RDMA header size, or negative errno.
 */

static ssize_t
rpcrdma_create_chunks(struct rpc_rqst *rqst, struct xdr_buf *target,
		struct rpcrdma_msg *headerp, enum rpcrdma_chunktype type)
{
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(rqst->rq_xprt);
	int n, nsegs, nchunks = 0;
	unsigned int pos;
	struct rpcrdma_mr_seg *seg = req->rl_segments;
	struct rpcrdma_read_chunk *cur_rchunk = NULL;
	struct rpcrdma_write_array *warray = NULL;
	struct rpcrdma_write_chunk *cur_wchunk = NULL;
	__be32 *iptr = headerp->rm_body.rm_chunks;
	int (*map)(struct rpcrdma_xprt *, struct rpcrdma_mr_seg *, int, bool);

	if (type == rpcrdma_readch || type == rpcrdma_areadch) {
		/* a read chunk - server will RDMA Read our memory */
		cur_rchunk = (struct rpcrdma_read_chunk *) iptr;
	} else {
		/* a write or reply chunk - server will RDMA Write our memory */
		*iptr++ = xdr_zero;	/* encode a NULL read chunk list */
		if (type == rpcrdma_replych)
			*iptr++ = xdr_zero;	/* a NULL write chunk list */
		warray = (struct rpcrdma_write_array *) iptr;
		cur_wchunk = (struct rpcrdma_write_chunk *) (warray + 1);
	}

	if (type == rpcrdma_replych || type == rpcrdma_areadch)
		pos = 0;
	else
		pos = target->head[0].iov_len;

	nsegs = rpcrdma_convert_iovs(target, pos, type, seg, RPCRDMA_MAX_SEGS);
	if (nsegs < 0)
		return nsegs;

	map = r_xprt->rx_ia.ri_ops->ro_map;
	do {
		n = map(r_xprt, seg, nsegs, cur_wchunk != NULL);
		if (n <= 0)
			goto out;
		if (cur_rchunk) {	/* read */
			cur_rchunk->rc_discrim = xdr_one;
			/* all read chunks have the same "position" */
			cur_rchunk->rc_position = cpu_to_be32(pos);
			cur_rchunk->rc_target.rs_handle =
						cpu_to_be32(seg->mr_rkey);
			cur_rchunk->rc_target.rs_length =
						cpu_to_be32(seg->mr_len);
			xdr_encode_hyper(
					(__be32 *)&cur_rchunk->rc_target.rs_offset,
					seg->mr_base);
			dprintk("RPC:       %s: read chunk "
				"elem %d@0x%llx:0x%x pos %u (%s)\n", __func__,
				seg->mr_len, (unsigned long long)seg->mr_base,
				seg->mr_rkey, pos, n < nsegs ? "more" : "last");
			cur_rchunk++;
			r_xprt->rx_stats.read_chunk_count++;
		} else {		/* write/reply */
			cur_wchunk->wc_target.rs_handle =
						cpu_to_be32(seg->mr_rkey);
			cur_wchunk->wc_target.rs_length =
						cpu_to_be32(seg->mr_len);
			xdr_encode_hyper(
					(__be32 *)&cur_wchunk->wc_target.rs_offset,
					seg->mr_base);
			dprintk("RPC:       %s: %s chunk "
				"elem %d@0x%llx:0x%x (%s)\n", __func__,
				(type == rpcrdma_replych) ? "reply" : "write",
				seg->mr_len, (unsigned long long)seg->mr_base,
				seg->mr_rkey, n < nsegs ? "more" : "last");
			cur_wchunk++;
			if (type == rpcrdma_replych)
				r_xprt->rx_stats.reply_chunk_count++;
			else
				r_xprt->rx_stats.write_chunk_count++;
			r_xprt->rx_stats.total_rdma_request += seg->mr_len;
		}
		nchunks++;
		seg   += n;
		nsegs -= n;
	} while (nsegs);

	/* success. all failures return above */
	req->rl_nchunks = nchunks;

	/*
	 * finish off header. If write, marshal discrim and nchunks.
	 */
	if (cur_rchunk) {
		iptr = (__be32 *) cur_rchunk;
		*iptr++ = xdr_zero;	/* finish the read chunk list */
		*iptr++ = xdr_zero;	/* encode a NULL write chunk list */
		*iptr++ = xdr_zero;	/* encode a NULL reply chunk */
	} else {
		warray->wc_discrim = xdr_one;
		warray->wc_nchunks = cpu_to_be32(nchunks);
		iptr = (__be32 *) cur_wchunk;
		if (type == rpcrdma_writech) {
			*iptr++ = xdr_zero; /* finish the write chunk list */
			*iptr++ = xdr_zero; /* encode a NULL reply chunk */
		}
	}

	/*
	 * Return header size.
	 */
	return (unsigned char *)iptr - (unsigned char *)headerp;

out:
	for (pos = 0; nchunks--;)
		pos += r_xprt->rx_ia.ri_ops->ro_unmap(r_xprt,
						      &req->rl_segments[pos]);
	return n;
}

/*
 * Copy write data inline.
 * This function is used for "small" requests. Data which is passed
 * to RPC via iovecs (or page list) is copied directly into the
 * pre-registered memory buffer for this request. For small amounts
 * of data, this is efficient. The cutoff value is tunable.
 */
static void rpcrdma_inline_pullup(struct rpc_rqst *rqst)
{
	int i, npages, curlen;
	int copy_len;
	unsigned char *srcp, *destp;
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(rqst->rq_xprt);
	int page_base;
	struct page **ppages;

	destp = rqst->rq_svec[0].iov_base;
	curlen = rqst->rq_svec[0].iov_len;
	destp += curlen;

	dprintk("RPC:       %s: destp 0x%p len %d hdrlen %d\n",
		__func__, destp, rqst->rq_slen, curlen);

	copy_len = rqst->rq_snd_buf.page_len;

	if (rqst->rq_snd_buf.tail[0].iov_len) {
		curlen = rqst->rq_snd_buf.tail[0].iov_len;
		if (destp + copy_len != rqst->rq_snd_buf.tail[0].iov_base) {
			memmove(destp + copy_len,
				rqst->rq_snd_buf.tail[0].iov_base, curlen);
			r_xprt->rx_stats.pullup_copy_count += curlen;
		}
		dprintk("RPC:       %s: tail destp 0x%p len %d\n",
			__func__, destp + copy_len, curlen);
		rqst->rq_svec[0].iov_len += curlen;
	}
	r_xprt->rx_stats.pullup_copy_count += copy_len;

	page_base = rqst->rq_snd_buf.page_base;
	ppages = rqst->rq_snd_buf.pages + (page_base >> PAGE_SHIFT);
	page_base &= ~PAGE_MASK;
	npages = PAGE_ALIGN(page_base+copy_len) >> PAGE_SHIFT;
	for (i = 0; copy_len && i < npages; i++) {
		curlen = PAGE_SIZE - page_base;
		if (curlen > copy_len)
			curlen = copy_len;
		dprintk("RPC:       %s: page %d destp 0x%p len %d curlen %d\n",
			__func__, i, destp, copy_len, curlen);
		srcp = kmap_atomic(ppages[i]);
		memcpy(destp, srcp+page_base, curlen);
		kunmap_atomic(srcp);
		rqst->rq_svec[0].iov_len += curlen;
		destp += curlen;
		copy_len -= curlen;
		page_base = 0;
	}
	/* header now contains entire send message */
}

/*
 * Marshal a request: the primary job of this routine is to choose
 * the transfer modes. See comments below.
 *
 * Uses multiple RDMA IOVs for a request:
 *  [0] -- RPC RDMA header, which uses memory from the *start* of the
 *         preregistered buffer that already holds the RPC data in
 *         its middle.
 *  [1] -- the RPC header/data, marshaled by RPC and the NFS protocol.
 *  [2] -- optional padding.
 *  [3] -- if padded, header only in [1] and data here.
 *
 * Returns zero on success, otherwise a negative errno.
 */

int
rpcrdma_marshal_req(struct rpc_rqst *rqst)
{
	struct rpc_xprt *xprt = rqst->rq_xprt;
	struct rpcrdma_xprt *r_xprt = rpcx_to_rdmax(xprt);
	struct rpcrdma_req *req = rpcr_to_rdmar(rqst);
	char *base;
	size_t rpclen;
	ssize_t hdrlen;
	enum rpcrdma_chunktype rtype, wtype;
	struct rpcrdma_msg *headerp;

#if defined(CONFIG_SUNRPC_BACKCHANNEL)
	if (test_bit(RPC_BC_PA_IN_USE, &rqst->rq_bc_pa_state))
		return rpcrdma_bc_marshal_reply(rqst);
#endif

	/*
	 * rpclen gets amount of data in first buffer, which is the
	 * pre-registered buffer.
	 */
	base = rqst->rq_svec[0].iov_base;
	rpclen = rqst->rq_svec[0].iov_len;

	headerp = rdmab_to_msg(req->rl_rdmabuf);
	/* don't byte-swap XID, it's already done in request */
	headerp->rm_xid = rqst->rq_xid;
	headerp->rm_vers = rpcrdma_version;
	headerp->rm_credit = cpu_to_be32(r_xprt->rx_buf.rb_max_requests);
	headerp->rm_type = rdma_msg;

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
	else if (rqst->rq_rcv_buf.flags & XDRBUF_READ)
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
	} else if (rqst->rq_snd_buf.flags & XDRBUF_WRITE) {
		rtype = rpcrdma_readch;
	} else {
		r_xprt->rx_stats.nomsg_call_count++;
		headerp->rm_type = htonl(RDMA_NOMSG);
		rtype = rpcrdma_areadch;
		rpclen = 0;
	}

	/* The following simplification is not true forever */
	if (rtype != rpcrdma_noch && wtype == rpcrdma_replych)
		wtype = rpcrdma_noch;
	if (rtype != rpcrdma_noch && wtype != rpcrdma_noch) {
		dprintk("RPC:       %s: cannot marshal multiple chunk lists\n",
			__func__);
		return -EIO;
	}

	hdrlen = RPCRDMA_HDRLEN_MIN;

	/*
	 * Pull up any extra send data into the preregistered buffer.
	 * When padding is in use and applies to the transfer, insert
	 * it and change the message type.
	 */
	if (rtype == rpcrdma_noch) {

		rpcrdma_inline_pullup(rqst);

		headerp->rm_body.rm_nochunks.rm_empty[0] = xdr_zero;
		headerp->rm_body.rm_nochunks.rm_empty[1] = xdr_zero;
		headerp->rm_body.rm_nochunks.rm_empty[2] = xdr_zero;
		/* new length after pullup */
		rpclen = rqst->rq_svec[0].iov_len;
	} else if (rtype == rpcrdma_readch)
		rpclen += rpcrdma_tail_pullup(&rqst->rq_snd_buf);
	if (rtype != rpcrdma_noch) {
		hdrlen = rpcrdma_create_chunks(rqst, &rqst->rq_snd_buf,
					       headerp, rtype);
		wtype = rtype;	/* simplify dprintk */

	} else if (wtype != rpcrdma_noch) {
		hdrlen = rpcrdma_create_chunks(rqst, &rqst->rq_rcv_buf,
					       headerp, wtype);
	}
	if (hdrlen < 0)
		return hdrlen;

	if (hdrlen + rpclen > RPCRDMA_INLINE_WRITE_THRESHOLD(rqst))
		goto out_overflow;

	dprintk("RPC:       %s: %s: hdrlen %zd rpclen %zd"
		" headerp 0x%p base 0x%p lkey 0x%x\n",
		__func__, transfertypes[wtype], hdrlen, rpclen,
		headerp, base, rdmab_lkey(req->rl_rdmabuf));

	/*
	 * initialize send_iov's - normally only two: rdma chunk header and
	 * single preregistered RPC header buffer, but if padding is present,
	 * then use a preregistered (and zeroed) pad buffer between the RPC
	 * header and any write data. In all non-rdma cases, any following
	 * data has been copied into the RPC header buffer.
	 */
	req->rl_send_iov[0].addr = rdmab_addr(req->rl_rdmabuf);
	req->rl_send_iov[0].length = hdrlen;
	req->rl_send_iov[0].lkey = rdmab_lkey(req->rl_rdmabuf);

	req->rl_niovs = 1;
	if (rtype == rpcrdma_areadch)
		return 0;

	req->rl_send_iov[1].addr = rdmab_addr(req->rl_sendbuf);
	req->rl_send_iov[1].length = rpclen;
	req->rl_send_iov[1].lkey = rdmab_lkey(req->rl_sendbuf);

	req->rl_niovs = 2;
	return 0;

out_overflow:
	pr_err("rpcrdma: send overflow: hdrlen %zd rpclen %zu %s\n",
		hdrlen, rpclen, transfertypes[wtype]);
	/* Terminate this RPC. Chunks registered above will be
	 * released by xprt_release -> xprt_rmda_free .
	 */
	return -EIO;
}

/*
 * Chase down a received write or reply chunklist to get length
 * RDMA'd by server. See map at rpcrdma_create_chunks()! :-)
 */
static int
rpcrdma_count_chunks(struct rpcrdma_rep *rep, unsigned int max, int wrchunk, __be32 **iptrp)
{
	unsigned int i, total_len;
	struct rpcrdma_write_chunk *cur_wchunk;
	char *base = (char *)rdmab_to_msg(rep->rr_rdmabuf);

	i = be32_to_cpu(**iptrp);
	if (i > max)
		return -1;
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

/*
 * Scatter inline received data back into provided iov's.
 */
static void
rpcrdma_inline_fixup(struct rpc_rqst *rqst, char *srcp, int copy_len, int pad)
{
	int i, npages, curlen, olen;
	char *destp;
	struct page **ppages;
	int page_base;

	curlen = rqst->rq_rcv_buf.head[0].iov_len;
	if (curlen > copy_len) {	/* write chunk header fixup */
		curlen = copy_len;
		rqst->rq_rcv_buf.head[0].iov_len = curlen;
	}

	dprintk("RPC:       %s: srcp 0x%p len %d hdrlen %d\n",
		__func__, srcp, copy_len, curlen);

	/* Shift pointer for first receive segment only */
	rqst->rq_rcv_buf.head[0].iov_base = srcp;
	srcp += curlen;
	copy_len -= curlen;

	olen = copy_len;
	i = 0;
	rpcx_to_rdmax(rqst->rq_xprt)->rx_stats.fixup_copy_count += olen;
	page_base = rqst->rq_rcv_buf.page_base;
	ppages = rqst->rq_rcv_buf.pages + (page_base >> PAGE_SHIFT);
	page_base &= ~PAGE_MASK;

	if (copy_len && rqst->rq_rcv_buf.page_len) {
		npages = PAGE_ALIGN(page_base +
			rqst->rq_rcv_buf.page_len) >> PAGE_SHIFT;
		for (; i < npages; i++) {
			curlen = PAGE_SIZE - page_base;
			if (curlen > copy_len)
				curlen = copy_len;
			dprintk("RPC:       %s: page %d"
				" srcp 0x%p len %d curlen %d\n",
				__func__, i, srcp, copy_len, curlen);
			destp = kmap_atomic(ppages[i]);
			memcpy(destp + page_base, srcp, curlen);
			flush_dcache_page(ppages[i]);
			kunmap_atomic(destp);
			srcp += curlen;
			copy_len -= curlen;
			if (copy_len == 0)
				break;
			page_base = 0;
		}
	}

	if (copy_len && rqst->rq_rcv_buf.tail[0].iov_len) {
		curlen = copy_len;
		if (curlen > rqst->rq_rcv_buf.tail[0].iov_len)
			curlen = rqst->rq_rcv_buf.tail[0].iov_len;
		if (rqst->rq_rcv_buf.tail[0].iov_base != srcp)
			memmove(rqst->rq_rcv_buf.tail[0].iov_base, srcp, curlen);
		dprintk("RPC:       %s: tail srcp 0x%p len %d curlen %d\n",
			__func__, srcp, copy_len, curlen);
		rqst->rq_rcv_buf.tail[0].iov_len = curlen;
		copy_len -= curlen; ++i;
	} else
		rqst->rq_rcv_buf.tail[0].iov_len = 0;

	if (pad) {
		/* implicit padding on terminal chunk */
		unsigned char *p = rqst->rq_rcv_buf.tail[0].iov_base;
		while (pad--)
			p[rqst->rq_rcv_buf.tail[0].iov_len++] = 0;
	}

	if (copy_len)
		dprintk("RPC:       %s: %d bytes in"
			" %d extra segments (%d lost)\n",
			__func__, olen, i, copy_len);

	/* TBD avoid a warning from call_decode() */
	rqst->rq_private_buf = rqst->rq_rcv_buf;
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
rpcrdma_reply_handler(struct rpcrdma_rep *rep)
{
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
		     req->rl_nchunks == 0))
			goto badheader;
		if (headerp->rm_body.rm_chunks[1] != xdr_zero) {
			/* count any expected write chunks in read reply */
			/* start at write chunk array count */
			iptr = &headerp->rm_body.rm_chunks[2];
			rdmalen = rpcrdma_count_chunks(rep,
						req->rl_nchunks, 1, &iptr);
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
		/* Fix up the rpc results for upper layer */
		rpcrdma_inline_fixup(rqst, (char *)iptr, rep->rr_len, rdmalen);
		break;

	case rdma_nomsg:
		/* never expect read or write chunks, always reply chunks */
		if (headerp->rm_body.rm_chunks[0] != xdr_zero ||
		    headerp->rm_body.rm_chunks[1] != xdr_zero ||
		    headerp->rm_body.rm_chunks[2] != xdr_one ||
		    req->rl_nchunks == 0)
			goto badheader;
		iptr = (__be32 *)((unsigned char *)headerp +
							RPCRDMA_HDRLEN_MIN);
		rdmalen = rpcrdma_count_chunks(rep, req->rl_nchunks, 0, &iptr);
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
		dprintk("%s: invalid rpcrdma reply header (type %d):"
				" chunks[012] == %d %d %d"
				" expected chunks <= %d\n",
				__func__, be32_to_cpu(headerp->rm_type),
				headerp->rm_body.rm_chunks[0],
				headerp->rm_body.rm_chunks[1],
				headerp->rm_body.rm_chunks[2],
				req->rl_nchunks);
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
	if (req->rl_nchunks)
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
	if (rpcrdma_ep_post_recv(&r_xprt->rx_ia, &r_xprt->rx_ep, rep))
		rpcrdma_recv_buffer_put(rep);
}
