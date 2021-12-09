/*
 * Copyright (c) Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

/* zstd_decompress_block :
 * this module takes care of decompressing _compressed_ block */

/*-*******************************************************
*  Dependencies
*********************************************************/
#include "../common/zstd_deps.h"   /* ZSTD_memcpy, ZSTD_memmove, ZSTD_memset */
#include "../common/compiler.h"    /* prefetch */
#include "../common/cpu.h"         /* bmi2 */
#include "../common/mem.h"         /* low level memory routines */
#define FSE_STATIC_LINKING_ONLY
#include "../common/fse.h"
#define HUF_STATIC_LINKING_ONLY
#include "../common/huf.h"
#include "../common/zstd_internal.h"
#include "zstd_decompress_internal.h"   /* ZSTD_DCtx */
#include "zstd_ddict.h"  /* ZSTD_DDictDictContent */
#include "zstd_decompress_block.h"

/*_*******************************************************
*  Macros
**********************************************************/

/* These two optional macros force the use one way or another of the two
 * ZSTD_decompressSequences implementations. You can't force in both directions
 * at the same time.
 */
#if defined(ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT) && \
    defined(ZSTD_FORCE_DECOMPRESS_SEQUENCES_LONG)
#error "Cannot force the use of the short and the long ZSTD_decompressSequences variants!"
#endif


/*_*******************************************************
*  Memory operations
**********************************************************/
static void ZSTD_copy4(void* dst, const void* src) { ZSTD_memcpy(dst, src, 4); }


/*-*************************************************************
 *   Block decoding
 ***************************************************************/

/*! ZSTD_getcBlockSize() :
 *  Provides the size of compressed block from block header `src` */
size_t ZSTD_getcBlockSize(const void* src, size_t srcSize,
                          blockProperties_t* bpPtr)
{
    RETURN_ERROR_IF(srcSize < ZSTD_blockHeaderSize, srcSize_wrong, "");

    {   U32 const cBlockHeader = MEM_readLE24(src);
        U32 const cSize = cBlockHeader >> 3;
        bpPtr->lastBlock = cBlockHeader & 1;
        bpPtr->blockType = (blockType_e)((cBlockHeader >> 1) & 3);
        bpPtr->origSize = cSize;   /* only useful for RLE */
        if (bpPtr->blockType == bt_rle) return 1;
        RETURN_ERROR_IF(bpPtr->blockType == bt_reserved, corruption_detected, "");
        return cSize;
    }
}


/* Hidden declaration for fullbench */
size_t ZSTD_decodeLiteralsBlock(ZSTD_DCtx* dctx,
                          const void* src, size_t srcSize);
/*! ZSTD_decodeLiteralsBlock() :
 * @return : nb of bytes read from src (< srcSize )
 *  note : symbol not declared but exposed for fullbench */
size_t ZSTD_decodeLiteralsBlock(ZSTD_DCtx* dctx,
                          const void* src, size_t srcSize)   /* note : srcSize < BLOCKSIZE */
{
    DEBUGLOG(5, "ZSTD_decodeLiteralsBlock");
    RETURN_ERROR_IF(srcSize < MIN_CBLOCK_SIZE, corruption_detected, "");

    {   const BYTE* const istart = (const BYTE*) src;
        symbolEncodingType_e const litEncType = (symbolEncodingType_e)(istart[0] & 3);

        switch(litEncType)
        {
        case set_repeat:
            DEBUGLOG(5, "set_repeat flag : re-using stats from previous compressed literals block");
            RETURN_ERROR_IF(dctx->litEntropy==0, dictionary_corrupted, "");
            ZSTD_FALLTHROUGH;

        case set_compressed:
            RETURN_ERROR_IF(srcSize < 5, corruption_detected, "srcSize >= MIN_CBLOCK_SIZE == 3; here we need up to 5 for case 3");
            {   size_t lhSize, litSize, litCSize;
                U32 singleStream=0;
                U32 const lhlCode = (istart[0] >> 2) & 3;
                U32 const lhc = MEM_readLE32(istart);
                size_t hufSuccess;
                switch(lhlCode)
                {
                case 0: case 1: default:   /* note : default is impossible, since lhlCode into [0..3] */
                    /* 2 - 2 - 10 - 10 */
                    singleStream = !lhlCode;
                    lhSize = 3;
                    litSize  = (lhc >> 4) & 0x3FF;
                    litCSize = (lhc >> 14) & 0x3FF;
                    break;
                case 2:
                    /* 2 - 2 - 14 - 14 */
                    lhSize = 4;
                    litSize  = (lhc >> 4) & 0x3FFF;
                    litCSize = lhc >> 18;
                    break;
                case 3:
                    /* 2 - 2 - 18 - 18 */
                    lhSize = 5;
                    litSize  = (lhc >> 4) & 0x3FFFF;
                    litCSize = (lhc >> 22) + ((size_t)istart[4] << 10);
                    break;
                }
                RETURN_ERROR_IF(litSize > ZSTD_BLOCKSIZE_MAX, corruption_detected, "");
                RETURN_ERROR_IF(litCSize + lhSize > srcSize, corruption_detected, "");

                /* prefetch huffman table if cold */
                if (dctx->ddictIsCold && (litSize > 768 /* heuristic */)) {
                    PREFETCH_AREA(dctx->HUFptr, sizeof(dctx->entropy.hufTable));
                }

                if (litEncType==set_repeat) {
                    if (singleStream) {
                        hufSuccess = HUF_decompress1X_usingDTable_bmi2(
                            dctx->litBuffer, litSize, istart+lhSize, litCSize,
                            dctx->HUFptr, dctx->bmi2);
                    } else {
                        hufSuccess = HUF_decompress4X_usingDTable_bmi2(
                            dctx->litBuffer, litSize, istart+lhSize, litCSize,
                            dctx->HUFptr, dctx->bmi2);
                    }
                } else {
                    if (singleStream) {
#if defined(HUF_FORCE_DECOMPRESS_X2)
                        hufSuccess = HUF_decompress1X_DCtx_wksp(
                            dctx->entropy.hufTable, dctx->litBuffer, litSize,
                            istart+lhSize, litCSize, dctx->workspace,
                            sizeof(dctx->workspace));
#else
                        hufSuccess = HUF_decompress1X1_DCtx_wksp_bmi2(
                            dctx->entropy.hufTable, dctx->litBuffer, litSize,
                            istart+lhSize, litCSize, dctx->workspace,
                            sizeof(dctx->workspace), dctx->bmi2);
#endif
                    } else {
                        hufSuccess = HUF_decompress4X_hufOnly_wksp_bmi2(
                            dctx->entropy.hufTable, dctx->litBuffer, litSize,
                            istart+lhSize, litCSize, dctx->workspace,
                            sizeof(dctx->workspace), dctx->bmi2);
                    }
                }

                RETURN_ERROR_IF(HUF_isError(hufSuccess), corruption_detected, "");

                dctx->litPtr = dctx->litBuffer;
                dctx->litSize = litSize;
                dctx->litEntropy = 1;
                if (litEncType==set_compressed) dctx->HUFptr = dctx->entropy.hufTable;
                ZSTD_memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
                return litCSize + lhSize;
            }

        case set_basic:
            {   size_t litSize, lhSize;
                U32 const lhlCode = ((istart[0]) >> 2) & 3;
                switch(lhlCode)
                {
                case 0: case 2: default:   /* note : default is impossible, since lhlCode into [0..3] */
                    lhSize = 1;
                    litSize = istart[0] >> 3;
                    break;
                case 1:
                    lhSize = 2;
                    litSize = MEM_readLE16(istart) >> 4;
                    break;
                case 3:
                    lhSize = 3;
                    litSize = MEM_readLE24(istart) >> 4;
                    break;
                }

                if (lhSize+litSize+WILDCOPY_OVERLENGTH > srcSize) {  /* risk reading beyond src buffer with wildcopy */
                    RETURN_ERROR_IF(litSize+lhSize > srcSize, corruption_detected, "");
                    ZSTD_memcpy(dctx->litBuffer, istart+lhSize, litSize);
                    dctx->litPtr = dctx->litBuffer;
                    dctx->litSize = litSize;
                    ZSTD_memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
                    return lhSize+litSize;
                }
                /* direct reference into compressed stream */
                dctx->litPtr = istart+lhSize;
                dctx->litSize = litSize;
                return lhSize+litSize;
            }

        case set_rle:
            {   U32 const lhlCode = ((istart[0]) >> 2) & 3;
                size_t litSize, lhSize;
                switch(lhlCode)
                {
                case 0: case 2: default:   /* note : default is impossible, since lhlCode into [0..3] */
                    lhSize = 1;
                    litSize = istart[0] >> 3;
                    break;
                case 1:
                    lhSize = 2;
                    litSize = MEM_readLE16(istart) >> 4;
                    break;
                case 3:
                    lhSize = 3;
                    litSize = MEM_readLE24(istart) >> 4;
                    RETURN_ERROR_IF(srcSize<4, corruption_detected, "srcSize >= MIN_CBLOCK_SIZE == 3; here we need lhSize+1 = 4");
                    break;
                }
                RETURN_ERROR_IF(litSize > ZSTD_BLOCKSIZE_MAX, corruption_detected, "");
                ZSTD_memset(dctx->litBuffer, istart[lhSize], litSize + WILDCOPY_OVERLENGTH);
                dctx->litPtr = dctx->litBuffer;
                dctx->litSize = litSize;
                return lhSize+1;
            }
        default:
            RETURN_ERROR(corruption_detected, "impossible");
        }
    }
}

/* Default FSE distribution tables.
 * These are pre-calculated FSE decoding tables using default distributions as defined in specification :
 * https://github.com/facebook/zstd/blob/release/doc/zstd_compression_format.md#default-distributions
 * They were generated programmatically with following method :
 * - start from default distributions, present in /lib/common/zstd_internal.h
 * - generate tables normally, using ZSTD_buildFSETable()
 * - printout the content of tables
 * - pretify output, report below, test with fuzzer to ensure it's correct */

/* Default FSE distribution table for Literal Lengths */
static const ZSTD_seqSymbol LL_defaultDTable[(1<<LL_DEFAULTNORMLOG)+1] = {
     {  1,  1,  1, LL_DEFAULTNORMLOG},  /* header : fastMode, tableLog */
     /* nextState, nbAddBits, nbBits, baseVal */
     {  0,  0,  4,    0},  { 16,  0,  4,    0},
     { 32,  0,  5,    1},  {  0,  0,  5,    3},
     {  0,  0,  5,    4},  {  0,  0,  5,    6},
     {  0,  0,  5,    7},  {  0,  0,  5,    9},
     {  0,  0,  5,   10},  {  0,  0,  5,   12},
     {  0,  0,  6,   14},  {  0,  1,  5,   16},
     {  0,  1,  5,   20},  {  0,  1,  5,   22},
     {  0,  2,  5,   28},  {  0,  3,  5,   32},
     {  0,  4,  5,   48},  { 32,  6,  5,   64},
     {  0,  7,  5,  128},  {  0,  8,  6,  256},
     {  0, 10,  6, 1024},  {  0, 12,  6, 4096},
     { 32,  0,  4,    0},  {  0,  0,  4,    1},
     {  0,  0,  5,    2},  { 32,  0,  5,    4},
     {  0,  0,  5,    5},  { 32,  0,  5,    7},
     {  0,  0,  5,    8},  { 32,  0,  5,   10},
     {  0,  0,  5,   11},  {  0,  0,  6,   13},
     { 32,  1,  5,   16},  {  0,  1,  5,   18},
     { 32,  1,  5,   22},  {  0,  2,  5,   24},
     { 32,  3,  5,   32},  {  0,  3,  5,   40},
     {  0,  6,  4,   64},  { 16,  6,  4,   64},
     { 32,  7,  5,  128},  {  0,  9,  6,  512},
     {  0, 11,  6, 2048},  { 48,  0,  4,    0},
     { 16,  0,  4,    1},  { 32,  0,  5,    2},
     { 32,  0,  5,    3},  { 32,  0,  5,    5},
     { 32,  0,  5,    6},  { 32,  0,  5,    8},
     { 32,  0,  5,    9},  { 32,  0,  5,   11},
     { 32,  0,  5,   12},  {  0,  0,  6,   15},
     { 32,  1,  5,   18},  { 32,  1,  5,   20},
     { 32,  2,  5,   24},  { 32,  2,  5,   28},
     { 32,  3,  5,   40},  { 32,  4,  5,   48},
     {  0, 16,  6,65536},  {  0, 15,  6,32768},
     {  0, 14,  6,16384},  {  0, 13,  6, 8192},
};   /* LL_defaultDTable */

/* Default FSE distribution table for Offset Codes */
static const ZSTD_seqSymbol OF_defaultDTable[(1<<OF_DEFAULTNORMLOG)+1] = {
    {  1,  1,  1, OF_DEFAULTNORMLOG},  /* header : fastMode, tableLog */
    /* nextState, nbAddBits, nbBits, baseVal */
    {  0,  0,  5,    0},     {  0,  6,  4,   61},
    {  0,  9,  5,  509},     {  0, 15,  5,32765},
    {  0, 21,  5,2097149},   {  0,  3,  5,    5},
    {  0,  7,  4,  125},     {  0, 12,  5, 4093},
    {  0, 18,  5,262141},    {  0, 23,  5,8388605},
    {  0,  5,  5,   29},     {  0,  8,  4,  253},
    {  0, 14,  5,16381},     {  0, 20,  5,1048573},
    {  0,  2,  5,    1},     { 16,  7,  4,  125},
    {  0, 11,  5, 2045},     {  0, 17,  5,131069},
    {  0, 22,  5,4194301},   {  0,  4,  5,   13},
    { 16,  8,  4,  253},     {  0, 13,  5, 8189},
    {  0, 19,  5,524285},    {  0,  1,  5,    1},
    { 16,  6,  4,   61},     {  0, 10,  5, 1021},
    {  0, 16,  5,65533},     {  0, 28,  5,268435453},
    {  0, 27,  5,134217725}, {  0, 26,  5,67108861},
    {  0, 25,  5,33554429},  {  0, 24,  5,16777213},
};   /* OF_defaultDTable */


/* Default FSE distribution table for Match Lengths */
static const ZSTD_seqSymbol ML_defaultDTable[(1<<ML_DEFAULTNORMLOG)+1] = {
    {  1,  1,  1, ML_DEFAULTNORMLOG},  /* header : fastMode, tableLog */
    /* nextState, nbAddBits, nbBits, baseVal */
    {  0,  0,  6,    3},  {  0,  0,  4,    4},
    { 32,  0,  5,    5},  {  0,  0,  5,    6},
    {  0,  0,  5,    8},  {  0,  0,  5,    9},
    {  0,  0,  5,   11},  {  0,  0,  6,   13},
    {  0,  0,  6,   16},  {  0,  0,  6,   19},
    {  0,  0,  6,   22},  {  0,  0,  6,   25},
    {  0,  0,  6,   28},  {  0,  0,  6,   31},
    {  0,  0,  6,   34},  {  0,  1,  6,   37},
    {  0,  1,  6,   41},  {  0,  2,  6,   47},
    {  0,  3,  6,   59},  {  0,  4,  6,   83},
    {  0,  7,  6,  131},  {  0,  9,  6,  515},
    { 16,  0,  4,    4},  {  0,  0,  4,    5},
    { 32,  0,  5,    6},  {  0,  0,  5,    7},
    { 32,  0,  5,    9},  {  0,  0,  5,   10},
    {  0,  0,  6,   12},  {  0,  0,  6,   15},
    {  0,  0,  6,   18},  {  0,  0,  6,   21},
    {  0,  0,  6,   24},  {  0,  0,  6,   27},
    {  0,  0,  6,   30},  {  0,  0,  6,   33},
    {  0,  1,  6,   35},  {  0,  1,  6,   39},
    {  0,  2,  6,   43},  {  0,  3,  6,   51},
    {  0,  4,  6,   67},  {  0,  5,  6,   99},
    {  0,  8,  6,  259},  { 32,  0,  4,    4},
    { 48,  0,  4,    4},  { 16,  0,  4,    5},
    { 32,  0,  5,    7},  { 32,  0,  5,    8},
    { 32,  0,  5,   10},  { 32,  0,  5,   11},
    {  0,  0,  6,   14},  {  0,  0,  6,   17},
    {  0,  0,  6,   20},  {  0,  0,  6,   23},
    {  0,  0,  6,   26},  {  0,  0,  6,   29},
    {  0,  0,  6,   32},  {  0, 16,  6,65539},
    {  0, 15,  6,32771},  {  0, 14,  6,16387},
    {  0, 13,  6, 8195},  {  0, 12,  6, 4099},
    {  0, 11,  6, 2051},  {  0, 10,  6, 1027},
};   /* ML_defaultDTable */


static void ZSTD_buildSeqTable_rle(ZSTD_seqSymbol* dt, U32 baseValue, U32 nbAddBits)
{
    void* ptr = dt;
    ZSTD_seqSymbol_header* const DTableH = (ZSTD_seqSymbol_header*)ptr;
    ZSTD_seqSymbol* const cell = dt + 1;

    DTableH->tableLog = 0;
    DTableH->fastMode = 0;

    cell->nbBits = 0;
    cell->nextState = 0;
    assert(nbAddBits < 255);
    cell->nbAdditionalBits = (BYTE)nbAddBits;
    cell->baseValue = baseValue;
}


/* ZSTD_buildFSETable() :
 * generate FSE decoding table for one symbol (ll, ml or off)
 * cannot fail if input is valid =>
 * all inputs are presumed validated at this stage */
FORCE_INLINE_TEMPLATE
void ZSTD_buildFSETable_body(ZSTD_seqSymbol* dt,
            const short* normalizedCounter, unsigned maxSymbolValue,
            const U32* baseValue, const U32* nbAdditionalBits,
            unsigned tableLog, void* wksp, size_t wkspSize)
{
    ZSTD_seqSymbol* const tableDecode = dt+1;
    U32 const maxSV1 = maxSymbolValue + 1;
    U32 const tableSize = 1 << tableLog;

    U16* symbolNext = (U16*)wksp;
    BYTE* spread = (BYTE*)(symbolNext + MaxSeq + 1);
    U32 highThreshold = tableSize - 1;


    /* Sanity Checks */
    assert(maxSymbolValue <= MaxSeq);
    assert(tableLog <= MaxFSELog);
    assert(wkspSize >= ZSTD_BUILD_FSE_TABLE_WKSP_SIZE);
    (void)wkspSize;
    /* Init, lay down lowprob symbols */
    {   ZSTD_seqSymbol_header DTableH;
        DTableH.tableLog = tableLog;
        DTableH.fastMode = 1;
        {   S16 const largeLimit= (S16)(1 << (tableLog-1));
            U32 s;
            for (s=0; s<maxSV1; s++) {
                if (normalizedCounter[s]==-1) {
                    tableDecode[highThreshold--].baseValue = s;
                    symbolNext[s] = 1;
                } else {
                    if (normalizedCounter[s] >= largeLimit) DTableH.fastMode=0;
                    assert(normalizedCounter[s]>=0);
                    symbolNext[s] = (U16)normalizedCounter[s];
        }   }   }
        ZSTD_memcpy(dt, &DTableH, sizeof(DTableH));
    }

    /* Spread symbols */
    assert(tableSize <= 512);
    /* Specialized symbol spreading for the case when there are
     * no low probability (-1 count) symbols. When compressing
     * small blocks we avoid low probability symbols to hit this
     * case, since header decoding speed matters more.
     */
    if (highThreshold == tableSize - 1) {
        size_t const tableMask = tableSize-1;
        size_t const step = FSE_TABLESTEP(tableSize);
        /* First lay down the symbols in order.
         * We use a uint64_t to lay down 8 bytes at a time. This reduces branch
         * misses since small blocks generally have small table logs, so nearly
         * all symbols have counts <= 8. We ensure we have 8 bytes at the end of
         * our buffer to handle the over-write.
         */
        {
            U64 const add = 0x0101010101010101ull;
            size_t pos = 0;
            U64 sv = 0;
            U32 s;
            for (s=0; s<maxSV1; ++s, sv += add) {
                int i;
                int const n = normalizedCounter[s];
                MEM_write64(spread + pos, sv);
                for (i = 8; i < n; i += 8) {
                    MEM_write64(spread + pos + i, sv);
                }
                pos += n;
            }
        }
        /* Now we spread those positions across the table.
         * The benefit of doing it in two stages is that we avoid the the
         * variable size inner loop, which caused lots of branch misses.
         * Now we can run through all the positions without any branch misses.
         * We unroll the loop twice, since that is what emperically worked best.
         */
        {
            size_t position = 0;
            size_t s;
            size_t const unroll = 2;
            assert(tableSize % unroll == 0); /* FSE_MIN_TABLELOG is 5 */
            for (s = 0; s < (size_t)tableSize; s += unroll) {
                size_t u;
                for (u = 0; u < unroll; ++u) {
                    size_t const uPosition = (position + (u * step)) & tableMask;
                    tableDecode[uPosition].baseValue = spread[s + u];
                }
                position = (position + (unroll * step)) & tableMask;
            }
            assert(position == 0);
        }
    } else {
        U32 const tableMask = tableSize-1;
        U32 const step = FSE_TABLESTEP(tableSize);
        U32 s, position = 0;
        for (s=0; s<maxSV1; s++) {
            int i;
            int const n = normalizedCounter[s];
            for (i=0; i<n; i++) {
                tableDecode[position].baseValue = s;
                position = (position + step) & tableMask;
                while (position > highThreshold) position = (position + step) & tableMask;   /* lowprob area */
        }   }
        assert(position == 0); /* position must reach all cells once, otherwise normalizedCounter is incorrect */
    }

    /* Build Decoding table */
    {
        U32 u;
        for (u=0; u<tableSize; u++) {
            U32 const symbol = tableDecode[u].baseValue;
            U32 const nextState = symbolNext[symbol]++;
            tableDecode[u].nbBits = (BYTE) (tableLog - BIT_highbit32(nextState) );
            tableDecode[u].nextState = (U16) ( (nextState << tableDecode[u].nbBits) - tableSize);
            assert(nbAdditionalBits[symbol] < 255);
            tableDecode[u].nbAdditionalBits = (BYTE)nbAdditionalBits[symbol];
            tableDecode[u].baseValue = baseValue[symbol];
        }
    }
}

/* Avoids the FORCE_INLINE of the _body() function. */
static void ZSTD_buildFSETable_body_default(ZSTD_seqSymbol* dt,
            const short* normalizedCounter, unsigned maxSymbolValue,
            const U32* baseValue, const U32* nbAdditionalBits,
            unsigned tableLog, void* wksp, size_t wkspSize)
{
    ZSTD_buildFSETable_body(dt, normalizedCounter, maxSymbolValue,
            baseValue, nbAdditionalBits, tableLog, wksp, wkspSize);
}

#if DYNAMIC_BMI2
TARGET_ATTRIBUTE("bmi2") static void ZSTD_buildFSETable_body_bmi2(ZSTD_seqSymbol* dt,
            const short* normalizedCounter, unsigned maxSymbolValue,
            const U32* baseValue, const U32* nbAdditionalBits,
            unsigned tableLog, void* wksp, size_t wkspSize)
{
    ZSTD_buildFSETable_body(dt, normalizedCounter, maxSymbolValue,
            baseValue, nbAdditionalBits, tableLog, wksp, wkspSize);
}
#endif

void ZSTD_buildFSETable(ZSTD_seqSymbol* dt,
            const short* normalizedCounter, unsigned maxSymbolValue,
            const U32* baseValue, const U32* nbAdditionalBits,
            unsigned tableLog, void* wksp, size_t wkspSize, int bmi2)
{
#if DYNAMIC_BMI2
    if (bmi2) {
        ZSTD_buildFSETable_body_bmi2(dt, normalizedCounter, maxSymbolValue,
                baseValue, nbAdditionalBits, tableLog, wksp, wkspSize);
        return;
    }
#endif
    (void)bmi2;
    ZSTD_buildFSETable_body_default(dt, normalizedCounter, maxSymbolValue,
            baseValue, nbAdditionalBits, tableLog, wksp, wkspSize);
}


/*! ZSTD_buildSeqTable() :
 * @return : nb bytes read from src,
 *           or an error code if it fails */
static size_t ZSTD_buildSeqTable(ZSTD_seqSymbol* DTableSpace, const ZSTD_seqSymbol** DTablePtr,
                                 symbolEncodingType_e type, unsigned max, U32 maxLog,
                                 const void* src, size_t srcSize,
                                 const U32* baseValue, const U32* nbAdditionalBits,
                                 const ZSTD_seqSymbol* defaultTable, U32 flagRepeatTable,
                                 int ddictIsCold, int nbSeq, U32* wksp, size_t wkspSize,
                                 int bmi2)
{
    switch(type)
    {
    case set_rle :
        RETURN_ERROR_IF(!srcSize, srcSize_wrong, "");
        RETURN_ERROR_IF((*(const BYTE*)src) > max, corruption_detected, "");
        {   U32 const symbol = *(const BYTE*)src;
            U32 const baseline = baseValue[symbol];
            U32 const nbBits = nbAdditionalBits[symbol];
            ZSTD_buildSeqTable_rle(DTableSpace, baseline, nbBits);
        }
        *DTablePtr = DTableSpace;
        return 1;
    case set_basic :
        *DTablePtr = defaultTable;
        return 0;
    case set_repeat:
        RETURN_ERROR_IF(!flagRepeatTable, corruption_detected, "");
        /* prefetch FSE table if used */
        if (ddictIsCold && (nbSeq > 24 /* heuristic */)) {
            const void* const pStart = *DTablePtr;
            size_t const pSize = sizeof(ZSTD_seqSymbol) * (SEQSYMBOL_TABLE_SIZE(maxLog));
            PREFETCH_AREA(pStart, pSize);
        }
        return 0;
    case set_compressed :
        {   unsigned tableLog;
            S16 norm[MaxSeq+1];
            size_t const headerSize = FSE_readNCount(norm, &max, &tableLog, src, srcSize);
            RETURN_ERROR_IF(FSE_isError(headerSize), corruption_detected, "");
            RETURN_ERROR_IF(tableLog > maxLog, corruption_detected, "");
            ZSTD_buildFSETable(DTableSpace, norm, max, baseValue, nbAdditionalBits, tableLog, wksp, wkspSize, bmi2);
            *DTablePtr = DTableSpace;
            return headerSize;
        }
    default :
        assert(0);
        RETURN_ERROR(GENERIC, "impossible");
    }
}

size_t ZSTD_decodeSeqHeaders(ZSTD_DCtx* dctx, int* nbSeqPtr,
                             const void* src, size_t srcSize)
{
    const BYTE* const istart = (const BYTE*)src;
    const BYTE* const iend = istart + srcSize;
    const BYTE* ip = istart;
    int nbSeq;
    DEBUGLOG(5, "ZSTD_decodeSeqHeaders");

    /* check */
    RETURN_ERROR_IF(srcSize < MIN_SEQUENCES_SIZE, srcSize_wrong, "");

    /* SeqHead */
    nbSeq = *ip++;
    if (!nbSeq) {
        *nbSeqPtr=0;
        RETURN_ERROR_IF(srcSize != 1, srcSize_wrong, "");
        return 1;
    }
    if (nbSeq > 0x7F) {
        if (nbSeq == 0xFF) {
            RETURN_ERROR_IF(ip+2 > iend, srcSize_wrong, "");
            nbSeq = MEM_readLE16(ip) + LONGNBSEQ;
            ip+=2;
        } else {
            RETURN_ERROR_IF(ip >= iend, srcSize_wrong, "");
            nbSeq = ((nbSeq-0x80)<<8) + *ip++;
        }
    }
    *nbSeqPtr = nbSeq;

    /* FSE table descriptors */
    RETURN_ERROR_IF(ip+1 > iend, srcSize_wrong, ""); /* minimum possible size: 1 byte for symbol encoding types */
    {   symbolEncodingType_e const LLtype = (symbolEncodingType_e)(*ip >> 6);
        symbolEncodingType_e const OFtype = (symbolEncodingType_e)((*ip >> 4) & 3);
        symbolEncodingType_e const MLtype = (symbolEncodingType_e)((*ip >> 2) & 3);
        ip++;

        /* Build DTables */
        {   size_t const llhSize = ZSTD_buildSeqTable(dctx->entropy.LLTable, &dctx->LLTptr,
                                                      LLtype, MaxLL, LLFSELog,
                                                      ip, iend-ip,
                                                      LL_base, LL_bits,
                                                      LL_defaultDTable, dctx->fseEntropy,
                                                      dctx->ddictIsCold, nbSeq,
                                                      dctx->workspace, sizeof(dctx->workspace),
                                                      dctx->bmi2);
            RETURN_ERROR_IF(ZSTD_isError(llhSize), corruption_detected, "ZSTD_buildSeqTable failed");
            ip += llhSize;
        }

        {   size_t const ofhSize = ZSTD_buildSeqTable(dctx->entropy.OFTable, &dctx->OFTptr,
                                                      OFtype, MaxOff, OffFSELog,
                                                      ip, iend-ip,
                                                      OF_base, OF_bits,
                                                      OF_defaultDTable, dctx->fseEntropy,
                                                      dctx->ddictIsCold, nbSeq,
                                                      dctx->workspace, sizeof(dctx->workspace),
                                                      dctx->bmi2);
            RETURN_ERROR_IF(ZSTD_isError(ofhSize), corruption_detected, "ZSTD_buildSeqTable failed");
            ip += ofhSize;
        }

        {   size_t const mlhSize = ZSTD_buildSeqTable(dctx->entropy.MLTable, &dctx->MLTptr,
                                                      MLtype, MaxML, MLFSELog,
                                                      ip, iend-ip,
                                                      ML_base, ML_bits,
                                                      ML_defaultDTable, dctx->fseEntropy,
                                                      dctx->ddictIsCold, nbSeq,
                                                      dctx->workspace, sizeof(dctx->workspace),
                                                      dctx->bmi2);
            RETURN_ERROR_IF(ZSTD_isError(mlhSize), corruption_detected, "ZSTD_buildSeqTable failed");
            ip += mlhSize;
        }
    }

    return ip-istart;
}


typedef struct {
    size_t litLength;
    size_t matchLength;
    size_t offset;
    const BYTE* match;
} seq_t;

typedef struct {
    size_t state;
    const ZSTD_seqSymbol* table;
} ZSTD_fseState;

typedef struct {
    BIT_DStream_t DStream;
    ZSTD_fseState stateLL;
    ZSTD_fseState stateOffb;
    ZSTD_fseState stateML;
    size_t prevOffset[ZSTD_REP_NUM];
    const BYTE* prefixStart;
    const BYTE* dictEnd;
    size_t pos;
} seqState_t;

/*! ZSTD_overlapCopy8() :
 *  Copies 8 bytes from ip to op and updates op and ip where ip <= op.
 *  If the offset is < 8 then the offset is spread to at least 8 bytes.
 *
 *  Precondition: *ip <= *op
 *  Postcondition: *op - *op >= 8
 */
HINT_INLINE void ZSTD_overlapCopy8(BYTE** op, BYTE const** ip, size_t offset) {
    assert(*ip <= *op);
    if (offset < 8) {
        /* close range match, overlap */
        static const U32 dec32table[] = { 0, 1, 2, 1, 4, 4, 4, 4 };   /* added */
        static const int dec64table[] = { 8, 8, 8, 7, 8, 9,10,11 };   /* subtracted */
        int const sub2 = dec64table[offset];
        (*op)[0] = (*ip)[0];
        (*op)[1] = (*ip)[1];
        (*op)[2] = (*ip)[2];
        (*op)[3] = (*ip)[3];
        *ip += dec32table[offset];
        ZSTD_copy4(*op+4, *ip);
        *ip -= sub2;
    } else {
        ZSTD_copy8(*op, *ip);
    }
    *ip += 8;
    *op += 8;
    assert(*op - *ip >= 8);
}

/*! ZSTD_safecopy() :
 *  Specialized version of memcpy() that is allowed to READ up to WILDCOPY_OVERLENGTH past the input buffer
 *  and write up to 16 bytes past oend_w (op >= oend_w is allowed).
 *  This function is only called in the uncommon case where the sequence is near the end of the block. It
 *  should be fast for a single long sequence, but can be slow for several short sequences.
 *
 *  @param ovtype controls the overlap detection
 *         - ZSTD_no_overlap: The source and destination are guaranteed to be at least WILDCOPY_VECLEN bytes apart.
 *         - ZSTD_overlap_src_before_dst: The src and dst may overlap and may be any distance apart.
 *           The src buffer must be before the dst buffer.
 */
static void ZSTD_safecopy(BYTE* op, BYTE* const oend_w, BYTE const* ip, ptrdiff_t length, ZSTD_overlap_e ovtype) {
    ptrdiff_t const diff = op - ip;
    BYTE* const oend = op + length;

    assert((ovtype == ZSTD_no_overlap && (diff <= -8 || diff >= 8 || op >= oend_w)) ||
           (ovtype == ZSTD_overlap_src_before_dst && diff >= 0));

    if (length < 8) {
        /* Handle short lengths. */
        while (op < oend) *op++ = *ip++;
        return;
    }
    if (ovtype == ZSTD_overlap_src_before_dst) {
        /* Copy 8 bytes and ensure the offset >= 8 when there can be overlap. */
        assert(length >= 8);
        ZSTD_overlapCopy8(&op, &ip, diff);
        assert(op - ip >= 8);
        assert(op <= oend);
    }

    if (oend <= oend_w) {
        /* No risk of overwrite. */
        ZSTD_wildcopy(op, ip, length, ovtype);
        return;
    }
    if (op <= oend_w) {
        /* Wildcopy until we get close to the end. */
        assert(oend > oend_w);
        ZSTD_wildcopy(op, ip, oend_w - op, ovtype);
        ip += oend_w - op;
        op = oend_w;
    }
    /* Handle the leftovers. */
    while (op < oend) *op++ = *ip++;
}

/* ZSTD_execSequenceEnd():
 * This version handles cases that are near the end of the output buffer. It requires
 * more careful checks to make sure there is no overflow. By separating out these hard
 * and unlikely cases, we can speed up the common cases.
 *
 * NOTE: This function needs to be fast for a single long sequence, but doesn't need
 * to be optimized for many small sequences, since those fall into ZSTD_execSequence().
 */
FORCE_NOINLINE
size_t ZSTD_execSequenceEnd(BYTE* op,
                            BYTE* const oend, seq_t sequence,
                            const BYTE** litPtr, const BYTE* const litLimit,
                            const BYTE* const prefixStart, const BYTE* const virtualStart, const BYTE* const dictEnd)
{
    BYTE* const oLitEnd = op + sequence.litLength;
    size_t const sequenceLength = sequence.litLength + sequence.matchLength;
    const BYTE* const iLitEnd = *litPtr + sequence.litLength;
    const BYTE* match = oLitEnd - sequence.offset;
    BYTE* const oend_w = oend - WILDCOPY_OVERLENGTH;

    /* bounds checks : careful of address space overflow in 32-bit mode */
    RETURN_ERROR_IF(sequenceLength > (size_t)(oend - op), dstSize_tooSmall, "last match must fit within dstBuffer");
    RETURN_ERROR_IF(sequence.litLength > (size_t)(litLimit - *litPtr), corruption_detected, "try to read beyond literal buffer");
    assert(op < op + sequenceLength);
    assert(oLitEnd < op + sequenceLength);

    /* copy literals */
    ZSTD_safecopy(op, oend_w, *litPtr, sequence.litLength, ZSTD_no_overlap);
    op = oLitEnd;
    *litPtr = iLitEnd;

    /* copy Match */
    if (sequence.offset > (size_t)(oLitEnd - prefixStart)) {
        /* offset beyond prefix */
        RETURN_ERROR_IF(sequence.offset > (size_t)(oLitEnd - virtualStart), corruption_detected, "");
        match = dictEnd - (prefixStart-match);
        if (match + sequence.matchLength <= dictEnd) {
            ZSTD_memmove(oLitEnd, match, sequence.matchLength);
            return sequenceLength;
        }
        /* span extDict & currentPrefixSegment */
        {   size_t const length1 = dictEnd - match;
            ZSTD_memmove(oLitEnd, match, length1);
            op = oLitEnd + length1;
            sequence.matchLength -= length1;
            match = prefixStart;
    }   }
    ZSTD_safecopy(op, oend_w, match, sequence.matchLength, ZSTD_overlap_src_before_dst);
    return sequenceLength;
}

HINT_INLINE
size_t ZSTD_execSequence(BYTE* op,
                         BYTE* const oend, seq_t sequence,
                         const BYTE** litPtr, const BYTE* const litLimit,
                         const BYTE* const prefixStart, const BYTE* const virtualStart, const BYTE* const dictEnd)
{
    BYTE* const oLitEnd = op + sequence.litLength;
    size_t const sequenceLength = sequence.litLength + sequence.matchLength;
    BYTE* const oMatchEnd = op + sequenceLength;   /* risk : address space overflow (32-bits) */
    BYTE* const oend_w = oend - WILDCOPY_OVERLENGTH;   /* risk : address space underflow on oend=NULL */
    const BYTE* const iLitEnd = *litPtr + sequence.litLength;
    const BYTE* match = oLitEnd - sequence.offset;

    assert(op != NULL /* Precondition */);
    assert(oend_w < oend /* No underflow */);
    /* Handle edge cases in a slow path:
     *   - Read beyond end of literals
     *   - Match end is within WILDCOPY_OVERLIMIT of oend
     *   - 32-bit mode and the match length overflows
     */
    if (UNLIKELY(
            iLitEnd > litLimit ||
            oMatchEnd > oend_w ||
            (MEM_32bits() && (size_t)(oend - op) < sequenceLength + WILDCOPY_OVERLENGTH)))
        return ZSTD_execSequenceEnd(op, oend, sequence, litPtr, litLimit, prefixStart, virtualStart, dictEnd);

    /* Assumptions (everything else goes into ZSTD_execSequenceEnd()) */
    assert(op <= oLitEnd /* No overflow */);
    assert(oLitEnd < oMatchEnd /* Non-zero match & no overflow */);
    assert(oMatchEnd <= oend /* No underflow */);
    assert(iLitEnd <= litLimit /* Literal length is in bounds */);
    assert(oLitEnd <= oend_w /* Can wildcopy literals */);
    assert(oMatchEnd <= oend_w /* Can wildcopy matches */);

    /* Copy Literals:
     * Split out litLength <= 16 since it is nearly always true. +1.6% on gcc-9.
     * We likely don't need the full 32-byte wildcopy.
     */
    assert(WILDCOPY_OVERLENGTH >= 16);
    ZSTD_copy16(op, (*litPtr));
    if (UNLIKELY(sequence.litLength > 16)) {
        ZSTD_wildcopy(op+16, (*litPtr)+16, sequence.litLength-16, ZSTD_no_overlap);
    }
    op = oLitEnd;
    *litPtr = iLitEnd;   /* update for next sequence */

    /* Copy Match */
    if (sequence.offset > (size_t)(oLitEnd - prefixStart)) {
        /* offset beyond prefix -> go into extDict */
        RETURN_ERROR_IF(UNLIKELY(sequence.offset > (size_t)(oLitEnd - virtualStart)), corruption_detected, "");
        match = dictEnd + (match - prefixStart);
        if (match + sequence.matchLength <= dictEnd) {
            ZSTD_memmove(oLitEnd, match, sequence.matchLength);
            return sequenceLength;
        }
        /* span extDict & currentPrefixSegment */
        {   size_t const length1 = dictEnd - match;
            ZSTD_memmove(oLitEnd, match, length1);
            op = oLitEnd + length1;
            sequence.matchLength -= length1;
            match = prefixStart;
    }   }
    /* Match within prefix of 1 or more bytes */
    assert(op <= oMatchEnd);
    assert(oMatchEnd <= oend_w);
    assert(match >= prefixStart);
    assert(sequence.matchLength >= 1);

    /* Nearly all offsets are >= WILDCOPY_VECLEN bytes, which means we can use wildcopy
     * without overlap checking.
     */
    if (LIKELY(sequence.offset >= WILDCOPY_VECLEN)) {
        /* We bet on a full wildcopy for matches, since we expect matches to be
         * longer than literals (in general). In silesia, ~10% of matches are longer
         * than 16 bytes.
         */
        ZSTD_wildcopy(op, match, (ptrdiff_t)sequence.matchLength, ZSTD_no_overlap);
        return sequenceLength;
    }
    assert(sequence.offset < WILDCOPY_VECLEN);

    /* Copy 8 bytes and spread the offset to be >= 8. */
    ZSTD_overlapCopy8(&op, &match, sequence.offset);

    /* If the match length is > 8 bytes, then continue with the wildcopy. */
    if (sequence.matchLength > 8) {
        assert(op < oMatchEnd);
        ZSTD_wildcopy(op, match, (ptrdiff_t)sequence.matchLength-8, ZSTD_overlap_src_before_dst);
    }
    return sequenceLength;
}

static void
ZSTD_initFseState(ZSTD_fseState* DStatePtr, BIT_DStream_t* bitD, const ZSTD_seqSymbol* dt)
{
    const void* ptr = dt;
    const ZSTD_seqSymbol_header* const DTableH = (const ZSTD_seqSymbol_header*)ptr;
    DStatePtr->state = BIT_readBits(bitD, DTableH->tableLog);
    DEBUGLOG(6, "ZSTD_initFseState : val=%u using %u bits",
                (U32)DStatePtr->state, DTableH->tableLog);
    BIT_reloadDStream(bitD);
    DStatePtr->table = dt + 1;
}

FORCE_INLINE_TEMPLATE void
ZSTD_updateFseState(ZSTD_fseState* DStatePtr, BIT_DStream_t* bitD)
{
    ZSTD_seqSymbol const DInfo = DStatePtr->table[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    size_t const lowBits = BIT_readBits(bitD, nbBits);
    DStatePtr->state = DInfo.nextState + lowBits;
}

FORCE_INLINE_TEMPLATE void
ZSTD_updateFseStateWithDInfo(ZSTD_fseState* DStatePtr, BIT_DStream_t* bitD, ZSTD_seqSymbol const DInfo)
{
    U32 const nbBits = DInfo.nbBits;
    size_t const lowBits = BIT_readBits(bitD, nbBits);
    DStatePtr->state = DInfo.nextState + lowBits;
}

/* We need to add at most (ZSTD_WINDOWLOG_MAX_32 - 1) bits to read the maximum
 * offset bits. But we can only read at most (STREAM_ACCUMULATOR_MIN_32 - 1)
 * bits before reloading. This value is the maximum number of bytes we read
 * after reloading when we are decoding long offsets.
 */
#define LONG_OFFSETS_MAX_EXTRA_BITS_32                       \
    (ZSTD_WINDOWLOG_MAX_32 > STREAM_ACCUMULATOR_MIN_32       \
        ? ZSTD_WINDOWLOG_MAX_32 - STREAM_ACCUMULATOR_MIN_32  \
        : 0)

typedef enum { ZSTD_lo_isRegularOffset, ZSTD_lo_isLongOffset=1 } ZSTD_longOffset_e;
typedef enum { ZSTD_p_noPrefetch=0, ZSTD_p_prefetch=1 } ZSTD_prefetch_e;

FORCE_INLINE_TEMPLATE seq_t
ZSTD_decodeSequence(seqState_t* seqState, const ZSTD_longOffset_e longOffsets, const ZSTD_prefetch_e prefetch)
{
    seq_t seq;
    ZSTD_seqSymbol const llDInfo = seqState->stateLL.table[seqState->stateLL.state];
    ZSTD_seqSymbol const mlDInfo = seqState->stateML.table[seqState->stateML.state];
    ZSTD_seqSymbol const ofDInfo = seqState->stateOffb.table[seqState->stateOffb.state];
    U32 const llBase = llDInfo.baseValue;
    U32 const mlBase = mlDInfo.baseValue;
    U32 const ofBase = ofDInfo.baseValue;
    BYTE const llBits = llDInfo.nbAdditionalBits;
    BYTE const mlBits = mlDInfo.nbAdditionalBits;
    BYTE const ofBits = ofDInfo.nbAdditionalBits;
    BYTE const totalBits = llBits+mlBits+ofBits;

    /* sequence */
    {   size_t offset;
        if (ofBits > 1) {
            ZSTD_STATIC_ASSERT(ZSTD_lo_isLongOffset == 1);
            ZSTD_STATIC_ASSERT(LONG_OFFSETS_MAX_EXTRA_BITS_32 == 5);
            assert(ofBits <= MaxOff);
            if (MEM_32bits() && longOffsets && (ofBits >= STREAM_ACCUMULATOR_MIN_32)) {
                U32 const extraBits = ofBits - MIN(ofBits, 32 - seqState->DStream.bitsConsumed);
                offset = ofBase + (BIT_readBitsFast(&seqState->DStream, ofBits - extraBits) << extraBits);
                BIT_reloadDStream(&seqState->DStream);
                if (extraBits) offset += BIT_readBitsFast(&seqState->DStream, extraBits);
                assert(extraBits <= LONG_OFFSETS_MAX_EXTRA_BITS_32);   /* to avoid another reload */
            } else {
                offset = ofBase + BIT_readBitsFast(&seqState->DStream, ofBits/*>0*/);   /* <=  (ZSTD_WINDOWLOG_MAX-1) bits */
                if (MEM_32bits()) BIT_reloadDStream(&seqState->DStream);
            }
            seqState->prevOffset[2] = seqState->prevOffset[1];
            seqState->prevOffset[1] = seqState->prevOffset[0];
            seqState->prevOffset[0] = offset;
        } else {
            U32 const ll0 = (llBase == 0);
            if (LIKELY((ofBits == 0))) {
                if (LIKELY(!ll0))
                    offset = seqState->prevOffset[0];
                else {
                    offset = seqState->prevOffset[1];
                    seqState->prevOffset[1] = seqState->prevOffset[0];
                    seqState->prevOffset[0] = offset;
                }
            } else {
                offset = ofBase + ll0 + BIT_readBitsFast(&seqState->DStream, 1);
                {   size_t temp = (offset==3) ? seqState->prevOffset[0] - 1 : seqState->prevOffset[offset];
                    temp += !temp;   /* 0 is not valid; input is corrupted; force offset to 1 */
                    if (offset != 1) seqState->prevOffset[2] = seqState->prevOffset[1];
                    seqState->prevOffset[1] = seqState->prevOffset[0];
                    seqState->prevOffset[0] = offset = temp;
        }   }   }
        seq.offset = offset;
    }

    seq.matchLength = mlBase;
    if (mlBits > 0)
        seq.matchLength += BIT_readBitsFast(&seqState->DStream, mlBits/*>0*/);

    if (MEM_32bits() && (mlBits+llBits >= STREAM_ACCUMULATOR_MIN_32-LONG_OFFSETS_MAX_EXTRA_BITS_32))
        BIT_reloadDStream(&seqState->DStream);
    if (MEM_64bits() && UNLIKELY(totalBits >= STREAM_ACCUMULATOR_MIN_64-(LLFSELog+MLFSELog+OffFSELog)))
        BIT_reloadDStream(&seqState->DStream);
    /* Ensure there are enough bits to read the rest of data in 64-bit mode. */
    ZSTD_STATIC_ASSERT(16+LLFSELog+MLFSELog+OffFSELog < STREAM_ACCUMULATOR_MIN_64);

    seq.litLength = llBase;
    if (llBits > 0)
        seq.litLength += BIT_readBitsFast(&seqState->DStream, llBits/*>0*/);

    if (MEM_32bits())
        BIT_reloadDStream(&seqState->DStream);

    DEBUGLOG(6, "seq: litL=%u, matchL=%u, offset=%u",
                (U32)seq.litLength, (U32)seq.matchLength, (U32)seq.offset);

    if (prefetch == ZSTD_p_prefetch) {
        size_t const pos = seqState->pos + seq.litLength;
        const BYTE* const matchBase = (seq.offset > pos) ? seqState->dictEnd : seqState->prefixStart;
        seq.match = matchBase + pos - seq.offset;  /* note : this operation can overflow when seq.offset is really too large, which can only happen when input is corrupted.
                                                    * No consequence though : no memory access will occur, offset is only used for prefetching */
        seqState->pos = pos + seq.matchLength;
    }

    /* ANS state update
     * gcc-9.0.0 does 2.5% worse with ZSTD_updateFseStateWithDInfo().
     * clang-9.2.0 does 7% worse with ZSTD_updateFseState().
     * Naturally it seems like ZSTD_updateFseStateWithDInfo() should be the
     * better option, so it is the default for other compilers. But, if you
     * measure that it is worse, please put up a pull request.
     */
    {
#if !defined(__clang__)
        const int kUseUpdateFseState = 1;
#else
        const int kUseUpdateFseState = 0;
#endif
        if (kUseUpdateFseState) {
            ZSTD_updateFseState(&seqState->stateLL, &seqState->DStream);    /* <=  9 bits */
            ZSTD_updateFseState(&seqState->stateML, &seqState->DStream);    /* <=  9 bits */
            if (MEM_32bits()) BIT_reloadDStream(&seqState->DStream);    /* <= 18 bits */
            ZSTD_updateFseState(&seqState->stateOffb, &seqState->DStream);  /* <=  8 bits */
        } else {
            ZSTD_updateFseStateWithDInfo(&seqState->stateLL, &seqState->DStream, llDInfo);    /* <=  9 bits */
            ZSTD_updateFseStateWithDInfo(&seqState->stateML, &seqState->DStream, mlDInfo);    /* <=  9 bits */
            if (MEM_32bits()) BIT_reloadDStream(&seqState->DStream);    /* <= 18 bits */
            ZSTD_updateFseStateWithDInfo(&seqState->stateOffb, &seqState->DStream, ofDInfo);  /* <=  8 bits */
        }
    }

    return seq;
}

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
MEM_STATIC int ZSTD_dictionaryIsActive(ZSTD_DCtx const* dctx, BYTE const* prefixStart, BYTE const* oLitEnd)
{
    size_t const windowSize = dctx->fParams.windowSize;
    /* No dictionary used. */
    if (dctx->dictContentEndForFuzzing == NULL) return 0;
    /* Dictionary is our prefix. */
    if (prefixStart == dctx->dictContentBeginForFuzzing) return 1;
    /* Dictionary is not our ext-dict. */
    if (dctx->dictEnd != dctx->dictContentEndForFuzzing) return 0;
    /* Dictionary is not within our window size. */
    if ((size_t)(oLitEnd - prefixStart) >= windowSize) return 0;
    /* Dictionary is active. */
    return 1;
}

MEM_STATIC void ZSTD_assertValidSequence(
        ZSTD_DCtx const* dctx,
        BYTE const* op, BYTE const* oend,
        seq_t const seq,
        BYTE const* prefixStart, BYTE const* virtualStart)
{
#if DEBUGLEVEL >= 1
    size_t const windowSize = dctx->fParams.windowSize;
    size_t const sequenceSize = seq.litLength + seq.matchLength;
    BYTE const* const oLitEnd = op + seq.litLength;
    DEBUGLOG(6, "Checking sequence: litL=%u matchL=%u offset=%u",
            (U32)seq.litLength, (U32)seq.matchLength, (U32)seq.offset);
    assert(op <= oend);
    assert((size_t)(oend - op) >= sequenceSize);
    assert(sequenceSize <= ZSTD_BLOCKSIZE_MAX);
    if (ZSTD_dictionaryIsActive(dctx, prefixStart, oLitEnd)) {
        size_t const dictSize = (size_t)((char const*)dctx->dictContentEndForFuzzing - (char const*)dctx->dictContentBeginForFuzzing);
        /* Offset must be within the dictionary. */
        assert(seq.offset <= (size_t)(oLitEnd - virtualStart));
        assert(seq.offset <= windowSize + dictSize);
    } else {
        /* Offset must be within our window. */
        assert(seq.offset <= windowSize);
    }
#else
    (void)dctx, (void)op, (void)oend, (void)seq, (void)prefixStart, (void)virtualStart;
#endif
}
#endif

#ifndef ZSTD_FORCE_DECOMPRESS_SEQUENCES_LONG
FORCE_INLINE_TEMPLATE size_t
DONT_VECTORIZE
ZSTD_decompressSequences_body( ZSTD_DCtx* dctx,
                               void* dst, size_t maxDstSize,
                         const void* seqStart, size_t seqSize, int nbSeq,
                         const ZSTD_longOffset_e isLongOffset,
                         const int frame)
{
    const BYTE* ip = (const BYTE*)seqStart;
    const BYTE* const iend = ip + seqSize;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + maxDstSize;
    BYTE* op = ostart;
    const BYTE* litPtr = dctx->litPtr;
    const BYTE* const litEnd = litPtr + dctx->litSize;
    const BYTE* const prefixStart = (const BYTE*) (dctx->prefixStart);
    const BYTE* const vBase = (const BYTE*) (dctx->virtualStart);
    const BYTE* const dictEnd = (const BYTE*) (dctx->dictEnd);
    DEBUGLOG(5, "ZSTD_decompressSequences_body");
    (void)frame;

    /* Regen sequences */
    if (nbSeq) {
        seqState_t seqState;
        size_t error = 0;
        dctx->fseEntropy = 1;
        { U32 i; for (i=0; i<ZSTD_REP_NUM; i++) seqState.prevOffset[i] = dctx->entropy.rep[i]; }
        RETURN_ERROR_IF(
            ERR_isError(BIT_initDStream(&seqState.DStream, ip, iend-ip)),
            corruption_detected, "");
        ZSTD_initFseState(&seqState.stateLL, &seqState.DStream, dctx->LLTptr);
        ZSTD_initFseState(&seqState.stateOffb, &seqState.DStream, dctx->OFTptr);
        ZSTD_initFseState(&seqState.stateML, &seqState.DStream, dctx->MLTptr);
        assert(dst != NULL);

        ZSTD_STATIC_ASSERT(
                BIT_DStream_unfinished < BIT_DStream_completed &&
                BIT_DStream_endOfBuffer < BIT_DStream_completed &&
                BIT_DStream_completed < BIT_DStream_overflow);

#if defined(__x86_64__)
        /* Align the decompression loop to 32 + 16 bytes.
         *
         * zstd compiled with gcc-9 on an Intel i9-9900k shows 10% decompression
         * speed swings based on the alignment of the decompression loop. This
         * performance swing is caused by parts of the decompression loop falling
         * out of the DSB. The entire decompression loop should fit in the DSB,
         * when it can't we get much worse performance. You can measure if you've
         * hit the good case or the bad case with this perf command for some
         * compressed file test.zst:
         *
         *   perf stat -e cycles -e instructions -e idq.all_dsb_cycles_any_uops \
         *             -e idq.all_mite_cycles_any_uops -- ./zstd -tq test.zst
         *
         * If you see most cycles served out of the MITE you've hit the bad case.
         * If you see most cycles served out of the DSB you've hit the good case.
         * If it is pretty even then you may be in an okay case.
         *
         * I've been able to reproduce this issue on the following CPUs:
         *   - Kabylake: Macbook Pro (15-inch, 2019) 2.4 GHz Intel Core i9
         *               Use Instruments->Counters to get DSB/MITE cycles.
         *               I never got performance swings, but I was able to
         *               go from the good case of mostly DSB to half of the
         *               cycles served from MITE.
         *   - Coffeelake: Intel i9-9900k
         *
         * I haven't been able to reproduce the instability or DSB misses on any
         * of the following CPUS:
         *   - Haswell
         *   - Broadwell: Intel(R) Xeon(R) CPU E5-2680 v4 @ 2.40GH
         *   - Skylake
         *
         * If you are seeing performance stability this script can help test.
         * It tests on 4 commits in zstd where I saw performance change.
         *
         *   https://gist.github.com/terrelln/9889fc06a423fd5ca6e99351564473f4
         */
        __asm__(".p2align 5");
        __asm__("nop");
        __asm__(".p2align 4");
#endif
        for ( ; ; ) {
            seq_t const sequence = ZSTD_decodeSequence(&seqState, isLongOffset, ZSTD_p_noPrefetch);
            size_t const oneSeqSize = ZSTD_execSequence(op, oend, sequence, &litPtr, litEnd, prefixStart, vBase, dictEnd);
#if defined(FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION) && defined(FUZZING_ASSERT_VALID_SEQUENCE)
            assert(!ZSTD_isError(oneSeqSize));
            if (frame) ZSTD_assertValidSequence(dctx, op, oend, sequence, prefixStart, vBase);
#endif
            DEBUGLOG(6, "regenerated sequence size : %u", (U32)oneSeqSize);
            BIT_reloadDStream(&(seqState.DStream));
            op += oneSeqSize;
            /* gcc and clang both don't like early returns in this loop.
             * Instead break and check for an error at the end of the loop.
             */
            if (UNLIKELY(ZSTD_isError(oneSeqSize))) {
                error = oneSeqSize;
                break;
            }
            if (UNLIKELY(!--nbSeq)) break;
        }

        /* check if reached exact end */
        DEBUGLOG(5, "ZSTD_decompressSequences_body: after decode loop, remaining nbSeq : %i", nbSeq);
        if (ZSTD_isError(error)) return error;
        RETURN_ERROR_IF(nbSeq, corruption_detected, "");
        RETURN_ERROR_IF(BIT_reloadDStream(&seqState.DStream) < BIT_DStream_completed, corruption_detected, "");
        /* save reps for next block */
        { U32 i; for (i=0; i<ZSTD_REP_NUM; i++) dctx->entropy.rep[i] = (U32)(seqState.prevOffset[i]); }
    }

    /* last literal segment */
    {   size_t const lastLLSize = litEnd - litPtr;
        RETURN_ERROR_IF(lastLLSize > (size_t)(oend-op), dstSize_tooSmall, "");
        if (op != NULL) {
            ZSTD_memcpy(op, litPtr, lastLLSize);
            op += lastLLSize;
        }
    }

    return op-ostart;
}

static size_t
ZSTD_decompressSequences_default(ZSTD_DCtx* dctx,
                                 void* dst, size_t maxDstSize,
                           const void* seqStart, size_t seqSize, int nbSeq,
                           const ZSTD_longOffset_e isLongOffset,
                           const int frame)
{
    return ZSTD_decompressSequences_body(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset, frame);
}
#endif /* ZSTD_FORCE_DECOMPRESS_SEQUENCES_LONG */

#ifndef ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT
FORCE_INLINE_TEMPLATE size_t
ZSTD_decompressSequencesLong_body(
                               ZSTD_DCtx* dctx,
                               void* dst, size_t maxDstSize,
                         const void* seqStart, size_t seqSize, int nbSeq,
                         const ZSTD_longOffset_e isLongOffset,
                         const int frame)
{
    const BYTE* ip = (const BYTE*)seqStart;
    const BYTE* const iend = ip + seqSize;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* const oend = ostart + maxDstSize;
    BYTE* op = ostart;
    const BYTE* litPtr = dctx->litPtr;
    const BYTE* const litEnd = litPtr + dctx->litSize;
    const BYTE* const prefixStart = (const BYTE*) (dctx->prefixStart);
    const BYTE* const dictStart = (const BYTE*) (dctx->virtualStart);
    const BYTE* const dictEnd = (const BYTE*) (dctx->dictEnd);
    (void)frame;

    /* Regen sequences */
    if (nbSeq) {
#define STORED_SEQS 4
#define STORED_SEQS_MASK (STORED_SEQS-1)
#define ADVANCED_SEQS 4
        seq_t sequences[STORED_SEQS];
        int const seqAdvance = MIN(nbSeq, ADVANCED_SEQS);
        seqState_t seqState;
        int seqNb;
        dctx->fseEntropy = 1;
        { int i; for (i=0; i<ZSTD_REP_NUM; i++) seqState.prevOffset[i] = dctx->entropy.rep[i]; }
        seqState.prefixStart = prefixStart;
        seqState.pos = (size_t)(op-prefixStart);
        seqState.dictEnd = dictEnd;
        assert(dst != NULL);
        assert(iend >= ip);
        RETURN_ERROR_IF(
            ERR_isError(BIT_initDStream(&seqState.DStream, ip, iend-ip)),
            corruption_detected, "");
        ZSTD_initFseState(&seqState.stateLL, &seqState.DStream, dctx->LLTptr);
        ZSTD_initFseState(&seqState.stateOffb, &seqState.DStream, dctx->OFTptr);
        ZSTD_initFseState(&seqState.stateML, &seqState.DStream, dctx->MLTptr);

        /* prepare in advance */
        for (seqNb=0; (BIT_reloadDStream(&seqState.DStream) <= BIT_DStream_completed) && (seqNb<seqAdvance); seqNb++) {
            sequences[seqNb] = ZSTD_decodeSequence(&seqState, isLongOffset, ZSTD_p_prefetch);
            PREFETCH_L1(sequences[seqNb].match); PREFETCH_L1(sequences[seqNb].match + sequences[seqNb].matchLength - 1); /* note : it's safe to invoke PREFETCH() on any memory address, including invalid ones */
        }
        RETURN_ERROR_IF(seqNb<seqAdvance, corruption_detected, "");

        /* decode and decompress */
        for ( ; (BIT_reloadDStream(&(seqState.DStream)) <= BIT_DStream_completed) && (seqNb<nbSeq) ; seqNb++) {
            seq_t const sequence = ZSTD_decodeSequence(&seqState, isLongOffset, ZSTD_p_prefetch);
            size_t const oneSeqSize = ZSTD_execSequence(op, oend, sequences[(seqNb-ADVANCED_SEQS) & STORED_SEQS_MASK], &litPtr, litEnd, prefixStart, dictStart, dictEnd);
#if defined(FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION) && defined(FUZZING_ASSERT_VALID_SEQUENCE)
            assert(!ZSTD_isError(oneSeqSize));
            if (frame) ZSTD_assertValidSequence(dctx, op, oend, sequences[(seqNb-ADVANCED_SEQS) & STORED_SEQS_MASK], prefixStart, dictStart);
#endif
            if (ZSTD_isError(oneSeqSize)) return oneSeqSize;
            PREFETCH_L1(sequence.match); PREFETCH_L1(sequence.match + sequence.matchLength - 1); /* note : it's safe to invoke PREFETCH() on any memory address, including invalid ones */
            sequences[seqNb & STORED_SEQS_MASK] = sequence;
            op += oneSeqSize;
        }
        RETURN_ERROR_IF(seqNb<nbSeq, corruption_detected, "");

        /* finish queue */
        seqNb -= seqAdvance;
        for ( ; seqNb<nbSeq ; seqNb++) {
            size_t const oneSeqSize = ZSTD_execSequence(op, oend, sequences[seqNb&STORED_SEQS_MASK], &litPtr, litEnd, prefixStart, dictStart, dictEnd);
#if defined(FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION) && defined(FUZZING_ASSERT_VALID_SEQUENCE)
            assert(!ZSTD_isError(oneSeqSize));
            if (frame) ZSTD_assertValidSequence(dctx, op, oend, sequences[seqNb&STORED_SEQS_MASK], prefixStart, dictStart);
#endif
            if (ZSTD_isError(oneSeqSize)) return oneSeqSize;
            op += oneSeqSize;
        }

        /* save reps for next block */
        { U32 i; for (i=0; i<ZSTD_REP_NUM; i++) dctx->entropy.rep[i] = (U32)(seqState.prevOffset[i]); }
    }

    /* last literal segment */
    {   size_t const lastLLSize = litEnd - litPtr;
        RETURN_ERROR_IF(lastLLSize > (size_t)(oend-op), dstSize_tooSmall, "");
        if (op != NULL) {
            ZSTD_memcpy(op, litPtr, lastLLSize);
            op += lastLLSize;
        }
    }

    return op-ostart;
}

static size_t
ZSTD_decompressSequencesLong_default(ZSTD_DCtx* dctx,
                                 void* dst, size_t maxDstSize,
                           const void* seqStart, size_t seqSize, int nbSeq,
                           const ZSTD_longOffset_e isLongOffset,
                           const int frame)
{
    return ZSTD_decompressSequencesLong_body(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset, frame);
}
#endif /* ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT */



#if DYNAMIC_BMI2

#ifndef ZSTD_FORCE_DECOMPRESS_SEQUENCES_LONG
static TARGET_ATTRIBUTE("bmi2") size_t
DONT_VECTORIZE
ZSTD_decompressSequences_bmi2(ZSTD_DCtx* dctx,
                                 void* dst, size_t maxDstSize,
                           const void* seqStart, size_t seqSize, int nbSeq,
                           const ZSTD_longOffset_e isLongOffset,
                           const int frame)
{
    return ZSTD_decompressSequences_body(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset, frame);
}
#endif /* ZSTD_FORCE_DECOMPRESS_SEQUENCES_LONG */

#ifndef ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT
static TARGET_ATTRIBUTE("bmi2") size_t
ZSTD_decompressSequencesLong_bmi2(ZSTD_DCtx* dctx,
                                 void* dst, size_t maxDstSize,
                           const void* seqStart, size_t seqSize, int nbSeq,
                           const ZSTD_longOffset_e isLongOffset,
                           const int frame)
{
    return ZSTD_decompressSequencesLong_body(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset, frame);
}
#endif /* ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT */

#endif /* DYNAMIC_BMI2 */

typedef size_t (*ZSTD_decompressSequences_t)(
                            ZSTD_DCtx* dctx,
                            void* dst, size_t maxDstSize,
                            const void* seqStart, size_t seqSize, int nbSeq,
                            const ZSTD_longOffset_e isLongOffset,
                            const int frame);

#ifndef ZSTD_FORCE_DECOMPRESS_SEQUENCES_LONG
static size_t
ZSTD_decompressSequences(ZSTD_DCtx* dctx, void* dst, size_t maxDstSize,
                   const void* seqStart, size_t seqSize, int nbSeq,
                   const ZSTD_longOffset_e isLongOffset,
                   const int frame)
{
    DEBUGLOG(5, "ZSTD_decompressSequences");
#if DYNAMIC_BMI2
    if (dctx->bmi2) {
        return ZSTD_decompressSequences_bmi2(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset, frame);
    }
#endif
  return ZSTD_decompressSequences_default(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset, frame);
}
#endif /* ZSTD_FORCE_DECOMPRESS_SEQUENCES_LONG */


#ifndef ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT
/* ZSTD_decompressSequencesLong() :
 * decompression function triggered when a minimum share of offsets is considered "long",
 * aka out of cache.
 * note : "long" definition seems overloaded here, sometimes meaning "wider than bitstream register", and sometimes meaning "farther than memory cache distance".
 * This function will try to mitigate main memory latency through the use of prefetching */
static size_t
ZSTD_decompressSequencesLong(ZSTD_DCtx* dctx,
                             void* dst, size_t maxDstSize,
                             const void* seqStart, size_t seqSize, int nbSeq,
                             const ZSTD_longOffset_e isLongOffset,
                             const int frame)
{
    DEBUGLOG(5, "ZSTD_decompressSequencesLong");
#if DYNAMIC_BMI2
    if (dctx->bmi2) {
        return ZSTD_decompressSequencesLong_bmi2(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset, frame);
    }
#endif
  return ZSTD_decompressSequencesLong_default(dctx, dst, maxDstSize, seqStart, seqSize, nbSeq, isLongOffset, frame);
}
#endif /* ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT */



#if !defined(ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT) && \
    !defined(ZSTD_FORCE_DECOMPRESS_SEQUENCES_LONG)
/* ZSTD_getLongOffsetsShare() :
 * condition : offTable must be valid
 * @return : "share" of long offsets (arbitrarily defined as > (1<<23))
 *           compared to maximum possible of (1<<OffFSELog) */
static unsigned
ZSTD_getLongOffsetsShare(const ZSTD_seqSymbol* offTable)
{
    const void* ptr = offTable;
    U32 const tableLog = ((const ZSTD_seqSymbol_header*)ptr)[0].tableLog;
    const ZSTD_seqSymbol* table = offTable + 1;
    U32 const max = 1 << tableLog;
    U32 u, total = 0;
    DEBUGLOG(5, "ZSTD_getLongOffsetsShare: (tableLog=%u)", tableLog);

    assert(max <= (1 << OffFSELog));  /* max not too large */
    for (u=0; u<max; u++) {
        if (table[u].nbAdditionalBits > 22) total += 1;
    }

    assert(tableLog <= OffFSELog);
    total <<= (OffFSELog - tableLog);  /* scale to OffFSELog */

    return total;
}
#endif

size_t
ZSTD_decompressBlock_internal(ZSTD_DCtx* dctx,
                              void* dst, size_t dstCapacity,
                        const void* src, size_t srcSize, const int frame)
{   /* blockType == blockCompressed */
    const BYTE* ip = (const BYTE*)src;
    /* isLongOffset must be true if there are long offsets.
     * Offsets are long if they are larger than 2^STREAM_ACCUMULATOR_MIN.
     * We don't expect that to be the case in 64-bit mode.
     * In block mode, window size is not known, so we have to be conservative.
     * (note: but it could be evaluated from current-lowLimit)
     */
    ZSTD_longOffset_e const isLongOffset = (ZSTD_longOffset_e)(MEM_32bits() && (!frame || (dctx->fParams.windowSize > (1ULL << STREAM_ACCUMULATOR_MIN))));
    DEBUGLOG(5, "ZSTD_decompressBlock_internal (size : %u)", (U32)srcSize);

    RETURN_ERROR_IF(srcSize >= ZSTD_BLOCKSIZE_MAX, srcSize_wrong, "");

    /* Decode literals section */
    {   size_t const litCSize = ZSTD_decodeLiteralsBlock(dctx, src, srcSize);
        DEBUGLOG(5, "ZSTD_decodeLiteralsBlock : %u", (U32)litCSize);
        if (ZSTD_isError(litCSize)) return litCSize;
        ip += litCSize;
        srcSize -= litCSize;
    }

    /* Build Decoding Tables */
    {
        /* These macros control at build-time which decompressor implementation
         * we use. If neither is defined, we do some inspection and dispatch at
         * runtime.
         */
#if !defined(ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT) && \
    !defined(ZSTD_FORCE_DECOMPRESS_SEQUENCES_LONG)
        int usePrefetchDecoder = dctx->ddictIsCold;
#endif
        int nbSeq;
        size_t const seqHSize = ZSTD_decodeSeqHeaders(dctx, &nbSeq, ip, srcSize);
        if (ZSTD_isError(seqHSize)) return seqHSize;
        ip += seqHSize;
        srcSize -= seqHSize;

        RETURN_ERROR_IF(dst == NULL && nbSeq > 0, dstSize_tooSmall, "NULL not handled");

#if !defined(ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT) && \
    !defined(ZSTD_FORCE_DECOMPRESS_SEQUENCES_LONG)
        if ( !usePrefetchDecoder
          && (!frame || (dctx->fParams.windowSize > (1<<24)))
          && (nbSeq>ADVANCED_SEQS) ) {  /* could probably use a larger nbSeq limit */
            U32 const shareLongOffsets = ZSTD_getLongOffsetsShare(dctx->OFTptr);
            U32 const minShare = MEM_64bits() ? 7 : 20; /* heuristic values, correspond to 2.73% and 7.81% */
            usePrefetchDecoder = (shareLongOffsets >= minShare);
        }
#endif

        dctx->ddictIsCold = 0;

#if !defined(ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT) && \
    !defined(ZSTD_FORCE_DECOMPRESS_SEQUENCES_LONG)
        if (usePrefetchDecoder)
#endif
#ifndef ZSTD_FORCE_DECOMPRESS_SEQUENCES_SHORT
            return ZSTD_decompressSequencesLong(dctx, dst, dstCapacity, ip, srcSize, nbSeq, isLongOffset, frame);
#endif

#ifndef ZSTD_FORCE_DECOMPRESS_SEQUENCES_LONG
        /* else */
        return ZSTD_decompressSequences(dctx, dst, dstCapacity, ip, srcSize, nbSeq, isLongOffset, frame);
#endif
    }
}


void ZSTD_checkContinuity(ZSTD_DCtx* dctx, const void* dst, size_t dstSize)
{
    if (dst != dctx->previousDstEnd && dstSize > 0) {   /* not contiguous */
        dctx->dictEnd = dctx->previousDstEnd;
        dctx->virtualStart = (const char*)dst - ((const char*)(dctx->previousDstEnd) - (const char*)(dctx->prefixStart));
        dctx->prefixStart = dst;
        dctx->previousDstEnd = dst;
    }
}


size_t ZSTD_decompressBlock(ZSTD_DCtx* dctx,
                            void* dst, size_t dstCapacity,
                      const void* src, size_t srcSize)
{
    size_t dSize;
    ZSTD_checkContinuity(dctx, dst, dstCapacity);
    dSize = ZSTD_decompressBlock_internal(dctx, dst, dstCapacity, src, srcSize, /* frame */ 0);
    dctx->previousDstEnd = (char*)dst + dSize;
    return dSize;
}
