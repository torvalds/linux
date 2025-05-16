// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

 /*-*************************************
 *  Dependencies
 ***************************************/
#include "zstd_compress_literals.h"


/* **************************************************************
*  Debug Traces
****************************************************************/
#if DEBUGLEVEL >= 2

static size_t showHexa(const void* src, size_t srcSize)
{
    const BYTE* const ip = (const BYTE*)src;
    size_t u;
    for (u=0; u<srcSize; u++) {
        RAWLOG(5, " %02X", ip[u]); (void)ip;
    }
    RAWLOG(5, " \n");
    return srcSize;
}

#endif


/* **************************************************************
*  Literals compression - special cases
****************************************************************/
size_t ZSTD_noCompressLiterals (void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    BYTE* const ostart = (BYTE*)dst;
    U32   const flSize = 1 + (srcSize>31) + (srcSize>4095);

    DEBUGLOG(5, "ZSTD_noCompressLiterals: srcSize=%zu, dstCapacity=%zu", srcSize, dstCapacity);

    RETURN_ERROR_IF(srcSize + flSize > dstCapacity, dstSize_tooSmall, "");

    switch(flSize)
    {
        case 1: /* 2 - 1 - 5 */
            ostart[0] = (BYTE)((U32)set_basic + (srcSize<<3));
            break;
        case 2: /* 2 - 2 - 12 */
            MEM_writeLE16(ostart, (U16)((U32)set_basic + (1<<2) + (srcSize<<4)));
            break;
        case 3: /* 2 - 2 - 20 */
            MEM_writeLE32(ostart, (U32)((U32)set_basic + (3<<2) + (srcSize<<4)));
            break;
        default:   /* not necessary : flSize is {1,2,3} */
            assert(0);
    }

    ZSTD_memcpy(ostart + flSize, src, srcSize);
    DEBUGLOG(5, "Raw (uncompressed) literals: %u -> %u", (U32)srcSize, (U32)(srcSize + flSize));
    return srcSize + flSize;
}

static int allBytesIdentical(const void* src, size_t srcSize)
{
    assert(srcSize >= 1);
    assert(src != NULL);
    {   const BYTE b = ((const BYTE*)src)[0];
        size_t p;
        for (p=1; p<srcSize; p++) {
            if (((const BYTE*)src)[p] != b) return 0;
        }
        return 1;
    }
}

size_t ZSTD_compressRleLiteralsBlock (void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    BYTE* const ostart = (BYTE*)dst;
    U32   const flSize = 1 + (srcSize>31) + (srcSize>4095);

    assert(dstCapacity >= 4); (void)dstCapacity;
    assert(allBytesIdentical(src, srcSize));

    switch(flSize)
    {
        case 1: /* 2 - 1 - 5 */
            ostart[0] = (BYTE)((U32)set_rle + (srcSize<<3));
            break;
        case 2: /* 2 - 2 - 12 */
            MEM_writeLE16(ostart, (U16)((U32)set_rle + (1<<2) + (srcSize<<4)));
            break;
        case 3: /* 2 - 2 - 20 */
            MEM_writeLE32(ostart, (U32)((U32)set_rle + (3<<2) + (srcSize<<4)));
            break;
        default:   /* not necessary : flSize is {1,2,3} */
            assert(0);
    }

    ostart[flSize] = *(const BYTE*)src;
    DEBUGLOG(5, "RLE : Repeated Literal (%02X: %u times) -> %u bytes encoded", ((const BYTE*)src)[0], (U32)srcSize, (U32)flSize + 1);
    return flSize+1;
}

/* ZSTD_minLiteralsToCompress() :
 * returns minimal amount of literals
 * for literal compression to even be attempted.
 * Minimum is made tighter as compression strategy increases.
 */
static size_t
ZSTD_minLiteralsToCompress(ZSTD_strategy strategy, HUF_repeat huf_repeat)
{
    assert((int)strategy >= 0);
    assert((int)strategy <= 9);
    /* btultra2 : min 8 bytes;
     * then 2x larger for each successive compression strategy
     * max threshold 64 bytes */
    {   int const shift = MIN(9-(int)strategy, 3);
        size_t const mintc = (huf_repeat == HUF_repeat_valid) ? 6 : (size_t)8 << shift;
        DEBUGLOG(7, "minLiteralsToCompress = %zu", mintc);
        return mintc;
    }
}

size_t ZSTD_compressLiterals (
                  void* dst, size_t dstCapacity,
            const void* src, size_t srcSize,
                  void* entropyWorkspace, size_t entropyWorkspaceSize,
            const ZSTD_hufCTables_t* prevHuf,
                  ZSTD_hufCTables_t* nextHuf,
                  ZSTD_strategy strategy,
                  int disableLiteralCompression,
                  int suspectUncompressible,
                  int bmi2)
{
    size_t const lhSize = 3 + (srcSize >= 1 KB) + (srcSize >= 16 KB);
    BYTE*  const ostart = (BYTE*)dst;
    U32 singleStream = srcSize < 256;
    SymbolEncodingType_e hType = set_compressed;
    size_t cLitSize;

    DEBUGLOG(5,"ZSTD_compressLiterals (disableLiteralCompression=%i, srcSize=%u, dstCapacity=%zu)",
                disableLiteralCompression, (U32)srcSize, dstCapacity);

    DEBUGLOG(6, "Completed literals listing (%zu bytes)", showHexa(src, srcSize));

    /* Prepare nextEntropy assuming reusing the existing table */
    ZSTD_memcpy(nextHuf, prevHuf, sizeof(*prevHuf));

    if (disableLiteralCompression)
        return ZSTD_noCompressLiterals(dst, dstCapacity, src, srcSize);

    /* if too small, don't even attempt compression (speed opt) */
    if (srcSize < ZSTD_minLiteralsToCompress(strategy, prevHuf->repeatMode))
        return ZSTD_noCompressLiterals(dst, dstCapacity, src, srcSize);

    RETURN_ERROR_IF(dstCapacity < lhSize+1, dstSize_tooSmall, "not enough space for compression");
    {   HUF_repeat repeat = prevHuf->repeatMode;
        int const flags = 0
            | (bmi2 ? HUF_flags_bmi2 : 0)
            | (strategy < ZSTD_lazy && srcSize <= 1024 ? HUF_flags_preferRepeat : 0)
            | (strategy >= HUF_OPTIMAL_DEPTH_THRESHOLD ? HUF_flags_optimalDepth : 0)
            | (suspectUncompressible ? HUF_flags_suspectUncompressible : 0);

        typedef size_t (*huf_compress_f)(void*, size_t, const void*, size_t, unsigned, unsigned, void*, size_t, HUF_CElt*, HUF_repeat*, int);
        huf_compress_f huf_compress;
        if (repeat == HUF_repeat_valid && lhSize == 3) singleStream = 1;
        huf_compress = singleStream ? HUF_compress1X_repeat : HUF_compress4X_repeat;
        cLitSize = huf_compress(ostart+lhSize, dstCapacity-lhSize,
                                src, srcSize,
                                HUF_SYMBOLVALUE_MAX, LitHufLog,
                                entropyWorkspace, entropyWorkspaceSize,
                                (HUF_CElt*)nextHuf->CTable,
                                &repeat, flags);
        DEBUGLOG(5, "%zu literals compressed into %zu bytes (before header)", srcSize, cLitSize);
        if (repeat != HUF_repeat_none) {
            /* reused the existing table */
            DEBUGLOG(5, "reusing statistics from previous huffman block");
            hType = set_repeat;
        }
    }

    {   size_t const minGain = ZSTD_minGain(srcSize, strategy);
        if ((cLitSize==0) || (cLitSize >= srcSize - minGain) || ERR_isError(cLitSize)) {
            ZSTD_memcpy(nextHuf, prevHuf, sizeof(*prevHuf));
            return ZSTD_noCompressLiterals(dst, dstCapacity, src, srcSize);
    }   }
    if (cLitSize==1) {
        /* A return value of 1 signals that the alphabet consists of a single symbol.
         * However, in some rare circumstances, it could be the compressed size (a single byte).
         * For that outcome to have a chance to happen, it's necessary that `srcSize < 8`.
         * (it's also necessary to not generate statistics).
         * Therefore, in such a case, actively check that all bytes are identical. */
        if ((srcSize >= 8) || allBytesIdentical(src, srcSize)) {
            ZSTD_memcpy(nextHuf, prevHuf, sizeof(*prevHuf));
            return ZSTD_compressRleLiteralsBlock(dst, dstCapacity, src, srcSize);
    }   }

    if (hType == set_compressed) {
        /* using a newly constructed table */
        nextHuf->repeatMode = HUF_repeat_check;
    }

    /* Build header */
    switch(lhSize)
    {
    case 3: /* 2 - 2 - 10 - 10 */
        if (!singleStream) assert(srcSize >= MIN_LITERALS_FOR_4_STREAMS);
        {   U32 const lhc = hType + ((U32)(!singleStream) << 2) + ((U32)srcSize<<4) + ((U32)cLitSize<<14);
            MEM_writeLE24(ostart, lhc);
            break;
        }
    case 4: /* 2 - 2 - 14 - 14 */
        assert(srcSize >= MIN_LITERALS_FOR_4_STREAMS);
        {   U32 const lhc = hType + (2 << 2) + ((U32)srcSize<<4) + ((U32)cLitSize<<18);
            MEM_writeLE32(ostart, lhc);
            break;
        }
    case 5: /* 2 - 2 - 18 - 18 */
        assert(srcSize >= MIN_LITERALS_FOR_4_STREAMS);
        {   U32 const lhc = hType + (3 << 2) + ((U32)srcSize<<4) + ((U32)cLitSize<<22);
            MEM_writeLE32(ostart, lhc);
            ostart[4] = (BYTE)(cLitSize >> 10);
            break;
        }
    default:  /* not possible : lhSize is {3,4,5} */
        assert(0);
    }
    DEBUGLOG(5, "Compressed literals: %u -> %u", (U32)srcSize, (U32)(lhSize+cLitSize));
    return lhSize+cLitSize;
}
