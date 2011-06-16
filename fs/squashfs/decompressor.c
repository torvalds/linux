/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * decompressor.c
 */

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "decompressor.h"
#include "squashfs.h"

/*
 * This file (and decompressor.h) implements a decompressor framework for
 * Squashfs, allowing multiple decompressors to be easily supported
 */

static const struct squashfs_decompressor squashfs_lzma_unsupported_comp_ops = {
	NULL, NULL, NULL, LZMA_COMPRESSION, "lzma", 0
};

#ifndef CONFIG_SQUASHFS_LZO
static const struct squashfs_decompressor squashfs_lzo_comp_ops = {
	NULL, NULL, NULL, LZO_COMPRESSION, "lzo", 0
};
#endif

#ifndef CONFIG_SQUASHFS_XZ
static const struct squashfs_decompressor squashfs_xz_comp_ops = {
	NULL, NULL, NULL, XZ_COMPRESSION, "xz", 0
};
#endif

static const struct squashfs_decompressor squashfs_unknown_comp_ops = {
	NULL, NULL, NULL, 0, "unknown", 0
};

static const struct squashfs_decompressor *decompressor[] = {
	&squashfs_zlib_comp_ops,
	&squashfs_lzo_comp_ops,
	&squashfs_xz_comp_ops,
	&squashfs_lzma_unsupported_comp_ops,
	&squashfs_unknown_comp_ops
};


const struct squashfs_decompressor *squashfs_lookup_decompressor(int id)
{
	int i;

	for (i = 0; decompressor[i]->id; i++)
		if (id == decompressor[i]->id)
			break;

	return decompressor[i];
}


void *squashfs_decompressor_init(struct super_block *sb, unsigned short flags)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	void *strm, *buffer = NULL;
	int length = 0;

	/*
	 * Read decompressor specific options from file system if present
	 */
	if (SQUASHFS_COMP_OPTS(flags)) {
		buffer = kmalloc(PAGE_CACHE_SIZE, GFP_KERNEL);
		if (buffer == NULL)
			return ERR_PTR(-ENOMEM);

		length = squashfs_read_data(sb, &buffer,
			sizeof(struct squashfs_super_block), 0, NULL,
			PAGE_CACHE_SIZE, 1);

		if (length < 0) {
			strm = ERR_PTR(length);
			goto finished;
		}
	}

	strm = msblk->decompressor->init(msblk, buffer, length);

finished:
	kfree(buffer);

	return strm;
}
