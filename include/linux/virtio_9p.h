#ifndef _LINUX_VIRTIO_9P_H
#define _LINUX_VIRTIO_9P_H
/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers. */
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>

/* The feature bitmap for virtio 9P */

/* The mount point is specified in a config variable */
#define VIRTIO_9P_MOUNT_TAG 0

struct virtio_9p_config {
	/* length of the tag name */
	__u16 tag_len;
	/* non-NULL terminated tag name */
	__u8 tag[0];
} __attribute__((packed));

#endif /* _LINUX_VIRTIO_9P_H */
