/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/* **************************************
*  Tuning parameters
****************************************/
#ifndef BMK_TIMETEST_DEFAULT_S   /* default minimum time per test */
#define BMK_TIMETEST_DEFAULT_S 3
#endif


/* *************************************
*  Includes
***************************************/
#include "platform.h"    /* Large Files support */
#include "util.h"        /* UTIL_getFileSize, UTIL_sleep */
#include <stdlib.h>      /* malloc, free */
#include <string.h>      /* memset, strerror */
#include <stdio.h>       /* fprintf, fopen */
#include <errno.h>
#include <assert.h>      /* assert */

#include "benchfn.h"
#include "mem.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#include "datagen.h"     /* RDG_genBuffer */
#include "xxhash.h"
#include "benchzstd.h"
#include "zstd_errors.h"


/* *************************************
*  Constants
***************************************/
#ifndef ZSTD_GIT_COMMIT
#  define ZSTD_GIT_COMMIT_STRING ""
#else
#  define ZSTD_GIT_COMMIT_STRING ZSTD_EXPAND_AND_QUOTE(ZSTD_GIT_COMMIT)
#endif

#define TIMELOOP_MICROSEC     (1*1000000ULL) /* 1 second */
#define TIMELOOP_NANOSEC      (1*1000000000ULL) /* 1 second */
#define ACTIVEPERIOD_MICROSEC (70*TIMELOOP_MICROSEC) /* 70 seconds */
#define COOLPERIOD_SEC        10

#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)

#define BMK_RUNTEST_DEFAULT_MS 1000

static const size_t maxMemory = (sizeof(size_t)==4)  ?
                    /* 32-bit */ (2 GB - 64 MB) :
                    /* 64-bit */ (size_t)(1ULL << ((sizeof(size_t)*8)-31));


/* *************************************
*  console display
***************************************/
#define DISPLAY(...)         fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...) if (displayLevel>=l) { DISPLAY(__VA_ARGS__); }
/* 0 : no display;   1: errors;   2 : + result + interaction + warnings;   3 : + progression;   4 : + information */

static const U64 g_refreshRate = SEC_TO_MICRO / 6;
static UTIL_time_t g_displayClock = UTIL_TIME_INITIALIZER;

#define DISPLAYUPDATE(l, ...) { if (displayLevel>=l) { \
            if ((UTIL_clockSpanMicro(g_displayClock) > g_refreshRate) || (displayLevel>=4)) \
            { g_displayClock = UTIL_getTime(); DISPLAY(__VA_ARGS__); \
            if (displayLevel>=4) fflush(stderr); } } }


/* *************************************
*  Exceptions
***************************************/
#ifndef DEBUG
#  define DEBUG 0
#endif
#define DEBUGOUTPUT(...) { if (DEBUG) DISPLAY(__VA_ARGS__); }

#define EXM_THROW_INT(errorNum, ...)  {               \
    DEBUGOUTPUT("%s: %i: \n", __FILE__, __LINE__);    \
    DISPLAYLEVEL(1, "Error %i : ", errorNum);         \
    DISPLAYLEVEL(1, __VA_ARGS__);                     \
    DISPLAYLEVEL(1, " \n");                           \
    return errorNum;                                  \
}

#define CHECK_Z(zf) {              \
    size_t const zerr = zf;        \
    if (ZSTD_isError(zerr)) {      \
        DEBUGOUTPUT("%s: %i: \n", __FILE__, __LINE__);  \
        DISPLAY("Error : ");       \
        DISPLAY("%s failed : %s",  \
                #zf, ZSTD_getErrorName(zerr));   \
        DISPLAY(" \n");            \
        exit(1);                   \
    }                              \
}

#define RETURN_ERROR(errorNum, retType, ...)  {       \
    retType r;                                        \
    memset(&r, 0, sizeof(retType));                   \
    DEBUGOUTPUT("%s: %i: \n", __FILE__, __LINE__);    \
    DISPLAYLEVEL(1, "Error %i : ", errorNum);         \
    DISPLAYLEVEL(1, __VA_ARGS__);                     \
    DISPLAYLEVEL(1, " \n");                           \
    r.tag = errorNum;                                 \
    return r;                                         \
}


/* *************************************
*  Benchmark Parameters
***************************************/

BMK_advancedParams_t BMK_initAdvancedParams(void) {
    BMK_advancedParams_t const res = {
        BMK_both, /* mode */
        BMK_TIMETEST_DEFAULT_S, /* nbSeconds */
        0, /* blockSize */
        0, /* nbWorkers */
        0, /* realTime */
        0, /* additionalParam */
        0, /* ldmFlag */
        0, /* ldmMinMatch */
        0, /* ldmHashLog */
        0, /* ldmBuckSizeLog */
        0  /* ldmHashRateLog */
    };
    return res;
}


/* ********************************************************
*  Bench functions
**********************************************************/
typedef struct {
    const void* srcPtr;
    size_t srcSize;
    void*  cPtr;
    size_t cRoom;
    size_t cSize;
    void*  resPtr;
    size_t resSize;
} blockParam_t;

#undef MIN
#undef MAX
#define MIN(a,b)    ((a) < (b) ? (a) : (b))
#define MAX(a,b)    ((a) > (b) ? (a) : (b))

static void BMK_initCCtx(ZSTD_CCtx* ctx,
    const void* dictBuffer, size_t dictBufferSize, int cLevel,
    const ZSTD_compressionParameters* comprParams, const BMK_advancedParams_t* adv) {
    ZSTD_CCtx_reset(ctx, ZSTD_reset_session_and_parameters);
    if (adv->nbWorkers==1) {
        CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_nbWorkers, 0));
    } else {
        CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_nbWorkers, adv->nbWorkers));
    }
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_compressionLevel, cLevel));
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_enableLongDistanceMatching, adv->ldmFlag));
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_ldmMinMatch, adv->ldmMinMatch));
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_ldmHashLog, adv->ldmHashLog));
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_ldmBucketSizeLog, adv->ldmBucketSizeLog));
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_ldmHashRateLog, adv->ldmHashRateLog));
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_windowLog, comprParams->windowLog));
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_hashLog, comprParams->hashLog));
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_chainLog, comprParams->chainLog));
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_searchLog, comprParams->searchLog));
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_minMatch, comprParams->minMatch));
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_targetLength, comprParams->targetLength));
    CHECK_Z(ZSTD_CCtx_setParameter(ctx, ZSTD_c_strategy, comprParams->strategy));
    CHECK_Z(ZSTD_CCtx_loadDictionary(ctx, dictBuffer, dictBufferSize));
}

static void BMK_initDCtx(ZSTD_DCtx* dctx,
    const void* dictBuffer, size_t dictBufferSize) {
    CHECK_Z(ZSTD_DCtx_reset(dctx, ZSTD_reset_session_and_parameters));
    CHECK_Z(ZSTD_DCtx_loadDictionary(dctx, dictBuffer, dictBufferSize));
}


typedef struct {
    ZSTD_CCtx* cctx;
    const void* dictBuffer;
    size_t dictBufferSize;
    int cLevel;
    const ZSTD_compressionParameters* comprParams;
    const BMK_advancedParams_t* adv;
} BMK_initCCtxArgs;

static size_t local_initCCtx(void* payload) {
    BMK_initCCtxArgs* ag = (BMK_initCCtxArgs*)payload;
    BMK_initCCtx(ag->cctx, ag->dictBuffer, ag->dictBufferSize, ag->cLevel, ag->comprParams, ag->adv);
    return 0;
}

typedef struct {
    ZSTD_DCtx* dctx;
    const void* dictBuffer;
    size_t dictBufferSize;
} BMK_initDCtxArgs;

static size_t local_initDCtx(void* payload) {
    BMK_initDCtxArgs* ag = (BMK_initDCtxArgs*)payload;
    BMK_initDCtx(ag->dctx, ag->dictBuffer, ag->dictBufferSize);
    return 0;
}


/* `addArgs` is the context */
static size_t local_defaultCompress(
                    const void* srcBuffer, size_t srcSize,
                    void* dstBuffer, size_t dstSize,
                    void* addArgs)
{
    ZSTD_CCtx* const cctx = (ZSTD_CCtx*)addArgs;
    return ZSTD_compress2(cctx, dstBuffer, dstSize, srcBuffer, srcSize);
}

/* `addArgs` is the context */
static size_t local_defaultDecompress(
                    const void* srcBuffer, size_t srcSize,
                    void* dstBuffer, size_t dstCapacity,
                    void* addArgs)
{
    size_t moreToFlush = 1;
    ZSTD_DCtx* const dctx = (ZSTD_DCtx*)addArgs;
    ZSTD_inBuffer in;
    ZSTD_outBuffer out;
    in.src = srcBuffer; in.size = srcSize; in.pos = 0;
    out.dst = dstBuffer; out.size = dstCapacity; out.pos = 0;
    while (moreToFlush) {
        if(out.pos == out.size) {
            return (size_t)-ZSTD_error_dstSize_tooSmall;
        }
        moreToFlush = ZSTD_decompressStream(dctx, &out, &in);
        if (ZSTD_isError(moreToFlush)) {
            return moreToFlush;
        }
    }
    return out.pos;

}


/* ================================================================= */
/*      Benchmark Zstandard, mem-to-mem scenarios                    */
/* ================================================================= */

int BMK_isSuccessful_benchOutcome(BMK_benchOutcome_t outcome)
{
    return outcome.tag == 0;
}

BMK_benchResult_t BMK_extract_benchResult(BMK_benchOutcome_t outcome)
{
    assert(outcome.tag == 0);
    return outcome.internal_never_use_directly;
}

static BMK_benchOutcome_t BMK_benchOutcome_error(void)
{
    BMK_benchOutcome_t b;
    memset(&b, 0, sizeof(b));
    b.tag = 1;
    return b;
}

static BMK_benchOutcome_t BMK_benchOutcome_setValidResult(BMK_benchResult_t result)
{
    BMK_benchOutcome_t b;
    b.tag = 0;
    b.internal_never_use_directly = result;
    return b;
}


/* benchMem with no allocation */
static BMK_benchOutcome_t
BMK_benchMemAdvancedNoAlloc(
                    const void** srcPtrs, size_t* srcSizes,
                    void** cPtrs, size_t* cCapacities, size_t* cSizes,
                    void** resPtrs, size_t* resSizes,
                    void** resultBufferPtr, void* compressedBuffer,
                    size_t maxCompressedSize,
                    BMK_timedFnState_t* timeStateCompress,
                    BMK_timedFnState_t* timeStateDecompress,

                    const void* srcBuffer, size_t srcSize,
                    const size_t* fileSizes, unsigned nbFiles,
                    const int cLevel,
                    const ZSTD_compressionParameters* comprParams,
                    const void* dictBuffer, size_t dictBufferSize,
                    ZSTD_CCtx* cctx, ZSTD_DCtx* dctx,
                    int displayLevel, const char* displayName,
                    const BMK_advancedParams_t* adv)
{
    size_t const blockSize = ((adv->blockSize>=32 && (adv->mode != BMK_decodeOnly)) ? adv->blockSize : srcSize) + (!srcSize);  /* avoid div by 0 */
    BMK_benchResult_t benchResult;
    size_t const loadedCompressedSize = srcSize;
    size_t cSize = 0;
    double ratio = 0.;
    U32 nbBlocks;

    assert(cctx != NULL); assert(dctx != NULL);

    /* init */
    memset(&benchResult, 0, sizeof(benchResult));
    if (strlen(displayName)>17) displayName += strlen(displayName) - 17;   /* display last 17 characters */
    if (adv->mode == BMK_decodeOnly) {  /* benchmark only decompression : source must be already compressed */
        const char* srcPtr = (const char*)srcBuffer;
        U64 totalDSize64 = 0;
        U32 fileNb;
        for (fileNb=0; fileNb<nbFiles; fileNb++) {
            U64 const fSize64 = ZSTD_findDecompressedSize(srcPtr, fileSizes[fileNb]);
            if (fSize64==0) RETURN_ERROR(32, BMK_benchOutcome_t, "Impossible to determine original size ");
            totalDSize64 += fSize64;
            srcPtr += fileSizes[fileNb];
        }
        {   size_t const decodedSize = (size_t)totalDSize64;
            assert((U64)decodedSize == totalDSize64);   /* check overflow */
            free(*resultBufferPtr);
            *resultBufferPtr = malloc(decodedSize);
            if (!(*resultBufferPtr)) {
                RETURN_ERROR(33, BMK_benchOutcome_t, "not enough memory");
            }
            if (totalDSize64 > decodedSize) {  /* size_t overflow */
                free(*resultBufferPtr);
                RETURN_ERROR(32, BMK_benchOutcome_t, "original size is too large");
            }
            cSize = srcSize;
            srcSize = decodedSize;
            ratio = (double)srcSize / (double)cSize;
        }
    }

    /* Init data blocks  */
    {   const char* srcPtr = (const char*)srcBuffer;
        char* cPtr = (char*)compressedBuffer;
        char* resPtr = (char*)(*resultBufferPtr);
        U32 fileNb;
        for (nbBlocks=0, fileNb=0; fileNb<nbFiles; fileNb++) {
            size_t remaining = fileSizes[fileNb];
            U32 const nbBlocksforThisFile = (adv->mode == BMK_decodeOnly) ? 1 : (U32)((remaining + (blockSize-1)) / blockSize);
            U32 const blockEnd = nbBlocks + nbBlocksforThisFile;
            for ( ; nbBlocks<blockEnd; nbBlocks++) {
                size_t const thisBlockSize = MIN(remaining, blockSize);
                srcPtrs[nbBlocks] = srcPtr;
                srcSizes[nbBlocks] = thisBlockSize;
                cPtrs[nbBlocks] = cPtr;
                cCapacities[nbBlocks] = (adv->mode == BMK_decodeOnly) ? thisBlockSize : ZSTD_compressBound(thisBlockSize);
                resPtrs[nbBlocks] = resPtr;
                resSizes[nbBlocks] = (adv->mode == BMK_decodeOnly) ? (size_t) ZSTD_findDecompressedSize(srcPtr, thisBlockSize) : thisBlockSize;
                srcPtr += thisBlockSize;
                cPtr += cCapacities[nbBlocks];
                resPtr += thisBlockSize;
                remaining -= thisBlockSize;
                if (adv->mode == BMK_decodeOnly) {
                    assert(nbBlocks==0);
                    cSizes[nbBlocks] = thisBlockSize;
                    benchResult.cSize = thisBlockSize;
                }
            }
        }
    }

    /* warmimg up `compressedBuffer` */
    if (adv->mode == BMK_decodeOnly) {
        memcpy(compressedBuffer, srcBuffer, loadedCompressedSize);
    } else {
        RDG_genBuffer(compressedBuffer, maxCompressedSize, 0.10, 0.50, 1);
    }

    /* Bench */
    {   U64 const crcOrig = (adv->mode == BMK_decodeOnly) ? 0 : XXH64(srcBuffer, srcSize, 0);
#       define NB_MARKS 4
        const char* marks[NB_MARKS] = { " |", " /", " =", " \\" };
        U32 markNb = 0;
        int compressionCompleted = (adv->mode == BMK_decodeOnly);
        int decompressionCompleted = (adv->mode == BMK_compressOnly);
        BMK_benchParams_t cbp, dbp;
        BMK_initCCtxArgs cctxprep;
        BMK_initDCtxArgs dctxprep;

        cbp.benchFn = local_defaultCompress;
        cbp.benchPayload = cctx;
        cbp.initFn = local_initCCtx;
        cbp.initPayload = &cctxprep;
        cbp.errorFn = ZSTD_isError;
        cbp.blockCount = nbBlocks;
        cbp.srcBuffers = srcPtrs;
        cbp.srcSizes = srcSizes;
        cbp.dstBuffers = cPtrs;
        cbp.dstCapacities = cCapacities;
        cbp.blockResults = cSizes;

        cctxprep.cctx = cctx;
        cctxprep.dictBuffer = dictBuffer;
        cctxprep.dictBufferSize = dictBufferSize;
        cctxprep.cLevel = cLevel;
        cctxprep.comprParams = comprParams;
        cctxprep.adv = adv;

        dbp.benchFn = local_defaultDecompress;
        dbp.benchPayload = dctx;
        dbp.initFn = local_initDCtx;
        dbp.initPayload = &dctxprep;
        dbp.errorFn = ZSTD_isError;
        dbp.blockCount = nbBlocks;
        dbp.srcBuffers = (const void* const *) cPtrs;
        dbp.srcSizes = cSizes;
        dbp.dstBuffers = resPtrs;
        dbp.dstCapacities = resSizes;
        dbp.blockResults = NULL;

        dctxprep.dctx = dctx;
        dctxprep.dictBuffer = dictBuffer;
        dctxprep.dictBufferSize = dictBufferSize;

        DISPLAYLEVEL(2, "\r%70s\r", "");   /* blank line */
        DISPLAYLEVEL(2, "%2s-%-17.17s :%10u ->\r", marks[markNb], displayName, (unsigned)srcSize);

        while (!(compressionCompleted && decompressionCompleted)) {
            if (!compressionCompleted) {
                BMK_runOutcome_t const cOutcome = BMK_benchTimedFn( timeStateCompress, cbp);

                if (!BMK_isSuccessful_runOutcome(cOutcome)) {
                    return BMK_benchOutcome_error();
                }

                {   BMK_runTime_t const cResult = BMK_extract_runTime(cOutcome);
                    cSize = cResult.sumOfReturn;
                    ratio = (double)srcSize / cSize;
                    {   BMK_benchResult_t newResult;
                        newResult.cSpeed = ((U64)srcSize * TIMELOOP_NANOSEC / cResult.nanoSecPerRun);
                        benchResult.cSize = cSize;
                        if (newResult.cSpeed > benchResult.cSpeed)
                            benchResult.cSpeed = newResult.cSpeed;
                }   }

                {   int const ratioAccuracy = (ratio < 10.) ? 3 : 2;
                    DISPLAYLEVEL(2, "%2s-%-17.17s :%10u ->%10u (%5.*f),%6.*f MB/s\r",
                            marks[markNb], displayName,
                            (unsigned)srcSize, (unsigned)cSize,
                            ratioAccuracy, ratio,
                            benchResult.cSpeed < (10 MB) ? 2 : 1, (double)benchResult.cSpeed / MB_UNIT);
                }
                compressionCompleted = BMK_isCompleted_TimedFn(timeStateCompress);
            }

            if(!decompressionCompleted) {
                BMK_runOutcome_t const dOutcome = BMK_benchTimedFn(timeStateDecompress, dbp);

                if(!BMK_isSuccessful_runOutcome(dOutcome)) {
                    return BMK_benchOutcome_error();
                }

                {   BMK_runTime_t const dResult = BMK_extract_runTime(dOutcome);
                    U64 const newDSpeed = (srcSize * TIMELOOP_NANOSEC / dResult.nanoSecPerRun);
                    if (newDSpeed > benchResult.dSpeed)
                        benchResult.dSpeed = newDSpeed;
                }

                {   int const ratioAccuracy = (ratio < 10.) ? 3 : 2;
                    DISPLAYLEVEL(2, "%2s-%-17.17s :%10u ->%10u (%5.*f),%6.*f MB/s ,%6.1f MB/s \r",
                            marks[markNb], displayName,
                            (unsigned)srcSize, (unsigned)benchResult.cSize,
                            ratioAccuracy, ratio,
                            benchResult.cSpeed < (10 MB) ? 2 : 1, (double)benchResult.cSpeed / MB_UNIT,
                            (double)benchResult.dSpeed / MB_UNIT);
                }
                decompressionCompleted = BMK_isCompleted_TimedFn(timeStateDecompress);
            }
            markNb = (markNb+1) % NB_MARKS;
        }   /* while (!(compressionCompleted && decompressionCompleted)) */

        /* CRC Checking */
        {   const BYTE* resultBuffer = (const BYTE*)(*resultBufferPtr);
            U64 const crcCheck = XXH64(resultBuffer, srcSize, 0);
            if ((adv->mode == BMK_both) && (crcOrig!=crcCheck)) {
                size_t u;
                DISPLAY("!!! WARNING !!! %14s : Invalid Checksum : %x != %x   \n",
                        displayName, (unsigned)crcOrig, (unsigned)crcCheck);
                for (u=0; u<srcSize; u++) {
                    if (((const BYTE*)srcBuffer)[u] != resultBuffer[u]) {
                        unsigned segNb, bNb, pos;
                        size_t bacc = 0;
                        DISPLAY("Decoding error at pos %u ", (unsigned)u);
                        for (segNb = 0; segNb < nbBlocks; segNb++) {
                            if (bacc + srcSizes[segNb] > u) break;
                            bacc += srcSizes[segNb];
                        }
                        pos = (U32)(u - bacc);
                        bNb = pos / (128 KB);
                        DISPLAY("(sample %u, block %u, pos %u) \n", segNb, bNb, pos);
                        if (u>5) {
                            int n;
                            DISPLAY("origin: ");
                            for (n=-5; n<0; n++) DISPLAY("%02X ", ((const BYTE*)srcBuffer)[u+n]);
                            DISPLAY(" :%02X:  ", ((const BYTE*)srcBuffer)[u]);
                            for (n=1; n<3; n++) DISPLAY("%02X ", ((const BYTE*)srcBuffer)[u+n]);
                            DISPLAY(" \n");
                            DISPLAY("decode: ");
                            for (n=-5; n<0; n++) DISPLAY("%02X ", resultBuffer[u+n]);
                            DISPLAY(" :%02X:  ", resultBuffer[u]);
                            for (n=1; n<3; n++) DISPLAY("%02X ", resultBuffer[u+n]);
                            DISPLAY(" \n");
                        }
                        break;
                    }
                    if (u==srcSize-1) {  /* should never happen */
                        DISPLAY("no difference detected\n");
                    }
                }
            }
        }   /* CRC Checking */

        if (displayLevel == 1) {   /* hidden display mode -q, used by python speed benchmark */
            double const cSpeed = (double)benchResult.cSpeed / MB_UNIT;
            double const dSpeed = (double)benchResult.dSpeed / MB_UNIT;
            if (adv->additionalParam) {
                DISPLAY("-%-3i%11i (%5.3f) %6.2f MB/s %6.1f MB/s  %s (param=%d)\n", cLevel, (int)cSize, ratio, cSpeed, dSpeed, displayName, adv->additionalParam);
            } else {
                DISPLAY("-%-3i%11i (%5.3f) %6.2f MB/s %6.1f MB/s  %s\n", cLevel, (int)cSize, ratio, cSpeed, dSpeed, displayName);
            }
        }

        DISPLAYLEVEL(2, "%2i#\n", cLevel);
    }   /* Bench */

    benchResult.cMem = (1ULL << (comprParams->windowLog)) + ZSTD_sizeof_CCtx(cctx);
    return BMK_benchOutcome_setValidResult(benchResult);
}

BMK_benchOutcome_t BMK_benchMemAdvanced(const void* srcBuffer, size_t srcSize,
                        void* dstBuffer, size_t dstCapacity,
                        const size_t* fileSizes, unsigned nbFiles,
                        int cLevel, const ZSTD_compressionParameters* comprParams,
                        const void* dictBuffer, size_t dictBufferSize,
                        int displayLevel, const char* displayName, const BMK_advancedParams_t* adv)

{
    int const dstParamsError = !dstBuffer ^ !dstCapacity;  /* must be both NULL or none */

    size_t const blockSize = ((adv->blockSize>=32 && (adv->mode != BMK_decodeOnly)) ? adv->blockSize : srcSize) + (!srcSize) /* avoid div by 0 */ ;
    U32 const maxNbBlocks = (U32) ((srcSize + (blockSize-1)) / blockSize) + nbFiles;

    /* these are the blockTable parameters, just split up */
    const void ** const srcPtrs = (const void**)malloc(maxNbBlocks * sizeof(void*));
    size_t* const srcSizes = (size_t*)malloc(maxNbBlocks * sizeof(size_t));


    void ** const cPtrs = (void**)malloc(maxNbBlocks * sizeof(void*));
    size_t* const cSizes = (size_t*)malloc(maxNbBlocks * sizeof(size_t));
    size_t* const cCapacities = (size_t*)malloc(maxNbBlocks * sizeof(size_t));

    void ** const resPtrs = (void**)malloc(maxNbBlocks * sizeof(void*));
    size_t* const resSizes = (size_t*)malloc(maxNbBlocks * sizeof(size_t));

    BMK_timedFnState_t* timeStateCompress = BMK_createTimedFnState(adv->nbSeconds * 1000, BMK_RUNTEST_DEFAULT_MS);
    BMK_timedFnState_t* timeStateDecompress = BMK_createTimedFnState(adv->nbSeconds * 1000, BMK_RUNTEST_DEFAULT_MS);

    ZSTD_CCtx* const cctx = ZSTD_createCCtx();
    ZSTD_DCtx* const dctx = ZSTD_createDCtx();

    const size_t maxCompressedSize = dstCapacity ? dstCapacity : ZSTD_compressBound(srcSize) + (maxNbBlocks * 1024);

    void* const internalDstBuffer = dstBuffer ? NULL : malloc(maxCompressedSize);
    void* const compressedBuffer = dstBuffer ? dstBuffer : internalDstBuffer;

    BMK_benchOutcome_t outcome = BMK_benchOutcome_error();  /* error by default */

    void* resultBuffer = srcSize ? malloc(srcSize) : NULL;

    int allocationincomplete = !srcPtrs || !srcSizes || !cPtrs ||
        !cSizes || !cCapacities || !resPtrs || !resSizes ||
        !timeStateCompress || !timeStateDecompress ||
        !cctx || !dctx ||
        !compressedBuffer || !resultBuffer;


    if (!allocationincomplete && !dstParamsError) {
        outcome = BMK_benchMemAdvancedNoAlloc(srcPtrs, srcSizes,
                                            cPtrs, cCapacities, cSizes,
                                            resPtrs, resSizes,
                                            &resultBuffer,
                                            compressedBuffer, maxCompressedSize,
                                            timeStateCompress, timeStateDecompress,
                                            srcBuffer, srcSize,
                                            fileSizes, nbFiles,
                                            cLevel, comprParams,
                                            dictBuffer, dictBufferSize,
                                            cctx, dctx,
                                            displayLevel, displayName, adv);
    }

    /* clean up */
    BMK_freeTimedFnState(timeStateCompress);
    BMK_freeTimedFnState(timeStateDecompress);

    ZSTD_freeCCtx(cctx);
    ZSTD_freeDCtx(dctx);

    free(internalDstBuffer);
    free(resultBuffer);

    free((void*)srcPtrs);
    free(srcSizes);
    free(cPtrs);
    free(cSizes);
    free(cCapacities);
    free(resPtrs);
    free(resSizes);

    if(allocationincomplete) {
        RETURN_ERROR(31, BMK_benchOutcome_t, "allocation error : not enough memory");
    }

    if(dstParamsError) {
        RETURN_ERROR(32, BMK_benchOutcome_t, "Dst parameters not coherent");
    }
    return outcome;
}

BMK_benchOutcome_t BMK_benchMem(const void* srcBuffer, size_t srcSize,
                        const size_t* fileSizes, unsigned nbFiles,
                        int cLevel, const ZSTD_compressionParameters* comprParams,
                        const void* dictBuffer, size_t dictBufferSize,
                        int displayLevel, const char* displayName) {

    BMK_advancedParams_t const adv = BMK_initAdvancedParams();
    return BMK_benchMemAdvanced(srcBuffer, srcSize,
                                NULL, 0,
                                fileSizes, nbFiles,
                                cLevel, comprParams,
                                dictBuffer, dictBufferSize,
                                displayLevel, displayName, &adv);
}

static BMK_benchOutcome_t BMK_benchCLevel(const void* srcBuffer, size_t benchedSize,
                            const size_t* fileSizes, unsigned nbFiles,
                            int cLevel, const ZSTD_compressionParameters* comprParams,
                            const void* dictBuffer, size_t dictBufferSize,
                            int displayLevel, const char* displayName,
                            BMK_advancedParams_t const * const adv)
{
    const char* pch = strrchr(displayName, '\\'); /* Windows */
    if (!pch) pch = strrchr(displayName, '/');    /* Linux */
    if (pch) displayName = pch+1;

    if (adv->realTime) {
        DISPLAYLEVEL(2, "Note : switching to real-time priority \n");
        SET_REALTIME_PRIORITY;
    }

    if (displayLevel == 1 && !adv->additionalParam)   /* --quiet mode */
        DISPLAY("bench %s %s: input %u bytes, %u seconds, %u KB blocks\n",
                ZSTD_VERSION_STRING, ZSTD_GIT_COMMIT_STRING,
                (unsigned)benchedSize, adv->nbSeconds, (unsigned)(adv->blockSize>>10));

    return BMK_benchMemAdvanced(srcBuffer, benchedSize,
                                NULL, 0,
                                fileSizes, nbFiles,
                                cLevel, comprParams,
                                dictBuffer, dictBufferSize,
                                displayLevel, displayName, adv);
}

BMK_benchOutcome_t BMK_syntheticTest(int cLevel, double compressibility,
                          const ZSTD_compressionParameters* compressionParams,
                          int displayLevel, const BMK_advancedParams_t* adv)
{
    char name[20] = {0};
    size_t const benchedSize = 10000000;
    void* srcBuffer;
    BMK_benchOutcome_t res;

    if (cLevel > ZSTD_maxCLevel()) {
        RETURN_ERROR(15, BMK_benchOutcome_t, "Invalid Compression Level");
    }

    /* Memory allocation */
    srcBuffer = malloc(benchedSize);
    if (!srcBuffer) RETURN_ERROR(21, BMK_benchOutcome_t, "not enough memory");

    /* Fill input buffer */
    RDG_genBuffer(srcBuffer, benchedSize, compressibility, 0.0, 0);

    /* Bench */
    snprintf (name, sizeof(name), "Synthetic %2u%%", (unsigned)(compressibility*100));
    res = BMK_benchCLevel(srcBuffer, benchedSize,
                    &benchedSize /* ? */, 1 /* ? */,
                    cLevel, compressionParams,
                    NULL, 0,  /* dictionary */
                    displayLevel, name, adv);

    /* clean up */
    free(srcBuffer);

    return res;
}



static size_t BMK_findMaxMem(U64 requiredMem)
{
    size_t const step = 64 MB;
    BYTE* testmem = NULL;

    requiredMem = (((requiredMem >> 26) + 1) << 26);
    requiredMem += step;
    if (requiredMem > maxMemory) requiredMem = maxMemory;

    do {
        testmem = (BYTE*)malloc((size_t)requiredMem);
        requiredMem -= step;
    } while (!testmem && requiredMem > 0);

    free(testmem);
    return (size_t)(requiredMem);
}

/*! BMK_loadFiles() :
 *  Loads `buffer` with content of files listed within `fileNamesTable`.
 *  At most, fills `buffer` entirely. */
static int BMK_loadFiles(void* buffer, size_t bufferSize,
                         size_t* fileSizes,
                         const char* const * fileNamesTable, unsigned nbFiles,
                         int displayLevel)
{
    size_t pos = 0, totalSize = 0;
    unsigned n;
    for (n=0; n<nbFiles; n++) {
        FILE* f;
        U64 fileSize = UTIL_getFileSize(fileNamesTable[n]);
        if (UTIL_isDirectory(fileNamesTable[n])) {
            DISPLAYLEVEL(2, "Ignoring %s directory...       \n", fileNamesTable[n]);
            fileSizes[n] = 0;
            continue;
        }
        if (fileSize == UTIL_FILESIZE_UNKNOWN) {
            DISPLAYLEVEL(2, "Cannot evaluate size of %s, ignoring ... \n", fileNamesTable[n]);
            fileSizes[n] = 0;
            continue;
        }
        f = fopen(fileNamesTable[n], "rb");
        if (f==NULL) EXM_THROW_INT(10, "impossible to open file %s", fileNamesTable[n]);
        DISPLAYUPDATE(2, "Loading %s...       \r", fileNamesTable[n]);
        if (fileSize > bufferSize-pos) fileSize = bufferSize-pos, nbFiles=n;   /* buffer too small - stop after this file */
        {   size_t const readSize = fread(((char*)buffer)+pos, 1, (size_t)fileSize, f);
            if (readSize != (size_t)fileSize) EXM_THROW_INT(11, "could not read %s", fileNamesTable[n]);
            pos += readSize;
        }
        fileSizes[n] = (size_t)fileSize;
        totalSize += (size_t)fileSize;
        fclose(f);
    }

    if (totalSize == 0) EXM_THROW_INT(12, "no data to bench");
    return 0;
}

BMK_benchOutcome_t BMK_benchFilesAdvanced(
                        const char* const * fileNamesTable, unsigned nbFiles,
                        const char* dictFileName, int cLevel,
                        const ZSTD_compressionParameters* compressionParams,
                        int displayLevel, const BMK_advancedParams_t* adv)
{
    void* srcBuffer = NULL;
    size_t benchedSize;
    void* dictBuffer = NULL;
    size_t dictBufferSize = 0;
    size_t* fileSizes = NULL;
    BMK_benchOutcome_t res;
    U64 const totalSizeToLoad = UTIL_getTotalFileSize(fileNamesTable, nbFiles);

    if (!nbFiles) {
        RETURN_ERROR(14, BMK_benchOutcome_t, "No Files to Benchmark");
    }

    if (cLevel > ZSTD_maxCLevel()) {
        RETURN_ERROR(15, BMK_benchOutcome_t, "Invalid Compression Level");
    }

    fileSizes = (size_t*)calloc(nbFiles, sizeof(size_t));
    if (!fileSizes) RETURN_ERROR(12, BMK_benchOutcome_t, "not enough memory for fileSizes");

    /* Load dictionary */
    if (dictFileName != NULL) {
        U64 const dictFileSize = UTIL_getFileSize(dictFileName);
        if (dictFileSize == UTIL_FILESIZE_UNKNOWN) {
            DISPLAYLEVEL(1, "error loading %s : %s \n", dictFileName, strerror(errno));
            free(fileSizes);
            RETURN_ERROR(9, BMK_benchOutcome_t, "benchmark aborted");
        }
        if (dictFileSize > 64 MB) {
            free(fileSizes);
            RETURN_ERROR(10, BMK_benchOutcome_t, "dictionary file %s too large", dictFileName);
        }
        dictBufferSize = (size_t)dictFileSize;
        dictBuffer = malloc(dictBufferSize);
        if (dictBuffer==NULL) {
            free(fileSizes);
            RETURN_ERROR(11, BMK_benchOutcome_t, "not enough memory for dictionary (%u bytes)",
                            (unsigned)dictBufferSize);
        }

        {   int const errorCode = BMK_loadFiles(dictBuffer, dictBufferSize,
                                                fileSizes, &dictFileName /*?*/,
                                                1 /*?*/, displayLevel);
            if (errorCode) {
                res = BMK_benchOutcome_error();
                goto _cleanUp;
        }   }
    }

    /* Memory allocation & restrictions */
    benchedSize = BMK_findMaxMem(totalSizeToLoad * 3) / 3;
    if ((U64)benchedSize > totalSizeToLoad) benchedSize = (size_t)totalSizeToLoad;
    if (benchedSize < totalSizeToLoad)
        DISPLAY("Not enough memory; testing %u MB only...\n", (unsigned)(benchedSize >> 20));

    srcBuffer = benchedSize ? malloc(benchedSize) : NULL;
    if (!srcBuffer) {
        free(dictBuffer);
        free(fileSizes);
        RETURN_ERROR(12, BMK_benchOutcome_t, "not enough memory");
    }

    /* Load input buffer */
    {   int const errorCode = BMK_loadFiles(srcBuffer, benchedSize,
                                        fileSizes, fileNamesTable, nbFiles,
                                        displayLevel);
        if (errorCode) {
            res = BMK_benchOutcome_error();
            goto _cleanUp;
    }   }

    /* Bench */
    {   char mfName[20] = {0};
        snprintf (mfName, sizeof(mfName), " %u files", nbFiles);
        {   const char* const displayName = (nbFiles > 1) ? mfName : fileNamesTable[0];
            res = BMK_benchCLevel(srcBuffer, benchedSize,
                                fileSizes, nbFiles,
                                cLevel, compressionParams,
                                dictBuffer, dictBufferSize,
                                displayLevel, displayName,
                                adv);
    }   }

_cleanUp:
    free(srcBuffer);
    free(dictBuffer);
    free(fileSizes);
    return res;
}


BMK_benchOutcome_t BMK_benchFiles(
                    const char* const * fileNamesTable, unsigned nbFiles,
                    const char* dictFileName,
                    int cLevel, const ZSTD_compressionParameters* compressionParams,
                    int displayLevel)
{
    BMK_advancedParams_t const adv = BMK_initAdvancedParams();
    return BMK_benchFilesAdvanced(fileNamesTable, nbFiles, dictFileName, cLevel, compressionParams, displayLevel, &adv);
}
