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

/*
 * Replace the pages in the rq_argpages array with the pages from the SGE in
 * the RDMA_RECV completion. The SGL should contain full pages up until the
 * last one.
 */
static void rdma_build_arg_xdr(struct svc_rqst *rqstp,
			       struct svc_rdma_op_ctxt *ctxt,
			       u32 byte_count)
{
	struct rpcrdma_msg *rmsgp;
	struct page *page;
	u32 bc;
	int sge_no;

	/* Swap the page in the SGE with the page in argpages */
	page = ctxt->pages[0];
	put_page(rqstp->rq_pages[0]);
	rqstp->rq_pages[0] = page;

	/* Set up the XDR head */
	rqstp->rq_arg.head[0].iov_base = page_address(page);
	rqstp->rq_arg.head[0].iov_len =
		min_t(size_t, byte_count, ctxt->sge[0].length);
	rqstp->rq_arg.len = byte_count;
	rqstp->rq_arg.buflen = byte_count;

	/* Compute bytes past head in the SGL */
	bc = byte_count - rqstp->rq_arg.head[0].iov_len;

	/* If data remains, store it in the pagelist */
	rqstp->rq_arg.page_len = bc;
	rqstp->rq_arg.page_base = 0;

	/* RDMA_NOMSG: RDMA READ data should land just after RDMA RECV data */
	rmsgp = (struct rpcrdma_msg *)rqstp->rq_arg.head[0].iov_base;
	if (rmsgp->rm_type == rdma_nomsg)
		rqstp->rq_arg.pages = &rqstp->rq_pages[0];
	else
		rqstp->rq_arg.pages = &rqstp->rq_pages[1];

	sge_no = 1;
	while (bc && sge_no < ctxt->count) {
		page = ctxt->pages[sge_no];
		put_page(rqstp->rq_pages[sge_no]);
		rqstp->rq_pages[sge_no] = page;
		bc -= min_t(u32, bc, ctxt->sge[sge_no].length);
		rqstp->rq_arg.buflen += ctxt->sge[sge_no].length;
		sge_no++;
	}
	rqstp->rq_respages = &rqstp->rq_pages[sge_no];
	rqstp->rq_next_page = rqstp->rq_respages + 1;

	/* If not all pages were used from the SGL, free the remaining ones */
	bc = sge_no;
	while (sge_no < ctxt->count) {
		page = ctxt->pages[sge_no++];
		put_page(page);
	}
	ctxt->count = bc;

	/* Set up tail */
	rqstp->rq_arg.tail[0].iov_base = NULL;
	rqstp->rq_arg.tail[0].iov_len = 0;
}

/* Issue an RDMA_READ using the local lkey to map the data sink */
int rdma_read_chunk_lcl(struct svcxprt_rdma *xprt,
			struct svc_rqst *rqstp,
			struct svc_rdma_op_ctxt *head,
			int *page_no,
			u32 *page_offset,
			u32 rs_handle,
			u32 rs_length,
			u64 rs_offset,
			bool last)
{
	struct ib_rdma_wr read_wr;
	int pages_needed = PAGE_ALIGN(*page_offset + rs_length) >> PAGE_SHIFT;
	struct svc_rdma_op_ctxt *ctxt = svc_rdma_get_context(xprt);
	int ret, read, pno;
	u32 pg_off = *page_offset;
	u32 pg_no = *page_no;

	ctxt->direction = DMA_FROM_DEVICE;
	ctxt->read_hdr = head;
	pages_needed = min_t(int, pages_needed, xprt->sc_max_sge_rd);
	read = min_t(int, (pages_needed << PAGE_SHIFT) - *page_offset,
		     rs_length);

	for (pno = 0; pno < pages_needed; pno++) {
		int len = min_t(int, rs_length, PAGE_SIZE - pg_off);

		head->arg.pages[pg_no] = rqstp->rq_arg.pages[pg_no];
		head->arg.page_len += len;

		head->arg.len += len;
		if (!pg_off)
			head->count++;
		rqstp->rq_respages = &rqstp->rq_arg.pages[pg_no+1];
		rqstp->rq_next_page = rqstp->rq_respages + 1;
		ctxt->sge[pno].addr =
			ib_dma_map_page(xprt->sc_cm_id->device,
					head->arg.pages[pg_no], pg_off,
					PAGE_SIZE - pg_off,
					DMA_FROM_DEVICE);
		ret = ib_dma_mapping_error(xprt->sc_cm_id->device,
					   ctxt->sge[pno].addr);
		if (ret)
			goto err;
		svc_rdma_count_mappings(xprt, ctxt);

		ctxt->sge[pno].lkey = xprt->sc_pd->local_dma_lkey;
		ctxt->sge[pno].length = len;
		ctxt->count++;

		/* adjust offset and wrap to next page if needed */
		pg_off += len;
		if (pg_off == PAGE_SIZE) {
			pg_off = 0;
			pg_no++;
		}
		rs_length -= len;
	}

	if (last && rs_length == 0)
		set_bit(RDMACTXT_F_LAST_CTXT, &ctxt->flags);
	else
		clear_bit(RDMACTXT_F_LAST_CTXT, &ctxt->flags);

	memset(&read_wr, 0, sizeof(read_wr));
	ctxt->cqe.done = svc_rdma_wc_read;
	read_wr.wr.wr_cqe = &ctxt->cqe;
	read_wr.wr.opcode = IB_WR_RDMA_READ;
	read_wr.wr.send_flags = IB_SEND_SIGNALED;
	read_wr.rkey = rs_handle;
	read_wr.remote_addr = rs_offset;
	read_wr.wr.sg_list = ctxt->sge;
	read_wr.wr.num_sge = pages_needed;

	ret = svc_rdma_send(xprt, &read_wr.wr);
	if (ret) {
		pr_err("svcrdma: Error %d posting RDMA_READ\n", ret);
		set_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags);
		goto err;
	}

	/* return current location in page array */
	*page_no = pg_no;
	*page_offset = pg_off;
	ret = read;
	atomic_inc(&rdma_stat_read);
	return ret;
 err:
	svc_rdma_unmap_dma(ctxt);
	svc_rdma_put_context(ctxt, 0);
	return ret;
}

/* Issue an RDMA_READ using an FRMR to map the data sink */
int rdma_read_chunk_frmr(struct svcxprt_rdma *xprt,
			 struct svc_rqst *rqstp,
			 struct svc_rdma_op_ctxt *head,
			 int *page_no,
			 u32 *page_offset,
			 u32 rs_handle,
			 u32 rs_length,
			 u64 rs_offset,
			 bool last)
{
	struct ib_rdma_wr read_wr;
	struct ib_send_wr inv_wr;
	struct ib_reg_wr reg_wr;
	u8 key;
	int nents = PAGE_ALIGN(*page_offset + rs_length) >> PAGE_SHIFT;
	struct svc_rdma_op_ctxt *ctxt = svc_rdma_get_context(xprt);
	struct svc_rdma_fastreg_mr *frmr = svc_rdma_get_frmr(xprt);
	int ret, read, pno, dma_nents, n;
	u32 pg_off = *page_offset;
	u32 pg_no = *page_no;

	if (IS_ERR(frmr))
		return -ENOMEM;

	ctxt->direction = DMA_FROM_DEVICE;
	ctxt->frmr = frmr;
	nents = min_t(unsigned int, nents, xprt->sc_frmr_pg_list_len);
	read = min_t(int, (nents << PAGE_SHIFT) - *page_offset, rs_length);

	frmr->direction = DMA_FROM_DEVICE;
	frmr->access_flags = (IB_ACCESS_LOCAL_WRITE|IB_ACCESS_REMOTE_WRITE);
	frmr->sg_nents = nents;

	for (pno = 0; pno < nents; pno++) {
		int len = min_t(int, rs_length, PAGE_SIZE - pg_off);

		head->arg.pages[pg_no] = rqstp->rq_arg.pages[pg_no];
		head->arg.page_len += len;
		head->arg.len += len;
		if (!pg_off)
			head->count++;

		sg_set_page(&frmr->sg[pno], rqstp->rq_arg.pages[pg_no],
			    len, pg_off);

		rqstp->rq_respages = &rqstp->rq_arg.pages[pg_no+1];
		rqstp->rq_next_page = rqstp->rq_respages + 1;

		/* adjust offset and wrap to next page if needed */
		pg_off += len;
		if (pg_off == PAGE_SIZE) {
			pg_off = 0;
			pg_no++;
		}
		rs_length -= len;
	}

	if (last && rs_length == 0)
		set_bit(RDMACTXT_F_LAST_CTXT, &ctxt->flags);
	else
		clear_bit(RDMACTXT_F_LAST_CTXT, &ctxt->flags);

	dma_nents = ib_dma_map_sg(xprt->sc_cm_id->device,
				  frmr->sg, frmr->sg_nents,
				  frmr->direction);
	if (!dma_nents) {
		pr_err("svcrdma: failed to dma map sg %p\n",
		       frmr->sg);
		return -ENOMEM;
	}

	n = ib_map_mr_sg(frmr->mr, frmr->sg, frmr->sg_nents, NULL, PAGE_SIZE);
	if (unlikely(n != frmr->sg_nents)) {
		pr_err("svcrdma: failed to map mr %p (%d/%d elements)\n",
		       frmr->mr, n, frmr->sg_nents);
		return n < 0 ? n : -EINVAL;
	}

	/* Bump the key */
	key = (u8)(frmr->mr->lkey & 0x000000FF);
	ib_update_fast_reg_key(frmr->mr, ++key);

	ctxt->sge[0].addr = frmr->mr->iova;
	ctxt->sge[0].lkey = frmr->mr->lkey;
	ctxt->sge[0].length = frmr->mr->length;
	ctxt->count = 1;
	ctxt->read_hdr = head;

	/* Prepare REG WR */
	ctxt->reg_cqe.done = svc_rdma_wc_reg;
	reg_wr.wr.wr_cqe = &ctxt->reg_cqe;
	reg_wr.wr.opcode = IB_WR_REG_MR;
	reg_wr.wr.send_flags = IB_SEND_SIGNALED;
	reg_wr.wr.num_sge = 0;
	reg_wr.mr = frmr->mr;
	reg_wr.key = frmr->mr->lkey;
	reg_wr.access = frmr->access_flags;
	reg_wr.wr.next = &read_wr.wr;

	/* Prepare RDMA_READ */
	memset(&read_wr, 0, sizeof(read_wr));
	ctxt->cqe.done = svc_rdma_wc_read;
	read_wr.wr.wr_cqe = &ctxt->cqe;
	read_wr.wr.send_flags = IB_SEND_SIGNALED;
	read_wr.rkey = rs_handle;
	read_wr.remote_addr = rs_offset;
	read_wr.wr.sg_list = ctxt->sge;
	read_wr.wr.num_sge = 1;
	if (xprt->sc_dev_caps & SVCRDMA_DEVCAP_READ_W_INV) {
		read_wr.wr.opcode = IB_WR_RDMA_READ_WITH_INV;
		read_wr.wr.ex.invalidate_rkey = ctxt->frmr->mr->lkey;
	} else {
		read_wr.wr.opcode = IB_WR_RDMA_READ;
		read_wr.wr.next = &inv_wr;
		/* Prepare invalidate */
		memset(&inv_wr, 0, sizeof(inv_wr));
		ctxt->inv_cqe.done = svc_rdma_wc_inv;
		inv_wr.wr_cqe = &ctxt->inv_cqe;
		inv_wr.opcode = IB_WR_LOCAL_INV;
		inv_wr.send_flags = IB_SEND_SIGNALED | IB_SEND_FENCE;
		inv_wr.ex.invalidate_rkey = frmr->mr->lkey;
	}

	/* Post the chain */
	ret = svc_rdma_send(xprt, &reg_wr.wr);
	if (ret) {
		pr_err("svcrdma: Error %d posting RDMA_READ\n", ret);
		set_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags);
		goto err;
	}

	/* return current location in page array */
	*page_no = pg_no;
	*page_offset = pg_off;
	ret = read;
	atomic_inc(&rdma_stat_read);
	return ret;
 err:
	svc_rdma_put_context(ctxt, 0);
	svc_rdma_put_frmr(xprt, frmr);
	return ret;
}

static unsigned int
rdma_rcl_chunk_count(struct rpcrdma_read_chunk *ch)
{
	unsigned int count;

	for (count = 0; ch->rc_discrim != xdr_zero; ch++)
		count++;
	return count;
}

/* If there was additional inline content, append it to the end of arg.pages.
 * Tail copy has to be done after the reader function has determined how many
 * pages are needed for RDMA READ.
 */
static int
rdma_copy_tail(struct svc_rqst *rqstp, struct svc_rdma_op_ctxt *head,
	       u32 position, u32 byte_count, u32 page_offset, int page_no)
{
	char *srcp, *destp;

	srcp = head->arg.head[0].iov_base + position;
	byte_count = head->arg.head[0].iov_len - position;
	if (byte_count > PAGE_SIZE) {
		dprintk("svcrdma: large tail unsupported\n");
		return 0;
	}

	/* Fit as much of the tail on the current page as possible */
	if (page_offset != PAGE_SIZE) {
		destp = page_address(rqstp->rq_arg.pages[page_no]);
		destp += page_offset;
		while (byte_count--) {
			*destp++ = *srcp++;
			page_offset++;
			if (page_offset == PAGE_SIZE && byte_count)
				goto more;
		}
		goto done;
	}

more:
	/* Fit the rest on the next page */
	page_no++;
	destp = page_address(rqstp->rq_arg.pages[page_no]);
	while (byte_count--)
		*destp++ = *srcp++;

	rqstp->rq_respages = &rqstp->rq_arg.pages[page_no+1];
	rqstp->rq_next_page = rqstp->rq_respages + 1;

done:
	byte_count = head->arg.head[0].iov_len - position;
	head->arg.page_len += byte_count;
	head->arg.len += byte_count;
	head->arg.buflen += byte_count;
	return 1;
}

/* Returns the address of the first read chunk or <nul> if no read chunk
 * is present
 */
static struct rpcrdma_read_chunk *
svc_rdma_get_read_chunk(struct rpcrdma_msg *rmsgp)
{
	struct rpcrdma_read_chunk *ch =
		(struct rpcrdma_read_chunk *)&rmsgp->rm_body.rm_chunks[0];

	if (ch->rc_discrim == xdr_zero)
		return NULL;
	return ch;
}

static int rdma_read_chunks(struct svcxprt_rdma *xprt,
			    struct rpcrdma_msg *rmsgp,
			    struct svc_rqst *rqstp,
			    struct svc_rdma_op_ctxt *head)
{
	int page_no, ret;
	struct rpcrdma_read_chunk *ch;
	u32 handle, page_offset, byte_count;
	u32 position;
	u64 rs_offset;
	bool last;

	/* If no read list is present, return 0 */
	ch = svc_rdma_get_read_chunk(rmsgp);
	if (!ch)
		return 0;

	if (rdma_rcl_chunk_count(ch) > RPCSVC_MAXPAGES)
		return -EINVAL;

	/* The request is completed when the RDMA_READs complete. The
	 * head context keeps all the pages that comprise the
	 * request.
	 */
	head->arg.head[0] = rqstp->rq_arg.head[0];
	head->arg.tail[0] = rqstp->rq_arg.tail[0];
	head->hdr_count = head->count;
	head->arg.page_base = 0;
	head->arg.page_len = 0;
	head->arg.len = rqstp->rq_arg.len;
	head->arg.buflen = rqstp->rq_arg.buflen;

	/* RDMA_NOMSG: RDMA READ data should land just after RDMA RECV data */
	position = be32_to_cpu(ch->rc_position);
	if (position == 0) {
		head->arg.pages = &head->pages[0];
		page_offset = head->byte_len;
	} else {
		head->arg.pages = &head->pages[head->count];
		page_offset = 0;
	}

	ret = 0;
	page_no = 0;
	for (; ch->rc_discrim != xdr_zero; ch++) {
		if (be32_to_cpu(ch->rc_position) != position)
			goto err;

		handle = be32_to_cpu(ch->rc_target.rs_handle),
		byte_count = be32_to_cpu(ch->rc_target.rs_length);
		xdr_decode_hyper((__be32 *)&ch->rc_target.rs_offset,
				 &rs_offset);

		while (byte_count > 0) {
			last = (ch + 1)->rc_discrim == xdr_zero;
			ret = xprt->sc_reader(xprt, rqstp, head,
					      &page_no, &page_offset,
					      handle, byte_count,
					      rs_offset, last);
			if (ret < 0)
				goto err;
			byte_count -= ret;
			rs_offset += ret;
			head->arg.buflen += ret;
		}
	}

	/* Read list may need XDR round-up (see RFC 5666, s. 3.7) */
	if (page_offset & 3) {
		u32 pad = 4 - (page_offset & 3);

		head->arg.tail[0].iov_len += pad;
		head->arg.len += pad;
		head->arg.buflen += pad;
		page_offset += pad;
	}

	ret = 1;
	if (position && position < head->arg.head[0].iov_len)
		ret = rdma_copy_tail(rqstp, head, position,
				     byte_count, page_offset, page_no);
	head->arg.head[0].iov_len = position;
	head->position = position;

 err:
	/* Detach arg pages. svc_recv will replenish them */
	for (page_no = 0;
	     &rqstp->rq_pages[page_no] < rqstp->rq_respages; page_no++)
		rqstp->rq_pages[page_no] = NULL;

	return ret;
}

static void rdma_read_complete(struct svc_rqst *rqstp,
			       struct svc_rdma_op_ctxt *head)
{
	int page_no;

	/* Copy RPC pages */
	for (page_no = 0; page_no < head->count; page_no++) {
		put_page(rqstp->rq_pages[page_no]);
		rqstp->rq_pages[page_no] = head->pages[page_no];
	}

	/* Adjustments made for RDMA_NOMSG type requests */
	if (head->position == 0) {
		if (head->arg.len <= head->sge[0].length) {
			head->arg.head[0].iov_len = head->arg.len -
							head->byte_len;
			head->arg.page_len = 0;
		} else {
			head->arg.head[0].iov_len = head->sge[0].length -
								head->byte_len;
			head->arg.page_len = head->arg.len -
						head->sge[0].length;
		}
	}

	/* Point rq_arg.pages past header */
	rqstp->rq_arg.pages = &rqstp->rq_pages[head->hdr_count];
	rqstp->rq_arg.page_len = head->arg.page_len;
	rqstp->rq_arg.page_base = head->arg.page_base;

	/* rq_respages starts after the last arg page */
	rqstp->rq_respages = &rqstp->rq_pages[page_no];
	rqstp->rq_next_page = rqstp->rq_respages + 1;

	/* Rebuild rq_arg head and tail. */
	rqstp->rq_arg.head[0] = head->arg.head[0];
	rqstp->rq_arg.tail[0] = head->arg.tail[0];
	rqstp->rq_arg.len = head->arg.len;
	rqstp->rq_arg.buflen = head->arg.buflen;
}

static void svc_rdma_send_error(struct svcxprt_rdma *xprt,
				__be32 *rdma_argp, int status)
{
	struct svc_rdma_op_ctxt *ctxt;
	__be32 *p, *err_msgp;
	unsigned int length;
	struct page *page;
	int ret;

	ret = svc_rdma_repost_recv(xprt, GFP_KERNEL);
	if (ret)
		return;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return;
	err_msgp = page_address(page);

	p = err_msgp;
	*p++ = *rdma_argp;
	*p++ = *(rdma_argp + 1);
	*p++ = xprt->sc_fc_credits;
	*p++ = rdma_error;
	if (status == -EPROTONOSUPPORT) {
		*p++ = err_vers;
		*p++ = rpcrdma_version;
		*p++ = rpcrdma_version;
	} else {
		*p++ = err_chunk;
	}
	length = (unsigned long)p - (unsigned long)err_msgp;

	/* Map transport header; no RPC message payload */
	ctxt = svc_rdma_get_context(xprt);
	ret = svc_rdma_map_reply_hdr(xprt, ctxt, err_msgp, length);
	if (ret) {
		dprintk("svcrdma: Error %d mapping send for protocol error\n",
			ret);
		return;
	}

	ret = svc_rdma_post_send_wr(xprt, ctxt, 1, 0);
	if (ret) {
		dprintk("svcrdma: Error %d posting send for protocol error\n",
			ret);
		svc_rdma_unmap_dma(ctxt);
		svc_rdma_put_context(ctxt, 1);
	}
}

/* By convention, backchannel calls arrive via rdma_msg type
 * messages, and never populate the chunk lists. This makes
 * the RPC/RDMA header small and fixed in size, so it is
 * straightforward to check the RPC header's direction field.
 */
static bool svc_rdma_is_backchannel_reply(struct svc_xprt *xprt,
					  __be32 *rdma_resp)
{
	__be32 *p;

	if (!xprt->xpt_bc_xprt)
		return false;

	p = rdma_resp + 3;
	if (*p++ != rdma_msg)
		return false;

	if (*p++ != xdr_zero)
		return false;
	if (*p++ != xdr_zero)
		return false;
	if (*p++ != xdr_zero)
		return false;

	/* XID sanity */
	if (*p++ != *rdma_resp)
		return false;
	/* call direction */
	if (*p == cpu_to_be32(RPC_CALL))
		return false;

	return true;
}

/*
 * Set up the rqstp thread context to point to the RQ buffer. If
 * necessary, pull additional data from the client with an RDMA_READ
 * request.
 */
int svc_rdma_recvfrom(struct svc_rqst *rqstp)
{
	struct svc_xprt *xprt = rqstp->rq_xprt;
	struct svcxprt_rdma *rdma_xprt =
		container_of(xprt, struct svcxprt_rdma, sc_xprt);
	struct svc_rdma_op_ctxt *ctxt = NULL;
	struct rpcrdma_msg *rmsgp;
	int ret = 0;

	dprintk("svcrdma: rqstp=%p\n", rqstp);

	spin_lock(&rdma_xprt->sc_rq_dto_lock);
	if (!list_empty(&rdma_xprt->sc_read_complete_q)) {
		ctxt = list_first_entry(&rdma_xprt->sc_read_complete_q,
					struct svc_rdma_op_ctxt, list);
		list_del(&ctxt->list);
		spin_unlock(&rdma_xprt->sc_rq_dto_lock);
		rdma_read_complete(rqstp, ctxt);
		goto complete;
	} else if (!list_empty(&rdma_xprt->sc_rq_dto_q)) {
		ctxt = list_first_entry(&rdma_xprt->sc_rq_dto_q,
					struct svc_rdma_op_ctxt, list);
		list_del(&ctxt->list);
	} else {
		atomic_inc(&rdma_stat_rq_starve);
		clear_bit(XPT_DATA, &xprt->xpt_flags);
		ctxt = NULL;
	}
	spin_unlock(&rdma_xprt->sc_rq_dto_lock);
	if (!ctxt) {
		/* This is the EAGAIN path. The svc_recv routine will
		 * return -EAGAIN, the nfsd thread will go to call into
		 * svc_recv again and we shouldn't be on the active
		 * transport list
		 */
		if (test_bit(XPT_CLOSE, &xprt->xpt_flags))
			goto defer;
		goto out;
	}
	dprintk("svcrdma: processing ctxt=%p on xprt=%p, rqstp=%p\n",
		ctxt, rdma_xprt, rqstp);
	atomic_inc(&rdma_stat_recv);

	/* Build up the XDR from the receive buffers. */
	rdma_build_arg_xdr(rqstp, ctxt, ctxt->byte_len);

	/* Decode the RDMA header. */
	rmsgp = (struct rpcrdma_msg *)rqstp->rq_arg.head[0].iov_base;
	ret = svc_rdma_xdr_decode_req(&rqstp->rq_arg);
	if (ret < 0)
		goto out_err;
	if (ret == 0)
		goto out_drop;
	rqstp->rq_xprt_hlen = ret;

	if (svc_rdma_is_backchannel_reply(xprt, &rmsgp->rm_xid)) {
		ret = svc_rdma_handle_bc_reply(xprt->xpt_bc_xprt,
					       &rmsgp->rm_xid,
					       &rqstp->rq_arg);
		svc_rdma_put_context(ctxt, 0);
		if (ret)
			goto repost;
		return ret;
	}

	/* Read read-list data. */
	ret = rdma_read_chunks(rdma_xprt, rmsgp, rqstp, ctxt);
	if (ret > 0) {
		/* read-list posted, defer until data received from client. */
		goto defer;
	} else if (ret < 0) {
		/* Post of read-list failed, free context. */
		svc_rdma_put_context(ctxt, 1);
		return 0;
	}

complete:
	ret = rqstp->rq_arg.head[0].iov_len
		+ rqstp->rq_arg.page_len
		+ rqstp->rq_arg.tail[0].iov_len;
	svc_rdma_put_context(ctxt, 0);
 out:
	dprintk("svcrdma: ret=%d, rq_arg.len=%u, "
		"rq_arg.head[0].iov_base=%p, rq_arg.head[0].iov_len=%zd\n",
		ret, rqstp->rq_arg.len,
		rqstp->rq_arg.head[0].iov_base,
		rqstp->rq_arg.head[0].iov_len);
	rqstp->rq_prot = IPPROTO_MAX;
	svc_xprt_copy_addrs(rqstp, xprt);
	return ret;

out_err:
	svc_rdma_send_error(rdma_xprt, &rmsgp->rm_xid, ret);
	svc_rdma_put_context(ctxt, 0);
	return 0;

defer:
	return 0;

out_drop:
	svc_rdma_put_context(ctxt, 1);
repost:
	return svc_rdma_repost_recv(rdma_xprt, GFP_KERNEL);
}
