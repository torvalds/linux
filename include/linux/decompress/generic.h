#ifndef DECOMPRESS_GENERIC_H
#define DECOMPRESS_GENERIC_H

/* Minimal chunksize to be read.
 *Bzip2 prefers at least 4096
 *Lzma prefers 0x10000 */
#define COMPR_IOBUF_SIZE	4096

typedef int (*decompress_fn) (unsigned char *inbuf, int len,
			      int(*fill)(void*, unsigned int),
			      int(*writebb)(void*, unsigned int),
			      unsigned char *output,
			      int *posp,
			      void(*error)(char *x));

/* inbuf   - input buffer
 *len     - len of pre-read data in inbuf
 *fill    - function to fill inbuf if empty
 *writebb - function to write out outbug
 *posp    - if non-null, input position (number of bytes read) will be
 *	  returned here
 *
 *If len != 0, the inbuf is initialized (with as much data), and fill
 *should not be called
 *If len = 0, the inbuf is allocated, but empty. Its size is IOBUF_SIZE
 *fill should be called (repeatedly...) to read data, at most IOBUF_SIZE
 */

/* Utility routine to detect the decompression method */
decompress_fn decompress_method(const unsigned char *inbuf, int len,
				const char **name);

#endif
