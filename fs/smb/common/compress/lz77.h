/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2024-2026, SUSE LLC
 *
 * Authors: Enzo Matsumiya <ematsumiya@suse.de>
 *
 * Implementation of the LZ77 "plain" compression algorithm, as per MS-XCA spec.
 */
#ifndef _SMB_COMPRESS_LZ77_H
#define _SMB_COMPRESS_LZ77_H

#include <linux/kernel.h>

/**
 * smb_lz77_compressed_alloc_size() - Compute compressed buffer size.
 * @size:	uncompressed (src) size
 *
 * Compute allocation size for the compressed buffer based on uncompressed size.
 * Accounts for metadata and overprovision for the worst case scenario.
 *
 * LZ77 metadata is a 4-byte flag that is written:
 * - on dst begin (pos 0)
 * - every 32 literals or matches
 * - on end-of-stream (possibly, if last write was another flag)
 *
 * Worst case scenario is an all-literal compression, which means:
 * metadata bytes = 4 + ((@size / 32) * 4) + 4, or, simplified, (@size >> 3) + 8
 *
 * The worst case scenario rarely happens, but such overprovisioning also
 * allows smb_lz77_compress() main loop to run without ever bound checking dst,
 * which is a huge perf improvement, while also being safe when compression goes
 * bad.
 *
 * Return: required (*) allocation size for compressed buffer.
 *
 * (*) checked once in the beginning of smb_lz77_compress()
 */
static __always_inline u32 smb_lz77_compressed_alloc_size(const u32 size)
{
	return size + (size >> 3) + 8;
}

int smb_lz77_compress(const void *src, const u32 slen, void *dst, u32 *dlen);
int smb_lz77_decompress(const void *src, const u32 slen, void *dst,
			const u32 dlen);
#endif /* _SMB_COMPRESS_LZ77_H */
