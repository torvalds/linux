/*	$OpenBSD: wfp.c,v 1.2 2020/01/13 16:51:04 bluhm Exp $	*/
/*-
 * Copyright (c) 2002, 2005 David Schultz <das@FreeBSD.org>
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
 */

/*
 * Test for wprintf() floating point formats.
 */

#include <assert.h>
#include <err.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define	testfmt(result, fmt, ...)	\
	_testfmt((result), __LINE__, #__VA_ARGS__, fmt, __VA_ARGS__)
void _testfmt(const wchar_t *, int, const char *, const wchar_t *, ...);
void smash_stack(void);

int
main(int argc, char *argv[])
{
	/*
	 * Basic tests of decimal output functionality.
	 */
	testfmt(L" 1.000000E+00", L"%13E", 1.0);
	testfmt(L"     1.000000", L"%13f", 1.0);
	testfmt(L"            1", L"%13G", 1.0);
	testfmt(L" 1.000000E+00", L"%13LE", 1.0L);
	testfmt(L"     1.000000", L"%13Lf", 1.0L);
	testfmt(L"            1", L"%13LG", 1.0L);

	testfmt(L"2.718282", L"%.*f", -2, 2.7182818);

	testfmt(L"1.234568e+06", L"%e", 1234567.8);
	testfmt(L"1234567.800000", L"%f", 1234567.8);
	testfmt(L"1.23457E+06", L"%G", 1234567.8);
	testfmt(L"1.234568e+06", L"%Le", 1234567.8L);
	testfmt(L"1234567.800000", L"%Lf", 1234567.8L);
	testfmt(L"1.23457E+06", L"%LG", 1234567.8L);

#if (LDBL_MANT_DIG > DBL_MANT_DIG) && !defined(__i386__)
	testfmt(L"123456789.864210", L"%Lf", 123456789.8642097531L);
	testfmt(L"-1.23457E+08", L"%LG", -123456789.8642097531L);
	testfmt(L"123456789.8642097531", L"%.10Lf", 123456789.8642097531L);
	testfmt(L" 3.141592653589793238e-4000", L"%L27.18Le",
	    3.14159265358979323846e-4000L);
#endif /* (LDBL_MANT_DIG > DBL_MANT_DIG) && !defined(__i386__) */

	/*
	 * Infinities and NaNs
	 */
#ifdef NAN
	testfmt(L"nan", L"%e", NAN);
	testfmt(L"NAN", L"%F", NAN);
	testfmt(L"nan", L"%g", NAN);
	testfmt(L"NAN", L"%LE", (long double)NAN);
	testfmt(L"  nan", L"%05e", NAN);
#endif /* NAN */

	testfmt(L"INF", L"%E", HUGE_VAL);
	testfmt(L"-inf", L"%f", -HUGE_VAL);
	testfmt(L"+inf", L"%+g", HUGE_VAL);
	testfmt(L" inf", L"%4.2Le", HUGE_VALL);
	testfmt(L"-inf", L"%Lf", -HUGE_VALL);
	testfmt(L"  inf", L"%05e", HUGE_VAL);
	testfmt(L" -inf", L"%05e", -HUGE_VAL);

	/*
	 * Padding
	 */
	testfmt(L"0.000000e+00", L"%e", 0.0);
	testfmt(L"0.000000", L"%F", (double)0.0);
	testfmt(L"0", L"%G", 0.0);
	testfmt(L"  0", L"%3.0Lg", 0.0L);
	testfmt(L"    0", L"%5.0f", 0.001);

	/*
	 * Precision specifiers
	 */
	testfmt(L"1.0123e+00", L"%.4e", 1.0123456789);
	testfmt(L"1.0123", L"%.4f", 1.0123456789);
	testfmt(L"1.012", L"%.4g", 1.0123456789);
	testfmt(L"1.2346e-02", L"%.4e", 0.0123456789);
	testfmt(L"0.0123", L"%.4f", 0.0123456789);
	testfmt(L"0.01235", L"%.4g", 0.0123456789);

	/*
	 * Signed conversions
	 */
	testfmt(L"+2.500000e-01", L"%+e", 0.25);
	testfmt(L"+0.000000", L"%+F", 0.0);
	testfmt(L"-1", L"%+g", -1.0);

	testfmt(L"-1.000000e+00", L"% e", -1.0);
	testfmt(L"+1.000000", L"% +f", 1.0);
	testfmt(L" 1", L"% g", 1.0);
	testfmt(L" 0", L"% g", 0.0);

	/*
	 * ``Alternate form''
	 */
	testfmt(L"1.250e+00", L"%#.3e", 1.25);
	testfmt(L"123.000000", L"%#f", 123.0);
	testfmt(L" 12345.", L"%#7.5g", 12345.0);
	testfmt(L" 1.00000", L"%#8g", 1.0);
	testfmt(L"0.0", L"%#.2g", 0.0);

	/*
	 * Padding and decimal point placement
	 */
	testfmt(L"03.2E+00", L"%08.1E", 3.25);
	testfmt(L"003.25", L"%06.2F", 3.25);
	testfmt(L"0003.25", L"%07.4G", 3.25);

	testfmt(L"3.14159e-05", L"%g", 3.14159e-5);
	testfmt(L"0.000314159", L"%g", 3.14159e-4);
	testfmt(L"3.14159e+06", L"%g", 3.14159e6);
	testfmt(L"314159", L"%g", 3.14159e5);
	testfmt(L"314159.", L"%#g", 3.14159e5);

	testfmt(L" 9.000000e+03", L"%13e", 9000.0);
	testfmt(L" 9000.000000", L"%12f", 9000.0);
	testfmt(L" 9000", L"%5g", 9000.0);
	testfmt(L" 900000.", L"%#8g", 900000.0);
	testfmt(L" 9e+06", L"%6g", 9000000.0);
	testfmt(L" 9.000000e-04", L"%13e", 0.0009);
	testfmt(L" 0.000900", L"%9f", 0.0009);
	testfmt(L" 0.0009", L"%7g", 0.0009);
	testfmt(L" 9e-05", L"%6g", 0.00009);
	testfmt(L" 9.00000e-05", L"%#12g", 0.00009);
	testfmt(L" 9.e-05", L"%#7.1g", 0.00009);

	testfmt(L" 0.0", L"%4.1f", 0.0);
	testfmt(L"90.0", L"%4.1f", 90.0);
	testfmt(L" 100", L"%4.0f", 100.0);
	testfmt(L"9.0e+01", L"%4.1e", 90.0);
	testfmt(L"1e+02", L"%4.0e", 100.0);

	/*
	 * Hexadecimal floating point (%a, %A) tests.  Some of these
	 * are only valid if the implementation converts to hex digits
	 * on nibble boundaries.
	 */
	testfmt(L"0x0p+0", L"%a", 0x0.0p0);
	testfmt(L"0X0.P+0", L"%#LA", 0x0.0p0L);
#ifdef NAN
	testfmt(L"inf", L"%La", (long double)INFINITY);
	testfmt(L"+INF", L"%+A", INFINITY);
	testfmt(L"nan", L"%La", (long double)NAN);
	testfmt(L"NAN", L"%A", NAN);
#endif /* NAN */

	testfmt(L" 0x1.23p+0", L"%10a", 0x1.23p0);
	testfmt(L" 0x1.23p-500", L"%12a", 0x1.23p-500);
	testfmt(L" 0x1.2p+40", L"%10.1a", 0x1.23p40);
	testfmt(L" 0X1.230000000000000000000000P-4", L"%32.24A", 0x1.23p-4);
	testfmt(L"0x1p-1074", L"%a", 0x1p-1074);
	testfmt(L"0x1.2345p-1024", L"%a", 0x1.2345p-1024);

	return (0);
}

void
smash_stack(void)
{
	static uint32_t junk = 0xdeadbeef;
	uint32_t buf[512];
	int i;

	for (i = 0; i < sizeof(buf) / sizeof(buf[0]); i++)
		buf[i] = junk;
}

void
_testfmt(const wchar_t *result, int line, const char *argstr,
    const wchar_t *fmt, ...)
{
	wchar_t ws[100];
	va_list ap;

	va_start(ap, fmt);
	smash_stack();
	vswprintf(ws, sizeof(ws)/sizeof(ws[0]), fmt, ap);
	if (wcscmp(result, ws) != 0) {
		const wchar_t *p = ws;
		char f[100], s[100], r[100];

		memset(f, 0, sizeof(f));
		memset(s, 0, sizeof(s));
		memset(r, 0, sizeof(r));
		wcsrtombs(f, &fmt, sizeof(f) - 1, NULL);
		wcsrtombs(s, &p, sizeof(s) - 1, NULL);
		wcsrtombs(r, &result, sizeof(r) - 1, NULL);

		errx(1, "line %d: printf(\"%s\", %s) ==> [%s], expected [%s]",
		    line, f, argstr, s, r);
	}
}
