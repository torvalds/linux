/* inftrees.h -- header to use inftrees.c
 * Copyright (C) 1995-1998 Mark Adler
 * For conditions of distribution and use, see copyright notice in zlib.h 
 */

/* WARNING: this file should *not* be used by applications. It is
   part of the implementation of the compression library and is
   subject to change. Applications should only use zlib.h.
 */

/* Huffman code lookup table entry--this entry is four bytes for machines
   that have 16-bit pointers (e.g. PC's in the small or medium model). */

#ifndef _INFTREES_H
#define _INFTREES_H

typedef struct inflate_huft_s inflate_huft;

struct inflate_huft_s {
  union {
    struct {
      Byte Exop;        /* number of extra bits or operation */
      Byte Bits;        /* number of bits in this code or subcode */
    } what;
    uInt pad;           /* pad structure to a power of 2 (4 bytes for */
  } word;               /*  16-bit, 8 bytes for 32-bit int's) */
  uInt base;            /* literal, length base, distance base,
                           or table offset */
};

/* Maximum size of dynamic tree.  The maximum found in a long but non-
   exhaustive search was 1004 huft structures (850 for length/literals
   and 154 for distances, the latter actually the result of an
   exhaustive search).  The actual maximum is not known, but the
   value below is more than safe. */
#define MANY 1440

extern int zlib_inflate_trees_bits (
    uInt *,                     /* 19 code lengths */
    uInt *,                     /* bits tree desired/actual depth */
    inflate_huft **,            /* bits tree result */
    inflate_huft *,             /* space for trees */
    z_streamp);                 /* for messages */

extern int zlib_inflate_trees_dynamic (
    uInt,                       /* number of literal/length codes */
    uInt,                       /* number of distance codes */
    uInt *,                     /* that many (total) code lengths */
    uInt *,                     /* literal desired/actual bit depth */
    uInt *,                     /* distance desired/actual bit depth */
    inflate_huft **,            /* literal/length tree result */
    inflate_huft **,            /* distance tree result */
    inflate_huft *,             /* space for trees */
    z_streamp);                 /* for messages */

extern int zlib_inflate_trees_fixed (
    uInt *,                     /* literal desired/actual bit depth */
    uInt *,                     /* distance desired/actual bit depth */
    inflate_huft **,            /* literal/length tree result */
    inflate_huft **,            /* distance tree result */
    inflate_huft *,             /* space for trees */
    z_streamp);                 /* for memory allocation */

#endif /* _INFTREES_H */
