/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025, Linaro Limited
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * QMI Thermal Mitigation Device (TMD) library header.
 */

#ifndef __QMI_TMD_H__
#define __QMI_TMD_H__

struct device;
struct qmi_tmd_client;

#if IS_ENABLED(CONFIG_QCOM_QMI_TMD)
struct qmi_tmd_client *qmi_tmd_init(struct device *dev,
				    unsigned int instance_id,
				    const char * const *tmd_names,
				    int num_tmds);

void qmi_tmd_exit(struct qmi_tmd_client *tmd_cli);
#else
static inline struct qmi_tmd_client *qmi_tmd_init(struct device *dev,
						  unsigned int instance_id,
						  const char * const *tmd_names,
						  int num_tmds)
{
	return NULL;
}

static inline void qmi_tmd_exit(struct qmi_tmd_client *tmd_cli)
{
}
#endif

#endif /* __QMI_TMD_H__ */
