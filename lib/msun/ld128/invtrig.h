/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 David Schultz <das@FreeBSD.ORG>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <float.h>

#include "fpmath.h"

#define	BIAS		(LDBL_MAX_EXP - 1)
#define	MANH_SIZE	(LDBL_MANH_SIZE + 1)

/* Approximation thresholds. */
#define	ASIN_LINEAR	(BIAS - 56)	/* 2**-56 */
#define	ACOS_CONST	(BIAS - 113)	/* 2**-113 */
#define	ATAN_CONST	(BIAS + 113)	/* 2**113 */
#define	ATAN_LINEAR	(BIAS - 56)	/* 2**-56 */

/* 0.95 */
#define	THRESH	((0xe666666666666666ULL>>(64-(MANH_SIZE-1)))|LDBL_NBIT)

/* Constants shared by the long double inverse trig functions. */
#define	pS0	_ItL_pS0
#define	pS1	_ItL_pS1
#define	pS2	_ItL_pS2
#define	pS3	_ItL_pS3
#define	pS4	_ItL_pS4
#define	pS5	_ItL_pS5
#define	pS6	_ItL_pS6
#define	pS7	_ItL_pS7
#define	pS8	_ItL_pS8
#define	pS9	_ItL_pS9
#define	qS1	_ItL_qS1
#define	qS2	_ItL_qS2
#define	qS3	_ItL_qS3
#define	qS4	_ItL_qS4
#define	qS5	_ItL_qS5
#define	qS6	_ItL_qS6
#define	qS7	_ItL_qS7
#define	qS8	_ItL_qS8
#define	qS9	_ItL_qS9
#define	atanhi	_ItL_atanhi
#define	atanlo	_ItL_atanlo
#define	aT	_ItL_aT
#define	pi_lo	_ItL_pi_lo

#define	pio2_hi	atanhi[3]
#define	pio2_lo	atanlo[3]
#define	pio4_hi	atanhi[1]

/* Constants shared by the long double inverse trig functions. */
extern const long double pS0, pS1, pS2, pS3, pS4, pS5, pS6, pS7, pS8, pS9;
extern const long double qS1, qS2, qS3, qS4, qS5, qS6, qS7, qS8, qS9;
extern const long double atanhi[], atanlo[], aT[];
extern const long double pi_lo;

static inline long double
P(long double x)
{

	return (x * (pS0 + x * (pS1 + x * (pS2 + x * (pS3 + x * \
		(pS4 + x * (pS5 + x * (pS6 + x * (pS7 + x * (pS8 + x * \
		pS9))))))))));
}

static inline long double
Q(long double x)
{

	return (1.0 + x * (qS1 + x * (qS2 + x * (qS3 + x * (qS4 + x * \
		(qS5 + x * (qS6 + x * (qS7 + x * (qS8 + x * qS9)))))))));
}

static inline long double
T_even(long double x)
{

	return (aT[0] + x * (aT[2] + x * (aT[4] + x * (aT[6] + x * \
		(aT[8] + x * (aT[10] + x * (aT[12] + x * (aT[14] + x * \
		(aT[16] + x * (aT[18] + x * (aT[20] + x * aT[22])))))))))));
}

static inline long double
T_odd(long double x)
{

	return (aT[1] + x * (aT[3] + x * (aT[5] + x * (aT[7] + x * \
		(aT[9] + x * (aT[11] + x * (aT[13] + x * (aT[15] + x * \
		(aT[17] + x * (aT[19] + x * (aT[21] + x * aT[23])))))))))));
}
