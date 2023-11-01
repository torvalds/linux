/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CRYPTO_QTI_VIRT_H
#define _CRYPTO_QTI_VIRT_H

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/blk-crypto.h>
//#include <linux/blk-crypto-profile.h>

//#define RAW_SECRET_SIZE 32

#if IS_ENABLED(CONFIG_QTI_CRYPTO_VIRTUALIZATION)
/**
 * crypto_qti_virt_program_key() - will send key and virtual slot
 * info to Back end (BE) and BE will program the key into specified
 * keyslot in the inline encryption hardware.
 *
 * @blk_crypto_key: Actual key or wrapped key
 * @slot: virtual slot
 *
 * Return: zero on success, else a -errno value
 */
int crypto_qti_virt_program_key(const struct blk_crypto_key *key,
					unsigned int slot);
/**
 * crypto_qti_virt_invalidate_key() - will virtual slot
 * info to Back end (BE) and BE will Evict key from the
 * specified keyslot in the hardware
 *
 * @slot: virtual slot
 *
 * Return: zero on success, else a -errno value
 */
int crypto_qti_virt_invalidate_key(unsigned int slot);

/**
 * crypto_qti_virt_derive_raw_secret_platform() - Derive
 * software secret from wrapped key
 *
 * @wrapped_key: The wrapped key
 * @wrapped_key_size: Size of the wrapped key in bytes
 * @secret: (output) the software secret
 * @secret_size: (output) the number of secret bytes to derive
 *
 * Return: zero on success, else a -errno value
 */
int crypto_qti_virt_derive_raw_secret_platform(const u8 *wrapped_key,
				unsigned int wrapped_key_size, u8 *secret,
				unsigned int secret_size);

/**
 * crypto_qti_virt_ice_get_info() - Determines the
 * total number of available slot for virtual machine
 *
 * @total_num_slots: its an out param and this will update
 * with max number of slots.
 *
 * Return: zero on success, else a -errno value
 */
int crypto_qti_virt_ice_get_info(uint32_t *total_num_slots);
int crypto_qti_virt_get_crypto_capabilities(unsigned int *crypto_modes_supported,
					    uint32_t crypto_array_size);
#else
static inline int crypto_qti_virt_program_key(const struct blk_crypto_key *key,
						unsigned int slot)
{
	return -EOPNOTSUPP;
}
static inline int crypto_qti_virt_invalidate_key(unsigned int slot)
{
	return -EOPNOTSUPP;
}
static inline int crypto_qti_virt_derive_raw_secret_platform(
				const u8 *wrapped_key,
				unsigned int wrapped_key_size, u8 *secret,
				unsigned int secret_size)
{
	return -EOPNOTSUPP;
}

static inline int crypto_qti_virt_ice_get_info(uint32_t *total_num_slots)
{
	return -EOPNOTSUPP;
}
static inline int crypto_qti_virt_get_crypto_capabilities(unsigned int *crypto_modes_supported,
							  uint32_t crypto_array_size)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_QTI_CRYPTO_VIRTUALIZATION */
#endif /*_CRYPTO_QTI_VIRT_H */

