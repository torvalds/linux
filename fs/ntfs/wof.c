// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Windows System Compression (WOF) decompression glue.
 *
 * Copyright (c) 2026 LG Electronics Co., Ltd.
 */

#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/overflow.h>
#include <linux/pagemap.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/unaligned.h>
#include <linux/vmalloc.h>

#include "ntfs.h"
#include "inode.h"
#include "debug.h"
#include "ntfs_codec.h"
#include "attrib.h"

static const __le16 WOF_NAME[] = {
	cpu_to_le16('W'), cpu_to_le16('o'), cpu_to_le16('f'),
	cpu_to_le16('C'), cpu_to_le16('o'), cpu_to_le16('m'),
	cpu_to_le16('p'), cpu_to_le16('r'), cpu_to_le16('e'),
	cpu_to_le16('s'), cpu_to_le16('s'), cpu_to_le16('e'),
	cpu_to_le16('d'), cpu_to_le16('D'), cpu_to_le16('a'),
	cpu_to_le16('t'), cpu_to_le16('a'),
};

#define WOF_NAME_LEN 17

#define NTFS_WOF_MAX_COMP_UNIT (1U << 15)
#define NTFS_WOF_MAX_PAGES \
	DIV_ROUND_UP(NTFS_WOF_MAX_COMP_UNIT + PAGE_SIZE - 1, PAGE_SIZE)

struct ntfs_wof_workspace {
	struct mutex *lock;
	const struct ntfs_codec_ops *codec;
	u32 comp_unit;
	void *input;
	size_t input_size;
	void *output;
	void *scratch;
};

static DEFINE_MUTEX(ntfs_wof_xpress4k_lock);
static DEFINE_MUTEX(ntfs_wof_xpress8k_lock);
static DEFINE_MUTEX(ntfs_wof_xpress16k_lock);
static DEFINE_MUTEX(ntfs_wof_lzx32k_lock);

static struct ntfs_wof_workspace ntfs_wof_xpress4k_workspace = {
	.lock = &ntfs_wof_xpress4k_lock,
	.codec = &ntfs_xpress4k_codec_ops,
	.comp_unit = 1U << 12,
};

static struct ntfs_wof_workspace ntfs_wof_xpress8k_workspace = {
	.lock = &ntfs_wof_xpress8k_lock,
	.codec = &ntfs_xpress8k_codec_ops,
	.comp_unit = 1U << 13,
};

static struct ntfs_wof_workspace ntfs_wof_xpress16k_workspace = {
	.lock = &ntfs_wof_xpress16k_lock,
	.codec = &ntfs_xpress16k_codec_ops,
	.comp_unit = 1U << 14,
};

static struct ntfs_wof_workspace ntfs_wof_lzx32k_workspace = {
	.lock = &ntfs_wof_lzx32k_lock,
	.codec = &ntfs_lzx32k_codec_ops,
	.comp_unit = 1U << 15,
};

static struct ntfs_wof_workspace *const ntfs_wof_workspaces[] = {
	&ntfs_wof_xpress4k_workspace,
	&ntfs_wof_xpress8k_workspace,
	&ntfs_wof_xpress16k_workspace,
	&ntfs_wof_lzx32k_workspace,
};

static struct ntfs_wof_workspace *ntfs_wof_workspace(u8 block_size_bits)
{
	switch (block_size_bits) {
	case 12:
		return &ntfs_wof_xpress4k_workspace;
	case 13:
		return &ntfs_wof_xpress8k_workspace;
	case 14:
		return &ntfs_wof_xpress16k_workspace;
	case 15:
		return &ntfs_wof_lzx32k_workspace;
	default:
		return NULL;
	}
}

static int ntfs_wof_workspace_prepare(struct ntfs_wof_workspace *ws)
{
	void *input, *output, *scratch;
	size_t scratch_size;

	if (ws->input)
		return 0;

	ws->input_size = round_up((size_t)ws->comp_unit + 511, 512);
	scratch_size = ws->codec->scratch_size(ws->comp_unit);
	if (!scratch_size)
		return -EINVAL;

	input = kvmalloc(ws->input_size, GFP_NOFS);
	output = kvmalloc(ws->comp_unit, GFP_NOFS);
	scratch = kvzalloc(scratch_size, GFP_NOFS);
	if (!input || !output || !scratch) {
		kvfree(input);
		kvfree(output);
		kvfree(scratch);
		return -ENOMEM;
	}

	ws->input = input;
	ws->output = output;
	ws->scratch = scratch;
	return 0;
}

void ntfs_wof_free_workspaces(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(ntfs_wof_workspaces); i++) {
		struct ntfs_wof_workspace *ws = ntfs_wof_workspaces[i];

		mutex_lock(ws->lock);
		kvfree(ws->input);
		kvfree(ws->output);
		kvfree(ws->scratch);
		ws->input = NULL;
		ws->output = NULL;
		ws->scratch = NULL;
		mutex_unlock(ws->lock);
	}
}

static int ntfs_bdev_read_from_rl(struct ntfs_volume *vol,
				  struct runlist *runlist,
				  sector_t start_sector, u64 sector_count,
				  void *buf)
{
	struct runlist_element *rl;
	u32 sec_per_clu_bits;
	s64 vcn;
	u64 sec_off;
	size_t buf_off = 0;
	unsigned int nofs_flags;
	int err;

	if (vol->cluster_size_bits < 9)
		return -EINVAL;
	sec_per_clu_bits = vol->cluster_size_bits - 9;
	vcn = start_sector >> sec_per_clu_bits;
	sec_off = start_sector & ((1ULL << sec_per_clu_bits) - 1);

	nofs_flags = memalloc_nofs_save();
	down_read(&runlist->lock);
	if (!runlist->rl) {
		err = -EINVAL;
		goto out_unlock;
	}

	rl = __ntfs_attr_find_vcn_nolock(runlist, vcn);
	if (IS_ERR(rl)) {
		err = PTR_ERR(rl);
		goto out_unlock;
	}

	while (sector_count > 0) {
		s64 lcn;
		s64 rl_end;
		u64 byte_off, byte_len, sectors, available;

		if (rl->length <= 0 || vcn < rl->vcn) {
			err = -EINVAL;
			goto out_unlock;
		}

		lcn = ntfs_rl_vcn_to_lcn(rl, vcn);
		if (lcn < 0 && lcn != LCN_HOLE) {
			err = -EINVAL;
			goto out_unlock;
		}

		if (check_add_overflow(rl->vcn, rl->length, &rl_end) ||
		    rl_end <= vcn ||
		    (u64)(rl_end - vcn) > (U64_MAX >> sec_per_clu_bits)) {
			err = -EOVERFLOW;
			goto out_unlock;
		}
		available = (u64)(rl_end - vcn) << sec_per_clu_bits;
		if (available <= sec_off) {
			err = -EINVAL;
			goto out_unlock;
		}
		available -= sec_off;
		sectors = min_t(u64, sector_count, available);
		if (check_mul_overflow(sectors, (u64)SECTOR_SIZE, &byte_len) ||
		    byte_len > SIZE_MAX - buf_off) {
			err = -EOVERFLOW;
			goto out_unlock;
		}

		if (lcn == LCN_HOLE) {
			memset((u8 *)buf + buf_off, 0, byte_len);
		} else {
			byte_off = ntfs_cluster_to_bytes(vol, lcn);
			if (check_add_overflow(byte_off, sec_off << 9,
					       &byte_off) ||
			    byte_off > S64_MAX) {
				err = -EOVERFLOW;
				goto out_unlock;
			}
			err = ntfs_bdev_read(vol->sb->s_bdev,
					     (char *)buf + buf_off,
					     (loff_t)byte_off, byte_len);
			if (err)
				goto out_unlock;
		}

		buf_off += byte_len;
		sector_count -= sectors;
		rl++;
		vcn = rl->vcn;
		sec_off = 0;
	}

	err = 0;
out_unlock:
	up_read(&runlist->lock);
	memalloc_nofs_restore(nofs_flags);
	return err;
}

static int parse_wof_chunk_table(struct ntfs_inode *base_ni,
				 struct ntfs_inode *ni, u64 chunk_idx,
				 u64 chunk_count, u32 decomp_size,
				 u64 *chunk_offset, u32 *chunk_size,
				 void *table_buf, size_t table_buf_size)
{
	u8 bytes_per_off;
	u8 *buf;
	u64 off[2];
	u64 byte_off, chunk_data_size, table_size;
	u32 bytes_to_read;
	int ret = 0;

	if (i_size_read(VFS_I(base_ni)) < (1ULL << 32))
		bytes_per_off = sizeof(__le32);
	else
		bytes_per_off = sizeof(__le64);

	if (!chunk_count || chunk_idx >= chunk_count)
		return -EINVAL;

	table_size = (chunk_count - 1) * bytes_per_off;
	if (ni->data_size < 0 || (u64)ni->data_size < table_size)
		return -EINVAL;
	chunk_data_size = (u64)ni->data_size - table_size;

	if (chunk_count == 1) {
		if (chunk_data_size > decomp_size)
			return -EINVAL;
		*chunk_offset = 0;
		*chunk_size = chunk_data_size;
		goto out;
	}

	byte_off = chunk_idx ? (chunk_idx - 1) * bytes_per_off : 0;
	bytes_to_read = chunk_idx + 1 == chunk_count ?
				bytes_per_off :
				(chunk_idx ? 2 : 1) * bytes_per_off;

	if (NInoNonResident(ni)) {
		sector_t start_sector = byte_off >> 9;
		u32 sector_off = byte_off & ((1 << 9) - 1);
		u32 sectors = DIV_ROUND_UP(sector_off + bytes_to_read, 512);

		if ((size_t)sectors << 9 > table_buf_size)
			return -EINVAL;
		buf = table_buf;
		ret = ntfs_bdev_read_from_rl(ni->vol, &ni->runlist,
					     start_sector, sectors, buf);
		if (ret)
			return -EIO;
		buf += sector_off;
	} else {
		struct ntfs_attr_search_ctx *ctx;
		u32 value_length;
		u16 value_offset;

		if (bytes_to_read > table_buf_size)
			return -EINVAL;

		mutex_lock(&base_ni->mrec_lock);
		ctx = ntfs_attr_get_search_ctx(base_ni, NULL);
		if (!ctx) {
			ret = -ENOMEM;
			goto out_unlock_mrec;
		}
		ret = ntfs_attr_lookup(ni->type, ni->name, ni->name_len,
				       CASE_SENSITIVE, 0, NULL, 0, ctx);
		if (ret)
			goto out_put_ctx;

		value_length =
			le32_to_cpu(ctx->attr->data.resident.value_length);
		value_offset =
			le16_to_cpu(ctx->attr->data.resident.value_offset);
		if (byte_off + bytes_to_read > value_length) {
			ret = -EINVAL;
			goto out_put_ctx;
		}
		memcpy(table_buf, (u8 *)ctx->attr + value_offset + byte_off,
		       bytes_to_read);
		buf = table_buf;
out_put_ctx:
		ntfs_attr_put_search_ctx(ctx);
out_unlock_mrec:
		mutex_unlock(&base_ni->mrec_lock);
		if (ret)
			return ret;
	}

	if (bytes_per_off == sizeof(__le32)) {
		off[0] = chunk_idx ? get_unaligned_le32(buf) : 0;
		if (chunk_idx + 1 == chunk_count)
			off[1] = chunk_data_size;
		else if (chunk_idx)
			off[1] = get_unaligned_le32(buf + bytes_per_off);
		else
			off[1] = get_unaligned_le32(buf);
	} else {
		off[0] = chunk_idx ? get_unaligned_le64(buf) : 0;
		if (chunk_idx + 1 == chunk_count)
			off[1] = chunk_data_size;
		else if (chunk_idx)
			off[1] = get_unaligned_le64(buf + bytes_per_off);
		else
			off[1] = get_unaligned_le64(buf);
	}

	if (off[1] <= off[0] || off[1] > chunk_data_size ||
	    off[1] - off[0] > decomp_size)
		return -EINVAL;

	*chunk_offset = table_size + off[0];
	*chunk_size = off[1] - off[0];
out:
	if (!*chunk_size)
		return -EINVAL;
	return 0;
}

static int ntfs_read_wof_chunk(struct ntfs_volume *vol,
			       struct ntfs_inode *wof_ni, u64 chunk_offset,
			       u32 chunk_size, void *input, size_t input_size,
			       char **chunk_mem)
{
	struct ntfs_inode *base_ni = wof_ni->ext.base_ntfs_ino;
	struct ntfs_attr_search_ctx *ctx;
	u32 input_offset = chunk_offset & 511;
	u32 input_size_aligned;
	u32 value_length;
	u16 value_offset;
	int err;

	input_size_aligned = round_up(chunk_size + input_offset, 512);
	if (input_size_aligned > input_size)
		return -EINVAL;

	if (NInoNonResident(wof_ni)) {
		err = ntfs_bdev_read_from_rl(vol, &wof_ni->runlist,
					     chunk_offset >> 9,
					     input_size_aligned >> 9, input);
		if (err)
			return err;
		*chunk_mem = (u8 *)input + input_offset;
		return 0;
	}

	mutex_lock(&base_ni->mrec_lock);
	ctx = ntfs_attr_get_search_ctx(base_ni, NULL);
	if (!ctx) {
		err = -ENOMEM;
		goto out_unlock_mrec;
	}

	err = ntfs_attr_lookup(wof_ni->type, wof_ni->name, wof_ni->name_len,
			       CASE_SENSITIVE, 0, NULL, 0, ctx);
	if (err)
		goto out_put_ctx;

	value_length = le32_to_cpu(ctx->attr->data.resident.value_length);
	value_offset = le16_to_cpu(ctx->attr->data.resident.value_offset);
	if (chunk_offset + chunk_size > value_length) {
		err = -EINVAL;
		goto out_put_ctx;
	}
	memcpy(input, (u8 *)ctx->attr + value_offset + chunk_offset,
	       chunk_size);
	*chunk_mem = input;
out_put_ctx:
	ntfs_attr_put_search_ctx(ctx);
out_unlock_mrec:
	mutex_unlock(&base_ni->mrec_lock);
	return err;
}

struct ntfs_wof_dest {
	struct folio *folios[NTFS_WOF_MAX_PAGES];
	struct page *pages[NTFS_WOF_MAX_PAGES];
	unsigned int nr_folios;
	unsigned int nr_pages;
};

static void ntfs_wof_release_dest(struct ntfs_wof_dest *dest,
				  struct folio *target, bool success)
{
	unsigned int i;

	for (i = 0; i < dest->nr_folios; i++) {
		struct folio *folio = dest->folios[i];

		if (folio == target)
			continue;
		if (success) {
			flush_dcache_folio(folio);
			folio_mark_uptodate(folio);
		} else {
			folio_clear_uptodate(folio);
		}
		folio_unlock(folio);
		folio_put(folio);
	}
}

static int ntfs_wof_collect_dest(struct address_space *mapping,
				 struct folio *target, loff_t chunk_start,
				 loff_t chunk_end, struct ntfs_wof_dest *dest)
{
	pgoff_t index, last, page_index;
	unsigned int i;

	memset(dest, 0, sizeof(*dest));
	index = chunk_start >> PAGE_SHIFT;
	last = (chunk_end - 1) >> PAGE_SHIFT;
	while (index <= last) {
		struct folio *folio;
		pgoff_t next;
		bool is_target;

		if (folio_contains(target, index)) {
			folio = target;
			is_target = true;
		} else {
			folio = __filemap_get_folio(
				mapping, index,
				FGP_LOCK | FGP_CREAT | FGP_NOFS | FGP_NOWAIT,
				GFP_NOFS);
			if (IS_ERR(folio))
				return PTR_ERR(folio);
			is_target = false;
			if (folio_pos(folio) < chunk_start ||
			    folio_next_pos(folio) > chunk_end) {
				folio_unlock(folio);
				folio_put(folio);
				return -EAGAIN;
			}
		}

		if (dest->nr_folios == ARRAY_SIZE(dest->folios)) {
			if (!is_target) {
				folio_unlock(folio);
				folio_put(folio);
			}
			return -EINVAL;
		}
		dest->folios[dest->nr_folios++] = folio;
		next = folio->index + folio_nr_pages(folio);
		if (next <= index)
			return -EAGAIN;
		index = next;
	}

	for (page_index = chunk_start >> PAGE_SHIFT; page_index <= last;
	     page_index++) {
		struct folio *folio = NULL;

		for (i = 0; i < dest->nr_folios; i++) {
			if (folio_contains(dest->folios[i], page_index)) {
				folio = dest->folios[i];
				break;
			}
		}
		if (!folio || dest->nr_pages == ARRAY_SIZE(dest->pages))
			return -EAGAIN;
		dest->pages[dest->nr_pages++] =
			folio_page(folio, page_index - folio->index);
	}
	return 0;
}

static int ntfs_wof_decode(struct ntfs_wof_workspace *ws, const void *src,
			   u32 src_len, void *dst, u32 dst_len)
{
	if (src_len == dst_len) {
		memcpy(dst, src, dst_len);
		return 0;
	}
	return ws->codec->decompress_chunk(ws->scratch, src, src_len, dst,
					   dst_len, ws->comp_unit);
}

static int ntfs_wof_decode_page_direct(struct ntfs_wof_workspace *ws,
				       struct folio *target, loff_t chunk_start,
				       const void *src, u32 src_len,
				       u32 dst_len)
{
	unsigned int page_offset = offset_in_page(chunk_start);
	struct page *page;
	pgoff_t page_index;
	void *addr;
	int err;

	page_index = chunk_start >> PAGE_SHIFT;
	if (!folio_contains(target, page_index))
		return -EAGAIN;

	page = folio_page(target, page_index - target->index);
	addr = kmap_local_page(page);
	err = ntfs_wof_decode(ws, src, src_len, (u8 *)addr + page_offset,
			      dst_len);
	kunmap_local(addr);
	if (err)
		return -EINVAL;
	return 0;
}

static int ntfs_wof_decode_folios_direct(struct ntfs_wof_workspace *ws,
					 struct address_space *mapping,
					 struct folio *target,
					 loff_t chunk_start, loff_t chunk_end,
					 const void *src, u32 src_len,
					 u32 dst_len)
{
	unsigned int page_offset = offset_in_page(chunk_start);
	struct ntfs_wof_dest dest;
	void *addr;
	unsigned int nofs_flags;
	int err;

	err = ntfs_wof_collect_dest(mapping, target, chunk_start, chunk_end,
				    &dest);
	if (err) {
		ntfs_wof_release_dest(&dest, target, false);
		return -EAGAIN;
	}

	nofs_flags = memalloc_nofs_save();
	addr = vmap(dest.pages, dest.nr_pages, VM_MAP, PAGE_KERNEL);
	memalloc_nofs_restore(nofs_flags);
	if (!addr) {
		ntfs_wof_release_dest(&dest, target, false);
		return -EAGAIN;
	}

	err = ntfs_wof_decode(ws, src, src_len, (u8 *)addr + page_offset,
			      dst_len);
	vunmap(addr);
	if (err) {
		ntfs_wof_release_dest(&dest, target, false);
		return -EINVAL;
	}
	ntfs_wof_release_dest(&dest, target, true);
	return 0;
}

static int ntfs_wof_try_direct(struct ntfs_wof_workspace *ws,
			       struct address_space *mapping,
			       struct folio *target, loff_t chunk_start,
			       loff_t chunk_end, const void *src, u32 src_len,
			       u32 dst_len)
{
	unsigned int page_offset = offset_in_page(chunk_start);

	if (dst_len <= PAGE_SIZE - page_offset)
		return ntfs_wof_decode_page_direct(ws, target, chunk_start, src,
						   src_len, dst_len);

	return ntfs_wof_decode_folios_direct(ws, mapping, target, chunk_start,
					     chunk_end, src, src_len, dst_len);
}

int ntfs_read_wof_compressed_block(struct folio *folio)
{
	struct address_space *mapping = folio->mapping;
	struct ntfs_inode *ni = NTFS_I(mapping->host), *wof_ni;
	struct inode *wof_inode;
	struct ntfs_volume *vol = ni->vol;
	struct ntfs_wof_workspace *ws;
	loff_t i_size = i_size_read(VFS_I(ni));
	loff_t folio_start = folio_pos(folio);
	loff_t folio_end = folio_next_pos(folio);
	char *chunk_mem;
	u32 decomp_size;
	u64 chunk_count, chunk_idx, last_chunk, chunk_offset;
	int err = 0;

	ws = ntfs_wof_workspace(ni->itype.compressed.block_size_bits);
	if (!ws) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (folio_start >= i_size) {
		folio_zero_segment(folio, 0, folio_size(folio));
		goto out;
	}

	wof_inode = ntfs_attr_iget(VFS_I(ni), AT_DATA, (__le16 *)WOF_NAME,
				   WOF_NAME_LEN);
	if (IS_ERR(wof_inode)) {
		err = PTR_ERR(wof_inode);
		goto out;
	}

	wof_ni = NTFS_I(wof_inode);
	if (wof_ni->initialized_size != wof_ni->data_size) {
		ntfs_error(vol->sb,
			   "WOF compressed stream is not fully initialized (init %lld, data %lld).",
			   wof_ni->initialized_size, wof_ni->data_size);
		err = -EIO;
		goto out_iput;
	}
	if (NInoNonResident(wof_ni) && !NInoFullyMapped(wof_ni)) {
		down_write(&wof_ni->runlist.lock);
		if (!NInoFullyMapped(wof_ni))
			err = ntfs_attr_map_whole_runlist(wof_ni);
		up_write(&wof_ni->runlist.lock);
		if (err)
			goto out_iput;
	}

	mutex_lock(ws->lock);
	err = ntfs_wof_workspace_prepare(ws);
	if (err)
		goto out_unlock_ws;

	chunk_idx = div_u64(folio_start, ws->comp_unit);
	last_chunk =
		div_u64(min_t(loff_t, folio_end, i_size) - 1, ws->comp_unit);
	chunk_count = DIV_ROUND_UP_ULL(i_size, ws->comp_unit);
	for (; chunk_idx <= last_chunk; chunk_idx++) {
		u32 chunk_size;
		u64 chunk_file_offset;
		loff_t chunk_end, copy_start, copy_end;

		decomp_size = chunk_idx + 1 == chunk_count ?
				      i_size - chunk_idx * ws->comp_unit :
				      ws->comp_unit;
		err = parse_wof_chunk_table(ni, wof_ni, chunk_idx, chunk_count,
					    decomp_size, &chunk_offset,
					    &chunk_size, ws->input,
					    ws->input_size);
		if (err)
			goto out_unlock_ws;

		err = ntfs_read_wof_chunk(vol, wof_ni, chunk_offset, chunk_size,
					  ws->input, ws->input_size,
					  &chunk_mem);
		if (err)
			goto out_unlock_ws;

		chunk_file_offset = chunk_idx * ws->comp_unit;
		chunk_end = chunk_file_offset + decomp_size;
		err = ntfs_wof_try_direct(ws, mapping, folio, chunk_file_offset,
					  chunk_end, chunk_mem, chunk_size,
					  decomp_size);
		if (!err)
			continue;
		if (err != -EAGAIN)
			goto out_unlock_ws;

		err = ntfs_wof_decode(ws, chunk_mem, chunk_size, ws->output,
				      decomp_size);
		if (err) {
			ntfs_error(vol->sb, "Decompression failed: %d", err);
			err = -EINVAL;
			goto out_unlock_ws;
		}
		copy_start = max_t(loff_t, folio_start, chunk_file_offset);
		copy_end = min_t(loff_t, folio_end,
				 chunk_file_offset + decomp_size);
		memcpy_to_folio(folio, copy_start - folio_start,
				ws->output + copy_start - chunk_file_offset,
				copy_end - copy_start);
	}

	if (folio_end > i_size)
		folio_zero_segment(folio, i_size - folio_start,
				   folio_size(folio));
out_unlock_ws:
	mutex_unlock(ws->lock);
out_iput:
	iput(wof_inode);
out:
	if (!err) {
		flush_dcache_folio(folio);
		folio_mark_uptodate(folio);
	} else {
		folio_clear_uptodate(folio);
	}
	folio_unlock(folio);
	return err;
}
