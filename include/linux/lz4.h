#ifndef __LZ4_H__
#define __LZ4_H__
/*
 * LZ4 Kernel Interface
 *
 * Copyright (C) 2013, LG Electronics, Kyungsik Lee <kyungsik.lee@lge.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * lz4_compressbound()
 * Provides the maximum size that LZ4 may output in a "worst case" scenario
 * (input data not compressible)
 */
static inline size_t lz4_compressbound(size_t isize)
{
	return isize + (isize / 255) + 16;
}

/*
 * lz4_decompress()
 *	src     : source address of the compressed data
 *	src_len : is the input size, whcih is returned after decompress done
 *	dest	: output buffer address of the decompressed data
 *	actual_dest_len: is the size of uncompressed data, supposing it's known
 *	return  : Success if return 0
 *		  Error if return (< 0)
 *	note :  Destination buffer must be already allocated.
 *		slightly faster than lz4_decompress_unknownoutputsize()
 */
int lz4_decompress(const char *src, size_t *src_len, char *dest,
		size_t actual_dest_len);

/*
 * lz4_decompress_unknownoutputsize()
 *	src     : source address of the compressed data
 *	src_len : is the input size, therefore the compressed size
 *	dest	: output buffer address of the decompressed data
 *	dest_len: is the max size of the destination buffer, which is
 *			returned with actual size of decompressed data after
 *			decompress done
 *	return  : Success if return 0
 *		  Error if return (< 0)
 *	note :  Destination buffer must be already allocated.
 */
int lz4_decompress_unknownoutputsize(const char *src, size_t src_len,
		char *dest, size_t *dest_len);
#endif
