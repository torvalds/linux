/*	$OpenBSD: tcp.h,v 1.24 2023/05/19 01:04:39 guenther Exp $	*/
/*	$NetBSD: tcp.h,v 1.8 1995/04/17 05:32:58 cgd Exp $	*/

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
 *	@(#)tcp.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _NETINET_TCP_H_
#define	_NETINET_TCP_H_

#include <sys/cdefs.h>

#if __BSD_VISIBLE

typedef u_int32_t tcp_seq;

/*
 * TCP header.
 * Per RFC 793, September, 1981.
 */
struct tcphdr {
	u_int16_t th_sport;		/* source port */
	u_int16_t th_dport;		/* destination port */
	tcp_seq	  th_seq;		/* sequence number */
	tcp_seq	  th_ack;		/* acknowledgement number */
#if _BYTE_ORDER == _LITTLE_ENDIAN
	u_int32_t th_x2:4,		/* (unused) */
		  th_off:4;		/* data offset */
#endif
#if _BYTE_ORDER == _BIG_ENDIAN
	u_int32_t th_off:4,		/* data offset */
		  th_x2:4;		/* (unused) */
#endif
	u_int8_t  th_flags;
#define	TH_FIN	  0x01
#define	TH_SYN	  0x02
#define	TH_RST	  0x04
#define	TH_PUSH	  0x08
#define	TH_ACK	  0x10
#define	TH_URG	  0x20
#define	TH_ECE	  0x40
#define	TH_CWR	  0x80
	u_int16_t th_win;			/* window */
	u_int16_t th_sum;			/* checksum */
	u_int16_t th_urp;			/* urgent pointer */
};
#define th_reseqlen th_urp			/* TCP data length for
						   resequencing/reassembly */

#define	TCPOPT_EOL		0
#define	TCPOPT_NOP		1
#define	TCPOPT_MAXSEG		2
#define	TCPOLEN_MAXSEG		4
#define	TCPOPT_WINDOW		3
#define	TCPOLEN_WINDOW		3
#define	TCPOPT_SACK_PERMITTED	4		/* Experimental */
#define	TCPOLEN_SACK_PERMITTED	2
#define	TCPOPT_SACK		5		/* Experimental */
#define	TCPOLEN_SACK		8		/* 2*sizeof(tcp_seq) */
#define	TCPOPT_TIMESTAMP	8
#define	TCPOLEN_TIMESTAMP		10
#define	TCPOLEN_TSTAMP_APPA		(TCPOLEN_TIMESTAMP+2) /* appendix A */
#define	TCPOPT_SIGNATURE	19
#define	TCPOLEN_SIGNATURE		18
#define	TCPOLEN_SIGLEN		(TCPOLEN_SIGNATURE+2) /* padding */

#define	MAX_TCPOPTLEN		40	/* Absolute maximum TCP options len */

#define	TCPOPT_TSTAMP_HDR	\
    (TCPOPT_NOP<<24|TCPOPT_NOP<<16|TCPOPT_TIMESTAMP<<8|TCPOLEN_TIMESTAMP)

/* Option definitions */
#define	TCPOPT_SACK_PERMIT_HDR \
(TCPOPT_NOP<<24|TCPOPT_NOP<<16|TCPOPT_SACK_PERMITTED<<8|TCPOLEN_SACK_PERMITTED)
#define	TCPOPT_SACK_HDR   (TCPOPT_NOP<<24|TCPOPT_NOP<<16|TCPOPT_SACK<<8)
/* Miscellaneous constants */
#define	MAX_SACK_BLKS	6	/* Max # SACK blocks stored at sender side */
#define	TCP_MAX_SACK	3	/* Max # SACKs sent in any segment */
#define	TCP_SACKHOLE_LIMIT 128	/* Max # SACK holes per connection */

/*
 * Default maximum segment size for TCP.
 * With an IP MSS of 576, this is 536,
 * but 512 is probably more convenient.
 * This should be defined as min(512, IP_MSS - sizeof (struct tcpiphdr)).
 */
#define	TCP_MSS		512

#define	TCP_MAXWIN	65535	/* largest value for (unscaled) window */

#define	TCP_MAX_WINSHIFT	14	/* maximum window shift */

/*
 * The TCP_INFO socket option comes from the Linux 2.6 TCP API, and permits
 * the caller to query certain information about the state of a TCP
 * connection.  Provide an overlapping set of fields with the Linux
 * implementation, but at the same time add a lot of OpenBSD specific
 * extra information.
 */
struct tcp_info {
	uint8_t		tcpi_state;		/* TCP FSM state. */
	uint8_t		__tcpi_ca_state;
	uint8_t		__tcpi_retransmits;
	uint8_t		__tcpi_probes;
	uint8_t		__tcpi_backoff;
	uint8_t		tcpi_options;		/* Options enabled on conn. */
#define	TCPI_OPT_TIMESTAMPS	0x01
#define	TCPI_OPT_SACK		0x02
#define	TCPI_OPT_WSCALE		0x04
#define	TCPI_OPT_ECN		0x08
#define	TCPI_OPT_TOE		0x10
	uint8_t		tcpi_snd_wscale;	/* RFC1323 send shift value. */
	uint8_t		tcpi_rcv_wscale;	/* RFC1323 recv shift value. */

	uint32_t	tcpi_rto;	   /* Retransmission timeout (usec). */
	uint32_t	__tcpi_ato;
	uint32_t	tcpi_snd_mss;		/* Max segment size for send. */
	uint32_t	tcpi_rcv_mss;		/* Max segment size for recv. */

	uint32_t	__tcpi_unacked;
	uint32_t	__tcpi_sacked;
	uint32_t	__tcpi_lost;
	uint32_t	__tcpi_retrans;
	uint32_t	__tcpi_fackets;

	/* Times; measurements in usecs. */
	uint32_t	tcpi_last_data_sent;	/* since last sent data. */
	uint32_t	tcpi_last_ack_sent;	/* since last sent ack. */
	uint32_t	tcpi_last_data_recv;	/* since last recv data. */
	uint32_t	tcpi_last_ack_recv;	/* since last recv ack. */

	/* Metrics; variable units. */
	uint32_t	__tcpi_pmtu;
	uint32_t	__tcpi_rcv_ssthresh;
	uint32_t	tcpi_rtt;		/* Smoothed RTT in usecs. */
	uint32_t	tcpi_rttvar;		/* RTT variance in usecs. */
	uint32_t	tcpi_snd_ssthresh;	/* Slow start threshold. */
	uint32_t	tcpi_snd_cwnd;		/* Send congestion window. */
	uint32_t	__tcpi_advmss;
	uint32_t	__tcpi_reordering;

	uint32_t	__tcpi_rcv_rtt;
	uint32_t	tcpi_rcv_space;		/* Advertised recv window. */

	/*
	 * Members below this point are only set if process is privileged,
	 * otherwise values will be 0.
	 */

	/* FreeBSD/NetBSD extensions to tcp_info. */
	uint32_t	tcpi_snd_wnd;		/* Advertised send window. */
	uint32_t	tcpi_snd_nxt;		/* Next egress seqno */
	uint32_t	tcpi_rcv_nxt;		/* Next ingress seqno */
	uint32_t	tcpi_toe_tid;		/* HWTID for TOE endpoints */
	uint32_t	tcpi_snd_rexmitpack;	/* Retransmitted packets */
	uint32_t	tcpi_rcv_ooopack;	/* Out-of-order packets */
	uint32_t	tcpi_snd_zerowin;	/* Zero-sized windows sent */

	/* OpenBSD extensions */
	uint32_t	tcpi_rttmin;
	uint32_t	tcpi_max_sndwnd;
	uint32_t	tcpi_rcv_adv;
	uint32_t	tcpi_rcv_up;
	uint32_t	tcpi_snd_una;
	uint32_t	tcpi_snd_up;
	uint32_t	tcpi_snd_wl1;
	uint32_t	tcpi_snd_wl2;
	uint32_t	tcpi_snd_max;
	uint32_t	tcpi_ts_recent;
	uint32_t	tcpi_ts_recent_age;
	uint32_t	tcpi_rfbuf_cnt;
	uint32_t	tcpi_rfbuf_ts;
	uint32_t	tcpi_so_rcv_sb_cc;
	uint32_t	tcpi_so_rcv_sb_hiwat;
	uint32_t	tcpi_so_rcv_sb_lowat;
	uint32_t	tcpi_so_rcv_sb_wat;
	uint32_t	tcpi_so_snd_sb_cc;
	uint32_t	tcpi_so_snd_sb_hiwat;
	uint32_t	tcpi_so_snd_sb_lowat;
	uint32_t	tcpi_so_snd_sb_wat;
};

#endif /* __BSD_VISIBLE */

/*
 * User-settable options (used with setsockopt).
 */
#define	TCP_NODELAY		0x01   /* don't delay send to coalesce pkts */
#define	TCP_MAXSEG		0x02   /* set maximum segment size */
#define	TCP_MD5SIG		0x04   /* enable TCP MD5 signature option */
#define	TCP_SACK_ENABLE		0x08   /* enable SACKs (if disabled by def.) */
#define	TCP_INFO		0x09   /* retrieve tcp_info structure */
#define	TCP_NOPUSH		0x10   /* don't push last block of write */

#endif /* _NETINET_TCP_H_ */
