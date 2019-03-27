/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1988, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1996
 *	Matt Thomas <matt@3am-software.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)in_cksum.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <machine/in_cksum.h>

/*
 * Checksum routine for Internet Protocol family headers
 *    (Portable Alpha version).
 *
 * This routine is very heavily used in the network
 * code and should be modified for each CPU to be as fast as possible.
 */

#define ADDCARRY(x)  (x > 65535 ? x -= 65535 : x)
#define REDUCE32							  \
    {									  \
	q_util.q = sum;							  \
	sum = q_util.s[0] + q_util.s[1] + q_util.s[2] + q_util.s[3];	  \
    }
#define REDUCE16							  \
    {									  \
	q_util.q = sum;							  \
	l_util.l = q_util.s[0] + q_util.s[1] + q_util.s[2] + q_util.s[3]; \
	sum = l_util.s[0] + l_util.s[1];				  \
	ADDCARRY(sum);							  \
    }

static const u_int32_t in_masks[] = {
#if _BYTE_ORDER == _LITTLE_ENDIAN
	/*0 bytes*/ /*1 byte*/	/*2 bytes*/ /*3 bytes*/
	0x00000000, 0x000000FF, 0x0000FFFF, 0x00FFFFFF,	/* offset 0 */
	0x00000000, 0x0000FF00, 0x00FFFF00, 0xFFFFFF00,	/* offset 1 */
	0x00000000, 0x00FF0000, 0xFFFF0000, 0xFFFF0000,	/* offset 2 */
	0x00000000, 0xFF000000, 0xFF000000, 0xFF000000,	/* offset 3 */
#else
	/*0 bytes*/ /*1 byte*/	/*2 bytes*/ /*3 bytes*/
	0x00000000, 0xFF000000, 0xFFFF0000, 0xFFFFFF00,	/* offset 0 */
	0x00000000, 0x00FF0000, 0x00FFFF00, 0x00FFFFFF,	/* offset 1 */
	0x00000000, 0x0000FF00, 0x0000FFFF, 0x0000FFFF,	/* offset 2 */
	0x00000000, 0x000000FF, 0x000000FF, 0x000000FF,	/* offset 3 */
#endif
};

union l_util {
	u_int16_t s[2];
	u_int32_t l;
};
union q_util {
	u_int16_t s[4];
	u_int32_t l[2];
	u_int64_t q;
};

static u_int64_t
in_cksumdata(const void *buf, int len)
{
	const u_int32_t *lw = (const u_int32_t *) buf;
	u_int64_t sum = 0;
	u_int64_t prefilled;
	int offset;
	union q_util q_util;

	if ((3 & (long) lw) == 0 && len == 20) {
		sum = (u_int64_t) lw[0] + lw[1] + lw[2] + lw[3] + lw[4];
		REDUCE32;
		return sum;
	}

	if ((offset = 3 & (long) lw) != 0) {
		const u_int32_t *masks = in_masks + (offset << 2);
		lw = (u_int32_t *) (((long) lw) - offset);
		sum = *lw++ & masks[len >= 3 ? 3 : len];
		len -= 4 - offset;
		if (len <= 0) {
			REDUCE32;
			return sum;
		}
	}
#if 0
	/*
	 * Force to cache line boundary.
	 */
	offset = 32 - (0x1f & (long) lw);
	if (offset < 32 && len > offset) {
		len -= offset;
		if (4 & offset) {
			sum += (u_int64_t) lw[0];
			lw += 1;
		}
		if (8 & offset) {
			sum += (u_int64_t) lw[0] + lw[1];
			lw += 2;
		}
		if (16 & offset) {
			sum += (u_int64_t) lw[0] + lw[1] + lw[2] + lw[3];
			lw += 4;
		}
	}
#endif
	/*
	 * access prefilling to start load of next cache line.
	 * then add current cache line
	 * save result of prefilling for loop iteration.
	 */
	prefilled = lw[0];
	while ((len -= 32) >= 4) {
		u_int64_t prefilling = lw[8];
		sum += prefilled + lw[1] + lw[2] + lw[3]
			+ lw[4] + lw[5] + lw[6] + lw[7];
		lw += 8;
		prefilled = prefilling;
	}
	if (len >= 0) {
		sum += prefilled + lw[1] + lw[2] + lw[3]
			+ lw[4] + lw[5] + lw[6] + lw[7];
		lw += 8;
	} else {
		len += 32;
	}
	while ((len -= 16) >= 0) {
		sum += (u_int64_t) lw[0] + lw[1] + lw[2] + lw[3];
		lw += 4;
	}
	len += 16;
	while ((len -= 4) >= 0) {
		sum += (u_int64_t) *lw++;
	}
	len += 4;
	if (len > 0)
		sum += (u_int64_t) (in_masks[len] & *lw);
	REDUCE32;
	return sum;
}

u_short
in_addword(u_short a, u_short b)
{
	u_int64_t sum = a + b;

	ADDCARRY(sum);
	return (sum);
}

u_short
in_pseudo(u_int32_t a, u_int32_t b, u_int32_t c)
{
	u_int64_t sum;
	union q_util q_util;
	union l_util l_util;

	sum = (u_int64_t) a + b + c;
	REDUCE16;
	return (sum);
}

u_short
in_cksum_skip(struct mbuf *m, int len, int skip)
{
	u_int64_t sum = 0;
	int mlen = 0;
	int clen = 0;
	caddr_t addr;
	union q_util q_util;
	union l_util l_util;

	len -= skip;
	for (; skip && m; m = m->m_next) {
		if (m->m_len > skip) {
			mlen = m->m_len - skip;
			addr = mtod(m, caddr_t) + skip;
			goto skip_start;
		} else {
			skip -= m->m_len;
		}
	}

	for (; m && len; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		mlen = m->m_len;
		addr = mtod(m, caddr_t);
skip_start:
		if (len < mlen)
			mlen = len;

		if ((clen ^ (uintptr_t) addr) & 1)
			sum += in_cksumdata(addr, mlen) << 8;
		else
			sum += in_cksumdata(addr, mlen);

		clen += mlen;
		len -= mlen;
	}
	REDUCE16;
	return (~sum & 0xffff);
}

u_int in_cksum_hdr(const struct ip *ip)
{
	u_int64_t sum = in_cksumdata(ip, sizeof(struct ip));
	union q_util q_util;
	union l_util l_util;
	REDUCE16;
	return (~sum & 0xffff);
}
