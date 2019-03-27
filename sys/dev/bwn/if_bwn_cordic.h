/*-
 * Copyright (c) 2016 Adrian Chadd <adrian@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */
#ifndef	__IF_BWN_CORDIC_H__
#define	__IF_BWN_CORDIC_H__

/*
 * These functions are used by the PHY code.
 */

/* Complex number using 2 32-bit signed integers */
struct bwn_c32 {
	int32_t i;
	int32_t q;
};

#define	CORDIC_CONVERT(value)	(((value) >= 0) ?	\
	    ((((value) >> 15) + 1) >> 1) :		\
	    -((((-(value)) >> 15) + 1) >> 1))

static const uint32_t bwn_arctg[] = {
    2949120, 1740967, 919879, 466945, 234379, 117304, 58666, 29335, 14668,
    7334, 3667, 1833, 917, 458, 229, 115, 57, 29,
};

/* http://bcm-v4.sipsolutions.net/802.11/PHY/Cordic */
static inline struct bwn_c32
bwn_cordic(int theta)
{
	uint8_t i;
	int32_t tmp;
	int8_t signx = 1;
	uint32_t angle = 0;
	struct bwn_c32 ret = { .i = 39797, .q = 0, };

	while (theta > (180 << 16))
		theta -= (360 << 16);
	while (theta < -(180 << 16))
		theta += (360 << 16);

	if (theta > (90 << 16)) {
		theta -= (180 << 16);
		signx = -1;
	} else if (theta < -(90 << 16)) {
		theta += (180 << 16);
		signx = -1;
	}

	for (i = 0; i <= 17; i++) {
		if (theta > angle) {
			tmp = ret.i - (ret.q >> i);
			ret.q += ret.i >> i;
			ret.i = tmp;
			angle += bwn_arctg[i];
		} else {
			tmp = ret.i + (ret.q >> i);
			ret.q -= ret.i >> i;
			ret.i = tmp;
			angle -= bwn_arctg[i];
		}
	}

	ret.i *= signx;
	ret.q *= signx;

	return ret;
}

#endif	/* __IF_BWN_CORDIC_H__ */
