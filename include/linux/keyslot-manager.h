/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_KEYSLOT_MANAGER_H
#define __LINUX_KEYSLOT_MANAGER_H

#include <linux/bio.h>

#ifdef CONFIG_BLK_INLINE_ENCRYPTION

struct keyslot_manager;

/**
 * struct keyslot_mgmt_ll_ops - functions to manage keyslots in hardware
 * @keyslot_program:	Program the specified key into the specified slot in the
 *			inline encryption hardware.
 * @keyslot_evict:	Evict key from the specified keyslot in the hardware.
 *			The key is provided so that e.g. dm layers can evict
 *			keys from the devices that they map over.
 *			Returns 0 on success, -errno otherwise.
 * @derive_raw_secret:	(Optional) Derive a software secret from a
 *			hardware-wrapped key.  Returns 0 on success, -EOPNOTSUPP
 *			if unsupported on the hardware, or another -errno code.
 *
 * This structure should be provided by storage device drivers when they set up
 * a keyslot manager - this structure holds the function ptrs that the keyslot
 * manager will use to manipulate keyslots in the hardware.
 */
struct keyslot_mgmt_ll_ops {
	int (*keyslot_program)(struct keyslot_manager *ksm,
			       const struct blk_crypto_key *key,
			       unsigned int slot);
	int (*keyslot_evict)(struct keyslot_manager *ksm,
			     const struct blk_crypto_key *key,
			     unsigned int slot);
	int (*derive_raw_secret)(struct keyslot_manager *ksm,
				 const u8 *wrapped_key,
				 unsigned int wrapped_key_size,
				 u8 *secret, unsigned int secret_size);
};

struct keyslot_manager *keyslot_manager_create(unsigned int num_slots,
	const struct keyslot_mgmt_ll_ops *ksm_ops,
	const unsigned int crypto_mode_supported[BLK_ENCRYPTION_MODE_MAX],
	void *ll_priv_data);

int keyslot_manager_get_slot_for_key(struct keyslot_manager *ksm,
				     const struct blk_crypto_key *key);

void keyslot_manager_get_slot(struct keyslot_manager *ksm, unsigned int slot);

void keyslot_manager_put_slot(struct keyslot_manager *ksm, unsigned int slot);

bool keyslot_manager_crypto_mode_supported(struct keyslot_manager *ksm,
					   enum blk_crypto_mode_num crypto_mode,
					   unsigned int data_unit_size);

int keyslot_manager_evict_key(struct keyslot_manager *ksm,
			      const struct blk_crypto_key *key);

void keyslot_manager_reprogram_all_keys(struct keyslot_manager *ksm);

void *keyslot_manager_private(struct keyslot_manager *ksm);

void keyslot_manager_destroy(struct keyslot_manager *ksm);

struct keyslot_manager *keyslot_manager_create_passthrough(
	const struct keyslot_mgmt_ll_ops *ksm_ops,
	const unsigned int crypto_mode_supported[BLK_ENCRYPTION_MODE_MAX],
	void *ll_priv_data);

void keyslot_manager_intersect_modes(struct keyslot_manager *parent,
				     const struct keyslot_manager *child);

int keyslot_manager_derive_raw_secret(struct keyslot_manager *ksm,
				      const u8 *wrapped_key,
				      unsigned int wrapped_key_size,
				      u8 *secret, unsigned int secret_size);

#endif /* CONFIG_BLK_INLINE_ENCRYPTION */

#endif /* __LINUX_KEYSLOT_MANAGER_H */
