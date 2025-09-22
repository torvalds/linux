/*	$OpenBSD: fp.c,v 1.2 2020/05/31 12:27:19 mortimer Exp $	*/
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
 * Test for printf() floating point formats.
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

#define	testfmt(result, fmt, ...)	\
	_testfmt((result), __LINE__, #__VA_ARGS__, fmt, __VA_ARGS__)
void _testfmt(const char *, int, const char *, const char *, ...);
void smash_stack(void);

int
main(int argc, char *argv[])
{
	/*
	 * Basic tests of decimal output functionality.
	 */
	testfmt(" 1.000000E+00", "%13E", 1.0);
	testfmt("     1.000000", "%13f", 1.0);
	testfmt("            1", "%13G", 1.0);
	testfmt(" 1.000000E+00", "%13LE", 1.0L);
	testfmt("     1.000000", "%13Lf", 1.0L);
	testfmt("            1", "%13LG", 1.0L);

	testfmt("2.718282", "%.*f", -2, 2.7182818);

	testfmt("1.234568e+06", "%e", 1234567.8);
	testfmt("1234567.800000", "%f", 1234567.8);
	testfmt("1.23457E+06", "%G", 1234567.8);
	testfmt("1.234568e+06", "%Le", 1234567.8L);
	testfmt("1234567.800000", "%Lf", 1234567.8L);
	testfmt("1.23457E+06", "%LG", 1234567.8L);

#if (LDBL_MANT_DIG > DBL_MANT_DIG) && !defined(__i386__)
	testfmt("123456789.864210", "%Lf", 123456789.8642097531L);
	testfmt("-1.23457E+08", "%LG", -123456789.8642097531L);
	testfmt("123456789.8642097531", "%.10Lf", 123456789.8642097531L);
	testfmt(" 3.141592653589793238e-4000", "%L27.18Le",
	    3.14159265358979323846e-4000L);
#endif /* (LDBL_MANT_DIG > DBL_MANT_DIG) && !defined(__i386__) */

	/*
	 * Infinities and NaNs
	 */
#ifdef NAN
	testfmt("nan", "%e", NAN);
	testfmt("NAN", "%F", NAN);
	testfmt("nan", "%g", NAN);
	testfmt("NAN", "%LE", (long double)NAN);
	testfmt("  nan", "%05e", NAN);
#endif /* NAN */

	testfmt("INF", "%E", HUGE_VAL);
	testfmt("-inf", "%f", -HUGE_VAL);
	testfmt("+inf", "%+g", HUGE_VAL);
	testfmt(" inf", "%4.2Le", HUGE_VALL);
	testfmt("-inf", "%Lf", -HUGE_VALL);
	testfmt("  inf", "%05e", HUGE_VAL);
	testfmt(" -inf", "%05e", -HUGE_VAL);

	/*
	 * Padding
	 */
	testfmt("0.000000e+00", "%e", 0.0);
	testfmt("0.000000", "%F", (double)0.0);
	testfmt("0", "%G", 0.0);
	testfmt("  0", "%3.0Lg", 0.0L);
	testfmt("    0", "%5.0f", 0.001);

	/*
	 * Precision specifiers
	 */
	testfmt("1.0123e+00", "%.4e", 1.0123456789);
	testfmt("1.0123", "%.4f", 1.0123456789);
	testfmt("1.012", "%.4g", 1.0123456789);
	testfmt("1.2346e-02", "%.4e", 0.0123456789);
	testfmt("0.0123", "%.4f", 0.0123456789);
	testfmt("0.01235", "%.4g", 0.0123456789);

	/*
	 * Signed conversions
	 */
	testfmt("+2.500000e-01", "%+e", 0.25);
	testfmt("+0.000000", "%+F", 0.0);
	testfmt("-1", "%+g", -1.0);

	testfmt("-1.000000e+00", "% e", -1.0);
	testfmt("+1.000000", "% +f", 1.0);
	testfmt(" 1", "% g", 1.0);
	testfmt(" 0", "% g", 0.0);

	/*
	 * ``Alternate form''
	 */
	testfmt("1.250e+00", "%#.3e", 1.25);
	testfmt("123.000000", "%#f", 123.0);
	testfmt(" 12345.", "%#7.5g", 12345.0);
	testfmt(" 1.00000", "%#8g", 1.0);
	testfmt("0.0", "%#.2g", 0.0);

	/*
	 * Padding and decimal point placement
	 */
	testfmt("03.2E+00", "%08.1E", 3.25);
	testfmt("003.25", "%06.2F", 3.25);
	testfmt("0003.25", "%07.4G", 3.25);

	testfmt("3.14159e-05", "%g", 3.14159e-5);
	testfmt("0.000314159", "%g", 3.14159e-4);
	testfmt("3.14159e+06", "%g", 3.14159e6);
	testfmt("314159", "%g", 3.14159e5);
	testfmt("314159.", "%#g", 3.14159e5);

	testfmt(" 9.000000e+03", "%13e", 9000.0);
	testfmt(" 9000.000000", "%12f", 9000.0);
	testfmt(" 9000", "%5g", 9000.0);
	testfmt(" 900000.", "%#8g", 900000.0);
	testfmt(" 9e+06", "%6g", 9000000.0);
	testfmt(" 9.000000e-04", "%13e", 0.0009);
	testfmt(" 0.000900", "%9f", 0.0009);
	testfmt(" 0.0009", "%7g", 0.0009);
	testfmt(" 9e-05", "%6g", 0.00009);
	testfmt(" 9.00000e-05", "%#12g", 0.00009);
	testfmt(" 9.e-05", "%#7.1g", 0.00009);

	testfmt(" 0.0", "%4.1f", 0.0);
	testfmt("90.0", "%4.1f", 90.0);
	testfmt(" 100", "%4.0f", 100.0);
	testfmt("9.0e+01", "%4.1e", 90.0);
	testfmt("1e+02", "%4.0e", 100.0);

	/*
	 * Hexadecimal floating point (%a, %A) tests.  Some of these
	 * are only valid if the implementation converts to hex digits
	 * on nibble boundaries.
	 */
	testfmt("0x0p+0", "%a", 0x0.0p0);
	testfmt("0X0.P+0", "%#LA", 0x0.0p0L);
#ifdef NAN
	testfmt("inf", "%La", (long double)INFINITY);
	testfmt("+INF", "%+A", INFINITY);
	testfmt("nan", "%La", (long double)NAN);
	testfmt("NAN", "%A", NAN);
#endif /* NAN */

	testfmt(" 0x1.23p+0", "%10a", 0x1.23p0);
	testfmt(" 0x1.23p-500", "%12a", 0x1.23p-500);
	testfmt(" 0x1.2p+40", "%10.1a", 0x1.23p40);
	testfmt(" 0X1.230000000000000000000000P-4", "%32.24A", 0x1.23p-4);
	testfmt("0x1p-1074", "%a", 0x1p-1074);
	testfmt("0x1.2345p-1024", "%a", 0x1.2345p-1024);

#if LDBL_MANT_DIG == 113
	testfmt("-0x1.e7d7c7b7a7978777675747372717p-14344", "%La",
			-0x1.e7d7c7b7a7978777675747372717p-14344L);
#elif LDBL_MANT_DIG == 64
	testfmt("-0x8.777675747372717p-16248", "%La",
			-0x8.777675747372717p-16248L);
#endif

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
_testfmt(const char *result, int line, const char *argstr, const char *fmt,...)
{
	char s[100];
	va_list ap;

	va_start(ap, fmt);
	smash_stack();
	vsnprintf(s, sizeof(s), fmt, ap);
	if (strcmp(result, s) != 0) {
		fprintf(stderr,
		    "%d: printf(\"%s\", %s) ==> [%s], expected [%s]\n",
		    line, fmt, argstr, s, result);
		abort();
	}
}
