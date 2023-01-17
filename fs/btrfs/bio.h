/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 * Copyright (C) 2022 Christoph Hellwig.
 */

#ifndef BTRFS_BIO_H
#define BTRFS_BIO_H

#include <linux/bio.h>
#include <linux/workqueue.h>
#include "tree-checker.h"

struct btrfs_bio;
struct btrfs_fs_info;

#define BTRFS_BIO_INLINE_CSUM_SIZE	64

/*
 * Maximum number of sectors for a single bio to limit the size of the
 * checksum array.  This matches the number of bio_vecs per bio and thus the
 * I/O size for buffered I/O.
 */
#define BTRFS_MAX_BIO_SECTORS		(256)

typedef void (*btrfs_bio_end_io_t)(struct btrfs_bio *bbio);

/*
 * Additional info to pass along bio.
 *
 * Mostly for btrfs specific features like csum and mirror_num.
 */
struct btrfs_bio {
	unsigned int mirror_num:7;

	/*
	 * Extra indicator for metadata bios.
	 * For some btrfs bios they use pages without a mapping, thus
	 * we can not rely on page->mapping->host to determine if
	 * it's a metadata bio.
	 */
	unsigned int is_metadata:1;
	struct bvec_iter iter;

	/* for direct I/O */
	u64 file_offset;

	/* @device is for stripe IO submission. */
	struct btrfs_device *device;
	union {
		/* For data checksum verification. */
		struct {
			u8 *csum;
			u8 csum_inline[BTRFS_BIO_INLINE_CSUM_SIZE];
		};

		/* For metadata parentness verification. */
		struct btrfs_tree_parent_check parent_check;
	};

	/* End I/O information supplied to btrfs_bio_alloc */
	btrfs_bio_end_io_t end_io;
	void *private;

	/* For read end I/O handling */
	struct work_struct end_io_work;

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

struct bio *btrfs_bio_alloc(unsigned int nr_vecs, blk_opf_t opf,
			    btrfs_bio_end_io_t end_io, void *private);
struct bio *btrfs_bio_clone_partial(struct bio *orig, u64 offset, u64 size,
				    btrfs_bio_end_io_t end_io, void *private);


static inline void btrfs_bio_end_io(struct btrfs_bio *bbio, blk_status_t status)
{
	bbio->bio.bi_status = status;
	bbio->end_io(bbio);
}

static inline void btrfs_bio_free_csum(struct btrfs_bio *bbio)
{
	if (bbio->is_metadata)
		return;
	if (bbio->csum != bbio->csum_inline) {
		kfree(bbio->csum);
		bbio->csum = NULL;
	}
}

/*
 * Iterate through a btrfs_bio (@bbio) on a per-sector basis.
 *
 * bvl        - struct bio_vec
 * bbio       - struct btrfs_bio
 * iters      - struct bvec_iter
 * bio_offset - unsigned int
 */
#define btrfs_bio_for_each_sector(fs_info, bvl, bbio, iter, bio_offset)	\
	for ((iter) = (bbio)->iter, (bio_offset) = 0;			\
	     (iter).bi_size &&					\
	     (((bvl) = bio_iter_iovec((&(bbio)->bio), (iter))), 1);	\
	     (bio_offset) += fs_info->sectorsize,			\
	     bio_advance_iter_single(&(bbio)->bio, &(iter),		\
	     (fs_info)->sectorsize))

void btrfs_submit_bio(struct btrfs_fs_info *fs_info, struct bio *bio,
		      int mirror_num);
int btrfs_repair_io_failure(struct btrfs_fs_info *fs_info, u64 ino, u64 start,
			    u64 length, u64 logical, struct page *page,
			    unsigned int pg_offset, int mirror_num);

#endif
