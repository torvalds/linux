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

/*
 * Replace the pages in the rq_argpages array with the pages from the SGE in
 * the RDMA_RECV completion. The SGL should contain full pages up until the
 * last one.
 */
static void rdma_build_arg_xdr(struct svc_rqst *rqstp,
			       struct svc_rdma_op_ctxt *ctxt,
			       u32 byte_count)
{
	struct page *page;
	u32 bc;
	int sge_no;

	/* Swap the page in the SGE with the page in argpages */
	page = ctxt->pages[0];
	put_page(rqstp->rq_pages[0]);
	rqstp->rq_pages[0] = page;

	/* Set up the XDR head */
	rqstp->rq_arg.head[0].iov_base = page_address(page);
	rqstp->rq_arg.head[0].iov_len = min(byte_count, ctxt->sge[0].length);
	rqstp->rq_arg.len = byte_count;
	rqstp->rq_arg.buflen = byte_count;

	/* Compute bytes past head in the SGL */
	bc = byte_count - rqstp->rq_arg.head[0].iov_len;

	/* If data remains, store it in the pagelist */
	rqstp->rq_arg.page_len = bc;
	rqstp->rq_arg.page_base = 0;
	rqstp->rq_arg.pages = &rqstp->rq_pages[1];
	sge_no = 1;
	while (bc && sge_no < ctxt->count) {
		page = ctxt->pages[sge_no];
		put_page(rqstp->rq_pages[sge_no]);
		rqstp->rq_pages[sge_no] = page;
		bc -= min(bc, ctxt->sge[sge_no].length);
		rqstp->rq_arg.buflen += ctxt->sge[sge_no].length;
		sge_no++;
	}
	rqstp->rq_respages = &rqstp->rq_pages[sge_no];

	/* We should never run out of SGE because the limit is defined to
	 * support the max allowed RPC data length
	 */
	BUG_ON(bc && (sge_no == ctxt->count));
	BUG_ON((rqstp->rq_arg.head[0].iov_len + rqstp->rq_arg.page_len)
	       != byte_count);
	BUG_ON(rqstp->rq_arg.len != byte_count);

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

struct chunk_sge {
	int start;		/* sge no for this chunk */
	int count;		/* sge count for this chunk */
};

/* Encode a read-chunk-list as an array of IB SGE
 *
 * Assumptions:
 * - chunk[0]->position points to pages[0] at an offset of 0
 * - pages[] is not physically or virtually contigous and consists of
 *   PAGE_SIZE elements.
 *
 * Output:
 * - sge array pointing into pages[] array.
 * - chunk_sge array specifying sge index and count for each
 *   chunk in the read list
 *
 */
static int rdma_rcl_to_sge(struct svcxprt_rdma *xprt,
			   struct svc_rqst *rqstp,
			   struct svc_rdma_op_ctxt *head,
			   struct rpcrdma_msg *rmsgp,
			   struct ib_sge *sge,
			   struct chunk_sge *ch_sge_ary,
			   int ch_count,
			   int byte_count)
{
	int sge_no;
	int sge_bytes;
	int page_off;
	int page_no;
	int ch_bytes;
	int ch_no;
	struct rpcrdma_read_chunk *ch;

	sge_no = 0;
	page_no = 0;
	page_off = 0;
	ch = (struct rpcrdma_read_chunk *)&rmsgp->rm_body.rm_chunks[0];
	ch_no = 0;
	ch_bytes = ch->rc_target.rs_length;
	head->arg.head[0] = rqstp->rq_arg.head[0];
	head->arg.tail[0] = rqstp->rq_arg.tail[0];
	head->arg.pages = &head->pages[head->count];
	head->sge[0].length = head->count; /* save count of hdr pages */
	head->arg.page_base = 0;
	head->arg.page_len = ch_bytes;
	head->arg.len = rqstp->rq_arg.len + ch_bytes;
	head->arg.buflen = rqstp->rq_arg.buflen + ch_bytes;
	head->count++;
	ch_sge_ary[0].start = 0;
	while (byte_count) {
		sge_bytes = min_t(int, PAGE_SIZE-page_off, ch_bytes);
		sge[sge_no].addr =
			ib_dma_map_page(xprt->sc_cm_id->device,
					rqstp->rq_arg.pages[page_no],
					page_off, sge_bytes,
					DMA_FROM_DEVICE);
		sge[sge_no].length = sge_bytes;
		sge[sge_no].lkey = xprt->sc_phys_mr->lkey;
		/*
		 * Don't bump head->count here because the same page
		 * may be used by multiple SGE.
		 */
		head->arg.pages[page_no] = rqstp->rq_arg.pages[page_no];
		rqstp->rq_respages = &rqstp->rq_arg.pages[page_no+1];

		byte_count -= sge_bytes;
		ch_bytes -= sge_bytes;
		sge_no++;
		/*
		 * If all bytes for this chunk have been mapped to an
		 * SGE, move to the next SGE
		 */
		if (ch_bytes == 0) {
			ch_sge_ary[ch_no].count =
				sge_no - ch_sge_ary[ch_no].start;
			ch_no++;
			ch++;
			ch_sge_ary[ch_no].start = sge_no;
			ch_bytes = ch->rc_target.rs_length;
			/* If bytes remaining account for next chunk */
			if (byte_count) {
				head->arg.page_len += ch_bytes;
				head->arg.len += ch_bytes;
				head->arg.buflen += ch_bytes;
			}
		}
		/*
		 * If this SGE consumed all of the page, move to the
		 * next page
		 */
		if ((sge_bytes + page_off) == PAGE_SIZE) {
			page_no++;
			page_off = 0;
			/*
			 * If there are still bytes left to map, bump
			 * the page count
			 */
			if (byte_count)
				head->count++;
		} else
			page_off += sge_bytes;
	}
	BUG_ON(byte_count != 0);
	return sge_no;
}

static void rdma_set_ctxt_sge(struct svc_rdma_op_ctxt *ctxt,
			      struct ib_sge *sge,
			      u64 *sgl_offset,
			      int count)
{
	int i;

	ctxt->count = count;
	for (i = 0; i < count; i++) {
		ctxt->sge[i].addr = sge[i].addr;
		ctxt->sge[i].length = sge[i].length;
		*sgl_offset = *sgl_offset + sge[i].length;
	}
}

static int rdma_read_max_sge(struct svcxprt_rdma *xprt, int sge_count)
{
	if ((RDMA_TRANSPORT_IWARP ==
	     rdma_node_get_transport(xprt->sc_cm_id->
				     device->node_type))
	    && sge_count > 1)
		return 1;
	else
		return min_t(int, sge_count, xprt->sc_max_sge);
}

/*
 * Use RDMA_READ to read data from the advertised client buffer into the
 * XDR stream starting at rq_arg.head[0].iov_base.
 * Each chunk in the array
 * contains the following fields:
 * discrim      - '1', This isn't used for data placement
 * position     - The xdr stream offset (the same for every chunk)
 * handle       - RMR for client memory region
 * length       - data transfer length
 * offset       - 64 bit tagged offset in remote memory region
 *
 * On our side, we need to read into a pagelist. The first page immediately
 * follows the RPC header.
 *
 * This function returns 1 to indicate success. The data is not yet in
 * the pagelist and therefore the RPC request must be deferred. The
 * I/O completion will enqueue the transport again and
 * svc_rdma_recvfrom will complete the request.
 *
 * NOTE: The ctxt must not be touched after the last WR has been posted
 * because the I/O completion processing may occur on another
 * processor and free / modify the context. Ne touche pas!
 */
static int rdma_read_xdr(struct svcxprt_rdma *xprt,
			 struct rpcrdma_msg *rmsgp,
			 struct svc_rqst *rqstp,
			 struct svc_rdma_op_ctxt *hdr_ctxt)
{
	struct ib_send_wr read_wr;
	int err = 0;
	int ch_no;
	struct ib_sge *sge;
	int ch_count;
	int byte_count;
	int sge_count;
	u64 sgl_offset;
	struct rpcrdma_read_chunk *ch;
	struct svc_rdma_op_ctxt *ctxt = NULL;
	struct svc_rdma_op_ctxt *head;
	struct svc_rdma_op_ctxt *tmp_sge_ctxt;
	struct svc_rdma_op_ctxt *tmp_ch_ctxt;
	struct chunk_sge *ch_sge_ary;

	/* If no read list is present, return 0 */
	ch = svc_rdma_get_read_chunk(rmsgp);
	if (!ch)
		return 0;

	/* Allocate temporary contexts to keep SGE */
	BUG_ON(sizeof(struct ib_sge) < sizeof(struct chunk_sge));
	tmp_sge_ctxt = svc_rdma_get_context(xprt);
	sge = tmp_sge_ctxt->sge;
	tmp_ch_ctxt = svc_rdma_get_context(xprt);
	ch_sge_ary = (struct chunk_sge *)tmp_ch_ctxt->sge;

	svc_rdma_rcl_chunk_counts(ch, &ch_count, &byte_count);
	sge_count = rdma_rcl_to_sge(xprt, rqstp, hdr_ctxt, rmsgp,
				    sge, ch_sge_ary,
				    ch_count, byte_count);
	head = svc_rdma_get_context(xprt);
	sgl_offset = 0;
	ch_no = 0;

	for (ch = (struct rpcrdma_read_chunk *)&rmsgp->rm_body.rm_chunks[0];
	     ch->rc_discrim != 0; ch++, ch_no++) {
next_sge:
		if (!ctxt)
			ctxt = head;
		else {
			ctxt->next = svc_rdma_get_context(xprt);
			ctxt = ctxt->next;
		}
		ctxt->next = NULL;
		ctxt->direction = DMA_FROM_DEVICE;
		clear_bit(RDMACTXT_F_READ_DONE, &ctxt->flags);
		clear_bit(RDMACTXT_F_LAST_CTXT, &ctxt->flags);
		if ((ch+1)->rc_discrim == 0) {
			/*
			 * Checked in sq_cq_reap to see if we need to
			 * be enqueued
			 */
			set_bit(RDMACTXT_F_LAST_CTXT, &ctxt->flags);
			ctxt->next = hdr_ctxt;
			hdr_ctxt->next = head;
		}

		/* Prepare READ WR */
		memset(&read_wr, 0, sizeof read_wr);
		ctxt->wr_op = IB_WR_RDMA_READ;
		read_wr.wr_id = (unsigned long)ctxt;
		read_wr.opcode = IB_WR_RDMA_READ;
		read_wr.send_flags = IB_SEND_SIGNALED;
		read_wr.wr.rdma.rkey = ch->rc_target.rs_handle;
		read_wr.wr.rdma.remote_addr =
			get_unaligned(&(ch->rc_target.rs_offset)) +
			sgl_offset;
		read_wr.sg_list = &sge[ch_sge_ary[ch_no].start];
		read_wr.num_sge =
			rdma_read_max_sge(xprt, ch_sge_ary[ch_no].count);
		rdma_set_ctxt_sge(ctxt, &sge[ch_sge_ary[ch_no].start],
				  &sgl_offset,
				  read_wr.num_sge);

		/* Post the read */
		err = svc_rdma_send(xprt, &read_wr);
		if (err) {
			printk(KERN_ERR "svcrdma: Error posting send = %d\n",
			       err);
			/*
			 * Break the circular list so free knows when
			 * to stop if the error happened to occur on
			 * the last read
			 */
			ctxt->next = NULL;
			goto out;
		}
		atomic_inc(&rdma_stat_read);

		if (read_wr.num_sge < ch_sge_ary[ch_no].count) {
			ch_sge_ary[ch_no].count -= read_wr.num_sge;
			ch_sge_ary[ch_no].start += read_wr.num_sge;
			goto next_sge;
		}
		sgl_offset = 0;
		err = 0;
	}

 out:
	svc_rdma_put_context(tmp_sge_ctxt, 0);
	svc_rdma_put_context(tmp_ch_ctxt, 0);

	/* Detach arg pages. svc_recv will replenish them */
	for (ch_no = 0; &rqstp->rq_pages[ch_no] < rqstp->rq_respages; ch_no++)
		rqstp->rq_pages[ch_no] = NULL;

	/*
	 * Detach res pages. svc_release must see a resused count of
	 * zero or it will attempt to put them.
	 */
	while (rqstp->rq_resused)
		rqstp->rq_respages[--rqstp->rq_resused] = NULL;

	if (err) {
		printk(KERN_ERR "svcrdma : RDMA_READ error = %d\n", err);
		set_bit(XPT_CLOSE, &xprt->sc_xprt.xpt_flags);
		/* Free the linked list of read contexts */
		while (head != NULL) {
			ctxt = head->next;
			svc_rdma_put_context(head, 1);
			head = ctxt;
		}
		return 0;
	}

	return 1;
}

static int rdma_read_complete(struct svc_rqst *rqstp,
			      struct svc_rdma_op_ctxt *data)
{
	struct svc_rdma_op_ctxt *head = data->next;
	int page_no;
	int ret;

	BUG_ON(!head);

	/* Copy RPC pages */
	for (page_no = 0; page_no < head->count; page_no++) {
		put_page(rqstp->rq_pages[page_no]);
		rqstp->rq_pages[page_no] = head->pages[page_no];
	}
	/* Point rq_arg.pages past header */
	rqstp->rq_arg.pages = &rqstp->rq_pages[head->sge[0].length];
	rqstp->rq_arg.page_len = head->arg.page_len;
	rqstp->rq_arg.page_base = head->arg.page_base;

	/* rq_respages starts after the last arg page */
	rqstp->rq_respages = &rqstp->rq_arg.pages[page_no];
	rqstp->rq_resused = 0;

	/* Rebuild rq_arg head and tail. */
	rqstp->rq_arg.head[0] = head->arg.head[0];
	rqstp->rq_arg.tail[0] = head->arg.tail[0];
	rqstp->rq_arg.len = head->arg.len;
	rqstp->rq_arg.buflen = head->arg.buflen;

	/* XXX: What should this be? */
	rqstp->rq_prot = IPPROTO_MAX;

	/*
	 * Free the contexts we used to build the RDMA_READ. We have
	 * to be careful here because the context list uses the same
	 * next pointer used to chain the contexts associated with the
	 * RDMA_READ
	 */
	data->next = NULL;	/* terminate circular list */
	do {
		data = head->next;
		svc_rdma_put_context(head, 0);
		head = data;
	} while (head != NULL);

	ret = rqstp->rq_arg.head[0].iov_len
		+ rqstp->rq_arg.page_len
		+ rqstp->rq_arg.tail[0].iov_len;
	dprintk("svcrdma: deferred read ret=%d, rq_arg.len =%d, "
		"rq_arg.head[0].iov_base=%p, rq_arg.head[0].iov_len = %zd\n",
		ret, rqstp->rq_arg.len,	rqstp->rq_arg.head[0].iov_base,
		rqstp->rq_arg.head[0].iov_len);

	/* Indicate that we've consumed an RQ credit */
	rqstp->rq_xprt_ctxt = rqstp->rq_xprt;
	svc_xprt_received(rqstp->rq_xprt);
	return ret;
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
	int len;

	dprintk("svcrdma: rqstp=%p\n", rqstp);

	/*
	 * The rq_xprt_ctxt indicates if we've consumed an RQ credit
	 * or not. It is used in the rdma xpo_release_rqst function to
	 * determine whether or not to return an RQ WQE to the RQ.
	 */
	rqstp->rq_xprt_ctxt = NULL;

	spin_lock_bh(&rdma_xprt->sc_read_complete_lock);
	if (!list_empty(&rdma_xprt->sc_read_complete_q)) {
		ctxt = list_entry(rdma_xprt->sc_read_complete_q.next,
				  struct svc_rdma_op_ctxt,
				  dto_q);
		list_del_init(&ctxt->dto_q);
	}
	spin_unlock_bh(&rdma_xprt->sc_read_complete_lock);
	if (ctxt)
		return rdma_read_complete(rqstp, ctxt);

	spin_lock_bh(&rdma_xprt->sc_rq_dto_lock);
	if (!list_empty(&rdma_xprt->sc_rq_dto_q)) {
		ctxt = list_entry(rdma_xprt->sc_rq_dto_q.next,
				  struct svc_rdma_op_ctxt,
				  dto_q);
		list_del_init(&ctxt->dto_q);
	} else {
		atomic_inc(&rdma_stat_rq_starve);
		clear_bit(XPT_DATA, &xprt->xpt_flags);
		ctxt = NULL;
	}
	spin_unlock_bh(&rdma_xprt->sc_rq_dto_lock);
	if (!ctxt) {
		/* This is the EAGAIN path. The svc_recv routine will
		 * return -EAGAIN, the nfsd thread will go to call into
		 * svc_recv again and we shouldn't be on the active
		 * transport list
		 */
		if (test_bit(XPT_CLOSE, &xprt->xpt_flags))
			goto close_out;

		BUG_ON(ret);
		goto out;
	}
	dprintk("svcrdma: processing ctxt=%p on xprt=%p, rqstp=%p, status=%d\n",
		ctxt, rdma_xprt, rqstp, ctxt->wc_status);
	BUG_ON(ctxt->wc_status != IB_WC_SUCCESS);
	atomic_inc(&rdma_stat_recv);

	/* Build up the XDR from the receive buffers. */
	rdma_build_arg_xdr(rqstp, ctxt, ctxt->byte_len);

	/* Decode the RDMA header. */
	len = svc_rdma_xdr_decode_req(&rmsgp, rqstp);
	rqstp->rq_xprt_hlen = len;

	/* If the request is invalid, reply with an error */
	if (len < 0) {
		if (len == -ENOSYS)
			(void)svc_rdma_send_error(rdma_xprt, rmsgp, ERR_VERS);
		goto close_out;
	}

	/* Read read-list data. If we would need to wait, defer
	 * it. Not that in this case, we don't return the RQ credit
	 * until after the read completes.
	 */
	if (rdma_read_xdr(rdma_xprt, rmsgp, rqstp, ctxt)) {
		svc_xprt_received(xprt);
		return 0;
	}

	/* Indicate we've consumed an RQ credit */
	rqstp->rq_xprt_ctxt = rqstp->rq_xprt;

	ret = rqstp->rq_arg.head[0].iov_len
		+ rqstp->rq_arg.page_len
		+ rqstp->rq_arg.tail[0].iov_len;
	svc_rdma_put_context(ctxt, 0);
 out:
	dprintk("svcrdma: ret = %d, rq_arg.len =%d, "
		"rq_arg.head[0].iov_base=%p, rq_arg.head[0].iov_len = %zd\n",
		ret, rqstp->rq_arg.len,
		rqstp->rq_arg.head[0].iov_base,
		rqstp->rq_arg.head[0].iov_len);
	rqstp->rq_prot = IPPROTO_MAX;
	svc_xprt_copy_addrs(rqstp, xprt);
	svc_xprt_received(xprt);
	return ret;

 close_out:
	if (ctxt) {
		svc_rdma_put_context(ctxt, 1);
		/* Indicate we've consumed an RQ credit */
		rqstp->rq_xprt_ctxt = rqstp->rq_xprt;
	}
	dprintk("svcrdma: transport %p is closing\n", xprt);
	/*
	 * Set the close bit and enqueue it. svc_recv will see the
	 * close bit and call svc_xprt_delete
	 */
	set_bit(XPT_CLOSE, &xprt->xpt_flags);
	svc_xprt_received(xprt);
	return 0;
}
