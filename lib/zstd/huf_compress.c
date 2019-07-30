/*
 * Huffman encoder, part of New Generation Entropy library
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
*  Includes
****************************************************************/
#include "bitstream.h"
#include "fse.h" /* header compression */
#include "huf.h"
#include <linux/kernel.h>
#include <linux/string.h> /* memcpy, memset */

/* **************************************************************
*  Error Management
****************************************************************/
#define HUF_STATIC_ASSERT(c)                                   \
	{                                                      \
		enum { HUF_static_assert = 1 / (int)(!!(c)) }; \
	} /* use only *after* variable declarations */
#define CHECK_V_F(e, f)     \
	size_t const e = f; \
	if (ERR_isError(e)) \
	return f
#define CHECK_F(f)                        \
	{                                 \
		CHECK_V_F(_var_err__, f); \
	}

/* **************************************************************
*  Utils
****************************************************************/
unsigned HUF_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue)
{
	return FSE_optimalTableLog_internal(maxTableLog, srcSize, maxSymbolValue, 1);
}

/* *******************************************************
*  HUF : Huffman block compression
*********************************************************/
/* HUF_compressWeights() :
 * Same as FSE_compress(), but dedicated to huff0's weights compression.
 * The use case needs much less stack memory.
 * Note : all elements within weightTable are supposed to be <= HUF_TABLELOG_MAX.
 */
#define MAX_FSE_TABLELOG_FOR_HUFF_HEADER 6
size_t HUF_compressWeights_wksp(void *dst, size_t dstSize, const void *weightTable, size_t wtSize, void *workspace, size_t workspaceSize)
{
	BYTE *const ostart = (BYTE *)dst;
	BYTE *op = ostart;
	BYTE *const oend = ostart + dstSize;

	U32 maxSymbolValue = HUF_TABLELOG_MAX;
	U32 tableLog = MAX_FSE_TABLELOG_FOR_HUFF_HEADER;

	FSE_CTable *CTable;
	U32 *count;
	S16 *norm;
	size_t spaceUsed32 = 0;

	HUF_STATIC_ASSERT(sizeof(FSE_CTable) == sizeof(U32));

	CTable = (FSE_CTable *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += FSE_CTABLE_SIZE_U32(MAX_FSE_TABLELOG_FOR_HUFF_HEADER, HUF_TABLELOG_MAX);
	count = (U32 *)workspace + spaceUsed32;
	spaceUsed32 += HUF_TABLELOG_MAX + 1;
	norm = (S16 *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += ALIGN(sizeof(S16) * (HUF_TABLELOG_MAX + 1), sizeof(U32)) >> 2;

	if ((spaceUsed32 << 2) > workspaceSize)
		return ERROR(tableLog_tooLarge);
	workspace = (U32 *)workspace + spaceUsed32;
	workspaceSize -= (spaceUsed32 << 2);

	/* init conditions */
	if (wtSize <= 1)
		return 0; /* Not compressible */

	/* Scan input and build symbol stats */
	{
		CHECK_V_F(maxCount, FSE_count_simple(count, &maxSymbolValue, weightTable, wtSize));
		if (maxCount == wtSize)
			return 1; /* only a single symbol in src : rle */
		if (maxCount == 1)
			return 0; /* each symbol present maximum once => not compressible */
	}

	tableLog = FSE_optimalTableLog(tableLog, wtSize, maxSymbolValue);
	CHECK_F(FSE_normalizeCount(norm, tableLog, count, wtSize, maxSymbolValue));

	/* Write table description header */
	{
		CHECK_V_F(hSize, FSE_writeNCount(op, oend - op, norm, maxSymbolValue, tableLog));
		op += hSize;
	}

	/* Compress */
	CHECK_F(FSE_buildCTable_wksp(CTable, norm, maxSymbolValue, tableLog, workspace, workspaceSize));
	{
		CHECK_V_F(cSize, FSE_compress_usingCTable(op, oend - op, weightTable, wtSize, CTable));
		if (cSize == 0)
			return 0; /* not enough space for compressed data */
		op += cSize;
	}

	return op - ostart;
}

struct HUF_CElt_s {
	U16 val;
	BYTE nbBits;
}; /* typedef'd to HUF_CElt within "huf.h" */

/*! HUF_writeCTable_wksp() :
	`CTable` : Huffman tree to save, using huf representation.
	@return : size of saved CTable */
size_t HUF_writeCTable_wksp(void *dst, size_t maxDstSize, const HUF_CElt *CTable, U32 maxSymbolValue, U32 huffLog, void *workspace, size_t workspaceSize)
{
	BYTE *op = (BYTE *)dst;
	U32 n;

	BYTE *bitsToWeight;
	BYTE *huffWeight;
	size_t spaceUsed32 = 0;

	bitsToWeight = (BYTE *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += ALIGN(HUF_TABLELOG_MAX + 1, sizeof(U32)) >> 2;
	huffWeight = (BYTE *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += ALIGN(HUF_SYMBOLVALUE_MAX, sizeof(U32)) >> 2;

	if ((spaceUsed32 << 2) > workspaceSize)
		return ERROR(tableLog_tooLarge);
	workspace = (U32 *)workspace + spaceUsed32;
	workspaceSize -= (spaceUsed32 << 2);

	/* check conditions */
	if (maxSymbolValue > HUF_SYMBOLVALUE_MAX)
		return ERROR(maxSymbolValue_tooLarge);

	/* convert to weight */
	bitsToWeight[0] = 0;
	for (n = 1; n < huffLog + 1; n++)
		bitsToWeight[n] = (BYTE)(huffLog + 1 - n);
	for (n = 0; n < maxSymbolValue; n++)
		huffWeight[n] = bitsToWeight[CTable[n].nbBits];

	/* attempt weights compression by FSE */
	{
		CHECK_V_F(hSize, HUF_compressWeights_wksp(op + 1, maxDstSize - 1, huffWeight, maxSymbolValue, workspace, workspaceSize));
		if ((hSize > 1) & (hSize < maxSymbolValue / 2)) { /* FSE compressed */
			op[0] = (BYTE)hSize;
			return hSize + 1;
		}
	}

	/* write raw values as 4-bits (max : 15) */
	if (maxSymbolValue > (256 - 128))
		return ERROR(GENERIC); /* should not happen : likely means source cannot be compressed */
	if (((maxSymbolValue + 1) / 2) + 1 > maxDstSize)
		return ERROR(dstSize_tooSmall); /* not enough space within dst buffer */
	op[0] = (BYTE)(128 /*special case*/ + (maxSymbolValue - 1));
	huffWeight[maxSymbolValue] = 0; /* to be sure it doesn't cause msan issue in final combination */
	for (n = 0; n < maxSymbolValue; n += 2)
		op[(n / 2) + 1] = (BYTE)((huffWeight[n] << 4) + huffWeight[n + 1]);
	return ((maxSymbolValue + 1) / 2) + 1;
}

size_t HUF_readCTable_wksp(HUF_CElt *CTable, U32 maxSymbolValue, const void *src, size_t srcSize, void *workspace, size_t workspaceSize)
{
	U32 *rankVal;
	BYTE *huffWeight;
	U32 tableLog = 0;
	U32 nbSymbols = 0;
	size_t readSize;
	size_t spaceUsed32 = 0;

	rankVal = (U32 *)workspace + spaceUsed32;
	spaceUsed32 += HUF_TABLELOG_ABSOLUTEMAX + 1;
	huffWeight = (BYTE *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += ALIGN(HUF_SYMBOLVALUE_MAX + 1, sizeof(U32)) >> 2;

	if ((spaceUsed32 << 2) > workspaceSize)
		return ERROR(tableLog_tooLarge);
	workspace = (U32 *)workspace + spaceUsed32;
	workspaceSize -= (spaceUsed32 << 2);

	/* get symbol weights */
	readSize = HUF_readStats_wksp(huffWeight, HUF_SYMBOLVALUE_MAX + 1, rankVal, &nbSymbols, &tableLog, src, srcSize, workspace, workspaceSize);
	if (ERR_isError(readSize))
		return readSize;

	/* check result */
	if (tableLog > HUF_TABLELOG_MAX)
		return ERROR(tableLog_tooLarge);
	if (nbSymbols > maxSymbolValue + 1)
		return ERROR(maxSymbolValue_tooSmall);

	/* Prepare base value per rank */
	{
		U32 n, nextRankStart = 0;
		for (n = 1; n <= tableLog; n++) {
			U32 curr = nextRankStart;
			nextRankStart += (rankVal[n] << (n - 1));
			rankVal[n] = curr;
		}
	}

	/* fill nbBits */
	{
		U32 n;
		for (n = 0; n < nbSymbols; n++) {
			const U32 w = huffWeight[n];
			CTable[n].nbBits = (BYTE)(tableLog + 1 - w);
		}
	}

	/* fill val */
	{
		U16 nbPerRank[HUF_TABLELOG_MAX + 2] = {0}; /* support w=0=>n=tableLog+1 */
		U16 valPerRank[HUF_TABLELOG_MAX + 2] = {0};
		{
			U32 n;
			for (n = 0; n < nbSymbols; n++)
				nbPerRank[CTable[n].nbBits]++;
		}
		/* determine stating value per rank */
		valPerRank[tableLog + 1] = 0; /* for w==0 */
		{
			U16 min = 0;
			U32 n;
			for (n = tableLog; n > 0; n--) { /* start at n=tablelog <-> w=1 */
				valPerRank[n] = min;     /* get starting value within each rank */
				min += nbPerRank[n];
				min >>= 1;
			}
		}
		/* assign value within rank, symbol order */
		{
			U32 n;
			for (n = 0; n <= maxSymbolValue; n++)
				CTable[n].val = valPerRank[CTable[n].nbBits]++;
		}
	}

	return readSize;
}

typedef struct nodeElt_s {
	U32 count;
	U16 parent;
	BYTE byte;
	BYTE nbBits;
} nodeElt;

static U32 HUF_setMaxHeight(nodeElt *huffNode, U32 lastNonNull, U32 maxNbBits)
{
	const U32 largestBits = huffNode[lastNonNull].nbBits;
	if (largestBits <= maxNbBits)
		return largestBits; /* early exit : no elt > maxNbBits */

	/* there are several too large elements (at least >= 2) */
	{
		int totalCost = 0;
		const U32 baseCost = 1 << (largestBits - maxNbBits);
		U32 n = lastNonNull;

		while (huffNode[n].nbBits > maxNbBits) {
			totalCost += baseCost - (1 << (largestBits - huffNode[n].nbBits));
			huffNode[n].nbBits = (BYTE)maxNbBits;
			n--;
		} /* n stops at huffNode[n].nbBits <= maxNbBits */
		while (huffNode[n].nbBits == maxNbBits)
			n--; /* n end at index of smallest symbol using < maxNbBits */

		/* renorm totalCost */
		totalCost >>= (largestBits - maxNbBits); /* note : totalCost is necessarily a multiple of baseCost */

		/* repay normalized cost */
		{
			U32 const noSymbol = 0xF0F0F0F0;
			U32 rankLast[HUF_TABLELOG_MAX + 2];
			int pos;

			/* Get pos of last (smallest) symbol per rank */
			memset(rankLast, 0xF0, sizeof(rankLast));
			{
				U32 currNbBits = maxNbBits;
				for (pos = n; pos >= 0; pos--) {
					if (huffNode[pos].nbBits >= currNbBits)
						continue;
					currNbBits = huffNode[pos].nbBits; /* < maxNbBits */
					rankLast[maxNbBits - currNbBits] = pos;
				}
			}

			while (totalCost > 0) {
				U32 nBitsToDecrease = BIT_highbit32(totalCost) + 1;
				for (; nBitsToDecrease > 1; nBitsToDecrease--) {
					U32 highPos = rankLast[nBitsToDecrease];
					U32 lowPos = rankLast[nBitsToDecrease - 1];
					if (highPos == noSymbol)
						continue;
					if (lowPos == noSymbol)
						break;
					{
						U32 const highTotal = huffNode[highPos].count;
						U32 const lowTotal = 2 * huffNode[lowPos].count;
						if (highTotal <= lowTotal)
							break;
					}
				}
				/* only triggered when no more rank 1 symbol left => find closest one (note : there is necessarily at least one !) */
				/* HUF_MAX_TABLELOG test just to please gcc 5+; but it should not be necessary */
				while ((nBitsToDecrease <= HUF_TABLELOG_MAX) && (rankLast[nBitsToDecrease] == noSymbol))
					nBitsToDecrease++;
				totalCost -= 1 << (nBitsToDecrease - 1);
				if (rankLast[nBitsToDecrease - 1] == noSymbol)
					rankLast[nBitsToDecrease - 1] = rankLast[nBitsToDecrease]; /* this rank is no longer empty */
				huffNode[rankLast[nBitsToDecrease]].nbBits++;
				if (rankLast[nBitsToDecrease] == 0) /* special case, reached largest symbol */
					rankLast[nBitsToDecrease] = noSymbol;
				else {
					rankLast[nBitsToDecrease]--;
					if (huffNode[rankLast[nBitsToDecrease]].nbBits != maxNbBits - nBitsToDecrease)
						rankLast[nBitsToDecrease] = noSymbol; /* this rank is now empty */
				}
			} /* while (totalCost > 0) */

			while (totalCost < 0) {		       /* Sometimes, cost correction overshoot */
				if (rankLast[1] == noSymbol) { /* special case : no rank 1 symbol (using maxNbBits-1); let's create one from largest rank 0
								  (using maxNbBits) */
					while (huffNode[n].nbBits == maxNbBits)
						n--;
					huffNode[n + 1].nbBits--;
					rankLast[1] = n + 1;
					totalCost++;
					continue;
				}
				huffNode[rankLast[1] + 1].nbBits--;
				rankLast[1]++;
				totalCost++;
			}
		}
	} /* there are several too large elements (at least >= 2) */

	return maxNbBits;
}

typedef struct {
	U32 base;
	U32 curr;
} rankPos;

static void HUF_sort(nodeElt *huffNode, const U32 *count, U32 maxSymbolValue)
{
	rankPos rank[32];
	U32 n;

	memset(rank, 0, sizeof(rank));
	for (n = 0; n <= maxSymbolValue; n++) {
		U32 r = BIT_highbit32(count[n] + 1);
		rank[r].base++;
	}
	for (n = 30; n > 0; n--)
		rank[n - 1].base += rank[n].base;
	for (n = 0; n < 32; n++)
		rank[n].curr = rank[n].base;
	for (n = 0; n <= maxSymbolValue; n++) {
		U32 const c = count[n];
		U32 const r = BIT_highbit32(c + 1) + 1;
		U32 pos = rank[r].curr++;
		while ((pos > rank[r].base) && (c > huffNode[pos - 1].count))
			huffNode[pos] = huffNode[pos - 1], pos--;
		huffNode[pos].count = c;
		huffNode[pos].byte = (BYTE)n;
	}
}

/** HUF_buildCTable_wksp() :
 *  Same as HUF_buildCTable(), but using externally allocated scratch buffer.
 *  `workSpace` must be aligned on 4-bytes boundaries, and be at least as large as a table of 1024 unsigned.
 */
#define STARTNODE (HUF_SYMBOLVALUE_MAX + 1)
typedef nodeElt huffNodeTable[2 * HUF_SYMBOLVALUE_MAX + 1 + 1];
size_t HUF_buildCTable_wksp(HUF_CElt *tree, const U32 *count, U32 maxSymbolValue, U32 maxNbBits, void *workSpace, size_t wkspSize)
{
	nodeElt *const huffNode0 = (nodeElt *)workSpace;
	nodeElt *const huffNode = huffNode0 + 1;
	U32 n, nonNullRank;
	int lowS, lowN;
	U16 nodeNb = STARTNODE;
	U32 nodeRoot;

	/* safety checks */
	if (wkspSize < sizeof(huffNodeTable))
		return ERROR(GENERIC); /* workSpace is not large enough */
	if (maxNbBits == 0)
		maxNbBits = HUF_TABLELOG_DEFAULT;
	if (maxSymbolValue > HUF_SYMBOLVALUE_MAX)
		return ERROR(GENERIC);
	memset(huffNode0, 0, sizeof(huffNodeTable));

	/* sort, decreasing order */
	HUF_sort(huffNode, count, maxSymbolValue);

	/* init for parents */
	nonNullRank = maxSymbolValue;
	while (huffNode[nonNullRank].count == 0)
		nonNullRank--;
	lowS = nonNullRank;
	nodeRoot = nodeNb + lowS - 1;
	lowN = nodeNb;
	huffNode[nodeNb].count = huffNode[lowS].count + huffNode[lowS - 1].count;
	huffNode[lowS].parent = huffNode[lowS - 1].parent = nodeNb;
	nodeNb++;
	lowS -= 2;
	for (n = nodeNb; n <= nodeRoot; n++)
		huffNode[n].count = (U32)(1U << 30);
	huffNode0[0].count = (U32)(1U << 31); /* fake entry, strong barrier */

	/* create parents */
	while (nodeNb <= nodeRoot) {
		U32 n1 = (huffNode[lowS].count < huffNode[lowN].count) ? lowS-- : lowN++;
		U32 n2 = (huffNode[lowS].count < huffNode[lowN].count) ? lowS-- : lowN++;
		huffNode[nodeNb].count = huffNode[n1].count + huffNode[n2].count;
		huffNode[n1].parent = huffNode[n2].parent = nodeNb;
		nodeNb++;
	}

	/* distribute weights (unlimited tree height) */
	huffNode[nodeRoot].nbBits = 0;
	for (n = nodeRoot - 1; n >= STARTNODE; n--)
		huffNode[n].nbBits = huffNode[huffNode[n].parent].nbBits + 1;
	for (n = 0; n <= nonNullRank; n++)
		huffNode[n].nbBits = huffNode[huffNode[n].parent].nbBits + 1;

	/* enforce maxTableLog */
	maxNbBits = HUF_setMaxHeight(huffNode, nonNullRank, maxNbBits);

	/* fill result into tree (val, nbBits) */
	{
		U16 nbPerRank[HUF_TABLELOG_MAX + 1] = {0};
		U16 valPerRank[HUF_TABLELOG_MAX + 1] = {0};
		if (maxNbBits > HUF_TABLELOG_MAX)
			return ERROR(GENERIC); /* check fit into table */
		for (n = 0; n <= nonNullRank; n++)
			nbPerRank[huffNode[n].nbBits]++;
		/* determine stating value per rank */
		{
			U16 min = 0;
			for (n = maxNbBits; n > 0; n--) {
				valPerRank[n] = min; /* get starting value within each rank */
				min += nbPerRank[n];
				min >>= 1;
			}
		}
		for (n = 0; n <= maxSymbolValue; n++)
			tree[huffNode[n].byte].nbBits = huffNode[n].nbBits; /* push nbBits per symbol, symbol order */
		for (n = 0; n <= maxSymbolValue; n++)
			tree[n].val = valPerRank[tree[n].nbBits]++; /* assign value within rank, symbol order */
	}

	return maxNbBits;
}

static size_t HUF_estimateCompressedSize(HUF_CElt *CTable, const unsigned *count, unsigned maxSymbolValue)
{
	size_t nbBits = 0;
	int s;
	for (s = 0; s <= (int)maxSymbolValue; ++s) {
		nbBits += CTable[s].nbBits * count[s];
	}
	return nbBits >> 3;
}

static int HUF_validateCTable(const HUF_CElt *CTable, const unsigned *count, unsigned maxSymbolValue)
{
	int bad = 0;
	int s;
	for (s = 0; s <= (int)maxSymbolValue; ++s) {
		bad |= (count[s] != 0) & (CTable[s].nbBits == 0);
	}
	return !bad;
}

static void HUF_encodeSymbol(BIT_CStream_t *bitCPtr, U32 symbol, const HUF_CElt *CTable)
{
	BIT_addBitsFast(bitCPtr, CTable[symbol].val, CTable[symbol].nbBits);
}

size_t HUF_compressBound(size_t size) { return HUF_COMPRESSBOUND(size); }

#define HUF_FLUSHBITS(s)  BIT_flushBits(s)

#define HUF_FLUSHBITS_1(stream)                                            \
	if (sizeof((stream)->bitContainer) * 8 < HUF_TABLELOG_MAX * 2 + 7) \
	HUF_FLUSHBITS(stream)

#define HUF_FLUSHBITS_2(stream)                                            \
	if (sizeof((stream)->bitContainer) * 8 < HUF_TABLELOG_MAX * 4 + 7) \
	HUF_FLUSHBITS(stream)

size_t HUF_compress1X_usingCTable(void *dst, size_t dstSize, const void *src, size_t srcSize, const HUF_CElt *CTable)
{
	const BYTE *ip = (const BYTE *)src;
	BYTE *const ostart = (BYTE *)dst;
	BYTE *const oend = ostart + dstSize;
	BYTE *op = ostart;
	size_t n;
	BIT_CStream_t bitC;

	/* init */
	if (dstSize < 8)
		return 0; /* not enough space to compress */
	{
		size_t const initErr = BIT_initCStream(&bitC, op, oend - op);
		if (HUF_isError(initErr))
			return 0;
	}

	n = srcSize & ~3; /* join to mod 4 */
	switch (srcSize & 3) {
	case 3: HUF_encodeSymbol(&bitC, ip[n + 2], CTable); HUF_FLUSHBITS_2(&bitC);
		/* fall through */
	case 2: HUF_encodeSymbol(&bitC, ip[n + 1], CTable); HUF_FLUSHBITS_1(&bitC);
		/* fall through */
	case 1: HUF_encodeSymbol(&bitC, ip[n + 0], CTable); HUF_FLUSHBITS(&bitC);
	case 0:
	default:;
	}

	for (; n > 0; n -= 4) { /* note : n&3==0 at this stage */
		HUF_encodeSymbol(&bitC, ip[n - 1], CTable);
		HUF_FLUSHBITS_1(&bitC);
		HUF_encodeSymbol(&bitC, ip[n - 2], CTable);
		HUF_FLUSHBITS_2(&bitC);
		HUF_encodeSymbol(&bitC, ip[n - 3], CTable);
		HUF_FLUSHBITS_1(&bitC);
		HUF_encodeSymbol(&bitC, ip[n - 4], CTable);
		HUF_FLUSHBITS(&bitC);
	}

	return BIT_closeCStream(&bitC);
}

size_t HUF_compress4X_usingCTable(void *dst, size_t dstSize, const void *src, size_t srcSize, const HUF_CElt *CTable)
{
	size_t const segmentSize = (srcSize + 3) / 4; /* first 3 segments */
	const BYTE *ip = (const BYTE *)src;
	const BYTE *const iend = ip + srcSize;
	BYTE *const ostart = (BYTE *)dst;
	BYTE *const oend = ostart + dstSize;
	BYTE *op = ostart;

	if (dstSize < 6 + 1 + 1 + 1 + 8)
		return 0; /* minimum space to compress successfully */
	if (srcSize < 12)
		return 0; /* no saving possible : too small input */
	op += 6;	  /* jumpTable */

	{
		CHECK_V_F(cSize, HUF_compress1X_usingCTable(op, oend - op, ip, segmentSize, CTable));
		if (cSize == 0)
			return 0;
		ZSTD_writeLE16(ostart, (U16)cSize);
		op += cSize;
	}

	ip += segmentSize;
	{
		CHECK_V_F(cSize, HUF_compress1X_usingCTable(op, oend - op, ip, segmentSize, CTable));
		if (cSize == 0)
			return 0;
		ZSTD_writeLE16(ostart + 2, (U16)cSize);
		op += cSize;
	}

	ip += segmentSize;
	{
		CHECK_V_F(cSize, HUF_compress1X_usingCTable(op, oend - op, ip, segmentSize, CTable));
		if (cSize == 0)
			return 0;
		ZSTD_writeLE16(ostart + 4, (U16)cSize);
		op += cSize;
	}

	ip += segmentSize;
	{
		CHECK_V_F(cSize, HUF_compress1X_usingCTable(op, oend - op, ip, iend - ip, CTable));
		if (cSize == 0)
			return 0;
		op += cSize;
	}

	return op - ostart;
}

static size_t HUF_compressCTable_internal(BYTE *const ostart, BYTE *op, BYTE *const oend, const void *src, size_t srcSize, unsigned singleStream,
					  const HUF_CElt *CTable)
{
	size_t const cSize =
	    singleStream ? HUF_compress1X_usingCTable(op, oend - op, src, srcSize, CTable) : HUF_compress4X_usingCTable(op, oend - op, src, srcSize, CTable);
	if (HUF_isError(cSize)) {
		return cSize;
	}
	if (cSize == 0) {
		return 0;
	} /* uncompressible */
	op += cSize;
	/* check compressibility */
	if ((size_t)(op - ostart) >= srcSize - 1) {
		return 0;
	}
	return op - ostart;
}

/* `workSpace` must a table of at least 1024 unsigned */
static size_t HUF_compress_internal(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned huffLog,
				    unsigned singleStream, void *workSpace, size_t wkspSize, HUF_CElt *oldHufTable, HUF_repeat *repeat, int preferRepeat)
{
	BYTE *const ostart = (BYTE *)dst;
	BYTE *const oend = ostart + dstSize;
	BYTE *op = ostart;

	U32 *count;
	size_t const countSize = sizeof(U32) * (HUF_SYMBOLVALUE_MAX + 1);
	HUF_CElt *CTable;
	size_t const CTableSize = sizeof(HUF_CElt) * (HUF_SYMBOLVALUE_MAX + 1);

	/* checks & inits */
	if (wkspSize < sizeof(huffNodeTable) + countSize + CTableSize)
		return ERROR(GENERIC);
	if (!srcSize)
		return 0; /* Uncompressed (note : 1 means rle, so first byte must be correct) */
	if (!dstSize)
		return 0; /* cannot fit within dst budget */
	if (srcSize > HUF_BLOCKSIZE_MAX)
		return ERROR(srcSize_wrong); /* curr block size limit */
	if (huffLog > HUF_TABLELOG_MAX)
		return ERROR(tableLog_tooLarge);
	if (!maxSymbolValue)
		maxSymbolValue = HUF_SYMBOLVALUE_MAX;
	if (!huffLog)
		huffLog = HUF_TABLELOG_DEFAULT;

	count = (U32 *)workSpace;
	workSpace = (BYTE *)workSpace + countSize;
	wkspSize -= countSize;
	CTable = (HUF_CElt *)workSpace;
	workSpace = (BYTE *)workSpace + CTableSize;
	wkspSize -= CTableSize;

	/* Heuristic : If we don't need to check the validity of the old table use the old table for small inputs */
	if (preferRepeat && repeat && *repeat == HUF_repeat_valid) {
		return HUF_compressCTable_internal(ostart, op, oend, src, srcSize, singleStream, oldHufTable);
	}

	/* Scan input and build symbol stats */
	{
		CHECK_V_F(largest, FSE_count_wksp(count, &maxSymbolValue, (const BYTE *)src, srcSize, (U32 *)workSpace));
		if (largest == srcSize) {
			*ostart = ((const BYTE *)src)[0];
			return 1;
		} /* single symbol, rle */
		if (largest <= (srcSize >> 7) + 1)
			return 0; /* Fast heuristic : not compressible enough */
	}

	/* Check validity of previous table */
	if (repeat && *repeat == HUF_repeat_check && !HUF_validateCTable(oldHufTable, count, maxSymbolValue)) {
		*repeat = HUF_repeat_none;
	}
	/* Heuristic : use existing table for small inputs */
	if (preferRepeat && repeat && *repeat != HUF_repeat_none) {
		return HUF_compressCTable_internal(ostart, op, oend, src, srcSize, singleStream, oldHufTable);
	}

	/* Build Huffman Tree */
	huffLog = HUF_optimalTableLog(huffLog, srcSize, maxSymbolValue);
	{
		CHECK_V_F(maxBits, HUF_buildCTable_wksp(CTable, count, maxSymbolValue, huffLog, workSpace, wkspSize));
		huffLog = (U32)maxBits;
		/* Zero the unused symbols so we can check it for validity */
		memset(CTable + maxSymbolValue + 1, 0, CTableSize - (maxSymbolValue + 1) * sizeof(HUF_CElt));
	}

	/* Write table description header */
	{
		CHECK_V_F(hSize, HUF_writeCTable_wksp(op, dstSize, CTable, maxSymbolValue, huffLog, workSpace, wkspSize));
		/* Check if using the previous table will be beneficial */
		if (repeat && *repeat != HUF_repeat_none) {
			size_t const oldSize = HUF_estimateCompressedSize(oldHufTable, count, maxSymbolValue);
			size_t const newSize = HUF_estimateCompressedSize(CTable, count, maxSymbolValue);
			if (oldSize <= hSize + newSize || hSize + 12 >= srcSize) {
				return HUF_compressCTable_internal(ostart, op, oend, src, srcSize, singleStream, oldHufTable);
			}
		}
		/* Use the new table */
		if (hSize + 12ul >= srcSize) {
			return 0;
		}
		op += hSize;
		if (repeat) {
			*repeat = HUF_repeat_none;
		}
		if (oldHufTable) {
			memcpy(oldHufTable, CTable, CTableSize);
		} /* Save the new table */
	}
	return HUF_compressCTable_internal(ostart, op, oend, src, srcSize, singleStream, CTable);
}

size_t HUF_compress1X_wksp(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned huffLog, void *workSpace,
			   size_t wkspSize)
{
	return HUF_compress_internal(dst, dstSize, src, srcSize, maxSymbolValue, huffLog, 1 /* single stream */, workSpace, wkspSize, NULL, NULL, 0);
}

size_t HUF_compress1X_repeat(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned huffLog, void *workSpace,
			     size_t wkspSize, HUF_CElt *hufTable, HUF_repeat *repeat, int preferRepeat)
{
	return HUF_compress_internal(dst, dstSize, src, srcSize, maxSymbolValue, huffLog, 1 /* single stream */, workSpace, wkspSize, hufTable, repeat,
				     preferRepeat);
}

size_t HUF_compress4X_wksp(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned huffLog, void *workSpace,
			   size_t wkspSize)
{
	return HUF_compress_internal(dst, dstSize, src, srcSize, maxSymbolValue, huffLog, 0 /* 4 streams */, workSpace, wkspSize, NULL, NULL, 0);
}

size_t HUF_compress4X_repeat(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned huffLog, void *workSpace,
			     size_t wkspSize, HUF_CElt *hufTable, HUF_repeat *repeat, int preferRepeat)
{
	return HUF_compress_internal(dst, dstSize, src, srcSize, maxSymbolValue, huffLog, 0 /* 4 streams */, workSpace, wkspSize, hufTable, repeat,
				     preferRepeat);
}
