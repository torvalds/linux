// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (c) 2016-2021 Christoph Hellwig.
 */
#include <linux/fs.h>
#include <linux/iomap.h>
#include "trace.h"

static inline int iomap_iter_advance(struct iomap_iter *iter)
{
	/* handle the previous iteration (if any) */
	if (iter->iomap.length) {
		if (iter->processed <= 0)
			return iter->processed;
		if (WARN_ON_ONCE(iter->processed > iomap_length(iter)))
			return -EIO;
		iter->pos += iter->processed;
		iter->len -= iter->processed;
		if (!iter->len)
			return 0;
	}

	/* clear the state for the next iteration */
	iter->processed = 0;
	memset(&iter->iomap, 0, sizeof(iter->iomap));
	memset(&iter->srcmap, 0, sizeof(iter->srcmap));
	return 1;
}

static inline void iomap_iter_done(struct iomap_iter *iter)
{
	WARN_ON_ONCE(iter->iomap.offset > iter->pos);
	WARN_ON_ONCE(iter->iomap.length == 0);
	WARN_ON_ONCE(iter->iomap.offset + iter->iomap.length <= iter->pos);

	trace_iomap_iter_dstmap(iter->inode, &iter->iomap);
	if (iter->srcmap.type != IOMAP_HOLE)
		trace_iomap_iter_srcmap(iter->inode, &iter->srcmap);
}

/**
 * iomap_iter - iterate over a ranges in a file
 * @iter: iteration structue
 * @ops: iomap ops provided by the file system
 *
 * Iterate over filesystem-provided space mappings for the provided file range.
 *
 * This function handles cleanup of resources acquired for iteration when the
 * filesystem indicates there are no more space mappings, which means that this
 * function must be called in a loop that continues as long it returns a
 * positive value.  If 0 or a negative value is returned, the caller must not
 * return to the loop body.  Within a loop body, there are two ways to break out
 * of the loop body:  leave @iter.processed unchanged, or set it to a negative
 * errno.
 */
int iomap_iter(struct iomap_iter *iter, const struct iomap_ops *ops)
{
	int ret;

	if (iter->iomap.length && ops->iomap_end) {
		ret = ops->iomap_end(iter->inode, iter->pos, iomap_length(iter),
				iter->processed > 0 ? iter->processed : 0,
				iter->flags, &iter->iomap);
		if (ret < 0 && !iter->processed)
			return ret;
	}

	trace_iomap_iter(iter, ops, _RET_IP_);
	ret = iomap_iter_advance(iter);
	if (ret <= 0)
		return ret;

	ret = ops->iomap_begin(iter->inode, iter->pos, iter->len, iter->flags,
			       &iter->iomap, &iter->srcmap);
	if (ret < 0)
		return ret;
	iomap_iter_done(iter);
	return 1;
}
