/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2019 Google LLC
 */
#ifndef _INCFS_INTEGRITY_H
#define _INCFS_INTEGRITY_H
#include <linux/types.h>
#include <linux/kernel.h>
#include <crypto/hash.h>

#include <uapi/linux/incrementalfs.h>

#include "internal.h"

#define INCFS_MAX_MTREE_LEVELS 8
#define INCFS_MAX_HASH_AREA_SIZE (1280 * 1024 * 1024)

struct incfs_hash_alg {
	const char *name;
	int digest_size;
	enum incfs_hash_tree_algorithm id;

	struct crypto_shash *shash;
};

/* Merkle tree structure. */
struct mtree {
	struct incfs_hash_alg *alg;

	u8 root_hash[INCFS_MAX_HASH_SIZE];

	/* Offset of each hash level in the hash area. */
	u32 hash_level_suboffset[INCFS_MAX_MTREE_LEVELS];

	u32 hash_tree_area_size;

	/* Number of levels in hash_level_suboffset */
	int depth;
};

struct signature_info {
	struct mem_range root_hash;

	struct mem_range additional_data;

	struct mem_range signature;

	enum incfs_hash_tree_algorithm hash_alg;
};

struct incfs_hash_alg *incfs_get_hash_alg(enum incfs_hash_tree_algorithm id);

struct mtree *incfs_alloc_mtree(enum incfs_hash_tree_algorithm id,
				int data_block_count,
				struct mem_range root_hash);

void incfs_free_mtree(struct mtree *tree);

size_t incfs_get_mtree_depth(enum incfs_hash_tree_algorithm alg, loff_t size);

size_t incfs_get_mtree_hash_count(enum incfs_hash_tree_algorithm alg,
					loff_t size);

int incfs_calc_digest(struct incfs_hash_alg *alg, struct mem_range data,
			struct mem_range digest);

int incfs_validate_pkcs7_signature(struct mem_range pkcs7_blob,
	struct mem_range root_hash, struct mem_range add_data);

void incfs_free_signature_info(struct signature_info *si);

#endif /* _INCFS_INTEGRITY_H */
