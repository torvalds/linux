// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Oracle. All rights reserved.
 */

#include <linux/sunrpc/svc_rdma.h>
#include <linux/sunrpc/rpc_rdma.h>

#include "xprt_rdma.h"
#include <trace/events/rpcrdma.h>

/**
 * pcl_free - Release all memory associated with a parsed chunk list
 * @pcl: parsed chunk list
 *
 */
void pcl_free(struct svc_rdma_pcl *pcl)
{
	while (!list_empty(&pcl->cl_chunks)) {
		struct svc_rdma_chunk *chunk;

		chunk = pcl_first_chunk(pcl);
		list_del(&chunk->ch_list);
		kfree(chunk);
	}
}

static struct svc_rdma_chunk *pcl_alloc_chunk(u32 segcount, u32 position)
{
	struct svc_rdma_chunk *chunk;

	chunk = kmalloc(struct_size(chunk, ch_segments, segcount), GFP_KERNEL);
	if (!chunk)
		return NULL;

	chunk->ch_position = position;
	chunk->ch_length = 0;
	chunk->ch_payload_length = 0;
	chunk->ch_segcount = 0;
	return chunk;
}

static struct svc_rdma_chunk *
pcl_lookup_position(struct svc_rdma_pcl *pcl, u32 position)
{
	struct svc_rdma_chunk *pos;

	pcl_for_each_chunk(pos, pcl) {
		if (pos->ch_position == position)
			return pos;
	}
	return NULL;
}

static void pcl_insert_position(struct svc_rdma_pcl *pcl,
				struct svc_rdma_chunk *chunk)
{
	struct svc_rdma_chunk *pos;

	pcl_for_each_chunk(pos, pcl) {
		if (pos->ch_position > chunk->ch_position)
			break;
	}
	__list_add(&chunk->ch_list, pos->ch_list.prev, &pos->ch_list);
	pcl->cl_count++;
}

static void pcl_set_read_segment(const struct svc_rdma_recv_ctxt *rctxt,
				 struct svc_rdma_chunk *chunk,
				 u32 handle, u32 length, u64 offset)
{
	struct svc_rdma_segment *segment;

	segment = &chunk->ch_segments[chunk->ch_segcount];
	segment->rs_handle = handle;
	segment->rs_length = length;
	segment->rs_offset = offset;

	trace_svcrdma_decode_rseg(&rctxt->rc_cid, chunk, segment);

	chunk->ch_length += length;
	chunk->ch_segcount++;
}

/**
 * pcl_alloc_call - Construct a parsed chunk list for the Call body
 * @rctxt: Ingress receive context
 * @p: Start of an un-decoded Read list
 *
 * Assumptions:
 * - The incoming Read list has already been sanity checked.
 * - cl_count is already set to the number of segments in
 *   the un-decoded list.
 * - The list might not be in order by position.
 *
 * Return values:
 *       %true: Parsed chunk list was successfully constructed, and
 *              cl_count is updated to be the number of chunks (ie.
 *              unique positions) in the Read list.
 *      %false: Memory allocation failed.
 */
bool pcl_alloc_call(struct svc_rdma_recv_ctxt *rctxt, __be32 *p)
{
	struct svc_rdma_pcl *pcl = &rctxt->rc_call_pcl;
	unsigned int i, segcount = pcl->cl_count;

	pcl->cl_count = 0;
	for (i = 0; i < segcount; i++) {
		struct svc_rdma_chunk *chunk;
		u32 position, handle, length;
		u64 offset;

		p++;	/* skip the list discriminator */
		p = xdr_decode_read_segment(p, &position, &handle,
					    &length, &offset);
		if (position != 0)
			continue;

		if (pcl_is_empty(pcl)) {
			chunk = pcl_alloc_chunk(segcount, position);
			if (!chunk)
				return false;
			pcl_insert_position(pcl, chunk);
		} else {
			chunk = list_first_entry(&pcl->cl_chunks,
						 struct svc_rdma_chunk,
						 ch_list);
		}

		pcl_set_read_segment(rctxt, chunk, handle, length, offset);
	}

	return true;
}

/**
 * pcl_alloc_read - Construct a parsed chunk list for normal Read chunks
 * @rctxt: Ingress receive context
 * @p: Start of an un-decoded Read list
 *
 * Assumptions:
 * - The incoming Read list has already been sanity checked.
 * - cl_count is already set to the number of segments in
 *   the un-decoded list.
 * - The list might not be in order by position.
 *
 * Return values:
 *       %true: Parsed chunk list was successfully constructed, and
 *              cl_count is updated to be the number of chunks (ie.
 *              unique position values) in the Read list.
 *      %false: Memory allocation failed.
 *
 * TODO:
 * - Check for chunk range overlaps
 */
bool pcl_alloc_read(struct svc_rdma_recv_ctxt *rctxt, __be32 *p)
{
	struct svc_rdma_pcl *pcl = &rctxt->rc_read_pcl;
	unsigned int i, segcount = pcl->cl_count;

	pcl->cl_count = 0;
	for (i = 0; i < segcount; i++) {
		struct svc_rdma_chunk *chunk;
		u32 position, handle, length;
		u64 offset;

		p++;	/* skip the list discriminator */
		p = xdr_decode_read_segment(p, &position, &handle,
					    &length, &offset);
		if (position == 0)
			continue;

		chunk = pcl_lookup_position(pcl, position);
		if (!chunk) {
			chunk = pcl_alloc_chunk(segcount, position);
			if (!chunk)
				return false;
			pcl_insert_position(pcl, chunk);
		}

		pcl_set_read_segment(rctxt, chunk, handle, length, offset);
	}

	return true;
}

/**
 * pcl_alloc_write - Construct a parsed chunk list from a Write list
 * @rctxt: Ingress receive context
 * @pcl: Parsed chunk list to populate
 * @p: Start of an un-decoded Write list
 *
 * Assumptions:
 * - The incoming Write list has already been sanity checked, and
 * - cl_count is set to the number of chunks in the un-decoded list.
 *
 * Return values:
 *       %true: Parsed chunk list was successfully constructed.
 *      %false: Memory allocation failed.
 */
bool pcl_alloc_write(struct svc_rdma_recv_ctxt *rctxt,
		     struct svc_rdma_pcl *pcl, __be32 *p)
{
	struct svc_rdma_segment *segment;
	struct svc_rdma_chunk *chunk;
	unsigned int i, j;
	u32 segcount;

	for (i = 0; i < pcl->cl_count; i++) {
		p++;	/* skip the list discriminator */
		segcount = be32_to_cpup(p++);

		chunk = pcl_alloc_chunk(segcount, 0);
		if (!chunk)
			return false;
		list_add_tail(&chunk->ch_list, &pcl->cl_chunks);

		for (j = 0; j < segcount; j++) {
			segment = &chunk->ch_segments[j];
			p = xdr_decode_rdma_segment(p, &segment->rs_handle,
						    &segment->rs_length,
						    &segment->rs_offset);
			trace_svcrdma_decode_wseg(&rctxt->rc_cid, chunk, j);

			chunk->ch_length += segment->rs_length;
			chunk->ch_segcount++;
		}
	}
	return true;
}

static int pcl_process_region(const struct xdr_buf *xdr,
			      unsigned int offset, unsigned int length,
			      int (*actor)(const struct xdr_buf *, void *),
			      void *data)
{
	struct xdr_buf subbuf;

	if (!length)
		return 0;
	if (xdr_buf_subsegment(xdr, &subbuf, offset, length))
		return -EMSGSIZE;
	return actor(&subbuf, data);
}

/**
 * pcl_process_nonpayloads - Process non-payload regions inside @xdr
 * @pcl: Chunk list to process
 * @xdr: xdr_buf to process
 * @actor: Function to invoke on each non-payload region
 * @data: Arguments for @actor
 *
 * This mechanism must ignore not only result payloads that were already
 * sent via RDMA Write, but also XDR padding for those payloads that
 * the upper layer has added.
 *
 * Assumptions:
 *  The xdr->len and ch_position fields are aligned to 4-byte multiples.
 *
 * Returns:
 *   On success, zero,
 *   %-EMSGSIZE on XDR buffer overflow, or
 *   The return value of @actor
 */
int pcl_process_nonpayloads(const struct svc_rdma_pcl *pcl,
			    const struct xdr_buf *xdr,
			    int (*actor)(const struct xdr_buf *, void *),
			    void *data)
{
	struct svc_rdma_chunk *chunk, *next;
	unsigned int start;
	int ret;

	chunk = pcl_first_chunk(pcl);

	/* No result payloads were generated */
	if (!chunk || !chunk->ch_payload_length)
		return actor(xdr, data);

	/* Process the region before the first result payload */
	ret = pcl_process_region(xdr, 0, chunk->ch_position, actor, data);
	if (ret < 0)
		return ret;

	/* Process the regions between each middle result payload */
	while ((next = pcl_next_chunk(pcl, chunk))) {
		if (!next->ch_payload_length)
			break;

		start = pcl_chunk_end_offset(chunk);
		ret = pcl_process_region(xdr, start, next->ch_position - start,
					 actor, data);
		if (ret < 0)
			return ret;

		chunk = next;
	}

	/* Process the region after the last result payload */
	start = pcl_chunk_end_offset(chunk);
	ret = pcl_process_region(xdr, start, xdr->len - start, actor, data);
	if (ret < 0)
		return ret;

	return 0;
}
