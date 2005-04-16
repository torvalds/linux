/* infcodes.h -- header to use infcodes.c
 * Copyright (C) 1995-1998 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

#ifndef _INFCODES_H
#define _INFCODES_H

#include "infblock.h"

struct inflate_codes_state;
typedef struct inflate_codes_state inflate_codes_statef;

extern inflate_codes_statef *zlib_inflate_codes_new (
    uInt, uInt,
    inflate_huft *, inflate_huft *,
    z_streamp );

extern int zlib_inflate_codes (
    inflate_blocks_statef *,
    z_streamp ,
    int);

extern void zlib_inflate_codes_free (
    inflate_codes_statef *,
    z_streamp );

#endif /* _INFCODES_H */
