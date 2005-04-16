/* infblock.h -- header to use infblock.c
 * Copyright (C) 1995-1998 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

#ifndef _INFBLOCK_H
#define _INFBLOCK_H

struct inflate_blocks_state;
typedef struct inflate_blocks_state inflate_blocks_statef;

extern inflate_blocks_statef * zlib_inflate_blocks_new (
    z_streamp z,
    check_func c,              /* check function */
    uInt w);                   /* window size */

extern int zlib_inflate_blocks (
    inflate_blocks_statef *,
    z_streamp ,
    int);                      /* initial return code */

extern void zlib_inflate_blocks_reset (
    inflate_blocks_statef *,
    z_streamp ,
    uLong *);                  /* check value on output */

extern int zlib_inflate_blocks_free (
    inflate_blocks_statef *,
    z_streamp);

extern void zlib_inflate_set_dictionary (
    inflate_blocks_statef *s,
    const Byte *d,  /* dictionary */
    uInt  n);       /* dictionary length */

extern int zlib_inflate_blocks_sync_point (
    inflate_blocks_statef *s);

#endif /* _INFBLOCK_H */
