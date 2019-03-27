/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016-2018 Netflix, Inc.
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

#ifndef __tcp_log_buf_h__
#define __tcp_log_buf_h__

#define	TCP_LOG_REASON_LEN	32
#define	TCP_LOG_BUF_VER		(6)

/*
 * Because the (struct tcp_log_buffer) includes 8-byte uint64_t's, it requires
 * 8-byte alignment to work properly on all platforms. Therefore, we will
 * enforce 8-byte alignment for all the structures that may appear by
 * themselves (instead of being embedded in another structure) in a data
 * stream.
 */
#define	ALIGN_TCP_LOG		__aligned(8)

/* Information about the socketbuffer state. */
struct tcp_log_sockbuf
{
	uint32_t	tls_sb_acc;	/* available chars (sb->sb_acc) */
	uint32_t	tls_sb_ccc;	/* claimed chars (sb->sb_ccc) */
	uint32_t	tls_sb_spare;	/* spare */
};

/* Optional, verbose information that may be appended to an event log. */
struct tcp_log_verbose
{
#define	TCP_FUNC_LEN	32
	char		tlv_snd_frm[TCP_FUNC_LEN]; /* tcp_output() caller */
	char		tlv_trace_func[TCP_FUNC_LEN]; /* Function that
							 generated trace */
	uint32_t	tlv_trace_line;	/* Line number that generated trace */
	uint8_t		_pad[4];
} ALIGN_TCP_LOG;

/* Internal RACK state variables. */
struct tcp_log_rack
{
	uint32_t	tlr_rack_rtt;		/* rc_rack_rtt */
	uint8_t		tlr_state;		/* Internal RACK state */
	uint8_t		_pad[3];		/* Padding */
};

struct tcp_log_bbr {
	uint64_t cur_del_rate;
	uint64_t delRate;
	uint64_t rttProp;
	uint64_t bw_inuse;
	uint32_t inflight;
	uint32_t applimited;
	uint32_t delivered;
	uint32_t timeStamp;
	uint32_t epoch;
	uint32_t lt_epoch;
	uint32_t pkts_out;
	uint32_t flex1;
	uint32_t flex2;
	uint32_t flex3;
	uint32_t flex4;
	uint32_t flex5;
	uint32_t flex6;
	uint32_t lost;
	uint16_t pacing_gain;
	uint16_t cwnd_gain;
	uint16_t flex7;
	uint8_t bbr_state;
	uint8_t bbr_substate;
	uint8_t inhpts;
	uint8_t ininput;
	uint8_t use_lt_bw;
	uint8_t flex8;
	uint32_t pkt_epoch;
};

/* Per-stack stack-specific info. */
union tcp_log_stackspecific
{
	struct tcp_log_rack u_rack;
	struct tcp_log_bbr u_bbr;
};

struct tcp_log_buffer
{
	/* Event basics */
	struct timeval	tlb_tv;		/* Timestamp of trace */
	uint32_t	tlb_ticks;	/* Timestamp of trace */
	uint32_t	tlb_sn;		/* Serial number */
	uint8_t		tlb_stackid;	/* Stack ID */
	uint8_t		tlb_eventid;	/* Event ID */
	uint16_t	tlb_eventflags;	/* Flags for the record */
#define	TLB_FLAG_RXBUF		0x0001	/* Includes receive buffer info */
#define	TLB_FLAG_TXBUF		0x0002	/* Includes send buffer info */
#define	TLB_FLAG_HDR		0x0004	/* Includes a TCP header */
#define	TLB_FLAG_VERBOSE	0x0008	/* Includes function/line numbers */
#define	TLB_FLAG_STACKINFO	0x0010	/* Includes stack-specific info */
	int		tlb_errno;	/* Event error (if any) */

	/* Internal session state */
	struct tcp_log_sockbuf tlb_rxbuf; /* Receive buffer */
	struct tcp_log_sockbuf tlb_txbuf; /* Send buffer */

	int		tlb_state;	/* TCPCB t_state */
	uint32_t	tlb_starttime;	/* TCPCB t_starttime */
	uint32_t	tlb_iss;		/* TCPCB iss */
	uint32_t	tlb_flags;	/* TCPCB flags */
	uint32_t	tlb_snd_una;	/* TCPCB snd_una */
	uint32_t	tlb_snd_max;	/* TCPCB snd_max */
	uint32_t	tlb_snd_cwnd;	/* TCPCB snd_cwnd */
	uint32_t	tlb_snd_nxt;	/* TCPCB snd_nxt */
	uint32_t	tlb_snd_recover;/* TCPCB snd_recover */
	uint32_t	tlb_snd_wnd;	/* TCPCB snd_wnd */
	uint32_t	tlb_snd_ssthresh; /* TCPCB snd_ssthresh */
	uint32_t	tlb_srtt;	/* TCPCB t_srtt */
	uint32_t	tlb_rttvar;	/* TCPCB t_rttvar */
	uint32_t	tlb_rcv_up;	/* TCPCB rcv_up */
	uint32_t	tlb_rcv_adv;	/* TCPCB rcv_adv */
	uint32_t	tlb_rcv_nxt;	/* TCPCB rcv_nxt */
	tcp_seq		tlb_sack_newdata; /* TCPCB sack_newdata */
	uint32_t       	tlb_rcv_wnd;	/* TCPCB rcv_wnd */
	uint32_t	tlb_dupacks;	/* TCPCB t_dupacks */
	int		tlb_segqlen;	/* TCPCB segqlen */
	int		tlb_snd_numholes; /* TCPCB snd_numholes */
	uint32_t 	tlb_flex1; /* Event specific information */
	uint32_t 	tlb_flex2; /* Event specific information */
	uint8_t		tlb_snd_scale:4, /* TCPCB snd_scale */
			tlb_rcv_scale:4; /* TCPCB rcv_scale */
	uint8_t		_pad[3];	/* Padding */

	/* Per-stack info */
	union tcp_log_stackspecific tlb_stackinfo;
#define	tlb_rack	tlb_stackinfo.u_rack

	/* The packet */
	uint32_t	tlb_len;	/* The packet's data length */
	struct tcphdr	tlb_th;		/* The TCP header */
	uint8_t		tlb_opts[TCP_MAXOLEN]; /* The TCP options */

	/* Verbose information (optional) */
	struct tcp_log_verbose tlb_verbose[0];
} ALIGN_TCP_LOG;

enum tcp_log_events {
	TCP_LOG_IN = 1,	/* Incoming packet                 1 */
	TCP_LOG_OUT,	/* Transmit (without other event)  2 */
	TCP_LOG_RTO,	/* Retransmit timeout              3 */
	TCP_LOG_TF_ACK,	/* Transmit due to TF_ACK          4 */
	TCP_LOG_BAD_RETRAN, /* Detected bad retransmission 5 */
	TCP_LOG_PRR,	/* Doing PRR                       6 */
	TCP_LOG_REORDER,/* Detected reorder                7 */
	TCP_LOG_PACER,	/* Pacer sending a packet          8 */
	BBR_LOG_BBRUPD,		/* We updated BBR info     9 */
	BBR_LOG_BBRSND,		/* We did a slot calculation and sending is done 10 */
	BBR_LOG_ACKCLEAR,	/* A ack clears all outstanding     11 */
	BBR_LOG_INQUEUE,	/* The tcb had a packet input to it 12 */
	BBR_LOG_TIMERSTAR,	/* Start a timer                    13 */
	BBR_LOG_TIMERCANC,	/* Cancel a timer                   14 */
	BBR_LOG_ENTREC,		/* Entered recovery                 15 */
	BBR_LOG_EXITREC,	/* Exited recovery                  16 */
	BBR_LOG_CWND,		/* Cwnd change                      17 */
	BBR_LOG_BWSAMP,		/* LT B/W sample has been made      18 */
	BBR_LOG_MSGSIZE,	/* We received a EMSGSIZE error     19 */
	BBR_LOG_BBRRTT,		/* BBR RTT is updated               20 */
	BBR_LOG_JUSTRET,	/* We just returned out of output   21 */
	BBR_LOG_STATE,		/* A BBR state change occured       22 */
	BBR_LOG_PKT_EPOCH,      /* A BBR packet epoch occured       23 */
	BBR_LOG_PERSIST,        /* BBR changed to/from a persists   24 */
	TCP_LOG_FLOWEND,        /* End of a flow                    25 */
	BBR_LOG_RTO,            /* BBR's timeout includes BBR info  26 */
	BBR_LOG_DOSEG_DONE,     /* pacer do_segment completes       27 */
	BBR_LOG_EXIT_GAIN,      /* pacer do_segment completes       28 */
	BBR_LOG_THRESH_CALC,    /* Doing threshold calculation      29 */
	BBR_LOG_EXTRACWNDGAIN,	/* Removed                          30 */
	TCP_LOG_USERSEND, 	/* User level sends data            31 */
	UNUSED_32,	 	/* Unused                           32 */
	UNUSED_33, 		/* Unused                           33 */
	BBR_LOG_TIME_EPOCH, 	/* A timed based Epoch occured      34 */
	BBR_LOG_TO_PROCESS,	/* A to was processed               35 */
	BBR_LOG_BBRTSO, 	/* TSO update	                    36 */
	BBR_LOG_PACERDIAG,	/* Pacer diag insert                37 */
	BBR_LOG_LOWGAIN,	/* Low gain accounting              38 */
	BBR_LOG_PROGRESS,	/* Progress timer event             39 */
	TCP_LOG_SOCKET_OPT,	/* A socket option is set	    40 */
	BBR_LOG_TIMERPREP,	/* A BBR var to debug out TLP issues  41 */
	BBR_LOG_ENOBUF_JMP,	/* We had a enobuf jump 42 */
	BBR_LOG_PACING_CALC,	/* calc the pacing time 43 */
	BBR_LOG_RTT_SHRINKS,	/* We had a log reduction of rttProp 44 */
	BBR_LOG_BW_RED_EV,	/* B/W reduction events 45 */
	BBR_LOG_REDUCE,		/* old bbr log reduce for 4.1 and earlier 46*/
	TCP_LOG_RTT,		/* A rtt (in useconds) is being sampled and applied to the srtt algo 47 */
	BBR_LOG_SETTINGS_CHG,   /* Settings changed for loss response 48 */
	BBR_LOG_SRTT_GAIN_EVENT, /* SRTT gaining 49 */
	TCP_LOG_REASS,		/* Reassembly buffer logging 50 */
	TCP_LOG_END		/* End (keep at end)	            51 */
};

enum tcp_log_states {
	TCP_LOG_STATE_CLEAR = -1,	/* Deactivate and clear tracing */
	TCP_LOG_STATE_OFF = 0,		/* Pause */
	TCP_LOG_STATE_TAIL=1,		/* Keep the trailing events */
	TCP_LOG_STATE_HEAD=2,		/* Keep the leading events */
	TCP_LOG_STATE_HEAD_AUTO=3,	/* Keep the leading events, and
					   automatically dump them to the
					   device  */
	TCP_LOG_STATE_CONTINUAL=4,	/* Continually dump the data when full */
	TCP_LOG_STATE_TAIL_AUTO=5,	/* Keep the trailing events, and
					   automatically dump them when the
					   session ends */
};

/* Use this if we don't know whether the operation succeeded. */
#define	ERRNO_UNK	(-1)

/*
 * If the user included dev/tcp_log/tcp_log_dev.h, then include our private
 * headers. Otherwise, there is no reason to pollute all the files with an
 * additional include.
 *
 * This structure is aligned to an 8-byte boundary to match the alignment
 * requirements of (struct tcp_log_buffer).
 */
#ifdef __tcp_log_dev_h__
struct tcp_log_header {
	struct tcp_log_common_header tlh_common;
#define	tlh_version	tlh_common.tlch_version
#define	tlh_type	tlh_common.tlch_type
#define	tlh_length	tlh_common.tlch_length
	struct in_endpoints	tlh_ie;
	struct timeval		tlh_offset;	/* Uptime -> UTC offset */
	char			tlh_id[TCP_LOG_ID_LEN];
	char			tlh_reason[TCP_LOG_REASON_LEN];
	uint8_t		tlh_af;
	uint8_t		_pad[7];
} ALIGN_TCP_LOG;

#ifdef _KERNEL
struct tcp_log_dev_log_queue {
	struct tcp_log_dev_queue tldl_common;
	char			tldl_id[TCP_LOG_ID_LEN];
	char			tldl_reason[TCP_LOG_REASON_LEN];
	struct in_endpoints	tldl_ie;
	struct tcp_log_stailq	tldl_entries;
	int			tldl_count;
	uint8_t			tldl_af;
};
#endif /* _KERNEL */
#endif /* __tcp_log_dev_h__ */

#ifdef _KERNEL

#define	TCP_LOG_BUF_DEFAULT_SESSION_LIMIT	10000
#define	TCP_LOG_BUF_DEFAULT_GLOBAL_LIMIT	1000000

/*
 * TCP_LOG_EVENT_VERBOSE: The same as TCP_LOG_EVENT, except it always
 * tries to record verbose information.
 */
#define	TCP_LOG_EVENT_VERBOSE(tp, th, rxbuf, txbuf, eventid, errornum, len, stackinfo, th_hostorder, tv) \
	do {								\
		if (tp->t_logstate != TCP_LOG_STATE_OFF)		\
			tcp_log_event_(tp, th, rxbuf, txbuf, eventid,	\
	 	        errornum, len, stackinfo, th_hostorder,		\
		        tp->t_output_caller, __func__, __LINE__, tv);	\
	} while (0)

/*
 * TCP_LOG_EVENT: This is a macro so we can capture function/line
 * information when needed.
 *
 * Prototype:
 * TCP_LOG_EVENT(struct tcpcb *tp, struct tcphdr *th, struct sockbuf *rxbuf, 
 *     struct sockbuf *txbuf, uint8_t eventid, int errornum,
 *     union tcp_log_stackspecific *stackinfo)
 *
 * tp is mandatory and must be write locked.
 * th is optional; if present, it will appear in the record.
 * rxbuf and txbuf are optional; if present, they will appear in the record.
 * eventid is mandatory.
 * errornum is mandatory (it indicates the success or failure of the
 *     operation associated with the event).
 * len indicates the length of the packet. If no packet, use 0.
 * stackinfo is optional; if present, it will appear in the record.
 */
#ifdef TCP_LOG_FORCEVERBOSE
#define	TCP_LOG_EVENT	TCP_LOG_EVENT_VERBOSE
#else
#define	TCP_LOG_EVENT(tp, th, rxbuf, txbuf, eventid, errornum, len, stackinfo, th_hostorder) \
	do {								\
		if (tcp_log_verbose)					\
			TCP_LOG_EVENT_VERBOSE(tp, th, rxbuf, txbuf,	\
			    eventid, errornum, len, stackinfo,		\
			    th_hostorder, NULL);				\
		else if (tp->t_logstate != TCP_LOG_STATE_OFF)		\
			tcp_log_event_(tp, th, rxbuf, txbuf, eventid,	\
			    errornum, len, stackinfo, th_hostorder,	\
			    NULL, NULL, 0, NULL);				\
	} while (0)
#endif /* TCP_LOG_FORCEVERBOSE */
#define	TCP_LOG_EVENTP(tp, th, rxbuf, txbuf, eventid, errornum, len, stackinfo, th_hostorder, tv) \
	do {								\
		if (tp->t_logstate != TCP_LOG_STATE_OFF)		\
			tcp_log_event_(tp, th, rxbuf, txbuf, eventid,	\
			    errornum, len, stackinfo, th_hostorder,	\
			    NULL, NULL, 0, tv);				\
	} while (0)


#ifdef TCP_BLACKBOX
extern bool tcp_log_verbose;
void tcp_log_drain(struct tcpcb *tp);
int tcp_log_dump_tp_logbuf(struct tcpcb *tp, char *reason, int how, bool force);
void tcp_log_dump_tp_bucket_logbufs(struct tcpcb *tp, char *reason);
struct tcp_log_buffer *tcp_log_event_(struct tcpcb *tp, struct tcphdr *th, struct sockbuf *rxbuf,
    struct sockbuf *txbuf, uint8_t eventid, int errornum, uint32_t len,
    union tcp_log_stackspecific *stackinfo, int th_hostorder,
    const char *output_caller, const char *func, int line, const struct timeval *tv);
size_t tcp_log_get_id(struct tcpcb *tp, char *buf);
u_int tcp_log_get_id_cnt(struct tcpcb *tp);
int tcp_log_getlogbuf(struct sockopt *sopt, struct tcpcb *tp);
void tcp_log_init(void);
int tcp_log_set_id(struct tcpcb *tp, char *id);
int tcp_log_state_change(struct tcpcb *tp, int state);
void tcp_log_tcpcbinit(struct tcpcb *tp);
void tcp_log_tcpcbfini(struct tcpcb *tp);
void tcp_log_flowend(struct tcpcb *tp);
#else /* !TCP_BLACKBOX */
#define tcp_log_verbose	(false)

static inline struct tcp_log_buffer *
tcp_log_event_(struct tcpcb *tp, struct tcphdr *th, struct sockbuf *rxbuf,
    struct sockbuf *txbuf, uint8_t eventid, int errornum, uint32_t len,
    union tcp_log_stackspecific *stackinfo, int th_hostorder,
    const char *output_caller, const char *func, int line,
    const struct timeval *tv)
{

	return (NULL);
}
#endif /* TCP_BLACKBOX */

#endif	/* _KERNEL */
#endif	/* __tcp_log_buf_h__ */
