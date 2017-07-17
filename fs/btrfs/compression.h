/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef __BTRFS_COMPRESSION_
#define __BTRFS_COMPRESSION_

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
	u32 sums;
};

void btrfs_init_compress(void);
void btrfs_exit_compress(void);

int btrfs_compress_pages(int type, struct address_space *mapping,
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
				  unsigned long nr_pages);
blk_status_t btrfs_submit_compressed_read(struct inode *inode, struct bio *bio,
				 int mirror_num, unsigned long bio_flags);

enum btrfs_compression_type {
	BTRFS_COMPRESS_NONE  = 0,
	BTRFS_COMPRESS_ZLIB  = 1,
	BTRFS_COMPRESS_LZO   = 2,
	BTRFS_COMPRESS_TYPES = 2,
	BTRFS_COMPRESS_LAST  = 3,
};

struct btrfs_compress_op {
	struct list_head *(*alloc_workspace)(void);

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
};

extern const struct btrfs_compress_op btrfs_zlib_compress;
extern const struct btrfs_compress_op btrfs_lzo_compress;

int btrfs_compress_heuristic(struct inode *inode, u64 start, u64 end);

#endif
