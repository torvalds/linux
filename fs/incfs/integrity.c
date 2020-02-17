// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 Google LLC
 */
#include <crypto/sha.h>
#include <crypto/hash.h>
#include <linux/err.h>
#include <linux/version.h>
#include <crypto/pkcs7.h>

#include "integrity.h"

int incfs_validate_pkcs7_signature(struct mem_range pkcs7_blob,
	struct mem_range root_hash, struct mem_range add_data)
{
	struct pkcs7_message *pkcs7 = NULL;
	const void *data = NULL;
	size_t data_len = 0;
	const char *p;
	int err;

	pkcs7 = pkcs7_parse_message(pkcs7_blob.data, pkcs7_blob.len);
	if (IS_ERR(pkcs7)) {
		pr_debug("PKCS#7 parsing error. ptr=%p size=%ld err=%ld\n",
			pkcs7_blob.data, pkcs7_blob.len, -PTR_ERR(pkcs7));
		return PTR_ERR(pkcs7);
	}

	err = pkcs7_get_content_data(pkcs7, &data, &data_len, NULL);
	if (err || data_len == 0 || data == NULL) {
		pr_debug("PKCS#7 message does not contain data\n");
		err = -EBADMSG;
		goto out;
	}

	if (root_hash.len == 0) {
		pr_debug("Root hash is empty.\n");
		err = -EBADMSG;
		goto out;
	}

	if (data_len != root_hash.len + add_data.len) {
		pr_debug("PKCS#7 data size doesn't match arguments.\n");
		err = -EKEYREJECTED;
		goto out;
	}

	p = data;
	if (memcmp(p, root_hash.data, root_hash.len) != 0) {
		pr_debug("Root hash mismatch.\n");
		err = -EKEYREJECTED;
		goto out;
	}
	p += root_hash.len;
	if (memcmp(p, add_data.data, add_data.len) != 0) {
		pr_debug("Additional data mismatch.\n");
		err = -EKEYREJECTED;
		goto out;
	}

	err = pkcs7_verify(pkcs7, VERIFYING_UNSPECIFIED_SIGNATURE);
	if (err)
		pr_debug("PKCS#7 signature verification error: %d\n", -err);

	/*
	 * RSA signature verification sometimes returns unexpected error codes
	 * when signature doesn't match.
	 */
	if (err == -ERANGE || err == -EINVAL)
		err = -EBADMSG;

out:
	pkcs7_free_message(pkcs7);
	return err;
}

struct incfs_hash_alg *incfs_get_hash_alg(enum incfs_hash_tree_algorithm id)
{
	static struct incfs_hash_alg sha256 = {
		.name = "sha256",
		.digest_size = SHA256_DIGEST_SIZE,
		.id = INCFS_HASH_TREE_SHA256
	};
	struct incfs_hash_alg *result = NULL;
	struct crypto_shash *shash;

	if (id == INCFS_HASH_TREE_SHA256) {
		BUILD_BUG_ON(INCFS_MAX_HASH_SIZE < SHA256_DIGEST_SIZE);
		result = &sha256;
	}

	if (result == NULL)
		return ERR_PTR(-ENOENT);

	/* pairs with cmpxchg_release() below */
	shash = smp_load_acquire(&result->shash);
	if (shash)
		return result;

	shash = crypto_alloc_shash(result->name, 0, 0);
	if (IS_ERR(shash)) {
		int err = PTR_ERR(shash);

		pr_err("Can't allocate hash alg %s, error code:%d",
			result->name, err);
		return ERR_PTR(err);
	}

	/* pairs with smp_load_acquire() above */
	if (cmpxchg_release(&result->shash, NULL, shash) != NULL)
		crypto_free_shash(shash);

	return result;
}


struct mtree *incfs_alloc_mtree(enum incfs_hash_tree_algorithm id,
				int data_block_count,
				struct mem_range root_hash)
{
	struct mtree *result = NULL;
	struct incfs_hash_alg *hash_alg = NULL;
	int hash_per_block;
	int lvl;
	int total_blocks = 0;
	int blocks_in_level[INCFS_MAX_MTREE_LEVELS];
	int blocks = data_block_count;

	if (data_block_count <= 0)
		return ERR_PTR(-EINVAL);

	hash_alg = incfs_get_hash_alg(id);
	if (IS_ERR(hash_alg))
		return ERR_PTR(PTR_ERR(hash_alg));

	if (root_hash.len < hash_alg->digest_size)
		return ERR_PTR(-EINVAL);

	result = kzalloc(sizeof(*result), GFP_NOFS);
	if (!result)
		return ERR_PTR(-ENOMEM);

	result->alg = hash_alg;
	hash_per_block = INCFS_DATA_FILE_BLOCK_SIZE / result->alg->digest_size;

	/* Calculating tree geometry. */
	/* First pass: calculate how many blocks in each tree level. */
	for (lvl = 0; blocks > 1; lvl++) {
		if (lvl >= INCFS_MAX_MTREE_LEVELS) {
			pr_err("incfs: too much data in mtree");
			goto err;
		}

		blocks = (blocks + hash_per_block - 1) / hash_per_block;
		blocks_in_level[lvl] = blocks;
		total_blocks += blocks;
	}
	result->depth = lvl;
	result->hash_tree_area_size = total_blocks * INCFS_DATA_FILE_BLOCK_SIZE;
	if (result->hash_tree_area_size > INCFS_MAX_HASH_AREA_SIZE)
		goto err;

	blocks = 0;
	/* Second pass: calculate offset of each level. 0th level goes last. */
	for (lvl = 0; lvl < result->depth; lvl++) {
		u32 suboffset;

		blocks += blocks_in_level[lvl];
		suboffset = (total_blocks - blocks)
					* INCFS_DATA_FILE_BLOCK_SIZE;

		result->hash_level_suboffset[lvl] = suboffset;
	}

	/* Root hash is stored separately from the rest of the tree. */
	memcpy(result->root_hash, root_hash.data, hash_alg->digest_size);
	return result;

err:
	kfree(result);
	return ERR_PTR(-E2BIG);
}

void incfs_free_mtree(struct mtree *tree)
{
	kfree(tree);
}

int incfs_calc_digest(struct incfs_hash_alg *alg, struct mem_range data,
			struct mem_range digest)
{
	SHASH_DESC_ON_STACK(desc, alg->shash);

	if (!alg || !alg->shash || !data.data || !digest.data)
		return -EFAULT;

	if (alg->digest_size > digest.len)
		return -EINVAL;

	desc->tfm = alg->shash;
	return crypto_shash_digest(desc, data.data, data.len, digest.data);
}

void incfs_free_signature_info(struct signature_info *si)
{
	if (!si)
		return;
	kfree(si->root_hash.data);
	kfree(si->additional_data.data);
	kfree(si->signature.data);
	kfree(si);
}

