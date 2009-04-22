/* RomFS storage access routines
 *
 * Copyright © 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/fs.h>
#include <linux/mtd/super.h>
#include <linux/buffer_head.h>
#include "internal.h"

#if !defined(CONFIG_ROMFS_ON_MTD) && !defined(CONFIG_ROMFS_ON_BLOCK)
#error no ROMFS backing store interface configured
#endif

#ifdef CONFIG_ROMFS_ON_MTD
#define ROMFS_MTD_READ(sb, ...) ((sb)->s_mtd->read((sb)->s_mtd, ##__VA_ARGS__))

/*
 * read data from an romfs image on an MTD device
 */
static int romfs_mtd_read(struct super_block *sb, unsigned long pos,
			  void *buf, size_t buflen)
{
	size_t rlen;
	int ret;

	ret = ROMFS_MTD_READ(sb, pos, buflen, &rlen, buf);
	return (ret < 0 || rlen != buflen) ? -EIO : 0;
}

/*
 * determine the length of a string in a romfs image on an MTD device
 */
static ssize_t romfs_mtd_strnlen(struct super_block *sb,
				 unsigned long pos, size_t maxlen)
{
	ssize_t n = 0;
	size_t segment;
	u_char buf[16], *p;
	size_t len;
	int ret;

	/* scan the string up to 16 bytes at a time */
	while (maxlen > 0) {
		segment = min_t(size_t, maxlen, 16);
		ret = ROMFS_MTD_READ(sb, pos, segment, &len, buf);
		if (ret < 0)
			return ret;
		p = memchr(buf, 0, len);
		if (p)
			return n + (p - buf);
		maxlen -= len;
		pos += len;
		n += len;
	}

	return n;
}

/*
 * compare a string to one in a romfs image on MTD
 * - return 1 if matched, 0 if differ, -ve if error
 */
static int romfs_mtd_strncmp(struct super_block *sb, unsigned long pos,
			     const char *str, size_t size)
{
	u_char buf[16];
	size_t len, segment;
	int ret;

	/* scan the string up to 16 bytes at a time */
	while (size > 0) {
		segment = min_t(size_t, size, 16);
		ret = ROMFS_MTD_READ(sb, pos, segment, &len, buf);
		if (ret < 0)
			return ret;
		if (memcmp(buf, str, len) != 0)
			return 0;
		size -= len;
		pos += len;
		str += len;
	}

	return 1;
}
#endif /* CONFIG_ROMFS_ON_MTD */

#ifdef CONFIG_ROMFS_ON_BLOCK
/*
 * read data from an romfs image on a block device
 */
static int romfs_blk_read(struct super_block *sb, unsigned long pos,
			  void *buf, size_t buflen)
{
	struct buffer_head *bh;
	unsigned long offset;
	size_t segment;

	/* copy the string up to blocksize bytes at a time */
	while (buflen > 0) {
		offset = pos & (ROMBSIZE - 1);
		segment = min_t(size_t, buflen, ROMBSIZE - offset);
		bh = sb_bread(sb, pos >> ROMBSBITS);
		if (!bh)
			return -EIO;
		memcpy(buf, bh->b_data + offset, segment);
		brelse(bh);
		buflen -= segment;
		pos += segment;
	}

	return 0;
}

/*
 * determine the length of a string in romfs on a block device
 */
static ssize_t romfs_blk_strnlen(struct super_block *sb,
				 unsigned long pos, size_t limit)
{
	struct buffer_head *bh;
	unsigned long offset;
	ssize_t n = 0;
	size_t segment;
	u_char *buf, *p;

	/* scan the string up to blocksize bytes at a time */
	while (limit > 0) {
		offset = pos & (ROMBSIZE - 1);
		segment = min_t(size_t, limit, ROMBSIZE - offset);
		bh = sb_bread(sb, pos >> ROMBSBITS);
		if (!bh)
			return -EIO;
		buf = bh->b_data + offset;
		p = memchr(buf, 0, segment);
		brelse(bh);
		if (p)
			return n + (p - buf);
		limit -= segment;
		pos += segment;
		n += segment;
	}

	return n;
}

/*
 * compare a string to one in a romfs image on a block device
 * - return 1 if matched, 0 if differ, -ve if error
 */
static int romfs_blk_strncmp(struct super_block *sb, unsigned long pos,
			     const char *str, size_t size)
{
	struct buffer_head *bh;
	unsigned long offset;
	size_t segment;
	bool x;

	/* scan the string up to 16 bytes at a time */
	while (size > 0) {
		offset = pos & (ROMBSIZE - 1);
		segment = min_t(size_t, size, ROMBSIZE - offset);
		bh = sb_bread(sb, pos >> ROMBSBITS);
		if (!bh)
			return -EIO;
		x = (memcmp(bh->b_data + offset, str, segment) != 0);
		brelse(bh);
		if (x)
			return 0;
		size -= segment;
		pos += segment;
		str += segment;
	}

	return 1;
}
#endif /* CONFIG_ROMFS_ON_BLOCK */

/*
 * read data from the romfs image
 */
int romfs_dev_read(struct super_block *sb, unsigned long pos,
		   void *buf, size_t buflen)
{
	size_t limit;

	limit = romfs_maxsize(sb);
	if (pos >= limit)
		return -EIO;
	if (buflen > limit - pos)
		buflen = limit - pos;

#ifdef CONFIG_ROMFS_ON_MTD
	if (sb->s_mtd)
		return romfs_mtd_read(sb, pos, buf, buflen);
#endif
#ifdef CONFIG_ROMFS_ON_BLOCK
	if (sb->s_bdev)
		return romfs_blk_read(sb, pos, buf, buflen);
#endif
	return -EIO;
}

/*
 * determine the length of a string in romfs
 */
ssize_t romfs_dev_strnlen(struct super_block *sb,
			  unsigned long pos, size_t maxlen)
{
	size_t limit;

	limit = romfs_maxsize(sb);
	if (pos >= limit)
		return -EIO;
	if (maxlen > limit - pos)
		maxlen = limit - pos;

#ifdef CONFIG_ROMFS_ON_MTD
	if (sb->s_mtd)
		return romfs_mtd_strnlen(sb, pos, limit);
#endif
#ifdef CONFIG_ROMFS_ON_BLOCK
	if (sb->s_bdev)
		return romfs_blk_strnlen(sb, pos, limit);
#endif
	return -EIO;
}

/*
 * compare a string to one in romfs
 * - return 1 if matched, 0 if differ, -ve if error
 */
int romfs_dev_strncmp(struct super_block *sb, unsigned long pos,
		      const char *str, size_t size)
{
	size_t limit;

	limit = romfs_maxsize(sb);
	if (pos >= limit)
		return -EIO;
	if (size > ROMFS_MAXFN)
		return -ENAMETOOLONG;
	if (size > limit - pos)
		return -EIO;

#ifdef CONFIG_ROMFS_ON_MTD
	if (sb->s_mtd)
		return romfs_mtd_strncmp(sb, pos, str, size);
#endif
#ifdef CONFIG_ROMFS_ON_BLOCK
	if (sb->s_bdev)
		return romfs_blk_strncmp(sb, pos, str, size);
#endif
	return -EIO;
}
