/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of https://github.com/facebook/zstd.
 * An additional grant of patent rights can be found in the PATENTS file in the
 * same directory.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation. This program is dual-licensed; you may select
 * either version 2 of the GNU General Public License ("GPL") or BSD license
 * ("BSD").
 */

#ifndef ZSTD_H
#define ZSTD_H

/* ======   Dependency   ======*/
#include <linux/types.h>   /* size_t */


/*-*****************************************************************************
 * Introduction
 *
 * zstd, short for Zstandard, is a fast lossless compression algorithm,
 * targeting real-time compression scenarios at zlib-level and better
 * compression ratios. The zstd compression library provides in-memory
 * compression and decompression functions. The library supports compression
 * levels from 1 up to ZSTD_maxCLevel() which is 22. Levels >= 20, labeled
 * ultra, should be used with caution, as they require more memory.
 * Compression can be done in:
 *  - a single step, reusing a context (described as Explicit memory management)
 *  - unbounded multiple steps (described as Streaming compression)
 * The compression ratio achievable on small data can be highly improved using
 * compression with a dictionary in:
 *  - a single step (described as Simple dictionary API)
 *  - a single step, reusing a dictionary (described as Fast dictionary API)
 ******************************************************************************/

/*======  Helper functions  ======*/

/**
 * enum ZSTD_ErrorCode - zstd error codes
 *
 * Functions that return size_t can be checked for errors using ZSTD_isError()
 * and the ZSTD_ErrorCode can be extracted using ZSTD_getErrorCode().
 */
typedef enum {
	ZSTD_error_no_error,
	ZSTD_error_GENERIC,
	ZSTD_error_prefix_unknown,
	ZSTD_error_version_unsupported,
	ZSTD_error_parameter_unknown,
	ZSTD_error_frameParameter_unsupported,
	ZSTD_error_frameParameter_unsupportedBy32bits,
	ZSTD_error_frameParameter_windowTooLarge,
	ZSTD_error_compressionParameter_unsupported,
	ZSTD_error_init_missing,
	ZSTD_error_memory_allocation,
	ZSTD_error_stage_wrong,
	ZSTD_error_dstSize_tooSmall,
	ZSTD_error_srcSize_wrong,
	ZSTD_error_corruption_detected,
	ZSTD_error_checksum_wrong,
	ZSTD_error_tableLog_tooLarge,
	ZSTD_error_maxSymbolValue_tooLarge,
	ZSTD_error_maxSymbolValue_tooSmall,
	ZSTD_error_dictionary_corrupted,
	ZSTD_error_dictionary_wrong,
	ZSTD_error_dictionaryCreation_failed,
	ZSTD_error_maxCode
} ZSTD_ErrorCode;

/**
 * ZSTD_maxCLevel() - maximum compression level available
 *
 * Return: Maximum compression level available.
 */
int ZSTD_maxCLevel(void);
/**
 * ZSTD_compressBound() - maximum compressed size in worst case scenario
 * @srcSize: The size of the data to compress.
 *
 * Return:   The maximum compressed size in the worst case scenario.
 */
size_t ZSTD_compressBound(size_t srcSize);
/**
 * ZSTD_isError() - tells if a size_t function result is an error code
 * @code:  The function result to check for error.
 *
 * Return: Non-zero iff the code is an error.
 */
static __attribute__((unused)) unsigned int ZSTD_isError(size_t code)
{
	return code > (size_t)-ZSTD_error_maxCode;
}
/**
 * ZSTD_getErrorCode() - translates an error function result to a ZSTD_ErrorCode
 * @functionResult: The result of a function for which ZSTD_isError() is true.
 *
 * Return:          The ZSTD_ErrorCode corresponding to the functionResult or 0
 *                  if the functionResult isn't an error.
 */
static __attribute__((unused)) ZSTD_ErrorCode ZSTD_getErrorCode(
	size_t functionResult)
{
	if (!ZSTD_isError(functionResult))
		return (ZSTD_ErrorCode)0;
	return (ZSTD_ErrorCode)(0 - functionResult);
}

/**
 * enum ZSTD_strategy - zstd compression search strategy
 *
 * From faster to stronger.
 */
typedef enum {
	ZSTD_fast,
	ZSTD_dfast,
	ZSTD_greedy,
	ZSTD_lazy,
	ZSTD_lazy2,
	ZSTD_btlazy2,
	ZSTD_btopt,
	ZSTD_btopt2
} ZSTD_strategy;

/**
 * struct ZSTD_compressionParameters - zstd compression parameters
 * @windowLog:    Log of the largest match distance. Larger means more
 *                compression, and more memory needed during decompression.
 * @chainLog:     Fully searched segment. Larger means more compression, slower,
 *                and more memory (useless for fast).
 * @hashLog:      Dispatch table. Larger means more compression,
 *                slower, and more memory.
 * @searchLog:    Number of searches. Larger means more compression and slower.
 * @searchLength: Match length searched. Larger means faster decompression,
 *                sometimes less compression.
 * @targetLength: Acceptable match size for optimal parser (only). Larger means
 *                more compression, and slower.
 * @strategy:     The zstd compression strategy.
 */
typedef struct {
	unsigned int windowLog;
	unsigned int chainLog;
	unsigned int hashLog;
	unsigned int searchLog;
	unsigned int searchLength;
	unsigned int targetLength;
	ZSTD_strategy strategy;
} ZSTD_compressionParameters;

/**
 * struct ZSTD_frameParameters - zstd frame parameters
 * @contentSizeFlag: Controls whether content size will be present in the frame
 *                   header (when known).
 * @checksumFlag:    Controls whether a 32-bit checksum is generated at the end
 *                   of the frame for error detection.
 * @noDictIDFlag:    Controls whether dictID will be saved into the frame header
 *                   when using dictionary compression.
 *
 * The default value is all fields set to 0.
 */
typedef struct {
	unsigned int contentSizeFlag;
	unsigned int checksumFlag;
	unsigned int noDictIDFlag;
} ZSTD_frameParameters;

/**
 * struct ZSTD_parameters - zstd parameters
 * @cParams: The compression parameters.
 * @fParams: The frame parameters.
 */
typedef struct {
	ZSTD_compressionParameters cParams;
	ZSTD_frameParameters fParams;
} ZSTD_parameters;

/**
 * ZSTD_getCParams() - returns ZSTD_compressionParameters for selected level
 * @compressionLevel: The compression level from 1 to ZSTD_maxCLevel().
 * @estimatedSrcSize: The estimated source size to compress or 0 if unknown.
 * @dictSize:         The dictionary size or 0 if a dictionary isn't being used.
 *
 * Return:            The selected ZSTD_compressionParameters.
 */
ZSTD_compressionParameters ZSTD_getCParams(int compressionLevel,
	unsigned long long estimatedSrcSize, size_t dictSize);

/**
 * ZSTD_getParams() - returns ZSTD_parameters for selected level
 * @compressionLevel: The compression level from 1 to ZSTD_maxCLevel().
 * @estimatedSrcSize: The estimated source size to compress or 0 if unknown.
 * @dictSize:         The dictionary size or 0 if a dictionary isn't being used.
 *
 * The same as ZSTD_getCParams() except also selects the default frame
 * parameters (all zero).
 *
 * Return:            The selected ZSTD_parameters.
 */
ZSTD_parameters ZSTD_getParams(int compressionLevel,
	unsigned long long estimatedSrcSize, size_t dictSize);

/*-*************************************
 * Explicit memory management
 **************************************/

/**
 * ZSTD_CCtxWorkspaceBound() - amount of memory needed to initialize a ZSTD_CCtx
 * @cParams: The compression parameters to be used for compression.
 *
 * If multiple compression parameters might be used, the caller must call
 * ZSTD_CCtxWorkspaceBound() for each set of parameters and use the maximum
 * size.
 *
 * Return:   A lower bound on the size of the workspace that is passed to
 *           ZSTD_initCCtx().
 */
size_t ZSTD_CCtxWorkspaceBound(ZSTD_compressionParameters cParams);

/**
 * struct ZSTD_CCtx - the zstd compression context
 *
 * When compressing many times it is recommended to allocate a context just once
 * and reuse it for each successive compression operation.
 */
typedef struct ZSTD_CCtx_s ZSTD_CCtx;
/**
 * ZSTD_initCCtx() - initialize a zstd compression context
 * @workspace:     The workspace to emplace the context into. It must outlive
 *                 the returned context.
 * @workspaceSize: The size of workspace. Use ZSTD_CCtxWorkspaceBound() to
 *                 determine how large the workspace must be.
 *
 * Return:         A compression context emplaced into workspace.
 */
ZSTD_CCtx *ZSTD_initCCtx(void *workspace, size_t workspaceSize);

/**
 * ZSTD_compressCCtx() - compress src into dst
 * @ctx:         The context. Must have been initialized with a workspace at
 *               least as large as ZSTD_CCtxWorkspaceBound(params.cParams).
 * @dst:         The buffer to compress src into.
 * @dstCapacity: The size of the destination buffer. May be any size, but
 *               ZSTD_compressBound(srcSize) is guaranteed to be large enough.
 * @src:         The data to compress.
 * @srcSize:     The size of the data to compress.
 * @params:      The parameters to use for compression. See ZSTD_getParams().
 *
 * Return:       The compressed size or an error, which can be checked using
 *               ZSTD_isError().
 */
size_t ZSTD_compressCCtx(ZSTD_CCtx *ctx, void *dst, size_t dstCapacity,
	const void *src, size_t srcSize, ZSTD_parameters params);

/**
 * ZSTD_DCtxWorkspaceBound() - amount of memory needed to initialize a ZSTD_DCtx
 *
 * Return: A lower bound on the size of the workspace that is passed to
 *         ZSTD_initDCtx().
 */
size_t ZSTD_DCtxWorkspaceBound(void);

/**
 * struct ZSTD_DCtx - the zstd decompression context
 *
 * When decompressing many times it is recommended to allocate a context just
 * once and reuse it for each successive decompression operation.
 */
typedef struct ZSTD_DCtx_s ZSTD_DCtx;
/**
 * ZSTD_initDCtx() - initialize a zstd decompression context
 * @workspace:     The workspace to emplace the context into. It must outlive
 *                 the returned context.
 * @workspaceSize: The size of workspace. Use ZSTD_DCtxWorkspaceBound() to
 *                 determine how large the workspace must be.
 *
 * Return:         A decompression context emplaced into workspace.
 */
ZSTD_DCtx *ZSTD_initDCtx(void *workspace, size_t workspaceSize);

/**
 * ZSTD_decompressDCtx() - decompress zstd compressed src into dst
 * @ctx:         The decompression context.
 * @dst:         The buffer to decompress src into.
 * @dstCapacity: The size of the destination buffer. Must be at least as large
 *               as the decompressed size. If the caller cannot upper bound the
 *               decompressed size, then it's better to use the streaming API.
 * @src:         The zstd compressed data to decompress. Multiple concatenated
 *               frames and skippable frames are allowed.
 * @srcSize:     The exact size of the data to decompress.
 *
 * Return:       The decompressed size or an error, which can be checked using
 *               ZSTD_isError().
 */
size_t ZSTD_decompressDCtx(ZSTD_DCtx *ctx, void *dst, size_t dstCapacity,
	const void *src, size_t srcSize);

/*-************************
 * Simple dictionary API
 **************************/

/**
 * ZSTD_compress_usingDict() - compress src into dst using a dictionary
 * @ctx:         The context. Must have been initialized with a workspace at
 *               least as large as ZSTD_CCtxWorkspaceBound(params.cParams).
 * @dst:         The buffer to compress src into.
 * @dstCapacity: The size of the destination buffer. May be any size, but
 *               ZSTD_compressBound(srcSize) is guaranteed to be large enough.
 * @src:         The data to compress.
 * @srcSize:     The size of the data to compress.
 * @dict:        The dictionary to use for compression.
 * @dictSize:    The size of the dictionary.
 * @params:      The parameters to use for compression. See ZSTD_getParams().
 *
 * Compression using a predefined dictionary. The same dictionary must be used
 * during decompression.
 *
 * Return:       The compressed size or an error, which can be checked using
 *               ZSTD_isError().
 */
size_t ZSTD_compress_usingDict(ZSTD_CCtx *ctx, void *dst, size_t dstCapacity,
	const void *src, size_t srcSize, const void *dict, size_t dictSize,
	ZSTD_parameters params);

/**
 * ZSTD_decompress_usingDict() - decompress src into dst using a dictionary
 * @ctx:         The decompression context.
 * @dst:         The buffer to decompress src into.
 * @dstCapacity: The size of the destination buffer. Must be at least as large
 *               as the decompressed size. If the caller cannot upper bound the
 *               decompressed size, then it's better to use the streaming API.
 * @src:         The zstd compressed data to decompress. Multiple concatenated
 *               frames and skippable frames are allowed.
 * @srcSize:     The exact size of the data to decompress.
 * @dict:        The dictionary to use for decompression. The same dictionary
 *               must've been used to compress the data.
 * @dictSize:    The size of the dictionary.
 *
 * Return:       The decompressed size or an error, which can be checked using
 *               ZSTD_isError().
 */
size_t ZSTD_decompress_usingDict(ZSTD_DCtx *ctx, void *dst, size_t dstCapacity,
	const void *src, size_t srcSize, const void *dict, size_t dictSize);

/*-**************************
 * Fast dictionary API
 ***************************/

/**
 * ZSTD_CDictWorkspaceBound() - memory needed to initialize a ZSTD_CDict
 * @cParams: The compression parameters to be used for compression.
 *
 * Return:   A lower bound on the size of the workspace that is passed to
 *           ZSTD_initCDict().
 */
size_t ZSTD_CDictWorkspaceBound(ZSTD_compressionParameters cParams);

/**
 * struct ZSTD_CDict - a digested dictionary to be used for compression
 */
typedef struct ZSTD_CDict_s ZSTD_CDict;

/**
 * ZSTD_initCDict() - initialize a digested dictionary for compression
 * @dictBuffer:    The dictionary to digest. The buffer is referenced by the
 *                 ZSTD_CDict so it must outlive the returned ZSTD_CDict.
 * @dictSize:      The size of the dictionary.
 * @params:        The parameters to use for compression. See ZSTD_getParams().
 * @workspace:     The workspace. It must outlive the returned ZSTD_CDict.
 * @workspaceSize: The workspace size. Must be at least
 *                 ZSTD_CDictWorkspaceBound(params.cParams).
 *
 * When compressing multiple messages / blocks with the same dictionary it is
 * recommended to load it just once. The ZSTD_CDict merely references the
 * dictBuffer, so it must outlive the returned ZSTD_CDict.
 *
 * Return:         The digested dictionary emplaced into workspace.
 */
ZSTD_CDict *ZSTD_initCDict(const void *dictBuffer, size_t dictSize,
	ZSTD_parameters params, void *workspace, size_t workspaceSize);

/**
 * ZSTD_compress_usingCDict() - compress src into dst using a ZSTD_CDict
 * @ctx:         The context. Must have been initialized with a workspace at
 *               least as large as ZSTD_CCtxWorkspaceBound(cParams) where
 *               cParams are the compression parameters used to initialize the
 *               cdict.
 * @dst:         The buffer to compress src into.
 * @dstCapacity: The size of the destination buffer. May be any size, but
 *               ZSTD_compressBound(srcSize) is guaranteed to be large enough.
 * @src:         The data to compress.
 * @srcSize:     The size of the data to compress.
 * @cdict:       The digested dictionary to use for compression.
 * @params:      The parameters to use for compression. See ZSTD_getParams().
 *
 * Compression using a digested dictionary. The same dictionary must be used
 * during decompression.
 *
 * Return:       The compressed size or an error, which can be checked using
 *               ZSTD_isError().
 */
size_t ZSTD_compress_usingCDict(ZSTD_CCtx *cctx, void *dst, size_t dstCapacity,
	const void *src, size_t srcSize, const ZSTD_CDict *cdict);


/**
 * ZSTD_DDictWorkspaceBound() - memory needed to initialize a ZSTD_DDict
 *
 * Return:  A lower bound on the size of the workspace that is passed to
 *          ZSTD_initDDict().
 */
size_t ZSTD_DDictWorkspaceBound(void);

/**
 * struct ZSTD_DDict - a digested dictionary to be used for decompression
 */
typedef struct ZSTD_DDict_s ZSTD_DDict;

/**
 * ZSTD_initDDict() - initialize a digested dictionary for decompression
 * @dictBuffer:    The dictionary to digest. The buffer is referenced by the
 *                 ZSTD_DDict so it must outlive the returned ZSTD_DDict.
 * @dictSize:      The size of the dictionary.
 * @workspace:     The workspace. It must outlive the returned ZSTD_DDict.
 * @workspaceSize: The workspace size. Must be at least
 *                 ZSTD_DDictWorkspaceBound().
 *
 * When decompressing multiple messages / blocks with the same dictionary it is
 * recommended to load it just once. The ZSTD_DDict merely references the
 * dictBuffer, so it must outlive the returned ZSTD_DDict.
 *
 * Return:         The digested dictionary emplaced into workspace.
 */
ZSTD_DDict *ZSTD_initDDict(const void *dictBuffer, size_t dictSize,
	void *workspace, size_t workspaceSize);

/**
 * ZSTD_decompress_usingDDict() - decompress src into dst using a ZSTD_DDict
 * @ctx:         The decompression context.
 * @dst:         The buffer to decompress src into.
 * @dstCapacity: The size of the destination buffer. Must be at least as large
 *               as the decompressed size. If the caller cannot upper bound the
 *               decompressed size, then it's better to use the streaming API.
 * @src:         The zstd compressed data to decompress. Multiple concatenated
 *               frames and skippable frames are allowed.
 * @srcSize:     The exact size of the data to decompress.
 * @ddict:       The digested dictionary to use for decompression. The same
 *               dictionary must've been used to compress the data.
 *
 * Return:       The decompressed size or an error, which can be checked using
 *               ZSTD_isError().
 */
size_t ZSTD_decompress_usingDDict(ZSTD_DCtx *dctx, void *dst,
	size_t dstCapacity, const void *src, size_t srcSize,
	const ZSTD_DDict *ddict);


/*-**************************
 * Streaming
 ***************************/

/**
 * struct ZSTD_inBuffer - input buffer for streaming
 * @src:  Start of the input buffer.
 * @size: Size of the input buffer.
 * @pos:  Position where reading stopped. Will be updated.
 *        Necessarily 0 <= pos <= size.
 */
typedef struct ZSTD_inBuffer_s {
	const void *src;
	size_t size;
	size_t pos;
} ZSTD_inBuffer;

/**
 * struct ZSTD_outBuffer - output buffer for streaming
 * @dst:  Start of the output buffer.
 * @size: Size of the output buffer.
 * @pos:  Position where writing stopped. Will be updated.
 *        Necessarily 0 <= pos <= size.
 */
typedef struct ZSTD_outBuffer_s {
	void *dst;
	size_t size;
	size_t pos;
} ZSTD_outBuffer;



/*-*****************************************************************************
 * Streaming compression - HowTo
 *
 * A ZSTD_CStream object is required to track streaming operation.
 * Use ZSTD_initCStream() to initialize a ZSTD_CStream object.
 * ZSTD_CStream objects can be reused multiple times on consecutive compression
 * operations. It is recommended to re-use ZSTD_CStream in situations where many
 * streaming operations will be achieved consecutively. Use one separate
 * ZSTD_CStream per thread for parallel execution.
 *
 * Use ZSTD_compressStream() repetitively to consume input stream.
 * The function will automatically update both `pos` fields.
 * Note that it may not consume the entire input, in which case `pos < size`,
 * and it's up to the caller to present again remaining data.
 * It returns a hint for the preferred number of bytes to use as an input for
 * the next function call.
 *
 * At any moment, it's possible to flush whatever data remains within internal
 * buffer, using ZSTD_flushStream(). `output->pos` will be updated. There might
 * still be some content left within the internal buffer if `output->size` is
 * too small. It returns the number of bytes left in the internal buffer and
 * must be called until it returns 0.
 *
 * ZSTD_endStream() instructs to finish a frame. It will perform a flush and
 * write frame epilogue. The epilogue is required for decoders to consider a
 * frame completed. Similar to ZSTD_flushStream(), it may not be able to flush
 * the full content if `output->size` is too small. In which case, call again
 * ZSTD_endStream() to complete the flush. It returns the number of bytes left
 * in the internal buffer and must be called until it returns 0.
 ******************************************************************************/

/**
 * ZSTD_CStreamWorkspaceBound() - memory needed to initialize a ZSTD_CStream
 * @cParams: The compression parameters to be used for compression.
 *
 * Return:   A lower bound on the size of the workspace that is passed to
 *           ZSTD_initCStream() and ZSTD_initCStream_usingCDict().
 */
size_t ZSTD_CStreamWorkspaceBound(ZSTD_compressionParameters cParams);

/**
 * struct ZSTD_CStream - the zstd streaming compression context
 */
typedef struct ZSTD_CStream_s ZSTD_CStream;

/*===== ZSTD_CStream management functions =====*/
/**
 * ZSTD_initCStream() - initialize a zstd streaming compression context
 * @params:         The zstd compression parameters.
 * @pledgedSrcSize: If params.fParams.contentSizeFlag == 1 then the caller must
 *                  pass the source size (zero means empty source). Otherwise,
 *                  the caller may optionally pass the source size, or zero if
 *                  unknown.
 * @workspace:      The workspace to emplace the context into. It must outlive
 *                  the returned context.
 * @workspaceSize:  The size of workspace.
 *                  Use ZSTD_CStreamWorkspaceBound(params.cParams) to determine
 *                  how large the workspace must be.
 *
 * Return:          The zstd streaming compression context.
 */
ZSTD_CStream *ZSTD_initCStream(ZSTD_parameters params,
	unsigned long long pledgedSrcSize, void *workspace,
	size_t workspaceSize);

/**
 * ZSTD_initCStream_usingCDict() - initialize a streaming compression context
 * @cdict:          The digested dictionary to use for compression.
 * @pledgedSrcSize: Optionally the source size, or zero if unknown.
 * @workspace:      The workspace to emplace the context into. It must outlive
 *                  the returned context.
 * @workspaceSize:  The size of workspace. Call ZSTD_CStreamWorkspaceBound()
 *                  with the cParams used to initialize the cdict to determine
 *                  how large the workspace must be.
 *
 * Return:          The zstd streaming compression context.
 */
ZSTD_CStream *ZSTD_initCStream_usingCDict(const ZSTD_CDict *cdict,
	unsigned long long pledgedSrcSize, void *workspace,
	size_t workspaceSize);

/*===== Streaming compression functions =====*/
/**
 * ZSTD_resetCStream() - reset the context using parameters from creation
 * @zcs:            The zstd streaming compression context to reset.
 * @pledgedSrcSize: Optionally the source size, or zero if unknown.
 *
 * Resets the context using the parameters from creation. Skips dictionary
 * loading, since it can be reused. If `pledgedSrcSize` is non-zero the frame
 * content size is always written into the frame header.
 *
 * Return:          Zero or an error, which can be checked using ZSTD_isError().
 */
size_t ZSTD_resetCStream(ZSTD_CStream *zcs, unsigned long long pledgedSrcSize);
/**
 * ZSTD_compressStream() - streaming compress some of input into output
 * @zcs:    The zstd streaming compression context.
 * @output: Destination buffer. `output->pos` is updated to indicate how much
 *          compressed data was written.
 * @input:  Source buffer. `input->pos` is updated to indicate how much data was
 *          read. Note that it may not consume the entire input, in which case
 *          `input->pos < input->size`, and it's up to the caller to present
 *          remaining data again.
 *
 * The `input` and `output` buffers may be any size. Guaranteed to make some
 * forward progress if `input` and `output` are not empty.
 *
 * Return:  A hint for the number of bytes to use as the input for the next
 *          function call or an error, which can be checked using
 *          ZSTD_isError().
 */
size_t ZSTD_compressStream(ZSTD_CStream *zcs, ZSTD_outBuffer *output,
	ZSTD_inBuffer *input);
/**
 * ZSTD_flushStream() - flush internal buffers into output
 * @zcs:    The zstd streaming compression context.
 * @output: Destination buffer. `output->pos` is updated to indicate how much
 *          compressed data was written.
 *
 * ZSTD_flushStream() must be called until it returns 0, meaning all the data
 * has been flushed. Since ZSTD_flushStream() causes a block to be ended,
 * calling it too often will degrade the compression ratio.
 *
 * Return:  The number of bytes still present within internal buffers or an
 *          error, which can be checked using ZSTD_isError().
 */
size_t ZSTD_flushStream(ZSTD_CStream *zcs, ZSTD_outBuffer *output);
/**
 * ZSTD_endStream() - flush internal buffers into output and end the frame
 * @zcs:    The zstd streaming compression context.
 * @output: Destination buffer. `output->pos` is updated to indicate how much
 *          compressed data was written.
 *
 * ZSTD_endStream() must be called until it returns 0, meaning all the data has
 * been flushed and the frame epilogue has been written.
 *
 * Return:  The number of bytes still present within internal buffers or an
 *          error, which can be checked using ZSTD_isError().
 */
size_t ZSTD_endStream(ZSTD_CStream *zcs, ZSTD_outBuffer *output);

/**
 * ZSTD_CStreamInSize() - recommended size for the input buffer
 *
 * Return: The recommended size for the input buffer.
 */
size_t ZSTD_CStreamInSize(void);
/**
 * ZSTD_CStreamOutSize() - recommended size for the output buffer
 *
 * When the output buffer is at least this large, it is guaranteed to be large
 * enough to flush at least one complete compressed block.
 *
 * Return: The recommended size for the output buffer.
 */
size_t ZSTD_CStreamOutSize(void);



/*-*****************************************************************************
 * Streaming decompression - HowTo
 *
 * A ZSTD_DStream object is required to track streaming operations.
 * Use ZSTD_initDStream() to initialize a ZSTD_DStream object.
 * ZSTD_DStream objects can be re-used multiple times.
 *
 * Use ZSTD_decompressStream() repetitively to consume your input.
 * The function will update both `pos` fields.
 * If `input->pos < input->size`, some input has not been consumed.
 * It's up to the caller to present again remaining data.
 * If `output->pos < output->size`, decoder has flushed everything it could.
 * Returns 0 iff a frame is completely decoded and fully flushed.
 * Otherwise it returns a suggested next input size that will never load more
 * than the current frame.
 ******************************************************************************/

/**
 * ZSTD_DStreamWorkspaceBound() - memory needed to initialize a ZSTD_DStream
 * @maxWindowSize: The maximum window size allowed for compressed frames.
 *
 * Return:         A lower bound on the size of the workspace that is passed to
 *                 ZSTD_initDStream() and ZSTD_initDStream_usingDDict().
 */
size_t ZSTD_DStreamWorkspaceBound(size_t maxWindowSize);

/**
 * struct ZSTD_DStream - the zstd streaming decompression context
 */
typedef struct ZSTD_DStream_s ZSTD_DStream;
/*===== ZSTD_DStream management functions =====*/
/**
 * ZSTD_initDStream() - initialize a zstd streaming decompression context
 * @maxWindowSize: The maximum window size allowed for compressed frames.
 * @workspace:     The workspace to emplace the context into. It must outlive
 *                 the returned context.
 * @workspaceSize: The size of workspace.
 *                 Use ZSTD_DStreamWorkspaceBound(maxWindowSize) to determine
 *                 how large the workspace must be.
 *
 * Return:         The zstd streaming decompression context.
 */
ZSTD_DStream *ZSTD_initDStream(size_t maxWindowSize, void *workspace,
	size_t workspaceSize);
/**
 * ZSTD_initDStream_usingDDict() - initialize streaming decompression context
 * @maxWindowSize: The maximum window size allowed for compressed frames.
 * @ddict:         The digested dictionary to use for decompression.
 * @workspace:     The workspace to emplace the context into. It must outlive
 *                 the returned context.
 * @workspaceSize: The size of workspace.
 *                 Use ZSTD_DStreamWorkspaceBound(maxWindowSize) to determine
 *                 how large the workspace must be.
 *
 * Return:         The zstd streaming decompression context.
 */
ZSTD_DStream *ZSTD_initDStream_usingDDict(size_t maxWindowSize,
	const ZSTD_DDict *ddict, void *workspace, size_t workspaceSize);

/*===== Streaming decompression functions =====*/
/**
 * ZSTD_resetDStream() - reset the context using parameters from creation
 * @zds:   The zstd streaming decompression context to reset.
 *
 * Resets the context using the parameters from creation. Skips dictionary
 * loading, since it can be reused.
 *
 * Return: Zero or an error, which can be checked using ZSTD_isError().
 */
size_t ZSTD_resetDStream(ZSTD_DStream *zds);
/**
 * ZSTD_decompressStream() - streaming decompress some of input into output
 * @zds:    The zstd streaming decompression context.
 * @output: Destination buffer. `output.pos` is updated to indicate how much
 *          decompressed data was written.
 * @input:  Source buffer. `input.pos` is updated to indicate how much data was
 *          read. Note that it may not consume the entire input, in which case
 *          `input.pos < input.size`, and it's up to the caller to present
 *          remaining data again.
 *
 * The `input` and `output` buffers may be any size. Guaranteed to make some
 * forward progress if `input` and `output` are not empty.
 * ZSTD_decompressStream() will not consume the last byte of the frame until
 * the entire frame is flushed.
 *
 * Return:  Returns 0 iff a frame is completely decoded and fully flushed.
 *          Otherwise returns a hint for the number of bytes to use as the input
 *          for the next function call or an error, which can be checked using
 *          ZSTD_isError(). The size hint will never load more than the frame.
 */
size_t ZSTD_decompressStream(ZSTD_DStream *zds, ZSTD_outBuffer *output,
	ZSTD_inBuffer *input);

/**
 * ZSTD_DStreamInSize() - recommended size for the input buffer
 *
 * Return: The recommended size for the input buffer.
 */
size_t ZSTD_DStreamInSize(void);
/**
 * ZSTD_DStreamOutSize() - recommended size for the output buffer
 *
 * When the output buffer is at least this large, it is guaranteed to be large
 * enough to flush at least one complete decompressed block.
 *
 * Return: The recommended size for the output buffer.
 */
size_t ZSTD_DStreamOutSize(void);


/* --- Constants ---*/
#define ZSTD_MAGICNUMBER            0xFD2FB528   /* >= v0.8.0 */
#define ZSTD_MAGIC_SKIPPABLE_START  0x184D2A50U

#define ZSTD_CONTENTSIZE_UNKNOWN (0ULL - 1)
#define ZSTD_CONTENTSIZE_ERROR   (0ULL - 2)

#define ZSTD_WINDOWLOG_MAX_32  27
#define ZSTD_WINDOWLOG_MAX_64  27
#define ZSTD_WINDOWLOG_MAX \
	((unsigned int)(sizeof(size_t) == 4 \
		? ZSTD_WINDOWLOG_MAX_32 \
		: ZSTD_WINDOWLOG_MAX_64))
#define ZSTD_WINDOWLOG_MIN 10
#define ZSTD_HASHLOG_MAX ZSTD_WINDOWLOG_MAX
#define ZSTD_HASHLOG_MIN        6
#define ZSTD_CHAINLOG_MAX     (ZSTD_WINDOWLOG_MAX+1)
#define ZSTD_CHAINLOG_MIN      ZSTD_HASHLOG_MIN
#define ZSTD_HASHLOG3_MAX      17
#define ZSTD_SEARCHLOG_MAX    (ZSTD_WINDOWLOG_MAX-1)
#define ZSTD_SEARCHLOG_MIN      1
/* only for ZSTD_fast, other strategies are limited to 6 */
#define ZSTD_SEARCHLENGTH_MAX   7
/* only for ZSTD_btopt, other strategies are limited to 4 */
#define ZSTD_SEARCHLENGTH_MIN   3
#define ZSTD_TARGETLENGTH_MIN   4
#define ZSTD_TARGETLENGTH_MAX 999

/* for static allocation */
#define ZSTD_FRAMEHEADERSIZE_MAX 18
#define ZSTD_FRAMEHEADERSIZE_MIN  6
static const size_t ZSTD_frameHeaderSize_prefix = 5;
static const size_t ZSTD_frameHeaderSize_min = ZSTD_FRAMEHEADERSIZE_MIN;
static const size_t ZSTD_frameHeaderSize_max = ZSTD_FRAMEHEADERSIZE_MAX;
/* magic number + skippable frame length */
static const size_t ZSTD_skippableHeaderSize = 8;


/*-*************************************
 * Compressed size functions
 **************************************/

/**
 * ZSTD_findFrameCompressedSize() - returns the size of a compressed frame
 * @src:     Source buffer. It should point to the start of a zstd encoded frame
 *           or a skippable frame.
 * @srcSize: The size of the source buffer. It must be at least as large as the
 *           size of the frame.
 *
 * Return:   The compressed size of the frame pointed to by `src` or an error,
 *           which can be check with ZSTD_isError().
 *           Suitable to pass to ZSTD_decompress() or similar functions.
 */
size_t ZSTD_findFrameCompressedSize(const void *src, size_t srcSize);

/*-*************************************
 * Decompressed size functions
 **************************************/
/**
 * ZSTD_getFrameContentSize() - returns the content size in a zstd frame header
 * @src:     It should point to the start of a zstd encoded frame.
 * @srcSize: The size of the source buffer. It must be at least as large as the
 *           frame header. `ZSTD_frameHeaderSize_max` is always large enough.
 *
 * Return:   The frame content size stored in the frame header if known.
 *           `ZSTD_CONTENTSIZE_UNKNOWN` if the content size isn't stored in the
 *           frame header. `ZSTD_CONTENTSIZE_ERROR` on invalid input.
 */
unsigned long long ZSTD_getFrameContentSize(const void *src, size_t srcSize);

/**
 * ZSTD_findDecompressedSize() - returns decompressed size of a series of frames
 * @src:     It should point to the start of a series of zstd encoded and/or
 *           skippable frames.
 * @srcSize: The exact size of the series of frames.
 *
 * If any zstd encoded frame in the series doesn't have the frame content size
 * set, `ZSTD_CONTENTSIZE_UNKNOWN` is returned. But frame content size is always
 * set when using ZSTD_compress(). The decompressed size can be very large.
 * If the source is untrusted, the decompressed size could be wrong or
 * intentionally modified. Always ensure the result fits within the
 * application's authorized limits. ZSTD_findDecompressedSize() handles multiple
 * frames, and so it must traverse the input to read each frame header. This is
 * efficient as most of the data is skipped, however it does mean that all frame
 * data must be present and valid.
 *
 * Return:   Decompressed size of all the data contained in the frames if known.
 *           `ZSTD_CONTENTSIZE_UNKNOWN` if the decompressed size is unknown.
 *           `ZSTD_CONTENTSIZE_ERROR` if an error occurred.
 */
unsigned long long ZSTD_findDecompressedSize(const void *src, size_t srcSize);

/*-*************************************
 * Advanced compression functions
 **************************************/
/**
 * ZSTD_checkCParams() - ensure parameter values remain within authorized range
 * @cParams: The zstd compression parameters.
 *
 * Return:   Zero or an error, which can be checked using ZSTD_isError().
 */
size_t ZSTD_checkCParams(ZSTD_compressionParameters cParams);

/**
 * ZSTD_adjustCParams() - optimize parameters for a given srcSize and dictSize
 * @srcSize:  Optionally the estimated source size, or zero if unknown.
 * @dictSize: Optionally the estimated dictionary size, or zero if unknown.
 *
 * Return:    The optimized parameters.
 */
ZSTD_compressionParameters ZSTD_adjustCParams(
	ZSTD_compressionParameters cParams, unsigned long long srcSize,
	size_t dictSize);

/*--- Advanced decompression functions ---*/

/**
 * ZSTD_isFrame() - returns true iff the buffer starts with a valid frame
 * @buffer: The source buffer to check.
 * @size:   The size of the source buffer, must be at least 4 bytes.
 *
 * Return: True iff the buffer starts with a zstd or skippable frame identifier.
 */
unsigned int ZSTD_isFrame(const void *buffer, size_t size);

/**
 * ZSTD_getDictID_fromDict() - returns the dictionary id stored in a dictionary
 * @dict:     The dictionary buffer.
 * @dictSize: The size of the dictionary buffer.
 *
 * Return:    The dictionary id stored within the dictionary or 0 if the
 *            dictionary is not a zstd dictionary. If it returns 0 the
 *            dictionary can still be loaded as a content-only dictionary.
 */
unsigned int ZSTD_getDictID_fromDict(const void *dict, size_t dictSize);

/**
 * ZSTD_getDictID_fromDDict() - returns the dictionary id stored in a ZSTD_DDict
 * @ddict: The ddict to find the id of.
 *
 * Return: The dictionary id stored within `ddict` or 0 if the dictionary is not
 *         a zstd dictionary. If it returns 0 `ddict` will be loaded as a
 *         content-only dictionary.
 */
unsigned int ZSTD_getDictID_fromDDict(const ZSTD_DDict *ddict);

/**
 * ZSTD_getDictID_fromFrame() - returns the dictionary id stored in a zstd frame
 * @src:     Source buffer. It must be a zstd encoded frame.
 * @srcSize: The size of the source buffer. It must be at least as large as the
 *           frame header. `ZSTD_frameHeaderSize_max` is always large enough.
 *
 * Return:   The dictionary id required to decompress the frame stored within
 *           `src` or 0 if the dictionary id could not be decoded. It can return
 *           0 if the frame does not require a dictionary, the dictionary id
 *           wasn't stored in the frame, `src` is not a zstd frame, or `srcSize`
 *           is too small.
 */
unsigned int ZSTD_getDictID_fromFrame(const void *src, size_t srcSize);

/**
 * struct ZSTD_frameParams - zstd frame parameters stored in the frame header
 * @frameContentSize: The frame content size, or 0 if not present.
 * @windowSize:       The window size, or 0 if the frame is a skippable frame.
 * @dictID:           The dictionary id, or 0 if not present.
 * @checksumFlag:     Whether a checksum was used.
 */
typedef struct {
	unsigned long long frameContentSize;
	unsigned int windowSize;
	unsigned int dictID;
	unsigned int checksumFlag;
} ZSTD_frameParams;

/**
 * ZSTD_getFrameParams() - extracts parameters from a zstd or skippable frame
 * @fparamsPtr: On success the frame parameters are written here.
 * @src:        The source buffer. It must point to a zstd or skippable frame.
 * @srcSize:    The size of the source buffer. `ZSTD_frameHeaderSize_max` is
 *              always large enough to succeed.
 *
 * Return:      0 on success. If more data is required it returns how many bytes
 *              must be provided to make forward progress. Otherwise it returns
 *              an error, which can be checked using ZSTD_isError().
 */
size_t ZSTD_getFrameParams(ZSTD_frameParams *fparamsPtr, const void *src,
	size_t srcSize);

/*-*****************************************************************************
 * Buffer-less and synchronous inner streaming functions
 *
 * This is an advanced API, giving full control over buffer management, for
 * users which need direct control over memory.
 * But it's also a complex one, with many restrictions (documented below).
 * Prefer using normal streaming API for an easier experience
 ******************************************************************************/

/*-*****************************************************************************
 * Buffer-less streaming compression (synchronous mode)
 *
 * A ZSTD_CCtx object is required to track streaming operations.
 * Use ZSTD_initCCtx() to initialize a context.
 * ZSTD_CCtx object can be re-used multiple times within successive compression
 * operations.
 *
 * Start by initializing a context.
 * Use ZSTD_compressBegin(), or ZSTD_compressBegin_usingDict() for dictionary
 * compression,
 * or ZSTD_compressBegin_advanced(), for finer parameter control.
 * It's also possible to duplicate a reference context which has already been
 * initialized, using ZSTD_copyCCtx()
 *
 * Then, consume your input using ZSTD_compressContinue().
 * There are some important considerations to keep in mind when using this
 * advanced function :
 * - ZSTD_compressContinue() has no internal buffer. It uses externally provided
 *   buffer only.
 * - Interface is synchronous : input is consumed entirely and produce 1+
 *   (or more) compressed blocks.
 * - Caller must ensure there is enough space in `dst` to store compressed data
 *   under worst case scenario. Worst case evaluation is provided by
 *   ZSTD_compressBound().
 *   ZSTD_compressContinue() doesn't guarantee recover after a failed
 *   compression.
 * - ZSTD_compressContinue() presumes prior input ***is still accessible and
 *   unmodified*** (up to maximum distance size, see WindowLog).
 *   It remembers all previous contiguous blocks, plus one separated memory
 *   segment (which can itself consists of multiple contiguous blocks)
 * - ZSTD_compressContinue() detects that prior input has been overwritten when
 *   `src` buffer overlaps. In which case, it will "discard" the relevant memory
 *   section from its history.
 *
 * Finish a frame with ZSTD_compressEnd(), which will write the last block(s)
 * and optional checksum. It's possible to use srcSize==0, in which case, it
 * will write a final empty block to end the frame. Without last block mark,
 * frames will be considered unfinished (corrupted) by decoders.
 *
 * `ZSTD_CCtx` object can be re-used (ZSTD_compressBegin()) to compress some new
 * frame.
 ******************************************************************************/

/*=====   Buffer-less streaming compression functions  =====*/
size_t ZSTD_compressBegin(ZSTD_CCtx *cctx, int compressionLevel);
size_t ZSTD_compressBegin_usingDict(ZSTD_CCtx *cctx, const void *dict,
	size_t dictSize, int compressionLevel);
size_t ZSTD_compressBegin_advanced(ZSTD_CCtx *cctx, const void *dict,
	size_t dictSize, ZSTD_parameters params,
	unsigned long long pledgedSrcSize);
size_t ZSTD_copyCCtx(ZSTD_CCtx *cctx, const ZSTD_CCtx *preparedCCtx,
	unsigned long long pledgedSrcSize);
size_t ZSTD_compressBegin_usingCDict(ZSTD_CCtx *cctx, const ZSTD_CDict *cdict,
	unsigned long long pledgedSrcSize);
size_t ZSTD_compressContinue(ZSTD_CCtx *cctx, void *dst, size_t dstCapacity,
	const void *src, size_t srcSize);
size_t ZSTD_compressEnd(ZSTD_CCtx *cctx, void *dst, size_t dstCapacity,
	const void *src, size_t srcSize);



/*-*****************************************************************************
 * Buffer-less streaming decompression (synchronous mode)
 *
 * A ZSTD_DCtx object is required to track streaming operations.
 * Use ZSTD_initDCtx() to initialize a context.
 * A ZSTD_DCtx object can be re-used multiple times.
 *
 * First typical operation is to retrieve frame parameters, using
 * ZSTD_getFrameParams(). It fills a ZSTD_frameParams structure which provide
 * important information to correctly decode the frame, such as the minimum
 * rolling buffer size to allocate to decompress data (`windowSize`), and the
 * dictionary ID used.
 * Note: content size is optional, it may not be present. 0 means unknown.
 * Note that these values could be wrong, either because of data malformation,
 * or because an attacker is spoofing deliberate false information. As a
 * consequence, check that values remain within valid application range,
 * especially `windowSize`, before allocation. Each application can set its own
 * limit, depending on local restrictions. For extended interoperability, it is
 * recommended to support at least 8 MB.
 * Frame parameters are extracted from the beginning of the compressed frame.
 * Data fragment must be large enough to ensure successful decoding, typically
 * `ZSTD_frameHeaderSize_max` bytes.
 * Result: 0: successful decoding, the `ZSTD_frameParams` structure is filled.
 *        >0: `srcSize` is too small, provide at least this many bytes.
 *        errorCode, which can be tested using ZSTD_isError().
 *
 * Start decompression, with ZSTD_decompressBegin() or
 * ZSTD_decompressBegin_usingDict(). Alternatively, you can copy a prepared
 * context, using ZSTD_copyDCtx().
 *
 * Then use ZSTD_nextSrcSizeToDecompress() and ZSTD_decompressContinue()
 * alternatively.
 * ZSTD_nextSrcSizeToDecompress() tells how many bytes to provide as 'srcSize'
 * to ZSTD_decompressContinue().
 * ZSTD_decompressContinue() requires this _exact_ amount of bytes, or it will
 * fail.
 *
 * The result of ZSTD_decompressContinue() is the number of bytes regenerated
 * within 'dst' (necessarily <= dstCapacity). It can be zero, which is not an
 * error; it just means ZSTD_decompressContinue() has decoded some metadata
 * item. It can also be an error code, which can be tested with ZSTD_isError().
 *
 * ZSTD_decompressContinue() needs previous data blocks during decompression, up
 * to `windowSize`. They should preferably be located contiguously, prior to
 * current block. Alternatively, a round buffer of sufficient size is also
 * possible. Sufficient size is determined by frame parameters.
 * ZSTD_decompressContinue() is very sensitive to contiguity, if 2 blocks don't
 * follow each other, make sure that either the compressor breaks contiguity at
 * the same place, or that previous contiguous segment is large enough to
 * properly handle maximum back-reference.
 *
 * A frame is fully decoded when ZSTD_nextSrcSizeToDecompress() returns zero.
 * Context can then be reset to start a new decompression.
 *
 * Note: it's possible to know if next input to present is a header or a block,
 * using ZSTD_nextInputType(). This information is not required to properly
 * decode a frame.
 *
 * == Special case: skippable frames ==
 *
 * Skippable frames allow integration of user-defined data into a flow of
 * concatenated frames. Skippable frames will be ignored (skipped) by a
 * decompressor. The format of skippable frames is as follows:
 * a) Skippable frame ID - 4 Bytes, Little endian format, any value from
 *    0x184D2A50 to 0x184D2A5F
 * b) Frame Size - 4 Bytes, Little endian format, unsigned 32-bits
 * c) Frame Content - any content (User Data) of length equal to Frame Size
 * For skippable frames ZSTD_decompressContinue() always returns 0.
 * For skippable frames ZSTD_getFrameParams() returns fparamsPtr->windowLog==0
 * what means that a frame is skippable.
 * Note: If fparamsPtr->frameContentSize==0, it is ambiguous: the frame might
 *       actually be a zstd encoded frame with no content. For purposes of
 *       decompression, it is valid in both cases to skip the frame using
 *       ZSTD_findFrameCompressedSize() to find its size in bytes.
 * It also returns frame size as fparamsPtr->frameContentSize.
 ******************************************************************************/

/*=====   Buffer-less streaming decompression functions  =====*/
size_t ZSTD_decompressBegin(ZSTD_DCtx *dctx);
size_t ZSTD_decompressBegin_usingDict(ZSTD_DCtx *dctx, const void *dict,
	size_t dictSize);
void   ZSTD_copyDCtx(ZSTD_DCtx *dctx, const ZSTD_DCtx *preparedDCtx);
size_t ZSTD_nextSrcSizeToDecompress(ZSTD_DCtx *dctx);
size_t ZSTD_decompressContinue(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity,
	const void *src, size_t srcSize);
typedef enum {
	ZSTDnit_frameHeader,
	ZSTDnit_blockHeader,
	ZSTDnit_block,
	ZSTDnit_lastBlock,
	ZSTDnit_checksum,
	ZSTDnit_skippableFrame
} ZSTD_nextInputType_e;
ZSTD_nextInputType_e ZSTD_nextInputType(ZSTD_DCtx *dctx);

/*-*****************************************************************************
 * Block functions
 *
 * Block functions produce and decode raw zstd blocks, without frame metadata.
 * Frame metadata cost is typically ~18 bytes, which can be non-negligible for
 * very small blocks (< 100 bytes). User will have to take in charge required
 * information to regenerate data, such as compressed and content sizes.
 *
 * A few rules to respect:
 * - Compressing and decompressing require a context structure
 *   + Use ZSTD_initCCtx() and ZSTD_initDCtx()
 * - It is necessary to init context before starting
 *   + compression : ZSTD_compressBegin()
 *   + decompression : ZSTD_decompressBegin()
 *   + variants _usingDict() are also allowed
 *   + copyCCtx() and copyDCtx() work too
 * - Block size is limited, it must be <= ZSTD_getBlockSizeMax()
 *   + If you need to compress more, cut data into multiple blocks
 *   + Consider using the regular ZSTD_compress() instead, as frame metadata
 *     costs become negligible when source size is large.
 * - When a block is considered not compressible enough, ZSTD_compressBlock()
 *   result will be zero. In which case, nothing is produced into `dst`.
 *   + User must test for such outcome and deal directly with uncompressed data
 *   + ZSTD_decompressBlock() doesn't accept uncompressed data as input!!!
 *   + In case of multiple successive blocks, decoder must be informed of
 *     uncompressed block existence to follow proper history. Use
 *     ZSTD_insertBlock() in such a case.
 ******************************************************************************/

/* Define for static allocation */
#define ZSTD_BLOCKSIZE_ABSOLUTEMAX (128 * 1024)
/*=====   Raw zstd block functions  =====*/
size_t ZSTD_getBlockSizeMax(ZSTD_CCtx *cctx);
size_t ZSTD_compressBlock(ZSTD_CCtx *cctx, void *dst, size_t dstCapacity,
	const void *src, size_t srcSize);
size_t ZSTD_decompressBlock(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity,
	const void *src, size_t srcSize);
size_t ZSTD_insertBlock(ZSTD_DCtx *dctx, const void *blockStart,
	size_t blockSize);

#endif  /* ZSTD_H */
