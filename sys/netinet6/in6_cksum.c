/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$KAME: in6_cksum.c,v 1.10 2000/12/03 00:53:59 itojun Exp $
 */

/*-
 * Copyright (c) 1988, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
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
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <netinet6/scope6_var.h>

/*
 * Checksum routine for Internet Protocol family headers (Portable Version).
 *
 * This routine is very heavily used in the network
 * code and should be modified for each CPU to be as fast as possible.
 */

#define ADDCARRY(x)  (x > 65535 ? x -= 65535 : x)
#define REDUCE {l_util.l = sum; sum = l_util.s[0] + l_util.s[1]; (void)ADDCARRY(sum);}

static int
_in6_cksum_pseudo(struct ip6_hdr *ip6, uint32_t len, uint8_t nxt, uint16_t csum)
{
	int sum;
	uint16_t scope, *w;
	union {
		u_int16_t phs[4];
		struct {
			u_int32_t	ph_len;
			u_int8_t	ph_zero[3];
			u_int8_t	ph_nxt;
		} __packed ph;
	} uph;

	sum = csum;

	/*
	 * First create IP6 pseudo header and calculate a summary.
	 */
	uph.ph.ph_len = htonl(len);
	uph.ph.ph_zero[0] = uph.ph.ph_zero[1] = uph.ph.ph_zero[2] = 0;
	uph.ph.ph_nxt = nxt;

	/* Payload length and upper layer identifier. */
	sum += uph.phs[0];  sum += uph.phs[1];
	sum += uph.phs[2];  sum += uph.phs[3];

	/* IPv6 source address. */
	scope = in6_getscope(&ip6->ip6_src);
	w = (u_int16_t *)&ip6->ip6_src;
	sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
	sum += w[4]; sum += w[5]; sum += w[6]; sum += w[7];
	if (scope != 0)
		sum -= scope;

	/* IPv6 destination address. */
	scope = in6_getscope(&ip6->ip6_dst);
	w = (u_int16_t *)&ip6->ip6_dst;
	sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
	sum += w[4]; sum += w[5]; sum += w[6]; sum += w[7];
	if (scope != 0)
		sum -= scope;

	return (sum);
}

int
in6_cksum_pseudo(struct ip6_hdr *ip6, uint32_t len, uint8_t nxt, uint16_t csum)
{
	int sum;
	union {
		u_int16_t s[2];
		u_int32_t l;
	} l_util;

	sum = _in6_cksum_pseudo(ip6, len, nxt, csum);
	REDUCE;
	return (sum);
}

/*
 * m MUST contain a contiguous IP6 header.
 * off is an offset where TCP/UDP/ICMP6 header starts.
 * len is a total length of a transport segment.
 * (e.g. TCP header + TCP payload)
 * cov is the number of bytes to be taken into account for the checksum
 */
int
in6_cksum_partial(struct mbuf *m, u_int8_t nxt, u_int32_t off,
    u_int32_t len, u_int32_t cov)
{
	struct ip6_hdr *ip6;
	u_int16_t *w, scope;
	int byte_swapped, mlen;
	int sum;
	union {
		u_int16_t phs[4];
		struct {
			u_int32_t	ph_len;
			u_int8_t	ph_zero[3];
			u_int8_t	ph_nxt;
		} __packed ph;
	} uph;
	union {
		u_int8_t	c[2];
		u_int16_t	s;
	} s_util;
	union {
		u_int16_t s[2];
		u_int32_t l;
	} l_util;

	/* Sanity check. */
	KASSERT(m->m_pkthdr.len >= off + len, ("%s: mbuf len (%d) < off(%d)+"
	    "len(%d)", __func__, m->m_pkthdr.len, off, len));

	/*
	 * First create IP6 pseudo header and calculate a summary.
	 */
	uph.ph.ph_len = htonl(len);
	uph.ph.ph_zero[0] = uph.ph.ph_zero[1] = uph.ph.ph_zero[2] = 0;
	uph.ph.ph_nxt = nxt;

	/* Payload length and upper layer identifier. */
	sum = uph.phs[0];  sum += uph.phs[1];
	sum += uph.phs[2];  sum += uph.phs[3];

	ip6 = mtod(m, struct ip6_hdr *);

	/* IPv6 source address. */
	scope = in6_getscope(&ip6->ip6_src);
	w = (u_int16_t *)&ip6->ip6_src;
	sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
	sum += w[4]; sum += w[5]; sum += w[6]; sum += w[7];
	if (scope != 0)
		sum -= scope;

	/* IPv6 destination address. */
	scope = in6_getscope(&ip6->ip6_dst);
	w = (u_int16_t *)&ip6->ip6_dst;
	sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
	sum += w[4]; sum += w[5]; sum += w[6]; sum += w[7];
	if (scope != 0)
		sum -= scope;

	/*
	 * Secondly calculate a summary of the first mbuf excluding offset.
	 */
	while (off > 0) {
		if (m->m_len <= off)
			off -= m->m_len;
		else
			break;
		m = m->m_next;
	}
	w = (u_int16_t *)(mtod(m, u_char *) + off);
	mlen = m->m_len - off;
	if (cov < mlen)
		mlen = cov;
	cov -= mlen;
	/*
	 * Force to even boundary.
	 */
	if ((1 & (long)w) && (mlen > 0)) {
		REDUCE;
		sum <<= 8;
		s_util.c[0] = *(u_char *)w;
		w = (u_int16_t *)((char *)w + 1);
		mlen--;
		byte_swapped = 1;
	} else
		byte_swapped = 0;
	
	/*
	 * Unroll the loop to make overhead from
	 * branches &c small.
	 */
	while ((mlen -= 32) >= 0) {
		sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
		sum += w[4]; sum += w[5]; sum += w[6]; sum += w[7];
		sum += w[8]; sum += w[9]; sum += w[10]; sum += w[11];
		sum += w[12]; sum += w[13]; sum += w[14]; sum += w[15];
		w += 16;
	}
	mlen += 32;
	while ((mlen -= 8) >= 0) {
		sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
		w += 4;
	}
	mlen += 8;
	if (mlen == 0 && byte_swapped == 0)
		goto next;
	REDUCE;
	while ((mlen -= 2) >= 0) {
		sum += *w++;
	}
	if (byte_swapped) {
		REDUCE;
		sum <<= 8;
		byte_swapped = 0;
		if (mlen == -1) {
			s_util.c[1] = *(char *)w;
			sum += s_util.s;
			mlen = 0;
		} else
			mlen = -1;
	} else if (mlen == -1)
		s_util.c[0] = *(char *)w;
 next:
	m = m->m_next;

	/*
	 * Lastly calculate a summary of the rest of mbufs.
	 */

	for (;m && cov; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		w = mtod(m, u_int16_t *);
		if (mlen == -1) {
			/*
			 * The first byte of this mbuf is the continuation
			 * of a word spanning between this mbuf and the
			 * last mbuf.
			 *
			 * s_util.c[0] is already saved when scanning previous
			 * mbuf.
			 */
			s_util.c[1] = *(char *)w;
			sum += s_util.s;
			w = (u_int16_t *)((char *)w + 1);
			mlen = m->m_len - 1;
			cov--;
		} else
			mlen = m->m_len;
		if (cov < mlen)
			mlen = cov;
		cov -= mlen;
		/*
		 * Force to even boundary.
		 */
		if ((1 & (long) w) && (mlen > 0)) {
			REDUCE;
			sum <<= 8;
			s_util.c[0] = *(u_char *)w;
			w = (u_int16_t *)((char *)w + 1);
			mlen--;
			byte_swapped = 1;
		}
		/*
		 * Unroll the loop to make overhead from
		 * branches &c small.
		 */
		while ((mlen -= 32) >= 0) {
			sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
			sum += w[4]; sum += w[5]; sum += w[6]; sum += w[7];
			sum += w[8]; sum += w[9]; sum += w[10]; sum += w[11];
			sum += w[12]; sum += w[13]; sum += w[14]; sum += w[15];
			w += 16;
		}
		mlen += 32;
		while ((mlen -= 8) >= 0) {
			sum += w[0]; sum += w[1]; sum += w[2]; sum += w[3];
			w += 4;
		}
		mlen += 8;
		if (mlen == 0 && byte_swapped == 0)
			continue;
		REDUCE;
		while ((mlen -= 2) >= 0) {
			sum += *w++;
		}
		if (byte_swapped) {
			REDUCE;
			sum <<= 8;
			byte_swapped = 0;
			if (mlen == -1) {
				s_util.c[1] = *(char *)w;
				sum += s_util.s;
				mlen = 0;
			} else
				mlen = -1;
		} else if (mlen == -1)
			s_util.c[0] = *(char *)w;
	}
	if (cov)
		panic("in6_cksum: out of data");
	if (mlen == -1) {
		/* The last mbuf has odd # of bytes. Follow the
		   standard (the odd byte may be shifted left by 8 bits
		   or not as determined by endian-ness of the machine) */
		s_util.c[1] = 0;
		sum += s_util.s;
	}
	REDUCE;
	return (~sum & 0xffff);
}

int
in6_cksum(struct mbuf *m, u_int8_t nxt, u_int32_t off, u_int32_t len)
{
	return (in6_cksum_partial(m, nxt, off, len, len));
}
