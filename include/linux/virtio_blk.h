#ifndef _LINUX_VIRTIO_BLK_H
#define _LINUX_VIRTIO_BLK_H
/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers. */
#include <linux/types.h>
#include <linux/virtio_config.h>

/* The ID for virtio_block */
#define VIRTIO_ID_BLOCK	2

/* Feature bits */
#define VIRTIO_BLK_F_BARRIER	0	/* Does host support barriers? */
#define VIRTIO_BLK_F_SIZE_MAX	1	/* Indicates maximum segment size */
#define VIRTIO_BLK_F_SEG_MAX	2	/* Indicates maximum # of segments */
#define VIRTIO_BLK_F_GEOMETRY	4	/* Legacy geometry available  */
#define VIRTIO_BLK_F_RO		5	/* Disk is read-only */
#define VIRTIO_BLK_F_BLK_SIZE	6	/* Block size of disk is available*/
#define VIRTIO_BLK_F_SCSI	7	/* Supports scsi command passthru */
#define VIRTIO_BLK_F_IDENTIFY	8	/* ATA IDENTIFY supported */

#define VIRTIO_BLK_ID_BYTES	(sizeof(__u16[256]))	/* IDENTIFY DATA */

struct virtio_blk_config {
	/* The capacity (in 512-byte sectors). */
	__u64 capacity;
	/* The maximum segment size (if VIRTIO_BLK_F_SIZE_MAX) */
	__u32 size_max;
	/* The maximum number of segments (if VIRTIO_BLK_F_SEG_MAX) */
	__u32 seg_max;
	/* geometry the device (if VIRTIO_BLK_F_GEOMETRY) */
	struct virtio_blk_geometry {
		__u16 cylinders;
		__u8 heads;
		__u8 sectors;
	} geometry;
	/* block size of device (if VIRTIO_BLK_F_BLK_SIZE) */
	__u32 blk_size;
	__u8 identify[VIRTIO_BLK_ID_BYTES];
} __attribute__((packed));

/* These two define direction. */
#define VIRTIO_BLK_T_IN		0
#define VIRTIO_BLK_T_OUT	1

/* This bit says it's a scsi command, not an actual read or write. */
#define VIRTIO_BLK_T_SCSI_CMD	2

/* Barrier before this op. */
#define VIRTIO_BLK_T_BARRIER	0x80000000

/* This is the first element of the read scatter-gather list. */
struct virtio_blk_outhdr {
	/* VIRTIO_BLK_T* */
	__u32 type;
	/* io priority. */
	__u32 ioprio;
	/* Sector (ie. 512 byte offset) */
	__u64 sector;
};

struct virtio_scsi_inhdr {
	__u32 errors;
	__u32 data_len;
	__u32 sense_len;
	__u32 residual;
};

/* And this is the final byte of the write scatter-gather list. */
#define VIRTIO_BLK_S_OK		0
#define VIRTIO_BLK_S_IOERR	1
#define VIRTIO_BLK_S_UNSUPP	2
#endif /* _LINUX_VIRTIO_BLK_H */
