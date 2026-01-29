// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2008 Oracle.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/bio.h>
#include <linux/lzo.h>
#include <linux/refcount.h>
#include "messages.h"
#include "compression.h"
#include "ctree.h"
#include "super.h"
#include "btrfs_inode.h"

#define LZO_LEN	4

/*
 * Btrfs LZO compression format
 *
 * Regular and inlined LZO compressed data extents consist of:
 *
 * 1.  Header
 *     Fixed size. LZO_LEN (4) bytes long, LE32.
 *     Records the total size (including the header) of compressed data.
 *
 * 2.  Segment(s)
 *     Variable size. Each segment includes one segment header, followed by data
 *     payload.
 *     One regular LZO compressed extent can have one or more segments.
 *     For inlined LZO compressed extent, only one segment is allowed.
 *     One segment represents at most one sector of uncompressed data.
 *
 * 2.1 Segment header
 *     Fixed size. LZO_LEN (4) bytes long, LE32.
 *     Records the total size of the segment (not including the header).
 *     Segment header never crosses sector boundary, thus it's possible to
 *     have at most 3 padding zeros at the end of the sector.
 *
 * 2.2 Data Payload
 *     Variable size. Size up limit should be lzo1x_worst_compress(sectorsize)
 *     which is 4419 for a 4KiB sectorsize.
 *
 * Example with 4K sectorsize:
 * Page 1:
 *          0     0x2   0x4   0x6   0x8   0xa   0xc   0xe     0x10
 * 0x0000   |  Header   | SegHdr 01 | Data payload 01 ...     |
 * ...
 * 0x0ff0   | SegHdr  N | Data payload  N     ...          |00|
 *                                                          ^^ padding zeros
 * Page 2:
 * 0x1000   | SegHdr N+1| Data payload N+1 ...                |
 */

struct workspace {
	void *mem;
	void *buf;	/* where decompressed data goes */
	void *cbuf;	/* where compressed data goes */
	struct list_head list;
};

static u32 workspace_buf_length(const struct btrfs_fs_info *fs_info)
{
	return lzo1x_worst_compress(fs_info->sectorsize);
}
static u32 workspace_cbuf_length(const struct btrfs_fs_info *fs_info)
{
	return lzo1x_worst_compress(fs_info->sectorsize);
}

void lzo_free_workspace(struct list_head *ws)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);

	kvfree(workspace->buf);
	kvfree(workspace->cbuf);
	kvfree(workspace->mem);
	kfree(workspace);
}

struct list_head *lzo_alloc_workspace(struct btrfs_fs_info *fs_info)
{
	struct workspace *workspace;

	workspace = kzalloc(sizeof(*workspace), GFP_KERNEL);
	if (!workspace)
		return ERR_PTR(-ENOMEM);

	workspace->mem = kvmalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL | __GFP_NOWARN);
	workspace->buf = kvmalloc(workspace_buf_length(fs_info), GFP_KERNEL | __GFP_NOWARN);
	workspace->cbuf = kvmalloc(workspace_cbuf_length(fs_info), GFP_KERNEL | __GFP_NOWARN);
	if (!workspace->mem || !workspace->buf || !workspace->cbuf)
		goto fail;

	INIT_LIST_HEAD(&workspace->list);

	return &workspace->list;
fail:
	lzo_free_workspace(&workspace->list);
	return ERR_PTR(-ENOMEM);
}

static inline void write_compress_length(char *buf, size_t len)
{
	__le32 dlen;

	dlen = cpu_to_le32(len);
	memcpy(buf, &dlen, LZO_LEN);
}

static inline size_t read_compress_length(const char *buf)
{
	__le32 dlen;

	memcpy(&dlen, buf, LZO_LEN);
	return le32_to_cpu(dlen);
}

/*
 * Write data into @out_folio and queue it into @out_bio.
 *
 * Return 0 if everything is fine and @total_out will be increased.
 * Return <0 for error.
 *
 * The @out_folio can be NULL after a full folio is queued.
 * Thus the caller should check and allocate a new folio when needed.
 */
static int write_and_queue_folio(struct bio *out_bio, struct folio **out_folio,
				 u32 *total_out, u32 write_len)
{
	const u32 fsize = folio_size(*out_folio);
	const u32 foffset = offset_in_folio(*out_folio, *total_out);

	ASSERT(out_folio && *out_folio);
	/* Should not cross folio boundary. */
	ASSERT(foffset + write_len <= fsize);

	/* We can not use bio_add_folio_nofail() which doesn't do any merge. */
	if (!bio_add_folio(out_bio, *out_folio, write_len, foffset)) {
		/*
		 * We have allocated a bio that havs BTRFS_MAX_COMPRESSED_PAGES
		 * vecs, and all ranges inside the same folio should have been
		 * merged.  If bio_add_folio() still failed, that means we have
		 * reached the bvec limits.
		 *
		 * This should only happen at the beginning of a folio, and
		 * caller is responsible for releasing the folio, since it's
		 * not yet queued into the bio.
		 */
		ASSERT(IS_ALIGNED(*total_out, fsize));
		return -E2BIG;
	}

	*total_out += write_len;
	/*
	 * The full folio has been filled and queued, reset @out_folio to NULL,
	 * so that error handling is fully handled by the bio.
	 */
	if (IS_ALIGNED(*total_out, fsize))
		*out_folio = NULL;
	return 0;
}

/*
 * Copy compressed data to bio.
 *
 * @out_bio:		The bio that will contain all the compressed data.
 * @compressed_data:	The compressed data of this segment.
 * @compressed_size:	The size of the compressed data.
 * @out_folio:		The current output folio, will be updated if a new
 *			folio is allocated.
 * @total_out:		The total bytes of current output.
 * @max_out:		The maximum size of the compressed data.
 *
 * Will do:
 *
 * - Write a segment header into the destination
 * - Copy the compressed buffer into the destination
 * - Make sure we have enough space in the last sector to fit a segment header
 *   If not, we will pad at most (LZO_LEN (4)) - 1 bytes of zeros.
 * - If a full folio is filled, it will be queued into @out_bio, and @out_folio
 *   will be updated.
 *
 * Will allocate new pages when needed.
 */
static int copy_compressed_data_to_bio(struct btrfs_fs_info *fs_info,
				       struct bio *out_bio,
				       const char *compressed_data,
				       size_t compressed_size,
				       struct folio **out_folio,
				       u32 *total_out, u32 max_out)
{
	const u32 sectorsize = fs_info->sectorsize;
	const u32 sectorsize_bits = fs_info->sectorsize_bits;
	const u32 fsize = btrfs_min_folio_size(fs_info);
	const u32 old_size = out_bio->bi_iter.bi_size;
	u32 copy_start;
	u32 sector_bytes_left;
	char *kaddr;
	int ret;

	ASSERT(out_folio);

	/* There should be at least a lzo header queued. */
	ASSERT(old_size);
	ASSERT(old_size == *total_out);

	/*
	 * We never allow a segment header crossing sector boundary, previous
	 * run should ensure we have enough space left inside the sector.
	 */
	ASSERT((old_size >> sectorsize_bits) == (old_size + LZO_LEN - 1) >> sectorsize_bits);

	if (!*out_folio) {
		*out_folio = btrfs_alloc_compr_folio(fs_info);
		if (!*out_folio)
			return -ENOMEM;
	}

	/* Write the segment header first. */
	kaddr = kmap_local_folio(*out_folio, offset_in_folio(*out_folio, *total_out));
	write_compress_length(kaddr, compressed_size);
	kunmap_local(kaddr);
	ret = write_and_queue_folio(out_bio, out_folio, total_out, LZO_LEN);
	if (ret < 0)
		return ret;

	copy_start = *total_out;

	/* Copy compressed data. */
	while (*total_out - copy_start < compressed_size) {
		u32 copy_len = min_t(u32, sectorsize - *total_out % sectorsize,
				     copy_start + compressed_size - *total_out);
		u32 foffset = *total_out & (fsize - 1);

		/* With the range copied, we're larger than the original range. */
		if (((*total_out + copy_len) >> sectorsize_bits) >=
		    max_out >> sectorsize_bits)
			return -E2BIG;

		if (!*out_folio) {
			*out_folio = btrfs_alloc_compr_folio(fs_info);
			if (!*out_folio)
				return -ENOMEM;
		}

		kaddr = kmap_local_folio(*out_folio, foffset);
		memcpy(kaddr, compressed_data + *total_out - copy_start, copy_len);
		kunmap_local(kaddr);
		ret = write_and_queue_folio(out_bio, out_folio, total_out, copy_len);
		if (ret < 0)
			return ret;
	}

	/*
	 * Check if we can fit the next segment header into the remaining space
	 * of the sector.
	 */
	sector_bytes_left = round_up(*total_out, sectorsize) - *total_out;
	if (sector_bytes_left >= LZO_LEN || sector_bytes_left == 0)
		return 0;

	ASSERT(*out_folio);

	/* The remaining size is not enough, pad it with zeros */
	folio_zero_range(*out_folio, offset_in_folio(*out_folio, *total_out), sector_bytes_left);
	return write_and_queue_folio(out_bio, out_folio, total_out, sector_bytes_left);
}

int lzo_compress_bio(struct list_head *ws, struct compressed_bio *cb)
{
	struct btrfs_inode *inode = cb->bbio.inode;
	struct btrfs_fs_info *fs_info = inode->root->fs_info;
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	struct bio *bio = &cb->bbio.bio;
	const u64 start = cb->start;
	const u32 len = cb->len;
	const u32 sectorsize = fs_info->sectorsize;
	const u32 min_folio_size = btrfs_min_folio_size(fs_info);
	struct address_space *mapping = inode->vfs_inode.i_mapping;
	struct folio *folio_in = NULL;
	struct folio *folio_out = NULL;
	char *sizes_ptr;
	int ret = 0;
	/* Points to the file offset of input data. */
	u64 cur_in = start;
	/* Points to the current output byte. */
	u32 total_out = 0;

	ASSERT(bio->bi_iter.bi_size == 0);
	ASSERT(len);

	folio_out = btrfs_alloc_compr_folio(fs_info);
	if (!folio_out)
		return -ENOMEM;

	/* Queue a segment header first. */
	ret = write_and_queue_folio(bio, &folio_out, &total_out, LZO_LEN);
	/* The first header should not fail. */
	ASSERT(ret == 0);

	while (cur_in < start + len) {
		char *data_in;
		const u32 sectorsize_mask = sectorsize - 1;
		u32 sector_off = (cur_in - start) & sectorsize_mask;
		u32 in_len;
		size_t out_len;

		/* Get the input page first. */
		if (!folio_in) {
			ret = btrfs_compress_filemap_get_folio(mapping, cur_in, &folio_in);
			if (ret < 0)
				goto out;
		}

		/* Compress at most one sector of data each time. */
		in_len = min_t(u32, start + len - cur_in, sectorsize - sector_off);
		ASSERT(in_len);
		data_in = kmap_local_folio(folio_in, offset_in_folio(folio_in, cur_in));
		ret = lzo1x_1_compress(data_in, in_len, workspace->cbuf, &out_len,
				       workspace->mem);
		kunmap_local(data_in);
		if (unlikely(ret < 0)) {
			/* lzo1x_1_compress never fails. */
			ret = -EIO;
			goto out;
		}

		ret = copy_compressed_data_to_bio(fs_info, bio, workspace->cbuf, out_len,
						  &folio_out, &total_out, len);
		if (ret < 0)
			goto out;

		cur_in += in_len;

		/*
		 * Check if we're making it bigger after two sectors.  And if
		 * it is so, give up.
		 */
		if (cur_in - start > sectorsize * 2 && cur_in - start < total_out) {
			ret = -E2BIG;
			goto out;
		}

		/* Check if we have reached input folio boundary. */
		if (IS_ALIGNED(cur_in, min_folio_size)) {
			folio_put(folio_in);
			folio_in = NULL;
		}
	}
	/*
	 * The last folio is already queued. Bio is responsible for freeing
	 * those folios now.
	 */
	folio_out = NULL;

	/* Store the size of all chunks of compressed data */
	sizes_ptr = kmap_local_folio(bio_first_folio_all(bio), 0);
	write_compress_length(sizes_ptr, total_out);
	kunmap_local(sizes_ptr);
out:
	/*
	 * We can only free the folio that has no part queued into the bio.
	 *
	 * As any folio that is already queued into bio will be released by
	 * the endio function of bio.
	 */
	if (folio_out && IS_ALIGNED(total_out, min_folio_size)) {
		btrfs_free_compr_folio(folio_out);
		folio_out = NULL;
	}
	if (folio_in)
		folio_put(folio_in);
	return ret;
}

static struct folio *get_current_folio(struct compressed_bio *cb, struct folio_iter *fi,
				       u32 *cur_folio_index, u32 cur_in)
{
	struct btrfs_fs_info *fs_info = cb_to_fs_info(cb);
	const u32 min_folio_shift = PAGE_SHIFT + fs_info->block_min_order;

	ASSERT(cur_folio_index);

	/* Need to switch to the next folio. */
	if (cur_in >> min_folio_shift != *cur_folio_index) {
		/* We can only do the switch one folio a time. */
		ASSERT(cur_in >> min_folio_shift == *cur_folio_index + 1);

		bio_next_folio(fi, &cb->bbio.bio);
		(*cur_folio_index)++;
	}
	return fi->folio;
}

/*
 * Copy the compressed segment payload into @dest.
 *
 * For the payload there will be no padding, just need to do page switching.
 */
static void copy_compressed_segment(struct compressed_bio *cb,
				    struct folio_iter *fi, u32 *cur_folio_index,
				    char *dest, u32 len, u32 *cur_in)
{
	u32 orig_in = *cur_in;

	while (*cur_in < orig_in + len) {
		struct folio *cur_folio = get_current_folio(cb, fi, cur_folio_index, *cur_in);
		u32 copy_len;

		ASSERT(cur_folio);
		copy_len = min_t(u32, orig_in + len - *cur_in,
				 folio_size(cur_folio) - offset_in_folio(cur_folio, *cur_in));
		ASSERT(copy_len);

		memcpy_from_folio(dest + *cur_in - orig_in, cur_folio,
				  offset_in_folio(cur_folio, *cur_in), copy_len);

		*cur_in += copy_len;
	}
}

int lzo_decompress_bio(struct list_head *ws, struct compressed_bio *cb)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	const struct btrfs_fs_info *fs_info = cb->bbio.inode->root->fs_info;
	const u32 sectorsize = fs_info->sectorsize;
	struct folio_iter fi;
	char *kaddr;
	int ret;
	/* Compressed data length, can be unaligned */
	u32 len_in;
	/* Offset inside the compressed data */
	u32 cur_in = 0;
	/* Bytes decompressed so far */
	u32 cur_out = 0;
	/* The current folio index number inside the bio. */
	u32 cur_folio_index = 0;

	bio_first_folio(&fi, &cb->bbio.bio, 0);
	/* There must be a compressed folio and matches the sectorsize. */
	if (unlikely(!fi.folio))
		return -EINVAL;
	ASSERT(folio_size(fi.folio) == sectorsize);
	kaddr = kmap_local_folio(fi.folio, 0);
	len_in = read_compress_length(kaddr);
	kunmap_local(kaddr);
	cur_in += LZO_LEN;

	/*
	 * LZO header length check
	 *
	 * The total length should not exceed the maximum extent length,
	 * and all sectors should be used.
	 * If this happens, it means the compressed extent is corrupted.
	 */
	if (unlikely(len_in > min_t(size_t, BTRFS_MAX_COMPRESSED, cb->compressed_len) ||
		     round_up(len_in, sectorsize) < cb->compressed_len)) {
		struct btrfs_inode *inode = cb->bbio.inode;

		btrfs_err(fs_info,
"lzo header invalid, root %llu inode %llu offset %llu lzo len %u compressed len %u",
			  btrfs_root_id(inode->root), btrfs_ino(inode),
			  cb->start, len_in, cb->compressed_len);
		return -EUCLEAN;
	}

	/* Go through each lzo segment */
	while (cur_in < len_in) {
		struct folio *cur_folio;
		/* Length of the compressed segment */
		u32 seg_len;
		u32 sector_bytes_left;
		size_t out_len = lzo1x_worst_compress(sectorsize);

		/*
		 * We should always have enough space for one segment header
		 * inside current sector.
		 */
		ASSERT(cur_in / sectorsize ==
		       (cur_in + LZO_LEN - 1) / sectorsize);
		cur_folio = get_current_folio(cb, &fi, &cur_folio_index, cur_in);
		ASSERT(cur_folio);
		kaddr = kmap_local_folio(cur_folio, 0);
		seg_len = read_compress_length(kaddr + offset_in_folio(cur_folio, cur_in));
		kunmap_local(kaddr);
		cur_in += LZO_LEN;

		if (unlikely(seg_len > workspace_cbuf_length(fs_info))) {
			struct btrfs_inode *inode = cb->bbio.inode;

			/*
			 * seg_len shouldn't be larger than we have allocated
			 * for workspace->cbuf
			 */
			btrfs_err(fs_info,
			"lzo segment too big, root %llu inode %llu offset %llu len %u",
				  btrfs_root_id(inode->root), btrfs_ino(inode),
				  cb->start, seg_len);
			return -EIO;
		}

		/* Copy the compressed segment payload into workspace */
		copy_compressed_segment(cb, &fi, &cur_folio_index, workspace->cbuf,
					seg_len, &cur_in);

		/* Decompress the data */
		ret = lzo1x_decompress_safe(workspace->cbuf, seg_len,
					    workspace->buf, &out_len);
		if (unlikely(ret != LZO_E_OK)) {
			struct btrfs_inode *inode = cb->bbio.inode;

			btrfs_err(fs_info,
		"lzo decompression failed, error %d root %llu inode %llu offset %llu",
				  ret, btrfs_root_id(inode->root), btrfs_ino(inode),
				  cb->start);
			return -EIO;
		}

		/* Copy the data into inode pages */
		ret = btrfs_decompress_buf2page(workspace->buf, out_len, cb, cur_out);
		cur_out += out_len;

		/* All data read, exit */
		if (ret == 0)
			return 0;
		ret = 0;

		/* Check if the sector has enough space for a segment header */
		sector_bytes_left = sectorsize - (cur_in % sectorsize);
		if (sector_bytes_left >= LZO_LEN)
			continue;

		/* Skip the padding zeros */
		cur_in += sector_bytes_left;
	}

	return 0;
}

int lzo_decompress(struct list_head *ws, const u8 *data_in,
		struct folio *dest_folio, unsigned long dest_pgoff, size_t srclen,
		size_t destlen)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	struct btrfs_fs_info *fs_info = folio_to_fs_info(dest_folio);
	const u32 sectorsize = fs_info->sectorsize;
	size_t in_len;
	size_t out_len;
	size_t max_segment_len = workspace_buf_length(fs_info);
	int ret;

	if (unlikely(srclen < LZO_LEN || srclen > max_segment_len + LZO_LEN * 2))
		return -EUCLEAN;

	in_len = read_compress_length(data_in);
	if (unlikely(in_len != srclen))
		return -EUCLEAN;
	data_in += LZO_LEN;

	in_len = read_compress_length(data_in);
	if (unlikely(in_len != srclen - LZO_LEN * 2))
		return -EUCLEAN;
	data_in += LZO_LEN;

	out_len = sectorsize;
	ret = lzo1x_decompress_safe(data_in, in_len, workspace->buf, &out_len);
	if (unlikely(ret != LZO_E_OK)) {
		struct btrfs_inode *inode = folio_to_inode(dest_folio);

		btrfs_err(fs_info,
		"lzo decompression failed, error %d root %llu inode %llu offset %llu",
			  ret, btrfs_root_id(inode->root), btrfs_ino(inode),
			  folio_pos(dest_folio));
		return -EIO;
	}

	ASSERT(out_len <= sectorsize);
	memcpy_to_folio(dest_folio, dest_pgoff, workspace->buf, out_len);
	/* Early end, considered as an error. */
	if (unlikely(out_len < destlen)) {
		folio_zero_range(dest_folio, dest_pgoff + out_len, destlen - out_len);
		return -EIO;
	}

	return 0;
}

const struct btrfs_compress_levels  btrfs_lzo_compress = {
	.max_level		= 1,
	.default_level		= 1,
};
