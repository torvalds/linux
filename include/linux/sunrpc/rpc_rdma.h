/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (c) 2015-2017 Oracle. All rights reserved.
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

#include <linux/types.h>
#include <linux/bitops.h>

#define RPCRDMA_VERSION		1
#define rpcrdma_version		cpu_to_be32(RPCRDMA_VERSION)

enum {
	RPCRDMA_V1_DEF_INLINE_SIZE	= 1024,
};

/*
 * XDR sizes, in quads
 */
enum {
	rpcrdma_fixed_maxsz	= 4,
	rpcrdma_segment_maxsz	= 4,
	rpcrdma_readseg_maxsz	= 1 + rpcrdma_segment_maxsz,
	rpcrdma_readchunk_maxsz	= 1 + rpcrdma_readseg_maxsz,
};

/*
 * Smallest RPC/RDMA header: rm_xid through rm_type, then rm_nochunks
 */
#define RPCRDMA_HDRLEN_MIN	(sizeof(__be32) * 7)
#define RPCRDMA_HDRLEN_ERR	(sizeof(__be32) * 5)

enum rpcrdma_errcode {
	ERR_VERS = 1,
	ERR_CHUNK = 2
};

enum rpcrdma_proc {
	RDMA_MSG = 0,		/* An RPC call or reply msg */
	RDMA_NOMSG = 1,		/* An RPC call or reply msg - separate body */
	RDMA_MSGP = 2,		/* An RPC call or reply msg with padding */
	RDMA_DONE = 3,		/* Client signals reply completion */
	RDMA_ERROR = 4		/* An RPC RDMA encoding error */
};

#define rdma_msg	cpu_to_be32(RDMA_MSG)
#define rdma_nomsg	cpu_to_be32(RDMA_NOMSG)
#define rdma_msgp	cpu_to_be32(RDMA_MSGP)
#define rdma_done	cpu_to_be32(RDMA_DONE)
#define rdma_error	cpu_to_be32(RDMA_ERROR)

#define err_vers	cpu_to_be32(ERR_VERS)
#define err_chunk	cpu_to_be32(ERR_CHUNK)

/*
 * Private extension to RPC-over-RDMA Version One.
 * Message passed during RDMA-CM connection set-up.
 *
 * Add new fields at the end, and don't permute existing
 * fields.
 */
struct rpcrdma_connect_private {
	__be32			cp_magic;
	u8			cp_version;
	u8			cp_flags;
	u8			cp_send_size;
	u8			cp_recv_size;
} __packed;

#define rpcrdma_cmp_magic	__cpu_to_be32(0xf6ab0e18)

enum {
	RPCRDMA_CMP_VERSION		= 1,
	RPCRDMA_CMP_F_SND_W_INV_OK	= BIT(0),
};

static inline u8
rpcrdma_encode_buffer_size(unsigned int size)
{
	return (size >> 10) - 1;
}

static inline unsigned int
rpcrdma_decode_buffer_size(u8 val)
{
	return ((unsigned int)val + 1) << 10;
}

/**
 * xdr_encode_rdma_segment - Encode contents of an RDMA segment
 * @p: Pointer into a send buffer
 * @handle: The RDMA handle to encode
 * @length: The RDMA length to encode
 * @offset: The RDMA offset to encode
 *
 * Return value:
 *   Pointer to the XDR position that follows the encoded RDMA segment
 */
static inline __be32 *xdr_encode_rdma_segment(__be32 *p, u32 handle,
					      u32 length, u64 offset)
{
	*p++ = cpu_to_be32(handle);
	*p++ = cpu_to_be32(length);
	return xdr_encode_hyper(p, offset);
}

/**
 * xdr_encode_read_segment - Encode contents of a Read segment
 * @p: Pointer into a send buffer
 * @position: The position to encode
 * @handle: The RDMA handle to encode
 * @length: The RDMA length to encode
 * @offset: The RDMA offset to encode
 *
 * Return value:
 *   Pointer to the XDR position that follows the encoded Read segment
 */
static inline __be32 *xdr_encode_read_segment(__be32 *p, u32 position,
					      u32 handle, u32 length,
					      u64 offset)
{
	*p++ = cpu_to_be32(position);
	return xdr_encode_rdma_segment(p, handle, length, offset);
}

/**
 * xdr_decode_rdma_segment - Decode contents of an RDMA segment
 * @p: Pointer to the undecoded RDMA segment
 * @handle: Upon return, the RDMA handle
 * @length: Upon return, the RDMA length
 * @offset: Upon return, the RDMA offset
 *
 * Return value:
 *   Pointer to the XDR item that follows the RDMA segment
 */
static inline __be32 *xdr_decode_rdma_segment(__be32 *p, u32 *handle,
					      u32 *length, u64 *offset)
{
	*handle = be32_to_cpup(p++);
	*length = be32_to_cpup(p++);
	return xdr_decode_hyper(p, offset);
}

/**
 * xdr_decode_read_segment - Decode contents of a Read segment
 * @p: Pointer to the undecoded Read segment
 * @position: Upon return, the segment's position
 * @handle: Upon return, the RDMA handle
 * @length: Upon return, the RDMA length
 * @offset: Upon return, the RDMA offset
 *
 * Return value:
 *   Pointer to the XDR item that follows the Read segment
 */
static inline __be32 *xdr_decode_read_segment(__be32 *p, u32 *position,
					      u32 *handle, u32 *length,
					      u64 *offset)
{
	*position = be32_to_cpup(p++);
	return xdr_decode_rdma_segment(p, handle, length, offset);
}

#endif				/* _LINUX_SUNRPC_RPC_RDMA_H */
