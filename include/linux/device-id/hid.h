/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_HID_H
#define LINUX_DEVICE_ID_HID_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

#define HID_ANY_ID				(~0)
#define HID_BUS_ANY				0xffff
#define HID_GROUP_ANY				0x0000

struct hid_device_id {
	__u16 bus;
	__u16 group;
	__u32 vendor;
	__u32 product;
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_HID_H */
