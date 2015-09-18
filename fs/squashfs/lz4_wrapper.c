/*
 * Copyright (c) 2013, 2014
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See
 * the COPYING file in the top-level directory.
 */

#include <linux/buffer_head.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/lz4.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs.h"
#include "decompressor.h"
#include "page_actor.h"

#define LZ4_LEGACY	1

struct lz4_comp_opts {
	__le32 version;
	__le32 flags;
};

struct squashfs_lz4 {
	void *input;
	void *output;
};


static void *lz4_comp_opts(struct squashfs_sb_info *msblk,
	void *buff, int len)
{
	struct lz4_comp_opts *comp_opts = buff;

	/* LZ4 compressed filesystems always have compression options */
	if (comp_opts == NULL || len < sizeof(*comp_opts))
		return ERR_PTR(-EIO);

	if (le32_to_cpu(comp_opts->version) != LZ4_LEGACY) {
		/* LZ4 format currently used by the kernel is the 'legacy'
		 * format */
		ERROR("Unknown LZ4 version\n");
		return ERR_PTR(-EINVAL);
	}

	return NULL;
}


static void *lz4_init(struct squashfs_sb_info *msblk, void *buff)
{
	int block_size = max_t(int, msblk->block_size, SQUASHFS_METADATA_SIZE);
	struct squashfs_lz4 *stream;

	stream = kzalloc(sizeof(*stream), GFP_KERNEL);
	if (stream == NULL)
		goto failed;
	stream->input = vmalloc(block_size);
	if (stream->input == NULL)
		goto failed2;
	stream->output = vmalloc(block_size);
	if (stream->output == NULL)
		goto failed3;

	return stream;

failed3:
	vfree(stream->input);
failed2:
	kfree(stream);
failed:
	ERROR("Failed to initialise LZ4 decompressor\n");
	return ERR_PTR(-ENOMEM);
}


static void lz4_free(void *strm)
{
	struct squashfs_lz4 *stream = strm;

	if (stream) {
		vfree(stream->input);
		vfree(stream->output);
	}
	kfree(stream);
}


static int lz4_uncompress(struct squashfs_sb_info *msblk, void *strm,
	struct buffer_head **bh, int b, int offset, int length,
	struct squashfs_page_actor *output)
{
	struct squashfs_lz4 *stream = strm;
	void *buff = stream->input, *data;
	int avail, i, bytes = length, res;
	size_t dest_len = output->length;

	for (i = 0; i < b; i++) {
		avail = min(bytes, msblk->devblksize - offset);
		memcpy(buff, bh[i]->b_data + offset, avail);
		buff += avail;
		bytes -= avail;
		offset = 0;
		put_bh(bh[i]);
	}

	res = lz4_decompress_unknownoutputsize(stream->input, length,
					stream->output, &dest_len);
	if (res)
		return -EIO;

	bytes = dest_len;
	data = squashfs_first_page(output);
	buff = stream->output;
	while (data) {
		if (bytes <= PAGE_CACHE_SIZE) {
			memcpy(data, buff, bytes);
			break;
		}
		memcpy(data, buff, PAGE_CACHE_SIZE);
		buff += PAGE_CACHE_SIZE;
		bytes -= PAGE_CACHE_SIZE;
		data = squashfs_next_page(output);
	}
	squashfs_finish_page(output);

	return dest_len;
}

const struct squashfs_decompressor squashfs_lz4_comp_ops = {
	.init = lz4_init,
	.comp_opts = lz4_comp_opts,
	.free = lz4_free,
	.decompress = lz4_uncompress,
	.id = LZ4_COMPRESSION,
	.name = "lz4",
	.supported = 1
};
