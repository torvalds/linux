// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Red Hat, Inc.
 * Copyright (c) 2018 Christoph Hellwig.
 */
#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/iomap.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>

static loff_t
iomap_seek_hole_actor(struct inode *inode, loff_t start, loff_t length,
		      void *data, struct iomap *iomap, struct iomap *srcmap)
{
	loff_t offset = start;

	switch (iomap->type) {
	case IOMAP_UNWRITTEN:
		offset = mapping_seek_hole_data(inode->i_mapping, start,
				start + length, SEEK_HOLE);
		if (offset == start + length)
			return length;
		fallthrough;
	case IOMAP_HOLE:
		*(loff_t *)data = offset;
		return 0;
	default:
		return length;
	}
}

loff_t
iomap_seek_hole(struct inode *inode, loff_t offset, const struct iomap_ops *ops)
{
	loff_t size = i_size_read(inode);
	loff_t length = size - offset;
	loff_t ret;

	/* Nothing to be found before or beyond the end of the file. */
	if (offset < 0 || offset >= size)
		return -ENXIO;

	while (length > 0) {
		ret = iomap_apply(inode, offset, length, IOMAP_REPORT, ops,
				  &offset, iomap_seek_hole_actor);
		if (ret < 0)
			return ret;
		if (ret == 0)
			break;

		offset += ret;
		length -= ret;
	}

	return offset;
}
EXPORT_SYMBOL_GPL(iomap_seek_hole);

static loff_t
iomap_seek_data_actor(struct inode *inode, loff_t start, loff_t length,
		      void *data, struct iomap *iomap, struct iomap *srcmap)
{
	loff_t offset = start;

	switch (iomap->type) {
	case IOMAP_HOLE:
		return length;
	case IOMAP_UNWRITTEN:
		offset = mapping_seek_hole_data(inode->i_mapping, start,
				start + length, SEEK_DATA);
		if (offset < 0)
			return length;
		fallthrough;
	default:
		*(loff_t *)data = offset;
		return 0;
	}
}

loff_t
iomap_seek_data(struct inode *inode, loff_t offset, const struct iomap_ops *ops)
{
	loff_t size = i_size_read(inode);
	loff_t ret;

	/* Nothing to be found before or beyond the end of the file. */
	if (offset < 0 || offset >= size)
		return -ENXIO;

	while (offset < size) {
		ret = iomap_apply(inode, offset, size - offset, IOMAP_REPORT,
				  ops, &offset, iomap_seek_data_actor);
		if (ret < 0)
			return ret;
		if (ret == 0)
			return offset;
		offset += ret;
	}

	/* We've reached the end of the file without finding data */
	return -ENXIO;
}
EXPORT_SYMBOL_GPL(iomap_seek_data);
