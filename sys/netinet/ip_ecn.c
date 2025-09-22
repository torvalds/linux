/*	$OpenBSD: ip_ecn.c,v 1.10 2025/07/08 00:47:41 jsg Exp $	*/
/*	$KAME: ip_ecn.c,v 1.9 2000/10/01 12:44:48 itojun Exp $	*/

/*
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

#include <sys/param.h>
#include <sys/systm.h>

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_ecn.h>

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
 * call it after you've done the default initialization/copy for the outer.
 */
void
ip_ecn_ingress(int mode, u_int8_t *outer, u_int8_t *inner)
{
	if (!outer || !inner)
		panic("NULL pointer passed to ip_ecn_ingress");

	*outer = *inner;
	switch (mode) {
	case ECN_ALLOWED:		/* ECN allowed */
	case ECN_ALLOWED_IPSEC:
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
 * call it after you've done the default initialization/copy for the inner.
 * the caller should drop the packet if the return value is 0.
 */
int
ip_ecn_egress(int mode, u_int8_t *outer, u_int8_t *inner)
{
	if (!outer || !inner)
		panic("NULL pointer passed to ip_ecn_egress");

	switch (mode) {
	case ECN_ALLOWED:
	case ECN_ALLOWED_IPSEC:
		/*
		 * full-functionality: if the outer is CE and the inner is
		 * not-ECT, should drop it.  otherwise, copy CE.
		 * However, according to RFC4301, we should just leave the
		 * inner as non-ECT for IPsec.
		 */
		if ((*outer & IPTOS_ECN_MASK) == IPTOS_ECN_CE) {
			if ((*inner & IPTOS_ECN_MASK) == IPTOS_ECN_NOTECT) {
				if (mode == ECN_ALLOWED_IPSEC)
					return (1);
				else
					return (0);
			}
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

/*
 * Patch the checksum with the difference between the old and new tos.
 * The patching is based on what pf_patch_8() and pf_cksum_fixkup() do,
 * but they're in pf, so we can't rely on them being available.
 */
void
ip_tos_patch(struct ip *ip, uint8_t tos)
{
	uint16_t old;
	uint16_t new;
	uint32_t x;

	old = htons(ip->ip_tos);
	new = htons(tos);

	ip->ip_tos = tos;

	x = ip->ip_sum + old - new;
	ip->ip_sum = (x) + (x >> 16);
}
