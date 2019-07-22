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
#include <linux/fsverity.h>

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

/**
 * fsverity_info - cached verity metadata for an inode
 *
 * When a verity file is first opened, an instance of this struct is allocated
 * and stored in ->i_verity_info; it remains until the inode is evicted.  It
 * caches information about the Merkle tree that's needed to efficiently verify
 * data read from the file.  It also caches the file measurement.  The Merkle
 * tree pages themselves are not cached here, but the filesystem may cache them.
 */
struct fsverity_info {
	struct merkle_tree_params tree_params;
	u8 root_hash[FS_VERITY_MAX_DIGEST_SIZE];
	u8 measurement[FS_VERITY_MAX_DIGEST_SIZE];
	const struct inode *inode;
};

/*
 * Merkle tree properties.  The file measurement is the hash of this structure.
 */
struct fsverity_descriptor {
	__u8 version;		/* must be 1 */
	__u8 hash_algorithm;	/* Merkle tree hash algorithm */
	__u8 log_blocksize;	/* log2 of size of data and tree blocks */
	__u8 salt_size;		/* size of salt in bytes; 0 if none */
	__le32 sig_size;	/* reserved, must be 0 */
	__le64 data_size;	/* size of file the Merkle tree is built over */
	__u8 root_hash[64];	/* Merkle tree root hash */
	__u8 salt[32];		/* salt prepended to each hashed block */
	__u8 __reserved[144];	/* must be 0's */
};

/* Arbitrary limit to bound the kmalloc() size.  Can be changed. */
#define FS_VERITY_MAX_DESCRIPTOR_SIZE	16384

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

/* open.c */

int fsverity_init_merkle_tree_params(struct merkle_tree_params *params,
				     const struct inode *inode,
				     unsigned int hash_algorithm,
				     unsigned int log_blocksize,
				     const u8 *salt, size_t salt_size);

struct fsverity_info *fsverity_create_info(const struct inode *inode,
					   const void *desc, size_t desc_size);

void fsverity_set_info(struct inode *inode, struct fsverity_info *vi);

void fsverity_free_info(struct fsverity_info *vi);

int __init fsverity_init_info_cache(void);

#endif /* _FSVERITY_PRIVATE_H */
