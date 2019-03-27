/*
 * Copyright (c) 2015-present, Yann Collet, Facebook, Inc.
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
#  define _CRT_SECURE_NO_WARNINGS     /* fgets */
#  pragma warning(disable : 4127)     /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4146)     /* disable: C4146: minus unsigned expression */
#endif


/*-************************************
*  Includes
**************************************/
#include <stdlib.h>       /* free */
#include <stdio.h>        /* fgets, sscanf */
#include <string.h>       /* strcmp */
#include "mem.h"
#define ZSTD_STATIC_LINKING_ONLY   /* ZSTD_maxCLevel */
#include "zstd.h"         /* ZSTD_compressBound */
#define ZBUFF_STATIC_LINKING_ONLY  /* ZBUFF_createCCtx_advanced */
#include "zbuff.h"        /* ZBUFF_isError */
#include "datagen.h"      /* RDG_genBuffer */
#define XXH_STATIC_LINKING_ONLY
#include "xxhash.h"       /* XXH64_* */
#include "util.h"


/*-************************************
*  Constants
**************************************/
#define KB *(1U<<10)
#define MB *(1U<<20)
#define GB *(1U<<30)

static const U32 nbTestsDefault = 10000;
#define COMPRESSIBLE_NOISE_LENGTH (10 MB)
#define FUZ_COMPRESSIBILITY_DEFAULT 50
static const U32 prime1 = 2654435761U;
static const U32 prime2 = 2246822519U;



/*-************************************
*  Display Macros
**************************************/
#define DISPLAY(...)          fprintf(stderr, __VA_ARGS__)
#define DISPLAYLEVEL(l, ...)  if (g_displayLevel>=l) { DISPLAY(__VA_ARGS__); }
static U32 g_displayLevel = 2;

static const U64 g_refreshRate = SEC_TO_MICRO / 6;
static UTIL_time_t g_displayClock = UTIL_TIME_INITIALIZER;

#define DISPLAYUPDATE(l, ...) if (g_displayLevel>=l) { \
            if ((UTIL_clockSpanMicro(g_displayClock) > g_refreshRate) || (g_displayLevel>=4)) \
            { g_displayClock = UTIL_getTime(); DISPLAY(__VA_ARGS__); \
            if (g_displayLevel>=4) fflush(stderr); } }

static U64 g_clockTime = 0;


/*-*******************************************************
*  Fuzzer functions
*********************************************************/
#undef MIN
#undef MAX
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
/*! FUZ_rand() :
    @return : a 27 bits random value, from a 32-bits `seed`.
    `seed` is also modified */
#  define FUZ_rotl32(x,r) ((x << r) | (x >> (32 - r)))
unsigned int FUZ_rand(unsigned int* seedPtr)
{
    U32 rand32 = *seedPtr;
    rand32 *= prime1;
    rand32 += prime2;
    rand32  = FUZ_rotl32(rand32, 13);
    *seedPtr = rand32;
    return rand32 >> 5;
}


/*
static unsigned FUZ_highbit32(U32 v32)
{
    unsigned nbBits = 0;
    if (v32==0) return 0;
    for ( ; v32 ; v32>>=1) nbBits++;
    return nbBits;
}
*/

static void* ZBUFF_allocFunction(void* opaque, size_t size)
{
    void* address = malloc(size);
    (void)opaque;
    /* DISPLAYLEVEL(4, "alloc %p, %d opaque=%p \n", address, (int)size, opaque); */
    return address;
}

static void ZBUFF_freeFunction(void* opaque, void* address)
{
    (void)opaque;
    /* if (address) DISPLAYLEVEL(4, "free %p opaque=%p \n", address, opaque); */
    free(address);
}

static int basicUnitTests(U32 seed, double compressibility, ZSTD_customMem customMem)
{
    int testResult = 0;
    size_t CNBufferSize = COMPRESSIBLE_NOISE_LENGTH;
    void* CNBuffer = malloc(CNBufferSize);
    size_t const skippableFrameSize = 11;
    size_t const compressedBufferSize = (8 + skippableFrameSize) + ZSTD_compressBound(COMPRESSIBLE_NOISE_LENGTH);
    void* compressedBuffer = malloc(compressedBufferSize);
    size_t const decodedBufferSize = CNBufferSize;
    void* decodedBuffer = malloc(decodedBufferSize);
    size_t cSize, readSize, readSkipSize, genSize;
    U32 testNb=0;
    ZBUFF_CCtx* zc = ZBUFF_createCCtx_advanced(customMem);
    ZBUFF_DCtx* zd = ZBUFF_createDCtx_advanced(customMem);

    /* Create compressible test buffer */
    if (!CNBuffer || !compressedBuffer || !decodedBuffer || !zc || !zd) {
        DISPLAY("Not enough memory, aborting\n");
        goto _output_error;
    }
    RDG_genBuffer(CNBuffer, CNBufferSize, compressibility, 0., seed);

    /* generate skippable frame */
    MEM_writeLE32(compressedBuffer, ZSTD_MAGIC_SKIPPABLE_START);
    MEM_writeLE32(((char*)compressedBuffer)+4, (U32)skippableFrameSize);
    cSize = skippableFrameSize + 8;

    /* Basic compression test */
    DISPLAYLEVEL(4, "test%3i : compress %u bytes : ", testNb++, COMPRESSIBLE_NOISE_LENGTH);
    ZBUFF_compressInitDictionary(zc, CNBuffer, 128 KB, 1);
    readSize = CNBufferSize;
    genSize = compressedBufferSize;
    { size_t const r = ZBUFF_compressContinue(zc, ((char*)compressedBuffer)+cSize, &genSize, CNBuffer, &readSize);
      if (ZBUFF_isError(r)) goto _output_error; }
    if (readSize != CNBufferSize) goto _output_error;   /* entire input should be consumed */
    cSize += genSize;
    genSize = compressedBufferSize - cSize;
    { size_t const r = ZBUFF_compressEnd(zc, ((char*)compressedBuffer)+cSize, &genSize);
      if (r != 0) goto _output_error; }  /* error, or some data not flushed */
    cSize += genSize;
    DISPLAYLEVEL(4, "OK (%u bytes : %.2f%%)\n", (U32)cSize, (double)cSize/COMPRESSIBLE_NOISE_LENGTH*100);

    /* skippable frame test */
    DISPLAYLEVEL(4, "test%3i : decompress skippable frame : ", testNb++);
    ZBUFF_decompressInitDictionary(zd, CNBuffer, 128 KB);
    readSkipSize = cSize;
    genSize = CNBufferSize;
    { size_t const r = ZBUFF_decompressContinue(zd, decodedBuffer, &genSize, compressedBuffer, &readSkipSize);
      if (r != 0) goto _output_error; }
    if (genSize != 0) goto _output_error;   /* skippable frame len is 0 */
    DISPLAYLEVEL(4, "OK \n");

    /* Basic decompression test */
    DISPLAYLEVEL(4, "test%3i : decompress %u bytes : ", testNb++, COMPRESSIBLE_NOISE_LENGTH);
    ZBUFF_decompressInitDictionary(zd, CNBuffer, 128 KB);
    readSize = cSize - readSkipSize;
    genSize = CNBufferSize;
    { size_t const r = ZBUFF_decompressContinue(zd, decodedBuffer, &genSize, ((char*)compressedBuffer)+readSkipSize, &readSize);
      if (r != 0) goto _output_error; }  /* should reach end of frame == 0; otherwise, some data left, or an error */
    if (genSize != CNBufferSize) goto _output_error;   /* should regenerate the same amount */
    if (readSize+readSkipSize != cSize) goto _output_error;   /* should have read the entire frame */
    DISPLAYLEVEL(4, "OK \n");

    /* check regenerated data is byte exact */
    DISPLAYLEVEL(4, "test%3i : check decompressed result : ", testNb++);
    {   size_t i;
        for (i=0; i<CNBufferSize; i++) {
            if (((BYTE*)decodedBuffer)[i] != ((BYTE*)CNBuffer)[i]) goto _output_error;;
    }   }
    DISPLAYLEVEL(4, "OK \n");

    /* Byte-by-byte decompression test */
    DISPLAYLEVEL(4, "test%3i : decompress byte-by-byte : ", testNb++);
    {   size_t r, pIn=0, pOut=0;
        do
        {   ZBUFF_decompressInitDictionary(zd, CNBuffer, 128 KB);
            r = 1;
            while (r) {
                size_t inS = 1;
                size_t outS = 1;
                r = ZBUFF_decompressContinue(zd, ((BYTE*)decodedBuffer)+pOut, &outS, ((BYTE*)compressedBuffer)+pIn, &inS);
                pIn += inS;
                pOut += outS;
            }
            readSize = pIn;
            genSize = pOut;
        } while (genSize==0);
    }
    if (genSize != CNBufferSize) goto _output_error;   /* should regenerate the same amount */
    if (readSize != cSize) goto _output_error;   /* should have read the entire frame */
    DISPLAYLEVEL(4, "OK \n");

    /* check regenerated data is byte exact */
    DISPLAYLEVEL(4, "test%3i : check decompressed result : ", testNb++);
    {   size_t i;
        for (i=0; i<CNBufferSize; i++) {
            if (((BYTE*)decodedBuffer)[i] != ((BYTE*)CNBuffer)[i]) goto _output_error;;
    }   }
    DISPLAYLEVEL(4, "OK \n");

_end:
    ZBUFF_freeCCtx(zc);
    ZBUFF_freeDCtx(zd);
    free(CNBuffer);
    free(compressedBuffer);
    free(decodedBuffer);
    return testResult;

_output_error:
    testResult = 1;
    DISPLAY("Error detected in Unit tests ! \n");
    goto _end;
}


static size_t findDiff(const void* buf1, const void* buf2, size_t max)
{
    const BYTE* b1 = (const BYTE*)buf1;
    const BYTE* b2 = (const BYTE*)buf2;
    size_t u;
    for (u=0; u<max; u++) {
        if (b1[u] != b2[u]) break;
    }
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

#define CHECK(cond, ...) if (cond) { DISPLAY("Error => "); DISPLAY(__VA_ARGS__); \
                         DISPLAY(" (seed %u, test nb %u)  \n", seed, testNb); goto _output_error; }

static int fuzzerTests(U32 seed, U32 nbTests, unsigned startTest, double compressibility)
{
    static const U32 maxSrcLog = 24;
    static const U32 maxSampleLog = 19;
    BYTE* cNoiseBuffer[5];
    size_t const srcBufferSize = (size_t)1<<maxSrcLog;
    BYTE* copyBuffer;
    size_t const copyBufferSize= srcBufferSize + (1<<maxSampleLog);
    BYTE* cBuffer;
    size_t const cBufferSize   = ZSTD_compressBound(srcBufferSize);
    BYTE* dstBuffer;
    size_t dstBufferSize = srcBufferSize;
    U32 result = 0;
    U32 testNb = 0;
    U32 coreSeed = seed;
    ZBUFF_CCtx* zc;
    ZBUFF_DCtx* zd;
    UTIL_time_t startClock = UTIL_getTime();

    /* allocations */
    zc = ZBUFF_createCCtx();
    zd = ZBUFF_createDCtx();
    cNoiseBuffer[0] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[1] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[2] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[3] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[4] = (BYTE*)malloc (srcBufferSize);
    copyBuffer= (BYTE*)malloc (copyBufferSize);
    dstBuffer = (BYTE*)malloc (dstBufferSize);
    cBuffer   = (BYTE*)malloc (cBufferSize);
    CHECK (!cNoiseBuffer[0] || !cNoiseBuffer[1] || !cNoiseBuffer[2] || !cNoiseBuffer[3] || !cNoiseBuffer[4] ||
           !copyBuffer || !dstBuffer || !cBuffer || !zc || !zd,
           "Not enough memory, fuzzer tests cancelled");

    /* Create initial samples */
    RDG_genBuffer(cNoiseBuffer[0], srcBufferSize, 0.00, 0., coreSeed);    /* pure noise */
    RDG_genBuffer(cNoiseBuffer[1], srcBufferSize, 0.05, 0., coreSeed);    /* barely compressible */
    RDG_genBuffer(cNoiseBuffer[2], srcBufferSize, compressibility, 0., coreSeed);
    RDG_genBuffer(cNoiseBuffer[3], srcBufferSize, 0.95, 0., coreSeed);    /* highly compressible */
    RDG_genBuffer(cNoiseBuffer[4], srcBufferSize, 1.00, 0., coreSeed);    /* sparse content */
    memset(copyBuffer, 0x65, copyBufferSize);                             /* make copyBuffer considered initialized */

    /* catch up testNb */
    for (testNb=1; testNb < startTest; testNb++)
        FUZ_rand(&coreSeed);

    /* test loop */
    for ( ; (testNb <= nbTests) || (UTIL_clockSpanMicro(startClock) < g_clockTime) ; testNb++ ) {
        U32 lseed;
        const BYTE* srcBuffer;
        const BYTE* dict;
        size_t maxTestSize, dictSize;
        size_t cSize, totalTestSize, totalCSize, totalGenSize;
        size_t errorCode;
        U32 n, nbChunks;
        XXH64_state_t xxhState;
        U64 crcOrig;

        /* init */
        DISPLAYUPDATE(2, "\r%6u", testNb);
        if (nbTests >= testNb) DISPLAYUPDATE(2, "/%6u   ", nbTests);
        FUZ_rand(&coreSeed);
        lseed = coreSeed ^ prime1;

        /* states full reset (unsynchronized) */
        /* some issues only happen when reusing states in a specific sequence of parameters */
        if ((FUZ_rand(&lseed) & 0xFF) == 131) { ZBUFF_freeCCtx(zc); zc = ZBUFF_createCCtx(); }
        if ((FUZ_rand(&lseed) & 0xFF) == 132) { ZBUFF_freeDCtx(zd); zd = ZBUFF_createDCtx(); }

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
            U32 const cLevel = (FUZ_rand(&lseed) % (ZSTD_maxCLevel() - (testLog/3))) + 1;
            maxTestSize = FUZ_rLogLength(&lseed, testLog);
            dictSize  = (FUZ_rand(&lseed)==1) ? FUZ_randomLength(&lseed, maxSampleLog) : 0;
            /* random dictionary selection */
            {   size_t const dictStart = FUZ_rand(&lseed) % (srcBufferSize - dictSize);
                dict = srcBuffer + dictStart;
            }
            {   ZSTD_parameters params = ZSTD_getParams(cLevel, 0, dictSize);
                params.fParams.checksumFlag = FUZ_rand(&lseed) & 1;
                params.fParams.noDictIDFlag = FUZ_rand(&lseed) & 1;
                {   size_t const initError = ZBUFF_compressInit_advanced(zc, dict, dictSize, params, ZSTD_CONTENTSIZE_UNKNOWN);
                    CHECK (ZBUFF_isError(initError),"init error : %s", ZBUFF_getErrorName(initError));
        }   }   }

        /* multi-segments compression test */
        XXH64_reset(&xxhState, 0);
        nbChunks    = (FUZ_rand(&lseed) & 127) + 2;
        for (n=0, cSize=0, totalTestSize=0 ; (n<nbChunks) && (totalTestSize < maxTestSize) ; n++) {
            /* compress random chunk into random size dst buffer */
            {   size_t readChunkSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t dstBuffSize = MIN(cBufferSize - cSize, randomDstSize);
                size_t const srcStart = FUZ_rand(&lseed) % (srcBufferSize - readChunkSize);

                size_t const compressionError = ZBUFF_compressContinue(zc, cBuffer+cSize, &dstBuffSize, srcBuffer+srcStart, &readChunkSize);
                CHECK (ZBUFF_isError(compressionError), "compression error : %s", ZBUFF_getErrorName(compressionError));

                XXH64_update(&xxhState, srcBuffer+srcStart, readChunkSize);
                memcpy(copyBuffer+totalTestSize, srcBuffer+srcStart, readChunkSize);
                cSize += dstBuffSize;
                totalTestSize += readChunkSize;
            }

            /* random flush operation, to mess around */
            if ((FUZ_rand(&lseed) & 15) == 0) {
                size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t dstBuffSize = MIN(cBufferSize - cSize, randomDstSize);
                size_t const flushError = ZBUFF_compressFlush(zc, cBuffer+cSize, &dstBuffSize);
                CHECK (ZBUFF_isError(flushError), "flush error : %s", ZBUFF_getErrorName(flushError));
                cSize += dstBuffSize;
        }   }

        /* final frame epilogue */
        {   size_t remainingToFlush = (size_t)(-1);
            while (remainingToFlush) {
                size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t dstBuffSize = MIN(cBufferSize - cSize, randomDstSize);
                U32 const enoughDstSize = dstBuffSize >= remainingToFlush;
                remainingToFlush = ZBUFF_compressEnd(zc, cBuffer+cSize, &dstBuffSize);
                CHECK (ZBUFF_isError(remainingToFlush), "flush error : %s", ZBUFF_getErrorName(remainingToFlush));
                CHECK (enoughDstSize && remainingToFlush, "ZBUFF_compressEnd() not fully flushed (%u remaining), but enough space available", (U32)remainingToFlush);
                cSize += dstBuffSize;
        }   }
        crcOrig = XXH64_digest(&xxhState);

        /* multi - fragments decompression test */
        ZBUFF_decompressInitDictionary(zd, dict, dictSize);
        errorCode = 1;
        for (totalCSize = 0, totalGenSize = 0 ; errorCode ; ) {
            size_t readCSrcSize = FUZ_randomLength(&lseed, maxSampleLog);
            size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
            size_t dstBuffSize = MIN(dstBufferSize - totalGenSize, randomDstSize);
            errorCode = ZBUFF_decompressContinue(zd, dstBuffer+totalGenSize, &dstBuffSize, cBuffer+totalCSize, &readCSrcSize);
            CHECK (ZBUFF_isError(errorCode), "decompression error : %s", ZBUFF_getErrorName(errorCode));
            totalGenSize += dstBuffSize;
            totalCSize += readCSrcSize;
        }
        CHECK (errorCode != 0, "frame not fully decoded");
        CHECK (totalGenSize != totalTestSize, "decompressed data : wrong size")
        CHECK (totalCSize != cSize, "compressed data should be fully read")
        { U64 const crcDest = XXH64(dstBuffer, totalTestSize, 0);
          if (crcDest!=crcOrig) findDiff(copyBuffer, dstBuffer, totalTestSize);
          CHECK (crcDest!=crcOrig, "decompressed data corrupted"); }

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
        ZBUFF_decompressInit(zd);
        totalCSize = 0;
        totalGenSize = 0;
        while ( (totalCSize < cSize) && (totalGenSize < dstBufferSize) ) {
            size_t readCSrcSize = FUZ_randomLength(&lseed, maxSampleLog);
            size_t const randomDstSize = FUZ_randomLength(&lseed, maxSampleLog);
            size_t dstBuffSize = MIN(dstBufferSize - totalGenSize, randomDstSize);
            size_t const decompressError = ZBUFF_decompressContinue(zd, dstBuffer+totalGenSize, &dstBuffSize, cBuffer+totalCSize, &readCSrcSize);
            if (ZBUFF_isError(decompressError)) break;   /* error correctly detected */
            totalGenSize += dstBuffSize;
            totalCSize += readCSrcSize;
    }   }
    DISPLAY("\r%u fuzzer tests completed   \n", testNb);

_cleanup:
    ZBUFF_freeCCtx(zc);
    ZBUFF_freeDCtx(zd);
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
int FUZ_usage(const char* programName)
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


int main(int argc, const char** argv)
{
    U32 seed=0;
    int seedset=0;
    int argNb;
    int nbTests = nbTestsDefault;
    int testNb = 0;
    int proba = FUZ_COMPRESSIBILITY_DEFAULT;
    int result=0;
    U32 mainPause = 0;
    const char* programName = argv[0];
    ZSTD_customMem customMem = { ZBUFF_allocFunction, ZBUFF_freeFunction, NULL };
    ZSTD_customMem customNULL = { NULL, NULL, NULL };

    /* Check command line */
    for(argNb=1; argNb<argc; argNb++) {
        const char* argument = argv[argNb];
        if(!argument) continue;   /* Protection if argument empty */

        /* Parsing commands. Aggregated commands are allowed */
        if (argument[0]=='-') {
            argument++;

            while (*argument!=0) {
                switch(*argument)
                {
                case 'h':
                    return FUZ_usage(programName);
                case 'v':
                    argument++;
                    g_displayLevel=4;
                    break;
                case 'q':
                    argument++;
                    g_displayLevel--;
                    break;
                case 'p': /* pause at the end */
                    argument++;
                    mainPause = 1;
                    break;

                case 'i':
                    argument++;
                    nbTests=0; g_clockTime=0;
                    while ((*argument>='0') && (*argument<='9')) {
                        nbTests *= 10;
                        nbTests += *argument - '0';
                        argument++;
                    }
                    break;

                case 'T':
                    argument++;
                    nbTests=0; g_clockTime=0;
                    while ((*argument>='0') && (*argument<='9')) {
                        g_clockTime *= 10;
                        g_clockTime += *argument - '0';
                        argument++;
                    }
                    if (*argument=='m') g_clockTime *=60, argument++;
                    if (*argument=='n') argument++;
                    g_clockTime *= SEC_TO_MICRO;
                    break;

                case 's':
                    argument++;
                    seed=0;
                    seedset=1;
                    while ((*argument>='0') && (*argument<='9')) {
                        seed *= 10;
                        seed += *argument - '0';
                        argument++;
                    }
                    break;

                case 't':
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
    DISPLAY("Starting zstd_buffered tester (%i-bits, %s)\n", (int)(sizeof(size_t)*8), ZSTD_VERSION_STRING);

    if (!seedset) {
        time_t const t = time(NULL);
        U32 const h = XXH32(&t, sizeof(t), 1);
        seed = h % 10000;
    }
    DISPLAY("Seed = %u\n", seed);
    if (proba!=FUZ_COMPRESSIBILITY_DEFAULT) DISPLAY("Compressibility : %i%%\n", proba);

    if (nbTests<=0) nbTests=1;

    if (testNb==0) {
        result = basicUnitTests(0, ((double)proba) / 100, customNULL);  /* constant seed for predictability */
        if (!result) {
            DISPLAYLEVEL(4, "Unit tests using customMem :\n")
            result = basicUnitTests(0, ((double)proba) / 100, customMem);  /* use custom memory allocation functions */
    }   }

    if (!result)
        result = fuzzerTests(seed, nbTests, testNb, ((double)proba) / 100);

    if (mainPause) {
        int unused;
        DISPLAY("Press Enter \n");
        unused = getchar();
        (void)unused;
    }
    return result;
}
