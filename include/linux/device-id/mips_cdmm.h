/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_MIPS_CDMM_H
#define LINUX_DEVICE_ID_MIPS_CDMM_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

/**
 * struct mips_cdmm_device_id - identifies devices in MIPS CDMM bus
 * @type:	Device type identifier.
 */
struct mips_cdmm_device_id {
	__u8	type;
};

#endif /* ifndef LINUX_DEVICE_ID_MIPS_CDMM_H */
