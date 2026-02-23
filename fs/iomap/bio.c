// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (C) 2016-2023 Christoph Hellwig.
 */
#include <linux/iomap.h>
#include <linux/pagemap.h>
#include "internal.h"
#include "trace.h"

static void iomap_read_end_io(struct bio *bio)
{
	int error = blk_status_to_errno(bio->bi_status);
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio)
		iomap_finish_folio_read(fi.folio, fi.offset, fi.length, error);
	bio_put(bio);
}

static void iomap_bio_submit_read(struct iomap_read_folio_ctx *ctx)
{
	struct bio *bio = ctx->read_ctx;

	if (bio)
		submit_bio(bio);
}

static void iomap_read_alloc_bio(const struct iomap_iter *iter,
		struct iomap_read_folio_ctx *ctx, size_t plen)
{
	const struct iomap *iomap = &iter->iomap;
	unsigned int nr_vecs = DIV_ROUND_UP(iomap_length(iter), PAGE_SIZE);
	struct folio *folio = ctx->cur_folio;
	gfp_t gfp = mapping_gfp_constraint(folio->mapping, GFP_KERNEL);
	gfp_t orig_gfp = gfp;
	struct bio *bio = ctx->read_ctx;

	if (bio)
		submit_bio(bio);

	/* Same as readahead_gfp_mask: */
	if (ctx->rac)
		gfp |= __GFP_NORETRY | __GFP_NOWARN;

	/*
	 * If the bio_alloc fails, try it again for a single page to avoid
	 * having to deal with partial page reads.  This emulates what
	 * do_mpage_read_folio does.
	 */
	bio = bio_alloc(iomap->bdev, bio_max_segs(nr_vecs), REQ_OP_READ, gfp);
	if (!bio)
		bio = bio_alloc(iomap->bdev, 1, REQ_OP_READ, orig_gfp);
	if (ctx->rac)
		bio->bi_opf |= REQ_RAHEAD;
	bio->bi_iter.bi_sector = iomap_sector(iomap, iter->pos);
	bio->bi_end_io = iomap_read_end_io;
	bio_add_folio_nofail(bio, folio, plen,
			offset_in_folio(folio, iter->pos));
	ctx->read_ctx = bio;
}

static int iomap_bio_read_folio_range(const struct iomap_iter *iter,
		struct iomap_read_folio_ctx *ctx, size_t plen)
{
	struct folio *folio = ctx->cur_folio;
	struct bio *bio = ctx->read_ctx;

	if (!bio ||
	    bio_end_sector(bio) != iomap_sector(&iter->iomap, iter->pos) ||
	    !bio_add_folio(bio, folio, plen, offset_in_folio(folio, iter->pos)))
		iomap_read_alloc_bio(iter, ctx, plen);
	return 0;
}

const struct iomap_read_ops iomap_bio_read_ops = {
	.read_folio_range = iomap_bio_read_folio_range,
	.submit_read = iomap_bio_submit_read,
};
EXPORT_SYMBOL_GPL(iomap_bio_read_ops);

int iomap_bio_read_folio_range_sync(const struct iomap_iter *iter,
		struct folio *folio, loff_t pos, size_t len)
{
	const struct iomap *srcmap = iomap_iter_srcmap(iter);
	struct bio_vec bvec;
	struct bio bio;

	bio_init(&bio, srcmap->bdev, &bvec, 1, REQ_OP_READ);
	bio.bi_iter.bi_sector = iomap_sector(srcmap, pos);
	bio_add_folio_nofail(&bio, folio, len, offset_in_folio(folio, pos));
	return submit_bio_wait(&bio);
}
