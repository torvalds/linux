/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_BCMA_H
#define LINUX_DEVICE_ID_BCMA_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#define BCMA_CORE(_manuf, _id, _rev, _class)  \
	{ .manuf = _manuf, .id = _id, .rev = _rev, .class = _class, }

#define BCMA_ANY_MANUF		0xFFFF
#define BCMA_ANY_ID		0xFFFF
#define BCMA_ANY_REV		0xFF
#define BCMA_ANY_CLASS		0xFF

/* Broadcom's specific AMBA core, see drivers/bcma/ */
struct bcma_device_id {
	__u16	manuf;
	__u16	id;
	__u8	rev;
	__u8	class;
} __attribute__((packed,aligned(2)));

#endif /* ifndef LINUX_DEVICE_ID_BCMA_H */
