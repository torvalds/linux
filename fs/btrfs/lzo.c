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

#define WORKSPACE_BUF_LENGTH	(lzo1x_worst_compress(PAGE_SIZE))
#define WORKSPACE_CBUF_LENGTH	(lzo1x_worst_compress(PAGE_SIZE))

struct workspace {
	void *mem;
	void *buf;	/* where decompressed data goes */
	void *cbuf;	/* where compressed data goes */
	struct list_head list;
};

static struct workspace_manager wsm;

void lzo_free_workspace(struct list_head *ws)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);

	kvfree(workspace->buf);
	kvfree(workspace->cbuf);
	kvfree(workspace->mem);
	kfree(workspace);
}

struct list_head *lzo_alloc_workspace(unsigned int level)
{
	struct workspace *workspace;

	workspace = kzalloc(sizeof(*workspace), GFP_KERNEL);
	if (!workspace)
		return ERR_PTR(-ENOMEM);

	workspace->mem = kvmalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL | __GFP_NOWARN);
	workspace->buf = kvmalloc(WORKSPACE_BUF_LENGTH, GFP_KERNEL | __GFP_NOWARN);
	workspace->cbuf = kvmalloc(WORKSPACE_CBUF_LENGTH, GFP_KERNEL | __GFP_NOWARN);
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
 * Will do:
 *
 * - Write a segment header into the destination
 * - Copy the compressed buffer into the destination
 * - Make sure we have enough space in the last sector to fit a segment header
 *   If not, we will pad at most (LZO_LEN (4)) - 1 bytes of zeros.
 *
 * Will allocate new pages when needed.
 */
static int copy_compressed_data_to_page(char *compressed_data,
					size_t compressed_size,
					struct folio **out_folios,
					unsigned long max_nr_folio,
					u32 *cur_out,
					const u32 sectorsize)
{
	u32 sector_bytes_left;
	u32 orig_out;
	struct folio *cur_folio;
	char *kaddr;

	if ((*cur_out / PAGE_SIZE) >= max_nr_folio)
		return -E2BIG;

	/*
	 * We never allow a segment header crossing sector boundary, previous
	 * run should ensure we have enough space left inside the sector.
	 */
	ASSERT((*cur_out / sectorsize) == (*cur_out + LZO_LEN - 1) / sectorsize);

	cur_folio = out_folios[*cur_out / PAGE_SIZE];
	/* Allocate a new page */
	if (!cur_folio) {
		cur_folio = btrfs_alloc_compr_folio();
		if (!cur_folio)
			return -ENOMEM;
		out_folios[*cur_out / PAGE_SIZE] = cur_folio;
	}

	kaddr = kmap_local_folio(cur_folio, 0);
	write_compress_length(kaddr + offset_in_page(*cur_out),
			      compressed_size);
	*cur_out += LZO_LEN;

	orig_out = *cur_out;

	/* Copy compressed data */
	while (*cur_out - orig_out < compressed_size) {
		u32 copy_len = min_t(u32, sectorsize - *cur_out % sectorsize,
				     orig_out + compressed_size - *cur_out);

		kunmap_local(kaddr);

		if ((*cur_out / PAGE_SIZE) >= max_nr_folio)
			return -E2BIG;

		cur_folio = out_folios[*cur_out / PAGE_SIZE];
		/* Allocate a new page */
		if (!cur_folio) {
			cur_folio = btrfs_alloc_compr_folio();
			if (!cur_folio)
				return -ENOMEM;
			out_folios[*cur_out / PAGE_SIZE] = cur_folio;
		}
		kaddr = kmap_local_folio(cur_folio, 0);

		memcpy(kaddr + offset_in_page(*cur_out),
		       compressed_data + *cur_out - orig_out, copy_len);

		*cur_out += copy_len;
	}

	/*
	 * Check if we can fit the next segment header into the remaining space
	 * of the sector.
	 */
	sector_bytes_left = round_up(*cur_out, sectorsize) - *cur_out;
	if (sector_bytes_left >= LZO_LEN || sector_bytes_left == 0)
		goto out;

	/* The remaining size is not enough, pad it with zeros */
	memset(kaddr + offset_in_page(*cur_out), 0,
	       sector_bytes_left);
	*cur_out += sector_bytes_left;

out:
	kunmap_local(kaddr);
	return 0;
}

int lzo_compress_folios(struct list_head *ws, struct address_space *mapping,
			u64 start, struct folio **folios, unsigned long *out_folios,
			unsigned long *total_in, unsigned long *total_out)
{
	struct workspace *workspace = list_entry(ws, struct workspace, list);
	const u32 sectorsize = inode_to_fs_info(mapping->host)->sectorsize;
	struct folio *folio_in = NULL;
	char *sizes_ptr;
	const unsigned long max_nr_folio = *out_folios;
	int ret = 0;
	/* Points to the file offset of input data */
	u64 cur_in = start;
	/* Points to the current output byte */
	u32 cur_out = 0;
	u32 len = *total_out;

	ASSERT(max_nr_folio > 0);
	*out_folios = 0;
	*total_out = 0;
	*total_in = 0;

	/*
	 * Skip the header for now, we will later come back and write the total
	 * compressed size
	 */
	cur_out += LZO_LEN;
	while (cur_in < start + len) {
		char *data_in;
		const u32 sectorsize_mask = sectorsize - 1;
		u32 sector_off = (cur_in - start) & sectorsize_mask;
		u32 in_len;
		size_t out_len;

		/* Get the input page first */
		if (!folio_in) {
			ret = btrfs_compress_filemap_get_folio(mapping, cur_in, &folio_in);
			if (ret < 0)
				goto out;
		}

		/* Compress at most one sector of data each time */
		in_len = min_t(u32, start + len - cur_in, sectorsize - sector_off);
		ASSERT(in_len);
		data_in = kmap_local_folio(folio_in, 0);
		ret = lzo1x_1_compress(data_in +
				       offset_in_page(cur_in), in_len,
				       workspace->cbuf, &out_len,
				       workspace->mem);
		kunmap_local(data_in);
		if (unlikely(ret < 0)) {
			/* lzo1x_1_compress never fails. */
			ret = -EIO;
			goto out;
		}

		ret = copy_compressed_data_to_page(workspace->cbuf, out_len,
						   folios, max_nr_folio,
						   &cur_out, sectorsize);
		if (ret < 0)
			goto out;

		cur_in += in_len;

		/*
		 * Check if we're making it bigger after two sectors.  And if
		 * it is so, give up.
		 */
		if (cur_in - start > sectorsize * 2 && cur_in - start < cur_out) {
			ret = -E2BIG;
			goto out;
		}

		/* Check if we have reached page boundary */
		if (PAGE_ALIGNED(cur_in)) {
			folio_put(folio_in);
			folio_in = NULL;
		}
	}

	/* Store the size of all chunks of compressed data */
	sizes_ptr = kmap_local_folio(folios[0], 0);
	write_compress_length(sizes_ptr, cur_out);
	kunmap_local(sizes_ptr);

	ret = 0;
	*total_out = cur_out;
	*total_in = cur_in - start;
out:
	if (folio_in)
		folio_put(folio_in);
	*out_folios = DIV_ROUND_UP(cur_out, PAGE_SIZE);
	return ret;
}

/*
 * Copy the compressed segment payload into @dest.
 *
 * For the payload there will be no padding, just need to do page switching.
 */
static void copy_compressed_segment(struct compressed_bio *cb,
				    char *dest, u32 len, u32 *cur_in)
{
	u32 orig_in = *cur_in;

	while (*cur_in < orig_in + len) {
		struct folio *cur_folio;
		u32 copy_len = min_t(u32, PAGE_SIZE - offset_in_page(*cur_in),
					  orig_in + len - *cur_in);

		ASSERT(copy_len);
		cur_folio = cb->compressed_folios[*cur_in / PAGE_SIZE];

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
	char *kaddr;
	int ret;
	/* Compressed data length, can be unaligned */
	u32 len_in;
	/* Offset inside the compressed data */
	u32 cur_in = 0;
	/* Bytes decompressed so far */
	u32 cur_out = 0;

	kaddr = kmap_local_folio(cb->compressed_folios[0], 0);
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
		cur_folio = cb->compressed_folios[cur_in / PAGE_SIZE];
		ASSERT(cur_folio);
		kaddr = kmap_local_folio(cur_folio, 0);
		seg_len = read_compress_length(kaddr + offset_in_page(cur_in));
		kunmap_local(kaddr);
		cur_in += LZO_LEN;

		if (unlikely(seg_len > WORKSPACE_CBUF_LENGTH)) {
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
		copy_compressed_segment(cb, workspace->cbuf, seg_len, &cur_in);

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
	size_t max_segment_len = WORKSPACE_BUF_LENGTH;
	int ret = 0;

	if (srclen < LZO_LEN || srclen > max_segment_len + LZO_LEN * 2)
		return -EUCLEAN;

	in_len = read_compress_length(data_in);
	if (in_len != srclen)
		return -EUCLEAN;
	data_in += LZO_LEN;

	in_len = read_compress_length(data_in);
	if (in_len != srclen - LZO_LEN * 2) {
		ret = -EUCLEAN;
		goto out;
	}
	data_in += LZO_LEN;

	out_len = sectorsize;
	ret = lzo1x_decompress_safe(data_in, in_len, workspace->buf, &out_len);
	if (unlikely(ret != LZO_E_OK)) {
		struct btrfs_inode *inode = folio_to_inode(dest_folio);

		btrfs_err(fs_info,
		"lzo decompression failed, error %d root %llu inode %llu offset %llu",
			  ret, btrfs_root_id(inode->root), btrfs_ino(inode),
			  folio_pos(dest_folio));
		ret = -EIO;
		goto out;
	}

	ASSERT(out_len <= sectorsize);
	memcpy_to_folio(dest_folio, dest_pgoff, workspace->buf, out_len);
	/* Early end, considered as an error. */
	if (unlikely(out_len < destlen)) {
		ret = -EIO;
		folio_zero_range(dest_folio, dest_pgoff + out_len, destlen - out_len);
	}
out:
	return ret;
}

const struct btrfs_compress_op btrfs_lzo_compress = {
	.workspace_manager	= &wsm,
	.max_level		= 1,
	.default_level		= 1,
};
