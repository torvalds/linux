/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_ULPI_H
#define LINUX_DEVICE_ID_ULPI_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

struct ulpi_device_id {
	__u16 vendor;
	__u16 product;
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_ULPI_H */
