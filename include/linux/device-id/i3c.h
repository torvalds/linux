/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_I3C_H
#define LINUX_DEVICE_ID_I3C_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

/* i3c */

#define I3C_MATCH_DCR			0x1
#define I3C_MATCH_MANUF			0x2
#define I3C_MATCH_PART			0x4
#define I3C_MATCH_EXTRA_INFO		0x8

struct i3c_device_id {
	__u8 match_flags;
	__u8 dcr;
	__u16 manuf_id;
	__u16 part_id;
	__u16 extra_info;

	const void *data;
};

#endif /* ifndef LINUX_DEVICE_ID_I3C_H */
