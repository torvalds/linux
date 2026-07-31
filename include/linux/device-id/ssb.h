/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_SSB_H
#define LINUX_DEVICE_ID_SSB_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#define SSB_ANY_VENDOR		0xFFFF
#define SSB_ANY_ID		0xFFFF
#define SSB_ANY_REV		0xFF

/* SSB core, see drivers/ssb/ */
struct ssb_device_id {
	__u16	vendor;
	__u16	coreid;
	__u8	revision;
	__u8	__pad;
} __attribute__((packed, aligned(2)));

#define SSB_DEVICE(_vendor, _coreid, _revision)  \
	{ .vendor = _vendor, .coreid = _coreid, .revision = _revision }

#endif /* ifndef LINUX_DEVICE_ID_SSB_H */
