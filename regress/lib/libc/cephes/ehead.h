/*	$OpenBSD: ehead.h,v 1.1 2011/07/02 18:11:01 martynas Exp $	*/

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

/* Include file for extended precision arithmetic programs.
 */

/* Number of 16 bit words in external x type format */
#define NE 10

/* Number of 16 bit words in internal format */
#define NI (NE+3)

/* Array offset to exponent */
#define E 1

/* Array offset to high guard word */
#define M 2

/* Number of bits of precision */
#define NBITS ((NI-4)*16)

/* Maximum number of decimal digits in ASCII conversion
 * = NBITS*log10(2)
 */
#define NDEC (NBITS*8/27)

/* The exponent of 1.0 */
#define EXONE (0x3fff)

void eadd(), esub(), emul(), ediv();
int ecmp(), enormlz(), eshift();
void eshup1(), eshup8(), eshup6(), eshdn1(), eshdn8(), eshdn6();
void eabs(), eneg(), emov(), eclear(), einfin(), efloor();
void eldexp(), efrexp(), eifrac(), ltoe();
void esqrt(), elog(), eexp(), etanh(), epow();
void asctoe(), asctoe24(), asctoe53(), asctoe64();
void etoasc(), e24toasc(), e53toasc(), e64toasc();
void etoe64(), etoe53(), etoe24(), e64toe(), e53toe(), e24toe();
int mtherr();
extern unsigned short ezero[], ehalf[], eone[], etwo[];
extern unsigned short elog2[], esqrt2[];


/* by Stephen L. Moshier. */
