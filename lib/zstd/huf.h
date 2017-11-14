/*
 * Huffman coder, part of New Generation Entropy library
 * header file
 * Copyright (C) 2013-2016, Yann Collet.
 *
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation. This program is dual-licensed; you may select
 * either version 2 of the GNU General Public License ("GPL") or BSD license
 * ("BSD").
 *
 * You can contact the author at :
 * - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
 */
#ifndef HUF_H_298734234
#define HUF_H_298734234

/* *** Dependencies *** */
#include <linux/types.h> /* size_t */

/* ***   Tool functions *** */
#define HUF_BLOCKSIZE_MAX (128 * 1024) /**< maximum input size for a single block compressed with HUF_compress */
size_t HUF_compressBound(size_t size); /**< maximum compressed size (worst case) */

/* Error Management */
unsigned HUF_isError(size_t code); /**< tells if a return value is an error code */

/* ***   Advanced function   *** */

/** HUF_compress4X_wksp() :
*   Same as HUF_compress2(), but uses externally allocated `workSpace`, which must be a table of >= 1024 unsigned */
size_t HUF_compress4X_wksp(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void *workSpace,
			   size_t wkspSize); /**< `workSpace` must be a table of at least HUF_COMPRESS_WORKSPACE_SIZE_U32 unsigned */

/* *** Dependencies *** */
#include "mem.h" /* U32 */

/* *** Constants *** */
#define HUF_TABLELOG_MAX 12     /* max configured tableLog (for static allocation); can be modified up to HUF_ABSOLUTEMAX_TABLELOG */
#define HUF_TABLELOG_DEFAULT 11 /* tableLog by default, when not specified */
#define HUF_SYMBOLVALUE_MAX 255

#define HUF_TABLELOG_ABSOLUTEMAX 15 /* absolute limit of HUF_MAX_TABLELOG. Beyond that value, code does not work */
#if (HUF_TABLELOG_MAX > HUF_TABLELOG_ABSOLUTEMAX)
#error "HUF_TABLELOG_MAX is too large !"
#endif

/* ****************************************
*  Static allocation
******************************************/
/* HUF buffer bounds */
#define HUF_CTABLEBOUND 129
#define HUF_BLOCKBOUND(size) (size + (size >> 8) + 8)			 /* only true if incompressible pre-filtered with fast heuristic */
#define HUF_COMPRESSBOUND(size) (HUF_CTABLEBOUND + HUF_BLOCKBOUND(size)) /* Macro version, useful for static allocation */

/* static allocation of HUF's Compression Table */
#define HUF_CREATE_STATIC_CTABLE(name, maxSymbolValue) \
	U32 name##hb[maxSymbolValue + 1];              \
	void *name##hv = &(name##hb);                  \
	HUF_CElt *name = (HUF_CElt *)(name##hv) /* no final ; */

/* static allocation of HUF's DTable */
typedef U32 HUF_DTable;
#define HUF_DTABLE_SIZE(maxTableLog) (1 + (1 << (maxTableLog)))
#define HUF_CREATE_STATIC_DTABLEX2(DTable, maxTableLog) HUF_DTable DTable[HUF_DTABLE_SIZE((maxTableLog)-1)] = {((U32)((maxTableLog)-1) * 0x01000001)}
#define HUF_CREATE_STATIC_DTABLEX4(DTable, maxTableLog) HUF_DTable DTable[HUF_DTABLE_SIZE(maxTableLog)] = {((U32)(maxTableLog)*0x01000001)}

/* The workspace must have alignment at least 4 and be at least this large */
#define HUF_COMPRESS_WORKSPACE_SIZE (6 << 10)
#define HUF_COMPRESS_WORKSPACE_SIZE_U32 (HUF_COMPRESS_WORKSPACE_SIZE / sizeof(U32))

/* The workspace must have alignment at least 4 and be at least this large */
#define HUF_DECOMPRESS_WORKSPACE_SIZE (3 << 10)
#define HUF_DECOMPRESS_WORKSPACE_SIZE_U32 (HUF_DECOMPRESS_WORKSPACE_SIZE / sizeof(U32))

/* ****************************************
*  Advanced decompression functions
******************************************/
size_t HUF_decompress4X_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace, size_t workspaceSize); /**< decodes RLE and uncompressed */
size_t HUF_decompress4X_hufOnly_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace,
				size_t workspaceSize);							       /**< considers RLE and uncompressed as errors */
size_t HUF_decompress4X2_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace,
				   size_t workspaceSize); /**< single-symbol decoder */
size_t HUF_decompress4X4_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace,
				   size_t workspaceSize); /**< double-symbols decoder */

/* ****************************************
*  HUF detailed API
******************************************/
/*!
HUF_compress() does the following:
1. count symbol occurrence from source[] into table count[] using FSE_count()
2. (optional) refine tableLog using HUF_optimalTableLog()
3. build Huffman table from count using HUF_buildCTable()
4. save Huffman table to memory buffer using HUF_writeCTable_wksp()
5. encode the data stream using HUF_compress4X_usingCTable()

The following API allows targeting specific sub-functions for advanced tasks.
For example, it's possible to compress several blocks using the same 'CTable',
or to save and regenerate 'CTable' using external methods.
*/
/* FSE_count() : find it within "fse.h" */
unsigned HUF_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue);
typedef struct HUF_CElt_s HUF_CElt; /* incomplete type */
size_t HUF_writeCTable_wksp(void *dst, size_t maxDstSize, const HUF_CElt *CTable, unsigned maxSymbolValue, unsigned huffLog, void *workspace, size_t workspaceSize);
size_t HUF_compress4X_usingCTable(void *dst, size_t dstSize, const void *src, size_t srcSize, const HUF_CElt *CTable);

typedef enum {
	HUF_repeat_none,  /**< Cannot use the previous table */
	HUF_repeat_check, /**< Can use the previous table but it must be checked. Note : The previous table must have been constructed by HUF_compress{1,
			     4}X_repeat */
	HUF_repeat_valid  /**< Can use the previous table and it is asumed to be valid */
} HUF_repeat;
/** HUF_compress4X_repeat() :
*   Same as HUF_compress4X_wksp(), but considers using hufTable if *repeat != HUF_repeat_none.
*   If it uses hufTable it does not modify hufTable or repeat.
*   If it doesn't, it sets *repeat = HUF_repeat_none, and it sets hufTable to the table used.
*   If preferRepeat then the old table will always be used if valid. */
size_t HUF_compress4X_repeat(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void *workSpace,
			     size_t wkspSize, HUF_CElt *hufTable, HUF_repeat *repeat,
			     int preferRepeat); /**< `workSpace` must be a table of at least HUF_COMPRESS_WORKSPACE_SIZE_U32 unsigned */

/** HUF_buildCTable_wksp() :
 *  Same as HUF_buildCTable(), but using externally allocated scratch buffer.
 *  `workSpace` must be aligned on 4-bytes boundaries, and be at least as large as a table of 1024 unsigned.
 */
size_t HUF_buildCTable_wksp(HUF_CElt *tree, const U32 *count, U32 maxSymbolValue, U32 maxNbBits, void *workSpace, size_t wkspSize);

/*! HUF_readStats() :
	Read compact Huffman tree, saved by HUF_writeCTable().
	`huffWeight` is destination buffer.
	@return : size read from `src` , or an error Code .
	Note : Needed by HUF_readCTable() and HUF_readDTableXn() . */
size_t HUF_readStats_wksp(BYTE *huffWeight, size_t hwSize, U32 *rankStats, U32 *nbSymbolsPtr, U32 *tableLogPtr, const void *src, size_t srcSize,
			  void *workspace, size_t workspaceSize);

/** HUF_readCTable() :
*   Loading a CTable saved with HUF_writeCTable() */
size_t HUF_readCTable_wksp(HUF_CElt *CTable, unsigned maxSymbolValue, const void *src, size_t srcSize, void *workspace, size_t workspaceSize);

/*
HUF_decompress() does the following:
1. select the decompression algorithm (X2, X4) based on pre-computed heuristics
2. build Huffman table from save, using HUF_readDTableXn()
3. decode 1 or 4 segments in parallel using HUF_decompressSXn_usingDTable
*/

/** HUF_selectDecoder() :
*   Tells which decoder is likely to decode faster,
*   based on a set of pre-determined metrics.
*   @return : 0==HUF_decompress4X2, 1==HUF_decompress4X4 .
*   Assumption : 0 < cSrcSize < dstSize <= 128 KB */
U32 HUF_selectDecoder(size_t dstSize, size_t cSrcSize);

size_t HUF_readDTableX2_wksp(HUF_DTable *DTable, const void *src, size_t srcSize, void *workspace, size_t workspaceSize);
size_t HUF_readDTableX4_wksp(HUF_DTable *DTable, const void *src, size_t srcSize, void *workspace, size_t workspaceSize);

size_t HUF_decompress4X_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable);
size_t HUF_decompress4X2_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable);
size_t HUF_decompress4X4_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable);

/* single stream variants */

size_t HUF_compress1X_wksp(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void *workSpace,
			   size_t wkspSize); /**< `workSpace` must be a table of at least HUF_COMPRESS_WORKSPACE_SIZE_U32 unsigned */
size_t HUF_compress1X_usingCTable(void *dst, size_t dstSize, const void *src, size_t srcSize, const HUF_CElt *CTable);
/** HUF_compress1X_repeat() :
*   Same as HUF_compress1X_wksp(), but considers using hufTable if *repeat != HUF_repeat_none.
*   If it uses hufTable it does not modify hufTable or repeat.
*   If it doesn't, it sets *repeat = HUF_repeat_none, and it sets hufTable to the table used.
*   If preferRepeat then the old table will always be used if valid. */
size_t HUF_compress1X_repeat(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void *workSpace,
			     size_t wkspSize, HUF_CElt *hufTable, HUF_repeat *repeat,
			     int preferRepeat); /**< `workSpace` must be a table of at least HUF_COMPRESS_WORKSPACE_SIZE_U32 unsigned */

size_t HUF_decompress1X_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace, size_t workspaceSize);
size_t HUF_decompress1X2_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace,
				   size_t workspaceSize); /**< single-symbol decoder */
size_t HUF_decompress1X4_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace,
				   size_t workspaceSize); /**< double-symbols decoder */

size_t HUF_decompress1X_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize,
				    const HUF_DTable *DTable); /**< automatic selection of sing or double symbol decoder, based on DTable */
size_t HUF_decompress1X2_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable);
size_t HUF_decompress1X4_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable);

#endif /* HUF_H_298734234 */
