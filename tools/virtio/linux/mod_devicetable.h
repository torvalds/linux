/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MOD_DEVICETABLE_H
#define _LINUX_MOD_DEVICETABLE_H

#include <linux/types.h>

struct virtio_device_id {
	__u32 device;
	__u32 vendor;
};

#define VIRTIO_DEV_ANY_ID	0xffffffff

#endif /* _LINUX_MOD_DEVICETABLE_H */
