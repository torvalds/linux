/*	$KAME: ip_ecn.c,v 1.12 2002/01/07 11:34:47 kjc Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (C) 1999 WIDE Project.
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
 */
/*
 * ECN consideration on tunnel ingress/egress operation.
 * http://www.aciri.org/floyd/papers/draft-ipsec-ecn-00.txt
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/errno.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#ifdef INET6
#include <netinet/ip6.h>
#endif

#include <netinet/ip_ecn.h>
#ifdef INET6
#include <netinet6/ip6_ecn.h>
#endif

/*
 * ECN and TOS (or TCLASS) processing rules at tunnel encapsulation and
 * decapsulation from RFC3168:
 *
 *                      Outer Hdr at                 Inner Hdr at
 *                      Encapsulator                 Decapsulator
 *   Header fields:     --------------------         ------------
 *     DS Field         copied from inner hdr        no change
 *     ECN Field        constructed by (I)           constructed by (E)
 *
 * ECN_ALLOWED (full functionality):
 *    (I) if the ECN field in the inner header is set to CE, then set the
 *    ECN field in the outer header to ECT(0).
 *    otherwise, copy the ECN field to the outer header.
 *
 *    (E) if the ECN field in the outer header is set to CE and the ECN
 *    field of the inner header is not-ECT, drop the packet.
 *    if the ECN field in the inner header is set to ECT(0) or ECT(1)
 *    and the ECN field in the outer header is set to CE, then copy CE to
 *    the inner header.  otherwise, make no change to the inner header.
 *
 * ECN_FORBIDDEN (limited functionality):
 *    (I) set the ECN field to not-ECT in the outer header.
 *
 *    (E) if the ECN field in the outer header is set to CE, drop the packet.
 *    otherwise, make no change to the ECN field in the inner header.
 *
 * the drop rule is for backward compatibility and protection against
 * erasure of CE.
 */

/*
 * modify outer ECN (TOS) field on ingress operation (tunnel encapsulation).
 */
void
ip_ecn_ingress(int mode, u_int8_t *outer, const u_int8_t *inner)
{

	if (!outer || !inner)
		panic("NULL pointer passed to ip_ecn_ingress");

	*outer = *inner;
	switch (mode) {
	case ECN_ALLOWED:		/* ECN allowed */
		/*
		 * full-functionality: if the inner is CE, set ECT(0)
		 * to the outer.  otherwise, copy the ECN field.
		 */
		if ((*inner & IPTOS_ECN_MASK) == IPTOS_ECN_CE)
			*outer &= ~IPTOS_ECN_ECT1;
		break;
	case ECN_FORBIDDEN:		/* ECN forbidden */
		/*
		 * limited-functionality: set not-ECT to the outer
		 */
		*outer &= ~IPTOS_ECN_MASK;
		break;
	case ECN_NOCARE:	/* no consideration to ECN */
		break;
	}
}

/*
 * modify inner ECN (TOS) field on egress operation (tunnel decapsulation).
 * the caller should drop the packet if the return value is 0.
 */
int
ip_ecn_egress(int mode, const u_int8_t *outer, u_int8_t *inner)
{

	if (!outer || !inner)
		panic("NULL pointer passed to ip_ecn_egress");

	switch (mode) {
	case ECN_ALLOWED:
		/*
		 * full-functionality: if the outer is CE and the inner is
		 * not-ECT, should drop it.  otherwise, copy CE.
		 */
		if ((*outer & IPTOS_ECN_MASK) == IPTOS_ECN_CE) {
			if ((*inner & IPTOS_ECN_MASK) == IPTOS_ECN_NOTECT)
				return (0);
			*inner |= IPTOS_ECN_CE;
		}
		break;
	case ECN_FORBIDDEN:		/* ECN forbidden */
		/*
		 * limited-functionality: if the outer is CE, should drop it.
		 * otherwise, leave the inner.
		 */
		if ((*outer & IPTOS_ECN_MASK) == IPTOS_ECN_CE)
			return (0);
		break;
	case ECN_NOCARE:	/* no consideration to ECN */
		break;
	}
	return (1);
}

#ifdef INET6
void
ip6_ecn_ingress(int mode, u_int32_t *outer, const u_int32_t *inner)
{
	u_int8_t outer8, inner8;

	if (!outer || !inner)
		panic("NULL pointer passed to ip6_ecn_ingress");

	inner8 = (ntohl(*inner) >> 20) & 0xff;
	ip_ecn_ingress(mode, &outer8, &inner8);
	*outer &= ~htonl(0xff << 20);
	*outer |= htonl((u_int32_t)outer8 << 20);
}

int
ip6_ecn_egress(int mode, const u_int32_t *outer, u_int32_t *inner)
{
	u_int8_t outer8, inner8, oinner8;

	if (!outer || !inner)
		panic("NULL pointer passed to ip6_ecn_egress");

	outer8 = (ntohl(*outer) >> 20) & 0xff;
	inner8 = oinner8 = (ntohl(*inner) >> 20) & 0xff;
	if (ip_ecn_egress(mode, &outer8, &inner8) == 0)
		return (0);
	if (inner8 != oinner8) {
		*inner &= ~htonl(0xff << 20);
		*inner |= htonl((u_int32_t)inner8 << 20);
	}
	return (1);
}
#endif
