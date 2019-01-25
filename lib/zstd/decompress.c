/**
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

/* ***************************************************************
*  Tuning parameters
*****************************************************************/
/*!
*  MAXWINDOWSIZE_DEFAULT :
*  maximum window size accepted by DStream, by default.
*  Frames requiring more memory will be rejected.
*/
#ifndef ZSTD_MAXWINDOWSIZE_DEFAULT
#define ZSTD_MAXWINDOWSIZE_DEFAULT ((1 << ZSTD_WINDOWLOG_MAX) + 1) /* defined within zstd.h */
#endif

/*-*******************************************************
*  Dependencies
*********************************************************/
#include "fse.h"
#include "huf.h"
#include "mem.h" /* low level memory routines */
#include "zstd_internal.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h> /* memcpy, memmove, memset */

#define ZSTD_PREFETCH(ptr) __builtin_prefetch(ptr, 0, 0)

/*-*************************************
*  Macros
***************************************/
#define ZSTD_isError ERR_isError /* for inlining */
#define FSE_isError ERR_isError
#define HUF_isError ERR_isError

/*_*******************************************************
*  Memory operations
**********************************************************/
static void ZSTD_copy4(void *dst, const void *src) { memcpy(dst, src, 4); }

/*-*************************************************************
*   Context management
***************************************************************/
typedef enum {
	ZSTDds_getFrameHeaderSize,
	ZSTDds_decodeFrameHeader,
	ZSTDds_decodeBlockHeader,
	ZSTDds_decompressBlock,
	ZSTDds_decompressLastBlock,
	ZSTDds_checkChecksum,
	ZSTDds_decodeSkippableHeader,
	ZSTDds_skipFrame
} ZSTD_dStage;

typedef struct {
	FSE_DTable LLTable[FSE_DTABLE_SIZE_U32(LLFSELog)];
	FSE_DTable OFTable[FSE_DTABLE_SIZE_U32(OffFSELog)];
	FSE_DTable MLTable[FSE_DTABLE_SIZE_U32(MLFSELog)];
	HUF_DTable hufTable[HUF_DTABLE_SIZE(HufLog)]; /* can accommodate HUF_decompress4X */
	U64 workspace[HUF_DECOMPRESS_WORKSPACE_SIZE_U32 / 2];
	U32 rep[ZSTD_REP_NUM];
} ZSTD_entropyTables_t;

struct ZSTD_DCtx_s {
	const FSE_DTable *LLTptr;
	const FSE_DTable *MLTptr;
	const FSE_DTable *OFTptr;
	const HUF_DTable *HUFptr;
	ZSTD_entropyTables_t entropy;
	const void *previousDstEnd; /* detect continuity */
	const void *base;	   /* start of curr segment */
	const void *vBase;	  /* virtual start of previous segment if it was just before curr one */
	const void *dictEnd;	/* end of previous segment */
	size_t expected;
	ZSTD_frameParams fParams;
	blockType_e bType; /* used in ZSTD_decompressContinue(), to transfer blockType between header decoding and block decoding stages */
	ZSTD_dStage stage;
	U32 litEntropy;
	U32 fseEntropy;
	struct xxh64_state xxhState;
	size_t headerSize;
	U32 dictID;
	const BYTE *litPtr;
	ZSTD_customMem customMem;
	size_t litSize;
	size_t rleSize;
	BYTE litBuffer[ZSTD_BLOCKSIZE_ABSOLUTEMAX + WILDCOPY_OVERLENGTH];
	BYTE headerBuffer[ZSTD_FRAMEHEADERSIZE_MAX];
}; /* typedef'd to ZSTD_DCtx within "zstd.h" */

size_t ZSTD_DCtxWorkspaceBound(void) { return ZSTD_ALIGN(sizeof(ZSTD_stack)) + ZSTD_ALIGN(sizeof(ZSTD_DCtx)); }

size_t ZSTD_decompressBegin(ZSTD_DCtx *dctx)
{
	dctx->expected = ZSTD_frameHeaderSize_prefix;
	dctx->stage = ZSTDds_getFrameHeaderSize;
	dctx->previousDstEnd = NULL;
	dctx->base = NULL;
	dctx->vBase = NULL;
	dctx->dictEnd = NULL;
	dctx->entropy.hufTable[0] = (HUF_DTable)((HufLog)*0x1000001); /* cover both little and big endian */
	dctx->litEntropy = dctx->fseEntropy = 0;
	dctx->dictID = 0;
	ZSTD_STATIC_ASSERT(sizeof(dctx->entropy.rep) == sizeof(repStartValue));
	memcpy(dctx->entropy.rep, repStartValue, sizeof(repStartValue)); /* initial repcodes */
	dctx->LLTptr = dctx->entropy.LLTable;
	dctx->MLTptr = dctx->entropy.MLTable;
	dctx->OFTptr = dctx->entropy.OFTable;
	dctx->HUFptr = dctx->entropy.hufTable;
	return 0;
}

ZSTD_DCtx *ZSTD_createDCtx_advanced(ZSTD_customMem customMem)
{
	ZSTD_DCtx *dctx;

	if (!customMem.customAlloc || !customMem.customFree)
		return NULL;

	dctx = (ZSTD_DCtx *)ZSTD_malloc(sizeof(ZSTD_DCtx), customMem);
	if (!dctx)
		return NULL;
	memcpy(&dctx->customMem, &customMem, sizeof(customMem));
	ZSTD_decompressBegin(dctx);
	return dctx;
}

ZSTD_DCtx *ZSTD_initDCtx(void *workspace, size_t workspaceSize)
{
	ZSTD_customMem const stackMem = ZSTD_initStack(workspace, workspaceSize);
	return ZSTD_createDCtx_advanced(stackMem);
}

size_t ZSTD_freeDCtx(ZSTD_DCtx *dctx)
{
	if (dctx == NULL)
		return 0; /* support free on NULL */
	ZSTD_free(dctx, dctx->customMem);
	return 0; /* reserved as a potential error code in the future */
}

void ZSTD_copyDCtx(ZSTD_DCtx *dstDCtx, const ZSTD_DCtx *srcDCtx)
{
	size_t const workSpaceSize = (ZSTD_BLOCKSIZE_ABSOLUTEMAX + WILDCOPY_OVERLENGTH) + ZSTD_frameHeaderSize_max;
	memcpy(dstDCtx, srcDCtx, sizeof(ZSTD_DCtx) - workSpaceSize); /* no need to copy workspace */
}

static void ZSTD_refDDict(ZSTD_DCtx *dstDCtx, const ZSTD_DDict *ddict);

/*-*************************************************************
*   Decompression section
***************************************************************/

/*! ZSTD_isFrame() :
 *  Tells if the content of `buffer` starts with a valid Frame Identifier.
 *  Note : Frame Identifier is 4 bytes. If `size < 4`, @return will always be 0.
 *  Note 2 : Legacy Frame Identifiers are considered valid only if Legacy Support is enabled.
 *  Note 3 : Skippable Frame Identifiers are considered valid. */
unsigned ZSTD_isFrame(const void *buffer, size_t size)
{
	if (size < 4)
		return 0;
	{
		U32 const magic = ZSTD_readLE32(buffer);
		if (magic == ZSTD_MAGICNUMBER)
			return 1;
		if ((magic & 0xFFFFFFF0U) == ZSTD_MAGIC_SKIPPABLE_START)
			return 1;
	}
	return 0;
}

/** ZSTD_frameHeaderSize() :
*   srcSize must be >= ZSTD_frameHeaderSize_prefix.
*   @return : size of the Frame Header */
static size_t ZSTD_frameHeaderSize(const void *src, size_t srcSize)
{
	if (srcSize < ZSTD_frameHeaderSize_prefix)
		return ERROR(srcSize_wrong);
	{
		BYTE const fhd = ((const BYTE *)src)[4];
		U32 const dictID = fhd & 3;
		U32 const singleSegment = (fhd >> 5) & 1;
		U32 const fcsId = fhd >> 6;
		return ZSTD_frameHeaderSize_prefix + !singleSegment + ZSTD_did_fieldSize[dictID] + ZSTD_fcs_fieldSize[fcsId] + (singleSegment && !fcsId);
	}
}

/** ZSTD_getFrameParams() :
*   decode Frame Header, or require larger `srcSize`.
*   @return : 0, `fparamsPtr` is correctly filled,
*            >0, `srcSize` is too small, result is expected `srcSize`,
*             or an error code, which can be tested using ZSTD_isError() */
size_t ZSTD_getFrameParams(ZSTD_frameParams *fparamsPtr, const void *src, size_t srcSize)
{
	const BYTE *ip = (const BYTE *)src;

	if (srcSize < ZSTD_frameHeaderSize_prefix)
		return ZSTD_frameHeaderSize_prefix;
	if (ZSTD_readLE32(src) != ZSTD_MAGICNUMBER) {
		if ((ZSTD_readLE32(src) & 0xFFFFFFF0U) == ZSTD_MAGIC_SKIPPABLE_START) {
			if (srcSize < ZSTD_skippableHeaderSize)
				return ZSTD_skippableHeaderSize; /* magic number + skippable frame length */
			memset(fparamsPtr, 0, sizeof(*fparamsPtr));
			fparamsPtr->frameContentSize = ZSTD_readLE32((const char *)src + 4);
			fparamsPtr->windowSize = 0; /* windowSize==0 means a frame is skippable */
			return 0;
		}
		return ERROR(prefix_unknown);
	}

	/* ensure there is enough `srcSize` to fully read/decode frame header */
	{
		size_t const fhsize = ZSTD_frameHeaderSize(src, srcSize);
		if (srcSize < fhsize)
			return fhsize;
	}

	{
		BYTE const fhdByte = ip[4];
		size_t pos = 5;
		U32 const dictIDSizeCode = fhdByte & 3;
		U32 const checksumFlag = (fhdByte >> 2) & 1;
		U32 const singleSegment = (fhdByte >> 5) & 1;
		U32 const fcsID = fhdByte >> 6;
		U32 const windowSizeMax = 1U << ZSTD_WINDOWLOG_MAX;
		U32 windowSize = 0;
		U32 dictID = 0;
		U64 frameContentSize = 0;
		if ((fhdByte & 0x08) != 0)
			return ERROR(frameParameter_unsupported); /* reserved bits, which must be zero */
		if (!singleSegment) {
			BYTE const wlByte = ip[pos++];
			U32 const windowLog = (wlByte >> 3) + ZSTD_WINDOWLOG_ABSOLUTEMIN;
			if (windowLog > ZSTD_WINDOWLOG_MAX)
				return ERROR(frameParameter_windowTooLarge); /* avoids issue with 1 << windowLog */
			windowSize = (1U << windowLog);
			windowSize += (windowSize >> 3) * (wlByte & 7);
		}

		switch (dictIDSizeCode) {
		default: /* impossible */
		case 0: break;
		case 1:
			dictID = ip[pos];
			pos++;
			break;
		case 2:
			dictID = ZSTD_readLE16(ip + pos);
			pos += 2;
			break;
		case 3:
			dictID = ZSTD_readLE32(ip + pos);
			pos += 4;
			break;
		}
		switch (fcsID) {
		default: /* impossible */
		case 0:
			if (singleSegment)
				frameContentSize = ip[pos];
			break;
		case 1: frameContentSize = ZSTD_readLE16(ip + pos) + 256; break;
		case 2: frameContentSize = ZSTD_readLE32(ip + pos); break;
		case 3: frameContentSize = ZSTD_readLE64(ip + pos); break;
		}
		if (!windowSize)
			windowSize = (U32)frameContentSize;
		if (windowSize > windowSizeMax)
			return ERROR(frameParameter_windowTooLarge);
		fparamsPtr->frameContentSize = frameContentSize;
		fparamsPtr->windowSize = windowSize;
		fparamsPtr->dictID = dictID;
		fparamsPtr->checksumFlag = checksumFlag;
	}
	return 0;
}

/** ZSTD_getFrameContentSize() :
*   compatible with legacy mode
*   @return : decompressed size of the single frame pointed to be `src` if known, otherwise
*             - ZSTD_CONTENTSIZE_UNKNOWN if the size cannot be determined
*             - ZSTD_CONTENTSIZE_ERROR if an error occurred (e.g. invalid magic number, srcSize too small) */
unsigned long long ZSTD_getFrameContentSize(const void *src, size_t srcSize)
{
	{
		ZSTD_frameParams fParams;
		if (ZSTD_getFrameParams(&fParams, src, srcSize) != 0)
			return ZSTD_CONTENTSIZE_ERROR;
		if (fParams.windowSize == 0) {
			/* Either skippable or empty frame, size == 0 either way */
			return 0;
		} else if (fParams.frameContentSize != 0) {
			return fParams.frameContentSize;
		} else {
			return ZSTD_CONTENTSIZE_UNKNOWN;
		}
	}
}

/** ZSTD_findDecompressedSize() :
 *  compatible with legacy mode
 *  `srcSize` must be the exact length of some number of ZSTD compressed and/or
 *      skippable frames
 *  @return : decompressed size of the frames contained */
unsigned long long ZSTD_findDecompressedSize(const void *src, size_t srcSize)
{
	{
		unsigned long long totalDstSize = 0;
		while (srcSize >= ZSTD_frameHeaderSize_prefix) {
			const U32 magicNumber = ZSTD_readLE32(src);

			if ((magicNumber & 0xFFFFFFF0U) == ZSTD_MAGIC_SKIPPABLE_START) {
				size_t skippableSize;
				if (srcSize < ZSTD_skippableHeaderSize)
					return ERROR(srcSize_wrong);
				skippableSize = ZSTD_readLE32((const BYTE *)src + 4) + ZSTD_skippableHeaderSize;
				if (srcSize < skippableSize) {
					return ZSTD_CONTENTSIZE_ERROR;
				}

				src = (const BYTE *)src + skippableSize;
				srcSize -= skippableSize;
				continue;
			}

			{
				unsigned long long const ret = ZSTD_getFrameContentSize(src, srcSize);
				if (ret >= ZSTD_CONTENTSIZE_ERROR)
					return ret;

				/* check for overflow */
				if (totalDstSize + ret < totalDstSize)
					return ZSTD_CONTENTSIZE_ERROR;
				totalDstSize += ret;
			}
			{
				size_t const frameSrcSize = ZSTD_findFrameCompressedSize(src, srcSize);
				if (ZSTD_isError(frameSrcSize)) {
					return ZSTD_CONTENTSIZE_ERROR;
				}

				src = (const BYTE *)src + frameSrcSize;
				srcSize -= frameSrcSize;
			}
		}

		if (srcSize) {
			return ZSTD_CONTENTSIZE_ERROR;
		}

		return totalDstSize;
	}
}

/** ZSTD_decodeFrameHeader() :
*   `headerSize` must be the size provided by ZSTD_frameHeaderSize().
*   @return : 0 if success, or an error code, which can be tested using ZSTD_isError() */
static size_t ZSTD_decodeFrameHeader(ZSTD_DCtx *dctx, const void *src, size_t headerSize)
{
	size_t const result = ZSTD_getFrameParams(&(dctx->fParams), src, headerSize);
	if (ZSTD_isError(result))
		return result; /* invalid header */
	if (result > 0)
		return ERROR(srcSize_wrong); /* headerSize too small */
	if (dctx->fParams.dictID && (dctx->dictID != dctx->fParams.dictID))
		return ERROR(dictionary_wrong);
	if (dctx->fParams.checksumFlag)
		xxh64_reset(&dctx->xxhState, 0);
	return 0;
}

typedef struct {
	blockType_e blockType;
	U32 lastBlock;
	U32 origSize;
} blockProperties_t;

/*! ZSTD_getcBlockSize() :
*   Provides the size of compressed block from block header `src` */
size_t ZSTD_getcBlockSize(const void *src, size_t srcSize, blockProperties_t *bpPtr)
{
	if (srcSize < ZSTD_blockHeaderSize)
		return ERROR(srcSize_wrong);
	{
		U32 const cBlockHeader = ZSTD_readLE24(src);
		U32 const cSize = cBlockHeader >> 3;
		bpPtr->lastBlock = cBlockHeader & 1;
		bpPtr->blockType = (blockType_e)((cBlockHeader >> 1) & 3);
		bpPtr->origSize = cSize; /* only useful for RLE */
		if (bpPtr->blockType == bt_rle)
			return 1;
		if (bpPtr->blockType == bt_reserved)
			return ERROR(corruption_detected);
		return cSize;
	}
}

static size_t ZSTD_copyRawBlock(void *dst, size_t dstCapacity, const void *src, size_t srcSize)
{
	if (srcSize > dstCapacity)
		return ERROR(dstSize_tooSmall);
	memcpy(dst, src, srcSize);
	return srcSize;
}

static size_t ZSTD_setRleBlock(void *dst, size_t dstCapacity, const void *src, size_t srcSize, size_t regenSize)
{
	if (srcSize != 1)
		return ERROR(srcSize_wrong);
	if (regenSize > dstCapacity)
		return ERROR(dstSize_tooSmall);
	memset(dst, *(const BYTE *)src, regenSize);
	return regenSize;
}

/*! ZSTD_decodeLiteralsBlock() :
	@return : nb of bytes read from src (< srcSize ) */
size_t ZSTD_decodeLiteralsBlock(ZSTD_DCtx *dctx, const void *src, size_t srcSize) /* note : srcSize < BLOCKSIZE */
{
	if (srcSize < MIN_CBLOCK_SIZE)
		return ERROR(corruption_detected);

	{
		const BYTE *const istart = (const BYTE *)src;
		symbolEncodingType_e const litEncType = (symbolEncodingType_e)(istart[0] & 3);

		switch (litEncType) {
		case set_repeat:
			if (dctx->litEntropy == 0)
				return ERROR(dictionary_corrupted);
		/* fall-through */
		case set_compressed:
			if (srcSize < 5)
				return ERROR(corruption_detected); /* srcSize >= MIN_CBLOCK_SIZE == 3; here we need up to 5 for case 3 */
			{
				size_t lhSize, litSize, litCSize;
				U32 singleStream = 0;
				U32 const lhlCode = (istart[0] >> 2) & 3;
				U32 const lhc = ZSTD_readLE32(istart);
				switch (lhlCode) {
				case 0:
				case 1:
				default: /* note : default is impossible, since lhlCode into [0..3] */
					/* 2 - 2 - 10 - 10 */
					singleStream = !lhlCode;
					lhSize = 3;
					litSize = (lhc >> 4) & 0x3FF;
					litCSize = (lhc >> 14) & 0x3FF;
					break;
				case 2:
					/* 2 - 2 - 14 - 14 */
					lhSize = 4;
					litSize = (lhc >> 4) & 0x3FFF;
					litCSize = lhc >> 18;
					break;
				case 3:
					/* 2 - 2 - 18 - 18 */
					lhSize = 5;
					litSize = (lhc >> 4) & 0x3FFFF;
					litCSize = (lhc >> 22) + (istart[4] << 10);
					break;
				}
				if (litSize > ZSTD_BLOCKSIZE_ABSOLUTEMAX)
					return ERROR(corruption_detected);
				if (litCSize + lhSize > srcSize)
					return ERROR(corruption_detected);

				if (HUF_isError(
					(litEncType == set_repeat)
					    ? (singleStream ? HUF_decompress1X_usingDTable(dctx->litBuffer, litSize, istart + lhSize, litCSize, dctx->HUFptr)
							    : HUF_decompress4X_usingDTable(dctx->litBuffer, litSize, istart + lhSize, litCSize, dctx->HUFptr))
					    : (singleStream
						   ? HUF_decompress1X2_DCtx_wksp(dctx->entropy.hufTable, dctx->litBuffer, litSize, istart + lhSize, litCSize,
										 dctx->entropy.workspace, sizeof(dctx->entropy.workspace))
						   : HUF_decompress4X_hufOnly_wksp(dctx->entropy.hufTable, dctx->litBuffer, litSize, istart + lhSize, litCSize,
										   dctx->entropy.workspace, sizeof(dctx->entropy.workspace)))))
					return ERROR(corruption_detected);

				dctx->litPtr = dctx->litBuffer;
				dctx->litSize = litSize;
				dctx->litEntropy = 1;
				if (litEncType == set_compressed)
					dctx->HUFptr = dctx->entropy.hufTable;
				memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
				return litCSize + lhSize;
			}

		case set_basic: {
			size_t litSize, lhSize;
			U32 const lhlCode = ((istart[0]) >> 2) & 3;
			switch (lhlCode) {
			case 0:
			case 2:
			default: /* note : default is impossible, since lhlCode into [0..3] */
				lhSize = 1;
				litSize = istart[0] >> 3;
				break;
			case 1:
				lhSize = 2;
				litSize = ZSTD_readLE16(istart) >> 4;
				break;
			case 3:
				lhSize = 3;
				litSize = ZSTD_readLE24(istart) >> 4;
				break;
			}

			if (lhSize + litSize + WILDCOPY_OVERLENGTH > srcSize) { /* risk reading beyond src buffer with wildcopy */
				if (litSize + lhSize > srcSize)
					return ERROR(corruption_detected);
				memcpy(dctx->litBuffer, istart + lhSize, litSize);
				dctx->litPtr = dctx->litBuffer;
				dctx->litSize = litSize;
				memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
				return lhSize + litSize;
			}
			/* direct reference into compressed stream */
			dctx->litPtr = istart + lhSize;
			dctx->litSize = litSize;
			return lhSize + litSize;
		}

		case set_rle: {
			U32 const lhlCode = ((istart[0]) >> 2) & 3;
			size_t litSize, lhSize;
			switch (lhlCode) {
			case 0:
			case 2:
			default: /* note : default is impossible, since lhlCode into [0..3] */
				lhSize = 1;
				litSize = istart[0] >> 3;
				break;
			case 1:
				lhSize = 2;
				litSize = ZSTD_readLE16(istart) >> 4;
				break;
			case 3:
				lhSize = 3;
				litSize = ZSTD_readLE24(istart) >> 4;
				if (srcSize < 4)
					return ERROR(corruption_detected); /* srcSize >= MIN_CBLOCK_SIZE == 3; here we need lhSize+1 = 4 */
				break;
			}
			if (litSize > ZSTD_BLOCKSIZE_ABSOLUTEMAX)
				return ERROR(corruption_detected);
			memset(dctx->litBuffer, istart[lhSize], litSize + WILDCOPY_OVERLENGTH);
			dctx->litPtr = dctx->litBuffer;
			dctx->litSize = litSize;
			return lhSize + 1;
		}
		default:
			return ERROR(corruption_detected); /* impossible */
		}
	}
}

typedef union {
	FSE_decode_t realData;
	U32 alignedBy4;
} FSE_decode_t4;

static const FSE_decode_t4 LL_defaultDTable[(1 << LL_DEFAULTNORMLOG) + 1] = {
    {{LL_DEFAULTNORMLOG, 1, 1}}, /* header : tableLog, fastMode, fastMode */
    {{0, 0, 4}},		 /* 0 : base, symbol, bits */
    {{16, 0, 4}},
    {{32, 1, 5}},
    {{0, 3, 5}},
    {{0, 4, 5}},
    {{0, 6, 5}},
    {{0, 7, 5}},
    {{0, 9, 5}},
    {{0, 10, 5}},
    {{0, 12, 5}},
    {{0, 14, 6}},
    {{0, 16, 5}},
    {{0, 18, 5}},
    {{0, 19, 5}},
    {{0, 21, 5}},
    {{0, 22, 5}},
    {{0, 24, 5}},
    {{32, 25, 5}},
    {{0, 26, 5}},
    {{0, 27, 6}},
    {{0, 29, 6}},
    {{0, 31, 6}},
    {{32, 0, 4}},
    {{0, 1, 4}},
    {{0, 2, 5}},
    {{32, 4, 5}},
    {{0, 5, 5}},
    {{32, 7, 5}},
    {{0, 8, 5}},
    {{32, 10, 5}},
    {{0, 11, 5}},
    {{0, 13, 6}},
    {{32, 16, 5}},
    {{0, 17, 5}},
    {{32, 19, 5}},
    {{0, 20, 5}},
    {{32, 22, 5}},
    {{0, 23, 5}},
    {{0, 25, 4}},
    {{16, 25, 4}},
    {{32, 26, 5}},
    {{0, 28, 6}},
    {{0, 30, 6}},
    {{48, 0, 4}},
    {{16, 1, 4}},
    {{32, 2, 5}},
    {{32, 3, 5}},
    {{32, 5, 5}},
    {{32, 6, 5}},
    {{32, 8, 5}},
    {{32, 9, 5}},
    {{32, 11, 5}},
    {{32, 12, 5}},
    {{0, 15, 6}},
    {{32, 17, 5}},
    {{32, 18, 5}},
    {{32, 20, 5}},
    {{32, 21, 5}},
    {{32, 23, 5}},
    {{32, 24, 5}},
    {{0, 35, 6}},
    {{0, 34, 6}},
    {{0, 33, 6}},
    {{0, 32, 6}},
}; /* LL_defaultDTable */

static const FSE_decode_t4 ML_defaultDTable[(1 << ML_DEFAULTNORMLOG) + 1] = {
    {{ML_DEFAULTNORMLOG, 1, 1}}, /* header : tableLog, fastMode, fastMode */
    {{0, 0, 6}},		 /* 0 : base, symbol, bits */
    {{0, 1, 4}},
    {{32, 2, 5}},
    {{0, 3, 5}},
    {{0, 5, 5}},
    {{0, 6, 5}},
    {{0, 8, 5}},
    {{0, 10, 6}},
    {{0, 13, 6}},
    {{0, 16, 6}},
    {{0, 19, 6}},
    {{0, 22, 6}},
    {{0, 25, 6}},
    {{0, 28, 6}},
    {{0, 31, 6}},
    {{0, 33, 6}},
    {{0, 35, 6}},
    {{0, 37, 6}},
    {{0, 39, 6}},
    {{0, 41, 6}},
    {{0, 43, 6}},
    {{0, 45, 6}},
    {{16, 1, 4}},
    {{0, 2, 4}},
    {{32, 3, 5}},
    {{0, 4, 5}},
    {{32, 6, 5}},
    {{0, 7, 5}},
    {{0, 9, 6}},
    {{0, 12, 6}},
    {{0, 15, 6}},
    {{0, 18, 6}},
    {{0, 21, 6}},
    {{0, 24, 6}},
    {{0, 27, 6}},
    {{0, 30, 6}},
    {{0, 32, 6}},
    {{0, 34, 6}},
    {{0, 36, 6}},
    {{0, 38, 6}},
    {{0, 40, 6}},
    {{0, 42, 6}},
    {{0, 44, 6}},
    {{32, 1, 4}},
    {{48, 1, 4}},
    {{16, 2, 4}},
    {{32, 4, 5}},
    {{32, 5, 5}},
    {{32, 7, 5}},
    {{32, 8, 5}},
    {{0, 11, 6}},
    {{0, 14, 6}},
    {{0, 17, 6}},
    {{0, 20, 6}},
    {{0, 23, 6}},
    {{0, 26, 6}},
    {{0, 29, 6}},
    {{0, 52, 6}},
    {{0, 51, 6}},
    {{0, 50, 6}},
    {{0, 49, 6}},
    {{0, 48, 6}},
    {{0, 47, 6}},
    {{0, 46, 6}},
}; /* ML_defaultDTable */

static const FSE_decode_t4 OF_defaultDTable[(1 << OF_DEFAULTNORMLOG) + 1] = {
    {{OF_DEFAULTNORMLOG, 1, 1}}, /* header : tableLog, fastMode, fastMode */
    {{0, 0, 5}},		 /* 0 : base, symbol, bits */
    {{0, 6, 4}},
    {{0, 9, 5}},
    {{0, 15, 5}},
    {{0, 21, 5}},
    {{0, 3, 5}},
    {{0, 7, 4}},
    {{0, 12, 5}},
    {{0, 18, 5}},
    {{0, 23, 5}},
    {{0, 5, 5}},
    {{0, 8, 4}},
    {{0, 14, 5}},
    {{0, 20, 5}},
    {{0, 2, 5}},
    {{16, 7, 4}},
    {{0, 11, 5}},
    {{0, 17, 5}},
    {{0, 22, 5}},
    {{0, 4, 5}},
    {{16, 8, 4}},
    {{0, 13, 5}},
    {{0, 19, 5}},
    {{0, 1, 5}},
    {{16, 6, 4}},
    {{0, 10, 5}},
    {{0, 16, 5}},
    {{0, 28, 5}},
    {{0, 27, 5}},
    {{0, 26, 5}},
    {{0, 25, 5}},
    {{0, 24, 5}},
}; /* OF_defaultDTable */

/*! ZSTD_buildSeqTable() :
	@return : nb bytes read from src,
			  or an error code if it fails, testable with ZSTD_isError()
*/
static size_t ZSTD_buildSeqTable(FSE_DTable *DTableSpace, const FSE_DTable **DTablePtr, symbolEncodingType_e type, U32 max, U32 maxLog, const void *src,
				 size_t srcSize, const FSE_decode_t4 *defaultTable, U32 flagRepeatTable, void *workspace, size_t workspaceSize)
{
	const void *const tmpPtr = defaultTable; /* bypass strict aliasing */
	switch (type) {
	case set_rle:
		if (!srcSize)
			return ERROR(srcSize_wrong);
		if ((*(const BYTE *)src) > max)
			return ERROR(corruption_detected);
		FSE_buildDTable_rle(DTableSpace, *(const BYTE *)src);
		*DTablePtr = DTableSpace;
		return 1;
	case set_basic: *DTablePtr = (const FSE_DTable *)tmpPtr; return 0;
	case set_repeat:
		if (!flagRepeatTable)
			return ERROR(corruption_detected);
		return 0;
	default: /* impossible */
	case set_compressed: {
		U32 tableLog;
		S16 *norm = (S16 *)workspace;
		size_t const spaceUsed32 = ALIGN(sizeof(S16) * (MaxSeq + 1), sizeof(U32)) >> 2;

		if ((spaceUsed32 << 2) > workspaceSize)
			return ERROR(GENERIC);
		workspace = (U32 *)workspace + spaceUsed32;
		workspaceSize -= (spaceUsed32 << 2);
		{
			size_t const headerSize = FSE_readNCount(norm, &max, &tableLog, src, srcSize);
			if (FSE_isError(headerSize))
				return ERROR(corruption_detected);
			if (tableLog > maxLog)
				return ERROR(corruption_detected);
			FSE_buildDTable_wksp(DTableSpace, norm, max, tableLog, workspace, workspaceSize);
			*DTablePtr = DTableSpace;
			return headerSize;
		}
	}
	}
}

size_t ZSTD_decodeSeqHeaders(ZSTD_DCtx *dctx, int *nbSeqPtr, const void *src, size_t srcSize)
{
	const BYTE *const istart = (const BYTE *const)src;
	const BYTE *const iend = istart + srcSize;
	const BYTE *ip = istart;

	/* check */
	if (srcSize < MIN_SEQUENCES_SIZE)
		return ERROR(srcSize_wrong);

	/* SeqHead */
	{
		int nbSeq = *ip++;
		if (!nbSeq) {
			*nbSeqPtr = 0;
			return 1;
		}
		if (nbSeq > 0x7F) {
			if (nbSeq == 0xFF) {
				if (ip + 2 > iend)
					return ERROR(srcSize_wrong);
				nbSeq = ZSTD_readLE16(ip) + LONGNBSEQ, ip += 2;
			} else {
				if (ip >= iend)
					return ERROR(srcSize_wrong);
				nbSeq = ((nbSeq - 0x80) << 8) + *ip++;
			}
		}
		*nbSeqPtr = nbSeq;
	}

	/* FSE table descriptors */
	if (ip + 4 > iend)
		return ERROR(srcSize_wrong); /* minimum possible size */
	{
		symbolEncodingType_e const LLtype = (symbolEncodingType_e)(*ip >> 6);
		symbolEncodingType_e const OFtype = (symbolEncodingType_e)((*ip >> 4) & 3);
		symbolEncodingType_e const MLtype = (symbolEncodingType_e)((*ip >> 2) & 3);
		ip++;

		/* Build DTables */
		{
			size_t const llhSize = ZSTD_buildSeqTable(dctx->entropy.LLTable, &dctx->LLTptr, LLtype, MaxLL, LLFSELog, ip, iend - ip,
								  LL_defaultDTable, dctx->fseEntropy, dctx->entropy.workspace, sizeof(dctx->entropy.workspace));
			if (ZSTD_isError(llhSize))
				return ERROR(corruption_detected);
			ip += llhSize;
		}
		{
			size_t const ofhSize = ZSTD_buildSeqTable(dctx->entropy.OFTable, &dctx->OFTptr, OFtype, MaxOff, OffFSELog, ip, iend - ip,
								  OF_defaultDTable, dctx->fseEntropy, dctx->entropy.workspace, sizeof(dctx->entropy.workspace));
			if (ZSTD_isError(ofhSize))
				return ERROR(corruption_detected);
			ip += ofhSize;
		}
		{
			size_t const mlhSize = ZSTD_buildSeqTable(dctx->entropy.MLTable, &dctx->MLTptr, MLtype, MaxML, MLFSELog, ip, iend - ip,
								  ML_defaultDTable, dctx->fseEntropy, dctx->entropy.workspace, sizeof(dctx->entropy.workspace));
			if (ZSTD_isError(mlhSize))
				return ERROR(corruption_detected);
			ip += mlhSize;
		}
	}

	return ip - istart;
}

typedef struct {
	size_t litLength;
	size_t matchLength;
	size_t offset;
	const BYTE *match;
} seq_t;

typedef struct {
	BIT_DStream_t DStream;
	FSE_DState_t stateLL;
	FSE_DState_t stateOffb;
	FSE_DState_t stateML;
	size_t prevOffset[ZSTD_REP_NUM];
	const BYTE *base;
	size_t pos;
	uPtrDiff gotoDict;
} seqState_t;

FORCE_NOINLINE
size_t ZSTD_execSequenceLast7(BYTE *op, BYTE *const oend, seq_t sequence, const BYTE **litPtr, const BYTE *const litLimit, const BYTE *const base,
			      const BYTE *const vBase, const BYTE *const dictEnd)
{
	BYTE *const oLitEnd = op + sequence.litLength;
	size_t const sequenceLength = sequence.litLength + sequence.matchLength;
	BYTE *const oMatchEnd = op + sequenceLength; /* risk : address space overflow (32-bits) */
	BYTE *const oend_w = oend - WILDCOPY_OVERLENGTH;
	const BYTE *const iLitEnd = *litPtr + sequence.litLength;
	const BYTE *match = oLitEnd - sequence.offset;

	/* check */
	if (oMatchEnd > oend)
		return ERROR(dstSize_tooSmall); /* last match must start at a minimum distance of WILDCOPY_OVERLENGTH from oend */
	if (iLitEnd > litLimit)
		return ERROR(corruption_detected); /* over-read beyond lit buffer */
	if (oLitEnd <= oend_w)
		return ERROR(GENERIC); /* Precondition */

	/* copy literals */
	if (op < oend_w) {
		ZSTD_wildcopy(op, *litPtr, oend_w - op);
		*litPtr += oend_w - op;
		op = oend_w;
	}
	while (op < oLitEnd)
		*op++ = *(*litPtr)++;

	/* copy Match */
	if (sequence.offset > (size_t)(oLitEnd - base)) {
		/* offset beyond prefix */
		if (sequence.offset > (size_t)(oLitEnd - vBase))
			return ERROR(corruption_detected);
		match = dictEnd - (base - match);
		if (match + sequence.matchLength <= dictEnd) {
			memmove(oLitEnd, match, sequence.matchLength);
			return sequenceLength;
		}
		/* span extDict & currPrefixSegment */
		{
			size_t const length1 = dictEnd - match;
			memmove(oLitEnd, match, length1);
			op = oLitEnd + length1;
			sequence.matchLength -= length1;
			match = base;
		}
	}
	while (op < oMatchEnd)
		*op++ = *match++;
	return sequenceLength;
}

static seq_t ZSTD_decodeSequence(seqState_t *seqState)
{
	seq_t seq;

	U32 const llCode = FSE_peekSymbol(&seqState->stateLL);
	U32 const mlCode = FSE_peekSymbol(&seqState->stateML);
	U32 const ofCode = FSE_peekSymbol(&seqState->stateOffb); /* <= maxOff, by table construction */

	U32 const llBits = LL_bits[llCode];
	U32 const mlBits = ML_bits[mlCode];
	U32 const ofBits = ofCode;
	U32 const totalBits = llBits + mlBits + ofBits;

	static const U32 LL_base[MaxLL + 1] = {0,  1,  2,  3,  4,  5,  6,  7,  8,    9,     10,    11,    12,    13,     14,     15,     16,     18,
					       20, 22, 24, 28, 32, 40, 48, 64, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000};

	static const U32 ML_base[MaxML + 1] = {3,  4,  5,  6,  7,  8,  9,  10,   11,    12,    13,    14,    15,     16,     17,     18,     19,     20,
					       21, 22, 23, 24, 25, 26, 27, 28,   29,    30,    31,    32,    33,     34,     35,     37,     39,     41,
					       43, 47, 51, 59, 67, 83, 99, 0x83, 0x103, 0x203, 0x403, 0x803, 0x1003, 0x2003, 0x4003, 0x8003, 0x10003};

	static const U32 OF_base[MaxOff + 1] = {0,       1,	1,	5,	0xD,      0x1D,      0x3D,      0x7D,      0xFD,     0x1FD,
						0x3FD,   0x7FD,    0xFFD,    0x1FFD,   0x3FFD,   0x7FFD,    0xFFFD,    0x1FFFD,   0x3FFFD,  0x7FFFD,
						0xFFFFD, 0x1FFFFD, 0x3FFFFD, 0x7FFFFD, 0xFFFFFD, 0x1FFFFFD, 0x3FFFFFD, 0x7FFFFFD, 0xFFFFFFD};

	/* sequence */
	{
		size_t offset;
		if (!ofCode)
			offset = 0;
		else {
			offset = OF_base[ofCode] + BIT_readBitsFast(&seqState->DStream, ofBits); /* <=  (ZSTD_WINDOWLOG_MAX-1) bits */
			if (ZSTD_32bits())
				BIT_reloadDStream(&seqState->DStream);
		}

		if (ofCode <= 1) {
			offset += (llCode == 0);
			if (offset) {
				size_t temp = (offset == 3) ? seqState->prevOffset[0] - 1 : seqState->prevOffset[offset];
				temp += !temp; /* 0 is not valid; input is corrupted; force offset to 1 */
				if (offset != 1)
					seqState->prevOffset[2] = seqState->prevOffset[1];
				seqState->prevOffset[1] = seqState->prevOffset[0];
				seqState->prevOffset[0] = offset = temp;
			} else {
				offset = seqState->prevOffset[0];
			}
		} else {
			seqState->prevOffset[2] = seqState->prevOffset[1];
			seqState->prevOffset[1] = seqState->prevOffset[0];
			seqState->prevOffset[0] = offset;
		}
		seq.offset = offset;
	}

	seq.matchLength = ML_base[mlCode] + ((mlCode > 31) ? BIT_readBitsFast(&seqState->DStream, mlBits) : 0); /* <=  16 bits */
	if (ZSTD_32bits() && (mlBits + llBits > 24))
		BIT_reloadDStream(&seqState->DStream);

	seq.litLength = LL_base[llCode] + ((llCode > 15) ? BIT_readBitsFast(&seqState->DStream, llBits) : 0); /* <=  16 bits */
	if (ZSTD_32bits() || (totalBits > 64 - 7 - (LLFSELog + MLFSELog + OffFSELog)))
		BIT_reloadDStream(&seqState->DStream);

	/* ANS state update */
	FSE_updateState(&seqState->stateLL, &seqState->DStream); /* <=  9 bits */
	FSE_updateState(&seqState->stateML, &seqState->DStream); /* <=  9 bits */
	if (ZSTD_32bits())
		BIT_reloadDStream(&seqState->DStream);		   /* <= 18 bits */
	FSE_updateState(&seqState->stateOffb, &seqState->DStream); /* <=  8 bits */

	seq.match = NULL;

	return seq;
}

FORCE_INLINE
size_t ZSTD_execSequence(BYTE *op, BYTE *const oend, seq_t sequence, const BYTE **litPtr, const BYTE *const litLimit, const BYTE *const base,
			 const BYTE *const vBase, const BYTE *const dictEnd)
{
	BYTE *const oLitEnd = op + sequence.litLength;
	size_t const sequenceLength = sequence.litLength + sequence.matchLength;
	BYTE *const oMatchEnd = op + sequenceLength; /* risk : address space overflow (32-bits) */
	BYTE *const oend_w = oend - WILDCOPY_OVERLENGTH;
	const BYTE *const iLitEnd = *litPtr + sequence.litLength;
	const BYTE *match = oLitEnd - sequence.offset;

	/* check */
	if (oMatchEnd > oend)
		return ERROR(dstSize_tooSmall); /* last match must start at a minimum distance of WILDCOPY_OVERLENGTH from oend */
	if (iLitEnd > litLimit)
		return ERROR(corruption_detected); /* over-read beyond lit buffer */
	if (oLitEnd > oend_w)
		return ZSTD_execSequenceLast7(op, oend, sequence, litPtr, litLimit, base, vBase, dictEnd);

	/* copy Literals */
	ZSTD_copy8(op, *litPtr);
	if (sequence.litLength > 8)
		ZSTD_wildcopy(op + 8, (*litPtr) + 8,
			      sequence.litLength - 8); /* note : since oLitEnd <= oend-WILDCOPY_OVERLENGTH, no risk of overwrite beyond oend */
	op = oLitEnd;
	*litPtr = iLitEnd; /* update for next sequence */

	/* copy Match */
	if (sequence.offset > (size_t)(oLitEnd - base)) {
		/* offset beyond prefix */
		if (sequence.offset > (size_t)(oLitEnd - vBase))
			return ERROR(corruption_detected);
		match = dictEnd + (match - base);
		if (match + sequence.matchLength <= dictEnd) {
			memmove(oLitEnd, match, sequence.matchLength);
			return sequenceLength;
		}
		/* span extDict & currPrefixSegment */
		{
			size_t const length1 = dictEnd - match;
			memmove(oLitEnd, match, length1);
			op = oLitEnd + length1;
			sequence.matchLength -= length1;
			match = base;
			if (op > oend_w || sequence.matchLength < MINMATCH) {
				U32 i;
				for (i = 0; i < sequence.matchLength; ++i)
					op[i] = match[i];
				return sequenceLength;
			}
		}
	}
	/* Requirement: op <= oend_w && sequence.matchLength >= MINMATCH */

	/* match within prefix */
	if (sequence.offset < 8) {
		/* close range match, overlap */
		static const U32 dec32table[] = {0, 1, 2, 1, 4, 4, 4, 4};   /* added */
		static const int dec64table[] = {8, 8, 8, 7, 8, 9, 10, 11}; /* subtracted */
		int const sub2 = dec64table[sequence.offset];
		op[0] = match[0];
		op[1] = match[1];
		op[2] = match[2];
		op[3] = match[3];
		match += dec32table[sequence.offset];
		ZSTD_copy4(op + 4, match);
		match -= sub2;
	} else {
		ZSTD_copy8(op, match);
	}
	op += 8;
	match += 8;

	if (oMatchEnd > oend - (16 - MINMATCH)) {
		if (op < oend_w) {
			ZSTD_wildcopy(op, match, oend_w - op);
			match += oend_w - op;
			op = oend_w;
		}
		while (op < oMatchEnd)
			*op++ = *match++;
	} else {
		ZSTD_wildcopy(op, match, (ptrdiff_t)sequence.matchLength - 8); /* works even if matchLength < 8 */
	}
	return sequenceLength;
}

static size_t ZSTD_decompressSequences(ZSTD_DCtx *dctx, void *dst, size_t maxDstSize, const void *seqStart, size_t seqSize)
{
	const BYTE *ip = (const BYTE *)seqStart;
	const BYTE *const iend = ip + seqSize;
	BYTE *const ostart = (BYTE * const)dst;
	BYTE *const oend = ostart + maxDstSize;
	BYTE *op = ostart;
	const BYTE *litPtr = dctx->litPtr;
	const BYTE *const litEnd = litPtr + dctx->litSize;
	const BYTE *const base = (const BYTE *)(dctx->base);
	const BYTE *const vBase = (const BYTE *)(dctx->vBase);
	const BYTE *const dictEnd = (const BYTE *)(dctx->dictEnd);
	int nbSeq;

	/* Build Decoding Tables */
	{
		size_t const seqHSize = ZSTD_decodeSeqHeaders(dctx, &nbSeq, ip, seqSize);
		if (ZSTD_isError(seqHSize))
			return seqHSize;
		ip += seqHSize;
	}

	/* Regen sequences */
	if (nbSeq) {
		seqState_t seqState;
		dctx->fseEntropy = 1;
		{
			U32 i;
			for (i = 0; i < ZSTD_REP_NUM; i++)
				seqState.prevOffset[i] = dctx->entropy.rep[i];
		}
		CHECK_E(BIT_initDStream(&seqState.DStream, ip, iend - ip), corruption_detected);
		FSE_initDState(&seqState.stateLL, &seqState.DStream, dctx->LLTptr);
		FSE_initDState(&seqState.stateOffb, &seqState.DStream, dctx->OFTptr);
		FSE_initDState(&seqState.stateML, &seqState.DStream, dctx->MLTptr);

		for (; (BIT_reloadDStream(&(seqState.DStream)) <= BIT_DStream_completed) && nbSeq;) {
			nbSeq--;
			{
				seq_t const sequence = ZSTD_decodeSequence(&seqState);
				size_t const oneSeqSize = ZSTD_execSequence(op, oend, sequence, &litPtr, litEnd, base, vBase, dictEnd);
				if (ZSTD_isError(oneSeqSize))
					return oneSeqSize;
				op += oneSeqSize;
			}
		}

		/* check if reached exact end */
		if (nbSeq)
			return ERROR(corruption_detected);
		/* save reps for next block */
		{
			U32 i;
			for (i = 0; i < ZSTD_REP_NUM; i++)
				dctx->entropy.rep[i] = (U32)(seqState.prevOffset[i]);
		}
	}

	/* last literal segment */
	{
		size_t const lastLLSize = litEnd - litPtr;
		if (lastLLSize > (size_t)(oend - op))
			return ERROR(dstSize_tooSmall);
		memcpy(op, litPtr, lastLLSize);
		op += lastLLSize;
	}

	return op - ostart;
}

FORCE_INLINE seq_t ZSTD_decodeSequenceLong_generic(seqState_t *seqState, int const longOffsets)
{
	seq_t seq;

	U32 const llCode = FSE_peekSymbol(&seqState->stateLL);
	U32 const mlCode = FSE_peekSymbol(&seqState->stateML);
	U32 const ofCode = FSE_peekSymbol(&seqState->stateOffb); /* <= maxOff, by table construction */

	U32 const llBits = LL_bits[llCode];
	U32 const mlBits = ML_bits[mlCode];
	U32 const ofBits = ofCode;
	U32 const totalBits = llBits + mlBits + ofBits;

	static const U32 LL_base[MaxLL + 1] = {0,  1,  2,  3,  4,  5,  6,  7,  8,    9,     10,    11,    12,    13,     14,     15,     16,     18,
					       20, 22, 24, 28, 32, 40, 48, 64, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000};

	static const U32 ML_base[MaxML + 1] = {3,  4,  5,  6,  7,  8,  9,  10,   11,    12,    13,    14,    15,     16,     17,     18,     19,     20,
					       21, 22, 23, 24, 25, 26, 27, 28,   29,    30,    31,    32,    33,     34,     35,     37,     39,     41,
					       43, 47, 51, 59, 67, 83, 99, 0x83, 0x103, 0x203, 0x403, 0x803, 0x1003, 0x2003, 0x4003, 0x8003, 0x10003};

	static const U32 OF_base[MaxOff + 1] = {0,       1,	1,	5,	0xD,      0x1D,      0x3D,      0x7D,      0xFD,     0x1FD,
						0x3FD,   0x7FD,    0xFFD,    0x1FFD,   0x3FFD,   0x7FFD,    0xFFFD,    0x1FFFD,   0x3FFFD,  0x7FFFD,
						0xFFFFD, 0x1FFFFD, 0x3FFFFD, 0x7FFFFD, 0xFFFFFD, 0x1FFFFFD, 0x3FFFFFD, 0x7FFFFFD, 0xFFFFFFD};

	/* sequence */
	{
		size_t offset;
		if (!ofCode)
			offset = 0;
		else {
			if (longOffsets) {
				int const extraBits = ofBits - MIN(ofBits, STREAM_ACCUMULATOR_MIN);
				offset = OF_base[ofCode] + (BIT_readBitsFast(&seqState->DStream, ofBits - extraBits) << extraBits);
				if (ZSTD_32bits() || extraBits)
					BIT_reloadDStream(&seqState->DStream);
				if (extraBits)
					offset += BIT_readBitsFast(&seqState->DStream, extraBits);
			} else {
				offset = OF_base[ofCode] + BIT_readBitsFast(&seqState->DStream, ofBits); /* <=  (ZSTD_WINDOWLOG_MAX-1) bits */
				if (ZSTD_32bits())
					BIT_reloadDStream(&seqState->DStream);
			}
		}

		if (ofCode <= 1) {
			offset += (llCode == 0);
			if (offset) {
				size_t temp = (offset == 3) ? seqState->prevOffset[0] - 1 : seqState->prevOffset[offset];
				temp += !temp; /* 0 is not valid; input is corrupted; force offset to 1 */
				if (offset != 1)
					seqState->prevOffset[2] = seqState->prevOffset[1];
				seqState->prevOffset[1] = seqState->prevOffset[0];
				seqState->prevOffset[0] = offset = temp;
			} else {
				offset = seqState->prevOffset[0];
			}
		} else {
			seqState->prevOffset[2] = seqState->prevOffset[1];
			seqState->prevOffset[1] = seqState->prevOffset[0];
			seqState->prevOffset[0] = offset;
		}
		seq.offset = offset;
	}

	seq.matchLength = ML_base[mlCode] + ((mlCode > 31) ? BIT_readBitsFast(&seqState->DStream, mlBits) : 0); /* <=  16 bits */
	if (ZSTD_32bits() && (mlBits + llBits > 24))
		BIT_reloadDStream(&seqState->DStream);

	seq.litLength = LL_base[llCode] + ((llCode > 15) ? BIT_readBitsFast(&seqState->DStream, llBits) : 0); /* <=  16 bits */
	if (ZSTD_32bits() || (totalBits > 64 - 7 - (LLFSELog + MLFSELog + OffFSELog)))
		BIT_reloadDStream(&seqState->DStream);

	{
		size_t const pos = seqState->pos + seq.litLength;
		seq.match = seqState->base + pos - seq.offset; /* single memory segment */
		if (seq.offset > pos)
			seq.match += seqState->gotoDict; /* separate memory segment */
		seqState->pos = pos + seq.matchLength;
	}

	/* ANS state update */
	FSE_updateState(&seqState->stateLL, &seqState->DStream); /* <=  9 bits */
	FSE_updateState(&seqState->stateML, &seqState->DStream); /* <=  9 bits */
	if (ZSTD_32bits())
		BIT_reloadDStream(&seqState->DStream);		   /* <= 18 bits */
	FSE_updateState(&seqState->stateOffb, &seqState->DStream); /* <=  8 bits */

	return seq;
}

static seq_t ZSTD_decodeSequenceLong(seqState_t *seqState, unsigned const windowSize)
{
	if (ZSTD_highbit32(windowSize) > STREAM_ACCUMULATOR_MIN) {
		return ZSTD_decodeSequenceLong_generic(seqState, 1);
	} else {
		return ZSTD_decodeSequenceLong_generic(seqState, 0);
	}
}

FORCE_INLINE
size_t ZSTD_execSequenceLong(BYTE *op, BYTE *const oend, seq_t sequence, const BYTE **litPtr, const BYTE *const litLimit, const BYTE *const base,
			     const BYTE *const vBase, const BYTE *const dictEnd)
{
	BYTE *const oLitEnd = op + sequence.litLength;
	size_t const sequenceLength = sequence.litLength + sequence.matchLength;
	BYTE *const oMatchEnd = op + sequenceLength; /* risk : address space overflow (32-bits) */
	BYTE *const oend_w = oend - WILDCOPY_OVERLENGTH;
	const BYTE *const iLitEnd = *litPtr + sequence.litLength;
	const BYTE *match = sequence.match;

	/* check */
	if (oMatchEnd > oend)
		return ERROR(dstSize_tooSmall); /* last match must start at a minimum distance of WILDCOPY_OVERLENGTH from oend */
	if (iLitEnd > litLimit)
		return ERROR(corruption_detected); /* over-read beyond lit buffer */
	if (oLitEnd > oend_w)
		return ZSTD_execSequenceLast7(op, oend, sequence, litPtr, litLimit, base, vBase, dictEnd);

	/* copy Literals */
	ZSTD_copy8(op, *litPtr);
	if (sequence.litLength > 8)
		ZSTD_wildcopy(op + 8, (*litPtr) + 8,
			      sequence.litLength - 8); /* note : since oLitEnd <= oend-WILDCOPY_OVERLENGTH, no risk of overwrite beyond oend */
	op = oLitEnd;
	*litPtr = iLitEnd; /* update for next sequence */

	/* copy Match */
	if (sequence.offset > (size_t)(oLitEnd - base)) {
		/* offset beyond prefix */
		if (sequence.offset > (size_t)(oLitEnd - vBase))
			return ERROR(corruption_detected);
		if (match + sequence.matchLength <= dictEnd) {
			memmove(oLitEnd, match, sequence.matchLength);
			return sequenceLength;
		}
		/* span extDict & currPrefixSegment */
		{
			size_t const length1 = dictEnd - match;
			memmove(oLitEnd, match, length1);
			op = oLitEnd + length1;
			sequence.matchLength -= length1;
			match = base;
			if (op > oend_w || sequence.matchLength < MINMATCH) {
				U32 i;
				for (i = 0; i < sequence.matchLength; ++i)
					op[i] = match[i];
				return sequenceLength;
			}
		}
	}
	/* Requirement: op <= oend_w && sequence.matchLength >= MINMATCH */

	/* match within prefix */
	if (sequence.offset < 8) {
		/* close range match, overlap */
		static const U32 dec32table[] = {0, 1, 2, 1, 4, 4, 4, 4};   /* added */
		static const int dec64table[] = {8, 8, 8, 7, 8, 9, 10, 11}; /* subtracted */
		int const sub2 = dec64table[sequence.offset];
		op[0] = match[0];
		op[1] = match[1];
		op[2] = match[2];
		op[3] = match[3];
		match += dec32table[sequence.offset];
		ZSTD_copy4(op + 4, match);
		match -= sub2;
	} else {
		ZSTD_copy8(op, match);
	}
	op += 8;
	match += 8;

	if (oMatchEnd > oend - (16 - MINMATCH)) {
		if (op < oend_w) {
			ZSTD_wildcopy(op, match, oend_w - op);
			match += oend_w - op;
			op = oend_w;
		}
		while (op < oMatchEnd)
			*op++ = *match++;
	} else {
		ZSTD_wildcopy(op, match, (ptrdiff_t)sequence.matchLength - 8); /* works even if matchLength < 8 */
	}
	return sequenceLength;
}

static size_t ZSTD_decompressSequencesLong(ZSTD_DCtx *dctx, void *dst, size_t maxDstSize, const void *seqStart, size_t seqSize)
{
	const BYTE *ip = (const BYTE *)seqStart;
	const BYTE *const iend = ip + seqSize;
	BYTE *const ostart = (BYTE * const)dst;
	BYTE *const oend = ostart + maxDstSize;
	BYTE *op = ostart;
	const BYTE *litPtr = dctx->litPtr;
	const BYTE *const litEnd = litPtr + dctx->litSize;
	const BYTE *const base = (const BYTE *)(dctx->base);
	const BYTE *const vBase = (const BYTE *)(dctx->vBase);
	const BYTE *const dictEnd = (const BYTE *)(dctx->dictEnd);
	unsigned const windowSize = dctx->fParams.windowSize;
	int nbSeq;

	/* Build Decoding Tables */
	{
		size_t const seqHSize = ZSTD_decodeSeqHeaders(dctx, &nbSeq, ip, seqSize);
		if (ZSTD_isError(seqHSize))
			return seqHSize;
		ip += seqHSize;
	}

	/* Regen sequences */
	if (nbSeq) {
#define STORED_SEQS 4
#define STOSEQ_MASK (STORED_SEQS - 1)
#define ADVANCED_SEQS 4
		seq_t *sequences = (seq_t *)dctx->entropy.workspace;
		int const seqAdvance = MIN(nbSeq, ADVANCED_SEQS);
		seqState_t seqState;
		int seqNb;
		ZSTD_STATIC_ASSERT(sizeof(dctx->entropy.workspace) >= sizeof(seq_t) * STORED_SEQS);
		dctx->fseEntropy = 1;
		{
			U32 i;
			for (i = 0; i < ZSTD_REP_NUM; i++)
				seqState.prevOffset[i] = dctx->entropy.rep[i];
		}
		seqState.base = base;
		seqState.pos = (size_t)(op - base);
		seqState.gotoDict = (uPtrDiff)dictEnd - (uPtrDiff)base; /* cast to avoid undefined behaviour */
		CHECK_E(BIT_initDStream(&seqState.DStream, ip, iend - ip), corruption_detected);
		FSE_initDState(&seqState.stateLL, &seqState.DStream, dctx->LLTptr);
		FSE_initDState(&seqState.stateOffb, &seqState.DStream, dctx->OFTptr);
		FSE_initDState(&seqState.stateML, &seqState.DStream, dctx->MLTptr);

		/* prepare in advance */
		for (seqNb = 0; (BIT_reloadDStream(&seqState.DStream) <= BIT_DStream_completed) && seqNb < seqAdvance; seqNb++) {
			sequences[seqNb] = ZSTD_decodeSequenceLong(&seqState, windowSize);
		}
		if (seqNb < seqAdvance)
			return ERROR(corruption_detected);

		/* decode and decompress */
		for (; (BIT_reloadDStream(&(seqState.DStream)) <= BIT_DStream_completed) && seqNb < nbSeq; seqNb++) {
			seq_t const sequence = ZSTD_decodeSequenceLong(&seqState, windowSize);
			size_t const oneSeqSize =
			    ZSTD_execSequenceLong(op, oend, sequences[(seqNb - ADVANCED_SEQS) & STOSEQ_MASK], &litPtr, litEnd, base, vBase, dictEnd);
			if (ZSTD_isError(oneSeqSize))
				return oneSeqSize;
			ZSTD_PREFETCH(sequence.match);
			sequences[seqNb & STOSEQ_MASK] = sequence;
			op += oneSeqSize;
		}
		if (seqNb < nbSeq)
			return ERROR(corruption_detected);

		/* finish queue */
		seqNb -= seqAdvance;
		for (; seqNb < nbSeq; seqNb++) {
			size_t const oneSeqSize = ZSTD_execSequenceLong(op, oend, sequences[seqNb & STOSEQ_MASK], &litPtr, litEnd, base, vBase, dictEnd);
			if (ZSTD_isError(oneSeqSize))
				return oneSeqSize;
			op += oneSeqSize;
		}

		/* save reps for next block */
		{
			U32 i;
			for (i = 0; i < ZSTD_REP_NUM; i++)
				dctx->entropy.rep[i] = (U32)(seqState.prevOffset[i]);
		}
	}

	/* last literal segment */
	{
		size_t const lastLLSize = litEnd - litPtr;
		if (lastLLSize > (size_t)(oend - op))
			return ERROR(dstSize_tooSmall);
		memcpy(op, litPtr, lastLLSize);
		op += lastLLSize;
	}

	return op - ostart;
}

static size_t ZSTD_decompressBlock_internal(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void *src, size_t srcSize)
{ /* blockType == blockCompressed */
	const BYTE *ip = (const BYTE *)src;

	if (srcSize >= ZSTD_BLOCKSIZE_ABSOLUTEMAX)
		return ERROR(srcSize_wrong);

	/* Decode literals section */
	{
		size_t const litCSize = ZSTD_decodeLiteralsBlock(dctx, src, srcSize);
		if (ZSTD_isError(litCSize))
			return litCSize;
		ip += litCSize;
		srcSize -= litCSize;
	}
	if (sizeof(size_t) > 4) /* do not enable prefetching on 32-bits x86, as it's performance detrimental */
				/* likely because of register pressure */
				/* if that's the correct cause, then 32-bits ARM should be affected differently */
				/* it would be good to test this on ARM real hardware, to see if prefetch version improves speed */
		if (dctx->fParams.windowSize > (1 << 23))
			return ZSTD_decompressSequencesLong(dctx, dst, dstCapacity, ip, srcSize);
	return ZSTD_decompressSequences(dctx, dst, dstCapacity, ip, srcSize);
}

static void ZSTD_checkContinuity(ZSTD_DCtx *dctx, const void *dst)
{
	if (dst != dctx->previousDstEnd) { /* not contiguous */
		dctx->dictEnd = dctx->previousDstEnd;
		dctx->vBase = (const char *)dst - ((const char *)(dctx->previousDstEnd) - (const char *)(dctx->base));
		dctx->base = dst;
		dctx->previousDstEnd = dst;
	}
}

size_t ZSTD_decompressBlock(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void *src, size_t srcSize)
{
	size_t dSize;
	ZSTD_checkContinuity(dctx, dst);
	dSize = ZSTD_decompressBlock_internal(dctx, dst, dstCapacity, src, srcSize);
	dctx->previousDstEnd = (char *)dst + dSize;
	return dSize;
}

/** ZSTD_insertBlock() :
	insert `src` block into `dctx` history. Useful to track uncompressed blocks. */
size_t ZSTD_insertBlock(ZSTD_DCtx *dctx, const void *blockStart, size_t blockSize)
{
	ZSTD_checkContinuity(dctx, blockStart);
	dctx->previousDstEnd = (const char *)blockStart + blockSize;
	return blockSize;
}

size_t ZSTD_generateNxBytes(void *dst, size_t dstCapacity, BYTE byte, size_t length)
{
	if (length > dstCapacity)
		return ERROR(dstSize_tooSmall);
	memset(dst, byte, length);
	return length;
}

/** ZSTD_findFrameCompressedSize() :
 *  compatible with legacy mode
 *  `src` must point to the start of a ZSTD frame, ZSTD legacy frame, or skippable frame
 *  `srcSize` must be at least as large as the frame contained
 *  @return : the compressed size of the frame starting at `src` */
size_t ZSTD_findFrameCompressedSize(const void *src, size_t srcSize)
{
	if (srcSize >= ZSTD_skippableHeaderSize && (ZSTD_readLE32(src) & 0xFFFFFFF0U) == ZSTD_MAGIC_SKIPPABLE_START) {
		return ZSTD_skippableHeaderSize + ZSTD_readLE32((const BYTE *)src + 4);
	} else {
		const BYTE *ip = (const BYTE *)src;
		const BYTE *const ipstart = ip;
		size_t remainingSize = srcSize;
		ZSTD_frameParams fParams;

		size_t const headerSize = ZSTD_frameHeaderSize(ip, remainingSize);
		if (ZSTD_isError(headerSize))
			return headerSize;

		/* Frame Header */
		{
			size_t const ret = ZSTD_getFrameParams(&fParams, ip, remainingSize);
			if (ZSTD_isError(ret))
				return ret;
			if (ret > 0)
				return ERROR(srcSize_wrong);
		}

		ip += headerSize;
		remainingSize -= headerSize;

		/* Loop on each block */
		while (1) {
			blockProperties_t blockProperties;
			size_t const cBlockSize = ZSTD_getcBlockSize(ip, remainingSize, &blockProperties);
			if (ZSTD_isError(cBlockSize))
				return cBlockSize;

			if (ZSTD_blockHeaderSize + cBlockSize > remainingSize)
				return ERROR(srcSize_wrong);

			ip += ZSTD_blockHeaderSize + cBlockSize;
			remainingSize -= ZSTD_blockHeaderSize + cBlockSize;

			if (blockProperties.lastBlock)
				break;
		}

		if (fParams.checksumFlag) { /* Frame content checksum */
			if (remainingSize < 4)
				return ERROR(srcSize_wrong);
			ip += 4;
			remainingSize -= 4;
		}

		return ip - ipstart;
	}
}

/*! ZSTD_decompressFrame() :
*   @dctx must be properly initialized */
static size_t ZSTD_decompressFrame(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void **srcPtr, size_t *srcSizePtr)
{
	const BYTE *ip = (const BYTE *)(*srcPtr);
	BYTE *const ostart = (BYTE * const)dst;
	BYTE *const oend = ostart + dstCapacity;
	BYTE *op = ostart;
	size_t remainingSize = *srcSizePtr;

	/* check */
	if (remainingSize < ZSTD_frameHeaderSize_min + ZSTD_blockHeaderSize)
		return ERROR(srcSize_wrong);

	/* Frame Header */
	{
		size_t const frameHeaderSize = ZSTD_frameHeaderSize(ip, ZSTD_frameHeaderSize_prefix);
		if (ZSTD_isError(frameHeaderSize))
			return frameHeaderSize;
		if (remainingSize < frameHeaderSize + ZSTD_blockHeaderSize)
			return ERROR(srcSize_wrong);
		CHECK_F(ZSTD_decodeFrameHeader(dctx, ip, frameHeaderSize));
		ip += frameHeaderSize;
		remainingSize -= frameHeaderSize;
	}

	/* Loop on each block */
	while (1) {
		size_t decodedSize;
		blockProperties_t blockProperties;
		size_t const cBlockSize = ZSTD_getcBlockSize(ip, remainingSize, &blockProperties);
		if (ZSTD_isError(cBlockSize))
			return cBlockSize;

		ip += ZSTD_blockHeaderSize;
		remainingSize -= ZSTD_blockHeaderSize;
		if (cBlockSize > remainingSize)
			return ERROR(srcSize_wrong);

		switch (blockProperties.blockType) {
		case bt_compressed: decodedSize = ZSTD_decompressBlock_internal(dctx, op, oend - op, ip, cBlockSize); break;
		case bt_raw: decodedSize = ZSTD_copyRawBlock(op, oend - op, ip, cBlockSize); break;
		case bt_rle: decodedSize = ZSTD_generateNxBytes(op, oend - op, *ip, blockProperties.origSize); break;
		case bt_reserved:
		default: return ERROR(corruption_detected);
		}

		if (ZSTD_isError(decodedSize))
			return decodedSize;
		if (dctx->fParams.checksumFlag)
			xxh64_update(&dctx->xxhState, op, decodedSize);
		op += decodedSize;
		ip += cBlockSize;
		remainingSize -= cBlockSize;
		if (blockProperties.lastBlock)
			break;
	}

	if (dctx->fParams.checksumFlag) { /* Frame content checksum verification */
		U32 const checkCalc = (U32)xxh64_digest(&dctx->xxhState);
		U32 checkRead;
		if (remainingSize < 4)
			return ERROR(checksum_wrong);
		checkRead = ZSTD_readLE32(ip);
		if (checkRead != checkCalc)
			return ERROR(checksum_wrong);
		ip += 4;
		remainingSize -= 4;
	}

	/* Allow caller to get size read */
	*srcPtr = ip;
	*srcSizePtr = remainingSize;
	return op - ostart;
}

static const void *ZSTD_DDictDictContent(const ZSTD_DDict *ddict);
static size_t ZSTD_DDictDictSize(const ZSTD_DDict *ddict);

static size_t ZSTD_decompressMultiFrame(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void *src, size_t srcSize, const void *dict, size_t dictSize,
					const ZSTD_DDict *ddict)
{
	void *const dststart = dst;

	if (ddict) {
		if (dict) {
			/* programmer error, these two cases should be mutually exclusive */
			return ERROR(GENERIC);
		}

		dict = ZSTD_DDictDictContent(ddict);
		dictSize = ZSTD_DDictDictSize(ddict);
	}

	while (srcSize >= ZSTD_frameHeaderSize_prefix) {
		U32 magicNumber;

		magicNumber = ZSTD_readLE32(src);
		if (magicNumber != ZSTD_MAGICNUMBER) {
			if ((magicNumber & 0xFFFFFFF0U) == ZSTD_MAGIC_SKIPPABLE_START) {
				size_t skippableSize;
				if (srcSize < ZSTD_skippableHeaderSize)
					return ERROR(srcSize_wrong);
				skippableSize = ZSTD_readLE32((const BYTE *)src + 4) + ZSTD_skippableHeaderSize;
				if (srcSize < skippableSize) {
					return ERROR(srcSize_wrong);
				}

				src = (const BYTE *)src + skippableSize;
				srcSize -= skippableSize;
				continue;
			} else {
				return ERROR(prefix_unknown);
			}
		}

		if (ddict) {
			/* we were called from ZSTD_decompress_usingDDict */
			ZSTD_refDDict(dctx, ddict);
		} else {
			/* this will initialize correctly with no dict if dict == NULL, so
			 * use this in all cases but ddict */
			CHECK_F(ZSTD_decompressBegin_usingDict(dctx, dict, dictSize));
		}
		ZSTD_checkContinuity(dctx, dst);

		{
			const size_t res = ZSTD_decompressFrame(dctx, dst, dstCapacity, &src, &srcSize);
			if (ZSTD_isError(res))
				return res;
			/* don't need to bounds check this, ZSTD_decompressFrame will have
			 * already */
			dst = (BYTE *)dst + res;
			dstCapacity -= res;
		}
	}

	if (srcSize)
		return ERROR(srcSize_wrong); /* input not entirely consumed */

	return (BYTE *)dst - (BYTE *)dststart;
}

size_t ZSTD_decompress_usingDict(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void *src, size_t srcSize, const void *dict, size_t dictSize)
{
	return ZSTD_decompressMultiFrame(dctx, dst, dstCapacity, src, srcSize, dict, dictSize, NULL);
}

size_t ZSTD_decompressDCtx(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void *src, size_t srcSize)
{
	return ZSTD_decompress_usingDict(dctx, dst, dstCapacity, src, srcSize, NULL, 0);
}

/*-**************************************
*   Advanced Streaming Decompression API
*   Bufferless and synchronous
****************************************/
size_t ZSTD_nextSrcSizeToDecompress(ZSTD_DCtx *dctx) { return dctx->expected; }

ZSTD_nextInputType_e ZSTD_nextInputType(ZSTD_DCtx *dctx)
{
	switch (dctx->stage) {
	default: /* should not happen */
	case ZSTDds_getFrameHeaderSize:
	case ZSTDds_decodeFrameHeader: return ZSTDnit_frameHeader;
	case ZSTDds_decodeBlockHeader: return ZSTDnit_blockHeader;
	case ZSTDds_decompressBlock: return ZSTDnit_block;
	case ZSTDds_decompressLastBlock: return ZSTDnit_lastBlock;
	case ZSTDds_checkChecksum: return ZSTDnit_checksum;
	case ZSTDds_decodeSkippableHeader:
	case ZSTDds_skipFrame: return ZSTDnit_skippableFrame;
	}
}

int ZSTD_isSkipFrame(ZSTD_DCtx *dctx) { return dctx->stage == ZSTDds_skipFrame; } /* for zbuff */

/** ZSTD_decompressContinue() :
*   @return : nb of bytes generated into `dst` (necessarily <= `dstCapacity)
*             or an error code, which can be tested using ZSTD_isError() */
size_t ZSTD_decompressContinue(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void *src, size_t srcSize)
{
	/* Sanity check */
	if (srcSize != dctx->expected)
		return ERROR(srcSize_wrong);
	if (dstCapacity)
		ZSTD_checkContinuity(dctx, dst);

	switch (dctx->stage) {
	case ZSTDds_getFrameHeaderSize:
		if (srcSize != ZSTD_frameHeaderSize_prefix)
			return ERROR(srcSize_wrong);					/* impossible */
		if ((ZSTD_readLE32(src) & 0xFFFFFFF0U) == ZSTD_MAGIC_SKIPPABLE_START) { /* skippable frame */
			memcpy(dctx->headerBuffer, src, ZSTD_frameHeaderSize_prefix);
			dctx->expected = ZSTD_skippableHeaderSize - ZSTD_frameHeaderSize_prefix; /* magic number + skippable frame length */
			dctx->stage = ZSTDds_decodeSkippableHeader;
			return 0;
		}
		dctx->headerSize = ZSTD_frameHeaderSize(src, ZSTD_frameHeaderSize_prefix);
		if (ZSTD_isError(dctx->headerSize))
			return dctx->headerSize;
		memcpy(dctx->headerBuffer, src, ZSTD_frameHeaderSize_prefix);
		if (dctx->headerSize > ZSTD_frameHeaderSize_prefix) {
			dctx->expected = dctx->headerSize - ZSTD_frameHeaderSize_prefix;
			dctx->stage = ZSTDds_decodeFrameHeader;
			return 0;
		}
		dctx->expected = 0; /* not necessary to copy more */
		/* fall through */

	case ZSTDds_decodeFrameHeader:
		memcpy(dctx->headerBuffer + ZSTD_frameHeaderSize_prefix, src, dctx->expected);
		CHECK_F(ZSTD_decodeFrameHeader(dctx, dctx->headerBuffer, dctx->headerSize));
		dctx->expected = ZSTD_blockHeaderSize;
		dctx->stage = ZSTDds_decodeBlockHeader;
		return 0;

	case ZSTDds_decodeBlockHeader: {
		blockProperties_t bp;
		size_t const cBlockSize = ZSTD_getcBlockSize(src, ZSTD_blockHeaderSize, &bp);
		if (ZSTD_isError(cBlockSize))
			return cBlockSize;
		dctx->expected = cBlockSize;
		dctx->bType = bp.blockType;
		dctx->rleSize = bp.origSize;
		if (cBlockSize) {
			dctx->stage = bp.lastBlock ? ZSTDds_decompressLastBlock : ZSTDds_decompressBlock;
			return 0;
		}
		/* empty block */
		if (bp.lastBlock) {
			if (dctx->fParams.checksumFlag) {
				dctx->expected = 4;
				dctx->stage = ZSTDds_checkChecksum;
			} else {
				dctx->expected = 0; /* end of frame */
				dctx->stage = ZSTDds_getFrameHeaderSize;
			}
		} else {
			dctx->expected = 3; /* go directly to next header */
			dctx->stage = ZSTDds_decodeBlockHeader;
		}
		return 0;
	}
	case ZSTDds_decompressLastBlock:
	case ZSTDds_decompressBlock: {
		size_t rSize;
		switch (dctx->bType) {
		case bt_compressed: rSize = ZSTD_decompressBlock_internal(dctx, dst, dstCapacity, src, srcSize); break;
		case bt_raw: rSize = ZSTD_copyRawBlock(dst, dstCapacity, src, srcSize); break;
		case bt_rle: rSize = ZSTD_setRleBlock(dst, dstCapacity, src, srcSize, dctx->rleSize); break;
		case bt_reserved: /* should never happen */
		default: return ERROR(corruption_detected);
		}
		if (ZSTD_isError(rSize))
			return rSize;
		if (dctx->fParams.checksumFlag)
			xxh64_update(&dctx->xxhState, dst, rSize);

		if (dctx->stage == ZSTDds_decompressLastBlock) { /* end of frame */
			if (dctx->fParams.checksumFlag) {	/* another round for frame checksum */
				dctx->expected = 4;
				dctx->stage = ZSTDds_checkChecksum;
			} else {
				dctx->expected = 0; /* ends here */
				dctx->stage = ZSTDds_getFrameHeaderSize;
			}
		} else {
			dctx->stage = ZSTDds_decodeBlockHeader;
			dctx->expected = ZSTD_blockHeaderSize;
			dctx->previousDstEnd = (char *)dst + rSize;
		}
		return rSize;
	}
	case ZSTDds_checkChecksum: {
		U32 const h32 = (U32)xxh64_digest(&dctx->xxhState);
		U32 const check32 = ZSTD_readLE32(src); /* srcSize == 4, guaranteed by dctx->expected */
		if (check32 != h32)
			return ERROR(checksum_wrong);
		dctx->expected = 0;
		dctx->stage = ZSTDds_getFrameHeaderSize;
		return 0;
	}
	case ZSTDds_decodeSkippableHeader: {
		memcpy(dctx->headerBuffer + ZSTD_frameHeaderSize_prefix, src, dctx->expected);
		dctx->expected = ZSTD_readLE32(dctx->headerBuffer + 4);
		dctx->stage = ZSTDds_skipFrame;
		return 0;
	}
	case ZSTDds_skipFrame: {
		dctx->expected = 0;
		dctx->stage = ZSTDds_getFrameHeaderSize;
		return 0;
	}
	default:
		return ERROR(GENERIC); /* impossible */
	}
}

static size_t ZSTD_refDictContent(ZSTD_DCtx *dctx, const void *dict, size_t dictSize)
{
	dctx->dictEnd = dctx->previousDstEnd;
	dctx->vBase = (const char *)dict - ((const char *)(dctx->previousDstEnd) - (const char *)(dctx->base));
	dctx->base = dict;
	dctx->previousDstEnd = (const char *)dict + dictSize;
	return 0;
}

/* ZSTD_loadEntropy() :
 * dict : must point at beginning of a valid zstd dictionary
 * @return : size of entropy tables read */
static size_t ZSTD_loadEntropy(ZSTD_entropyTables_t *entropy, const void *const dict, size_t const dictSize)
{
	const BYTE *dictPtr = (const BYTE *)dict;
	const BYTE *const dictEnd = dictPtr + dictSize;

	if (dictSize <= 8)
		return ERROR(dictionary_corrupted);
	dictPtr += 8; /* skip header = magic + dictID */

	{
		size_t const hSize = HUF_readDTableX4_wksp(entropy->hufTable, dictPtr, dictEnd - dictPtr, entropy->workspace, sizeof(entropy->workspace));
		if (HUF_isError(hSize))
			return ERROR(dictionary_corrupted);
		dictPtr += hSize;
	}

	{
		short offcodeNCount[MaxOff + 1];
		U32 offcodeMaxValue = MaxOff, offcodeLog;
		size_t const offcodeHeaderSize = FSE_readNCount(offcodeNCount, &offcodeMaxValue, &offcodeLog, dictPtr, dictEnd - dictPtr);
		if (FSE_isError(offcodeHeaderSize))
			return ERROR(dictionary_corrupted);
		if (offcodeLog > OffFSELog)
			return ERROR(dictionary_corrupted);
		CHECK_E(FSE_buildDTable_wksp(entropy->OFTable, offcodeNCount, offcodeMaxValue, offcodeLog, entropy->workspace, sizeof(entropy->workspace)), dictionary_corrupted);
		dictPtr += offcodeHeaderSize;
	}

	{
		short matchlengthNCount[MaxML + 1];
		unsigned matchlengthMaxValue = MaxML, matchlengthLog;
		size_t const matchlengthHeaderSize = FSE_readNCount(matchlengthNCount, &matchlengthMaxValue, &matchlengthLog, dictPtr, dictEnd - dictPtr);
		if (FSE_isError(matchlengthHeaderSize))
			return ERROR(dictionary_corrupted);
		if (matchlengthLog > MLFSELog)
			return ERROR(dictionary_corrupted);
		CHECK_E(FSE_buildDTable_wksp(entropy->MLTable, matchlengthNCount, matchlengthMaxValue, matchlengthLog, entropy->workspace, sizeof(entropy->workspace)), dictionary_corrupted);
		dictPtr += matchlengthHeaderSize;
	}

	{
		short litlengthNCount[MaxLL + 1];
		unsigned litlengthMaxValue = MaxLL, litlengthLog;
		size_t const litlengthHeaderSize = FSE_readNCount(litlengthNCount, &litlengthMaxValue, &litlengthLog, dictPtr, dictEnd - dictPtr);
		if (FSE_isError(litlengthHeaderSize))
			return ERROR(dictionary_corrupted);
		if (litlengthLog > LLFSELog)
			return ERROR(dictionary_corrupted);
		CHECK_E(FSE_buildDTable_wksp(entropy->LLTable, litlengthNCount, litlengthMaxValue, litlengthLog, entropy->workspace, sizeof(entropy->workspace)), dictionary_corrupted);
		dictPtr += litlengthHeaderSize;
	}

	if (dictPtr + 12 > dictEnd)
		return ERROR(dictionary_corrupted);
	{
		int i;
		size_t const dictContentSize = (size_t)(dictEnd - (dictPtr + 12));
		for (i = 0; i < 3; i++) {
			U32 const rep = ZSTD_readLE32(dictPtr);
			dictPtr += 4;
			if (rep == 0 || rep >= dictContentSize)
				return ERROR(dictionary_corrupted);
			entropy->rep[i] = rep;
		}
	}

	return dictPtr - (const BYTE *)dict;
}

static size_t ZSTD_decompress_insertDictionary(ZSTD_DCtx *dctx, const void *dict, size_t dictSize)
{
	if (dictSize < 8)
		return ZSTD_refDictContent(dctx, dict, dictSize);
	{
		U32 const magic = ZSTD_readLE32(dict);
		if (magic != ZSTD_DICT_MAGIC) {
			return ZSTD_refDictContent(dctx, dict, dictSize); /* pure content mode */
		}
	}
	dctx->dictID = ZSTD_readLE32((const char *)dict + 4);

	/* load entropy tables */
	{
		size_t const eSize = ZSTD_loadEntropy(&dctx->entropy, dict, dictSize);
		if (ZSTD_isError(eSize))
			return ERROR(dictionary_corrupted);
		dict = (const char *)dict + eSize;
		dictSize -= eSize;
	}
	dctx->litEntropy = dctx->fseEntropy = 1;

	/* reference dictionary content */
	return ZSTD_refDictContent(dctx, dict, dictSize);
}

size_t ZSTD_decompressBegin_usingDict(ZSTD_DCtx *dctx, const void *dict, size_t dictSize)
{
	CHECK_F(ZSTD_decompressBegin(dctx));
	if (dict && dictSize)
		CHECK_E(ZSTD_decompress_insertDictionary(dctx, dict, dictSize), dictionary_corrupted);
	return 0;
}

/* ======   ZSTD_DDict   ====== */

struct ZSTD_DDict_s {
	void *dictBuffer;
	const void *dictContent;
	size_t dictSize;
	ZSTD_entropyTables_t entropy;
	U32 dictID;
	U32 entropyPresent;
	ZSTD_customMem cMem;
}; /* typedef'd to ZSTD_DDict within "zstd.h" */

size_t ZSTD_DDictWorkspaceBound(void) { return ZSTD_ALIGN(sizeof(ZSTD_stack)) + ZSTD_ALIGN(sizeof(ZSTD_DDict)); }

static const void *ZSTD_DDictDictContent(const ZSTD_DDict *ddict) { return ddict->dictContent; }

static size_t ZSTD_DDictDictSize(const ZSTD_DDict *ddict) { return ddict->dictSize; }

static void ZSTD_refDDict(ZSTD_DCtx *dstDCtx, const ZSTD_DDict *ddict)
{
	ZSTD_decompressBegin(dstDCtx); /* init */
	if (ddict) {		       /* support refDDict on NULL */
		dstDCtx->dictID = ddict->dictID;
		dstDCtx->base = ddict->dictContent;
		dstDCtx->vBase = ddict->dictContent;
		dstDCtx->dictEnd = (const BYTE *)ddict->dictContent + ddict->dictSize;
		dstDCtx->previousDstEnd = dstDCtx->dictEnd;
		if (ddict->entropyPresent) {
			dstDCtx->litEntropy = 1;
			dstDCtx->fseEntropy = 1;
			dstDCtx->LLTptr = ddict->entropy.LLTable;
			dstDCtx->MLTptr = ddict->entropy.MLTable;
			dstDCtx->OFTptr = ddict->entropy.OFTable;
			dstDCtx->HUFptr = ddict->entropy.hufTable;
			dstDCtx->entropy.rep[0] = ddict->entropy.rep[0];
			dstDCtx->entropy.rep[1] = ddict->entropy.rep[1];
			dstDCtx->entropy.rep[2] = ddict->entropy.rep[2];
		} else {
			dstDCtx->litEntropy = 0;
			dstDCtx->fseEntropy = 0;
		}
	}
}

static size_t ZSTD_loadEntropy_inDDict(ZSTD_DDict *ddict)
{
	ddict->dictID = 0;
	ddict->entropyPresent = 0;
	if (ddict->dictSize < 8)
		return 0;
	{
		U32 const magic = ZSTD_readLE32(ddict->dictContent);
		if (magic != ZSTD_DICT_MAGIC)
			return 0; /* pure content mode */
	}
	ddict->dictID = ZSTD_readLE32((const char *)ddict->dictContent + 4);

	/* load entropy tables */
	CHECK_E(ZSTD_loadEntropy(&ddict->entropy, ddict->dictContent, ddict->dictSize), dictionary_corrupted);
	ddict->entropyPresent = 1;
	return 0;
}

static ZSTD_DDict *ZSTD_createDDict_advanced(const void *dict, size_t dictSize, unsigned byReference, ZSTD_customMem customMem)
{
	if (!customMem.customAlloc || !customMem.customFree)
		return NULL;

	{
		ZSTD_DDict *const ddict = (ZSTD_DDict *)ZSTD_malloc(sizeof(ZSTD_DDict), customMem);
		if (!ddict)
			return NULL;
		ddict->cMem = customMem;

		if ((byReference) || (!dict) || (!dictSize)) {
			ddict->dictBuffer = NULL;
			ddict->dictContent = dict;
		} else {
			void *const internalBuffer = ZSTD_malloc(dictSize, customMem);
			if (!internalBuffer) {
				ZSTD_freeDDict(ddict);
				return NULL;
			}
			memcpy(internalBuffer, dict, dictSize);
			ddict->dictBuffer = internalBuffer;
			ddict->dictContent = internalBuffer;
		}
		ddict->dictSize = dictSize;
		ddict->entropy.hufTable[0] = (HUF_DTable)((HufLog)*0x1000001); /* cover both little and big endian */
		/* parse dictionary content */
		{
			size_t const errorCode = ZSTD_loadEntropy_inDDict(ddict);
			if (ZSTD_isError(errorCode)) {
				ZSTD_freeDDict(ddict);
				return NULL;
			}
		}

		return ddict;
	}
}

/*! ZSTD_initDDict() :
*   Create a digested dictionary, to start decompression without startup delay.
*   `dict` content is copied inside DDict.
*   Consequently, `dict` can be released after `ZSTD_DDict` creation */
ZSTD_DDict *ZSTD_initDDict(const void *dict, size_t dictSize, void *workspace, size_t workspaceSize)
{
	ZSTD_customMem const stackMem = ZSTD_initStack(workspace, workspaceSize);
	return ZSTD_createDDict_advanced(dict, dictSize, 1, stackMem);
}

size_t ZSTD_freeDDict(ZSTD_DDict *ddict)
{
	if (ddict == NULL)
		return 0; /* support free on NULL */
	{
		ZSTD_customMem const cMem = ddict->cMem;
		ZSTD_free(ddict->dictBuffer, cMem);
		ZSTD_free(ddict, cMem);
		return 0;
	}
}

/*! ZSTD_getDictID_fromDict() :
 *  Provides the dictID stored within dictionary.
 *  if @return == 0, the dictionary is not conformant with Zstandard specification.
 *  It can still be loaded, but as a content-only dictionary. */
unsigned ZSTD_getDictID_fromDict(const void *dict, size_t dictSize)
{
	if (dictSize < 8)
		return 0;
	if (ZSTD_readLE32(dict) != ZSTD_DICT_MAGIC)
		return 0;
	return ZSTD_readLE32((const char *)dict + 4);
}

/*! ZSTD_getDictID_fromDDict() :
 *  Provides the dictID of the dictionary loaded into `ddict`.
 *  If @return == 0, the dictionary is not conformant to Zstandard specification, or empty.
 *  Non-conformant dictionaries can still be loaded, but as content-only dictionaries. */
unsigned ZSTD_getDictID_fromDDict(const ZSTD_DDict *ddict)
{
	if (ddict == NULL)
		return 0;
	return ZSTD_getDictID_fromDict(ddict->dictContent, ddict->dictSize);
}

/*! ZSTD_getDictID_fromFrame() :
 *  Provides the dictID required to decompressed the frame stored within `src`.
 *  If @return == 0, the dictID could not be decoded.
 *  This could for one of the following reasons :
 *  - The frame does not require a dictionary to be decoded (most common case).
 *  - The frame was built with dictID intentionally removed. Whatever dictionary is necessary is a hidden information.
 *    Note : this use case also happens when using a non-conformant dictionary.
 *  - `srcSize` is too small, and as a result, the frame header could not be decoded (only possible if `srcSize < ZSTD_FRAMEHEADERSIZE_MAX`).
 *  - This is not a Zstandard frame.
 *  When identifying the exact failure cause, it's possible to used ZSTD_getFrameParams(), which will provide a more precise error code. */
unsigned ZSTD_getDictID_fromFrame(const void *src, size_t srcSize)
{
	ZSTD_frameParams zfp = {0, 0, 0, 0};
	size_t const hError = ZSTD_getFrameParams(&zfp, src, srcSize);
	if (ZSTD_isError(hError))
		return 0;
	return zfp.dictID;
}

/*! ZSTD_decompress_usingDDict() :
*   Decompression using a pre-digested Dictionary
*   Use dictionary without significant overhead. */
size_t ZSTD_decompress_usingDDict(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void *src, size_t srcSize, const ZSTD_DDict *ddict)
{
	/* pass content and size in case legacy frames are encountered */
	return ZSTD_decompressMultiFrame(dctx, dst, dstCapacity, src, srcSize, NULL, 0, ddict);
}

/*=====================================
*   Streaming decompression
*====================================*/

typedef enum { zdss_init, zdss_loadHeader, zdss_read, zdss_load, zdss_flush } ZSTD_dStreamStage;

/* *** Resource management *** */
struct ZSTD_DStream_s {
	ZSTD_DCtx *dctx;
	ZSTD_DDict *ddictLocal;
	const ZSTD_DDict *ddict;
	ZSTD_frameParams fParams;
	ZSTD_dStreamStage stage;
	char *inBuff;
	size_t inBuffSize;
	size_t inPos;
	size_t maxWindowSize;
	char *outBuff;
	size_t outBuffSize;
	size_t outStart;
	size_t outEnd;
	size_t blockSize;
	BYTE headerBuffer[ZSTD_FRAMEHEADERSIZE_MAX]; /* tmp buffer to store frame header */
	size_t lhSize;
	ZSTD_customMem customMem;
	void *legacyContext;
	U32 previousLegacyVersion;
	U32 legacyVersion;
	U32 hostageByte;
}; /* typedef'd to ZSTD_DStream within "zstd.h" */

size_t ZSTD_DStreamWorkspaceBound(size_t maxWindowSize)
{
	size_t const blockSize = MIN(maxWindowSize, ZSTD_BLOCKSIZE_ABSOLUTEMAX);
	size_t const inBuffSize = blockSize;
	size_t const outBuffSize = maxWindowSize + blockSize + WILDCOPY_OVERLENGTH * 2;
	return ZSTD_DCtxWorkspaceBound() + ZSTD_ALIGN(sizeof(ZSTD_DStream)) + ZSTD_ALIGN(inBuffSize) + ZSTD_ALIGN(outBuffSize);
}

static ZSTD_DStream *ZSTD_createDStream_advanced(ZSTD_customMem customMem)
{
	ZSTD_DStream *zds;

	if (!customMem.customAlloc || !customMem.customFree)
		return NULL;

	zds = (ZSTD_DStream *)ZSTD_malloc(sizeof(ZSTD_DStream), customMem);
	if (zds == NULL)
		return NULL;
	memset(zds, 0, sizeof(ZSTD_DStream));
	memcpy(&zds->customMem, &customMem, sizeof(ZSTD_customMem));
	zds->dctx = ZSTD_createDCtx_advanced(customMem);
	if (zds->dctx == NULL) {
		ZSTD_freeDStream(zds);
		return NULL;
	}
	zds->stage = zdss_init;
	zds->maxWindowSize = ZSTD_MAXWINDOWSIZE_DEFAULT;
	return zds;
}

ZSTD_DStream *ZSTD_initDStream(size_t maxWindowSize, void *workspace, size_t workspaceSize)
{
	ZSTD_customMem const stackMem = ZSTD_initStack(workspace, workspaceSize);
	ZSTD_DStream *zds = ZSTD_createDStream_advanced(stackMem);
	if (!zds) {
		return NULL;
	}

	zds->maxWindowSize = maxWindowSize;
	zds->stage = zdss_loadHeader;
	zds->lhSize = zds->inPos = zds->outStart = zds->outEnd = 0;
	ZSTD_freeDDict(zds->ddictLocal);
	zds->ddictLocal = NULL;
	zds->ddict = zds->ddictLocal;
	zds->legacyVersion = 0;
	zds->hostageByte = 0;

	{
		size_t const blockSize = MIN(zds->maxWindowSize, ZSTD_BLOCKSIZE_ABSOLUTEMAX);
		size_t const neededOutSize = zds->maxWindowSize + blockSize + WILDCOPY_OVERLENGTH * 2;

		zds->inBuff = (char *)ZSTD_malloc(blockSize, zds->customMem);
		zds->inBuffSize = blockSize;
		zds->outBuff = (char *)ZSTD_malloc(neededOutSize, zds->customMem);
		zds->outBuffSize = neededOutSize;
		if (zds->inBuff == NULL || zds->outBuff == NULL) {
			ZSTD_freeDStream(zds);
			return NULL;
		}
	}
	return zds;
}

ZSTD_DStream *ZSTD_initDStream_usingDDict(size_t maxWindowSize, const ZSTD_DDict *ddict, void *workspace, size_t workspaceSize)
{
	ZSTD_DStream *zds = ZSTD_initDStream(maxWindowSize, workspace, workspaceSize);
	if (zds) {
		zds->ddict = ddict;
	}
	return zds;
}

size_t ZSTD_freeDStream(ZSTD_DStream *zds)
{
	if (zds == NULL)
		return 0; /* support free on null */
	{
		ZSTD_customMem const cMem = zds->customMem;
		ZSTD_freeDCtx(zds->dctx);
		zds->dctx = NULL;
		ZSTD_freeDDict(zds->ddictLocal);
		zds->ddictLocal = NULL;
		ZSTD_free(zds->inBuff, cMem);
		zds->inBuff = NULL;
		ZSTD_free(zds->outBuff, cMem);
		zds->outBuff = NULL;
		ZSTD_free(zds, cMem);
		return 0;
	}
}

/* *** Initialization *** */

size_t ZSTD_DStreamInSize(void) { return ZSTD_BLOCKSIZE_ABSOLUTEMAX + ZSTD_blockHeaderSize; }
size_t ZSTD_DStreamOutSize(void) { return ZSTD_BLOCKSIZE_ABSOLUTEMAX; }

size_t ZSTD_resetDStream(ZSTD_DStream *zds)
{
	zds->stage = zdss_loadHeader;
	zds->lhSize = zds->inPos = zds->outStart = zds->outEnd = 0;
	zds->legacyVersion = 0;
	zds->hostageByte = 0;
	return ZSTD_frameHeaderSize_prefix;
}

/* *****   Decompression   ***** */

ZSTD_STATIC size_t ZSTD_limitCopy(void *dst, size_t dstCapacity, const void *src, size_t srcSize)
{
	size_t const length = MIN(dstCapacity, srcSize);
	memcpy(dst, src, length);
	return length;
}

size_t ZSTD_decompressStream(ZSTD_DStream *zds, ZSTD_outBuffer *output, ZSTD_inBuffer *input)
{
	const char *const istart = (const char *)(input->src) + input->pos;
	const char *const iend = (const char *)(input->src) + input->size;
	const char *ip = istart;
	char *const ostart = (char *)(output->dst) + output->pos;
	char *const oend = (char *)(output->dst) + output->size;
	char *op = ostart;
	U32 someMoreWork = 1;

	while (someMoreWork) {
		switch (zds->stage) {
		case zdss_init:
			ZSTD_resetDStream(zds); /* transparent reset on starting decoding a new frame */
						/* fall-through */

		case zdss_loadHeader: {
			size_t const hSize = ZSTD_getFrameParams(&zds->fParams, zds->headerBuffer, zds->lhSize);
			if (ZSTD_isError(hSize))
				return hSize;
			if (hSize != 0) {				   /* need more input */
				size_t const toLoad = hSize - zds->lhSize; /* if hSize!=0, hSize > zds->lhSize */
				if (toLoad > (size_t)(iend - ip)) {	/* not enough input to load full header */
					memcpy(zds->headerBuffer + zds->lhSize, ip, iend - ip);
					zds->lhSize += iend - ip;
					input->pos = input->size;
					return (MAX(ZSTD_frameHeaderSize_min, hSize) - zds->lhSize) +
					       ZSTD_blockHeaderSize; /* remaining header bytes + next block header */
				}
				memcpy(zds->headerBuffer + zds->lhSize, ip, toLoad);
				zds->lhSize = hSize;
				ip += toLoad;
				break;
			}

			/* check for single-pass mode opportunity */
			if (zds->fParams.frameContentSize && zds->fParams.windowSize /* skippable frame if == 0 */
			    && (U64)(size_t)(oend - op) >= zds->fParams.frameContentSize) {
				size_t const cSize = ZSTD_findFrameCompressedSize(istart, iend - istart);
				if (cSize <= (size_t)(iend - istart)) {
					size_t const decompressedSize = ZSTD_decompress_usingDDict(zds->dctx, op, oend - op, istart, cSize, zds->ddict);
					if (ZSTD_isError(decompressedSize))
						return decompressedSize;
					ip = istart + cSize;
					op += decompressedSize;
					zds->dctx->expected = 0;
					zds->stage = zdss_init;
					someMoreWork = 0;
					break;
				}
			}

			/* Consume header */
			ZSTD_refDDict(zds->dctx, zds->ddict);
			{
				size_t const h1Size = ZSTD_nextSrcSizeToDecompress(zds->dctx); /* == ZSTD_frameHeaderSize_prefix */
				CHECK_F(ZSTD_decompressContinue(zds->dctx, NULL, 0, zds->headerBuffer, h1Size));
				{
					size_t const h2Size = ZSTD_nextSrcSizeToDecompress(zds->dctx);
					CHECK_F(ZSTD_decompressContinue(zds->dctx, NULL, 0, zds->headerBuffer + h1Size, h2Size));
				}
			}

			zds->fParams.windowSize = MAX(zds->fParams.windowSize, 1U << ZSTD_WINDOWLOG_ABSOLUTEMIN);
			if (zds->fParams.windowSize > zds->maxWindowSize)
				return ERROR(frameParameter_windowTooLarge);

			/* Buffers are preallocated, but double check */
			{
				size_t const blockSize = MIN(zds->maxWindowSize, ZSTD_BLOCKSIZE_ABSOLUTEMAX);
				size_t const neededOutSize = zds->maxWindowSize + blockSize + WILDCOPY_OVERLENGTH * 2;
				if (zds->inBuffSize < blockSize) {
					return ERROR(GENERIC);
				}
				if (zds->outBuffSize < neededOutSize) {
					return ERROR(GENERIC);
				}
				zds->blockSize = blockSize;
			}
			zds->stage = zdss_read;
		}
		/* fall through */

		case zdss_read: {
			size_t const neededInSize = ZSTD_nextSrcSizeToDecompress(zds->dctx);
			if (neededInSize == 0) { /* end of frame */
				zds->stage = zdss_init;
				someMoreWork = 0;
				break;
			}
			if ((size_t)(iend - ip) >= neededInSize) { /* decode directly from src */
				const int isSkipFrame = ZSTD_isSkipFrame(zds->dctx);
				size_t const decodedSize = ZSTD_decompressContinue(zds->dctx, zds->outBuff + zds->outStart,
										   (isSkipFrame ? 0 : zds->outBuffSize - zds->outStart), ip, neededInSize);
				if (ZSTD_isError(decodedSize))
					return decodedSize;
				ip += neededInSize;
				if (!decodedSize && !isSkipFrame)
					break; /* this was just a header */
				zds->outEnd = zds->outStart + decodedSize;
				zds->stage = zdss_flush;
				break;
			}
			if (ip == iend) {
				someMoreWork = 0;
				break;
			} /* no more input */
			zds->stage = zdss_load;
			/* pass-through */
		}
		/* fall through */

		case zdss_load: {
			size_t const neededInSize = ZSTD_nextSrcSizeToDecompress(zds->dctx);
			size_t const toLoad = neededInSize - zds->inPos; /* should always be <= remaining space within inBuff */
			size_t loadedSize;
			if (toLoad > zds->inBuffSize - zds->inPos)
				return ERROR(corruption_detected); /* should never happen */
			loadedSize = ZSTD_limitCopy(zds->inBuff + zds->inPos, toLoad, ip, iend - ip);
			ip += loadedSize;
			zds->inPos += loadedSize;
			if (loadedSize < toLoad) {
				someMoreWork = 0;
				break;
			} /* not enough input, wait for more */

			/* decode loaded input */
			{
				const int isSkipFrame = ZSTD_isSkipFrame(zds->dctx);
				size_t const decodedSize = ZSTD_decompressContinue(zds->dctx, zds->outBuff + zds->outStart, zds->outBuffSize - zds->outStart,
										   zds->inBuff, neededInSize);
				if (ZSTD_isError(decodedSize))
					return decodedSize;
				zds->inPos = 0; /* input is consumed */
				if (!decodedSize && !isSkipFrame) {
					zds->stage = zdss_read;
					break;
				} /* this was just a header */
				zds->outEnd = zds->outStart + decodedSize;
				zds->stage = zdss_flush;
				/* pass-through */
			}
		}
		/* fall through */

		case zdss_flush: {
			size_t const toFlushSize = zds->outEnd - zds->outStart;
			size_t const flushedSize = ZSTD_limitCopy(op, oend - op, zds->outBuff + zds->outStart, toFlushSize);
			op += flushedSize;
			zds->outStart += flushedSize;
			if (flushedSize == toFlushSize) { /* flush completed */
				zds->stage = zdss_read;
				if (zds->outStart + zds->blockSize > zds->outBuffSize)
					zds->outStart = zds->outEnd = 0;
				break;
			}
			/* cannot complete flush */
			someMoreWork = 0;
			break;
		}
		default:
			return ERROR(GENERIC); /* impossible */
		}
	}

	/* result */
	input->pos += (size_t)(ip - istart);
	output->pos += (size_t)(op - ostart);
	{
		size_t nextSrcSizeHint = ZSTD_nextSrcSizeToDecompress(zds->dctx);
		if (!nextSrcSizeHint) {			    /* frame fully decoded */
			if (zds->outEnd == zds->outStart) { /* output fully flushed */
				if (zds->hostageByte) {
					if (input->pos >= input->size) {
						zds->stage = zdss_read;
						return 1;
					}	     /* can't release hostage (not present) */
					input->pos++; /* release hostage */
				}
				return 0;
			}
			if (!zds->hostageByte) { /* output not fully flushed; keep last byte as hostage; will be released when all output is flushed */
				input->pos--;    /* note : pos > 0, otherwise, impossible to finish reading last block */
				zds->hostageByte = 1;
			}
			return 1;
		}
		nextSrcSizeHint += ZSTD_blockHeaderSize * (ZSTD_nextInputType(zds->dctx) == ZSTDnit_block); /* preload header of next block */
		if (zds->inPos > nextSrcSizeHint)
			return ERROR(GENERIC); /* should never happen */
		nextSrcSizeHint -= zds->inPos; /* already loaded*/
		return nextSrcSizeHint;
	}
}

EXPORT_SYMBOL(ZSTD_DCtxWorkspaceBound);
EXPORT_SYMBOL(ZSTD_initDCtx);
EXPORT_SYMBOL(ZSTD_decompressDCtx);
EXPORT_SYMBOL(ZSTD_decompress_usingDict);

EXPORT_SYMBOL(ZSTD_DDictWorkspaceBound);
EXPORT_SYMBOL(ZSTD_initDDict);
EXPORT_SYMBOL(ZSTD_decompress_usingDDict);

EXPORT_SYMBOL(ZSTD_DStreamWorkspaceBound);
EXPORT_SYMBOL(ZSTD_initDStream);
EXPORT_SYMBOL(ZSTD_initDStream_usingDDict);
EXPORT_SYMBOL(ZSTD_resetDStream);
EXPORT_SYMBOL(ZSTD_decompressStream);
EXPORT_SYMBOL(ZSTD_DStreamInSize);
EXPORT_SYMBOL(ZSTD_DStreamOutSize);

EXPORT_SYMBOL(ZSTD_findFrameCompressedSize);
EXPORT_SYMBOL(ZSTD_getFrameContentSize);
EXPORT_SYMBOL(ZSTD_findDecompressedSize);

EXPORT_SYMBOL(ZSTD_isFrame);
EXPORT_SYMBOL(ZSTD_getDictID_fromDict);
EXPORT_SYMBOL(ZSTD_getDictID_fromDDict);
EXPORT_SYMBOL(ZSTD_getDictID_fromFrame);

EXPORT_SYMBOL(ZSTD_getFrameParams);
EXPORT_SYMBOL(ZSTD_decompressBegin);
EXPORT_SYMBOL(ZSTD_decompressBegin_usingDict);
EXPORT_SYMBOL(ZSTD_copyDCtx);
EXPORT_SYMBOL(ZSTD_nextSrcSizeToDecompress);
EXPORT_SYMBOL(ZSTD_decompressContinue);
EXPORT_SYMBOL(ZSTD_nextInputType);

EXPORT_SYMBOL(ZSTD_decompressBlock);
EXPORT_SYMBOL(ZSTD_insertBlock);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Zstd Decompressor");
