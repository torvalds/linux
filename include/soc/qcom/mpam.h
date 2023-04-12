/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_MPAM_H
#define _QCOM_MPAM_H

#if IS_ENABLED(CONFIG_QTI_MPAM)
int qcom_mpam_set_cache_portion(u32 part_id, u32 cache_portion);
#else
static inline int qcom_mpam_set_cache_portion(u32 part_id, u32 cache_portion)
{
	return 0;
}
#endif

#endif /* _QCOM_MPAM_H */
