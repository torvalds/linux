/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_SERIO_H
#define LINUX_DEVICE_ID_SERIO_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#define SERIO_ANY	0xff

struct serio_device_id {
	__u8 type;
	__u8 extra;
	__u8 id;
	__u8 proto;
};

#endif /* ifndef LINUX_DEVICE_ID_SERIO_H */
