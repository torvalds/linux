// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (c) 2016-2021 Christoph Hellwig.
 */
#include <linux/iomap.h>
#include "trace.h"

/*
 * Release the iter folio batch. Note that the iomap flag is meant to control
 * the I/O path for the mapping and may not be set in error situations.
 */
static inline void iomap_iter_clean_fbatch(const struct iomap_iter *iter,
		struct iomap *iomap)
{
	if (!iter->fbatch)
		return;
	iomap->flags &= ~IOMAP_F_FOLIO_BATCH;
	if (folio_batch_count(iter->fbatch)) {
		folio_batch_release(iter->fbatch);
		folio_batch_reinit(iter->fbatch);
	}
}

/* Advance the current iterator position and decrement the remaining length */
int iomap_iter_advance(struct iomap_iter *iter, u64 count)
{
	if (WARN_ON_ONCE(count > iomap_length(iter)))
		return -EIO;
	iter->pos += count;
	iter->len -= count;
	return 0;
}

static inline void iomap_iter_done(struct iomap_iter *iter)
{
	WARN_ON_ONCE(iter->iomap.offset > iter->pos);
	WARN_ON_ONCE(iter->iomap.length == 0);
	WARN_ON_ONCE(iter->iomap.offset + iter->iomap.length <= iter->pos);
	WARN_ON_ONCE(iter->iomap.flags & IOMAP_F_STALE);

	iter->iter_start_pos = iter->pos;

	trace_iomap_iter_dstmap(iter->inode, &iter->iomap);
	if (iter->srcmap.type != IOMAP_HOLE)
		trace_iomap_iter_srcmap(iter->inode, &iter->srcmap);
}

/**
 * iomap_iter_continue - decide whether iteration should continue
 * @iter: iteration structure
 * @iomap: the mapping that was just processed
 * @srcmap: the source mapping that was just processed
 *
 * Helper normally called via iomap_iter_next(). Called after the previous
 * mapping has been finished to determine whether there is more of the file
 * range left to process.
 *
 * Returns 1 if there is more work to do, in which case @iomap and @srcmap are
 * cleared so the caller can produce the next mapping; zero if the range is
 * fully consumed; or a negative errno on error.
 */
int iomap_iter_continue(const struct iomap_iter *iter, struct iomap *iomap,
		struct iomap *srcmap, int ret)
{
	const bool stale = iomap->flags & IOMAP_F_STALE;
	const ssize_t advanced = iter->pos - iter->iter_start_pos;

	if (ret < 0 && !advanced)
		return ret;

	/*
	 * Use iter->len to determine whether to continue onto the next mapping.
	 * Explicitly terminate on error status or if the current iter has not
	 * advanced at all (i.e. no work was done for some reason) unless the
	 * mapping has been marked stale and needs to be reprocessed.
	 */
	if (WARN_ON_ONCE(iter->status > 0))
		/* detect old return semantics where this would advance */
		ret = -EIO;
	else if (iter->status < 0)
		ret = iter->status;
	else if (iter->len == 0 || (!advanced && !stale))
		ret = 0;
	else
		ret = 1;

	iomap_iter_clean_fbatch(iter, iomap);

	if (ret <= 0)
		return ret;

	memset(iomap, 0, sizeof(*iomap));
	memset(srcmap, 0, sizeof(*srcmap));

	return ret;
}
EXPORT_SYMBOL_GPL(iomap_iter_continue);

/**
 * iomap_iter - iterate over ranges in a file
 * @iter: iteration structure
 * @ops: iomap ops provided by the filesystem
 *
 * Iterate over filesystem-provided space mappings for the provided file range.
 *
 * This function handles cleanup of resources acquired for iteration when the
 * filesystem indicates there are no more space mappings, which means that this
 * function must be called in a loop that continues as long it returns a
 * positive value.  If 0 or a negative value is returned, the caller must not
 * return to the loop body.  Within a loop body, there are two ways to break out
 * of the loop body:  leave @iter.status unchanged, or set it to a negative
 * errno.
 */
int iomap_iter(struct iomap_iter *iter, const struct iomap_ops *ops)
{
	int ret;

	trace_iomap_iter(iter, ops, _RET_IP_);

	ret = iomap_iter_next(iter, &iter->iomap, &iter->srcmap,
			ops->iomap_begin, ops->iomap_end);
	iter->status = 0;
	if (ret > 0)
		iomap_iter_done(iter);
	else if (ret < 0)
		iomap_iter_clean_fbatch(iter, &iter->iomap);

	return ret;
}
