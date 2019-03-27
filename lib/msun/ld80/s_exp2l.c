/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005-2008 David Schultz <das@FreeBSD.ORG>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <float.h>
#include <stdint.h>

#ifdef __i386__
#include <ieeefp.h>
#endif

#include "fpmath.h"
#include "math.h"
#include "math_private.h"

#define	TBLBITS	7
#define	TBLSIZE	(1 << TBLBITS)

#define	BIAS	(LDBL_MAX_EXP - 1)

static volatile long double
    huge = 0x1p10000L,
    twom10000 = 0x1p-10000L;

static const union IEEEl2bits
P1 = LD80C(0xb17217f7d1cf79ac, -1, 6.93147180559945309429e-1L);

static const double
redux = 0x1.8p63 / TBLSIZE,
/*
 * Domain [-0.00390625, 0.00390625], range ~[-1.7079e-23, 1.7079e-23]
 * |exp(x) - p(x)| < 2**-75.6
 */
P2 = 2.4022650695910072e-1,		/*  0x1ebfbdff82c58f.0p-55 */
P3 = 5.5504108664816879e-2,		/*  0x1c6b08d7049e1a.0p-57 */
P4 = 9.6181291055695180e-3,		/*  0x13b2ab6fa8321a.0p-59 */
P5 = 1.3333563089183052e-3,		/*  0x15d8806f67f251.0p-62 */
P6 = 1.5413361552277414e-4;		/*  0x1433ddacff3441.0p-65 */

static const double tbl[TBLSIZE * 2] = {
	0x1.6a09e667f3bcdp-1,	-0x1.bdd3413b2648p-55,
	0x1.6c012750bdabfp-1,	-0x1.2895667ff0cp-57,
	0x1.6dfb23c651a2fp-1,	-0x1.bbe3a683c88p-58,
	0x1.6ff7df9519484p-1,	-0x1.83c0f25860fp-56,
	0x1.71f75e8ec5f74p-1,	-0x1.16e4786887bp-56,
	0x1.73f9a48a58174p-1,	-0x1.0a8d96c65d5p-55,
	0x1.75feb564267c9p-1,	-0x1.0245957316ep-55,
	0x1.780694fde5d3fp-1,	 0x1.866b80a0216p-55,
	0x1.7a11473eb0187p-1,	-0x1.41577ee0499p-56,
	0x1.7c1ed0130c132p-1,	 0x1.f124cd1164ep-55,
	0x1.7e2f336cf4e62p-1,	 0x1.05d02ba157ap-57,
	0x1.80427543e1a12p-1,	-0x1.27c86626d97p-55,
	0x1.82589994cce13p-1,	-0x1.d4c1dd41533p-55,
	0x1.8471a4623c7adp-1,	-0x1.8d684a341cep-56,
	0x1.868d99b4492edp-1,	-0x1.fc6f89bd4f68p-55,
	0x1.88ac7d98a6699p-1,	 0x1.994c2f37cb5p-55,
	0x1.8ace5422aa0dbp-1,	 0x1.6e9f156864bp-55,
	0x1.8cf3216b5448cp-1,	-0x1.0d55e32e9e4p-57,
	0x1.8f1ae99157736p-1,	 0x1.5cc13a2e397p-56,
	0x1.9145b0b91ffc6p-1,	-0x1.dd6792e5825p-55,
	0x1.93737b0cdc5e5p-1,	-0x1.75fc781b58p-58,
	0x1.95a44cbc8520fp-1,	-0x1.64b7c96a5fp-57,
	0x1.97d829fde4e5p-1,	-0x1.d185b7c1b86p-55,
	0x1.9a0f170ca07bap-1,	-0x1.173bd91cee6p-55,
	0x1.9c49182a3f09p-1,	 0x1.c7c46b071f2p-57,
	0x1.9e86319e32323p-1,	 0x1.824ca78e64cp-57,
	0x1.a0c667b5de565p-1,	-0x1.359495d1cd5p-55,
	0x1.a309bec4a2d33p-1,	 0x1.6305c7ddc368p-55,
	0x1.a5503b23e255dp-1,	-0x1.d2f6edb8d42p-55,
	0x1.a799e1330b358p-1,	 0x1.bcb7ecac564p-55,
	0x1.a9e6b5579fdbfp-1,	 0x1.0fac90ef7fdp-55,
	0x1.ac36bbfd3f37ap-1,	-0x1.f9234cae76dp-56,
	0x1.ae89f995ad3adp-1,	 0x1.7a1cd345dcc8p-55,
	0x1.b0e07298db666p-1,	-0x1.bdef54c80e4p-55,
	0x1.b33a2b84f15fbp-1,	-0x1.2805e3084d8p-58,
	0x1.b59728de5593ap-1,	-0x1.c71dfbbba6ep-55,
	0x1.b7f76f2fb5e47p-1,	-0x1.5584f7e54acp-57,
	0x1.ba5b030a1064ap-1,	-0x1.efcd30e5429p-55,
	0x1.bcc1e904bc1d2p-1,	 0x1.23dd07a2d9fp-56,
	0x1.bf2c25bd71e09p-1,	-0x1.efdca3f6b9c8p-55,
	0x1.c199bdd85529cp-1,	 0x1.11065895049p-56,
	0x1.c40ab5fffd07ap-1,	 0x1.b4537e083c6p-55,
	0x1.c67f12e57d14bp-1,	 0x1.2884dff483c8p-55,
	0x1.c8f6d9406e7b5p-1,	 0x1.1acbc48805cp-57,
	0x1.cb720dcef9069p-1,	 0x1.503cbd1e94ap-57,
	0x1.cdf0b555dc3fap-1,	-0x1.dd83b53829dp-56,
	0x1.d072d4a07897cp-1,	-0x1.cbc3743797a8p-55,
	0x1.d2f87080d89f2p-1,	-0x1.d487b719d858p-55,
	0x1.d5818dcfba487p-1,	 0x1.2ed02d75b37p-56,
	0x1.d80e316c98398p-1,	-0x1.11ec18bedep-55,
	0x1.da9e603db3285p-1,	 0x1.c2300696db5p-55,
	0x1.dd321f301b46p-1,	 0x1.2da5778f019p-55,
	0x1.dfc97337b9b5fp-1,	-0x1.1a5cd4f184b8p-55,
	0x1.e264614f5a129p-1,	-0x1.7b627817a148p-55,
	0x1.e502ee78b3ff6p-1,	 0x1.39e8980a9cdp-56,
	0x1.e7a51fbc74c83p-1,	 0x1.2d522ca0c8ep-55,
	0x1.ea4afa2a490dap-1,	-0x1.e9c23179c288p-55,
	0x1.ecf482d8e67f1p-1,	-0x1.c93f3b411ad8p-55,
	0x1.efa1bee615a27p-1,	 0x1.dc7f486a4b68p-55,
	0x1.f252b376bba97p-1,	 0x1.3a1a5bf0d8e8p-55,
	0x1.f50765b6e454p-1,	 0x1.9d3e12dd8a18p-55,
	0x1.f7bfdad9cbe14p-1,	-0x1.dbb12d00635p-55,
	0x1.fa7c1819e90d8p-1,	 0x1.74853f3a593p-56,
	0x1.fd3c22b8f71f1p-1,	 0x1.2eb74966578p-58,
	0x1p+0,	 0x0p+0,
	0x1.0163da9fb3335p+0,	 0x1.b61299ab8cd8p-54,
	0x1.02c9a3e778061p+0,	-0x1.19083535b08p-56,
	0x1.04315e86e7f85p+0,	-0x1.0a31c1977c98p-54,
	0x1.059b0d3158574p+0,	 0x1.d73e2a475b4p-55,
	0x1.0706b29ddf6dep+0,	-0x1.c91dfe2b13cp-55,
	0x1.0874518759bc8p+0,	 0x1.186be4bb284p-57,
	0x1.09e3ecac6f383p+0,	 0x1.14878183161p-54,
	0x1.0b5586cf9890fp+0,	 0x1.8a62e4adc61p-54,
	0x1.0cc922b7247f7p+0,	 0x1.01edc16e24f8p-54,
	0x1.0e3ec32d3d1a2p+0,	 0x1.03a1727c58p-59,
	0x1.0fb66affed31bp+0,	-0x1.b9bedc44ebcp-57,
	0x1.11301d0125b51p+0,	-0x1.6c51039449bp-54,
	0x1.12abdc06c31ccp+0,	-0x1.1b514b36ca8p-58,
	0x1.1429aaea92dep+0,	-0x1.32fbf9af1368p-54,
	0x1.15a98c8a58e51p+0,	 0x1.2406ab9eeabp-55,
	0x1.172b83c7d517bp+0,	-0x1.19041b9d78ap-55,
	0x1.18af9388c8deap+0,	-0x1.11023d1970f8p-54,
	0x1.1a35beb6fcb75p+0,	 0x1.e5b4c7b4969p-55,
	0x1.1bbe084045cd4p+0,	-0x1.95386352ef6p-54,
	0x1.1d4873168b9aap+0,	 0x1.e016e00a264p-54,
	0x1.1ed5022fcd91dp+0,	-0x1.1df98027bb78p-54,
	0x1.2063b88628cd6p+0,	 0x1.dc775814a85p-55,
	0x1.21f49917ddc96p+0,	 0x1.2a97e9494a6p-55,
	0x1.2387a6e756238p+0,	 0x1.9b07eb6c7058p-54,
	0x1.251ce4fb2a63fp+0,	 0x1.ac155bef4f5p-55,
	0x1.26b4565e27cddp+0,	 0x1.2bd339940eap-55,
	0x1.284dfe1f56381p+0,	-0x1.a4c3a8c3f0d8p-54,
	0x1.29e9df51fdee1p+0,	 0x1.612e8afad12p-55,
	0x1.2b87fd0dad99p+0,	-0x1.10adcd6382p-59,
	0x1.2d285a6e4030bp+0,	 0x1.0024754db42p-54,
	0x1.2ecafa93e2f56p+0,	 0x1.1ca0f45d524p-56,
	0x1.306fe0a31b715p+0,	 0x1.6f46ad23183p-55,
	0x1.32170fc4cd831p+0,	 0x1.a9ce78e1804p-55,
	0x1.33c08b26416ffp+0,	 0x1.327218436598p-54,
	0x1.356c55f929ff1p+0,	-0x1.b5cee5c4e46p-55,
	0x1.371a7373aa9cbp+0,	-0x1.63aeabf42ebp-54,
	0x1.38cae6d05d866p+0,	-0x1.e958d3c99048p-54,
	0x1.3a7db34e59ff7p+0,	-0x1.5e436d661f6p-56,
	0x1.3c32dc313a8e5p+0,	-0x1.efff8375d2ap-54,
	0x1.3dea64c123422p+0,	 0x1.ada0911f09fp-55,
	0x1.3fa4504ac801cp+0,	-0x1.7d023f956fap-54,
	0x1.4160a21f72e2ap+0,	-0x1.ef3691c309p-58,
	0x1.431f5d950a897p+0,	-0x1.1c7dde35f7ap-55,
	0x1.44e086061892dp+0,	 0x1.89b7a04ef8p-59,
	0x1.46a41ed1d0057p+0,	 0x1.c944bd1648a8p-54,
	0x1.486a2b5c13cdp+0,	 0x1.3c1a3b69062p-56,
	0x1.4a32af0d7d3dep+0,	 0x1.9cb62f3d1be8p-54,
	0x1.4bfdad5362a27p+0,	 0x1.d4397afec42p-56,
	0x1.4dcb299fddd0dp+0,	 0x1.8ecdbbc6a78p-54,
	0x1.4f9b2769d2ca7p+0,	-0x1.4b309d25958p-54,
	0x1.516daa2cf6642p+0,	-0x1.f768569bd94p-55,
	0x1.5342b569d4f82p+0,	-0x1.07abe1db13dp-55,
	0x1.551a4ca5d920fp+0,	-0x1.d689cefede6p-55,
	0x1.56f4736b527dap+0,	 0x1.9bb2c011d938p-54,
	0x1.58d12d497c7fdp+0,	 0x1.295e15b9a1ep-55,
	0x1.5ab07dd485429p+0,	 0x1.6324c0546478p-54,
	0x1.5c9268a5946b7p+0,	 0x1.c4b1b81698p-60,
	0x1.5e76f15ad2148p+0,	 0x1.ba6f93080e68p-54,
	0x1.605e1b976dc09p+0,	-0x1.3e2429b56de8p-54,
	0x1.6247eb03a5585p+0,	-0x1.383c17e40b48p-54,
	0x1.6434634ccc32p+0,	-0x1.c483c759d89p-55,
	0x1.6623882552225p+0,	-0x1.bb60987591cp-54,
	0x1.68155d44ca973p+0,	 0x1.038ae44f74p-57,
};

/**
 * Compute the base 2 exponential of x for Intel 80-bit format.
 *
 * Accuracy: Peak error < 0.511 ulp.
 *
 * Method: (equally-spaced tables)
 *
 *   Reduce x:
 *     x = 2**k + y, for integer k and |y| <= 1/2.
 *     Thus we have exp2l(x) = 2**k * exp2(y).
 *
 *   Reduce y:
 *     y = i/TBLSIZE + z for integer i near y * TBLSIZE.
 *     Thus we have exp2(y) = exp2(i/TBLSIZE) * exp2(z),
 *     with |z| <= 2**-(TBLBITS+1).
 *
 *   We compute exp2(i/TBLSIZE) via table lookup and exp2(z) via a
 *   degree-6 minimax polynomial with maximum error under 2**-75.6.
 *   The table entries each have 104 bits of accuracy, encoded as
 *   a pair of double precision values.
 */
long double
exp2l(long double x)
{
	union IEEEl2bits u, v;
	long double r, twopk, twopkp10000, z;
	uint32_t hx, ix, i0;
	int k;

	/* Filter out exceptional cases. */
	u.e = x;
	hx = u.xbits.expsign;
	ix = hx & 0x7fff;
	if (ix >= BIAS + 14) {		/* |x| >= 16384 or x is NaN */
		if (ix == BIAS + LDBL_MAX_EXP) {
			if (hx & 0x8000 && u.xbits.man == 1ULL << 63)
				return (0.0L);	/* x is -Inf */
			return (x + x); /* x is +Inf, NaN or unsupported */
		}
		if (x >= 16384)
			return (huge * huge);	/* overflow */
		if (x <= -16446)
			return (twom10000 * twom10000);	/* underflow */
	} else if (ix <= BIAS - 66) {	/* |x| < 0x1p-65 (includes pseudos) */
		return (1.0L + x);	/* 1 with inexact */
	}

	ENTERI();

	/*
	 * Reduce x, computing z, i0, and k. The low bits of x + redux
	 * contain the 16-bit integer part of the exponent (k) followed by
	 * TBLBITS fractional bits (i0). We use bit tricks to extract these
	 * as integers, then set z to the remainder.
	 *
	 * Example: Suppose x is 0xabc.123456p0 and TBLBITS is 8.
	 * Then the low-order word of x + redux is 0x000abc12,
	 * We split this into k = 0xabc and i0 = 0x12 (adjusted to
	 * index into the table), then we compute z = 0x0.003456p0.
	 *
	 * XXX If the exponent is negative, the computation of k depends on
	 *     '>>' doing sign extension.
	 */
	u.e = x + redux;
	i0 = u.bits.manl + TBLSIZE / 2;
	k = (int)i0 >> TBLBITS;
	i0 = (i0 & (TBLSIZE - 1)) << 1;
	u.e -= redux;
	z = x - u.e;
	v.xbits.man = 1ULL << 63;
	if (k >= LDBL_MIN_EXP) {
		v.xbits.expsign = BIAS + k;
		twopk = v.e;
	} else {
		v.xbits.expsign = BIAS + k + 10000;
		twopkp10000 = v.e;
	}

	/* Compute r = exp2l(y) = exp2lt[i0] * p(z). */
	long double t_hi = tbl[i0];
	long double t_lo = tbl[i0 + 1];
	r = t_lo + (t_hi + t_lo) * z * (P1.e + z * (P2 + z * (P3 + z * (P4
	    + z * (P5 + z * P6))))) + t_hi;

	/* Scale by 2**k. */
	if (k >= LDBL_MIN_EXP) {
		if (k == LDBL_MAX_EXP)
			RETURNI(r * 2.0 * 0x1p16383L);
		RETURNI(r * twopk);
	} else {
		RETURNI(r * twopkp10000 * twom10000);
	}
}
