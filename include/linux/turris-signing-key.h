/* SPDX-License-Identifier: GPL-2.0 */
/*
 * 2025 by Marek Behún <kabel@kernel.org>
 */

#ifndef __TURRIS_SIGNING_KEY_H
#define __TURRIS_SIGNING_KEY_H

#include <linux/key.h>
#include <linux/types.h>

struct device;

#ifdef CONFIG_KEYS
struct turris_signing_key_subtype {
	u16 key_size;
	u8 data_size;
	u8 sig_size;
	u8 public_key_size;
	const char *hash_algo;
	const void *(*get_public_key)(const struct key *key);
	int (*sign)(const struct key *key, const void *msg, void *signature);
};

static inline struct device *turris_signing_key_get_dev(const struct key *key)
{
	return key->payload.data[1];
}

int
devm_turris_signing_key_create(struct device *dev, const struct turris_signing_key_subtype *subtype,
			       const char *desc);
#endif

#endif /* __TURRIS_SIGNING_KEY_H */
