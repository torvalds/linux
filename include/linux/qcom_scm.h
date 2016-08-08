/* Copyright (c) 2010-2015, The Linux Foundation. All rights reserved.
 * Copyright (C) 2015 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __QCOM_SCM_H
#define __QCOM_SCM_H

extern int qcom_scm_set_cold_boot_addr(void *entry, const cpumask_t *cpus);
extern int qcom_scm_set_warm_boot_addr(void *entry, const cpumask_t *cpus);

#define QCOM_SCM_HDCP_MAX_REQ_CNT	5

struct qcom_scm_hdcp_req {
	u32 addr;
	u32 val;
};

extern bool qcom_scm_is_available(void);

extern bool qcom_scm_hdcp_available(void);
extern int qcom_scm_hdcp_req(struct qcom_scm_hdcp_req *req, u32 req_cnt,
		u32 *resp);

extern bool qcom_scm_pas_supported(u32 peripheral);
extern int qcom_scm_pas_init_image(u32 peripheral, const void *metadata,
		size_t size);
extern int qcom_scm_pas_mem_setup(u32 peripheral, phys_addr_t addr,
		phys_addr_t size);
extern int qcom_scm_pas_auth_and_reset(u32 peripheral);
extern int qcom_scm_pas_shutdown(u32 peripheral);

#define QCOM_SCM_CPU_PWR_DOWN_L2_ON	0x0
#define QCOM_SCM_CPU_PWR_DOWN_L2_OFF	0x1

extern void qcom_scm_cpu_power_down(u32 flags);

#define QCOM_SCM_VERSION(major, minor) (((major) << 16) | ((minor) & 0xFF))

extern u32 qcom_scm_get_version(void);

#endif
