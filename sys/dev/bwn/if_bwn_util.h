/*-
 * Copyright (c) 2016 Adrian Chadd <adrian@FreeBSD.org>.
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
#ifndef	__IF_BWN_UTIL_H__
#define	__IF_BWN_UTIL_H__

/* Hamming weight; used in the PHY routines */
static inline int
bwn_hweight32(uint32_t val)
{
	int i, r = 0;
	/* yes, could do it without a loop.. */
	for (i = 0; i < 32; i++) {
		r = r + (val & 1);
		val = val >> 1;
	}
	return r;
}

/* Clamp value; PHY code */
static inline int
bwn_clamp_val(int val, int lo, int hi)
{
	if (val < lo)
		return lo;
	if (val > hi)
		return hi;
	return val;
}

/* Q52 format - used in PHY routines */
#define	INT_TO_Q52(i)		((i) << 2)
#define	Q52_TO_INT(q52)		((q52) >> 2)
#define	Q52_FMT			"%u.%u"
#define	Q52_ARG(q52)		Q52_TO_INT(q52), ((((q52) & 0x3) * 100) / 4)

extern	unsigned int bwn_sqrt(struct bwn_mac *mac, unsigned int x);

#endif	/* __IF_BWN_UTIL_H__ */
