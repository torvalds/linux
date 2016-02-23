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

int svc_rdma_xdr_decode_req(struct rpcrdma_msg **rdma_req,
			    struct svc_rqst *rqstp)
{
	struct rpcrdma_msg *rmsgp = NULL;
	__be32 *va, *vaend;
	u32 hdr_len;

	rmsgp = (struct rpcrdma_msg *)rqstp->rq_arg.head[0].iov_base;

	/* Verify that there's enough bytes for header + something */
	if (rqstp->rq_arg.len <= RPCRDMA_HDRLEN_MIN) {
		dprintk("svcrdma: header too short = %d\n",
			rqstp->rq_arg.len);
		return -EINVAL;
	}

	if (rmsgp->rm_vers != rpcrdma_version)
		return -ENOSYS;

	/* Pull in the extra for the padded case and bump our pointer */
	if (rmsgp->rm_type == rdma_msgp) {
		int hdrlen;

		rmsgp->rm_body.rm_padded.rm_align =
			be32_to_cpu(rmsgp->rm_body.rm_padded.rm_align);
		rmsgp->rm_body.rm_padded.rm_thresh =
			be32_to_cpu(rmsgp->rm_body.rm_padded.rm_thresh);

		va = &rmsgp->rm_body.rm_padded.rm_pempty[4];
		rqstp->rq_arg.head[0].iov_base = va;
		hdrlen = (u32)((unsigned long)va - (unsigned long)rmsgp);
		rqstp->rq_arg.head[0].iov_len -= hdrlen;
		if (hdrlen > rqstp->rq_arg.len)
			return -EINVAL;
		return hdrlen;
	}

	/* The chunk list may contain either a read chunk list or a write
	 * chunk list and a reply chunk list.
	 */
	va = &rmsgp->rm_body.rm_chunks[0];
	vaend = (__be32 *)((unsigned long)rmsgp + rqstp->rq_arg.len);
	va = decode_read_list(va, vaend);
	if (!va)
		return -EINVAL;
	va = decode_write_list(va, vaend);
	if (!va)
		return -EINVAL;
	va = decode_reply_array(va, vaend);
	if (!va)
		return -EINVAL;

	rqstp->rq_arg.head[0].iov_base = va;
	hdr_len = (unsigned long)va - (unsigned long)rmsgp;
	rqstp->rq_arg.head[0].iov_len -= hdr_len;

	*rdma_req = rmsgp;
	return hdr_len;
}

int svc_rdma_xdr_encode_error(struct svcxprt_rdma *xprt,
			      struct rpcrdma_msg *rmsgp,
			      enum rpcrdma_errcode err, __be32 *va)
{
	__be32 *startp = va;

	*va++ = rmsgp->rm_xid;
	*va++ = rmsgp->rm_vers;
	*va++ = cpu_to_be32(xprt->sc_max_requests);
	*va++ = rdma_error;
	*va++ = cpu_to_be32(err);
	if (err == ERR_VERS) {
		*va++ = rpcrdma_version;
		*va++ = rpcrdma_version;
	}

	return (int)((unsigned long)va - (unsigned long)startp);
}

int svc_rdma_xdr_get_reply_hdr_len(struct rpcrdma_msg *rmsgp)
{
	struct rpcrdma_write_array *wr_ary;

	/* There is no read-list in a reply */

	/* skip write list */
	wr_ary = (struct rpcrdma_write_array *)
		&rmsgp->rm_body.rm_chunks[1];
	if (wr_ary->wc_discrim)
		wr_ary = (struct rpcrdma_write_array *)
			&wr_ary->wc_array[be32_to_cpu(wr_ary->wc_nchunks)].
			wc_target.rs_length;
	else
		wr_ary = (struct rpcrdma_write_array *)
			&wr_ary->wc_nchunks;

	/* skip reply array */
	if (wr_ary->wc_discrim)
		wr_ary = (struct rpcrdma_write_array *)
			&wr_ary->wc_array[be32_to_cpu(wr_ary->wc_nchunks)];
	else
		wr_ary = (struct rpcrdma_write_array *)
			&wr_ary->wc_nchunks;

	return (unsigned long) wr_ary - (unsigned long) rmsgp;
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

void svc_rdma_xdr_encode_reply_header(struct svcxprt_rdma *xprt,
				  struct rpcrdma_msg *rdma_argp,
				  struct rpcrdma_msg *rdma_resp,
				  enum rpcrdma_proc rdma_type)
{
	rdma_resp->rm_xid = rdma_argp->rm_xid;
	rdma_resp->rm_vers = rdma_argp->rm_vers;
	rdma_resp->rm_credit = cpu_to_be32(xprt->sc_max_requests);
	rdma_resp->rm_type = cpu_to_be32(rdma_type);

	/* Encode <nul> chunks lists */
	rdma_resp->rm_body.rm_chunks[0] = xdr_zero;
	rdma_resp->rm_body.rm_chunks[1] = xdr_zero;
	rdma_resp->rm_body.rm_chunks[2] = xdr_zero;
}
