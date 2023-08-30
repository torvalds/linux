// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (C) 2016-2019 Christoph Hellwig.
 */
#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/iomap.h>
#include <linux/pagemap.h>
#include <linux/uio.h>
#include <linux/buffer_head.h>
#include <linux/dax.h>
#include <linux/writeback.h>
#include <linux/list_sort.h>
#include <linux/swap.h>
#include <linux/bio.h>
#include <linux/sched/signal.h>
#include <linux/migrate.h>
#include "trace.h"

#include "../internal.h"

#define IOEND_BATCH_SIZE	4096

typedef int (*iomap_punch_t)(struct inode *inode, loff_t offset, loff_t length);
/*
 * Structure allocated for each folio to track per-block uptodate, dirty state
 * and I/O completions.
 */
struct iomap_folio_state {
	atomic_t		read_bytes_pending;
	atomic_t		write_bytes_pending;
	spinlock_t		state_lock;

	/*
	 * Each block has two bits in this bitmap:
	 * Bits [0..blocks_per_folio) has the uptodate status.
	 * Bits [b_p_f...(2*b_p_f))   has the dirty status.
	 */
	unsigned long		state[];
};

static struct bio_set iomap_ioend_bioset;

static inline bool ifs_is_fully_uptodate(struct folio *folio,
		struct iomap_folio_state *ifs)
{
	struct inode *inode = folio->mapping->host;

	return bitmap_full(ifs->state, i_blocks_per_folio(inode, folio));
}

static inline bool ifs_block_is_uptodate(struct iomap_folio_state *ifs,
		unsigned int block)
{
	return test_bit(block, ifs->state);
}

static void ifs_set_range_uptodate(struct folio *folio,
		struct iomap_folio_state *ifs, size_t off, size_t len)
{
	struct inode *inode = folio->mapping->host;
	unsigned int first_blk = off >> inode->i_blkbits;
	unsigned int last_blk = (off + len - 1) >> inode->i_blkbits;
	unsigned int nr_blks = last_blk - first_blk + 1;
	unsigned long flags;

	spin_lock_irqsave(&ifs->state_lock, flags);
	bitmap_set(ifs->state, first_blk, nr_blks);
	if (ifs_is_fully_uptodate(folio, ifs))
		folio_mark_uptodate(folio);
	spin_unlock_irqrestore(&ifs->state_lock, flags);
}

static void iomap_set_range_uptodate(struct folio *folio, size_t off,
		size_t len)
{
	struct iomap_folio_state *ifs = folio->private;

	if (ifs)
		ifs_set_range_uptodate(folio, ifs, off, len);
	else
		folio_mark_uptodate(folio);
}

static inline bool ifs_block_is_dirty(struct folio *folio,
		struct iomap_folio_state *ifs, int block)
{
	struct inode *inode = folio->mapping->host;
	unsigned int blks_per_folio = i_blocks_per_folio(inode, folio);

	return test_bit(block + blks_per_folio, ifs->state);
}

static void ifs_clear_range_dirty(struct folio *folio,
		struct iomap_folio_state *ifs, size_t off, size_t len)
{
	struct inode *inode = folio->mapping->host;
	unsigned int blks_per_folio = i_blocks_per_folio(inode, folio);
	unsigned int first_blk = (off >> inode->i_blkbits);
	unsigned int last_blk = (off + len - 1) >> inode->i_blkbits;
	unsigned int nr_blks = last_blk - first_blk + 1;
	unsigned long flags;

	spin_lock_irqsave(&ifs->state_lock, flags);
	bitmap_clear(ifs->state, first_blk + blks_per_folio, nr_blks);
	spin_unlock_irqrestore(&ifs->state_lock, flags);
}

static void iomap_clear_range_dirty(struct folio *folio, size_t off, size_t len)
{
	struct iomap_folio_state *ifs = folio->private;

	if (ifs)
		ifs_clear_range_dirty(folio, ifs, off, len);
}

static void ifs_set_range_dirty(struct folio *folio,
		struct iomap_folio_state *ifs, size_t off, size_t len)
{
	struct inode *inode = folio->mapping->host;
	unsigned int blks_per_folio = i_blocks_per_folio(inode, folio);
	unsigned int first_blk = (off >> inode->i_blkbits);
	unsigned int last_blk = (off + len - 1) >> inode->i_blkbits;
	unsigned int nr_blks = last_blk - first_blk + 1;
	unsigned long flags;

	spin_lock_irqsave(&ifs->state_lock, flags);
	bitmap_set(ifs->state, first_blk + blks_per_folio, nr_blks);
	spin_unlock_irqrestore(&ifs->state_lock, flags);
}

static void iomap_set_range_dirty(struct folio *folio, size_t off, size_t len)
{
	struct iomap_folio_state *ifs = folio->private;

	if (ifs)
		ifs_set_range_dirty(folio, ifs, off, len);
}

static struct iomap_folio_state *ifs_alloc(struct inode *inode,
		struct folio *folio, unsigned int flags)
{
	struct iomap_folio_state *ifs = folio->private;
	unsigned int nr_blocks = i_blocks_per_folio(inode, folio);
	gfp_t gfp;

	if (ifs || nr_blocks <= 1)
		return ifs;

	if (flags & IOMAP_NOWAIT)
		gfp = GFP_NOWAIT;
	else
		gfp = GFP_NOFS | __GFP_NOFAIL;

	/*
	 * ifs->state tracks two sets of state flags when the
	 * filesystem block size is smaller than the folio size.
	 * The first state tracks per-block uptodate and the
	 * second tracks per-block dirty state.
	 */
	ifs = kzalloc(struct_size(ifs, state,
		      BITS_TO_LONGS(2 * nr_blocks)), gfp);
	if (!ifs)
		return ifs;

	spin_lock_init(&ifs->state_lock);
	if (folio_test_uptodate(folio))
		bitmap_set(ifs->state, 0, nr_blocks);
	if (folio_test_dirty(folio))
		bitmap_set(ifs->state, nr_blocks, nr_blocks);
	folio_attach_private(folio, ifs);

	return ifs;
}

static void ifs_free(struct folio *folio)
{
	struct iomap_folio_state *ifs = folio_detach_private(folio);

	if (!ifs)
		return;
	WARN_ON_ONCE(atomic_read(&ifs->read_bytes_pending));
	WARN_ON_ONCE(atomic_read(&ifs->write_bytes_pending));
	WARN_ON_ONCE(ifs_is_fully_uptodate(folio, ifs) !=
			folio_test_uptodate(folio));
	kfree(ifs);
}

/*
 * Calculate the range inside the folio that we actually need to read.
 */
static void iomap_adjust_read_range(struct inode *inode, struct folio *folio,
		loff_t *pos, loff_t length, size_t *offp, size_t *lenp)
{
	struct iomap_folio_state *ifs = folio->private;
	loff_t orig_pos = *pos;
	loff_t isize = i_size_read(inode);
	unsigned block_bits = inode->i_blkbits;
	unsigned block_size = (1 << block_bits);
	size_t poff = offset_in_folio(folio, *pos);
	size_t plen = min_t(loff_t, folio_size(folio) - poff, length);
	unsigned first = poff >> block_bits;
	unsigned last = (poff + plen - 1) >> block_bits;

	/*
	 * If the block size is smaller than the page size, we need to check the
	 * per-block uptodate status and adjust the offset and length if needed
	 * to avoid reading in already uptodate ranges.
	 */
	if (ifs) {
		unsigned int i;

		/* move forward for each leading block marked uptodate */
		for (i = first; i <= last; i++) {
			if (!ifs_block_is_uptodate(ifs, i))
				break;
			*pos += block_size;
			poff += block_size;
			plen -= block_size;
			first++;
		}

		/* truncate len if we find any trailing uptodate block(s) */
		for ( ; i <= last; i++) {
			if (ifs_block_is_uptodate(ifs, i)) {
				plen -= (last - i + 1) * block_size;
				last = i - 1;
				break;
			}
		}
	}

	/*
	 * If the extent spans the block that contains the i_size, we need to
	 * handle both halves separately so that we properly zero data in the
	 * page cache for blocks that are entirely outside of i_size.
	 */
	if (orig_pos <= isize && orig_pos + length > isize) {
		unsigned end = offset_in_folio(folio, isize - 1) >> block_bits;

		if (first <= end && last > end)
			plen -= (last - end) * block_size;
	}

	*offp = poff;
	*lenp = plen;
}

static void iomap_finish_folio_read(struct folio *folio, size_t offset,
		size_t len, int error)
{
	struct iomap_folio_state *ifs = folio->private;

	if (unlikely(error)) {
		folio_clear_uptodate(folio);
		folio_set_error(folio);
	} else {
		iomap_set_range_uptodate(folio, offset, len);
	}

	if (!ifs || atomic_sub_and_test(len, &ifs->read_bytes_pending))
		folio_unlock(folio);
}

static void iomap_read_end_io(struct bio *bio)
{
	int error = blk_status_to_errno(bio->bi_status);
	struct folio_iter fi;

	bio_for_each_folio_all(fi, bio)
		iomap_finish_folio_read(fi.folio, fi.offset, fi.length, error);
	bio_put(bio);
}

struct iomap_readpage_ctx {
	struct folio		*cur_folio;
	bool			cur_folio_in_bio;
	struct bio		*bio;
	struct readahead_control *rac;
};

/**
 * iomap_read_inline_data - copy inline data into the page cache
 * @iter: iteration structure
 * @folio: folio to copy to
 *
 * Copy the inline data in @iter into @folio and zero out the rest of the folio.
 * Only a single IOMAP_INLINE extent is allowed at the end of each file.
 * Returns zero for success to complete the read, or the usual negative errno.
 */
static int iomap_read_inline_data(const struct iomap_iter *iter,
		struct folio *folio)
{
	const struct iomap *iomap = iomap_iter_srcmap(iter);
	size_t size = i_size_read(iter->inode) - iomap->offset;
	size_t poff = offset_in_page(iomap->offset);
	size_t offset = offset_in_folio(folio, iomap->offset);
	void *addr;

	if (folio_test_uptodate(folio))
		return 0;

	if (WARN_ON_ONCE(size > PAGE_SIZE - poff))
		return -EIO;
	if (WARN_ON_ONCE(size > PAGE_SIZE -
			 offset_in_page(iomap->inline_data)))
		return -EIO;
	if (WARN_ON_ONCE(size > iomap->length))
		return -EIO;
	if (offset > 0)
		ifs_alloc(iter->inode, folio, iter->flags);

	addr = kmap_local_folio(folio, offset);
	memcpy(addr, iomap->inline_data, size);
	memset(addr + size, 0, PAGE_SIZE - poff - size);
	kunmap_local(addr);
	iomap_set_range_uptodate(folio, offset, PAGE_SIZE - poff);
	return 0;
}

static inline bool iomap_block_needs_zeroing(const struct iomap_iter *iter,
		loff_t pos)
{
	const struct iomap *srcmap = iomap_iter_srcmap(iter);

	return srcmap->type != IOMAP_MAPPED ||
		(srcmap->flags & IOMAP_F_NEW) ||
		pos >= i_size_read(iter->inode);
}

static loff_t iomap_readpage_iter(const struct iomap_iter *iter,
		struct iomap_readpage_ctx *ctx, loff_t offset)
{
	const struct iomap *iomap = &iter->iomap;
	loff_t pos = iter->pos + offset;
	loff_t length = iomap_length(iter) - offset;
	struct folio *folio = ctx->cur_folio;
	struct iomap_folio_state *ifs;
	loff_t orig_pos = pos;
	size_t poff, plen;
	sector_t sector;

	if (iomap->type == IOMAP_INLINE)
		return iomap_read_inline_data(iter, folio);

	/* zero post-eof blocks as the page may be mapped */
	ifs = ifs_alloc(iter->inode, folio, iter->flags);
	iomap_adjust_read_range(iter->inode, folio, &pos, length, &poff, &plen);
	if (plen == 0)
		goto done;

	if (iomap_block_needs_zeroing(iter, pos)) {
		folio_zero_range(folio, poff, plen);
		iomap_set_range_uptodate(folio, poff, plen);
		goto done;
	}

	ctx->cur_folio_in_bio = true;
	if (ifs)
		atomic_add(plen, &ifs->read_bytes_pending);

	sector = iomap_sector(iomap, pos);
	if (!ctx->bio ||
	    bio_end_sector(ctx->bio) != sector ||
	    !bio_add_folio(ctx->bio, folio, plen, poff)) {
		gfp_t gfp = mapping_gfp_constraint(folio->mapping, GFP_KERNEL);
		gfp_t orig_gfp = gfp;
		unsigned int nr_vecs = DIV_ROUND_UP(length, PAGE_SIZE);

		if (ctx->bio)
			submit_bio(ctx->bio);

		if (ctx->rac) /* same as readahead_gfp_mask */
			gfp |= __GFP_NORETRY | __GFP_NOWARN;
		ctx->bio = bio_alloc(iomap->bdev, bio_max_segs(nr_vecs),
				     REQ_OP_READ, gfp);
		/*
		 * If the bio_alloc fails, try it again for a single page to
		 * avoid having to deal with partial page reads.  This emulates
		 * what do_mpage_read_folio does.
		 */
		if (!ctx->bio) {
			ctx->bio = bio_alloc(iomap->bdev, 1, REQ_OP_READ,
					     orig_gfp);
		}
		if (ctx->rac)
			ctx->bio->bi_opf |= REQ_RAHEAD;
		ctx->bio->bi_iter.bi_sector = sector;
		ctx->bio->bi_end_io = iomap_read_end_io;
		bio_add_folio_nofail(ctx->bio, folio, plen, poff);
	}

done:
	/*
	 * Move the caller beyond our range so that it keeps making progress.
	 * For that, we have to include any leading non-uptodate ranges, but
	 * we can skip trailing ones as they will be handled in the next
	 * iteration.
	 */
	return pos - orig_pos + plen;
}

int iomap_read_folio(struct folio *folio, const struct iomap_ops *ops)
{
	struct iomap_iter iter = {
		.inode		= folio->mapping->host,
		.pos		= folio_pos(folio),
		.len		= folio_size(folio),
	};
	struct iomap_readpage_ctx ctx = {
		.cur_folio	= folio,
	};
	int ret;

	trace_iomap_readpage(iter.inode, 1);

	while ((ret = iomap_iter(&iter, ops)) > 0)
		iter.processed = iomap_readpage_iter(&iter, &ctx, 0);

	if (ret < 0)
		folio_set_error(folio);

	if (ctx.bio) {
		submit_bio(ctx.bio);
		WARN_ON_ONCE(!ctx.cur_folio_in_bio);
	} else {
		WARN_ON_ONCE(ctx.cur_folio_in_bio);
		folio_unlock(folio);
	}

	/*
	 * Just like mpage_readahead and block_read_full_folio, we always
	 * return 0 and just set the folio error flag on errors.  This
	 * should be cleaned up throughout the stack eventually.
	 */
	return 0;
}
EXPORT_SYMBOL_GPL(iomap_read_folio);

static loff_t iomap_readahead_iter(const struct iomap_iter *iter,
		struct iomap_readpage_ctx *ctx)
{
	loff_t length = iomap_length(iter);
	loff_t done, ret;

	for (done = 0; done < length; done += ret) {
		if (ctx->cur_folio &&
		    offset_in_folio(ctx->cur_folio, iter->pos + done) == 0) {
			if (!ctx->cur_folio_in_bio)
				folio_unlock(ctx->cur_folio);
			ctx->cur_folio = NULL;
		}
		if (!ctx->cur_folio) {
			ctx->cur_folio = readahead_folio(ctx->rac);
			ctx->cur_folio_in_bio = false;
		}
		ret = iomap_readpage_iter(iter, ctx, done);
		if (ret <= 0)
			return ret;
	}

	return done;
}

/**
 * iomap_readahead - Attempt to read pages from a file.
 * @rac: Describes the pages to be read.
 * @ops: The operations vector for the filesystem.
 *
 * This function is for filesystems to call to implement their readahead
 * address_space operation.
 *
 * Context: The @ops callbacks may submit I/O (eg to read the addresses of
 * blocks from disc), and may wait for it.  The caller may be trying to
 * access a different page, and so sleeping excessively should be avoided.
 * It may allocate memory, but should avoid costly allocations.  This
 * function is called with memalloc_nofs set, so allocations will not cause
 * the filesystem to be reentered.
 */
void iomap_readahead(struct readahead_control *rac, const struct iomap_ops *ops)
{
	struct iomap_iter iter = {
		.inode	= rac->mapping->host,
		.pos	= readahead_pos(rac),
		.len	= readahead_length(rac),
	};
	struct iomap_readpage_ctx ctx = {
		.rac	= rac,
	};

	trace_iomap_readahead(rac->mapping->host, readahead_count(rac));

	while (iomap_iter(&iter, ops) > 0)
		iter.processed = iomap_readahead_iter(&iter, &ctx);

	if (ctx.bio)
		submit_bio(ctx.bio);
	if (ctx.cur_folio) {
		if (!ctx.cur_folio_in_bio)
			folio_unlock(ctx.cur_folio);
	}
}
EXPORT_SYMBOL_GPL(iomap_readahead);

/*
 * iomap_is_partially_uptodate checks whether blocks within a folio are
 * uptodate or not.
 *
 * Returns true if all blocks which correspond to the specified part
 * of the folio are uptodate.
 */
bool iomap_is_partially_uptodate(struct folio *folio, size_t from, size_t count)
{
	struct iomap_folio_state *ifs = folio->private;
	struct inode *inode = folio->mapping->host;
	unsigned first, last, i;

	if (!ifs)
		return false;

	/* Caller's range may extend past the end of this folio */
	count = min(folio_size(folio) - from, count);

	/* First and last blocks in range within folio */
	first = from >> inode->i_blkbits;
	last = (from + count - 1) >> inode->i_blkbits;

	for (i = first; i <= last; i++)
		if (!ifs_block_is_uptodate(ifs, i))
			return false;
	return true;
}
EXPORT_SYMBOL_GPL(iomap_is_partially_uptodate);

/**
 * iomap_get_folio - get a folio reference for writing
 * @iter: iteration structure
 * @pos: start offset of write
 * @len: Suggested size of folio to create.
 *
 * Returns a locked reference to the folio at @pos, or an error pointer if the
 * folio could not be obtained.
 */
struct folio *iomap_get_folio(struct iomap_iter *iter, loff_t pos, size_t len)
{
	fgf_t fgp = FGP_WRITEBEGIN | FGP_NOFS;

	if (iter->flags & IOMAP_NOWAIT)
		fgp |= FGP_NOWAIT;
	fgp |= fgf_set_order(len);

	return __filemap_get_folio(iter->inode->i_mapping, pos >> PAGE_SHIFT,
			fgp, mapping_gfp_mask(iter->inode->i_mapping));
}
EXPORT_SYMBOL_GPL(iomap_get_folio);

bool iomap_release_folio(struct folio *folio, gfp_t gfp_flags)
{
	trace_iomap_release_folio(folio->mapping->host, folio_pos(folio),
			folio_size(folio));

	/*
	 * If the folio is dirty, we refuse to release our metadata because
	 * it may be partially dirty.  Once we track per-block dirty state,
	 * we can release the metadata if every block is dirty.
	 */
	if (folio_test_dirty(folio))
		return false;
	ifs_free(folio);
	return true;
}
EXPORT_SYMBOL_GPL(iomap_release_folio);

void iomap_invalidate_folio(struct folio *folio, size_t offset, size_t len)
{
	trace_iomap_invalidate_folio(folio->mapping->host,
					folio_pos(folio) + offset, len);

	/*
	 * If we're invalidating the entire folio, clear the dirty state
	 * from it and release it to avoid unnecessary buildup of the LRU.
	 */
	if (offset == 0 && len == folio_size(folio)) {
		WARN_ON_ONCE(folio_test_writeback(folio));
		folio_cancel_dirty(folio);
		ifs_free(folio);
	}
}
EXPORT_SYMBOL_GPL(iomap_invalidate_folio);

bool iomap_dirty_folio(struct address_space *mapping, struct folio *folio)
{
	struct inode *inode = mapping->host;
	size_t len = folio_size(folio);

	ifs_alloc(inode, folio, 0);
	iomap_set_range_dirty(folio, 0, len);
	return filemap_dirty_folio(mapping, folio);
}
EXPORT_SYMBOL_GPL(iomap_dirty_folio);

static void
iomap_write_failed(struct inode *inode, loff_t pos, unsigned len)
{
	loff_t i_size = i_size_read(inode);

	/*
	 * Only truncate newly allocated pages beyoned EOF, even if the
	 * write started inside the existing inode size.
	 */
	if (pos + len > i_size)
		truncate_pagecache_range(inode, max(pos, i_size),
					 pos + len - 1);
}

static int iomap_read_folio_sync(loff_t block_start, struct folio *folio,
		size_t poff, size_t plen, const struct iomap *iomap)
{
	struct bio_vec bvec;
	struct bio bio;

	bio_init(&bio, iomap->bdev, &bvec, 1, REQ_OP_READ);
	bio.bi_iter.bi_sector = iomap_sector(iomap, block_start);
	bio_add_folio_nofail(&bio, folio, plen, poff);
	return submit_bio_wait(&bio);
}

static int __iomap_write_begin(const struct iomap_iter *iter, loff_t pos,
		size_t len, struct folio *folio)
{
	const struct iomap *srcmap = iomap_iter_srcmap(iter);
	struct iomap_folio_state *ifs;
	loff_t block_size = i_blocksize(iter->inode);
	loff_t block_start = round_down(pos, block_size);
	loff_t block_end = round_up(pos + len, block_size);
	unsigned int nr_blocks = i_blocks_per_folio(iter->inode, folio);
	size_t from = offset_in_folio(folio, pos), to = from + len;
	size_t poff, plen;

	/*
	 * If the write completely overlaps the current folio, then
	 * entire folio will be dirtied so there is no need for
	 * per-block state tracking structures to be attached to this folio.
	 */
	if (pos <= folio_pos(folio) &&
	    pos + len >= folio_pos(folio) + folio_size(folio))
		return 0;

	ifs = ifs_alloc(iter->inode, folio, iter->flags);
	if ((iter->flags & IOMAP_NOWAIT) && !ifs && nr_blocks > 1)
		return -EAGAIN;

	if (folio_test_uptodate(folio))
		return 0;
	folio_clear_error(folio);

	do {
		iomap_adjust_read_range(iter->inode, folio, &block_start,
				block_end - block_start, &poff, &plen);
		if (plen == 0)
			break;

		if (!(iter->flags & IOMAP_UNSHARE) &&
		    (from <= poff || from >= poff + plen) &&
		    (to <= poff || to >= poff + plen))
			continue;

		if (iomap_block_needs_zeroing(iter, block_start)) {
			if (WARN_ON_ONCE(iter->flags & IOMAP_UNSHARE))
				return -EIO;
			folio_zero_segments(folio, poff, from, to, poff + plen);
		} else {
			int status;

			if (iter->flags & IOMAP_NOWAIT)
				return -EAGAIN;

			status = iomap_read_folio_sync(block_start, folio,
					poff, plen, srcmap);
			if (status)
				return status;
		}
		iomap_set_range_uptodate(folio, poff, plen);
	} while ((block_start += plen) < block_end);

	return 0;
}

static struct folio *__iomap_get_folio(struct iomap_iter *iter, loff_t pos,
		size_t len)
{
	const struct iomap_folio_ops *folio_ops = iter->iomap.folio_ops;

	if (folio_ops && folio_ops->get_folio)
		return folio_ops->get_folio(iter, pos, len);
	else
		return iomap_get_folio(iter, pos, len);
}

static void __iomap_put_folio(struct iomap_iter *iter, loff_t pos, size_t ret,
		struct folio *folio)
{
	const struct iomap_folio_ops *folio_ops = iter->iomap.folio_ops;

	if (folio_ops && folio_ops->put_folio) {
		folio_ops->put_folio(iter->inode, pos, ret, folio);
	} else {
		folio_unlock(folio);
		folio_put(folio);
	}
}

static int iomap_write_begin_inline(const struct iomap_iter *iter,
		struct folio *folio)
{
	/* needs more work for the tailpacking case; disable for now */
	if (WARN_ON_ONCE(iomap_iter_srcmap(iter)->offset != 0))
		return -EIO;
	return iomap_read_inline_data(iter, folio);
}

static int iomap_write_begin(struct iomap_iter *iter, loff_t pos,
		size_t len, struct folio **foliop)
{
	const struct iomap_folio_ops *folio_ops = iter->iomap.folio_ops;
	const struct iomap *srcmap = iomap_iter_srcmap(iter);
	struct folio *folio;
	int status = 0;

	BUG_ON(pos + len > iter->iomap.offset + iter->iomap.length);
	if (srcmap != &iter->iomap)
		BUG_ON(pos + len > srcmap->offset + srcmap->length);

	if (fatal_signal_pending(current))
		return -EINTR;

	if (!mapping_large_folio_support(iter->inode->i_mapping))
		len = min_t(size_t, len, PAGE_SIZE - offset_in_page(pos));

	folio = __iomap_get_folio(iter, pos, len);
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	/*
	 * Now we have a locked folio, before we do anything with it we need to
	 * check that the iomap we have cached is not stale. The inode extent
	 * mapping can change due to concurrent IO in flight (e.g.
	 * IOMAP_UNWRITTEN state can change and memory reclaim could have
	 * reclaimed a previously partially written page at this index after IO
	 * completion before this write reaches this file offset) and hence we
	 * could do the wrong thing here (zero a page range incorrectly or fail
	 * to zero) and corrupt data.
	 */
	if (folio_ops && folio_ops->iomap_valid) {
		bool iomap_valid = folio_ops->iomap_valid(iter->inode,
							 &iter->iomap);
		if (!iomap_valid) {
			iter->iomap.flags |= IOMAP_F_STALE;
			status = 0;
			goto out_unlock;
		}
	}

	if (pos + len > folio_pos(folio) + folio_size(folio))
		len = folio_pos(folio) + folio_size(folio) - pos;

	if (srcmap->type == IOMAP_INLINE)
		status = iomap_write_begin_inline(iter, folio);
	else if (srcmap->flags & IOMAP_F_BUFFER_HEAD)
		status = __block_write_begin_int(folio, pos, len, NULL, srcmap);
	else
		status = __iomap_write_begin(iter, pos, len, folio);

	if (unlikely(status))
		goto out_unlock;

	*foliop = folio;
	return 0;

out_unlock:
	__iomap_put_folio(iter, pos, 0, folio);
	iomap_write_failed(iter->inode, pos, len);

	return status;
}

static size_t __iomap_write_end(struct inode *inode, loff_t pos, size_t len,
		size_t copied, struct folio *folio)
{
	flush_dcache_folio(folio);

	/*
	 * The blocks that were entirely written will now be uptodate, so we
	 * don't have to worry about a read_folio reading them and overwriting a
	 * partial write.  However, if we've encountered a short write and only
	 * partially written into a block, it will not be marked uptodate, so a
	 * read_folio might come in and destroy our partial write.
	 *
	 * Do the simplest thing and just treat any short write to a
	 * non-uptodate page as a zero-length write, and force the caller to
	 * redo the whole thing.
	 */
	if (unlikely(copied < len && !folio_test_uptodate(folio)))
		return 0;
	iomap_set_range_uptodate(folio, offset_in_folio(folio, pos), len);
	iomap_set_range_dirty(folio, offset_in_folio(folio, pos), copied);
	filemap_dirty_folio(inode->i_mapping, folio);
	return copied;
}

static size_t iomap_write_end_inline(const struct iomap_iter *iter,
		struct folio *folio, loff_t pos, size_t copied)
{
	const struct iomap *iomap = &iter->iomap;
	void *addr;

	WARN_ON_ONCE(!folio_test_uptodate(folio));
	BUG_ON(!iomap_inline_data_valid(iomap));

	flush_dcache_folio(folio);
	addr = kmap_local_folio(folio, pos);
	memcpy(iomap_inline_data(iomap, pos), addr, copied);
	kunmap_local(addr);

	mark_inode_dirty(iter->inode);
	return copied;
}

/* Returns the number of bytes copied.  May be 0.  Cannot be an errno. */
static size_t iomap_write_end(struct iomap_iter *iter, loff_t pos, size_t len,
		size_t copied, struct folio *folio)
{
	const struct iomap *srcmap = iomap_iter_srcmap(iter);
	loff_t old_size = iter->inode->i_size;
	size_t ret;

	if (srcmap->type == IOMAP_INLINE) {
		ret = iomap_write_end_inline(iter, folio, pos, copied);
	} else if (srcmap->flags & IOMAP_F_BUFFER_HEAD) {
		ret = block_write_end(NULL, iter->inode->i_mapping, pos, len,
				copied, &folio->page, NULL);
	} else {
		ret = __iomap_write_end(iter->inode, pos, len, copied, folio);
	}

	/*
	 * Update the in-memory inode size after copying the data into the page
	 * cache.  It's up to the file system to write the updated size to disk,
	 * preferably after I/O completion so that no stale data is exposed.
	 */
	if (pos + ret > old_size) {
		i_size_write(iter->inode, pos + ret);
		iter->iomap.flags |= IOMAP_F_SIZE_CHANGED;
	}
	__iomap_put_folio(iter, pos, ret, folio);

	if (old_size < pos)
		pagecache_isize_extended(iter->inode, old_size, pos);
	if (ret < len)
		iomap_write_failed(iter->inode, pos + ret, len - ret);
	return ret;
}

static loff_t iomap_write_iter(struct iomap_iter *iter, struct iov_iter *i)
{
	loff_t length = iomap_length(iter);
	size_t chunk = PAGE_SIZE << MAX_PAGECACHE_ORDER;
	loff_t pos = iter->pos;
	ssize_t written = 0;
	long status = 0;
	struct address_space *mapping = iter->inode->i_mapping;
	unsigned int bdp_flags = (iter->flags & IOMAP_NOWAIT) ? BDP_ASYNC : 0;

	do {
		struct folio *folio;
		size_t offset;		/* Offset into folio */
		size_t bytes;		/* Bytes to write to folio */
		size_t copied;		/* Bytes copied from user */

		offset = pos & (chunk - 1);
		bytes = min(chunk - offset, iov_iter_count(i));
		status = balance_dirty_pages_ratelimited_flags(mapping,
							       bdp_flags);
		if (unlikely(status))
			break;

		if (bytes > length)
			bytes = length;

		/*
		 * Bring in the user page that we'll copy from _first_.
		 * Otherwise there's a nasty deadlock on copying from the
		 * same page as we're writing to, without it being marked
		 * up-to-date.
		 *
		 * For async buffered writes the assumption is that the user
		 * page has already been faulted in. This can be optimized by
		 * faulting the user page.
		 */
		if (unlikely(fault_in_iov_iter_readable(i, bytes) == bytes)) {
			status = -EFAULT;
			break;
		}

		status = iomap_write_begin(iter, pos, bytes, &folio);
		if (unlikely(status))
			break;
		if (iter->iomap.flags & IOMAP_F_STALE)
			break;

		offset = offset_in_folio(folio, pos);
		if (bytes > folio_size(folio) - offset)
			bytes = folio_size(folio) - offset;

		if (mapping_writably_mapped(mapping))
			flush_dcache_folio(folio);

		copied = copy_folio_from_iter_atomic(folio, offset, bytes, i);
		status = iomap_write_end(iter, pos, bytes, copied, folio);

		if (unlikely(copied != status))
			iov_iter_revert(i, copied - status);

		cond_resched();
		if (unlikely(status == 0)) {
			/*
			 * A short copy made iomap_write_end() reject the
			 * thing entirely.  Might be memory poisoning
			 * halfway through, might be a race with munmap,
			 * might be severe memory pressure.
			 */
			if (copied)
				bytes = copied;
			if (chunk > PAGE_SIZE)
				chunk /= 2;
		} else {
			pos += status;
			written += status;
			length -= status;
		}
	} while (iov_iter_count(i) && length);

	if (status == -EAGAIN) {
		iov_iter_revert(i, written);
		return -EAGAIN;
	}
	return written ? written : status;
}

ssize_t
iomap_file_buffered_write(struct kiocb *iocb, struct iov_iter *i,
		const struct iomap_ops *ops)
{
	struct iomap_iter iter = {
		.inode		= iocb->ki_filp->f_mapping->host,
		.pos		= iocb->ki_pos,
		.len		= iov_iter_count(i),
		.flags		= IOMAP_WRITE,
	};
	ssize_t ret;

	if (iocb->ki_flags & IOCB_NOWAIT)
		iter.flags |= IOMAP_NOWAIT;

	while ((ret = iomap_iter(&iter, ops)) > 0)
		iter.processed = iomap_write_iter(&iter, i);

	if (unlikely(iter.pos == iocb->ki_pos))
		return ret;
	ret = iter.pos - iocb->ki_pos;
	iocb->ki_pos = iter.pos;
	return ret;
}
EXPORT_SYMBOL_GPL(iomap_file_buffered_write);

static int iomap_write_delalloc_ifs_punch(struct inode *inode,
		struct folio *folio, loff_t start_byte, loff_t end_byte,
		iomap_punch_t punch)
{
	unsigned int first_blk, last_blk, i;
	loff_t last_byte;
	u8 blkbits = inode->i_blkbits;
	struct iomap_folio_state *ifs;
	int ret = 0;

	/*
	 * When we have per-block dirty tracking, there can be
	 * blocks within a folio which are marked uptodate
	 * but not dirty. In that case it is necessary to punch
	 * out such blocks to avoid leaking any delalloc blocks.
	 */
	ifs = folio->private;
	if (!ifs)
		return ret;

	last_byte = min_t(loff_t, end_byte - 1,
			folio_pos(folio) + folio_size(folio) - 1);
	first_blk = offset_in_folio(folio, start_byte) >> blkbits;
	last_blk = offset_in_folio(folio, last_byte) >> blkbits;
	for (i = first_blk; i <= last_blk; i++) {
		if (!ifs_block_is_dirty(folio, ifs, i)) {
			ret = punch(inode, folio_pos(folio) + (i << blkbits),
				    1 << blkbits);
			if (ret)
				return ret;
		}
	}

	return ret;
}


static int iomap_write_delalloc_punch(struct inode *inode, struct folio *folio,
		loff_t *punch_start_byte, loff_t start_byte, loff_t end_byte,
		iomap_punch_t punch)
{
	int ret = 0;

	if (!folio_test_dirty(folio))
		return ret;

	/* if dirty, punch up to offset */
	if (start_byte > *punch_start_byte) {
		ret = punch(inode, *punch_start_byte,
				start_byte - *punch_start_byte);
		if (ret)
			return ret;
	}

	/* Punch non-dirty blocks within folio */
	ret = iomap_write_delalloc_ifs_punch(inode, folio, start_byte,
			end_byte, punch);
	if (ret)
		return ret;

	/*
	 * Make sure the next punch start is correctly bound to
	 * the end of this data range, not the end of the folio.
	 */
	*punch_start_byte = min_t(loff_t, end_byte,
				folio_pos(folio) + folio_size(folio));

	return ret;
}

/*
 * Scan the data range passed to us for dirty page cache folios. If we find a
 * dirty folio, punch out the preceeding range and update the offset from which
 * the next punch will start from.
 *
 * We can punch out storage reservations under clean pages because they either
 * contain data that has been written back - in which case the delalloc punch
 * over that range is a no-op - or they have been read faults in which case they
 * contain zeroes and we can remove the delalloc backing range and any new
 * writes to those pages will do the normal hole filling operation...
 *
 * This makes the logic simple: we only need to keep the delalloc extents only
 * over the dirty ranges of the page cache.
 *
 * This function uses [start_byte, end_byte) intervals (i.e. open ended) to
 * simplify range iterations.
 */
static int iomap_write_delalloc_scan(struct inode *inode,
		loff_t *punch_start_byte, loff_t start_byte, loff_t end_byte,
		iomap_punch_t punch)
{
	while (start_byte < end_byte) {
		struct folio	*folio;
		int ret;

		/* grab locked page */
		folio = filemap_lock_folio(inode->i_mapping,
				start_byte >> PAGE_SHIFT);
		if (IS_ERR(folio)) {
			start_byte = ALIGN_DOWN(start_byte, PAGE_SIZE) +
					PAGE_SIZE;
			continue;
		}

		ret = iomap_write_delalloc_punch(inode, folio, punch_start_byte,
						 start_byte, end_byte, punch);
		if (ret) {
			folio_unlock(folio);
			folio_put(folio);
			return ret;
		}

		/* move offset to start of next folio in range */
		start_byte = folio_next_index(folio) << PAGE_SHIFT;
		folio_unlock(folio);
		folio_put(folio);
	}
	return 0;
}

/*
 * Punch out all the delalloc blocks in the range given except for those that
 * have dirty data still pending in the page cache - those are going to be
 * written and so must still retain the delalloc backing for writeback.
 *
 * As we are scanning the page cache for data, we don't need to reimplement the
 * wheel - mapping_seek_hole_data() does exactly what we need to identify the
 * start and end of data ranges correctly even for sub-folio block sizes. This
 * byte range based iteration is especially convenient because it means we
 * don't have to care about variable size folios, nor where the start or end of
 * the data range lies within a folio, if they lie within the same folio or even
 * if there are multiple discontiguous data ranges within the folio.
 *
 * It should be noted that mapping_seek_hole_data() is not aware of EOF, and so
 * can return data ranges that exist in the cache beyond EOF. e.g. a page fault
 * spanning EOF will initialise the post-EOF data to zeroes and mark it up to
 * date. A write page fault can then mark it dirty. If we then fail a write()
 * beyond EOF into that up to date cached range, we allocate a delalloc block
 * beyond EOF and then have to punch it out. Because the range is up to date,
 * mapping_seek_hole_data() will return it, and we will skip the punch because
 * the folio is dirty. THis is incorrect - we always need to punch out delalloc
 * beyond EOF in this case as writeback will never write back and covert that
 * delalloc block beyond EOF. Hence we limit the cached data scan range to EOF,
 * resulting in always punching out the range from the EOF to the end of the
 * range the iomap spans.
 *
 * Intervals are of the form [start_byte, end_byte) (i.e. open ended) because it
 * matches the intervals returned by mapping_seek_hole_data(). i.e. SEEK_DATA
 * returns the start of a data range (start_byte), and SEEK_HOLE(start_byte)
 * returns the end of the data range (data_end). Using closed intervals would
 * require sprinkling this code with magic "+ 1" and "- 1" arithmetic and expose
 * the code to subtle off-by-one bugs....
 */
static int iomap_write_delalloc_release(struct inode *inode,
		loff_t start_byte, loff_t end_byte, iomap_punch_t punch)
{
	loff_t punch_start_byte = start_byte;
	loff_t scan_end_byte = min(i_size_read(inode), end_byte);
	int error = 0;

	/*
	 * Lock the mapping to avoid races with page faults re-instantiating
	 * folios and dirtying them via ->page_mkwrite whilst we walk the
	 * cache and perform delalloc extent removal. Failing to do this can
	 * leave dirty pages with no space reservation in the cache.
	 */
	filemap_invalidate_lock(inode->i_mapping);
	while (start_byte < scan_end_byte) {
		loff_t		data_end;

		start_byte = mapping_seek_hole_data(inode->i_mapping,
				start_byte, scan_end_byte, SEEK_DATA);
		/*
		 * If there is no more data to scan, all that is left is to
		 * punch out the remaining range.
		 */
		if (start_byte == -ENXIO || start_byte == scan_end_byte)
			break;
		if (start_byte < 0) {
			error = start_byte;
			goto out_unlock;
		}
		WARN_ON_ONCE(start_byte < punch_start_byte);
		WARN_ON_ONCE(start_byte > scan_end_byte);

		/*
		 * We find the end of this contiguous cached data range by
		 * seeking from start_byte to the beginning of the next hole.
		 */
		data_end = mapping_seek_hole_data(inode->i_mapping, start_byte,
				scan_end_byte, SEEK_HOLE);
		if (data_end < 0) {
			error = data_end;
			goto out_unlock;
		}
		WARN_ON_ONCE(data_end <= start_byte);
		WARN_ON_ONCE(data_end > scan_end_byte);

		error = iomap_write_delalloc_scan(inode, &punch_start_byte,
				start_byte, data_end, punch);
		if (error)
			goto out_unlock;

		/* The next data search starts at the end of this one. */
		start_byte = data_end;
	}

	if (punch_start_byte < end_byte)
		error = punch(inode, punch_start_byte,
				end_byte - punch_start_byte);
out_unlock:
	filemap_invalidate_unlock(inode->i_mapping);
	return error;
}

/*
 * When a short write occurs, the filesystem may need to remove reserved space
 * that was allocated in ->iomap_begin from it's ->iomap_end method. For
 * filesystems that use delayed allocation, we need to punch out delalloc
 * extents from the range that are not dirty in the page cache. As the write can
 * race with page faults, there can be dirty pages over the delalloc extent
 * outside the range of a short write but still within the delalloc extent
 * allocated for this iomap.
 *
 * This function uses [start_byte, end_byte) intervals (i.e. open ended) to
 * simplify range iterations.
 *
 * The punch() callback *must* only punch delalloc extents in the range passed
 * to it. It must skip over all other types of extents in the range and leave
 * them completely unchanged. It must do this punch atomically with respect to
 * other extent modifications.
 *
 * The punch() callback may be called with a folio locked to prevent writeback
 * extent allocation racing at the edge of the range we are currently punching.
 * The locked folio may or may not cover the range being punched, so it is not
 * safe for the punch() callback to lock folios itself.
 *
 * Lock order is:
 *
 * inode->i_rwsem (shared or exclusive)
 *   inode->i_mapping->invalidate_lock (exclusive)
 *     folio_lock()
 *       ->punch
 *         internal filesystem allocation lock
 */
int iomap_file_buffered_write_punch_delalloc(struct inode *inode,
		struct iomap *iomap, loff_t pos, loff_t length,
		ssize_t written, iomap_punch_t punch)
{
	loff_t			start_byte;
	loff_t			end_byte;
	unsigned int		blocksize = i_blocksize(inode);

	if (iomap->type != IOMAP_DELALLOC)
		return 0;

	/* If we didn't reserve the blocks, we're not allowed to punch them. */
	if (!(iomap->flags & IOMAP_F_NEW))
		return 0;

	/*
	 * start_byte refers to the first unused block after a short write. If
	 * nothing was written, round offset down to point at the first block in
	 * the range.
	 */
	if (unlikely(!written))
		start_byte = round_down(pos, blocksize);
	else
		start_byte = round_up(pos + written, blocksize);
	end_byte = round_up(pos + length, blocksize);

	/* Nothing to do if we've written the entire delalloc extent */
	if (start_byte >= end_byte)
		return 0;

	return iomap_write_delalloc_release(inode, start_byte, end_byte,
					punch);
}
EXPORT_SYMBOL_GPL(iomap_file_buffered_write_punch_delalloc);

static loff_t iomap_unshare_iter(struct iomap_iter *iter)
{
	struct iomap *iomap = &iter->iomap;
	const struct iomap *srcmap = iomap_iter_srcmap(iter);
	loff_t pos = iter->pos;
	loff_t length = iomap_length(iter);
	long status = 0;
	loff_t written = 0;

	/* don't bother with blocks that are not shared to start with */
	if (!(iomap->flags & IOMAP_F_SHARED))
		return length;
	/* don't bother with holes or unwritten extents */
	if (srcmap->type == IOMAP_HOLE || srcmap->type == IOMAP_UNWRITTEN)
		return length;

	do {
		unsigned long offset = offset_in_page(pos);
		unsigned long bytes = min_t(loff_t, PAGE_SIZE - offset, length);
		struct folio *folio;

		status = iomap_write_begin(iter, pos, bytes, &folio);
		if (unlikely(status))
			return status;
		if (iter->iomap.flags & IOMAP_F_STALE)
			break;

		status = iomap_write_end(iter, pos, bytes, bytes, folio);
		if (WARN_ON_ONCE(status == 0))
			return -EIO;

		cond_resched();

		pos += status;
		written += status;
		length -= status;

		balance_dirty_pages_ratelimited(iter->inode->i_mapping);
	} while (length);

	return written;
}

int
iomap_file_unshare(struct inode *inode, loff_t pos, loff_t len,
		const struct iomap_ops *ops)
{
	struct iomap_iter iter = {
		.inode		= inode,
		.pos		= pos,
		.len		= len,
		.flags		= IOMAP_WRITE | IOMAP_UNSHARE,
	};
	int ret;

	while ((ret = iomap_iter(&iter, ops)) > 0)
		iter.processed = iomap_unshare_iter(&iter);
	return ret;
}
EXPORT_SYMBOL_GPL(iomap_file_unshare);

static loff_t iomap_zero_iter(struct iomap_iter *iter, bool *did_zero)
{
	const struct iomap *srcmap = iomap_iter_srcmap(iter);
	loff_t pos = iter->pos;
	loff_t length = iomap_length(iter);
	loff_t written = 0;

	/* already zeroed?  we're done. */
	if (srcmap->type == IOMAP_HOLE || srcmap->type == IOMAP_UNWRITTEN)
		return length;

	do {
		struct folio *folio;
		int status;
		size_t offset;
		size_t bytes = min_t(u64, SIZE_MAX, length);

		status = iomap_write_begin(iter, pos, bytes, &folio);
		if (status)
			return status;
		if (iter->iomap.flags & IOMAP_F_STALE)
			break;

		offset = offset_in_folio(folio, pos);
		if (bytes > folio_size(folio) - offset)
			bytes = folio_size(folio) - offset;

		folio_zero_range(folio, offset, bytes);
		folio_mark_accessed(folio);

		bytes = iomap_write_end(iter, pos, bytes, bytes, folio);
		if (WARN_ON_ONCE(bytes == 0))
			return -EIO;

		pos += bytes;
		length -= bytes;
		written += bytes;
	} while (length > 0);

	if (did_zero)
		*did_zero = true;
	return written;
}

int
iomap_zero_range(struct inode *inode, loff_t pos, loff_t len, bool *did_zero,
		const struct iomap_ops *ops)
{
	struct iomap_iter iter = {
		.inode		= inode,
		.pos		= pos,
		.len		= len,
		.flags		= IOMAP_ZERO,
	};
	int ret;

	while ((ret = iomap_iter(&iter, ops)) > 0)
		iter.processed = iomap_zero_iter(&iter, did_zero);
	return ret;
}
EXPORT_SYMBOL_GPL(iomap_zero_range);

int
iomap_truncate_page(struct inode *inode, loff_t pos, bool *did_zero,
		const struct iomap_ops *ops)
{
	unsigned int blocksize = i_blocksize(inode);
	unsigned int off = pos & (blocksize - 1);

	/* Block boundary? Nothing to do */
	if (!off)
		return 0;
	return iomap_zero_range(inode, pos, blocksize - off, did_zero, ops);
}
EXPORT_SYMBOL_GPL(iomap_truncate_page);

static loff_t iomap_folio_mkwrite_iter(struct iomap_iter *iter,
		struct folio *folio)
{
	loff_t length = iomap_length(iter);
	int ret;

	if (iter->iomap.flags & IOMAP_F_BUFFER_HEAD) {
		ret = __block_write_begin_int(folio, iter->pos, length, NULL,
					      &iter->iomap);
		if (ret)
			return ret;
		block_commit_write(&folio->page, 0, length);
	} else {
		WARN_ON_ONCE(!folio_test_uptodate(folio));
		folio_mark_dirty(folio);
	}

	return length;
}

vm_fault_t iomap_page_mkwrite(struct vm_fault *vmf, const struct iomap_ops *ops)
{
	struct iomap_iter iter = {
		.inode		= file_inode(vmf->vma->vm_file),
		.flags		= IOMAP_WRITE | IOMAP_FAULT,
	};
	struct folio *folio = page_folio(vmf->page);
	ssize_t ret;

	folio_lock(folio);
	ret = folio_mkwrite_check_truncate(folio, iter.inode);
	if (ret < 0)
		goto out_unlock;
	iter.pos = folio_pos(folio);
	iter.len = ret;
	while ((ret = iomap_iter(&iter, ops)) > 0)
		iter.processed = iomap_folio_mkwrite_iter(&iter, folio);

	if (ret < 0)
		goto out_unlock;
	folio_wait_stable(folio);
	return VM_FAULT_LOCKED;
out_unlock:
	folio_unlock(folio);
	return vmf_fs_error(ret);
}
EXPORT_SYMBOL_GPL(iomap_page_mkwrite);

static void iomap_finish_folio_write(struct inode *inode, struct folio *folio,
		size_t len, int error)
{
	struct iomap_folio_state *ifs = folio->private;

	if (error) {
		folio_set_error(folio);
		mapping_set_error(inode->i_mapping, error);
	}

	WARN_ON_ONCE(i_blocks_per_folio(inode, folio) > 1 && !ifs);
	WARN_ON_ONCE(ifs && atomic_read(&ifs->write_bytes_pending) <= 0);

	if (!ifs || atomic_sub_and_test(len, &ifs->write_bytes_pending))
		folio_end_writeback(folio);
}

/*
 * We're now finished for good with this ioend structure.  Update the page
 * state, release holds on bios, and finally free up memory.  Do not use the
 * ioend after this.
 */
static u32
iomap_finish_ioend(struct iomap_ioend *ioend, int error)
{
	struct inode *inode = ioend->io_inode;
	struct bio *bio = &ioend->io_inline_bio;
	struct bio *last = ioend->io_bio, *next;
	u64 start = bio->bi_iter.bi_sector;
	loff_t offset = ioend->io_offset;
	bool quiet = bio_flagged(bio, BIO_QUIET);
	u32 folio_count = 0;

	for (bio = &ioend->io_inline_bio; bio; bio = next) {
		struct folio_iter fi;

		/*
		 * For the last bio, bi_private points to the ioend, so we
		 * need to explicitly end the iteration here.
		 */
		if (bio == last)
			next = NULL;
		else
			next = bio->bi_private;

		/* walk all folios in bio, ending page IO on them */
		bio_for_each_folio_all(fi, bio) {
			iomap_finish_folio_write(inode, fi.folio, fi.length,
					error);
			folio_count++;
		}
		bio_put(bio);
	}
	/* The ioend has been freed by bio_put() */

	if (unlikely(error && !quiet)) {
		printk_ratelimited(KERN_ERR
"%s: writeback error on inode %lu, offset %lld, sector %llu",
			inode->i_sb->s_id, inode->i_ino, offset, start);
	}
	return folio_count;
}

/*
 * Ioend completion routine for merged bios. This can only be called from task
 * contexts as merged ioends can be of unbound length. Hence we have to break up
 * the writeback completions into manageable chunks to avoid long scheduler
 * holdoffs. We aim to keep scheduler holdoffs down below 10ms so that we get
 * good batch processing throughput without creating adverse scheduler latency
 * conditions.
 */
void
iomap_finish_ioends(struct iomap_ioend *ioend, int error)
{
	struct list_head tmp;
	u32 completions;

	might_sleep();

	list_replace_init(&ioend->io_list, &tmp);
	completions = iomap_finish_ioend(ioend, error);

	while (!list_empty(&tmp)) {
		if (completions > IOEND_BATCH_SIZE * 8) {
			cond_resched();
			completions = 0;
		}
		ioend = list_first_entry(&tmp, struct iomap_ioend, io_list);
		list_del_init(&ioend->io_list);
		completions += iomap_finish_ioend(ioend, error);
	}
}
EXPORT_SYMBOL_GPL(iomap_finish_ioends);

/*
 * We can merge two adjacent ioends if they have the same set of work to do.
 */
static bool
iomap_ioend_can_merge(struct iomap_ioend *ioend, struct iomap_ioend *next)
{
	if (ioend->io_bio->bi_status != next->io_bio->bi_status)
		return false;
	if ((ioend->io_flags & IOMAP_F_SHARED) ^
	    (next->io_flags & IOMAP_F_SHARED))
		return false;
	if ((ioend->io_type == IOMAP_UNWRITTEN) ^
	    (next->io_type == IOMAP_UNWRITTEN))
		return false;
	if (ioend->io_offset + ioend->io_size != next->io_offset)
		return false;
	/*
	 * Do not merge physically discontiguous ioends. The filesystem
	 * completion functions will have to iterate the physical
	 * discontiguities even if we merge the ioends at a logical level, so
	 * we don't gain anything by merging physical discontiguities here.
	 *
	 * We cannot use bio->bi_iter.bi_sector here as it is modified during
	 * submission so does not point to the start sector of the bio at
	 * completion.
	 */
	if (ioend->io_sector + (ioend->io_size >> 9) != next->io_sector)
		return false;
	return true;
}

void
iomap_ioend_try_merge(struct iomap_ioend *ioend, struct list_head *more_ioends)
{
	struct iomap_ioend *next;

	INIT_LIST_HEAD(&ioend->io_list);

	while ((next = list_first_entry_or_null(more_ioends, struct iomap_ioend,
			io_list))) {
		if (!iomap_ioend_can_merge(ioend, next))
			break;
		list_move_tail(&next->io_list, &ioend->io_list);
		ioend->io_size += next->io_size;
	}
}
EXPORT_SYMBOL_GPL(iomap_ioend_try_merge);

static int
iomap_ioend_compare(void *priv, const struct list_head *a,
		const struct list_head *b)
{
	struct iomap_ioend *ia = container_of(a, struct iomap_ioend, io_list);
	struct iomap_ioend *ib = container_of(b, struct iomap_ioend, io_list);

	if (ia->io_offset < ib->io_offset)
		return -1;
	if (ia->io_offset > ib->io_offset)
		return 1;
	return 0;
}

void
iomap_sort_ioends(struct list_head *ioend_list)
{
	list_sort(NULL, ioend_list, iomap_ioend_compare);
}
EXPORT_SYMBOL_GPL(iomap_sort_ioends);

static void iomap_writepage_end_bio(struct bio *bio)
{
	struct iomap_ioend *ioend = bio->bi_private;

	iomap_finish_ioend(ioend, blk_status_to_errno(bio->bi_status));
}

/*
 * Submit the final bio for an ioend.
 *
 * If @error is non-zero, it means that we have a situation where some part of
 * the submission process has failed after we've marked pages for writeback
 * and unlocked them.  In this situation, we need to fail the bio instead of
 * submitting it.  This typically only happens on a filesystem shutdown.
 */
static int
iomap_submit_ioend(struct iomap_writepage_ctx *wpc, struct iomap_ioend *ioend,
		int error)
{
	ioend->io_bio->bi_private = ioend;
	ioend->io_bio->bi_end_io = iomap_writepage_end_bio;

	if (wpc->ops->prepare_ioend)
		error = wpc->ops->prepare_ioend(ioend, error);
	if (error) {
		/*
		 * If we're failing the IO now, just mark the ioend with an
		 * error and finish it.  This will run IO completion immediately
		 * as there is only one reference to the ioend at this point in
		 * time.
		 */
		ioend->io_bio->bi_status = errno_to_blk_status(error);
		bio_endio(ioend->io_bio);
		return error;
	}

	submit_bio(ioend->io_bio);
	return 0;
}

static struct iomap_ioend *
iomap_alloc_ioend(struct inode *inode, struct iomap_writepage_ctx *wpc,
		loff_t offset, sector_t sector, struct writeback_control *wbc)
{
	struct iomap_ioend *ioend;
	struct bio *bio;

	bio = bio_alloc_bioset(wpc->iomap.bdev, BIO_MAX_VECS,
			       REQ_OP_WRITE | wbc_to_write_flags(wbc),
			       GFP_NOFS, &iomap_ioend_bioset);
	bio->bi_iter.bi_sector = sector;
	wbc_init_bio(wbc, bio);

	ioend = container_of(bio, struct iomap_ioend, io_inline_bio);
	INIT_LIST_HEAD(&ioend->io_list);
	ioend->io_type = wpc->iomap.type;
	ioend->io_flags = wpc->iomap.flags;
	ioend->io_inode = inode;
	ioend->io_size = 0;
	ioend->io_folios = 0;
	ioend->io_offset = offset;
	ioend->io_bio = bio;
	ioend->io_sector = sector;
	return ioend;
}

/*
 * Allocate a new bio, and chain the old bio to the new one.
 *
 * Note that we have to perform the chaining in this unintuitive order
 * so that the bi_private linkage is set up in the right direction for the
 * traversal in iomap_finish_ioend().
 */
static struct bio *
iomap_chain_bio(struct bio *prev)
{
	struct bio *new;

	new = bio_alloc(prev->bi_bdev, BIO_MAX_VECS, prev->bi_opf, GFP_NOFS);
	bio_clone_blkg_association(new, prev);
	new->bi_iter.bi_sector = bio_end_sector(prev);

	bio_chain(prev, new);
	bio_get(prev);		/* for iomap_finish_ioend */
	submit_bio(prev);
	return new;
}

static bool
iomap_can_add_to_ioend(struct iomap_writepage_ctx *wpc, loff_t offset,
		sector_t sector)
{
	if ((wpc->iomap.flags & IOMAP_F_SHARED) !=
	    (wpc->ioend->io_flags & IOMAP_F_SHARED))
		return false;
	if (wpc->iomap.type != wpc->ioend->io_type)
		return false;
	if (offset != wpc->ioend->io_offset + wpc->ioend->io_size)
		return false;
	if (sector != bio_end_sector(wpc->ioend->io_bio))
		return false;
	/*
	 * Limit ioend bio chain lengths to minimise IO completion latency. This
	 * also prevents long tight loops ending page writeback on all the
	 * folios in the ioend.
	 */
	if (wpc->ioend->io_folios >= IOEND_BATCH_SIZE)
		return false;
	return true;
}

/*
 * Test to see if we have an existing ioend structure that we could append to
 * first; otherwise finish off the current ioend and start another.
 */
static void
iomap_add_to_ioend(struct inode *inode, loff_t pos, struct folio *folio,
		struct iomap_folio_state *ifs, struct iomap_writepage_ctx *wpc,
		struct writeback_control *wbc, struct list_head *iolist)
{
	sector_t sector = iomap_sector(&wpc->iomap, pos);
	unsigned len = i_blocksize(inode);
	size_t poff = offset_in_folio(folio, pos);

	if (!wpc->ioend || !iomap_can_add_to_ioend(wpc, pos, sector)) {
		if (wpc->ioend)
			list_add(&wpc->ioend->io_list, iolist);
		wpc->ioend = iomap_alloc_ioend(inode, wpc, pos, sector, wbc);
	}

	if (!bio_add_folio(wpc->ioend->io_bio, folio, len, poff)) {
		wpc->ioend->io_bio = iomap_chain_bio(wpc->ioend->io_bio);
		bio_add_folio_nofail(wpc->ioend->io_bio, folio, len, poff);
	}

	if (ifs)
		atomic_add(len, &ifs->write_bytes_pending);
	wpc->ioend->io_size += len;
	wbc_account_cgroup_owner(wbc, &folio->page, len);
}

/*
 * We implement an immediate ioend submission policy here to avoid needing to
 * chain multiple ioends and hence nest mempool allocations which can violate
 * the forward progress guarantees we need to provide. The current ioend we're
 * adding blocks to is cached in the writepage context, and if the new block
 * doesn't append to the cached ioend, it will create a new ioend and cache that
 * instead.
 *
 * If a new ioend is created and cached, the old ioend is returned and queued
 * locally for submission once the entire page is processed or an error has been
 * detected.  While ioends are submitted immediately after they are completed,
 * batching optimisations are provided by higher level block plugging.
 *
 * At the end of a writeback pass, there will be a cached ioend remaining on the
 * writepage context that the caller will need to submit.
 */
static int
iomap_writepage_map(struct iomap_writepage_ctx *wpc,
		struct writeback_control *wbc, struct inode *inode,
		struct folio *folio, u64 end_pos)
{
	struct iomap_folio_state *ifs = folio->private;
	struct iomap_ioend *ioend, *next;
	unsigned len = i_blocksize(inode);
	unsigned nblocks = i_blocks_per_folio(inode, folio);
	u64 pos = folio_pos(folio);
	int error = 0, count = 0, i;
	LIST_HEAD(submit_list);

	WARN_ON_ONCE(end_pos <= pos);

	if (!ifs && nblocks > 1) {
		ifs = ifs_alloc(inode, folio, 0);
		iomap_set_range_dirty(folio, 0, end_pos - pos);
	}

	WARN_ON_ONCE(ifs && atomic_read(&ifs->write_bytes_pending) != 0);

	/*
	 * Walk through the folio to find areas to write back. If we
	 * run off the end of the current map or find the current map
	 * invalid, grab a new one.
	 */
	for (i = 0; i < nblocks && pos < end_pos; i++, pos += len) {
		if (ifs && !ifs_block_is_dirty(folio, ifs, i))
			continue;

		error = wpc->ops->map_blocks(wpc, inode, pos);
		if (error)
			break;
		trace_iomap_writepage_map(inode, &wpc->iomap);
		if (WARN_ON_ONCE(wpc->iomap.type == IOMAP_INLINE))
			continue;
		if (wpc->iomap.type == IOMAP_HOLE)
			continue;
		iomap_add_to_ioend(inode, pos, folio, ifs, wpc, wbc,
				 &submit_list);
		count++;
	}
	if (count)
		wpc->ioend->io_folios++;

	WARN_ON_ONCE(!wpc->ioend && !list_empty(&submit_list));
	WARN_ON_ONCE(!folio_test_locked(folio));
	WARN_ON_ONCE(folio_test_writeback(folio));
	WARN_ON_ONCE(folio_test_dirty(folio));

	/*
	 * We cannot cancel the ioend directly here on error.  We may have
	 * already set other pages under writeback and hence we have to run I/O
	 * completion to mark the error state of the pages under writeback
	 * appropriately.
	 */
	if (unlikely(error)) {
		/*
		 * Let the filesystem know what portion of the current page
		 * failed to map. If the page hasn't been added to ioend, it
		 * won't be affected by I/O completion and we must unlock it
		 * now.
		 */
		if (wpc->ops->discard_folio)
			wpc->ops->discard_folio(folio, pos);
		if (!count) {
			folio_unlock(folio);
			goto done;
		}
	}

	/*
	 * We can have dirty bits set past end of file in page_mkwrite path
	 * while mapping the last partial folio. Hence it's better to clear
	 * all the dirty bits in the folio here.
	 */
	iomap_clear_range_dirty(folio, 0, folio_size(folio));
	folio_start_writeback(folio);
	folio_unlock(folio);

	/*
	 * Preserve the original error if there was one; catch
	 * submission errors here and propagate into subsequent ioend
	 * submissions.
	 */
	list_for_each_entry_safe(ioend, next, &submit_list, io_list) {
		int error2;

		list_del_init(&ioend->io_list);
		error2 = iomap_submit_ioend(wpc, ioend, error);
		if (error2 && !error)
			error = error2;
	}

	/*
	 * We can end up here with no error and nothing to write only if we race
	 * with a partial page truncate on a sub-page block sized filesystem.
	 */
	if (!count)
		folio_end_writeback(folio);
done:
	mapping_set_error(inode->i_mapping, error);
	return error;
}

/*
 * Write out a dirty page.
 *
 * For delalloc space on the page, we need to allocate space and flush it.
 * For unwritten space on the page, we need to start the conversion to
 * regular allocated space.
 */
static int iomap_do_writepage(struct folio *folio,
		struct writeback_control *wbc, void *data)
{
	struct iomap_writepage_ctx *wpc = data;
	struct inode *inode = folio->mapping->host;
	u64 end_pos, isize;

	trace_iomap_writepage(inode, folio_pos(folio), folio_size(folio));

	/*
	 * Refuse to write the folio out if we're called from reclaim context.
	 *
	 * This avoids stack overflows when called from deeply used stacks in
	 * random callers for direct reclaim or memcg reclaim.  We explicitly
	 * allow reclaim from kswapd as the stack usage there is relatively low.
	 *
	 * This should never happen except in the case of a VM regression so
	 * warn about it.
	 */
	if (WARN_ON_ONCE((current->flags & (PF_MEMALLOC|PF_KSWAPD)) ==
			PF_MEMALLOC))
		goto redirty;

	/*
	 * Is this folio beyond the end of the file?
	 *
	 * The folio index is less than the end_index, adjust the end_pos
	 * to the highest offset that this folio should represent.
	 * -----------------------------------------------------
	 * |			file mapping	       | <EOF> |
	 * -----------------------------------------------------
	 * | Page ... | Page N-2 | Page N-1 |  Page N  |       |
	 * ^--------------------------------^----------|--------
	 * |     desired writeback range    |      see else    |
	 * ---------------------------------^------------------|
	 */
	isize = i_size_read(inode);
	end_pos = folio_pos(folio) + folio_size(folio);
	if (end_pos > isize) {
		/*
		 * Check whether the page to write out is beyond or straddles
		 * i_size or not.
		 * -------------------------------------------------------
		 * |		file mapping		        | <EOF>  |
		 * -------------------------------------------------------
		 * | Page ... | Page N-2 | Page N-1 |  Page N   | Beyond |
		 * ^--------------------------------^-----------|---------
		 * |				    |      Straddles     |
		 * ---------------------------------^-----------|--------|
		 */
		size_t poff = offset_in_folio(folio, isize);
		pgoff_t end_index = isize >> PAGE_SHIFT;

		/*
		 * Skip the page if it's fully outside i_size, e.g.
		 * due to a truncate operation that's in progress.  We've
		 * cleaned this page and truncate will finish things off for
		 * us.
		 *
		 * Note that the end_index is unsigned long.  If the given
		 * offset is greater than 16TB on a 32-bit system then if we
		 * checked if the page is fully outside i_size with
		 * "if (page->index >= end_index + 1)", "end_index + 1" would
		 * overflow and evaluate to 0.  Hence this page would be
		 * redirtied and written out repeatedly, which would result in
		 * an infinite loop; the user program performing this operation
		 * would hang.  Instead, we can detect this situation by
		 * checking if the page is totally beyond i_size or if its
		 * offset is just equal to the EOF.
		 */
		if (folio->index > end_index ||
		    (folio->index == end_index && poff == 0))
			goto unlock;

		/*
		 * The page straddles i_size.  It must be zeroed out on each
		 * and every writepage invocation because it may be mmapped.
		 * "A file is mapped in multiples of the page size.  For a file
		 * that is not a multiple of the page size, the remaining
		 * memory is zeroed when mapped, and writes to that region are
		 * not written out to the file."
		 */
		folio_zero_segment(folio, poff, folio_size(folio));
		end_pos = isize;
	}

	return iomap_writepage_map(wpc, wbc, inode, folio, end_pos);

redirty:
	folio_redirty_for_writepage(wbc, folio);
unlock:
	folio_unlock(folio);
	return 0;
}

int
iomap_writepages(struct address_space *mapping, struct writeback_control *wbc,
		struct iomap_writepage_ctx *wpc,
		const struct iomap_writeback_ops *ops)
{
	int			ret;

	wpc->ops = ops;
	ret = write_cache_pages(mapping, wbc, iomap_do_writepage, wpc);
	if (!wpc->ioend)
		return ret;
	return iomap_submit_ioend(wpc, wpc->ioend, ret);
}
EXPORT_SYMBOL_GPL(iomap_writepages);

static int __init iomap_init(void)
{
	return bioset_init(&iomap_ioend_bioset, 4 * (PAGE_SIZE / SECTOR_SIZE),
			   offsetof(struct iomap_ioend, io_inline_bio),
			   BIOSET_NEED_BVECS);
}
fs_initcall(iomap_init);
