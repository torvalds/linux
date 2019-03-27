/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008-2010 Lawrence Stewart <lstewart@freebsd.org>
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Lawrence Stewart while studying at the Centre
 * for Advanced Internet Architectures, Swinburne University of Technology, made
 * possible in part by a grant from the Cisco University Research Program Fund
 * at Community Foundation Silicon Valley.
 *
 * Portions of this software were developed at the Centre for Advanced
 * Internet Architectures, Swinburne University of Technology, Melbourne,
 * Australia by David Hayes under sponsorship from the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef _NETINET_CC_CUBIC_H_
#define _NETINET_CC_CUBIC_H_

/* Number of bits of precision for fixed point math calcs. */
#define	CUBIC_SHIFT		8

#define	CUBIC_SHIFT_4		32

/* 0.5 << CUBIC_SHIFT. */
#define	RENO_BETA		128

/* ~0.7 << CUBIC_SHIFT. */
#define	CUBIC_BETA		179

/* ~0.3 << CUBIC_SHIFT. */
#define	ONE_SUB_CUBIC_BETA	77

/* 3 * ONE_SUB_CUBIC_BETA. */
#define	THREE_X_PT3		231

/* (2 << CUBIC_SHIFT) - ONE_SUB_CUBIC_BETA. */
#define	TWO_SUB_PT3		435

/* ~0.4 << CUBIC_SHIFT. */
#define	CUBIC_C_FACTOR		102

/* CUBIC fast convergence factor: (1+beta_cubic)/2. */
#define	CUBIC_FC_FACTOR		217

/* Don't trust s_rtt until this many rtt samples have been taken. */
#define	CUBIC_MIN_RTT_SAMPLES	8

/* Userland only bits. */
#ifndef _KERNEL

extern int hz;

/*
 * Implementation based on the formulae found in the CUBIC Internet Draft
 * "draft-ietf-tcpm-cubic-04".
 *
 */

static __inline float
theoretical_cubic_k(double wmax_pkts)
{
	double C;

	C = 0.4;

	return (pow((wmax_pkts * 0.3) / C, (1.0 / 3.0)) * pow(2, CUBIC_SHIFT));
}

static __inline unsigned long
theoretical_cubic_cwnd(int ticks_since_cong, unsigned long wmax, uint32_t smss)
{
	double C, wmax_pkts;

	C = 0.4;
	wmax_pkts = wmax / (double)smss;

	return (smss * (wmax_pkts +
	    (C * pow(ticks_since_cong / (double)hz -
	    theoretical_cubic_k(wmax_pkts) / pow(2, CUBIC_SHIFT), 3.0))));
}

static __inline unsigned long
theoretical_reno_cwnd(int ticks_since_cong, int rtt_ticks, unsigned long wmax,
    uint32_t smss)
{

	return ((wmax * 0.5) + ((ticks_since_cong / (float)rtt_ticks) * smss));
}

static __inline unsigned long
theoretical_tf_cwnd(int ticks_since_cong, int rtt_ticks, unsigned long wmax,
    uint32_t smss)
{

	return ((wmax * 0.7) + ((3 * 0.3) / (2 - 0.3) *
	    (ticks_since_cong / (float)rtt_ticks) * smss));
}

#endif /* !_KERNEL */

/*
 * Compute the CUBIC K value used in the cwnd calculation, using an
 * implementation of eqn 2 in the I-D. The method used
 * here is adapted from Apple Computer Technical Report #KT-32.
 */
static __inline int64_t
cubic_k(unsigned long wmax_pkts)
{
	int64_t s, K;
	uint16_t p;

	K = s = 0;
	p = 0;

	/* (wmax * beta)/C with CUBIC_SHIFT worth of precision. */
	s = ((wmax_pkts * ONE_SUB_CUBIC_BETA) << CUBIC_SHIFT) / CUBIC_C_FACTOR;

	/* Rebase s to be between 1 and 1/8 with a shift of CUBIC_SHIFT. */
	while (s >= 256) {
		s >>= 3;
		p++;
	}

	/*
	 * Some magic constants taken from the Apple TR with appropriate
	 * shifts: 275 == 1.072302 << CUBIC_SHIFT, 98 == 0.3812513 <<
	 * CUBIC_SHIFT, 120 == 0.46946116 << CUBIC_SHIFT.
	 */
	K = (((s * 275) >> CUBIC_SHIFT) + 98) -
	    (((s * s * 120) >> CUBIC_SHIFT) >> CUBIC_SHIFT);

	/* Multiply by 2^p to undo the rebasing of s from above. */
	return (K <<= p);
}

/*
 * Compute the new cwnd value using an implementation of eqn 1 from the I-D.
 * Thanks to Kip Macy for help debugging this function.
 *
 * XXXLAS: Characterise bounds for overflow.
 */
static __inline unsigned long
cubic_cwnd(int ticks_since_cong, unsigned long wmax, uint32_t smss, int64_t K)
{
	int64_t cwnd;

	/* K is in fixed point form with CUBIC_SHIFT worth of precision. */

	/* t - K, with CUBIC_SHIFT worth of precision. */
	cwnd = ((int64_t)(ticks_since_cong << CUBIC_SHIFT) - (K * hz)) / hz;

	/* (t - K)^3, with CUBIC_SHIFT^3 worth of precision. */
	cwnd *= (cwnd * cwnd);

	/*
	 * C(t - K)^3 + wmax
	 * The down shift by CUBIC_SHIFT_4 is because cwnd has 4 lots of
	 * CUBIC_SHIFT included in the value. 3 from the cubing of cwnd above,
	 * and an extra from multiplying through by CUBIC_C_FACTOR.
	 */
	cwnd = ((cwnd * CUBIC_C_FACTOR * smss) >> CUBIC_SHIFT_4) + wmax;

	return ((unsigned long)cwnd);
}

/*
 * Compute an approximation of the NewReno cwnd some number of ticks after a
 * congestion event. RTT should be the average RTT estimate for the path
 * measured over the previous congestion epoch and wmax is the value of cwnd at
 * the last congestion event. The "TCP friendly" concept in the CUBIC I-D is
 * rather tricky to understand and it turns out this function is not required.
 * It is left here for reference.
 */
static __inline unsigned long
reno_cwnd(int ticks_since_cong, int rtt_ticks, unsigned long wmax,
    uint32_t smss)
{

	/*
	 * For NewReno, beta = 0.5, therefore: W_tcp(t) = wmax*0.5 + t/RTT
	 * W_tcp(t) deals with cwnd/wmax in pkts, so because our cwnd is in
	 * bytes, we have to multiply by smss.
	 */
	return (((wmax * RENO_BETA) + (((ticks_since_cong * smss)
	    << CUBIC_SHIFT) / rtt_ticks)) >> CUBIC_SHIFT);
}

/*
 * Compute an approximation of the "TCP friendly" cwnd some number of ticks
 * after a congestion event that is designed to yield the same average cwnd as
 * NewReno while using CUBIC's beta of 0.7. RTT should be the average RTT
 * estimate for the path measured over the previous congestion epoch and wmax is
 * the value of cwnd at the last congestion event.
 */
static __inline unsigned long
tf_cwnd(int ticks_since_cong, int rtt_ticks, unsigned long wmax,
    uint32_t smss)
{

	/* Equation 4 of I-D. */
	return (((wmax * CUBIC_BETA) + (((THREE_X_PT3 * ticks_since_cong *
	    smss) << CUBIC_SHIFT) / TWO_SUB_PT3 / rtt_ticks)) >> CUBIC_SHIFT);
}

#endif /* _NETINET_CC_CUBIC_H_ */
