/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef __QCOM_ICE_H__
#define __QCOM_ICE_H__

#include <linux/blk-crypto.h>
#include <linux/types.h>

struct qcom_ice;

int qcom_ice_enable(struct qcom_ice *ice);
int qcom_ice_resume(struct qcom_ice *ice);
int qcom_ice_suspend(struct qcom_ice *ice);
int qcom_ice_program_key(struct qcom_ice *ice, unsigned int slot,
			 const struct blk_crypto_key *blk_key);
int qcom_ice_evict_key(struct qcom_ice *ice, int slot);
struct qcom_ice *devm_of_qcom_ice_get(struct device *dev);

#endif /* __QCOM_ICE_H__ */
