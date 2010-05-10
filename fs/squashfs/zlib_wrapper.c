/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009
 * Phillip Lougher <phillip@lougher.demon.co.uk>
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
 * zlib_wrapper.c
 */


#include <linux/mutex.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/zlib.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs_fs_i.h"
#include "squashfs.h"
#include "decompressor.h"

static void *zlib_init(struct squashfs_sb_info *dummy)
{
	z_stream *stream = kmalloc(sizeof(z_stream), GFP_KERNEL);
	if (stream == NULL)
		goto failed;
	stream->workspace = kmalloc(zlib_inflate_workspacesize(),
		GFP_KERNEL);
	if (stream->workspace == NULL)
		goto failed;

	return stream;

failed:
	ERROR("Failed to allocate zlib workspace\n");
	kfree(stream);
	return NULL;
}


static void zlib_free(void *strm)
{
	z_stream *stream = strm;

	if (stream)
		kfree(stream->workspace);
	kfree(stream);
}


static int zlib_uncompress(struct squashfs_sb_info *msblk, void **buffer,
	struct buffer_head **bh, int b, int offset, int length, int srclength,
	int pages)
{
	int zlib_err = 0, zlib_init = 0;
	int avail, bytes, k = 0, page = 0;
	z_stream *stream = msblk->stream;

	mutex_lock(&msblk->read_data_mutex);

	stream->avail_out = 0;
	stream->avail_in = 0;

	bytes = length;
	do {
		if (stream->avail_in == 0 && k < b) {
			avail = min(bytes, msblk->devblksize - offset);
			bytes -= avail;
			wait_on_buffer(bh[k]);
			if (!buffer_uptodate(bh[k]))
				goto release_mutex;

			if (avail == 0) {
				offset = 0;
				put_bh(bh[k++]);
				continue;
			}

			stream->next_in = bh[k]->b_data + offset;
			stream->avail_in = avail;
			offset = 0;
		}

		if (stream->avail_out == 0 && page < pages) {
			stream->next_out = buffer[page++];
			stream->avail_out = PAGE_CACHE_SIZE;
		}

		if (!zlib_init) {
			zlib_err = zlib_inflateInit(stream);
			if (zlib_err != Z_OK) {
				ERROR("zlib_inflateInit returned unexpected "
					"result 0x%x, srclength %d\n",
					zlib_err, srclength);
				goto release_mutex;
			}
			zlib_init = 1;
		}

		zlib_err = zlib_inflate(stream, Z_SYNC_FLUSH);

		if (stream->avail_in == 0 && k < b)
			put_bh(bh[k++]);
	} while (zlib_err == Z_OK);

	if (zlib_err != Z_STREAM_END) {
		ERROR("zlib_inflate error, data probably corrupt\n");
		goto release_mutex;
	}

	zlib_err = zlib_inflateEnd(stream);
	if (zlib_err != Z_OK) {
		ERROR("zlib_inflate error, data probably corrupt\n");
		goto release_mutex;
	}

	length = stream->total_out;
	mutex_unlock(&msblk->read_data_mutex);
	return length;

release_mutex:
	mutex_unlock(&msblk->read_data_mutex);

	for (; k < b; k++)
		put_bh(bh[k]);

	return -EIO;
}

const struct squashfs_decompressor squashfs_zlib_comp_ops = {
	.init = zlib_init,
	.free = zlib_free,
	.decompress = zlib_uncompress,
	.id = ZLIB_COMPRESSION,
	.name = "zlib",
	.supported = 1
};

