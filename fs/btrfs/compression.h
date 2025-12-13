/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#ifndef BTRFS_COMPRESSION_H
#define BTRFS_COMPRESSION_H

#include <linux/sizes.h>
#include <linux/mm.h>
#include <linux/list.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/pagemap.h>
#include "bio.h"
#include "fs.h"
#include "messages.h"

struct address_space;
struct page;
struct inode;
struct btrfs_inode;
struct btrfs_ordered_extent;
struct btrfs_bio;

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
#define BTRFS_MAX_COMPRESSED_PAGES	(BTRFS_MAX_COMPRESSED / PAGE_SIZE)
static_assert((BTRFS_MAX_COMPRESSED % PAGE_SIZE) == 0);

/* Maximum size of data before compression */
#define BTRFS_MAX_UNCOMPRESSED		(SZ_128K)

#define	BTRFS_ZLIB_DEFAULT_LEVEL		3

struct compressed_bio {
	/* Number of compressed folios in the array. */
	unsigned int nr_folios;

	/* The folios with the compressed data on them. */
	struct folio **compressed_folios;

	/* starting offset in the inode for our pages */
	u64 start;

	/* Number of bytes in the inode we're working on */
	unsigned int len;

	/* Number of bytes on disk */
	unsigned int compressed_len;

	/* The compression algorithm for this bio */
	u8 compress_type;

	/* Whether this is a write for writeback. */
	bool writeback;

	union {
		/* For reads, this is the bio we are copying the data into */
		struct btrfs_bio *orig_bbio;
		struct work_struct write_end_work;
	};

	/* Must be last. */
	struct btrfs_bio bbio;
};

static inline struct btrfs_fs_info *cb_to_fs_info(const struct compressed_bio *cb)
{
	return cb->bbio.fs_info;
}

/* @range_end must be exclusive. */
static inline u32 btrfs_calc_input_length(struct folio *folio, u64 range_end, u64 cur)
{
	/* @cur must be inside the folio. */
	ASSERT(folio_pos(folio) <= cur);
	ASSERT(cur < folio_end(folio));
	return min(range_end, folio_end(folio)) - cur;
}

int btrfs_alloc_compress_wsm(struct btrfs_fs_info *fs_info);
void btrfs_free_compress_wsm(struct btrfs_fs_info *fs_info);

int __init btrfs_init_compress(void);
void __cold btrfs_exit_compress(void);

bool btrfs_compress_level_valid(unsigned int type, int level);
int btrfs_compress_folios(unsigned int type, int level, struct btrfs_inode *inode,
			  u64 start, struct folio **folios, unsigned long *out_folios,
			 unsigned long *total_in, unsigned long *total_out);
int btrfs_decompress(int type, const u8 *data_in, struct folio *dest_folio,
		     unsigned long start_byte, size_t srclen, size_t destlen);
int btrfs_decompress_buf2page(const char *buf, u32 buf_len,
			      struct compressed_bio *cb, u32 decompressed);

void btrfs_submit_compressed_write(struct btrfs_ordered_extent *ordered,
				   struct folio **compressed_folios,
				   unsigned int nr_folios, blk_opf_t write_flags,
				   bool writeback);
void btrfs_submit_compressed_read(struct btrfs_bio *bbio);

int btrfs_compress_str2level(unsigned int type, const char *str, int *level_ret);

struct folio *btrfs_alloc_compr_folio(struct btrfs_fs_info *fs_info);
void btrfs_free_compr_folio(struct folio *folio);

struct workspace_manager {
	struct list_head idle_ws;
	spinlock_t ws_lock;
	/* Number of free workspaces */
	int free_ws;
	/* Total number of allocated workspaces */
	atomic_t total_ws;
	/* Waiters for a free workspace */
	wait_queue_head_t ws_wait;
};

struct list_head *btrfs_get_workspace(struct btrfs_fs_info *fs_info, int type, int level);
void btrfs_put_workspace(struct btrfs_fs_info *fs_info, int type, struct list_head *ws);

struct btrfs_compress_levels {
	/* Maximum level supported by the compression algorithm */
	int min_level;
	int max_level;
	int default_level;
};

/* The heuristic workspaces are managed via the 0th workspace manager */
#define BTRFS_NR_WORKSPACE_MANAGERS	BTRFS_NR_COMPRESS_TYPES

extern const struct btrfs_compress_levels btrfs_heuristic_compress;
extern const struct btrfs_compress_levels btrfs_zlib_compress;
extern const struct btrfs_compress_levels btrfs_lzo_compress;
extern const struct btrfs_compress_levels btrfs_zstd_compress;

const char* btrfs_compress_type2str(enum btrfs_compression_type type);
bool btrfs_compress_is_valid_type(const char *str, size_t len);

int btrfs_compress_heuristic(struct btrfs_inode *inode, u64 start, u64 end);

int btrfs_compress_filemap_get_folio(struct address_space *mapping, u64 start,
				     struct folio **in_folio_ret);

int zlib_compress_folios(struct list_head *ws, struct btrfs_inode *inode,
			 u64 start, struct folio **folios, unsigned long *out_folios,
		unsigned long *total_in, unsigned long *total_out);
int zlib_decompress_bio(struct list_head *ws, struct compressed_bio *cb);
int zlib_decompress(struct list_head *ws, const u8 *data_in,
		struct folio *dest_folio, unsigned long dest_pgoff, size_t srclen,
		size_t destlen);
struct list_head *zlib_alloc_workspace(struct btrfs_fs_info *fs_info, unsigned int level);
void zlib_free_workspace(struct list_head *ws);
struct list_head *zlib_get_workspace(struct btrfs_fs_info *fs_info, unsigned int level);

int lzo_compress_folios(struct list_head *ws, struct btrfs_inode *inode,
			u64 start, struct folio **folios, unsigned long *out_folios,
		unsigned long *total_in, unsigned long *total_out);
int lzo_decompress_bio(struct list_head *ws, struct compressed_bio *cb);
int lzo_decompress(struct list_head *ws, const u8 *data_in,
		struct folio *dest_folio, unsigned long dest_pgoff, size_t srclen,
		size_t destlen);
struct list_head *lzo_alloc_workspace(struct btrfs_fs_info *fs_info);
void lzo_free_workspace(struct list_head *ws);

int zstd_compress_folios(struct list_head *ws, struct btrfs_inode *inode,
			 u64 start, struct folio **folios, unsigned long *out_folios,
		unsigned long *total_in, unsigned long *total_out);
int zstd_decompress_bio(struct list_head *ws, struct compressed_bio *cb);
int zstd_decompress(struct list_head *ws, const u8 *data_in,
		struct folio *dest_folio, unsigned long dest_pgoff, size_t srclen,
		size_t destlen);
int zstd_alloc_workspace_manager(struct btrfs_fs_info *fs_info);
void zstd_free_workspace_manager(struct btrfs_fs_info *fs_info);
struct list_head *zstd_alloc_workspace(struct btrfs_fs_info *fs_info, int level);
void zstd_free_workspace(struct list_head *ws);
struct list_head *zstd_get_workspace(struct btrfs_fs_info *fs_info, int level);
void zstd_put_workspace(struct btrfs_fs_info *fs_info, struct list_head *ws);

#endif
