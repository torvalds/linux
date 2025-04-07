/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023, Linaro Limited
 */

#ifndef __QCOM_ICE_H__
#define __QCOM_ICE_H__

#include <linux/types.h>

struct qcom_ice;

enum qcom_ice_crypto_key_size {
	QCOM_ICE_CRYPTO_KEY_SIZE_INVALID	= 0x0,
	QCOM_ICE_CRYPTO_KEY_SIZE_128		= 0x1,
	QCOM_ICE_CRYPTO_KEY_SIZE_192		= 0x2,
	QCOM_ICE_CRYPTO_KEY_SIZE_256		= 0x3,
	QCOM_ICE_CRYPTO_KEY_SIZE_512		= 0x4,
};

enum qcom_ice_crypto_alg {
	QCOM_ICE_CRYPTO_ALG_AES_XTS		= 0x0,
	QCOM_ICE_CRYPTO_ALG_BITLOCKER_AES_CBC	= 0x1,
	QCOM_ICE_CRYPTO_ALG_AES_ECB		= 0x2,
	QCOM_ICE_CRYPTO_ALG_ESSIV_AES_CBC	= 0x3,
};

int qcom_ice_enable(struct qcom_ice *ice);
int qcom_ice_resume(struct qcom_ice *ice);
int qcom_ice_suspend(struct qcom_ice *ice);
int qcom_ice_program_key(struct qcom_ice *ice,
			 u8 algorithm_id, u8 key_size,
			 const u8 crypto_key[], u8 data_unit_size,
			 int slot);
int qcom_ice_evict_key(struct qcom_ice *ice, int slot);
struct qcom_ice *devm_of_qcom_ice_get(struct device *dev);

#endif /* __QCOM_ICE_H__ */
