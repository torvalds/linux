/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the TCP protocol.
 *
 * Version:	@(#)tcp.h	1.0.2	04/28/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _UAPI_LINUX_TCP_H
#define _UAPI_LINUX_TCP_H

#include <linux/types.h>
#include <asm/byteorder.h>
#include <linux/socket.h>

struct tcphdr {
	__be16	source;
	__be16	dest;
	__be32	seq;
	__be32	ack_seq;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	__u16	res1:4,
		doff:4,
		fin:1,
		syn:1,
		rst:1,
		psh:1,
		ack:1,
		urg:1,
		ece:1,
		cwr:1;
#elif defined(__BIG_ENDIAN_BITFIELD)
	__u16	doff:4,
		res1:4,
		cwr:1,
		ece:1,
		urg:1,
		ack:1,
		psh:1,
		rst:1,
		syn:1,
		fin:1;
#else
#error	"Adjust your <asm/byteorder.h> defines"
#endif	
	__be16	window;
	__sum16	check;
	__be16	urg_ptr;
};

/*
 *	The union cast uses a gcc extension to avoid aliasing problems
 *  (union is compatible to any of its members)
 *  This means this part of the code is -fstrict-aliasing safe now.
 */
union tcp_word_hdr { 
	struct tcphdr hdr;
	__be32 		  words[5];
}; 

#define tcp_flag_word(tp) ( ((union tcp_word_hdr *)(tp))->words [3]) 

enum { 
	TCP_FLAG_CWR = __constant_cpu_to_be32(0x00800000),
	TCP_FLAG_ECE = __constant_cpu_to_be32(0x00400000),
	TCP_FLAG_URG = __constant_cpu_to_be32(0x00200000),
	TCP_FLAG_ACK = __constant_cpu_to_be32(0x00100000),
	TCP_FLAG_PSH = __constant_cpu_to_be32(0x00080000),
	TCP_FLAG_RST = __constant_cpu_to_be32(0x00040000),
	TCP_FLAG_SYN = __constant_cpu_to_be32(0x00020000),
	TCP_FLAG_FIN = __constant_cpu_to_be32(0x00010000),
	TCP_RESERVED_BITS = __constant_cpu_to_be32(0x0F000000),
	TCP_DATA_OFFSET = __constant_cpu_to_be32(0xF0000000)
}; 

/*
 * TCP general constants
 */
#define TCP_MSS_DEFAULT		 536U	/* IPv4 (RFC1122, RFC2581) */
#define TCP_MSS_DESIRED		1220U	/* IPv6 (tunneled), EDNS0 (RFC3226) */

/* TCP socket options */
#define TCP_NODELAY		1	/* Turn off Nagle's algorithm. */
#define TCP_MAXSEG		2	/* Limit MSS */
#define TCP_CORK		3	/* Never send partially complete segments */
#define TCP_KEEPIDLE		4	/* Start keeplives after this period */
#define TCP_KEEPINTVL		5	/* Interval between keepalives */
#define TCP_KEEPCNT		6	/* Number of keepalives before death */
#define TCP_SYNCNT		7	/* Number of SYN retransmits */
#define TCP_LINGER2		8	/* Life time of orphaned FIN-WAIT-2 state */
#define TCP_DEFER_ACCEPT	9	/* Wake up listener only when data arrive */
#define TCP_WINDOW_CLAMP	10	/* Bound advertised window */
#define TCP_INFO		11	/* Information about this connection. */
#define TCP_QUICKACK		12	/* Block/reenable quick acks */
#define TCP_CONGESTION		13	/* Congestion control algorithm */
#define TCP_MD5SIG		14	/* TCP MD5 Signature (RFC2385) */
#define TCP_THIN_LINEAR_TIMEOUTS 16      /* Use linear timeouts for thin streams*/
#define TCP_THIN_DUPACK         17      /* Fast retrans. after 1 dupack */
#define TCP_USER_TIMEOUT	18	/* How long for loss retry before timeout */
#define TCP_REPAIR		19	/* TCP sock is under repair right now */
#define TCP_REPAIR_QUEUE	20
#define TCP_QUEUE_SEQ		21
#define TCP_REPAIR_OPTIONS	22
#define TCP_FASTOPEN		23	/* Enable FastOpen on listeners */
#define TCP_TIMESTAMP		24
#define TCP_NOTSENT_LOWAT	25	/* limit number of unsent bytes in write queue */
#define TCP_CC_INFO		26	/* Get Congestion Control (optional) info */
#define TCP_SAVE_SYN		27	/* Record SYN headers for new connections */
#define TCP_SAVED_SYN		28	/* Get SYN headers recorded for connection */
#define TCP_REPAIR_WINDOW	29	/* Get/set window parameters */
#define TCP_FASTOPEN_CONNECT	30	/* Attempt FastOpen with connect */
#define TCP_ULP			31	/* Attach a ULP to a TCP connection */
#define TCP_MD5SIG_EXT		32	/* TCP MD5 Signature with extensions */
#define TCP_FASTOPEN_KEY	33	/* Set the key for Fast Open (cookie) */
#define TCP_FASTOPEN_NO_COOKIE	34	/* Enable TFO without a TFO cookie */
#define TCP_ZEROCOPY_RECEIVE	35
#define TCP_INQ			36	/* Notify bytes available to read as a cmsg on read */

#define TCP_CM_INQ		TCP_INQ

#define TCP_REPAIR_ON		1
#define TCP_REPAIR_OFF		0
#define TCP_REPAIR_OFF_NO_WP	-1	/* Turn off without window probes */

struct tcp_repair_opt {
	__u32	opt_code;
	__u32	opt_val;
};

struct tcp_repair_window {
	__u32	snd_wl1;
	__u32	snd_wnd;
	__u32	max_window;

	__u32	rcv_wnd;
	__u32	rcv_wup;
};

enum {
	TCP_NO_QUEUE,
	TCP_RECV_QUEUE,
	TCP_SEND_QUEUE,
	TCP_QUEUES_NR,
};

/* for TCP_INFO socket option */
#define TCPI_OPT_TIMESTAMPS	1
#define TCPI_OPT_SACK		2
#define TCPI_OPT_WSCALE		4
#define TCPI_OPT_ECN		8 /* ECN was negociated at TCP session init */
#define TCPI_OPT_ECN_SEEN	16 /* we received at least one packet with ECT */
#define TCPI_OPT_SYN_DATA	32 /* SYN-ACK acked data in SYN sent or rcvd */

enum tcp_ca_state {
	TCP_CA_Open = 0,
#define TCPF_CA_Open	(1<<TCP_CA_Open)
	TCP_CA_Disorder = 1,
#define TCPF_CA_Disorder (1<<TCP_CA_Disorder)
	TCP_CA_CWR = 2,
#define TCPF_CA_CWR	(1<<TCP_CA_CWR)
	TCP_CA_Recovery = 3,
#define TCPF_CA_Recovery (1<<TCP_CA_Recovery)
	TCP_CA_Loss = 4
#define TCPF_CA_Loss	(1<<TCP_CA_Loss)
};

struct tcp_info {
	__u8	tcpi_state;
	__u8	tcpi_ca_state;
	__u8	tcpi_retransmits;
	__u8	tcpi_probes;
	__u8	tcpi_backoff;
	__u8	tcpi_options;
	__u8	tcpi_snd_wscale : 4, tcpi_rcv_wscale : 4;
	__u8	tcpi_delivery_rate_app_limited:1;

	__u32	tcpi_rto;
	__u32	tcpi_ato;
	__u32	tcpi_snd_mss;
	__u32	tcpi_rcv_mss;

	__u32	tcpi_unacked;
	__u32	tcpi_sacked;
	__u32	tcpi_lost;
	__u32	tcpi_retrans;
	__u32	tcpi_fackets;

	/* Times. */
	__u32	tcpi_last_data_sent;
	__u32	tcpi_last_ack_sent;     /* Not remembered, sorry. */
	__u32	tcpi_last_data_recv;
	__u32	tcpi_last_ack_recv;

	/* Metrics. */
	__u32	tcpi_pmtu;
	__u32	tcpi_rcv_ssthresh;
	__u32	tcpi_rtt;
	__u32	tcpi_rttvar;
	__u32	tcpi_snd_ssthresh;
	__u32	tcpi_snd_cwnd;
	__u32	tcpi_advmss;
	__u32	tcpi_reordering;

	__u32	tcpi_rcv_rtt;
	__u32	tcpi_rcv_space;

	__u32	tcpi_total_retrans;

	__u64	tcpi_pacing_rate;
	__u64	tcpi_max_pacing_rate;
	__u64	tcpi_bytes_acked;    /* RFC4898 tcpEStatsAppHCThruOctetsAcked */
	__u64	tcpi_bytes_received; /* RFC4898 tcpEStatsAppHCThruOctetsReceived */
	__u32	tcpi_segs_out;	     /* RFC4898 tcpEStatsPerfSegsOut */
	__u32	tcpi_segs_in;	     /* RFC4898 tcpEStatsPerfSegsIn */

	__u32	tcpi_notsent_bytes;
	__u32	tcpi_min_rtt;
	__u32	tcpi_data_segs_in;	/* RFC4898 tcpEStatsDataSegsIn */
	__u32	tcpi_data_segs_out;	/* RFC4898 tcpEStatsDataSegsOut */

	__u64   tcpi_delivery_rate;

	__u64	tcpi_busy_time;      /* Time (usec) busy sending data */
	__u64	tcpi_rwnd_limited;   /* Time (usec) limited by receive window */
	__u64	tcpi_sndbuf_limited; /* Time (usec) limited by send buffer */

	__u32	tcpi_delivered;
	__u32	tcpi_delivered_ce;

	__u64	tcpi_bytes_sent;     /* RFC4898 tcpEStatsPerfHCDataOctetsOut */
	__u64	tcpi_bytes_retrans;  /* RFC4898 tcpEStatsPerfOctetsRetrans */
	__u32	tcpi_dsack_dups;     /* RFC4898 tcpEStatsStackDSACKDups */
	__u32	tcpi_reord_seen;     /* reordering events seen */
};

/* netlink attributes types for SCM_TIMESTAMPING_OPT_STATS */
enum {
	TCP_NLA_PAD,
	TCP_NLA_BUSY,		/* Time (usec) busy sending data */
	TCP_NLA_RWND_LIMITED,	/* Time (usec) limited by receive window */
	TCP_NLA_SNDBUF_LIMITED,	/* Time (usec) limited by send buffer */
	TCP_NLA_DATA_SEGS_OUT,	/* Data pkts sent including retransmission */
	TCP_NLA_TOTAL_RETRANS,	/* Data pkts retransmitted */
	TCP_NLA_PACING_RATE,    /* Pacing rate in bytes per second */
	TCP_NLA_DELIVERY_RATE,  /* Delivery rate in bytes per second */
	TCP_NLA_SND_CWND,       /* Sending congestion window */
	TCP_NLA_REORDERING,     /* Reordering metric */
	TCP_NLA_MIN_RTT,        /* minimum RTT */
	TCP_NLA_RECUR_RETRANS,  /* Recurring retransmits for the current pkt */
	TCP_NLA_DELIVERY_RATE_APP_LMT, /* delivery rate application limited ? */
	TCP_NLA_SNDQ_SIZE,	/* Data (bytes) pending in send queue */
	TCP_NLA_CA_STATE,	/* ca_state of socket */
	TCP_NLA_SND_SSTHRESH,	/* Slow start size threshold */
	TCP_NLA_DELIVERED,	/* Data pkts delivered incl. out-of-order */
	TCP_NLA_DELIVERED_CE,	/* Like above but only ones w/ CE marks */
	TCP_NLA_BYTES_SENT,	/* Data bytes sent including retransmission */
	TCP_NLA_BYTES_RETRANS,	/* Data bytes retransmitted */
	TCP_NLA_DSACK_DUPS,	/* DSACK blocks received */
	TCP_NLA_REORD_SEEN,	/* reordering events seen */
};

/* for TCP_MD5SIG socket option */
#define TCP_MD5SIG_MAXKEYLEN	80

/* tcp_md5sig extension flags for TCP_MD5SIG_EXT */
#define TCP_MD5SIG_FLAG_PREFIX		1	/* address prefix length */

struct tcp_md5sig {
	struct __kernel_sockaddr_storage tcpm_addr;	/* address associated */
	__u8	tcpm_flags;				/* extension flags */
	__u8	tcpm_prefixlen;				/* address prefix */
	__u16	tcpm_keylen;				/* key length */
	__u32	__tcpm_pad;				/* zero */
	__u8	tcpm_key[TCP_MD5SIG_MAXKEYLEN];		/* key (binary) */
};

/* INET_DIAG_MD5SIG */
struct tcp_diag_md5sig {
	__u8	tcpm_family;
	__u8	tcpm_prefixlen;
	__u16	tcpm_keylen;
	__be32	tcpm_addr[4];
	__u8	tcpm_key[TCP_MD5SIG_MAXKEYLEN];
};

/* setsockopt(fd, IPPROTO_TCP, TCP_ZEROCOPY_RECEIVE, ...) */

struct tcp_zerocopy_receive {
	__u64 address;		/* in: address of mapping */
	__u32 length;		/* in/out: number of bytes to map/mapped */
	__u32 recv_skip_hint;	/* out: amount of bytes to skip */
};
#endif /* _UAPI_LINUX_TCP_H */
