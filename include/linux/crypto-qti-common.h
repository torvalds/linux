/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CRYPTO_QTI_COMMON_H
#define _CRYPTO_QTI_COMMON_H

#include <linux/blk-crypto.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/delay.h>

#define RAW_SECRET_SIZE 32
#define QTI_ICE_MAX_BIST_CHECK_COUNT 100
#define QTI_ICE_TYPE_NAME_LEN 8

/* Storage types for crypto */
#define UFS_CE 10
#define SDCC_CE 20

struct ice_mmio_data {
	void __iomem *ice_base_mmio;
#if (IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER) || IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1))
	void __iomem *ice_hwkm_mmio;
#endif
#if IS_ENABLED(CONFIG_QTI_HW_KEY_MANAGER_V1)
	struct device *dev;
	void __iomem *km_base;
	struct resource *km_res;
	struct list_head clk_list_head;
	bool is_hwkm_clk_available;
	bool is_hwkm_enabled;
#endif
};

#if IS_ENABLED(CONFIG_QTI_CRYPTO_COMMON)
int crypto_qti_init_crypto(void *mmio_data);
int crypto_qti_enable(void *mmio_data);
void crypto_qti_disable(void);
int crypto_qti_debug(const struct ice_mmio_data *mmio_data);
int crypto_qti_keyslot_program(const struct ice_mmio_data *mmio_data,
			       const struct blk_crypto_key *key,
			       unsigned int slot, u8 data_unit_mask,
			       int capid, int storage_type);
int crypto_qti_keyslot_evict(const struct ice_mmio_data *mmio_data,
							unsigned int slot, int storage_type);
int crypto_qti_derive_raw_secret(const struct ice_mmio_data *mmio_data, const u8 *wrapped_key,
					unsigned int wrapped_key_size, u8 *secret,
					unsigned int secret_size);

#else
static inline int crypto_qti_init_crypto(void *mmio_data)
{
	return -EOPNOTSUPP;
}
static inline int crypto_qti_enable(void *mmio_data)
{
	return -EOPNOTSUPP;
}
static inline void crypto_qti_disable(void)
{}
static inline int crypto_qti_debug(const struct ice_mmio_data *mmio_data)
{
	return -EOPNOTSUPP;
}
static inline int crypto_qti_keyslot_program(const struct ice_mmio_data *mmio_data,
						const struct blk_crypto_key *key,
						unsigned int slot,
						u8 data_unit_mask,
						int capid, int storage_type)
{
	return -EOPNOTSUPP;
}
static inline int crypto_qti_keyslot_evict(const struct ice_mmio_data *mmio_data,
						unsigned int slot, int storage_type)
{
	return -EOPNOTSUPP;
}
static inline int crypto_qti_derive_raw_secret(const struct ice_mmio_data *mmio_data,
						const u8 *wrapped_key,
						unsigned int wrapped_key_size,
						u8 *secret,
						unsigned int secret_size)
{
	return -EOPNOTSUPP;
}

#endif /* CONFIG_QTI_CRYPTO_COMMON */

#endif /* _CRYPTO_QTI_COMMON_H */
