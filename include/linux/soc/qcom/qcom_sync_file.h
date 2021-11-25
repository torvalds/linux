/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _LINUX_QCOM_SPEC_SYNC_H
#define _LINUX_QCOM_SPEC_SYNC_H

#include <linux/dma-fence-array.h>

#define SPEC_FENCE_FLAG_FENCE_ARRAY	  16 /* fence-array is speculative */
#define SPEC_FENCE_FLAG_FENCE_ARRAY_BOUND 17 /* fence-array is bound */

#if IS_ENABLED(CONFIG_QCOM_SPEC_SYNC)

/**
 * spec_sync_wait_bind_array() - Waits until the fence-array passed as parameter is bound.
 * @fence_array: fence-array to wait-on until it is populated.
 * @timeout_ms: timeout to wait.
 *
 * This function will wait until the fence-array passed as paremeter is bound; i.e. all the
 * dma-fences that conform the fence-array are populated by the spec-fence driver bind ioctl.
 * Once this function returns success, all the fences in the array should be valid.
 *
 * Return: 0 on success or negative errno (-EINVAL)
 */
int spec_sync_wait_bind_array(struct dma_fence_array *fence_array, u32 timeout_ms);

#else

static inline int spec_sync_wait_bind_array(struct dma_fence_array *fence_array, u32 timeout_ms)
{
	return -EINVAL;
}

#endif /* CONFIG_QCOM_SPEC_SYNC */

#endif /* _LINUX_QCOM_SPEC_SYNC_H */
