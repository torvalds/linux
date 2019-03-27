/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_LEGACY_H
#define ZSTD_LEGACY_H

#if defined (__cplusplus)
extern "C" {
#endif

/* *************************************
*  Includes
***************************************/
#include "mem.h"            /* MEM_STATIC */
#include "error_private.h"  /* ERROR */
#include "zstd.h"           /* ZSTD_inBuffer, ZSTD_outBuffer */

#if !defined (ZSTD_LEGACY_SUPPORT) || (ZSTD_LEGACY_SUPPORT == 0)
#  undef ZSTD_LEGACY_SUPPORT
#  define ZSTD_LEGACY_SUPPORT 8
#endif

#if (ZSTD_LEGACY_SUPPORT <= 1)
#  include "zstd_v01.h"
#endif
#if (ZSTD_LEGACY_SUPPORT <= 2)
#  include "zstd_v02.h"
#endif
#if (ZSTD_LEGACY_SUPPORT <= 3)
#  include "zstd_v03.h"
#endif
#if (ZSTD_LEGACY_SUPPORT <= 4)
#  include "zstd_v04.h"
#endif
#if (ZSTD_LEGACY_SUPPORT <= 5)
#  include "zstd_v05.h"
#endif
#if (ZSTD_LEGACY_SUPPORT <= 6)
#  include "zstd_v06.h"
#endif
#if (ZSTD_LEGACY_SUPPORT <= 7)
#  include "zstd_v07.h"
#endif

/** ZSTD_isLegacy() :
    @return : > 0 if supported by legacy decoder. 0 otherwise.
              return value is the version.
*/
MEM_STATIC unsigned ZSTD_isLegacy(const void* src, size_t srcSize)
{
    U32 magicNumberLE;
    if (srcSize<4) return 0;
    magicNumberLE = MEM_readLE32(src);
    switch(magicNumberLE)
    {
#if (ZSTD_LEGACY_SUPPORT <= 1)
        case ZSTDv01_magicNumberLE:return 1;
#endif
#if (ZSTD_LEGACY_SUPPORT <= 2)
        case ZSTDv02_magicNumber : return 2;
#endif
#if (ZSTD_LEGACY_SUPPORT <= 3)
        case ZSTDv03_magicNumber : return 3;
#endif
#if (ZSTD_LEGACY_SUPPORT <= 4)
        case ZSTDv04_magicNumber : return 4;
#endif
#if (ZSTD_LEGACY_SUPPORT <= 5)
        case ZSTDv05_MAGICNUMBER : return 5;
#endif
#if (ZSTD_LEGACY_SUPPORT <= 6)
        case ZSTDv06_MAGICNUMBER : return 6;
#endif
#if (ZSTD_LEGACY_SUPPORT <= 7)
        case ZSTDv07_MAGICNUMBER : return 7;
#endif
        default : return 0;
    }
}


MEM_STATIC unsigned long long ZSTD_getDecompressedSize_legacy(const void* src, size_t srcSize)
{
    U32 const version = ZSTD_isLegacy(src, srcSize);
    if (version < 5) return 0;  /* no decompressed size in frame header, or not a legacy format */
#if (ZSTD_LEGACY_SUPPORT <= 5)
    if (version==5) {
        ZSTDv05_parameters fParams;
        size_t const frResult = ZSTDv05_getFrameParams(&fParams, src, srcSize);
        if (frResult != 0) return 0;
        return fParams.srcSize;
    }
#endif
#if (ZSTD_LEGACY_SUPPORT <= 6)
    if (version==6) {
        ZSTDv06_frameParams fParams;
        size_t const frResult = ZSTDv06_getFrameParams(&fParams, src, srcSize);
        if (frResult != 0) return 0;
        return fParams.frameContentSize;
    }
#endif
#if (ZSTD_LEGACY_SUPPORT <= 7)
    if (version==7) {
        ZSTDv07_frameParams fParams;
        size_t const frResult = ZSTDv07_getFrameParams(&fParams, src, srcSize);
        if (frResult != 0) return 0;
        return fParams.frameContentSize;
    }
#endif
    return 0;   /* should not be possible */
}


MEM_STATIC size_t ZSTD_decompressLegacy(
                     void* dst, size_t dstCapacity,
               const void* src, size_t compressedSize,
               const void* dict,size_t dictSize)
{
    U32 const version = ZSTD_isLegacy(src, compressedSize);
    (void)dst; (void)dstCapacity; (void)dict; (void)dictSize;  /* unused when ZSTD_LEGACY_SUPPORT >= 8 */
    switch(version)
    {
#if (ZSTD_LEGACY_SUPPORT <= 1)
        case 1 :
            return ZSTDv01_decompress(dst, dstCapacity, src, compressedSize);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 2)
        case 2 :
            return ZSTDv02_decompress(dst, dstCapacity, src, compressedSize);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 3)
        case 3 :
            return ZSTDv03_decompress(dst, dstCapacity, src, compressedSize);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 4)
        case 4 :
            return ZSTDv04_decompress(dst, dstCapacity, src, compressedSize);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 5)
        case 5 :
            {   size_t result;
                ZSTDv05_DCtx* const zd = ZSTDv05_createDCtx();
                if (zd==NULL) return ERROR(memory_allocation);
                result = ZSTDv05_decompress_usingDict(zd, dst, dstCapacity, src, compressedSize, dict, dictSize);
                ZSTDv05_freeDCtx(zd);
                return result;
            }
#endif
#if (ZSTD_LEGACY_SUPPORT <= 6)
        case 6 :
            {   size_t result;
                ZSTDv06_DCtx* const zd = ZSTDv06_createDCtx();
                if (zd==NULL) return ERROR(memory_allocation);
                result = ZSTDv06_decompress_usingDict(zd, dst, dstCapacity, src, compressedSize, dict, dictSize);
                ZSTDv06_freeDCtx(zd);
                return result;
            }
#endif
#if (ZSTD_LEGACY_SUPPORT <= 7)
        case 7 :
            {   size_t result;
                ZSTDv07_DCtx* const zd = ZSTDv07_createDCtx();
                if (zd==NULL) return ERROR(memory_allocation);
                result = ZSTDv07_decompress_usingDict(zd, dst, dstCapacity, src, compressedSize, dict, dictSize);
                ZSTDv07_freeDCtx(zd);
                return result;
            }
#endif
        default :
            return ERROR(prefix_unknown);
    }
}

MEM_STATIC size_t ZSTD_findFrameCompressedSizeLegacy(const void *src,
                                             size_t compressedSize)
{
    U32 const version = ZSTD_isLegacy(src, compressedSize);
    switch(version)
    {
#if (ZSTD_LEGACY_SUPPORT <= 1)
        case 1 :
            return ZSTDv01_findFrameCompressedSize(src, compressedSize);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 2)
        case 2 :
            return ZSTDv02_findFrameCompressedSize(src, compressedSize);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 3)
        case 3 :
            return ZSTDv03_findFrameCompressedSize(src, compressedSize);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 4)
        case 4 :
            return ZSTDv04_findFrameCompressedSize(src, compressedSize);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 5)
        case 5 :
            return ZSTDv05_findFrameCompressedSize(src, compressedSize);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 6)
        case 6 :
            return ZSTDv06_findFrameCompressedSize(src, compressedSize);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 7)
        case 7 :
            return ZSTDv07_findFrameCompressedSize(src, compressedSize);
#endif
        default :
            return ERROR(prefix_unknown);
    }
}

MEM_STATIC size_t ZSTD_freeLegacyStreamContext(void* legacyContext, U32 version)
{
    switch(version)
    {
        default :
        case 1 :
        case 2 :
        case 3 :
            (void)legacyContext;
            return ERROR(version_unsupported);
#if (ZSTD_LEGACY_SUPPORT <= 4)
        case 4 : return ZBUFFv04_freeDCtx((ZBUFFv04_DCtx*)legacyContext);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 5)
        case 5 : return ZBUFFv05_freeDCtx((ZBUFFv05_DCtx*)legacyContext);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 6)
        case 6 : return ZBUFFv06_freeDCtx((ZBUFFv06_DCtx*)legacyContext);
#endif
#if (ZSTD_LEGACY_SUPPORT <= 7)
        case 7 : return ZBUFFv07_freeDCtx((ZBUFFv07_DCtx*)legacyContext);
#endif
    }
}


MEM_STATIC size_t ZSTD_initLegacyStream(void** legacyContext, U32 prevVersion, U32 newVersion,
                                        const void* dict, size_t dictSize)
{
    DEBUGLOG(5, "ZSTD_initLegacyStream for v0.%u", newVersion);
    if (prevVersion != newVersion) ZSTD_freeLegacyStreamContext(*legacyContext, prevVersion);
    switch(newVersion)
    {
        default :
        case 1 :
        case 2 :
        case 3 :
            (void)dict; (void)dictSize;
            return 0;
#if (ZSTD_LEGACY_SUPPORT <= 4)
        case 4 :
        {
            ZBUFFv04_DCtx* dctx = (prevVersion != newVersion) ? ZBUFFv04_createDCtx() : (ZBUFFv04_DCtx*)*legacyContext;
            if (dctx==NULL) return ERROR(memory_allocation);
            ZBUFFv04_decompressInit(dctx);
            ZBUFFv04_decompressWithDictionary(dctx, dict, dictSize);
            *legacyContext = dctx;
            return 0;
        }
#endif
#if (ZSTD_LEGACY_SUPPORT <= 5)
        case 5 :
        {
            ZBUFFv05_DCtx* dctx = (prevVersion != newVersion) ? ZBUFFv05_createDCtx() : (ZBUFFv05_DCtx*)*legacyContext;
            if (dctx==NULL) return ERROR(memory_allocation);
            ZBUFFv05_decompressInitDictionary(dctx, dict, dictSize);
            *legacyContext = dctx;
            return 0;
        }
#endif
#if (ZSTD_LEGACY_SUPPORT <= 6)
        case 6 :
        {
            ZBUFFv06_DCtx* dctx = (prevVersion != newVersion) ? ZBUFFv06_createDCtx() : (ZBUFFv06_DCtx*)*legacyContext;
            if (dctx==NULL) return ERROR(memory_allocation);
            ZBUFFv06_decompressInitDictionary(dctx, dict, dictSize);
            *legacyContext = dctx;
            return 0;
        }
#endif
#if (ZSTD_LEGACY_SUPPORT <= 7)
        case 7 :
        {
            ZBUFFv07_DCtx* dctx = (prevVersion != newVersion) ? ZBUFFv07_createDCtx() : (ZBUFFv07_DCtx*)*legacyContext;
            if (dctx==NULL) return ERROR(memory_allocation);
            ZBUFFv07_decompressInitDictionary(dctx, dict, dictSize);
            *legacyContext = dctx;
            return 0;
        }
#endif
    }
}



MEM_STATIC size_t ZSTD_decompressLegacyStream(void* legacyContext, U32 version,
                                              ZSTD_outBuffer* output, ZSTD_inBuffer* input)
{
    DEBUGLOG(5, "ZSTD_decompressLegacyStream for v0.%u", version);
    switch(version)
    {
        default :
        case 1 :
        case 2 :
        case 3 :
            (void)legacyContext; (void)output; (void)input;
            return ERROR(version_unsupported);
#if (ZSTD_LEGACY_SUPPORT <= 4)
        case 4 :
            {
                ZBUFFv04_DCtx* dctx = (ZBUFFv04_DCtx*) legacyContext;
                const void* src = (const char*)input->src + input->pos;
                size_t readSize = input->size - input->pos;
                void* dst = (char*)output->dst + output->pos;
                size_t decodedSize = output->size - output->pos;
                size_t const hintSize = ZBUFFv04_decompressContinue(dctx, dst, &decodedSize, src, &readSize);
                output->pos += decodedSize;
                input->pos += readSize;
                return hintSize;
            }
#endif
#if (ZSTD_LEGACY_SUPPORT <= 5)
        case 5 :
            {
                ZBUFFv05_DCtx* dctx = (ZBUFFv05_DCtx*) legacyContext;
                const void* src = (const char*)input->src + input->pos;
                size_t readSize = input->size - input->pos;
                void* dst = (char*)output->dst + output->pos;
                size_t decodedSize = output->size - output->pos;
                size_t const hintSize = ZBUFFv05_decompressContinue(dctx, dst, &decodedSize, src, &readSize);
                output->pos += decodedSize;
                input->pos += readSize;
                return hintSize;
            }
#endif
#if (ZSTD_LEGACY_SUPPORT <= 6)
        case 6 :
            {
                ZBUFFv06_DCtx* dctx = (ZBUFFv06_DCtx*) legacyContext;
                const void* src = (const char*)input->src + input->pos;
                size_t readSize = input->size - input->pos;
                void* dst = (char*)output->dst + output->pos;
                size_t decodedSize = output->size - output->pos;
                size_t const hintSize = ZBUFFv06_decompressContinue(dctx, dst, &decodedSize, src, &readSize);
                output->pos += decodedSize;
                input->pos += readSize;
                return hintSize;
            }
#endif
#if (ZSTD_LEGACY_SUPPORT <= 7)
        case 7 :
            {
                ZBUFFv07_DCtx* dctx = (ZBUFFv07_DCtx*) legacyContext;
                const void* src = (const char*)input->src + input->pos;
                size_t readSize = input->size - input->pos;
                void* dst = (char*)output->dst + output->pos;
                size_t decodedSize = output->size - output->pos;
                size_t const hintSize = ZBUFFv07_decompressContinue(dctx, dst, &decodedSize, src, &readSize);
                output->pos += decodedSize;
                input->pos += readSize;
                return hintSize;
            }
#endif
    }
}


#if defined (__cplusplus)
}
#endif

#endif   /* ZSTD_LEGACY_H */
