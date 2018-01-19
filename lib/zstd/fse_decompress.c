/*
 * FSE : Finite State Entropy decoder
 * Copyright (C) 2013-2015, Yann Collet.
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
*  Includes
****************************************************************/
#include "bitstream.h"
#include "fse.h"
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/string.h> /* memcpy, memset */

/* **************************************************************
*  Error Management
****************************************************************/
#define FSE_isError ERR_isError
#define FSE_STATIC_ASSERT(c)                                   \
	{                                                      \
		enum { FSE_static_assert = 1 / (int)(!!(c)) }; \
	} /* use only *after* variable declarations */

/* check and forward error code */
#define CHECK_F(f)                  \
	{                           \
		size_t const e = f; \
		if (FSE_isError(e)) \
			return e;   \
	}

/* **************************************************************
*  Templates
****************************************************************/
/*
  designed to be included
  for type-specific functions (template emulation in C)
  Objective is to write these functions only once, for improved maintenance
*/

/* safety checks */
#ifndef FSE_FUNCTION_EXTENSION
#error "FSE_FUNCTION_EXTENSION must be defined"
#endif
#ifndef FSE_FUNCTION_TYPE
#error "FSE_FUNCTION_TYPE must be defined"
#endif

/* Function names */
#define FSE_CAT(X, Y) X##Y
#define FSE_FUNCTION_NAME(X, Y) FSE_CAT(X, Y)
#define FSE_TYPE_NAME(X, Y) FSE_CAT(X, Y)

/* Function templates */

size_t FSE_buildDTable_wksp(FSE_DTable *dt, const short *normalizedCounter, unsigned maxSymbolValue, unsigned tableLog, void *workspace, size_t workspaceSize)
{
	void *const tdPtr = dt + 1; /* because *dt is unsigned, 32-bits aligned on 32-bits */
	FSE_DECODE_TYPE *const tableDecode = (FSE_DECODE_TYPE *)(tdPtr);
	U16 *symbolNext = (U16 *)workspace;

	U32 const maxSV1 = maxSymbolValue + 1;
	U32 const tableSize = 1 << tableLog;
	U32 highThreshold = tableSize - 1;

	/* Sanity Checks */
	if (workspaceSize < sizeof(U16) * (FSE_MAX_SYMBOL_VALUE + 1))
		return ERROR(tableLog_tooLarge);
	if (maxSymbolValue > FSE_MAX_SYMBOL_VALUE)
		return ERROR(maxSymbolValue_tooLarge);
	if (tableLog > FSE_MAX_TABLELOG)
		return ERROR(tableLog_tooLarge);

	/* Init, lay down lowprob symbols */
	{
		FSE_DTableHeader DTableH;
		DTableH.tableLog = (U16)tableLog;
		DTableH.fastMode = 1;
		{
			S16 const largeLimit = (S16)(1 << (tableLog - 1));
			U32 s;
			for (s = 0; s < maxSV1; s++) {
				if (normalizedCounter[s] == -1) {
					tableDecode[highThreshold--].symbol = (FSE_FUNCTION_TYPE)s;
					symbolNext[s] = 1;
				} else {
					if (normalizedCounter[s] >= largeLimit)
						DTableH.fastMode = 0;
					symbolNext[s] = normalizedCounter[s];
				}
			}
		}
		memcpy(dt, &DTableH, sizeof(DTableH));
	}

	/* Spread symbols */
	{
		U32 const tableMask = tableSize - 1;
		U32 const step = FSE_TABLESTEP(tableSize);
		U32 s, position = 0;
		for (s = 0; s < maxSV1; s++) {
			int i;
			for (i = 0; i < normalizedCounter[s]; i++) {
				tableDecode[position].symbol = (FSE_FUNCTION_TYPE)s;
				position = (position + step) & tableMask;
				while (position > highThreshold)
					position = (position + step) & tableMask; /* lowprob area */
			}
		}
		if (position != 0)
			return ERROR(GENERIC); /* position must reach all cells once, otherwise normalizedCounter is incorrect */
	}

	/* Build Decoding table */
	{
		U32 u;
		for (u = 0; u < tableSize; u++) {
			FSE_FUNCTION_TYPE const symbol = (FSE_FUNCTION_TYPE)(tableDecode[u].symbol);
			U16 nextState = symbolNext[symbol]++;
			tableDecode[u].nbBits = (BYTE)(tableLog - BIT_highbit32((U32)nextState));
			tableDecode[u].newState = (U16)((nextState << tableDecode[u].nbBits) - tableSize);
		}
	}

	return 0;
}

/*-*******************************************************
*  Decompression (Byte symbols)
*********************************************************/
size_t FSE_buildDTable_rle(FSE_DTable *dt, BYTE symbolValue)
{
	void *ptr = dt;
	FSE_DTableHeader *const DTableH = (FSE_DTableHeader *)ptr;
	void *dPtr = dt + 1;
	FSE_decode_t *const cell = (FSE_decode_t *)dPtr;

	DTableH->tableLog = 0;
	DTableH->fastMode = 0;

	cell->newState = 0;
	cell->symbol = symbolValue;
	cell->nbBits = 0;

	return 0;
}

size_t FSE_buildDTable_raw(FSE_DTable *dt, unsigned nbBits)
{
	void *ptr = dt;
	FSE_DTableHeader *const DTableH = (FSE_DTableHeader *)ptr;
	void *dPtr = dt + 1;
	FSE_decode_t *const dinfo = (FSE_decode_t *)dPtr;
	const unsigned tableSize = 1 << nbBits;
	const unsigned tableMask = tableSize - 1;
	const unsigned maxSV1 = tableMask + 1;
	unsigned s;

	/* Sanity checks */
	if (nbBits < 1)
		return ERROR(GENERIC); /* min size */

	/* Build Decoding Table */
	DTableH->tableLog = (U16)nbBits;
	DTableH->fastMode = 1;
	for (s = 0; s < maxSV1; s++) {
		dinfo[s].newState = 0;
		dinfo[s].symbol = (BYTE)s;
		dinfo[s].nbBits = (BYTE)nbBits;
	}

	return 0;
}

FORCE_INLINE size_t FSE_decompress_usingDTable_generic(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const FSE_DTable *dt,
						       const unsigned fast)
{
	BYTE *const ostart = (BYTE *)dst;
	BYTE *op = ostart;
	BYTE *const omax = op + maxDstSize;
	BYTE *const olimit = omax - 3;

	BIT_DStream_t bitD;
	FSE_DState_t state1;
	FSE_DState_t state2;

	/* Init */
	CHECK_F(BIT_initDStream(&bitD, cSrc, cSrcSize));

	FSE_initDState(&state1, &bitD, dt);
	FSE_initDState(&state2, &bitD, dt);

#define FSE_GETSYMBOL(statePtr) fast ? FSE_decodeSymbolFast(statePtr, &bitD) : FSE_decodeSymbol(statePtr, &bitD)

	/* 4 symbols per loop */
	for (; (BIT_reloadDStream(&bitD) == BIT_DStream_unfinished) & (op < olimit); op += 4) {
		op[0] = FSE_GETSYMBOL(&state1);

		if (FSE_MAX_TABLELOG * 2 + 7 > sizeof(bitD.bitContainer) * 8) /* This test must be static */
			BIT_reloadDStream(&bitD);

		op[1] = FSE_GETSYMBOL(&state2);

		if (FSE_MAX_TABLELOG * 4 + 7 > sizeof(bitD.bitContainer) * 8) /* This test must be static */
		{
			if (BIT_reloadDStream(&bitD) > BIT_DStream_unfinished) {
				op += 2;
				break;
			}
		}

		op[2] = FSE_GETSYMBOL(&state1);

		if (FSE_MAX_TABLELOG * 2 + 7 > sizeof(bitD.bitContainer) * 8) /* This test must be static */
			BIT_reloadDStream(&bitD);

		op[3] = FSE_GETSYMBOL(&state2);
	}

	/* tail */
	/* note : BIT_reloadDStream(&bitD) >= FSE_DStream_partiallyFilled; Ends at exactly BIT_DStream_completed */
	while (1) {
		if (op > (omax - 2))
			return ERROR(dstSize_tooSmall);
		*op++ = FSE_GETSYMBOL(&state1);
		if (BIT_reloadDStream(&bitD) == BIT_DStream_overflow) {
			*op++ = FSE_GETSYMBOL(&state2);
			break;
		}

		if (op > (omax - 2))
			return ERROR(dstSize_tooSmall);
		*op++ = FSE_GETSYMBOL(&state2);
		if (BIT_reloadDStream(&bitD) == BIT_DStream_overflow) {
			*op++ = FSE_GETSYMBOL(&state1);
			break;
		}
	}

	return op - ostart;
}

size_t FSE_decompress_usingDTable(void *dst, size_t originalSize, const void *cSrc, size_t cSrcSize, const FSE_DTable *dt)
{
	const void *ptr = dt;
	const FSE_DTableHeader *DTableH = (const FSE_DTableHeader *)ptr;
	const U32 fastMode = DTableH->fastMode;

	/* select fast mode (static) */
	if (fastMode)
		return FSE_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 1);
	return FSE_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 0);
}

size_t FSE_decompress_wksp(void *dst, size_t dstCapacity, const void *cSrc, size_t cSrcSize, unsigned maxLog, void *workspace, size_t workspaceSize)
{
	const BYTE *const istart = (const BYTE *)cSrc;
	const BYTE *ip = istart;
	unsigned tableLog;
	unsigned maxSymbolValue = FSE_MAX_SYMBOL_VALUE;
	size_t NCountLength;

	FSE_DTable *dt;
	short *counting;
	size_t spaceUsed32 = 0;

	FSE_STATIC_ASSERT(sizeof(FSE_DTable) == sizeof(U32));

	dt = (FSE_DTable *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += FSE_DTABLE_SIZE_U32(maxLog);
	counting = (short *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += ALIGN(sizeof(short) * (FSE_MAX_SYMBOL_VALUE + 1), sizeof(U32)) >> 2;

	if ((spaceUsed32 << 2) > workspaceSize)
		return ERROR(tableLog_tooLarge);
	workspace = (U32 *)workspace + spaceUsed32;
	workspaceSize -= (spaceUsed32 << 2);

	/* normal FSE decoding mode */
	NCountLength = FSE_readNCount(counting, &maxSymbolValue, &tableLog, istart, cSrcSize);
	if (FSE_isError(NCountLength))
		return NCountLength;
	// if (NCountLength >= cSrcSize) return ERROR(srcSize_wrong);   /* too small input size; supposed to be already checked in NCountLength, only remaining
	// case : NCountLength==cSrcSize */
	if (tableLog > maxLog)
		return ERROR(tableLog_tooLarge);
	ip += NCountLength;
	cSrcSize -= NCountLength;

	CHECK_F(FSE_buildDTable_wksp(dt, counting, maxSymbolValue, tableLog, workspace, workspaceSize));

	return FSE_decompress_usingDTable(dst, dstCapacity, ip, cSrcSize, dt); /* always return, even if it is an error code */
}
