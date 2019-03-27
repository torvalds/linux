/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993
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
 *	@(#)tcp_debug.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#ifndef _NETINET_TCP_DEBUG_H_
#define	_NETINET_TCP_DEBUG_H_

struct	tcp_debug {
	uint32_t	td_time;	/* network format */
	short	td_act;
	short	td_ostate;
	caddr_t	td_tcb;
	int	td_family;
	/*
	 * Co-existense of td_ti and td_ti6 below is ugly, but it is necessary
	 * to achieve backword compatibility to some extent.
	 */
	struct	tcpiphdr td_ti;
	struct {
#define	IP6_HDR_LEN	40	/* sizeof(struct ip6_hdr) */
#if !defined(_KERNEL) && defined(INET6)
		struct	ip6_hdr ip6;
#else
		u_char	ip6buf[IP6_HDR_LEN];
#endif
		struct	tcphdr th;
	} td_ti6;
#define	td_ip6buf	td_ti6.ip6buf
	short	td_req;
	struct	tcpcb td_cb;
};

#define	TA_INPUT	0
#define	TA_OUTPUT	1
#define	TA_USER		2
#define	TA_RESPOND	3
#define	TA_DROP		4

#ifdef TANAMES
static const char	*tanames[] =
    { "input", "output", "user", "respond", "drop" };
#endif

#define	TCP_NDEBUG 100

#ifndef _KERNEL
/* XXX common variables for broken applications. */
struct	tcp_debug tcp_debug[TCP_NDEBUG];
int	tcp_debx;
#endif

#endif /* !_NETINET_TCP_DEBUG_H_ */
