/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2005 John Bicket
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 */
#ifndef	__ATH_RATE_SAMPLE_TXSCHEDULES_H__
#define	__ATH_RATE_SAMPLE_TXSCHEDULES_H__

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#define A(_r) \
    (((_r) == 6)   ? 0 : (((_r) == 9)   ? 1 : (((_r) == 12)  ? 2 : \
    (((_r) == 18)  ? 3 : (((_r) == 24)  ? 4 : (((_r) == 36)  ? 5 : \
    (((_r) == 48)  ? 6 : (((_r) == 54)  ? 7 : 0))))))))
static const struct txschedule series_11a[] = {
	{ 3,A( 6), 3,A(  6), 0,A(  6), 0,A( 6) },	/*   6Mb/s */
	{ 4,A( 9), 3,A(  6), 4,A(  6), 0,A( 6) },	/*   9Mb/s */
	{ 4,A(12), 3,A(  6), 4,A(  6), 0,A( 6) },	/*  12Mb/s */
	{ 4,A(18), 3,A( 12), 4,A(  6), 2,A( 6) },	/*  18Mb/s */
	{ 4,A(24), 3,A( 18), 4,A( 12), 2,A( 6) },	/*  24Mb/s */
	{ 4,A(36), 3,A( 24), 4,A( 18), 2,A( 6) },	/*  36Mb/s */
	{ 4,A(48), 3,A( 36), 4,A( 24), 2,A(12) },	/*  48Mb/s */
	{ 4,A(54), 3,A( 48), 4,A( 36), 2,A(24) }	/*  54Mb/s */
};

#define NA1(_r) \
	(((_r) == 6.5)  ? 8 : (((_r) == 13)  ?  9 : (((_r) == 19.5)? 10 : \
	(((_r) == 26)  ? 11 : (((_r) == 39)  ? 12 : (((_r) == 52)  ? 13 : \
	(((_r) == 58.5)? 14 : (((_r) == 65)  ? 15 : 0))))))))
#define NA2(_r) \
	(((_r) == 13) ? 16 : (((_r) == 26) ? 17 : (((_r) == 39) ? 18 : \
	(((_r) == 52) ? 19 : (((_r) == 78) ? 20 : (((_r) == 104)? 21 : \
	(((_r) == 117)? 22 : (((_r) == 130)? 23 : 0))))))))
#define NA3(_r) \
	(((_r) == 19.5)  ? 24 : (((_r) == 39) ? 25 : (((_r) == 58.5)  ? 26 : \
	(((_r) == 78)  ? 27 : (((_r) == 117) ? 28 : (((_r) == 156) ? 29 : \
	(((_r) == 175.5) ? 30 : (((_r) == 195)? 31 : 0))))))))
static const struct txschedule series_11na[] = {
	{ 3,A( 6), 3,A(  6), 0,A(  6), 0,A( 6) },       /*   6Mb/s */
	{ 4,A( 9), 3,A(  6), 4,A(  6), 0,A( 6) },       /*   9Mb/s */
	{ 4,A(12), 3,A(  6), 4,A(  6), 0,A( 6) },       /*  12Mb/s */
	{ 4,A(18), 3,A( 12), 4,A(  6), 2,A( 6) },       /*  18Mb/s */
	{ 4,A(24), 3,A( 18), 4,A( 12), 2,A( 6) },       /*  24Mb/s */
	{ 4,A(36), 3,A( 24), 4,A( 18), 2,A( 6) },       /*  36Mb/s */
	{ 4,A(48), 3,A( 36), 4,A( 24), 2,A(12) },       /*  48Mb/s */
	{ 4,A(54), 3,A( 48), 4,A( 36), 2,A(24) },       /*  54Mb/s */

	/* 1 stream rates */

	{ 3,NA1( 6.5), 3,NA1( 6.5), 0,NA1( 6.5), 0,NA1(6.5) },  /* 6.5Mb/s */
	{ 4,NA1(  13), 3,NA1( 6.5), 4,NA1( 6.5), 0,NA1(6.5) },  /*  13Mb/s */
	{ 4,NA1(19.5), 3,NA1( 6.5), 4,NA1( 6.5), 0,NA1(6.5) },  /*19.5Mb/s */
	{ 4,NA1(  26), 3,NA1(19.5), 4,NA1( 6.5), 2,NA1(6.5) },  /*  26Mb/s */
	{ 4,NA1(  39), 3,NA1(  26), 4,NA1(19.5), 2,NA1(6.5) },  /*  39Mb/s */
	{ 4,NA1(  52), 3,NA1(  39), 4,NA1(  26), 2,NA1(6.5) },  /*  52Mb/s */
	{ 4,NA1(58.5), 3,NA1(  52), 4,NA1(  39), 2,NA1( 13) },  /*58.5Mb/s */
	{ 4,NA1(  65), 3,NA1(58.5), 4,NA1(  52), 2,NA1( 13) },  /*  65Mb/s */

	/* 2 stream rates */

	{ 3,NA2(  13), 3,NA2(  13), 0,NA2(  13), 0,NA2( 13) },  /*  13Mb/s */
	{ 4,NA2(  26), 3,NA2(  13), 4,NA2(  13), 0,NA2( 13) },  /*  26Mb/s */
	{ 4,NA2(  39), 3,NA2(  26), 4,NA2(  13), 2,NA2( 13) },  /*  39Mb/s */
	{ 4,NA2(  52), 3,NA2(  39), 4,NA2(  26), 2,NA2( 13) },  /*  52Mb/s */
	{ 4,NA2(  78), 3,NA2(  52), 4,NA2(  39), 2,NA2( 13) },  /*  78Mb/s */
	{ 4,NA2( 104), 3,NA2(  78), 4,NA2(  52), 2,NA2( 13) },  /* 104Mb/s */
	{ 4,NA2( 117), 3,NA2( 104), 4,NA2(  78), 2,NA2( 26) },  /* 117Mb/s */
	{ 4,NA2( 130), 3,NA2( 117), 4,NA2( 104), 2,NA2( 26) },   /* 130Mb/s */

	/* 3 stream rates */

	{ 3,NA3(19.5), 3,NA3(19.5), 0,NA3(19.5), 0,NA3(19.5) },  /*  19Mb/s */
	{ 3,NA3(  39), 3,NA3(19.5), 0,NA3(19.5), 0,NA3(19.5) },  /*  39Mb/s */
	{ 3,NA3(58.5), 3,NA3(  39), 0,NA3(19.5), 0,NA3(19.5) },  /*  58Mb/s */
	{ 3,NA3(  78), 3,NA3(58.5), 0,NA3(  39), 0,NA3(19.5) },  /*  78Mb/s */
	{ 3,NA3( 117), 3,NA3(  78), 0,NA3(58.5), 0,NA3(19.5) },  /* 117Mb/s */
	{ 3,NA3( 156), 3,NA3( 117), 0,NA3(  78), 0,NA3(19.5) },  /*  156Mb/s */
	{ 3,NA3(175.5), 3,NA3( 156), 0,NA3( 117), 0,NA3(  39) },  /*  175Mb/s */
	{ 3,NA3( 195), 3,NA3( 195), 0,NA3( 156), 0,NA3(58.5) },  /* 195Mb/s */
};
#undef A
#undef NA3
#undef NA2
#undef NA1

#define G(_r) \
    (((_r) == 1)   ? 0 : (((_r) == 2)   ? 1 : (((_r) == 5.5) ? 2 : \
    (((_r) == 11)  ? 3 : (((_r) == 6)   ? 4 : (((_r) == 9)   ? 5 : \
    (((_r) == 12)  ? 6 : (((_r) == 18)  ? 7 : (((_r) == 24)  ? 8 : \
    (((_r) == 36)  ? 9 : (((_r) == 48)  ? 10 : (((_r) == 54)  ? 11 : 0))))))))))))
static const struct txschedule series_11g[] = {
	{ 3,G( 1), 3,G(  1), 0,G(  1), 0,G( 1) },	/*   1Mb/s */
	{ 4,G( 2), 3,G(  1), 4,G(  1), 0,G( 1) },	/*   2Mb/s */
	{ 4,G(5.5),3,G(  2), 4,G(  1), 2,G( 1) },	/* 5.5Mb/s */
	{ 4,G(11), 3,G(5.5), 4,G(  2), 2,G( 1) },	/*  11Mb/s */
	{ 4,G( 6), 3,G(5.5), 4,G(  2), 2,G( 1) },	/*   6Mb/s */
	{ 4,G( 9), 3,G(  6), 4,G(5.5), 2,G( 1) },	/*   9Mb/s */
	{ 4,G(12), 3,G( 11), 4,G(5.5), 2,G( 1) },	/*  12Mb/s */
	{ 4,G(18), 3,G( 12), 4,G( 11), 2,G( 1) },	/*  18Mb/s */
	{ 4,G(24), 3,G( 18), 4,G( 12), 2,G( 1) },	/*  24Mb/s */
	{ 4,G(36), 3,G( 24), 4,G( 18), 2,G( 1) },	/*  36Mb/s */
	{ 4,G(48), 3,G( 36), 4,G( 24), 2,G( 1) },	/*  48Mb/s */
	{ 4,G(54), 3,G( 48), 4,G( 36), 2,G( 1) }	/*  54Mb/s */
};

#define NG1(_r) \
	(((_r) == 6.5) ? 12 : (((_r) == 13) ? 13 : (((_r) == 19.5)? 14 : \
	(((_r) == 26)  ? 15 : (((_r) == 39) ? 16 : (((_r) == 52)  ? 17 : \
	(((_r) == 58.5)? 18 : (((_r) == 65) ? 19 : 0))))))))
#define NG2(_r) \
	(((_r) == 13)  ? 20 : (((_r) == 26) ? 21 : (((_r) == 39)  ? 22 : \
	(((_r) == 52)  ? 23 : (((_r) == 78) ? 24 : (((_r) == 104) ? 25 : \
	(((_r) == 117) ? 26 : (((_r) == 130)? 27 : 0))))))))
#define NG3(_r) \
	(((_r) == 19.5)  ? 28 : (((_r) == 39) ? 29 : (((_r) == 58.5)  ? 30 : \
	(((_r) == 78)  ? 31 : (((_r) == 117) ? 32 : (((_r) == 156) ? 33 : \
	(((_r) == 175.5) ? 34 : (((_r) == 195)? 35 : 0))))))))

static const struct txschedule series_11ng[] = {
	{ 3,G( 1), 3,G(  1), 0,G(  1), 0,G( 1) },       /*   1Mb/s */
	{ 4,G( 2), 3,G(  1), 4,G(  1), 0,G( 1) },       /*   2Mb/s */
	{ 4,G(5.5),3,G(  2), 4,G(  1), 2,G( 1) },       /* 5.5Mb/s */
	{ 4,G(11), 3,G(5.5), 4,G(  2), 2,G( 1) },       /*  11Mb/s */
	{ 4,G( 6), 3,G(5.5), 4,G(  2), 2,G( 1) },       /*   6Mb/s */
	{ 4,G( 9), 3,G(  6), 4,G(5.5), 2,G( 1) },       /*   9Mb/s */
	{ 4,G(12), 3,G( 11), 4,G(5.5), 2,G( 1) },       /*  12Mb/s */
	{ 4,G(18), 3,G( 12), 4,G( 11), 2,G( 1) },       /*  18Mb/s */
	{ 4,G(24), 3,G( 18), 4,G( 12), 2,G( 1) },       /*  24Mb/s */
	{ 4,G(36), 3,G( 24), 4,G( 18), 2,G( 1) },       /*  36Mb/s */
	{ 4,G(48), 3,G( 36), 4,G( 24), 2,G( 1) },       /*  48Mb/s */
	{ 4,G(54), 3,G( 48), 4,G( 36), 2,G( 1) },       /*  54Mb/s */

	/* 1 stream rates */

	{ 3,NG1( 6.5), 3,NG1( 6.5), 0,NG1( 6.5), 0,NG1(6.5) },  /* 6.5Mb/s */
	{ 4,NG1(  13), 3,NG1( 6.5), 4,NG1( 6.5), 0,NG1(6.5) },  /*  13Mb/s */
	{ 4,NG1(19.5), 3,NG1( 6.5), 4,NG1( 6.5), 0,NG1(6.5) },  /*19.5Mb/s */
	{ 4,NG1(  26), 3,NG1(19.5), 4,NG1( 6.5), 2,NG1(6.5) },  /*  26Mb/s */
	{ 4,NG1(  39), 3,NG1(  26), 4,NG1(19.5), 2,NG1(6.5) },  /*  39Mb/s */
	{ 4,NG1(  52), 3,NG1(  39), 4,NG1(  26), 2,NG1(6.5) },  /*  52Mb/s */
	{ 4,NG1(58.5), 3,NG1(  52), 4,NG1(  39), 2,NG1( 13) },  /*58.5Mb/s */
	{ 4,NG1(  65), 3,NG1(58.5), 4,NG1(  52), 2,NG1( 13) },  /*  65Mb/s */

	/* 2 stream rates */

	{ 3,NG2(  13), 3,NG2(  13), 0,NG2(  13), 0,NG2( 13) },  /*  13Mb/s */
	{ 4,NG2(  26), 3,NG2(  13), 4,NG2(  13), 0,NG2( 13) },  /*  26Mb/s */
	{ 4,NG2(  39), 3,NG2(  26), 4,NG2(  13), 2,NG2( 13) },  /*  39Mb/s */
	{ 4,NG2(  52), 3,NG2(  39), 4,NG2(  26), 2,NG2( 13) },  /*  52Mb/s */
	{ 4,NG2(  78), 3,NG2(  52), 4,NG2(  39), 2,NG2( 13) },  /*  78Mb/s */
	{ 4,NG2( 104), 3,NG2(  78), 4,NG2(  52), 2,NG2( 13) },  /* 104Mb/s */
	{ 4,NG2( 117), 3,NG2( 104), 4,NG2(  78), 2,NG2( 26) },  /* 117Mb/s */
	{ 4,NG2( 130), 3,NG2( 117), 4,NG2( 104), 2,NG2( 26) },  /* 130Mb/s */

	/* 3 stream rates */

	{ 3,NG3(19.5), 3,NG3(19.5), 0,NG3(19.5), 0,NG3(19.5) },  /*  19Mb/s */
	{ 3,NG3(  39), 3,NG3(19.5), 0,NG3(19.5), 0,NG3(19.5) },  /*  39Mb/s */
	{ 3,NG3(58.5), 3,NG3(  39), 0,NG3(19.5), 0,NG3(19.5) },  /*  58Mb/s */
	{ 3,NG3(  78), 3,NG3(58.5), 0,NG3(  39), 0,NG3(19.5) },  /*  78Mb/s */
	{ 3,NG3( 117), 3,NG3(  78), 0,NG3(58.5), 0,NG3(19.5) },  /* 117Mb/s */
	{ 3,NG3( 156), 3,NG3( 117), 0,NG3(  78), 0,NG3(19.5) },  /*  156Mb/s */
	{ 3,NG3(175.5), 3,NG3( 156), 0,NG3( 117), 0,NG3(  39) },  /*  175Mb/s */
	{ 3,NG3( 195), 3,NG3( 195), 0,NG3( 156), 0,NG3(58.5) },  /* 195Mb/s */

};
#undef G
#undef NG3
#undef NG2
#undef NG1

#define H(_r) \
    (((_r) == 3)   ? 0 : (((_r) == 4.5) ? 1 : (((_r) == 6)  ? 2 : \
    (((_r) == 9)   ? 3 : (((_r) == 12)  ? 4 : (((_r) == 18) ? 5 : \
    (((_r) == 24)  ? 6 : (((_r) == 27)  ? 7 : 0))))))))
static const struct txschedule series_half[] = {
	{ 3,H( 3), 3,H(  3), 0,H(  3), 0,H( 3) },	/*   3Mb/s */
	{ 4,H(4.5),3,H(  3), 4,H(  3), 0,H( 3) },	/* 4.5Mb/s */
	{ 4,H( 6), 3,H(  3), 4,H(  3), 0,H( 3) },	/*   6Mb/s */
	{ 4,H( 9), 3,H(  6), 4,H(  3), 2,H( 3) },	/*   9Mb/s */
	{ 4,H(12), 3,H(  9), 4,H(  6), 2,H( 3) },	/*  12Mb/s */
	{ 4,H(18), 3,H( 12), 4,H(  9), 2,H( 3) },	/*  18Mb/s */
	{ 4,H(24), 3,H( 18), 4,H( 12), 2,H( 6) },	/*  24Mb/s */
	{ 4,H(27), 3,H( 24), 4,H( 18), 2,H(12) }	/*  27Mb/s */
};
#undef H

#ifdef Q
#undef Q
#endif
#define Q(_r) \
    (((_r) == 1.5) ? 0 : (((_r) ==2.25) ? 1 : (((_r) == 3)  ? 2 : \
    (((_r) == 4.5) ? 3 : (((_r) ==  6)  ? 4 : (((_r) == 9)  ? 5 : \
    (((_r) == 12)  ? 6 : (((_r) == 13.5)? 7 : 0))))))))
static const struct txschedule series_quarter[] = {
	{ 3,Q( 1.5),3,Q(1.5), 0,Q(1.5), 0,Q(1.5) },	/* 1.5Mb/s */
	{ 4,Q(2.25),3,Q(1.5), 4,Q(1.5), 0,Q(1.5) },	/*2.25Mb/s */
	{ 4,Q(   3),3,Q(1.5), 4,Q(1.5), 0,Q(1.5) },	/*   3Mb/s */
	{ 4,Q( 4.5),3,Q(  3), 4,Q(1.5), 2,Q(1.5) },	/* 4.5Mb/s */
	{ 4,Q(   6),3,Q(4.5), 4,Q(  3), 2,Q(1.5) },	/*   6Mb/s */
	{ 4,Q(   9),3,Q(  6), 4,Q(4.5), 2,Q(1.5) },	/*   9Mb/s */
	{ 4,Q(  12),3,Q(  9), 4,Q(  6), 2,Q(  3) },	/*  12Mb/s */
	{ 4,Q(13.5),3,Q( 12), 4,Q(  9), 2,Q(  6) }	/*13.5Mb/s */
};
#undef Q

#endif
