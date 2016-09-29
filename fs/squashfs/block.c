/*
 * Squashfs - a compressed read only filesystem for Linux
 *
 * Copyright (c) 2002, 2003, 2004, 2005, 2006, 2007, 2008
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
 * block.c
 */

/*
 * This file implements the low-level routines to read and decompress
 * datablocks and metadata blocks.
 */

#include <linux/fs.h>
#include <linux/vfs.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/workqueue.h>

#include "squashfs_fs.h"
#include "squashfs_fs_sb.h"
#include "squashfs.h"
#include "decompressor.h"
#include "page_actor.h"

static struct workqueue_struct *squashfs_read_wq;

struct squashfs_read_request {
	struct super_block *sb;
	u64 index;
	int length;
	int compressed;
	int offset;
	u64 read_end;
	struct squashfs_page_actor *output;
	enum {
		SQUASHFS_COPY,
		SQUASHFS_DECOMPRESS,
		SQUASHFS_METADATA,
	} data_processing;
	bool synchronous;

	/*
	 * If the read is synchronous, it is possible to retrieve information
	 * about the request by setting these pointers.
	 */
	int *res;
	int *bytes_read;
	int *bytes_uncompressed;

	int nr_buffers;
	struct buffer_head **bh;
	struct work_struct offload;
};

struct squashfs_bio_request {
	struct buffer_head **bh;
	int nr_buffers;
};

static int squashfs_bio_submit(struct squashfs_read_request *req);

int squashfs_init_read_wq(void)
{
	squashfs_read_wq = create_workqueue("SquashFS read wq");
	return !!squashfs_read_wq;
}

void squashfs_destroy_read_wq(void)
{
	flush_workqueue(squashfs_read_wq);
	destroy_workqueue(squashfs_read_wq);
}

static void free_read_request(struct squashfs_read_request *req, int error)
{
	if (!req->synchronous)
		squashfs_page_actor_free(req->output, error);
	if (req->res)
		*(req->res) = error;
	kfree(req->bh);
	kfree(req);
}

static void squashfs_process_blocks(struct squashfs_read_request *req)
{
	int error = 0;
	int bytes, i, length;
	struct squashfs_sb_info *msblk = req->sb->s_fs_info;
	struct squashfs_page_actor *actor = req->output;
	struct buffer_head **bh = req->bh;
	int nr_buffers = req->nr_buffers;

	for (i = 0; i < nr_buffers; ++i) {
		if (!bh[i])
			continue;
		wait_on_buffer(bh[i]);
		if (!buffer_uptodate(bh[i]))
			error = -EIO;
	}
	if (error)
		goto cleanup;

	if (req->data_processing == SQUASHFS_METADATA) {
		/* Extract the length of the metadata block */
		if (req->offset != msblk->devblksize - 1)
			length = *((u16 *)(bh[0]->b_data + req->offset));
		else {
			length = bh[0]->b_data[req->offset];
			length |= bh[1]->b_data[0] << 8;
		}
		req->compressed = SQUASHFS_COMPRESSED(length);
		req->data_processing = req->compressed ? SQUASHFS_DECOMPRESS
						       : SQUASHFS_COPY;
		length = SQUASHFS_COMPRESSED_SIZE(length);
		if (req->index + length + 2 > req->read_end) {
			for (i = 0; i < nr_buffers; ++i)
				put_bh(bh[i]);
			kfree(bh);
			req->length = length;
			req->index += 2;
			squashfs_bio_submit(req);
			return;
		}
		req->length = length;
		req->offset = (req->offset + 2) % PAGE_SIZE;
		if (req->offset < 2) {
			put_bh(bh[0]);
			++bh;
			--nr_buffers;
		}
	}
	if (req->bytes_read)
		*(req->bytes_read) = req->length;

	if (req->data_processing == SQUASHFS_COPY) {
		squashfs_bh_to_actor(bh, nr_buffers, req->output, req->offset,
			req->length, msblk->devblksize);
	} else if (req->data_processing == SQUASHFS_DECOMPRESS) {
		req->length = squashfs_decompress(msblk, bh, nr_buffers,
			req->offset, req->length, actor);
		if (req->length < 0) {
			error = -EIO;
			goto cleanup;
		}
	}

	/* Last page may have trailing bytes not filled */
	bytes = req->length % PAGE_SIZE;
	if (bytes && actor->page[actor->pages - 1])
		zero_user_segment(actor->page[actor->pages - 1], bytes,
				  PAGE_SIZE);

cleanup:
	if (req->bytes_uncompressed)
		*(req->bytes_uncompressed) = req->length;
	if (error) {
		for (i = 0; i < nr_buffers; ++i)
			if (bh[i])
				put_bh(bh[i]);
	}
	free_read_request(req, error);
}

static void read_wq_handler(struct work_struct *work)
{
	squashfs_process_blocks(container_of(work,
		    struct squashfs_read_request, offload));
}

static void squashfs_bio_end_io(struct bio *bio)
{
	int i;
	int error = bio->bi_error;
	struct squashfs_bio_request *bio_req = bio->bi_private;

	bio_put(bio);

	for (i = 0; i < bio_req->nr_buffers; ++i) {
		if (!bio_req->bh[i])
			continue;
		if (!error)
			set_buffer_uptodate(bio_req->bh[i]);
		else
			clear_buffer_uptodate(bio_req->bh[i]);
		unlock_buffer(bio_req->bh[i]);
	}
	kfree(bio_req);
}

static int bh_is_optional(struct squashfs_read_request *req, int idx)
{
	int start_idx, end_idx;
	struct squashfs_sb_info *msblk = req->sb->s_fs_info;

	start_idx = (idx * msblk->devblksize - req->offset) / PAGE_CACHE_SIZE;
	end_idx = ((idx + 1) * msblk->devblksize - req->offset + 1) / PAGE_CACHE_SIZE;
	if (start_idx >= req->output->pages)
		return 1;
	if (start_idx < 0)
		start_idx = end_idx;
	if (end_idx >= req->output->pages)
		end_idx = start_idx;
	return !req->output->page[start_idx] && !req->output->page[end_idx];
}

static int actor_getblks(struct squashfs_read_request *req, u64 block)
{
	int i;

	req->bh = kmalloc_array(req->nr_buffers, sizeof(*(req->bh)), GFP_NOIO);
	if (!req->bh)
		return -ENOMEM;

	for (i = 0; i < req->nr_buffers; ++i) {
		/*
		 * When dealing with an uncompressed block, the actor may
		 * contains NULL pages. There's no need to read the buffers
		 * associated with these pages.
		 */
		if (!req->compressed && bh_is_optional(req, i)) {
			req->bh[i] = NULL;
			continue;
		}
		req->bh[i] = sb_getblk(req->sb, block + i);
		if (!req->bh[i]) {
			while (--i) {
				if (req->bh[i])
					put_bh(req->bh[i]);
			}
			return -1;
		}
	}
	return 0;
}

static int squashfs_bio_submit(struct squashfs_read_request *req)
{
	struct bio *bio = NULL;
	struct buffer_head *bh;
	struct squashfs_bio_request *bio_req = NULL;
	int b = 0, prev_block = 0;
	struct squashfs_sb_info *msblk = req->sb->s_fs_info;

	u64 read_start = round_down(req->index, msblk->devblksize);
	u64 read_end = round_up(req->index + req->length, msblk->devblksize);
	sector_t block = read_start >> msblk->devblksize_log2;
	sector_t block_end = read_end >> msblk->devblksize_log2;
	int offset = read_start - round_down(req->index, PAGE_SIZE);
	int nr_buffers = block_end - block;
	int blksz = msblk->devblksize;
	int bio_max_pages = nr_buffers > BIO_MAX_PAGES ? BIO_MAX_PAGES
						       : nr_buffers;

	/* Setup the request */
	req->read_end = read_end;
	req->offset = req->index - read_start;
	req->nr_buffers = nr_buffers;
	if (actor_getblks(req, block) < 0)
		goto getblk_failed;

	/* Create and submit the BIOs */
	for (b = 0; b < nr_buffers; ++b, offset += blksz) {
		bh = req->bh[b];
		if (!bh || !trylock_buffer(bh))
			continue;
		if (buffer_uptodate(bh)) {
			unlock_buffer(bh);
			continue;
		}
		offset %= PAGE_SIZE;

		/* Append the buffer to the current BIO if it is contiguous */
		if (bio && bio_req && prev_block + 1 == b) {
			if (bio_add_page(bio, bh->b_page, blksz, offset)) {
				bio_req->nr_buffers += 1;
				prev_block = b;
				continue;
			}
		}

		/* Otherwise, submit the current BIO and create a new one */
		if (bio)
			submit_bio(READ, bio);
		bio_req = kcalloc(1, sizeof(struct squashfs_bio_request),
				  GFP_NOIO);
		if (!bio_req)
			goto req_alloc_failed;
		bio_req->bh = &req->bh[b];
		bio = bio_alloc(GFP_NOIO, bio_max_pages);
		if (!bio)
			goto bio_alloc_failed;
		bio->bi_bdev = req->sb->s_bdev;
		bio->bi_iter.bi_sector = (block + b)
				       << (msblk->devblksize_log2 - 9);
		bio->bi_private = bio_req;
		bio->bi_end_io = squashfs_bio_end_io;

		bio_add_page(bio, bh->b_page, blksz, offset);
		bio_req->nr_buffers += 1;
		prev_block = b;
	}
	if (bio)
		submit_bio(READ, bio);

	if (req->synchronous)
		squashfs_process_blocks(req);
	else {
		INIT_WORK(&req->offload, read_wq_handler);
		schedule_work(&req->offload);
	}
	return 0;

bio_alloc_failed:
	kfree(bio_req);
req_alloc_failed:
	unlock_buffer(bh);
	while (--nr_buffers >= b)
		if (req->bh[nr_buffers])
			put_bh(req->bh[nr_buffers]);
	while (--b >= 0)
		if (req->bh[b])
			wait_on_buffer(req->bh[b]);
getblk_failed:
	free_read_request(req, -ENOMEM);
	return -ENOMEM;
}

static int read_metadata_block(struct squashfs_read_request *req,
			       u64 *next_index)
{
	int ret, error, bytes_read = 0, bytes_uncompressed = 0;
	struct squashfs_sb_info *msblk = req->sb->s_fs_info;

	if (req->index + 2 > msblk->bytes_used) {
		free_read_request(req, -EINVAL);
		return -EINVAL;
	}
	req->length = 2;

	/* Do not read beyond the end of the device */
	if (req->index + req->length > msblk->bytes_used)
		req->length = msblk->bytes_used - req->index;
	req->data_processing = SQUASHFS_METADATA;

	/*
	 * Reading metadata is always synchronous because we don't know the
	 * length in advance and the function is expected to update
	 * 'next_index' and return the length.
	 */
	req->synchronous = true;
	req->res = &error;
	req->bytes_read = &bytes_read;
	req->bytes_uncompressed = &bytes_uncompressed;

	TRACE("Metadata block @ 0x%llx, %scompressed size %d, src size %d\n",
	      req->index, req->compressed ? "" : "un", bytes_read,
	      req->output->length);

	ret = squashfs_bio_submit(req);
	if (ret)
		return ret;
	if (error)
		return error;
	if (next_index)
		*next_index += 2 + bytes_read;
	return bytes_uncompressed;
}

static int read_data_block(struct squashfs_read_request *req, int length,
			   u64 *next_index, bool synchronous)
{
	int ret, error = 0, bytes_uncompressed = 0, bytes_read = 0;

	req->compressed = SQUASHFS_COMPRESSED_BLOCK(length);
	req->length = length = SQUASHFS_COMPRESSED_SIZE_BLOCK(length);
	req->data_processing = req->compressed ? SQUASHFS_DECOMPRESS
					       : SQUASHFS_COPY;

	req->synchronous = synchronous;
	if (synchronous) {
		req->res = &error;
		req->bytes_read = &bytes_read;
		req->bytes_uncompressed = &bytes_uncompressed;
	}

	TRACE("Data block @ 0x%llx, %scompressed size %d, src size %d\n",
	      req->index, req->compressed ? "" : "un", req->length,
	      req->output->length);

	ret = squashfs_bio_submit(req);
	if (ret)
		return ret;
	if (synchronous)
		ret = error ? error : bytes_uncompressed;
	if (next_index)
		*next_index += length;
	return ret;
}

/*
 * Read and decompress a metadata block or datablock.  Length is non-zero
 * if a datablock is being read (the size is stored elsewhere in the
 * filesystem), otherwise the length is obtained from the first two bytes of
 * the metadata block.  A bit in the length field indicates if the block
 * is stored uncompressed in the filesystem (usually because compression
 * generated a larger block - this does occasionally happen with compression
 * algorithms).
 */
static int __squashfs_read_data(struct super_block *sb, u64 index, int length,
	u64 *next_index, struct squashfs_page_actor *output, bool sync)
{
	struct squashfs_read_request *req;

	req = kcalloc(1, sizeof(struct squashfs_read_request), GFP_KERNEL);
	if (!req) {
		if (!sync)
			squashfs_page_actor_free(output, -ENOMEM);
		return -ENOMEM;
	}

	req->sb = sb;
	req->index = index;
	req->output = output;

	if (next_index)
		*next_index = index;

	if (length)
		length = read_data_block(req, length, next_index, sync);
	else
		length = read_metadata_block(req, next_index);

	if (length < 0) {
		ERROR("squashfs_read_data failed to read block 0x%llx\n",
		      (unsigned long long)index);
		return -EIO;
	}

	return length;
}

int squashfs_read_data(struct super_block *sb, u64 index, int length,
	u64 *next_index, struct squashfs_page_actor *output)
{
	return __squashfs_read_data(sb, index, length, next_index, output,
				    true);
}

int squashfs_read_data_async(struct super_block *sb, u64 index, int length,
	u64 *next_index, struct squashfs_page_actor *output)
{

	return __squashfs_read_data(sb, index, length, next_index, output,
				    false);
}
