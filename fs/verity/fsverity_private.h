/* SPDX-License-Identifier: GPL-2.0 */
/*
 * fs-verity: read-only file-based authenticity protection
 *
 * Copyright 2019 Google LLC
 */

#ifndef _FSVERITY_PRIVATE_H
#define _FSVERITY_PRIVATE_H

#define pr_fmt(fmt) "fs-verity: " fmt

#include <linux/fsverity.h>
#include <linux/mempool.h>

struct ahash_request;

/*
 * Implementation limit: maximum depth of the Merkle tree.  For now 8 is plenty;
 * it's enough for over U64_MAX bytes of data using SHA-256 and 4K blocks.
 */
#define FS_VERITY_MAX_LEVELS		8

/* A hash algorithm supported by fs-verity */
struct fsverity_hash_alg {
	struct crypto_ahash *tfm; /* hash tfm, allocated on demand */
	const char *name;	  /* crypto API name, e.g. sha256 */
	unsigned int digest_size; /* digest size in bytes, e.g. 32 for SHA-256 */
	unsigned int block_size;  /* block size in bytes, e.g. 64 for SHA-256 */
	mempool_t req_pool;	  /* mempool with a preallocated hash request */
	/*
	 * The HASH_ALGO_* constant for this algorithm.  This is different from
	 * FS_VERITY_HASH_ALG_*, which uses a different numbering scheme.
	 */
	enum hash_algo algo_id;
};

/* Merkle tree parameters: hash algorithm, initial hash state, and topology */
struct merkle_tree_params {
	struct fsverity_hash_alg *hash_alg; /* the hash algorithm */
	const u8 *hashstate;		/* initial hash state or NULL */
	unsigned int digest_size;	/* same as hash_alg->digest_size */
	unsigned int block_size;	/* size of data and tree blocks */
	unsigned int hashes_per_block;	/* number of hashes per tree block */
	unsigned int blocks_per_page;	/* PAGE_SIZE / block_size */
	u8 log_digestsize;		/* log2(digest_size) */
	u8 log_blocksize;		/* log2(block_size) */
	u8 log_arity;			/* log2(hashes_per_block) */
	u8 log_blocks_per_page;		/* log2(blocks_per_page) */
	unsigned int num_levels;	/* number of levels in Merkle tree */
	u64 tree_size;			/* Merkle tree size in bytes */
	unsigned long tree_pages;	/* Merkle tree size in pages */

	/*
	 * Starting block index for each tree level, ordered from leaf level (0)
	 * to root level ('num_levels - 1')
	 */
	unsigned long level_start[FS_VERITY_MAX_LEVELS];
};

/*
 * fsverity_info - cached verity metadata for an inode
 *
 * When a verity file is first opened, an instance of this struct is allocated
 * and stored in ->i_verity_info; it remains until the inode is evicted.  It
 * caches information about the Merkle tree that's needed to efficiently verify
 * data read from the file.  It also caches the file digest.  The Merkle tree
 * pages themselves are not cached here, but the filesystem may cache them.
 */
struct fsverity_info {
	struct merkle_tree_params tree_params;
	u8 root_hash[FS_VERITY_MAX_DIGEST_SIZE];
	u8 file_digest[FS_VERITY_MAX_DIGEST_SIZE];
	const struct inode *inode;
	unsigned long *hash_block_verified;
	spinlock_t hash_page_init_lock;
};

#define FS_VERITY_MAX_SIGNATURE_SIZE	(FS_VERITY_MAX_DESCRIPTOR_SIZE - \
					 sizeof(struct fsverity_descriptor))

/* hash_algs.c */

extern struct fsverity_hash_alg fsverity_hash_algs[];

struct fsverity_hash_alg *fsverity_get_hash_alg(const struct inode *inode,
						unsigned int num);
struct ahash_request *fsverity_alloc_hash_request(struct fsverity_hash_alg *alg,
						  gfp_t gfp_flags);
void fsverity_free_hash_request(struct fsverity_hash_alg *alg,
				struct ahash_request *req);
const u8 *fsverity_prepare_hash_state(struct fsverity_hash_alg *alg,
				      const u8 *salt, size_t salt_size);
int fsverity_hash_block(const struct merkle_tree_params *params,
			const struct inode *inode, struct ahash_request *req,
			struct page *page, unsigned int offset, u8 *out);
int fsverity_hash_buffer(struct fsverity_hash_alg *alg,
			 const void *data, size_t size, u8 *out);
void __init fsverity_check_hash_algs(void);

/* init.c */

void __printf(3, 4) __cold
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
					   struct fsverity_descriptor *desc);

void fsverity_set_info(struct inode *inode, struct fsverity_info *vi);

void fsverity_free_info(struct fsverity_info *vi);

int fsverity_get_descriptor(struct inode *inode,
			    struct fsverity_descriptor **desc_ret);

int __init fsverity_init_info_cache(void);
void __init fsverity_exit_info_cache(void);

/* signature.c */

#ifdef CONFIG_FS_VERITY_BUILTIN_SIGNATURES
int fsverity_verify_signature(const struct fsverity_info *vi,
			      const u8 *signature, size_t sig_size);

int __init fsverity_init_signature(void);
#else /* !CONFIG_FS_VERITY_BUILTIN_SIGNATURES */
static inline int
fsverity_verify_signature(const struct fsverity_info *vi,
			  const u8 *signature, size_t sig_size)
{
	return 0;
}

static inline int fsverity_init_signature(void)
{
	return 0;
}
#endif /* !CONFIG_FS_VERITY_BUILTIN_SIGNATURES */

/* verify.c */

int __init fsverity_init_workqueue(void);
void __init fsverity_exit_workqueue(void);

#endif /* _FSVERITY_PRIVATE_H */
