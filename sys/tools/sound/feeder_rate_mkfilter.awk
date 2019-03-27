#!/usr/bin/awk -f
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2007-2009 Ariff Abdullah <ariff@FreeBSD.org>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#
# $FreeBSD$
#

#
# FIR filter design by windowing method. This might become one of the
# funniest joke I've ever written due to too many tricks being applied to
# ensure maximum precision (well, in fact this is already have the same
# precision granularity compared to its C counterpart). Nevertheless, it's
# working, precise, dynamically tunable based on "presets".
#
# XXX EXPECT TOTAL REWRITE! DON'T ARGUE!
#
# TODO: Using ultraspherical window might be a good idea.
#
# Based on:
#
# "Digital Audio Resampling" by Julius O. Smith III
#
#  - http://ccrma.stanford.edu/~jos/resample/
#

#
# Some basic Math functions.
#
function abs(x)
{
	return (((x < 0) ? -x : x) + 0);
}

function fabs(x)
{
	return (((x < 0.0) ? -x : x) + 0.0);
}

function ceil(x, r)
{
	r = int(x);
	if (r < x)
		r++;
	return (r + 0);
}

function floor(x, r)
{
	r = int(x);
	if (r > x)
		r--;
	return (r + 0);
}

function pow(x, y)
{
	return (exp(1.0 * y * log(1.0 * x)));
}

#
# What the hell...
#
function shl(x, y)
{
	while (y > 0) {
		x *= 2;
		y--;
	}
	return (x);
}

function shr(x, y)
{
	while (y > 0 && x != 0) {
		x = floor(x / 2);
		y--;
	}
	return (x);
}

function fx_floor(v, o, r)
{
	if (fabs(v) < fabs(smallest))
		smallest = v;
	if (fabs(v) > fabs(largest))
		largest = v;

	r = floor((v * o) + 0.5);
	if (r < INT32_MIN || r > INT32_MAX)
		printf("\n#error overflow v=%f, please reduce %d\n", v, o);

	return (r);
}

#
# Kaiser linear piecewise functions.
#
function kaiserAttn2Beta(attn, beta)
{
	if (attn < 0.0)
		return (Z_KAISER_BETA_DEFAULT);

	if (attn > 50.0)
		beta = 0.1102 * ((1.0 * attn) - 8.7);
	else if (attn > 21.0)
		beta = (0.5842 * pow((1.0 * attn) - 21.0, 0.4)) +	\
		    (0.07886 * ((1.0 * attn) - 21.0));
	else
		beta = 0.0;

	return (beta);
}

function kaiserBeta2Attn(beta, x, y, i, attn, xbeta)
{
	if (beta < Z_WINDOW_KAISER)
		return (Z_KAISER_ATTN_DEFAULT);
	
	if (beta > kaiserAttn2Beta(50.0))
		attn = ((1.0 * beta) / 0.1102) + 8.7;
	else {
		x = 21.0;
		y = 50.0;
		attn = 0.5 * (x + y);
		for (i = 0; i < 128; i++) {
			xbeta = kaiserAttn2Beta(attn)
			if (beta == xbeta ||				\
			    (i > 63 &&					\
			    fabs(beta - xbeta) < Z_KAISER_EPSILON))
				break;
			if (beta > xbeta)
				x = attn;
			else
				y = attn;
			attn = 0.5 * (x + y);
		}
	}

	return (attn);
}

function kaiserRolloff(len, attn)
{
	return (1.0 - (((1.0 * attn) - 7.95) / (((1.0 * len) - 1.0) * 14.36)));
}

#
# 0th order modified Bessel function of the first kind.
#
function I0(x, s, u, n, h, t)
{
	s = n = u = 1.0;
	h = x * 0.5;

	do {
		t = h / n;
		n += 1.0;
		t *= t;
		u *= t;
		s += u;
	} while (u >= (I0_EPSILON * s));

	return (s);
}

function wname(beta)
{
	if (beta >= Z_WINDOW_KAISER)
		return ("Kaiser");
	else if (beta == Z_WINDOW_BLACKMAN_NUTTALL)
		return ("Blackman - Nuttall");
	else if (beta == Z_WINDOW_NUTTALL)
		return ("Nuttall");
	else if (beta == Z_WINDOW_BLACKMAN_HARRIS)
		return ("Blackman - Harris");
	else if (beta == Z_WINDOW_BLACKMAN)
		return ("Blackman");
	else if (beta == Z_WINDOW_HAMMING)
		return ("Hamming");
	else if (beta == Z_WINDOW_HANN)
		return ("Hann");
	else
		return ("What The Hell !?!?");
}

function rolloff_round(x)
{
	if (x < 0.67)
		x = 0.67;
	else if (x > 1.0)
		x = 1.0;
	
	return (x);
}

function tap_round(x, y)
{
	y = floor(x + 3);
	y -= y % 4;
	return (y);
}

function lpf(imp, n, rolloff, beta, num, i, j, x, nm, ibeta, w)
{
	rolloff = rolloff_round(rolloff + (Z_NYQUIST_HOVER * (1.0 - rolloff)));
	imp[0] = rolloff;

	#
	# Generate ideal sinc impulses, locate the last zero-crossing and pad
	# the remaining with 0.
	#
	# Note that there are other (faster) ways of calculating this without
	# the misery of traversing the entire sinc given the fact that the
	# distance between each zero crossings is actually the bandwidth of
	# the impulses, but it seems having 0.0001% chances of failure due to
	# limited precision.
	#
	j = n;
	for (i = 1; i < n; i++) {
		x = (M_PI * i) / (1.0 * num);
		imp[i] = sin(x * rolloff) / x;
		if (i != 1 && (imp[i] * imp[i - 1]) <= 0.0)
			j = i;
	}

	for (i = j; i < n; i++)
		imp[i] = 0.0;

	nm = 1.0 * (j - 1);

	if (beta >= Z_WINDOW_KAISER)
		ibeta = I0(beta);

	for (i = 1; i < j; i++) {
		if (beta >= Z_WINDOW_KAISER) {
			# Kaiser window...
			x = (1.0 * i) / nm;
			w = I0(beta * sqrt(1.0 - (x * x))) / ibeta;
		} else {
			# Cosined windows...
			x = (M_PI * i) / nm;
			if (beta == Z_WINDOW_BLACKMAN_NUTTALL) {
				# Blackman - Nuttall
				w = 0.36335819 + (0.4891775 * cos(x)) +	\
				    (0.1365995 * cos(2 * x)) +		\
				    (0.0106411 * cos(3 * x));
			} else if (beta == Z_WINDOW_NUTTALL) {
				# Nuttall
		        	w = 0.355768 + (0.487396 * cos(x)) +	\
				    (0.144232 * cos(2 * x)) +		\
				    (0.012604 * cos(3 * x));
			} else if (beta == Z_WINDOW_BLACKMAN_HARRIS) {
				# Blackman - Harris
				w = 0.422323 + (0.49755 * cos(x)) +	\
				    (0.07922 * cos(2 * x));
			} else if (beta == Z_WINDOW_BLACKMAN) {
				# Blackman
				w = 0.42 + (0.50 * cos(x)) +		\
				    (0.08 * cos(2 * x));
			} else if (beta == Z_WINDOW_HAMMING) {
				# Hamming
				w = 0.54 + (0.46 * cos(x));
			} else if (beta == Z_WINDOW_HANN) {
				# Hann
				w = 0.50 + (0.50 * cos(x));
			} else {
				# What The Hell !?!?
				w = 0.0;
			}
		}
		imp[i] *= w;
	}

	imp["impulse_length"] = j;
	imp["rolloff"] = rolloff;
}

function mkfilter(imp, nmult, rolloff, beta, num,			\
    nwing, mwing, nrolloff, i, dcgain, v, quality)
{
	nwing = floor((nmult * num) / 2) + 1;

	lpf(imp, nwing, rolloff, beta, num);

	mwing = imp["impulse_length"];
	nrolloff = imp["rolloff"];
	quality = imp["quality"];

	dcgain = 0.0;
	for (i = num; i < mwing; i += num)
		dcgain += imp[i];
	dcgain *= 2.0;
	dcgain += imp[0];

	for (i = 0; i < nwing; i++)
		imp[i] /= dcgain;

	if (quality > 2)
		printf("\n");
	printf("/*\n");
	printf(" *   quality = %d\n", quality);
	printf(" *    window = %s\n", wname(beta));
	if (beta >= Z_WINDOW_KAISER) {
		printf(" *             beta: %.2f\n", beta);
		printf(" *             stop: -%.2f dB\n",		\
		    kaiserBeta2Attn(beta));
	}
	printf(" *    length = %d\n", nmult);
	printf(" * bandwidth = %.2f%%", rolloff * 100.0);
	if (rolloff != nrolloff) {
		printf(" + %.2f%% = %.2f%% (nyquist hover: %.2f%%)",	\
		    (nrolloff - rolloff) * 100.0, nrolloff * 100.0,	\
		    Z_NYQUIST_HOVER * 100.0);
	}
	printf("\n");
	printf(" *     drift = %d\n", num);
	printf(" *     width = %d\n", mwing);
	printf(" */\n");
	printf("static int32_t z_coeff_q%d[%d] = {",			\
	    quality, nwing + (Z_COEFF_OFFSET * 2));
	for (i = 0; i < (nwing + (Z_COEFF_OFFSET * 2)); i++) {
		if ((i % 5) == 0)
			printf("\n      ");
		if (i < Z_COEFF_OFFSET)
			v = fx_floor(imp[Z_COEFF_OFFSET - i], Z_COEFF_ONE);
		else if ((i - Z_COEFF_OFFSET) >= nwing)
			v = fx_floor(					\
			    imp[nwing + nwing - i + Z_COEFF_OFFSET - 1],\
			    Z_COEFF_ONE);
		else
			v = fx_floor(imp[i - Z_COEFF_OFFSET], Z_COEFF_ONE);
		printf(" %s0x%08x,", (v < 0) ? "-" : " ", abs(v));
	}
	printf("\n};\n\n");
	printf("/*\n");
	printf(" * interpolated q%d differences.\n", quality);
	printf(" */\n");
	printf("static int32_t z_dcoeff_q%d[%d] = {", quality, nwing);
	for (i = 1; i <= nwing; i++) {
		if ((i % 5) == 1)
			printf("\n      ");
		v = -imp[i - 1];
		if (i != nwing)
			v += imp[i];
		v = fx_floor(v, Z_INTERP_COEFF_ONE);
		if (abs(v) > abs(largest_interp))
			largest_interp = v;
		printf(" %s0x%08x,", (v < 0) ? "-" : " ", abs(v));
	}
	printf("\n};\n");

	return (nwing);
}

function filter_parse(s, a, i, attn, alen)
{
	split(s, a, ":");
	alen = length(a);

	if (alen > 0 && a[1] == "OVERSAMPLING_FACTOR") {
		if (alen != 2)
			return (-1);
		init_drift(floor(a[2]));
		return (-1);
	}

	if (alen > 0 && a[1] == "COEFFICIENT_BIT") {
		if (alen != 2)
			return (-1);
		init_coeff_bit(floor(a[2]));
		return (-1);
	}

	if (alen > 0 && a[1] == "ACCUMULATOR_BIT") {
		if (alen != 2)
			return (-1);
		init_accum_bit(floor(a[2]));
		return (-1);
	}

	if (alen > 0 && a[1] == "INTERPOLATOR") {
		if (alen != 2)
			return (-1);
		init_coeff_interpolator(toupper(a[2]));
		return (-1);
	}

	if (alen == 1 || alen == 2) {
		if (a[1] == "NYQUIST_HOVER") {
			i = 1.0 * a[2];
			Z_NYQUIST_HOVER = (i > 0.0 && i < 1.0) ? i : 0.0;
			return (-1);
		}
		i = 1;
		if (alen == 1) {
			attn = Z_KAISER_ATTN_DEFAULT;
			Popts["beta"] = Z_KAISER_BETA_DEFAULT;
		} else if (Z_WINDOWS[a[1]] < Z_WINDOW_KAISER) {
			Popts["beta"] = Z_WINDOWS[a[1]];
			i = tap_round(a[2]);
			Popts["nmult"] = i;
			if (i < 28)
				i = 28;
			i = 1.0 - (6.44 / i);
			Popts["rolloff"] = rolloff_round(i);
			return (0);
		} else {
			attn = 1.0 * a[i++];
			Popts["beta"] = kaiserAttn2Beta(attn);
		}
		i = tap_round(a[i]);
		Popts["nmult"] = i;
		if (i > 7 && i < 28)
			i = 27;
		i = kaiserRolloff(i, attn);
		Popts["rolloff"] = rolloff_round(i);

		return (0);
	}

	if (!(alen == 3 || alen == 4))
		return (-1);

	i = 2;

	if (a[1] == "kaiser") {
		if (alen > 2)
			Popts["beta"] = 1.0 * a[i++];
		else
			Popts["beta"] = Z_KAISER_BETA_DEFAULT;
	} else if (Z_WINDOWS[a[1]] < Z_WINDOW_KAISER)
		Popts["beta"] = Z_WINDOWS[a[1]];
	else if (1.0 * a[1] < Z_WINDOW_KAISER)
		return (-1);
	else
		Popts["beta"] = kaiserAttn2Beta(1.0 * a[1]);
	Popts["nmult"] = tap_round(a[i++]);
	if (a[1] == "kaiser" && alen == 3)
		i = kaiserRolloff(Popts["nmult"],			\
		    kaiserBeta2Attn(Popts["beta"]));
	else
		i = 1.0 * a[i];
	Popts["rolloff"] = rolloff_round(i);

	return (0);
}

function genscale(bit, s1, s2, scale)
{
	if ((bit + Z_COEFF_SHIFT) > Z_ACCUMULATOR_BIT)
		s1 = Z_COEFF_SHIFT -					\
		    (32 - (Z_ACCUMULATOR_BIT - Z_COEFF_SHIFT));
	else
		s1 = Z_COEFF_SHIFT - (32 - bit);

	s2 = Z_SHIFT + (32 - bit);

	if (s1 == 0)
		scale = "v";
	else if (s1 < 0)
		scale = sprintf("(v) << %d", abs(s1));
	else
		scale = sprintf("(v) >> %d", s1);
	
	scale = sprintf("(%s) * Z_SCALE_CAST(s)", scale);

	if (s2 != 0)
		scale = sprintf("(%s) >> %d", scale, s2);

	printf("#define Z_SCALE_%d(v, s)\t%s(%s)\n",			\
	    bit, (bit < 10) ? "\t" : "", scale);
}

function genlerp(bit, use64, lerp)
{
	if ((bit + Z_LINEAR_SHIFT) <= 32) {
		lerp = sprintf("(((y) - (x)) * (z)) >> %d", Z_LINEAR_SHIFT);
	} else if (use64 != 0) {
		if ((bit + Z_LINEAR_SHIFT) <= 64) {
			lerp = sprintf(					\
			    "(((int64_t)(y) - (x)) * (z)) "		\
			    ">> %d",					\
			    Z_LINEAR_SHIFT);
		} else {
			lerp = sprintf(					\
			    "((int64_t)((y) >> %d) - ((x) >> %d)) * ",	\
			    "(z)"					\
			    bit + Z_LINEAR_SHIFT - 64,			\
			    bit + Z_LINEAR_SHIFT - 64);
			if ((64 - bit) != 0)
				lerp = sprintf("(%s) >> %d", lerp, 64 - bit);
		}
	} else {
		lerp = sprintf(						\
		    "(((y) >> %d) - ((x) >> %d)) * (z)",		\
		    bit + Z_LINEAR_SHIFT - 32,				\
		    bit + Z_LINEAR_SHIFT - 32);
		if ((32 - bit) != 0)
			lerp = sprintf("(%s) >> %d", lerp, 32 - bit);
	}

	printf("#define Z_LINEAR_INTERPOLATE_%d(z, x, y)"		\
	    "\t\t\t\t%s\\\n\t((x) + (%s))\n",					\
	    bit, (bit < 10) ? "\t" : "", lerp);
}

function init_drift(drift, xdrift)
{
	xdrift = floor(drift);

	if (Z_DRIFT_SHIFT != -1) {
		if (xdrift != Z_DRIFT_SHIFT)
			printf("#error Z_DRIFT_SHIFT reinitialize!\n");
		return;
	}

	#
	# Initialize filter oversampling factor, or in other word
	# Z_DRIFT_SHIFT.
	#
	if (xdrift < 0)
		xdrift = 1;
	else if (xdrift > 31)
		xdrift = 31;

	Z_DRIFT_SHIFT  = xdrift;
	Z_DRIFT_ONE    = shl(1, Z_DRIFT_SHIFT);

	Z_SHIFT        = Z_FULL_SHIFT - Z_DRIFT_SHIFT;
	Z_ONE          = shl(1, Z_SHIFT);
	Z_MASK         = Z_ONE - 1;
}

function init_coeff_bit(cbit, xcbit)
{
	xcbit = floor(cbit);

	if (Z_COEFF_SHIFT != 0) {
		if (xcbit != Z_COEFF_SHIFT)
			printf("#error Z_COEFF_SHIFT reinitialize!\n");
		return;
	}

	#
	# Initialize dynamic range of coefficients.
	#
	if (xcbit < 1)
		xcbit = 1;
	else if (xcbit > 30)
		xcbit = 30;

	Z_COEFF_SHIFT = xcbit;
	Z_COEFF_ONE   = shl(1, Z_COEFF_SHIFT);
}

function init_accum_bit(accbit, xaccbit)
{
	xaccbit = floor(accbit);

	if (Z_ACCUMULATOR_BIT != 0) {
		if (xaccbit != Z_ACCUMULATOR_BIT)
			printf("#error Z_ACCUMULATOR_BIT reinitialize!\n");
		return;
	}

	#
	# Initialize dynamic range of accumulator.
	#
	if (xaccbit > 64)
		xaccbit = 64;
	else if (xaccbit < 32)
		xaccbit = 32;

	Z_ACCUMULATOR_BIT = xaccbit;
}

function init_coeff_interpolator(interp)
{
	#
	# Validate interpolator type.
	#
	if (interp == "ZOH" || interp == "LINEAR" ||			\
	    interp == "QUADRATIC" || interp == "HERMITE" ||		\
	    interp == "BSPLINE" || interp == "OPT32X" ||		\
	    interp == "OPT16X" || interp == "OPT8X" ||			\
	    interp == "OPT4X" || interp == "OPT2X")
		Z_COEFF_INTERPOLATOR = interp;
}

BEGIN {
	I0_EPSILON = 1e-21;
	M_PI = atan2(0.0, -1.0);

	INT32_MAX =  1 + ((shl(1, 30) - 1) * 2);
	INT32_MIN = -1 - INT32_MAX;

	Z_COEFF_OFFSET = 5;

	Z_ACCUMULATOR_BIT_DEFAULT = 58;
	Z_ACCUMULATOR_BIT         = 0;

	Z_FULL_SHIFT   = 30;
	Z_FULL_ONE     = shl(1, Z_FULL_SHIFT);

	Z_COEFF_SHIFT_DEFAULT = 30;
	Z_COEFF_SHIFT         = 0;
	Z_COEFF_ONE           = 0;

	Z_COEFF_INTERPOLATOR  = 0;

	Z_INTERP_COEFF_SHIFT = 24;
	Z_INTERP_COEFF_ONE   = shl(1, Z_INTERP_COEFF_SHIFT);

	Z_LINEAR_FULL_SHIFT = Z_FULL_SHIFT;
	Z_LINEAR_FULL_ONE   = shl(1, Z_LINEAR_FULL_SHIFT);
	Z_LINEAR_SHIFT      = 8;
	Z_LINEAR_UNSHIFT    = Z_LINEAR_FULL_SHIFT - Z_LINEAR_SHIFT;
	Z_LINEAR_ONE        = shl(1, Z_LINEAR_SHIFT)

	Z_DRIFT_SHIFT_DEFAULT = 5;
	Z_DRIFT_SHIFT         = -1;
	# meehhhh... let it overflow...
	#Z_SCALE_SHIFT   = 31;
	#Z_SCALE_ONE     = shl(1, Z_SCALE_SHIFT);

	Z_WINDOW_KAISER           =  0.0;
	Z_WINDOW_BLACKMAN_NUTTALL = -1.0;
	Z_WINDOW_NUTTALL          = -2.0;
	Z_WINDOW_BLACKMAN_HARRIS  = -3.0;
	Z_WINDOW_BLACKMAN         = -4.0;
	Z_WINDOW_HAMMING          = -5.0;
	Z_WINDOW_HANN             = -6.0;

	Z_WINDOWS["blackman_nuttall"] = Z_WINDOW_BLACKMAN_NUTTALL;
	Z_WINDOWS["nuttall"]          = Z_WINDOW_NUTTALL;
	Z_WINDOWS["blackman_harris"]  = Z_WINDOW_BLACKMAN_HARRIS;
	Z_WINDOWS["blackman"]         = Z_WINDOW_BLACKMAN;
	Z_WINDOWS["hamming"]          = Z_WINDOW_HAMMING;
	Z_WINDOWS["hann"]             = Z_WINDOW_HANN;

	Z_KAISER_2_BLACKMAN_BETA  = 8.568611;
	Z_KAISER_2_BLACKMAN_NUTTALL_BETA = 11.98;

	Z_KAISER_ATTN_DEFAULT = 100;
	Z_KAISER_BETA_DEFAULT = kaiserAttn2Beta(Z_KAISER_ATTN_DEFAULT);

	Z_KAISER_EPSILON = 1e-21;

	#
	# This is practically a joke.
	#
	Z_NYQUIST_HOVER = 0.0;

	smallest = 10.000000;
	largest  =  0.000010;
	largest_interp = 0;

	if (ARGC < 2) {
		ARGC = 1;
		ARGV[ARGC++] = "100:8:0.85";
		ARGV[ARGC++] = "100:36:0.92";
		ARGV[ARGC++] = "100:164:0.97";
		#ARGV[ARGC++] = "100:8";
		#ARGV[ARGC++] = "100:16";
		#ARGV[ARGC++] = "100:32:0.7929";
		#ARGV[ARGC++] = "100:64:0.8990";
		#ARGV[ARGC++] = "100:128:0.9499";
	}

	printf("#ifndef _FEEDER_RATE_GEN_H_\n");
	printf("#define _FEEDER_RATE_GEN_H_\n\n");
	printf("/*\n");
	printf(" * Generated using feeder_rate_mkfilter.awk, heaven, wind and awesome.\n");
	printf(" *\n");
	printf(" * DO NOT EDIT!\n");
	printf(" */\n\n");
	printf("#define FEEDER_RATE_PRESETS\t\"");
	for (i = 1; i < ARGC; i++)
		printf("%s%s", (i == 1) ? "" : " ", ARGV[i]); 
	printf("\"\n\n");
	imp["quality"] = 2;
	for (i = 1; i < ARGC; i++) {
		if (filter_parse(ARGV[i]) == 0) {
			beta = Popts["beta"];
			nmult = Popts["nmult"];
			rolloff = Popts["rolloff"];
			if (Z_DRIFT_SHIFT == -1)
				init_drift(Z_DRIFT_SHIFT_DEFAULT);
			if (Z_COEFF_SHIFT == 0)
				init_coeff_bit(Z_COEFF_SHIFT_DEFAULT);
			if (Z_ACCUMULATOR_BIT == 0)
				init_accum_bit(Z_ACCUMULATOR_BIT_DEFAULT);
			ztab[imp["quality"] - 2] =				\
			    mkfilter(imp, nmult, rolloff, beta, Z_DRIFT_ONE);
			imp["quality"]++;
		}
	}

	printf("\n");
	#
	# XXX
	#
	#if (length(ztab) > 0) {
	#	j = 0;
	#	for (i = 0; i < length(ztab); i++) {
	#		if (ztab[i] > j)
	#			j = ztab[i];
	#	}
	#	printf("static int32_t z_coeff_zero[%d] = {", j);
	#	for (i = 0; i < j; i++) {
	#		if ((i % 19) == 0)
	#			printf("\n");
	#		printf("  0,");
	#	}
	#	printf("\n};\n\n");
	#}
	#
	# XXX
	#
	printf("static const struct {\n");
	printf("\tint32_t len;\n");
	printf("\tint32_t *coeff;\n");
	printf("\tint32_t *dcoeff;\n");
	printf("} z_coeff_tab[] = {\n");
	if (length(ztab) > 0) {
		j = 0;
		for (i = 0; i < length(ztab); i++) {
			if (ztab[i] > j)
				j = ztab[i];
		}
		j = length(sprintf("%d", j));
		lfmt = sprintf("%%%dd", j);
		j = length(sprintf("z_coeff_q%d", length(ztab) + 1));
		zcfmt = sprintf("%%-%ds", j);
		zdcfmt = sprintf("%%-%ds", j + 1);

		for (i = 0; i < length(ztab); i++) {
			l = sprintf(lfmt, ztab[i]);
			zc = sprintf("z_coeff_q%d", i + 2);
			zc = sprintf(zcfmt, zc);
			zdc = sprintf("z_dcoeff_q%d", i + 2);
			zdc = sprintf(zdcfmt, zdc);
			printf("\t{ %s, %s, %s },\n", l, zc, zdc);
		}
	} else
		printf("\t{ 0, NULL, NULL }\n");
	printf("};\n\n");

	#Z_UNSHIFT = 0;
	#v = shr(Z_ONE - 1, Z_UNSHIFT) * abs(largest_interp);
	#while (v < 0 || v > INT32_MAX) {
	#	Z_UNSHIFT += 1;
	#	v = shr(Z_ONE - 1, Z_UNSHIFT) * abs(largest_interp);
	#}
	v = ((Z_ONE - 1) * abs(largest_interp)) / INT32_MAX;
	Z_UNSHIFT = ceil(log(v) / log(2.0));
	Z_INTERP_SHIFT = Z_SHIFT - Z_UNSHIFT + Z_INTERP_COEFF_SHIFT;
	
	Z_INTERP_UNSHIFT = (Z_SHIFT - Z_UNSHIFT) + Z_INTERP_COEFF_SHIFT	\
	    - Z_COEFF_SHIFT;

	printf("#define Z_COEFF_TAB_SIZE\t\t\t\t\t\t\\\n");
	printf("\t((int32_t)(sizeof(z_coeff_tab) /");
	printf(" sizeof(z_coeff_tab[0])))\n\n");
	printf("#define Z_COEFF_OFFSET\t\t%d\n\n", Z_COEFF_OFFSET);
	printf("#define Z_RSHIFT(x, y)\t\t(((x) + "			\
	    "(1 << ((y) - 1))) >> (y))\n");
	printf("#define Z_RSHIFT_L(x, y)\t(((x) + "			\
	    "(1LL << ((y) - 1))) >> (y))\n\n");
	printf("#define Z_FULL_SHIFT\t\t%d\n", Z_FULL_SHIFT);
	printf("#define Z_FULL_ONE\t\t0x%08x%s\n", Z_FULL_ONE,		\
	    (Z_FULL_ONE > INT32_MAX) ? "U" : "");
	printf("\n");
	printf("#define Z_DRIFT_SHIFT\t\t%d\n", Z_DRIFT_SHIFT);
	#printf("#define Z_DRIFT_ONE\t\t0x%08x\n", Z_DRIFT_ONE);
	printf("\n");
	printf("#define Z_SHIFT\t\t\t%d\n", Z_SHIFT);
	printf("#define Z_ONE\t\t\t0x%08x\n", Z_ONE);
	printf("#define Z_MASK\t\t\t0x%08x\n", Z_MASK);
	printf("\n");
	printf("#define Z_COEFF_SHIFT\t\t%d\n", Z_COEFF_SHIFT);
	zinterphp = "(z) * (d)";
	zinterpunshift = Z_SHIFT + Z_INTERP_COEFF_SHIFT - Z_COEFF_SHIFT;
	if (zinterpunshift > 0) {
		v = (Z_ONE - 1) * abs(largest_interp);
		if (v < INT32_MIN || v > INT32_MAX)
			zinterphp = sprintf("(int64_t)%s", zinterphp);
		zinterphp = sprintf("(%s) >> %d", zinterphp, zinterpunshift);
	} else if (zinterpunshift < 0)
		zinterphp = sprintf("(%s) << %d", zinterphp,		\
		    abs(zinterpunshift));
	if (Z_UNSHIFT == 0)
		zinterp = "z";
	else
		zinterp = sprintf("(z) >> %d", Z_UNSHIFT);
	zinterp = sprintf("(%s) * (d)", zinterp);
	if (Z_INTERP_UNSHIFT < 0)
		zinterp = sprintf("(%s) << %d", zinterp,		\
		    abs(Z_INTERP_UNSHIFT));
	else if (Z_INTERP_UNSHIFT > 0)
		zinterp = sprintf("(%s) >> %d", zinterp, Z_INTERP_UNSHIFT);
	if (zinterphp != zinterp) {
		printf("\n#ifdef SND_FEEDER_RATE_HP\n");
		printf("#define Z_COEFF_INTERPOLATE(z, c, d)"		\
		    "\t\t\t\t\t\\\n\t((c) + (%s))\n", zinterphp);
		printf("#else\n");
		printf("#define Z_COEFF_INTERPOLATE(z, c, d)"		\
		    "\t\t\t\t\t\\\n\t((c) + (%s))\n", zinterp);
		printf("#endif\n");
	} else
		printf("#define Z_COEFF_INTERPOLATE(z, c, d)"		\
		    "\t\t\t\t\t\\\n\t((c) + (%s))\n", zinterp);
	#printf("\n");
	#printf("#define Z_SCALE_SHIFT\t\t%d\n", Z_SCALE_SHIFT);
	#printf("#define Z_SCALE_ONE\t\t0x%08x%s\n", Z_SCALE_ONE,	\
	#    (Z_SCALE_ONE > INT32_MAX) ? "U" : "");
	printf("\n");
	printf("#define Z_SCALE_CAST(s)\t\t((uint32_t)(s))\n");
	genscale(8);
	genscale(16);
	genscale(24);
	genscale(32);
	printf("\n");
	printf("#define Z_ACCUMULATOR_BIT\t%d\n\n", Z_ACCUMULATOR_BIT)
	for (i = 8; i <= 32; i += 8) {
		gbit = ((i + Z_COEFF_SHIFT) > Z_ACCUMULATOR_BIT) ?	\
		    (i - (Z_ACCUMULATOR_BIT - Z_COEFF_SHIFT)) : 0;
		printf("#define Z_GUARD_BIT_%d\t\t%d\n", i, gbit);
		if (gbit == 0)
			printf("#define Z_NORM_%d(v)\t\t(v)\n\n", i);
		else
			printf("#define Z_NORM_%d(v)\t\t"		\
			    "((v) >> Z_GUARD_BIT_%d)\n\n", i, i);
	}
	printf("\n");
	printf("#define Z_LINEAR_FULL_ONE\t0x%08xU\n", Z_LINEAR_FULL_ONE);
	printf("#define Z_LINEAR_SHIFT\t\t%d\n", Z_LINEAR_SHIFT);
	printf("#define Z_LINEAR_UNSHIFT\t%d\n", Z_LINEAR_UNSHIFT);
	printf("#define Z_LINEAR_ONE\t\t0x%08x\n", Z_LINEAR_ONE);
	printf("\n");
	printf("#ifdef SND_PCM_64\n");
	genlerp(8, 1);
	genlerp(16, 1);
	genlerp(24, 1);
	genlerp(32, 1);
	printf("#else\t/* !SND_PCM_64 */\n");
	genlerp(8, 0);
	genlerp(16, 0);
	genlerp(24, 0);
	genlerp(32, 0);
	printf("#endif\t/* SND_PCM_64 */\n");
	printf("\n");
	printf("#define Z_QUALITY_ZOH\t\t0\n");
	printf("#define Z_QUALITY_LINEAR\t1\n");
	printf("#define Z_QUALITY_SINC\t\t%d\n",			\
	    floor((length(ztab) - 1) / 2) + 2);
	printf("\n");
	printf("#define Z_QUALITY_MIN\t\t0\n");
	printf("#define Z_QUALITY_MAX\t\t%d\n", length(ztab) + 1);
	if (Z_COEFF_INTERPOLATOR != 0)
		printf("\n#define Z_COEFF_INTERP_%s\t1\n",		\
		    Z_COEFF_INTERPOLATOR);
	printf("\n/*\n * smallest: %.32f\n *  largest: %.32f\n *\n",	\
	    smallest, largest);
	printf(" * z_unshift=%d, z_interp_shift=%d\n *\n",		\
	    Z_UNSHIFT, Z_INTERP_SHIFT);
	v = shr(Z_ONE - 1, Z_UNSHIFT) * abs(largest_interp);
	printf(" * largest interpolation multiplication: %d\n */\n", v);
	if (v < INT32_MIN || v > INT32_MAX) {
		printf("\n#ifndef SND_FEEDER_RATE_HP\n");
		printf("#error interpolation overflow, please reduce"	\
		    " Z_INTERP_SHIFT\n");
		printf("#endif\n");
	}

	printf("\n#endif /* !_FEEDER_RATE_GEN_H_ */\n");
}
