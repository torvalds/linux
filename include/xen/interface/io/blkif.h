/******************************************************************************
 * blkif.h
 *
 * Unified block-device I/O interface for Xen guest OSes.
 *
 * Copyright (c) 2003-2004, Keir Fraser
 */

#ifndef __XEN_PUBLIC_IO_BLKIF_H__
#define __XEN_PUBLIC_IO_BLKIF_H__

#include "ring.h"
#include "../grant_table.h"

/*
 * Front->back notifications: When enqueuing a new request, sending a
 * notification can be made conditional on req_event (i.e., the generic
 * hold-off mechanism provided by the ring macros). Backends must set
 * req_event appropriately (e.g., using RING_FINAL_CHECK_FOR_REQUESTS()).
 *
 * Back->front notifications: When enqueuing a new response, sending a
 * notification can be made conditional on rsp_event (i.e., the generic
 * hold-off mechanism provided by the ring macros). Frontends must set
 * rsp_event appropriately (e.g., using RING_FINAL_CHECK_FOR_RESPONSES()).
 */

typedef uint16_t blkif_vdev_t;
typedef uint64_t blkif_sector_t;

/*
 * REQUEST CODES.
 */
#define BLKIF_OP_READ              0
#define BLKIF_OP_WRITE             1
/*
 * Recognised only if "feature-barrier" is present in backend xenbus info.
 * The "feature_barrier" node contains a boolean indicating whether barrier
 * requests are likely to succeed or fail. Either way, a barrier request
 * may fail at any time with BLKIF_RSP_EOPNOTSUPP if it is unsupported by
 * the underlying block-device hardware. The boolean simply indicates whether
 * or not it is worthwhile for the frontend to attempt barrier requests.
 * If a backend does not recognise BLKIF_OP_WRITE_BARRIER, it should *not*
 * create the "feature-barrier" node!
 */
#define BLKIF_OP_WRITE_BARRIER     2

/*
 * Maximum scatter/gather segments per request.
 * This is carefully chosen so that sizeof(struct blkif_ring) <= PAGE_SIZE.
 * NB. This could be 12 if the ring indexes weren't stored in the same page.
 */
#define BLKIF_MAX_SEGMENTS_PER_REQUEST 11

struct blkif_request {
	uint8_t        operation;    /* BLKIF_OP_???                         */
	uint8_t        nr_segments;  /* number of segments                   */
	blkif_vdev_t   handle;       /* only for read/write requests         */
	uint64_t       id;           /* private guest value, echoed in resp  */
	blkif_sector_t sector_number;/* start sector idx on disk (r/w only)  */
	struct blkif_request_segment {
		grant_ref_t gref;        /* reference to I/O buffer frame        */
		/* @first_sect: first sector in frame to transfer (inclusive).   */
		/* @last_sect: last sector in frame to transfer (inclusive).     */
		uint8_t     first_sect, last_sect;
	} seg[BLKIF_MAX_SEGMENTS_PER_REQUEST];
};

struct blkif_response {
	uint64_t        id;              /* copied from request */
	uint8_t         operation;       /* copied from request */
	int16_t         status;          /* BLKIF_RSP_???       */
};

/*
 * STATUS RETURN CODES.
 */
 /* Operation not supported (only happens on barrier writes). */
#define BLKIF_RSP_EOPNOTSUPP  -2
 /* Operation failed for some unspecified reason (-EIO). */
#define BLKIF_RSP_ERROR       -1
 /* Operation completed successfully. */
#define BLKIF_RSP_OKAY         0

/*
 * Generate blkif ring structures and types.
 */

DEFINE_RING_TYPES(blkif, struct blkif_request, struct blkif_response);

#define VDISK_CDROM        0x1
#define VDISK_REMOVABLE    0x2
#define VDISK_READONLY     0x4

#endif /* __XEN_PUBLIC_IO_BLKIF_H__ */
