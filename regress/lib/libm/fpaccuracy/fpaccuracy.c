/*	$OpenBSD: fpaccuracy.c,v 1.2 2018/03/10 20:52:58 kettenis Exp $	*/

/*
 * Written by Martynas Venckus.  Public domain
 */

#include <err.h>
#include <stdio.h>

#include "fpaccuracy.h"

int
main(int argc, char *argv[])
{
	FILE *out;
	int retval = 0;

	if ((out = fopen("fpaccuracy.out", "w")) == NULL)
		err(1, NULL);

	fprintf(out, "%8s %5s %27s %26s %25s\n", "function", "tests",
		"max err in ulps", "argument (max err)", "value (max err)");
	retval |= fpaccuracy_Gamma(out);
	retval |= fpaccuracy_INV(out);
	retval |= fpaccuracy_Pix(out);
	retval |= fpaccuracy_acos(out);
	retval |= fpaccuracy_acosh(out);
	retval |= fpaccuracy_asin(out);
	retval |= fpaccuracy_asinh(out);
	retval |= fpaccuracy_atan(out);
	retval |= fpaccuracy_atanh(out);
	retval |= fpaccuracy_cos(out);
	retval |= fpaccuracy_cosh(out);
	retval |= fpaccuracy_erf(out);
	retval |= fpaccuracy_erfc(out);
	retval |= fpaccuracy_exp(out);
	retval |= fpaccuracy_j0(out);
	retval |= fpaccuracy_j1(out);
	retval |= fpaccuracy_lgamma(out);
	retval |= fpaccuracy_log(out);
	retval |= fpaccuracy_log10(out);
	retval |= fpaccuracy_pow2_x(out);
	retval |= fpaccuracy_powx_275(out);
	retval |= fpaccuracy_sin(out);
	retval |= fpaccuracy_sincos_sin(out);
	retval |= fpaccuracy_sincos_cos(out);
	retval |= fpaccuracy_sinh(out);
	retval |= fpaccuracy_sqrt(out);
	retval |= fpaccuracy_tan(out);
	retval |= fpaccuracy_tanh(out);
	retval |= fpaccuracy_y0(out);
	retval |= fpaccuracy_y1(out);

	fclose(out);

	return retval;
}

