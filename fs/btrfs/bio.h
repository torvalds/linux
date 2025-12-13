/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 * Copyright (C) 2022 Christoph Hellwig.
 */

#ifndef BTRFS_BIO_H
#define BTRFS_BIO_H

#include <linux/types.h>
#include <linux/bio.h>
#include <linux/workqueue.h>
#include "tree-checker.h"

struct btrfs_bio;
struct btrfs_fs_info;
struct btrfs_inode;

#define BTRFS_BIO_INLINE_CSUM_SIZE	64

/*
 * Maximum number of sectors for a single bio to limit the size of the
 * checksum array.  This matches the number of bio_vecs per bio and thus the
 * I/O size for buffered I/O.
 */
#define BTRFS_MAX_BIO_SECTORS		(256)

typedef void (*btrfs_bio_end_io_t)(struct btrfs_bio *bbio);

/*
 * Highlevel btrfs I/O structure.  It is allocated by btrfs_bio_alloc and
 * passed to btrfs_submit_bbio() for mapping to the physical devices.
 */
struct btrfs_bio {
	/*
	 * Inode and offset into it that this I/O operates on.
	 * Only set for data I/O.
	 */
	struct btrfs_inode *inode;
	u64 file_offset;

	union {
		/*
		 * For data reads: checksumming and original I/O information.
		 * (for internal use in the btrfs_submit_bbio() machinery only)
		 */
		struct {
			u8 *csum;
			u8 csum_inline[BTRFS_BIO_INLINE_CSUM_SIZE];
			struct bvec_iter saved_iter;
		};

		/*
		 * For data writes:
		 * - ordered extent covering the bio
		 * - pointer to the checksums for this bio
		 * - original physical address from the allocator
		 *   (for zone append only)
		 */
		struct {
			struct btrfs_ordered_extent *ordered;
			struct btrfs_ordered_sum *sums;
			u64 orig_physical;
		};

		/* For metadata reads: parentness verification. */
		struct btrfs_tree_parent_check parent_check;
	};

	/* End I/O information supplied to btrfs_bio_alloc */
	btrfs_bio_end_io_t end_io;
	void *private;

	/* For internal use in read end I/O handling */
	unsigned int mirror_num;
	atomic_t pending_ios;
	struct work_struct end_io_work;

	/* File system that this I/O operates on. */
	struct btrfs_fs_info *fs_info;

	/* Save the first error status of split bio. */
	blk_status_t status;

	/* Use the commit root to look up csums (data read bio only). */
	bool csum_search_commit_root;
	/*
	 * This member must come last, bio_alloc_bioset will allocate enough
	 * bytes for entire btrfs_bio but relies on bio being last.
	 */
	struct bio bio;
};

static inline struct btrfs_bio *btrfs_bio(struct bio *bio)
{
	return container_of(bio, struct btrfs_bio, bio);
}

int __init btrfs_bioset_init(void);
void __cold btrfs_bioset_exit(void);

void btrfs_bio_init(struct btrfs_bio *bbio, struct btrfs_fs_info *fs_info,
		    btrfs_bio_end_io_t end_io, void *private);
struct btrfs_bio *btrfs_bio_alloc(unsigned int nr_vecs, blk_opf_t opf,
				  struct btrfs_fs_info *fs_info,
				  btrfs_bio_end_io_t end_io, void *private);
void btrfs_bio_end_io(struct btrfs_bio *bbio, blk_status_t status);

/* Submit using blkcg_punt_bio_submit. */
#define REQ_BTRFS_CGROUP_PUNT			REQ_FS_PRIVATE

void btrfs_submit_bbio(struct btrfs_bio *bbio, int mirror_num);
void btrfs_submit_repair_write(struct btrfs_bio *bbio, int mirror_num, bool dev_replace);
int btrfs_repair_io_failure(struct btrfs_fs_info *fs_info, u64 ino, u64 start,
			    u64 length, u64 logical, phys_addr_t paddr, int mirror_num);

#endif
