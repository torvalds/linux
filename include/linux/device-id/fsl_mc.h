/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_DEVICE_ID_FSL_MC_H
#define LINUX_DEVICE_ID_FSL_MC_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

/**
 * struct fsl_mc_device_id - MC object device identifier
 * @vendor: vendor ID
 * @obj_type: MC object type
 *
 * Type of entries in the "device Id" table for MC object devices supported by
 * a MC object device driver. The last entry of the table has vendor set to 0x0
 */
struct fsl_mc_device_id {
	__u16 vendor;
	const char obj_type[16];
};

#endif /* ifndef LINUX_DEVICE_ID_FSL_MC_H */
