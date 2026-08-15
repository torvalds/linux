/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_AP_H
#define LINUX_DEVICE_ID_AP_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

#define AP_DEVICE_ID_MATCH_CARD_TYPE		0x01
#define AP_DEVICE_ID_MATCH_QUEUE_TYPE		0x02

/* s390 AP bus devices */
struct ap_device_id {
	__u16 match_flags;	/* which fields to match against */
	__u8 dev_type;		/* device type */
	kernel_ulong_t driver_info;
};

#endif /* ifndef LINUX_DEVICE_ID_AP_H */
