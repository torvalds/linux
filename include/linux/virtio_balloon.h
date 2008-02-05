#ifndef _LINUX_VIRTIO_BALLOON_H
#define _LINUX_VIRTIO_BALLOON_H
#include <linux/virtio_config.h>

/* The ID for virtio_balloon */
#define VIRTIO_ID_BALLOON	5

/* The feature bitmap for virtio balloon */
#define VIRTIO_BALLOON_F_MUST_TELL_HOST	0 /* Tell before reclaiming pages */

struct virtio_balloon_config
{
	/* Number of pages host wants Guest to give up. */
	__le32 num_pages;
	/* Number of pages we've actually got in balloon. */
	__le32 actual;
};
#endif /* _LINUX_VIRTIO_BALLOON_H */
