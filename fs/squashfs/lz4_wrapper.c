// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013, 2014
 * Phillip Lougher <phillip@squashfs.org.uk>
 */

#include <linux/bio.h>
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
	struct bio *bio, int offset, int length,
	struct squashfs_page_actor *output)
{
	struct bvec_iter_all iter_all = {};
	struct bio_vec *bvec = bvec_init_iter_all(&iter_all);
	struct squashfs_lz4 *stream = strm;
	void *buff = stream->input, *data;
	int bytes = length, res;

	while (bio_next_segment(bio, &iter_all)) {
		int avail = min(bytes, ((int)bvec->bv_len) - offset);

		data = bvec_virt(bvec);
		memcpy(buff, data + offset, avail);
		buff += avail;
		bytes -= avail;
		offset = 0;
	}

	res = LZ4_decompress_safe(stream->input, stream->output,
		length, output->length);

	if (res < 0)
		return -EIO;

	bytes = res;
	data = squashfs_first_page(output);
	buff = stream->output;
	while (data) {
		if (bytes <= PAGE_SIZE) {
			if (!IS_ERR(data))
				memcpy(data, buff, bytes);
			break;
		}
		if (!IS_ERR(data))
			memcpy(data, buff, PAGE_SIZE);
		buff += PAGE_SIZE;
		bytes -= PAGE_SIZE;
		data = squashfs_next_page(output);
	}
	squashfs_finish_page(output);

	return res;
}

const struct squashfs_decompressor squashfs_lz4_comp_ops = {
	.init = lz4_init,
	.comp_opts = lz4_comp_opts,
	.free = lz4_free,
	.decompress = lz4_uncompress,
	.id = LZ4_COMPRESSION,
	.name = "lz4",
	.alloc_buffer = 0,
	.supported = 1
};
