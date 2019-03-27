/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990 The Regents of the University of California.
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
 */
/*-
 * Copyright (c) 2001 by Thomas Moestl <tmm@FreeBSD.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from tahoe:	in_cksum.c	1.2	86/01/05
 *	from:		@(#)in_cksum.c	1.3 (Berkeley) 1/19/91
 * 	from: FreeBSD: src/sys/i386/i386/in_cksum.c,v 1.22 2000/11/25
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#include <machine/in_cksum.h>

/*
 * Checksum routine for Internet Protocol family headers.
 *
 * This routine is very heavily used in the network
 * code and should be modified for each CPU to be as fast as possible.
 *
 * This implementation is a sparc64 version.  Most code was taken over
 * and adapted from the i386.  Some optimizations were changed to achieve
 * (hopefully) better performance.
 * This uses 64-bit loads, but 32-bit additions due to the lack of a 64-bit
 * add-with-carry operation.
 */

/*
 * REDUCE() is actually not used that frequently... maybe a C implementation
 * would suffice.
 */
#define	REDUCE(sum, tmp) __asm(						\
	"sll %2, 16, %1\n"						\
	"addcc %2, %1, %0\n"						\
	"srl %0, 16, %0\n"						\
	"addc %0, 0, %0" : "=r" (sum), "=&r" (tmp) : "0" (sum) : "cc")

/*
 * Note that some of these macros depend on the flags being preserved
 * between calls, thus they have to be used within a single __asm().
 */
#define	LD64_ADD32(n, mod)						\
	"ldx [%3 + " #n "], %1\n"					\
	"add" #mod " %2, %1, %0\n"					\
	"srlx %1, 32, %1\n"						\
	"addccc %0, %1, %0\n"

#define	LD32_ADD32(n, mod)						\
	"lduw [%3 + " #n "], %1\n"					\
	"add" #mod " %2, %1, %0\n"

#define	MOP(sum, tmp, addr)						\
	"addc %2, 0, %0"						\
	: "=r" (sum), "=&r" (tmp) : "0" (sum), "r" (addr) : "cc"

u_short
in_cksum_skip(struct mbuf *m, int len, int skip)
{
	u_short *w;
	unsigned long tmp, sum = 0;
	int mlen = 0;
	int byte_swapped = 0;
	u_short	su = 0;

	len -= skip;
	for (; skip > 0 && m != NULL; m = m->m_next) {
		if (m->m_len > skip) {
			mlen = m->m_len - skip;
			w = (u_short *)(mtod(m, u_char *) + skip);
			goto skip_start;
		} else
			skip -= m->m_len;
	}

	for (; m != NULL && len > 0; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		w = mtod(m, u_short *);
		if (mlen == -1) {
			/*
			 * The first byte of this mbuf is the continuation
			 * of a word spanning between this mbuf and the
			 * last mbuf.
			 *
			 * The high order byte of su is already saved when
			 * scanning previous mbuf.  sum was REDUCEd when we
			 * found mlen == -1
			 */
			sum += su | *(u_char *)w;
			w = (u_short *)((u_char *)w + 1);
			mlen = m->m_len - 1;
			len--;
		} else
			mlen = m->m_len;
skip_start:
		if (len < mlen)
			mlen = len;
		len -= mlen;
		/*
		 * Force to a 8-byte boundary first so that we can use
		 * LD64_ADD32.
		 */
		if (((u_long)w & 7) != 0) {
			REDUCE(sum, tmp);
			if (((u_long)w & 1) != 0 && mlen >= 1) {
				sum <<= 8;
				su = *(u_char *)w << 8;
				w = (u_short *)((u_char *)w + 1);
				mlen--;
				byte_swapped = 1;
			}
			if (((u_long)w & 2) != 0 && mlen >= 2) {
				sum += *w++;
				mlen -= 2;
			}
			if (((u_long)w & 4) != 0 && mlen >= 4) {
				__asm(
				    LD32_ADD32(0, cc)
				    MOP(sum, tmp, w)
				);
				w += 2;
				mlen -= 4;
			}
		}
		/*
		 * Do as much of the checksum as possible 64 bits at at time.
		 * In fact, this loop is unrolled to make overhead from
		 * branches &c small.
		 */
		for (; mlen >= 64; mlen -= 64) {
			__asm(
			    LD64_ADD32(0, cc)
			    LD64_ADD32(8, ccc)
			    LD64_ADD32(16, ccc)
			    LD64_ADD32(24, ccc)
			    LD64_ADD32(32, ccc)
			    LD64_ADD32(40, ccc)
			    LD64_ADD32(48, ccc)
			    LD64_ADD32(56, ccc)
			    MOP(sum, tmp, w)
			);
			w += 32;
		}
		if (mlen >= 32) {
			__asm(
			    LD64_ADD32(0, cc)
			    LD64_ADD32(8, ccc)
			    LD64_ADD32(16, ccc)
			    LD64_ADD32(24, ccc)
			    MOP(sum, tmp, w)
			);
			w += 16;
			mlen -= 32;
		}
		if (mlen >= 16) {
			__asm(
			    LD64_ADD32(0, cc)
			    LD64_ADD32(8, ccc)
			    MOP(sum, tmp, w)
			);
			w += 8;
			mlen -= 16;
		}
		if (mlen >= 8) {
			__asm(
			    LD64_ADD32(0, cc)
			    MOP(sum, tmp, w)
			);
			w += 4;
			mlen -= 8;
		}
		REDUCE(sum, tmp);
		while ((mlen -= 2) >= 0)
			sum += *w++;
		if (byte_swapped) {
			sum <<= 8;
			byte_swapped = 0;
			if (mlen == -1) {
				su |= *(u_char *)w;
				sum += su;
				mlen = 0;
			} else
				mlen = -1;
		} else if (mlen == -1) {
			/*
			 * This mbuf has odd number of bytes.
			 * There could be a word split between
			 * this mbuf and the next mbuf.
			 * Save the last byte (to prepend to next mbuf).
			 */
			su = *(u_char *)w << 8;
		}
	}

	if (len)
		printf("%s: out of data by %d\n", __func__, len);
	if (mlen == -1) {
		/*
		 * The last mbuf has odd # of bytes.  Follow the
		 * standard (the odd byte is shifted left by 8 bits).
		 */
		sum += su & 0xff00;
	}
	REDUCE(sum, tmp);
	return (~sum & 0xffff);
}
