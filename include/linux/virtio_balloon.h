#ifndef _LINUX_VIRTIO_BALLOON_H
#define _LINUX_VIRTIO_BALLOON_H
/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers. */
#include <linux/virtio_config.h>

/* The feature bitmap for virtio balloon */
#define VIRTIO_BALLOON_F_MUST_TELL_HOST	0 /* Tell before reclaiming pages */

/* Size of a PFN in the balloon interface. */
#define VIRTIO_BALLOON_PFN_SHIFT 12

struct virtio_balloon_config
{
	/* Number of pages host wants Guest to give up. */
	__le32 num_pages;
	/* Number of pages we've actually got in balloon. */
	__le32 actual;
};
#endif /* _LINUX_VIRTIO_BALLOON_H */
