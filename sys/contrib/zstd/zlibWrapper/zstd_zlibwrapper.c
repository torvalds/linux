/*
 * Copyright (c) 2016-present, Przemyslaw Skibinski, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/* ===   Tuning parameters   === */
#ifndef ZWRAP_USE_ZSTD
    #define ZWRAP_USE_ZSTD 0
#endif


/* ===   Dependencies   === */
#include <stdlib.h>
#include <stdio.h>                 /* vsprintf */
#include <stdarg.h>                /* va_list, for z_gzprintf */
#define NO_DUMMY_DECL
#define ZLIB_CONST
#include <zlib.h>                  /* without #define Z_PREFIX */
#include "zstd_zlibwrapper.h"
#define ZSTD_STATIC_LINKING_ONLY   /* ZSTD_isFrame, ZSTD_MAGICNUMBER */
#include "zstd.h"
#include "zstd_internal.h"         /* ZSTD_malloc, ZSTD_free */


/* ===   Constants   === */
#define Z_INFLATE_SYNC              8
#define ZLIB_HEADERSIZE             4
#define ZSTD_HEADERSIZE             ZSTD_FRAMEHEADERSIZE_MIN
#define ZWRAP_DEFAULT_CLEVEL        3   /* Z_DEFAULT_COMPRESSION is translated to ZWRAP_DEFAULT_CLEVEL for zstd */


/* ===   Debug   === */
#define LOG_WRAPPERC(...)  /* fprintf(stderr, __VA_ARGS__) */
#define LOG_WRAPPERD(...)  /* fprintf(stderr, __VA_ARGS__) */

#define FINISH_WITH_GZ_ERR(msg) { (void)msg; return Z_STREAM_ERROR; }
#define FINISH_WITH_NULL_ERR(msg) { (void)msg; return NULL; }


/* ===   Wrapper   === */
static int g_ZWRAP_useZSTDcompression = ZWRAP_USE_ZSTD; /* 0 = don't use ZSTD */

void ZWRAP_useZSTDcompression(int turn_on) { g_ZWRAP_useZSTDcompression = turn_on; }

int ZWRAP_isUsingZSTDcompression(void) { return g_ZWRAP_useZSTDcompression; }



static ZWRAP_decompress_type g_ZWRAPdecompressionType = ZWRAP_AUTO;

void ZWRAP_setDecompressionType(ZWRAP_decompress_type type) { g_ZWRAPdecompressionType = type; };

ZWRAP_decompress_type ZWRAP_getDecompressionType(void) { return g_ZWRAPdecompressionType; }



const char * zstdVersion(void) { return ZSTD_VERSION_STRING; }

ZEXTERN const char * ZEXPORT z_zlibVersion OF((void)) { return zlibVersion();  }



static void* ZWRAP_allocFunction(void* opaque, size_t size)
{
    z_streamp strm = (z_streamp) opaque;
    void* address = strm->zalloc(strm->opaque, 1, (uInt)size);
    /* LOG_WRAPPERC("ZWRAP alloc %p, %d \n", address, (int)size); */
    return address;
}

static void ZWRAP_freeFunction(void* opaque, void* address)
{
    z_streamp strm = (z_streamp) opaque;
    strm->zfree(strm->opaque, address);
   /* if (address) LOG_WRAPPERC("ZWRAP free %p \n", address); */
}



/* ===   Compression   === */
typedef enum { ZWRAP_useInit, ZWRAP_useReset, ZWRAP_streamEnd } ZWRAP_state_t;

typedef struct {
    ZSTD_CStream* zbc;
    int compressionLevel;
    int streamEnd; /* a flag to signal the end of a stream */
    unsigned long long totalInBytes; /* we need it as strm->total_in can be reset by user */
    ZSTD_customMem customMem;
    z_stream allocFunc; /* copy of zalloc, zfree, opaque */
    ZSTD_inBuffer inBuffer;
    ZSTD_outBuffer outBuffer;
    ZWRAP_state_t comprState;
    unsigned long long pledgedSrcSize;
} ZWRAP_CCtx;

typedef ZWRAP_CCtx internal_state;



static size_t ZWRAP_freeCCtx(ZWRAP_CCtx* zwc)
{
    if (zwc==NULL) return 0;   /* support free on NULL */
    ZSTD_freeCStream(zwc->zbc);
    ZSTD_free(zwc, zwc->customMem);
    return 0;
}


static ZWRAP_CCtx* ZWRAP_createCCtx(z_streamp strm)
{
    ZWRAP_CCtx* zwc;

    if (strm->zalloc && strm->zfree) {
        zwc = (ZWRAP_CCtx*)strm->zalloc(strm->opaque, 1, sizeof(ZWRAP_CCtx));
        if (zwc==NULL) return NULL;
        memset(zwc, 0, sizeof(ZWRAP_CCtx));
        memcpy(&zwc->allocFunc, strm, sizeof(z_stream));
        { ZSTD_customMem const ZWRAP_customMem = { ZWRAP_allocFunction, ZWRAP_freeFunction, &zwc->allocFunc };
          zwc->customMem = ZWRAP_customMem; }
    } else {
        zwc = (ZWRAP_CCtx*)calloc(1, sizeof(*zwc));
        if (zwc==NULL) return NULL;
    }

    return zwc;
}


static int ZWRAP_initializeCStream(ZWRAP_CCtx* zwc, const void* dict, size_t dictSize, unsigned long long pledgedSrcSize)
{
    LOG_WRAPPERC("- ZWRAP_initializeCStream=%p\n", zwc);
    if (zwc == NULL || zwc->zbc == NULL) return Z_STREAM_ERROR;

    if (!pledgedSrcSize) pledgedSrcSize = zwc->pledgedSrcSize;
    {   ZSTD_parameters const params = ZSTD_getParams(zwc->compressionLevel, pledgedSrcSize, dictSize);
        size_t initErr;
        LOG_WRAPPERC("pledgedSrcSize=%d windowLog=%d chainLog=%d hashLog=%d searchLog=%d minMatch=%d strategy=%d\n",
                    (int)pledgedSrcSize, params.cParams.windowLog, params.cParams.chainLog, params.cParams.hashLog, params.cParams.searchLog, params.cParams.minMatch, params.cParams.strategy);
        initErr = ZSTD_initCStream_advanced(zwc->zbc, dict, dictSize, params, pledgedSrcSize);
        if (ZSTD_isError(initErr)) return Z_STREAM_ERROR;
    }

    return Z_OK;
}


static int ZWRAPC_finishWithError(ZWRAP_CCtx* zwc, z_streamp strm, int error)
{
    LOG_WRAPPERC("- ZWRAPC_finishWithError=%d\n", error);
    if (zwc) ZWRAP_freeCCtx(zwc);
    if (strm) strm->state = NULL;
    return (error) ? error : Z_STREAM_ERROR;
}


static int ZWRAPC_finishWithErrorMsg(z_streamp strm, char* message)
{
    ZWRAP_CCtx* zwc = (ZWRAP_CCtx*) strm->state;
    strm->msg = message;
    if (zwc == NULL) return Z_STREAM_ERROR;

    return ZWRAPC_finishWithError(zwc, strm, 0);
}


int ZWRAP_setPledgedSrcSize(z_streamp strm, unsigned long long pledgedSrcSize)
{
    ZWRAP_CCtx* zwc = (ZWRAP_CCtx*) strm->state;
    if (zwc == NULL) return Z_STREAM_ERROR;

    zwc->pledgedSrcSize = pledgedSrcSize;
    zwc->comprState = ZWRAP_useInit;
    return Z_OK;
}


ZEXTERN int ZEXPORT z_deflateInit_ OF((z_streamp strm, int level,
                                     const char *version, int stream_size))
{
    ZWRAP_CCtx* zwc;

    LOG_WRAPPERC("- deflateInit level=%d\n", level);
    if (!g_ZWRAP_useZSTDcompression) {
        return deflateInit_((strm), (level), version, stream_size);
    }

    zwc = ZWRAP_createCCtx(strm);
    if (zwc == NULL) return Z_MEM_ERROR;

    if (level == Z_DEFAULT_COMPRESSION)
        level = ZWRAP_DEFAULT_CLEVEL;

    zwc->streamEnd = 0;
    zwc->totalInBytes = 0;
    zwc->compressionLevel = level;
    strm->state = (struct internal_state*) zwc; /* use state which in not used by user */
    strm->total_in = 0;
    strm->total_out = 0;
    strm->adler = 0;
    return Z_OK;
}


ZEXTERN int ZEXPORT z_deflateInit2_ OF((z_streamp strm, int level, int method,
                                      int windowBits, int memLevel,
                                      int strategy, const char *version,
                                      int stream_size))
{
    if (!g_ZWRAP_useZSTDcompression)
        return deflateInit2_(strm, level, method, windowBits, memLevel, strategy, version, stream_size);

    return z_deflateInit_ (strm, level, version, stream_size);
}


int ZWRAP_deflateReset_keepDict(z_streamp strm)
{
    LOG_WRAPPERC("- ZWRAP_deflateReset_keepDict\n");
    if (!g_ZWRAP_useZSTDcompression)
        return deflateReset(strm);

    { ZWRAP_CCtx* zwc = (ZWRAP_CCtx*) strm->state;
      if (zwc) {
          zwc->streamEnd = 0;
          zwc->totalInBytes = 0;
      }
    }

    strm->total_in = 0;
    strm->total_out = 0;
    strm->adler = 0;
    return Z_OK;
}


ZEXTERN int ZEXPORT z_deflateReset OF((z_streamp strm))
{
    LOG_WRAPPERC("- deflateReset\n");
    if (!g_ZWRAP_useZSTDcompression)
        return deflateReset(strm);

    ZWRAP_deflateReset_keepDict(strm);

    { ZWRAP_CCtx* zwc = (ZWRAP_CCtx*) strm->state;
      if (zwc) zwc->comprState = ZWRAP_useInit;
    }
    return Z_OK;
}


ZEXTERN int ZEXPORT z_deflateSetDictionary OF((z_streamp strm,
                                             const Bytef *dictionary,
                                             uInt  dictLength))
{
    if (!g_ZWRAP_useZSTDcompression) {
        LOG_WRAPPERC("- deflateSetDictionary\n");
        return deflateSetDictionary(strm, dictionary, dictLength);
    }

    {   ZWRAP_CCtx* zwc = (ZWRAP_CCtx*) strm->state;
        LOG_WRAPPERC("- deflateSetDictionary level=%d\n", (int)zwc->compressionLevel);
        if (!zwc) return Z_STREAM_ERROR;
        if (zwc->zbc == NULL) {
            zwc->zbc = ZSTD_createCStream_advanced(zwc->customMem);
            if (zwc->zbc == NULL) return ZWRAPC_finishWithError(zwc, strm, 0);
        }
        { int res = ZWRAP_initializeCStream(zwc, dictionary, dictLength, ZSTD_CONTENTSIZE_UNKNOWN);
          if (res != Z_OK) return ZWRAPC_finishWithError(zwc, strm, res); }
        zwc->comprState = ZWRAP_useReset;
    }

    return Z_OK;
}


ZEXTERN int ZEXPORT z_deflate OF((z_streamp strm, int flush))
{
    ZWRAP_CCtx* zwc;

    if (!g_ZWRAP_useZSTDcompression) {
        LOG_WRAPPERC("- deflate1 flush=%d avail_in=%d avail_out=%d total_in=%d total_out=%d\n",
                    (int)flush, (int)strm->avail_in, (int)strm->avail_out, (int)strm->total_in, (int)strm->total_out);
        return deflate(strm, flush);
    }

    zwc = (ZWRAP_CCtx*) strm->state;
    if (zwc == NULL) { LOG_WRAPPERC("zwc == NULL\n"); return Z_STREAM_ERROR; }

    if (zwc->zbc == NULL) {
        zwc->zbc = ZSTD_createCStream_advanced(zwc->customMem);
        if (zwc->zbc == NULL) return ZWRAPC_finishWithError(zwc, strm, 0);
        { int const initErr = ZWRAP_initializeCStream(zwc, NULL, 0, (flush == Z_FINISH) ? strm->avail_in : ZSTD_CONTENTSIZE_UNKNOWN);
          if (initErr != Z_OK) return ZWRAPC_finishWithError(zwc, strm, initErr); }
        if (flush != Z_FINISH) zwc->comprState = ZWRAP_useReset;
    } else {
        if (zwc->totalInBytes == 0) {
            if (zwc->comprState == ZWRAP_useReset) {
                size_t const resetErr = ZSTD_resetCStream(zwc->zbc, (flush == Z_FINISH) ? strm->avail_in : zwc->pledgedSrcSize);
                if (ZSTD_isError(resetErr)) {
                    LOG_WRAPPERC("ERROR: ZSTD_resetCStream errorCode=%s\n",
                                ZSTD_getErrorName(resetErr));
                    return ZWRAPC_finishWithError(zwc, strm, 0);
                }
            } else {
                int const res = ZWRAP_initializeCStream(zwc, NULL, 0, (flush == Z_FINISH) ? strm->avail_in : ZSTD_CONTENTSIZE_UNKNOWN);
                if (res != Z_OK) return ZWRAPC_finishWithError(zwc, strm, res);
                if (flush != Z_FINISH) zwc->comprState = ZWRAP_useReset;
            }
        }  /* (zwc->totalInBytes == 0) */
    }  /* ! (zwc->zbc == NULL) */

    LOG_WRAPPERC("- deflate2 flush=%d avail_in=%d avail_out=%d total_in=%d total_out=%d\n", (int)flush, (int)strm->avail_in, (int)strm->avail_out, (int)strm->total_in, (int)strm->total_out);
    if (strm->avail_in > 0) {
        zwc->inBuffer.src = strm->next_in;
        zwc->inBuffer.size = strm->avail_in;
        zwc->inBuffer.pos = 0;
        zwc->outBuffer.dst = strm->next_out;
        zwc->outBuffer.size = strm->avail_out;
        zwc->outBuffer.pos = 0;
        { size_t const cErr = ZSTD_compressStream(zwc->zbc, &zwc->outBuffer, &zwc->inBuffer);
          LOG_WRAPPERC("deflate ZSTD_compressStream srcSize=%d dstCapacity=%d\n", (int)zwc->inBuffer.size, (int)zwc->outBuffer.size);
          if (ZSTD_isError(cErr)) return ZWRAPC_finishWithError(zwc, strm, 0);
        }
        strm->next_out += zwc->outBuffer.pos;
        strm->total_out += zwc->outBuffer.pos;
        strm->avail_out -= zwc->outBuffer.pos;
        strm->total_in += zwc->inBuffer.pos;
        zwc->totalInBytes += zwc->inBuffer.pos;
        strm->next_in += zwc->inBuffer.pos;
        strm->avail_in -= zwc->inBuffer.pos;
    }

    if (flush == Z_FULL_FLUSH
#if ZLIB_VERNUM >= 0x1240
        || flush == Z_TREES
#endif
        || flush == Z_BLOCK)
        return ZWRAPC_finishWithErrorMsg(strm, "Z_FULL_FLUSH, Z_BLOCK and Z_TREES are not supported!");

    if (flush == Z_FINISH) {
        size_t bytesLeft;
        if (zwc->streamEnd) return Z_STREAM_END;
        zwc->outBuffer.dst = strm->next_out;
        zwc->outBuffer.size = strm->avail_out;
        zwc->outBuffer.pos = 0;
        bytesLeft = ZSTD_endStream(zwc->zbc, &zwc->outBuffer);
        LOG_WRAPPERC("deflate ZSTD_endStream dstCapacity=%d bytesLeft=%d\n", (int)strm->avail_out, (int)bytesLeft);
        if (ZSTD_isError(bytesLeft)) return ZWRAPC_finishWithError(zwc, strm, 0);
        strm->next_out += zwc->outBuffer.pos;
        strm->total_out += zwc->outBuffer.pos;
        strm->avail_out -= zwc->outBuffer.pos;
        if (bytesLeft == 0) {
            zwc->streamEnd = 1;
            LOG_WRAPPERC("Z_STREAM_END2 strm->total_in=%d strm->avail_out=%d strm->total_out=%d\n",
                        (int)strm->total_in, (int)strm->avail_out, (int)strm->total_out);
            return Z_STREAM_END;
    }   }
    else
    if (flush == Z_SYNC_FLUSH || flush == Z_PARTIAL_FLUSH) {
        size_t bytesLeft;
        zwc->outBuffer.dst = strm->next_out;
        zwc->outBuffer.size = strm->avail_out;
        zwc->outBuffer.pos = 0;
        bytesLeft = ZSTD_flushStream(zwc->zbc, &zwc->outBuffer);
        LOG_WRAPPERC("deflate ZSTD_flushStream dstCapacity=%d bytesLeft=%d\n", (int)strm->avail_out, (int)bytesLeft);
        if (ZSTD_isError(bytesLeft)) return ZWRAPC_finishWithError(zwc, strm, 0);
        strm->next_out += zwc->outBuffer.pos;
        strm->total_out += zwc->outBuffer.pos;
        strm->avail_out -= zwc->outBuffer.pos;
    }
    LOG_WRAPPERC("- deflate3 flush=%d avail_in=%d avail_out=%d total_in=%d total_out=%d\n", (int)flush, (int)strm->avail_in, (int)strm->avail_out, (int)strm->total_in, (int)strm->total_out);
    return Z_OK;
}


ZEXTERN int ZEXPORT z_deflateEnd OF((z_streamp strm))
{
    if (!g_ZWRAP_useZSTDcompression) {
        LOG_WRAPPERC("- deflateEnd\n");
        return deflateEnd(strm);
    }
    LOG_WRAPPERC("- deflateEnd total_in=%d total_out=%d\n", (int)(strm->total_in), (int)(strm->total_out));
    {   size_t errorCode;
        ZWRAP_CCtx* zwc = (ZWRAP_CCtx*) strm->state;
        if (zwc == NULL) return Z_OK;  /* structures are already freed */
        strm->state = NULL;
        errorCode = ZWRAP_freeCCtx(zwc);
        if (ZSTD_isError(errorCode)) return Z_STREAM_ERROR;
    }
    return Z_OK;
}


ZEXTERN uLong ZEXPORT z_deflateBound OF((z_streamp strm,
                                       uLong sourceLen))
{
    if (!g_ZWRAP_useZSTDcompression)
        return deflateBound(strm, sourceLen);

    return ZSTD_compressBound(sourceLen);
}


ZEXTERN int ZEXPORT z_deflateParams OF((z_streamp strm,
                                      int level,
                                      int strategy))
{
    if (!g_ZWRAP_useZSTDcompression) {
        LOG_WRAPPERC("- deflateParams level=%d strategy=%d\n", level, strategy);
        return deflateParams(strm, level, strategy);
    }

    return Z_OK;
}





/* ===   Decompression   === */

typedef enum { ZWRAP_ZLIB_STREAM, ZWRAP_ZSTD_STREAM, ZWRAP_UNKNOWN_STREAM } ZWRAP_stream_type;

typedef struct {
    ZSTD_DStream* zbd;
    char headerBuf[16];   /* must be >= ZSTD_frameHeaderSize_min */
    int errorCount;
    unsigned long long totalInBytes; /* we need it as strm->total_in can be reset by user */
    ZWRAP_state_t decompState;
    ZSTD_inBuffer inBuffer;
    ZSTD_outBuffer outBuffer;

    /* zlib params */
    int stream_size;
    char *version;
    int windowBits;
    ZSTD_customMem customMem;
    z_stream allocFunc; /* just to copy zalloc, zfree, opaque */
} ZWRAP_DCtx;


static void ZWRAP_initDCtx(ZWRAP_DCtx* zwd)
{
    zwd->errorCount = 0;
    zwd->outBuffer.pos = 0;
    zwd->outBuffer.size = 0;
}

static ZWRAP_DCtx* ZWRAP_createDCtx(z_streamp strm)
{
    ZWRAP_DCtx* zwd;
    MEM_STATIC_ASSERT(sizeof(zwd->headerBuf) >= ZSTD_FRAMEHEADERSIZE_MIN);   /* check static buffer size condition */

    if (strm->zalloc && strm->zfree) {
        zwd = (ZWRAP_DCtx*)strm->zalloc(strm->opaque, 1, sizeof(ZWRAP_DCtx));
        if (zwd==NULL) return NULL;
        memset(zwd, 0, sizeof(ZWRAP_DCtx));
        zwd->allocFunc = *strm;  /* just to copy zalloc, zfree & opaque */
        { ZSTD_customMem const ZWRAP_customMem = { ZWRAP_allocFunction, ZWRAP_freeFunction, &zwd->allocFunc };
          zwd->customMem = ZWRAP_customMem; }
    } else {
        zwd = (ZWRAP_DCtx*)calloc(1, sizeof(*zwd));
        if (zwd==NULL) return NULL;
    }

    ZWRAP_initDCtx(zwd);
    return zwd;
}

static size_t ZWRAP_freeDCtx(ZWRAP_DCtx* zwd)
{
    if (zwd==NULL) return 0;   /* support free on null */
    ZSTD_freeDStream(zwd->zbd);
    ZSTD_free(zwd->version, zwd->customMem);
    ZSTD_free(zwd, zwd->customMem);
    return 0;
}


int ZWRAP_isUsingZSTDdecompression(z_streamp strm)
{
    if (strm == NULL) return 0;
    return (strm->reserved == ZWRAP_ZSTD_STREAM);
}


static int ZWRAPD_finishWithError(ZWRAP_DCtx* zwd, z_streamp strm, int error)
{
    LOG_WRAPPERD("- ZWRAPD_finishWithError=%d\n", error);
    ZWRAP_freeDCtx(zwd);
    strm->state = NULL;
    return (error) ? error : Z_STREAM_ERROR;
}

static int ZWRAPD_finishWithErrorMsg(z_streamp strm, char* message)
{
    ZWRAP_DCtx* const zwd = (ZWRAP_DCtx*) strm->state;
    strm->msg = message;
    if (zwd == NULL) return Z_STREAM_ERROR;

    return ZWRAPD_finishWithError(zwd, strm, 0);
}


ZEXTERN int ZEXPORT z_inflateInit_ OF((z_streamp strm,
                                     const char *version, int stream_size))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB) {
        strm->reserved = ZWRAP_ZLIB_STREAM;
        return inflateInit(strm);
    }

    {   ZWRAP_DCtx* const zwd = ZWRAP_createDCtx(strm);
        LOG_WRAPPERD("- inflateInit\n");
        if (zwd == NULL) return ZWRAPD_finishWithError(zwd, strm, 0);

        zwd->version = ZSTD_malloc(strlen(version)+1, zwd->customMem);
        if (zwd->version == NULL) return ZWRAPD_finishWithError(zwd, strm, 0);
        strcpy(zwd->version, version);

        zwd->stream_size = stream_size;
        zwd->totalInBytes = 0;
        strm->state = (struct internal_state*) zwd;
        strm->total_in = 0;
        strm->total_out = 0;
        strm->reserved = ZWRAP_UNKNOWN_STREAM;
        strm->adler = 0;
    }

    return Z_OK;
}


ZEXTERN int ZEXPORT z_inflateInit2_ OF((z_streamp strm, int  windowBits,
                                      const char *version, int stream_size))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB) {
        return inflateInit2_(strm, windowBits, version, stream_size);
    }

    {   int const ret = z_inflateInit_ (strm, version, stream_size);
        LOG_WRAPPERD("- inflateInit2 windowBits=%d\n", windowBits);
        if (ret == Z_OK) {
            ZWRAP_DCtx* const zwd = (ZWRAP_DCtx*)strm->state;
            if (zwd == NULL) return Z_STREAM_ERROR;
            zwd->windowBits = windowBits;
        }
        return ret;
    }
}

int ZWRAP_inflateReset_keepDict(z_streamp strm)
{
    LOG_WRAPPERD("- ZWRAP_inflateReset_keepDict\n");
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved)
        return inflateReset(strm);

    {   ZWRAP_DCtx* const zwd = (ZWRAP_DCtx*) strm->state;
        if (zwd == NULL) return Z_STREAM_ERROR;
        ZWRAP_initDCtx(zwd);
        zwd->decompState = ZWRAP_useReset;
        zwd->totalInBytes = 0;
    }

    strm->total_in = 0;
    strm->total_out = 0;
    return Z_OK;
}


ZEXTERN int ZEXPORT z_inflateReset OF((z_streamp strm))
{
    LOG_WRAPPERD("- inflateReset\n");
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved)
        return inflateReset(strm);

    { int const ret = ZWRAP_inflateReset_keepDict(strm);
      if (ret != Z_OK) return ret; }

    { ZWRAP_DCtx* const zwd = (ZWRAP_DCtx*) strm->state;
      if (zwd == NULL) return Z_STREAM_ERROR;
      zwd->decompState = ZWRAP_useInit; }

    return Z_OK;
}


#if ZLIB_VERNUM >= 0x1240
ZEXTERN int ZEXPORT z_inflateReset2 OF((z_streamp strm,
                                      int windowBits))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved)
        return inflateReset2(strm, windowBits);

    {   int const ret = z_inflateReset (strm);
        if (ret == Z_OK) {
            ZWRAP_DCtx* const zwd = (ZWRAP_DCtx*)strm->state;
            if (zwd == NULL) return Z_STREAM_ERROR;
            zwd->windowBits = windowBits;
        }
        return ret;
    }
}
#endif


ZEXTERN int ZEXPORT z_inflateSetDictionary OF((z_streamp strm,
                                             const Bytef *dictionary,
                                             uInt  dictLength))
{
    LOG_WRAPPERD("- inflateSetDictionary\n");
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved)
        return inflateSetDictionary(strm, dictionary, dictLength);

    {   ZWRAP_DCtx* const zwd = (ZWRAP_DCtx*) strm->state;
        if (zwd == NULL || zwd->zbd == NULL) return Z_STREAM_ERROR;
        { size_t const initErr = ZSTD_initDStream_usingDict(zwd->zbd, dictionary, dictLength);
          if (ZSTD_isError(initErr)) return ZWRAPD_finishWithError(zwd, strm, 0); }
        zwd->decompState = ZWRAP_useReset;

        if (zwd->totalInBytes == ZSTD_HEADERSIZE) {
            zwd->inBuffer.src = zwd->headerBuf;
            zwd->inBuffer.size = zwd->totalInBytes;
            zwd->inBuffer.pos = 0;
            zwd->outBuffer.dst = strm->next_out;
            zwd->outBuffer.size = 0;
            zwd->outBuffer.pos = 0;
            {   size_t const errorCode = ZSTD_decompressStream(zwd->zbd, &zwd->outBuffer, &zwd->inBuffer);
                LOG_WRAPPERD("inflateSetDictionary ZSTD_decompressStream errorCode=%d srcSize=%d dstCapacity=%d\n",
                             (int)errorCode, (int)zwd->inBuffer.size, (int)zwd->outBuffer.size);
                if (zwd->inBuffer.pos < zwd->outBuffer.size || ZSTD_isError(errorCode)) {
                    LOG_WRAPPERD("ERROR: ZSTD_decompressStream %s\n",
                                 ZSTD_getErrorName(errorCode));
                    return ZWRAPD_finishWithError(zwd, strm, 0);
    }   }   }   }

    return Z_OK;
}


ZEXTERN int ZEXPORT z_inflate OF((z_streamp strm, int flush))
{
    ZWRAP_DCtx* zwd;

    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved) {
        int const result = inflate(strm, flush);
        LOG_WRAPPERD("- inflate2 flush=%d avail_in=%d avail_out=%d total_in=%d total_out=%d res=%d\n",
                     (int)flush, (int)strm->avail_in, (int)strm->avail_out, (int)strm->total_in, (int)strm->total_out, result);
        return result;
    }

    if (strm->avail_in <= 0) return Z_OK;

    zwd = (ZWRAP_DCtx*) strm->state;
    LOG_WRAPPERD("- inflate1 flush=%d avail_in=%d avail_out=%d total_in=%d total_out=%d\n",
                 (int)flush, (int)strm->avail_in, (int)strm->avail_out, (int)strm->total_in, (int)strm->total_out);

    if (zwd == NULL) return Z_STREAM_ERROR;
    if (zwd->decompState == ZWRAP_streamEnd) return Z_STREAM_END;

    if (zwd->totalInBytes < ZLIB_HEADERSIZE) {
        if (zwd->totalInBytes == 0 && strm->avail_in >= ZLIB_HEADERSIZE) {
            if (MEM_readLE32(strm->next_in) != ZSTD_MAGICNUMBER) {
                {   int const initErr = (zwd->windowBits) ?
                                inflateInit2_(strm, zwd->windowBits, zwd->version, zwd->stream_size) :
                                inflateInit_(strm, zwd->version, zwd->stream_size);
                    LOG_WRAPPERD("ZLIB inflateInit errorCode=%d\n", initErr);
                    if (initErr != Z_OK) return ZWRAPD_finishWithError(zwd, strm, initErr);
                }

                strm->reserved = ZWRAP_ZLIB_STREAM;
                { size_t const freeErr = ZWRAP_freeDCtx(zwd);
                  if (ZSTD_isError(freeErr)) goto error; }

                {   int const result = (flush == Z_INFLATE_SYNC) ?
                                        inflateSync(strm) :
                                        inflate(strm, flush);
                    LOG_WRAPPERD("- inflate3 flush=%d avail_in=%d avail_out=%d total_in=%d total_out=%d res=%d\n",
                                 (int)flush, (int)strm->avail_in, (int)strm->avail_out, (int)strm->total_in, (int)strm->total_out, res);
                    return result;
            }   }
        } else {  /* ! (zwd->totalInBytes == 0 && strm->avail_in >= ZLIB_HEADERSIZE) */
            size_t const srcSize = MIN(strm->avail_in, ZLIB_HEADERSIZE - zwd->totalInBytes);
            memcpy(zwd->headerBuf+zwd->totalInBytes, strm->next_in, srcSize);
            strm->total_in += srcSize;
            zwd->totalInBytes += srcSize;
            strm->next_in += srcSize;
            strm->avail_in -= srcSize;
            if (zwd->totalInBytes < ZLIB_HEADERSIZE) return Z_OK;

            if (MEM_readLE32(zwd->headerBuf) != ZSTD_MAGICNUMBER) {
                z_stream strm2;
                strm2.next_in = strm->next_in;
                strm2.avail_in = strm->avail_in;
                strm2.next_out = strm->next_out;
                strm2.avail_out = strm->avail_out;

                {   int const initErr = (zwd->windowBits) ?
                                inflateInit2_(strm, zwd->windowBits, zwd->version, zwd->stream_size) :
                                inflateInit_(strm, zwd->version, zwd->stream_size);
                    LOG_WRAPPERD("ZLIB inflateInit errorCode=%d\n", initErr);
                    if (initErr != Z_OK) return ZWRAPD_finishWithError(zwd, strm, initErr);
                }

                /* inflate header */
                strm->next_in = (unsigned char*)zwd->headerBuf;
                strm->avail_in = ZLIB_HEADERSIZE;
                strm->avail_out = 0;
                {   int const dErr = inflate(strm, Z_NO_FLUSH);
                    LOG_WRAPPERD("ZLIB inflate errorCode=%d strm->avail_in=%d\n",
                                  dErr, (int)strm->avail_in);
                    if (dErr != Z_OK)
                        return ZWRAPD_finishWithError(zwd, strm, dErr);
                }
                if (strm->avail_in > 0) goto error;

                strm->next_in = strm2.next_in;
                strm->avail_in = strm2.avail_in;
                strm->next_out = strm2.next_out;
                strm->avail_out = strm2.avail_out;

                strm->reserved = ZWRAP_ZLIB_STREAM; /* mark as zlib stream */
                { size_t const freeErr = ZWRAP_freeDCtx(zwd);
                  if (ZSTD_isError(freeErr)) goto error; }

                {   int const result = (flush == Z_INFLATE_SYNC) ?
                                       inflateSync(strm) :
                                       inflate(strm, flush);
                    LOG_WRAPPERD("- inflate2 flush=%d avail_in=%d avail_out=%d total_in=%d total_out=%d res=%d\n",
                                 (int)flush, (int)strm->avail_in, (int)strm->avail_out, (int)strm->total_in, (int)strm->total_out, res);
                    return result;
        }   }   }  /* if ! (zwd->totalInBytes == 0 && strm->avail_in >= ZLIB_HEADERSIZE) */
    }  /* (zwd->totalInBytes < ZLIB_HEADERSIZE) */

    strm->reserved = ZWRAP_ZSTD_STREAM; /* mark as zstd steam */

    if (flush == Z_INFLATE_SYNC) { strm->msg = "inflateSync is not supported!"; goto error; }

    if (!zwd->zbd) {
        zwd->zbd = ZSTD_createDStream_advanced(zwd->customMem);
        if (zwd->zbd == NULL) { LOG_WRAPPERD("ERROR: ZSTD_createDStream_advanced\n"); goto error; }
        zwd->decompState = ZWRAP_useInit;
    }

    if (zwd->totalInBytes < ZSTD_HEADERSIZE) {
        if (zwd->totalInBytes == 0 && strm->avail_in >= ZSTD_HEADERSIZE) {
            if (zwd->decompState == ZWRAP_useInit) {
                size_t const initErr = ZSTD_initDStream(zwd->zbd);
                if (ZSTD_isError(initErr)) {
                    LOG_WRAPPERD("ERROR: ZSTD_initDStream errorCode=%s\n",
                                 ZSTD_getErrorName(initErr));
                    goto error;
                }
            } else {
                size_t const resetErr = ZSTD_resetDStream(zwd->zbd);
                if (ZSTD_isError(resetErr)) goto error;
            }
        } else {
            size_t const srcSize = MIN(strm->avail_in, ZSTD_HEADERSIZE - zwd->totalInBytes);
            memcpy(zwd->headerBuf+zwd->totalInBytes, strm->next_in, srcSize);
            strm->total_in += srcSize;
            zwd->totalInBytes += srcSize;
            strm->next_in += srcSize;
            strm->avail_in -= srcSize;
            if (zwd->totalInBytes < ZSTD_HEADERSIZE) return Z_OK;

            if (zwd->decompState == ZWRAP_useInit) {
                size_t const initErr = ZSTD_initDStream(zwd->zbd);
                if (ZSTD_isError(initErr)) {
                    LOG_WRAPPERD("ERROR: ZSTD_initDStream errorCode=%s\n",
                                ZSTD_getErrorName(initErr));
                    goto error;
                }
            } else {
                size_t const resetErr = ZSTD_resetDStream(zwd->zbd);
                if (ZSTD_isError(resetErr)) goto error;
            }

            zwd->inBuffer.src = zwd->headerBuf;
            zwd->inBuffer.size = ZSTD_HEADERSIZE;
            zwd->inBuffer.pos = 0;
            zwd->outBuffer.dst = strm->next_out;
            zwd->outBuffer.size = 0;
            zwd->outBuffer.pos = 0;
            {   size_t const dErr = ZSTD_decompressStream(zwd->zbd, &zwd->outBuffer, &zwd->inBuffer);
                LOG_WRAPPERD("inflate ZSTD_decompressStream1 errorCode=%d srcSize=%d dstCapacity=%d\n",
                            (int)dErr, (int)zwd->inBuffer.size, (int)zwd->outBuffer.size);
                if (ZSTD_isError(dErr)) {
                    LOG_WRAPPERD("ERROR: ZSTD_decompressStream1 %s\n", ZSTD_getErrorName(dErr));
                    goto error;
            }   }
            if (zwd->inBuffer.pos != zwd->inBuffer.size) goto error; /* not consumed */
        }
    }   /* (zwd->totalInBytes < ZSTD_HEADERSIZE) */

    zwd->inBuffer.src = strm->next_in;
    zwd->inBuffer.size = strm->avail_in;
    zwd->inBuffer.pos = 0;
    zwd->outBuffer.dst = strm->next_out;
    zwd->outBuffer.size = strm->avail_out;
    zwd->outBuffer.pos = 0;
    {   size_t const dErr = ZSTD_decompressStream(zwd->zbd, &zwd->outBuffer, &zwd->inBuffer);
        LOG_WRAPPERD("inflate ZSTD_decompressStream2 errorCode=%d srcSize=%d dstCapacity=%d\n",
                    (int)dErr, (int)strm->avail_in, (int)strm->avail_out);
        if (ZSTD_isError(dErr)) {
            zwd->errorCount++;
            LOG_WRAPPERD("ERROR: ZSTD_decompressStream2 %s zwd->errorCount=%d\n",
                        ZSTD_getErrorName(dErr), zwd->errorCount);
            if (zwd->errorCount<=1) return Z_NEED_DICT; else goto error;
        }
        LOG_WRAPPERD("inflate inBuffer.pos=%d inBuffer.size=%d outBuffer.pos=%d outBuffer.size=%d o\n",
                    (int)zwd->inBuffer.pos, (int)zwd->inBuffer.size, (int)zwd->outBuffer.pos, (int)zwd->outBuffer.size);
        strm->next_out += zwd->outBuffer.pos;
        strm->total_out += zwd->outBuffer.pos;
        strm->avail_out -= zwd->outBuffer.pos;
        strm->total_in += zwd->inBuffer.pos;
        zwd->totalInBytes += zwd->inBuffer.pos;
        strm->next_in += zwd->inBuffer.pos;
        strm->avail_in -= zwd->inBuffer.pos;
        if (dErr == 0) {
            LOG_WRAPPERD("inflate Z_STREAM_END1 avail_in=%d avail_out=%d total_in=%d total_out=%d\n",
                        (int)strm->avail_in, (int)strm->avail_out, (int)strm->total_in, (int)strm->total_out);
            zwd->decompState = ZWRAP_streamEnd;
            return Z_STREAM_END;
        }
    }  /* dErr lifetime */

    LOG_WRAPPERD("- inflate2 flush=%d avail_in=%d avail_out=%d total_in=%d total_out=%d res=%d\n",
                (int)flush, (int)strm->avail_in, (int)strm->avail_out, (int)strm->total_in, (int)strm->total_out, Z_OK);
    return Z_OK;

error:
    return ZWRAPD_finishWithError(zwd, strm, 0);
}


ZEXTERN int ZEXPORT z_inflateEnd OF((z_streamp strm))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved)
        return inflateEnd(strm);

    LOG_WRAPPERD("- inflateEnd total_in=%d total_out=%d\n",
                (int)(strm->total_in), (int)(strm->total_out));
    {   ZWRAP_DCtx* const zwd = (ZWRAP_DCtx*) strm->state;
        if (zwd == NULL) return Z_OK;  /* structures are already freed */
        { size_t const freeErr = ZWRAP_freeDCtx(zwd);
          if (ZSTD_isError(freeErr)) return Z_STREAM_ERROR; }
        strm->state = NULL;
    }
    return Z_OK;
}


ZEXTERN int ZEXPORT z_inflateSync OF((z_streamp strm))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved) {
        return inflateSync(strm);
    }

    return z_inflate(strm, Z_INFLATE_SYNC);
}



/* Advanced compression functions */
ZEXTERN int ZEXPORT z_deflateCopy OF((z_streamp dest,
                                    z_streamp source))
{
    if (!g_ZWRAP_useZSTDcompression)
        return deflateCopy(dest, source);
    return ZWRAPC_finishWithErrorMsg(source, "deflateCopy is not supported!");
}


ZEXTERN int ZEXPORT z_deflateTune OF((z_streamp strm,
                                    int good_length,
                                    int max_lazy,
                                    int nice_length,
                                    int max_chain))
{
    if (!g_ZWRAP_useZSTDcompression)
        return deflateTune(strm, good_length, max_lazy, nice_length, max_chain);
    return ZWRAPC_finishWithErrorMsg(strm, "deflateTune is not supported!");
}


#if ZLIB_VERNUM >= 0x1260
ZEXTERN int ZEXPORT z_deflatePending OF((z_streamp strm,
                                       unsigned *pending,
                                       int *bits))
{
    if (!g_ZWRAP_useZSTDcompression)
        return deflatePending(strm, pending, bits);
    return ZWRAPC_finishWithErrorMsg(strm, "deflatePending is not supported!");
}
#endif


ZEXTERN int ZEXPORT z_deflatePrime OF((z_streamp strm,
                                     int bits,
                                     int value))
{
    if (!g_ZWRAP_useZSTDcompression)
        return deflatePrime(strm, bits, value);
    return ZWRAPC_finishWithErrorMsg(strm, "deflatePrime is not supported!");
}


ZEXTERN int ZEXPORT z_deflateSetHeader OF((z_streamp strm,
                                         gz_headerp head))
{
    if (!g_ZWRAP_useZSTDcompression)
        return deflateSetHeader(strm, head);
    return ZWRAPC_finishWithErrorMsg(strm, "deflateSetHeader is not supported!");
}




/* Advanced decompression functions */
#if ZLIB_VERNUM >= 0x1280
ZEXTERN int ZEXPORT z_inflateGetDictionary OF((z_streamp strm,
                                             Bytef *dictionary,
                                             uInt  *dictLength))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved)
        return inflateGetDictionary(strm, dictionary, dictLength);
    return ZWRAPD_finishWithErrorMsg(strm, "inflateGetDictionary is not supported!");
}
#endif


ZEXTERN int ZEXPORT z_inflateCopy OF((z_streamp dest,
                                    z_streamp source))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !source->reserved)
        return inflateCopy(dest, source);
    return ZWRAPD_finishWithErrorMsg(source, "inflateCopy is not supported!");
}


#if ZLIB_VERNUM >= 0x1240
ZEXTERN long ZEXPORT z_inflateMark OF((z_streamp strm))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved)
        return inflateMark(strm);
    return ZWRAPD_finishWithErrorMsg(strm, "inflateMark is not supported!");
}
#endif


ZEXTERN int ZEXPORT z_inflatePrime OF((z_streamp strm,
                                     int bits,
                                     int value))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved)
        return inflatePrime(strm, bits, value);
    return ZWRAPD_finishWithErrorMsg(strm, "inflatePrime is not supported!");
}


ZEXTERN int ZEXPORT z_inflateGetHeader OF((z_streamp strm,
                                         gz_headerp head))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved)
        return inflateGetHeader(strm, head);
    return ZWRAPD_finishWithErrorMsg(strm, "inflateGetHeader is not supported!");
}


ZEXTERN int ZEXPORT z_inflateBackInit_ OF((z_streamp strm, int windowBits,
                                         unsigned char FAR *window,
                                         const char *version,
                                         int stream_size))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved)
        return inflateBackInit_(strm, windowBits, window, version, stream_size);
    return ZWRAPD_finishWithErrorMsg(strm, "inflateBackInit is not supported!");
}


ZEXTERN int ZEXPORT z_inflateBack OF((z_streamp strm,
                                    in_func in, void FAR *in_desc,
                                    out_func out, void FAR *out_desc))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved)
        return inflateBack(strm, in, in_desc, out, out_desc);
    return ZWRAPD_finishWithErrorMsg(strm, "inflateBack is not supported!");
}


ZEXTERN int ZEXPORT z_inflateBackEnd OF((z_streamp strm))
{
    if (g_ZWRAPdecompressionType == ZWRAP_FORCE_ZLIB || !strm->reserved)
        return inflateBackEnd(strm);
    return ZWRAPD_finishWithErrorMsg(strm, "inflateBackEnd is not supported!");
}


ZEXTERN uLong ZEXPORT z_zlibCompileFlags OF((void)) { return zlibCompileFlags(); };



                    /* ===   utility functions  === */
#ifndef Z_SOLO

ZEXTERN int ZEXPORT z_compress OF((Bytef *dest,   uLongf *destLen,
                                 const Bytef *source, uLong sourceLen))
{
    if (!g_ZWRAP_useZSTDcompression)
        return compress(dest, destLen, source, sourceLen);

    {   size_t dstCapacity = *destLen;
        size_t const cSize = ZSTD_compress(dest, dstCapacity,
                                           source, sourceLen,
                                           ZWRAP_DEFAULT_CLEVEL);
        LOG_WRAPPERD("z_compress sourceLen=%d dstCapacity=%d\n",
                    (int)sourceLen, (int)dstCapacity);
        if (ZSTD_isError(cSize)) return Z_STREAM_ERROR;
        *destLen = cSize;
    }
    return Z_OK;
}


ZEXTERN int ZEXPORT z_compress2 OF((Bytef *dest,   uLongf *destLen,
                                  const Bytef *source, uLong sourceLen,
                                  int level))
{
    if (!g_ZWRAP_useZSTDcompression)
        return compress2(dest, destLen, source, sourceLen, level);

    { size_t dstCapacity = *destLen;
      size_t const cSize = ZSTD_compress(dest, dstCapacity, source, sourceLen, level);
      if (ZSTD_isError(cSize)) return Z_STREAM_ERROR;
      *destLen = cSize;
    }
    return Z_OK;
}


ZEXTERN uLong ZEXPORT z_compressBound OF((uLong sourceLen))
{
    if (!g_ZWRAP_useZSTDcompression)
        return compressBound(sourceLen);

    return ZSTD_compressBound(sourceLen);
}


ZEXTERN int ZEXPORT z_uncompress OF((Bytef *dest,   uLongf *destLen,
                                   const Bytef *source, uLong sourceLen))
{
    if (!ZSTD_isFrame(source, sourceLen))
        return uncompress(dest, destLen, source, sourceLen);

    { size_t dstCapacity = *destLen;
      size_t const dSize = ZSTD_decompress(dest, dstCapacity, source, sourceLen);
      if (ZSTD_isError(dSize)) return Z_STREAM_ERROR;
      *destLen = dSize;
     }
    return Z_OK;
}

#endif /* !Z_SOLO */


                        /* checksum functions */

ZEXTERN uLong ZEXPORT z_adler32 OF((uLong adler, const Bytef *buf, uInt len))
{
    return adler32(adler, buf, len);
}

ZEXTERN uLong ZEXPORT z_crc32   OF((uLong crc, const Bytef *buf, uInt len))
{
    return crc32(crc, buf, len);
}


#if ZLIB_VERNUM >= 0x12B0
ZEXTERN uLong ZEXPORT z_adler32_z OF((uLong adler, const Bytef *buf, z_size_t len))
{
    return adler32_z(adler, buf, len);
}

ZEXTERN uLong ZEXPORT z_crc32_z OF((uLong crc, const Bytef *buf, z_size_t len))
{
    return crc32_z(crc, buf, len);
}
#endif


#if ZLIB_VERNUM >= 0x1270
ZEXTERN const z_crc_t FAR * ZEXPORT z_get_crc_table    OF((void))
{
    return get_crc_table();
}
#endif
