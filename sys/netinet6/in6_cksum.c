/*	$OpenBSD: in6_cksum.c,v 1.18 2019/04/22 22:47:49 bluhm Exp $	*/
/*	$KAME: in6_cksum.c,v 1.10 2000/12/03 00:53:59 itojun Exp $	*/

/*
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
 */

/*
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

#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/systm.h>
#include <netinet/in.h>
#include <netinet/ip6.h>

/*
 * Checksum routine for Internet Protocol family headers (Portable Version).
 *
 * This routine is very heavily used in the network
 * code and should be modified for each CPU to be as fast as possible.
 */

#define ADDCARRY(x)  (x > 65535 ? x -= 65535 : x)
#define REDUCE {l_util.l = sum; sum = l_util.s[0] + l_util.s[1]; ADDCARRY(sum);}

/*
 * m MUST contain a continuous IP6 header.
 * off is a offset where TCP/UDP/ICMP6 header starts.
 * len is a total length of a transport segment.
 * (e.g. TCP header + TCP payload)
 */

int
in6_cksum(struct mbuf *m, uint8_t nxt, uint32_t off, uint32_t len)
{
	uint16_t *w;
	int sum = 0;
	int mlen = 0;
	int byte_swapped = 0;
	struct ip6_hdr *ip6;
	union {
		uint16_t phs[4];
		struct {
			uint32_t ph_len;
			uint8_t  ph_zero[3];
			uint8_t  ph_nxt;
		} ph __packed;
	} uph;
	union {
		uint8_t  c[2];
		uint16_t s;
	} s_util;
	union {
		uint16_t s[2];
		uint32_t l;
	} l_util;

	/* sanity check */
	if (m->m_pkthdr.len < off + len) {
		panic("%s: mbuf len (%d) < off+len (%d+%d)",
		    __func__, m->m_pkthdr.len, off, len);
	}

	/* Skip pseudo-header if nxt == 0. */
	if (nxt == 0)
		 goto skip_phdr;

	bzero(&uph, sizeof(uph));

	/*
	 * First create IP6 pseudo header and calculate a summary.
	 */
	ip6 = mtod(m, struct ip6_hdr *);
	w = (uint16_t *)&ip6->ip6_src;
	uph.ph.ph_len = htonl(len);
	uph.ph.ph_nxt = nxt;

	/* IPv6 source address */
	sum += w[0];
	if (!IN6_IS_SCOPE_EMBED(&ip6->ip6_src))
		sum += w[1];
	sum += w[2]; sum += w[3]; sum += w[4]; sum += w[5];
	sum += w[6]; sum += w[7];
	/* IPv6 destination address */
	sum += w[8];
	if (!IN6_IS_SCOPE_EMBED(&ip6->ip6_dst))
		sum += w[9];
	sum += w[10]; sum += w[11]; sum += w[12]; sum += w[13];
	sum += w[14]; sum += w[15];
	/* Payload length and upper layer identifier */
	sum += uph.phs[0];  sum += uph.phs[1];
	sum += uph.phs[2];  sum += uph.phs[3];

skip_phdr:
	/*
	 * Secondly calculate a summary of the first mbuf excluding offset.
	 */
	while (m != NULL && off > 0) {
		if (m->m_len <= off)
			off -= m->m_len;
		else
			break;
		m = m->m_next;
	}
	if (m == NULL) {
		if  (off)
			panic("%s: out of header, off %u", __func__, off);
		goto end;
	}
	w = (uint16_t *)(mtod(m, uint8_t *) + off);
	mlen = m->m_len - off;
	if (len < mlen)
		mlen = len;
	len -= mlen;
	/*
	 * Force to even boundary.
	 */
	if ((1 & (long) w) && (mlen > 0)) {
		REDUCE;
		sum <<= 8;
		s_util.c[0] = *(uint8_t *)w;
		w = (uint16_t *)((uint8_t *)w + 1);
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
			s_util.c[1] = *(uint8_t *)w;
			sum += s_util.s;
			mlen = 0;
		} else
			mlen = -1;
	} else if (mlen == -1)
		s_util.c[0] = *(uint8_t *)w;
 next:
	m = m->m_next;

	/*
	 * Lastly calculate a summary of the rest of mbufs.
	 */

	for (;m && len; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		w = mtod(m, uint16_t *);
		if (mlen == -1) {
			/*
			 * The first byte of this mbuf is the continuation
			 * of a word spanning between this mbuf and the
			 * last mbuf.
			 *
			 * s_util.c[0] is already saved when scanning previous
			 * mbuf.
			 */
			s_util.c[1] = *(uint8_t *)w;
			sum += s_util.s;
			w = (uint16_t *)((uint8_t *)w + 1);
			mlen = m->m_len - 1;
			len--;
		} else
			mlen = m->m_len;
		if (len < mlen)
			mlen = len;
		len -= mlen;
		/*
		 * Force to even boundary.
		 */
		if ((1 & (long) w) && (mlen > 0)) {
			REDUCE;
			sum <<= 8;
			s_util.c[0] = *(uint8_t *)w;
			w = (uint16_t *)((uint8_t *)w + 1);
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
				s_util.c[1] = *(uint8_t *)w;
				sum += s_util.s;
				mlen = 0;
			} else
				mlen = -1;
		} else if (mlen == -1)
			s_util.c[0] = *(uint8_t *)w;
	}
 end:
	if (len)
		panic("%s: out of data, len %u", __func__, len);
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
