/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_DEVICE_ID_ARM_SMCCC_H
#define __LINUX_DEVICE_ID_ARM_SMCCC_H

#ifdef __KERNEL__
#include <linux/types.h>
#endif

#define ARM_SMCCC_MODULE_PREFIX "arm_smccc:"

/**
 * struct arm_smccc_device_id - Arm SMCCC bus device identifier
 * @func_id: SMCCC function identifier
 */
struct arm_smccc_device_id {
	__u32 func_id;
};

#endif /* __LINUX_DEVICE_ID_ARM_SMCCC_H */
