/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1987, 1993
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/libkern.h>
#include <machine/endian.h>

typedef	const void	*cvp;
typedef	const unsigned char	*ustring;
typedef unsigned long	ul;
typedef const unsigned long	*culp;

/*
 * bcmp -- vax cmpc3 instruction
 */
int
(bcmp)(const void *b1, const void *b2, size_t length)
{
#if BYTE_ORDER == LITTLE_ENDIAN
	/*
	 * The following code is endian specific.  Changing it from
	 * little-endian to big-endian is fairly trivial, but making
	 * it do both is more difficult.
	 *
	 * Note that this code will reference the entire longword which
	 * includes the final byte to compare.  I don't believe this is
	 * a problem since AFAIK, objects are not protected at smaller
	 * than longword boundaries.
	 */
	int	shl, shr, len = length;
	ustring	p1 = b1, p2 = b2;
	ul	va, vb;

	if (len == 0)
		return (0);

	/*
	 * align p1 to a longword boundary
	 */
	while ((long)p1 & (sizeof(long) - 1)) {
		if (*p1++ != *p2++)
			return (1);
		if (--len <= 0)
			return (0);
	}

	/*
	 * align p2 to longword boundary and calculate the shift required to
	 * align p1 and p2
	 */
	shr = (long)p2 & (sizeof(long) - 1);
	if (shr != 0) {
		p2 -= shr;			/* p2 now longword aligned */
		shr <<= 3;			/* offset in bits */
		shl = (sizeof(long) << 3) - shr;

		va = *(culp)p2;
		p2 += sizeof(long);

		while ((len -= sizeof(long)) >= 0) {
			vb = *(culp)p2;
			p2 += sizeof(long);
			if (*(culp)p1 != (va >> shr | vb << shl))
				return (1);
			p1 += sizeof(long);
			va = vb;
		}
		/*
		 * At this point, len is between -sizeof(long) and -1,
		 * representing 0 .. sizeof(long)-1 bytes remaining.
		 */
		if (!(len += sizeof(long)))
			return (0);

		len <<= 3;		/* remaining length in bits */
		/*
		 * The following is similar to the `if' condition
		 * inside the above while loop.  The ?: is necessary
		 * to avoid accessing the longword after the longword
		 * containing the last byte to be compared.
		 */
		return ((((va >> shr | ((shl < len) ? *(culp)p2 << shl : 0)) ^
		    *(culp)p1) & ((1L << len) - 1)) != 0);
	} else {
		/* p1 and p2 have common alignment so no shifting needed */
		while ((len -= sizeof(long)) >= 0) {
			if (*(culp)p1 != *(culp)p2)
				return (1);
			p1 += sizeof(long);
			p2 += sizeof(long);
		}

		/*
		 * At this point, len is between -sizeof(long) and -1,
		 * representing 0 .. sizeof(long)-1 bytes remaining.
		 */
		if (!(len += sizeof(long)))
			return (0);

		return (((*(culp)p1 ^ *(culp)p2)
			 & ((1L << (len << 3)) - 1)) != 0);
	}
#else
	const char *p1, *p2;

	if (length == 0)
		return(0);
	p1 = b1;
	p2 = b2;
	do
		if (*p1++ != *p2++)
			break;
	while (--length);
	return(length);
#endif
}
