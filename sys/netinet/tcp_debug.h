/*	$OpenBSD: tcp_debug.h,v 1.11 2018/05/10 13:30:25 bluhm Exp $	*/
/*	$NetBSD: tcp_debug.h,v 1.5 1994/06/29 06:38:38 cgd Exp $	*/

/*
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
 */

#ifndef _NETINET_TCP_DEBUG_H_
#define _NETINET_TCP_DEBUG_H_

/*
 * Tcp+ip header, after ip options removed.
 */
struct tcpiphdr {
	struct	ipovly ti_i;		/* overlaid ip structure */
	struct	tcphdr ti_t;		/* tcp header */
};
#define	ti_x1		ti_i.ih_x1
#define	ti_pr		ti_i.ih_pr
#define	ti_len		ti_i.ih_len
#define	ti_src		ti_i.ih_src
#define	ti_dst		ti_i.ih_dst
#define	ti_sport	ti_t.th_sport
#define	ti_dport	ti_t.th_dport
#define	ti_seq		ti_t.th_seq
#define	ti_ack		ti_t.th_ack
#define	ti_x2		ti_t.th_x2
#define	ti_off		ti_t.th_off
#define	ti_flags	ti_t.th_flags
#define	ti_win		ti_t.th_win
#define	ti_sum		ti_t.th_sum
#define	ti_urp		ti_t.th_urp

struct tcpipv6hdr {
	struct ip6_hdr ti6_i;
	struct tcphdr ti6_t;
};

#define	ti6_src		ti6_i.ip6_src
#define	ti6_dst		ti6_i.ip6_dst
#define	ti6_plen	ti6_i.ip6_plen
#define	ti6_sport	ti6_t.th_sport
#define	ti6_dport	ti6_t.th_dport
#define	ti6_seq		ti6_t.th_seq
#define	ti6_ack		ti6_t.th_ack
#define	ti6_x2		ti6_t.th_x2
#define	ti6_off		ti6_t.th_off
#define	ti6_flags	ti6_t.th_flags
#define	ti6_win		ti6_t.th_win
#define	ti6_sum		ti6_t.th_sum
#define	ti6_urp		ti6_t.th_urp

struct	tcp_debug {
	uint32_t td_time;
	short	td_act;
	short	td_ostate;
	caddr_t	td_tcb;
	struct	tcpiphdr td_ti;
	struct  tcpipv6hdr td_ti6;
	short	td_req;
	struct	tcpcb td_cb;
};

#define	TA_INPUT	0
#define	TA_OUTPUT	1
#define	TA_USER		2
#define	TA_RESPOND	3
#define	TA_DROP		4
#define	TA_TIMER	5

#ifdef TANAMES
const char *tanames[] =
    { "input", "output", "user", "respond", "drop", "timer" };
#endif /* TANAMES */

#define	TCP_NDEBUG 100
extern struct	tcp_debug tcp_debug[];
extern int	tcp_debx;
#endif /* _NETINET_TCP_DEBUG_H_ */
