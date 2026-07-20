/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_PARISC_H
#define LINUX_DEVICE_ID_PARISC_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#define PA_HWTYPE_ANY_ID	0xff
#define PA_HVERSION_REV_ANY_ID	0xff
#define PA_HVERSION_ANY_ID	0xffff
#define PA_SVERSION_ANY_ID	0xffffffff

struct parisc_device_id {
	__u8	hw_type;	/* 5 bits used */
	__u8	hversion_rev;	/* 4 bits */
	__u16	hversion;	/* 12 bits */
	__u32	sversion;	/* 20 bits */
};

#endif /* ifndef LINUX_DEVICE_ID_PARISC_H */
