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
#define LZ4_MEM_COMPRESS	(16384)
#define LZ4HC_MEM_COMPRESS	(262144 + (2 * sizeof(unsigned char *)))

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
 * lz4_compress()
 *	src     : source address of the original data
 *	src_len : size of the original data
 *	dst	: output buffer address of the compressed data
 *		This requires 'dst' of size LZ4_COMPRESSBOUND.
 *	dst_len : is the output size, which is returned after compress done
 *	workmem : address of the working memory.
 *		This requires 'workmem' of size LZ4_MEM_COMPRESS.
 *	return  : Success if return 0
 *		  Error if return (< 0)
 *	note :  Destination buffer and workmem must be already allocated with
 *		the defined size.
 */
int lz4_compress(const unsigned char *src, size_t src_len,
		unsigned char *dst, size_t *dst_len, void *wrkmem);

 /*
  * lz4hc_compress()
  *	 src	 : source address of the original data
  *	 src_len : size of the original data
  *	 dst	 : output buffer address of the compressed data
  *		This requires 'dst' of size LZ4_COMPRESSBOUND.
  *	 dst_len : is the output size, which is returned after compress done
  *	 workmem : address of the working memory.
  *		This requires 'workmem' of size LZ4HC_MEM_COMPRESS.
  *	 return  : Success if return 0
  *		   Error if return (< 0)
  *	 note :  Destination buffer and workmem must be already allocated with
  *		 the defined size.
  */
int lz4hc_compress(const unsigned char *src, size_t src_len,
		unsigned char *dst, size_t *dst_len, void *wrkmem);

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
int lz4_decompress(const unsigned char *src, size_t *src_len,
		unsigned char *dest, size_t actual_dest_len);

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
int lz4_decompress_unknownoutputsize(const unsigned char *src, size_t src_len,
		unsigned char *dest, size_t *dest_len);
#endif
