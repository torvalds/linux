/* SPDX-License-Identifier: 0BSD */

/*
 * XZ decompressor
 *
 * Authors: Lasse Collin <lasse.collin@tukaani.org>
 *          Igor Pavlov <https://7-zip.org/>
 */

#ifndef XZ_H
#define XZ_H

#ifdef __KERNEL__
#	include <linux/stddef.h>
#	include <linux/types.h>
#else
#	include <stddef.h>
#	include <stdint.h>
#endif

/**
 * enum xz_mode - Operation mode
 *
 * @XZ_SINGLE:              Single-call mode. This uses less RAM than
 *                          multi-call modes, because the LZMA2
 *                          dictionary doesn't need to be allocated as
 *                          part of the decoder state. All required data
 *                          structures are allocated at initialization,
 *                          so xz_dec_run() cannot return XZ_MEM_ERROR.
 * @XZ_PREALLOC:            Multi-call mode with preallocated LZMA2
 *                          dictionary buffer. All data structures are
 *                          allocated at initialization, so xz_dec_run()
 *                          cannot return XZ_MEM_ERROR.
 * @XZ_DYNALLOC:            Multi-call mode. The LZMA2 dictionary is
 *                          allocated once the required size has been
 *                          parsed from the stream headers. If the
 *                          allocation fails, xz_dec_run() will return
 *                          XZ_MEM_ERROR.
 *
 * It is possible to enable support only for a subset of the above
 * modes at compile time by defining XZ_DEC_SINGLE, XZ_DEC_PREALLOC,
 * or XZ_DEC_DYNALLOC. The xz_dec kernel module is always compiled
 * with support for all operation modes, but the preboot code may
 * be built with fewer features to minimize code size.
 */
enum xz_mode {
	XZ_SINGLE,
	XZ_PREALLOC,
	XZ_DYNALLOC
};

/**
 * enum xz_ret - Return codes
 * @XZ_OK:                  Everything is OK so far. More input or more
 *                          output space is required to continue. This
 *                          return code is possible only in multi-call mode
 *                          (XZ_PREALLOC or XZ_DYNALLOC).
 * @XZ_STREAM_END:          Operation finished successfully.
 * @XZ_UNSUPPORTED_CHECK:   Integrity check type is not supported. Decoding
 *                          is still possible in multi-call mode by simply
 *                          calling xz_dec_run() again.
 *                          Note that this return value is used only if
 *                          XZ_DEC_ANY_CHECK was defined at build time,
 *                          which is not used in the kernel. Unsupported
 *                          check types return XZ_OPTIONS_ERROR if
 *                          XZ_DEC_ANY_CHECK was not defined at build time.
 * @XZ_MEM_ERROR:           Allocating memory failed. This return code is
 *                          possible only if the decoder was initialized
 *                          with XZ_DYNALLOC. The amount of memory that was
 *                          tried to be allocated was no more than the
 *                          dict_max argument given to xz_dec_init().
 * @XZ_MEMLIMIT_ERROR:      A bigger LZMA2 dictionary would be needed than
 *                          allowed by the dict_max argument given to
 *                          xz_dec_init(). This return value is possible
 *                          only in multi-call mode (XZ_PREALLOC or
 *                          XZ_DYNALLOC); the single-call mode (XZ_SINGLE)
 *                          ignores the dict_max argument.
 * @XZ_FORMAT_ERROR:        File format was not recognized (wrong magic
 *                          bytes).
 * @XZ_OPTIONS_ERROR:       This implementation doesn't support the requested
 *                          compression options. In the decoder this means
 *                          that the header CRC32 matches, but the header
 *                          itself specifies something that we don't support.
 * @XZ_DATA_ERROR:          Compressed data is corrupt.
 * @XZ_BUF_ERROR:           Cannot make any progress. Details are slightly
 *                          different between multi-call and single-call
 *                          mode; more information below.
 *
 * In multi-call mode, XZ_BUF_ERROR is returned when two consecutive calls
 * to XZ code cannot consume any input and cannot produce any new output.
 * This happens when there is no new input available, or the output buffer
 * is full while at least one output byte is still pending. Assuming your
 * code is not buggy, you can get this error only when decoding a compressed
 * stream that is truncated or otherwise corrupt.
 *
 * In single-call mode, XZ_BUF_ERROR is returned only when the output buffer
 * is too small or the compressed input is corrupt in a way that makes the
 * decoder produce more output than the caller expected. When it is
 * (relatively) clear that the compressed input is truncated, XZ_DATA_ERROR
 * is used instead of XZ_BUF_ERROR.
 */
enum xz_ret {
	XZ_OK,
	XZ_STREAM_END,
	XZ_UNSUPPORTED_CHECK,
	XZ_MEM_ERROR,
	XZ_MEMLIMIT_ERROR,
	XZ_FORMAT_ERROR,
	XZ_OPTIONS_ERROR,
	XZ_DATA_ERROR,
	XZ_BUF_ERROR
};

/**
 * struct xz_buf - Passing input and output buffers to XZ code
 * @in:         Beginning of the input buffer. This may be NULL if and only
 *              if in_pos is equal to in_size.
 * @in_pos:     Current position in the input buffer. This must not exceed
 *              in_size.
 * @in_size:    Size of the input buffer
 * @out:        Beginning of the output buffer. This may be NULL if and only
 *              if out_pos is equal to out_size.
 * @out_pos:    Current position in the output buffer. This must not exceed
 *              out_size.
 * @out_size:   Size of the output buffer
 *
 * Only the contents of the output buffer from out[out_pos] onward, and
 * the variables in_pos and out_pos are modified by the XZ code.
 */
struct xz_buf {
	const uint8_t *in;
	size_t in_pos;
	size_t in_size;

	uint8_t *out;
	size_t out_pos;
	size_t out_size;
};

/*
 * struct xz_dec - Opaque type to hold the XZ decoder state
 */
struct xz_dec;

/**
 * xz_dec_init() - Allocate and initialize a XZ decoder state
 * @mode:       Operation mode
 * @dict_max:   Maximum size of the LZMA2 dictionary (history buffer) for
 *              multi-call decoding. This is ignored in single-call mode
 *              (mode == XZ_SINGLE). LZMA2 dictionary is always 2^n bytes
 *              or 2^n + 2^(n-1) bytes (the latter sizes are less common
 *              in practice), so other values for dict_max don't make sense.
 *              In the kernel, dictionary sizes of 64 KiB, 128 KiB, 256 KiB,
 *              512 KiB, and 1 MiB are probably the only reasonable values,
 *              except for kernel and initramfs images where a bigger
 *              dictionary can be fine and useful.
 *
 * Single-call mode (XZ_SINGLE): xz_dec_run() decodes the whole stream at
 * once. The caller must provide enough output space or the decoding will
 * fail. The output space is used as the dictionary buffer, which is why
 * there is no need to allocate the dictionary as part of the decoder's
 * internal state.
 *
 * Because the output buffer is used as the workspace, streams encoded using
 * a big dictionary are not a problem in single-call mode. It is enough that
 * the output buffer is big enough to hold the actual uncompressed data; it
 * can be smaller than the dictionary size stored in the stream headers.
 *
 * Multi-call mode with preallocated dictionary (XZ_PREALLOC): dict_max bytes
 * of memory is preallocated for the LZMA2 dictionary. This way there is no
 * risk that xz_dec_run() could run out of memory, since xz_dec_run() will
 * never allocate any memory. Instead, if the preallocated dictionary is too
 * small for decoding the given input stream, xz_dec_run() will return
 * XZ_MEMLIMIT_ERROR. Thus, it is important to know what kind of data will be
 * decoded to avoid allocating excessive amount of memory for the dictionary.
 *
 * Multi-call mode with dynamically allocated dictionary (XZ_DYNALLOC):
 * dict_max specifies the maximum allowed dictionary size that xz_dec_run()
 * may allocate once it has parsed the dictionary size from the stream
 * headers. This way excessive allocations can be avoided while still
 * limiting the maximum memory usage to a sane value to prevent running the
 * system out of memory when decompressing streams from untrusted sources.
 *
 * On success, xz_dec_init() returns a pointer to struct xz_dec, which is
 * ready to be used with xz_dec_run(). If memory allocation fails,
 * xz_dec_init() returns NULL.
 */
struct xz_dec *xz_dec_init(enum xz_mode mode, uint32_t dict_max);

/**
 * xz_dec_run() - Run the XZ decoder
 * @s:          Decoder state allocated using xz_dec_init()
 * @b:          Input and output buffers
 *
 * The possible return values depend on build options and operation mode.
 * See enum xz_ret for details.
 *
 * Note that if an error occurs in single-call mode (return value is not
 * XZ_STREAM_END), b->in_pos and b->out_pos are not modified and the
 * contents of the output buffer from b->out[b->out_pos] onward are
 * undefined. This is true even after XZ_BUF_ERROR, because with some filter
 * chains, there may be a second pass over the output buffer, and this pass
 * cannot be properly done if the output buffer is truncated. Thus, you
 * cannot give the single-call decoder a too small buffer and then expect to
 * get that amount valid data from the beginning of the stream. You must use
 * the multi-call decoder if you don't want to uncompress the whole stream.
 */
enum xz_ret xz_dec_run(struct xz_dec *s, struct xz_buf *b);

/**
 * xz_dec_reset() - Reset an already allocated decoder state
 * @s:          Decoder state allocated using xz_dec_init()
 *
 * This function can be used to reset the multi-call decoder state without
 * freeing and reallocating memory with xz_dec_end() and xz_dec_init().
 *
 * In single-call mode, xz_dec_reset() is always called in the beginning of
 * xz_dec_run(). Thus, explicit call to xz_dec_reset() is useful only in
 * multi-call mode.
 */
void xz_dec_reset(struct xz_dec *s);

/**
 * xz_dec_end() - Free the memory allocated for the decoder state
 * @s:          Decoder state allocated using xz_dec_init(). If s is NULL,
 *              this function does nothing.
 */
void xz_dec_end(struct xz_dec *s);

/**
 * DOC: MicroLZMA decompressor
 *
 * This MicroLZMA header format was created for use in EROFS but may be used
 * by others too. **In most cases one needs the XZ APIs above instead.**
 *
 * The compressed format supported by this decoder is a raw LZMA stream
 * whose first byte (always 0x00) has been replaced with bitwise-negation
 * of the LZMA properties (lc/lp/pb) byte. For example, if lc/lp/pb is
 * 3/0/2, the first byte is 0xA2. This way the first byte can never be 0x00.
 * Just like with LZMA2, lc + lp <= 4 must be true. The LZMA end-of-stream
 * marker must not be used. The unused values are reserved for future use.
 */

/*
 * struct xz_dec_microlzma - Opaque type to hold the MicroLZMA decoder state
 */
struct xz_dec_microlzma;

/**
 * xz_dec_microlzma_alloc() - Allocate memory for the MicroLZMA decoder
 * @mode:       XZ_SINGLE or XZ_PREALLOC
 * @dict_size:  LZMA dictionary size. This must be at least 4 KiB and
 *              at most 3 GiB.
 *
 * In contrast to xz_dec_init(), this function only allocates the memory
 * and remembers the dictionary size. xz_dec_microlzma_reset() must be used
 * before calling xz_dec_microlzma_run().
 *
 * The amount of allocated memory is a little less than 30 KiB with XZ_SINGLE.
 * With XZ_PREALLOC also a dictionary buffer of dict_size bytes is allocated.
 *
 * On success, xz_dec_microlzma_alloc() returns a pointer to
 * struct xz_dec_microlzma. If memory allocation fails or
 * dict_size is invalid, NULL is returned.
 */
struct xz_dec_microlzma *xz_dec_microlzma_alloc(enum xz_mode mode,
						uint32_t dict_size);

/**
 * xz_dec_microlzma_reset() - Reset the MicroLZMA decoder state
 * @s:          Decoder state allocated using xz_dec_microlzma_alloc()
 * @comp_size:  Compressed size of the input stream
 * @uncomp_size:  Uncompressed size of the input stream. A value smaller
 *              than the real uncompressed size of the input stream can
 *              be specified if uncomp_size_is_exact is set to false.
 *              uncomp_size can never be set to a value larger than the
 *              expected real uncompressed size because it would eventually
 *              result in XZ_DATA_ERROR.
 * @uncomp_size_is_exact:  This is an int instead of bool to avoid
 *              requiring stdbool.h. This should normally be set to true.
 *              When this is set to false, error detection is weaker.
 */
void xz_dec_microlzma_reset(struct xz_dec_microlzma *s, uint32_t comp_size,
			    uint32_t uncomp_size, int uncomp_size_is_exact);

/**
 * xz_dec_microlzma_run() - Run the MicroLZMA decoder
 * @s:          Decoder state initialized using xz_dec_microlzma_reset()
 * @b:          Input and output buffers
 *
 * This works similarly to xz_dec_run() with a few important differences.
 * Only the differences are documented here.
 *
 * The only possible return values are XZ_OK, XZ_STREAM_END, and
 * XZ_DATA_ERROR. This function cannot return XZ_BUF_ERROR: if no progress
 * is possible due to lack of input data or output space, this function will
 * keep returning XZ_OK. Thus, the calling code must be written so that it
 * will eventually provide input and output space matching (or exceeding)
 * comp_size and uncomp_size arguments given to xz_dec_microlzma_reset().
 * If the caller cannot do this (for example, if the input file is truncated
 * or otherwise corrupt), the caller must detect this error by itself to
 * avoid an infinite loop.
 *
 * If the compressed data seems to be corrupt, XZ_DATA_ERROR is returned.
 * This can happen also when incorrect dictionary, uncompressed, or
 * compressed sizes have been specified.
 *
 * With XZ_PREALLOC only: As an extra feature, b->out may be NULL to skip over
 * uncompressed data. This way the caller doesn't need to provide a temporary
 * output buffer for the bytes that will be ignored.
 *
 * With XZ_SINGLE only: In contrast to xz_dec_run(), the return value XZ_OK
 * is also possible and thus XZ_SINGLE is actually a limited multi-call mode.
 * After XZ_OK the bytes decoded so far may be read from the output buffer.
 * It is possible to continue decoding but the variables b->out and b->out_pos
 * MUST NOT be changed by the caller. Increasing the value of b->out_size is
 * allowed to make more output space available; one doesn't need to provide
 * space for the whole uncompressed data on the first call. The input buffer
 * may be changed normally like with XZ_PREALLOC. This way input data can be
 * provided from non-contiguous memory.
 */
enum xz_ret xz_dec_microlzma_run(struct xz_dec_microlzma *s, struct xz_buf *b);

/**
 * xz_dec_microlzma_end() - Free the memory allocated for the decoder state
 * @s:          Decoder state allocated using xz_dec_microlzma_alloc().
 *              If s is NULL, this function does nothing.
 */
void xz_dec_microlzma_end(struct xz_dec_microlzma *s);

/*
 * Standalone build (userspace build or in-kernel build for boot time use)
 * needs a CRC32 implementation. For normal in-kernel use, kernel's own
 * CRC32 module is used instead, and users of this module don't need to
 * care about the functions below.
 */
#ifndef XZ_INTERNAL_CRC32
#	ifdef __KERNEL__
#		define XZ_INTERNAL_CRC32 0
#	else
#		define XZ_INTERNAL_CRC32 1
#	endif
#endif

#if XZ_INTERNAL_CRC32
/*
 * This must be called before any other xz_* function to initialize
 * the CRC32 lookup table.
 */
void xz_crc32_init(void);

/*
 * Update CRC32 value using the polynomial from IEEE-802.3. To start a new
 * calculation, the third argument must be zero. To continue the calculation,
 * the previously returned value is passed as the third argument.
 */
uint32_t xz_crc32(const uint8_t *buf, size_t size, uint32_t crc);
#endif
#endif
