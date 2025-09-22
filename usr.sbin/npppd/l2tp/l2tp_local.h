/*	$OpenBSD: l2tp_local.h,v 1.6 2012/07/16 18:05:36 markus Exp $	*/
/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
 */
#ifndef	L2TP_LOCAL_H
#define	L2TP_LOCAL_H 1
/* $Id: l2tp_local.h,v 1.6 2012/07/16 18:05:36 markus Exp $ */

#ifndef	GETSHORT
#define	GETSHORT(s, cp) {	\
    s = *(cp)++ << 8;		\
    s |= *(cp)++;		\
}
#endif

struct l2tp_header {
#if	BYTE_ORDER == LITTLE_ENDIAN
	uint8_t		p:1,
			o:1,
			x2:1,
			s:1,
			x1:2,
			l:1,
			t:1;
	uint8_t		ver:4,
			x3:4;
#else
	uint8_t		t:1,
			l:1,
			x1:2,
			s:1,
			x2:1,
			o:1,
			p:1;
	uint8_t		x3:4,
			ver:4;
#endif
	uint16_t	length;
	uint16_t	tunnel_id;
	uint16_t	session_id;
	uint16_t	ns;
	uint16_t	nr;
} __attribute__((__packed__));

#ifndef	countof
#define	countof(x)	(sizeof((x)) / sizeof((x)[0]))
#endif

#define	LISTENER_SOCK(ctrl)	\
	((l2tpd_listener *)slist_get(&(ctrl)->l2tpd->listener, \
	    (ctrl)->listener_index))->sock
#ifndef SIN
#define SIN(ss)	((struct sockaddr_in *)(ss))
#endif
#define SIN6(ss)	((struct sockaddr_in6 *)(ss))

#define	L2TP_SESSION_ID_MASK		0x00007fff
#define	L2TP_SESSION_ID_SHUFFLE_MARK	0x10000000

#ifndef	L2TP_NCALL 
#define	L2TP_NCALL 		10000
#endif

#if L2TP_NCALL > 0xffff
#error L2TP_NCALL must be less than 65536
#endif

#ifndef USE_LIBSOCKUTIL
struct in_ipsec_sa_cookie	{
	u_int32_t	ipsecflow;
};
#endif

#endif
