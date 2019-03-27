/*-
 * Copyright (c) 2016 Netflix, Inc.
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
 * $FreeBSD$
 */

#ifndef _NETINET_TCP_RACK_H_
#define _NETINET_TCP_RACK_H_

#define RACK_ACKED	  0x0001/* The remote endpoint acked this */
#define RACK_TO_MIXED	  0x0002/* A timeout occured that mixed the send order */
#define RACK_DEFERRED	  0x0004/* We can't use this for RTT calc */
#define RACK_OVERMAX	  0x0008/* We have more retran's then we can fit */
#define RACK_SACK_PASSED  0x0010/* A sack was done above this block */
#define RACK_WAS_SACKPASS 0x0020/* We retransmitted due to SACK pass */
#define RACK_HAS_FIN	  0x0040/* segment is sent with fin */
#define RACK_TLP	  0x0080/* segment sent as tail-loss-probe */

#define RACK_NUM_OF_RETRANS 3

#define RACK_INITIAL_RTO 1000 /* 1 second in milli seconds */

struct rack_sendmap {
	TAILQ_ENTRY(rack_sendmap) r_next;	/* seq number arrayed next */
	TAILQ_ENTRY(rack_sendmap) r_tnext;	/* Time of transmit based next */
	uint32_t r_tim_lastsent[RACK_NUM_OF_RETRANS];
	uint32_t r_start;	/* Sequence number of the segment */
	uint32_t r_end;		/* End seq, this is 1 beyond actually */
	uint32_t r_rtr_bytes;	/* How many bytes have been retransmitted */
	uint16_t r_rtr_cnt;	/* Retran count, index this -1 to get time
				 * sent */
	uint8_t r_flags;	/* Flags as defined above */
	uint8_t r_sndcnt;	/* Retran count, not limited by
				 * RACK_NUM_OF_RETRANS */
	uint8_t r_in_tmap;	/* Flag to see if its in the r_tnext array */
	uint8_t r_resv[3];
};

TAILQ_HEAD(rack_head, rack_sendmap);


/*
 * We use the rate sample structure to
 * assist in single sack/ack rate and rtt
 * calculation. In the future we will expand
 * this in BBR to do forward rate sample
 * b/w estimation.
 */
#define RACK_RTT_EMPTY 0x00000001	/* Nothing yet stored in RTT's */
#define RACK_RTT_VALID 0x00000002	/* We have at least one valid RTT */
struct rack_rtt_sample {
	uint32_t rs_flags;
	uint32_t rs_rtt_lowest;
	uint32_t rs_rtt_highest;
	uint32_t rs_rtt_cnt;
	uint64_t rs_rtt_tot;
};

#define RACK_LOG_TYPE_ACK	0x01
#define RACK_LOG_TYPE_OUT	0x02
#define RACK_LOG_TYPE_TO	0x03
#define RACK_LOG_TYPE_ALLOC     0x04
#define RACK_LOG_TYPE_FREE      0x05


struct rack_log {
	union {
		struct rack_sendmap *rsm;	/* For alloc/free */
		uint64_t sb_acc;/* For out/ack or t-o */
	};
	uint32_t th_seq;
	uint32_t th_ack;
	uint32_t snd_una;
	uint32_t snd_nxt;	/* th_win for TYPE_ACK */
	uint32_t snd_max;
	uint32_t blk_start[4];
	uint32_t blk_end[4];
	uint8_t type;
	uint8_t n_sackblks;
	uint16_t len;		/* Timeout T3=1, TLP=2, RACK=3 */
};

/*
 * Magic numbers for logging timeout events if the
 * logging is enabled.
 */
#define RACK_TO_FRM_TMR  1
#define RACK_TO_FRM_TLP  2
#define RACK_TO_FRM_RACK 3
#define RACK_TO_FRM_KEEP 4
#define RACK_TO_FRM_PERSIST 5
#define RACK_TO_FRM_DELACK 6

struct rack_opts_stats {
	uint64_t tcp_rack_prop_rate;
 	uint64_t tcp_rack_prop;
	uint64_t tcp_rack_tlp_reduce;
	uint64_t tcp_rack_early_recov;
	uint64_t tcp_rack_pace_always;
	uint64_t tcp_rack_pace_reduce;
	uint64_t tcp_rack_max_seg;
	uint64_t tcp_rack_prr_sendalot;
	uint64_t tcp_rack_min_to;
	uint64_t tcp_rack_early_seg;
	uint64_t tcp_rack_reord_thresh;
	uint64_t tcp_rack_reord_fade;
	uint64_t tcp_rack_tlp_thresh;
	uint64_t tcp_rack_pkt_delay;
	uint64_t tcp_rack_tlp_inc_var;
	uint64_t tcp_tlp_use;
	uint64_t tcp_rack_idle_reduce;
	uint64_t tcp_rack_idle_reduce_high;
	uint64_t rack_no_timer_in_hpts;
	uint64_t tcp_rack_min_pace_seg;
	uint64_t tcp_rack_min_pace;
};

#define TLP_USE_ID	1	/* Internet draft behavior */
#define TLP_USE_TWO_ONE 2	/* Use 2.1 behavior */
#define TLP_USE_TWO_TWO 3	/* Use 2.2 behavior */

#ifdef _KERNEL
#define RACK_OPTS_SIZE (sizeof(struct rack_opts_stats)/sizeof(uint64_t))
extern counter_u64_t rack_opts_arry[RACK_OPTS_SIZE];
#define RACK_OPTS_ADD(name, amm) counter_u64_add(rack_opts_arry[(offsetof(struct rack_opts_stats, name)/sizeof(uint64_t))], (amm))
#define RACK_OPTS_INC(name) RACK_OPTS_ADD(name, 1)
#endif
/*
 * As we get each SACK we wade through the
 * rc_map and mark off what is acked.
 * We also increment rc_sacked as well.
 *
 * We also pay attention to missing entries
 * based on the time and possibly mark them
 * for retransmit. If we do and we are not already
 * in recovery we enter recovery. In doing
 * so we claer prr_delivered/holes_rxt and prr_sent_dur_rec.
 * We also setup rc_next/rc_snd_nxt/rc_send_end so
 * we will know where to send from. When not in
 * recovery rc_next will be NULL and rc_snd_nxt should
 * equal snd_max.
 *
 * Whenever we retransmit from recovery we increment
 * rc_holes_rxt as we retran a block and mark it as retransmitted
 * with the time it was sent. During non-recovery sending we
 * add to our map and note the time down of any send expanding
 * the rc_map at the tail and moving rc_snd_nxt up with snd_max.
 *
 * In recovery during SACK/ACK processing if a chunk has
 * been retransmitted and it is now acked, we decrement rc_holes_rxt.
 * When we retransmit from the scoreboard we use
 * rc_next and rc_snd_nxt/rc_send_end to help us
 * find what needs to be retran.
 *
 * To calculate pipe we simply take (snd_max - snd_una) + rc_holes_rxt
 * This gets us the effect of RFC6675 pipe, counting twice for
 * bytes retransmitted.
 */

#define TT_RACK_FR_TMR	0x2000

/*
 * Locking for the rack control block.
 * a) Locked by INP_WLOCK
 * b) Locked by the hpts-mutex
 *
 */

struct rack_control {
	/* Second cache line 0x40 from tcp_rack */
	struct rack_head rc_map;/* List of all segments Lock(a) */
	struct rack_head rc_tmap;	/* List in transmit order Lock(a) */
	struct rack_sendmap *rc_tlpsend;	/* Remembered place for
						 * tlp_sending Lock(a) */
	struct rack_sendmap *rc_resend;	/* something we have been asked to
					 * resend */
	uint32_t rc_hpts_flags;
	uint32_t rc_timer_exp;	/* If a timer ticks of expiry */
	uint32_t rc_rack_min_rtt;	/* lowest RTT seen Lock(a) */
	uint32_t rc_rack_largest_cwnd;	/* Largest CWND we have seen Lock(a) */

	/* Third Cache line 0x80 */
	struct rack_head rc_free;	/* Allocation array */
	uint32_t rc_time_last_sent;	/* Time we last sent some data and
					 * logged it Lock(a). */
	uint32_t rc_reorder_ts;	/* Last time we saw reordering Lock(a) */

	uint32_t rc_tlp_new_data;	/* we need to send new-data on a TLP
					 * Lock(a) */
	uint32_t rc_prr_out;	/* bytes sent during recovery Lock(a) */

	uint32_t rc_prr_recovery_fs;	/* recovery fs point Lock(a) */

	uint32_t rc_prr_sndcnt;	/* Prr sndcnt Lock(a) */

	uint32_t rc_sacked;	/* Tot sacked on scoreboard Lock(a) */
	uint32_t rc_last_tlp_seq;	/* Last tlp sequence Lock(a) */

	uint32_t rc_prr_delivered;	/* during recovery prr var Lock(a) */
	uint16_t rc_tlp_send_cnt;	/* Number of TLP sends we have done
					 * since peer spoke to us Lock(a) */
	uint16_t rc_tlp_seg_send_cnt;	/* Number of times we have TLP sent
					 * rc_last_tlp_seq Lock(a) */

	uint32_t rc_loss_count;	/* During recovery how many segments were lost
				 * Lock(a) */
	uint32_t rc_reorder_fade;	/* Socket option value Lock(a) */

	/* Forth cache line 0xc0  */
	/* Times */

	uint32_t rc_rack_tmit_time;	/* Rack transmit time Lock(a) */
	uint32_t rc_holes_rxt;	/* Tot retraned from scoreboard Lock(a) */

	/* Variables to track bad retransmits and recover */
	uint32_t rc_rsm_start;	/* RSM seq number we retransmitted Lock(a) */
	uint32_t rc_cwnd_at;	/* cwnd at the retransmit Lock(a) */

	uint32_t rc_ssthresh_at;/* ssthresh at the retransmit Lock(a) */
	uint32_t rc_num_maps_alloced;	/* Number of map blocks (sacks) we
					 * have allocated */
	uint32_t rc_rcvtime;	/* When we last received data */
	uint32_t rc_notused;
	uint32_t rc_last_output_to; 
	uint32_t rc_went_idle_time;

	struct rack_sendmap *rc_sacklast;	/* sack remembered place
						 * Lock(a) */

	struct rack_sendmap *rc_next;	/* remembered place where we next
					 * retransmit at Lock(a) */
	struct rack_sendmap *rc_rsm_at_retran;	/* Debug variable kept for
						 * cache line alignment
						 * Lock(a) */
	/* Cache line split 0x100 */
	struct sack_filter rack_sf;
	/* Cache line split 0x140 */
	/* Flags for various things */
	struct rack_rtt_sample rack_rs;
	uint32_t rc_tlp_threshold;	/* Socket option value Lock(a) */
	uint16_t rc_early_recovery_segs;	/* Socket option value Lock(a) */
	uint16_t rc_reorder_shift;	/* Socket option value Lock(a) */
	uint16_t rc_pkt_delay;	/* Socket option value Lock(a) */
	uint8_t rc_prop_rate;	/* Socket option value Lock(a) */
	uint8_t rc_prop_reduce;	/* Socket option value Lock(a) */
	uint8_t rc_tlp_cwnd_reduce;	/* Socket option value Lock(a) */
	uint8_t rc_early_recovery;	/* Socket option value Lock(a) */
	uint8_t rc_prr_sendalot;/* Socket option value Lock(a) */
	uint8_t rc_min_to;	/* Socket option value Lock(a) */
	uint8_t rc_prr_inc_var;	/* Socket option value Lock(a) */
	uint8_t rc_tlp_rtx_out;	/* This is TLPRtxOut in the draft */
	uint8_t rc_rate_sample_method;
};

#ifdef _KERNEL

struct tcp_rack {
	/* First cache line 0x00 */
	TAILQ_ENTRY(tcp_rack) r_hpts;	/* hptsi queue next Lock(b) */
	int32_t(*r_substate) (struct mbuf *, struct tcphdr *,
	    struct socket *, struct tcpcb *, struct tcpopt *,
	    int32_t, int32_t, uint32_t, int, int);	/* Lock(a) */
	struct tcpcb *rc_tp;	/* The tcpcb Lock(a) */
	struct inpcb *rc_inp;	/* The inpcb Lock(a) */
	uint32_t rc_free_cnt;	/* Number of free entries on the rc_free list
				 * Lock(a) */
	uint32_t rc_rack_rtt;	/* RACK-RTT Lock(a) */
	uint16_t r_wanted_output;	/* Output routine wanted to be called */
	uint16_t r_cpu;		/* CPU that the INP is running on Lock(a) */
	uint16_t rc_pace_max_segs;	/* Socket option value Lock(a) */
	uint16_t rc_pace_reduce;/* Socket option value Lock(a) */

	uint8_t r_state;	/* Current rack state Lock(a) */
	uint8_t rc_tmr_stopped : 7,
		t_timers_stopped : 1;
	uint8_t rc_enobuf;	/* count of enobufs on connection provides
				 * backoff Lock(a) */
	uint8_t r_timer_override : 1,	/* hpts override Lock(a) */
		r_tlp_running : 1, 	/* Running from a TLP timeout Lock(a) */
		r_is_v6 : 1,	/* V6 pcb Lock(a)  */
		rc_in_persist : 1,
		rc_last_pto_set : 1, /* XXX not used */
		rc_tlp_in_progress : 1,
		rc_always_pace : 1,	/* Socket option value Lock(a) */
		rc_timer_up : 1;	/* The rack timer is up flag  Lock(a) */
	uint8_t r_idle_reduce_largest : 1,
		r_enforce_min_pace : 2,
		r_min_pace_seg_thresh : 5;
	uint8_t rack_tlp_threshold_use;
	uint8_t rc_allow_data_af_clo: 1,
		delayed_ack : 1,
		rc_avail : 6;
	uint8_t r_resv[2];	/* Fill to cache line boundary */
	/* Cache line 2 0x40 */
	struct rack_control r_ctl;
}        __aligned(CACHE_LINE_SIZE);

#endif
#endif
