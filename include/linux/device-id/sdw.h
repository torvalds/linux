/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_SDW_H
#define LINUX_DEVICE_ID_SDW_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

struct sdw_device_id {
	__u16 mfg_id;
	__u16 part_id;
	__u8  sdw_version;
	__u8  class_id;
	kernel_ulong_t driver_data;
};

#endif /* ifndef LINUX_DEVICE_ID_SDW_H */
