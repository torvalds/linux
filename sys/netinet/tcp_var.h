/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1993, 1994, 1995
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
 *	@(#)tcp_var.h	8.4 (Berkeley) 5/24/95
 * $FreeBSD$
 */

#ifndef _NETINET_TCP_VAR_H_
#define _NETINET_TCP_VAR_H_

#include <netinet/tcp.h>
#include <netinet/tcp_fsm.h>

#ifdef _KERNEL
#include <net/vnet.h>
#include <sys/mbuf.h>
#endif

#if defined(_KERNEL) || defined(_WANT_TCPCB)
/* TCP segment queue entry */
struct tseg_qent {
	TAILQ_ENTRY(tseg_qent) tqe_q;
	struct	mbuf   *tqe_m;		/* mbuf contains packet */
	struct  mbuf   *tqe_last;	/* last mbuf in chain */
	tcp_seq tqe_start;		/* TCP Sequence number start */
	int	tqe_len;		/* TCP segment data length */
	uint32_t tqe_flags;		/* The flags from the th->th_flags */
	uint32_t tqe_mbuf_cnt;		/* Count of mbuf overhead */
};
TAILQ_HEAD(tsegqe_head, tseg_qent);

struct sackblk {
	tcp_seq start;		/* start seq no. of sack block */
	tcp_seq end;		/* end seq no. */
};

struct sackhole {
	tcp_seq start;		/* start seq no. of hole */
	tcp_seq end;		/* end seq no. */
	tcp_seq rxmit;		/* next seq. no in hole to be retransmitted */
	TAILQ_ENTRY(sackhole) scblink;	/* scoreboard linkage */
};

struct sackhint {
	struct sackhole	*nexthole;
	int		sack_bytes_rexmit;
	tcp_seq		last_sack_ack;	/* Most recent/largest sacked ack */

	int		ispare;		/* explicit pad for 64bit alignment */
	int             sacked_bytes;	/*
					 * Total sacked bytes reported by the
					 * receiver via sack option
					 */
	uint32_t	_pad1[1];	/* TBD */
	uint64_t	_pad[1];	/* TBD */
};

#define SEGQ_EMPTY(tp) TAILQ_EMPTY(&(tp)->t_segq)

STAILQ_HEAD(tcp_log_stailq, tcp_log_mem);

/*
 * Tcp control block, one per tcp; fields:
 * Organized for 64 byte cacheline efficiency based
 * on common tcp_input/tcp_output processing.
 */
struct tcpcb {
	/* Cache line 1 */
	struct	inpcb *t_inpcb;		/* back pointer to internet pcb */
	struct tcp_function_block *t_fb;/* TCP function call block */
	void	*t_fb_ptr;		/* Pointer to t_fb specific data */
	uint32_t t_maxseg:24,		/* maximum segment size */
		t_logstate:8;		/* State of "black box" logging */
	uint32_t t_port:16,		/* Tunneling (over udp) port */
		t_state:4,		/* state of this connection */
		t_idle_reduce : 1,
		t_delayed_ack: 7,	/* Delayed ack variable */
		bits_spare : 4;
	u_int	t_flags;
	tcp_seq	snd_una;		/* sent but unacknowledged */
	tcp_seq	snd_max;		/* highest sequence number sent;
					 * used to recognize retransmits
					 */
	tcp_seq	snd_nxt;		/* send next */
	tcp_seq	snd_up;			/* send urgent pointer */
	uint32_t  snd_wnd;		/* send window */
	uint32_t  snd_cwnd;		/* congestion-controlled window */
	uint32_t t_peakrate_thr; 	/* pre-calculated peak rate threshold */
	/* Cache line 2 */
	u_int32_t  ts_offset;		/* our timestamp offset */
	u_int32_t	rfbuf_ts;	/* recv buffer autoscaling timestamp */
	int	rcv_numsacks;		/* # distinct sack blks present */
	u_int	t_tsomax;		/* TSO total burst length limit in bytes */
	u_int	t_tsomaxsegcount;	/* TSO maximum segment count */
	u_int	t_tsomaxsegsize;	/* TSO maximum segment size in bytes */
	tcp_seq	rcv_nxt;		/* receive next */
	tcp_seq	rcv_adv;		/* advertised window */
	uint32_t  rcv_wnd;		/* receive window */
	u_int	t_flags2;		/* More tcpcb flags storage */
	int	t_srtt;			/* smoothed round-trip time */
	int	t_rttvar;		/* variance in round-trip time */
	u_int32_t  ts_recent;		/* timestamp echo data */
	u_char	snd_scale;		/* window scaling for send window */
	u_char	rcv_scale;		/* window scaling for recv window */
	u_char	snd_limited;		/* segments limited transmitted */
	u_char	request_r_scale;	/* pending window scaling */
	tcp_seq	last_ack_sent;
	u_int	t_rcvtime;		/* inactivity time */
	/* Cache line 3 */
	tcp_seq	rcv_up;			/* receive urgent pointer */
	int	t_segqlen;		/* segment reassembly queue length */
	uint32_t t_segqmbuflen;		/* Count of bytes mbufs on all entries */
	struct	tsegqe_head t_segq;	/* segment reassembly queue */
	struct mbuf      *t_in_pkt;
	struct mbuf	 *t_tail_pkt;
	struct tcp_timer *t_timers;	/* All the TCP timers in one struct */
	struct	vnet *t_vnet;		/* back pointer to parent vnet */
	uint32_t  snd_ssthresh;		/* snd_cwnd size threshold for
					 * for slow start exponential to
					 * linear switch
					 */
	tcp_seq	snd_wl1;		/* window update seg seq number */
	/* Cache line 4 */
	tcp_seq	snd_wl2;		/* window update seg ack number */

	tcp_seq	irs;			/* initial receive sequence number */
	tcp_seq	iss;		        /* initial send sequence number */
	u_int   t_acktime;
	u_int	ts_recent_age;		/* when last updated */
	tcp_seq	snd_recover;		/* for use in NewReno Fast Recovery */
	uint16_t cl4_spare;		/* Spare to adjust CL 4 */
	char	t_oobflags;		/* have some */
	char	t_iobc;			/* input character */
	int	t_rxtcur;		/* current retransmit value (ticks) */

	int	t_rxtshift;		/* log(2) of rexmt exp. backoff */
	u_int	t_rtttime;		/* RTT measurement start time */

	tcp_seq	t_rtseq;		/* sequence number being timed */
	u_int	t_starttime;		/* time connection was established */

	u_int	t_pmtud_saved_maxseg;	/* pre-blackhole MSS */
	u_int	t_rttmin;		/* minimum rtt allowed */

	u_int	t_rttbest;		/* best rtt we've seen */

	int	t_softerror;		/* possible error not yet reported */
	uint32_t  max_sndwnd;		/* largest window peer has offered */
	/* Cache line 5 */
	uint32_t  snd_cwnd_prev;	/* cwnd prior to retransmit */
	uint32_t  snd_ssthresh_prev;	/* ssthresh prior to retransmit */
	tcp_seq	snd_recover_prev;	/* snd_recover prior to retransmit */
	int	t_sndzerowin;		/* zero-window updates sent */
	u_long	t_rttupdated;		/* number of times rtt sampled */
	int	snd_numholes;		/* number of holes seen by sender */
	u_int	t_badrxtwin;		/* window for retransmit recovery */
	TAILQ_HEAD(sackhole_head, sackhole) snd_holes;
					/* SACK scoreboard (sorted) */
	tcp_seq	snd_fack;		/* last seq number(+1) sack'd by rcv'r*/
	tcp_seq sack_newdata;		/* New data xmitted in this recovery
					   episode starts at this seq number */
	struct sackblk sackblks[MAX_SACK_BLKS]; /* seq nos. of sack blocks */
	struct sackhint	sackhint;	/* SACK scoreboard hint */
	int	t_rttlow;		/* smallest observerved RTT */
	int	rfbuf_cnt;		/* recv buffer autoscaling byte count */
	struct toedev	*tod;		/* toedev handling this connection */
	int	t_sndrexmitpack;	/* retransmit packets sent */
	int	t_rcvoopack;		/* out-of-order packets received */
	void	*t_toe;			/* TOE pcb pointer */
	struct cc_algo	*cc_algo;	/* congestion control algorithm */
	struct cc_var	*ccv;		/* congestion control specific vars */
	struct osd	*osd;		/* storage for Khelp module data */
	int	t_bytes_acked;		/* # bytes acked during current RTT */
	u_int   t_maxunacktime;
	u_int	t_keepinit;		/* time to establish connection */
	u_int	t_keepidle;		/* time before keepalive probes begin */
	u_int	t_keepintvl;		/* interval between keepalives */
	u_int	t_keepcnt;		/* number of keepalives before close */
	int	t_dupacks;		/* consecutive dup acks recd */
	int	t_lognum;		/* Number of log entries */
	struct tcp_log_stailq t_logs;	/* Log buffer */
	struct tcp_log_id_node *t_lin;
	struct tcp_log_id_bucket *t_lib;
	const char *t_output_caller;	/* Function that called tcp_output */
	uint32_t t_logsn;		/* Log "serial number" */
	uint8_t t_tfo_client_cookie_len; /* TCP Fast Open client cookie length */
	unsigned int *t_tfo_pending;	/* TCP Fast Open server pending counter */
	union {
		uint8_t client[TCP_FASTOPEN_MAX_COOKIE_LEN];
		uint64_t server;
	} t_tfo_cookie;			/* TCP Fast Open cookie to send */
#ifdef TCPPCAP
	struct mbufq t_inpkts;		/* List of saved input packets. */
	struct mbufq t_outpkts;		/* List of saved output packets. */
#endif
};
#endif	/* _KERNEL || _WANT_TCPCB */

#ifdef _KERNEL
struct tcptemp {
	u_char	tt_ipgen[40]; /* the size must be of max ip header, now IPv6 */
	struct	tcphdr tt_t;
};

/* 
 * TODO: We yet need to brave plowing in
 * to tcp_input() and the pru_usrreq() block.
 * Right now these go to the old standards which
 * are somewhat ok, but in the long term may
 * need to be changed. If we do tackle tcp_input()
 * then we need to get rid of the tcp_do_segment()
 * function below.
 */
/* Flags for tcp functions */
#define TCP_FUNC_BEING_REMOVED 0x01   	/* Can no longer be referenced */

/*
 * If defining the optional tcp_timers, in the
 * tfb_tcp_timer_stop call you must use the
 * callout_async_drain() function with the
 * tcp_timer_discard callback. You should check
 * the return of callout_async_drain() and if 0
 * increment tt_draincnt. Since the timer sub-system
 * does not know your callbacks you must provide a
 * stop_all function that loops through and calls
 * tcp_timer_stop() with each of your defined timers.
 * Adding a tfb_tcp_handoff_ok function allows the socket
 * option to change stacks to query you even if the
 * connection is in a later stage. You return 0 to
 * say you can take over and run your stack, you return
 * non-zero (an error number) to say no you can't.
 * If the function is undefined you can only change
 * in the early states (before connect or listen).
 * tfb_tcp_fb_fini is changed to add a flag to tell
 * the old stack if the tcb is being destroyed or
 * not. A one in the flag means the TCB is being
 * destroyed, a zero indicates its transitioning to
 * another stack (via socket option).
 */
struct tcp_function_block {
	char tfb_tcp_block_name[TCP_FUNCTION_NAME_LEN_MAX];
	int	(*tfb_tcp_output)(struct tcpcb *);
	int	(*tfb_tcp_output_wtime)(struct tcpcb *, const struct timeval *);
	void	(*tfb_tcp_do_segment)(struct mbuf *, struct tcphdr *,
			    struct socket *, struct tcpcb *,
		        int, int, uint8_t);
	void	(*tfb_tcp_hpts_do_segment)(struct mbuf *, struct tcphdr *,
			    struct socket *, struct tcpcb *,
			    int, int, uint8_t,
			    int, struct timeval *);
	int     (*tfb_tcp_ctloutput)(struct socket *so, struct sockopt *sopt,
			    struct inpcb *inp, struct tcpcb *tp);
	/* Optional memory allocation/free routine */
	int	(*tfb_tcp_fb_init)(struct tcpcb *);
	void	(*tfb_tcp_fb_fini)(struct tcpcb *, int);
	/* Optional timers, must define all if you define one */
	int	(*tfb_tcp_timer_stop_all)(struct tcpcb *);
	void	(*tfb_tcp_timer_activate)(struct tcpcb *,
			    uint32_t, u_int);
	int	(*tfb_tcp_timer_active)(struct tcpcb *, uint32_t);
	void	(*tfb_tcp_timer_stop)(struct tcpcb *, uint32_t);
	void	(*tfb_tcp_rexmit_tmr)(struct tcpcb *);
	int	(*tfb_tcp_handoff_ok)(struct tcpcb *);
	void	(*tfb_tcp_mtu_chg)(struct tcpcb *);
	volatile uint32_t tfb_refcnt;
	uint32_t  tfb_flags;
	uint8_t	tfb_id;
};

struct tcp_function {
	TAILQ_ENTRY(tcp_function)	tf_next;
	char				tf_name[TCP_FUNCTION_NAME_LEN_MAX];
	struct tcp_function_block	*tf_fb;
};

TAILQ_HEAD(tcp_funchead, tcp_function);
#endif	/* _KERNEL */

/*
 * Flags and utility macros for the t_flags field.
 */
#define	TF_ACKNOW	0x000001	/* ack peer immediately */
#define	TF_DELACK	0x000002	/* ack, but try to delay it */
#define	TF_NODELAY	0x000004	/* don't delay packets to coalesce */
#define	TF_NOOPT	0x000008	/* don't use tcp options */
#define	TF_SENTFIN	0x000010	/* have sent FIN */
#define	TF_REQ_SCALE	0x000020	/* have/will request window scaling */
#define	TF_RCVD_SCALE	0x000040	/* other side has requested scaling */
#define	TF_REQ_TSTMP	0x000080	/* have/will request timestamps */
#define	TF_RCVD_TSTMP	0x000100	/* a timestamp was received in SYN */
#define	TF_SACK_PERMIT	0x000200	/* other side said I could SACK */
#define	TF_NEEDSYN	0x000400	/* send SYN (implicit state) */
#define	TF_NEEDFIN	0x000800	/* send FIN (implicit state) */
#define	TF_NOPUSH	0x001000	/* don't push */
#define	TF_PREVVALID	0x002000	/* saved values for bad rxmit valid */
#define	TF_MORETOCOME	0x010000	/* More data to be appended to sock */
#define	TF_LQ_OVERFLOW	0x020000	/* listen queue overflow */
#define	TF_LASTIDLE	0x040000	/* connection was previously idle */
#define	TF_RXWIN0SENT	0x080000	/* sent a receiver win 0 in response */
#define	TF_FASTRECOVERY	0x100000	/* in NewReno Fast Recovery */
#define	TF_WASFRECOVERY	0x200000	/* was in NewReno Fast Recovery */
#define	TF_SIGNATURE	0x400000	/* require MD5 digests (RFC2385) */
#define	TF_FORCEDATA	0x800000	/* force out a byte */
#define	TF_TSO		0x1000000	/* TSO enabled on this connection */
#define	TF_TOE		0x2000000	/* this connection is offloaded */
#define	TF_ECN_PERMIT	0x4000000	/* connection ECN-ready */
#define	TF_ECN_SND_CWR	0x8000000	/* ECN CWR in queue */
#define	TF_ECN_SND_ECE	0x10000000	/* ECN ECE in queue */
#define	TF_CONGRECOVERY	0x20000000	/* congestion recovery mode */
#define	TF_WASCRECOVERY	0x40000000	/* was in congestion recovery */
#define	TF_FASTOPEN	0x80000000	/* TCP Fast Open indication */

#define	IN_FASTRECOVERY(t_flags)	(t_flags & TF_FASTRECOVERY)
#define	ENTER_FASTRECOVERY(t_flags)	t_flags |= TF_FASTRECOVERY
#define	EXIT_FASTRECOVERY(t_flags)	t_flags &= ~TF_FASTRECOVERY

#define	IN_CONGRECOVERY(t_flags)	(t_flags & TF_CONGRECOVERY)
#define	ENTER_CONGRECOVERY(t_flags)	t_flags |= TF_CONGRECOVERY
#define	EXIT_CONGRECOVERY(t_flags)	t_flags &= ~TF_CONGRECOVERY

#define	IN_RECOVERY(t_flags) (t_flags & (TF_CONGRECOVERY | TF_FASTRECOVERY))
#define	ENTER_RECOVERY(t_flags) t_flags |= (TF_CONGRECOVERY | TF_FASTRECOVERY)
#define	EXIT_RECOVERY(t_flags) t_flags &= ~(TF_CONGRECOVERY | TF_FASTRECOVERY)

#if defined(_KERNEL) && !defined(TCP_RFC7413)
#define	IS_FASTOPEN(t_flags)		(false)
#else
#define	IS_FASTOPEN(t_flags)		(t_flags & TF_FASTOPEN)
#endif

#define	BYTES_THIS_ACK(tp, th)	(th->th_ack - tp->snd_una)

/*
 * Flags for the t_oobflags field.
 */
#define	TCPOOB_HAVEDATA	0x01
#define	TCPOOB_HADDATA	0x02

/*
 * Flags for the extended TCP flags field, t_flags2
 */
#define	TF2_PLPMTU_BLACKHOLE	0x00000001 /* Possible PLPMTUD Black Hole. */
#define	TF2_PLPMTU_PMTUD	0x00000002 /* Allowed to attempt PLPMTUD. */
#define	TF2_PLPMTU_MAXSEGSNT	0x00000004 /* Last seg sent was full seg. */
#define	TF2_LOG_AUTO		0x00000008 /* Session is auto-logging. */
#define TF2_DROP_AF_DATA 	0x00000010 /* Drop after all data ack'd */

/*
 * Structure to hold TCP options that are only used during segment
 * processing (in tcp_input), but not held in the tcpcb.
 * It's basically used to reduce the number of parameters
 * to tcp_dooptions and tcp_addoptions.
 * The binary order of the to_flags is relevant for packing of the
 * options in tcp_addoptions.
 */
struct tcpopt {
	u_int32_t	to_flags;	/* which options are present */
#define	TOF_MSS		0x0001		/* maximum segment size */
#define	TOF_SCALE	0x0002		/* window scaling */
#define	TOF_SACKPERM	0x0004		/* SACK permitted */
#define	TOF_TS		0x0010		/* timestamp */
#define	TOF_SIGNATURE	0x0040		/* TCP-MD5 signature option (RFC2385) */
#define	TOF_SACK	0x0080		/* Peer sent SACK option */
#define	TOF_FASTOPEN	0x0100		/* TCP Fast Open (TFO) cookie */
#define	TOF_MAXOPT	0x0200
	u_int32_t	to_tsval;	/* new timestamp */
	u_int32_t	to_tsecr;	/* reflected timestamp */
	u_char		*to_sacks;	/* pointer to the first SACK blocks */
	u_char		*to_signature;	/* pointer to the TCP-MD5 signature */
	u_int8_t	*to_tfo_cookie; /* pointer to the TFO cookie */
	u_int16_t	to_mss;		/* maximum segment size */
	u_int8_t	to_wscale;	/* window scaling */
	u_int8_t	to_nsacks;	/* number of SACK blocks */
	u_int8_t	to_tfo_len;	/* TFO cookie length */
	u_int32_t	to_spare;	/* UTO */
};

/*
 * Flags for tcp_dooptions.
 */
#define	TO_SYN		0x01		/* parse SYN-only options */

struct hc_metrics_lite {	/* must stay in sync with hc_metrics */
	uint32_t	rmx_mtu;	/* MTU for this path */
	uint32_t	rmx_ssthresh;	/* outbound gateway buffer limit */
	uint32_t	rmx_rtt;	/* estimated round trip time */
	uint32_t	rmx_rttvar;	/* estimated rtt variance */
	uint32_t	rmx_cwnd;	/* congestion window */
	uint32_t	rmx_sendpipe;   /* outbound delay-bandwidth product */
	uint32_t	rmx_recvpipe;   /* inbound delay-bandwidth product */
};

/*
 * Used by tcp_maxmtu() to communicate interface specific features
 * and limits at the time of connection setup.
 */
struct tcp_ifcap {
	int	ifcap;
	u_int	tsomax;
	u_int	tsomaxsegcount;
	u_int	tsomaxsegsize;
};

#ifndef _NETINET_IN_PCB_H_
struct in_conninfo;
#endif /* _NETINET_IN_PCB_H_ */

struct tcptw {
	struct inpcb	*tw_inpcb;	/* XXX back pointer to internet pcb */
	tcp_seq		snd_nxt;
	tcp_seq		rcv_nxt;
	tcp_seq		iss;
	tcp_seq		irs;
	u_short		last_win;	/* cached window value */
	short		tw_so_options;	/* copy of so_options */
	struct ucred	*tw_cred;	/* user credentials */
	u_int32_t	t_recent;
	u_int32_t	ts_offset;	/* our timestamp offset */
	u_int		t_starttime;
	int		tw_time;
	TAILQ_ENTRY(tcptw) tw_2msl;
	void		*tw_pspare;	/* TCP_SIGNATURE */
	u_int		*tw_spare;	/* TCP_SIGNATURE */
};

#define	intotcpcb(ip)	((struct tcpcb *)(ip)->inp_ppcb)
#define	intotw(ip)	((struct tcptw *)(ip)->inp_ppcb)
#define	sototcpcb(so)	(intotcpcb(sotoinpcb(so)))

/*
 * The smoothed round-trip time and estimated variance
 * are stored as fixed point numbers scaled by the values below.
 * For convenience, these scales are also used in smoothing the average
 * (smoothed = (1/scale)sample + ((scale-1)/scale)smoothed).
 * With these scales, srtt has 3 bits to the right of the binary point,
 * and thus an "ALPHA" of 0.875.  rttvar has 2 bits to the right of the
 * binary point, and is smoothed with an ALPHA of 0.75.
 */
#define	TCP_RTT_SCALE		32	/* multiplier for srtt; 3 bits frac. */
#define	TCP_RTT_SHIFT		5	/* shift for srtt; 3 bits frac. */
#define	TCP_RTTVAR_SCALE	16	/* multiplier for rttvar; 2 bits */
#define	TCP_RTTVAR_SHIFT	4	/* shift for rttvar; 2 bits */
#define	TCP_DELTA_SHIFT		2	/* see tcp_input.c */

/*
 * The initial retransmission should happen at rtt + 4 * rttvar.
 * Because of the way we do the smoothing, srtt and rttvar
 * will each average +1/2 tick of bias.  When we compute
 * the retransmit timer, we want 1/2 tick of rounding and
 * 1 extra tick because of +-1/2 tick uncertainty in the
 * firing of the timer.  The bias will give us exactly the
 * 1.5 tick we need.  But, because the bias is
 * statistical, we have to test that we don't drop below
 * the minimum feasible timer (which is 2 ticks).
 * This version of the macro adapted from a paper by Lawrence
 * Brakmo and Larry Peterson which outlines a problem caused
 * by insufficient precision in the original implementation,
 * which results in inappropriately large RTO values for very
 * fast networks.
 */
#define	TCP_REXMTVAL(tp) \
	max((tp)->t_rttmin, (((tp)->t_srtt >> (TCP_RTT_SHIFT - TCP_DELTA_SHIFT))  \
	  + (tp)->t_rttvar) >> TCP_DELTA_SHIFT)

/*
 * TCP statistics.
 * Many of these should be kept per connection,
 * but that's inconvenient at the moment.
 */
struct	tcpstat {
	uint64_t tcps_connattempt;	/* connections initiated */
	uint64_t tcps_accepts;		/* connections accepted */
	uint64_t tcps_connects;		/* connections established */
	uint64_t tcps_drops;		/* connections dropped */
	uint64_t tcps_conndrops;	/* embryonic connections dropped */
	uint64_t tcps_minmssdrops;	/* average minmss too low drops */
	uint64_t tcps_closed;		/* conn. closed (includes drops) */
	uint64_t tcps_segstimed;	/* segs where we tried to get rtt */
	uint64_t tcps_rttupdated;	/* times we succeeded */
	uint64_t tcps_delack;		/* delayed acks sent */
	uint64_t tcps_timeoutdrop;	/* conn. dropped in rxmt timeout */
	uint64_t tcps_rexmttimeo;	/* retransmit timeouts */
	uint64_t tcps_persisttimeo;	/* persist timeouts */
	uint64_t tcps_keeptimeo;	/* keepalive timeouts */
	uint64_t tcps_keepprobe;	/* keepalive probes sent */
	uint64_t tcps_keepdrops;	/* connections dropped in keepalive */

	uint64_t tcps_sndtotal;		/* total packets sent */
	uint64_t tcps_sndpack;		/* data packets sent */
	uint64_t tcps_sndbyte;		/* data bytes sent */
	uint64_t tcps_sndrexmitpack;	/* data packets retransmitted */
	uint64_t tcps_sndrexmitbyte;	/* data bytes retransmitted */
	uint64_t tcps_sndrexmitbad;	/* unnecessary packet retransmissions */
	uint64_t tcps_sndacks;		/* ack-only packets sent */
	uint64_t tcps_sndprobe;		/* window probes sent */
	uint64_t tcps_sndurg;		/* packets sent with URG only */
	uint64_t tcps_sndwinup;		/* window update-only packets sent */
	uint64_t tcps_sndctrl;		/* control (SYN|FIN|RST) packets sent */

	uint64_t tcps_rcvtotal;		/* total packets received */
	uint64_t tcps_rcvpack;		/* packets received in sequence */
	uint64_t tcps_rcvbyte;		/* bytes received in sequence */
	uint64_t tcps_rcvbadsum;	/* packets received with ccksum errs */
	uint64_t tcps_rcvbadoff;	/* packets received with bad offset */
	uint64_t tcps_rcvreassfull;	/* packets dropped for no reass space */
	uint64_t tcps_rcvshort;		/* packets received too short */
	uint64_t tcps_rcvduppack;	/* duplicate-only packets received */
	uint64_t tcps_rcvdupbyte;	/* duplicate-only bytes received */
	uint64_t tcps_rcvpartduppack;	/* packets with some duplicate data */
	uint64_t tcps_rcvpartdupbyte;	/* dup. bytes in part-dup. packets */
	uint64_t tcps_rcvoopack;	/* out-of-order packets received */
	uint64_t tcps_rcvoobyte;	/* out-of-order bytes received */
	uint64_t tcps_rcvpackafterwin;	/* packets with data after window */
	uint64_t tcps_rcvbyteafterwin;	/* bytes rcvd after window */
	uint64_t tcps_rcvafterclose;	/* packets rcvd after "close" */
	uint64_t tcps_rcvwinprobe;	/* rcvd window probe packets */
	uint64_t tcps_rcvdupack;	/* rcvd duplicate acks */
	uint64_t tcps_rcvacktoomuch;	/* rcvd acks for unsent data */
	uint64_t tcps_rcvackpack;	/* rcvd ack packets */
	uint64_t tcps_rcvackbyte;	/* bytes acked by rcvd acks */
	uint64_t tcps_rcvwinupd;	/* rcvd window update packets */
	uint64_t tcps_pawsdrop;		/* segments dropped due to PAWS */
	uint64_t tcps_predack;		/* times hdr predict ok for acks */
	uint64_t tcps_preddat;		/* times hdr predict ok for data pkts */
	uint64_t tcps_pcbcachemiss;
	uint64_t tcps_cachedrtt;	/* times cached RTT in route updated */
	uint64_t tcps_cachedrttvar;	/* times cached rttvar updated */
	uint64_t tcps_cachedssthresh;	/* times cached ssthresh updated */
	uint64_t tcps_usedrtt;		/* times RTT initialized from route */
	uint64_t tcps_usedrttvar;	/* times RTTVAR initialized from rt */
	uint64_t tcps_usedssthresh;	/* times ssthresh initialized from rt*/
	uint64_t tcps_persistdrop;	/* timeout in persist state */
	uint64_t tcps_badsyn;		/* bogus SYN, e.g. premature ACK */
	uint64_t tcps_mturesent;	/* resends due to MTU discovery */
	uint64_t tcps_listendrop;	/* listen queue overflows */
	uint64_t tcps_badrst;		/* ignored RSTs in the window */

	uint64_t tcps_sc_added;		/* entry added to syncache */
	uint64_t tcps_sc_retransmitted;	/* syncache entry was retransmitted */
	uint64_t tcps_sc_dupsyn;	/* duplicate SYN packet */
	uint64_t tcps_sc_dropped;	/* could not reply to packet */
	uint64_t tcps_sc_completed;	/* successful extraction of entry */
	uint64_t tcps_sc_bucketoverflow;/* syncache per-bucket limit hit */
	uint64_t tcps_sc_cacheoverflow;	/* syncache cache limit hit */
	uint64_t tcps_sc_reset;		/* RST removed entry from syncache */
	uint64_t tcps_sc_stale;		/* timed out or listen socket gone */
	uint64_t tcps_sc_aborted;	/* syncache entry aborted */
	uint64_t tcps_sc_badack;	/* removed due to bad ACK */
	uint64_t tcps_sc_unreach;	/* ICMP unreachable received */
	uint64_t tcps_sc_zonefail;	/* zalloc() failed */
	uint64_t tcps_sc_sendcookie;	/* SYN cookie sent */
	uint64_t tcps_sc_recvcookie;	/* SYN cookie received */

	uint64_t tcps_hc_added;		/* entry added to hostcache */
	uint64_t tcps_hc_bucketoverflow;/* hostcache per bucket limit hit */

	uint64_t tcps_finwait2_drops;    /* Drop FIN_WAIT_2 connection after time limit */

	/* SACK related stats */
	uint64_t tcps_sack_recovery_episode; /* SACK recovery episodes */
	uint64_t tcps_sack_rexmits;	    /* SACK rexmit segments   */
	uint64_t tcps_sack_rexmit_bytes;    /* SACK rexmit bytes      */
	uint64_t tcps_sack_rcv_blocks;	    /* SACK blocks (options) received */
	uint64_t tcps_sack_send_blocks;	    /* SACK blocks (options) sent     */
	uint64_t tcps_sack_sboverflow;	    /* times scoreboard overflowed */
	
	/* ECN related stats */
	uint64_t tcps_ecn_ce;		/* ECN Congestion Experienced */
	uint64_t tcps_ecn_ect0;		/* ECN Capable Transport */
	uint64_t tcps_ecn_ect1;		/* ECN Capable Transport */
	uint64_t tcps_ecn_shs;		/* ECN successful handshakes */
	uint64_t tcps_ecn_rcwnd;	/* # times ECN reduced the cwnd */

	/* TCP_SIGNATURE related stats */
	uint64_t tcps_sig_rcvgoodsig;	/* Total matching signature received */
	uint64_t tcps_sig_rcvbadsig;	/* Total bad signature received */
	uint64_t tcps_sig_err_buildsig;	/* Failed to make signature */
	uint64_t tcps_sig_err_sigopt;	/* No signature expected by socket */
	uint64_t tcps_sig_err_nosigopt;	/* No signature provided by segment */

	/* Path MTU Discovery Black Hole Detection related stats */
	uint64_t tcps_pmtud_blackhole_activated;	 /* Black Hole Count */
	uint64_t tcps_pmtud_blackhole_activated_min_mss; /* BH at min MSS Count */
	uint64_t tcps_pmtud_blackhole_failed;		 /* Black Hole Failure Count */

	uint64_t _pad[12];		/* 6 UTO, 6 TBD */
};

#define	tcps_rcvmemdrop	tcps_rcvreassfull	/* compat */

#ifdef _KERNEL
#define	TI_UNLOCKED	1
#define	TI_RLOCKED	2
#include <sys/counter.h>

VNET_PCPUSTAT_DECLARE(struct tcpstat, tcpstat);	/* tcp statistics */
/*
 * In-kernel consumers can use these accessor macros directly to update
 * stats.
 */
#define	TCPSTAT_ADD(name, val)	\
    VNET_PCPUSTAT_ADD(struct tcpstat, tcpstat, name, (val))
#define	TCPSTAT_INC(name)	TCPSTAT_ADD(name, 1)

/*
 * Kernel module consumers must use this accessor macro.
 */
void	kmod_tcpstat_inc(int statnum);
#define	KMOD_TCPSTAT_INC(name)						\
    kmod_tcpstat_inc(offsetof(struct tcpstat, name) / sizeof(uint64_t))

/*
 * Running TCP connection count by state.
 */
VNET_DECLARE(counter_u64_t, tcps_states[TCP_NSTATES]);
#define	V_tcps_states	VNET(tcps_states)
#define	TCPSTATES_INC(state)	counter_u64_add(V_tcps_states[state], 1)
#define	TCPSTATES_DEC(state)	counter_u64_add(V_tcps_states[state], -1)

/*
 * TCP specific helper hook point identifiers.
 */
#define	HHOOK_TCP_EST_IN		0
#define	HHOOK_TCP_EST_OUT		1
#define	HHOOK_TCP_LAST			HHOOK_TCP_EST_OUT

struct tcp_hhook_data {
	struct tcpcb	*tp;
	struct tcphdr	*th;
	struct tcpopt	*to;
	uint32_t	len;
	int		tso;
	tcp_seq		curack;
};
#ifdef TCP_HHOOK
void hhook_run_tcp_est_out(struct tcpcb *tp,
	struct tcphdr *th, struct tcpopt *to,
	uint32_t len, int tso);
#endif
#endif

/*
 * TCB structure exported to user-land via sysctl(3).
 *
 * Fields prefixed with "xt_" are unique to the export structure, and fields
 * with "t_" or other prefixes match corresponding fields of 'struct tcpcb'.
 *
 * Legend:
 * (s) - used by userland utilities in src
 * (p) - used by utilities in ports
 * (3) - is known to be used by third party software not in ports
 * (n) - no known usage
 *
 * Evil hack: declare only if in_pcb.h and sys/socketvar.h have been
 * included.  Not all of our clients do.
 */
#if defined(_NETINET_IN_PCB_H_) && defined(_SYS_SOCKETVAR_H_)
struct xtcpcb {
	ksize_t	xt_len;		/* length of this structure */
	struct xinpcb	xt_inp;
	char		xt_stack[TCP_FUNCTION_NAME_LEN_MAX];	/* (s) */
	char		xt_logid[TCP_LOG_ID_LEN];	/* (s) */
	int64_t		spare64[8];
	int32_t		t_state;		/* (s,p) */
	uint32_t	t_flags;		/* (s,p) */
	int32_t		t_sndzerowin;		/* (s) */
	int32_t		t_sndrexmitpack;	/* (s) */
	int32_t		t_rcvoopack;		/* (s) */
	int32_t		t_rcvtime;		/* (s) */
	int32_t		tt_rexmt;		/* (s) */
	int32_t		tt_persist;		/* (s) */
	int32_t		tt_keep;		/* (s) */
	int32_t		tt_2msl;		/* (s) */
	int32_t		tt_delack;		/* (s) */
	int32_t		t_logstate;		/* (3) */
	int32_t		spare32[32];
} __aligned(8);

#ifdef _KERNEL
void	tcp_inptoxtp(const struct inpcb *, struct xtcpcb *);
#endif
#endif

/*
 * TCP function information (name-to-id mapping, aliases, and refcnt)
 * exported to user-land via sysctl(3).
 */
struct tcp_function_info {
	uint32_t	tfi_refcnt;
	uint8_t		tfi_id;
	char		tfi_name[TCP_FUNCTION_NAME_LEN_MAX];
	char		tfi_alias[TCP_FUNCTION_NAME_LEN_MAX];
};

/*
 * Identifiers for TCP sysctl nodes
 */
#define	TCPCTL_DO_RFC1323	1	/* use RFC-1323 extensions */
#define	TCPCTL_MSSDFLT		3	/* MSS default */
#define TCPCTL_STATS		4	/* statistics */
#define	TCPCTL_RTTDFLT		5	/* default RTT estimate */
#define	TCPCTL_KEEPIDLE		6	/* keepalive idle timer */
#define	TCPCTL_KEEPINTVL	7	/* interval to send keepalives */
#define	TCPCTL_SENDSPACE	8	/* send buffer space */
#define	TCPCTL_RECVSPACE	9	/* receive buffer space */
#define	TCPCTL_KEEPINIT		10	/* timeout for establishing syn */
#define	TCPCTL_PCBLIST		11	/* list of all outstanding PCBs */
#define	TCPCTL_DELACKTIME	12	/* time before sending delayed ACK */
#define	TCPCTL_V6MSSDFLT	13	/* MSS default for IPv6 */
#define	TCPCTL_SACK		14	/* Selective Acknowledgement,rfc 2018 */
#define	TCPCTL_DROP		15	/* drop tcp connection */
#define	TCPCTL_STATES		16	/* connection counts by TCP state */

#ifdef _KERNEL
#ifdef SYSCTL_DECL
SYSCTL_DECL(_net_inet_tcp);
SYSCTL_DECL(_net_inet_tcp_sack);
MALLOC_DECLARE(M_TCPLOG);
#endif

extern	int tcp_log_in_vain;

/*
 * Global TCP tunables shared between different stacks.
 * Please keep the list sorted.
 */
VNET_DECLARE(int, drop_synfin);
VNET_DECLARE(int, path_mtu_discovery);
VNET_DECLARE(int, tcp_abc_l_var);
VNET_DECLARE(int, tcp_autorcvbuf_inc);
VNET_DECLARE(int, tcp_autorcvbuf_max);
VNET_DECLARE(int, tcp_autosndbuf_inc);
VNET_DECLARE(int, tcp_autosndbuf_max);
VNET_DECLARE(int, tcp_delack_enabled);
VNET_DECLARE(int, tcp_do_autorcvbuf);
VNET_DECLARE(int, tcp_do_autosndbuf);
VNET_DECLARE(int, tcp_do_ecn);
VNET_DECLARE(int, tcp_do_rfc1323);
VNET_DECLARE(int, tcp_do_rfc3042);
VNET_DECLARE(int, tcp_do_rfc3390);
VNET_DECLARE(int, tcp_do_rfc3465);
VNET_DECLARE(int, tcp_do_rfc6675_pipe);
VNET_DECLARE(int, tcp_do_sack);
VNET_DECLARE(int, tcp_do_tso);
VNET_DECLARE(int, tcp_ecn_maxretries);
VNET_DECLARE(int, tcp_initcwnd_segments);
VNET_DECLARE(int, tcp_insecure_rst);
VNET_DECLARE(int, tcp_insecure_syn);
VNET_DECLARE(int, tcp_minmss);
VNET_DECLARE(int, tcp_mssdflt);
VNET_DECLARE(int, tcp_recvspace);
VNET_DECLARE(int, tcp_sack_globalholes);
VNET_DECLARE(int, tcp_sack_globalmaxholes);
VNET_DECLARE(int, tcp_sack_maxholes);
VNET_DECLARE(int, tcp_sc_rst_sock_fail);
VNET_DECLARE(int, tcp_sendspace);
VNET_DECLARE(struct inpcbhead, tcb);
VNET_DECLARE(struct inpcbinfo, tcbinfo);

#define	V_drop_synfin			VNET(drop_synfin)
#define	V_path_mtu_discovery		VNET(path_mtu_discovery)
#define	V_tcb				VNET(tcb)
#define	V_tcbinfo			VNET(tcbinfo)
#define	V_tcp_abc_l_var			VNET(tcp_abc_l_var)
#define	V_tcp_autorcvbuf_inc		VNET(tcp_autorcvbuf_inc)
#define	V_tcp_autorcvbuf_max		VNET(tcp_autorcvbuf_max)
#define	V_tcp_autosndbuf_inc		VNET(tcp_autosndbuf_inc)
#define	V_tcp_autosndbuf_max		VNET(tcp_autosndbuf_max)
#define	V_tcp_delack_enabled		VNET(tcp_delack_enabled)
#define	V_tcp_do_autorcvbuf		VNET(tcp_do_autorcvbuf)
#define	V_tcp_do_autosndbuf		VNET(tcp_do_autosndbuf)
#define	V_tcp_do_ecn			VNET(tcp_do_ecn)
#define	V_tcp_do_rfc1323		VNET(tcp_do_rfc1323)
#define	V_tcp_do_rfc3042		VNET(tcp_do_rfc3042)
#define	V_tcp_do_rfc3390		VNET(tcp_do_rfc3390)
#define	V_tcp_do_rfc3465		VNET(tcp_do_rfc3465)
#define	V_tcp_do_rfc6675_pipe		VNET(tcp_do_rfc6675_pipe)
#define	V_tcp_do_sack			VNET(tcp_do_sack)
#define	V_tcp_do_tso			VNET(tcp_do_tso)
#define	V_tcp_ecn_maxretries		VNET(tcp_ecn_maxretries)
#define	V_tcp_initcwnd_segments		VNET(tcp_initcwnd_segments)
#define	V_tcp_insecure_rst		VNET(tcp_insecure_rst)
#define	V_tcp_insecure_syn		VNET(tcp_insecure_syn)
#define	V_tcp_minmss			VNET(tcp_minmss)
#define	V_tcp_mssdflt			VNET(tcp_mssdflt)
#define	V_tcp_recvspace			VNET(tcp_recvspace)
#define	V_tcp_sack_globalholes		VNET(tcp_sack_globalholes)
#define	V_tcp_sack_globalmaxholes	VNET(tcp_sack_globalmaxholes)
#define	V_tcp_sack_maxholes		VNET(tcp_sack_maxholes)
#define	V_tcp_sc_rst_sock_fail		VNET(tcp_sc_rst_sock_fail)
#define	V_tcp_sendspace			VNET(tcp_sendspace)
#define	V_tcp_udp_tunneling_overhead	VNET(tcp_udp_tunneling_overhead)
#define	V_tcp_udp_tunneling_port	VNET(tcp_udp_tunneling_port)


#ifdef TCP_HHOOK
VNET_DECLARE(struct hhook_head *, tcp_hhh[HHOOK_TCP_LAST + 1]);
#define	V_tcp_hhh		VNET(tcp_hhh)
#endif

int	 tcp_addoptions(struct tcpopt *, u_char *);
int	 tcp_ccalgounload(struct cc_algo *unload_algo);
struct tcpcb *
	 tcp_close(struct tcpcb *);
void	 tcp_discardcb(struct tcpcb *);
void	 tcp_twstart(struct tcpcb *);
void	 tcp_twclose(struct tcptw *, int);
void	 tcp_ctlinput(int, struct sockaddr *, void *);
int	 tcp_ctloutput(struct socket *, struct sockopt *);
struct tcpcb *
	 tcp_drop(struct tcpcb *, int);
void	 tcp_drain(void);
void	 tcp_init(void);
void	 tcp_fini(void *);
char	*tcp_log_addrs(struct in_conninfo *, struct tcphdr *, void *,
	    const void *);
char	*tcp_log_vain(struct in_conninfo *, struct tcphdr *, void *,
	    const void *);
int	 tcp_reass(struct tcpcb *, struct tcphdr *, tcp_seq *, int *, struct mbuf *);
void	 tcp_reass_global_init(void);
void	 tcp_reass_flush(struct tcpcb *);
void	 tcp_dooptions(struct tcpopt *, u_char *, int, int);
void	tcp_dropwithreset(struct mbuf *, struct tcphdr *,
		     struct tcpcb *, int, int);
void	tcp_pulloutofband(struct socket *,
		     struct tcphdr *, struct mbuf *, int);
void	tcp_xmit_timer(struct tcpcb *, int);
void	tcp_newreno_partial_ack(struct tcpcb *, struct tcphdr *);
void	cc_ack_received(struct tcpcb *tp, struct tcphdr *th,
			    uint16_t nsegs, uint16_t type);
void 	cc_conn_init(struct tcpcb *tp);
void 	cc_post_recovery(struct tcpcb *tp, struct tcphdr *th);
void	cc_cong_signal(struct tcpcb *tp, struct tcphdr *th, uint32_t type);
#ifdef TCP_HHOOK
void	hhook_run_tcp_est_in(struct tcpcb *tp,
			    struct tcphdr *th, struct tcpopt *to);
#endif

int	 tcp_input(struct mbuf **, int *, int);
int	 tcp_autorcvbuf(struct mbuf *, struct tcphdr *, struct socket *,
	    struct tcpcb *, int);
void	 tcp_do_segment(struct mbuf *, struct tcphdr *,
			struct socket *, struct tcpcb *, int, int, uint8_t);

int register_tcp_functions(struct tcp_function_block *blk, int wait);
int register_tcp_functions_as_names(struct tcp_function_block *blk,
    int wait, const char *names[], int *num_names);
int register_tcp_functions_as_name(struct tcp_function_block *blk,
    const char *name, int wait);
int deregister_tcp_functions(struct tcp_function_block *blk, bool quiesce,
    bool force);
struct tcp_function_block *find_and_ref_tcp_functions(struct tcp_function_set *fs);
void tcp_switch_back_to_default(struct tcpcb *tp);
struct tcp_function_block *
find_and_ref_tcp_fb(struct tcp_function_block *fs);
int tcp_default_ctloutput(struct socket *so, struct sockopt *sopt, struct inpcb *inp, struct tcpcb *tp);

uint32_t tcp_maxmtu(struct in_conninfo *, struct tcp_ifcap *);
uint32_t tcp_maxmtu6(struct in_conninfo *, struct tcp_ifcap *);
u_int	 tcp_maxseg(const struct tcpcb *);
void	 tcp_mss_update(struct tcpcb *, int, int, struct hc_metrics_lite *,
	    struct tcp_ifcap *);
void	 tcp_mss(struct tcpcb *, int);
int	 tcp_mssopt(struct in_conninfo *);
struct inpcb *
	 tcp_drop_syn_sent(struct inpcb *, int);
struct tcpcb *
	 tcp_newtcpcb(struct inpcb *);
int	 tcp_output(struct tcpcb *);
void	 tcp_state_change(struct tcpcb *, int);
void	 tcp_respond(struct tcpcb *, void *,
	    struct tcphdr *, struct mbuf *, tcp_seq, tcp_seq, int);
void	 tcp_tw_init(void);
#ifdef VIMAGE
void	 tcp_tw_destroy(void);
#endif
void	 tcp_tw_zone_change(void);
int	 tcp_twcheck(struct inpcb *, struct tcpopt *, struct tcphdr *,
	    struct mbuf *, int);
void	 tcp_setpersist(struct tcpcb *);
void	 tcp_slowtimo(void);
struct tcptemp *
	 tcpip_maketemplate(struct inpcb *);
void	 tcpip_fillheaders(struct inpcb *, void *, void *);
void	 tcp_timer_activate(struct tcpcb *, uint32_t, u_int);
int	 tcp_timer_suspend(struct tcpcb *, uint32_t);
void	 tcp_timers_unsuspend(struct tcpcb *, uint32_t);
int	 tcp_timer_active(struct tcpcb *, uint32_t);
void	 tcp_timer_stop(struct tcpcb *, uint32_t);
void	 tcp_trace(short, short, struct tcpcb *, void *, struct tcphdr *, int);
int	 inp_to_cpuid(struct inpcb *inp);
/*
 * All tcp_hc_* functions are IPv4 and IPv6 (via in_conninfo)
 */
void	 tcp_hc_init(void);
#ifdef VIMAGE
void	 tcp_hc_destroy(void);
#endif
void	 tcp_hc_get(struct in_conninfo *, struct hc_metrics_lite *);
uint32_t tcp_hc_getmtu(struct in_conninfo *);
void	 tcp_hc_updatemtu(struct in_conninfo *, uint32_t);
void	 tcp_hc_update(struct in_conninfo *, struct hc_metrics_lite *);

extern	struct pr_usrreqs tcp_usrreqs;

uint32_t tcp_new_ts_offset(struct in_conninfo *);
tcp_seq	 tcp_new_isn(struct in_conninfo *);

int	 tcp_sack_doack(struct tcpcb *, struct tcpopt *, tcp_seq);
void	 tcp_update_sack_list(struct tcpcb *tp, tcp_seq rcv_laststart, tcp_seq rcv_lastend);
void	 tcp_clean_sackreport(struct tcpcb *tp);
void	 tcp_sack_adjust(struct tcpcb *tp);
struct sackhole *tcp_sack_output(struct tcpcb *tp, int *sack_bytes_rexmt);
void	 tcp_sack_partialack(struct tcpcb *, struct tcphdr *);
void	 tcp_free_sackholes(struct tcpcb *tp);
int	 tcp_newreno(struct tcpcb *, struct tcphdr *);
int	 tcp_compute_pipe(struct tcpcb *);
uint32_t tcp_compute_initwnd(uint32_t);
void	 tcp_sndbuf_autoscale(struct tcpcb *, struct socket *, uint32_t);
struct mbuf *
	 tcp_m_copym(struct mbuf *m, int32_t off0, int32_t *plen,
	   int32_t seglimit, int32_t segsize, struct sockbuf *sb);


static inline void
tcp_fields_to_host(struct tcphdr *th)
{

	th->th_seq = ntohl(th->th_seq);
	th->th_ack = ntohl(th->th_ack);
	th->th_win = ntohs(th->th_win);
	th->th_urp = ntohs(th->th_urp);
}

static inline void
tcp_fields_to_net(struct tcphdr *th)
{

	th->th_seq = htonl(th->th_seq);
	th->th_ack = htonl(th->th_ack);
	th->th_win = htons(th->th_win);
	th->th_urp = htons(th->th_urp);
}
#endif /* _KERNEL */

#endif /* _NETINET_TCP_VAR_H_ */
