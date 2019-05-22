/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#ifndef BTRFS_COMPRESSION_H
#define BTRFS_COMPRESSION_H

#include <linux/sizes.h>

/*
 * We want to make sure that amount of RAM required to uncompress an extent is
 * reasonable, so we limit the total size in ram of a compressed extent to
 * 128k.  This is a crucial number because it also controls how easily we can
 * spread reads across cpus for decompression.
 *
 * We also want to make sure the amount of IO required to do a random read is
 * reasonably small, so we limit the size of a compressed extent to 128k.
 */

/* Maximum length of compressed data stored on disk */
#define BTRFS_MAX_COMPRESSED		(SZ_128K)
/* Maximum size of data before compression */
#define BTRFS_MAX_UNCOMPRESSED		(SZ_128K)

#define	BTRFS_ZLIB_DEFAULT_LEVEL		3

struct compressed_bio {
	/* number of bios pending for this compressed extent */
	refcount_t pending_bios;

	/* the pages with the compressed data on them */
	struct page **compressed_pages;

	/* inode that owns this data */
	struct inode *inode;

	/* starting offset in the inode for our pages */
	u64 start;

	/* number of bytes in the inode we're working on */
	unsigned long len;

	/* number of bytes on disk */
	unsigned long compressed_len;

	/* the compression algorithm for this bio */
	int compress_type;

	/* number of compressed pages in the array */
	unsigned long nr_pages;

	/* IO errors */
	int errors;
	int mirror_num;

	/* for reads, this is the bio we are copying the data into */
	struct bio *orig_bio;

	/*
	 * the start of a variable length array of checksums only
	 * used by reads
	 */
	u8 sums[];
};

static inline unsigned int btrfs_compress_type(unsigned int type_level)
{
	return (type_level & 0xF);
}

static inline unsigned int btrfs_compress_level(unsigned int type_level)
{
	return ((type_level & 0xF0) >> 4);
}

void __init btrfs_init_compress(void);
void __cold btrfs_exit_compress(void);

int btrfs_compress_pages(unsigned int type_level, struct address_space *mapping,
			 u64 start, struct page **pages,
			 unsigned long *out_pages,
			 unsigned long *total_in,
			 unsigned long *total_out);
int btrfs_decompress(int type, unsigned char *data_in, struct page *dest_page,
		     unsigned long start_byte, size_t srclen, size_t destlen);
int btrfs_decompress_buf2page(const char *buf, unsigned long buf_start,
			      unsigned long total_out, u64 disk_start,
			      struct bio *bio);

blk_status_t btrfs_submit_compressed_write(struct inode *inode, u64 start,
				  unsigned long len, u64 disk_start,
				  unsigned long compressed_len,
				  struct page **compressed_pages,
				  unsigned long nr_pages,
				  unsigned int write_flags);
blk_status_t btrfs_submit_compressed_read(struct inode *inode, struct bio *bio,
				 int mirror_num, unsigned long bio_flags);

unsigned int btrfs_compress_str2level(unsigned int type, const char *str);

enum btrfs_compression_type {
	BTRFS_COMPRESS_NONE  = 0,
	BTRFS_COMPRESS_ZLIB  = 1,
	BTRFS_COMPRESS_LZO   = 2,
	BTRFS_COMPRESS_ZSTD  = 3,
	BTRFS_COMPRESS_TYPES = 3,
};

struct workspace_manager {
	const struct btrfs_compress_op *ops;
	struct list_head idle_ws;
	spinlock_t ws_lock;
	/* Number of free workspaces */
	int free_ws;
	/* Total number of allocated workspaces */
	atomic_t total_ws;
	/* Waiters for a free workspace */
	wait_queue_head_t ws_wait;
};

void btrfs_init_workspace_manager(struct workspace_manager *wsm,
				  const struct btrfs_compress_op *ops);
struct list_head *btrfs_get_workspace(struct workspace_manager *wsm,
				      unsigned int level);
void btrfs_put_workspace(struct workspace_manager *wsm, struct list_head *ws);
void btrfs_cleanup_workspace_manager(struct workspace_manager *wsm);

struct btrfs_compress_op {
	void (*init_workspace_manager)(void);

	void (*cleanup_workspace_manager)(void);

	struct list_head *(*get_workspace)(unsigned int level);

	void (*put_workspace)(struct list_head *ws);

	struct list_head *(*alloc_workspace)(unsigned int level);

	void (*free_workspace)(struct list_head *workspace);

	int (*compress_pages)(struct list_head *workspace,
			      struct address_space *mapping,
			      u64 start,
			      struct page **pages,
			      unsigned long *out_pages,
			      unsigned long *total_in,
			      unsigned long *total_out);

	int (*decompress_bio)(struct list_head *workspace,
				struct compressed_bio *cb);

	int (*decompress)(struct list_head *workspace,
			  unsigned char *data_in,
			  struct page *dest_page,
			  unsigned long start_byte,
			  size_t srclen, size_t destlen);

	/*
	 * This bounds the level set by the user to be within range of a
	 * particular compression type.  It returns the level that will be used
	 * if the level is out of bounds or the default if 0 is passed in.
	 */
	unsigned int (*set_level)(unsigned int level);
};

/* The heuristic workspaces are managed via the 0th workspace manager */
#define BTRFS_NR_WORKSPACE_MANAGERS	(BTRFS_COMPRESS_TYPES + 1)

extern const struct btrfs_compress_op btrfs_heuristic_compress;
extern const struct btrfs_compress_op btrfs_zlib_compress;
extern const struct btrfs_compress_op btrfs_lzo_compress;
extern const struct btrfs_compress_op btrfs_zstd_compress;

const char* btrfs_compress_type2str(enum btrfs_compression_type type);

int btrfs_compress_heuristic(struct inode *inode, u64 start, u64 end);

#endif
