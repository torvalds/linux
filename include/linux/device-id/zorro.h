/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_ZORRO_H
#define LINUX_DEVICE_ID_ZORRO_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

#define ZORRO_WILDCARD			(0xffffffff)	/* not official */

#define ZORRO_DEVICE_MODALIAS_FMT	"zorro:i%08X"

struct zorro_device_id {
	__u32 id;			/* Device ID or ZORRO_WILDCARD */
	kernel_ulong_t driver_data;	/* Data private to the driver */
};

#endif /* ifndef LINUX_DEVICE_ID_ZORRO_H */
