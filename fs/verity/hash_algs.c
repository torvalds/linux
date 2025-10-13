// SPDX-License-Identifier: GPL-2.0
/*
 * fs-verity hash algorithms
 *
 * Copyright 2019 Google LLC
 */

#include "fsverity_private.h"

/* The hash algorithms supported by fs-verity */
const struct fsverity_hash_alg fsverity_hash_algs[] = {
	[FS_VERITY_HASH_ALG_SHA256] = {
		.name = "sha256",
		.digest_size = SHA256_DIGEST_SIZE,
		.block_size = SHA256_BLOCK_SIZE,
		.algo_id = HASH_ALGO_SHA256,
	},
	[FS_VERITY_HASH_ALG_SHA512] = {
		.name = "sha512",
		.digest_size = SHA512_DIGEST_SIZE,
		.block_size = SHA512_BLOCK_SIZE,
		.algo_id = HASH_ALGO_SHA512,
	},
};

/**
 * fsverity_get_hash_alg() - get a hash algorithm by number
 * @inode: optional inode for logging purposes
 * @num: the hash algorithm number
 *
 * Get the struct fsverity_hash_alg for the given hash algorithm number.
 *
 * Return: pointer to the hash alg if it's known, otherwise NULL.
 */
const struct fsverity_hash_alg *fsverity_get_hash_alg(const struct inode *inode,
						      unsigned int num)
{
	if (num >= ARRAY_SIZE(fsverity_hash_algs) ||
	    !fsverity_hash_algs[num].name) {
		fsverity_warn(inode, "Unknown hash algorithm number: %u", num);
		return NULL;
	}
	return &fsverity_hash_algs[num];
}

/**
 * fsverity_prepare_hash_state() - precompute the initial hash state
 * @alg: hash algorithm
 * @salt: a salt which is to be prepended to all data to be hashed
 * @salt_size: salt size in bytes
 *
 * Return: the kmalloc()'ed initial hash state, or NULL if out of memory.
 */
union fsverity_hash_ctx *
fsverity_prepare_hash_state(const struct fsverity_hash_alg *alg,
			    const u8 *salt, size_t salt_size)
{
	u8 *padded_salt = NULL;
	size_t padded_salt_size;
	union fsverity_hash_ctx ctx;
	void *res = NULL;

	/*
	 * Zero-pad the salt to the next multiple of the input size of the hash
	 * algorithm's compression function, e.g. 64 bytes for SHA-256 or 128
	 * bytes for SHA-512.  This ensures that the hash algorithm won't have
	 * any bytes buffered internally after processing the salt, thus making
	 * salted hashing just as fast as unsalted hashing.
	 */
	padded_salt_size = round_up(salt_size, alg->block_size);
	padded_salt = kzalloc(padded_salt_size, GFP_KERNEL);
	if (!padded_salt)
		return NULL;
	memcpy(padded_salt, salt, salt_size);

	switch (alg->algo_id) {
	case HASH_ALGO_SHA256:
		sha256_init(&ctx.sha256);
		sha256_update(&ctx.sha256, padded_salt, padded_salt_size);
		res = kmemdup(&ctx.sha256, sizeof(ctx.sha256), GFP_KERNEL);
		break;
	case HASH_ALGO_SHA512:
		sha512_init(&ctx.sha512);
		sha512_update(&ctx.sha512, padded_salt, padded_salt_size);
		res = kmemdup(&ctx.sha512, sizeof(ctx.sha512), GFP_KERNEL);
		break;
	default:
		WARN_ON_ONCE(1);
	}
	kfree(padded_salt);
	return res;
}

/**
 * fsverity_hash_block() - hash a single data or hash block
 * @params: the Merkle tree's parameters
 * @data: virtual address of a buffer containing the block to hash
 * @out: output digest, size 'params->digest_size' bytes
 *
 * Hash a single data or hash block.  The hash is salted if a salt is specified
 * in the Merkle tree parameters.
 */
void fsverity_hash_block(const struct merkle_tree_params *params,
			 const void *data, u8 *out)
{
	union fsverity_hash_ctx ctx;

	if (!params->hashstate) {
		fsverity_hash_buffer(params->hash_alg, data, params->block_size,
				     out);
		return;
	}

	switch (params->hash_alg->algo_id) {
	case HASH_ALGO_SHA256:
		ctx.sha256 = params->hashstate->sha256;
		sha256_update(&ctx.sha256, data, params->block_size);
		sha256_final(&ctx.sha256, out);
		return;
	case HASH_ALGO_SHA512:
		ctx.sha512 = params->hashstate->sha512;
		sha512_update(&ctx.sha512, data, params->block_size);
		sha512_final(&ctx.sha512, out);
		return;
	default:
		BUG();
	}
}

/**
 * fsverity_hash_buffer() - hash some data
 * @alg: the hash algorithm to use
 * @data: the data to hash
 * @size: size of data to hash, in bytes
 * @out: output digest, size 'alg->digest_size' bytes
 */
void fsverity_hash_buffer(const struct fsverity_hash_alg *alg,
			  const void *data, size_t size, u8 *out)
{
	switch (alg->algo_id) {
	case HASH_ALGO_SHA256:
		sha256(data, size, out);
		return;
	case HASH_ALGO_SHA512:
		sha512(data, size, out);
		return;
	default:
		BUG();
	}
}

void __init fsverity_check_hash_algs(void)
{
	size_t i;

	/*
	 * Sanity check the hash algorithms (could be a build-time check, but
	 * they're in an array)
	 */
	for (i = 0; i < ARRAY_SIZE(fsverity_hash_algs); i++) {
		const struct fsverity_hash_alg *alg = &fsverity_hash_algs[i];

		if (!alg->name)
			continue;

		/*
		 * 0 must never be allocated as an FS_VERITY_HASH_ALG_* value,
		 * as it is reserved for users that use 0 to mean unspecified or
		 * a default value.  fs/verity/ itself doesn't care and doesn't
		 * have a default algorithm, but some users make use of this.
		 */
		BUG_ON(i == 0);

		BUG_ON(alg->digest_size > FS_VERITY_MAX_DIGEST_SIZE);

		/*
		 * For efficiency, the implementation currently assumes the
		 * digest and block sizes are powers of 2.  This limitation can
		 * be lifted if the code is updated to handle other values.
		 */
		BUG_ON(!is_power_of_2(alg->digest_size));
		BUG_ON(!is_power_of_2(alg->block_size));

		/* Verify that there is a valid mapping to HASH_ALGO_*. */
		BUG_ON(alg->algo_id == 0);
		BUG_ON(alg->digest_size != hash_digest_size[alg->algo_id]);
	}
}
