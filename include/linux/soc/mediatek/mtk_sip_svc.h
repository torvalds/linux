/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019 MediaTek Inc.
 */
#ifndef __MTK_SIP_SVC_H
#define __MTK_SIP_SVC_H

/* Error Code */
#define SIP_SVC_E_SUCCESS               0
#define SIP_SVC_E_NOT_SUPPORTED         -1
#define SIP_SVC_E_INVALID_PARAMS        -2
#define SIP_SVC_E_INVALID_RANGE         -3
#define SIP_SVC_E_PERMISSION_DENIED     -4

#ifdef CONFIG_ARM64
#define MTK_SIP_SMC_CONVENTION          ARM_SMCCC_SMC_64
#else
#define MTK_SIP_SMC_CONVENTION          ARM_SMCCC_SMC_32
#endif

#define MTK_SIP_SMC_CMD(fn_id) \
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, MTK_SIP_SMC_CONVENTION, \
			   ARM_SMCCC_OWNER_SIP, fn_id)

/* IOMMU related SMC call */
#define MTK_SIP_KERNEL_IOMMU_CONTROL	MTK_SIP_SMC_CMD(0x514)

#endif
