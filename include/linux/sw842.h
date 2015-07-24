#ifndef __SW842_H__
#define __SW842_H__

#define SW842_MEM_COMPRESS	(0xf000)

int sw842_compress(const u8 *src, unsigned int srclen,
		   u8 *dst, unsigned int *destlen, void *wmem);

int sw842_decompress(const u8 *src, unsigned int srclen,
		     u8 *dst, unsigned int *destlen);

#endif
