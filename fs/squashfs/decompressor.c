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
#include "page_actor.h"

/*
 * This file (and decompressor.h) implements a decompressor framework for
 * Squashfs, allowing multiple decompressors to be easily supported
 */

static const struct squashfs_decompressor squashfs_lzma_unsupported_comp_ops = {
	NULL, NULL, NULL, NULL, LZMA_COMPRESSION, "lzma", 0
};

#ifndef CONFIG_SQUASHFS_LZ4
static const struct squashfs_decompressor squashfs_lz4_comp_ops = {
	NULL, NULL, NULL, NULL, LZ4_COMPRESSION, "lz4", 0
};
#endif

#ifndef CONFIG_SQUASHFS_LZO
static const struct squashfs_decompressor squashfs_lzo_comp_ops = {
	NULL, NULL, NULL, NULL, LZO_COMPRESSION, "lzo", 0
};
#endif

#ifndef CONFIG_SQUASHFS_XZ
static const struct squashfs_decompressor squashfs_xz_comp_ops = {
	NULL, NULL, NULL, NULL, XZ_COMPRESSION, "xz", 0
};
#endif

#ifndef CONFIG_SQUASHFS_ZLIB
static const struct squashfs_decompressor squashfs_zlib_comp_ops = {
	NULL, NULL, NULL, NULL, ZLIB_COMPRESSION, "zlib", 0
};
#endif

static const struct squashfs_decompressor squashfs_unknown_comp_ops = {
	NULL, NULL, NULL, NULL, 0, "unknown", 0
};

static const struct squashfs_decompressor *decompressor[] = {
	&squashfs_zlib_comp_ops,
	&squashfs_lz4_comp_ops,
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


static void *get_comp_opts(struct super_block *sb, unsigned short flags)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	void *buffer = NULL, *comp_opts;
	struct squashfs_page_actor *actor = NULL;
	int length = 0;

	/*
	 * Read decompressor specific options from file system if present
	 */
	if (SQUASHFS_COMP_OPTS(flags)) {
		buffer = kmalloc(PAGE_CACHE_SIZE, GFP_KERNEL);
		if (buffer == NULL) {
			comp_opts = ERR_PTR(-ENOMEM);
			goto out;
		}

		actor = squashfs_page_actor_init(&buffer, 1, 0);
		if (actor == NULL) {
			comp_opts = ERR_PTR(-ENOMEM);
			goto out;
		}

		length = squashfs_read_data(sb,
			sizeof(struct squashfs_super_block), 0, NULL, actor);

		if (length < 0) {
			comp_opts = ERR_PTR(length);
			goto out;
		}
	}

	comp_opts = squashfs_comp_opts(msblk, buffer, length);

out:
	kfree(actor);
	kfree(buffer);
	return comp_opts;
}


void *squashfs_decompressor_setup(struct super_block *sb, unsigned short flags)
{
	struct squashfs_sb_info *msblk = sb->s_fs_info;
	void *stream, *comp_opts = get_comp_opts(sb, flags);

	if (IS_ERR(comp_opts))
		return comp_opts;

	stream = squashfs_decompressor_create(msblk, comp_opts);
	if (IS_ERR(stream))
		kfree(comp_opts);

	return stream;
}
