/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2008, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2012, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2012, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifndef _NETINET_SCTP_STRUCTS_H_
#define _NETINET_SCTP_STRUCTS_H_

#include <netinet/sctp_os.h>
#include <netinet/sctp_header.h>
#include <netinet/sctp_auth.h>

struct sctp_timer {
	sctp_os_timer_t timer;

	int type;
	/*
	 * Depending on the timer type these will be setup and cast with the
	 * appropriate entity.
	 */
	void *ep;
	void *tcb;
	void *net;
	void *vnet;

	/* for sanity checking */
	void *self;
	uint32_t ticks;
	uint32_t stopped_from;
};


struct sctp_foo_stuff {
	struct sctp_inpcb *inp;
	uint32_t lineno;
	uint32_t ticks;
	int updown;
};


/*
 * This is the information we track on each interface that we know about from
 * the distant end.
 */
TAILQ_HEAD(sctpnetlisthead, sctp_nets);

struct sctp_stream_reset_list {
	TAILQ_ENTRY(sctp_stream_reset_list) next_resp;
	uint32_t seq;
	uint32_t tsn;
	uint32_t number_entries;
	uint16_t list_of_streams[];
};

TAILQ_HEAD(sctp_resethead, sctp_stream_reset_list);

/*
 * Users of the iterator need to malloc a iterator with a call to
 * sctp_initiate_iterator(inp_func, assoc_func, inp_func,  pcb_flags, pcb_features,
 *     asoc_state, void-ptr-arg, uint32-arg, end_func, inp);
 *
 * Use the following two defines if you don't care what pcb flags are on the EP
 * and/or you don't care what state the association is in.
 *
 * Note that if you specify an INP as the last argument then ONLY each
 * association of that single INP will be executed upon. Note that the pcb
 * flags STILL apply so if the inp you specify has different pcb_flags then
 * what you put in pcb_flags nothing will happen. use SCTP_PCB_ANY_FLAGS to
 * assure the inp you specify gets treated.
 */
#define SCTP_PCB_ANY_FLAGS	0x00000000
#define SCTP_PCB_ANY_FEATURES	0x00000000
#define SCTP_ASOC_ANY_STATE	0x00000000

typedef void (*asoc_func) (struct sctp_inpcb *, struct sctp_tcb *, void *ptr,
    uint32_t val);
typedef int (*inp_func) (struct sctp_inpcb *, void *ptr, uint32_t val);
typedef void (*end_func) (void *ptr, uint32_t val);

#if defined(__FreeBSD__) && defined(SCTP_MCORE_INPUT) && defined(SMP)
/* whats on the mcore control struct */
struct sctp_mcore_queue {
	TAILQ_ENTRY(sctp_mcore_queue) next;
	struct vnet *vn;
	struct mbuf *m;
	int off;
	int v6;
};

TAILQ_HEAD(sctp_mcore_qhead, sctp_mcore_queue);

struct sctp_mcore_ctrl {
	SCTP_PROCESS_STRUCT thread_proc;
	struct sctp_mcore_qhead que;
	struct mtx core_mtx;
	struct mtx que_mtx;
	int running;
	int cpuid;
};


#endif


struct sctp_iterator {
	TAILQ_ENTRY(sctp_iterator) sctp_nxt_itr;
	struct vnet *vn;
	struct sctp_timer tmr;
	struct sctp_inpcb *inp;	/* current endpoint */
	struct sctp_tcb *stcb;	/* current* assoc */
	struct sctp_inpcb *next_inp;	/* special hook to skip to */
	asoc_func function_assoc;	/* per assoc function */
	inp_func function_inp;	/* per endpoint function */
	inp_func function_inp_end;	/* end INP function */
	end_func function_atend;	/* iterator completion function */
	void *pointer;		/* pointer for apply func to use */
	uint32_t val;		/* value for apply func to use */
	uint32_t pcb_flags;	/* endpoint flags being checked */
	uint32_t pcb_features;	/* endpoint features being checked */
	uint32_t asoc_state;	/* assoc state being checked */
	uint32_t iterator_flags;
	uint8_t no_chunk_output;
	uint8_t done_current_ep;
};

/* iterator_flags values */
#define SCTP_ITERATOR_DO_ALL_INP	0x00000001
#define SCTP_ITERATOR_DO_SINGLE_INP	0x00000002


TAILQ_HEAD(sctpiterators, sctp_iterator);

struct sctp_copy_all {
	struct sctp_inpcb *inp;	/* ep */
	struct mbuf *m;
	struct sctp_sndrcvinfo sndrcv;
	ssize_t sndlen;
	int cnt_sent;
	int cnt_failed;
};

struct sctp_asconf_iterator {
	struct sctpladdr list_of_work;
	int cnt;
};

struct iterator_control {
	struct mtx ipi_iterator_wq_mtx;
	struct mtx it_mtx;
	SCTP_PROCESS_STRUCT thread_proc;
	struct sctpiterators iteratorhead;
	struct sctp_iterator *cur_it;
	uint32_t iterator_running;
	uint32_t iterator_flags;
};
#define SCTP_ITERATOR_STOP_CUR_IT	0x00000004
#define SCTP_ITERATOR_STOP_CUR_INP	0x00000008

struct sctp_net_route {
	sctp_rtentry_t *ro_rt;
	struct llentry *ro_lle;
	char *ro_prepend;
	uint16_t ro_plen;
	uint16_t ro_flags;
	uint16_t ro_mtu;
	uint16_t spare;
	union sctp_sockstore _l_addr;	/* remote peer addr */
	struct sctp_ifa *_s_addr;	/* our selected src addr */
};

struct htcp {
	uint16_t alpha;		/* Fixed point arith, << 7 */
	uint8_t beta;		/* Fixed point arith, << 7 */
	uint8_t modeswitch;	/* Delay modeswitch until we had at least one
				 * congestion event */
	uint32_t last_cong;	/* Time since last congestion event end */
	uint32_t undo_last_cong;
	uint16_t bytes_acked;
	uint32_t bytecount;
	uint32_t minRTT;
	uint32_t maxRTT;

	uint32_t undo_maxRTT;
	uint32_t undo_old_maxB;

	/* Bandwidth estimation */
	uint32_t minB;
	uint32_t maxB;
	uint32_t old_maxB;
	uint32_t Bi;
	uint32_t lasttime;
};

struct rtcc_cc {
	struct timeval tls;	/* The time we started the sending  */
	uint64_t lbw;		/* Our last estimated bw */
	uint64_t lbw_rtt;	/* RTT at bw estimate */
	uint64_t bw_bytes;	/* The total bytes since this sending began */
	uint64_t bw_tot_time;	/* The total time since sending began */
	uint64_t new_tot_time;	/* temp holding the new value */
	uint64_t bw_bytes_at_last_rttc;	/* What bw_bytes was at last rtt calc */
	uint32_t cwnd_at_bw_set;	/* Cwnd at last bw saved - lbw */
	uint32_t vol_reduce;	/* cnt of voluntary reductions */
	uint16_t steady_step;	/* The number required to be in steady state */
	uint16_t step_cnt;	/* The current number */
	uint8_t ret_from_eq;	/* When all things are equal what do I return
				 * 0/1 - 1 no cc advance */
	uint8_t use_dccc_ecn;	/* Flag to enable DCCC ECN */
	uint8_t tls_needs_set;	/* Flag to indicate we need to set tls 0 or 1
				 * means set at send 2 not */
	uint8_t last_step_state;	/* Last state if steady state stepdown
					 * is on */
	uint8_t rtt_set_this_sack;	/* Flag saying this sack had RTT calc
					 * on it */
	uint8_t last_inst_ind;	/* Last saved inst indication */
};


struct sctp_nets {
	TAILQ_ENTRY(sctp_nets) sctp_next;	/* next link */

	/*
	 * Things on the top half may be able to be split into a common
	 * structure shared by all.
	 */
	struct sctp_timer pmtu_timer;
	struct sctp_timer hb_timer;

	/*
	 * The following two in combination equate to a route entry for v6
	 * or v4.
	 */
	struct sctp_net_route ro;

	/* mtu discovered so far */
	uint32_t mtu;
	uint32_t ssthresh;	/* not sure about this one for split */
	uint32_t last_cwr_tsn;
	uint32_t cwr_window_tsn;
	uint32_t ecn_ce_pkt_cnt;
	uint32_t lost_cnt;
	/* smoothed average things for RTT and RTO itself */
	int lastsa;
	int lastsv;
	uint64_t rtt;		/* last measured rtt value in us */
	unsigned int RTO;

	/* This is used for SHUTDOWN/SHUTDOWN-ACK/SEND or INIT timers */
	struct sctp_timer rxt_timer;

	/* last time in seconds I sent to it */
	struct timeval last_sent_time;
	union cc_control_data {
		struct htcp htcp_ca;	/* JRS - struct used in HTCP algorithm */
		struct rtcc_cc rtcc;	/* rtcc module cc stuff  */
	}               cc_mod;
	int ref_count;

	/* Congestion stats per destination */
	/*
	 * flight size variables and such, sorry Vern, I could not avoid
	 * this if I wanted performance :>
	 */
	uint32_t flight_size;
	uint32_t cwnd;		/* actual cwnd */
	uint32_t prev_cwnd;	/* cwnd before any processing */
	uint32_t ecn_prev_cwnd;	/* ECN prev cwnd at first ecn_echo seen in new
				 * window */
	uint32_t partial_bytes_acked;	/* in CA tracks when to incr a MTU */
	/* tracking variables to avoid the aloc/free in sack processing */
	unsigned int net_ack;
	unsigned int net_ack2;

	/*
	 * JRS - 5/8/07 - Variable to track last time a destination was
	 * active for CMT PF
	 */
	uint32_t last_active;

	/*
	 * CMT variables (iyengar@cis.udel.edu)
	 */
	uint32_t this_sack_highest_newack;	/* tracks highest TSN newly
						 * acked for a given dest in
						 * the current SACK. Used in
						 * SFR and HTNA algos */
	uint32_t pseudo_cumack;	/* CMT CUC algorithm. Maintains next expected
				 * pseudo-cumack for this destination */
	uint32_t rtx_pseudo_cumack;	/* CMT CUC algorithm. Maintains next
					 * expected pseudo-cumack for this
					 * destination */

	/* CMT fast recovery variables */
	uint32_t fast_recovery_tsn;
	uint32_t heartbeat_random1;
	uint32_t heartbeat_random2;
#ifdef INET6
	uint32_t flowlabel;
#endif
	uint8_t dscp;

	struct timeval start_time;	/* time when this net was created */
	uint32_t marked_retrans;	/* number or DATA chunks marked for
					 * timer based retransmissions */
	uint32_t marked_fastretrans;
	uint32_t heart_beat_delay;	/* Heart Beat delay in ms */

	/* if this guy is ok or not ... status */
	uint16_t dest_state;
	/* number of timeouts to consider the destination unreachable */
	uint16_t failure_threshold;
	/* number of timeouts to consider the destination potentially failed */
	uint16_t pf_threshold;
	/* error stats on the destination */
	uint16_t error_count;
	/* UDP port number in case of UDP tunneling */
	uint16_t port;

	uint8_t fast_retran_loss_recovery;
	uint8_t will_exit_fast_recovery;
	/* Flags that probably can be combined into dest_state */
	uint8_t fast_retran_ip;	/* fast retransmit in progress */
	uint8_t hb_responded;
	uint8_t saw_newack;	/* CMT's SFR algorithm flag */
	uint8_t src_addr_selected;	/* if we split we move */
	uint8_t indx_of_eligible_next_to_use;
	uint8_t addr_is_local;	/* its a local address (if known) could move
				 * in split */

	/*
	 * CMT variables (iyengar@cis.udel.edu)
	 */
	uint8_t find_pseudo_cumack;	/* CMT CUC algorithm. Flag used to
					 * find a new pseudocumack. This flag
					 * is set after a new pseudo-cumack
					 * has been received and indicates
					 * that the sender should find the
					 * next pseudo-cumack expected for
					 * this destination */
	uint8_t find_rtx_pseudo_cumack;	/* CMT CUCv2 algorithm. Flag used to
					 * find a new rtx-pseudocumack. This
					 * flag is set after a new
					 * rtx-pseudo-cumack has been received
					 * and indicates that the sender
					 * should find the next
					 * rtx-pseudo-cumack expected for this
					 * destination */
	uint8_t new_pseudo_cumack;	/* CMT CUC algorithm. Flag used to
					 * indicate if a new pseudo-cumack or
					 * rtx-pseudo-cumack has been received */
	uint8_t window_probe;	/* Doing a window probe? */
	uint8_t RTO_measured;	/* Have we done the first measure */
	uint8_t last_hs_used;	/* index into the last HS table entry we used */
	uint8_t lan_type;
	uint8_t rto_needed;
	uint32_t flowid;
	uint8_t flowtype;
};


struct sctp_data_chunkrec {
	uint32_t tsn;		/* the TSN of this transmit */
	uint32_t mid;		/* the message identifier of this transmit */
	uint16_t sid;		/* the stream number of this guy */
	uint32_t ppid;
	uint32_t context;	/* from send */
	uint32_t cwnd_at_send;
	/*
	 * part of the Highest sacked algorithm to be able to stroke counts
	 * on ones that are FR'd.
	 */
	uint32_t fast_retran_tsn;	/* sending_seq at the time of FR */
	struct timeval timetodrop;	/* time we drop it from queue */
	uint32_t fsn;		/* Fragment Sequence Number */
	uint8_t doing_fast_retransmit;
	uint8_t rcv_flags;	/* flags pulled from data chunk on inbound for
				 * outbound holds sending flags for PR-SCTP. */
	uint8_t state_flags;
	uint8_t chunk_was_revoked;
	uint8_t fwd_tsn_cnt;
};

TAILQ_HEAD(sctpchunk_listhead, sctp_tmit_chunk);

/* The lower byte is used to enumerate PR_SCTP policies */
#define CHUNK_FLAGS_PR_SCTP_TTL	        SCTP_PR_SCTP_TTL
#define CHUNK_FLAGS_PR_SCTP_BUF	        SCTP_PR_SCTP_BUF
#define CHUNK_FLAGS_PR_SCTP_RTX         SCTP_PR_SCTP_RTX

/* The upper byte is used as a bit mask */
#define CHUNK_FLAGS_FRAGMENT_OK	        0x0100

struct chk_id {
	uint8_t id;
	uint8_t can_take_data;
};


struct sctp_tmit_chunk {
	union {
		struct sctp_data_chunkrec data;
		struct chk_id chunk_id;
	}     rec;
	struct sctp_association *asoc;	/* bp to asoc this belongs to */
	struct timeval sent_rcv_time;	/* filled in if RTT being calculated */
	struct mbuf *data;	/* pointer to mbuf chain of data */
	struct mbuf *last_mbuf;	/* pointer to last mbuf in chain */
	struct sctp_nets *whoTo;
	          TAILQ_ENTRY(sctp_tmit_chunk) sctp_next;	/* next link */
	int32_t sent;		/* the send status */
	uint16_t snd_count;	/* number of times I sent */
	uint16_t flags;		/* flags, such as FRAGMENT_OK */
	uint16_t send_size;
	uint16_t book_size;
	uint16_t mbcnt;
	uint16_t auth_keyid;
	uint8_t holds_key_ref;	/* flag if auth keyid refcount is held */
	uint8_t pad_inplace;
	uint8_t do_rtt;
	uint8_t book_size_scale;
	uint8_t no_fr_allowed;
	uint8_t copy_by_ref;
	uint8_t window_probe;
};

struct sctp_queued_to_read {	/* sinfo structure Pluse more */
	uint16_t sinfo_stream;	/* off the wire */
	uint16_t sinfo_flags;	/* SCTP_UNORDERED from wire use SCTP_EOF for
				 * EOR */
	uint32_t sinfo_ppid;	/* off the wire */
	uint32_t sinfo_context;	/* pick this up from assoc def context? */
	uint32_t sinfo_timetolive;	/* not used by kernel */
	uint32_t sinfo_tsn;	/* Use this in reassembly as first TSN */
	uint32_t sinfo_cumtsn;	/* Use this in reassembly as last TSN */
	sctp_assoc_t sinfo_assoc_id;	/* our assoc id */
	/* Non sinfo stuff */
	uint32_t mid;		/* Fragment Index */
	uint32_t length;	/* length of data */
	uint32_t held_length;	/* length held in sb */
	uint32_t top_fsn;	/* Highest FSN in queue */
	uint32_t fsn_included;	/* Highest FSN in *data portion */
	struct sctp_nets *whoFrom;	/* where it came from */
	struct mbuf *data;	/* front of the mbuf chain of data with
				 * PKT_HDR */
	struct mbuf *tail_mbuf;	/* used for multi-part data */
	struct mbuf *aux_data;	/* used to hold/cache  control if o/s does not
				 * take it from us */
	struct sctp_tcb *stcb;	/* assoc, used for window update */
	         TAILQ_ENTRY(sctp_queued_to_read) next;
	         TAILQ_ENTRY(sctp_queued_to_read) next_instrm;
	struct sctpchunk_listhead reasm;
	uint16_t port_from;
	uint16_t spec_flags;	/* Flags to hold the notification field */
	uint8_t do_not_ref_stcb;
	uint8_t end_added;
	uint8_t pdapi_aborted;
	uint8_t pdapi_started;
	uint8_t some_taken;
	uint8_t last_frag_seen;
	uint8_t first_frag_seen;
	uint8_t on_read_q;
	uint8_t on_strm_q;
};

#define SCTP_ON_ORDERED 1
#define SCTP_ON_UNORDERED 2

/* This data structure will be on the outbound
 * stream queues. Data will be pulled off from
 * the front of the mbuf data and chunk-ified
 * by the output routines. We will custom
 * fit every chunk we pull to the send/sent
 * queue to make up the next full packet
 * if we can. An entry cannot be removed
 * from the stream_out queue until
 * the msg_is_complete flag is set. This
 * means at times data/tail_mbuf MIGHT
 * be NULL.. If that occurs it happens
 * for one of two reasons. Either the user
 * is blocked on a send() call and has not
 * awoken to copy more data down... OR
 * the user is in the explict MSG_EOR mode
 * and wrote some data, but has not completed
 * sending.
 */
struct sctp_stream_queue_pending {
	struct mbuf *data;
	struct mbuf *tail_mbuf;
	struct timeval ts;
	struct sctp_nets *net;
	          TAILQ_ENTRY(sctp_stream_queue_pending) next;
	          TAILQ_ENTRY(sctp_stream_queue_pending) ss_next;
	uint32_t fsn;
	uint32_t length;
	uint32_t timetolive;
	uint32_t ppid;
	uint32_t context;
	uint16_t sinfo_flags;
	uint16_t sid;
	uint16_t act_flags;
	uint16_t auth_keyid;
	uint8_t holds_key_ref;
	uint8_t msg_is_complete;
	uint8_t some_taken;
	uint8_t sender_all_done;
	uint8_t put_last_out;
	uint8_t discard_rest;
};

/*
 * this struct contains info that is used to track inbound stream data and
 * help with ordering.
 */
TAILQ_HEAD(sctpwheelunrel_listhead, sctp_stream_in);
struct sctp_stream_in {
	struct sctp_readhead inqueue;
	struct sctp_readhead uno_inqueue;
	uint32_t last_mid_delivered;	/* used for re-order */
	uint16_t sid;
	uint8_t delivery_started;
	uint8_t pd_api_started;
};

TAILQ_HEAD(sctpwheel_listhead, sctp_stream_out);
TAILQ_HEAD(sctplist_listhead, sctp_stream_queue_pending);


/* Round-robin schedulers */
struct ss_rr {
	/* next link in wheel */
	TAILQ_ENTRY(sctp_stream_out) next_spoke;
};

/* Priority scheduler */
struct ss_prio {
	/* next link in wheel */
	TAILQ_ENTRY(sctp_stream_out) next_spoke;
	/* priority id */
	uint16_t priority;
};

/* Fair Bandwidth scheduler */
struct ss_fb {
	/* next link in wheel */
	TAILQ_ENTRY(sctp_stream_out) next_spoke;
	/* stores message size */
	int32_t rounds;
};

/*
 * This union holds all data necessary for
 * different stream schedulers.
 */
struct scheduling_data {
	struct sctp_stream_out *locked_on_sending;
	/* circular looking for output selection */
	struct sctp_stream_out *last_out_stream;
	union {
		struct sctpwheel_listhead wheel;
		struct sctplist_listhead list;
	}     out;
};

/*
 * This union holds all parameters per stream
 * necessary for different stream schedulers.
 */
union scheduling_parameters {
	struct ss_rr rr;
	struct ss_prio prio;
	struct ss_fb fb;
};

/* States for outgoing streams */
#define SCTP_STREAM_CLOSED           0x00
#define SCTP_STREAM_OPENING          0x01
#define SCTP_STREAM_OPEN             0x02
#define SCTP_STREAM_RESET_PENDING    0x03
#define SCTP_STREAM_RESET_IN_FLIGHT  0x04

#define SCTP_MAX_STREAMS_AT_ONCE_RESET 200

/* This struct is used to track the traffic on outbound streams */
struct sctp_stream_out {
	struct sctp_streamhead outqueue;
	union scheduling_parameters ss_params;
	uint32_t chunks_on_queues;	/* send queue and sent queue */
#if defined(SCTP_DETAILED_STR_STATS)
	uint32_t abandoned_unsent[SCTP_PR_SCTP_MAX + 1];
	uint32_t abandoned_sent[SCTP_PR_SCTP_MAX + 1];
#else
	/* Only the aggregation */
	uint32_t abandoned_unsent[1];
	uint32_t abandoned_sent[1];
#endif
	/*
	 * For associations using DATA chunks, the lower 16-bit of
	 * next_mid_ordered are used as the next SSN.
	 */
	uint32_t next_mid_ordered;
	uint32_t next_mid_unordered;
	uint16_t sid;
	uint8_t last_msg_incomplete;
	uint8_t state;
};

/* used to keep track of the addresses yet to try to add/delete */
TAILQ_HEAD(sctp_asconf_addrhead, sctp_asconf_addr);
struct sctp_asconf_addr {
	TAILQ_ENTRY(sctp_asconf_addr) next;
	struct sctp_asconf_addr_param ap;
	struct sctp_ifa *ifa;	/* save the ifa for add/del ip */
	uint8_t sent;		/* has this been sent yet? */
	uint8_t special_del;	/* not to be used in lookup */
};

struct sctp_scoping {
	uint8_t ipv4_addr_legal;
	uint8_t ipv6_addr_legal;
	uint8_t loopback_scope;
	uint8_t ipv4_local_scope;
	uint8_t local_scope;
	uint8_t site_scope;
};

#define SCTP_TSN_LOG_SIZE 40

struct sctp_tsn_log {
	void *stcb;
	uint32_t tsn;
	uint32_t seq;
	uint16_t strm;
	uint16_t sz;
	uint16_t flgs;
	uint16_t in_pos;
	uint16_t in_out;
	uint16_t resv;
};

#define SCTP_FS_SPEC_LOG_SIZE 200
struct sctp_fs_spec_log {
	uint32_t sent;
	uint32_t total_flight;
	uint32_t tsn;
	uint16_t book;
	uint8_t incr;
	uint8_t decr;
};

/* This struct is here to cut out the compatiabilty
 * pad that bulks up both the inp and stcb. The non
 * pad portion MUST stay in complete sync with
 * sctp_sndrcvinfo... i.e. if sinfo_xxxx is added
 * this must be done here too.
 */
struct sctp_nonpad_sndrcvinfo {
	uint16_t sinfo_stream;
	uint16_t sinfo_ssn;
	uint16_t sinfo_flags;
	uint32_t sinfo_ppid;
	uint32_t sinfo_context;
	uint32_t sinfo_timetolive;
	uint32_t sinfo_tsn;
	uint32_t sinfo_cumtsn;
	sctp_assoc_t sinfo_assoc_id;
	uint16_t sinfo_keynumber;
	uint16_t sinfo_keynumber_valid;
};

/*
 * JRS - Structure to hold function pointers to the functions responsible
 * for congestion control.
 */

struct sctp_cc_functions {
	void (*sctp_set_initial_cc_param) (struct sctp_tcb *stcb, struct sctp_nets *net);
	void (*sctp_cwnd_update_after_sack) (struct sctp_tcb *stcb,
	    struct sctp_association *asoc,
	    int accum_moved, int reneged_all, int will_exit);
	void (*sctp_cwnd_update_exit_pf) (struct sctp_tcb *stcb, struct sctp_nets *net);
	void (*sctp_cwnd_update_after_fr) (struct sctp_tcb *stcb,
	    struct sctp_association *asoc);
	void (*sctp_cwnd_update_after_timeout) (struct sctp_tcb *stcb,
	    struct sctp_nets *net);
	void (*sctp_cwnd_update_after_ecn_echo) (struct sctp_tcb *stcb,
	    struct sctp_nets *net, int in_window, int num_pkt_lost);
	void (*sctp_cwnd_update_after_packet_dropped) (struct sctp_tcb *stcb,
	    struct sctp_nets *net, struct sctp_pktdrop_chunk *cp,
	    uint32_t *bottle_bw, uint32_t *on_queue);
	void (*sctp_cwnd_update_after_output) (struct sctp_tcb *stcb,
	    struct sctp_nets *net, int burst_limit);
	void (*sctp_cwnd_update_packet_transmitted) (struct sctp_tcb *stcb,
	    struct sctp_nets *net);
	void (*sctp_cwnd_update_tsn_acknowledged) (struct sctp_nets *net,
	    struct sctp_tmit_chunk *);
	void (*sctp_cwnd_new_transmission_begins) (struct sctp_tcb *stcb,
	    struct sctp_nets *net);
	void (*sctp_cwnd_prepare_net_for_sack) (struct sctp_tcb *stcb,
	    struct sctp_nets *net);
	int (*sctp_cwnd_socket_option) (struct sctp_tcb *stcb, int set, struct sctp_cc_option *);
	void (*sctp_rtt_calculated) (struct sctp_tcb *, struct sctp_nets *, struct timeval *);
};

/*
 * RS - Structure to hold function pointers to the functions responsible
 * for stream scheduling.
 */
struct sctp_ss_functions {
	void (*sctp_ss_init) (struct sctp_tcb *stcb, struct sctp_association *asoc,
	    int holds_lock);
	void (*sctp_ss_clear) (struct sctp_tcb *stcb, struct sctp_association *asoc,
	    int clear_values, int holds_lock);
	void (*sctp_ss_init_stream) (struct sctp_tcb *stcb, struct sctp_stream_out *strq, struct sctp_stream_out *with_strq);
	void (*sctp_ss_add_to_stream) (struct sctp_tcb *stcb, struct sctp_association *asoc,
	    struct sctp_stream_out *strq, struct sctp_stream_queue_pending *sp, int holds_lock);
	int (*sctp_ss_is_empty) (struct sctp_tcb *stcb, struct sctp_association *asoc);
	void (*sctp_ss_remove_from_stream) (struct sctp_tcb *stcb, struct sctp_association *asoc,
	    struct sctp_stream_out *strq, struct sctp_stream_queue_pending *sp, int holds_lock);
struct sctp_stream_out *(*sctp_ss_select_stream) (struct sctp_tcb *stcb,
	    struct sctp_nets *net, struct sctp_association *asoc);
	void (*sctp_ss_scheduled) (struct sctp_tcb *stcb, struct sctp_nets *net,
	    struct sctp_association *asoc, struct sctp_stream_out *strq, int moved_how_much);
	void (*sctp_ss_packet_done) (struct sctp_tcb *stcb, struct sctp_nets *net,
	    struct sctp_association *asoc);
	int (*sctp_ss_get_value) (struct sctp_tcb *stcb, struct sctp_association *asoc,
	    struct sctp_stream_out *strq, uint16_t *value);
	int (*sctp_ss_set_value) (struct sctp_tcb *stcb, struct sctp_association *asoc,
	    struct sctp_stream_out *strq, uint16_t value);
	int (*sctp_ss_is_user_msgs_incomplete) (struct sctp_tcb *stcb, struct sctp_association *asoc);
};

/* used to save ASCONF chunks for retransmission */
TAILQ_HEAD(sctp_asconf_head, sctp_asconf);
struct sctp_asconf {
	TAILQ_ENTRY(sctp_asconf) next;
	uint32_t serial_number;
	uint16_t snd_count;
	struct mbuf *data;
	uint16_t len;
};

/* used to save ASCONF-ACK chunks for retransmission */
TAILQ_HEAD(sctp_asconf_ackhead, sctp_asconf_ack);
struct sctp_asconf_ack {
	TAILQ_ENTRY(sctp_asconf_ack) next;
	uint32_t serial_number;
	struct sctp_nets *last_sent_to;
	struct mbuf *data;
	uint16_t len;
};

/*
 * Here we have information about each individual association that we track.
 * We probably in production would be more dynamic. But for ease of
 * implementation we will have a fixed array that we hunt for in a linear
 * fashion.
 */
struct sctp_association {
	/* association state */
	int state;

	/* queue of pending addrs to add/delete */
	struct sctp_asconf_addrhead asconf_queue;

	struct timeval time_entered;	/* time we entered state */
	struct timeval time_last_rcvd;
	struct timeval time_last_sent;
	struct timeval time_last_sat_advance;
	struct sctp_nonpad_sndrcvinfo def_send;

	/* timers and such */
	struct sctp_timer dack_timer;	/* Delayed ack timer */
	struct sctp_timer asconf_timer;	/* asconf */
	struct sctp_timer strreset_timer;	/* stream reset */
	struct sctp_timer shut_guard_timer;	/* shutdown guard */
	struct sctp_timer autoclose_timer;	/* automatic close timer */
	struct sctp_timer delayed_event_timer;	/* timer for delayed events */
	struct sctp_timer delete_prim_timer;	/* deleting primary dst */

	/* list of restricted local addresses */
	struct sctpladdr sctp_restricted_addrs;

	/* last local address pending deletion (waiting for an address add) */
	struct sctp_ifa *asconf_addr_del_pending;
	/* Deleted primary destination (used to stop timer) */
	struct sctp_nets *deleted_primary;

	struct sctpnetlisthead nets;	/* remote address list */

	/* Free chunk list */
	struct sctpchunk_listhead free_chunks;

	/* Control chunk queue */
	struct sctpchunk_listhead control_send_queue;

	/* ASCONF chunk queue */
	struct sctpchunk_listhead asconf_send_queue;

	/*
	 * Once a TSN hits the wire it is moved to the sent_queue. We
	 * maintain two counts here (don't know if any but retran_cnt is
	 * needed). The idea is that the sent_queue_retran_cnt reflects how
	 * many chunks have been marked for retranmission by either T3-rxt
	 * or FR.
	 */
	struct sctpchunk_listhead sent_queue;
	struct sctpchunk_listhead send_queue;

	/* Scheduling queues */
	struct scheduling_data ss_data;

	/* If an iterator is looking at me, this is it */
	struct sctp_iterator *stcb_starting_point_for_iterator;

	/* ASCONF save the last ASCONF-ACK so we can resend it if necessary */
	struct sctp_asconf_ackhead asconf_ack_sent;

	/*
	 * pointer to last stream reset queued to control queue by us with
	 * requests.
	 */
	struct sctp_tmit_chunk *str_reset;
	/*
	 * if Source Address Selection happening, this will rotate through
	 * the link list.
	 */
	struct sctp_laddr *last_used_address;

	/* stream arrays */
	struct sctp_stream_in *strmin;
	struct sctp_stream_out *strmout;
	uint8_t *mapping_array;
	/* primary destination to use */
	struct sctp_nets *primary_destination;
	struct sctp_nets *alternate;	/* If primary is down or PF */
	/* For CMT */
	struct sctp_nets *last_net_cmt_send_started;
	/* last place I got a data chunk from */
	struct sctp_nets *last_data_chunk_from;
	/* last place I got a control from */
	struct sctp_nets *last_control_chunk_from;


	/*
	 * wait to the point the cum-ack passes req->send_reset_at_tsn for
	 * any req on the list.
	 */
	struct sctp_resethead resetHead;

	/* queue of chunks waiting to be sent into the local stack */
	struct sctp_readhead pending_reply_queue;

	/* JRS - the congestion control functions are in this struct */
	struct sctp_cc_functions cc_functions;
	/*
	 * JRS - value to store the currently loaded congestion control
	 * module
	 */
	uint32_t congestion_control_module;
	/* RS - the stream scheduling functions are in this struct */
	struct sctp_ss_functions ss_functions;
	/* RS - value to store the currently loaded stream scheduling module */
	uint32_t stream_scheduling_module;

	uint32_t vrf_id;
	uint32_t cookie_preserve_req;
	/* ASCONF next seq I am sending out, inits at init-tsn */
	uint32_t asconf_seq_out;
	uint32_t asconf_seq_out_acked;
	/* ASCONF last received ASCONF from peer, starts at peer's TSN-1 */
	uint32_t asconf_seq_in;

	/* next seq I am sending in str reset messages */
	uint32_t str_reset_seq_out;
	/* next seq I am expecting in str reset messages */
	uint32_t str_reset_seq_in;

	/* various verification tag information */
	uint32_t my_vtag;	/* The tag to be used. if assoc is re-initited
				 * by remote end, and I have unlocked this
				 * will be regenerated to a new random value. */
	uint32_t peer_vtag;	/* The peers last tag */

	uint32_t my_vtag_nonce;
	uint32_t peer_vtag_nonce;

	uint32_t assoc_id;

	/* This is the SCTP fragmentation threshold */
	uint32_t smallest_mtu;

	/*
	 * Special hook for Fast retransmit, allows us to track the highest
	 * TSN that is NEW in this SACK if gap ack blocks are present.
	 */
	uint32_t this_sack_highest_gap;

	/*
	 * The highest consecutive TSN that has been acked by peer on my
	 * sends
	 */
	uint32_t last_acked_seq;

	/* The next TSN that I will use in sending. */
	uint32_t sending_seq;

	/* Original seq number I used ??questionable to keep?? */
	uint32_t init_seq_number;


	/* The Advanced Peer Ack Point, as required by the PR-SCTP */
	/* (A1 in Section 4.2) */
	uint32_t advanced_peer_ack_point;

	/*
	 * The highest consequetive TSN at the bottom of the mapping array
	 * (for his sends).
	 */
	uint32_t cumulative_tsn;
	/*
	 * Used to track the mapping array and its offset bits. This MAY be
	 * lower then cumulative_tsn.
	 */
	uint32_t mapping_array_base_tsn;
	/*
	 * used to track highest TSN we have received and is listed in the
	 * mapping array.
	 */
	uint32_t highest_tsn_inside_map;

	/* EY - new NR variables used for nr_sack based on mapping_array */
	uint8_t *nr_mapping_array;
	uint32_t highest_tsn_inside_nr_map;

	uint32_t fast_recovery_tsn;
	uint32_t sat_t3_recovery_tsn;
	uint32_t tsn_last_delivered;
	/*
	 * For the pd-api we should re-write this a bit more efficient. We
	 * could have multiple sctp_queued_to_read's that we are building at
	 * once. Now we only do this when we get ready to deliver to the
	 * socket buffer. Note that we depend on the fact that the struct is
	 * "stuck" on the read queue until we finish all the pd-api.
	 */
	struct sctp_queued_to_read *control_pdapi;

	uint32_t tsn_of_pdapi_last_delivered;
	uint32_t pdapi_ppid;
	uint32_t context;
	uint32_t last_reset_action[SCTP_MAX_RESET_PARAMS];
	uint32_t last_sending_seq[SCTP_MAX_RESET_PARAMS];
	uint32_t last_base_tsnsent[SCTP_MAX_RESET_PARAMS];
#ifdef SCTP_ASOCLOG_OF_TSNS
	/*
	 * special log  - This adds considerable size to the asoc, but
	 * provides a log that you can use to detect problems via kgdb.
	 */
	struct sctp_tsn_log in_tsnlog[SCTP_TSN_LOG_SIZE];
	struct sctp_tsn_log out_tsnlog[SCTP_TSN_LOG_SIZE];
	uint32_t cumack_log[SCTP_TSN_LOG_SIZE];
	uint32_t cumack_logsnt[SCTP_TSN_LOG_SIZE];
	uint16_t tsn_in_at;
	uint16_t tsn_out_at;
	uint16_t tsn_in_wrapped;
	uint16_t tsn_out_wrapped;
	uint16_t cumack_log_at;
	uint16_t cumack_log_atsnt;
#endif				/* SCTP_ASOCLOG_OF_TSNS */
#ifdef SCTP_FS_SPEC_LOG
	struct sctp_fs_spec_log fslog[SCTP_FS_SPEC_LOG_SIZE];
	uint16_t fs_index;
#endif

	/*
	 * window state information and smallest MTU that I use to bound
	 * segmentation
	 */
	uint32_t peers_rwnd;
	uint32_t my_rwnd;
	uint32_t my_last_reported_rwnd;
	uint32_t sctp_frag_point;

	uint32_t total_output_queue_size;

	uint32_t sb_cc;		/* shadow of sb_cc */
	uint32_t sb_send_resv;	/* amount reserved on a send */
	uint32_t my_rwnd_control_len;	/* shadow of sb_mbcnt used for rwnd
					 * control */
#ifdef INET6
	uint32_t default_flowlabel;
#endif
	uint32_t pr_sctp_cnt;
	int ctrl_queue_cnt;	/* could be removed  REM - NO IT CAN'T!! RRS */
	/*
	 * All outbound datagrams queue into this list from the individual
	 * stream queue. Here they get assigned a TSN and then await
	 * sending. The stream seq comes when it is first put in the
	 * individual str queue
	 */
	unsigned int stream_queue_cnt;
	unsigned int send_queue_cnt;
	unsigned int sent_queue_cnt;
	unsigned int sent_queue_cnt_removeable;
	/*
	 * Number on sent queue that are marked for retran until this value
	 * is 0 we only send one packet of retran'ed data.
	 */
	unsigned int sent_queue_retran_cnt;

	unsigned int size_on_reasm_queue;
	unsigned int cnt_on_reasm_queue;
	unsigned int fwd_tsn_cnt;
	/* amount of data (bytes) currently in flight (on all destinations) */
	unsigned int total_flight;
	/* Total book size in flight */
	unsigned int total_flight_count;	/* count of chunks used with
						 * book total */
	/* count of destinaton nets and list of destination nets */
	unsigned int numnets;

	/* Total error count on this association */
	unsigned int overall_error_count;

	unsigned int cnt_msg_on_sb;

	/* All stream count of chunks for delivery */
	unsigned int size_on_all_streams;
	unsigned int cnt_on_all_streams;

	/* Heart Beat delay in ms */
	uint32_t heart_beat_delay;

	/* autoclose */
	unsigned int sctp_autoclose_ticks;

	/* how many preopen streams we have */
	unsigned int pre_open_streams;

	/* How many streams I support coming into me */
	unsigned int max_inbound_streams;

	/* the cookie life I award for any cookie, in seconds */
	unsigned int cookie_life;
	/* time to delay acks for */
	unsigned int delayed_ack;
	unsigned int old_delayed_ack;
	unsigned int sack_freq;
	unsigned int data_pkts_seen;

	unsigned int numduptsns;
	int dup_tsns[SCTP_MAX_DUP_TSNS];
	unsigned int initial_init_rto_max;	/* initial RTO for INIT's */
	unsigned int initial_rto;	/* initial send RTO */
	unsigned int minrto;	/* per assoc RTO-MIN */
	unsigned int maxrto;	/* per assoc RTO-MAX */

	/* authentication fields */
	sctp_auth_chklist_t *local_auth_chunks;
	sctp_auth_chklist_t *peer_auth_chunks;
	sctp_hmaclist_t *local_hmacs;	/* local HMACs supported */
	sctp_hmaclist_t *peer_hmacs;	/* peer HMACs supported */
	struct sctp_keyhead shared_keys;	/* assoc's shared keys */
	sctp_authinfo_t authinfo;	/* randoms, cached keys */
	/*
	 * refcnt to block freeing when a sender or receiver is off coping
	 * user data in.
	 */
	uint32_t refcnt;
	uint32_t chunks_on_out_queue;	/* total chunks floating around,
					 * locked by send socket buffer */
	uint32_t peers_adaptation;
	uint32_t default_mtu;
	uint16_t peer_hmac_id;	/* peer HMAC id to send */

	/*
	 * Being that we have no bag to collect stale cookies, and that we
	 * really would not want to anyway.. we will count them in this
	 * counter. We of course feed them to the pigeons right away (I have
	 * always thought of pigeons as flying rats).
	 */
	uint16_t stale_cookie_count;

	/*
	 * For the partial delivery API, if up, invoked this is what last
	 * TSN I delivered
	 */
	uint16_t str_of_pdapi;
	uint16_t ssn_of_pdapi;

	/* counts of actual built streams. Allocation may be more however */
	/* could re-arrange to optimize space here. */
	uint16_t streamincnt;
	uint16_t streamoutcnt;
	uint16_t strm_realoutsize;
	uint16_t strm_pending_add_size;
	/* my maximum number of retrans of INIT and SEND */
	/* copied from SCTP but should be individually setable */
	uint16_t max_init_times;
	uint16_t max_send_times;

	uint16_t def_net_failure;

	uint16_t def_net_pf_threshold;

	/*
	 * lock flag: 0 is ok to send, 1+ (duals as a retran count) is
	 * awaiting ACK
	 */
	uint16_t mapping_array_size;

	uint16_t last_strm_seq_delivered;
	uint16_t last_strm_no_delivered;

	uint16_t last_revoke_count;
	int16_t num_send_timers_up;

	uint16_t stream_locked_on;
	uint16_t ecn_echo_cnt_onq;

	uint16_t free_chunk_cnt;
	uint8_t stream_locked;
	uint8_t authenticated;	/* packet authenticated ok */
	/*
	 * This flag indicates that a SACK need to be sent. Initially this
	 * is 1 to send the first sACK immediately.
	 */
	uint8_t send_sack;

	/* max burst of new packets into the network */
	uint32_t max_burst;
	/* max burst of fast retransmit packets */
	uint32_t fr_max_burst;

	uint8_t sat_network;	/* RTT is in range of sat net or greater */
	uint8_t sat_network_lockout;	/* lockout code */
	uint8_t burst_limit_applied;	/* Burst limit in effect at last send? */
	/* flag goes on when we are doing a partial delivery api */
	uint8_t hb_random_values[4];
	uint8_t fragmented_delivery_inprogress;
	uint8_t fragment_flags;
	uint8_t last_flags_delivered;
	uint8_t hb_ect_randombit;
	uint8_t hb_random_idx;
	uint8_t default_dscp;
	uint8_t asconf_del_pending;	/* asconf delete last addr pending */
	uint8_t trigger_reset;
	/*
	 * This value, plus all other ack'd but above cum-ack is added
	 * together to cross check against the bit that we have yet to
	 * define (probably in the SACK). When the cum-ack is updated, this
	 * sum is updated as well.
	 */

	/* Flags whether an extension is supported or not */
	uint8_t ecn_supported;
	uint8_t prsctp_supported;
	uint8_t auth_supported;
	uint8_t asconf_supported;
	uint8_t reconfig_supported;
	uint8_t nrsack_supported;
	uint8_t pktdrop_supported;
	uint8_t idata_supported;

	/* Did the peer make the stream config (add out) request */
	uint8_t peer_req_out;

	uint8_t local_strreset_support;
	uint8_t peer_supports_nat;

	struct sctp_scoping scope;
	/* flags to handle send alternate net tracking */
	uint8_t used_alt_asconfack;
	uint8_t fast_retran_loss_recovery;
	uint8_t sat_t3_loss_recovery;
	uint8_t dropped_special_cnt;
	uint8_t seen_a_sack_this_pkt;
	uint8_t stream_reset_outstanding;
	uint8_t stream_reset_out_is_outstanding;
	uint8_t delayed_connection;
	uint8_t ifp_had_enobuf;
	uint8_t saw_sack_with_frags;
	uint8_t saw_sack_with_nr_frags;
	uint8_t in_asocid_hash;
	uint8_t assoc_up_sent;
	uint8_t adaptation_needed;
	uint8_t adaptation_sent;
	/* CMT variables */
	uint8_t cmt_dac_pkts_rcvd;
	uint8_t sctp_cmt_on_off;
	uint8_t iam_blocking;
	uint8_t cookie_how[8];
	/* JRS 5/21/07 - CMT PF variable */
	uint8_t sctp_cmt_pf;
	uint8_t use_precise_time;
	uint64_t sctp_features;
	uint32_t max_cwnd;
	uint16_t port;		/* remote UDP encapsulation port */
	/*
	 * The mapping array is used to track out of order sequences above
	 * last_acked_seq. 0 indicates packet missing 1 indicates packet
	 * rec'd. We slide it up every time we raise last_acked_seq and 0
	 * trailing locactions out.  If I get a TSN above the array
	 * mappingArraySz, I discard the datagram and let retransmit happen.
	 */
	uint32_t marked_retrans;
	uint32_t timoinit;
	uint32_t timodata;
	uint32_t timosack;
	uint32_t timoshutdown;
	uint32_t timoheartbeat;
	uint32_t timocookie;
	uint32_t timoshutdownack;
	struct timeval start_time;
	struct timeval discontinuity_time;
	uint64_t abandoned_unsent[SCTP_PR_SCTP_MAX + 1];
	uint64_t abandoned_sent[SCTP_PR_SCTP_MAX + 1];
};

#endif
