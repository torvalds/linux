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

#include <linux/sunrpc/xdr.h>
#include <linux/sunrpc/debug.h>
#include <asm/unaligned.h>
#include <linux/sunrpc/rpc_rdma.h>
#include <linux/sunrpc/svc_rdma.h>

#define RPCDBG_FACILITY	RPCDBG_SVCXPRT

/*
 * Decodes a read chunk list. The expected format is as follows:
 *    descrim  : xdr_one
 *    position : __be32 offset into XDR stream
 *    handle   : __be32 RKEY
 *    . . .
 *  end-of-list: xdr_zero
 */
static __be32 *decode_read_list(__be32 *va, __be32 *vaend)
{
	struct rpcrdma_read_chunk *ch = (struct rpcrdma_read_chunk *)va;

	while (ch->rc_discrim != xdr_zero) {
		if (((unsigned long)ch + sizeof(struct rpcrdma_read_chunk)) >
		    (unsigned long)vaend) {
			dprintk("svcrdma: vaend=%p, ch=%p\n", vaend, ch);
			return NULL;
		}
		ch++;
	}
	return &ch->rc_position;
}

/*
 * Decodes a write chunk list. The expected format is as follows:
 *    descrim  : xdr_one
 *    nchunks  : <count>
 *       handle   : __be32 RKEY           ---+
 *       length   : __be32 <len of segment>  |
 *       offset   : remove va                + <count>
 *       . . .                               |
 *                                        ---+
 */
static __be32 *decode_write_list(__be32 *va, __be32 *vaend)
{
	unsigned long start, end;
	int nchunks;

	struct rpcrdma_write_array *ary =
		(struct rpcrdma_write_array *)va;

	/* Check for not write-array */
	if (ary->wc_discrim == xdr_zero)
		return &ary->wc_nchunks;

	if ((unsigned long)ary + sizeof(struct rpcrdma_write_array) >
	    (unsigned long)vaend) {
		dprintk("svcrdma: ary=%p, vaend=%p\n", ary, vaend);
		return NULL;
	}
	nchunks = be32_to_cpu(ary->wc_nchunks);

	start = (unsigned long)&ary->wc_array[0];
	end = (unsigned long)vaend;
	if (nchunks < 0 ||
	    nchunks > (SIZE_MAX - start) / sizeof(struct rpcrdma_write_chunk) ||
	    (start + (sizeof(struct rpcrdma_write_chunk) * nchunks)) > end) {
		dprintk("svcrdma: ary=%p, wc_nchunks=%d, vaend=%p\n",
			ary, nchunks, vaend);
		return NULL;
	}
	/*
	 * rs_length is the 2nd 4B field in wc_target and taking its
	 * address skips the list terminator
	 */
	return &ary->wc_array[nchunks].wc_target.rs_length;
}

static __be32 *decode_reply_array(__be32 *va, __be32 *vaend)
{
	unsigned long start, end;
	int nchunks;
	struct rpcrdma_write_array *ary =
		(struct rpcrdma_write_array *)va;

	/* Check for no reply-array */
	if (ary->wc_discrim == xdr_zero)
		return &ary->wc_nchunks;

	if ((unsigned long)ary + sizeof(struct rpcrdma_write_array) >
	    (unsigned long)vaend) {
		dprintk("svcrdma: ary=%p, vaend=%p\n", ary, vaend);
		return NULL;
	}
	nchunks = be32_to_cpu(ary->wc_nchunks);

	start = (unsigned long)&ary->wc_array[0];
	end = (unsigned long)vaend;
	if (nchunks < 0 ||
	    nchunks > (SIZE_MAX - start) / sizeof(struct rpcrdma_write_chunk) ||
	    (start + (sizeof(struct rpcrdma_write_chunk) * nchunks)) > end) {
		dprintk("svcrdma: ary=%p, wc_nchunks=%d, vaend=%p\n",
			ary, nchunks, vaend);
		return NULL;
	}
	return (__be32 *)&ary->wc_array[nchunks];
}

/**
 * svc_rdma_xdr_decode_req - Parse incoming RPC-over-RDMA header
 * @rq_arg: Receive buffer
 *
 * On entry, xdr->head[0].iov_base points to first byte in the
 * RPC-over-RDMA header.
 *
 * On successful exit, head[0] points to first byte past the
 * RPC-over-RDMA header. For RDMA_MSG, this is the RPC message.
 * The length of the RPC-over-RDMA header is returned.
 */
int svc_rdma_xdr_decode_req(struct xdr_buf *rq_arg)
{
	struct rpcrdma_msg *rmsgp;
	__be32 *va, *vaend;
	unsigned int len;
	u32 hdr_len;

	/* Verify that there's enough bytes for header + something */
	if (rq_arg->len <= RPCRDMA_HDRLEN_ERR) {
		dprintk("svcrdma: header too short = %d\n",
			rq_arg->len);
		return -EINVAL;
	}

	rmsgp = (struct rpcrdma_msg *)rq_arg->head[0].iov_base;
	if (rmsgp->rm_vers != rpcrdma_version) {
		dprintk("%s: bad version %u\n", __func__,
			be32_to_cpu(rmsgp->rm_vers));
		return -EPROTONOSUPPORT;
	}

	switch (be32_to_cpu(rmsgp->rm_type)) {
	case RDMA_MSG:
	case RDMA_NOMSG:
		break;

	case RDMA_DONE:
		/* Just drop it */
		dprintk("svcrdma: dropping RDMA_DONE message\n");
		return 0;

	case RDMA_ERROR:
		/* Possible if this is a backchannel reply.
		 * XXX: We should cancel this XID, though.
		 */
		dprintk("svcrdma: dropping RDMA_ERROR message\n");
		return 0;

	case RDMA_MSGP:
		/* Pull in the extra for the padded case, bump our pointer */
		rmsgp->rm_body.rm_padded.rm_align =
			be32_to_cpu(rmsgp->rm_body.rm_padded.rm_align);
		rmsgp->rm_body.rm_padded.rm_thresh =
			be32_to_cpu(rmsgp->rm_body.rm_padded.rm_thresh);

		va = &rmsgp->rm_body.rm_padded.rm_pempty[4];
		rq_arg->head[0].iov_base = va;
		len = (u32)((unsigned long)va - (unsigned long)rmsgp);
		rq_arg->head[0].iov_len -= len;
		if (len > rq_arg->len)
			return -EINVAL;
		return len;
	default:
		dprintk("svcrdma: bad rdma procedure (%u)\n",
			be32_to_cpu(rmsgp->rm_type));
		return -EINVAL;
	}

	/* The chunk list may contain either a read chunk list or a write
	 * chunk list and a reply chunk list.
	 */
	va = &rmsgp->rm_body.rm_chunks[0];
	vaend = (__be32 *)((unsigned long)rmsgp + rq_arg->len);
	va = decode_read_list(va, vaend);
	if (!va) {
		dprintk("svcrdma: failed to decode read list\n");
		return -EINVAL;
	}
	va = decode_write_list(va, vaend);
	if (!va) {
		dprintk("svcrdma: failed to decode write list\n");
		return -EINVAL;
	}
	va = decode_reply_array(va, vaend);
	if (!va) {
		dprintk("svcrdma: failed to decode reply chunk\n");
		return -EINVAL;
	}

	rq_arg->head[0].iov_base = va;
	hdr_len = (unsigned long)va - (unsigned long)rmsgp;
	rq_arg->head[0].iov_len -= hdr_len;
	return hdr_len;
}

int svc_rdma_xdr_encode_error(struct svcxprt_rdma *xprt,
			      struct rpcrdma_msg *rmsgp,
			      enum rpcrdma_errcode err, __be32 *va)
{
	__be32 *startp = va;

	*va++ = rmsgp->rm_xid;
	*va++ = rmsgp->rm_vers;
	*va++ = xprt->sc_fc_credits;
	*va++ = rdma_error;
	*va++ = cpu_to_be32(err);
	if (err == ERR_VERS) {
		*va++ = rpcrdma_version;
		*va++ = rpcrdma_version;
	}

	return (int)((unsigned long)va - (unsigned long)startp);
}

/**
 * svc_rdma_xdr_get_reply_hdr_length - Get length of Reply transport header
 * @rdma_resp: buffer containing Reply transport header
 *
 * Returns length of transport header, in bytes.
 */
unsigned int svc_rdma_xdr_get_reply_hdr_len(__be32 *rdma_resp)
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

void svc_rdma_xdr_encode_write_list(struct rpcrdma_msg *rmsgp, int chunks)
{
	struct rpcrdma_write_array *ary;

	/* no read-list */
	rmsgp->rm_body.rm_chunks[0] = xdr_zero;

	/* write-array discrim */
	ary = (struct rpcrdma_write_array *)
		&rmsgp->rm_body.rm_chunks[1];
	ary->wc_discrim = xdr_one;
	ary->wc_nchunks = cpu_to_be32(chunks);

	/* write-list terminator */
	ary->wc_array[chunks].wc_target.rs_handle = xdr_zero;

	/* reply-array discriminator */
	ary->wc_array[chunks].wc_target.rs_length = xdr_zero;
}

void svc_rdma_xdr_encode_reply_array(struct rpcrdma_write_array *ary,
				 int chunks)
{
	ary->wc_discrim = xdr_one;
	ary->wc_nchunks = cpu_to_be32(chunks);
}

void svc_rdma_xdr_encode_array_chunk(struct rpcrdma_write_array *ary,
				     int chunk_no,
				     __be32 rs_handle,
				     __be64 rs_offset,
				     u32 write_len)
{
	struct rpcrdma_segment *seg = &ary->wc_array[chunk_no].wc_target;
	seg->rs_handle = rs_handle;
	seg->rs_offset = rs_offset;
	seg->rs_length = cpu_to_be32(write_len);
}
