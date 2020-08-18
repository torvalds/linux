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

#ifndef ZSTD_CCOMMON_H_MODULE
#define ZSTD_CCOMMON_H_MODULE

/*-*******************************************************
*  Compiler specifics
*********************************************************/
#define FORCE_INLINE static __always_inline
#define FORCE_NOINLINE static noinline

/*-*************************************
*  Dependencies
***************************************/
#include "error_private.h"
#include "mem.h"
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/xxhash.h>
#include <linux/zstd.h>

/*-*************************************
*  shared macros
***************************************/
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CHECK_F(f)                       \
	{                                \
		size_t const errcod = f; \
		if (ERR_isError(errcod)) \
			return errcod;   \
	} /* check and Forward error code */
#define CHECK_E(f, e)                    \
	{                                \
		size_t const errcod = f; \
		if (ERR_isError(errcod)) \
			return ERROR(e); \
	} /* check and send Error code */
#define ZSTD_STATIC_ASSERT(c)                                   \
	{                                                       \
		enum { ZSTD_static_assert = 1 / (int)(!!(c)) }; \
	}

/*-*************************************
*  Common constants
***************************************/
#define ZSTD_OPT_NUM (1 << 12)
#define ZSTD_DICT_MAGIC 0xEC30A437 /* v0.7+ */

#define ZSTD_REP_NUM 3		      /* number of repcodes */
#define ZSTD_REP_CHECK (ZSTD_REP_NUM) /* number of repcodes to check by the optimal parser */
#define ZSTD_REP_MOVE (ZSTD_REP_NUM - 1)
#define ZSTD_REP_MOVE_OPT (ZSTD_REP_NUM)
static const U32 repStartValue[ZSTD_REP_NUM] = {1, 4, 8};

#define KB *(1 << 10)
#define MB *(1 << 20)
#define GB *(1U << 30)

#define BIT7 128
#define BIT6 64
#define BIT5 32
#define BIT4 16
#define BIT1 2
#define BIT0 1

#define ZSTD_WINDOWLOG_ABSOLUTEMIN 10
static const size_t ZSTD_fcs_fieldSize[4] = {0, 2, 4, 8};
static const size_t ZSTD_did_fieldSize[4] = {0, 1, 2, 4};

#define ZSTD_BLOCKHEADERSIZE 3 /* C standard doesn't allow `static const` variable to be init using another `static const` variable */
static const size_t ZSTD_blockHeaderSize = ZSTD_BLOCKHEADERSIZE;
typedef enum { bt_raw, bt_rle, bt_compressed, bt_reserved } blockType_e;

#define MIN_SEQUENCES_SIZE 1									  /* nbSeq==0 */
#define MIN_CBLOCK_SIZE (1 /*litCSize*/ + 1 /* RLE or RAW */ + MIN_SEQUENCES_SIZE /* nbSeq==0 */) /* for a non-null block */

#define HufLog 12
typedef enum { set_basic, set_rle, set_compressed, set_repeat } symbolEncodingType_e;

#define LONGNBSEQ 0x7F00

#define MINMATCH 3
#define EQUAL_READ32 4

#define Litbits 8
#define MaxLit ((1 << Litbits) - 1)
#define MaxML 52
#define MaxLL 35
#define MaxOff 28
#define MaxSeq MAX(MaxLL, MaxML) /* Assumption : MaxOff < MaxLL,MaxML */
#define MLFSELog 9
#define LLFSELog 9
#define OffFSELog 8

static const U32 LL_bits[MaxLL + 1] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const S16 LL_defaultNorm[MaxLL + 1] = {4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};
#define LL_DEFAULTNORMLOG 6 /* for static allocation */
static const U32 LL_defaultNormLog = LL_DEFAULTNORMLOG;

static const U32 ML_bits[MaxML + 1] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0, 0,
				       0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const S16 ML_defaultNorm[MaxML + 1] = {1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,  1, 1,
					      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};
#define ML_DEFAULTNORMLOG 6 /* for static allocation */
static const U32 ML_defaultNormLog = ML_DEFAULTNORMLOG;

static const S16 OF_defaultNorm[MaxOff + 1] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};
#define OF_DEFAULTNORMLOG 5 /* for static allocation */
static const U32 OF_defaultNormLog = OF_DEFAULTNORMLOG;

/*-*******************************************
*  Shared functions to include for inlining
*********************************************/
ZSTD_STATIC void ZSTD_copy8(void *dst, const void *src) {
	/*
	 * zstd relies heavily on gcc being able to analyze and inline this
	 * memcpy() call, since it is called in a tight loop. Preboot mode
	 * is compiled in freestanding mode, which stops gcc from analyzing
	 * memcpy(). Use __builtin_memcpy() to tell gcc to analyze this as a
	 * regular memcpy().
	 */
	__builtin_memcpy(dst, src, 8);
}
/*! ZSTD_wildcopy() :
*   custom version of memcpy(), can copy up to 7 bytes too many (8 bytes if length==0) */
#define WILDCOPY_OVERLENGTH 8
ZSTD_STATIC void ZSTD_wildcopy(void *dst, const void *src, ptrdiff_t length)
{
	const BYTE* ip = (const BYTE*)src;
	BYTE* op = (BYTE*)dst;
	BYTE* const oend = op + length;
#if defined(GCC_VERSION) && GCC_VERSION >= 70000 && GCC_VERSION < 70200
	/*
	 * Work around https://gcc.gnu.org/bugzilla/show_bug.cgi?id=81388.
	 * Avoid the bad case where the loop only runs once by handling the
	 * special case separately. This doesn't trigger the bug because it
	 * doesn't involve pointer/integer overflow.
	 */
	if (length <= 8)
		return ZSTD_copy8(dst, src);
#endif
	do {
		ZSTD_copy8(op, ip);
		op += 8;
		ip += 8;
	} while (op < oend);
}

/*-*******************************************
*  Private interfaces
*********************************************/
typedef struct ZSTD_stats_s ZSTD_stats_t;

typedef struct {
	U32 off;
	U32 len;
} ZSTD_match_t;

typedef struct {
	U32 price;
	U32 off;
	U32 mlen;
	U32 litlen;
	U32 rep[ZSTD_REP_NUM];
} ZSTD_optimal_t;

typedef struct seqDef_s {
	U32 offset;
	U16 litLength;
	U16 matchLength;
} seqDef;

typedef struct {
	seqDef *sequencesStart;
	seqDef *sequences;
	BYTE *litStart;
	BYTE *lit;
	BYTE *llCode;
	BYTE *mlCode;
	BYTE *ofCode;
	U32 longLengthID; /* 0 == no longLength; 1 == Lit.longLength; 2 == Match.longLength; */
	U32 longLengthPos;
	/* opt */
	ZSTD_optimal_t *priceTable;
	ZSTD_match_t *matchTable;
	U32 *matchLengthFreq;
	U32 *litLengthFreq;
	U32 *litFreq;
	U32 *offCodeFreq;
	U32 matchLengthSum;
	U32 matchSum;
	U32 litLengthSum;
	U32 litSum;
	U32 offCodeSum;
	U32 log2matchLengthSum;
	U32 log2matchSum;
	U32 log2litLengthSum;
	U32 log2litSum;
	U32 log2offCodeSum;
	U32 factor;
	U32 staticPrices;
	U32 cachedPrice;
	U32 cachedLitLength;
	const BYTE *cachedLiterals;
} seqStore_t;

const seqStore_t *ZSTD_getSeqStore(const ZSTD_CCtx *ctx);
void ZSTD_seqToCodes(const seqStore_t *seqStorePtr);
int ZSTD_isSkipFrame(ZSTD_DCtx *dctx);

/*= Custom memory allocation functions */
typedef void *(*ZSTD_allocFunction)(void *opaque, size_t size);
typedef void (*ZSTD_freeFunction)(void *opaque, void *address);
typedef struct {
	ZSTD_allocFunction customAlloc;
	ZSTD_freeFunction customFree;
	void *opaque;
} ZSTD_customMem;

void *ZSTD_malloc(size_t size, ZSTD_customMem customMem);
void ZSTD_free(void *ptr, ZSTD_customMem customMem);

/*====== stack allocation  ======*/

typedef struct {
	void *ptr;
	const void *end;
} ZSTD_stack;

#define ZSTD_ALIGN(x) ALIGN(x, sizeof(size_t))
#define ZSTD_PTR_ALIGN(p) PTR_ALIGN(p, sizeof(size_t))

ZSTD_customMem ZSTD_initStack(void *workspace, size_t workspaceSize);

void *ZSTD_stackAllocAll(void *opaque, size_t *size);
void *ZSTD_stackAlloc(void *opaque, size_t size);
void ZSTD_stackFree(void *opaque, void *address);

/*======  common function  ======*/

ZSTD_STATIC U32 ZSTD_highbit32(U32 val) { return 31 - __builtin_clz(val); }

/* hidden functions */

/* ZSTD_invalidateRepCodes() :
 * ensures next compression will not use repcodes from previous block.
 * Note : only works with regular variant;
 *        do not use with extDict variant ! */
void ZSTD_invalidateRepCodes(ZSTD_CCtx *cctx);

size_t ZSTD_freeCCtx(ZSTD_CCtx *cctx);
size_t ZSTD_freeDCtx(ZSTD_DCtx *dctx);
size_t ZSTD_freeCDict(ZSTD_CDict *cdict);
size_t ZSTD_freeDDict(ZSTD_DDict *cdict);
size_t ZSTD_freeCStream(ZSTD_CStream *zcs);
size_t ZSTD_freeDStream(ZSTD_DStream *zds);

#endif /* ZSTD_CCOMMON_H_MODULE */
