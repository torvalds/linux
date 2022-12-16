/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */

#ifndef __LINUX_BLK_CRYPTO_PROFILE_H
#define __LINUX_BLK_CRYPTO_PROFILE_H

#include <linux/bio.h>
#include <linux/blk-crypto.h>

struct blk_crypto_profile;

/**
 * struct blk_crypto_ll_ops - functions to control inline encryption hardware
 *
 * Low-level operations for controlling inline encryption hardware.  This
 * interface must be implemented by storage drivers that support inline
 * encryption.  All functions may sleep, are serialized by profile->lock, and
 * are never called while profile->dev (if set) is runtime-suspended.
 */
struct blk_crypto_ll_ops {

	/**
	 * @keyslot_program: Program a key into the inline encryption hardware.
	 *
	 * Program @key into the specified @slot in the inline encryption
	 * hardware, overwriting any key that the keyslot may already contain.
	 * The keyslot is guaranteed to not be in-use by any I/O.
	 *
	 * This is required if the device has keyslots.  Otherwise (i.e. if the
	 * device is a layered device, or if the device is real hardware that
	 * simply doesn't have the concept of keyslots) it is never called.
	 *
	 * Must return 0 on success, or -errno on failure.
	 */
	int (*keyslot_program)(struct blk_crypto_profile *profile,
			       const struct blk_crypto_key *key,
			       unsigned int slot);

	/**
	 * @keyslot_evict: Evict a key from the inline encryption hardware.
	 *
	 * If the device has keyslots, this function must evict the key from the
	 * specified @slot.  The slot will contain @key, but there should be no
	 * need for the @key argument to be used as @slot should be sufficient.
	 * The keyslot is guaranteed to not be in-use by any I/O.
	 *
	 * If the device doesn't have keyslots itself, this function must evict
	 * @key from any underlying devices.  @slot won't be valid in this case.
	 *
	 * If there are no keyslots and no underlying devices, this function
	 * isn't required.
	 *
	 * Must return 0 on success, or -errno on failure.
	 */
	int (*keyslot_evict)(struct blk_crypto_profile *profile,
			     const struct blk_crypto_key *key,
			     unsigned int slot);

	/**
	 * @derive_sw_secret: Derive the software secret from a hardware-wrapped
	 *		      key in ephemerally-wrapped form.
	 *
	 * This only needs to be implemented if BLK_CRYPTO_KEY_TYPE_HW_WRAPPED
	 * is supported.
	 *
	 * Must return 0 on success, -EBADMSG if the key is invalid, or another
	 * -errno code on other errors.
	 */
	int (*derive_sw_secret)(struct blk_crypto_profile *profile,
				const u8 *eph_key, size_t eph_key_size,
				u8 sw_secret[BLK_CRYPTO_SW_SECRET_SIZE]);
};

/**
 * struct blk_crypto_profile - inline encryption profile for a device
 *
 * This struct contains a storage device's inline encryption capabilities (e.g.
 * the supported crypto algorithms), driver-provided functions to control the
 * inline encryption hardware (e.g. programming and evicting keys), and optional
 * device-independent keyslot management data.
 */
struct blk_crypto_profile {

	/* public: Drivers must initialize the following fields. */

	/**
	 * @ll_ops: Driver-provided functions to control the inline encryption
	 * hardware, e.g. program and evict keys.
	 */
	struct blk_crypto_ll_ops ll_ops;

	/**
	 * @max_dun_bytes_supported: The maximum number of bytes supported for
	 * specifying the data unit number (DUN).  Specifically, the range of
	 * supported DUNs is 0 through (1 << (8 * max_dun_bytes_supported)) - 1.
	 */
	unsigned int max_dun_bytes_supported;

	/**
	 * @key_types_supported: A bitmask of the supported key types:
	 * BLK_CRYPTO_KEY_TYPE_STANDARD and/or BLK_CRYPTO_KEY_TYPE_HW_WRAPPED.
	 */
	unsigned int key_types_supported;

	/**
	 * @modes_supported: Array of bitmasks that specifies whether each
	 * combination of crypto mode and data unit size is supported.
	 * Specifically, the i'th bit of modes_supported[crypto_mode] is set if
	 * crypto_mode can be used with a data unit size of (1 << i).  Note that
	 * only data unit sizes that are powers of 2 can be supported.
	 */
	unsigned int modes_supported[BLK_ENCRYPTION_MODE_MAX];

	/**
	 * @dev: An optional device for runtime power management.  If the driver
	 * provides this device, it will be runtime-resumed before any function
	 * in @ll_ops is called and will remain resumed during the call.
	 */
	struct device *dev;

	/* private: The following fields shouldn't be accessed by drivers. */

	/* Number of keyslots, or 0 if not applicable */
	unsigned int num_slots;

	/*
	 * Serializes all calls to functions in @ll_ops as well as all changes
	 * to @slot_hashtable.  This can also be taken in read mode to look up
	 * keyslots while ensuring that they can't be changed concurrently.
	 */
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
	struct blk_crypto_keyslot *slots;
};

int blk_crypto_profile_init(struct blk_crypto_profile *profile,
			    unsigned int num_slots);

int devm_blk_crypto_profile_init(struct device *dev,
				 struct blk_crypto_profile *profile,
				 unsigned int num_slots);

unsigned int blk_crypto_keyslot_index(struct blk_crypto_keyslot *slot);

void blk_crypto_reprogram_all_keys(struct blk_crypto_profile *profile);

void blk_crypto_profile_destroy(struct blk_crypto_profile *profile);

void blk_crypto_intersect_capabilities(struct blk_crypto_profile *parent,
				       const struct blk_crypto_profile *child);

bool blk_crypto_has_capabilities(const struct blk_crypto_profile *target,
				 const struct blk_crypto_profile *reference);

void blk_crypto_update_capabilities(struct blk_crypto_profile *dst,
				    const struct blk_crypto_profile *src);

#endif /* __LINUX_BLK_CRYPTO_PROFILE_H */
