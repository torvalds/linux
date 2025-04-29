// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2021 Christoph Hellwig.
 */
#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/iomap.h>
#include <linux/fiemap.h>
#include <linux/pagemap.h>

static int iomap_to_fiemap(struct fiemap_extent_info *fi,
		const struct iomap *iomap, u32 flags)
{
	switch (iomap->type) {
	case IOMAP_HOLE:
		/* skip holes */
		return 0;
	case IOMAP_DELALLOC:
		flags |= FIEMAP_EXTENT_DELALLOC | FIEMAP_EXTENT_UNKNOWN;
		break;
	case IOMAP_MAPPED:
		break;
	case IOMAP_UNWRITTEN:
		flags |= FIEMAP_EXTENT_UNWRITTEN;
		break;
	case IOMAP_INLINE:
		flags |= FIEMAP_EXTENT_DATA_INLINE;
		break;
	}

	if (iomap->flags & IOMAP_F_MERGED)
		flags |= FIEMAP_EXTENT_MERGED;
	if (iomap->flags & IOMAP_F_SHARED)
		flags |= FIEMAP_EXTENT_SHARED;

	return fiemap_fill_next_extent(fi, iomap->offset,
			iomap->addr != IOMAP_NULL_ADDR ? iomap->addr : 0,
			iomap->length, flags);
}

static int iomap_fiemap_iter(struct iomap_iter *iter,
		struct fiemap_extent_info *fi, struct iomap *prev)
{
	int ret;

	if (iter->iomap.type == IOMAP_HOLE)
		goto advance;

	ret = iomap_to_fiemap(fi, prev, 0);
	*prev = iter->iomap;
	if (ret < 0)
		return ret;
	if (ret == 1)	/* extent array full */
		return 0;

advance:
	return iomap_iter_advance_full(iter);
}

int iomap_fiemap(struct inode *inode, struct fiemap_extent_info *fi,
		u64 start, u64 len, const struct iomap_ops *ops)
{
	struct iomap_iter iter = {
		.inode		= inode,
		.pos		= start,
		.len		= len,
		.flags		= IOMAP_REPORT,
	};
	struct iomap prev = {
		.type		= IOMAP_HOLE,
	};
	int ret;

	ret = fiemap_prep(inode, fi, start, &iter.len, 0);
	if (ret)
		return ret;

	while ((ret = iomap_iter(&iter, ops)) > 0)
		iter.status = iomap_fiemap_iter(&iter, fi, &prev);

	if (prev.type != IOMAP_HOLE) {
		ret = iomap_to_fiemap(fi, &prev, FIEMAP_EXTENT_LAST);
		if (ret < 0)
			return ret;
	}

	/* inode with no (attribute) mapping will give ENOENT */
	if (ret < 0 && ret != -ENOENT)
		return ret;
	return 0;
}
EXPORT_SYMBOL_GPL(iomap_fiemap);

/* legacy ->bmap interface.  0 is the error return (!) */
sector_t
iomap_bmap(struct address_space *mapping, sector_t bno,
		const struct iomap_ops *ops)
{
	struct iomap_iter iter = {
		.inode	= mapping->host,
		.pos	= (loff_t)bno << mapping->host->i_blkbits,
		.len	= i_blocksize(mapping->host),
		.flags	= IOMAP_REPORT,
	};
	const unsigned int blkshift = mapping->host->i_blkbits - SECTOR_SHIFT;
	int ret;

	if (filemap_write_and_wait(mapping))
		return 0;

	bno = 0;
	while ((ret = iomap_iter(&iter, ops)) > 0) {
		if (iter.iomap.type == IOMAP_MAPPED)
			bno = iomap_sector(&iter.iomap, iter.pos) >> blkshift;
		/* leave iter.status unset to abort loop */
	}
	if (ret)
		return 0;

	return bno;
}
EXPORT_SYMBOL_GPL(iomap_bmap);
