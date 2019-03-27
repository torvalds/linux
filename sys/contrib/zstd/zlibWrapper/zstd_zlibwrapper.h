/*
 * Copyright (c) 2016-present, Przemyslaw Skibinski, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_ZLIBWRAPPER_H
#define ZSTD_ZLIBWRAPPER_H

#if defined (__cplusplus)
extern "C" {
#endif


#define ZLIB_CONST
#define Z_PREFIX
#define ZLIB_INTERNAL   /* disables gz*64 functions but fixes zlib 1.2.4 with Z_PREFIX */
#include <zlib.h>

#if !defined(z_const)
    #define z_const
#endif


/* returns a string with version of zstd library */
const char * zstdVersion(void);


/*** COMPRESSION ***/
/* ZWRAP_useZSTDcompression() enables/disables zstd compression during runtime.
   By default zstd compression is disabled. To enable zstd compression please use one of the methods:
   - compilation with the additional option -DZWRAP_USE_ZSTD=1
   - using '#define ZWRAP_USE_ZSTD 1' in source code before '#include "zstd_zlibwrapper.h"'
   - calling ZWRAP_useZSTDcompression(1)
   All above-mentioned methods will enable zstd compression for all threads.
   Be aware that ZWRAP_useZSTDcompression() is not thread-safe and may lead to a race condition. */
void ZWRAP_useZSTDcompression(int turn_on);

/* checks if zstd compression is turned on */
int ZWRAP_isUsingZSTDcompression(void);

/* Changes a pledged source size for a given compression stream.
   It will change ZSTD compression parameters what may improve compression speed and/or ratio.
   The function should be called just after deflateInit() or deflateReset() and before deflate() or deflateSetDictionary().
   It's only helpful when data is compressed in blocks.
   There will be no change in case of deflateInit() or deflateReset() immediately followed by deflate(strm, Z_FINISH)
   as this case is automatically detected.  */
int ZWRAP_setPledgedSrcSize(z_streamp strm, unsigned long long pledgedSrcSize);

/* Similar to deflateReset but preserves dictionary set using deflateSetDictionary.
   It should improve compression speed because there will be less calls to deflateSetDictionary
   When using zlib compression this method redirects to deflateReset. */
int ZWRAP_deflateReset_keepDict(z_streamp strm);



/*** DECOMPRESSION ***/
typedef enum { ZWRAP_FORCE_ZLIB, ZWRAP_AUTO } ZWRAP_decompress_type;

/* ZWRAP_setDecompressionType() enables/disables automatic recognition of zstd/zlib compressed data during runtime.
   By default auto-detection of zstd and zlib streams in enabled (ZWRAP_AUTO).
   Forcing zlib decompression with ZWRAP_setDecompressionType(ZWRAP_FORCE_ZLIB) slightly improves
   decompression speed of zlib-encoded streams.
   Be aware that ZWRAP_setDecompressionType() is not thread-safe and may lead to a race condition. */
void ZWRAP_setDecompressionType(ZWRAP_decompress_type type);

/* checks zstd decompression type */
ZWRAP_decompress_type ZWRAP_getDecompressionType(void);

/* Checks if zstd decompression is used for a given stream.
   If will return 1 only when inflate() was called and zstd header was detected. */
int ZWRAP_isUsingZSTDdecompression(z_streamp strm);

/* Similar to inflateReset but preserves dictionary set using inflateSetDictionary.
   inflate() will return Z_NEED_DICT only for the first time what will improve decompression speed.
   For zlib streams this method redirects to inflateReset. */
int ZWRAP_inflateReset_keepDict(z_streamp strm);


#if defined (__cplusplus)
}
#endif

#endif /* ZSTD_ZLIBWRAPPER_H */
