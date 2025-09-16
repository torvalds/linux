// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor policy loading interface function definitions.
 *
 * Copyright 2013 Canonical Ltd.
 *
 * Fns to provide a checksum of policy that has been loaded this can be
 * compared to userspace policy compiles to check loaded policy is what
 * it should be.
 */

#include <crypto/sha2.h>

#include "include/apparmor.h"
#include "include/crypto.h"

unsigned int aa_hash_size(void)
{
	return SHA256_DIGEST_SIZE;
}

char *aa_calc_hash(void *data, size_t len)
{
	char *hash;

	hash = kzalloc(SHA256_DIGEST_SIZE, GFP_KERNEL);
	if (!hash)
		return ERR_PTR(-ENOMEM);

	sha256(data, len, hash);
	return hash;
}

int aa_calc_profile_hash(struct aa_profile *profile, u32 version, void *start,
			 size_t len)
{
	struct sha256_ctx sctx;
	__le32 le32_version = cpu_to_le32(version);

	if (!aa_g_hash_policy)
		return 0;

	profile->hash = kzalloc(SHA256_DIGEST_SIZE, GFP_KERNEL);
	if (!profile->hash)
		return -ENOMEM;

	sha256_init(&sctx);
	sha256_update(&sctx, (u8 *)&le32_version, 4);
	sha256_update(&sctx, (u8 *)start, len);
	sha256_final(&sctx, profile->hash);
	return 0;
}

static int __init init_profile_hash(void)
{
	if (apparmor_initialized)
		aa_info_message("AppArmor sha256 policy hashing enabled");
	return 0;
}
late_initcall(init_profile_hash);
