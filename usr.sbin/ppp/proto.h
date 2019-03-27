/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996 - 2001 Brian Somers <brian@Awfulhak.org>
 *          based on work by Toshiharu OHNO <tony-o@iij.ad.jp>
 *                           Internet Initiative Japan, Inc (IIJ)
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 *  Definition of protocol numbers
 */
#define	PROTO_IP	0x0021	/* IP */
#define	PROTO_VJUNCOMP	0x002f	/* VJ Uncompressed */
#define	PROTO_VJCOMP	0x002d	/* VJ Compressed */
#define	PROTO_MP	0x003d	/* Multilink fragment */
#ifndef NOINET6
#define	PROTO_IPV6	0x0057	/* IPv6 */
#endif
#define	PROTO_ICOMPD	0x00fb	/* Individual link compressed */
#define	PROTO_COMPD	0x00fd	/* Compressed datagram */

#define PROTO_COMPRESSIBLE(p) (((p) & 0xff81) == 0x01)

#define	PROTO_IPCP	0x8021
#ifndef NOINET6
#define	PROTO_IPV6CP	0x8057
#endif
#define	PROTO_ICCP	0x80fb
#define	PROTO_CCP	0x80fd

#define	PROTO_LCP	0xc021
#define	PROTO_PAP	0xc023
#define	PROTO_CBCP	0xc029
#define	PROTO_LQR	0xc025
#define	PROTO_CHAP	0xc223

struct lcp;

extern int proto_WrapperOctets(struct lcp *, u_short);
struct mbuf *proto_Prepend(struct mbuf *, u_short, unsigned, int);

extern struct layer protolayer;
