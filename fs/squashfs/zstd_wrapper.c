// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * zstd_wrapper.c
 */

#include <linux/mutex.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/zstd.h>
#include <linux/vmalloc.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs.h"
#include "decompressor.h"
#include "page_actor.h"

struct workspace {
	void *mem;
	size_t mem_size;
	size_t window_size;
};

static void *zstd_init(struct squashfs_sb_info *msblk, void *buff)
{
	struct workspace *wksp = kmalloc(sizeof(*wksp), GFP_KERNEL);

	if (wksp == NULL)
		goto failed;
	wksp->window_size = max_t(size_t,
			msblk->block_size, SQUASHFS_METADATA_SIZE);
	wksp->mem_size = ZSTD_DStreamWorkspaceBound(wksp->window_size);
	wksp->mem = vmalloc(wksp->mem_size);
	if (wksp->mem == NULL)
		goto failed;

	return wksp;

failed:
	ERROR("Failed to allocate zstd workspace\n");
	kfree(wksp);
	return ERR_PTR(-ENOMEM);
}


static void zstd_free(void *strm)
{
	struct workspace *wksp = strm;

	if (wksp)
		vfree(wksp->mem);
	kfree(wksp);
}


static int zstd_uncompress(struct squashfs_sb_info *msblk, void *strm,
	struct bio *bio, int offset, int length,
	struct squashfs_page_actor *output)
{
	struct workspace *wksp = strm;
	ZSTD_DStream *stream;
	size_t total_out = 0;
	int error = 0;
	ZSTD_inBuffer in_buf = { NULL, 0, 0 };
	ZSTD_outBuffer out_buf = { NULL, 0, 0 };
	struct bvec_iter_all iter_all = {};
	struct bio_vec *bvec = bvec_init_iter_all(&iter_all);

	stream = ZSTD_initDStream(wksp->window_size, wksp->mem, wksp->mem_size);

	if (!stream) {
		ERROR("Failed to initialize zstd decompressor\n");
		return -EIO;
	}

	out_buf.size = PAGE_SIZE;
	out_buf.dst = squashfs_first_page(output);

	for (;;) {
		size_t zstd_err;

		if (in_buf.pos == in_buf.size) {
			const void *data;
			int avail;

			if (!bio_next_segment(bio, &iter_all)) {
				error = -EIO;
				break;
			}

			avail = min(length, ((int)bvec->bv_len) - offset);
			data = bvec_virt(bvec);
			length -= avail;
			in_buf.src = data + offset;
			in_buf.size = avail;
			in_buf.pos = 0;
			offset = 0;
		}

		if (out_buf.pos == out_buf.size) {
			out_buf.dst = squashfs_next_page(output);
			if (out_buf.dst == NULL) {
				/* Shouldn't run out of pages
				 * before stream is done.
				 */
				error = -EIO;
				break;
			}
			out_buf.pos = 0;
			out_buf.size = PAGE_SIZE;
		}

		total_out -= out_buf.pos;
		zstd_err = ZSTD_decompressStream(stream, &out_buf, &in_buf);
		total_out += out_buf.pos; /* add the additional data produced */
		if (zstd_err == 0)
			break;

		if (ZSTD_isError(zstd_err)) {
			ERROR("zstd decompression error: %d\n",
					(int)ZSTD_getErrorCode(zstd_err));
			error = -EIO;
			break;
		}
	}

	squashfs_finish_page(output);

	return error ? error : total_out;
}

const struct squashfs_decompressor squashfs_zstd_comp_ops = {
	.init = zstd_init,
	.free = zstd_free,
	.decompress = zstd_uncompress,
	.id = ZSTD_COMPRESSION,
	.name = "zstd",
	.supported = 1
};
