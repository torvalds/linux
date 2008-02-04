#ifndef _LINUX_VIRTIO_BLK_H
#define _LINUX_VIRTIO_BLK_H
#include <linux/virtio_config.h>

/* The ID for virtio_block */
#define VIRTIO_ID_BLOCK	2

/* Feature bits */
#define VIRTIO_BLK_F_BARRIER	0	/* Does host support barriers? */
#define VIRTIO_BLK_F_SIZE_MAX	1	/* Indicates maximum segment size */
#define VIRTIO_BLK_F_SEG_MAX	2	/* Indicates maximum # of segments */

struct virtio_blk_config
{
	/* The capacity (in 512-byte sectors). */
	__le64 capacity;
	/* The maximum segment size (if VIRTIO_BLK_F_SIZE_MAX) */
	__le32 size_max;
	/* The maximum number of segments (if VIRTIO_BLK_F_SEG_MAX) */
	__le32 seg_max;
} __attribute__((packed));

/* These two define direction. */
#define VIRTIO_BLK_T_IN		0
#define VIRTIO_BLK_T_OUT	1

/* This bit says it's a scsi command, not an actual read or write. */
#define VIRTIO_BLK_T_SCSI_CMD	2

/* Barrier before this op. */
#define VIRTIO_BLK_T_BARRIER	0x80000000

/* This is the first element of the read scatter-gather list. */
struct virtio_blk_outhdr
{
	/* VIRTIO_BLK_T* */
	__u32 type;
	/* io priority. */
	__u32 ioprio;
	/* Sector (ie. 512 byte offset) */
	__u64 sector;
};

#define VIRTIO_BLK_S_OK		0
#define VIRTIO_BLK_S_IOERR	1
#define VIRTIO_BLK_S_UNSUPP	2

/* This is the first element of the write scatter-gather list */
struct virtio_blk_inhdr
{
	unsigned char status;
};
#endif /* _LINUX_VIRTIO_BLK_H */
