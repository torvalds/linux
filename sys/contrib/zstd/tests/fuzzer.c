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
#  define _CRT_SECURE_NO_WARNINGS   /* fgets */
#  pragma warning(disable : 4127)   /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4204)   /* disable: C4204: non-constant aggregate initializer */
#endif


/*-************************************
*  Includes
**************************************/
#include <stdlib.h>       /* free */
#include <stdio.h>        /* fgets, sscanf */
#include <string.h>       /* strcmp */
#include <assert.h>
#define ZSTD_STATIC_LINKING_ONLY  /* ZSTD_compressContinue, ZSTD_compressBlock */
#include "fse.h"
#include "zstd.h"         /* ZSTD_VERSION_STRING */
#include "zstd_errors.h"  /* ZSTD_getErrorCode */
#include "zstdmt_compress.h"
#define ZDICT_STATIC_LINKING_ONLY
#include "zdict.h"        /* ZDICT_trainFromBuffer */
#include "datagen.h"      /* RDG_genBuffer */
#include "mem.h"
#define XXH_STATIC_LINKING_ONLY   /* XXH64_state_t */
#include "xxhash.h"       /* XXH64 */
#include "util.h"


/*-************************************
*  Constants
**************************************/
#define KB *(1U<<10)
#define MB *(1U<<20)
#define GB *(1U<<30)

static const int FUZ_compressibility_default = 50;
static const int nbTestsDefault = 30000;


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


/*-*******************************************************
*  Compile time test
*********************************************************/
#undef MIN
#undef MAX
/* Declaring the function is it isn't unused */
void FUZ_bug976(void);
void FUZ_bug976(void)
{   /* these constants shall not depend on MIN() macro */
    assert(ZSTD_HASHLOG_MAX < 31);
    assert(ZSTD_CHAINLOG_MAX < 31);
}


/*-*******************************************************
*  Internal functions
*********************************************************/
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

#define FUZ_rotl32(x,r) ((x << r) | (x >> (32 - r)))
static U32 FUZ_rand(U32* src)
{
    static const U32 prime1 = 2654435761U;
    static const U32 prime2 = 2246822519U;
    U32 rand32 = *src;
    rand32 *= prime1;
    rand32 += prime2;
    rand32  = FUZ_rotl32(rand32, 13);
    *src = rand32;
    return rand32 >> 5;
}

static U32 FUZ_highbit32(U32 v32)
{
    unsigned nbBits = 0;
    if (v32==0) return 0;
    while (v32) v32 >>= 1, nbBits++;
    return nbBits;
}


/*=============================================
*   Test macros
=============================================*/
#define CHECK_Z(f) {                               \
    size_t const err = f;                          \
    if (ZSTD_isError(err)) {                       \
        DISPLAY("Error => %s : %s ",               \
                #f, ZSTD_getErrorName(err));       \
        exit(1);                                   \
}   }

#define CHECK_V(var, fn)  size_t const var = fn; if (ZSTD_isError(var)) goto _output_error
#define CHECK(fn)  { CHECK_V(err, fn); }
#define CHECKPLUS(var, fn, more)  { CHECK_V(var, fn); more; }

#define CHECK_EQ(lhs, rhs) {                                      \
    if ((lhs) != (rhs)) {                                         \
        DISPLAY("Error L%u => %s != %s ", __LINE__, #lhs, #rhs);  \
        goto _output_error;                                       \
    }                                                             \
}


/*=============================================
*   Memory Tests
=============================================*/
#if defined(__APPLE__) && defined(__MACH__)

#include <malloc/malloc.h>    /* malloc_size */

typedef struct {
    unsigned long long totalMalloc;
    size_t currentMalloc;
    size_t peakMalloc;
    unsigned nbMalloc;
    unsigned nbFree;
} mallocCounter_t;

static const mallocCounter_t INIT_MALLOC_COUNTER = { 0, 0, 0, 0, 0 };

static void* FUZ_mallocDebug(void* counter, size_t size)
{
    mallocCounter_t* const mcPtr = (mallocCounter_t*)counter;
    void* const ptr = malloc(size);
    if (ptr==NULL) return NULL;
    DISPLAYLEVEL(4, "allocating %u KB => effectively %u KB \n",
        (unsigned)(size >> 10), (unsigned)(malloc_size(ptr) >> 10));  /* OS-X specific */
    mcPtr->totalMalloc += size;
    mcPtr->currentMalloc += size;
    if (mcPtr->currentMalloc > mcPtr->peakMalloc)
        mcPtr->peakMalloc = mcPtr->currentMalloc;
    mcPtr->nbMalloc += 1;
    return ptr;
}

static void FUZ_freeDebug(void* counter, void* address)
{
    mallocCounter_t* const mcPtr = (mallocCounter_t*)counter;
    DISPLAYLEVEL(4, "freeing %u KB \n", (unsigned)(malloc_size(address) >> 10));
    mcPtr->nbFree += 1;
    mcPtr->currentMalloc -= malloc_size(address);  /* OS-X specific */
    free(address);
}

static void FUZ_displayMallocStats(mallocCounter_t count)
{
    DISPLAYLEVEL(3, "peak:%6u KB,  nbMallocs:%2u, total:%6u KB \n",
        (unsigned)(count.peakMalloc >> 10),
        count.nbMalloc,
        (unsigned)(count.totalMalloc >> 10));
}

static int FUZ_mallocTests_internal(unsigned seed, double compressibility, unsigned part,
                void* inBuffer, size_t inSize, void* outBuffer, size_t outSize)
{
    /* test only played in verbose mode, as they are long */
    if (g_displayLevel<3) return 0;

    /* Create compressible noise */
    if (!inBuffer || !outBuffer) {
        DISPLAY("Not enough memory, aborting\n");
        exit(1);
    }
    RDG_genBuffer(inBuffer, inSize, compressibility, 0. /*auto*/, seed);

    /* simple compression tests */
    if (part <= 1)
    {   int compressionLevel;
        for (compressionLevel=1; compressionLevel<=6; compressionLevel++) {
            mallocCounter_t malcount = INIT_MALLOC_COUNTER;
            ZSTD_customMem const cMem = { FUZ_mallocDebug, FUZ_freeDebug, &malcount };
            ZSTD_CCtx* const cctx = ZSTD_createCCtx_advanced(cMem);
            CHECK_Z( ZSTD_compressCCtx(cctx, outBuffer, outSize, inBuffer, inSize, compressionLevel) );
            ZSTD_freeCCtx(cctx);
            DISPLAYLEVEL(3, "compressCCtx level %i : ", compressionLevel);
            FUZ_displayMallocStats(malcount);
    }   }

    /* streaming compression tests */
    if (part <= 2)
    {   int compressionLevel;
        for (compressionLevel=1; compressionLevel<=6; compressionLevel++) {
            mallocCounter_t malcount = INIT_MALLOC_COUNTER;
            ZSTD_customMem const cMem = { FUZ_mallocDebug, FUZ_freeDebug, &malcount };
            ZSTD_CCtx* const cstream = ZSTD_createCStream_advanced(cMem);
            ZSTD_outBuffer out = { outBuffer, outSize, 0 };
            ZSTD_inBuffer in = { inBuffer, inSize, 0 };
            CHECK_Z( ZSTD_initCStream(cstream, compressionLevel) );
            CHECK_Z( ZSTD_compressStream(cstream, &out, &in) );
            CHECK_Z( ZSTD_endStream(cstream, &out) );
            ZSTD_freeCStream(cstream);
            DISPLAYLEVEL(3, "compressStream level %i : ", compressionLevel);
            FUZ_displayMallocStats(malcount);
    }   }

    /* advanced MT API test */
    if (part <= 3)
    {   unsigned nbThreads;
        for (nbThreads=1; nbThreads<=4; nbThreads++) {
            int compressionLevel;
            for (compressionLevel=1; compressionLevel<=6; compressionLevel++) {
                mallocCounter_t malcount = INIT_MALLOC_COUNTER;
                ZSTD_customMem const cMem = { FUZ_mallocDebug, FUZ_freeDebug, &malcount };
                ZSTD_CCtx* const cctx = ZSTD_createCCtx_advanced(cMem);
                CHECK_Z( ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, compressionLevel) );
                CHECK_Z( ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, nbThreads) );
                CHECK_Z( ZSTD_compress2(cctx, outBuffer, outSize, inBuffer, inSize) );
                ZSTD_freeCCtx(cctx);
                DISPLAYLEVEL(3, "compress_generic,-T%u,end level %i : ",
                                nbThreads, compressionLevel);
                FUZ_displayMallocStats(malcount);
    }   }   }

    /* advanced MT streaming API test */
    if (part <= 4)
    {   unsigned nbThreads;
        for (nbThreads=1; nbThreads<=4; nbThreads++) {
            int compressionLevel;
            for (compressionLevel=1; compressionLevel<=6; compressionLevel++) {
                mallocCounter_t malcount = INIT_MALLOC_COUNTER;
                ZSTD_customMem const cMem = { FUZ_mallocDebug, FUZ_freeDebug, &malcount };
                ZSTD_CCtx* const cctx = ZSTD_createCCtx_advanced(cMem);
                ZSTD_outBuffer out = { outBuffer, outSize, 0 };
                ZSTD_inBuffer in = { inBuffer, inSize, 0 };
                CHECK_Z( ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, compressionLevel) );
                CHECK_Z( ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, nbThreads) );
                CHECK_Z( ZSTD_compressStream2(cctx, &out, &in, ZSTD_e_continue) );
                while ( ZSTD_compressStream2(cctx, &out, &in, ZSTD_e_end) ) {}
                ZSTD_freeCCtx(cctx);
                DISPLAYLEVEL(3, "compress_generic,-T%u,continue level %i : ",
                                nbThreads, compressionLevel);
                FUZ_displayMallocStats(malcount);
    }   }   }

    return 0;
}

static int FUZ_mallocTests(unsigned seed, double compressibility, unsigned part)
{
    size_t const inSize = 64 MB + 16 MB + 4 MB + 1 MB + 256 KB + 64 KB; /* 85.3 MB */
    size_t const outSize = ZSTD_compressBound(inSize);
    void* const inBuffer = malloc(inSize);
    void* const outBuffer = malloc(outSize);
    int result;

    /* Create compressible noise */
    if (!inBuffer || !outBuffer) {
        DISPLAY("Not enough memory, aborting \n");
        exit(1);
    }

    result = FUZ_mallocTests_internal(seed, compressibility, part,
                    inBuffer, inSize, outBuffer, outSize);

    free(inBuffer);
    free(outBuffer);
    return result;
}

#else

static int FUZ_mallocTests(unsigned seed, double compressibility, unsigned part)
{
    (void)seed; (void)compressibility; (void)part;
    return 0;
}

#endif

/*=============================================
*   Unit tests
=============================================*/

static int basicUnitTests(U32 seed, double compressibility)
{
    size_t const CNBuffSize = 5 MB;
    void* const CNBuffer = malloc(CNBuffSize);
    size_t const compressedBufferSize = ZSTD_compressBound(CNBuffSize);
    void* const compressedBuffer = malloc(compressedBufferSize);
    void* const decodedBuffer = malloc(CNBuffSize);
    int testResult = 0;
    unsigned testNb=0;
    size_t cSize;

    /* Create compressible noise */
    if (!CNBuffer || !compressedBuffer || !decodedBuffer) {
        DISPLAY("Not enough memory, aborting\n");
        testResult = 1;
        goto _end;
    }
    RDG_genBuffer(CNBuffer, CNBuffSize, compressibility, 0., seed);

    /* Basic tests */
    DISPLAYLEVEL(3, "test%3u : ZSTD_getErrorName : ", testNb++);
    {   const char* errorString = ZSTD_getErrorName(0);
        DISPLAYLEVEL(3, "OK : %s \n", errorString);
    }

    DISPLAYLEVEL(3, "test%3u : ZSTD_getErrorName with wrong value : ", testNb++);
    {   const char* errorString = ZSTD_getErrorName(499);
        DISPLAYLEVEL(3, "OK : %s \n", errorString);
    }

    DISPLAYLEVEL(3, "test%3u : min compression level : ", testNb++);
    {   int const mcl = ZSTD_minCLevel();
        DISPLAYLEVEL(3, "%i (OK) \n", mcl);
    }

    DISPLAYLEVEL(3, "test%3u : compress %u bytes : ", testNb++, (unsigned)CNBuffSize);
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        if (cctx==NULL) goto _output_error;
        CHECKPLUS(r, ZSTD_compressCCtx(cctx,
                            compressedBuffer, compressedBufferSize,
                            CNBuffer, CNBuffSize, 1),
                  cSize=r );
        DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBuffSize*100);

        DISPLAYLEVEL(3, "test%3i : size of cctx for level 1 : ", testNb++);
        {   size_t const cctxSize = ZSTD_sizeof_CCtx(cctx);
            DISPLAYLEVEL(3, "%u bytes \n", (unsigned)cctxSize);
        }
        ZSTD_freeCCtx(cctx);
    }

    DISPLAYLEVEL(3, "test%3i : decompress skippable frame -8 size : ", testNb++);
    {
       char const skippable8[] = "\x50\x2a\x4d\x18\xf8\xff\xff\xff";
       size_t const size = ZSTD_decompress(NULL, 0, skippable8, 8);
       if (!ZSTD_isError(size)) goto _output_error;
    }
    DISPLAYLEVEL(3, "OK \n");


    DISPLAYLEVEL(3, "test%3i : ZSTD_getFrameContentSize test : ", testNb++);
    {   unsigned long long const rSize = ZSTD_getFrameContentSize(compressedBuffer, cSize);
        if (rSize != CNBuffSize) goto _output_error;
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : ZSTD_findDecompressedSize test : ", testNb++);
    {   unsigned long long const rSize = ZSTD_findDecompressedSize(compressedBuffer, cSize);
        if (rSize != CNBuffSize) goto _output_error;
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : decompress %u bytes : ", testNb++, (unsigned)CNBuffSize);
    { size_t const r = ZSTD_decompress(decodedBuffer, CNBuffSize, compressedBuffer, cSize);
      if (r != CNBuffSize) goto _output_error; }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : check decompressed result : ", testNb++);
    {   size_t u;
        for (u=0; u<CNBuffSize; u++) {
            if (((BYTE*)decodedBuffer)[u] != ((BYTE*)CNBuffer)[u]) goto _output_error;;
    }   }
    DISPLAYLEVEL(3, "OK \n");


    DISPLAYLEVEL(3, "test%3i : decompress with null dict : ", testNb++);
    {   ZSTD_DCtx* const dctx = ZSTD_createDCtx(); assert(dctx != NULL);
        {   size_t const r = ZSTD_decompress_usingDict(dctx,
                                                    decodedBuffer, CNBuffSize,
                                                    compressedBuffer, cSize,
                                                    NULL, 0);
            if (r != CNBuffSize) goto _output_error;
        }
        ZSTD_freeDCtx(dctx);
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : decompress with null DDict : ", testNb++);
    {   ZSTD_DCtx* const dctx = ZSTD_createDCtx(); assert(dctx != NULL);
        {   size_t const r = ZSTD_decompress_usingDDict(dctx,
                                                    decodedBuffer, CNBuffSize,
                                                    compressedBuffer, cSize,
                                                    NULL);
            if (r != CNBuffSize) goto _output_error;
        }
        ZSTD_freeDCtx(dctx);
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : decompress with 1 missing byte : ", testNb++);
    { size_t const r = ZSTD_decompress(decodedBuffer, CNBuffSize, compressedBuffer, cSize-1);
      if (!ZSTD_isError(r)) goto _output_error;
      if (ZSTD_getErrorCode((size_t)r) != ZSTD_error_srcSize_wrong) goto _output_error; }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : decompress with 1 too much byte : ", testNb++);
    { size_t const r = ZSTD_decompress(decodedBuffer, CNBuffSize, compressedBuffer, cSize+1);
      if (!ZSTD_isError(r)) goto _output_error;
      if (ZSTD_getErrorCode(r) != ZSTD_error_srcSize_wrong) goto _output_error; }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : decompress too large input : ", testNb++);
    { size_t const r = ZSTD_decompress(decodedBuffer, CNBuffSize, compressedBuffer, compressedBufferSize);
      if (!ZSTD_isError(r)) goto _output_error;
      if (ZSTD_getErrorCode(r) != ZSTD_error_srcSize_wrong) goto _output_error; }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3d : check CCtx size after compressing empty input : ", testNb++);
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        size_t const r = ZSTD_compressCCtx(cctx, compressedBuffer, compressedBufferSize, NULL, 0, 19);
        if (ZSTD_isError(r)) goto _output_error;
        if (ZSTD_sizeof_CCtx(cctx) > (1U << 20)) goto _output_error;
        ZSTD_freeCCtx(cctx);
        cSize = r;
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3d : decompress empty frame into NULL : ", testNb++);
    {   size_t const r = ZSTD_decompress(NULL, 0, compressedBuffer, cSize);
        if (ZSTD_isError(r)) goto _output_error;
        if (r != 0) goto _output_error;
    }
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        ZSTD_outBuffer output;
        if (cctx==NULL) goto _output_error;
        output.dst = compressedBuffer;
        output.size = compressedBufferSize;
        output.pos = 0;
        CHECK_Z( ZSTD_initCStream(cctx, 1) );    /* content size unknown */
        CHECK_Z( ZSTD_flushStream(cctx, &output) );   /* ensure no possibility to "concatenate" and determine the content size */
        CHECK_Z( ZSTD_endStream(cctx, &output) );
        ZSTD_freeCCtx(cctx);
        /* single scan decompression */
        {   size_t const r = ZSTD_decompress(NULL, 0, compressedBuffer, output.pos);
            if (ZSTD_isError(r)) goto _output_error;
            if (r != 0) goto _output_error;
        }
        /* streaming decompression */
        {   ZSTD_DCtx* const dstream = ZSTD_createDStream();
            ZSTD_inBuffer dinput;
            ZSTD_outBuffer doutput;
            size_t ipos;
            if (dstream==NULL) goto _output_error;
            dinput.src = compressedBuffer;
            dinput.size = 0;
            dinput.pos = 0;
            doutput.dst = NULL;
            doutput.size = 0;
            doutput.pos = 0;
            CHECK_Z ( ZSTD_initDStream(dstream) );
            for (ipos=1; ipos<=output.pos; ipos++) {
                dinput.size = ipos;
                CHECK_Z ( ZSTD_decompressStream(dstream, &doutput, &dinput) );
            }
            if (doutput.pos != 0) goto _output_error;
            ZSTD_freeDStream(dstream);
        }
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3d : re-use CCtx with expanding block size : ", testNb++);
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        ZSTD_parameters const params = ZSTD_getParams(1, ZSTD_CONTENTSIZE_UNKNOWN, 0);
        assert(params.fParams.contentSizeFlag == 1);  /* block size will be adapted if pledgedSrcSize is enabled */
        CHECK_Z( ZSTD_compressBegin_advanced(cctx, NULL, 0, params, 1 /*pledgedSrcSize*/) );
        CHECK_Z( ZSTD_compressEnd(cctx, compressedBuffer, compressedBufferSize, CNBuffer, 1) ); /* creates a block size of 1 */

        CHECK_Z( ZSTD_compressBegin_advanced(cctx, NULL, 0, params, ZSTD_CONTENTSIZE_UNKNOWN) );  /* re-use same parameters */
        {   size_t const inSize = 2* 128 KB;
            size_t const outSize = ZSTD_compressBound(inSize);
            CHECK_Z( ZSTD_compressEnd(cctx, compressedBuffer, outSize, CNBuffer, inSize) );
            /* will fail if blockSize is not resized */
        }
        ZSTD_freeCCtx(cctx);
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3d : re-using a CCtx should compress the same : ", testNb++);
    {   size_t const sampleSize = 30;
        int i;
        for (i=0; i<20; i++)
            ((char*)CNBuffer)[i] = (char)i;   /* ensure no match during initial section */
        memcpy((char*)CNBuffer + 20, CNBuffer, 10);   /* create one match, starting from beginning of sample, which is the difficult case (see #1241) */
        for (i=1; i<=19; i++) {
            ZSTD_CCtx* const cctx = ZSTD_createCCtx();
            size_t size1, size2;
            DISPLAYLEVEL(5, "l%i ", i);
            size1 = ZSTD_compressCCtx(cctx, compressedBuffer, compressedBufferSize, CNBuffer, sampleSize, i);
            CHECK_Z(size1);

            size2 = ZSTD_compressCCtx(cctx, compressedBuffer, compressedBufferSize, CNBuffer, sampleSize, i);
            CHECK_Z(size2);
            CHECK_EQ(size1, size2);

            CHECK_Z( ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, i) );
            size2 = ZSTD_compress2(cctx, compressedBuffer, compressedBufferSize, CNBuffer, sampleSize);
            CHECK_Z(size2);
            CHECK_EQ(size1, size2);

            size2 = ZSTD_compress2(cctx, compressedBuffer, ZSTD_compressBound(sampleSize) - 1, CNBuffer, sampleSize);  /* force streaming, as output buffer is not large enough to guarantee success */
            CHECK_Z(size2);
            CHECK_EQ(size1, size2);

            {   ZSTD_inBuffer inb;
                ZSTD_outBuffer outb;
                inb.src = CNBuffer;
                inb.pos = 0;
                inb.size = sampleSize;
                outb.dst = compressedBuffer;
                outb.pos = 0;
                outb.size = ZSTD_compressBound(sampleSize) - 1;  /* force streaming, as output buffer is not large enough to guarantee success */
                CHECK_Z( ZSTD_compressStream2(cctx, &outb, &inb, ZSTD_e_end) );
                assert(inb.pos == inb.size);
                CHECK_EQ(size1, outb.pos);
            }

            ZSTD_freeCCtx(cctx);
        }
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3d : btultra2 & 1st block : ", testNb++);
    {   size_t const sampleSize = 1024;
        ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        ZSTD_inBuffer inb;
        ZSTD_outBuffer outb;
        inb.src = CNBuffer;
        inb.pos = 0;
        inb.size = 0;
        outb.dst = compressedBuffer;
        outb.pos = 0;
        outb.size = compressedBufferSize;
        CHECK_Z( ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, ZSTD_maxCLevel()) );

        inb.size = sampleSize;   /* start with something, so that context is already used */
        CHECK_Z( ZSTD_compressStream2(cctx, &outb, &inb, ZSTD_e_end) );   /* will break internal assert if stats_init is not disabled */
        assert(inb.pos == inb.size);
        outb.pos = 0;     /* cancel output */

        CHECK_Z( ZSTD_CCtx_setPledgedSrcSize(cctx, sampleSize) );
        inb.size = 4;   /* too small size : compression will be skipped */
        inb.pos = 0;
        CHECK_Z( ZSTD_compressStream2(cctx, &outb, &inb, ZSTD_e_flush) );
        assert(inb.pos == inb.size);

        inb.size += 5;   /* too small size : compression will be skipped */
        CHECK_Z( ZSTD_compressStream2(cctx, &outb, &inb, ZSTD_e_flush) );
        assert(inb.pos == inb.size);

        inb.size += 11;   /* small enough to attempt compression */
        CHECK_Z( ZSTD_compressStream2(cctx, &outb, &inb, ZSTD_e_flush) );
        assert(inb.pos == inb.size);

        assert(inb.pos < sampleSize);
        inb.size = sampleSize;   /* large enough to trigger stats_init, but no longer at beginning */
        CHECK_Z( ZSTD_compressStream2(cctx, &outb, &inb, ZSTD_e_end) );   /* will break internal assert if stats_init is not disabled */
        assert(inb.pos == inb.size);
        ZSTD_freeCCtx(cctx);
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3d : ZSTD_CCtx_getParameter() : ", testNb++);
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        ZSTD_outBuffer out = {NULL, 0, 0};
        ZSTD_inBuffer in = {NULL, 0, 0};
        int value;

        CHECK_Z(ZSTD_CCtx_getParameter(cctx, ZSTD_c_compressionLevel, &value));
        CHECK_EQ(value, 3);
        CHECK_Z(ZSTD_CCtx_getParameter(cctx, ZSTD_c_hashLog, &value));
        CHECK_EQ(value, 0);
        CHECK_Z(ZSTD_CCtx_setParameter(cctx, ZSTD_c_hashLog, ZSTD_HASHLOG_MIN));
        CHECK_Z(ZSTD_CCtx_getParameter(cctx, ZSTD_c_compressionLevel, &value));
        CHECK_EQ(value, 3);
        CHECK_Z(ZSTD_CCtx_getParameter(cctx, ZSTD_c_hashLog, &value));
        CHECK_EQ(value, ZSTD_HASHLOG_MIN);
        CHECK_Z(ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, 7));
        CHECK_Z(ZSTD_CCtx_getParameter(cctx, ZSTD_c_compressionLevel, &value));
        CHECK_EQ(value, 7);
        CHECK_Z(ZSTD_CCtx_getParameter(cctx, ZSTD_c_hashLog, &value));
        CHECK_EQ(value, ZSTD_HASHLOG_MIN);
        /* Start a compression job */
        ZSTD_compressStream2(cctx, &out, &in, ZSTD_e_continue);
        CHECK_Z(ZSTD_CCtx_getParameter(cctx, ZSTD_c_compressionLevel, &value));
        CHECK_EQ(value, 7);
        CHECK_Z(ZSTD_CCtx_getParameter(cctx, ZSTD_c_hashLog, &value));
        CHECK_EQ(value, ZSTD_HASHLOG_MIN);
        /* Reset the CCtx */
        ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
        CHECK_Z(ZSTD_CCtx_getParameter(cctx, ZSTD_c_compressionLevel, &value));
        CHECK_EQ(value, 7);
        CHECK_Z(ZSTD_CCtx_getParameter(cctx, ZSTD_c_hashLog, &value));
        CHECK_EQ(value, ZSTD_HASHLOG_MIN);
        /* Reset the parameters */
        ZSTD_CCtx_reset(cctx, ZSTD_reset_parameters);
        CHECK_Z(ZSTD_CCtx_getParameter(cctx, ZSTD_c_compressionLevel, &value));
        CHECK_EQ(value, 3);
        CHECK_Z(ZSTD_CCtx_getParameter(cctx, ZSTD_c_hashLog, &value));
        CHECK_EQ(value, 0);

        ZSTD_freeCCtx(cctx);
    }
    DISPLAYLEVEL(3, "OK \n");

    /* this test is really too long, and should be made faster */
    DISPLAYLEVEL(3, "test%3d : overflow protection with large windowLog : ", testNb++);
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        ZSTD_parameters params = ZSTD_getParams(-999, ZSTD_CONTENTSIZE_UNKNOWN, 0);
        size_t const nbCompressions = ((1U << 31) / CNBuffSize) + 2;   /* ensure U32 overflow protection is triggered */
        size_t cnb;
        assert(cctx != NULL);
        params.fParams.contentSizeFlag = 0;
        params.cParams.windowLog = ZSTD_WINDOWLOG_MAX;
        for (cnb = 0; cnb < nbCompressions; ++cnb) {
            DISPLAYLEVEL(6, "run %zu / %zu \n", cnb, nbCompressions);
            CHECK_Z( ZSTD_compressBegin_advanced(cctx, NULL, 0, params, ZSTD_CONTENTSIZE_UNKNOWN) );  /* re-use same parameters */
            CHECK_Z( ZSTD_compressEnd(cctx, compressedBuffer, compressedBufferSize, CNBuffer, CNBuffSize) );
        }
        ZSTD_freeCCtx(cctx);
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3d : size down context : ", testNb++);
    {   ZSTD_CCtx* const largeCCtx = ZSTD_createCCtx();
        assert(largeCCtx != NULL);
        CHECK_Z( ZSTD_compressBegin(largeCCtx, 19) );   /* streaming implies ZSTD_CONTENTSIZE_UNKNOWN, which maximizes memory usage */
        CHECK_Z( ZSTD_compressEnd(largeCCtx, compressedBuffer, compressedBufferSize, CNBuffer, 1) );
        {   size_t const largeCCtxSize = ZSTD_sizeof_CCtx(largeCCtx);   /* size of context must be measured after compression */
            {   ZSTD_CCtx* const smallCCtx = ZSTD_createCCtx();
                assert(smallCCtx != NULL);
                CHECK_Z(ZSTD_compressCCtx(smallCCtx, compressedBuffer, compressedBufferSize, CNBuffer, 1, 1));
                {   size_t const smallCCtxSize = ZSTD_sizeof_CCtx(smallCCtx);
                    DISPLAYLEVEL(5, "(large) %zuKB > 32*%zuKB (small) : ",
                                largeCCtxSize>>10, smallCCtxSize>>10);
                    assert(largeCCtxSize > 32* smallCCtxSize);  /* note : "too large" definition is handled within zstd_compress.c .
                                                                 * make this test case extreme, so that it doesn't depend on a possibly fluctuating definition */
                }
                ZSTD_freeCCtx(smallCCtx);
            }
            {   U32 const maxNbAttempts = 1100;   /* nb of usages before triggering size down is handled within zstd_compress.c.
                                                   * currently defined as 128x, but could be adjusted in the future.
                                                   * make this test long enough so that it's not too much tied to the current definition within zstd_compress.c */
                unsigned u;
                for (u=0; u<maxNbAttempts; u++) {
                    CHECK_Z(ZSTD_compressCCtx(largeCCtx, compressedBuffer, compressedBufferSize, CNBuffer, 1, 1));
                    if (ZSTD_sizeof_CCtx(largeCCtx) < largeCCtxSize) break;   /* sized down */
                }
                DISPLAYLEVEL(5, "size down after %u attempts : ", u);
                if (u==maxNbAttempts) goto _output_error;   /* no sizedown happened */
            }
        }
        ZSTD_freeCCtx(largeCCtx);
    }
    DISPLAYLEVEL(3, "OK \n");

    /* Static CCtx tests */
#define STATIC_CCTX_LEVEL 3
    DISPLAYLEVEL(3, "test%3i : create static CCtx for level %u :", testNb++, STATIC_CCTX_LEVEL);
    {   size_t const staticCCtxSize = ZSTD_estimateCStreamSize(STATIC_CCTX_LEVEL);
        void* const staticCCtxBuffer = malloc(staticCCtxSize);
        size_t const staticDCtxSize = ZSTD_estimateDCtxSize();
        void* const staticDCtxBuffer = malloc(staticDCtxSize);
        if (staticCCtxBuffer==NULL || staticDCtxBuffer==NULL) {
            free(staticCCtxBuffer);
            free(staticDCtxBuffer);
            DISPLAY("Not enough memory, aborting\n");
            testResult = 1;
            goto _end;
        }
        {   ZSTD_CCtx* staticCCtx = ZSTD_initStaticCCtx(staticCCtxBuffer, staticCCtxSize);
            ZSTD_DCtx* staticDCtx = ZSTD_initStaticDCtx(staticDCtxBuffer, staticDCtxSize);
            if ((staticCCtx==NULL) || (staticDCtx==NULL)) goto _output_error;
            DISPLAYLEVEL(3, "OK \n");

            DISPLAYLEVEL(3, "test%3i : init CCtx for level %u : ", testNb++, STATIC_CCTX_LEVEL);
            { size_t const r = ZSTD_compressBegin(staticCCtx, STATIC_CCTX_LEVEL);
              if (ZSTD_isError(r)) goto _output_error; }
            DISPLAYLEVEL(3, "OK \n");

            DISPLAYLEVEL(3, "test%3i : simple compression test with static CCtx : ", testNb++);
            CHECKPLUS(r, ZSTD_compressCCtx(staticCCtx,
                            compressedBuffer, compressedBufferSize,
                            CNBuffer, CNBuffSize, STATIC_CCTX_LEVEL),
                      cSize=r );
            DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n",
                            (unsigned)cSize, (double)cSize/CNBuffSize*100);

            DISPLAYLEVEL(3, "test%3i : simple decompression test with static DCtx : ", testNb++);
            { size_t const r = ZSTD_decompressDCtx(staticDCtx,
                                                decodedBuffer, CNBuffSize,
                                                compressedBuffer, cSize);
              if (r != CNBuffSize) goto _output_error; }
            DISPLAYLEVEL(3, "OK \n");

            DISPLAYLEVEL(3, "test%3i : check decompressed result : ", testNb++);
            {   size_t u;
                for (u=0; u<CNBuffSize; u++) {
                    if (((BYTE*)decodedBuffer)[u] != ((BYTE*)CNBuffer)[u])
                        goto _output_error;;
            }   }
            DISPLAYLEVEL(3, "OK \n");

            DISPLAYLEVEL(3, "test%3i : init CCtx for too large level (must fail) : ", testNb++);
            { size_t const r = ZSTD_compressBegin(staticCCtx, ZSTD_maxCLevel());
              if (!ZSTD_isError(r)) goto _output_error; }
            DISPLAYLEVEL(3, "OK \n");

            DISPLAYLEVEL(3, "test%3i : init CCtx for small level %u (should work again) : ", testNb++, 1);
            { size_t const r = ZSTD_compressBegin(staticCCtx, 1);
              if (ZSTD_isError(r)) goto _output_error; }
            DISPLAYLEVEL(3, "OK \n");

            DISPLAYLEVEL(3, "test%3i : init CStream for small level %u : ", testNb++, 1);
            { size_t const r = ZSTD_initCStream(staticCCtx, 1);
              if (ZSTD_isError(r)) goto _output_error; }
            DISPLAYLEVEL(3, "OK \n");

            DISPLAYLEVEL(3, "test%3i : init CStream with dictionary (should fail) : ", testNb++);
            { size_t const r = ZSTD_initCStream_usingDict(staticCCtx, CNBuffer, 64 KB, 1);
              if (!ZSTD_isError(r)) goto _output_error; }
            DISPLAYLEVEL(3, "OK \n");

            DISPLAYLEVEL(3, "test%3i : init DStream (should fail) : ", testNb++);
            { size_t const r = ZSTD_initDStream(staticDCtx);
              if (ZSTD_isError(r)) goto _output_error; }
            {   ZSTD_outBuffer output = { decodedBuffer, CNBuffSize, 0 };
                ZSTD_inBuffer input = { compressedBuffer, ZSTD_FRAMEHEADERSIZE_MAX+1, 0 };
                size_t const r = ZSTD_decompressStream(staticDCtx, &output, &input);
                if (!ZSTD_isError(r)) goto _output_error;
            }
            DISPLAYLEVEL(3, "OK \n");
        }
        free(staticCCtxBuffer);
        free(staticDCtxBuffer);
    }

    DISPLAYLEVEL(3, "test%3i : Static negative levels : ", testNb++);
    {   size_t const cctxSizeN1 = ZSTD_estimateCCtxSize(-1);
        size_t const cctxSizeP1 = ZSTD_estimateCCtxSize(1);
        size_t const cstreamSizeN1 = ZSTD_estimateCStreamSize(-1);
        size_t const cstreamSizeP1 = ZSTD_estimateCStreamSize(1);

        if (!(0 < cctxSizeN1 && cctxSizeN1 <= cctxSizeP1)) goto _output_error;
        if (!(0 < cstreamSizeN1 && cstreamSizeN1 <= cstreamSizeP1)) goto _output_error;
    }
    DISPLAYLEVEL(3, "OK \n");


    /* ZSTDMT simple MT compression test */
    DISPLAYLEVEL(3, "test%3i : create ZSTDMT CCtx : ", testNb++);
    {   ZSTDMT_CCtx* mtctx = ZSTDMT_createCCtx(2);
        if (mtctx==NULL) {
            DISPLAY("mtctx : mot enough memory, aborting \n");
            testResult = 1;
            goto _end;
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3u : compress %u bytes with 2 threads : ", testNb++, (unsigned)CNBuffSize);
        CHECKPLUS(r, ZSTDMT_compressCCtx(mtctx,
                                compressedBuffer, compressedBufferSize,
                                CNBuffer, CNBuffSize,
                                1),
                  cSize=r );
        DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBuffSize*100);

        DISPLAYLEVEL(3, "test%3i : decompressed size test : ", testNb++);
        {   unsigned long long const rSize = ZSTD_getFrameContentSize(compressedBuffer, cSize);
            if (rSize != CNBuffSize)  {
                DISPLAY("ZSTD_getFrameContentSize incorrect : %u != %u \n", (unsigned)rSize, (unsigned)CNBuffSize);
                goto _output_error;
        }   }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : decompress %u bytes : ", testNb++, (unsigned)CNBuffSize);
        { size_t const r = ZSTD_decompress(decodedBuffer, CNBuffSize, compressedBuffer, cSize);
          if (r != CNBuffSize) goto _output_error; }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : check decompressed result : ", testNb++);
        {   size_t u;
            for (u=0; u<CNBuffSize; u++) {
                if (((BYTE*)decodedBuffer)[u] != ((BYTE*)CNBuffer)[u]) goto _output_error;;
        }   }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : compress -T2 with checksum : ", testNb++);
        {   ZSTD_parameters params = ZSTD_getParams(1, CNBuffSize, 0);
            params.fParams.checksumFlag = 1;
            params.fParams.contentSizeFlag = 1;
            CHECKPLUS(r, ZSTDMT_compress_advanced(mtctx,
                                    compressedBuffer, compressedBufferSize,
                                    CNBuffer, CNBuffSize,
                                    NULL, params, 3 /*overlapRLog*/),
                      cSize=r );
        }
        DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBuffSize*100);

        DISPLAYLEVEL(3, "test%3i : decompress %u bytes : ", testNb++, (unsigned)CNBuffSize);
        { size_t const r = ZSTD_decompress(decodedBuffer, CNBuffSize, compressedBuffer, cSize);
          if (r != CNBuffSize) goto _output_error; }
        DISPLAYLEVEL(3, "OK \n");

        ZSTDMT_freeCCtx(mtctx);
    }


    /* Simple API multiframe test */
    DISPLAYLEVEL(3, "test%3i : compress multiple frames : ", testNb++);
    {   size_t off = 0;
        int i;
        int const segs = 4;
        /* only use the first half so we don't push against size limit of compressedBuffer */
        size_t const segSize = (CNBuffSize / 2) / segs;
        for (i = 0; i < segs; i++) {
            CHECK_V(r, ZSTD_compress(
                            (BYTE *)compressedBuffer + off, CNBuffSize - off,
                            (BYTE *)CNBuffer + segSize * i,
                            segSize, 5));
            off += r;
            if (i == segs/2) {
                /* insert skippable frame */
                const U32 skipLen = 129 KB;
                MEM_writeLE32((BYTE*)compressedBuffer + off, ZSTD_MAGIC_SKIPPABLE_START);
                MEM_writeLE32((BYTE*)compressedBuffer + off + 4, skipLen);
                off += skipLen + ZSTD_SKIPPABLEHEADERSIZE;
            }
        }
        cSize = off;
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : get decompressed size of multiple frames : ", testNb++);
    {   unsigned long long const r = ZSTD_findDecompressedSize(compressedBuffer, cSize);
        if (r != CNBuffSize / 2) goto _output_error; }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : decompress multiple frames : ", testNb++);
    {   CHECK_V(r, ZSTD_decompress(decodedBuffer, CNBuffSize, compressedBuffer, cSize));
        if (r != CNBuffSize / 2) goto _output_error; }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : check decompressed result : ", testNb++);
    if (memcmp(decodedBuffer, CNBuffer, CNBuffSize / 2) != 0) goto _output_error;
    DISPLAYLEVEL(3, "OK \n");

    /* Dictionary and CCtx Duplication tests */
    {   ZSTD_CCtx* const ctxOrig = ZSTD_createCCtx();
        ZSTD_CCtx* const ctxDuplicated = ZSTD_createCCtx();
        ZSTD_DCtx* const dctx = ZSTD_createDCtx();
        static const size_t dictSize = 551;
        assert(dctx != NULL); assert(ctxOrig != NULL); assert(ctxDuplicated != NULL);

        DISPLAYLEVEL(3, "test%3i : copy context too soon : ", testNb++);
        { size_t const copyResult = ZSTD_copyCCtx(ctxDuplicated, ctxOrig, 0);
          if (!ZSTD_isError(copyResult)) goto _output_error; }   /* error must be detected */
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : load dictionary into context : ", testNb++);
        CHECK( ZSTD_compressBegin_usingDict(ctxOrig, CNBuffer, dictSize, 2) );
        CHECK( ZSTD_copyCCtx(ctxDuplicated, ctxOrig, 0) ); /* Begin_usingDict implies unknown srcSize, so match that */
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : compress with flat dictionary : ", testNb++);
        cSize = 0;
        CHECKPLUS(r, ZSTD_compressEnd(ctxOrig, compressedBuffer, compressedBufferSize,
                                           (const char*)CNBuffer + dictSize, CNBuffSize - dictSize),
                  cSize += r);
        DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBuffSize*100);

        DISPLAYLEVEL(3, "test%3i : frame built with flat dictionary should be decompressible : ", testNb++);
        CHECKPLUS(r, ZSTD_decompress_usingDict(dctx,
                                       decodedBuffer, CNBuffSize,
                                       compressedBuffer, cSize,
                                       CNBuffer, dictSize),
                  if (r != CNBuffSize - dictSize) goto _output_error);
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : compress with duplicated context : ", testNb++);
        {   size_t const cSizeOrig = cSize;
            cSize = 0;
            CHECKPLUS(r, ZSTD_compressEnd(ctxDuplicated, compressedBuffer, compressedBufferSize,
                                               (const char*)CNBuffer + dictSize, CNBuffSize - dictSize),
                      cSize += r);
            if (cSize != cSizeOrig) goto _output_error;   /* should be identical ==> same size */
        }
        DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBuffSize*100);

        DISPLAYLEVEL(3, "test%3i : frame built with duplicated context should be decompressible : ", testNb++);
        CHECKPLUS(r, ZSTD_decompress_usingDict(dctx,
                                           decodedBuffer, CNBuffSize,
                                           compressedBuffer, cSize,
                                           CNBuffer, dictSize),
                  if (r != CNBuffSize - dictSize) goto _output_error);
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : decompress with DDict : ", testNb++);
        {   ZSTD_DDict* const ddict = ZSTD_createDDict(CNBuffer, dictSize);
            size_t const r = ZSTD_decompress_usingDDict(dctx, decodedBuffer, CNBuffSize, compressedBuffer, cSize, ddict);
            if (r != CNBuffSize - dictSize) goto _output_error;
            DISPLAYLEVEL(3, "OK (size of DDict : %u) \n", (unsigned)ZSTD_sizeof_DDict(ddict));
            ZSTD_freeDDict(ddict);
        }

        DISPLAYLEVEL(3, "test%3i : decompress with static DDict : ", testNb++);
        {   size_t const ddictBufferSize = ZSTD_estimateDDictSize(dictSize, ZSTD_dlm_byCopy);
            void* ddictBuffer = malloc(ddictBufferSize);
            if (ddictBuffer == NULL) goto _output_error;
            {   const ZSTD_DDict* const ddict = ZSTD_initStaticDDict(ddictBuffer, ddictBufferSize, CNBuffer, dictSize, ZSTD_dlm_byCopy, ZSTD_dct_auto);
                size_t const r = ZSTD_decompress_usingDDict(dctx, decodedBuffer, CNBuffSize, compressedBuffer, cSize, ddict);
                if (r != CNBuffSize - dictSize) goto _output_error;
            }
            free(ddictBuffer);
            DISPLAYLEVEL(3, "OK (size of static DDict : %u) \n", (unsigned)ddictBufferSize);
        }

        DISPLAYLEVEL(3, "test%3i : check content size on duplicated context : ", testNb++);
        {   size_t const testSize = CNBuffSize / 3;
            {   ZSTD_parameters p = ZSTD_getParams(2, testSize, dictSize);
                p.fParams.contentSizeFlag = 1;
                CHECK( ZSTD_compressBegin_advanced(ctxOrig, CNBuffer, dictSize, p, testSize-1) );
            }
            CHECK( ZSTD_copyCCtx(ctxDuplicated, ctxOrig, testSize) );

            CHECKPLUS(r, ZSTD_compressEnd(ctxDuplicated, compressedBuffer, ZSTD_compressBound(testSize),
                                          (const char*)CNBuffer + dictSize, testSize),
                      cSize = r);
            {   ZSTD_frameHeader zfh;
                if (ZSTD_getFrameHeader(&zfh, compressedBuffer, cSize)) goto _output_error;
                if ((zfh.frameContentSize != testSize) && (zfh.frameContentSize != 0)) goto _output_error;
        }   }
        DISPLAYLEVEL(3, "OK \n");

        ZSTD_freeCCtx(ctxOrig);
        ZSTD_freeCCtx(ctxDuplicated);
        ZSTD_freeDCtx(dctx);
    }

    /* Dictionary and dictBuilder tests */
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        size_t const dictBufferCapacity = 16 KB;
        void* dictBuffer = malloc(dictBufferCapacity);
        size_t const totalSampleSize = 1 MB;
        size_t const sampleUnitSize = 8 KB;
        U32 const nbSamples = (U32)(totalSampleSize / sampleUnitSize);
        size_t* const samplesSizes = (size_t*) malloc(nbSamples * sizeof(size_t));
        size_t dictSize;
        U32 dictID;

        if (dictBuffer==NULL || samplesSizes==NULL) {
            free(dictBuffer);
            free(samplesSizes);
            goto _output_error;
        }

        DISPLAYLEVEL(3, "test%3i : dictBuilder on cyclic data : ", testNb++);
        assert(compressedBufferSize >= totalSampleSize);
        { U32 u; for (u=0; u<totalSampleSize; u++) ((BYTE*)decodedBuffer)[u] = (BYTE)u; }
        { U32 u; for (u=0; u<nbSamples; u++) samplesSizes[u] = sampleUnitSize; }
        {   size_t const sDictSize = ZDICT_trainFromBuffer(dictBuffer, dictBufferCapacity,
                                         decodedBuffer, samplesSizes, nbSamples);
            if (ZDICT_isError(sDictSize)) goto _output_error;
            DISPLAYLEVEL(3, "OK, created dictionary of size %u \n", (unsigned)sDictSize);
        }

        DISPLAYLEVEL(3, "test%3i : dictBuilder : ", testNb++);
        { U32 u; for (u=0; u<nbSamples; u++) samplesSizes[u] = sampleUnitSize; }
        dictSize = ZDICT_trainFromBuffer(dictBuffer, dictBufferCapacity,
                                         CNBuffer, samplesSizes, nbSamples);
        if (ZDICT_isError(dictSize)) goto _output_error;
        DISPLAYLEVEL(3, "OK, created dictionary of size %u \n", (unsigned)dictSize);

        DISPLAYLEVEL(3, "test%3i : Multithreaded COVER dictBuilder : ", testNb++);
        { U32 u; for (u=0; u<nbSamples; u++) samplesSizes[u] = sampleUnitSize; }
        {   ZDICT_cover_params_t coverParams;
            memset(&coverParams, 0, sizeof(coverParams));
            coverParams.steps = 8;
            coverParams.nbThreads = 4;
            dictSize = ZDICT_optimizeTrainFromBuffer_cover(
                dictBuffer, dictBufferCapacity,
                CNBuffer, samplesSizes, nbSamples/8,  /* less samples for faster tests */
                &coverParams);
            if (ZDICT_isError(dictSize)) goto _output_error;
        }
        DISPLAYLEVEL(3, "OK, created dictionary of size %u \n", (unsigned)dictSize);

        DISPLAYLEVEL(3, "test%3i : Multithreaded FASTCOVER dictBuilder : ", testNb++);
        { U32 u; for (u=0; u<nbSamples; u++) samplesSizes[u] = sampleUnitSize; }
        {   ZDICT_fastCover_params_t fastCoverParams;
            memset(&fastCoverParams, 0, sizeof(fastCoverParams));
            fastCoverParams.steps = 8;
            fastCoverParams.nbThreads = 4;
            dictSize = ZDICT_optimizeTrainFromBuffer_fastCover(
                dictBuffer, dictBufferCapacity,
                CNBuffer, samplesSizes, nbSamples,
                &fastCoverParams);
            if (ZDICT_isError(dictSize)) goto _output_error;
        }
        DISPLAYLEVEL(3, "OK, created dictionary of size %u \n", (unsigned)dictSize);

        DISPLAYLEVEL(3, "test%3i : check dictID : ", testNb++);
        dictID = ZDICT_getDictID(dictBuffer, dictSize);
        if (dictID==0) goto _output_error;
        DISPLAYLEVEL(3, "OK : %u \n", (unsigned)dictID);

        DISPLAYLEVEL(3, "test%3i : compress with dictionary : ", testNb++);
        cSize = ZSTD_compress_usingDict(cctx, compressedBuffer, compressedBufferSize,
                                        CNBuffer, CNBuffSize,
                                        dictBuffer, dictSize, 4);
        if (ZSTD_isError(cSize)) goto _output_error;
        DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBuffSize*100);

        DISPLAYLEVEL(3, "test%3i : retrieve dictID from dictionary : ", testNb++);
        {   U32 const did = ZSTD_getDictID_fromDict(dictBuffer, dictSize);
            if (did != dictID) goto _output_error;   /* non-conformant (content-only) dictionary */
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : retrieve dictID from frame : ", testNb++);
        {   U32 const did = ZSTD_getDictID_fromFrame(compressedBuffer, cSize);
            if (did != dictID) goto _output_error;   /* non-conformant (content-only) dictionary */
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : frame built with dictionary should be decompressible : ", testNb++);
        {   ZSTD_DCtx* const dctx = ZSTD_createDCtx(); assert(dctx != NULL);
            CHECKPLUS(r, ZSTD_decompress_usingDict(dctx,
                                           decodedBuffer, CNBuffSize,
                                           compressedBuffer, cSize,
                                           dictBuffer, dictSize),
                      if (r != CNBuffSize) goto _output_error);
            ZSTD_freeDCtx(dctx);
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : estimate CDict size : ", testNb++);
        {   ZSTD_compressionParameters const cParams = ZSTD_getCParams(1, CNBuffSize, dictSize);
            size_t const estimatedSize = ZSTD_estimateCDictSize_advanced(dictSize, cParams, ZSTD_dlm_byRef);
            DISPLAYLEVEL(3, "OK : %u \n", (unsigned)estimatedSize);
        }

        DISPLAYLEVEL(3, "test%3i : compress with CDict ", testNb++);
        {   ZSTD_compressionParameters const cParams = ZSTD_getCParams(1, CNBuffSize, dictSize);
            ZSTD_CDict* const cdict = ZSTD_createCDict_advanced(dictBuffer, dictSize,
                                            ZSTD_dlm_byRef, ZSTD_dct_auto,
                                            cParams, ZSTD_defaultCMem);
            DISPLAYLEVEL(3, "(size : %u) : ", (unsigned)ZSTD_sizeof_CDict(cdict));
            cSize = ZSTD_compress_usingCDict(cctx, compressedBuffer, compressedBufferSize,
                                                 CNBuffer, CNBuffSize, cdict);
            ZSTD_freeCDict(cdict);
            if (ZSTD_isError(cSize)) goto _output_error;
        }
        DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBuffSize*100);

        DISPLAYLEVEL(3, "test%3i : retrieve dictID from frame : ", testNb++);
        {   U32 const did = ZSTD_getDictID_fromFrame(compressedBuffer, cSize);
            if (did != dictID) goto _output_error;   /* non-conformant (content-only) dictionary */
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : frame built with dictionary should be decompressible : ", testNb++);
        {   ZSTD_DCtx* const dctx = ZSTD_createDCtx(); assert(dctx != NULL);
            CHECKPLUS(r, ZSTD_decompress_usingDict(dctx,
                                           decodedBuffer, CNBuffSize,
                                           compressedBuffer, cSize,
                                           dictBuffer, dictSize),
                      if (r != CNBuffSize) goto _output_error);
            ZSTD_freeDCtx(dctx);
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : compress with static CDict : ", testNb++);
        {   int const maxLevel = ZSTD_maxCLevel();
            int level;
            for (level = 1; level <= maxLevel; ++level) {
                ZSTD_compressionParameters const cParams = ZSTD_getCParams(level, CNBuffSize, dictSize);
                size_t const cdictSize = ZSTD_estimateCDictSize_advanced(dictSize, cParams, ZSTD_dlm_byCopy);
                void* const cdictBuffer = malloc(cdictSize);
                if (cdictBuffer==NULL) goto _output_error;
                {   const ZSTD_CDict* const cdict = ZSTD_initStaticCDict(
                                                cdictBuffer, cdictSize,
                                                dictBuffer, dictSize,
                                                ZSTD_dlm_byCopy, ZSTD_dct_auto,
                                                cParams);
                    if (cdict == NULL) {
                        DISPLAY("ZSTD_initStaticCDict failed ");
                        goto _output_error;
                    }
                    cSize = ZSTD_compress_usingCDict(cctx,
                                    compressedBuffer, compressedBufferSize,
                                    CNBuffer, MIN(10 KB, CNBuffSize), cdict);
                    if (ZSTD_isError(cSize)) {
                        DISPLAY("ZSTD_compress_usingCDict failed ");
                        goto _output_error;
                }   }
                free(cdictBuffer);
        }   }
        DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBuffSize*100);

        DISPLAYLEVEL(3, "test%3i : ZSTD_compress_usingCDict_advanced, no contentSize, no dictID : ", testNb++);
        {   ZSTD_frameParameters const fParams = { 0 /* frameSize */, 1 /* checksum */, 1 /* noDictID*/ };
            ZSTD_compressionParameters const cParams = ZSTD_getCParams(1, CNBuffSize, dictSize);
            ZSTD_CDict* const cdict = ZSTD_createCDict_advanced(dictBuffer, dictSize, ZSTD_dlm_byRef, ZSTD_dct_auto, cParams, ZSTD_defaultCMem);
            cSize = ZSTD_compress_usingCDict_advanced(cctx, compressedBuffer, compressedBufferSize,
                                                 CNBuffer, CNBuffSize, cdict, fParams);
            ZSTD_freeCDict(cdict);
            if (ZSTD_isError(cSize)) goto _output_error;
        }
        DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBuffSize*100);

        DISPLAYLEVEL(3, "test%3i : try retrieving contentSize from frame : ", testNb++);
        {   U64 const contentSize = ZSTD_getFrameContentSize(compressedBuffer, cSize);
            if (contentSize != ZSTD_CONTENTSIZE_UNKNOWN) goto _output_error;
        }
        DISPLAYLEVEL(3, "OK (unknown)\n");

        DISPLAYLEVEL(3, "test%3i : frame built without dictID should be decompressible : ", testNb++);
        {   ZSTD_DCtx* const dctx = ZSTD_createDCtx(); assert(dctx != NULL);
            CHECKPLUS(r, ZSTD_decompress_usingDict(dctx,
                                           decodedBuffer, CNBuffSize,
                                           compressedBuffer, cSize,
                                           dictBuffer, dictSize),
                      if (r != CNBuffSize) goto _output_error);
            ZSTD_freeDCtx(dctx);
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : ZSTD_compress_advanced, no dictID : ", testNb++);
        {   ZSTD_parameters p = ZSTD_getParams(3, CNBuffSize, dictSize);
            p.fParams.noDictIDFlag = 1;
            cSize = ZSTD_compress_advanced(cctx, compressedBuffer, compressedBufferSize,
                                           CNBuffer, CNBuffSize,
                                           dictBuffer, dictSize, p);
            if (ZSTD_isError(cSize)) goto _output_error;
        }
        DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/CNBuffSize*100);

        DISPLAYLEVEL(3, "test%3i : frame built without dictID should be decompressible : ", testNb++);
        {   ZSTD_DCtx* const dctx = ZSTD_createDCtx(); assert(dctx != NULL);
            CHECKPLUS(r, ZSTD_decompress_usingDict(dctx,
                                           decodedBuffer, CNBuffSize,
                                           compressedBuffer, cSize,
                                           dictBuffer, dictSize),
                      if (r != CNBuffSize) goto _output_error);
            ZSTD_freeDCtx(dctx);
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : dictionary containing only header should return error : ", testNb++);
        {   ZSTD_DCtx* const dctx = ZSTD_createDCtx();
            assert(dctx != NULL);
            {   const size_t ret = ZSTD_decompress_usingDict(
                    dctx, decodedBuffer, CNBuffSize, compressedBuffer, cSize,
                    "\x37\xa4\x30\xec\x11\x22\x33\x44", 8);
                if (ZSTD_getErrorCode(ret) != ZSTD_error_dictionary_corrupted)
                    goto _output_error;
            }
            ZSTD_freeDCtx(dctx);
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : Building cdict w/ ZSTD_dm_fullDict on a good dictionary : ", testNb++);
        {   ZSTD_compressionParameters const cParams = ZSTD_getCParams(1, CNBuffSize, dictSize);
            ZSTD_CDict* const cdict = ZSTD_createCDict_advanced(dictBuffer, dictSize, ZSTD_dlm_byRef, ZSTD_dct_fullDict, cParams, ZSTD_defaultCMem);
            if (cdict==NULL) goto _output_error;
            ZSTD_freeCDict(cdict);
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : Building cdict w/ ZSTD_dm_fullDict on a rawContent (must fail) : ", testNb++);
        {   ZSTD_compressionParameters const cParams = ZSTD_getCParams(1, CNBuffSize, dictSize);
            ZSTD_CDict* const cdict = ZSTD_createCDict_advanced((const char*)dictBuffer+1, dictSize-1, ZSTD_dlm_byRef, ZSTD_dct_fullDict, cParams, ZSTD_defaultCMem);
            if (cdict!=NULL) goto _output_error;
            ZSTD_freeCDict(cdict);
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : Loading rawContent starting with dict header w/ ZSTD_dm_auto should fail : ", testNb++);
        {
            size_t ret;
            MEM_writeLE32((char*)dictBuffer+2, ZSTD_MAGIC_DICTIONARY);
            ret = ZSTD_CCtx_loadDictionary_advanced(
                    cctx, (const char*)dictBuffer+2, dictSize-2, ZSTD_dlm_byRef, ZSTD_dct_auto);
            if (!ZSTD_isError(ret)) goto _output_error;
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : Loading rawContent starting with dict header w/ ZSTD_dm_rawContent should pass : ", testNb++);
        {
            size_t ret;
            MEM_writeLE32((char*)dictBuffer+2, ZSTD_MAGIC_DICTIONARY);
            ret = ZSTD_CCtx_loadDictionary_advanced(
                    cctx, (const char*)dictBuffer+2, dictSize-2, ZSTD_dlm_byRef, ZSTD_dct_rawContent);
            if (ZSTD_isError(ret)) goto _output_error;
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : Dictionary with non-default repcodes : ", testNb++);
        { U32 u; for (u=0; u<nbSamples; u++) samplesSizes[u] = sampleUnitSize; }
        dictSize = ZDICT_trainFromBuffer(dictBuffer, dictSize,
                                         CNBuffer, samplesSizes, nbSamples);
        if (ZDICT_isError(dictSize)) goto _output_error;
        /* Set all the repcodes to non-default */
        {
            BYTE* dictPtr = (BYTE*)dictBuffer;
            BYTE* dictLimit = dictPtr + dictSize - 12;
            /* Find the repcodes */
            while (dictPtr < dictLimit &&
                   (MEM_readLE32(dictPtr) != 1 || MEM_readLE32(dictPtr + 4) != 4 ||
                    MEM_readLE32(dictPtr + 8) != 8)) {
                ++dictPtr;
            }
            if (dictPtr >= dictLimit) goto _output_error;
            MEM_writeLE32(dictPtr + 0, 10);
            MEM_writeLE32(dictPtr + 4, 10);
            MEM_writeLE32(dictPtr + 8, 10);
            /* Set the last 8 bytes to 'x' */
            memset((BYTE*)dictBuffer + dictSize - 8, 'x', 8);
        }
        /* The optimal parser checks all the repcodes.
         * Make sure at least one is a match >= targetLength so that it is
         * immediately chosen. This will make sure that the compressor and
         * decompressor agree on at least one of the repcodes.
         */
        {   size_t dSize;
            BYTE data[1024];
            ZSTD_DCtx* const dctx = ZSTD_createDCtx();
            ZSTD_compressionParameters const cParams = ZSTD_getCParams(19, CNBuffSize, dictSize);
            ZSTD_CDict* const cdict = ZSTD_createCDict_advanced(dictBuffer, dictSize,
                                            ZSTD_dlm_byRef, ZSTD_dct_auto,
                                            cParams, ZSTD_defaultCMem);
            assert(dctx != NULL); assert(cdict != NULL);
            memset(data, 'x', sizeof(data));
            cSize = ZSTD_compress_usingCDict(cctx, compressedBuffer, compressedBufferSize,
                                             data, sizeof(data), cdict);
            ZSTD_freeCDict(cdict);
            if (ZSTD_isError(cSize)) { DISPLAYLEVEL(5, "Compression error %s : ", ZSTD_getErrorName(cSize)); goto _output_error; }
            dSize = ZSTD_decompress_usingDict(dctx, decodedBuffer, sizeof(data), compressedBuffer, cSize, dictBuffer, dictSize);
            if (ZSTD_isError(dSize)) { DISPLAYLEVEL(5, "Decompression error %s : ", ZSTD_getErrorName(dSize)); goto _output_error; }
            if (memcmp(data, decodedBuffer, sizeof(data))) { DISPLAYLEVEL(5, "Data corruption : "); goto _output_error; }
            ZSTD_freeDCtx(dctx);
        }
        DISPLAYLEVEL(3, "OK \n");

        ZSTD_freeCCtx(cctx);
        free(dictBuffer);
        free(samplesSizes);
    }

    /* COVER dictionary builder tests */
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        size_t dictSize = 16 KB;
        size_t optDictSize = dictSize;
        void* dictBuffer = malloc(dictSize);
        size_t const totalSampleSize = 1 MB;
        size_t const sampleUnitSize = 8 KB;
        U32 const nbSamples = (U32)(totalSampleSize / sampleUnitSize);
        size_t* const samplesSizes = (size_t*) malloc(nbSamples * sizeof(size_t));
        ZDICT_cover_params_t params;
        U32 dictID;

        if (dictBuffer==NULL || samplesSizes==NULL) {
            free(dictBuffer);
            free(samplesSizes);
            goto _output_error;
        }

        DISPLAYLEVEL(3, "test%3i : ZDICT_trainFromBuffer_cover : ", testNb++);
        { U32 u; for (u=0; u<nbSamples; u++) samplesSizes[u] = sampleUnitSize; }
        memset(&params, 0, sizeof(params));
        params.d = 1 + (FUZ_rand(&seed) % 16);
        params.k = params.d + (FUZ_rand(&seed) % 256);
        dictSize = ZDICT_trainFromBuffer_cover(dictBuffer, dictSize,
                                               CNBuffer, samplesSizes, nbSamples,
                                               params);
        if (ZDICT_isError(dictSize)) goto _output_error;
        DISPLAYLEVEL(3, "OK, created dictionary of size %u \n", (unsigned)dictSize);

        DISPLAYLEVEL(3, "test%3i : check dictID : ", testNb++);
        dictID = ZDICT_getDictID(dictBuffer, dictSize);
        if (dictID==0) goto _output_error;
        DISPLAYLEVEL(3, "OK : %u \n", (unsigned)dictID);

        DISPLAYLEVEL(3, "test%3i : ZDICT_optimizeTrainFromBuffer_cover : ", testNb++);
        memset(&params, 0, sizeof(params));
        params.steps = 4;
        optDictSize = ZDICT_optimizeTrainFromBuffer_cover(dictBuffer, optDictSize,
                                                          CNBuffer, samplesSizes,
                                                          nbSamples / 4, &params);
        if (ZDICT_isError(optDictSize)) goto _output_error;
        DISPLAYLEVEL(3, "OK, created dictionary of size %u \n", (unsigned)optDictSize);

        DISPLAYLEVEL(3, "test%3i : check dictID : ", testNb++);
        dictID = ZDICT_getDictID(dictBuffer, optDictSize);
        if (dictID==0) goto _output_error;
        DISPLAYLEVEL(3, "OK : %u \n", (unsigned)dictID);

        ZSTD_freeCCtx(cctx);
        free(dictBuffer);
        free(samplesSizes);
    }

    /* Decompression defense tests */
    DISPLAYLEVEL(3, "test%3i : Check input length for magic number : ", testNb++);
    { size_t const r = ZSTD_decompress(decodedBuffer, CNBuffSize, CNBuffer, 3);   /* too small input */
      if (!ZSTD_isError(r)) goto _output_error;
      if (ZSTD_getErrorCode(r) != ZSTD_error_srcSize_wrong) goto _output_error; }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : Check magic Number : ", testNb++);
    ((char*)(CNBuffer))[0] = 1;
    { size_t const r = ZSTD_decompress(decodedBuffer, CNBuffSize, CNBuffer, 4);
      if (!ZSTD_isError(r)) goto _output_error; }
    DISPLAYLEVEL(3, "OK \n");

    /* content size verification test */
    DISPLAYLEVEL(3, "test%3i : Content size verification : ", testNb++);
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        size_t const srcSize = 5000;
        size_t const wrongSrcSize = (srcSize + 1000);
        ZSTD_parameters params = ZSTD_getParams(1, wrongSrcSize, 0);
        params.fParams.contentSizeFlag = 1;
        CHECK( ZSTD_compressBegin_advanced(cctx, NULL, 0, params, wrongSrcSize) );
        {   size_t const result = ZSTD_compressEnd(cctx, decodedBuffer, CNBuffSize, CNBuffer, srcSize);
            if (!ZSTD_isError(result)) goto _output_error;
            if (ZSTD_getErrorCode(result) != ZSTD_error_srcSize_wrong) goto _output_error;
            DISPLAYLEVEL(3, "OK : %s \n", ZSTD_getErrorName(result));
        }
        ZSTD_freeCCtx(cctx);
    }

    /* negative compression level test : ensure simple API and advanced API produce same result */
    DISPLAYLEVEL(3, "test%3i : negative compression level : ", testNb++);
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        size_t const srcSize = CNBuffSize / 5;
        int const compressionLevel = -1;

        assert(cctx != NULL);
        {   ZSTD_parameters const params = ZSTD_getParams(compressionLevel, srcSize, 0);
            size_t const cSize_1pass = ZSTD_compress_advanced(cctx,
                                        compressedBuffer, compressedBufferSize,
                                        CNBuffer, srcSize,
                                        NULL, 0,
                                        params);
            if (ZSTD_isError(cSize_1pass)) goto _output_error;

            CHECK( ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, compressionLevel) );
            {   size_t const compressionResult = ZSTD_compress2(cctx,
                                    compressedBuffer, compressedBufferSize,
                                    CNBuffer, srcSize);
                DISPLAYLEVEL(5, "simple=%zu vs %zu=advanced : ", cSize_1pass, compressionResult);
                if (ZSTD_isError(compressionResult)) goto _output_error;
                if (compressionResult != cSize_1pass) goto _output_error;
        }   }
        ZSTD_freeCCtx(cctx);
    }
    DISPLAYLEVEL(3, "OK \n");

    /* parameters order test */
    {   size_t const inputSize = CNBuffSize / 2;
        U64 xxh64;

        {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
            DISPLAYLEVEL(3, "test%3i : parameters in order : ", testNb++);
            assert(cctx != NULL);
            CHECK( ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, 2) );
            CHECK( ZSTD_CCtx_setParameter(cctx, ZSTD_c_enableLongDistanceMatching, 1) );
            CHECK( ZSTD_CCtx_setParameter(cctx, ZSTD_c_windowLog, 18) );
            {   size_t const compressedSize = ZSTD_compress2(cctx,
                                compressedBuffer, ZSTD_compressBound(inputSize),
                                CNBuffer, inputSize);
                CHECK(compressedSize);
                cSize = compressedSize;
                xxh64 = XXH64(compressedBuffer, compressedSize, 0);
            }
            DISPLAYLEVEL(3, "OK (compress : %u -> %u bytes)\n", (unsigned)inputSize, (unsigned)cSize);
            ZSTD_freeCCtx(cctx);
        }

        {   ZSTD_CCtx* cctx = ZSTD_createCCtx();
            DISPLAYLEVEL(3, "test%3i : parameters disordered : ", testNb++);
            CHECK( ZSTD_CCtx_setParameter(cctx, ZSTD_c_windowLog, 18) );
            CHECK( ZSTD_CCtx_setParameter(cctx, ZSTD_c_enableLongDistanceMatching, 1) );
            CHECK( ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, 2) );
            {   size_t const result = ZSTD_compress2(cctx,
                                compressedBuffer, ZSTD_compressBound(inputSize),
                                CNBuffer, inputSize);
                CHECK(result);
                if (result != cSize) goto _output_error;   /* must result in same compressed result, hence same size */
                if (XXH64(compressedBuffer, result, 0) != xxh64) goto _output_error;  /* must result in exactly same content, hence same hash */
                DISPLAYLEVEL(3, "OK (compress : %u -> %u bytes)\n", (unsigned)inputSize, (unsigned)result);
            }
            ZSTD_freeCCtx(cctx);
        }
    }

    /* advanced parameters for decompression */
    {   ZSTD_DCtx* const dctx = ZSTD_createDCtx();
        assert(dctx != NULL);

        DISPLAYLEVEL(3, "test%3i : get dParameter bounds ", testNb++);
        {   ZSTD_bounds const bounds = ZSTD_dParam_getBounds(ZSTD_d_windowLogMax);
            CHECK(bounds.error);
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : wrong dParameter : ", testNb++);
        {   size_t const sr = ZSTD_DCtx_setParameter(dctx, (ZSTD_dParameter)999999, 0);
            if (!ZSTD_isError(sr)) goto _output_error;
        }
        {   ZSTD_bounds const bounds = ZSTD_dParam_getBounds((ZSTD_dParameter)999998);
            if (!ZSTD_isError(bounds.error)) goto _output_error;
        }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : out of bound dParameter : ", testNb++);
        {   size_t const sr = ZSTD_DCtx_setParameter(dctx, ZSTD_d_windowLogMax, 9999);
            if (!ZSTD_isError(sr)) goto _output_error;
        }
        {   size_t const sr = ZSTD_DCtx_setParameter(dctx, ZSTD_d_format, (ZSTD_format_e)888);
            if (!ZSTD_isError(sr)) goto _output_error;
        }
        DISPLAYLEVEL(3, "OK \n");

        ZSTD_freeDCtx(dctx);
    }


    /* custom formats tests */
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        ZSTD_DCtx* const dctx = ZSTD_createDCtx();
        size_t const inputSize = CNBuffSize / 2;   /* won't cause pb with small dict size */
        assert(dctx != NULL); assert(cctx != NULL);

        /* basic block compression */
        DISPLAYLEVEL(3, "test%3i : magic-less format test : ", testNb++);
        CHECK( ZSTD_CCtx_setParameter(cctx, ZSTD_c_format, ZSTD_f_zstd1_magicless) );
        {   ZSTD_inBuffer in = { CNBuffer, inputSize, 0 };
            ZSTD_outBuffer out = { compressedBuffer, ZSTD_compressBound(inputSize), 0 };
            size_t const result = ZSTD_compressStream2(cctx, &out, &in, ZSTD_e_end);
            if (result != 0) goto _output_error;
            if (in.pos != in.size) goto _output_error;
            cSize = out.pos;
        }
        DISPLAYLEVEL(3, "OK (compress : %u -> %u bytes)\n", (unsigned)inputSize, (unsigned)cSize);

        DISPLAYLEVEL(3, "test%3i : decompress normally (should fail) : ", testNb++);
        {   size_t const decodeResult = ZSTD_decompressDCtx(dctx, decodedBuffer, CNBuffSize, compressedBuffer, cSize);
            if (ZSTD_getErrorCode(decodeResult) != ZSTD_error_prefix_unknown) goto _output_error;
            DISPLAYLEVEL(3, "OK : %s \n", ZSTD_getErrorName(decodeResult));
        }

        DISPLAYLEVEL(3, "test%3i : decompress of magic-less frame : ", testNb++);
        ZSTD_DCtx_reset(dctx, ZSTD_reset_session_and_parameters);
        CHECK( ZSTD_DCtx_setParameter(dctx, ZSTD_d_format, ZSTD_f_zstd1_magicless) );
        {   ZSTD_frameHeader zfh;
            size_t const zfhrt = ZSTD_getFrameHeader_advanced(&zfh, compressedBuffer, cSize, ZSTD_f_zstd1_magicless);
            if (zfhrt != 0) goto _output_error;
        }
        /* one shot */
        {   size_t const result = ZSTD_decompressDCtx(dctx, decodedBuffer, CNBuffSize, compressedBuffer, cSize);
            if (result != inputSize) goto _output_error;
            DISPLAYLEVEL(3, "one-shot OK, ");
        }
        /* streaming */
        {   ZSTD_inBuffer in = { compressedBuffer, cSize, 0 };
            ZSTD_outBuffer out = { decodedBuffer, CNBuffSize, 0 };
            size_t const result = ZSTD_decompressStream(dctx, &out, &in);
            if (result != 0) goto _output_error;
            if (in.pos != in.size) goto _output_error;
            if (out.pos != inputSize) goto _output_error;
            DISPLAYLEVEL(3, "streaming OK : regenerated %u bytes \n", (unsigned)out.pos);
        }

        ZSTD_freeCCtx(cctx);
        ZSTD_freeDCtx(dctx);
    }

    /* block API tests */
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        ZSTD_DCtx* const dctx = ZSTD_createDCtx();
        static const size_t dictSize = 65 KB;
        static const size_t blockSize = 100 KB;   /* won't cause pb with small dict size */
        size_t cSize2;
        assert(cctx != NULL); assert(dctx != NULL);

        /* basic block compression */
        DISPLAYLEVEL(3, "test%3i : Block compression test : ", testNb++);
        CHECK( ZSTD_compressBegin(cctx, 5) );
        CHECK( ZSTD_getBlockSize(cctx) >= blockSize);
        cSize = ZSTD_compressBlock(cctx, compressedBuffer, ZSTD_compressBound(blockSize), CNBuffer, blockSize);
        if (ZSTD_isError(cSize)) goto _output_error;
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : Block decompression test : ", testNb++);
        CHECK( ZSTD_decompressBegin(dctx) );
        { CHECK_V(r, ZSTD_decompressBlock(dctx, decodedBuffer, CNBuffSize, compressedBuffer, cSize) );
          if (r != blockSize) goto _output_error; }
        DISPLAYLEVEL(3, "OK \n");

        /* very long stream of block compression */
        DISPLAYLEVEL(3, "test%3i : Huge block streaming compression test : ", testNb++);
        CHECK( ZSTD_compressBegin(cctx, -199) );  /* we just want to quickly overflow internal U32 index */
        CHECK( ZSTD_getBlockSize(cctx) >= blockSize);
        {   U64 const toCompress = 5000000000ULL;   /* > 4 GB */
            U64 compressed = 0;
            while (compressed < toCompress) {
                size_t const blockCSize = ZSTD_compressBlock(cctx, compressedBuffer, ZSTD_compressBound(blockSize), CNBuffer, blockSize);
                assert(blockCSize != 0);
                if (ZSTD_isError(blockCSize)) goto _output_error;
                compressed += blockCSize;
            }
        }
        DISPLAYLEVEL(3, "OK \n");

        /* dictionary block compression */
        DISPLAYLEVEL(3, "test%3i : Dictionary Block compression test : ", testNb++);
        CHECK( ZSTD_compressBegin_usingDict(cctx, CNBuffer, dictSize, 5) );
        cSize = ZSTD_compressBlock(cctx, compressedBuffer, ZSTD_compressBound(blockSize), (char*)CNBuffer+dictSize, blockSize);
        if (ZSTD_isError(cSize)) goto _output_error;
        cSize2 = ZSTD_compressBlock(cctx, (char*)compressedBuffer+cSize, ZSTD_compressBound(blockSize), (char*)CNBuffer+dictSize+blockSize, blockSize);
        if (ZSTD_isError(cSize2)) goto _output_error;
        memcpy((char*)compressedBuffer+cSize, (char*)CNBuffer+dictSize+blockSize, blockSize);   /* fake non-compressed block */
        cSize2 = ZSTD_compressBlock(cctx, (char*)compressedBuffer+cSize+blockSize, ZSTD_compressBound(blockSize),
                                          (char*)CNBuffer+dictSize+2*blockSize, blockSize);
        if (ZSTD_isError(cSize2)) goto _output_error;
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : Dictionary Block decompression test : ", testNb++);
        CHECK( ZSTD_decompressBegin_usingDict(dctx, CNBuffer, dictSize) );
        { CHECK_V( r, ZSTD_decompressBlock(dctx, decodedBuffer, CNBuffSize, compressedBuffer, cSize) );
          if (r != blockSize) goto _output_error; }
        ZSTD_insertBlock(dctx, (char*)decodedBuffer+blockSize, blockSize);   /* insert non-compressed block into dctx history */
        { CHECK_V( r, ZSTD_decompressBlock(dctx, (char*)decodedBuffer+2*blockSize, CNBuffSize, (char*)compressedBuffer+cSize+blockSize, cSize2) );
          if (r != blockSize) goto _output_error; }
        DISPLAYLEVEL(3, "OK \n");

        DISPLAYLEVEL(3, "test%3i : Block compression with CDict : ", testNb++);
        {   ZSTD_CDict* const cdict = ZSTD_createCDict(CNBuffer, dictSize, 3);
            if (cdict==NULL) goto _output_error;
            CHECK( ZSTD_compressBegin_usingCDict(cctx, cdict) );
            CHECK( ZSTD_compressBlock(cctx, compressedBuffer, ZSTD_compressBound(blockSize), (char*)CNBuffer+dictSize, blockSize) );
            ZSTD_freeCDict(cdict);
        }
        DISPLAYLEVEL(3, "OK \n");

        ZSTD_freeCCtx(cctx);
        ZSTD_freeDCtx(dctx);
    }

    /* long rle test */
    {   size_t sampleSize = 0;
        DISPLAYLEVEL(3, "test%3i : Long RLE test : ", testNb++);
        RDG_genBuffer(CNBuffer, sampleSize, compressibility, 0., seed+1);
        memset((char*)CNBuffer+sampleSize, 'B', 256 KB - 1);
        sampleSize += 256 KB - 1;
        RDG_genBuffer((char*)CNBuffer+sampleSize, 96 KB, compressibility, 0., seed+2);
        sampleSize += 96 KB;
        cSize = ZSTD_compress(compressedBuffer, ZSTD_compressBound(sampleSize), CNBuffer, sampleSize, 1);
        if (ZSTD_isError(cSize)) goto _output_error;
        { CHECK_V(regenSize, ZSTD_decompress(decodedBuffer, sampleSize, compressedBuffer, cSize));
          if (regenSize!=sampleSize) goto _output_error; }
        DISPLAYLEVEL(3, "OK \n");
    }

    /* All zeroes test (test bug #137) */
    #define ZEROESLENGTH 100
    DISPLAYLEVEL(3, "test%3i : compress %u zeroes : ", testNb++, ZEROESLENGTH);
    memset(CNBuffer, 0, ZEROESLENGTH);
    { CHECK_V(r, ZSTD_compress(compressedBuffer, ZSTD_compressBound(ZEROESLENGTH), CNBuffer, ZEROESLENGTH, 1) );
      cSize = r; }
    DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/ZEROESLENGTH*100);

    DISPLAYLEVEL(3, "test%3i : decompress %u zeroes : ", testNb++, ZEROESLENGTH);
    { CHECK_V(r, ZSTD_decompress(decodedBuffer, ZEROESLENGTH, compressedBuffer, cSize) );
      if (r != ZEROESLENGTH) goto _output_error; }
    DISPLAYLEVEL(3, "OK \n");

    /* nbSeq limit test */
    #define _3BYTESTESTLENGTH 131000
    #define NB3BYTESSEQLOG   9
    #define NB3BYTESSEQ     (1 << NB3BYTESSEQLOG)
    #define NB3BYTESSEQMASK (NB3BYTESSEQ-1)
    /* creates a buffer full of 3-bytes sequences */
    {   BYTE _3BytesSeqs[NB3BYTESSEQ][3];
        U32 rSeed = 1;

        /* create batch of 3-bytes sequences */
        {   int i;
            for (i=0; i < NB3BYTESSEQ; i++) {
                _3BytesSeqs[i][0] = (BYTE)(FUZ_rand(&rSeed) & 255);
                _3BytesSeqs[i][1] = (BYTE)(FUZ_rand(&rSeed) & 255);
                _3BytesSeqs[i][2] = (BYTE)(FUZ_rand(&rSeed) & 255);
        }   }

        /* randomly fills CNBuffer with prepared 3-bytes sequences */
        {   int i;
            for (i=0; i < _3BYTESTESTLENGTH; i += 3) {   /* note : CNBuffer size > _3BYTESTESTLENGTH+3 */
                U32 const id = FUZ_rand(&rSeed) & NB3BYTESSEQMASK;
                ((BYTE*)CNBuffer)[i+0] = _3BytesSeqs[id][0];
                ((BYTE*)CNBuffer)[i+1] = _3BytesSeqs[id][1];
                ((BYTE*)CNBuffer)[i+2] = _3BytesSeqs[id][2];
    }   }   }
    DISPLAYLEVEL(3, "test%3i : growing nbSeq : ", testNb++);
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        size_t const maxNbSeq = _3BYTESTESTLENGTH / 3;
        size_t const bound = ZSTD_compressBound(_3BYTESTESTLENGTH);
        size_t nbSeq = 1;
        while (nbSeq <= maxNbSeq) {
          CHECK(ZSTD_compressCCtx(cctx, compressedBuffer, bound, CNBuffer, nbSeq * 3, 19));
          /* Check every sequence for the first 100, then skip more rapidly. */
          if (nbSeq < 100) {
            ++nbSeq;
          } else {
            nbSeq += (nbSeq >> 2);
          }
        }
        ZSTD_freeCCtx(cctx);
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : compress lots 3-bytes sequences : ", testNb++);
    { CHECK_V(r, ZSTD_compress(compressedBuffer, ZSTD_compressBound(_3BYTESTESTLENGTH),
                                 CNBuffer, _3BYTESTESTLENGTH, 19) );
      cSize = r; }
    DISPLAYLEVEL(3, "OK (%u bytes : %.2f%%)\n", (unsigned)cSize, (double)cSize/_3BYTESTESTLENGTH*100);

    DISPLAYLEVEL(3, "test%3i : decompress lots 3-bytes sequence : ", testNb++);
    { CHECK_V(r, ZSTD_decompress(decodedBuffer, _3BYTESTESTLENGTH, compressedBuffer, cSize) );
      if (r != _3BYTESTESTLENGTH) goto _output_error; }
    DISPLAYLEVEL(3, "OK \n");


    DISPLAYLEVEL(3, "test%3i : growing literals buffer : ", testNb++);
    RDG_genBuffer(CNBuffer, CNBuffSize, 0.0, 0.1, seed);
    {   ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        size_t const bound = ZSTD_compressBound(CNBuffSize);
        size_t size = 1;
        while (size <= CNBuffSize) {
          CHECK(ZSTD_compressCCtx(cctx, compressedBuffer, bound, CNBuffer, size, 3));
          /* Check every size for the first 100, then skip more rapidly. */
          if (size < 100) {
            ++size;
          } else {
            size += (size >> 2);
          }
        }
        ZSTD_freeCCtx(cctx);
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : incompressible data and ill suited dictionary : ", testNb++);
    {   /* Train a dictionary on low characters */
        size_t dictSize = 16 KB;
        void* const dictBuffer = malloc(dictSize);
        size_t const totalSampleSize = 1 MB;
        size_t const sampleUnitSize = 8 KB;
        U32 const nbSamples = (U32)(totalSampleSize / sampleUnitSize);
        size_t* const samplesSizes = (size_t*) malloc(nbSamples * sizeof(size_t));
        if (!dictBuffer || !samplesSizes) goto _output_error;
        { U32 u; for (u=0; u<nbSamples; u++) samplesSizes[u] = sampleUnitSize; }
        dictSize = ZDICT_trainFromBuffer(dictBuffer, dictSize, CNBuffer, samplesSizes, nbSamples);
        if (ZDICT_isError(dictSize)) goto _output_error;
        /* Reverse the characters to make the dictionary ill suited */
        {   U32 u;
            for (u = 0; u < CNBuffSize; ++u) {
              ((BYTE*)CNBuffer)[u] = 255 - ((BYTE*)CNBuffer)[u];
            }
        }
        {   /* Compress the data */
            size_t const inputSize = 500;
            size_t const outputSize = ZSTD_compressBound(inputSize);
            void* const outputBuffer = malloc(outputSize);
            ZSTD_CCtx* const cctx = ZSTD_createCCtx();
            if (!outputBuffer || !cctx) goto _output_error;
            CHECK(ZSTD_compress_usingDict(cctx, outputBuffer, outputSize, CNBuffer, inputSize, dictBuffer, dictSize, 1));
            free(outputBuffer);
            ZSTD_freeCCtx(cctx);
        }

        free(dictBuffer);
        free(samplesSizes);
    }
    DISPLAYLEVEL(3, "OK \n");


    /* findFrameCompressedSize on skippable frames */
    DISPLAYLEVEL(3, "test%3i : frame compressed size of skippable frame : ", testNb++);
    {   const char* frame = "\x50\x2a\x4d\x18\x05\x0\x0\0abcde";
        size_t const frameSrcSize = 13;
        if (ZSTD_findFrameCompressedSize(frame, frameSrcSize) != frameSrcSize) goto _output_error; }
    DISPLAYLEVEL(3, "OK \n");

    /* error string tests */
    DISPLAYLEVEL(3, "test%3i : testing ZSTD error code strings : ", testNb++);
    if (strcmp("No error detected", ZSTD_getErrorName((ZSTD_ErrorCode)(0-ZSTD_error_no_error))) != 0) goto _output_error;
    if (strcmp("No error detected", ZSTD_getErrorString(ZSTD_error_no_error)) != 0) goto _output_error;
    if (strcmp("Unspecified error code", ZSTD_getErrorString((ZSTD_ErrorCode)(0-ZSTD_error_GENERIC))) != 0) goto _output_error;
    if (strcmp("Error (generic)", ZSTD_getErrorName((size_t)0-ZSTD_error_GENERIC)) != 0) goto _output_error;
    if (strcmp("Error (generic)", ZSTD_getErrorString(ZSTD_error_GENERIC)) != 0) goto _output_error;
    if (strcmp("No error detected", ZSTD_getErrorName(ZSTD_error_GENERIC)) != 0) goto _output_error;
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : testing ZSTD dictionary sizes : ", testNb++);
    RDG_genBuffer(CNBuffer, CNBuffSize, compressibility, 0., seed);
    {
        size_t const size = MIN(128 KB, CNBuffSize);
        ZSTD_CCtx* const cctx = ZSTD_createCCtx();
        ZSTD_CDict* const lgCDict = ZSTD_createCDict(CNBuffer, size, 1);
        ZSTD_CDict* const smCDict = ZSTD_createCDict(CNBuffer, 1 KB, 1);
        ZSTD_frameHeader lgHeader;
        ZSTD_frameHeader smHeader;

        CHECK_Z(ZSTD_compress_usingCDict(cctx, compressedBuffer, compressedBufferSize, CNBuffer, size, lgCDict));
        CHECK_Z(ZSTD_getFrameHeader(&lgHeader, compressedBuffer, compressedBufferSize));
        CHECK_Z(ZSTD_compress_usingCDict(cctx, compressedBuffer, compressedBufferSize, CNBuffer, size, smCDict));
        CHECK_Z(ZSTD_getFrameHeader(&smHeader, compressedBuffer, compressedBufferSize));

        if (lgHeader.windowSize != smHeader.windowSize) goto _output_error;

        ZSTD_freeCDict(smCDict);
        ZSTD_freeCDict(lgCDict);
        ZSTD_freeCCtx(cctx);
    }
    DISPLAYLEVEL(3, "OK \n");

    DISPLAYLEVEL(3, "test%3i : testing FSE_normalizeCount() PR#1255: ", testNb++);
    {
        short norm[32];
        unsigned count[32];
        unsigned const tableLog = 5;
        size_t const nbSeq = 32;
        unsigned const maxSymbolValue = 31;
        size_t i;

        for (i = 0; i < 32; ++i)
            count[i] = 1;
        /* Calling FSE_normalizeCount() on a uniform distribution should not
         * cause a division by zero.
         */
        FSE_normalizeCount(norm, tableLog, count, nbSeq, maxSymbolValue);
    }
    DISPLAYLEVEL(3, "OK \n");

_end:
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


static ZSTD_parameters FUZ_makeParams(ZSTD_compressionParameters cParams, ZSTD_frameParameters fParams)
{
    ZSTD_parameters params;
    params.cParams = cParams;
    params.fParams = fParams;
    return params;
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

#undef CHECK
#define CHECK(cond, ...) {                                    \
    if (cond) {                                               \
        DISPLAY("Error => ");                                 \
        DISPLAY(__VA_ARGS__);                                 \
        DISPLAY(" (seed %u, test nb %u)  \n", (unsigned)seed, testNb);  \
        goto _output_error;                                   \
}   }

#undef CHECK_Z
#define CHECK_Z(f) {                                          \
    size_t const err = f;                                     \
    if (ZSTD_isError(err)) {                                  \
        DISPLAY("Error => %s : %s ",                          \
                #f, ZSTD_getErrorName(err));                  \
        DISPLAY(" (seed %u, test nb %u)  \n", (unsigned)seed, testNb);  \
        goto _output_error;                                   \
}   }


static int fuzzerTests(U32 seed, unsigned nbTests, unsigned startTest, U32 const maxDurationS, double compressibility, int bigTests)
{
    static const U32 maxSrcLog = 23;
    static const U32 maxSampleLog = 22;
    size_t const srcBufferSize = (size_t)1<<maxSrcLog;
    size_t const dstBufferSize = (size_t)1<<maxSampleLog;
    size_t const cBufferSize   = ZSTD_compressBound(dstBufferSize);
    BYTE* cNoiseBuffer[5];
    BYTE* const cBuffer = (BYTE*) malloc (cBufferSize);
    BYTE* const dstBuffer = (BYTE*) malloc (dstBufferSize);
    BYTE* const mirrorBuffer = (BYTE*) malloc (dstBufferSize);
    ZSTD_CCtx* const refCtx = ZSTD_createCCtx();
    ZSTD_CCtx* const ctx = ZSTD_createCCtx();
    ZSTD_DCtx* const dctx = ZSTD_createDCtx();
    U32 result = 0;
    unsigned testNb = 0;
    U32 coreSeed = seed;
    UTIL_time_t const startClock = UTIL_getTime();
    U64 const maxClockSpan = maxDurationS * SEC_TO_MICRO;
    int const cLevelLimiter = bigTests ? 3 : 2;

    /* allocation */
    cNoiseBuffer[0] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[1] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[2] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[3] = (BYTE*)malloc (srcBufferSize);
    cNoiseBuffer[4] = (BYTE*)malloc (srcBufferSize);
    CHECK (!cNoiseBuffer[0] || !cNoiseBuffer[1] || !cNoiseBuffer[2] || !cNoiseBuffer[3] || !cNoiseBuffer[4]
           || !dstBuffer || !mirrorBuffer || !cBuffer || !refCtx || !ctx || !dctx,
           "Not enough memory, fuzzer tests cancelled");

    /* Create initial samples */
    RDG_genBuffer(cNoiseBuffer[0], srcBufferSize, 0.00, 0., coreSeed);    /* pure noise */
    RDG_genBuffer(cNoiseBuffer[1], srcBufferSize, 0.05, 0., coreSeed);    /* barely compressible */
    RDG_genBuffer(cNoiseBuffer[2], srcBufferSize, compressibility, 0., coreSeed);
    RDG_genBuffer(cNoiseBuffer[3], srcBufferSize, 0.95, 0., coreSeed);    /* highly compressible */
    RDG_genBuffer(cNoiseBuffer[4], srcBufferSize, 1.00, 0., coreSeed);    /* sparse content */

    /* catch up testNb */
    for (testNb=1; testNb < startTest; testNb++) FUZ_rand(&coreSeed);

    /* main test loop */
    for ( ; (testNb <= nbTests) || (UTIL_clockSpanMicro(startClock) < maxClockSpan); testNb++ ) {
        BYTE* srcBuffer;   /* jumping pointer */
        U32 lseed;
        size_t sampleSize, maxTestSize, totalTestSize;
        size_t cSize, totalCSize, totalGenSize;
        U64 crcOrig;
        BYTE* sampleBuffer;
        const BYTE* dict;
        size_t dictSize;

        /* notification */
        if (nbTests >= testNb) { DISPLAYUPDATE(2, "\r%6u/%6u    ", testNb, nbTests); }
        else { DISPLAYUPDATE(2, "\r%6u          ", testNb); }

        FUZ_rand(&coreSeed);
        { U32 const prime1 = 2654435761U; lseed = coreSeed ^ prime1; }

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

        /* select src segment */
        sampleSize = FUZ_randomLength(&lseed, maxSampleLog);

        /* create sample buffer (to catch read error with valgrind & sanitizers)  */
        sampleBuffer = (BYTE*)malloc(sampleSize);
        CHECK(sampleBuffer==NULL, "not enough memory for sample buffer");
        { size_t const sampleStart = FUZ_rand(&lseed) % (srcBufferSize - sampleSize);
          memcpy(sampleBuffer, srcBuffer + sampleStart, sampleSize); }
        crcOrig = XXH64(sampleBuffer, sampleSize, 0);

        /* compression tests */
        {   int const cLevelPositive =
                    ( FUZ_rand(&lseed) %
                     (ZSTD_maxCLevel() - (FUZ_highbit32((U32)sampleSize) / cLevelLimiter)) )
                    + 1;
            int const cLevel = ((FUZ_rand(&lseed) & 15) == 3) ?
                             - (int)((FUZ_rand(&lseed) & 7) + 1) :   /* test negative cLevel */
                             cLevelPositive;
            DISPLAYLEVEL(5, "fuzzer t%u: Simple compression test (level %i) \n", testNb, cLevel);
            cSize = ZSTD_compressCCtx(ctx, cBuffer, cBufferSize, sampleBuffer, sampleSize, cLevel);
            CHECK(ZSTD_isError(cSize), "ZSTD_compressCCtx failed : %s", ZSTD_getErrorName(cSize));

            /* compression failure test : too small dest buffer */
            assert(cSize > 3);
            {   const size_t missing = (FUZ_rand(&lseed) % (cSize-2)) + 1;
                const size_t tooSmallSize = cSize - missing;
                const unsigned endMark = 0x4DC2B1A9;
                memcpy(dstBuffer+tooSmallSize, &endMark, sizeof(endMark));
                DISPLAYLEVEL(5, "fuzzer t%u: compress into too small buffer of size %u (missing %u bytes) \n",
                            testNb, (unsigned)tooSmallSize, (unsigned)missing);
                { size_t const errorCode = ZSTD_compressCCtx(ctx, dstBuffer, tooSmallSize, sampleBuffer, sampleSize, cLevel);
                  CHECK(!ZSTD_isError(errorCode), "ZSTD_compressCCtx should have failed ! (buffer too small : %u < %u)", (unsigned)tooSmallSize, (unsigned)cSize); }
                { unsigned endCheck; memcpy(&endCheck, dstBuffer+tooSmallSize, sizeof(endCheck));
                  CHECK(endCheck != endMark, "ZSTD_compressCCtx : dst buffer overflow  (check.%08X != %08X.mark)", endCheck, endMark); }
        }   }

        /* frame header decompression test */
        {   ZSTD_frameHeader zfh;
            CHECK_Z( ZSTD_getFrameHeader(&zfh, cBuffer, cSize) );
            CHECK(zfh.frameContentSize != sampleSize, "Frame content size incorrect");
        }

        /* Decompressed size test */
        {   unsigned long long const rSize = ZSTD_findDecompressedSize(cBuffer, cSize);
            CHECK(rSize != sampleSize, "decompressed size incorrect");
        }

        /* successful decompression test */
        DISPLAYLEVEL(5, "fuzzer t%u: simple decompression test \n", testNb);
        {   size_t const margin = (FUZ_rand(&lseed) & 1) ? 0 : (FUZ_rand(&lseed) & 31) + 1;
            size_t const dSize = ZSTD_decompress(dstBuffer, sampleSize + margin, cBuffer, cSize);
            CHECK(dSize != sampleSize, "ZSTD_decompress failed (%s) (srcSize : %u ; cSize : %u)", ZSTD_getErrorName(dSize), (unsigned)sampleSize, (unsigned)cSize);
            {   U64 const crcDest = XXH64(dstBuffer, sampleSize, 0);
                CHECK(crcOrig != crcDest, "decompression result corrupted (pos %u / %u)", (unsigned)findDiff(sampleBuffer, dstBuffer, sampleSize), (unsigned)sampleSize);
        }   }

        free(sampleBuffer);   /* no longer useful after this point */

        /* truncated src decompression test */
        DISPLAYLEVEL(5, "fuzzer t%u: decompression of truncated source \n", testNb);
        {   size_t const missing = (FUZ_rand(&lseed) % (cSize-2)) + 1;   /* no problem, as cSize > 4 (frameHeaderSizer) */
            size_t const tooSmallSize = cSize - missing;
            void* cBufferTooSmall = malloc(tooSmallSize);   /* valgrind will catch read overflows */
            CHECK(cBufferTooSmall == NULL, "not enough memory !");
            memcpy(cBufferTooSmall, cBuffer, tooSmallSize);
            { size_t const errorCode = ZSTD_decompress(dstBuffer, dstBufferSize, cBufferTooSmall, tooSmallSize);
              CHECK(!ZSTD_isError(errorCode), "ZSTD_decompress should have failed ! (truncated src buffer)"); }
            free(cBufferTooSmall);
        }

        /* too small dst decompression test */
        DISPLAYLEVEL(5, "fuzzer t%u: decompress into too small dst buffer \n", testNb);
        if (sampleSize > 3) {
            size_t const missing = (FUZ_rand(&lseed) % (sampleSize-2)) + 1;   /* no problem, as cSize > 4 (frameHeaderSizer) */
            size_t const tooSmallSize = sampleSize - missing;
            static const BYTE token = 0xA9;
            dstBuffer[tooSmallSize] = token;
            { size_t const errorCode = ZSTD_decompress(dstBuffer, tooSmallSize, cBuffer, cSize);
              CHECK(!ZSTD_isError(errorCode), "ZSTD_decompress should have failed : %u > %u (dst buffer too small)", (unsigned)errorCode, (unsigned)tooSmallSize); }
            CHECK(dstBuffer[tooSmallSize] != token, "ZSTD_decompress : dst buffer overflow");
        }

        /* noisy src decompression test */
        if (cSize > 6) {
            /* insert noise into src */
            {   U32 const maxNbBits = FUZ_highbit32((U32)(cSize-4));
                size_t pos = 4;   /* preserve magic number (too easy to detect) */
                for (;;) {
                    /* keep some original src */
                    {   U32 const nbBits = FUZ_rand(&lseed) % maxNbBits;
                        size_t const mask = (1<<nbBits) - 1;
                        size_t const skipLength = FUZ_rand(&lseed) & mask;
                        pos += skipLength;
                    }
                    if (pos >= cSize) break;
                    /* add noise */
                    {   U32 const nbBitsCodes = FUZ_rand(&lseed) % maxNbBits;
                        U32 const nbBits = nbBitsCodes ? nbBitsCodes-1 : 0;
                        size_t const mask = (1<<nbBits) - 1;
                        size_t const rNoiseLength = (FUZ_rand(&lseed) & mask) + 1;
                        size_t const noiseLength = MIN(rNoiseLength, cSize-pos);
                        size_t const noiseStart = FUZ_rand(&lseed) % (srcBufferSize - noiseLength);
                        memcpy(cBuffer + pos, srcBuffer + noiseStart, noiseLength);
                        pos += noiseLength;
            }   }   }

            /* decompress noisy source */
            DISPLAYLEVEL(5, "fuzzer t%u: decompress noisy source \n", testNb);
            {   U32 const endMark = 0xA9B1C3D6;
                memcpy(dstBuffer+sampleSize, &endMark, 4);
                {   size_t const decompressResult = ZSTD_decompress(dstBuffer, sampleSize, cBuffer, cSize);
                    /* result *may* be an unlikely success, but even then, it must strictly respect dst buffer boundaries */
                    CHECK((!ZSTD_isError(decompressResult)) && (decompressResult>sampleSize),
                          "ZSTD_decompress on noisy src : result is too large : %u > %u (dst buffer)", (unsigned)decompressResult, (unsigned)sampleSize);
                }
                {   U32 endCheck; memcpy(&endCheck, dstBuffer+sampleSize, 4);
                    CHECK(endMark!=endCheck, "ZSTD_decompress on noisy src : dst buffer overflow");
        }   }   }   /* noisy src decompression test */

        /*=====   Bufferless streaming compression test, scattered segments and dictionary   =====*/
        DISPLAYLEVEL(5, "fuzzer t%u: Bufferless streaming compression test \n", testNb);
        {   U32 const testLog = FUZ_rand(&lseed) % maxSrcLog;
            U32 const dictLog = FUZ_rand(&lseed) % maxSrcLog;
            int const cLevel = (FUZ_rand(&lseed) %
                                (ZSTD_maxCLevel() -
                                 (MAX(testLog, dictLog) / cLevelLimiter))) +
                               1;
            maxTestSize = FUZ_rLogLength(&lseed, testLog);
            if (maxTestSize >= dstBufferSize) maxTestSize = dstBufferSize-1;

            dictSize = FUZ_rLogLength(&lseed, dictLog);   /* needed also for decompression */
            dict = srcBuffer + (FUZ_rand(&lseed) % (srcBufferSize - dictSize));

            DISPLAYLEVEL(6, "fuzzer t%u: Compressing up to <=%u bytes at level %i with dictionary size %u \n",
                            testNb, (unsigned)maxTestSize, cLevel, (unsigned)dictSize);

            if (FUZ_rand(&lseed) & 0xF) {
                CHECK_Z ( ZSTD_compressBegin_usingDict(refCtx, dict, dictSize, cLevel) );
            } else {
                ZSTD_compressionParameters const cPar = ZSTD_getCParams(cLevel, ZSTD_CONTENTSIZE_UNKNOWN, dictSize);
                ZSTD_frameParameters const fPar = { FUZ_rand(&lseed)&1 /* contentSizeFlag */,
                                                    !(FUZ_rand(&lseed)&3) /* contentChecksumFlag*/,
                                                    0 /*NodictID*/ };   /* note : since dictionary is fake, dictIDflag has no impact */
                ZSTD_parameters const p = FUZ_makeParams(cPar, fPar);
                CHECK_Z ( ZSTD_compressBegin_advanced(refCtx, dict, dictSize, p, 0) );
            }
            CHECK_Z( ZSTD_copyCCtx(ctx, refCtx, 0) );
        }

        {   U32 const nbChunks = (FUZ_rand(&lseed) & 127) + 2;
            U32 n;
            XXH64_state_t xxhState;
            XXH64_reset(&xxhState, 0);
            for (totalTestSize=0, cSize=0, n=0 ; n<nbChunks ; n++) {
                size_t const segmentSize = FUZ_randomLength(&lseed, maxSampleLog);
                size_t const segmentStart = FUZ_rand(&lseed) % (srcBufferSize - segmentSize);

                if (cBufferSize-cSize < ZSTD_compressBound(segmentSize)) break;   /* avoid invalid dstBufferTooSmall */
                if (totalTestSize+segmentSize > maxTestSize) break;

                {   size_t const compressResult = ZSTD_compressContinue(ctx, cBuffer+cSize, cBufferSize-cSize, srcBuffer+segmentStart, segmentSize);
                    CHECK (ZSTD_isError(compressResult), "multi-segments compression error : %s", ZSTD_getErrorName(compressResult));
                    cSize += compressResult;
                }
                XXH64_update(&xxhState, srcBuffer+segmentStart, segmentSize);
                memcpy(mirrorBuffer + totalTestSize, srcBuffer+segmentStart, segmentSize);
                totalTestSize += segmentSize;
            }

            {   size_t const flushResult = ZSTD_compressEnd(ctx, cBuffer+cSize, cBufferSize-cSize, NULL, 0);
                CHECK (ZSTD_isError(flushResult), "multi-segments epilogue error : %s", ZSTD_getErrorName(flushResult));
                cSize += flushResult;
            }
            crcOrig = XXH64_digest(&xxhState);
        }

        /* streaming decompression test */
        DISPLAYLEVEL(5, "fuzzer t%u: Bufferless streaming decompression test \n", testNb);
        /* ensure memory requirement is good enough (should always be true) */
        {   ZSTD_frameHeader zfh;
            CHECK( ZSTD_getFrameHeader(&zfh, cBuffer, ZSTD_FRAMEHEADERSIZE_MAX),
                  "ZSTD_getFrameHeader(): error retrieving frame information");
            {   size_t const roundBuffSize = ZSTD_decodingBufferSize_min(zfh.windowSize, zfh.frameContentSize);
                CHECK_Z(roundBuffSize);
                CHECK((roundBuffSize > totalTestSize) && (zfh.frameContentSize!=ZSTD_CONTENTSIZE_UNKNOWN),
                      "ZSTD_decodingBufferSize_min() requires more memory (%u) than necessary (%u)",
                      (unsigned)roundBuffSize, (unsigned)totalTestSize );
        }   }
        if (dictSize<8) dictSize=0, dict=NULL;   /* disable dictionary */
        CHECK_Z( ZSTD_decompressBegin_usingDict(dctx, dict, dictSize) );
        totalCSize = 0;
        totalGenSize = 0;
        while (totalCSize < cSize) {
            size_t const inSize = ZSTD_nextSrcSizeToDecompress(dctx);
            size_t const genSize = ZSTD_decompressContinue(dctx, dstBuffer+totalGenSize, dstBufferSize-totalGenSize, cBuffer+totalCSize, inSize);
            CHECK (ZSTD_isError(genSize), "ZSTD_decompressContinue error : %s", ZSTD_getErrorName(genSize));
            totalGenSize += genSize;
            totalCSize += inSize;
        }
        CHECK (ZSTD_nextSrcSizeToDecompress(dctx) != 0, "frame not fully decoded");
        CHECK (totalGenSize != totalTestSize, "streaming decompressed data : wrong size")
        CHECK (totalCSize != cSize, "compressed data should be fully read")
        {   U64 const crcDest = XXH64(dstBuffer, totalTestSize, 0);
            CHECK(crcOrig != crcDest, "streaming decompressed data corrupted (pos %u / %u)",
                (unsigned)findDiff(mirrorBuffer, dstBuffer, totalTestSize), (unsigned)totalTestSize);
        }
    }   /* for ( ; (testNb <= nbTests) */
    DISPLAY("\r%u fuzzer tests completed   \n", testNb-1);

_cleanup:
    ZSTD_freeCCtx(refCtx);
    ZSTD_freeCCtx(ctx);
    ZSTD_freeDCtx(dctx);
    free(cNoiseBuffer[0]);
    free(cNoiseBuffer[1]);
    free(cNoiseBuffer[2]);
    free(cNoiseBuffer[3]);
    free(cNoiseBuffer[4]);
    free(cBuffer);
    free(dstBuffer);
    free(mirrorBuffer);
    return result;

_output_error:
    result = 1;
    goto _cleanup;
}


/*_*******************************************************
*  Command line
*********************************************************/
static int FUZ_usage(const char* programName)
{
    DISPLAY( "Usage :\n");
    DISPLAY( "      %s [args]\n", programName);
    DISPLAY( "\n");
    DISPLAY( "Arguments :\n");
    DISPLAY( " -i#    : Nb of tests (default:%i) \n", nbTestsDefault);
    DISPLAY( " -s#    : Select seed (default:prompt user)\n");
    DISPLAY( " -t#    : Select starting test number (default:0)\n");
    DISPLAY( " -P#    : Select compressibility in %% (default:%i%%)\n", FUZ_compressibility_default);
    DISPLAY( " -v     : verbose\n");
    DISPLAY( " -p     : pause at the end\n");
    DISPLAY( " -h     : display help and exit\n");
    return 0;
}

/*! readU32FromChar() :
    @return : unsigned integer value read from input in `char` format
    allows and interprets K, KB, KiB, M, MB and MiB suffix.
    Will also modify `*stringPtr`, advancing it to position where it stopped reading.
    Note : function result can overflow if digit string > MAX_UINT */
static unsigned readU32FromChar(const char** stringPtr)
{
    unsigned result = 0;
    while ((**stringPtr >='0') && (**stringPtr <='9'))
        result *= 10, result += **stringPtr - '0', (*stringPtr)++ ;
    if ((**stringPtr=='K') || (**stringPtr=='M')) {
        result <<= 10;
        if (**stringPtr=='M') result <<= 10;
        (*stringPtr)++ ;
        if (**stringPtr=='i') (*stringPtr)++;
        if (**stringPtr=='B') (*stringPtr)++;
    }
    return result;
}

/** longCommandWArg() :
 *  check if *stringPtr is the same as longCommand.
 *  If yes, @return 1 and advances *stringPtr to the position which immediately follows longCommand.
 *  @return 0 and doesn't modify *stringPtr otherwise.
 */
static unsigned longCommandWArg(const char** stringPtr, const char* longCommand)
{
    size_t const comSize = strlen(longCommand);
    int const result = !strncmp(*stringPtr, longCommand, comSize);
    if (result) *stringPtr += comSize;
    return result;
}

int main(int argc, const char** argv)
{
    U32 seed = 0;
    int seedset = 0;
    int argNb;
    int nbTests = nbTestsDefault;
    int testNb = 0;
    int proba = FUZ_compressibility_default;
    int result = 0;
    U32 mainPause = 0;
    U32 maxDuration = 0;
    int bigTests = 1;
    U32 memTestsOnly = 0;
    const char* const programName = argv[0];

    /* Check command line */
    for (argNb=1; argNb<argc; argNb++) {
        const char* argument = argv[argNb];
        if(!argument) continue;   /* Protection if argument empty */

        /* Handle commands. Aggregated commands are allowed */
        if (argument[0]=='-') {

            if (longCommandWArg(&argument, "--memtest=")) { memTestsOnly = readU32FromChar(&argument); continue; }

            if (!strcmp(argument, "--memtest")) { memTestsOnly=1; continue; }
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

                case 'i':
                    argument++; maxDuration = 0;
                    nbTests = readU32FromChar(&argument);
                    break;

                case 'T':
                    argument++;
                    nbTests = 0;
                    maxDuration = readU32FromChar(&argument);
                    if (*argument=='s') argument++;   /* seconds */
                    if (*argument=='m') maxDuration *= 60, argument++;   /* minutes */
                    if (*argument=='n') argument++;
                    break;

                case 's':
                    argument++;
                    seedset = 1;
                    seed = readU32FromChar(&argument);
                    break;

                case 't':
                    argument++;
                    testNb = readU32FromChar(&argument);
                    break;

                case 'P':   /* compressibility % */
                    argument++;
                    proba = readU32FromChar(&argument);
                    if (proba>100) proba = 100;
                    break;

                default:
                    return (FUZ_usage(programName), 1);
    }   }   }   }   /* for (argNb=1; argNb<argc; argNb++) */

    /* Get Seed */
    DISPLAY("Starting zstd tester (%i-bits, %s)\n", (int)(sizeof(size_t)*8), ZSTD_VERSION_STRING);

    if (!seedset) {
        time_t const t = time(NULL);
        U32 const h = XXH32(&t, sizeof(t), 1);
        seed = h % 10000;
    }

    DISPLAY("Seed = %u\n", (unsigned)seed);
    if (proba!=FUZ_compressibility_default) DISPLAY("Compressibility : %i%%\n", proba);

    if (memTestsOnly) {
        g_displayLevel = MAX(3, g_displayLevel);
        return FUZ_mallocTests(seed, ((double)proba) / 100, memTestsOnly);
    }

    if (nbTests < testNb) nbTests = testNb;

    if (testNb==0)
        result = basicUnitTests(0, ((double)proba) / 100);  /* constant seed for predictability */
    if (!result)
        result = fuzzerTests(seed, nbTests, testNb, maxDuration, ((double)proba) / 100, bigTests);
    if (mainPause) {
        int unused;
        DISPLAY("Press Enter \n");
        unused = getchar();
        (void)unused;
    }
    return result;
}
