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
enum blk_crypto_key_type qcom_ice_get_supported_key_type(struct qcom_ice *ice);
int qcom_ice_derive_sw_secret(struct qcom_ice *ice,
			      const u8 *eph_key, size_t eph_key_size,
			      u8 sw_secret[BLK_CRYPTO_SW_SECRET_SIZE]);
int qcom_ice_generate_key(struct qcom_ice *ice,
			  u8 lt_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE]);
int qcom_ice_prepare_key(struct qcom_ice *ice,
			 const u8 *lt_key, size_t lt_key_size,
			 u8 eph_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE]);
int qcom_ice_import_key(struct qcom_ice *ice,
			const u8 *raw_key, size_t raw_key_size,
			u8 lt_key[BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE]);
struct qcom_ice *devm_of_qcom_ice_get(struct device *dev);

#endif /* __QCOM_ICE_H__ */
