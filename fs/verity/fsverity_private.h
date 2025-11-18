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

/*
 * Implementation limit: maximum depth of the Merkle tree.  For now 8 is plenty;
 * it's enough for over U64_MAX bytes of data using SHA-256 and 4K blocks.
 */
#define FS_VERITY_MAX_LEVELS		8

/* A hash algorithm supported by fs-verity */
struct fsverity_hash_alg {
	const char *name;	  /* crypto API name, e.g. sha256 */
	unsigned int digest_size; /* digest size in bytes, e.g. 32 for SHA-256 */
	unsigned int block_size;  /* block size in bytes, e.g. 64 for SHA-256 */
	/*
	 * The HASH_ALGO_* constant for this algorithm.  This is different from
	 * FS_VERITY_HASH_ALG_*, which uses a different numbering scheme.
	 */
	enum hash_algo algo_id;
};

union fsverity_hash_ctx {
	struct sha256_ctx sha256;
	struct sha512_ctx sha512;
};

/* Merkle tree parameters: hash algorithm, initial hash state, and topology */
struct merkle_tree_params {
	const struct fsverity_hash_alg *hash_alg; /* the hash algorithm */
	/* initial hash state if salted, NULL if unsalted */
	const union fsverity_hash_ctx *hashstate;
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
 * and a pointer to it is stored in the file's in-memory inode.  It remains
 * until the inode is evicted.  It caches information about the Merkle tree
 * that's needed to efficiently verify data read from the file.  It also caches
 * the file digest.  The Merkle tree pages themselves are not cached here, but
 * the filesystem may cache them.
 */
struct fsverity_info {
	struct merkle_tree_params tree_params;
	u8 root_hash[FS_VERITY_MAX_DIGEST_SIZE];
	u8 file_digest[FS_VERITY_MAX_DIGEST_SIZE];
	const struct inode *inode;
	unsigned long *hash_block_verified;
};

#define FS_VERITY_MAX_SIGNATURE_SIZE	(FS_VERITY_MAX_DESCRIPTOR_SIZE - \
					 sizeof(struct fsverity_descriptor))

/* hash_algs.c */

extern const struct fsverity_hash_alg fsverity_hash_algs[];

const struct fsverity_hash_alg *fsverity_get_hash_alg(const struct inode *inode,
						      unsigned int num);
union fsverity_hash_ctx *
fsverity_prepare_hash_state(const struct fsverity_hash_alg *alg,
			    const u8 *salt, size_t salt_size);
void fsverity_hash_block(const struct merkle_tree_params *params,
			 const void *data, u8 *out);
void fsverity_hash_buffer(const struct fsverity_hash_alg *alg,
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

/* measure.c */

#ifdef CONFIG_BPF_SYSCALL
void __init fsverity_init_bpf(void);
#else
static inline void fsverity_init_bpf(void)
{
}
#endif

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

void __init fsverity_init_info_cache(void);

/* signature.c */

#ifdef CONFIG_FS_VERITY_BUILTIN_SIGNATURES
extern int fsverity_require_signatures;
int fsverity_verify_signature(const struct fsverity_info *vi,
			      const u8 *signature, size_t sig_size);

void __init fsverity_init_signature(void);
#else /* !CONFIG_FS_VERITY_BUILTIN_SIGNATURES */
static inline int
fsverity_verify_signature(const struct fsverity_info *vi,
			  const u8 *signature, size_t sig_size)
{
	return 0;
}

static inline void fsverity_init_signature(void)
{
}
#endif /* !CONFIG_FS_VERITY_BUILTIN_SIGNATURES */

/* verify.c */

void __init fsverity_init_workqueue(void);

#endif /* _FSVERITY_PRIVATE_H */
