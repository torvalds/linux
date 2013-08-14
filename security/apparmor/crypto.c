/*
 * AppArmor security module
 *
 * This file contains AppArmor policy loading interface function definitions.
 *
 * Copyright 2013 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * Fns to provide a checksum of policy that has been loaded this can be
 * compared to userspace policy compiles to check loaded policy is what
 * it should be.
 */

#include <linux/crypto.h>

#include "include/apparmor.h"
#include "include/crypto.h"

static unsigned int apparmor_hash_size;

static struct crypto_hash *apparmor_tfm;

unsigned int aa_hash_size(void)
{
	return apparmor_hash_size;
}

int aa_calc_profile_hash(struct aa_profile *profile, u32 version, void *start,
			 size_t len)
{
	struct scatterlist sg[2];
	struct hash_desc desc = {
		.tfm = apparmor_tfm,
		.flags = 0
	};
	int error = -ENOMEM;
	u32 le32_version = cpu_to_le32(version);

	if (!apparmor_tfm)
		return 0;

	sg_init_table(sg, 2);
	sg_set_buf(&sg[0], &le32_version, 4);
	sg_set_buf(&sg[1], (u8 *) start, len);

	profile->hash = kzalloc(apparmor_hash_size, GFP_KERNEL);
	if (!profile->hash)
		goto fail;

	error = crypto_hash_init(&desc);
	if (error)
		goto fail;
	error = crypto_hash_update(&desc, &sg[0], 4);
	if (error)
		goto fail;
	error = crypto_hash_update(&desc, &sg[1], len);
	if (error)
		goto fail;
	error = crypto_hash_final(&desc, profile->hash);
	if (error)
		goto fail;

	return 0;

fail:
	kfree(profile->hash);
	profile->hash = NULL;

	return error;
}

static int __init init_profile_hash(void)
{
	struct crypto_hash *tfm;

	if (!apparmor_initialized)
		return 0;

	tfm = crypto_alloc_hash("sha1", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		int error = PTR_ERR(tfm);
		AA_ERROR("failed to setup profile sha1 hashing: %d\n", error);
		return error;
	}
	apparmor_tfm = tfm;
	apparmor_hash_size = crypto_hash_digestsize(apparmor_tfm);

	aa_info_message("AppArmor sha1 policy hashing enabled");

	return 0;
}

late_initcall(init_profile_hash);
