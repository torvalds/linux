/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_COREBOOT_H
#define LINUX_DEVICE_ID_COREBOOT_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

/**
 * struct coreboot_device_id - Identifies a coreboot table entry
 * @tag: tag ID
 * @driver_data: driver specific data
 */
struct coreboot_device_id {
	__u32 tag;
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_COREBOOT_H */
