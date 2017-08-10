/*
 * Huffman decoder, part of New Generation Entropy library
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

/* **************************************************************
*  Compiler specifics
****************************************************************/
#define FORCE_INLINE static __always_inline

/* **************************************************************
*  Dependencies
****************************************************************/
#include "bitstream.h" /* BIT_* */
#include "fse.h"       /* header compression */
#include "huf.h"
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/string.h> /* memcpy, memset */

/* **************************************************************
*  Error Management
****************************************************************/
#define HUF_STATIC_ASSERT(c)                                   \
	{                                                      \
		enum { HUF_static_assert = 1 / (int)(!!(c)) }; \
	} /* use only *after* variable declarations */

/*-***************************/
/*  generic DTableDesc       */
/*-***************************/

typedef struct {
	BYTE maxTableLog;
	BYTE tableType;
	BYTE tableLog;
	BYTE reserved;
} DTableDesc;

static DTableDesc HUF_getDTableDesc(const HUF_DTable *table)
{
	DTableDesc dtd;
	memcpy(&dtd, table, sizeof(dtd));
	return dtd;
}

/*-***************************/
/*  single-symbol decoding   */
/*-***************************/

typedef struct {
	BYTE byte;
	BYTE nbBits;
} HUF_DEltX2; /* single-symbol decoding */

size_t HUF_readDTableX2_wksp(HUF_DTable *DTable, const void *src, size_t srcSize, void *workspace, size_t workspaceSize)
{
	U32 tableLog = 0;
	U32 nbSymbols = 0;
	size_t iSize;
	void *const dtPtr = DTable + 1;
	HUF_DEltX2 *const dt = (HUF_DEltX2 *)dtPtr;

	U32 *rankVal;
	BYTE *huffWeight;
	size_t spaceUsed32 = 0;

	rankVal = (U32 *)workspace + spaceUsed32;
	spaceUsed32 += HUF_TABLELOG_ABSOLUTEMAX + 1;
	huffWeight = (BYTE *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += ALIGN(HUF_SYMBOLVALUE_MAX + 1, sizeof(U32)) >> 2;

	if ((spaceUsed32 << 2) > workspaceSize)
		return ERROR(tableLog_tooLarge);
	workspace = (U32 *)workspace + spaceUsed32;
	workspaceSize -= (spaceUsed32 << 2);

	HUF_STATIC_ASSERT(sizeof(DTableDesc) == sizeof(HUF_DTable));
	/* memset(huffWeight, 0, sizeof(huffWeight)); */ /* is not necessary, even though some analyzer complain ... */

	iSize = HUF_readStats_wksp(huffWeight, HUF_SYMBOLVALUE_MAX + 1, rankVal, &nbSymbols, &tableLog, src, srcSize, workspace, workspaceSize);
	if (HUF_isError(iSize))
		return iSize;

	/* Table header */
	{
		DTableDesc dtd = HUF_getDTableDesc(DTable);
		if (tableLog > (U32)(dtd.maxTableLog + 1))
			return ERROR(tableLog_tooLarge); /* DTable too small, Huffman tree cannot fit in */
		dtd.tableType = 0;
		dtd.tableLog = (BYTE)tableLog;
		memcpy(DTable, &dtd, sizeof(dtd));
	}

	/* Calculate starting value for each rank */
	{
		U32 n, nextRankStart = 0;
		for (n = 1; n < tableLog + 1; n++) {
			U32 const curr = nextRankStart;
			nextRankStart += (rankVal[n] << (n - 1));
			rankVal[n] = curr;
		}
	}

	/* fill DTable */
	{
		U32 n;
		for (n = 0; n < nbSymbols; n++) {
			U32 const w = huffWeight[n];
			U32 const length = (1 << w) >> 1;
			U32 u;
			HUF_DEltX2 D;
			D.byte = (BYTE)n;
			D.nbBits = (BYTE)(tableLog + 1 - w);
			for (u = rankVal[w]; u < rankVal[w] + length; u++)
				dt[u] = D;
			rankVal[w] += length;
		}
	}

	return iSize;
}

static BYTE HUF_decodeSymbolX2(BIT_DStream_t *Dstream, const HUF_DEltX2 *dt, const U32 dtLog)
{
	size_t const val = BIT_lookBitsFast(Dstream, dtLog); /* note : dtLog >= 1 */
	BYTE const c = dt[val].byte;
	BIT_skipBits(Dstream, dt[val].nbBits);
	return c;
}

#define HUF_DECODE_SYMBOLX2_0(ptr, DStreamPtr) *ptr++ = HUF_decodeSymbolX2(DStreamPtr, dt, dtLog)

#define HUF_DECODE_SYMBOLX2_1(ptr, DStreamPtr)         \
	if (ZSTD_64bits() || (HUF_TABLELOG_MAX <= 12)) \
	HUF_DECODE_SYMBOLX2_0(ptr, DStreamPtr)

#define HUF_DECODE_SYMBOLX2_2(ptr, DStreamPtr) \
	if (ZSTD_64bits())                     \
	HUF_DECODE_SYMBOLX2_0(ptr, DStreamPtr)

FORCE_INLINE size_t HUF_decodeStreamX2(BYTE *p, BIT_DStream_t *const bitDPtr, BYTE *const pEnd, const HUF_DEltX2 *const dt, const U32 dtLog)
{
	BYTE *const pStart = p;

	/* up to 4 symbols at a time */
	while ((BIT_reloadDStream(bitDPtr) == BIT_DStream_unfinished) && (p <= pEnd - 4)) {
		HUF_DECODE_SYMBOLX2_2(p, bitDPtr);
		HUF_DECODE_SYMBOLX2_1(p, bitDPtr);
		HUF_DECODE_SYMBOLX2_2(p, bitDPtr);
		HUF_DECODE_SYMBOLX2_0(p, bitDPtr);
	}

	/* closer to the end */
	while ((BIT_reloadDStream(bitDPtr) == BIT_DStream_unfinished) && (p < pEnd))
		HUF_DECODE_SYMBOLX2_0(p, bitDPtr);

	/* no more data to retrieve from bitstream, hence no need to reload */
	while (p < pEnd)
		HUF_DECODE_SYMBOLX2_0(p, bitDPtr);

	return pEnd - pStart;
}

static size_t HUF_decompress1X2_usingDTable_internal(void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable)
{
	BYTE *op = (BYTE *)dst;
	BYTE *const oend = op + dstSize;
	const void *dtPtr = DTable + 1;
	const HUF_DEltX2 *const dt = (const HUF_DEltX2 *)dtPtr;
	BIT_DStream_t bitD;
	DTableDesc const dtd = HUF_getDTableDesc(DTable);
	U32 const dtLog = dtd.tableLog;

	{
		size_t const errorCode = BIT_initDStream(&bitD, cSrc, cSrcSize);
		if (HUF_isError(errorCode))
			return errorCode;
	}

	HUF_decodeStreamX2(op, &bitD, oend, dt, dtLog);

	/* check */
	if (!BIT_endOfDStream(&bitD))
		return ERROR(corruption_detected);

	return dstSize;
}

size_t HUF_decompress1X2_usingDTable(void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable)
{
	DTableDesc dtd = HUF_getDTableDesc(DTable);
	if (dtd.tableType != 0)
		return ERROR(GENERIC);
	return HUF_decompress1X2_usingDTable_internal(dst, dstSize, cSrc, cSrcSize, DTable);
}

size_t HUF_decompress1X2_DCtx_wksp(HUF_DTable *DCtx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace, size_t workspaceSize)
{
	const BYTE *ip = (const BYTE *)cSrc;

	size_t const hSize = HUF_readDTableX2_wksp(DCtx, cSrc, cSrcSize, workspace, workspaceSize);
	if (HUF_isError(hSize))
		return hSize;
	if (hSize >= cSrcSize)
		return ERROR(srcSize_wrong);
	ip += hSize;
	cSrcSize -= hSize;

	return HUF_decompress1X2_usingDTable_internal(dst, dstSize, ip, cSrcSize, DCtx);
}

static size_t HUF_decompress4X2_usingDTable_internal(void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable)
{
	/* Check */
	if (cSrcSize < 10)
		return ERROR(corruption_detected); /* strict minimum : jump table + 1 byte per stream */

	{
		const BYTE *const istart = (const BYTE *)cSrc;
		BYTE *const ostart = (BYTE *)dst;
		BYTE *const oend = ostart + dstSize;
		const void *const dtPtr = DTable + 1;
		const HUF_DEltX2 *const dt = (const HUF_DEltX2 *)dtPtr;

		/* Init */
		BIT_DStream_t bitD1;
		BIT_DStream_t bitD2;
		BIT_DStream_t bitD3;
		BIT_DStream_t bitD4;
		size_t const length1 = ZSTD_readLE16(istart);
		size_t const length2 = ZSTD_readLE16(istart + 2);
		size_t const length3 = ZSTD_readLE16(istart + 4);
		size_t const length4 = cSrcSize - (length1 + length2 + length3 + 6);
		const BYTE *const istart1 = istart + 6; /* jumpTable */
		const BYTE *const istart2 = istart1 + length1;
		const BYTE *const istart3 = istart2 + length2;
		const BYTE *const istart4 = istart3 + length3;
		const size_t segmentSize = (dstSize + 3) / 4;
		BYTE *const opStart2 = ostart + segmentSize;
		BYTE *const opStart3 = opStart2 + segmentSize;
		BYTE *const opStart4 = opStart3 + segmentSize;
		BYTE *op1 = ostart;
		BYTE *op2 = opStart2;
		BYTE *op3 = opStart3;
		BYTE *op4 = opStart4;
		U32 endSignal;
		DTableDesc const dtd = HUF_getDTableDesc(DTable);
		U32 const dtLog = dtd.tableLog;

		if (length4 > cSrcSize)
			return ERROR(corruption_detected); /* overflow */
		{
			size_t const errorCode = BIT_initDStream(&bitD1, istart1, length1);
			if (HUF_isError(errorCode))
				return errorCode;
		}
		{
			size_t const errorCode = BIT_initDStream(&bitD2, istart2, length2);
			if (HUF_isError(errorCode))
				return errorCode;
		}
		{
			size_t const errorCode = BIT_initDStream(&bitD3, istart3, length3);
			if (HUF_isError(errorCode))
				return errorCode;
		}
		{
			size_t const errorCode = BIT_initDStream(&bitD4, istart4, length4);
			if (HUF_isError(errorCode))
				return errorCode;
		}

		/* 16-32 symbols per loop (4-8 symbols per stream) */
		endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
		for (; (endSignal == BIT_DStream_unfinished) && (op4 < (oend - 7));) {
			HUF_DECODE_SYMBOLX2_2(op1, &bitD1);
			HUF_DECODE_SYMBOLX2_2(op2, &bitD2);
			HUF_DECODE_SYMBOLX2_2(op3, &bitD3);
			HUF_DECODE_SYMBOLX2_2(op4, &bitD4);
			HUF_DECODE_SYMBOLX2_1(op1, &bitD1);
			HUF_DECODE_SYMBOLX2_1(op2, &bitD2);
			HUF_DECODE_SYMBOLX2_1(op3, &bitD3);
			HUF_DECODE_SYMBOLX2_1(op4, &bitD4);
			HUF_DECODE_SYMBOLX2_2(op1, &bitD1);
			HUF_DECODE_SYMBOLX2_2(op2, &bitD2);
			HUF_DECODE_SYMBOLX2_2(op3, &bitD3);
			HUF_DECODE_SYMBOLX2_2(op4, &bitD4);
			HUF_DECODE_SYMBOLX2_0(op1, &bitD1);
			HUF_DECODE_SYMBOLX2_0(op2, &bitD2);
			HUF_DECODE_SYMBOLX2_0(op3, &bitD3);
			HUF_DECODE_SYMBOLX2_0(op4, &bitD4);
			endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
		}

		/* check corruption */
		if (op1 > opStart2)
			return ERROR(corruption_detected);
		if (op2 > opStart3)
			return ERROR(corruption_detected);
		if (op3 > opStart4)
			return ERROR(corruption_detected);
		/* note : op4 supposed already verified within main loop */

		/* finish bitStreams one by one */
		HUF_decodeStreamX2(op1, &bitD1, opStart2, dt, dtLog);
		HUF_decodeStreamX2(op2, &bitD2, opStart3, dt, dtLog);
		HUF_decodeStreamX2(op3, &bitD3, opStart4, dt, dtLog);
		HUF_decodeStreamX2(op4, &bitD4, oend, dt, dtLog);

		/* check */
		endSignal = BIT_endOfDStream(&bitD1) & BIT_endOfDStream(&bitD2) & BIT_endOfDStream(&bitD3) & BIT_endOfDStream(&bitD4);
		if (!endSignal)
			return ERROR(corruption_detected);

		/* decoded size */
		return dstSize;
	}
}

size_t HUF_decompress4X2_usingDTable(void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable)
{
	DTableDesc dtd = HUF_getDTableDesc(DTable);
	if (dtd.tableType != 0)
		return ERROR(GENERIC);
	return HUF_decompress4X2_usingDTable_internal(dst, dstSize, cSrc, cSrcSize, DTable);
}

size_t HUF_decompress4X2_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace, size_t workspaceSize)
{
	const BYTE *ip = (const BYTE *)cSrc;

	size_t const hSize = HUF_readDTableX2_wksp(dctx, cSrc, cSrcSize, workspace, workspaceSize);
	if (HUF_isError(hSize))
		return hSize;
	if (hSize >= cSrcSize)
		return ERROR(srcSize_wrong);
	ip += hSize;
	cSrcSize -= hSize;

	return HUF_decompress4X2_usingDTable_internal(dst, dstSize, ip, cSrcSize, dctx);
}

/* *************************/
/* double-symbols decoding */
/* *************************/
typedef struct {
	U16 sequence;
	BYTE nbBits;
	BYTE length;
} HUF_DEltX4; /* double-symbols decoding */

typedef struct {
	BYTE symbol;
	BYTE weight;
} sortedSymbol_t;

/* HUF_fillDTableX4Level2() :
 * `rankValOrigin` must be a table of at least (HUF_TABLELOG_MAX + 1) U32 */
static void HUF_fillDTableX4Level2(HUF_DEltX4 *DTable, U32 sizeLog, const U32 consumed, const U32 *rankValOrigin, const int minWeight,
				   const sortedSymbol_t *sortedSymbols, const U32 sortedListSize, U32 nbBitsBaseline, U16 baseSeq)
{
	HUF_DEltX4 DElt;
	U32 rankVal[HUF_TABLELOG_MAX + 1];

	/* get pre-calculated rankVal */
	memcpy(rankVal, rankValOrigin, sizeof(rankVal));

	/* fill skipped values */
	if (minWeight > 1) {
		U32 i, skipSize = rankVal[minWeight];
		ZSTD_writeLE16(&(DElt.sequence), baseSeq);
		DElt.nbBits = (BYTE)(consumed);
		DElt.length = 1;
		for (i = 0; i < skipSize; i++)
			DTable[i] = DElt;
	}

	/* fill DTable */
	{
		U32 s;
		for (s = 0; s < sortedListSize; s++) { /* note : sortedSymbols already skipped */
			const U32 symbol = sortedSymbols[s].symbol;
			const U32 weight = sortedSymbols[s].weight;
			const U32 nbBits = nbBitsBaseline - weight;
			const U32 length = 1 << (sizeLog - nbBits);
			const U32 start = rankVal[weight];
			U32 i = start;
			const U32 end = start + length;

			ZSTD_writeLE16(&(DElt.sequence), (U16)(baseSeq + (symbol << 8)));
			DElt.nbBits = (BYTE)(nbBits + consumed);
			DElt.length = 2;
			do {
				DTable[i++] = DElt;
			} while (i < end); /* since length >= 1 */

			rankVal[weight] += length;
		}
	}
}

typedef U32 rankVal_t[HUF_TABLELOG_MAX][HUF_TABLELOG_MAX + 1];
typedef U32 rankValCol_t[HUF_TABLELOG_MAX + 1];

static void HUF_fillDTableX4(HUF_DEltX4 *DTable, const U32 targetLog, const sortedSymbol_t *sortedList, const U32 sortedListSize, const U32 *rankStart,
			     rankVal_t rankValOrigin, const U32 maxWeight, const U32 nbBitsBaseline)
{
	U32 rankVal[HUF_TABLELOG_MAX + 1];
	const int scaleLog = nbBitsBaseline - targetLog; /* note : targetLog >= srcLog, hence scaleLog <= 1 */
	const U32 minBits = nbBitsBaseline - maxWeight;
	U32 s;

	memcpy(rankVal, rankValOrigin, sizeof(rankVal));

	/* fill DTable */
	for (s = 0; s < sortedListSize; s++) {
		const U16 symbol = sortedList[s].symbol;
		const U32 weight = sortedList[s].weight;
		const U32 nbBits = nbBitsBaseline - weight;
		const U32 start = rankVal[weight];
		const U32 length = 1 << (targetLog - nbBits);

		if (targetLog - nbBits >= minBits) { /* enough room for a second symbol */
			U32 sortedRank;
			int minWeight = nbBits + scaleLog;
			if (minWeight < 1)
				minWeight = 1;
			sortedRank = rankStart[minWeight];
			HUF_fillDTableX4Level2(DTable + start, targetLog - nbBits, nbBits, rankValOrigin[nbBits], minWeight, sortedList + sortedRank,
					       sortedListSize - sortedRank, nbBitsBaseline, symbol);
		} else {
			HUF_DEltX4 DElt;
			ZSTD_writeLE16(&(DElt.sequence), symbol);
			DElt.nbBits = (BYTE)(nbBits);
			DElt.length = 1;
			{
				U32 const end = start + length;
				U32 u;
				for (u = start; u < end; u++)
					DTable[u] = DElt;
			}
		}
		rankVal[weight] += length;
	}
}

size_t HUF_readDTableX4_wksp(HUF_DTable *DTable, const void *src, size_t srcSize, void *workspace, size_t workspaceSize)
{
	U32 tableLog, maxW, sizeOfSort, nbSymbols;
	DTableDesc dtd = HUF_getDTableDesc(DTable);
	U32 const maxTableLog = dtd.maxTableLog;
	size_t iSize;
	void *dtPtr = DTable + 1; /* force compiler to avoid strict-aliasing */
	HUF_DEltX4 *const dt = (HUF_DEltX4 *)dtPtr;
	U32 *rankStart;

	rankValCol_t *rankVal;
	U32 *rankStats;
	U32 *rankStart0;
	sortedSymbol_t *sortedSymbol;
	BYTE *weightList;
	size_t spaceUsed32 = 0;

	HUF_STATIC_ASSERT((sizeof(rankValCol_t) & 3) == 0);

	rankVal = (rankValCol_t *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += (sizeof(rankValCol_t) * HUF_TABLELOG_MAX) >> 2;
	rankStats = (U32 *)workspace + spaceUsed32;
	spaceUsed32 += HUF_TABLELOG_MAX + 1;
	rankStart0 = (U32 *)workspace + spaceUsed32;
	spaceUsed32 += HUF_TABLELOG_MAX + 2;
	sortedSymbol = (sortedSymbol_t *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += ALIGN(sizeof(sortedSymbol_t) * (HUF_SYMBOLVALUE_MAX + 1), sizeof(U32)) >> 2;
	weightList = (BYTE *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += ALIGN(HUF_SYMBOLVALUE_MAX + 1, sizeof(U32)) >> 2;

	if ((spaceUsed32 << 2) > workspaceSize)
		return ERROR(tableLog_tooLarge);
	workspace = (U32 *)workspace + spaceUsed32;
	workspaceSize -= (spaceUsed32 << 2);

	rankStart = rankStart0 + 1;
	memset(rankStats, 0, sizeof(U32) * (2 * HUF_TABLELOG_MAX + 2 + 1));

	HUF_STATIC_ASSERT(sizeof(HUF_DEltX4) == sizeof(HUF_DTable)); /* if compiler fails here, assertion is wrong */
	if (maxTableLog > HUF_TABLELOG_MAX)
		return ERROR(tableLog_tooLarge);
	/* memset(weightList, 0, sizeof(weightList)); */ /* is not necessary, even though some analyzer complain ... */

	iSize = HUF_readStats_wksp(weightList, HUF_SYMBOLVALUE_MAX + 1, rankStats, &nbSymbols, &tableLog, src, srcSize, workspace, workspaceSize);
	if (HUF_isError(iSize))
		return iSize;

	/* check result */
	if (tableLog > maxTableLog)
		return ERROR(tableLog_tooLarge); /* DTable can't fit code depth */

	/* find maxWeight */
	for (maxW = tableLog; rankStats[maxW] == 0; maxW--) {
	} /* necessarily finds a solution before 0 */

	/* Get start index of each weight */
	{
		U32 w, nextRankStart = 0;
		for (w = 1; w < maxW + 1; w++) {
			U32 curr = nextRankStart;
			nextRankStart += rankStats[w];
			rankStart[w] = curr;
		}
		rankStart[0] = nextRankStart; /* put all 0w symbols at the end of sorted list*/
		sizeOfSort = nextRankStart;
	}

	/* sort symbols by weight */
	{
		U32 s;
		for (s = 0; s < nbSymbols; s++) {
			U32 const w = weightList[s];
			U32 const r = rankStart[w]++;
			sortedSymbol[r].symbol = (BYTE)s;
			sortedSymbol[r].weight = (BYTE)w;
		}
		rankStart[0] = 0; /* forget 0w symbols; this is beginning of weight(1) */
	}

	/* Build rankVal */
	{
		U32 *const rankVal0 = rankVal[0];
		{
			int const rescale = (maxTableLog - tableLog) - 1; /* tableLog <= maxTableLog */
			U32 nextRankVal = 0;
			U32 w;
			for (w = 1; w < maxW + 1; w++) {
				U32 curr = nextRankVal;
				nextRankVal += rankStats[w] << (w + rescale);
				rankVal0[w] = curr;
			}
		}
		{
			U32 const minBits = tableLog + 1 - maxW;
			U32 consumed;
			for (consumed = minBits; consumed < maxTableLog - minBits + 1; consumed++) {
				U32 *const rankValPtr = rankVal[consumed];
				U32 w;
				for (w = 1; w < maxW + 1; w++) {
					rankValPtr[w] = rankVal0[w] >> consumed;
				}
			}
		}
	}

	HUF_fillDTableX4(dt, maxTableLog, sortedSymbol, sizeOfSort, rankStart0, rankVal, maxW, tableLog + 1);

	dtd.tableLog = (BYTE)maxTableLog;
	dtd.tableType = 1;
	memcpy(DTable, &dtd, sizeof(dtd));
	return iSize;
}

static U32 HUF_decodeSymbolX4(void *op, BIT_DStream_t *DStream, const HUF_DEltX4 *dt, const U32 dtLog)
{
	size_t const val = BIT_lookBitsFast(DStream, dtLog); /* note : dtLog >= 1 */
	memcpy(op, dt + val, 2);
	BIT_skipBits(DStream, dt[val].nbBits);
	return dt[val].length;
}

static U32 HUF_decodeLastSymbolX4(void *op, BIT_DStream_t *DStream, const HUF_DEltX4 *dt, const U32 dtLog)
{
	size_t const val = BIT_lookBitsFast(DStream, dtLog); /* note : dtLog >= 1 */
	memcpy(op, dt + val, 1);
	if (dt[val].length == 1)
		BIT_skipBits(DStream, dt[val].nbBits);
	else {
		if (DStream->bitsConsumed < (sizeof(DStream->bitContainer) * 8)) {
			BIT_skipBits(DStream, dt[val].nbBits);
			if (DStream->bitsConsumed > (sizeof(DStream->bitContainer) * 8))
				/* ugly hack; works only because it's the last symbol. Note : can't easily extract nbBits from just this symbol */
				DStream->bitsConsumed = (sizeof(DStream->bitContainer) * 8);
		}
	}
	return 1;
}

#define HUF_DECODE_SYMBOLX4_0(ptr, DStreamPtr) ptr += HUF_decodeSymbolX4(ptr, DStreamPtr, dt, dtLog)

#define HUF_DECODE_SYMBOLX4_1(ptr, DStreamPtr)         \
	if (ZSTD_64bits() || (HUF_TABLELOG_MAX <= 12)) \
	ptr += HUF_decodeSymbolX4(ptr, DStreamPtr, dt, dtLog)

#define HUF_DECODE_SYMBOLX4_2(ptr, DStreamPtr) \
	if (ZSTD_64bits())                     \
	ptr += HUF_decodeSymbolX4(ptr, DStreamPtr, dt, dtLog)

FORCE_INLINE size_t HUF_decodeStreamX4(BYTE *p, BIT_DStream_t *bitDPtr, BYTE *const pEnd, const HUF_DEltX4 *const dt, const U32 dtLog)
{
	BYTE *const pStart = p;

	/* up to 8 symbols at a time */
	while ((BIT_reloadDStream(bitDPtr) == BIT_DStream_unfinished) & (p < pEnd - (sizeof(bitDPtr->bitContainer) - 1))) {
		HUF_DECODE_SYMBOLX4_2(p, bitDPtr);
		HUF_DECODE_SYMBOLX4_1(p, bitDPtr);
		HUF_DECODE_SYMBOLX4_2(p, bitDPtr);
		HUF_DECODE_SYMBOLX4_0(p, bitDPtr);
	}

	/* closer to end : up to 2 symbols at a time */
	while ((BIT_reloadDStream(bitDPtr) == BIT_DStream_unfinished) & (p <= pEnd - 2))
		HUF_DECODE_SYMBOLX4_0(p, bitDPtr);

	while (p <= pEnd - 2)
		HUF_DECODE_SYMBOLX4_0(p, bitDPtr); /* no need to reload : reached the end of DStream */

	if (p < pEnd)
		p += HUF_decodeLastSymbolX4(p, bitDPtr, dt, dtLog);

	return p - pStart;
}

static size_t HUF_decompress1X4_usingDTable_internal(void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable)
{
	BIT_DStream_t bitD;

	/* Init */
	{
		size_t const errorCode = BIT_initDStream(&bitD, cSrc, cSrcSize);
		if (HUF_isError(errorCode))
			return errorCode;
	}

	/* decode */
	{
		BYTE *const ostart = (BYTE *)dst;
		BYTE *const oend = ostart + dstSize;
		const void *const dtPtr = DTable + 1; /* force compiler to not use strict-aliasing */
		const HUF_DEltX4 *const dt = (const HUF_DEltX4 *)dtPtr;
		DTableDesc const dtd = HUF_getDTableDesc(DTable);
		HUF_decodeStreamX4(ostart, &bitD, oend, dt, dtd.tableLog);
	}

	/* check */
	if (!BIT_endOfDStream(&bitD))
		return ERROR(corruption_detected);

	/* decoded size */
	return dstSize;
}

size_t HUF_decompress1X4_usingDTable(void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable)
{
	DTableDesc dtd = HUF_getDTableDesc(DTable);
	if (dtd.tableType != 1)
		return ERROR(GENERIC);
	return HUF_decompress1X4_usingDTable_internal(dst, dstSize, cSrc, cSrcSize, DTable);
}

size_t HUF_decompress1X4_DCtx_wksp(HUF_DTable *DCtx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace, size_t workspaceSize)
{
	const BYTE *ip = (const BYTE *)cSrc;

	size_t const hSize = HUF_readDTableX4_wksp(DCtx, cSrc, cSrcSize, workspace, workspaceSize);
	if (HUF_isError(hSize))
		return hSize;
	if (hSize >= cSrcSize)
		return ERROR(srcSize_wrong);
	ip += hSize;
	cSrcSize -= hSize;

	return HUF_decompress1X4_usingDTable_internal(dst, dstSize, ip, cSrcSize, DCtx);
}

static size_t HUF_decompress4X4_usingDTable_internal(void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable)
{
	if (cSrcSize < 10)
		return ERROR(corruption_detected); /* strict minimum : jump table + 1 byte per stream */

	{
		const BYTE *const istart = (const BYTE *)cSrc;
		BYTE *const ostart = (BYTE *)dst;
		BYTE *const oend = ostart + dstSize;
		const void *const dtPtr = DTable + 1;
		const HUF_DEltX4 *const dt = (const HUF_DEltX4 *)dtPtr;

		/* Init */
		BIT_DStream_t bitD1;
		BIT_DStream_t bitD2;
		BIT_DStream_t bitD3;
		BIT_DStream_t bitD4;
		size_t const length1 = ZSTD_readLE16(istart);
		size_t const length2 = ZSTD_readLE16(istart + 2);
		size_t const length3 = ZSTD_readLE16(istart + 4);
		size_t const length4 = cSrcSize - (length1 + length2 + length3 + 6);
		const BYTE *const istart1 = istart + 6; /* jumpTable */
		const BYTE *const istart2 = istart1 + length1;
		const BYTE *const istart3 = istart2 + length2;
		const BYTE *const istart4 = istart3 + length3;
		size_t const segmentSize = (dstSize + 3) / 4;
		BYTE *const opStart2 = ostart + segmentSize;
		BYTE *const opStart3 = opStart2 + segmentSize;
		BYTE *const opStart4 = opStart3 + segmentSize;
		BYTE *op1 = ostart;
		BYTE *op2 = opStart2;
		BYTE *op3 = opStart3;
		BYTE *op4 = opStart4;
		U32 endSignal;
		DTableDesc const dtd = HUF_getDTableDesc(DTable);
		U32 const dtLog = dtd.tableLog;

		if (length4 > cSrcSize)
			return ERROR(corruption_detected); /* overflow */
		{
			size_t const errorCode = BIT_initDStream(&bitD1, istart1, length1);
			if (HUF_isError(errorCode))
				return errorCode;
		}
		{
			size_t const errorCode = BIT_initDStream(&bitD2, istart2, length2);
			if (HUF_isError(errorCode))
				return errorCode;
		}
		{
			size_t const errorCode = BIT_initDStream(&bitD3, istart3, length3);
			if (HUF_isError(errorCode))
				return errorCode;
		}
		{
			size_t const errorCode = BIT_initDStream(&bitD4, istart4, length4);
			if (HUF_isError(errorCode))
				return errorCode;
		}

		/* 16-32 symbols per loop (4-8 symbols per stream) */
		endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
		for (; (endSignal == BIT_DStream_unfinished) & (op4 < (oend - (sizeof(bitD4.bitContainer) - 1)));) {
			HUF_DECODE_SYMBOLX4_2(op1, &bitD1);
			HUF_DECODE_SYMBOLX4_2(op2, &bitD2);
			HUF_DECODE_SYMBOLX4_2(op3, &bitD3);
			HUF_DECODE_SYMBOLX4_2(op4, &bitD4);
			HUF_DECODE_SYMBOLX4_1(op1, &bitD1);
			HUF_DECODE_SYMBOLX4_1(op2, &bitD2);
			HUF_DECODE_SYMBOLX4_1(op3, &bitD3);
			HUF_DECODE_SYMBOLX4_1(op4, &bitD4);
			HUF_DECODE_SYMBOLX4_2(op1, &bitD1);
			HUF_DECODE_SYMBOLX4_2(op2, &bitD2);
			HUF_DECODE_SYMBOLX4_2(op3, &bitD3);
			HUF_DECODE_SYMBOLX4_2(op4, &bitD4);
			HUF_DECODE_SYMBOLX4_0(op1, &bitD1);
			HUF_DECODE_SYMBOLX4_0(op2, &bitD2);
			HUF_DECODE_SYMBOLX4_0(op3, &bitD3);
			HUF_DECODE_SYMBOLX4_0(op4, &bitD4);

			endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
		}

		/* check corruption */
		if (op1 > opStart2)
			return ERROR(corruption_detected);
		if (op2 > opStart3)
			return ERROR(corruption_detected);
		if (op3 > opStart4)
			return ERROR(corruption_detected);
		/* note : op4 already verified within main loop */

		/* finish bitStreams one by one */
		HUF_decodeStreamX4(op1, &bitD1, opStart2, dt, dtLog);
		HUF_decodeStreamX4(op2, &bitD2, opStart3, dt, dtLog);
		HUF_decodeStreamX4(op3, &bitD3, opStart4, dt, dtLog);
		HUF_decodeStreamX4(op4, &bitD4, oend, dt, dtLog);

		/* check */
		{
			U32 const endCheck = BIT_endOfDStream(&bitD1) & BIT_endOfDStream(&bitD2) & BIT_endOfDStream(&bitD3) & BIT_endOfDStream(&bitD4);
			if (!endCheck)
				return ERROR(corruption_detected);
		}

		/* decoded size */
		return dstSize;
	}
}

size_t HUF_decompress4X4_usingDTable(void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable)
{
	DTableDesc dtd = HUF_getDTableDesc(DTable);
	if (dtd.tableType != 1)
		return ERROR(GENERIC);
	return HUF_decompress4X4_usingDTable_internal(dst, dstSize, cSrc, cSrcSize, DTable);
}

size_t HUF_decompress4X4_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace, size_t workspaceSize)
{
	const BYTE *ip = (const BYTE *)cSrc;

	size_t hSize = HUF_readDTableX4_wksp(dctx, cSrc, cSrcSize, workspace, workspaceSize);
	if (HUF_isError(hSize))
		return hSize;
	if (hSize >= cSrcSize)
		return ERROR(srcSize_wrong);
	ip += hSize;
	cSrcSize -= hSize;

	return HUF_decompress4X4_usingDTable_internal(dst, dstSize, ip, cSrcSize, dctx);
}

/* ********************************/
/* Generic decompression selector */
/* ********************************/

size_t HUF_decompress1X_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable)
{
	DTableDesc const dtd = HUF_getDTableDesc(DTable);
	return dtd.tableType ? HUF_decompress1X4_usingDTable_internal(dst, maxDstSize, cSrc, cSrcSize, DTable)
			     : HUF_decompress1X2_usingDTable_internal(dst, maxDstSize, cSrc, cSrcSize, DTable);
}

size_t HUF_decompress4X_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable)
{
	DTableDesc const dtd = HUF_getDTableDesc(DTable);
	return dtd.tableType ? HUF_decompress4X4_usingDTable_internal(dst, maxDstSize, cSrc, cSrcSize, DTable)
			     : HUF_decompress4X2_usingDTable_internal(dst, maxDstSize, cSrc, cSrcSize, DTable);
}

typedef struct {
	U32 tableTime;
	U32 decode256Time;
} algo_time_t;
static const algo_time_t algoTime[16 /* Quantization */][3 /* single, double, quad */] = {
    /* single, double, quad */
    {{0, 0}, {1, 1}, {2, 2}},		     /* Q==0 : impossible */
    {{0, 0}, {1, 1}, {2, 2}},		     /* Q==1 : impossible */
    {{38, 130}, {1313, 74}, {2151, 38}},     /* Q == 2 : 12-18% */
    {{448, 128}, {1353, 74}, {2238, 41}},    /* Q == 3 : 18-25% */
    {{556, 128}, {1353, 74}, {2238, 47}},    /* Q == 4 : 25-32% */
    {{714, 128}, {1418, 74}, {2436, 53}},    /* Q == 5 : 32-38% */
    {{883, 128}, {1437, 74}, {2464, 61}},    /* Q == 6 : 38-44% */
    {{897, 128}, {1515, 75}, {2622, 68}},    /* Q == 7 : 44-50% */
    {{926, 128}, {1613, 75}, {2730, 75}},    /* Q == 8 : 50-56% */
    {{947, 128}, {1729, 77}, {3359, 77}},    /* Q == 9 : 56-62% */
    {{1107, 128}, {2083, 81}, {4006, 84}},   /* Q ==10 : 62-69% */
    {{1177, 128}, {2379, 87}, {4785, 88}},   /* Q ==11 : 69-75% */
    {{1242, 128}, {2415, 93}, {5155, 84}},   /* Q ==12 : 75-81% */
    {{1349, 128}, {2644, 106}, {5260, 106}}, /* Q ==13 : 81-87% */
    {{1455, 128}, {2422, 124}, {4174, 124}}, /* Q ==14 : 87-93% */
    {{722, 128}, {1891, 145}, {1936, 146}},  /* Q ==15 : 93-99% */
};

/** HUF_selectDecoder() :
*   Tells which decoder is likely to decode faster,
*   based on a set of pre-determined metrics.
*   @return : 0==HUF_decompress4X2, 1==HUF_decompress4X4 .
*   Assumption : 0 < cSrcSize < dstSize <= 128 KB */
U32 HUF_selectDecoder(size_t dstSize, size_t cSrcSize)
{
	/* decoder timing evaluation */
	U32 const Q = (U32)(cSrcSize * 16 / dstSize); /* Q < 16 since dstSize > cSrcSize */
	U32 const D256 = (U32)(dstSize >> 8);
	U32 const DTime0 = algoTime[Q][0].tableTime + (algoTime[Q][0].decode256Time * D256);
	U32 DTime1 = algoTime[Q][1].tableTime + (algoTime[Q][1].decode256Time * D256);
	DTime1 += DTime1 >> 3; /* advantage to algorithm using less memory, for cache eviction */

	return DTime1 < DTime0;
}

typedef size_t (*decompressionAlgo)(void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize);

size_t HUF_decompress4X_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace, size_t workspaceSize)
{
	/* validation checks */
	if (dstSize == 0)
		return ERROR(dstSize_tooSmall);
	if (cSrcSize > dstSize)
		return ERROR(corruption_detected); /* invalid */
	if (cSrcSize == dstSize) {
		memcpy(dst, cSrc, dstSize);
		return dstSize;
	} /* not compressed */
	if (cSrcSize == 1) {
		memset(dst, *(const BYTE *)cSrc, dstSize);
		return dstSize;
	} /* RLE */

	{
		U32 const algoNb = HUF_selectDecoder(dstSize, cSrcSize);
		return algoNb ? HUF_decompress4X4_DCtx_wksp(dctx, dst, dstSize, cSrc, cSrcSize, workspace, workspaceSize)
			      : HUF_decompress4X2_DCtx_wksp(dctx, dst, dstSize, cSrc, cSrcSize, workspace, workspaceSize);
	}
}

size_t HUF_decompress4X_hufOnly_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace, size_t workspaceSize)
{
	/* validation checks */
	if (dstSize == 0)
		return ERROR(dstSize_tooSmall);
	if ((cSrcSize >= dstSize) || (cSrcSize <= 1))
		return ERROR(corruption_detected); /* invalid */

	{
		U32 const algoNb = HUF_selectDecoder(dstSize, cSrcSize);
		return algoNb ? HUF_decompress4X4_DCtx_wksp(dctx, dst, dstSize, cSrc, cSrcSize, workspace, workspaceSize)
			      : HUF_decompress4X2_DCtx_wksp(dctx, dst, dstSize, cSrc, cSrcSize, workspace, workspaceSize);
	}
}

size_t HUF_decompress1X_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace, size_t workspaceSize)
{
	/* validation checks */
	if (dstSize == 0)
		return ERROR(dstSize_tooSmall);
	if (cSrcSize > dstSize)
		return ERROR(corruption_detected); /* invalid */
	if (cSrcSize == dstSize) {
		memcpy(dst, cSrc, dstSize);
		return dstSize;
	} /* not compressed */
	if (cSrcSize == 1) {
		memset(dst, *(const BYTE *)cSrc, dstSize);
		return dstSize;
	} /* RLE */

	{
		U32 const algoNb = HUF_selectDecoder(dstSize, cSrcSize);
		return algoNb ? HUF_decompress1X4_DCtx_wksp(dctx, dst, dstSize, cSrc, cSrcSize, workspace, workspaceSize)
			      : HUF_decompress1X2_DCtx_wksp(dctx, dst, dstSize, cSrc, cSrcSize, workspace, workspaceSize);
	}
}
