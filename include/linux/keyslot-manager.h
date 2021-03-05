/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_KEYSLOT_MANAGER_H
#define __LINUX_KEYSLOT_MANAGER_H

#include <linux/bio.h>
#include <linux/blk-crypto.h>

/* Inline crypto feature bits.  Must set at least one. */
enum {
	/* Support for standard software-specified keys */
	BLK_CRYPTO_FEATURE_STANDARD_KEYS = BIT(0),

	/* Support for hardware-wrapped keys */
	BLK_CRYPTO_FEATURE_WRAPPED_KEYS = BIT(1),
};

struct blk_keyslot_manager;

/**
 * struct blk_ksm_ll_ops - functions to manage keyslots in hardware
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
struct blk_ksm_ll_ops {
	int (*keyslot_program)(struct blk_keyslot_manager *ksm,
			       const struct blk_crypto_key *key,
			       unsigned int slot);
	int (*keyslot_evict)(struct blk_keyslot_manager *ksm,
			     const struct blk_crypto_key *key,
			     unsigned int slot);
	int (*derive_raw_secret)(struct blk_keyslot_manager *ksm,
				 const u8 *wrapped_key,
				 unsigned int wrapped_key_size,
				 u8 *secret, unsigned int secret_size);
};

struct blk_keyslot_manager {
	/*
	 * The struct blk_ksm_ll_ops that this keyslot manager will use
	 * to perform operations like programming and evicting keys on the
	 * device
	 */
	struct blk_ksm_ll_ops ksm_ll_ops;

	/*
	 * The maximum number of bytes supported for specifying the data unit
	 * number.
	 */
	unsigned int max_dun_bytes_supported;

	/*
	 * The supported features as a bitmask of BLK_CRYPTO_FEATURE_* flags.
	 * Most drivers should set BLK_CRYPTO_FEATURE_STANDARD_KEYS here.
	 */
	unsigned int features;

	/*
	 * Array of size BLK_ENCRYPTION_MODE_MAX of bitmasks that represents
	 * whether a crypto mode and data unit size are supported. The i'th
	 * bit of crypto_mode_supported[crypto_mode] is set iff a data unit
	 * size of (1 << i) is supported. We only support data unit sizes
	 * that are powers of 2.
	 */
	unsigned int crypto_modes_supported[BLK_ENCRYPTION_MODE_MAX];

	/* Device for runtime power management (NULL if none) */
	struct device *dev;

	/* Here onwards are *private* fields for internal keyslot manager use */

	unsigned int num_slots;

	/* Protects programming and evicting keys from the device */
	struct rw_semaphore lock;

	/* List of idle slots, with least recently used slot at front */
	wait_queue_head_t idle_slots_wait_queue;
	struct list_head idle_slots;
	spinlock_t idle_slots_lock;

	/*
	 * Hash table which maps struct *blk_crypto_key to keyslots, so that we
	 * can find a key's keyslot in O(1) time rather than O(num_slots).
	 * Protected by 'lock'.
	 */
	struct hlist_head *slot_hashtable;
	unsigned int log_slot_ht_size;

	/* Per-keyslot data */
	struct blk_ksm_keyslot *slots;
};

int blk_ksm_init(struct blk_keyslot_manager *ksm, unsigned int num_slots);

int devm_blk_ksm_init(struct device *dev, struct blk_keyslot_manager *ksm,
		      unsigned int num_slots);

blk_status_t blk_ksm_get_slot_for_key(struct blk_keyslot_manager *ksm,
				      const struct blk_crypto_key *key,
				      struct blk_ksm_keyslot **slot_ptr);

unsigned int blk_ksm_get_slot_idx(struct blk_ksm_keyslot *slot);

void blk_ksm_put_slot(struct blk_ksm_keyslot *slot);

bool blk_ksm_crypto_cfg_supported(struct blk_keyslot_manager *ksm,
				  const struct blk_crypto_config *cfg);

int blk_ksm_evict_key(struct blk_keyslot_manager *ksm,
		      const struct blk_crypto_key *key);

void blk_ksm_reprogram_all_keys(struct blk_keyslot_manager *ksm);

void blk_ksm_destroy(struct blk_keyslot_manager *ksm);

int blk_ksm_derive_raw_secret(struct blk_keyslot_manager *ksm,
			      const u8 *wrapped_key,
			      unsigned int wrapped_key_size,
			      u8 *secret, unsigned int secret_size);

void blk_ksm_intersect_modes(struct blk_keyslot_manager *parent,
			     const struct blk_keyslot_manager *child);

void blk_ksm_init_passthrough(struct blk_keyslot_manager *ksm);

bool blk_ksm_is_superset(struct blk_keyslot_manager *ksm_superset,
			 struct blk_keyslot_manager *ksm_subset);

void blk_ksm_update_capabilities(struct blk_keyslot_manager *target_ksm,
				 struct blk_keyslot_manager *reference_ksm);

#endif /* __LINUX_KEYSLOT_MANAGER_H */
