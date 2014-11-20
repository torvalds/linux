/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010
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
 * xz_wrapper.c
 */


#include <linux/mutex.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/xz.h>
#include <linux/bitops.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs.h"
#include "decompressor.h"
#include "page_actor.h"

struct squashfs_xz {
	struct xz_dec *state;
	struct xz_buf buf;
};

struct disk_comp_opts {
	__le32 dictionary_size;
	__le32 flags;
};

struct comp_opts {
	int dict_size;
};

static void *squashfs_xz_comp_opts(struct squashfs_sb_info *msblk,
	void *buff, int len)
{
	struct disk_comp_opts *comp_opts = buff;
	struct comp_opts *opts;
	int err = 0, n;

	opts = kmalloc(sizeof(*opts), GFP_KERNEL);
	if (opts == NULL) {
		err = -ENOMEM;
		goto out2;
	}

	if (comp_opts) {
		/* check compressor options are the expected length */
		if (len < sizeof(*comp_opts)) {
			err = -EIO;
			goto out;
		}

		opts->dict_size = le32_to_cpu(comp_opts->dictionary_size);

		/* the dictionary size should be 2^n or 2^n+2^(n+1) */
		n = ffs(opts->dict_size) - 1;
		if (opts->dict_size != (1 << n) && opts->dict_size != (1 << n) +
						(1 << (n + 1))) {
			err = -EIO;
			goto out;
		}
	} else
		/* use defaults */
		opts->dict_size = max_t(int, msblk->block_size,
							SQUASHFS_METADATA_SIZE);

	return opts;

out:
	kfree(opts);
out2:
	return ERR_PTR(err);
}


static void *squashfs_xz_init(struct squashfs_sb_info *msblk, void *buff)
{
	struct comp_opts *comp_opts = buff;
	struct squashfs_xz *stream;
	int err;

	stream = kmalloc(sizeof(*stream), GFP_KERNEL);
	if (stream == NULL) {
		err = -ENOMEM;
		goto failed;
	}

	stream->state = xz_dec_init(XZ_PREALLOC, comp_opts->dict_size);
	if (stream->state == NULL) {
		kfree(stream);
		err = -ENOMEM;
		goto failed;
	}

	return stream;

failed:
	ERROR("Failed to initialise xz decompressor\n");
	return ERR_PTR(err);
}


static void squashfs_xz_free(void *strm)
{
	struct squashfs_xz *stream = strm;

	if (stream) {
		xz_dec_end(stream->state);
		kfree(stream);
	}
}


static int squashfs_xz_uncompress(struct squashfs_sb_info *msblk, void *strm,
	struct buffer_head **bh, int b, int offset, int length,
	struct squashfs_page_actor *output)
{
	enum xz_ret xz_err;
	int avail, total = 0, k = 0;
	struct squashfs_xz *stream = strm;

	xz_dec_reset(stream->state);
	stream->buf.in_pos = 0;
	stream->buf.in_size = 0;
	stream->buf.out_pos = 0;
	stream->buf.out_size = PAGE_CACHE_SIZE;
	stream->buf.out = squashfs_first_page(output);

	do {
		if (stream->buf.in_pos == stream->buf.in_size && k < b) {
			avail = min(length, msblk->devblksize - offset);
			length -= avail;
			stream->buf.in = bh[k]->b_data + offset;
			stream->buf.in_size = avail;
			stream->buf.in_pos = 0;
			offset = 0;
		}

		if (stream->buf.out_pos == stream->buf.out_size) {
			stream->buf.out = squashfs_next_page(output);
			if (stream->buf.out != NULL) {
				stream->buf.out_pos = 0;
				total += PAGE_CACHE_SIZE;
			}
		}

		xz_err = xz_dec_run(stream->state, &stream->buf);

		if (stream->buf.in_pos == stream->buf.in_size && k < b)
			put_bh(bh[k++]);
	} while (xz_err == XZ_OK);

	squashfs_finish_page(output);

	if (xz_err != XZ_STREAM_END || k < b)
		goto out;

	return total + stream->buf.out_pos;

out:
	for (; k < b; k++)
		put_bh(bh[k]);

	return -EIO;
}

const struct squashfs_decompressor squashfs_xz_comp_ops = {
	.init = squashfs_xz_init,
	.comp_opts = squashfs_xz_comp_opts,
	.free = squashfs_xz_free,
	.decompress = squashfs_xz_uncompress,
	.id = XZ_COMPRESSION,
	.name = "xz",
	.supported = 1
};
