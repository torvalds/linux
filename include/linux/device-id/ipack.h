/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_IPACK_H
#define LINUX_DEVICE_ID_IPACK_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#define IPACK_ANY_FORMAT 0xff
#define IPACK_ANY_ID (~0)
struct ipack_device_id {
	__u8  format;			/* Format version or IPACK_ANY_ID */
	__u32 vendor;			/* Vendor ID or IPACK_ANY_ID */
	__u32 device;			/* Device ID or IPACK_ANY_ID */
};

#endif /* ifndef LINUX_DEVICE_ID_IPACK_H */
