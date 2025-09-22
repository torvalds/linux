/*	$OpenBSD: in_cksum.c,v 1.11 2022/02/01 15:30:10 miod Exp $	*/
/*	$NetBSD: in_cksum.c,v 1.7 2003/07/15 02:54:48 lukem Exp $	*/

/*
 * Copyright 2001 Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Simon Burge and Eduardo Horvath for Wasabi Systems, Inc.
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
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/socketvar.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>

/*
 * Checksum routine for Internet Protocol family headers.
 *
 * This routine is very heavily used in the network
 * code and should be modified for each CPU to be as fast as possible.
 *
 * PowerPC version.
 */

#define	REDUCE1		sum = (sum & 0xffff) + (sum >> 16)
/* Two REDUCE1s is faster than REDUCE1; if (sum > 65535) sum -= 65536; */
#define	REDUCE		{ REDUCE1; REDUCE1; }

static __inline__ int
in_cksum_internal(struct mbuf *m, int off, int len, u_int sum)
{
	uint8_t *w;
	int mlen = 0;
	int byte_swapped = 0;
	int n;

	union {
		uint8_t  c[2];
		uint16_t s;
	} s_util;

	for (;m && len; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		w = mtod(m, uint8_t *) + off;

		/*
		 * 'off' can only be non-zero on the first pass of this
		 * loop when mlen != -1, so we don't need to worry about
		 * 'off' in the if clause below.
		 */
		if (mlen == -1) {
			/*
			 * The first byte of this mbuf is the continuation
			 * of a word spanning between this mbuf and the
			 * last mbuf.
			 *
			 * s_util.c[0] is already saved when scanning previous
			 * mbuf.
			 */
			s_util.c[1] = *w++;
			sum += s_util.s;
			mlen = m->m_len - 1;
			len--;
		} else {
			mlen = m->m_len - off;
			off = 0;
		}
		if (len < mlen)
			mlen = len;
		len -= mlen;

		/*
		 * Force to a word boundary.
		 */
		if ((3 & (long) w) && (mlen > 0)) {
			if ((1 & (long) w)) {
				REDUCE;
				sum <<= 8;
				s_util.c[0] = *w++;
				mlen--;
				byte_swapped = 1;
			}
			if ((2 & (long) w) && (mlen > 1)) {
				/*
				 * Since the `sum' may contain full 32 bit
				 * value, we can't simply add any value.
				 */
				__asm volatile(
				    "lhz 7,0(%1);"	/* load current data
							   half word */
				    "addc %0,%0,7;"	/* add to sum */
				    "addze %0,%0;"	/* add carry bit */
				    : "+r"(sum)
				    : "b"(w)
				    : "7");		/* clobber r7 */
				w += 2;
				mlen -= 2;
			}
		}

		if (mlen >= 64) {
			n = mlen >> 6;
			__asm volatile(
			    "addic 0,0,0;"		/* clear carry */
			    "mtctr %1;"			/* load loop count */
			    "1:"
			    "lwz 7,4(%2);"		/* load current data
							   word */
			    "lwz 8,8(%2);"
			    "lwz 9,12(%2);"
			    "lwz 10,16(%2);"
			    "adde %0,%0,7;"		/* add to sum */
			    "adde %0,%0,8;"
			    "adde %0,%0,9;"
			    "adde %0,%0,10;"
			    "lwz 7,20(%2);"
			    "lwz 8,24(%2);"
			    "lwz 9,28(%2);"
			    "lwz 10,32(%2);"
			    "adde %0,%0,7;"
			    "adde %0,%0,8;"
			    "adde %0,%0,9;"
			    "adde %0,%0,10;"
			    "lwz 7,36(%2);"
			    "lwz 8,40(%2);"
			    "lwz 9,44(%2);"
			    "lwz 10,48(%2);"
			    "adde %0,%0,7;"
			    "adde %0,%0,8;"
			    "adde %0,%0,9;"
			    "adde %0,%0,10;"
			    "lwz 7,52(%2);"
			    "lwz 8,56(%2);"
			    "lwz 9,60(%2);"
			    "lwzu 10,64(%2);"
			    "adde %0,%0,7;"
			    "adde %0,%0,8;"
			    "adde %0,%0,9;"
			    "adde %0,%0,10;"
			    "bdnz 1b;"			/* loop */
			    "addze %0,%0;"		/* add carry bit */
			    : "+r"(sum)
			    : "r"(n), "b"(w - 4)
			    : "7", "8", "9", "10");	/* clobber r7, r8, r9,
							   r10 */
			w += n * 64;
			mlen -= n * 64;
		}

		if (mlen >= 8) {
			n = mlen >> 3;
			__asm volatile(
			    "addic 0,0,0;"		/* clear carry */
			    "mtctr %1;"			/* load loop count */
			    "1:"
			    "lwz 7,4(%2);"		/* load current data
							   word */
			    "lwzu 8,8(%2);"
			    "adde %0,%0,7;"		/* add to sum */
			    "adde %0,%0,8;"
			    "bdnz 1b;"			/* loop */
			    "addze %0,%0;"		/* add carry bit */
			    : "+r"(sum)
			    : "r"(n), "b"(w - 4)
			    : "7", "8");		/* clobber r7, r8 */
			w += n * 8;
			mlen -= n * 8;
		}

		if (mlen == 0 && byte_swapped == 0)
			continue;
		REDUCE;

		while ((mlen -= 2) >= 0) {
			sum += *(uint16_t *)w;
			w += 2;
		}

		if (byte_swapped) {
			REDUCE;
			sum <<= 8;
			byte_swapped = 0;
			if (mlen == -1) {
				s_util.c[1] = *w;
				sum += s_util.s;
				mlen = 0;
			} else
				mlen = -1;
		} else if (mlen == -1)
			s_util.c[0] = *w;
	}
	if (len)
		printf("cksum: out of data\n");
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
in_cksum(struct mbuf *m, int len)
{

	return (in_cksum_internal(m, 0, len, 0));
}

int
in4_cksum(struct mbuf *m, uint8_t nxt, int off, int len)
{
	uint16_t *w;
	u_int sum = 0;
	union {
		struct ipovly ipov;
		u_int16_t w[10];
	} u;

	if (nxt != 0) {
		/* pseudo header */
		u.ipov.ih_x1[8] = 0;
		u.ipov.ih_pr = nxt;
		u.ipov.ih_len = htons(len);
		u.ipov.ih_src = mtod(m, struct ip *)->ip_src;
		u.ipov.ih_dst = mtod(m, struct ip *)->ip_dst;
		w = u.w;
		/* assumes sizeof(ipov) == 20 and first 8 bytes are zeroes */
		sum += w[4]; sum += w[5]; sum += w[6];
		sum += w[7]; sum += w[8]; sum += w[9];
	}

	/* skip unnecessary part */
	while (m && off > 0) {
		if (m->m_len > off)
			break;
		off -= m->m_len;
		m = m->m_next;
	}

	return (in_cksum_internal(m, off, len, sum));
}
