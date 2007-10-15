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

#ifndef _LINUX_SUNRPC_RPC_RDMA_H
#define _LINUX_SUNRPC_RPC_RDMA_H

struct rpcrdma_segment {
	uint32_t rs_handle;	/* Registered memory handle */
	uint32_t rs_length;	/* Length of the chunk in bytes */
	uint64_t rs_offset;	/* Chunk virtual address or offset */
};

/*
 * read chunk(s), encoded as a linked list.
 */
struct rpcrdma_read_chunk {
	uint32_t rc_discrim;	/* 1 indicates presence */
	uint32_t rc_position;	/* Position in XDR stream */
	struct rpcrdma_segment rc_target;
};

/*
 * write chunk, and reply chunk.
 */
struct rpcrdma_write_chunk {
	struct rpcrdma_segment wc_target;
};

/*
 * write chunk(s), encoded as a counted array.
 */
struct rpcrdma_write_array {
	uint32_t wc_discrim;	/* 1 indicates presence */
	uint32_t wc_nchunks;	/* Array count */
	struct rpcrdma_write_chunk wc_array[0];
};

struct rpcrdma_msg {
	uint32_t rm_xid;	/* Mirrors the RPC header xid */
	uint32_t rm_vers;	/* Version of this protocol */
	uint32_t rm_credit;	/* Buffers requested/granted */
	uint32_t rm_type;	/* Type of message (enum rpcrdma_proc) */
	union {

		struct {			/* no chunks */
			uint32_t rm_empty[3];	/* 3 empty chunk lists */
		} rm_nochunks;

		struct {			/* no chunks and padded */
			uint32_t rm_align;	/* Padding alignment */
			uint32_t rm_thresh;	/* Padding threshold */
			uint32_t rm_pempty[3];	/* 3 empty chunk lists */
		} rm_padded;

		uint32_t rm_chunks[0];	/* read, write and reply chunks */

	} rm_body;
};

#define RPCRDMA_HDRLEN_MIN	28

enum rpcrdma_errcode {
	ERR_VERS = 1,
	ERR_CHUNK = 2
};

struct rpcrdma_err_vers {
	uint32_t rdma_vers_low;	/* Version range supported by peer */
	uint32_t rdma_vers_high;
};

enum rpcrdma_proc {
	RDMA_MSG = 0,		/* An RPC call or reply msg */
	RDMA_NOMSG = 1,		/* An RPC call or reply msg - separate body */
	RDMA_MSGP = 2,		/* An RPC call or reply msg with padding */
	RDMA_DONE = 3,		/* Client signals reply completion */
	RDMA_ERROR = 4		/* An RPC RDMA encoding error */
};

#endif				/* _LINUX_SUNRPC_RPC_RDMA_H */
