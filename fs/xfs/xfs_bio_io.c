// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Christoph Hellwig.
 */
#include "xfs.h"

static inline unsigned int bio_max_vecs(unsigned int count)
{
	return min_t(unsigned, howmany(count, PAGE_SIZE), BIO_MAX_PAGES);
}

int
xfs_rw_bdev(
	struct block_device	*bdev,
	sector_t		sector,
	unsigned int		count,
	char			*data,
	unsigned int		op)

{
	unsigned int		is_vmalloc = is_vmalloc_addr(data);
	unsigned int		left = count;
	int			error;
	struct bio		*bio;

	if (is_vmalloc && op == REQ_OP_WRITE)
		flush_kernel_vmap_range(data, count);

	bio = bio_alloc(GFP_KERNEL, bio_max_vecs(left));
	bio_set_dev(bio, bdev);
	bio->bi_iter.bi_sector = sector;
	bio->bi_opf = op | REQ_META | REQ_SYNC;

	do {
		struct page	*page = kmem_to_page(data);
		unsigned int	off = offset_in_page(data);
		unsigned int	len = min_t(unsigned, left, PAGE_SIZE - off);

		while (bio_add_page(bio, page, len, off) != len) {
			struct bio	*prev = bio;

			bio = bio_alloc(GFP_KERNEL, bio_max_vecs(left));
			bio_copy_dev(bio, prev);
			bio->bi_iter.bi_sector = bio_end_sector(prev);
			bio->bi_opf = prev->bi_opf;
			bio_chain(bio, prev);

			submit_bio(prev);
		}

		data += len;
		left -= len;
	} while (left > 0);

	error = submit_bio_wait(bio);
	bio_put(bio);

	if (is_vmalloc && op == REQ_OP_READ)
		invalidate_kernel_vmap_range(data, count);
	return error;
}
