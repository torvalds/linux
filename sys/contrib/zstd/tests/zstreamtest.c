/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/*-************************************
 *  Compiler specific
 **************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  define _CRT_SECURE_NO_WARNINGS   /* fgets */
#  pragma warning(disable : 4127)   /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4146)   /* disable: C4146: minus unsigned expression */
#endif


/*-************************************
 *  Includes
 **************************************/
#include <stdlib.h>       /* free */
#include <stdio.h>        /* fgets, sscanf */
#include <string.h>       /* strcmp */
#include <assert.h>       /* assert */
#include "mem.h"
#define ZSTD_STATIC_LINKING_ONLY  /* ZSTD_maxCLevel, ZSTD_customMem, ZSTD_getDictID_fromFrame */
#include "zstd.h"         /* ZSTD_compressBound */
#include "zstd_errors.h"  /* ZSTD_error_srcSize_wrong */
#include "zstdmt_compress.h"
#include "zdict.h"        /* ZDICT_trainFromBuffer */
#include "datagen.h"      /* RDG_genBuffer */
#define XXH_STATIC_LINKING_ONLY   /* XXH64_state_t */
#include "xxhash.h"       /* XXH64_* */
#include "seqgen.h"
#include "util.h"


/*-************************************
 *  Constants
 **************************************/
#define KB *(1U<<10)
#define MB *(1U<<20)
#define GB *(1U<<30)

static const int nbTestsDefault = 10000;
static const U32 g_cLevelMax_smallTests = 10;
#define COMPRESSIBLE_NOISE_LENGTH (10 MB)
#define FUZ_COMPRESSIBILITY_DEFAULT 50
static const U32 prime32 = 2654435761U;


/*-************************************
 *  Display Macros
 **************************************/
#define DISPLAY(...)          fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...)  if (g_displayLevel>=l) {                     \
                                  DISPLAY(__VA_ARGS__);                    \
                                  if (g_displayLevel>=4) fflush(stderr); }
static U32 g_displayLevel = 2;

static const U64 g_refreshRate = SEC_TO_MICRO / 6;
static UTIL_time_t g_displayClock = UTIL_TIME_INITIALIZER;

#define DISPLAYUPDATE(l, ...) if (g_displayLevel>=l) { \
            if ((UTIL_clockSpanMicro(g_displayClock) > g_refreshRate) || (g_displayLevel>=4)) \
            { g_displayClock = UTIL_getTime(); DISPLAY(__VA_ARGS__); \
            if (g_displayLevel>=4) fflush(stderr); } }

static U64 g_clockTime = 0;


/*-*******************************************************
 *  Check macros
 *********************************************************/
#undef MIN
#undef MAX
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
/*! FUZ_rand() :
    @return : a 27 bits random value, from a 32-bits `seed`.
    `seed` is also modified */
#define FUZ_rotl32(x,r) ((x << r) | (x >> (32 - r)))
static U32 FUZ_rand(U32* seedPtr)
{
    static const U32 prime2 = 2246822519U;
    U32 rand32 = *seedPtr;
    rand32 *= prime32;
    rand32 += prime2;
    rand32  = FUZ_rotl32(rand32, 13);
    *seedPtr = rand32;
    return rand32 >> 5;
}

#define CHECK(cond, ...) {                                   \
    if (cond) {                                              \
        DISPLAY("Error => ");                                \
        DISPLAY(__VA_ARGS__);                                \
        DISPLAY(" (seed %u, test nb %u, line %u) \n",        \
                (unsigned)seed, testNb, __LINE__);           \
        goto _output_error;                                  \
}   }

#define CHECK_Z(f) {                                         \
    size_t const err = f;                                    \
    CHECK(ZSTD_isError(err), "%s : %s ",                     \
          #f, ZSTD_getErrorName(err));                       \
}

#define CHECK_RET(ret, cond, ...) {                          \
    if (cond) {                                              \
        DISPLAY("Error %llu => ", (unsigned long long)ret);  \
        DISPLAY(__VA_ARGS__);                                \
        DISPLAY(" (line %u)\n", __LINE__);                   \
        return ret;                                          \
}   }

#define CHECK_RET_Z(f) {                                     \
    size_t const err = f;                                    \
    CHECK_RET(err, ZSTD_isError(err), "%s : %s ",            \
          #f, ZSTD_getErrorName(err));                       \
}


/*======================================================
 *   Basic Unit tests
 *======================================================*/

typedef struct {
    void* start;
    size_t size;
    size_t filled;
} buffer_t;

static const buffer_t kBuffNull = { NULL, 0 , 0 };

static void FUZ_freeDictionary(buffer_t dict)
{
    free(dict.start);
}

static buffer_t FUZ_createDictionary(const void* src, size_t srcSize, size_t blockSize, size_t requestedDictSize)
{
    buffer_t dict = kBuffNull;
    size_t const nbBlocks = (srcSize + (blockSize-1)) / blockSize;
    size_t* const blockSizes = (size_t*)malloc(nbBlocks * sizeof(size_t));
    if (!blockSizes) return kBuffNull;
    dict.start = malloc(requestedDictSize);
    if (!dict.start) { free(blockSizes); return kBuffNull; }
    {   size_t nb;
        for (nb=0; nb<nbBlocks-1; nb++) blockSizes[nb] = blockSize;
        blockSizes[nbBlocks-1] = srcSize - (blockSize * (nbBlocks-1));
    }
    {   size_t const dictSize = ZDICT_trainFromBuffer(dict.start, requestedDictSize, src, blockSizes, (unsigned)nbBlocks);
        free(blockSizes);
        if (ZDICT_isError(dictSize)) { FUZ_freeDictionary(dict); return kBuffNull; }
        dict.size = requestedDictSize;
        dict.filled = dictSize;
        return dict;
    }
}

/* Round trips data and updates xxh with the decompressed data produced */
static size_t SEQ_roundTrip(ZSTD_CCtx* cctx, ZSTD_DCtx* dctx,
                            XXH64_state_t* xxh, void* data, size_t size,
                            ZSTD_EndDirective endOp)
{
    static BYTE compressed[1024];
    static BYTE uncompressed[1024];

    ZSTD_inBuffer cin = {data, size, 0};
    size_t cret;

    do {
        ZSTD_outBuffer cout = { compressed, sizeof(compressed), 0 };
        ZSTD_inBuffer din   = { compressed, 0, 0 };
        ZSTD_outBuffer dout = { uncompressed, 0, 0 };

        cret = ZSTD_compressStream2(cctx, &cout, &cin, endOp);
        if (ZSTD_isError(cret))
            return cret;

        din.size = cout.pos;
        while (din.pos < din.size || (endOp == ZSTD_e_end && cret == 0)) {
            size_t dret;

            dout.pos = 0;
            dout.size = sizeof(uncompressed);
            dret = ZSTD_decompressStream(dctx, &dout, &din);
            if (ZSTD_isError(dret))
                return dret;
            XXH64_update(xxh, dout.dst, dout.pos);
            if (dret == 0)
                break;
        }
    } while (cin.pos < cin.size || (endOp != ZSTD_e_continue && cret != 0));
    return 0;
}

/* Generates some data and round trips it */
static size_t SEQ_generateRoundTrip(ZSTD_CCtx* cctx, ZSTD_DCtx* dctx,
                                    XXH64_state_t* xxh, SEQ_stream* seq,
                                    SEQ_gen_type type, unsigned value)
{
    static BYTE data[1024];
    size_t gen;

    do {
        SEQ_outBuffer sout = {data, sizeof(data), 0};
        size_t ret;
        gen = SEQ_gen(seq, type, value, &sout);

        ret = SEQ_roundTrip(cctx, dctx, xxh, sout.dst, sout.pos, ZSTD_e_continue);
        if (ZSTD_isError(ret))
            return ret;
    } while (gen != 0);

    return 0;
}

static size_t getCCtxParams(ZSTD_CCtx* zc, ZSTD_parameters* savedParams)
{
    int value;
    CHECK_RET_Z(ZSTD_CCtx_getParameter(zc, ZSTD_c_windowLog, (int*)&savedParams->cParams.windowLog));
    CHECK_RET_Z(ZSTD_CCtx_getParameter(zc, ZSTD_c_hashLog, (int*)&savedParams->cParams.hashLog));
    CHECK_RET_Z(ZSTD_CCtx_getParameter(zc, ZSTD_c_chainLog, (int*)&savedParams->cParams.chainLog));
    CHECK_RET_Z(ZSTD_CCtx_getParameter(zc, ZSTD_c_searchLog, (int*)&savedParams->cParams.searchLog));
    CHECK_RET_Z(ZSTD_CCtx_getParameter(zc, ZSTD_c_minMatch, (int*)&savedParams->cParams.minMatch));
    CHECK_RET_Z(ZSTD_CCtx_getParameter(zc, ZSTD_c_targetLength, (int*)&savedParams->cParams.targetLength));
    CHECK_RET_Z(ZSTD_CCtx_getParameter(zc, ZSTD_c_strategy, &value));
    savedParams->cParams.strategy = value;

    CHECK_RET_Z(ZSTD_CCtx_getParameter(zc, ZSTD_c_checksumFlag, &savedParams->fParams.checksumFlag));
    CHECK_RET_Z(ZSTD_CCtx_getParameter(zc, ZSTD_c_contentSizeFlag, &savedParams->fParams.contentSizeFlag));
    CHECK_RET_Z(ZSTD_CCtx_getParameter(zc, ZSTD_c_dictIDFlag, &value));
    savedParams->fParams.noDictIDFlag = !value;
    return 0;
}

static U32 badParameters(ZSTD_CCtx* zc, ZSTD_parameters const savedParams)
{
    ZSTD_parameters params;
    if (ZSTD_isError(getCCtxParams(zc, &params))) return 10;
    CHECK_RET(1, params.cParams.windowLog != savedParams.cParams.windowLog, "windowLog");
    CHECK_RET(2, params.cParams.hashLog != savedParams.cParams.hashLog, "hashLog");
    CHECK_RET(3, params.cParams.chainLog != savedParams.cParams.chainLog, "chainLog");
    CHECK_RET(4, params.cParams.searchLog != savedParams.cParams.searchLog, "searchLog");
    CHECK_RET(5, params.cParams.minMatch != savedParams.cParams.minMatch, "minMatch");
    CHECK_RET(6, params.cParams.targetLength != savedParams.cParams.targetLength, "targetLength");

    CHECK_RET(7, params.fParams.checksumFlag != savedParams.fParams.checksumFlag, "checksumFlag");
    CHECK_RET(8, params.fParams.contentSizeFlag != savedParams.fParams.contentSizeFlag, "contentSizeFlag");
    CHECK_RET(9, params.fParams.noDictIDFlag != savedParams.fParams.noDictIDFlag, "noDictIDFlag");
    return 0;
}

static int basicUnitTests(U32 seed, double compressibility)
{
    size_t const CNBufferSize = COMPRESSIBLE_NOISE_LENGTH;
    void* CNBuffer = malloc(CNBufferSize);
    size_t const skippableFrameSize = 200 KB;
    size_t const compressedBufferSize = (8 + skippableFrameSize) + ZSTD_compressBound(COMPRESSIBLE_NOISE_LENGTH);
    void* compressedBuffer = malloc(compressedBufferSize);
    size_t const decodedBufferSize = CNBufferSize;
    void* decodedBuffer = malloc(decodedBufferSize);
    size_t cSize;
    int testResult = 0;
    int testNb = 1;
    U32 coreSeed = 0;  /* this name to conform with CHECK_Z macro display */
    ZSTD_CStream* zc = ZSTD_createCStream();
    ZSTD_DStream* zd = ZSTD_createDStream();
    ZSTDMT_CCtx* mtctx = ZSTDMT_createCCtx(2);

    ZSTD_inBuffer  inBuff, inBuff2;
    ZSTD_outBuffer outBuff;
    buffer_t dictionary = kBuffNull;
    size_t const dictSize = 128 KB;
    unsigned dictID = 0;

    /* Create compressible test buffer */
    if (!CNBuffer || !compressedBuffer || !decodedBuffer || !zc || !zd) {
        DISPLAY("Not enough memory, aborting \n");
        goto _output_error;
    }
    RDG_genBuffer(CNBuffer, CNBufferSize, compressibility, 0., seed);

    /* Create dictionary */
    DISPLAYLEVEL(3, "creating dictionary for unit tests \n");
    dictionary = FUZ_createDictionary(CNBuffer, CNBufferSize / 3, 16 KB, 48 KB);
    if (!dictionary.start) {
        DISPLAY("Error creating dictionary, aborting \n");
        goto _output_error;
    }
    dictID = ZDICT_getDictID(dictionary.start, dictionary.filled);

    /* Basic compression test */
    DISPLAYLEVEL(3, "test%3i : compress %u bytes : ", testNb++, COMPRESSIBLE_NOISE_LENGTH);
    CHECK_Z( ZSTD_initCStream(zc, 1 /* cLevel */) );
    outBuff.dst = (char*)(compressedBuffer);
    outBuff.size = compressedBufferSize;
    outBuff.pos = 0;
    inBuff.src = CNBuffer;
    inBuff.size = CNBufferSize;
    inBuff.pos = 0;
    CHECK_Z( ZSTD_compressStream(zc, &outBuff, &inBuff) );
    if (inBuff.pos != inBuff.size) goto _output_error;   /* entire input should be consumed */
    { size_t const r = ZSTD_endStream(zc, &outBuff);
      if (r != 0) goto _output_error; }  /* error, or some data not flushed */
    DISPLAYLEVEL(3, "OK (%u bytes)\n", (unsigned)outBuff.pos);

    /* generate skippable frame */
    MEM_writeLE32(compressedBuffer, ZSTD_MAGIC_SKIPPABLE_START);
    MEM_writeLE32(((char*)compressedBuffer)+4, (U32)skippableFrameSize);
    cSize = skippableFrameSize + 8;

    /* Basic compression test using dict */
    DISPLAYLEVEL(3, "test%3i : skipframe + compress %u bytes : ", testNb++, COMPRESSIBLE_NOISE_LENGTH);
    CHECK_Z( ZSTD_initCStream_usingDict(zc, CNBuffer, dictSize, 1 /* cLevel */) );
    outBuff.dst = (char*)(compressedBuffer)+cSize;
    assert(compressedBufferSize > cSize);
    outBuff.size = compressedBufferSize - cSize;
    outBuff.pos = 0;
    inBuff.src = CNBuffer;
    inBuff.size = CNBufferSize;
    inBuff.pos = 0;
    CHECK_Z( ZSTD_compressStream(zc, &outBuff, &inBuff) );
    if (inBuff.pos != inBuff.size) goto _output_error;   /* entire input should be consumed */
    { size_t const r = ZSTD_endStream(zc, &outBuff);
      if (r != 0) goto _output_error; }  /* error, or some data not flushed */
    cSize += outBuff.pos;
    DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n",
                    (unsigned)cSize, (double)cSize/COMPRESSIBLE_NOISE_LENGTH*100);

    /* context size functions */
    DISPLAYLEVEL(3, "test%3i : estimate CStream size : ", testNb++);
    {   ZSTD_compressionParameters const cParams = ZSTD_getCParams(1, CNBufferSize, dictSize);
        size_t const cstreamSize = ZSTD_estimateCStreamSize_usingCParams(cParams);
        size_t const cdictSize = ZSTD_estimateCDictSize_advanced(dictSize, cParams, ZSTD_dlm_byCopy); /* uses ZSTD_initCStream_usingDict() */
        if (ZSTD_isError(cstreamSize)) goto _output_error;
        if (ZSTD_isError(cdictSize)) goto _output_error;
        DISPLAYLEVEL(3, "OK (%u bytes) \n", (unsigned)(cstreamSize + cdictSize));
    }

    DISPLAYLEVEL(3, "test%3i : check actual CStream size : ", testNb++);
    {   size_t const s = ZSTD_sizeof_CStream(zc);
        if (ZSTD_isError(s)) goto _output_error;
        DISPLAYLEVEL(3, "OK (%u bytes) \n", (unsigned)s);
    }

    /* Attempt bad compression parameters */
    DISPLAYLEVEL(3, "test%3i : use bad compression parameters : ", testNb++);
    {   size_t r;
        ZSTD_parameters params = ZSTD_getParams(1, 0, 0);
        params.cParams.minMatch = 2;
        r = ZSTD_initCStream_advanced(zc, NULL, 0, params, 0);
        if (!ZSTD_isError(r)) goto _output_error;
        DISPLAYLEVEL(3, "init error : %s \n", ZSTD_getErrorName(r));
    }

    /* skippable frame test */
    DISPLAYLEVEL(3, "test%3i : decompress skippable frame : ", testNb++);
    CHECK_Z( ZSTD_initDStream_usingDict(zd, CNBuffer, dictSize) );
    inBuff.src = compressedBuffer;
    inBuff.size = cSize;
    inBuff.pos = 0;
    outBuff.dst = decodedBuffer;
    outBuff.size = CNBufferSize;
    outBuff.pos = 0;
    {   size_t const r = ZSTD_decompressStream(zd, &outBuff, &inBuff);
        DISPLAYLEVEL(5, " ( ZSTD_decompressStream => %u ) ", (unsigned)r);
        if (r != 0) goto _output_error;
    }
    if (outBuff.pos != 0) goto _output_error;   /* skippable frame output len is 0 */
    DISPLAYLEVEL(3, "OK \n");

    /* Basic decompression test */
    inBuff2 = inBuff;
    DISPLAYLEVEL(3, "test%3i : decompress %u bytes : ", testNb++, COMPRESSIBLE_NOISE_LENGTH);
    ZSTD_initDStream_usingDict(zd, CNBuffer, dictSize);
    CHECK_Z( ZSTD_DCtx_setParameter(zd, ZSTD_d_windowLogMax, ZSTD_WINDOWLOG_LIMIT_DEFAULT+1) );  /* large limit */
    { size_t const remaining = ZSTD_decompressStream(zd, &outBuff, &inBuff);
      if (remaining != 0) goto _output_error; }  /* should reach end of frame == 0; otherwise, some data left, or an error */
    if (outBuff.pos != CNBufferSize) goto _output_error;   /* should regenerate the same amount */
    if (inBuff.pos != inBuff.size) goto _output_error;   /* should have read the entire frame */
    DISPLAYLEVEL(3, "OK \n");

    /* Re-use without init */
    DISPLAYLEVEL(3, "test%3i : decompress again without init (re-use previous settings): ", testNb++);
    outBuff.pos = 0;
    { size_t const remaining = ZSTD_decompressStream(zd, &outBuff, &inBuff2);
      if (remaining != 0) goto _output_error; }  /* should reach end of frame == 0; otherwise, some data left, or an error */
    if (outBuff.pos != CNBufferSize) goto _output_error;   /* should regenerate the same amount */
    if (inBuff.pos != inBuff.size) goto _output_error;   /* should have read the entire frame */
    DISPLAYLEVEL(3, "OK \n");

    /* check regenerated data is byte exact */
    DISPLAYLEVEL(3, "test%3i : check decompressed result : ", testNb++);
    {   size_t i;
        for (i=0; i<CNBufferSize; i++) {
            if (((BYTE*)decodedBuffer)[i] != ((BYTE*)CNBuffer)[i]) goto _output_error;
    }   }
    DISPLAYLEVEL(3, "OK \n");

    /* context size functions */
    DISPLAYLEVEL(3, "test%3i : estimate DStream size : ", testNb++);
    {   ZSTD_frameHeader fhi;
        const void* cStart = (char*)compressedBuffer + (skippableFrameSize + 8);
        size_t const gfhError = ZSTD_getFrameHeader(&fhi, cStart, cSize);
        if (gfhError!=0) goto _output_error;
        DISPLAYLEVEL(5, " (windowSize : %u) ", (unsigned)fhi.windowSize);
        {   size_t const s = ZSTD_estimateDStreamSize(fhi.windowSize)
                            /* uses ZSTD_initDStream_usingDict() */
                           + ZSTD_estimateDDictSize(dictSize, ZSTD_dlm_byCopy);
            if (ZSTD_isError(s)) goto _output_error;
            DISPLAYLEVEL(3, "OK (%u bytes) \n", (unsigned)s);
    }   }

    DISPLAYLEVEL(3, "test%3i : check actual DStream size : ", testNb++);
    { size_t const s = ZSTD_sizeof_DStream(zd);
      if (ZSTD_isError(s)) goto _output_error;
      DISPLAYLEVEL(3, "OK (%u bytes) \n", (unsigned)s);
    }

    /* Decompression by small increment */
    DISPLAYLEVEL(3, "test%3i : decompress byte-by-byte : ", testNb++);
    {   /* skippable frame */
        size_t r = 1;
        ZSTD_initDStream_usingDict(zd, CNBuffer, dictSize);
        inBuff.src = compressedBuffer;
        outBuff.dst = decodedBuffer;
        inBuff.pos = 0;
        outBuff.pos = 0;
        while (r) {   /* skippable frame */
            size_t const inSize = (FUZ_rand(&coreSeed) & 15) + 1;
            size_t const outSize = (FUZ_rand(&coreSeed) & 15) + 1;
            inBuff.size = inBuff.pos + inSize;
            outBuff.size = outBuff.pos + outSize;
            r = ZSTD_decompressStream(zd, &outBuff, &inBuff);
            if (ZSTD_isError(r)) DISPLAYLEVEL(4, "ZSTD_decompressStream on skippable frame error : %s \n", ZSTD_getErrorName(r));
            if (ZSTD_isError(r)) goto _output_error;
        }
        /* normal frame */
        ZSTD_initDStream_usingDict(zd, CNBuffer, dictSize);
        r=1;
        while (r) {
            size_t const inSize = FUZ_rand(&coreSeed) & 15;
            size_t const outSize = (FUZ_rand(&coreSeed) & 15) + (!inSize);   /* avoid having both sizes at 0 => would trigger a no_forward_progress error */
            inBuff.size = inBuff.pos + inSize;
            outBuff.size = outBuff.pos + outSize;
            r = ZSTD_decompressStream(zd, &outBuff, &inBuff);
            if (ZSTD_isError(r)) DISPLAYLEVEL(4, "ZSTD_decompressStream error : %s \n", ZSTD_getErrorName(r));
            if (ZSTD_isError(r)) goto _output_error;
        }
    }
    if (outBuff.pos != CNBufferSize) DISPLAYLEVEL(4, "outBuff.pos != CNBufferSize : should have regenerated same amount ! \n");
    if (outBuff.pos != CNBufferSize) goto _output_error;   /* should regenerate the same amount */
    if (inBuff.pos != cSize) DISPLAYLEVEL(4, "inBuff.pos != cSize : should have real all input ! \n");
    if (inBuff.pos != cSize) goto _output_error;   /* should have read the entire frame */
    DISPLAYLEVEL(3, "OK \n");

    /* check regenerated data is byte exact */
    DISPLAYLEVEL(3, "test%3i : check decompressed result : ", testNb++);
    {   size_t i;
        for (i=0; i<CNBufferSize; i++) {
            if (((BYTE*)decodedBuffer)[i] != ((BYTE*)CNBuffer)[i]) goto _output_error;;
    }   }
    DISPLAYLEVEL(3, "OK \n");

    /* Decompression forward progress */
    DISPLAYLEVEL(3, "test%3i : generate error when ZSTD_decompressStream() doesn't progress : ", testNb++);
    {   /* skippable frame */
        size_t r = 0;
        int decNb = 0;
        int const maxDec = 100;
        inBuff.src = compressedBuffer;
        inBuff.size = cSize;
        inBuff.pos = 0;

        outBuff.dst = decodedBuffer;
        outBuff.pos = 0;
        outBuff.size = CNBufferSize-1;   /* 1 byte missing */

        for (decNb=0; decNb<maxDec; decNb++) {
            if (r==0) ZSTD_initDStream_usingDict(zd, CNBuffer, dictSize);
            r = ZSTD_decompressStream(zd, &outBuff, &inBuff);
            if (ZSTD_isError(r)) break;
        }
        if (!ZSTD_isError(r)) DISPLAYLEVEL(4, "ZSTD_decompressStream should have triggered a no_forward_progress error \n");
        if (!ZSTD_isError(r)) goto _output_error;   /* should have triggered no_forward_progress error */
    }
    DISPLAYLEVEL(3, "OK \n");

    /* _srcSize compression test */
    DISPLAYLEVEL(3, "test%3i : compress_srcSize %u bytes : ", testNb++, COMPRESSIBLE_NOISE_LENGTH);
    ZSTD_initCStream_srcSize(zc, 1, CNBufferSize);
    outBuff.dst = (char*)(compressedBuffer);
    outBuff.size = compressedBufferSize;
    outBuff.pos = 0;
    inBuff.src = CNBuffer;
    inBuff.size = CNBufferSize;
    inBuff.pos = 0;
    CHECK_Z( ZSTD_compressStream(zc, &outBuff, &inBuff) );
    if (inBuff.pos != inBuff.size) goto _output_error;   /* entire input should be consumed */
    { size_t const r = ZSTD_endStream(zc, &outBuff);
      if (r != 0) goto _output_error; }  /* error, or some data not flushed */
    { unsigned long long origSize = ZSTD_findDecompressedSize(outBuff.dst, outBuff.pos);
      if ((size_t)origSize != CNBufferSize) goto _output_error; }  /* exact original size must be present */
    DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/COMPRESSIBLE_NOISE_LENGTH*100);

    /* wrong _srcSize compression test */
    DISPLAYLEVEL(3, "test%3i : too large srcSize : %u bytes : ", testNb++, COMPRESSIBLE_NOISE_LENGTH-1);
    ZSTD_initCStream_srcSize(zc, 1, CNBufferSize+1);
    outBuff.dst = (char*)(compressedBuffer);
    outBuff.size = compressedBufferSize;
    outBuff.pos = 0;
    inBuff.src = CNBuffer;
    inBuff.size = CNBufferSize;
    inBuff.pos = 0;
    CHECK_Z( ZSTD_compressStream(zc, &outBuff, &inBuff) );
    if (inBuff.pos != inBuff.size) goto _output_error;   /* entire input should be consumed */
    { size_t const r = ZSTD_endStream(zc, &outBuff);
      if (ZSTD_getErrorCode(r) != ZSTD_error_srcSize_wrong) goto _output_error;    /* must fail : wrong srcSize */
      DISPLAYLEVEL(3, "OK (error detected : %s) \n", ZSTD_getErrorName(r)); }

    /* wrong _srcSize compression test */
    DISPLAYLEVEL(3, "test%3i : too small srcSize : %u bytes : ", testNb++, COMPRESSIBLE_NOISE_LENGTH-1);
    ZSTD_initCStream_srcSize(zc, 1, CNBufferSize-1);
    outBuff.dst = (char*)(compressedBuffer);
    outBuff.size = compressedBufferSize;
    outBuff.pos = 0;
    inBuff.src = CNBuffer;
    inBuff.size = CNBufferSize;
    inBuff.pos = 0;
    {   size_t const r = ZSTD_compressStream(zc, &outBuff, &inBuff);
        if (ZSTD_getErrorCode(r) != ZSTD_error_srcSize_wrong) goto _output_error;    /* must fail : wrong srcSize */
        DISPLAYLEVEL(3, "OK (error detected : %s) \n", ZSTD_getErrorName(r));
    }

    DISPLAYLEVEL(3, "test%3i : wrong srcSize !contentSizeFlag : %u bytes : ", testNb++, COMPRESSIBLE_NOISE_LENGTH-1);
    {   ZSTD_parameters params = ZSTD_getParams(1, CNBufferSize, 0);
        params.fParams.contentSizeFlag = 0;
        CHECK_Z(ZSTD_initCStream_advanced(zc, NULL, 0, params, CNBufferSize - MIN(CNBufferSize, 200 KB)));
        outBuff.dst = (char*)compressedBuffer;
        outBuff.size = compressedBufferSize;
        outBuff.pos = 0;
        inBuff.src = CNBuffer;
        inBuff.size = CNBufferSize;
        inBuff.pos = 0;
        {   size_t const r = ZSTD_compressStream(zc, &outBuff, &inBuff);
            if (ZSTD_getErrorCode(r) != ZSTD_error_srcSize_wrong) goto _output_error;    /* must fail : wrong srcSize */
            DISPLAYLEVEL(3, "OK (error detected : %s) \n", ZSTD_getErrorName(r));
    }   }

    /* Complex context re-use scenario */
    DISPLAYLEVEL(3, "test%3i : context re-use : ", testNb++);
    ZSTD_freeCStream(zc);
    zc = ZSTD_createCStream();
    if (zc==NULL) goto _output_error;   /* memory allocation issue */
    /* use 1 */
    {   size_t const inSize = 513;
        DISPLAYLEVEL(5, "use1 ");
        ZSTD_initCStream_advanced(zc, NULL, 0, ZSTD_getParams(19, inSize, 0), inSize);   /* needs btopt + search3 to trigger hashLog3 */
        inBuff.src = CNBuffer;
        inBuff.size = inSize;
        inBuff.pos = 0;
        outBuff.dst = (char*)(compressedBuffer)+cSize;
        outBuff.size = ZSTD_compressBound(inSize);
        outBuff.pos = 0;
        DISPLAYLEVEL(5, "compress1 ");
        CHECK_Z( ZSTD_compressStream(zc, &outBuff, &inBuff) );
        if (inBuff.pos != inBuff.size) goto _output_error;   /* entire input should be consumed */
        DISPLAYLEVEL(5, "end1 ");
        { size_t const r = ZSTD_endStream(zc, &outBuff);
            if (r != 0) goto _output_error; }  /* error, or some data not flushed */
    }
    /* use 2 */
    {   size_t const inSize = 1025;   /* will not continue, because tables auto-adjust and are therefore different size */
        DISPLAYLEVEL(5, "use2 ");
        ZSTD_initCStream_advanced(zc, NULL, 0, ZSTD_getParams(19, inSize, 0), inSize);   /* needs btopt + search3 to trigger hashLog3 */
        inBuff.src = CNBuffer;
        inBuff.size = inSize;
        inBuff.pos = 0;
        outBuff.dst = (char*)(compressedBuffer)+cSize;
        outBuff.size = ZSTD_compressBound(inSize);
        outBuff.pos = 0;
        DISPLAYLEVEL(5, "compress2 ");
        CHECK_Z( ZSTD_compressStream(zc, &outBuff, &inBuff) );
        if (inBuff.pos != inBuff.size) goto _output_error;   /* entire input should be consumed */
        DISPLAYLEVEL(5, "end2 ");
        { size_t const r = ZSTD_endStream(zc, &outBuff);
            if (r != 0) goto _output_error; }  /* error, or some data not flushed */
    }
    DISPLAYLEVEL(3, "OK \n");

    /* CDict scenario */
    DISPLAYLEVEL(3, "test%3i : digested dictionary : ", testNb++);
    {   ZSTD_CDict* const cdict = ZSTD_createCDict(dictionary.start, dictionary.filled, 1 /*byRef*/ );
        size_t const initError = ZSTD_initCStream_usingCDict(zc, cdict);
        DISPLAYLEVEL(5, "ZSTD_initCStream_usingCDict result : %u ", (unsigned)initError);
        if (ZSTD_isError(initError)) goto _output_error;
        outBuff.dst = compressedBuffer;
        outBuff.size = compressedBufferSize;
        outBuff.pos = 0;
        inBuff.src = CNBuffer;
        inBuff.size = CNBufferSize;
        inBuff.pos = 0;
        DISPLAYLEVEL(5, "- starting ZSTD_compressStream ");
        CHECK_Z( ZSTD_compressStream(zc, &outBuff, &inBuff) );
        if (inBuff.pos != inBuff.size) goto _output_error;   /* entire input should be consumed */
        {   size_t const r = ZSTD_endStream(zc, &outBuff);
            DISPLAYLEVEL(5, "- ZSTD_endStream result : %u ", (unsigned)r);
            if (r != 0) goto _output_error;  /* error, or some data not flushed */
        }
        cSize = outBuff.pos;
        ZSTD_freeCDict(cdict);
        DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBufferSize*100);
    }

    DISPLAYLEVEL(3, "test%3i : check CStream size : ", testNb++);
    { size_t const s = ZSTD_sizeof_CStream(zc);
      if (ZSTD_isError(s)) goto _output_error;
      DISPLAYLEVEL(3, "OK (%u bytes) \n", (unsigned)s);
    }

    DISPLAYLEVEL(4, "test%3i : check Dictionary ID : ", testNb++);
    { unsigned const dID = ZSTD_getDictID_fromFrame(compressedBuffer, cSize);
      if (dID != dictID) goto _output_error;
      DISPLAYLEVEL(4, "OK (%u) \n", dID);
    }

    /* DDict scenario */
    DISPLAYLEVEL(3, "test%3i : decompress %u bytes with digested dictionary : ", testNb++, (unsigned)CNBufferSize);
    {   ZSTD_DDict* const ddict = ZSTD_createDDict(dictionary.start, dictionary.filled);
        size_t const initError = ZSTD_initDStream_usingDDict(zd, ddict);
        if (ZSTD_isError(initError)) goto _output_error;
        outBuff.dst = decodedBuffer;
        outBuff.size = CNBufferSize;
        outBuff.pos = 0;
        inBuff.src = compressedBuffer;
        inBuff.size = cSize;
        inBuff.pos = 0;
        { size_t const r = ZSTD_decompressStream(zd, &outBuff, &inBuff);
          if (r != 0) goto _output_error; }  /* should reach end of frame == 0; otherwise, some data left, or an error */
        if (outBuff.pos != CNBufferSize) goto _output_error;   /* should regenerate the same amount */
        if (inBuff.pos != inBuff.size) goto _output_error;   /* should have read the entire frame */
        ZSTD_freeDDict(ddict);
        DISPLAYLEVEL(3, "OK \n");
    }

    /* Memory restriction */
    DISPLAYLEVEL(3, "test%3i : maxWindowSize < frame requirement : ", testNb++);
    ZSTD_initDStream_usingDict(zd, CNBuffer, dictSize);
    CHECK_Z( ZSTD_DCtx_setParameter(zd, ZSTD_d_windowLogMax, 10) );  /* too small limit */
    outBuff.dst = decodedBuffer;
    outBuff.size = CNBufferSize;
    outBuff.pos = 0;
    inBuff.src = compressedBuffer;
    inBuff.size = cSize;
    inBuff.pos = 0;
    { size_t const r = ZSTD_decompressStream(zd, &outBuff, &inBuff);
      if (!ZSTD_isError(r)) goto _output_error;  /* must fail : frame requires > 100 bytes */
      DISPLAYLEVEL(3, "OK (%s)\n", ZSTD_getErrorName(r)); }
    ZSTD_DCtx_reset(zd, ZSTD_reset_session_and_parameters);   /* leave zd in good shape for next tests */

    DISPLAYLEVEL(3, "test%3i : dictionary source size and level : ", testNb++);
    {   ZSTD_DCtx* const dctx = ZSTD_createDCtx();
        int const maxLevel = 16;   /* first level with zstd_opt */
        int level;
        assert(maxLevel < ZSTD_maxCLevel());
        CHECK_Z( ZSTD_DCtx_loadDictionary_byReference(dctx, dictionary.start, dictionary.filled) );
        for (level = 1; level <= maxLevel; ++level) {
            ZSTD_CDict* const cdict = ZSTD_createCDict(dictionary.start, dictionary.filled, level);
            size_t const maxSize = MIN(1 MB, CNBufferSize);
            size_t size;
            for (size = 512; size <= maxSize; size <<= 1) {
                U64 const crcOrig = XXH64(CNBuffer, size, 0);
                ZSTD_CCtx* const cctx = ZSTD_createCCtx();
                ZSTD_parameters savedParams;
                getCCtxParams(cctx, &savedParams);
                outBuff.dst = compressedBuffer;
                outBuff.size = compressedBufferSize;
                outBuff.pos = 0;
                inBuff.src = CNBuffer;
                inBuff.size = size;
                inBuff.pos = 0;
                CHECK_Z(ZSTD_CCtx_refCDict(cctx, cdict));
                CHECK_Z(ZSTD_compressStream2(cctx, &outBuff, &inBuff, ZSTD_e_end));
                CHECK(badParameters(cctx, savedParams), "Bad CCtx params");
                if (inBuff.pos != inBuff.size) goto _output_error;
                {   ZSTD_outBuffer decOut = {decodedBuffer, size, 0};
                    ZSTD_inBuffer decIn = {outBuff.dst, outBuff.pos, 0};
                    CHECK_Z( ZSTD_decompressStream(dctx, &decOut, &decIn) );
                    if (decIn.pos != decIn.size) goto _output_error;
                    if (decOut.pos != size) goto _output_error;
                    {   U64 const crcDec = XXH64(decOut.dst, decOut.pos, 0);
                        if (crcDec != crcOrig) goto _output_error;
                }   }
                ZSTD_freeCCtx(cctx);
            }
            ZSTD_freeCDict(cdict);
        }
        ZSTD_freeDCtx(dctx);
    }
    DISPLAYLEVEL(3, "OK\n");

    DISPLAYLEVEL(3, "test%3i : ZSTD_initCStream_usingCDict_advanced with masked dictID : ", testNb++);
    {   ZSTD_compressionParameters const cParams = ZSTD_getCParams(1, CNBufferSize, dictionary.filled);
        ZSTD_frameParameters const fParams = { 1 /* contentSize */, 1 /* checksum */, 1 /* noDictID */};
        ZSTD_CDict* const cdict = ZSTD_createCDict_advanced(dictionary.start, dictionary.filled, ZSTD_dlm_byRef, ZSTD_dct_auto, cParams, ZSTD_defaultCMem);
        size_t const initError = ZSTD_initCStream_usingCDict_advanced(zc, cdict, fParams, CNBufferSize);
        if (ZSTD_isError(initError)) goto _output_error;
        outBuff.dst = compressedBuffer;
        outBuff.size = compressedBufferSize;
        outBuff.pos = 0;
        inBuff.src = CNBuffer;
        inBuff.size = CNBufferSize;
        inBuff.pos = 0;
        CHECK_Z( ZSTD_compressStream(zc, &outBuff, &inBuff) );
        if (inBuff.pos != inBuff.size) goto _output_error;  /* entire input should be consumed */
        { size_t const r = ZSTD_endStream(zc, &outBuff);
          if (r != 0) goto _output_error; }  /* error, or some data not flushed */
        cSize = outBuff.pos;
        ZSTD_freeCDict(cdict);
        DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBufferSize*100);
    }

    DISPLAYLEVEL(3, "test%3i : try retrieving dictID from frame : ", testNb++);
    {   U32 const did = ZSTD_getDictID_fromFrame(compressedBuffer, cSize);
        if (did != 0) goto _output_error;
    }
    DISPLAYLEVEL(3, "OK (not detected) \n");

    DISPLAYLEVEL(3, "test%3i : decompress without dictionary : ", testNb++);
    {   size_t const r = ZSTD_decompress(decodedBuffer, CNBufferSize, compressedBuffer, cSize);
        if (!ZSTD_isError(r)) goto _output_error;  /* must fail : dictionary not used */
        DISPLAYLEVEL(3, "OK (%s)\n", ZSTD_getErrorName(r));
    }

    DISPLAYLEVEL(3, "test%3i : compress with ZSTD_CCtx_refPrefix : ", testNb++);
    CHECK_Z( ZSTD_CCtx_refPrefix(zc, dictionary.start, dictionary.filled) );
    outBuff.dst = compressedBuffer;
    outBuff.size = compressedBufferSize;
    outBuff.pos = 0;
    inBuff.src = CNBuffer;
    inBuff.size = CNBufferSize;
    inBuff.pos = 0;
    CHECK_Z( ZSTD_compressStream2(zc, &outBuff, &inBuff, ZSTD_e_end) );
    if (inBuff.pos != inBuff.size) goto _output_error;  /* entire input should be consumed */
    cSize = outBuff.pos;
    DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBufferSize*100);

    DISPLAYLEVEL(3, "test%3i : decompress with ZSTD_DCtx_refPrefix : ", testNb++);
    CHECK_Z( ZSTD_DCtx_refPrefix(zd, dictionary.start, dictionary.filled) );
    outBuff.dst = decodedBuffer;
    outBuff.size = CNBufferSize;
    outBuff.pos = 0;
    inBuff.src = compressedBuffer;
    inBuff.size = cSize;
    inBuff.pos = 0;
    CHECK_Z( ZSTD_decompressStream(zd, &outBuff, &inBuff) );
    if (inBuff.pos != inBuff.size) goto _output_error;  /* entire input should be consumed */
    if (outBuff.pos != CNBufferSize) goto _output_error;  /* must regenerate whole input */
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : decompress without dictionary (should fail): ", testNb++);
    {   size_t const r = ZSTD_decompress(decodedBuffer, CNBufferSize, compressedBuffer, cSize);
        if (!ZSTD_isError(r)) goto _output_error;  /* must fail : dictionary not used */
        DISPLAYLEVEL(3, "OK (%s)\n", ZSTD_getErrorName(r));
    }

    DISPLAYLEVEL(3, "test%3i : compress again with ZSTD_compressStream2 : ", testNb++);
    outBuff.dst = compressedBuffer;
    outBuff.size = compressedBufferSize;
    outBuff.pos = 0;
    inBuff.src = CNBuffer;
    inBuff.size = CNBufferSize;
    inBuff.pos = 0;
    CHECK_Z( ZSTD_compressStream2(zc, &outBuff, &inBuff, ZSTD_e_end) );
    if (inBuff.pos != inBuff.size) goto _output_error;  /* entire input should be consumed */
    cSize = outBuff.pos;
    DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBufferSize*100);

    DISPLAYLEVEL(3, "test%3i : decompress without dictionary (should work): ", testNb++);
    CHECK_Z( ZSTD_decompress(decodedBuffer, CNBufferSize, compressedBuffer, cSize) );
    DISPLAYLEVEL(3, "OK \n");

    /* Empty srcSize */
    DISPLAYLEVEL(3, "test%3i : ZSTD_initCStream_advanced with pledgedSrcSize=0 and dict : ", testNb++);
    {   ZSTD_parameters params = ZSTD_getParams(5, 0, 0);
        params.fParams.contentSizeFlag = 1;
        CHECK_Z( ZSTD_initCStream_advanced(zc, dictionary.start, dictionary.filled, params, 0 /* pledgedSrcSize==0 means "empty" when params.fParams.contentSizeFlag is set */) );
    } /* cstream advanced shall write content size = 0 */
    outBuff.dst = compressedBuffer;
    outBuff.size = compressedBufferSize;
    outBuff.pos = 0;
    inBuff.src = CNBuffer;
    inBuff.size = 0;
    inBuff.pos = 0;
    CHECK_Z( ZSTD_compressStream(zc, &outBuff, &inBuff) );
    if (ZSTD_endStream(zc, &outBuff) != 0) goto _output_error;
    cSize = outBuff.pos;
    if (ZSTD_findDecompressedSize(compressedBuffer, cSize) != 0) goto _output_error;
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : pledgedSrcSize == 0 behaves properly : ", testNb++);
    {   ZSTD_parameters params = ZSTD_getParams(5, 0, 0);
        params.fParams.contentSizeFlag = 1;
        CHECK_Z( ZSTD_initCStream_advanced(zc, NULL, 0, params, 0) );
    } /* cstream advanced shall write content size = 0 */
    inBuff.src = CNBuffer;
    inBuff.size = 0;
    inBuff.pos = 0;
    outBuff.dst = compressedBuffer;
    outBuff.size = compressedBufferSize;
    outBuff.pos = 0;
    CHECK_Z( ZSTD_compressStream(zc, &outBuff, &inBuff) );
    if (ZSTD_endStream(zc, &outBuff) != 0) goto _output_error;
    cSize = outBuff.pos;
    if (ZSTD_findDecompressedSize(compressedBuffer, cSize) != 0) goto _output_error;

    ZSTD_resetCStream(zc, 0); /* resetCStream should treat 0 as unknown */
    outBuff.dst = compressedBuffer;
    outBuff.size = compressedBufferSize;
    outBuff.pos = 0;
    inBuff.src = CNBuffer;
    inBuff.size = 0;
    inBuff.pos = 0;
    CHECK_Z( ZSTD_compressStream(zc, &outBuff, &inBuff) );
    if (ZSTD_endStream(zc, &outBuff) != 0) goto _output_error;
    cSize = outBuff.pos;
    if (ZSTD_findDecompressedSize(compressedBuffer, cSize) != ZSTD_CONTENTSIZE_UNKNOWN) goto _output_error;
    DISPLAYLEVEL(3, "OK \n");

    /* Basic multithreading compression test */
    DISPLAYLEVEL(3, "test%3i : compress %u bytes with multiple threads : ", testNb++, COMPRESSIBLE_NOISE_LENGTH);
    {   ZSTD_parameters const params = ZSTD_getParams(1, 0, 0);
        int jobSize;
        CHECK_Z( ZSTDMT_getMTCtxParameter(mtctx, ZSTDMT_p_jobSize, &jobSize));
        CHECK(jobSize != 0, "job size non-zero");
        CHECK_Z( ZSTDMT_initCStream_advanced(mtctx, CNBuffer, dictSize, params, CNBufferSize) );
        CHECK_Z( ZSTDMT_getMTCtxParameter(mtctx, ZSTDMT_p_jobSize, &jobSize));
        CHECK(jobSize != 0, "job size non-zero");
    }
    outBuff.dst = compressedBuffer;
    outBuff.size = compressedBufferSize;
    outBuff.pos = 0;
    inBuff.src = CNBuffer;
    inBuff.size = CNBufferSize;
    inBuff.pos = 0;
    {   size_t const compressResult = ZSTDMT_compressStream_generic(mtctx, &outBuff, &inBuff, ZSTD_e_end);
        if (compressResult != 0) goto _output_error;  /* compression must be completed in a single round */
    }
    if (inBuff.pos != inBuff.size) goto _output_error;   /* entire input should be consumed */
    {   size_t const compressedSize = ZSTD_findFrameCompressedSize(compressedBuffer, outBuff.pos);
        if (compressedSize != outBuff.pos) goto _output_error;  /* must be a full valid frame */
    }
    DISPLAYLEVEL(3, "OK \n");

    /* Complex multithreading + dictionary test */
    {   U32 const nbWorkers = 2;
        size_t const jobSize = 4 * 1 MB;
        size_t const srcSize = jobSize * nbWorkers;  /* we want each job to have predictable size */
        size_t const segLength = 2 KB;
        size_t const offset = 600 KB;   /* must be larger than window defined in cdict */
        size_t const start = jobSize + (offset-1);
        const BYTE* const srcToCopy = (const BYTE*)CNBuffer + start;
        BYTE* const dst = (BYTE*)CNBuffer + start - offset;
        DISPLAYLEVEL(3, "test%3i : compress %u bytes with multiple threads + dictionary : ", testNb++, (unsigned)srcSize);
        CHECK_Z( ZSTD_CCtx_setParameter(zc, ZSTD_c_compressionLevel, 3) );
        CHECK_Z( ZSTD_CCtx_setParameter(zc, ZSTD_c_nbWorkers, nbWorkers) );
        CHECK_Z( ZSTD_CCtx_setParameter(zc, ZSTD_c_jobSize, jobSize) );
        assert(start > offset);
        assert(start + segLength < COMPRESSIBLE_NOISE_LENGTH);
        memcpy(dst, srcToCopy, segLength);   /* create a long repetition at long distance for job 2 */
        outBuff.dst = compressedBuffer;
        outBuff.size = compressedBufferSize;
        outBuff.pos = 0;
        inBuff.src = CNBuffer;
        inBuff.size = srcSize; assert(srcSize < COMPRESSIBLE_NOISE_LENGTH);
        inBuff.pos = 0;
    }
    {   ZSTD_compressionParameters const cParams = ZSTD_getCParams(1, 4 KB, dictionary.filled);   /* intentionnally lies on estimatedSrcSize, to push cdict into targeting a small window size */
        ZSTD_CDict* const cdict = ZSTD_createCDict_advanced(dictionary.start, dictionary.filled, ZSTD_dlm_byRef, ZSTD_dct_fullDict, cParams, ZSTD_defaultCMem);
        DISPLAYLEVEL(5, "cParams.windowLog = %u : ", cParams.windowLog);
        CHECK_Z( ZSTD_CCtx_refCDict(zc, cdict) );
        CHECK_Z( ZSTD_compressStream2(zc, &outBuff, &inBuff, ZSTD_e_end) );
        CHECK_Z( ZSTD_CCtx_refCDict(zc, NULL) );  /* do not keep a reference to cdict, as its lifetime ends */
        ZSTD_freeCDict(cdict);
    }
    if (inBuff.pos != inBuff.size) goto _output_error;   /* entire input should be consumed */
    cSize = outBuff.pos;
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : decompress large frame created from multiple threads + dictionary : ", testNb++);
    {   ZSTD_DStream* const dstream = ZSTD_createDCtx();
        ZSTD_frameHeader zfh;
        ZSTD_getFrameHeader(&zfh, compressedBuffer, cSize);
        DISPLAYLEVEL(5, "frame windowsize = %u : ", (unsigned)zfh.windowSize);
        outBuff.dst = decodedBuffer;
        outBuff.size = CNBufferSize;
        outBuff.pos = 0;
        inBuff.src = compressedBuffer;
        inBuff.pos = 0;
        CHECK_Z( ZSTD_initDStream_usingDict(dstream, dictionary.start, dictionary.filled) );
        inBuff.size = 1;  /* avoid shortcut to single-pass mode */
        CHECK_Z( ZSTD_decompressStream(dstream, &outBuff, &inBuff) );
        inBuff.size = cSize;
        CHECK_Z( ZSTD_decompressStream(dstream, &outBuff, &inBuff) );
        if (inBuff.pos != inBuff.size) goto _output_error;   /* entire input should be consumed */
        ZSTD_freeDStream(dstream);
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : check dictionary FSE tables can represent every code : ", testNb++);
    {   unsigned const kMaxWindowLog = 24;
        unsigned value;
        ZSTD_compressionParameters cParams = ZSTD_getCParams(3, 1U << kMaxWindowLog, 1024);
        ZSTD_CDict* cdict;
        ZSTD_DDict* ddict;
        SEQ_stream seq = SEQ_initStream(0x87654321);
        SEQ_gen_type type;
        XXH64_state_t xxh;

        XXH64_reset(&xxh, 0);
        cParams.windowLog = kMaxWindowLog;
        cdict = ZSTD_createCDict_advanced(dictionary.start, dictionary.filled, ZSTD_dlm_byRef, ZSTD_dct_fullDict, cParams, ZSTD_defaultCMem);
        ddict = ZSTD_createDDict(dictionary.start, dictionary.filled);

        if (!cdict || !ddict) goto _output_error;

        ZSTD_CCtx_reset(zc, ZSTD_reset_session_only);
        ZSTD_resetDStream(zd);
        CHECK_Z(ZSTD_CCtx_refCDict(zc, cdict));
        CHECK_Z(ZSTD_initDStream_usingDDict(zd, ddict));
        CHECK_Z(ZSTD_DCtx_setParameter(zd, ZSTD_d_windowLogMax, kMaxWindowLog));
        /* Test all values < 300 */
        for (value = 0; value < 300; ++value) {
            for (type = (SEQ_gen_type)0; type < SEQ_gen_max; ++type) {
                CHECK_Z(SEQ_generateRoundTrip(zc, zd, &xxh, &seq, type, value));
            }
        }
        /* Test values 2^8 to 2^17 */
        for (value = (1 << 8); value < (1 << 17); value <<= 1) {
            for (type = (SEQ_gen_type)0; type < SEQ_gen_max; ++type) {
                CHECK_Z(SEQ_generateRoundTrip(zc, zd, &xxh, &seq, type, value));
                CHECK_Z(SEQ_generateRoundTrip(zc, zd, &xxh, &seq, type, value + (value >> 2)));
            }
        }
        /* Test offset values up to the max window log */
        for (value = 8; value <= kMaxWindowLog; ++value) {
            CHECK_Z(SEQ_generateRoundTrip(zc, zd, &xxh, &seq, SEQ_gen_of, (1U << value) - 1));
        }

        CHECK_Z(SEQ_roundTrip(zc, zd, &xxh, NULL, 0, ZSTD_e_end));
        CHECK(SEQ_digest(&seq) != XXH64_digest(&xxh), "SEQ XXH64 does not match");

        ZSTD_freeCDict(cdict);
        ZSTD_freeDDict(ddict);
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : ZSTD_initCStream_srcSize sets requestedParams : ", testNb++);
    {   int level;
        CHECK_Z(ZSTD_initCStream_srcSize(zc, 11, ZSTD_CONTENTSIZE_UNKNOWN));
        CHECK_Z(ZSTD_CCtx_getParameter(zc, ZSTD_c_compressionLevel, &level));
        CHECK(level != 11, "Compression level does not match");
        ZSTD_resetCStream(zc, ZSTD_CONTENTSIZE_UNKNOWN);
        CHECK_Z(ZSTD_CCtx_getParameter(zc, ZSTD_c_compressionLevel, &level));
        CHECK(level != 11, "Compression level does not match");
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : ZSTD_initCStream_advanced sets requestedParams : ", testNb++);
    {   ZSTD_parameters const params = ZSTD_getParams(9, 0, 0);
        CHECK_Z(ZSTD_initCStream_advanced(zc, NULL, 0, params, ZSTD_CONTENTSIZE_UNKNOWN));
        CHECK(badParameters(zc, params), "Compression parameters do not match");
        ZSTD_resetCStream(zc, ZSTD_CONTENTSIZE_UNKNOWN);
        CHECK(badParameters(zc, params), "Compression parameters do not match");
    }
    DISPLAYLEVEL(3, "OK \n");

    /* Overlen overwriting window data bug */
    DISPLAYLEVEL(3, "test%3i : wildcopy doesn't overwrite potential match data : ", testNb++);
    {   /* This test has a window size of 1024 bytes and consists of 3 blocks:
            1. 'a' repeated 517 times
            2. 'b' repeated 516 times
            3. a compressed block with no literals and 3 sequence commands:
                litlength = 0, offset = 24, match length = 24
                litlength = 0, offset = 24, match length = 3 (this one creates an overlength write of length 2*WILDCOPY_OVERLENGTH - 3)
                litlength = 0, offset = 1021, match length = 3 (this one will try to read from overwritten data if the buffer is too small) */

        const char* testCase =
            "\x28\xB5\x2F\xFD\x04\x00\x4C\x00\x00\x10\x61\x61\x01\x00\x00\x2A"
            "\x80\x05\x44\x00\x00\x08\x62\x01\x00\x00\x2A\x20\x04\x5D\x00\x00"
            "\x00\x03\x40\x00\x00\x64\x60\x27\xB0\xE0\x0C\x67\x62\xCE\xE0";
        ZSTD_DStream* const zds = ZSTD_createDStream();
        if (zds==NULL) goto _output_error;

        CHECK_Z( ZSTD_initDStream(zds) );
        inBuff.src = testCase;
        inBuff.size = 47;
        inBuff.pos = 0;
        outBuff.dst = decodedBuffer;
        outBuff.size = CNBufferSize;
        outBuff.pos = 0;

        while (inBuff.pos < inBuff.size) {
            CHECK_Z( ZSTD_decompressStream(zds, &outBuff, &inBuff) );
        }

        ZSTD_freeDStream(zds);
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : dictionary + uncompressible block + reusing tables checks offset table validity: ", testNb++);
    {   ZSTD_CDict* const cdict = ZSTD_createCDict_advanced(
            dictionary.start, dictionary.filled,
            ZSTD_dlm_byRef, ZSTD_dct_fullDict,
            ZSTD_getCParams(3, 0, dictionary.filled),
            ZSTD_defaultCMem);
        const size_t inbufsize = 2 * 128 * 1024; /* 2 blocks */
        const size_t outbufsize = ZSTD_compressBound(inbufsize);
        size_t inbufpos = 0;
        size_t cursegmentlen;
        BYTE *inbuf = (BYTE *)malloc(inbufsize);
        BYTE *outbuf = (BYTE *)malloc(outbufsize);
        BYTE *checkbuf = (BYTE *)malloc(inbufsize);
        size_t ret;

        CHECK(cdict == NULL, "failed to alloc cdict");
        CHECK(inbuf == NULL, "failed to alloc input buffer");

        /* first block is uncompressible */
        cursegmentlen = 128 * 1024;
        RDG_genBuffer(inbuf + inbufpos, cursegmentlen, 0., 0., seed);
        inbufpos += cursegmentlen;

        /* second block is compressible */
        cursegmentlen = 128 * 1024 - 256;
        RDG_genBuffer(inbuf + inbufpos, cursegmentlen, 0.05, 0., seed);
        inbufpos += cursegmentlen;

        /* and includes a very long backref */
        cursegmentlen = 128;
        memcpy(inbuf + inbufpos, dictionary.start + 256, cursegmentlen);
        inbufpos += cursegmentlen;

        /* and includes a very long backref */
        cursegmentlen = 128;
        memcpy(inbuf + inbufpos, dictionary.start + 128, cursegmentlen);
        inbufpos += cursegmentlen;

        ret = ZSTD_compress_usingCDict(zc, outbuf, outbufsize, inbuf, inbufpos, cdict);
        CHECK_Z(ret);

        ret = ZSTD_decompress_usingDict(zd, checkbuf, inbufsize, outbuf, ret, dictionary.start, dictionary.filled);
        CHECK_Z(ret);

        CHECK(memcmp(inbuf, checkbuf, inbufpos), "start and finish buffers don't match");

        ZSTD_freeCDict(cdict);
        free(inbuf);
        free(outbuf);
        free(checkbuf);
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : dictionary + small blocks + reusing tables checks offset table validity: ", testNb++);
    {   ZSTD_CDict* const cdict = ZSTD_createCDict_advanced(
            dictionary.start, dictionary.filled,
            ZSTD_dlm_byRef, ZSTD_dct_fullDict,
            ZSTD_getCParams(3, 0, dictionary.filled),
            ZSTD_defaultCMem);
        ZSTD_outBuffer out = {compressedBuffer, compressedBufferSize, 0};
        int remainingInput = 256 * 1024;
        int offset;

        CHECK_Z(ZSTD_CCtx_reset(zc, ZSTD_reset_session_and_parameters));
        CHECK_Z(ZSTD_CCtx_refCDict(zc, cdict));
        CHECK_Z(ZSTD_CCtx_setParameter(zc, ZSTD_c_checksumFlag, 1));
        /* Write a bunch of 6 byte blocks */
        while (remainingInput > 0) {
          char testBuffer[6] = "\xAA\xAA\xAA\xAA\xAA\xAA";
          const size_t kSmallBlockSize = sizeof(testBuffer);
          ZSTD_inBuffer in = {testBuffer, kSmallBlockSize, 0};

          CHECK_Z(ZSTD_compressStream2(zc, &out, &in, ZSTD_e_flush));
          CHECK(in.pos != in.size, "input not fully consumed");
          remainingInput -= kSmallBlockSize;
        }
        /* Write several very long offset matches into the dictionary */
        for (offset = 1024; offset >= 0; offset -= 128) {
          ZSTD_inBuffer in = {dictionary.start + offset, 128, 0};
          ZSTD_EndDirective flush = offset > 0 ? ZSTD_e_continue : ZSTD_e_end;
          CHECK_Z(ZSTD_compressStream2(zc, &out, &in, flush));
          CHECK(in.pos != in.size, "input not fully consumed");
        }
        /* Ensure decompression works */
        CHECK_Z(ZSTD_decompress_usingDict(zd, decodedBuffer, CNBufferSize, out.dst, out.pos, dictionary.start, dictionary.filled));

        ZSTD_freeCDict(cdict);
    }
    DISPLAYLEVEL(3, "OK \n");

_end:
    FUZ_freeDictionary(dictionary);
    ZSTD_freeCStream(zc);
    ZSTD_freeDStream(zd);
    ZSTDMT_freeCCtx(mtctx);
    free(CNBuffer);
    free(compressedBuffer);
    free(decodedBuffer);
    return testResult;

_output_error:
    testResult = 1;
    DISPLAY("Error detected in Unit tests ! \n");
    goto _end;
}


/* ======   Fuzzer tests   ====== */

static size_t findDiff(const void* buf1, const void* buf2, size_t max)
{
    const BYTE* b1 = (const BYTE*)buf1;
    const BYTE* b2 = (const BYTE*)buf2;
    size_t u;
    for (u=0; u<max; u++) {
        if (b1[u] != b2[u]) break;
    }
    if (u==max) {
        DISPLAY("=> No difference detected within %u bytes \n", (unsigned)max);
        return u;
    }
    DISPLAY("Error at position %u / %u \n", (unsigned)u, (unsigned)max);
    if (u>=3)
        DISPLAY(" %02X %02X %02X ",
                b1[u-3], b1[u-2], b1[u-1]);
    DISPLAY(" :%02X:  %02X %02X %02X %02X %02X \n",
            b1[u], b1[u+1], b1[u+2], b1[u+3], b1[u+4], b1[u+5]);
    if (u>=3)
        DISPLAY(" %02X %02X %02X ",
                b2[u-3], b2[u-2], b2[u-1]);
    DISPLAY(" :%02X:  %02X %02X %02X %02X %02X \n",
            b2[u], b2[u+1], b2[u+2], b2[u+3], b2[u+4], b2[u+5]);
    return u;
}

static size_t FUZ_rLogLength(U32* seed, U32 logLength)
{
    size_t const lengthMask = ((size_t)1 << logLength) - 1;
    return (lengthMask+1) + (FUZ_rand(seed) & lengthMask);
}

static size_t FUZ_randomLength(U32* seed, U32 maxLog)
{
    U32 const logLength = FUZ_rand(seed) % maxLog;
    return FUZ_rLogLength(seed, logLength);
}

/* Return value in range minVal <= v <= maxVal */
static U32 FUZ_randomClampedLength(U32* seed, U32 minVal, U32 maxVal)
{
    U32 const mod = maxVal < minVal ? 1 : (maxVal + 1) - minVal;
    return (U32)((FUZ_rand(seed) % mod) + minVal);
}

static int fuzzerTests(U32 seed, unsigned nbTests, unsigned startTest, double compressibility, int bigTests)
{
    U32 const maxSrcLog = bigTests ? 24 : 22;
    static const U32 maxSampleLog = 19;
    size_t const srcBufferSize = (size_t)1<<maxSrcLog;
    BYTE* cNoiseBuffer[5];
    size_t const copyBufferSize = srcBufferSize + (1<<maxSampleLog);
    BYTE*  const copyBuffer = (BYTE*)malloc (copyBufferSize);
    size_t const cBufferSize = ZSTD_compressBound(srcBufferSize);
    BYTE*  const cBuffer = (BYTE*)malloc (cBufferSize);
    size_t const dstBufferSize = srcBufferSize;
    BYTE*  const dstBuffer = (BYTE*)malloc (dstBufferSize);
    U32 result = 0;
    unsigned testNb = 0;
    U32 coreSeed = seed;
    ZSTD_CStream* zc = ZSTD_createCStream();   /* will be re-created sometimes */
    ZSTD_DStream* zd = ZSTD_createDStream();   /* will be re-created sometimes */
    ZSTD_DStream* const zd_noise = ZSTD_createDStream();
    UTIL_time_t const startClock = UTIL_getTime();
    const BYTE* dict = NULL;  /* can keep same dict on 2 consecutive tests */
    size_t dictSize = 0;
    U32 oldTestLog = 0;
    U32 const cLevelMax = bigTests ? (U32)ZSTD_maxCLevel() : g_cLevelMax_smallTests;

    /* allocations */
    cNoiseBuffer[0] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[1] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[2] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[3] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[4] = (BYTE*)malloc (srcBufferSize);
    CHECK (!cNoiseBuffer[0] || !cNoiseBuffer[1] || !cNoiseBuffer[2] || !cNoiseBuffer[3] || !cNoiseBuffer[4] ||
           !copyBuffer || !dstBuffer || !cBuffer || !zc || !zd || !zd_noise ,
           "Not enough memory, fuzzer tests cancelled");

    /* Create initial samples */
    RDG_genBuffer(cNoiseBuffer[0], srcBufferSize, 0.00, 0., coreSeed);    /* pure noise */
    RDG_genBuffer(cNoiseBuffer[1], srcBufferSize, 0.05, 0., coreSeed);    /* barely compressible */
    RDG_genBuffer(cNoiseBuffer[2], srcBufferSize, compressibility, 0., coreSeed);
    RDG_genBuffer(cNoiseBuffer[3], srcBufferSize, 0.95, 0., coreSeed);    /* highly compressible */
    RDG_genBuffer(cNoiseBuffer[4], srcBufferSize, 1.00, 0., coreSeed);    /* sparse content */
    memset(copyBuffer, 0x65, copyBufferSize);                             /* make copyBuffer considered initialized */
    ZSTD_initDStream_usingDict(zd, NULL, 0);  /* ensure at least one init */

    /* catch up testNb */
    for (testNb=1; testNb < startTest; testNb++)
        FUZ_rand(&coreSeed);

    /* test loop */
    for ( ; (testNb <= nbTests) || (UTIL_clockSpanMicro(startClock) < g_clockTime) ; testNb++ ) {
        U32 lseed;
        const BYTE* srcBuffer;
        size_t totalTestSize, totalGenSize, cSize;
        XXH64_state_t xxhState;
        U64 crcOrig;
        U32 resetAllowed = 1;
        size_t maxTestSize;

        /* init */
        FUZ_rand(&coreSeed);
        lseed = coreSeed ^ prime32;
        if (nbTests >= testNb) {
            DISPLAYUPDATE(2, "\r%6u/%6u    ", testNb, nbTests);
        } else {
            DISPLAYUPDATE(2, "\r%6u        ", testNb);
        }

        /* states full reset (deliberately not synchronized) */
        /* some issues can only happen when reusing states */
        if ((FUZ_rand(&lseed) & 0xFF) == 131) {
            ZSTD_freeCStream(zc);
            zc = ZSTD_createCStream();
            CHECK(zc==NULL, "ZSTD_createCStream : allocation error");
            resetAllowed=0;
        }
        if ((FUZ_rand(&lseed) & 0xFF) == 132) {
            ZSTD_freeDStream(zd);
            zd = ZSTD_createDStream();
            CHECK(zd==NULL, "ZSTD_createDStream : allocation error");
            CHECK_Z( ZSTD_initDStream_usingDict(zd, NULL, 0) );  /* ensure at least one init */
        }

        /* srcBuffer selection [0-4] */
        {   U32 buffNb = FUZ_rand(&lseed) & 0x7F;
            if (buffNb & 7) buffNb=2;   /* most common : compressible (P) */
            else {
                buffNb >>= 3;
                if (buffNb & 7) {
                    const U32 tnb[2] = { 1, 3 };   /* barely/highly compressible */
                    buffNb = tnb[buffNb >> 3];
                } else {
                    const U32 tnb[2] = { 0, 4 };   /* not compressible / sparse */
                    buffNb = tnb[buffNb >> 3];
            }   }
            srcBuffer = cNoiseBuffer[buffNb];
        }

        /* compression init */
        if ((FUZ_rand(&lseed)&1) /* at beginning, to keep same nb of rand */
            && oldTestLog /* at least one test happened */ && resetAllowed) {
            maxTestSize = FUZ_randomLength(&lseed, oldTestLog+2);
            maxTestSize = MIN(maxTestSize, srcBufferSize-16);
            {   U64 const pledgedSrcSize = (FUZ_rand(&lseed) & 3) ? 0 : maxTestSize;
                CHECK_Z( ZSTD_resetCStream(zc, pledgedSrcSize) );
            }
        } else {
            U32 const testLog = FUZ_rand(&lseed) % maxSrcLog;
            U32 const dictLog = FUZ_rand(&lseed) % maxSrcLog;
            U32 const cLevelCandidate = ( FUZ_rand(&lseed) %
                                (ZSTD_maxCLevel() -
                                (MAX(testLog, dictLog) / 3)))
                                 + 1;
            U32 const cLevel = MIN(cLevelCandidate, cLevelMax);
            maxTestSize = FUZ_rLogLength(&lseed, testLog);
            oldTestLog = testLog;
            /* random dictionary selection */
            dictSize  = ((FUZ_rand(&lseed)&7)==1) ? FUZ_rLogLength(&lseed, dictLog) : 0;
            {   size_t const dictStart = FUZ_rand(&lseed) % (srcBufferSize - dictSize);
                dict = srcBuffer + dictStart;
            }
            {   U64 const pledgedSrcSize = (FUZ_rand(&lseed) & 3) ? ZSTD_CONTENTSIZE_UNKNOWN : maxTestSize;
                ZSTD_parameters params = ZSTD_getParams(cLevel, pledgedSrcSize, dictSize);
                params.fParams.checksumFlag = FUZ_rand(&lseed) & 1;
                params.fParams.noDictIDFlag = FUZ_rand(&lseed) & 1;
                params.fParams.contentSizeFlag = FUZ_rand(&lseed) & 1;
                CHECK_Z ( ZSTD_initCStream_advanced(zc, dict, dictSize, params, pledgedSrcSize) );
        }   }

        /* multi-segments compression test */
        XXH64_reset(&xxhState, 0);
        {   ZSTD_outBuffer outBuff = { cBuffer, cBufferSize, 0 } ;
            U32 n;
            for (n=0, cSize=0, totalTestSize=0 ; totalTestSize < maxTestSize ; n++) {
                /* compress random chunks into randomly sized dst buffers */
                {   size_t const randomSrcSize = FUZ_randomLength(&lseed, maxSampleLog);
                    size_t const srcSize = MIN(maxTestSize-totalTestSize, randomSrcSize);
                    size_t const srcStart = FUZ_rand(&lseed) % (srcBufferSize - srcSize);
                    size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                    size_t const dstBuffSize = MIN(cBufferSize - cSize, randomDstSize);
                    ZSTD_inBuffer inBuff = { srcBuffer+srcStart, srcSize, 0 };
                    outBuff.size = outBuff.pos + dstBuffSize;

                    CHECK_Z( ZSTD_compressStream(zc, &outBuff, &inBuff) );

                    XXH64_update(&xxhState, srcBuffer+srcStart, inBuff.pos);
                    memcpy(copyBuffer+totalTestSize, srcBuffer+srcStart, inBuff.pos);
                    totalTestSize += inBuff.pos;
                }

                /* random flush operation, to mess around */
                if ((FUZ_rand(&lseed) & 15) == 0) {
                    size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                    size_t const adjustedDstSize = MIN(cBufferSize - cSize, randomDstSize);
                    outBuff.size = outBuff.pos + adjustedDstSize;
                    CHECK_Z( ZSTD_flushStream(zc, &outBuff) );
            }   }

            /* final frame epilogue */
            {   size_t remainingToFlush = (size_t)(-1);
                while (remainingToFlush) {
                    size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                    size_t const adjustedDstSize = MIN(cBufferSize - cSize, randomDstSize);
                    outBuff.size = outBuff.pos + adjustedDstSize;
                    remainingToFlush = ZSTD_endStream(zc, &outBuff);
                    CHECK (ZSTD_isError(remainingToFlush), "end error : %s", ZSTD_getErrorName(remainingToFlush));
            }   }
            crcOrig = XXH64_digest(&xxhState);
            cSize = outBuff.pos;
        }

        /* multi - fragments decompression test */
        if (!dictSize /* don't reset if dictionary : could be different */ && (FUZ_rand(&lseed) & 1)) {
            CHECK_Z ( ZSTD_resetDStream(zd) );
        } else {
            CHECK_Z ( ZSTD_initDStream_usingDict(zd, dict, dictSize) );
        }
        {   size_t decompressionResult = 1;
            ZSTD_inBuffer  inBuff = { cBuffer, cSize, 0 };
            ZSTD_outBuffer outBuff= { dstBuffer, dstBufferSize, 0 };
            for (totalGenSize = 0 ; decompressionResult ; ) {
                size_t const readCSrcSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const dstBuffSize = MIN(dstBufferSize - totalGenSize, randomDstSize);
                inBuff.size = inBuff.pos + readCSrcSize;
                outBuff.size = outBuff.pos + dstBuffSize;
                decompressionResult = ZSTD_decompressStream(zd, &outBuff, &inBuff);
                if (ZSTD_getErrorCode(decompressionResult) == ZSTD_error_checksum_wrong) {
                    DISPLAY("checksum error : \n");
                    findDiff(copyBuffer, dstBuffer, totalTestSize);
                }
                CHECK( ZSTD_isError(decompressionResult), "decompression error : %s",
                       ZSTD_getErrorName(decompressionResult) );
            }
            CHECK (decompressionResult != 0, "frame not fully decoded");
            CHECK (outBuff.pos != totalTestSize, "decompressed data : wrong size (%u != %u)",
                    (unsigned)outBuff.pos, (unsigned)totalTestSize);
            CHECK (inBuff.pos != cSize, "compressed data should be fully read")
            {   U64 const crcDest = XXH64(dstBuffer, totalTestSize, 0);
                if (crcDest!=crcOrig) findDiff(copyBuffer, dstBuffer, totalTestSize);
                CHECK (crcDest!=crcOrig, "decompressed data corrupted");
        }   }

        /*=====   noisy/erroneous src decompression test   =====*/

        /* add some noise */
        {   U32 const nbNoiseChunks = (FUZ_rand(&lseed) & 7) + 2;
            U32 nn; for (nn=0; nn<nbNoiseChunks; nn++) {
                size_t const randomNoiseSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const noiseSize  = MIN((cSize/3) , randomNoiseSize);
                size_t const noiseStart = FUZ_rand(&lseed) % (srcBufferSize - noiseSize);
                size_t const cStart = FUZ_rand(&lseed) % (cSize - noiseSize);
                memcpy(cBuffer+cStart, srcBuffer+noiseStart, noiseSize);
        }   }

        /* try decompression on noisy data */
        CHECK_Z( ZSTD_initDStream(zd_noise) );   /* note : no dictionary */
        {   ZSTD_inBuffer  inBuff = { cBuffer, cSize, 0 };
            ZSTD_outBuffer outBuff= { dstBuffer, dstBufferSize, 0 };
            while (outBuff.pos < dstBufferSize) {
                size_t const randomCSrcSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const adjustedDstSize = MIN(dstBufferSize - outBuff.pos, randomDstSize);
                size_t const adjustedCSrcSize = MIN(cSize - inBuff.pos, randomCSrcSize);
                outBuff.size = outBuff.pos + adjustedDstSize;
                inBuff.size  = inBuff.pos + adjustedCSrcSize;
                {   size_t const decompressError = ZSTD_decompressStream(zd, &outBuff, &inBuff);
                    if (ZSTD_isError(decompressError)) break;   /* error correctly detected */
                    /* No forward progress possible */
                    if (outBuff.pos < outBuff.size && inBuff.pos == cSize) break;
    }   }   }   }
    DISPLAY("\r%u fuzzer tests completed   \n", testNb);

_cleanup:
    ZSTD_freeCStream(zc);
    ZSTD_freeDStream(zd);
    ZSTD_freeDStream(zd_noise);
    free(cNoiseBuffer[0]);
    free(cNoiseBuffer[1]);
    free(cNoiseBuffer[2]);
    free(cNoiseBuffer[3]);
    free(cNoiseBuffer[4]);
    free(copyBuffer);
    free(cBuffer);
    free(dstBuffer);
    return result;

_output_error:
    result = 1;
    goto _cleanup;
}


/* fuzzing ZSTDMT_* interface */
static int fuzzerTests_MT(U32 seed, int nbTests, int startTest,
                          double compressibility, int bigTests)
{
    const U32 maxSrcLog = bigTests ? 24 : 22;
    static const U32 maxSampleLog = 19;
    size_t const srcBufferSize = (size_t)1<<maxSrcLog;
    BYTE* cNoiseBuffer[5];
    size_t const copyBufferSize= srcBufferSize + (1<<maxSampleLog);
    BYTE*  const copyBuffer = (BYTE*)malloc (copyBufferSize);
    size_t const cBufferSize   = ZSTD_compressBound(srcBufferSize);
    BYTE*  const cBuffer = (BYTE*)malloc (cBufferSize);
    size_t const dstBufferSize = srcBufferSize;
    BYTE*  const dstBuffer = (BYTE*)malloc (dstBufferSize);
    U32 result = 0;
    int testNb = 0;
    U32 coreSeed = seed;
    int nbThreads = 2;
    ZSTDMT_CCtx* zc = ZSTDMT_createCCtx(nbThreads);   /* will be reset sometimes */
    ZSTD_DStream* zd = ZSTD_createDStream();   /* will be reset sometimes */
    ZSTD_DStream* const zd_noise = ZSTD_createDStream();
    UTIL_time_t const startClock = UTIL_getTime();
    const BYTE* dict=NULL;   /* can keep same dict on 2 consecutive tests */
    size_t dictSize = 0;
    int const cLevelMax = bigTests ? (U32)ZSTD_maxCLevel()-1 : g_cLevelMax_smallTests;
    U32 const nbThreadsMax = bigTests ? 4 : 2;

    /* allocations */
    cNoiseBuffer[0] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[1] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[2] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[3] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[4] = (BYTE*)malloc (srcBufferSize);
    CHECK (!cNoiseBuffer[0] || !cNoiseBuffer[1] || !cNoiseBuffer[2] || !cNoiseBuffer[3] || !cNoiseBuffer[4] ||
           !copyBuffer || !dstBuffer || !cBuffer || !zc || !zd || !zd_noise ,
           "Not enough memory, fuzzer tests cancelled");

    /* Create initial samples */
    RDG_genBuffer(cNoiseBuffer[0], srcBufferSize, 0.00, 0., coreSeed);    /* pure noise */
    RDG_genBuffer(cNoiseBuffer[1], srcBufferSize, 0.05, 0., coreSeed);    /* barely compressible */
    RDG_genBuffer(cNoiseBuffer[2], srcBufferSize, compressibility, 0., coreSeed);
    RDG_genBuffer(cNoiseBuffer[3], srcBufferSize, 0.95, 0., coreSeed);    /* highly compressible */
    RDG_genBuffer(cNoiseBuffer[4], srcBufferSize, 1.00, 0., coreSeed);    /* sparse content */
    memset(copyBuffer, 0x65, copyBufferSize);                             /* make copyBuffer considered initialized */
    ZSTD_initDStream_usingDict(zd, NULL, 0);  /* ensure at least one init */
    DISPLAYLEVEL(6, "Creating initial context with %i threads \n", nbThreads);

    /* catch up testNb */
    for (testNb=1; testNb < startTest; testNb++)
        FUZ_rand(&coreSeed);

    /* test loop */
    for ( ; (testNb <= nbTests) || (UTIL_clockSpanMicro(startClock) < g_clockTime) ; testNb++ ) {
        U32 lseed;
        const BYTE* srcBuffer;
        size_t totalTestSize, totalGenSize, cSize;
        XXH64_state_t xxhState;
        U64 crcOrig;
        size_t maxTestSize;

        FUZ_rand(&coreSeed);
        if (nbTests >= testNb) {
            DISPLAYUPDATE(2, "\r%6u/%6u    ", testNb, nbTests);
        } else {
            DISPLAYUPDATE(2, "\r%6u         ", testNb);
        }
        lseed = coreSeed ^ prime32;

        /* states full reset (deliberately not synchronized) */
        /* some issues can only happen when reusing states */
        if ((FUZ_rand(&lseed) & 0xFF) == 131) {
            nbThreads = (FUZ_rand(&lseed) % nbThreadsMax) + 1;
            DISPLAYLEVEL(5, "Creating new context with %u threads \n", nbThreads);
            ZSTDMT_freeCCtx(zc);
            zc = ZSTDMT_createCCtx(nbThreads);
            CHECK(zc==NULL, "ZSTDMT_createCCtx allocation error")
        }
        if ((FUZ_rand(&lseed) & 0xFF) == 132) {
            ZSTD_freeDStream(zd);
            zd = ZSTD_createDStream();
            CHECK(zd==NULL, "ZSTDMT_createCCtx allocation error")
            ZSTD_initDStream_usingDict(zd, NULL, 0);  /* ensure at least one init */
        }

        /* srcBuffer selection [0-4] */
        {   U32 buffNb = FUZ_rand(&lseed) & 0x7F;
            if (buffNb & 7) buffNb=2;   /* most common : compressible (P) */
            else {
                buffNb >>= 3;
                if (buffNb & 7) {
                    const U32 tnb[2] = { 1, 3 };   /* barely/highly compressible */
                    buffNb = tnb[buffNb >> 3];
                } else {
                    const U32 tnb[2] = { 0, 4 };   /* not compressible / sparse */
                    buffNb = tnb[buffNb >> 3];
            }   }
            srcBuffer = cNoiseBuffer[buffNb];
        }

        /* compression init */
        {   U32 const testLog = FUZ_rand(&lseed) % maxSrcLog;
            U32 const dictLog = FUZ_rand(&lseed) % maxSrcLog;
            int const cLevelCandidate = ( FUZ_rand(&lseed)
                            % (ZSTD_maxCLevel() - (MAX(testLog, dictLog) / 2)) )
                            + 1;
            int const cLevelThreadAdjusted = cLevelCandidate - (nbThreads * 2) + 2;  /* reduce cLevel when multiple threads to reduce memory consumption */
            int const cLevelMin = MAX(cLevelThreadAdjusted, 1);  /* no negative cLevel yet */
            int const cLevel = MIN(cLevelMin, cLevelMax);
            maxTestSize = FUZ_rLogLength(&lseed, testLog);

            if (FUZ_rand(&lseed)&1) {   /* simple init */
                int const compressionLevel = (FUZ_rand(&lseed) % 5) + 1;
                DISPLAYLEVEL(5, "Init with compression level = %i \n", compressionLevel);
                CHECK_Z( ZSTDMT_initCStream(zc, compressionLevel) );
            } else {   /* advanced init */
                /* random dictionary selection */
                dictSize  = ((FUZ_rand(&lseed)&63)==1) ? FUZ_rLogLength(&lseed, dictLog) : 0;
                {   size_t const dictStart = FUZ_rand(&lseed) % (srcBufferSize - dictSize);
                    dict = srcBuffer + dictStart;
                }
                {   U64 const pledgedSrcSize = (FUZ_rand(&lseed) & 3) ? ZSTD_CONTENTSIZE_UNKNOWN : maxTestSize;
                    ZSTD_parameters params = ZSTD_getParams(cLevel, pledgedSrcSize, dictSize);
                    DISPLAYLEVEL(5, "Init with windowLog = %u, pledgedSrcSize = %u, dictSize = %u \n",
                        params.cParams.windowLog, (unsigned)pledgedSrcSize, (unsigned)dictSize);
                    params.fParams.checksumFlag = FUZ_rand(&lseed) & 1;
                    params.fParams.noDictIDFlag = FUZ_rand(&lseed) & 1;
                    params.fParams.contentSizeFlag = FUZ_rand(&lseed) & 1;
                    DISPLAYLEVEL(5, "checksumFlag : %u \n", params.fParams.checksumFlag);
                    CHECK_Z( ZSTDMT_setMTCtxParameter(zc, ZSTDMT_p_overlapLog, FUZ_rand(&lseed) % 12) );
                    CHECK_Z( ZSTDMT_setMTCtxParameter(zc, ZSTDMT_p_jobSize, FUZ_rand(&lseed) % (2*maxTestSize+1)) );   /* custom job size */
                    CHECK_Z( ZSTDMT_initCStream_advanced(zc, dict, dictSize, params, pledgedSrcSize) );
        }   }   }

        /* multi-segments compression test */
        XXH64_reset(&xxhState, 0);
        {   ZSTD_outBuffer outBuff = { cBuffer, cBufferSize, 0 } ;
            U32 n;
            for (n=0, cSize=0, totalTestSize=0 ; totalTestSize < maxTestSize ; n++) {
                /* compress random chunks into randomly sized dst buffers */
                {   size_t const randomSrcSize = FUZ_randomLength(&lseed, maxSampleLog);
                    size_t const srcSize = MIN (maxTestSize-totalTestSize, randomSrcSize);
                    size_t const srcStart = FUZ_rand(&lseed) % (srcBufferSize - srcSize);
                    size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                    size_t const dstBuffSize = MIN(cBufferSize - cSize, randomDstSize);
                    ZSTD_inBuffer inBuff = { srcBuffer+srcStart, srcSize, 0 };
                    outBuff.size = outBuff.pos + dstBuffSize;

                    DISPLAYLEVEL(6, "Sending %u bytes to compress \n", (unsigned)srcSize);
                    CHECK_Z( ZSTDMT_compressStream(zc, &outBuff, &inBuff) );
                    DISPLAYLEVEL(6, "%u bytes read by ZSTDMT_compressStream \n", (unsigned)inBuff.pos);

                    XXH64_update(&xxhState, srcBuffer+srcStart, inBuff.pos);
                    memcpy(copyBuffer+totalTestSize, srcBuffer+srcStart, inBuff.pos);
                    totalTestSize += inBuff.pos;
                }

                /* random flush operation, to mess around */
                if ((FUZ_rand(&lseed) & 15) == 0) {
                    size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                    size_t const adjustedDstSize = MIN(cBufferSize - cSize, randomDstSize);
                    size_t const previousPos = outBuff.pos;
                    outBuff.size = outBuff.pos + adjustedDstSize;
                    DISPLAYLEVEL(5, "Flushing into dst buffer of size %u \n", (unsigned)adjustedDstSize);
                    CHECK_Z( ZSTDMT_flushStream(zc, &outBuff) );
                    assert(outBuff.pos >= previousPos);
                    DISPLAYLEVEL(6, "%u bytes flushed by ZSTDMT_flushStream \n", (unsigned)(outBuff.pos-previousPos));
            }   }

            /* final frame epilogue */
            {   size_t remainingToFlush = (size_t)(-1);
                while (remainingToFlush) {
                    size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                    size_t const adjustedDstSize = MIN(cBufferSize - cSize, randomDstSize);
                    size_t const previousPos = outBuff.pos;
                    outBuff.size = outBuff.pos + adjustedDstSize;
                    DISPLAYLEVEL(5, "Ending into dst buffer of size %u \n", (unsigned)adjustedDstSize);
                    remainingToFlush = ZSTDMT_endStream(zc, &outBuff);
                    CHECK (ZSTD_isError(remainingToFlush), "ZSTDMT_endStream error : %s", ZSTD_getErrorName(remainingToFlush));
                    assert(outBuff.pos >= previousPos);
                    DISPLAYLEVEL(6, "%u bytes flushed by ZSTDMT_endStream \n", (unsigned)(outBuff.pos-previousPos));
                    DISPLAYLEVEL(5, "endStream : remainingToFlush : %u \n", (unsigned)remainingToFlush);
            }   }
            crcOrig = XXH64_digest(&xxhState);
            cSize = outBuff.pos;
            DISPLAYLEVEL(5, "Frame completed : %u bytes compressed into %u bytes \n",
                            (unsigned)totalTestSize, (unsigned)cSize);
        }

        /* multi - fragments decompression test */
        assert(totalTestSize < dstBufferSize);
        memset(dstBuffer, 170, totalTestSize);   /* init dest area */
        if (!dictSize /* don't reset if dictionary : could be different */ && (FUZ_rand(&lseed) & 1)) {
            CHECK_Z( ZSTD_resetDStream(zd) );
        } else {
            CHECK_Z( ZSTD_initDStream_usingDict(zd, dict, dictSize) );
        }
        {   size_t decompressionResult = 1;
            ZSTD_inBuffer  inBuff = { cBuffer, cSize, 0 };
            ZSTD_outBuffer outBuff= { dstBuffer, dstBufferSize, 0 };
            for (totalGenSize = 0 ; decompressionResult ; ) {
                size_t const readCSrcSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const dstBuffSize = MIN(dstBufferSize - totalGenSize, randomDstSize);
                inBuff.size = inBuff.pos + readCSrcSize;
                outBuff.size = outBuff.pos + dstBuffSize;
                DISPLAYLEVEL(6, "ZSTD_decompressStream input %u bytes into outBuff %u bytes \n",
                                (unsigned)readCSrcSize, (unsigned)dstBuffSize);
                decompressionResult = ZSTD_decompressStream(zd, &outBuff, &inBuff);
                if (ZSTD_isError(decompressionResult)) {
                    DISPLAY("ZSTD_decompressStream error : %s \n", ZSTD_getErrorName(decompressionResult));
                    findDiff(copyBuffer, dstBuffer, totalTestSize);
                }
                CHECK (ZSTD_isError(decompressionResult), "decompression error : %s", ZSTD_getErrorName(decompressionResult));
                DISPLAYLEVEL(6, "total ingested (inBuff.pos) = %u and produced (outBuff.pos) = %u \n",
                                (unsigned)inBuff.pos, (unsigned)outBuff.pos);
            }
            CHECK (outBuff.pos != totalTestSize,
                    "decompressed data : wrong size (%u != %u)",
                    (unsigned)outBuff.pos, (unsigned)totalTestSize );
            CHECK (inBuff.pos != cSize,
                    "compressed data should be fully read (%u != %u)",
                    (unsigned)inBuff.pos, (unsigned)cSize );
            {   U64 const crcDest = XXH64(dstBuffer, totalTestSize, 0);
                if (crcDest!=crcOrig) findDiff(copyBuffer, dstBuffer, totalTestSize);
                CHECK (crcDest!=crcOrig, "decompressed data corrupted");
        }   }

        /*=====   noisy/erroneous src decompression test   =====*/

        /* add some noise */
        {   U32 const nbNoiseChunks = (FUZ_rand(&lseed) & 7) + 2;
            U32 nn; for (nn=0; nn<nbNoiseChunks; nn++) {
                size_t const randomNoiseSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const noiseSize  = MIN((cSize/3) , randomNoiseSize);
                size_t const noiseStart = FUZ_rand(&lseed) % (srcBufferSize - noiseSize);
                size_t const cStart = FUZ_rand(&lseed) % (cSize - noiseSize);
                memcpy(cBuffer+cStart, srcBuffer+noiseStart, noiseSize);
        }   }

        /* try decompression on noisy data */
        CHECK_Z( ZSTD_initDStream(zd_noise) );   /* note : no dictionary */
        {   ZSTD_inBuffer  inBuff = { cBuffer, cSize, 0 };
            ZSTD_outBuffer outBuff= { dstBuffer, dstBufferSize, 0 };
            while (outBuff.pos < dstBufferSize) {
                size_t const randomCSrcSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const adjustedDstSize = MIN(dstBufferSize - outBuff.pos, randomDstSize);
                size_t const adjustedCSrcSize = MIN(cSize - inBuff.pos, randomCSrcSize);
                outBuff.size = outBuff.pos + adjustedDstSize;
                inBuff.size  = inBuff.pos + adjustedCSrcSize;
                {   size_t const decompressError = ZSTD_decompressStream(zd, &outBuff, &inBuff);
                    if (ZSTD_isError(decompressError)) break;   /* error correctly detected */
                    /* No forward progress possible */
                    if (outBuff.pos < outBuff.size && inBuff.pos == cSize) break;
    }   }   }   }
    DISPLAY("\r%u fuzzer tests completed   \n", testNb);

_cleanup:
    ZSTDMT_freeCCtx(zc);
    ZSTD_freeDStream(zd);
    ZSTD_freeDStream(zd_noise);
    free(cNoiseBuffer[0]);
    free(cNoiseBuffer[1]);
    free(cNoiseBuffer[2]);
    free(cNoiseBuffer[3]);
    free(cNoiseBuffer[4]);
    free(copyBuffer);
    free(cBuffer);
    free(dstBuffer);
    return result;

_output_error:
    result = 1;
    goto _cleanup;
}

/** If useOpaqueAPI, sets param in cctxParams.
 *  Otherwise, sets the param in zc. */
static size_t setCCtxParameter(ZSTD_CCtx* zc, ZSTD_CCtx_params* cctxParams,
                               ZSTD_cParameter param, unsigned value,
                               int useOpaqueAPI)
{
    if (useOpaqueAPI) {
        return ZSTD_CCtxParam_setParameter(cctxParams, param, value);
    } else {
        return ZSTD_CCtx_setParameter(zc, param, value);
    }
}

/* Tests for ZSTD_compress_generic() API */
static int fuzzerTests_newAPI(U32 seed, int nbTests, int startTest,
                              double compressibility, int bigTests)
{
    U32 const maxSrcLog = bigTests ? 24 : 22;
    static const U32 maxSampleLog = 19;
    size_t const srcBufferSize = (size_t)1<<maxSrcLog;
    BYTE* cNoiseBuffer[5];
    size_t const copyBufferSize= srcBufferSize + (1<<maxSampleLog);
    BYTE*  const copyBuffer = (BYTE*)malloc (copyBufferSize);
    size_t const cBufferSize   = ZSTD_compressBound(srcBufferSize);
    BYTE*  const cBuffer = (BYTE*)malloc (cBufferSize);
    size_t const dstBufferSize = srcBufferSize;
    BYTE*  const dstBuffer = (BYTE*)malloc (dstBufferSize);
    U32 result = 0;
    int testNb = 0;
    U32 coreSeed = seed;
    ZSTD_CCtx* zc = ZSTD_createCCtx();   /* will be reset sometimes */
    ZSTD_DStream* zd = ZSTD_createDStream();   /* will be reset sometimes */
    ZSTD_DStream* const zd_noise = ZSTD_createDStream();
    UTIL_time_t const startClock = UTIL_getTime();
    const BYTE* dict = NULL;   /* can keep same dict on 2 consecutive tests */
    size_t dictSize = 0;
    U32 oldTestLog = 0;
    U32 windowLogMalus = 0;   /* can survive between 2 loops */
    U32 const cLevelMax = bigTests ? (U32)ZSTD_maxCLevel()-1 : g_cLevelMax_smallTests;
    U32 const nbThreadsMax = bigTests ? 4 : 2;
    ZSTD_CCtx_params* cctxParams = ZSTD_createCCtxParams();

    /* allocations */
    cNoiseBuffer[0] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[1] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[2] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[3] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[4] = (BYTE*)malloc (srcBufferSize);
    CHECK (!cNoiseBuffer[0] || !cNoiseBuffer[1] || !cNoiseBuffer[2] || !cNoiseBuffer[3] || !cNoiseBuffer[4] ||
           !copyBuffer || !dstBuffer || !cBuffer || !zc || !zd || !zd_noise ,
           "Not enough memory, fuzzer tests cancelled");

    /* Create initial samples */
    RDG_genBuffer(cNoiseBuffer[0], srcBufferSize, 0.00, 0., coreSeed);    /* pure noise */
    RDG_genBuffer(cNoiseBuffer[1], srcBufferSize, 0.05, 0., coreSeed);    /* barely compressible */
    RDG_genBuffer(cNoiseBuffer[2], srcBufferSize, compressibility, 0., coreSeed);
    RDG_genBuffer(cNoiseBuffer[3], srcBufferSize, 0.95, 0., coreSeed);    /* highly compressible */
    RDG_genBuffer(cNoiseBuffer[4], srcBufferSize, 1.00, 0., coreSeed);    /* sparse content */
    memset(copyBuffer, 0x65, copyBufferSize);                             /* make copyBuffer considered initialized */
    CHECK_Z( ZSTD_initDStream_usingDict(zd, NULL, 0) );   /* ensure at least one init */

    /* catch up testNb */
    for (testNb=1; testNb < startTest; testNb++)
        FUZ_rand(&coreSeed);

    /* test loop */
    for ( ; (testNb <= nbTests) || (UTIL_clockSpanMicro(startClock) < g_clockTime) ; testNb++ ) {
        U32 lseed;
        int opaqueAPI;
        const BYTE* srcBuffer;
        size_t totalTestSize, totalGenSize, cSize;
        XXH64_state_t xxhState;
        U64 crcOrig;
        U32 resetAllowed = 1;
        size_t maxTestSize;
        ZSTD_parameters savedParams;

        /* init */
        if (nbTests >= testNb) { DISPLAYUPDATE(2, "\r%6u/%6u    ", testNb, nbTests); }
        else { DISPLAYUPDATE(2, "\r%6u          ", testNb); }
        FUZ_rand(&coreSeed);
        lseed = coreSeed ^ prime32;
        DISPLAYLEVEL(5, " ***  Test %u  *** \n", testNb);
        opaqueAPI = FUZ_rand(&lseed) & 1;

        /* states full reset (deliberately not synchronized) */
        /* some issues can only happen when reusing states */
        if ((FUZ_rand(&lseed) & 0xFF) == 131) {
            DISPLAYLEVEL(5, "Creating new context \n");
            ZSTD_freeCCtx(zc);
            zc = ZSTD_createCCtx();
            CHECK(zc == NULL, "ZSTD_createCCtx allocation error");
            resetAllowed = 0;
        }
        if ((FUZ_rand(&lseed) & 0xFF) == 132) {
            ZSTD_freeDStream(zd);
            zd = ZSTD_createDStream();
            CHECK(zd == NULL, "ZSTD_createDStream allocation error");
            ZSTD_initDStream_usingDict(zd, NULL, 0);  /* ensure at least one init */
        }

        /* srcBuffer selection [0-4] */
        {   U32 buffNb = FUZ_rand(&lseed) & 0x7F;
            if (buffNb & 7) buffNb=2;   /* most common : compressible (P) */
            else {
                buffNb >>= 3;
                if (buffNb & 7) {
                    const U32 tnb[2] = { 1, 3 };   /* barely/highly compressible */
                    buffNb = tnb[buffNb >> 3];
                } else {
                    const U32 tnb[2] = { 0, 4 };   /* not compressible / sparse */
                    buffNb = tnb[buffNb >> 3];
            }   }
            srcBuffer = cNoiseBuffer[buffNb];
        }

        /* compression init */
        CHECK_Z( ZSTD_CCtx_loadDictionary(zc, NULL, 0) );   /* cancel previous dict /*/
        if ((FUZ_rand(&lseed)&1) /* at beginning, to keep same nb of rand */
          && oldTestLog   /* at least one test happened */
          && resetAllowed) {
            /* just set a compression level */
            maxTestSize = FUZ_randomLength(&lseed, oldTestLog+2);
            if (maxTestSize >= srcBufferSize) maxTestSize = srcBufferSize-1;
            {   int const compressionLevel = (FUZ_rand(&lseed) % 5) + 1;
                DISPLAYLEVEL(5, "t%u : compression level : %i \n", testNb, compressionLevel);
                CHECK_Z (setCCtxParameter(zc, cctxParams, ZSTD_c_compressionLevel, compressionLevel, opaqueAPI) );
            }
        } else {
            U32 const testLog = FUZ_rand(&lseed) % maxSrcLog;
            U32 const dictLog = FUZ_rand(&lseed) % maxSrcLog;
            U32 const cLevelCandidate = (FUZ_rand(&lseed) %
                               (ZSTD_maxCLevel() -
                               (MAX(testLog, dictLog) / 2))) +
                               1;
            int const cLevel = MIN(cLevelCandidate, cLevelMax);
            DISPLAYLEVEL(5, "t%i: base cLevel : %u \n", testNb, cLevel);
            maxTestSize = FUZ_rLogLength(&lseed, testLog);
            DISPLAYLEVEL(5, "t%i: maxTestSize : %u \n", testNb, (unsigned)maxTestSize);
            oldTestLog = testLog;
            /* random dictionary selection */
            dictSize  = ((FUZ_rand(&lseed)&63)==1) ? FUZ_rLogLength(&lseed, dictLog) : 0;
            {   size_t const dictStart = FUZ_rand(&lseed) % (srcBufferSize - dictSize);
                dict = srcBuffer + dictStart;
                if (!dictSize) dict=NULL;
            }
            {   U64 const pledgedSrcSize = (FUZ_rand(&lseed) & 3) ? ZSTD_CONTENTSIZE_UNKNOWN : maxTestSize;
                ZSTD_compressionParameters cParams = ZSTD_getCParams(cLevel, pledgedSrcSize, dictSize);
                const U32 windowLogMax = bigTests ? 24 : 20;
                const U32 searchLogMax = bigTests ? 15 : 13;
                if (dictSize)
                    DISPLAYLEVEL(5, "t%u: with dictionary of size : %zu \n", testNb, dictSize);

                /* mess with compression parameters */
                cParams.windowLog += (FUZ_rand(&lseed) & 3) - 1;
                cParams.windowLog = MIN(windowLogMax, cParams.windowLog);
                cParams.hashLog += (FUZ_rand(&lseed) & 3) - 1;
                cParams.chainLog += (FUZ_rand(&lseed) & 3) - 1;
                cParams.searchLog += (FUZ_rand(&lseed) & 3) - 1;
                cParams.searchLog = MIN(searchLogMax, cParams.searchLog);
                cParams.minMatch += (FUZ_rand(&lseed) & 3) - 1;
                cParams.targetLength = (U32)((cParams.targetLength + 1 ) * (0.5 + ((double)(FUZ_rand(&lseed) & 127) / 128)));
                cParams = ZSTD_adjustCParams(cParams, pledgedSrcSize, dictSize);

                if (FUZ_rand(&lseed) & 1) {
                    DISPLAYLEVEL(5, "t%u: windowLog : %u \n", testNb, cParams.windowLog);
                    CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_windowLog, cParams.windowLog, opaqueAPI) );
                    assert(cParams.windowLog >= ZSTD_WINDOWLOG_MIN);   /* guaranteed by ZSTD_adjustCParams() */
                    windowLogMalus = (cParams.windowLog - ZSTD_WINDOWLOG_MIN) / 5;
                }
                if (FUZ_rand(&lseed) & 1) {
                    DISPLAYLEVEL(5, "t%u: hashLog : %u \n", testNb, cParams.hashLog);
                    CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_hashLog, cParams.hashLog, opaqueAPI) );
                }
                if (FUZ_rand(&lseed) & 1) {
                    DISPLAYLEVEL(5, "t%u: chainLog : %u \n", testNb, cParams.chainLog);
                    CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_chainLog, cParams.chainLog, opaqueAPI) );
                }
                if (FUZ_rand(&lseed) & 1) CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_searchLog, cParams.searchLog, opaqueAPI) );
                if (FUZ_rand(&lseed) & 1) CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_minMatch, cParams.minMatch, opaqueAPI) );
                if (FUZ_rand(&lseed) & 1) CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_targetLength, cParams.targetLength, opaqueAPI) );

                /* mess with long distance matching parameters */
                if (bigTests) {
                    if (FUZ_rand(&lseed) & 1) CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_enableLongDistanceMatching, FUZ_rand(&lseed) & 63, opaqueAPI) );
                    if (FUZ_rand(&lseed) & 3) CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_ldmHashLog, FUZ_randomClampedLength(&lseed, ZSTD_HASHLOG_MIN, 23), opaqueAPI) );
                    if (FUZ_rand(&lseed) & 3) CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_ldmMinMatch, FUZ_randomClampedLength(&lseed, ZSTD_LDM_MINMATCH_MIN, ZSTD_LDM_MINMATCH_MAX), opaqueAPI) );
                    if (FUZ_rand(&lseed) & 3) CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_ldmBucketSizeLog, FUZ_randomClampedLength(&lseed, ZSTD_LDM_BUCKETSIZELOG_MIN, ZSTD_LDM_BUCKETSIZELOG_MAX), opaqueAPI) );
                    if (FUZ_rand(&lseed) & 3) CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_ldmHashRateLog, FUZ_randomClampedLength(&lseed, ZSTD_LDM_HASHRATELOG_MIN, ZSTD_LDM_HASHRATELOG_MAX), opaqueAPI) );
                }

                /* mess with frame parameters */
                if (FUZ_rand(&lseed) & 1) {
                    int const checksumFlag = FUZ_rand(&lseed) & 1;
                    DISPLAYLEVEL(5, "t%u: frame checksum : %u \n", testNb, checksumFlag);
                    CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_checksumFlag, checksumFlag, opaqueAPI) );
                }
                if (FUZ_rand(&lseed) & 1) CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_dictIDFlag, FUZ_rand(&lseed) & 1, opaqueAPI) );
                if (FUZ_rand(&lseed) & 1) CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_contentSizeFlag, FUZ_rand(&lseed) & 1, opaqueAPI) );
                if (FUZ_rand(&lseed) & 1) {
                    DISPLAYLEVEL(5, "t%u: pledgedSrcSize : %u \n", testNb, (unsigned)pledgedSrcSize);
                    CHECK_Z( ZSTD_CCtx_setPledgedSrcSize(zc, pledgedSrcSize) );
                }

                /* multi-threading parameters. Only adjust ocassionally for small tests. */
                if (bigTests || (FUZ_rand(&lseed) & 0xF) == 0xF) {
                    U32 const nbThreadsCandidate = (FUZ_rand(&lseed) & 4) + 1;
                    U32 const nbThreadsAdjusted = (windowLogMalus < nbThreadsCandidate) ? nbThreadsCandidate - windowLogMalus : 1;
                    int const nbThreads = MIN(nbThreadsAdjusted, nbThreadsMax);
                    DISPLAYLEVEL(5, "t%i: nbThreads : %u \n", testNb, nbThreads);
                    CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_nbWorkers, nbThreads, opaqueAPI) );
                    if (nbThreads > 1) {
                        U32 const jobLog = FUZ_rand(&lseed) % (testLog+1);
                        CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_overlapLog, FUZ_rand(&lseed) % 10, opaqueAPI) );
                        CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_jobSize, (U32)FUZ_rLogLength(&lseed, jobLog), opaqueAPI) );
                    }
                }
                /* Enable rsyncable mode 1 in 4 times. */
                setCCtxParameter(zc, cctxParams, ZSTD_c_rsyncable, (FUZ_rand(&lseed) % 4 == 0), opaqueAPI);

                if (FUZ_rand(&lseed) & 1) CHECK_Z( setCCtxParameter(zc, cctxParams, ZSTD_c_forceMaxWindow, FUZ_rand(&lseed) & 1, opaqueAPI) );

                /* Apply parameters */
                if (opaqueAPI) {
                    DISPLAYLEVEL(5, "t%u: applying CCtxParams \n", testNb);
                    CHECK_Z (ZSTD_CCtx_setParametersUsingCCtxParams(zc, cctxParams) );
                }

                if (FUZ_rand(&lseed) & 1) {
                    if (FUZ_rand(&lseed) & 1) {
                        CHECK_Z( ZSTD_CCtx_loadDictionary(zc, dict, dictSize) );
                    } else {
                        CHECK_Z( ZSTD_CCtx_loadDictionary_byReference(zc, dict, dictSize) );
                    }
                    if (dict && dictSize) {
                        /* test that compression parameters are rejected (correctly) after loading a non-NULL dictionary */
                        if (opaqueAPI) {
                            size_t const setError = ZSTD_CCtx_setParametersUsingCCtxParams(zc, cctxParams);
                            CHECK(!ZSTD_isError(setError), "ZSTD_CCtx_setParametersUsingCCtxParams should have failed");
                        } else {
                            size_t const setError = ZSTD_CCtx_setParameter(zc, ZSTD_c_windowLog, cParams.windowLog-1);
                            CHECK(!ZSTD_isError(setError), "ZSTD_CCtx_setParameter should have failed");
                        }
                    }
                } else {
                    CHECK_Z( ZSTD_CCtx_refPrefix(zc, dict, dictSize) );
                }
        }   }

        CHECK_Z(getCCtxParams(zc, &savedParams));

        /* multi-segments compression test */
        XXH64_reset(&xxhState, 0);
        {   ZSTD_outBuffer outBuff = { cBuffer, cBufferSize, 0 } ;
            for (cSize=0, totalTestSize=0 ; (totalTestSize < maxTestSize) ; ) {
                /* compress random chunks into randomly sized dst buffers */
                size_t const randomSrcSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const srcSize = MIN(maxTestSize-totalTestSize, randomSrcSize);
                size_t const srcStart = FUZ_rand(&lseed) % (srcBufferSize - srcSize);
                size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog+1);
                size_t const dstBuffSize = MIN(cBufferSize - cSize, randomDstSize);
                ZSTD_EndDirective const flush = (FUZ_rand(&lseed) & 15) ? ZSTD_e_continue : ZSTD_e_flush;
                ZSTD_inBuffer inBuff = { srcBuffer+srcStart, srcSize, 0 };
                outBuff.size = outBuff.pos + dstBuffSize;

                CHECK_Z( ZSTD_compressStream2(zc, &outBuff, &inBuff, flush) );
                DISPLAYLEVEL(6, "t%u: compress consumed %u bytes (total : %u) ; flush: %u (total : %u) \n",
                    testNb, (unsigned)inBuff.pos, (unsigned)(totalTestSize + inBuff.pos), (unsigned)flush, (unsigned)outBuff.pos);

                XXH64_update(&xxhState, srcBuffer+srcStart, inBuff.pos);
                memcpy(copyBuffer+totalTestSize, srcBuffer+srcStart, inBuff.pos);
                totalTestSize += inBuff.pos;
            }

            /* final frame epilogue */
            {   size_t remainingToFlush = 1;
                while (remainingToFlush) {
                    ZSTD_inBuffer inBuff = { NULL, 0, 0 };
                    size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog+1);
                    size_t const adjustedDstSize = MIN(cBufferSize - cSize, randomDstSize);
                    outBuff.size = outBuff.pos + adjustedDstSize;
                    DISPLAYLEVEL(6, "t%u: End-flush into dst buffer of size %u \n", testNb, (unsigned)adjustedDstSize);
                    remainingToFlush = ZSTD_compressStream2(zc, &outBuff, &inBuff, ZSTD_e_end);
                    DISPLAYLEVEL(6, "t%u: Total flushed so far : %u bytes \n", testNb, (unsigned)outBuff.pos);
                    CHECK( ZSTD_isError(remainingToFlush),
                          "ZSTD_compressStream2 w/ ZSTD_e_end error : %s",
                           ZSTD_getErrorName(remainingToFlush) );
            }   }
            crcOrig = XXH64_digest(&xxhState);
            cSize = outBuff.pos;
            DISPLAYLEVEL(5, "Frame completed : %zu bytes \n", cSize);
        }

        CHECK(badParameters(zc, savedParams), "CCtx params are wrong");

        /* multi - fragments decompression test */
        if (!dictSize /* don't reset if dictionary : could be different */ && (FUZ_rand(&lseed) & 1)) {
            DISPLAYLEVEL(5, "resetting DCtx (dict:%p) \n", dict);
            CHECK_Z( ZSTD_resetDStream(zd) );
        } else {
            if (dictSize)
                DISPLAYLEVEL(5, "using dictionary of size %zu \n", dictSize);
            CHECK_Z( ZSTD_initDStream_usingDict(zd, dict, dictSize) );
        }
        {   size_t decompressionResult = 1;
            ZSTD_inBuffer  inBuff = { cBuffer, cSize, 0 };
            ZSTD_outBuffer outBuff= { dstBuffer, dstBufferSize, 0 };
            for (totalGenSize = 0 ; decompressionResult ; ) {
                size_t const readCSrcSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const dstBuffSize = MIN(dstBufferSize - totalGenSize, randomDstSize);
                inBuff.size = inBuff.pos + readCSrcSize;
                outBuff.size = outBuff.pos + dstBuffSize;
                DISPLAYLEVEL(6, "decompression presented %u new bytes (pos:%u/%u)\n",
                                (unsigned)readCSrcSize, (unsigned)inBuff.pos, (unsigned)cSize);
                decompressionResult = ZSTD_decompressStream(zd, &outBuff, &inBuff);
                DISPLAYLEVEL(6, "so far: consumed = %u, produced = %u \n",
                                (unsigned)inBuff.pos, (unsigned)outBuff.pos);
                if (ZSTD_isError(decompressionResult)) {
                    DISPLAY("ZSTD_decompressStream error : %s \n", ZSTD_getErrorName(decompressionResult));
                    findDiff(copyBuffer, dstBuffer, totalTestSize);
                }
                CHECK (ZSTD_isError(decompressionResult), "decompression error : %s", ZSTD_getErrorName(decompressionResult));
                CHECK (inBuff.pos > cSize, "ZSTD_decompressStream consumes too much input : %u > %u ", (unsigned)inBuff.pos, (unsigned)cSize);
            }
            CHECK (inBuff.pos != cSize, "compressed data should be fully read (%u != %u)", (unsigned)inBuff.pos, (unsigned)cSize);
            CHECK (outBuff.pos != totalTestSize, "decompressed data : wrong size (%u != %u)", (unsigned)outBuff.pos, (unsigned)totalTestSize);
            {   U64 const crcDest = XXH64(dstBuffer, totalTestSize, 0);
                if (crcDest!=crcOrig) findDiff(copyBuffer, dstBuffer, totalTestSize);
                CHECK (crcDest!=crcOrig, "decompressed data corrupted");
        }   }

        /*=====   noisy/erroneous src decompression test   =====*/

        /* add some noise */
        {   U32 const nbNoiseChunks = (FUZ_rand(&lseed) & 7) + 2;
            U32 nn; for (nn=0; nn<nbNoiseChunks; nn++) {
                size_t const randomNoiseSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const noiseSize  = MIN((cSize/3) , randomNoiseSize);
                size_t const noiseStart = FUZ_rand(&lseed) % (srcBufferSize - noiseSize);
                size_t const cStart = FUZ_rand(&lseed) % (cSize - noiseSize);
                memcpy(cBuffer+cStart, srcBuffer+noiseStart, noiseSize);
        }   }

        /* try decompression on noisy data */
        CHECK_Z( ZSTD_initDStream(zd_noise) );   /* note : no dictionary */
        {   ZSTD_inBuffer  inBuff = { cBuffer, cSize, 0 };
            ZSTD_outBuffer outBuff= { dstBuffer, dstBufferSize, 0 };
            while (outBuff.pos < dstBufferSize) {
                size_t const randomCSrcSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const adjustedDstSize = MIN(dstBufferSize - outBuff.pos, randomDstSize);
                size_t const adjustedCSrcSize = MIN(cSize - inBuff.pos, randomCSrcSize);
                outBuff.size = outBuff.pos + adjustedDstSize;
                inBuff.size  = inBuff.pos + adjustedCSrcSize;
                {   size_t const decompressError = ZSTD_decompressStream(zd, &outBuff, &inBuff);
                    if (ZSTD_isError(decompressError)) break;   /* error correctly detected */
                    /* Good so far, but no more progress possible */
                    if (outBuff.pos < outBuff.size && inBuff.pos == cSize) break;
    }   }   }   }
    DISPLAY("\r%u fuzzer tests completed   \n", testNb-1);

_cleanup:
    ZSTD_freeCCtx(zc);
    ZSTD_freeDStream(zd);
    ZSTD_freeDStream(zd_noise);
    ZSTD_freeCCtxParams(cctxParams);
    free(cNoiseBuffer[0]);
    free(cNoiseBuffer[1]);
    free(cNoiseBuffer[2]);
    free(cNoiseBuffer[3]);
    free(cNoiseBuffer[4]);
    free(copyBuffer);
    free(cBuffer);
    free(dstBuffer);
    return result;

_output_error:
    result = 1;
    goto _cleanup;
}

/*-*******************************************************
*  Command line
*********************************************************/
static int FUZ_usage(const char* programName)
{
    DISPLAY( "Usage :\n");
    DISPLAY( "      %s [args]\n", programName);
    DISPLAY( "\n");
    DISPLAY( "Arguments :\n");
    DISPLAY( " -i#    : Nb of tests (default:%u) \n", nbTestsDefault);
    DISPLAY( " -s#    : Select seed (default:prompt user)\n");
    DISPLAY( " -t#    : Select starting test number (default:0)\n");
    DISPLAY( " -P#    : Select compressibility in %% (default:%i%%)\n", FUZ_COMPRESSIBILITY_DEFAULT);
    DISPLAY( " -v     : verbose\n");
    DISPLAY( " -p     : pause at the end\n");
    DISPLAY( " -h     : display help and exit\n");
    return 0;
}

typedef enum { simple_api, mt_api, advanced_api } e_api;

int main(int argc, const char** argv)
{
    U32 seed = 0;
    int seedset = 0;
    int nbTests = nbTestsDefault;
    int testNb = 0;
    int proba = FUZ_COMPRESSIBILITY_DEFAULT;
    int result = 0;
    int mainPause = 0;
    int bigTests = (sizeof(size_t) == 8);
    e_api selected_api = simple_api;
    const char* const programName = argv[0];
    int argNb;

    /* Check command line */
    for(argNb=1; argNb<argc; argNb++) {
        const char* argument = argv[argNb];
        assert(argument != NULL);

        /* Parsing commands. Aggregated commands are allowed */
        if (argument[0]=='-') {

            if (!strcmp(argument, "--mt")) { selected_api=mt_api; testNb += !testNb; continue; }
            if (!strcmp(argument, "--newapi")) { selected_api=advanced_api; testNb += !testNb; continue; }
            if (!strcmp(argument, "--no-big-tests")) { bigTests=0; continue; }

            argument++;
            while (*argument!=0) {
                switch(*argument)
                {
                case 'h':
                    return FUZ_usage(programName);

                case 'v':
                    argument++;
                    g_displayLevel++;
                    break;

                case 'q':
                    argument++;
                    g_displayLevel--;
                    break;

                case 'p': /* pause at the end */
                    argument++;
                    mainPause = 1;
                    break;

                case 'i':   /* limit tests by nb of iterations (default) */
                    argument++;
                    nbTests=0; g_clockTime=0;
                    while ((*argument>='0') && (*argument<='9')) {
                        nbTests *= 10;
                        nbTests += *argument - '0';
                        argument++;
                    }
                    break;

                case 'T':   /* limit tests by time */
                    argument++;
                    nbTests=0; g_clockTime=0;
                    while ((*argument>='0') && (*argument<='9')) {
                        g_clockTime *= 10;
                        g_clockTime += *argument - '0';
                        argument++;
                    }
                    if (*argument=='m') {    /* -T1m == -T60 */
                        g_clockTime *=60, argument++;
                        if (*argument=='n') argument++; /* -T1mn == -T60 */
                    } else if (*argument=='s') argument++; /* -T10s == -T10 */
                    g_clockTime *= SEC_TO_MICRO;
                    break;

                case 's':   /* manually select seed */
                    argument++;
                    seedset=1;
                    seed=0;
                    while ((*argument>='0') && (*argument<='9')) {
                        seed *= 10;
                        seed += *argument - '0';
                        argument++;
                    }
                    break;

                case 't':   /* select starting test number */
                    argument++;
                    testNb=0;
                    while ((*argument>='0') && (*argument<='9')) {
                        testNb *= 10;
                        testNb += *argument - '0';
                        argument++;
                    }
                    break;

                case 'P':   /* compressibility % */
                    argument++;
                    proba=0;
                    while ((*argument>='0') && (*argument<='9')) {
                        proba *= 10;
                        proba += *argument - '0';
                        argument++;
                    }
                    if (proba<0) proba=0;
                    if (proba>100) proba=100;
                    break;

                default:
                    return FUZ_usage(programName);
                }
    }   }   }   /* for(argNb=1; argNb<argc; argNb++) */

    /* Get Seed */
    DISPLAY("Starting zstream tester (%i-bits, %s)\n", (int)(sizeof(size_t)*8), ZSTD_VERSION_STRING);

    if (!seedset) {
        time_t const t = time(NULL);
        U32 const h = XXH32(&t, sizeof(t), 1);
        seed = h % 10000;
    }

    DISPLAY("Seed = %u\n", (unsigned)seed);
    if (proba!=FUZ_COMPRESSIBILITY_DEFAULT) DISPLAY("Compressibility : %i%%\n", proba);

    if (nbTests<=0) nbTests=1;

    if (testNb==0) {
        result = basicUnitTests(0, ((double)proba) / 100);  /* constant seed for predictability */
    }

    if (!result) {
        switch(selected_api)
        {
        case simple_api :
            result = fuzzerTests(seed, nbTests, testNb, ((double)proba) / 100, bigTests);
            break;
        case mt_api :
            result = fuzzerTests_MT(seed, nbTests, testNb, ((double)proba) / 100, bigTests);
            break;
        case advanced_api :
            result = fuzzerTests_newAPI(seed, nbTests, testNb, ((double)proba) / 100, bigTests);
            break;
        default :
            assert(0);   /* impossible */
        }
    }

    if (mainPause) {
        int unused;
        DISPLAY("Press Enter \n");
        unused = getchar();
        (void)unused;
    }
    return result;
}
