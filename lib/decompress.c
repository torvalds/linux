/*
 * decompress.c
 *
 * Detect the decompression method based on magic number
 */

#include <linux/decompress/generic.h>

#include <linux/decompress/bunzip2.h>
#include <linux/decompress/unlzma.h>
#include <linux/decompress/inflate.h>

#include <linux/types.h>
#include <linux/string.h>

static const struct compress_format {
	unsigned char magic[2];
	const char *name;
	decompress_fn decompressor;
} compressed_formats[] = {
#ifdef CONFIG_DECOMPRESS_GZIP
	{ {037, 0213}, "gzip", gunzip },
	{ {037, 0236}, "gzip", gunzip },
#endif
#ifdef CONFIG_DECOMPRESS_BZIP2
	{ {0x42, 0x5a}, "bzip2", bunzip2 },
#endif
#ifdef CONFIG_DECOMPRESS_LZMA
	{ {0x5d, 0x00}, "lzma", unlzma },
#endif
	{ {0, 0}, NULL, NULL }
};

decompress_fn decompress_method(const unsigned char *inbuf, int len,
				const char **name)
{
	const struct compress_format *cf;

	if (len < 2)
		return NULL;	/* Need at least this much... */

	for (cf = compressed_formats; cf->decompressor; cf++) {
		if (!memcmp(inbuf, cf->magic, 2))
			break;

	}
	if (name)
		*name = cf->name;
	return cf->decompressor;
}
