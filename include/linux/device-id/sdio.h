/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_SDIO_H
#define LINUX_DEVICE_ID_SDIO_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

/* SDIO */

#define SDIO_ANY_ID (~0)

struct sdio_device_id {
	__u8	class;			/* Standard interface or SDIO_ANY_ID */
	__u16	vendor;			/* Vendor or SDIO_ANY_ID */
	__u16	device;			/* Device ID or SDIO_ANY_ID */
	kernel_ulong_t driver_data;	/* Data private to the driver */
};

#endif /* ifndef LINUX_DEVICE_ID_SDIO_H */
