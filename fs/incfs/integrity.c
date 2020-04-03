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

struct signature_info {
	u32 version;
	enum incfs_hash_tree_algorithm hash_algorithm;
	u8 log2_blocksize;
	struct mem_range salt;
	struct mem_range root_hash;
};

static bool read_u32(u8 **p, u8 *top, u32 *result)
{
	if (*p + sizeof(u32) > top)
		return false;

	*result = le32_to_cpu(*(__le32 *)*p);
	*p += sizeof(u32);
	return true;
}

static bool read_u8(u8 **p, u8 *top, u8 *result)
{
	if (*p + sizeof(u8) > top)
		return false;

	*result = *(u8 *)*p;
	*p += sizeof(u8);
	return true;
}

static bool read_mem_range(u8 **p, u8 *top, struct mem_range *range)
{
	u32 len;

	if (!read_u32(p, top, &len) || *p + len > top)
		return false;

	range->len = len;
	range->data = *p;
	*p += len;
	return true;
}

static int incfs_parse_signature(struct mem_range signature,
				 struct signature_info *si)
{
	u8 *p = signature.data;
	u8 *top = signature.data + signature.len;
	u32 hash_section_size;

	if (signature.len > INCFS_MAX_SIGNATURE_SIZE)
		return -EINVAL;

	if (!read_u32(&p, top, &si->version) ||
	    si->version != INCFS_SIGNATURE_VERSION)
		return -EINVAL;

	if (!read_u32(&p, top, &hash_section_size) ||
	    p + hash_section_size > top)
		return -EINVAL;
	top = p + hash_section_size;

	if (!read_u32(&p, top, &si->hash_algorithm) ||
	    si->hash_algorithm != INCFS_HASH_TREE_SHA256)
		return -EINVAL;

	if (!read_u8(&p, top, &si->log2_blocksize) || si->log2_blocksize != 12)
		return -EINVAL;

	if (!read_mem_range(&p, top, &si->salt))
		return -EINVAL;

	if (!read_mem_range(&p, top, &si->root_hash))
		return -EINVAL;

	if (p != top)
		return -EINVAL;

	return 0;
}

struct mtree *incfs_alloc_mtree(struct mem_range signature,
				int data_block_count)
{
	int error;
	struct signature_info si;
	struct mtree *result = NULL;
	struct incfs_hash_alg *hash_alg = NULL;
	int hash_per_block;
	int lvl;
	int total_blocks = 0;
	int blocks_in_level[INCFS_MAX_MTREE_LEVELS];
	int blocks = data_block_count;

	if (data_block_count <= 0)
		return ERR_PTR(-EINVAL);

	error = incfs_parse_signature(signature, &si);
	if (error)
		return ERR_PTR(error);

	hash_alg = incfs_get_hash_alg(si.hash_algorithm);
	if (IS_ERR(hash_alg))
		return ERR_PTR(PTR_ERR(hash_alg));

	if (si.root_hash.len < hash_alg->digest_size)
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
	memcpy(result->root_hash, si.root_hash.data, hash_alg->digest_size);
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

	if (data.len < INCFS_DATA_FILE_BLOCK_SIZE) {
		int err;
		void *buf = kzalloc(INCFS_DATA_FILE_BLOCK_SIZE, GFP_NOFS);

		if (!buf)
			return -ENOMEM;

		memcpy(buf, data.data, data.len);
		err = crypto_shash_digest(desc, buf, INCFS_DATA_FILE_BLOCK_SIZE,
					  digest.data);
		kfree(buf);
		return err;
	}
	return crypto_shash_digest(desc, data.data, data.len, digest.data);
}

