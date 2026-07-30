/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_HDA_H
#define LINUX_DEVICE_ID_HDA_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

struct hda_device_id {
	__u32 vendor_id;
	__u32 rev_id;
	__u8 api_version;
	const char *name;
	unsigned long driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_HDA_H */
