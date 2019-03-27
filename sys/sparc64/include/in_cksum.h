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
 *	from: Id: in_cksum.c,v 1.8 1995/12/03 18:35:19 bde Exp
 *	from: FreeBSD: src/sys/alpha/include/in_cksum.h,v 1.5 2000/05/06
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_IN_CKSUM_H_
#define	_MACHINE_IN_CKSUM_H_	1

#include <sys/cdefs.h>

#define	in_cksum(m, len)	in_cksum_skip(m, len, 0)

#if defined(IPVERSION) && (IPVERSION == 4)
static __inline void
in_cksum_update(struct ip *ip)
{
	int __tmp;

	__tmp = (int)ip->ip_sum + 1;
	ip->ip_sum = __tmp + (__tmp >> 16);
}
#endif

static __inline u_short
in_addword(u_short sum, u_short b)
{
	u_long __ret, __tmp;

	__asm(
	    "sll %2, 16, %0\n"
	    "sll %3, 16, %1\n"
	    "addcc %0, %1, %0\n"
	    "srl %0, 16, %0\n"
	    "addc %0, 0, %0\n"
	    : "=&r" (__ret), "=&r" (__tmp) : "r" (sum), "r" (b) : "cc");
	return (__ret);
}

static __inline u_short
in_pseudo(u_int sum, u_int b, u_int c)
{
	u_long __tmp;

	__asm(
	    "addcc %0, %3, %0\n"
	    "addccc %0, %4, %0\n"
	    "addc %0, 0, %0\n"
	    "sll %0, 16, %1\n"
	    "addcc %0, %1, %0\n"
	    "srl %0, 16, %0\n"
	    "addc %0, 0, %0\n"
	    : "=r" (sum), "=&r" (__tmp) : "0" (sum), "r" (b), "r" (c) : "cc");
	return (sum);
}

#if defined(IPVERSION) && (IPVERSION == 4)
static __inline u_int
in_cksum_hdr(struct ip *ip)
{
	u_long __ret, __tmp1, __tmp2, __tmp3, __tmp4;

	/*
	 * Use 32-bit memory accesses and additions - addition with carry only
	 * works for 32 bits, and fixing up alignment issues for 64 is probably
	 * more trouble than it's worth.
	 * This may read outside of the ip header, but does not cross a page
	 * boundary in doing so, so that should be OK.
	 * Actually, this specialized implementation might be overkill - using
	 * a generic implementation for both in_cksum_skip and in_cksum_hdr
	 * should not be too much more expensive.
	 */
#define	__LD_ADD(addr, tmp, sum, offs, mod)				\
    "lduw [" #addr " + " #offs "], " #tmp "\n"				\
    "add" # mod " " #sum ", " #tmp ", " #sum "\n"

	__asm(
	    "and %5, 3, %3\n"
	    "andn %5, 3, %1\n"
	    "brz,pt %3, 0f\n"
	    " lduw [%1], %0\n"
	    "mov 4, %4\n"
	    "sub %4, %3, %4\n"
	    "sll %4, 3, %4\n"		/* fix up unaligned buffers */
	    "mov 1, %2\n"
	    "sll %2, %4, %4\n"
	    "sub %4, 1, %4\n"
	    "lduw [%1 + 20], %2\n"
	    "andn %2, %4, %2\n"
	    "and %0, %4, %0\n"
	    "or %0, %2, %0\n"
	    "0:\n"
	    __LD_ADD(%1, %2, %0, 4, cc)
	    __LD_ADD(%1, %2, %0, 8, ccc)
	    __LD_ADD(%1, %2, %0, 12, ccc)
	    __LD_ADD(%1, %2, %0, 16, ccc)
	    "addc %0, 0, %0\n"		/* reduce */
	    "1:\n"
	    "sll %0, 16, %2\n"
	    "addcc %0, %2, %0\n"
	    "srl %0, 16, %0\n"
	    "addc %0, 0, %0\n"
	    "andcc %3, 1, %3\n"		/* need to byte-swap? */
	    "clr %3\n"
	    "bne,a,pn %%xcc, 1b\n"
	    " sll %0, 8, %0\n"
	    "not %0\n"
	    "sll %0, 16, %0\n"
	    "srl %0, 16, %0\n"
	    : "=&r" (__ret), "=r" (__tmp1), "=&r" (__tmp2), "=&r" (__tmp3),
	    "=&r" (__tmp4) : "1" (ip) : "cc");
#undef __LD_ADD
	return (__ret);
}
#endif

#ifdef _KERNEL
u_short	in_cksum_skip(struct mbuf *m, int len, int skip);
#endif

#endif /* _MACHINE_IN_CKSUM_H_ */
