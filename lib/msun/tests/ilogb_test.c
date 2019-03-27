/*-
 * Copyright (c) 2004 Stefan Farfeleder
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

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int
main(void)
{
	char buf[128], *end;
	double d;
	float f;
	long double ld;
	int e, i;

	printf("1..3\n");
	assert(ilogb(0) == FP_ILOGB0);
	assert(ilogb(NAN) == FP_ILOGBNAN);
	assert(ilogb(INFINITY) == INT_MAX);
	for (e = DBL_MIN_EXP - DBL_MANT_DIG; e < DBL_MAX_EXP; e++) {
		snprintf(buf, sizeof(buf), "0x1.p%d", e);
		d = strtod(buf, &end);
		assert(*end == '\0');
		i = ilogb(d);
		assert(i == e);
	}
	printf("ok 1 - ilogb\n");

	assert(ilogbf(0) == FP_ILOGB0);
	assert(ilogbf(NAN) == FP_ILOGBNAN);
	assert(ilogbf(INFINITY) == INT_MAX);
	for (e = FLT_MIN_EXP - FLT_MANT_DIG; e < FLT_MAX_EXP; e++) {
		snprintf(buf, sizeof(buf), "0x1.p%d", e);
		f = strtof(buf, &end);
		assert(*end == '\0');
		i = ilogbf(f);
		assert(i == e);
	}
	printf("ok 2 - ilogbf\n");

	assert(ilogbl(0) == FP_ILOGB0);
	assert(ilogbl(NAN) == FP_ILOGBNAN);
	assert(ilogbl(INFINITY) == INT_MAX);
	for (e = LDBL_MIN_EXP - LDBL_MANT_DIG; e < LDBL_MAX_EXP; e++) {
		snprintf(buf, sizeof(buf), "0x1.p%d", e);
		ld = strtold(buf, &end);
		assert(*end == '\0');
		i = ilogbl(ld);
		assert(i == e);
	}
	printf("ok 3 - ilogbl\n");

	return (0);
}
