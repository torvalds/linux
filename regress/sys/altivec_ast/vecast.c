/*	$OpenBSD: vecast.c,v 1.1 2022/10/22 17:50:28 gkoehler Exp $	*/

/*
 * Copyright (c) 2022 George Koehler <gkoehler@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <altivec.h>
#include <err.h>
#include <math.h>
#include <stdio.h>

struct double4 {
	double		d[4];
};

union vu {
	vector float	vf;
	vector int	vi;
	vector unsigned	vu;
	float		f[4];
	int		i[4];
	unsigned	u[4];
};

#define AD(a, b, c, d)	(struct double4){a, b, c, d}
#define VF(a, b, c, d)	(vector float)(a, b, c, d)
#define VI(a, b, c, d)	(vector int)(a, b, c, d)
#define VU(a, b, c, d)	(vector unsigned)(a, b, c, d)
#define rsqrt(f)	(1.0 / sqrt(f))

int fail;

void
ck_equal(const char *what, vector float out, vector float answer)
{
	if (vec_any_ne(out, answer)) {
		union vu a, b;

		a.vf = out;
		b.vf = answer;
		warnx("%s: {%a, %a, %a, %a} should be {%a, %a, %a, %a}",
		    what, a.f[0], a.f[1], a.f[2], a.f[3],
		    b.f[0], b.f[1], b.f[2], b.f[3]);
		fail = 1;
	}
}

void
ck_equal_i(const char *what, vector int out, vector int answer)
{
	if (vec_any_ne(out, answer)) {
		union vu a, b;

		a.vi = out;
		b.vi = answer;
		warnx("%s: {%d, %d, %d, %d} should be {%d, %d, %d, %d}",
		    what, a.i[0], a.i[1], a.i[2], a.i[3],
		    b.i[0], b.i[1], b.i[2], b.i[3]);
		fail = 1;
	}
}

void
ck_equal_u(const char *what, vector unsigned out, vector unsigned answer)
{
	if (vec_any_ne(out, answer)) {
		union vu a, b;

		a.vi = out;
		b.vi = answer;
		warnx("%s: {%u, %u, %u, %u} should be {%u, %u, %u, %u}",
		    what, a.u[0], a.u[1], a.u[2], a.u[3],
		    b.u[0], b.u[1], b.u[2], b.u[3]);
		fail = 1;
	}
}

enum error_check {REL_1_IN, ABS_1_IN};

/* Checks that error is at most 1 in err_den. */
void
ck_estimate(const char *what, vector float out, struct double4 answer,
    enum error_check which, double err_den)
{
	union vu u;
	int i, warned = 0;

	u.vf = out;
	for (i = 0; i < 4; i++) {
		double estimate = u.f[i];
		double target = answer.d[i];
		double error;

		switch (which) {
		case REL_1_IN:	/* relative error */
			error = fabs(target / (estimate - target));
			break;
		case ABS_1_IN:	/* absolute error */
			error = fabs(1 / (estimate - target));
			break;
		default:
			errx(1, "invalid check");
		}

		if (error < err_den) {
			if (!warned) {
				warnx("%s: {%a, %a, %a, %a} should be "
				    "near {%a, %a, %a, %a} (1/%g)",
				    what, u.f[0], u.f[1], u.f[2], u.f[3],
				    answer.d[0], answer.d[1], answer.d[2],
				    answer.d[3], err_den);
				warned = 1;
				fail = 1;
			}
			warnx("%a is off %a by 1/%g", estimate, target,
			    error);
		}
	}
}

/*
 * Tries altivec with denormal or subnormal floats.
 * These are single-precision floats f, where
 *   0 < |f| < 2**-126 = 0x1p-126 = 0x10p-130 = 1.17549435E-38F
 */
int
main(void)
{
	struct double4 dan;
	volatile vector float in1, in2, in3;
	vector float ans;
	vector int ian;
	vector unsigned uan;

	/* in1 + in2 */
	in1 = VF(10, 0x10p-140, 0x20p-130, -0x2000p-134);
	in2 = VF( 4,  0x5p-140, -0x1p-130,  0x1fffp-134);
	ans = VF(14, 0x15p-140, 0x1fp-130,    -0x1p-134);
	ck_equal("vec_add", vec_add(in1, in2), ans);

	/* in1 - in2 */
	in1 = VF(0x4000p-134, 10, 0x10p-140,   0x3p-130);
	in2 = VF(0x3ffep-134,  4,  0x5p-140,  0x40p-130);
	ans = VF(   0x2p-134,  6,  0xbp-140, -0x3dp-130);
	ck_equal("vec_sub", vec_sub(in1, in2), ans);

	/* in1 * in2 + in3 */
	in1 = VF( 0x6p-70,   0x6p-140, 6,   0x6p-100);
	in2 = VF( 0x7p-70,   0x7p50,   7,   0x7p-30);
	in3 = VF(  0,          0,      1, -0x20p-130);
	ans = VF(0x2ap-140, 0x2ap-90, 43,   0xap-130);
	ck_equal("vec_madd", vec_madd(in1, in2, in3), ans);

	/* in3 - in1 * in2 */
	in1 = VF( 0xbp-30,    0xbp-70,   0xbp44,  11);
	in2 = VF( 0x3p-100,   0x3p-70,  -0x3p-138, 3);
	in3 = VF(0x25p-130,     0,         0,     35);
	ans = VF( 0x4p-130, -0x21p-140, 0x21p-94,  2);
	ck_equal("vec_nmsub", vec_nmsub(in1, in2, in3), ans);

	/* 1 / in1 */
	in1 = VF(      3,       0x3p126,       0x3p-126, 0x1p127);
	dan = AD(1.0 / 3, 1.0 / 0x3p126, 1.0 / 0x3p-126, 0x1p-127);
	ck_estimate("vec_re", vec_re(in1), dan, REL_1_IN, 4096);

	/* 1 / sqrt(in1) */
	in1 = VF(1,       2,        0x1p-128,        0x5p-135);
	dan = AD(1, rsqrt(2), rsqrt(0x1p-128), rsqrt(0x5p-135));
	ck_estimate("vec_rsqrt", vec_rsqrte(in1), dan, REL_1_IN, 4096);

	/* log2(in1) */
	in1 = VF(0x1p-130, 0x1p-149, 32, 0x1p-10);
	dan = AD(    -130,     -149,  5,     -10);
	ck_estimate("vec_loge", vec_loge(in1), dan, ABS_1_IN, 32);
	in1 = VF(     0x123p-139,       0xabcp-145,  1, 1);
	dan = AD(log2(0x123p-139), log2(0xabcp-145), 0, 0);
	ck_estimate("vec_loge", vec_loge(in1), dan, ABS_1_IN, 32);

	/* 2**in1 */
	in1 = VF(    -149,     -138,     -127,   10);
	ans = VF(0x1p-149, 0x1p-138, 0x1p-127, 1024);
	ck_equal("vec_expte", vec_expte(in1), ans);
	in1 = VF(    -10,      -145.3,       -136.9,       -127.1);
	dan = AD(0x1p-10, exp2(-145.3), exp2(-136.9), exp2(-127.1));
	ck_estimate("vec_expte", vec_expte(in1), dan, REL_1_IN, 16);

	/* (int)(in1 * 2**exponent) */
	in1 = VF(0x1p-127, 2.34, -0xfedp-140, -19.8);
	ian = VI(  0,      2,         0,      -19);
	ck_equal_i("vec_cts", vec_cts(in1, 0), ian);
	in1 = VF(0x1p-113, -1, -0xabcp-143, 0x1fp-10);
	ian = VI(  0,   -1024,      0,      0x1f);
	ck_equal_i("vec_cts", vec_cts(in1, 10), ian);

	/* (unsigned)(in1 * 2**exponent) */
	in1 = VF(0x1.ap-130, 0x1.ep-140, 24000012, 0);
	uan = VU(  0,          0,      3072001536, 0);
	ck_equal_u("vec_ctu", vec_ctu(in1, 7), uan);

	return fail;
}
