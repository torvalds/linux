/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_SSAM_H
#define LINUX_DEVICE_ID_SSAM_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

/* Surface System Aggregator Module */

#define SSAM_MATCH_TARGET	0x1
#define SSAM_MATCH_INSTANCE	0x2
#define SSAM_MATCH_FUNCTION	0x4

struct ssam_device_id {
	__u8 match_flags;

	__u8 domain;
	__u8 category;
	__u8 target;
	__u8 instance;
	__u8 function;

	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_SSAM_H */
