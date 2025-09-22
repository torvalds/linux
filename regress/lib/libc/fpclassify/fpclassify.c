/*	$OpenBSD: fpclassify.c,v 1.1 2008/09/07 20:36:10 martynas Exp $	*/
/*-
 * Copyright (c) 2003 Mike Barcroft <mike@FreeBSD.org>
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
 * $FreeBSD: src/tools/regression/lib/libc/gen/test-fpclassify.c,v 1.3 2003/03/27 05:32:28 das Exp $
 */

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>

int
main(void)
{

	assert(fpclassify((float)0) == FP_ZERO);
	assert(fpclassify((float)-0.0) == FP_ZERO);
	assert(fpclassify((float)1) == FP_NORMAL);
	assert(fpclassify((float)1000) == FP_NORMAL);
#ifndef __alpha__
	assert(fpclassify(0x1.2p-150f) == FP_SUBNORMAL);
#endif /* !__alpha__ */
	assert(fpclassify(HUGE_VALF) == FP_INFINITE);
	assert(fpclassify((float)HUGE_VAL) == FP_INFINITE);
	assert(fpclassify((float)HUGE_VALL) == FP_INFINITE);
	assert(fpclassify(NAN) == FP_NAN);

	assert(fpclassify((double)0) == FP_ZERO);
	assert(fpclassify((double)-0) == FP_ZERO);
	assert(fpclassify((double)1) == FP_NORMAL);
	assert(fpclassify((double)1000) == FP_NORMAL);
#ifndef __alpha__
	assert(fpclassify(0x1.2p-1075) == FP_SUBNORMAL);
#endif /* !__alpha__ */
	assert(fpclassify(HUGE_VAL) == FP_INFINITE);
	assert(fpclassify((double)HUGE_VALF) == FP_INFINITE);
	assert(fpclassify((double)HUGE_VALL) == FP_INFINITE);
	assert(fpclassify((double)NAN) == FP_NAN);

	assert(fpclassify((long double)0) == FP_ZERO);
	assert(fpclassify((long double)-0.0) == FP_ZERO);
	assert(fpclassify((long double)1) == FP_NORMAL);
	assert(fpclassify((long double)1000) == FP_NORMAL);
#if (LDBL_MANT_DIG > DBL_MANT_DIG)
	assert(fpclassify(0x1.2p-16383L) == FP_SUBNORMAL);
#endif /* (LDBL_MANT_DIG > DBL_MANT_DIG) */
	assert(fpclassify(HUGE_VALL) == FP_INFINITE);
	assert(fpclassify((long double)HUGE_VALF) == FP_INFINITE);
	assert(fpclassify((long double)HUGE_VAL) == FP_INFINITE);
	assert(fpclassify((long double)NAN) == FP_NAN);

	return (0);
}
