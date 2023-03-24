/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __QCOM_SCM_HAB_H_
#define __QCOM_SCM_HAB_H_

#if IS_ENABLED(CONFIG_QCOM_SCM_HAB)
int scm_qcpe_hab_open(void);
void scm_qcpe_hab_close(void);
int scm_call_qcpe(const struct arm_smccc_args *smc,
		struct arm_smccc_res *res, const bool atomic);

#else
static inline int scm_qcpe_hab_open(void)
{
	return -EOPNOTSUPP;
}

static inline void scm_qcpe_hab_close(void)
{
}

static inline int scm_call_qcpe(const struct arm_smccc_args *smc,
		struct arm_smccc_res *res, const bool atomic)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_QCOM_SCM_HAB */

#endif /* __QCOM_SCM_HAB_H_ */
