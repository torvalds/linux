#!/usr/bin/awk -f
#
# SPDX-License-Identifier: BSD-2-Clause-FreeBSD
#
# Copyright (c) 2008-2009 Ariff Abdullah <ariff@FreeBSD.org>
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
# Biquad coefficients generator for Parametric Software Equalizer. Not as ugly
# as 'feeder_rate_mkfilter.awk'
#
# Based on:
#
#  "Cookbook formulae for audio EQ biquad filter coefficients"
#    by Robert Bristow-Johnson  <rbj@audioimagination.com>
#
#    -  http://www.musicdsp.org/files/Audio-EQ-Cookbook.txt
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

function feedeq_w0(fc, rate)
{
	return ((2.0 * M_PI * fc) / (1.0 * rate));
}

function feedeq_A(gain, A)
{
	if (FEEDEQ_TYPE == FEEDEQ_TYPE_PEQ || FEEDEQ_TYPE == FEEDEQ_TYPE_SHELF)
		A = pow(10, gain / 40.0);
	else
		A = sqrt(pow(10, gain / 20.0));

	return (A);
}

function feedeq_alpha(w0, A, QS)
{
	if (FEEDEQ_TYPE == FEEDEQ_TYPE_PEQ)
		alpha = sin(w0) / (2.0 * QS);
	else if (FEEDEQ_TYPE == FEEDEQ_TYPE_SHELF)
		alpha = sin(w0) * 0.5 * sqrt(A + ((1.0 / A) *		\
		    ((1.0 / QS) - 1.0)) + 2.0);
	else
		alpha = 0.0;

	return (alpha);
}

function feedeq_fx_floor(v, r)
{
	if (fabs(v) < fabs(smallest))
		smallest = v;
	if (fabs(v) > fabs(largest))
		largest = v;

	r = floor((v * FEEDEQ_COEFF_ONE) + 0.5);

	if (r < INT32_MIN || r > INT32_MAX)
		printf("\n#error overflow v=%f, "			\
		    "please reduce FEEDEQ_COEFF_SHIFT\n", v);

	return (r);
}

function feedeq_gen_biquad_coeffs(coeffs, rate, gain,			\
    w0, A, alpha, a0, a1, a2, b0, b1, b2)
{
	w0    = feedeq_w0(FEEDEQ_TREBLE_SFREQ, 1.0 * rate);
	A     = feedeq_A(1.0 * gain);
	alpha = feedeq_alpha(w0, A, FEEDEQ_TREBLE_SLOPE);

	if (FEEDEQ_TYPE == FEEDEQ_TYPE_PEQ) {
		b0 =  1.0 + (alpha * A);
		b1 = -2.0 * cos(w0);
		b2 =  1.0 - (alpha * A);
		a0 =  1.0 + (alpha / A);
		a1 = -2.0 * cos(w0);
		a2 =  1.0 - (alpha / A);
	} else if (FEEDEQ_TYPE == FEEDEQ_TYPE_SHELF) {
		b0 =      A*((A+1.0)+((A-1.0)*cos(w0))+(2.0*sqrt(A)*alpha));
		b1 = -2.0*A*((A-1.0)+((A+1.0)*cos(w0))                    );
		b2 =      A*((A+1.0)+((A-1.0)*cos(w0))-(2.0*sqrt(A)*alpha));
		a0 =         (A+1.0)-((A-1.0)*cos(w0))+(2.0*sqrt(A)*alpha );
		a1 =  2.0 * ((A-1.0)-((A+1.0)*cos(w0))                    );
		a2 =         (A+1.0)-((A-1.0)*cos(w0))-(2.0*sqrt(A)*alpha );
	} else
		b0 = b1 = b2 = a0 = a1 = a2 = 0.0;

	b0 /= a0;
	b1 /= a0;
	b2 /= a0;
	a1 /= a0;
	a2 /= a0;

	coeffs["treble", gain, 0] = feedeq_fx_floor(a0);
	coeffs["treble", gain, 1] = feedeq_fx_floor(a1);
	coeffs["treble", gain, 2] = feedeq_fx_floor(a2);
	coeffs["treble", gain, 3] = feedeq_fx_floor(b0);
	coeffs["treble", gain, 4] = feedeq_fx_floor(b1);
	coeffs["treble", gain, 5] = feedeq_fx_floor(b2);

	w0    = feedeq_w0(FEEDEQ_BASS_SFREQ, 1.0 * rate);
	A     = feedeq_A(1.0 * gain);
	alpha = feedeq_alpha(w0, A, FEEDEQ_BASS_SLOPE);

	if (FEEDEQ_TYPE == FEEDEQ_TYPE_PEQ) {
		b0 =  1.0 + (alpha * A);
		b1 = -2.0 * cos(w0);
		b2 =  1.0 - (alpha * A);
		a0 =  1.0 + (alpha / A);
		a1 = -2.0 * cos(w0);
		a2 =  1.0 - (alpha / A);
	} else if (FEEDEQ_TYPE == FEEDEQ_TYPE_SHELF) {
		b0 =      A*((A+1.0)-((A-1.0)*cos(w0))+(2.0*sqrt(A)*alpha));
		b1 =  2.0*A*((A-1.0)-((A+1.0)*cos(w0))                    );
		b2 =      A*((A+1.0)-((A-1.0)*cos(w0))-(2.0*sqrt(A)*alpha));
		a0 =         (A+1.0)+((A-1.0)*cos(w0))+(2.0*sqrt(A)*alpha );
		a1 = -2.0 * ((A-1.0)+((A+1.0)*cos(w0))                    );
		a2 =         (A+1.0)+((A-1.0)*cos(w0))-(2.0*sqrt(A)*alpha );
	} else
		b0 = b1 = b2 = a0 = a1 = a2 = 0.0;

	b0 /= a0;
	b1 /= a0;
	b2 /= a0;
	a1 /= a0;
	a2 /= a0;

	coeffs["bass", gain, 0] = feedeq_fx_floor(a0);
	coeffs["bass", gain, 1] = feedeq_fx_floor(a1);
	coeffs["bass", gain, 2] = feedeq_fx_floor(a2);
	coeffs["bass", gain, 3] = feedeq_fx_floor(b0);
	coeffs["bass", gain, 4] = feedeq_fx_floor(b1);
	coeffs["bass", gain, 5] = feedeq_fx_floor(b2);
}

function feedeq_gen_freq_coeffs(frq, g, i, v)
{
	coeffs[0] = 0;

	for (g = (FEEDEQ_GAIN_MIN * FEEDEQ_GAIN_DIV);			\
	    g <= (FEEDEQ_GAIN_MAX * FEEDEQ_GAIN_DIV);			\
	    g += FEEDEQ_GAIN_STEP) {
		feedeq_gen_biquad_coeffs(coeffs, frq,			\
		    g * FEEDEQ_GAIN_RECIPROCAL);
	}

	printf("\nstatic struct feed_eq_coeff eq_%d[%d] "		\
	    "= {\n", frq, FEEDEQ_LEVELS);
	for (g = (FEEDEQ_GAIN_MIN * FEEDEQ_GAIN_DIV);			\
	    g <= (FEEDEQ_GAIN_MAX * FEEDEQ_GAIN_DIV);			\
	    g += FEEDEQ_GAIN_STEP) {
		printf("     {{ ");
		for (i = 1; i < 6; i++) {
			v = coeffs["treble", g * FEEDEQ_GAIN_RECIPROCAL, i];
			printf("%s0x%08x%s",				\
			    (v < 0) ? "-" : " ", abs(v),		\
			    (i == 5) ? " " : ", ");
		}
		printf("},\n      { ");
		for (i = 1; i < 6; i++) {
			v = coeffs["bass", g * FEEDEQ_GAIN_RECIPROCAL, i];
			printf("%s0x%08x%s",				\
			    (v < 0) ? "-" : " ", abs(v),		\
			    (i == 5) ? " " : ", ");
		}
		printf("}}%s\n",					\
		    (g < (FEEDEQ_GAIN_MAX * FEEDEQ_GAIN_DIV)) ? "," : "");
	}
	printf("};\n");
}

function feedeq_calc_preamp(norm, gain, shift, mul, bit, attn)
{
	shift = FEEDEQ_PREAMP_SHIFT;

	if (floor(FEEDEQ_PREAMP_BITDB) == 6 &&				\
	    (1.0 * floor(gain)) == gain && (floor(gain) % 6) == 0) {
		mul = 1;
		shift = floor(floor(gain) / 6);
	} else {
		bit = 32.0 - ((1.0 * gain) / (1.0 * FEEDEQ_PREAMP_BITDB));
		attn = pow(2.0, bit) / pow(2.0, 32.0);
		mul = floor((attn * FEEDEQ_PREAMP_ONE) + 0.5);
	}

	while ((mul % 2) == 0 && shift > 0) {
		mul = floor(mul / 2);
		shift--;
	}

	norm["mul"] = mul;
	norm["shift"] = shift;
}

BEGIN {
	M_PI = atan2(0.0, -1.0);

	INT32_MAX = 1 + ((shl(1, 30) - 1) * 2);
	INT32_MIN = -1 - INT32_MAX;

	FEEDEQ_TYPE_PEQ   = 0;
	FEEDEQ_TYPE_SHELF = 1;

	FEEDEQ_TYPE       = FEEDEQ_TYPE_PEQ;

	FEEDEQ_COEFF_SHIFT = 24;
	FEEDEQ_COEFF_ONE   = shl(1, FEEDEQ_COEFF_SHIFT);

	FEEDEQ_PREAMP_SHIFT = 31;
	FEEDEQ_PREAMP_ONE   = shl(1, FEEDEQ_PREAMP_SHIFT);
	FEEDEQ_PREAMP_BITDB = 6; # 20.0 * (log(2.0) / log(10.0));

	FEEDEQ_GAIN_DIV   = 10;
	i = 0;
	j = 1;
	while (j < FEEDEQ_GAIN_DIV) {
		j *= 2;
		i++;
	}
	FEEDEQ_GAIN_SHIFT = i;
	FEEDEQ_GAIN_FMASK = shl(1, FEEDEQ_GAIN_SHIFT) - 1;

	FEEDEQ_GAIN_RECIPROCAL = 1.0 / FEEDEQ_GAIN_DIV;

	if (ARGC == 2) {
		i = 1;
		split(ARGV[1], arg, ":");
		while (match(arg[i], "^[^0-9]*$")) {
			if (arg[i] == "PEQ") {
				FEEDEQ_TYPE = FEEDEQ_TYPE_PEQ;
			} else if (arg[i] == "SHELF") {
				FEEDEQ_TYPE = FEEDEQ_TYPE_SHELF;
			}
			i++;
		}
		split(arg[i++], subarg, ",");
		FEEDEQ_TREBLE_SFREQ = 1.0 * subarg[1];
		FEEDEQ_TREBLE_SLOPE = 1.0 * subarg[2];
		split(arg[i++], subarg, ",");
		FEEDEQ_BASS_SFREQ = 1.0 * subarg[1];
		FEEDEQ_BASS_SLOPE = 1.0 * subarg[2];
		split(arg[i++], subarg, ",");
		FEEDEQ_GAIN_MIN = floor(1.0 * subarg[1]);
		FEEDEQ_GAIN_MAX = floor(1.0 * subarg[2]);
		if (length(subarg) > 2) {
			j = floor(1.0 * FEEDEQ_GAIN_DIV * subarg[3]);
			if (j < 2)
				j = 1;
			else if (j < 5)
				j = 2;
			else if (j < 10)
				j = 5;
			else
				j = 10;
			if (j > FEEDEQ_GAIN_DIV || (FEEDEQ_GAIN_DIV % j) != 0)
				j = FEEDEQ_GAIN_DIV;
			FEEDEQ_GAIN_STEP = j;
		} else
			FEEDEQ_GAIN_STEP = FEEDEQ_GAIN_DIV;
		split(arg[i], subarg, ",");
		for (i = 1; i <= length(subarg); i++)
			allfreq[i - 1] = floor(1.0 * subarg[i]);
	} else {
		FEEDEQ_TREBLE_SFREQ  = 16000.0;
		FEEDEQ_TREBLE_SLOPE  = 0.25;
		FEEDEQ_BASS_SFREQ    = 62.0;
		FEEDEQ_BASS_SLOPE    = 0.25;

		FEEDEQ_GAIN_MIN  = -9;
		FEEDEQ_GAIN_MAX  = 9;

		FEEDEQ_GAIN_STEP = FEEDEQ_GAIN_DIV;


		allfreq[0] = 44100;
		allfreq[1] = 48000;
		allfreq[2] = 88200;
		allfreq[3] = 96000;
		allfreq[4] = 176400;
		allfreq[5] = 192000;
	}

	FEEDEQ_LEVELS = ((FEEDEQ_GAIN_MAX - FEEDEQ_GAIN_MIN) *		\
	    floor(FEEDEQ_GAIN_DIV / FEEDEQ_GAIN_STEP)) + 1;

	FEEDEQ_ERR_CLIP = 0;

	smallest = 10.000000;
	largest  =  0.000010;

	printf("#ifndef _FEEDER_EQ_GEN_H_\n");
	printf("#define _FEEDER_EQ_GEN_H_\n\n");
	printf("/*\n");
	printf(" * Generated using feeder_eq_mkfilter.awk, heaven, wind and awesome.\n");
	printf(" *\n");
	printf(" * DO NOT EDIT!\n");
	printf(" */\n\n");
	printf("/*\n");
	printf(" * EQ: %s\n", (FEEDEQ_TYPE == FEEDEQ_TYPE_SHELF) ?	\
	    "Shelving" : "Peaking EQ");
	printf(" */\n");
	printf("#define FEEDER_EQ_PRESETS\t\"");
	printf("%s:%d,%.4f,%d,%.4f:%d,%d,%.1f:",			\
	    (FEEDEQ_TYPE == FEEDEQ_TYPE_SHELF) ? "SHELF" : "PEQ",	\
	    FEEDEQ_TREBLE_SFREQ, FEEDEQ_TREBLE_SLOPE,			\
	    FEEDEQ_BASS_SFREQ, FEEDEQ_BASS_SLOPE,			\
	    FEEDEQ_GAIN_MIN, FEEDEQ_GAIN_MAX,				\
	    FEEDEQ_GAIN_STEP * FEEDEQ_GAIN_RECIPROCAL);
	for (i = 0; i < length(allfreq); i++) {
		if (i != 0)
			printf(",");
		printf("%d", allfreq[i]);
	}
	printf("\"\n\n");
	printf("struct feed_eq_coeff_tone {\n");
	printf("\tint32_t a1, a2;\n");
	printf("\tint32_t b0, b1, b2;\n");
	printf("};\n\n");
	printf("struct feed_eq_coeff {\n");
	#printf("\tstruct {\n");
	#printf("\t\tint32_t a1, a2;\n");
	#printf("\t\tint32_t b0, b1, b2;\n");
	#printf("\t} treble, bass;\n");
	printf("\tstruct feed_eq_coeff_tone treble;\n");
	printf("\tstruct feed_eq_coeff_tone bass;\n");
	#printf("\tstruct {\n");
	#printf("\t\tint32_t a1, a2;\n");
	#printf("\t\tint32_t b0, b1, b2;\n");
	#printf("\t} bass;\n");
	printf("};\n");
	for (i = 0; i < length(allfreq); i++)
		feedeq_gen_freq_coeffs(allfreq[i]);
	printf("\n");
	printf("static const struct {\n");
	printf("\tuint32_t rate;\n");
	printf("\tstruct feed_eq_coeff *coeff;\n");
	printf("} feed_eq_tab[] = {\n");
	for (i = 0; i < length(allfreq); i++) {
		printf("\t{ %6d, eq_%-6d },\n", allfreq[i], allfreq[i]);
	}
	printf("};\n");

	printf("\n#define FEEDEQ_RATE_MIN\t\t%d\n", allfreq[0]);
	printf("#define FEEDEQ_RATE_MAX\t\t%d\n", allfreq[length(allfreq) - 1]);
	printf("\n#define FEEDEQ_TAB_SIZE\t\t\t\t\t\t\t\\\n");
	printf("\t((int32_t)(sizeof(feed_eq_tab) / sizeof(feed_eq_tab[0])))\n");

	printf("\nstatic const struct {\n");
	printf("\tint32_t mul, shift;\n");
	printf("} feed_eq_preamp[] = {\n");
	for (i = (FEEDEQ_GAIN_MAX * 2 * FEEDEQ_GAIN_DIV); i >= 0;	\
	    i -= FEEDEQ_GAIN_STEP) {
		feedeq_calc_preamp(norm, i * FEEDEQ_GAIN_RECIPROCAL);
		dbgain = ((FEEDEQ_GAIN_MAX * FEEDEQ_GAIN_DIV) - i) *	\
		    FEEDEQ_GAIN_RECIPROCAL;
		printf("\t{ 0x%08x, 0x%08x },\t/* %+5.1f dB */\n",	\
		    norm["mul"], norm["shift"], dbgain);
	}
	printf("};\n");

	printf("\n#define FEEDEQ_GAIN_MIN\t\t%d", FEEDEQ_GAIN_MIN);
	printf("\n#define FEEDEQ_GAIN_MAX\t\t%d\n", FEEDEQ_GAIN_MAX);

	printf("\n#define FEEDEQ_GAIN_SHIFT\t%d\n", FEEDEQ_GAIN_SHIFT);
	printf("#define FEEDEQ_GAIN_DIV\t\t%d\n", FEEDEQ_GAIN_DIV);
	printf("#define FEEDEQ_GAIN_FMASK\t0x%08x\n", FEEDEQ_GAIN_FMASK);
	printf("#define FEEDEQ_GAIN_STEP\t%d\n", FEEDEQ_GAIN_STEP);

	#printf("\n#define FEEDEQ_PREAMP_MIN\t-%d\n",			\
	#    shl(FEEDEQ_GAIN_MAX, FEEDEQ_GAIN_SHIFT));
	#printf("#define FEEDEQ_PREAMP_MAX\t%d\n",			\
	#    shl(FEEDEQ_GAIN_MAX, FEEDEQ_GAIN_SHIFT));

	printf("\n#define FEEDEQ_COEFF_SHIFT\t%d\n", FEEDEQ_COEFF_SHIFT);

	#feedeq_calc_preamp(norm, FEEDEQ_GAIN_MAX);

	#printf("#define FEEDEQ_COEFF_NORM(v)\t(");
	#if (norm["mul"] == 1)
	#	printf("(v) >> %d", norm["shift"]);
	#else
	#	printf("(0x%xLL * (v)) >> %d", norm["mul"], norm["shift"]);
	#printf(")\n");

	#printf("\n#define FEEDEQ_LEVELS\t\t%d\n", FEEDEQ_LEVELS);
	if (FEEDEQ_ERR_CLIP != 0)
		printf("\n#define FEEDEQ_ERR_CLIP\t\t%d\n", FEEDEQ_ERR_CLIP);
	printf("\n/*\n");
	printf(" * volume level mapping (0 - 100):\n");
	printf(" *\n");

	for (i = 0; i <= 100; i++) {
		ind = floor((i * FEEDEQ_LEVELS) / 100);
		if (ind >= FEEDEQ_LEVELS)
			ind = FEEDEQ_LEVELS - 1;
		printf(" *\t%3d  ->  %3d (%+5.1f dB)\n",		\
		    i, ind, FEEDEQ_GAIN_MIN +				\
		    (ind * (FEEDEQ_GAIN_RECIPROCAL * FEEDEQ_GAIN_STEP)));
	}

	printf(" */\n");
	printf("\n/*\n * smallest: %.32f\n *  largest: %.32f\n */\n",	\
	    smallest, largest);
	printf("\n#endif\t/* !_FEEDER_EQ_GEN_H_ */\n");
}
