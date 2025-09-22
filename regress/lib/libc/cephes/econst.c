/*	$OpenBSD: econst.c,v 1.1 2011/07/02 18:11:01 martynas Exp $	*/

/*
 * Copyright (c) 2008 Stephen L. Moshier <steve@moshier.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*							econst.c	*/
/*  e type constants used by high precision check routines */

#include "ehead.h"


#if NE == 10
/* 0.0 */
unsigned short ezero[NE] =
 {0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,};

/* 5.0E-1 */
unsigned short ehalf[NE] =
 {0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x8000, 0x3ffe,};

/* 1.0E0 */
unsigned short eone[NE] =
 {0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x8000, 0x3fff,};

/* 2.0E0 */
unsigned short etwo[NE] =
 {0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x8000, 0x4000,};

/* 3.2E1 */
unsigned short e32[NE] =
 {0x0000, 0x0000, 0x0000, 0x0000,
  0x0000, 0x0000, 0x0000, 0x0000, 0x8000, 0x4004,};

/* 6.93147180559945309417232121458176568075500134360255E-1 */
unsigned short elog2[NE] =
 {0x40f3, 0xf6af, 0x03f2, 0xb398,
  0xc9e3, 0x79ab, 0150717, 0013767, 0130562, 0x3ffe,};

/* 1.41421356237309504880168872420969807856967187537695E0 */
unsigned short esqrt2[NE] =
 {0x1d6f, 0xbe9f, 0x754a, 0x89b3,
  0x597d, 0x6484, 0174736, 0171463, 0132404, 0x3fff,};

/* 3.14159265358979323846264338327950288419716939937511E0 */
unsigned short epi[NE] =
 {0x2902, 0x1cd1, 0x80dc, 0x628b,
  0xc4c6, 0xc234, 0020550, 0155242, 0144417, 0040000,};
  
/* 5.7721566490153286060651209008240243104215933593992E-1 */
unsigned short eeul[NE] = {
0xd1be,0xc7a4,0076660,0063743,0111704,0x3ffe,};

#else

/* 0.0 */
unsigned short ezero[NE] = {
0, 0000000,0000000,0000000,0000000,0000000,};
/* 5.0E-1 */
unsigned short ehalf[NE] = {
0, 0000000,0000000,0000000,0100000,0x3ffe,};
/* 1.0E0 */
unsigned short eone[NE] = {
0, 0000000,0000000,0000000,0100000,0x3fff,};
/* 2.0E0 */
unsigned short etwo[NE] = {
0, 0000000,0000000,0000000,0100000,0040000,};
/* 3.2E1 */
unsigned short e32[NE] = {
0, 0000000,0000000,0000000,0100000,0040004,};
/* 6.93147180559945309417232121458176568075500134360255E-1 */
unsigned short elog2[NE] = {
0xc9e4,0x79ab,0150717,0013767,0130562,0x3ffe,};
/* 1.41421356237309504880168872420969807856967187537695E0 */
unsigned short esqrt2[NE] = {
0x597e,0x6484,0174736,0171463,0132404,0x3fff,};
/* 2/sqrt(PI) =
 * 1.12837916709551257389615890312154517168810125865800E0 */
unsigned short eoneopi[NE] = {
0x71d5,0x688d,0012333,0135202,0110156,0x3fff,};
/* 3.14159265358979323846264338327950288419716939937511E0 */
unsigned short epi[NE] = {
0xc4c6,0xc234,0020550,0155242,0144417,0040000,};
/* 5.7721566490153286060651209008240243104215933593992E-1 */
unsigned short eeul[NE] = {
0xd1be,0xc7a4,0076660,0063743,0111704,0x3ffe,};
#endif
extern unsigned short ezero[];
extern unsigned short ehalf[];
extern unsigned short eone[];
extern unsigned short etwo[];
extern unsigned short e32[];
extern unsigned short elog2[];
extern unsigned short esqrt2[];
extern unsigned short eoneopi[];
extern unsigned short epi[];
extern unsigned short eeul[];

