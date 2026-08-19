/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_IEEE1394_H
#define LINUX_DEVICE_ID_IEEE1394_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

#define IEEE1394_MATCH_VENDOR_ID	0x0001
#define IEEE1394_MATCH_MODEL_ID		0x0002
#define IEEE1394_MATCH_SPECIFIER_ID	0x0004
#define IEEE1394_MATCH_VERSION		0x0008

struct ieee1394_device_id {
	__u32 match_flags;
	__u32 vendor_id;
	__u32 model_id;
	__u32 specifier_id;
	__u32 version;
	union {
		kernel_ulong_t driver_data;
		const void *driver_data_ptr;
	};
};

#endif /* ifndef LINUX_DEVICE_ID_IEEE1394_H */
