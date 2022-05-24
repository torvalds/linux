// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Christoph Hellwig.
 */
#include "xfs.h"

static inline unsigned int bio_max_vecs(unsigned int count)
{
	return bio_max_segs(howmany(count, PAGE_SIZE));
}

static void
xfs_flush_bdev_async_endio(
	struct bio	*bio)
{
	complete(bio->bi_private);
}

/*
 * Submit a request for an async cache flush to run. If the request queue does
 * not require flush operations, just skip it altogether. If the caller needs
 * to wait for the flush completion at a later point in time, they must supply a
 * valid completion. This will be signalled when the flush completes.  The
 * caller never sees the bio that is issued here.
 */
void
xfs_flush_bdev_async(
	struct bio		*bio,
	struct block_device	*bdev,
	struct completion	*done)
{
	struct request_queue	*q = bdev->bd_disk->queue;

	if (!test_bit(QUEUE_FLAG_WC, &q->queue_flags)) {
		complete(done);
		return;
	}

	bio_init(bio, bdev, NULL, 0, REQ_OP_WRITE | REQ_PREFLUSH | REQ_SYNC);
	bio->bi_private = done;
	bio->bi_end_io = xfs_flush_bdev_async_endio;

	submit_bio(bio);
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
