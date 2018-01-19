/*
 * Common functions of New Generation Entropy library
 * Copyright (C) 2016, Yann Collet.
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

/* *************************************
*  Dependencies
***************************************/
#include "error_private.h" /* ERR_*, ERROR */
#include "fse.h"
#include "huf.h"
#include "mem.h"

/*===   Version   ===*/
unsigned FSE_versionNumber(void) { return FSE_VERSION_NUMBER; }

/*===   Error Management   ===*/
unsigned FSE_isError(size_t code) { return ERR_isError(code); }

unsigned HUF_isError(size_t code) { return ERR_isError(code); }

/*-**************************************************************
*  FSE NCount encoding-decoding
****************************************************************/
size_t FSE_readNCount(short *normalizedCounter, unsigned *maxSVPtr, unsigned *tableLogPtr, const void *headerBuffer, size_t hbSize)
{
	const BYTE *const istart = (const BYTE *)headerBuffer;
	const BYTE *const iend = istart + hbSize;
	const BYTE *ip = istart;
	int nbBits;
	int remaining;
	int threshold;
	U32 bitStream;
	int bitCount;
	unsigned charnum = 0;
	int previous0 = 0;

	if (hbSize < 4)
		return ERROR(srcSize_wrong);
	bitStream = ZSTD_readLE32(ip);
	nbBits = (bitStream & 0xF) + FSE_MIN_TABLELOG; /* extract tableLog */
	if (nbBits > FSE_TABLELOG_ABSOLUTE_MAX)
		return ERROR(tableLog_tooLarge);
	bitStream >>= 4;
	bitCount = 4;
	*tableLogPtr = nbBits;
	remaining = (1 << nbBits) + 1;
	threshold = 1 << nbBits;
	nbBits++;

	while ((remaining > 1) & (charnum <= *maxSVPtr)) {
		if (previous0) {
			unsigned n0 = charnum;
			while ((bitStream & 0xFFFF) == 0xFFFF) {
				n0 += 24;
				if (ip < iend - 5) {
					ip += 2;
					bitStream = ZSTD_readLE32(ip) >> bitCount;
				} else {
					bitStream >>= 16;
					bitCount += 16;
				}
			}
			while ((bitStream & 3) == 3) {
				n0 += 3;
				bitStream >>= 2;
				bitCount += 2;
			}
			n0 += bitStream & 3;
			bitCount += 2;
			if (n0 > *maxSVPtr)
				return ERROR(maxSymbolValue_tooSmall);
			while (charnum < n0)
				normalizedCounter[charnum++] = 0;
			if ((ip <= iend - 7) || (ip + (bitCount >> 3) <= iend - 4)) {
				ip += bitCount >> 3;
				bitCount &= 7;
				bitStream = ZSTD_readLE32(ip) >> bitCount;
			} else {
				bitStream >>= 2;
			}
		}
		{
			int const max = (2 * threshold - 1) - remaining;
			int count;

			if ((bitStream & (threshold - 1)) < (U32)max) {
				count = bitStream & (threshold - 1);
				bitCount += nbBits - 1;
			} else {
				count = bitStream & (2 * threshold - 1);
				if (count >= threshold)
					count -= max;
				bitCount += nbBits;
			}

			count--;				 /* extra accuracy */
			remaining -= count < 0 ? -count : count; /* -1 means +1 */
			normalizedCounter[charnum++] = (short)count;
			previous0 = !count;
			while (remaining < threshold) {
				nbBits--;
				threshold >>= 1;
			}

			if ((ip <= iend - 7) || (ip + (bitCount >> 3) <= iend - 4)) {
				ip += bitCount >> 3;
				bitCount &= 7;
			} else {
				bitCount -= (int)(8 * (iend - 4 - ip));
				ip = iend - 4;
			}
			bitStream = ZSTD_readLE32(ip) >> (bitCount & 31);
		}
	} /* while ((remaining>1) & (charnum<=*maxSVPtr)) */
	if (remaining != 1)
		return ERROR(corruption_detected);
	if (bitCount > 32)
		return ERROR(corruption_detected);
	*maxSVPtr = charnum - 1;

	ip += (bitCount + 7) >> 3;
	return ip - istart;
}

/*! HUF_readStats() :
	Read compact Huffman tree, saved by HUF_writeCTable().
	`huffWeight` is destination buffer.
	`rankStats` is assumed to be a table of at least HUF_TABLELOG_MAX U32.
	@return : size read from `src` , or an error Code .
	Note : Needed by HUF_readCTable() and HUF_readDTableX?() .
*/
size_t HUF_readStats_wksp(BYTE *huffWeight, size_t hwSize, U32 *rankStats, U32 *nbSymbolsPtr, U32 *tableLogPtr, const void *src, size_t srcSize, void *workspace, size_t workspaceSize)
{
	U32 weightTotal;
	const BYTE *ip = (const BYTE *)src;
	size_t iSize;
	size_t oSize;

	if (!srcSize)
		return ERROR(srcSize_wrong);
	iSize = ip[0];
	/* memset(huffWeight, 0, hwSize);   */ /* is not necessary, even though some analyzer complain ... */

	if (iSize >= 128) { /* special header */
		oSize = iSize - 127;
		iSize = ((oSize + 1) / 2);
		if (iSize + 1 > srcSize)
			return ERROR(srcSize_wrong);
		if (oSize >= hwSize)
			return ERROR(corruption_detected);
		ip += 1;
		{
			U32 n;
			for (n = 0; n < oSize; n += 2) {
				huffWeight[n] = ip[n / 2] >> 4;
				huffWeight[n + 1] = ip[n / 2] & 15;
			}
		}
	} else {						 /* header compressed with FSE (normal case) */
		if (iSize + 1 > srcSize)
			return ERROR(srcSize_wrong);
		oSize = FSE_decompress_wksp(huffWeight, hwSize - 1, ip + 1, iSize, 6, workspace, workspaceSize); /* max (hwSize-1) values decoded, as last one is implied */
		if (FSE_isError(oSize))
			return oSize;
	}

	/* collect weight stats */
	memset(rankStats, 0, (HUF_TABLELOG_MAX + 1) * sizeof(U32));
	weightTotal = 0;
	{
		U32 n;
		for (n = 0; n < oSize; n++) {
			if (huffWeight[n] >= HUF_TABLELOG_MAX)
				return ERROR(corruption_detected);
			rankStats[huffWeight[n]]++;
			weightTotal += (1 << huffWeight[n]) >> 1;
		}
	}
	if (weightTotal == 0)
		return ERROR(corruption_detected);

	/* get last non-null symbol weight (implied, total must be 2^n) */
	{
		U32 const tableLog = BIT_highbit32(weightTotal) + 1;
		if (tableLog > HUF_TABLELOG_MAX)
			return ERROR(corruption_detected);
		*tableLogPtr = tableLog;
		/* determine last weight */
		{
			U32 const total = 1 << tableLog;
			U32 const rest = total - weightTotal;
			U32 const verif = 1 << BIT_highbit32(rest);
			U32 const lastWeight = BIT_highbit32(rest) + 1;
			if (verif != rest)
				return ERROR(corruption_detected); /* last value must be a clean power of 2 */
			huffWeight[oSize] = (BYTE)lastWeight;
			rankStats[lastWeight]++;
		}
	}

	/* check tree construction validity */
	if ((rankStats[1] < 2) || (rankStats[1] & 1))
		return ERROR(corruption_detected); /* by construction : at least 2 elts of rank 1, must be even */

	/* results */
	*nbSymbolsPtr = (U32)(oSize + 1);
	return iSize + 1;
}
