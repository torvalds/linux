/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020, Oracle and/or its affiliates
 */

#ifndef SVC_RDMA_PCL_H
#define SVC_RDMA_PCL_H

#include <linux/list.h>

struct svc_rdma_segment {
	u32			rs_handle;
	u32			rs_length;
	u64			rs_offset;
};

struct svc_rdma_chunk {
	struct list_head	ch_list;

	u32			ch_position;
	u32			ch_length;
	u32			ch_payload_length;

	u32			ch_segcount;
	struct svc_rdma_segment	ch_segments[];
};

struct svc_rdma_pcl {
	unsigned int		cl_count;
	struct list_head	cl_chunks;
};

/**
 * pcl_init - Initialize a parsed chunk list
 * @pcl: parsed chunk list to initialize
 *
 */
static inline void pcl_init(struct svc_rdma_pcl *pcl)
{
	INIT_LIST_HEAD(&pcl->cl_chunks);
}

/**
 * pcl_is_empty - Return true if parsed chunk list is empty
 * @pcl: parsed chunk list
 *
 */
static inline bool pcl_is_empty(const struct svc_rdma_pcl *pcl)
{
	return list_empty(&pcl->cl_chunks);
}

/**
 * pcl_first_chunk - Return first chunk in a parsed chunk list
 * @pcl: parsed chunk list
 *
 * Returns the first chunk in the list, or NULL if the list is empty.
 */
static inline struct svc_rdma_chunk *
pcl_first_chunk(const struct svc_rdma_pcl *pcl)
{
	if (pcl_is_empty(pcl))
		return NULL;
	return list_first_entry(&pcl->cl_chunks, struct svc_rdma_chunk,
				ch_list);
}

/**
 * pcl_next_chunk - Return next chunk in a parsed chunk list
 * @pcl: a parsed chunk list
 * @chunk: chunk in @pcl
 *
 * Returns the next chunk in the list, or NULL if @chunk is already last.
 */
static inline struct svc_rdma_chunk *
pcl_next_chunk(const struct svc_rdma_pcl *pcl, struct svc_rdma_chunk *chunk)
{
	if (list_is_last(&chunk->ch_list, &pcl->cl_chunks))
		return NULL;
	return list_next_entry(chunk, ch_list);
}

/**
 * pcl_for_each_chunk - Iterate over chunks in a parsed chunk list
 * @pos: the loop cursor
 * @pcl: a parsed chunk list
 */
#define pcl_for_each_chunk(pos, pcl) \
	for (pos = list_first_entry(&(pcl)->cl_chunks, struct svc_rdma_chunk, ch_list); \
	     &pos->ch_list != &(pcl)->cl_chunks; \
	     pos = list_next_entry(pos, ch_list))

/**
 * pcl_for_each_segment - Iterate over segments in a parsed chunk
 * @pos: the loop cursor
 * @chunk: a parsed chunk
 */
#define pcl_for_each_segment(pos, chunk) \
	for (pos = &(chunk)->ch_segments[0]; \
	     pos <= &(chunk)->ch_segments[(chunk)->ch_segcount - 1]; \
	     pos++)

/**
 * pcl_chunk_end_offset - Return offset of byte range following @chunk
 * @chunk: chunk in @pcl
 *
 * Returns starting offset of the region just after @chunk
 */
static inline unsigned int
pcl_chunk_end_offset(const struct svc_rdma_chunk *chunk)
{
	return xdr_align_size(chunk->ch_position + chunk->ch_payload_length);
}

struct svc_rdma_recv_ctxt;

extern void pcl_free(struct svc_rdma_pcl *pcl);
extern bool pcl_alloc_call(struct svc_rdma_recv_ctxt *rctxt, __be32 *p);
extern bool pcl_alloc_read(struct svc_rdma_recv_ctxt *rctxt, __be32 *p);
extern bool pcl_alloc_write(struct svc_rdma_recv_ctxt *rctxt,
			    struct svc_rdma_pcl *pcl, __be32 *p);
extern int pcl_process_nonpayloads(const struct svc_rdma_pcl *pcl,
				   const struct xdr_buf *xdr,
				   int (*actor)(const struct xdr_buf *,
						void *),
				   void *data);

#endif	/* SVC_RDMA_PCL_H */
