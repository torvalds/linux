/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_MPAM_H
#define _QCOM_MPAM_H

#if IS_ENABLED(CONFIG_QTI_MPAM)
int qcom_mpam_set_cache_portion(u32 part_id, u32 cache_portion, u64 config_ctrl);
int qcom_mpam_get_cache_portion(u32 part_id, u64 *config_ctrl);
#else
static inline int qcom_mpam_set_cache_portion(u32 part_id, u32 cache_portion, u64 config_ctrl)
{
	return -ENODEV;
}

static inline int qcom_mpam_get_cache_portion(u32 part_id, u64 *config_ctrl)
{
	return -ENODEV;
}
#endif

#endif /* _QCOM_MPAM_H */
