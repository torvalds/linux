/******************************************************************************
 * blkif.h
 *
 * Unified block-device I/O interface for Xen guest OSes.
 *
 * Copyright (c) 2003-2004, Keir Fraser
 */

#ifndef __XEN_PUBLIC_IO_BLKIF_H__
#define __XEN_PUBLIC_IO_BLKIF_H__

#include <xen/interface/io/ring.h>
#include <xen/interface/grant_table.h>

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
 * Recognised if "feature-flush-cache" is present in backend xenbus
 * info.  A flush will ask the underlying storage hardware to flush its
 * non-volatile caches as appropriate.  The "feature-flush-cache" node
 * contains a boolean indicating whether flush requests are likely to
 * succeed or fail. Either way, a flush request may fail at any time
 * with BLKIF_RSP_EOPNOTSUPP if it is unsupported by the underlying
 * block-device hardware. The boolean simply indicates whether or not it
 * is worthwhile for the frontend to attempt flushes.  If a backend does
 * not recognise BLKIF_OP_WRITE_FLUSH_CACHE, it should *not* create the
 * "feature-flush-cache" node!
 */
#define BLKIF_OP_FLUSH_DISKCACHE   3

/*
 * Recognised only if "feature-discard" is present in backend xenbus info.
 * The "feature-discard" node contains a boolean indicating whether trim
 * (ATA) or unmap (SCSI) - conviently called discard requests are likely
 * to succeed or fail. Either way, a discard request
 * may fail at any time with BLKIF_RSP_EOPNOTSUPP if it is unsupported by
 * the underlying block-device hardware. The boolean simply indicates whether
 * or not it is worthwhile for the frontend to attempt discard requests.
 * If a backend does not recognise BLKIF_OP_DISCARD, it should *not*
 * create the "feature-discard" node!
 *
 * Discard operation is a request for the underlying block device to mark
 * extents to be erased. However, discard does not guarantee that the blocks
 * will be erased from the device - it is just a hint to the device
 * controller that these blocks are no longer in use. What the device
 * controller does with that information is left to the controller.
 * Discard operations are passed with sector_number as the
 * sector index to begin discard operations at and nr_sectors as the number of
 * sectors to be discarded. The specified sectors should be discarded if the
 * underlying block device supports trim (ATA) or unmap (SCSI) operations,
 * or a BLKIF_RSP_EOPNOTSUPP  should be returned.
 * More information about trim/unmap operations at:
 * http://t13.org/Documents/UploadedDocuments/docs2008/
 *     e07154r6-Data_Set_Management_Proposal_for_ATA-ACS2.doc
 * http://www.seagate.com/staticfiles/support/disc/manuals/
 *     Interface%20manuals/100293068c.pdf
 * The backend can optionally provide three extra XenBus attributes to
 * further optimize the discard functionality:
 * 'discard-aligment' - Devices that support discard functionality may
 * internally allocate space in units that are bigger than the exported
 * logical block size. The discard-alignment parameter indicates how many bytes
 * the beginning of the partition is offset from the internal allocation unit's
 * natural alignment.
 * 'discard-granularity'  - Devices that support discard functionality may
 * internally allocate space using units that are bigger than the logical block
 * size. The discard-granularity parameter indicates the size of the internal
 * allocation unit in bytes if reported by the device. Otherwise the
 * discard-granularity will be set to match the device's physical block size.
 * 'discard-secure' - All copies of the discarded sectors (potentially created
 * by garbage collection) must also be erased.  To use this feature, the flag
 * BLKIF_DISCARD_SECURE must be set in the blkif_request_trim.
 */
#define BLKIF_OP_DISCARD           5

/*
 * Maximum scatter/gather segments per request.
 * This is carefully chosen so that sizeof(struct blkif_ring) <= PAGE_SIZE.
 * NB. This could be 12 if the ring indexes weren't stored in the same page.
 */
#define BLKIF_MAX_SEGMENTS_PER_REQUEST 11

struct blkif_request_rw {
	uint8_t        nr_segments;  /* number of segments                   */
	blkif_vdev_t   handle;       /* only for read/write requests         */
#ifdef CONFIG_X86_64
	uint32_t       _pad1;	     /* offsetof(blkif_request,u.rw.id) == 8 */
#endif
	uint64_t       id;           /* private guest value, echoed in resp  */
	blkif_sector_t sector_number;/* start sector idx on disk (r/w only)  */
	struct blkif_request_segment {
		grant_ref_t gref;        /* reference to I/O buffer frame        */
		/* @first_sect: first sector in frame to transfer (inclusive).   */
		/* @last_sect: last sector in frame to transfer (inclusive).     */
		uint8_t     first_sect, last_sect;
	} seg[BLKIF_MAX_SEGMENTS_PER_REQUEST];
} __attribute__((__packed__));

struct blkif_request_discard {
	uint8_t        flag;         /* BLKIF_DISCARD_SECURE or zero.        */
#define BLKIF_DISCARD_SECURE (1<<0)  /* ignored if discard-secure=0          */
	blkif_vdev_t   _pad1;        /* only for read/write requests         */
#ifdef CONFIG_X86_64
	uint32_t       _pad2;        /* offsetof(blkif_req..,u.discard.id)==8*/
#endif
	uint64_t       id;           /* private guest value, echoed in resp  */
	blkif_sector_t sector_number;
	uint64_t       nr_sectors;
	uint8_t        _pad3;
} __attribute__((__packed__));

struct blkif_request_other {
	uint8_t      _pad1;
	blkif_vdev_t _pad2;        /* only for read/write requests         */
#ifdef CONFIG_X86_64
	uint32_t     _pad3;        /* offsetof(blkif_req..,u.other.id)==8*/
#endif
	uint64_t     id;           /* private guest value, echoed in resp  */
} __attribute__((__packed__));

struct blkif_request {
	uint8_t        operation;    /* BLKIF_OP_???                         */
	union {
		struct blkif_request_rw rw;
		struct blkif_request_discard discard;
		struct blkif_request_other other;
	} u;
} __attribute__((__packed__));

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

/* Xen-defined major numbers for virtual disks, they look strangely
 * familiar */
#define XEN_IDE0_MAJOR	3
#define XEN_IDE1_MAJOR	22
#define XEN_SCSI_DISK0_MAJOR	8
#define XEN_SCSI_DISK1_MAJOR	65
#define XEN_SCSI_DISK2_MAJOR	66
#define XEN_SCSI_DISK3_MAJOR	67
#define XEN_SCSI_DISK4_MAJOR	68
#define XEN_SCSI_DISK5_MAJOR	69
#define XEN_SCSI_DISK6_MAJOR	70
#define XEN_SCSI_DISK7_MAJOR	71
#define XEN_SCSI_DISK8_MAJOR	128
#define XEN_SCSI_DISK9_MAJOR	129
#define XEN_SCSI_DISK10_MAJOR	130
#define XEN_SCSI_DISK11_MAJOR	131
#define XEN_SCSI_DISK12_MAJOR	132
#define XEN_SCSI_DISK13_MAJOR	133
#define XEN_SCSI_DISK14_MAJOR	134
#define XEN_SCSI_DISK15_MAJOR	135

#endif /* __XEN_PUBLIC_IO_BLKIF_H__ */
