#ifndef _LINUX_VIRTIO_BLK_H
#define _LINUX_VIRTIO_BLK_H
#include <linux/virtio_config.h>

/* The ID for virtio_block */
#define VIRTIO_ID_BLOCK	2

/* Feature bits */
#define VIRTIO_CONFIG_BLK_F	0x40
#define VIRTIO_BLK_F_BARRIER	1	/* Does host support barriers? */

/* The capacity (in 512-byte sectors). */
#define VIRTIO_CONFIG_BLK_F_CAPACITY	0x41
/* The maximum segment size. */
#define VIRTIO_CONFIG_BLK_F_SIZE_MAX	0x42
/* The maximum number of segments. */
#define VIRTIO_CONFIG_BLK_F_SEG_MAX	0x43

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
	/* Where to put reply. */
	__u64 id;
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
