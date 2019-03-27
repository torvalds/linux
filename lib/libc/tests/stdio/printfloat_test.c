/*-
 * Copyright (c) 2002-2009 David Schultz <das@FreeBSD.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <err.h>
#include <fenv.h>
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <atf-c.h>

static void
smash_stack(void)
{
	static uint32_t junk = 0xdeadbeef;
	uint32_t buf[512];
	int i;

	for (i = 0; i < sizeof(buf) / sizeof(buf[0]); i++)
		buf[i] = junk;
}

#define	testfmt(result, fmt, ...)       \
	_testfmt((result), #__VA_ARGS__, fmt, __VA_ARGS__)
static void
_testfmt(const char *result, const char *argstr, const char *fmt,...)
{
#define	BUF	100
	wchar_t ws[BUF], wfmt[BUF], wresult[BUF];
	char s[BUF];
	va_list ap, ap2;

	va_start(ap, fmt);
	va_copy(ap2, ap);
	smash_stack();
	vsnprintf(s, sizeof(s), fmt, ap);
	ATF_CHECK_MSG(strcmp(result, s) == 0,
	    "printf(\"%s\", %s) ==> [%s], expected [%s]",
	    fmt, argstr, s, result);

	smash_stack();
	mbstowcs(ws, s, BUF - 1);
	mbstowcs(wfmt, fmt, BUF - 1);
	mbstowcs(wresult, result, BUF - 1);
	vswprintf(ws, sizeof(ws) / sizeof(ws[0]), wfmt, ap2);
	ATF_CHECK_MSG(wcscmp(wresult, ws) == 0,
	    "wprintf(\"%ls\", %s) ==> [%ls], expected [%ls]",
	    wfmt, argstr, ws, wresult);

	va_end(ap);
	va_end(ap2);
}

ATF_TC_WITHOUT_HEAD(float_within_limits);
ATF_TC_BODY(float_within_limits, tc)
{

	ATF_REQUIRE(setlocale(LC_NUMERIC, "C"));

	/* Basic tests of decimal output functionality. */
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
#endif
}

ATF_TC_WITHOUT_HEAD(infinities_and_nans);
ATF_TC_BODY(infinities_and_nans, tc)
{

	ATF_REQUIRE(setlocale(LC_NUMERIC, "C"));

	testfmt("nan", "%e", NAN);
	testfmt("NAN", "%F", NAN);
	testfmt("nan", "%g", NAN);
	testfmt("NAN", "%LE", (long double)NAN);
	testfmt("  nan", "%05e", NAN);

	testfmt("INF", "%E", HUGE_VAL);
	testfmt("-inf", "%f", -HUGE_VAL);
	testfmt("+inf", "%+g", HUGE_VAL);
	testfmt(" inf", "%4.2Le", HUGE_VALL);
	testfmt("-inf", "%Lf", -HUGE_VALL);
	testfmt("  inf", "%05e", HUGE_VAL);
	testfmt(" -inf", "%05e", -HUGE_VAL);
}

ATF_TC_WITHOUT_HEAD(padding);
ATF_TC_BODY(padding, tc)
{

	ATF_REQUIRE(setlocale(LC_NUMERIC, "C"));

	testfmt("0.000000e+00", "%e", 0.0);
	testfmt("0.000000", "%F", (double)0.0);
	testfmt("0", "%G", 0.0);
	testfmt("  0", "%3.0Lg", 0.0L);
	testfmt("    0", "%5.0f", 0.001);
}

ATF_TC_WITHOUT_HEAD(precision_specifiers);
ATF_TC_BODY(precision_specifiers, tc)
{

	ATF_REQUIRE(setlocale(LC_NUMERIC, "C"));

	testfmt("1.0123e+00", "%.4e", 1.0123456789);
	testfmt("1.0123", "%.4f", 1.0123456789);
	testfmt("1.012", "%.4g", 1.0123456789);
	testfmt("1.2346e-02", "%.4e", 0.0123456789);
	testfmt("0.0123", "%.4f", 0.0123456789);
	testfmt("0.01235", "%.4g", 0.0123456789);
}

ATF_TC_WITHOUT_HEAD(thousands_separator_and_other_locale_tests);
ATF_TC_BODY(thousands_separator_and_other_locale_tests, tc)
{

	ATF_REQUIRE(setlocale(LC_NUMERIC, "C"));

	testfmt("12345678.0625", "%'.04f", 12345678.0625);
	testfmt("0012345678.0625", "%'015.4F", 12345678.0625);

	ATF_REQUIRE(setlocale(LC_NUMERIC, "hi_IN.ISCII-DEV")); /* grouping == 2;3 */
	testfmt("1,23,45,678.0625", "%'.4f", 12345678.0625);
	testfmt("01,23,45,678.0625", "%'017.4F", 12345678.0625);
	testfmt(" 9,000", "%'6.0f", 9000.0);
	testfmt("9,000.0", "%'.1f", 9000.0);

	ATF_REQUIRE(setlocale(LC_NUMERIC, "ru_RU.ISO8859-5")); /* decimalpoint==, */
	testfmt("3,1415", "%g", 3.1415);

	/* thousands=. decimalpoint=, grouping=3;3 */
	ATF_REQUIRE(setlocale(LC_NUMERIC, "el_GR.ISO8859-7")); /* decimalpoint==, */
	testfmt("1.234,00", "%'.2f", 1234.00);
	testfmt("123.456,789", "%'.3f", 123456.789);

	ATF_REQUIRE(setlocale(LC_NUMERIC, "C"));
	testfmt("12345678.062500", "%'f", 12345678.0625);
	testfmt("9000.000000", "%'f", 9000.0);
}

ATF_TC_WITHOUT_HEAD(signed_conversions);
ATF_TC_BODY(signed_conversions, tc)
{

	ATF_REQUIRE(setlocale(LC_NUMERIC, "C"));

	testfmt("+2.500000e-01", "%+e", 0.25);
	testfmt("+0.000000", "%+F", 0.0);
	testfmt("-1", "%+g", -1.0);

	testfmt("-1.000000e+00", "% e", -1.0);
	testfmt("+1.000000", "% +f", 1.0);
	testfmt(" 1", "% g", 1.0);
	testfmt(" 0", "% g", 0.0);
}

ATF_TC_WITHOUT_HEAD(alternate_form);
ATF_TC_BODY(alternate_form, tc)
{

	ATF_REQUIRE(setlocale(LC_NUMERIC, "C"));

	testfmt("1.250e+00", "%#.3e", 1.25);
	testfmt("123.000000", "%#f", 123.0);
	testfmt(" 12345.", "%#7.5g", 12345.0);
	testfmt(" 1.00000", "%#8g", 1.0);
	testfmt("0.0", "%#.2g", 0.0);
}

ATF_TC_WITHOUT_HEAD(padding_and_decimal_point_placement);
ATF_TC_BODY(padding_and_decimal_point_placement, tc)
{

	ATF_REQUIRE(setlocale(LC_NUMERIC, "C"));

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
}

ATF_TC_WITHOUT_HEAD(decimal_rounding);
ATF_TC_BODY(decimal_rounding, tc)
{

	ATF_REQUIRE(setlocale(LC_NUMERIC, "C"));

	fesetround(FE_DOWNWARD);
	testfmt("4.437", "%.3f", 4.4375);
	testfmt("-4.438", "%.3f", -4.4375);
	testfmt("4.437", "%.3Lf", 4.4375L);
	testfmt("-4.438", "%.3Lf", -4.4375L);

	fesetround(FE_UPWARD);
	testfmt("4.438", "%.3f", 4.4375);
	testfmt("-4.437", "%.3f", -4.4375);
	testfmt("4.438", "%.3Lf", 4.4375L);
	testfmt("-4.437", "%.3Lf", -4.4375L);

	fesetround(FE_TOWARDZERO);
	testfmt("4.437", "%.3f", 4.4375);
	testfmt("-4.437", "%.3f", -4.4375);
	testfmt("4.437", "%.3Lf", 4.4375L);
	testfmt("-4.437", "%.3Lf", -4.4375L);

	fesetround(FE_TONEAREST);
	testfmt("4.438", "%.3f", 4.4375);
	testfmt("-4.438", "%.3f", -4.4375);
	testfmt("4.438", "%.3Lf", 4.4375L);
	testfmt("-4.438", "%.3Lf", -4.4375L);
}

ATF_TC_WITHOUT_HEAD(hexadecimal_floating_point);
ATF_TC_BODY(hexadecimal_floating_point, tc)
{

	ATF_REQUIRE(setlocale(LC_NUMERIC, "C"));

	/*
	 * Hexadecimal floating point (%a, %A) tests.  Some of these
	 * are only valid if the implementation converts to hex digits
	 * on nibble boundaries.
	 */
	testfmt("0x0p+0", "%a", 0x0.0p0);
	testfmt("0X0.P+0", "%#LA", 0x0.0p0L);
	testfmt("inf", "%La", (long double)INFINITY);
	testfmt("+INF", "%+A", INFINITY);
	testfmt("nan", "%La", (long double)NAN);
	testfmt("NAN", "%A", NAN);

	testfmt(" 0x1.23p+0", "%10a", 0x1.23p0);
	testfmt(" 0x1.23p-500", "%12a", 0x1.23p-500);
	testfmt(" 0x1.2p+40", "%10.1a", 0x1.23p40);
	testfmt(" 0X1.230000000000000000000000P-4", "%32.24A", 0x1.23p-4);
	testfmt("0x1p-1074", "%a", 0x1p-1074);
	testfmt("0x1.2345p-1024", "%a", 0x1.2345p-1024);

#if (LDBL_MANT_DIG == 64)
	testfmt("0x1.921fb54442d18468p+1", "%La", 0x3.243f6a8885a308dp0L);
	testfmt("0x1p-16445", "%La", 0x1p-16445L);
	testfmt("0x1.30ecap-16381", "%La", 0x9.8765p-16384L);
#elif (LDBL_MANT_DIG == 113)
	testfmt("0x1.921fb54442d18469898cc51701b8p+1", "%La",
	    0x3.243f6a8885a308d313198a2e037p0L);
	testfmt("0x1p-16494", "%La", 0x1p-16494L);
	testfmt("0x1.2345p-16384", "%La", 0x1.2345p-16384L);
#else
	testfmt("0x1.921fb54442d18p+1", "%La", 0x3.243f6a8885a31p0L);
	testfmt("0x1p-1074", "%La", 0x1p-1074L);
	testfmt("0x1.30ecap-1021", "%La", 0x9.8765p-1024L);
#endif
}

ATF_TC_WITHOUT_HEAD(hexadecimal_rounding);
ATF_TC_BODY(hexadecimal_rounding, tc)
{

	ATF_REQUIRE(setlocale(LC_NUMERIC, "C"));

	fesetround(FE_TOWARDZERO);
	testfmt("0X1.23456789ABCP+0", "%.11A", 0x1.23456789abcdep0);
	testfmt("-0x1.23456p+0", "%.5a", -0x1.23456789abcdep0);
	testfmt("0x1.23456p+0", "%.5a", 0x1.23456789abcdep0);
	testfmt("0x1.234567p+0", "%.6a", 0x1.23456789abcdep0);
	testfmt("-0x1.234566p+0", "%.6a", -0x1.23456689abcdep0);

	fesetround(FE_DOWNWARD);
	testfmt("0X1.23456789ABCP+0", "%.11A", 0x1.23456789abcdep0);
	testfmt("-0x1.23457p+0", "%.5a", -0x1.23456789abcdep0);
	testfmt("0x1.23456p+0", "%.5a", 0x1.23456789abcdep0);
	testfmt("0x1.234567p+0", "%.6a", 0x1.23456789abcdep0);
	testfmt("-0x1.234567p+0", "%.6a", -0x1.23456689abcdep0);

	fesetround(FE_UPWARD);
	testfmt("0X1.23456789ABDP+0", "%.11A", 0x1.23456789abcdep0);
	testfmt("-0x1.23456p+0", "%.5a", -0x1.23456789abcdep0);
	testfmt("0x1.23457p+0", "%.5a", 0x1.23456789abcdep0);
	testfmt("0x1.234568p+0", "%.6a", 0x1.23456789abcdep0);
	testfmt("-0x1.234566p+0", "%.6a", -0x1.23456689abcdep0);

	fesetround(FE_TONEAREST);
	testfmt("0x1.23456789abcdep+4", "%a", 0x1.23456789abcdep4);
	testfmt("0X1.23456789ABDP+0", "%.11A", 0x1.23456789abcdep0);
	testfmt("-0x1.23456p+0", "%.5a", -0x1.23456789abcdep0);
	testfmt("0x1.23456p+0", "%.5a", 0x1.23456789abcdep0);
	testfmt("0x1.234568p+0", "%.6a", 0x1.23456789abcdep0);
	testfmt("-0x1.234567p+0", "%.6a", -0x1.23456689abcdep0);
	testfmt("0x1.00p-1029", "%.2a", 0x1.fffp-1030);
	testfmt("0x1.00p-1026", "%.2a", 0xf.fffp-1030);
	testfmt("0x1.83p+0", "%.2a", 1.51);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, float_within_limits);
	ATF_TP_ADD_TC(tp, infinities_and_nans);
	ATF_TP_ADD_TC(tp, padding);
	ATF_TP_ADD_TC(tp, precision_specifiers);
	ATF_TP_ADD_TC(tp, thousands_separator_and_other_locale_tests);
	ATF_TP_ADD_TC(tp, signed_conversions);
	ATF_TP_ADD_TC(tp, alternate_form);
	ATF_TP_ADD_TC(tp, padding_and_decimal_point_placement);
	ATF_TP_ADD_TC(tp, decimal_rounding);
	ATF_TP_ADD_TC(tp, hexadecimal_floating_point);
	ATF_TP_ADD_TC(tp, hexadecimal_rounding);

	return (atf_no_error());
}
