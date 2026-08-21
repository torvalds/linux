/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Transparent compression codec interface.
 *
 * Copyright (c) 2026 LG Electronics Co., Ltd.
 */

#ifndef _NTFS_CODEC_H
#define _NTFS_CODEC_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>

struct compress_context;

enum ntfs_codec_id {
	NTFS_CODEC_LZNT1,
#ifdef CONFIG_NTFS_FS_WOF_COMPRESSION
	NTFS_CODEC_XPRESS4K,
	NTFS_CODEC_XPRESS8K,
	NTFS_CODEC_XPRESS16K,
	NTFS_CODEC_LZX32K,
#endif
};

struct ntfs_codec_ops {
	enum ntfs_codec_id id;
	const char *name;
	size_t (*scratch_size)(u32 chunk_size);
	int (*decompress_chunk)(void *scratch,
				const void *src, size_t src_len,
				void *dst, size_t dst_len,
				u32 chunk_size);
	int (*decompress_pages)(struct page *dest_pages[],
				int completed_pages[],
				int *dest_index, int *dest_ofs,
				int dest_max_index, int dest_max_ofs,
				int xpage, char *xpage_done,
				u8 *cb_start, u32 cb_size,
				loff_t i_size, s64 initialized_size);
	int (*compress_subblock)(struct compress_context *pctx,
				 const char *inbuf, int bufsize, char *outbuf);
};

extern const struct ntfs_codec_ops ntfs_lznt1_codec_ops;

#endif /* _NTFS_CODEC_H */
