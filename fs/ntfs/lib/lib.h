/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * lib.h - Public declarations for the LZX and XPRESS decompressors.
 *
 * Adapted for the linux kernel.  These are the low-level decompressor
 * allocations; WOF (system-compressed) access goes through the
 * ntfs_codec_ops interface declared in "../ntfs_codec.h".
 */

#ifndef _LINUX_NTFS_LIB_LIB_H
#define _LINUX_NTFS_LIB_LIB_H

#include <linux/types.h>

/* globals from xpress_decompress.c */
struct xpress_decompressor *xpress_allocate_decompressor(void);
void xpress_free_decompressor(struct xpress_decompressor *d);
int xpress_decompress(struct xpress_decompressor *d,
		      const void *compressed_data, size_t compressed_size,
		      void *uncompressed_data, size_t uncompressed_size);

/* globals from lzx_decompress.c */
struct lzx_decompressor *lzx_allocate_decompressor(void);
void lzx_free_decompressor(struct lzx_decompressor *d);
int lzx_decompress(struct lzx_decompressor *d, const void *compressed_data,
		   size_t compressed_size, void *uncompressed_data,
		   size_t uncompressed_size);

#endif /* _LINUX_NTFS_LIB_LIB_H */
