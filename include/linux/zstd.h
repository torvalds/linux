/* SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause */
/*
 * Copyright (c) Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of https://github.com/facebook/zstd) and
 * the GPLv2 (found in the COPYING file in the root directory of
 * https://github.com/facebook/zstd). You may select, at your option, one of the
 * above-listed licenses.
 */

#ifndef LINUX_ZSTD_H
#define LINUX_ZSTD_H

/**
 * This is a kernel-style API that wraps the upstream zstd API, which cannot be
 * used directly because the symbols aren't exported. It exposes the minimal
 * functionality which is currently required by users of zstd in the kernel.
 * Expose extra functions from lib/zstd/zstd.h as needed.
 */

/* ======   Dependency   ====== */
#include <linux/types.h>
#include <linux/zstd_errors.h>
#include <linux/zstd_lib.h>

/* ======   Helper Functions   ====== */
/**
 * zstd_compress_bound() - maximum compressed size in worst case scenario
 * @src_size: The size of the data to compress.
 *
 * Return:    The maximum compressed size in the worst case scenario.
 */
size_t zstd_compress_bound(size_t src_size);

/**
 * zstd_is_error() - tells if a size_t function result is an error code
 * @code:  The function result to check for error.
 *
 * Return: Non-zero iff the code is an error.
 */
unsigned int zstd_is_error(size_t code);

/**
 * enum zstd_error_code - zstd error codes
 */
typedef ZSTD_ErrorCode zstd_error_code;

/**
 * zstd_get_error_code() - translates an error function result to an error code
 * @code:  The function result for which zstd_is_error(code) is true.
 *
 * Return: A unique error code for this error.
 */
zstd_error_code zstd_get_error_code(size_t code);

/**
 * zstd_get_error_name() - translates an error function result to a string
 * @code:  The function result for which zstd_is_error(code) is true.
 *
 * Return: An error string corresponding to the error code.
 */
const char *zstd_get_error_name(size_t code);

/**
 * zstd_min_clevel() - minimum allowed compression level
 *
 * Return: The minimum allowed compression level.
 */
int zstd_min_clevel(void);

/**
 * zstd_max_clevel() - maximum allowed compression level
 *
 * Return: The maximum allowed compression level.
 */
int zstd_max_clevel(void);

/* ======   Parameter Selection   ====== */

/**
 * enum zstd_strategy - zstd compression search strategy
 *
 * From faster to stronger. See zstd_lib.h.
 */
typedef ZSTD_strategy zstd_strategy;

/**
 * struct zstd_compression_parameters - zstd compression parameters
 * @windowLog:    Log of the largest match distance. Larger means more
 *                compression, and more memory needed during decompression.
 * @chainLog:     Fully searched segment. Larger means more compression,
 *                slower, and more memory (useless for fast).
 * @hashLog:      Dispatch table. Larger means more compression,
 *                slower, and more memory.
 * @searchLog:    Number of searches. Larger means more compression and slower.
 * @searchLength: Match length searched. Larger means faster decompression,
 *                sometimes less compression.
 * @targetLength: Acceptable match size for optimal parser (only). Larger means
 *                more compression, and slower.
 * @strategy:     The zstd compression strategy.
 *
 * See zstd_lib.h.
 */
typedef ZSTD_compressionParameters zstd_compression_parameters;

/**
 * struct zstd_frame_parameters - zstd frame parameters
 * @contentSizeFlag: Controls whether content size will be present in the
 *                   frame header (when known).
 * @checksumFlag:    Controls whether a 32-bit checksum is generated at the
 *                   end of the frame for error detection.
 * @noDictIDFlag:    Controls whether dictID will be saved into the frame
 *                   header when using dictionary compression.
 *
 * The default value is all fields set to 0. See zstd_lib.h.
 */
typedef ZSTD_frameParameters zstd_frame_parameters;

/**
 * struct zstd_parameters - zstd parameters
 * @cParams: The compression parameters.
 * @fParams: The frame parameters.
 */
typedef ZSTD_parameters zstd_parameters;

/**
 * zstd_get_params() - returns zstd_parameters for selected level
 * @level:              The compression level
 * @estimated_src_size: The estimated source size to compress or 0
 *                      if unknown.
 *
 * Return:              The selected zstd_parameters.
 */
zstd_parameters zstd_get_params(int level,
	unsigned long long estimated_src_size);

/* ======   Single-pass Compression   ====== */

typedef ZSTD_CCtx zstd_cctx;

/**
 * zstd_cctx_workspace_bound() - max memory needed to initialize a zstd_cctx
 * @parameters: The compression parameters to be used.
 *
 * If multiple compression parameters might be used, the caller must call
 * zstd_cctx_workspace_bound() for each set of parameters and use the maximum
 * size.
 *
 * Return:      A lower bound on the size of the workspace that is passed to
 *              zstd_init_cctx().
 */
size_t zstd_cctx_workspace_bound(const zstd_compression_parameters *parameters);

/**
 * zstd_init_cctx() - initialize a zstd compression context
 * @workspace:      The workspace to emplace the context into. It must outlive
 *                  the returned context.
 * @workspace_size: The size of workspace. Use zstd_cctx_workspace_bound() to
 *                  determine how large the workspace must be.
 *
 * Return:          A zstd compression context or NULL on error.
 */
zstd_cctx *zstd_init_cctx(void *workspace, size_t workspace_size);

/**
 * zstd_compress_cctx() - compress src into dst with the initialized parameters
 * @cctx:         The context. Must have been initialized with zstd_init_cctx().
 * @dst:          The buffer to compress src into.
 * @dst_capacity: The size of the destination buffer. May be any size, but
 *                ZSTD_compressBound(srcSize) is guaranteed to be large enough.
 * @src:          The data to compress.
 * @src_size:     The size of the data to compress.
 * @parameters:   The compression parameters to be used.
 *
 * Return:        The compressed size or an error, which can be checked using
 *                zstd_is_error().
 */
size_t zstd_compress_cctx(zstd_cctx *cctx, void *dst, size_t dst_capacity,
	const void *src, size_t src_size, const zstd_parameters *parameters);

/* ======   Single-pass Decompression   ====== */

typedef ZSTD_DCtx zstd_dctx;

/**
 * zstd_dctx_workspace_bound() - max memory needed to initialize a zstd_dctx
 *
 * Return: A lower bound on the size of the workspace that is passed to
 *         zstd_init_dctx().
 */
size_t zstd_dctx_workspace_bound(void);

/**
 * zstd_init_dctx() - initialize a zstd decompression context
 * @workspace:      The workspace to emplace the context into. It must outlive
 *                  the returned context.
 * @workspace_size: The size of workspace. Use zstd_dctx_workspace_bound() to
 *                  determine how large the workspace must be.
 *
 * Return:          A zstd decompression context or NULL on error.
 */
zstd_dctx *zstd_init_dctx(void *workspace, size_t workspace_size);

/**
 * zstd_decompress_dctx() - decompress zstd compressed src into dst
 * @dctx:         The decompression context.
 * @dst:          The buffer to decompress src into.
 * @dst_capacity: The size of the destination buffer. Must be at least as large
 *                as the decompressed size. If the caller cannot upper bound the
 *                decompressed size, then it's better to use the streaming API.
 * @src:          The zstd compressed data to decompress. Multiple concatenated
 *                frames and skippable frames are allowed.
 * @src_size:     The exact size of the data to decompress.
 *
 * Return:        The decompressed size or an error, which can be checked using
 *                zstd_is_error().
 */
size_t zstd_decompress_dctx(zstd_dctx *dctx, void *dst, size_t dst_capacity,
	const void *src, size_t src_size);

/* ======   Streaming Buffers   ====== */

/**
 * struct zstd_in_buffer - input buffer for streaming
 * @src:  Start of the input buffer.
 * @size: Size of the input buffer.
 * @pos:  Position where reading stopped. Will be updated.
 *        Necessarily 0 <= pos <= size.
 *
 * See zstd_lib.h.
 */
typedef ZSTD_inBuffer zstd_in_buffer;

/**
 * struct zstd_out_buffer - output buffer for streaming
 * @dst:  Start of the output buffer.
 * @size: Size of the output buffer.
 * @pos:  Position where writing stopped. Will be updated.
 *        Necessarily 0 <= pos <= size.
 *
 * See zstd_lib.h.
 */
typedef ZSTD_outBuffer zstd_out_buffer;

/* ======   Streaming Compression   ====== */

typedef ZSTD_CStream zstd_cstream;

/**
 * zstd_cstream_workspace_bound() - memory needed to initialize a zstd_cstream
 * @cparams: The compression parameters to be used for compression.
 *
 * Return:   A lower bound on the size of the workspace that is passed to
 *           zstd_init_cstream().
 */
size_t zstd_cstream_workspace_bound(const zstd_compression_parameters *cparams);

/**
 * zstd_init_cstream() - initialize a zstd streaming compression context
 * @parameters        The zstd parameters to use for compression.
 * @pledged_src_size: If params.fParams.contentSizeFlag == 1 then the caller
 *                    must pass the source size (zero means empty source).
 *                    Otherwise, the caller may optionally pass the source
 *                    size, or zero if unknown.
 * @workspace:        The workspace to emplace the context into. It must outlive
 *                    the returned context.
 * @workspace_size:   The size of workspace.
 *                    Use zstd_cstream_workspace_bound(params->cparams) to
 *                    determine how large the workspace must be.
 *
 * Return:            The zstd streaming compression context or NULL on error.
 */
zstd_cstream *zstd_init_cstream(const zstd_parameters *parameters,
	unsigned long long pledged_src_size, void *workspace, size_t workspace_size);

/**
 * zstd_reset_cstream() - reset the context using parameters from creation
 * @cstream:          The zstd streaming compression context to reset.
 * @pledged_src_size: Optionally the source size, or zero if unknown.
 *
 * Resets the context using the parameters from creation. Skips dictionary
 * loading, since it can be reused. If `pledged_src_size` is non-zero the frame
 * content size is always written into the frame header.
 *
 * Return:            Zero or an error, which can be checked using
 *                    zstd_is_error().
 */
size_t zstd_reset_cstream(zstd_cstream *cstream,
	unsigned long long pledged_src_size);

/**
 * zstd_compress_stream() - streaming compress some of input into output
 * @cstream: The zstd streaming compression context.
 * @output:  Destination buffer. `output->pos` is updated to indicate how much
 *           compressed data was written.
 * @input:   Source buffer. `input->pos` is updated to indicate how much data
 *           was read. Note that it may not consume the entire input, in which
 *           case `input->pos < input->size`, and it's up to the caller to
 *           present remaining data again.
 *
 * The `input` and `output` buffers may be any size. Guaranteed to make some
 * forward progress if `input` and `output` are not empty.
 *
 * Return:   A hint for the number of bytes to use as the input for the next
 *           function call or an error, which can be checked using
 *           zstd_is_error().
 */
size_t zstd_compress_stream(zstd_cstream *cstream, zstd_out_buffer *output,
	zstd_in_buffer *input);

/**
 * zstd_flush_stream() - flush internal buffers into output
 * @cstream: The zstd streaming compression context.
 * @output:  Destination buffer. `output->pos` is updated to indicate how much
 *           compressed data was written.
 *
 * zstd_flush_stream() must be called until it returns 0, meaning all the data
 * has been flushed. Since zstd_flush_stream() causes a block to be ended,
 * calling it too often will degrade the compression ratio.
 *
 * Return:   The number of bytes still present within internal buffers or an
 *           error, which can be checked using zstd_is_error().
 */
size_t zstd_flush_stream(zstd_cstream *cstream, zstd_out_buffer *output);

/**
 * zstd_end_stream() - flush internal buffers into output and end the frame
 * @cstream: The zstd streaming compression context.
 * @output:  Destination buffer. `output->pos` is updated to indicate how much
 *           compressed data was written.
 *
 * zstd_end_stream() must be called until it returns 0, meaning all the data has
 * been flushed and the frame epilogue has been written.
 *
 * Return:   The number of bytes still present within internal buffers or an
 *           error, which can be checked using zstd_is_error().
 */
size_t zstd_end_stream(zstd_cstream *cstream, zstd_out_buffer *output);

/* ======   Streaming Decompression   ====== */

typedef ZSTD_DStream zstd_dstream;

/**
 * zstd_dstream_workspace_bound() - memory needed to initialize a zstd_dstream
 * @max_window_size: The maximum window size allowed for compressed frames.
 *
 * Return:           A lower bound on the size of the workspace that is passed
 *                   to zstd_init_dstream().
 */
size_t zstd_dstream_workspace_bound(size_t max_window_size);

/**
 * zstd_init_dstream() - initialize a zstd streaming decompression context
 * @max_window_size: The maximum window size allowed for compressed frames.
 * @workspace:       The workspace to emplace the context into. It must outlive
 *                   the returned context.
 * @workspaceSize:   The size of workspace.
 *                   Use zstd_dstream_workspace_bound(max_window_size) to
 *                   determine how large the workspace must be.
 *
 * Return:           The zstd streaming decompression context.
 */
zstd_dstream *zstd_init_dstream(size_t max_window_size, void *workspace,
	size_t workspace_size);

/**
 * zstd_reset_dstream() - reset the context using parameters from creation
 * @dstream: The zstd streaming decompression context to reset.
 *
 * Resets the context using the parameters from creation. Skips dictionary
 * loading, since it can be reused.
 *
 * Return:   Zero or an error, which can be checked using zstd_is_error().
 */
size_t zstd_reset_dstream(zstd_dstream *dstream);

/**
 * zstd_decompress_stream() - streaming decompress some of input into output
 * @dstream: The zstd streaming decompression context.
 * @output:  Destination buffer. `output.pos` is updated to indicate how much
 *           decompressed data was written.
 * @input:   Source buffer. `input.pos` is updated to indicate how much data was
 *           read. Note that it may not consume the entire input, in which case
 *           `input.pos < input.size`, and it's up to the caller to present
 *           remaining data again.
 *
 * The `input` and `output` buffers may be any size. Guaranteed to make some
 * forward progress if `input` and `output` are not empty.
 * zstd_decompress_stream() will not consume the last byte of the frame until
 * the entire frame is flushed.
 *
 * Return:   Returns 0 iff a frame is completely decoded and fully flushed.
 *           Otherwise returns a hint for the number of bytes to use as the
 *           input for the next function call or an error, which can be checked
 *           using zstd_is_error(). The size hint will never load more than the
 *           frame.
 */
size_t zstd_decompress_stream(zstd_dstream *dstream, zstd_out_buffer *output,
	zstd_in_buffer *input);

/* ======   Frame Inspection Functions ====== */

/**
 * zstd_find_frame_compressed_size() - returns the size of a compressed frame
 * @src:      Source buffer. It should point to the start of a zstd encoded
 *            frame or a skippable frame.
 * @src_size: The size of the source buffer. It must be at least as large as the
 *            size of the frame.
 *
 * Return:    The compressed size of the frame pointed to by `src` or an error,
 *            which can be check with zstd_is_error().
 *            Suitable to pass to ZSTD_decompress() or similar functions.
 */
size_t zstd_find_frame_compressed_size(const void *src, size_t src_size);

/**
 * struct zstd_frame_params - zstd frame parameters stored in the frame header
 * @frameContentSize: The frame content size, or ZSTD_CONTENTSIZE_UNKNOWN if not
 *                    present.
 * @windowSize:       The window size, or 0 if the frame is a skippable frame.
 * @blockSizeMax:     The maximum block size.
 * @frameType:        The frame type (zstd or skippable)
 * @headerSize:       The size of the frame header.
 * @dictID:           The dictionary id, or 0 if not present.
 * @checksumFlag:     Whether a checksum was used.
 *
 * See zstd_lib.h.
 */
typedef ZSTD_frameHeader zstd_frame_header;

/**
 * zstd_get_frame_header() - extracts parameters from a zstd or skippable frame
 * @params:   On success the frame parameters are written here.
 * @src:      The source buffer. It must point to a zstd or skippable frame.
 * @src_size: The size of the source buffer.
 *
 * Return:    0 on success. If more data is required it returns how many bytes
 *            must be provided to make forward progress. Otherwise it returns
 *            an error, which can be checked using zstd_is_error().
 */
size_t zstd_get_frame_header(zstd_frame_header *params, const void *src,
	size_t src_size);

#endif  /* LINUX_ZSTD_H */
