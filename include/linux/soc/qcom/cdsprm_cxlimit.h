/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_CDSPRM_LIMITS_H__
#define __QCOM_CDSPRM_LIMITS_H__

enum cdsprm_npu_corner {
	CDSPRM_NPU_CLK_OFF = 0,
	CDSPRM_NPU_MIN_SVS,
	CDSPRM_NPU_LOW_SVS,
	CDSPRM_NPU_SVS,
	CDSPRM_NPU_SVS_L1,
	CDSPRM_NPU_NOM,
	CDSPRM_NPU_NOM_L1,
	CDSPRM_NPU_TURBO,
	CDSPRM_NPU_TURBO_L1,
};

struct cdsprm_npu_limit_cbs {
	int (*set_corner_limit)(enum cdsprm_npu_corner);
};

enum cdsprm_compute_priority {
	CDSPRM_COMPUTE_HVX_MAX = 1,
	CDSPRM_COMPUTE_AIX_MAX = 2,
	CDSPRM_COMPUTE_HVX_OVER_AIX = 3,
	CDSPRM_COMPUTE_AIX_OVER_HVX = 4,
	CDSPRM_COMPUTE_BALANCED = 5,
};

int cdsprm_compute_core_set_priority(enum cdsprm_compute_priority);

/* For NPU driver */

/**
 * cdsprm_cxlimit_npu_limit_register() - Register NPU corner limit method with
 * cdspprm cxlimit driver.
 * @arg: cdsprm_npu_limit_cbs structure with set_corner_limit method defined
 *
 * Note: To be called from NPU driver only.
 */
int cdsprm_cxlimit_npu_limit_register(const struct cdsprm_npu_limit_cbs *arg);
/**
 * cdsprm_cxlimit_npu_limit_deregister() - deregister NPU corner limit
 * notification from cdsprm cxlimit driver.
 *
 * Note: To be called from NPU driver only.
 */
int cdsprm_cxlimit_npu_limit_deregister(void);
/**
 * cdsprm_cxlimit_npu_activity_notify() - Notify NPU activity status to
 * cdsprm cxlimit driver.
 * @arg: b_enabled 0 - After NPU activity stop
 *                 1 - Before NPU activity start
 *
 * Note: To be called from NPU driver only.
 */
int cdsprm_cxlimit_npu_activity_notify(unsigned int b_enabled);
/**
 * cdsprm_cxlimit_npu_corner_notify() - Notify cdsprm cxlimit driver of NPU
 * corner request.
 * @arg: enum cdsprm_npu_corner - NPU corner value.
 *            CDSPRM_NPU_CLK_OFF for clock off notification.
 *
 * Note: To be called from NPU driver only.
 */
enum cdsprm_npu_corner cdsprm_cxlimit_npu_corner_notify(enum cdsprm_npu_corner);

/* For Camera driver */

/**
 * cdsprm_cxlimit_camera_activity_notify() - Notify cdsprm cxlimit driver of
 * Camera activity
 * @arg: b_enabled 0 - After Camera activity stop
 *                 1 - Before Camera activity start
 *
 * Note: To be called from Camera driver only.
 */
int cdsprm_cxlimit_camera_activity_notify(unsigned int b_enabled);

#endif
