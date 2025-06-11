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
	enum req_op		op)

{
	unsigned int		done = 0, added;
	int			error;
	struct bio		*bio;

	op |= REQ_META | REQ_SYNC;
	if (!is_vmalloc_addr(data))
		return bdev_rw_virt(bdev, sector, data, count, op);

	bio = bio_alloc(bdev, bio_max_vecs(count), op, GFP_KERNEL);
	bio->bi_iter.bi_sector = sector;

	do {
		added = bio_add_vmalloc_chunk(bio, data + done, count - done);
		if (!added) {
			struct bio	*prev = bio;

			bio = bio_alloc(prev->bi_bdev,
					bio_max_vecs(count - done),
					prev->bi_opf, GFP_KERNEL);
			bio->bi_iter.bi_sector = bio_end_sector(prev);
			bio_chain(prev, bio);
			submit_bio(prev);
		}
		done += added;
	} while (done < count);

	error = submit_bio_wait(bio);
	bio_put(bio);

	if (op == REQ_OP_READ)
		invalidate_kernel_vmap_range(data, count);
	return error;
}
