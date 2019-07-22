/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fs-verity: read-only file-based authenticity protection
 *
 * Copyright 2019 Google LLC
 */

#ifndef _FSVERITY_PRIVATE_H
#define _FSVERITY_PRIVATE_H

#ifdef CONFIG_FS_VERITY_DEBUG
#define DEBUG
#endif

#define pr_fmt(fmt) "fs-verity: " fmt

#include <crypto/sha.h>
#include <linux/fs.h>
#include <uapi/linux/fsverity.h>

struct ahash_request;

/*
 * Implementation limit: maximum depth of the Merkle tree.  For now 8 is plenty;
 * it's enough for over U64_MAX bytes of data using SHA-256 and 4K blocks.
 */
#define FS_VERITY_MAX_LEVELS		8

/*
 * Largest digest size among all hash algorithms supported by fs-verity.
 * Currently assumed to be <= size of fsverity_descriptor::root_hash.
 */
#define FS_VERITY_MAX_DIGEST_SIZE	SHA256_DIGEST_SIZE

/* A hash algorithm supported by fs-verity */
struct fsverity_hash_alg {
	struct crypto_ahash *tfm; /* hash tfm, allocated on demand */
	const char *name;	  /* crypto API name, e.g. sha256 */
	unsigned int digest_size; /* digest size in bytes, e.g. 32 for SHA-256 */
	unsigned int block_size;  /* block size in bytes, e.g. 64 for SHA-256 */
};

/* Merkle tree parameters: hash algorithm, initial hash state, and topology */
struct merkle_tree_params {
	const struct fsverity_hash_alg *hash_alg; /* the hash algorithm */
	const u8 *hashstate;		/* initial hash state or NULL */
	unsigned int digest_size;	/* same as hash_alg->digest_size */
	unsigned int block_size;	/* size of data and tree blocks */
	unsigned int hashes_per_block;	/* number of hashes per tree block */
	unsigned int log_blocksize;	/* log2(block_size) */
	unsigned int log_arity;		/* log2(hashes_per_block) */
	unsigned int num_levels;	/* number of levels in Merkle tree */
	u64 tree_size;			/* Merkle tree size in bytes */

	/*
	 * Starting block index for each tree level, ordered from leaf level (0)
	 * to root level ('num_levels - 1')
	 */
	u64 level_start[FS_VERITY_MAX_LEVELS];
};

/* hash_algs.c */

extern struct fsverity_hash_alg fsverity_hash_algs[];

const struct fsverity_hash_alg *fsverity_get_hash_alg(const struct inode *inode,
						      unsigned int num);
const u8 *fsverity_prepare_hash_state(const struct fsverity_hash_alg *alg,
				      const u8 *salt, size_t salt_size);
int fsverity_hash_page(const struct merkle_tree_params *params,
		       const struct inode *inode,
		       struct ahash_request *req, struct page *page, u8 *out);
int fsverity_hash_buffer(const struct fsverity_hash_alg *alg,
			 const void *data, size_t size, u8 *out);
void __init fsverity_check_hash_algs(void);

/* init.c */

extern void __printf(3, 4) __cold
fsverity_msg(const struct inode *inode, const char *level,
	     const char *fmt, ...);

#define fsverity_warn(inode, fmt, ...)		\
	fsverity_msg((inode), KERN_WARNING, fmt, ##__VA_ARGS__)
#define fsverity_err(inode, fmt, ...)		\
	fsverity_msg((inode), KERN_ERR, fmt, ##__VA_ARGS__)

#endif /* _FSVERITY_PRIVATE_H */
