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

struct incfs_hash_alg *incfs_get_hash_alg(enum incfs_hash_tree_algorithm id);

struct mtree *incfs_alloc_mtree(struct mem_range signature,
				int data_block_count);

void incfs_free_mtree(struct mtree *tree);

size_t incfs_get_mtree_depth(enum incfs_hash_tree_algorithm alg, loff_t size);

size_t incfs_get_mtree_hash_count(enum incfs_hash_tree_algorithm alg,
					loff_t size);

int incfs_calc_digest(struct incfs_hash_alg *alg, struct mem_range data,
			struct mem_range digest);

#endif /* _INCFS_INTEGRITY_H */
