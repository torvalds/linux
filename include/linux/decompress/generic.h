#ifndef DECOMPRESS_GENERIC_H
#define DECOMPRESS_GENERIC_H

typedef int (*decompress_fn) (unsigned char *inbuf, long len,
			      long (*fill)(void*, unsigned long),
			      long (*flush)(void*, unsigned long),
			      unsigned char *outbuf,
			      long *posp,
			      void(*error)(char *x));

/* inbuf   - input buffer
 *len     - len of pre-read data in inbuf
 *fill    - function to fill inbuf when empty
 *flush   - function to write out outbuf
 *outbuf  - output buffer
 *posp    - if non-null, input position (number of bytes read) will be
 *	  returned here
 *
 *If len != 0, inbuf should contain all the necessary input data, and fill
 *should be NULL
 *If len = 0, inbuf can be NULL, in which case the decompressor will allocate
 *the input buffer.  If inbuf != NULL it must be at least XXX_IOBUF_SIZE bytes.
 *fill will be called (repeatedly...) to read data, at most XXX_IOBUF_SIZE
 *bytes should be read per call.  Replace XXX with the appropriate decompressor
 *name, i.e. LZMA_IOBUF_SIZE.
 *
 *If flush = NULL, outbuf must be large enough to buffer all the expected
 *output.  If flush != NULL, the output buffer will be allocated by the
 *decompressor (outbuf = NULL), and the flush function will be called to
 *flush the output buffer at the appropriate time (decompressor and stream
 *dependent).
 */


/* Utility routine to detect the decompression method */
decompress_fn decompress_method(const unsigned char *inbuf, long len,
				const char **name);

#endif
