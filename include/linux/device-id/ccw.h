/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_CCW_H
#define LINUX_DEVICE_ID_CCW_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef unsigned long kernel_ulong_t;
#endif

#define CCW_DEVICE_ID_MATCH_CU_TYPE		0x01
#define CCW_DEVICE_ID_MATCH_CU_MODEL		0x02
#define CCW_DEVICE_ID_MATCH_DEVICE_TYPE		0x04
#define CCW_DEVICE_ID_MATCH_DEVICE_MODEL	0x08

/* s390 CCW devices */
struct ccw_device_id {
	__u16	match_flags;	/* which fields to match against */

	__u16	cu_type;	/* control unit type     */
	__u16	dev_type;	/* device type           */
	__u8	cu_model;	/* control unit model    */
	__u8	dev_model;	/* device model          */

	kernel_ulong_t driver_info;
};

#endif /* ifndef LINUX_DEVICE_ID_CCW_H */
