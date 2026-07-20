/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_CSS_H
#define LINUX_DEVICE_ID_CSS_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

/* s390 css bus devices (subchannels) */
struct css_device_id {
	__u8 match_flags;
	__u8 type; /* subchannel type */
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_CSS_H */
