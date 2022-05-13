// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Christoph Hellwig.
 */
#include "xfs.h"

static inline unsigned int bio_max_vecs(unsigned int count)
{
	return bio_max_segs(howmany(count, PAGE_SIZE));
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

	bio = bio_alloc(bdev, bio_max_vecs(left), op | REQ_META | REQ_SYNC,
			GFP_KERNEL);
	bio->bi_iter.bi_sector = sector;

	do {
		struct page	*page = kmem_to_page(data);
		unsigned int	off = offset_in_page(data);
		unsigned int	len = min_t(unsigned, left, PAGE_SIZE - off);

		while (bio_add_page(bio, page, len, off) != len) {
			struct bio	*prev = bio;

			bio = bio_alloc(prev->bi_bdev, bio_max_vecs(left),
					prev->bi_opf, GFP_KERNEL);
			bio->bi_iter.bi_sector = bio_end_sector(prev);
			bio_chain(prev, bio);

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
