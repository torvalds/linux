/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_VIRTIO_H
#define LINUX_DEVICE_ID_VIRTIO_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#define VIRTIO_DEV_ANY_ID	0xffffffff

struct virtio_device_id {
	__u32 device;
	__u32 vendor;
};

#endif /* ifndef LINUX_DEVICE_ID_VIRTIO_H */
