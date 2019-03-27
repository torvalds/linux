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
 *	@(#)tcp.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#ifndef _NETINET_TCP_H_
#define _NETINET_TCP_H_

#include <sys/cdefs.h>
#include <sys/types.h>

#if __BSD_VISIBLE

typedef	u_int32_t tcp_seq;

#define tcp6_seq	tcp_seq	/* for KAME src sync over BSD*'s */
#define tcp6hdr		tcphdr	/* for KAME src sync over BSD*'s */

/*
 * TCP header.
 * Per RFC 793, September, 1981.
 */
struct tcphdr {
	u_short	th_sport;		/* source port */
	u_short	th_dport;		/* destination port */
	tcp_seq	th_seq;			/* sequence number */
	tcp_seq	th_ack;			/* acknowledgement number */
#if BYTE_ORDER == LITTLE_ENDIAN
	u_char	th_x2:4,		/* (unused) */
		th_off:4;		/* data offset */
#endif
#if BYTE_ORDER == BIG_ENDIAN
	u_char	th_off:4,		/* data offset */
		th_x2:4;		/* (unused) */
#endif
	u_char	th_flags;
#define	TH_FIN	0x01
#define	TH_SYN	0x02
#define	TH_RST	0x04
#define	TH_PUSH	0x08
#define	TH_ACK	0x10
#define	TH_URG	0x20
#define	TH_ECE	0x40
#define	TH_CWR	0x80
#define	TH_FLAGS	(TH_FIN|TH_SYN|TH_RST|TH_PUSH|TH_ACK|TH_URG|TH_ECE|TH_CWR)
#define	PRINT_TH_FLAGS	"\20\1FIN\2SYN\3RST\4PUSH\5ACK\6URG\7ECE\10CWR"

	u_short	th_win;			/* window */
	u_short	th_sum;			/* checksum */
	u_short	th_urp;			/* urgent pointer */
};

#define	TCPOPT_EOL		0
#define	   TCPOLEN_EOL			1
#define	TCPOPT_PAD		0		/* padding after EOL */
#define	   TCPOLEN_PAD			1
#define	TCPOPT_NOP		1
#define	   TCPOLEN_NOP			1
#define	TCPOPT_MAXSEG		2
#define    TCPOLEN_MAXSEG		4
#define TCPOPT_WINDOW		3
#define    TCPOLEN_WINDOW		3
#define TCPOPT_SACK_PERMITTED	4
#define    TCPOLEN_SACK_PERMITTED	2
#define TCPOPT_SACK		5
#define	   TCPOLEN_SACKHDR		2
#define    TCPOLEN_SACK			8	/* 2*sizeof(tcp_seq) */
#define TCPOPT_TIMESTAMP	8
#define    TCPOLEN_TIMESTAMP		10
#define    TCPOLEN_TSTAMP_APPA		(TCPOLEN_TIMESTAMP+2) /* appendix A */
#define	TCPOPT_SIGNATURE	19		/* Keyed MD5: RFC 2385 */
#define	   TCPOLEN_SIGNATURE		18
#define	TCPOPT_FAST_OPEN	34
#define	   TCPOLEN_FAST_OPEN_EMPTY	2

/* Miscellaneous constants */
#define	MAX_SACK_BLKS	6	/* Max # SACK blocks stored at receiver side */
#define	TCP_MAX_SACK	4	/* MAX # SACKs sent in any segment */


/*
 * The default maximum segment size (MSS) to be used for new TCP connections
 * when path MTU discovery is not enabled.
 *
 * RFC879 derives the default MSS from the largest datagram size hosts are
 * minimally required to handle directly or through IP reassembly minus the
 * size of the IP and TCP header.  With IPv6 the minimum MTU is specified
 * in RFC2460.
 *
 * For IPv4 the MSS is 576 - sizeof(struct tcpiphdr)
 * For IPv6 the MSS is IPV6_MMTU - sizeof(struct ip6_hdr) - sizeof(struct tcphdr)
 *
 * We use explicit numerical definition here to avoid header pollution.
 */
#define	TCP_MSS		536
#define	TCP6_MSS	1220

/*
 * Limit the lowest MSS we accept for path MTU discovery and the TCP SYN MSS
 * option.  Allowing low values of MSS can consume significant resources and
 * be used to mount a resource exhaustion attack.
 * Connections requesting lower MSS values will be rounded up to this value
 * and the IP_DF flag will be cleared to allow fragmentation along the path.
 *
 * See tcp_subr.c tcp_minmss SYSCTL declaration for more comments.  Setting
 * it to "0" disables the minmss check.
 *
 * The default value is fine for TCP across the Internet's smallest official
 * link MTU (256 bytes for AX.25 packet radio).  However, a connection is very
 * unlikely to come across such low MTU interfaces these days (anno domini 2003).
 */
#define	TCP_MINMSS 216

#define	TCP_MAXWIN	65535	/* largest value for (unscaled) window */
#define	TTCP_CLIENT_SND_WND	4096	/* dflt send window for T/TCP client */

#define TCP_MAX_WINSHIFT	14	/* maximum window shift */

#define TCP_MAXBURST		4	/* maximum segments in a burst */

#define TCP_MAXHLEN	(0xf<<2)	/* max length of header in bytes */
#define TCP_MAXOLEN	(TCP_MAXHLEN - sizeof(struct tcphdr))
					/* max space left for options */

#define TCP_FASTOPEN_MIN_COOKIE_LEN	4	/* Per RFC7413 */
#define TCP_FASTOPEN_MAX_COOKIE_LEN	16	/* Per RFC7413 */
#define TCP_FASTOPEN_PSK_LEN		16	/* Same as TCP_FASTOPEN_KEY_LEN */
#endif /* __BSD_VISIBLE */

/*
 * User-settable options (used with setsockopt).  These are discrete
 * values and are not masked together.  Some values appear to be
 * bitmasks for historical reasons.
 */
#define	TCP_NODELAY	1	/* don't delay send to coalesce packets */
#if __BSD_VISIBLE
#define	TCP_MAXSEG	2	/* set maximum segment size */
#define TCP_NOPUSH	4	/* don't push last block of write */
#define TCP_NOOPT	8	/* don't use TCP options */
#define TCP_MD5SIG	16	/* use MD5 digests (RFC2385) */
#define	TCP_INFO	32	/* retrieve tcp_info structure */
#define	TCP_LOG		34	/* configure event logging for connection */
#define	TCP_LOGBUF	35	/* retrieve event log for connection */
#define	TCP_LOGID	36	/* configure log ID to correlate connections */
#define	TCP_LOGDUMP	37	/* dump connection log events to device */
#define	TCP_LOGDUMPID	38	/* dump events from connections with same ID to
				   device */
#define	TCP_CONGESTION	64	/* get/set congestion control algorithm */
#define	TCP_CCALGOOPT	65	/* get/set cc algorithm specific options */
#define TCP_DELACK  	72	/* socket option for delayed ack */
#define	TCP_KEEPINIT	128	/* N, time to establish connection */
#define	TCP_KEEPIDLE	256	/* L,N,X start keeplives after this period */
#define	TCP_KEEPINTVL	512	/* L,N interval between keepalives */
#define	TCP_KEEPCNT	1024	/* L,N number of keepalives before close */
#define	TCP_FASTOPEN	1025	/* enable TFO / was created via TFO */
#define	TCP_PCAP_OUT	2048	/* number of output packets to keep */
#define	TCP_PCAP_IN	4096	/* number of input packets to keep */
#define TCP_FUNCTION_BLK 8192	/* Set the tcp function pointers to the specified stack */
/* Options for Rack and BBR */
#define TCP_RACK_PROP	      1051 /* RACK proportional rate reduction (bool) */
#define TCP_RACK_TLP_REDUCE   1052 /* RACK TLP cwnd reduction (bool) */
#define TCP_RACK_PACE_REDUCE  1053 /* RACK Pacing reduction factor (divisor) */
#define TCP_RACK_PACE_MAX_SEG 1054 /* Max segments in a pace */
#define TCP_RACK_PACE_ALWAYS  1055 /* Use the always pace method */
#define TCP_RACK_PROP_RATE    1056 /* The proportional reduction rate */
#define TCP_RACK_PRR_SENDALOT 1057 /* Allow PRR to send more than one seg */
#define TCP_RACK_MIN_TO       1058 /* Minimum time between rack t-o's in ms */
#define TCP_RACK_EARLY_RECOV  1059 /* Should recovery happen early (bool) */
#define TCP_RACK_EARLY_SEG    1060 /* If early recovery max segments */
#define TCP_RACK_REORD_THRESH 1061 /* RACK reorder threshold (shift amount) */
#define TCP_RACK_REORD_FADE   1062 /* Does reordering fade after ms time */
#define TCP_RACK_TLP_THRESH   1063 /* RACK TLP theshold i.e. srtt+(srtt/N) */
#define TCP_RACK_PKT_DELAY    1064 /* RACK added ms i.e. rack-rtt + reord + N */
#define TCP_RACK_TLP_INC_VAR  1065 /* Does TLP include rtt variance in t-o */
#define TCP_RACK_SESS_CWV     1066 /* Enable RFC7611 cwnd validation on sess */
#define TCP_BBR_IWINTSO	      1067 /* Initial TSO window for BBRs first sends */
#define TCP_BBR_RECFORCE      1068 /* Enter recovery force out a segment disregard pacer */
#define TCP_BBR_STARTUP_PG    1069 /* Startup pacing gain */
#define TCP_BBR_DRAIN_PG      1070 /* Drain pacing gain */
#define TCP_BBR_RWND_IS_APP   1071 /* Rwnd limited is considered app limited */
#define TCP_BBR_PROBE_RTT_INT 1072 /* How long in useconds between probe-rtt */
#define TCP_BBR_ONE_RETRAN    1073 /* Is only one segment allowed out during retran */
#define TCP_BBR_STARTUP_LOSS_EXIT 1074	/* Do we exit a loss during startup if not 20% incr */
#define TCP_BBR_USE_LOWGAIN   1075 /* lower the gain in PROBE_BW enable */
#define TCP_BBR_LOWGAIN_THRESH 1076 /* How many cycles do we stay in lowgain */
#define TCP_BBR_LOWGAIN_HALF  1077 /* Do we halfstep lowgain down */
#define TCP_BBR_LOWGAIN_FD    1078 /* Do we force a drain when lowgain in place */
#define TCP_BBR_USEDEL_RATE   1079 /* Enable use of delivery rate for loss recovery */
#define TCP_BBR_MIN_RTO       1080 /* Min RTO in milliseconds */
#define TCP_BBR_MAX_RTO	      1081 /* Max RTO in milliseconds */
#define TCP_BBR_REC_OVER_HPTS 1082 /* Recovery override htps settings 0/1/3 */
#define TCP_BBR_UNLIMITED     1083 /* Does BBR, in non-recovery not use cwnd */
#define TCP_BBR_DRAIN_INC_EXTRA 1084 /* Does the 3/4 drain target include the extra gain */
#define TCP_BBR_STARTUP_EXIT_EPOCH 1085 /* what epoch gets us out of startup */
#define TCP_BBR_PACE_PER_SEC   1086
#define TCP_BBR_PACE_DEL_TAR   1087
#define TCP_BBR_PACE_SEG_MAX   1088
#define TCP_BBR_PACE_SEG_MIN   1089
#define TCP_BBR_PACE_CROSS     1090
#define TCP_RACK_IDLE_REDUCE_HIGH 1092  /* Reduce the highest cwnd seen to IW on idle */
#define TCP_RACK_IDLE_REDUCE_HIGH 1092  /* Reduce the highest cwnd seen to IW on idle */
#define TCP_RACK_MIN_PACE      1093 	/* Do we enforce rack min pace time */
#define TCP_RACK_MIN_PACE_SEG  1094	/* If so what is the seg threshould */
#define TCP_RACK_TLP_USE       1095
#define TCP_BBR_ACK_COMP_ALG   1096 	/* Not used */
#define TCP_BBR_EXTRA_GAIN     1097
#define TCP_BBR_RACK_RTT_USE   1098	/* what RTT should we use 0, 1, or 2? */
#define TCP_BBR_RETRAN_WTSO    1099
#define TCP_DATA_AFTER_CLOSE   1100
#define TCP_BBR_PROBE_RTT_GAIN 1101
#define TCP_BBR_PROBE_RTT_LEN  1102


/* Start of reserved space for third-party user-settable options. */
#define	TCP_VENDOR	SO_VENDOR

#define	TCP_CA_NAME_MAX	16	/* max congestion control name length */

#define	TCPI_OPT_TIMESTAMPS	0x01
#define	TCPI_OPT_SACK		0x02
#define	TCPI_OPT_WSCALE		0x04
#define	TCPI_OPT_ECN		0x08
#define	TCPI_OPT_TOE		0x10

/* Maximum length of log ID. */
#define TCP_LOG_ID_LEN	64

/*
 * The TCP_INFO socket option comes from the Linux 2.6 TCP API, and permits
 * the caller to query certain information about the state of a TCP
 * connection.  We provide an overlapping set of fields with the Linux
 * implementation, but since this is a fixed size structure, room has been
 * left for growth.  In order to maximize potential future compatibility with
 * the Linux API, the same variable names and order have been adopted, and
 * padding left to make room for omitted fields in case they are added later.
 *
 * XXX: This is currently an unstable ABI/API, in that it is expected to
 * change.
 */
struct tcp_info {
	u_int8_t	tcpi_state;		/* TCP FSM state. */
	u_int8_t	__tcpi_ca_state;
	u_int8_t	__tcpi_retransmits;
	u_int8_t	__tcpi_probes;
	u_int8_t	__tcpi_backoff;
	u_int8_t	tcpi_options;		/* Options enabled on conn. */
	u_int8_t	tcpi_snd_wscale:4,	/* RFC1323 send shift value. */
			tcpi_rcv_wscale:4;	/* RFC1323 recv shift value. */

	u_int32_t	tcpi_rto;		/* Retransmission timeout (usec). */
	u_int32_t	__tcpi_ato;
	u_int32_t	tcpi_snd_mss;		/* Max segment size for send. */
	u_int32_t	tcpi_rcv_mss;		/* Max segment size for receive. */

	u_int32_t	__tcpi_unacked;
	u_int32_t	__tcpi_sacked;
	u_int32_t	__tcpi_lost;
	u_int32_t	__tcpi_retrans;
	u_int32_t	__tcpi_fackets;

	/* Times; measurements in usecs. */
	u_int32_t	__tcpi_last_data_sent;
	u_int32_t	__tcpi_last_ack_sent;	/* Also unimpl. on Linux? */
	u_int32_t	tcpi_last_data_recv;	/* Time since last recv data. */
	u_int32_t	__tcpi_last_ack_recv;

	/* Metrics; variable units. */
	u_int32_t	__tcpi_pmtu;
	u_int32_t	__tcpi_rcv_ssthresh;
	u_int32_t	tcpi_rtt;		/* Smoothed RTT in usecs. */
	u_int32_t	tcpi_rttvar;		/* RTT variance in usecs. */
	u_int32_t	tcpi_snd_ssthresh;	/* Slow start threshold. */
	u_int32_t	tcpi_snd_cwnd;		/* Send congestion window. */
	u_int32_t	__tcpi_advmss;
	u_int32_t	__tcpi_reordering;

	u_int32_t	__tcpi_rcv_rtt;
	u_int32_t	tcpi_rcv_space;		/* Advertised recv window. */

	/* FreeBSD extensions to tcp_info. */
	u_int32_t	tcpi_snd_wnd;		/* Advertised send window. */
	u_int32_t	tcpi_snd_bwnd;		/* No longer used. */
	u_int32_t	tcpi_snd_nxt;		/* Next egress seqno */
	u_int32_t	tcpi_rcv_nxt;		/* Next ingress seqno */
	u_int32_t	tcpi_toe_tid;		/* HWTID for TOE endpoints */
	u_int32_t	tcpi_snd_rexmitpack;	/* Retransmitted packets */
	u_int32_t	tcpi_rcv_ooopack;	/* Out-of-order packets */
	u_int32_t	tcpi_snd_zerowin;	/* Zero-sized windows sent */
	
	/* Padding to grow without breaking ABI. */
	u_int32_t	__tcpi_pad[26];		/* Padding. */
};

/*
 * If this structure is provided when setting the TCP_FASTOPEN socket
 * option, and the enable member is non-zero, a subsequent connect will use
 * pre-shared key (PSK) mode using the provided key.
 */
struct tcp_fastopen {
	int enable;
	uint8_t psk[TCP_FASTOPEN_PSK_LEN];
};
#endif
#define TCP_FUNCTION_NAME_LEN_MAX 32

struct tcp_function_set {
	char function_set_name[TCP_FUNCTION_NAME_LEN_MAX];
	uint32_t pcbcnt;
};

#endif /* !_NETINET_TCP_H_ */
