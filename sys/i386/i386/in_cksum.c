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
 *
 *	from tahoe:	in_cksum.c	1.2	86/01/05
 *	from:		@(#)in_cksum.c	1.3 (Berkeley) 1/19/91
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
 * This implementation is 386 version.
 */

#undef	ADDCARRY
#define ADDCARRY(x)     if ((x) > 0xffff) (x) -= 0xffff
/*
 * icc needs to be special cased here, as the asm code below results
 * in broken code if compiled with icc.
 */
#if !defined(__GNUCLIKE_ASM) || defined(__INTEL_COMPILER)
/* non gcc parts stolen from sys/alpha/alpha/in_cksum.c */
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
#endif
#define REDUCE          {sum = (sum & 0xffff) + (sum >> 16); ADDCARRY(sum);}

#if !defined(__GNUCLIKE_ASM) || defined(__INTEL_COMPILER)
static const u_int32_t in_masks[] = {
	/*0 bytes*/ /*1 byte*/	/*2 bytes*/ /*3 bytes*/
	0x00000000, 0x000000FF, 0x0000FFFF, 0x00FFFFFF,	/* offset 0 */
	0x00000000, 0x0000FF00, 0x00FFFF00, 0xFFFFFF00,	/* offset 1 */
	0x00000000, 0x00FF0000, 0xFFFF0000, 0xFFFF0000,	/* offset 2 */
	0x00000000, 0xFF000000, 0xFF000000, 0xFF000000,	/* offset 3 */
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
in_cksumdata(const u_int32_t *lw, int len)
{
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
		if ((clen ^ (long) addr) & 1)
		    sum += in_cksumdata((const u_int32_t *)addr, mlen) << 8;
		else
		    sum += in_cksumdata((const u_int32_t *)addr, mlen);

		clen += mlen;
		len -= mlen;
	}
	REDUCE16;
	return (~sum & 0xffff);
}

u_int in_cksum_hdr(const struct ip *ip)
{
    u_int64_t sum = in_cksumdata((const u_int32_t *)ip, sizeof(struct ip));
    union q_util q_util;
    union l_util l_util;

    REDUCE16;
    return (~sum & 0xffff);
}
#else

/*
 * These asm statements require __volatile because they pass information
 * via the condition codes.  GCC does not currently provide a way to specify
 * the condition codes as an input or output operand.
 *
 * The LOAD macro below is effectively a prefetch into cache.  GCC will
 * load the value into a register but will not use it.  Since modern CPUs
 * reorder operations, this will generally take place in parallel with
 * other calculations.
 */
u_short
in_cksum_skip(m, len, skip)
	struct mbuf *m;
	int len;
	int skip;
{
	u_short *w;
	unsigned sum = 0;
	int mlen = 0;
	int byte_swapped = 0;
	union { char	c[2]; u_short	s; } su;

	len -= skip;
	for (; skip && m; m = m->m_next) {
		if (m->m_len > skip) {
			mlen = m->m_len - skip;
			w = (u_short *)(mtod(m, u_char *) + skip);
			goto skip_start;
		} else {
			skip -= m->m_len;
		}
	}

	for (;m && len; m = m->m_next) {
		if (m->m_len == 0)
			continue;
		w = mtod(m, u_short *);
		if (mlen == -1) {
			/*
			 * The first byte of this mbuf is the continuation
			 * of a word spanning between this mbuf and the
			 * last mbuf.
			 */

			/* su.c[0] is already saved when scanning previous
			 * mbuf.  sum was REDUCEd when we found mlen == -1
			 */
			su.c[1] = *(u_char *)w;
			sum += su.s;
			w = (u_short *)((char *)w + 1);
			mlen = m->m_len - 1;
			len--;
		} else
			mlen = m->m_len;
skip_start:
		if (len < mlen)
			mlen = len;
		len -= mlen;
		/*
		 * Force to long boundary so we do longword aligned
		 * memory operations
		 */
		if (3 & (int) w) {
			REDUCE;
			if ((1 & (int) w) && (mlen > 0)) {
				sum <<= 8;
				su.c[0] = *(char *)w;
				w = (u_short *)((char *)w + 1);
				mlen--;
				byte_swapped = 1;
			}
			if ((2 & (int) w) && (mlen >= 2)) {
				sum += *w++;
				mlen -= 2;
			}
		}
		/*
		 * Advance to a 486 cache line boundary.
		 */
		if (4 & (int) w && mlen >= 4) {
			__asm __volatile (
				"addl %1, %0\n"
				"adcl $0, %0"
				: "+r" (sum)
				: "g" (((const u_int32_t *)w)[0])
			);
			w += 2;
			mlen -= 4;
		}
		if (8 & (int) w && mlen >= 8) {
			__asm __volatile (
				"addl %1, %0\n"
				"adcl %2, %0\n"
				"adcl $0, %0"
				: "+r" (sum)
				: "g" (((const u_int32_t *)w)[0]),
				  "g" (((const u_int32_t *)w)[1])
			);
			w += 4;
			mlen -= 8;
		}
		/*
		 * Do as much of the checksum as possible 32 bits at at time.
		 * In fact, this loop is unrolled to make overhead from
		 * branches &c small.
		 */
		mlen -= 1;
		while ((mlen -= 32) >= 0) {
			/*
			 * Add with carry 16 words and fold in the last
			 * carry by adding a 0 with carry.
			 *
			 * The early ADD(16) and the LOAD(32) are to load
			 * the next 2 cache lines in advance on 486's.  The
			 * 486 has a penalty of 2 clock cycles for loading
			 * a cache line, plus whatever time the external
			 * memory takes to load the first word(s) addressed.
			 * These penalties are unavoidable.  Subsequent
			 * accesses to a cache line being loaded (and to
			 * other external memory?) are delayed until the
			 * whole load finishes.  These penalties are mostly
			 * avoided by not accessing external memory for
			 * 8 cycles after the ADD(16) and 12 cycles after
			 * the LOAD(32).  The loop terminates when mlen
			 * is initially 33 (not 32) to guaranteed that
			 * the LOAD(32) is within bounds.
			 */
			__asm __volatile (
				"addl %1, %0\n"
				"adcl %2, %0\n"
				"adcl %3, %0\n"
				"adcl %4, %0\n"
				"adcl %5, %0\n"
				"mov  %6, %%eax\n"
				"adcl %7, %0\n"
				"adcl %8, %0\n"
				"adcl %9, %0\n"
				"adcl $0, %0"
				: "+r" (sum)
				: "g" (((const u_int32_t *)w)[4]),
				  "g" (((const u_int32_t *)w)[0]),
				  "g" (((const u_int32_t *)w)[1]),
				  "g" (((const u_int32_t *)w)[2]),
				  "g" (((const u_int32_t *)w)[3]),
				  "g" (((const u_int32_t *)w)[8]),
				  "g" (((const u_int32_t *)w)[5]),
				  "g" (((const u_int32_t *)w)[6]),
				  "g" (((const u_int32_t *)w)[7])
				: "eax"
			);
			w += 16;
		}
		mlen += 32 + 1;
		if (mlen >= 32) {
			__asm __volatile (
				"addl %1, %0\n"
				"adcl %2, %0\n"
				"adcl %3, %0\n"
				"adcl %4, %0\n"
				"adcl %5, %0\n"
				"adcl %6, %0\n"
				"adcl %7, %0\n"
				"adcl %8, %0\n"
				"adcl $0, %0"
				: "+r" (sum)
				: "g" (((const u_int32_t *)w)[4]),
				  "g" (((const u_int32_t *)w)[0]),
				  "g" (((const u_int32_t *)w)[1]),
				  "g" (((const u_int32_t *)w)[2]),
				  "g" (((const u_int32_t *)w)[3]),
				  "g" (((const u_int32_t *)w)[5]),
				  "g" (((const u_int32_t *)w)[6]),
				  "g" (((const u_int32_t *)w)[7])
			);
			w += 16;
			mlen -= 32;
		}
		if (mlen >= 16) {
			__asm __volatile (
				"addl %1, %0\n"
				"adcl %2, %0\n"
				"adcl %3, %0\n"
				"adcl %4, %0\n"
				"adcl $0, %0"
				: "+r" (sum)
				: "g" (((const u_int32_t *)w)[0]),
				  "g" (((const u_int32_t *)w)[1]),
				  "g" (((const u_int32_t *)w)[2]),
				  "g" (((const u_int32_t *)w)[3])
			);
			w += 8;
			mlen -= 16;
		}
		if (mlen >= 8) {
			__asm __volatile (
				"addl %1, %0\n"
				"adcl %2, %0\n"
				"adcl $0, %0"
				: "+r" (sum)
				: "g" (((const u_int32_t *)w)[0]),
				  "g" (((const u_int32_t *)w)[1])
			);
			w += 4;
			mlen -= 8;
		}
		if (mlen == 0 && byte_swapped == 0)
			continue;       /* worth 1% maybe ?? */
		REDUCE;
		while ((mlen -= 2) >= 0) {
			sum += *w++;
		}
		if (byte_swapped) {
			sum <<= 8;
			byte_swapped = 0;
			if (mlen == -1) {
				su.c[1] = *(char *)w;
				sum += su.s;
				mlen = 0;
			} else
				mlen = -1;
		} else if (mlen == -1)
			/*
			 * This mbuf has odd number of bytes.
			 * There could be a word split betwen
			 * this mbuf and the next mbuf.
			 * Save the last byte (to prepend to next mbuf).
			 */
			su.c[0] = *(char *)w;
	}

	if (len)
		printf("%s: out of data by %d\n", __func__, len);
	if (mlen == -1) {
		/* The last mbuf has odd # of bytes. Follow the
		   standard (the odd byte is shifted left by 8 bits) */
		su.c[1] = 0;
		sum += su.s;
	}
	REDUCE;
	return (~sum & 0xffff);
}
#endif
