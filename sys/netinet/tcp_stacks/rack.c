/*-
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_inet.h"
#include "opt_inet6.h"
#include "opt_ipsec.h"
#include "opt_tcpdebug.h"

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#ifdef TCP_HHOOK
#include <sys/hhook.h>
#endif
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/proc.h>		/* for proc0 declaration */
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#ifdef NETFLIX_STATS
#include <sys/stats.h>
#endif
#include <sys/refcount.h>
#include <sys/queue.h>
#include <sys/smp.h>
#include <sys/kthread.h>
#include <sys/kern_prefetch.h>

#include <vm/uma.h>

#include <net/route.h>
#include <net/vnet.h>

#define TCPSTATES		/* for logging */

#include <netinet/in.h>
#include <netinet/in_kdtrace.h>
#include <netinet/in_pcb.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>	/* required for icmp_var.h */
#include <netinet/icmp_var.h>	/* for ICMP_BANDLIM */
#include <netinet/ip_var.h>
#include <netinet/ip6.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/ip6_var.h>
#include <netinet/tcp.h>
#define	TCPOUTFLAGS
#include <netinet/tcp_fsm.h>
#include <netinet/tcp_log_buf.h>
#include <netinet/tcp_seq.h>
#include <netinet/tcp_timer.h>
#include <netinet/tcp_var.h>
#include <netinet/tcp_hpts.h>
#include <netinet/tcpip.h>
#include <netinet/cc/cc.h>
#ifdef NETFLIX_CWV
#include <netinet/tcp_newcwv.h>
#endif
#include <netinet/tcp_fastopen.h>
#ifdef TCPDEBUG
#include <netinet/tcp_debug.h>
#endif				/* TCPDEBUG */
#ifdef TCP_OFFLOAD
#include <netinet/tcp_offload.h>
#endif
#ifdef INET6
#include <netinet6/tcp6_var.h>
#endif

#include <netipsec/ipsec_support.h>

#if defined(IPSEC) || defined(IPSEC_SUPPORT)
#include <netipsec/ipsec.h>
#include <netipsec/ipsec6.h>
#endif				/* IPSEC */

#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <machine/in_cksum.h>

#ifdef MAC
#include <security/mac/mac_framework.h>
#endif
#include "sack_filter.h"
#include "tcp_rack.h"
#include "rack_bbr_common.h"

uma_zone_t rack_zone;
uma_zone_t rack_pcb_zone;

#ifndef TICKS2SBT
#define	TICKS2SBT(__t)	(tick_sbt * ((sbintime_t)(__t)))
#endif

struct sysctl_ctx_list rack_sysctl_ctx;
struct sysctl_oid *rack_sysctl_root;

#define CUM_ACKED 1
#define SACKED 2

/*
 * The RACK module incorporates a number of
 * TCP ideas that have been put out into the IETF
 * over the last few years:
 * - Matt Mathis's Rate Halving which slowly drops
 *    the congestion window so that the ack clock can
 *    be maintained during a recovery.
 * - Yuchung Cheng's RACK TCP (for which its named) that
 *    will stop us using the number of dup acks and instead
 *    use time as the gage of when we retransmit.
 * - Reorder Detection of RFC4737 and the Tail-Loss probe draft
 *    of Dukkipati et.al.
 * RACK depends on SACK, so if an endpoint arrives that
 * cannot do SACK the state machine below will shuttle the
 * connection back to using the "default" TCP stack that is
 * in FreeBSD.
 *
 * To implement RACK the original TCP stack was first decomposed
 * into a functional state machine with individual states
 * for each of the possible TCP connection states. The do_segement
 * functions role in life is to mandate the connection supports SACK
 * initially and then assure that the RACK state matches the conenction
 * state before calling the states do_segment function. Each
 * state is simplified due to the fact that the original do_segment
 * has been decomposed and we *know* what state we are in (no
 * switches on the state) and all tests for SACK are gone. This
 * greatly simplifies what each state does.
 *
 * TCP output is also over-written with a new version since it
 * must maintain the new rack scoreboard.
 *
 */
static int32_t rack_precache = 1;
static int32_t rack_tlp_thresh = 1;
static int32_t rack_reorder_thresh = 2;
static int32_t rack_reorder_fade = 60000;	/* 0 - never fade, def 60,000
						 * - 60 seconds */
static int32_t rack_pkt_delay = 1;
static int32_t rack_inc_var = 0;/* For TLP */
static int32_t rack_reduce_largest_on_idle = 0;
static int32_t rack_min_pace_time = 0;
static int32_t rack_min_pace_time_seg_req=6;
static int32_t rack_early_recovery = 1;
static int32_t rack_early_recovery_max_seg = 6;
static int32_t rack_send_a_lot_in_prr = 1;
static int32_t rack_min_to = 1;	/* Number of ms minimum timeout */
static int32_t rack_tlp_in_recovery = 1;	/* Can we do TLP in recovery? */
static int32_t rack_verbose_logging = 0;
static int32_t rack_ignore_data_after_close = 1;
/*
 * Currently regular tcp has a rto_min of 30ms
 * the backoff goes 12 times so that ends up
 * being a total of 122.850 seconds before a
 * connection is killed.
 */
static int32_t rack_tlp_min = 10;
static int32_t rack_rto_min = 30;	/* 30ms same as main freebsd */
static int32_t rack_rto_max = 30000;	/* 30 seconds */
static const int32_t rack_free_cache = 2;
static int32_t rack_hptsi_segments = 40;
static int32_t rack_rate_sample_method = USE_RTT_LOW;
static int32_t rack_pace_every_seg = 1;
static int32_t rack_delayed_ack_time = 200;	/* 200ms */
static int32_t rack_slot_reduction = 4;
static int32_t rack_lower_cwnd_at_tlp = 0;
static int32_t rack_use_proportional_reduce = 0;
static int32_t rack_proportional_rate = 10;
static int32_t rack_tlp_max_resend = 2;
static int32_t rack_limited_retran = 0;
static int32_t rack_always_send_oldest = 0;
static int32_t rack_sack_block_limit = 128;
static int32_t rack_use_sack_filter = 1;
static int32_t rack_tlp_threshold_use = TLP_USE_TWO_ONE;

/* Rack specific counters */
counter_u64_t rack_badfr;
counter_u64_t rack_badfr_bytes;
counter_u64_t rack_rtm_prr_retran;
counter_u64_t rack_rtm_prr_newdata;
counter_u64_t rack_timestamp_mismatch;
counter_u64_t rack_reorder_seen;
counter_u64_t rack_paced_segments;
counter_u64_t rack_unpaced_segments;
counter_u64_t rack_saw_enobuf;
counter_u64_t rack_saw_enetunreach;

/* Tail loss probe counters */
counter_u64_t rack_tlp_tot;
counter_u64_t rack_tlp_newdata;
counter_u64_t rack_tlp_retran;
counter_u64_t rack_tlp_retran_bytes;
counter_u64_t rack_tlp_retran_fail;
counter_u64_t rack_to_tot;
counter_u64_t rack_to_arm_rack;
counter_u64_t rack_to_arm_tlp;
counter_u64_t rack_to_alloc;
counter_u64_t rack_to_alloc_hard;
counter_u64_t rack_to_alloc_emerg;

counter_u64_t rack_sack_proc_all;
counter_u64_t rack_sack_proc_short;
counter_u64_t rack_sack_proc_restart;
counter_u64_t rack_runt_sacks;
counter_u64_t rack_used_tlpmethod;
counter_u64_t rack_used_tlpmethod2;
counter_u64_t rack_enter_tlp_calc;
counter_u64_t rack_input_idle_reduces;
counter_u64_t rack_tlp_does_nada;

/* Temp CPU counters */
counter_u64_t rack_find_high;

counter_u64_t rack_progress_drops;
counter_u64_t rack_out_size[TCP_MSS_ACCT_SIZE];
counter_u64_t rack_opts_arry[RACK_OPTS_SIZE];

static void
rack_log_progress_event(struct tcp_rack *rack, struct tcpcb *tp, uint32_t tick,  int event, int line);

static int
rack_process_ack(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, struct tcpopt *to,
    uint32_t tiwin, int32_t tlen, int32_t * ofia, int32_t thflags, int32_t * ret_val);
static int
rack_process_data(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, int32_t drop_hdrlen, int32_t tlen,
    uint32_t tiwin, int32_t thflags, int32_t nxt_pkt);
static void
rack_ack_received(struct tcpcb *tp, struct tcp_rack *rack,
    struct tcphdr *th, uint16_t nsegs, uint16_t type, int32_t recovery);
static struct rack_sendmap *rack_alloc(struct tcp_rack *rack);
static struct rack_sendmap *
rack_check_recovery_mode(struct tcpcb *tp,
    uint32_t tsused);
static void
rack_cong_signal(struct tcpcb *tp, struct tcphdr *th,
    uint32_t type);
static void rack_counter_destroy(void);
static int
rack_ctloutput(struct socket *so, struct sockopt *sopt,
    struct inpcb *inp, struct tcpcb *tp);
static int32_t rack_ctor(void *mem, int32_t size, void *arg, int32_t how);
static void
rack_do_segment(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, int32_t drop_hdrlen, int32_t tlen,
    uint8_t iptos);
static void rack_dtor(void *mem, int32_t size, void *arg);
static void
rack_earlier_retran(struct tcpcb *tp, struct rack_sendmap *rsm,
    uint32_t t, uint32_t cts);
static struct rack_sendmap *
rack_find_high_nonack(struct tcp_rack *rack,
    struct rack_sendmap *rsm);
static struct rack_sendmap *rack_find_lowest_rsm(struct tcp_rack *rack);
static void rack_free(struct tcp_rack *rack, struct rack_sendmap *rsm);
static void rack_fini(struct tcpcb *tp, int32_t tcb_is_purged);
static int
rack_get_sockopt(struct socket *so, struct sockopt *sopt,
    struct inpcb *inp, struct tcpcb *tp, struct tcp_rack *rack);
static int32_t rack_handoff_ok(struct tcpcb *tp);
static int32_t rack_init(struct tcpcb *tp);
static void rack_init_sysctls(void);
static void
rack_log_ack(struct tcpcb *tp, struct tcpopt *to,
    struct tcphdr *th);
static void
rack_log_output(struct tcpcb *tp, struct tcpopt *to, int32_t len,
    uint32_t seq_out, uint8_t th_flags, int32_t err, uint32_t ts,
    uint8_t pass, struct rack_sendmap *hintrsm);
static void
rack_log_sack_passed(struct tcpcb *tp, struct tcp_rack *rack,
    struct rack_sendmap *rsm);
static void rack_log_to_event(struct tcp_rack *rack, int32_t to_num);
static int32_t rack_output(struct tcpcb *tp);
static void
rack_hpts_do_segment(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, int32_t drop_hdrlen, int32_t tlen,
    uint8_t iptos, int32_t nxt_pkt, struct timeval *tv);

static uint32_t
rack_proc_sack_blk(struct tcpcb *tp, struct tcp_rack *rack,
    struct sackblk *sack, struct tcpopt *to, struct rack_sendmap **prsm,
    uint32_t cts);
static void rack_post_recovery(struct tcpcb *tp, struct tcphdr *th);
static void rack_remxt_tmr(struct tcpcb *tp);
static int
rack_set_sockopt(struct socket *so, struct sockopt *sopt,
    struct inpcb *inp, struct tcpcb *tp, struct tcp_rack *rack);
static void rack_set_state(struct tcpcb *tp, struct tcp_rack *rack);
static int32_t rack_stopall(struct tcpcb *tp);
static void
rack_timer_activate(struct tcpcb *tp, uint32_t timer_type,
    uint32_t delta);
static int32_t rack_timer_active(struct tcpcb *tp, uint32_t timer_type);
static void rack_timer_cancel(struct tcpcb *tp, struct tcp_rack *rack, uint32_t cts, int line);
static void rack_timer_stop(struct tcpcb *tp, uint32_t timer_type);
static uint32_t
rack_update_entry(struct tcpcb *tp, struct tcp_rack *rack,
    struct rack_sendmap *rsm, uint32_t ts, int32_t * lenp);
static void
rack_update_rsm(struct tcpcb *tp, struct tcp_rack *rack,
    struct rack_sendmap *rsm, uint32_t ts);
static int
rack_update_rtt(struct tcpcb *tp, struct tcp_rack *rack,
    struct rack_sendmap *rsm, struct tcpopt *to, uint32_t cts, int32_t ack_type);
static int32_t tcp_addrack(module_t mod, int32_t type, void *data);
static void
rack_challenge_ack(struct mbuf *m, struct tcphdr *th,
    struct tcpcb *tp, int32_t * ret_val);
static int
rack_do_close_wait(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen,
    int32_t tlen, uint32_t tiwin, int32_t thflags, int32_t nxt_pkt);
static int
rack_do_closing(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen,
    int32_t tlen, uint32_t tiwin, int32_t thflags, int32_t nxt_pkt);
static void
rack_do_drop(struct mbuf *m, struct tcpcb *tp);
static void
rack_do_dropafterack(struct mbuf *m, struct tcpcb *tp,
    struct tcphdr *th, int32_t thflags, int32_t tlen, int32_t * ret_val);
static void
rack_do_dropwithreset(struct mbuf *m, struct tcpcb *tp,
	struct tcphdr *th, int32_t rstreason, int32_t tlen);
static int
rack_do_established(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen,
    int32_t tlen, uint32_t tiwin, int32_t thflags, int32_t nxt_pkt);
static int
rack_do_fastnewdata(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen,
    int32_t tlen, uint32_t tiwin, int32_t nxt_pkt);
static int
rack_do_fin_wait_1(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen,
    int32_t tlen, uint32_t tiwin, int32_t thflags, int32_t nxt_pkt);
static int
rack_do_fin_wait_2(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen,
    int32_t tlen, uint32_t tiwin, int32_t thflags, int32_t nxt_pkt);
static int
rack_do_lastack(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen,
    int32_t tlen, uint32_t tiwin, int32_t thflags, int32_t nxt_pkt);
static int
rack_do_syn_recv(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen,
    int32_t tlen, uint32_t tiwin, int32_t thflags, int32_t nxt_pkt);
static int
rack_do_syn_sent(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen,
    int32_t tlen, uint32_t tiwin, int32_t thflags, int32_t nxt_pkt);
static int
rack_drop_checks(struct tcpopt *to, struct mbuf *m,
    struct tcphdr *th, struct tcpcb *tp, int32_t * tlenp, int32_t * thf,
    int32_t * drop_hdrlen, int32_t * ret_val);
static int
rack_process_rst(struct mbuf *m, struct tcphdr *th,
    struct socket *so, struct tcpcb *tp);
struct rack_sendmap *
tcp_rack_output(struct tcpcb *tp, struct tcp_rack *rack,
    uint32_t tsused);
static void tcp_rack_xmit_timer(struct tcp_rack *rack, int32_t rtt);
static void
     tcp_rack_partialack(struct tcpcb *tp, struct tcphdr *th);

static int
rack_ts_check(struct mbuf *m, struct tcphdr *th,
    struct tcpcb *tp, int32_t tlen, int32_t thflags, int32_t * ret_val);

int32_t rack_clear_counter=0;


static int
sysctl_rack_clear(SYSCTL_HANDLER_ARGS)
{
	uint32_t stat;
	int32_t error;

	error = SYSCTL_OUT(req, &rack_clear_counter, sizeof(uint32_t));
	if (error || req->newptr == NULL)
		return error;

	error = SYSCTL_IN(req, &stat, sizeof(uint32_t));
	if (error)
		return (error);
	if (stat == 1) {
#ifdef INVARIANTS
		printf("Clearing RACK counters\n");
#endif
		counter_u64_zero(rack_badfr);
		counter_u64_zero(rack_badfr_bytes);
		counter_u64_zero(rack_rtm_prr_retran);
		counter_u64_zero(rack_rtm_prr_newdata);
		counter_u64_zero(rack_timestamp_mismatch);
		counter_u64_zero(rack_reorder_seen);
		counter_u64_zero(rack_tlp_tot);
		counter_u64_zero(rack_tlp_newdata);
		counter_u64_zero(rack_tlp_retran);
		counter_u64_zero(rack_tlp_retran_bytes);
		counter_u64_zero(rack_tlp_retran_fail);
		counter_u64_zero(rack_to_tot);
		counter_u64_zero(rack_to_arm_rack);
		counter_u64_zero(rack_to_arm_tlp);
		counter_u64_zero(rack_paced_segments);
		counter_u64_zero(rack_unpaced_segments);
		counter_u64_zero(rack_saw_enobuf);
		counter_u64_zero(rack_saw_enetunreach);
		counter_u64_zero(rack_to_alloc_hard);
		counter_u64_zero(rack_to_alloc_emerg);
		counter_u64_zero(rack_sack_proc_all);
		counter_u64_zero(rack_sack_proc_short);
		counter_u64_zero(rack_sack_proc_restart);
		counter_u64_zero(rack_to_alloc);
		counter_u64_zero(rack_find_high);
		counter_u64_zero(rack_runt_sacks);
		counter_u64_zero(rack_used_tlpmethod);
		counter_u64_zero(rack_used_tlpmethod2);
		counter_u64_zero(rack_enter_tlp_calc);
		counter_u64_zero(rack_progress_drops);
		counter_u64_zero(rack_tlp_does_nada);
	}
	rack_clear_counter = 0;
	return (0);
}



static void
rack_init_sysctls()
{
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "rate_sample_method", CTLFLAG_RW,
	    &rack_rate_sample_method , USE_RTT_LOW,
	    "What method should we use for rate sampling 0=high, 1=low ");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "data_after_close", CTLFLAG_RW,
	    &rack_ignore_data_after_close, 0,
	    "Do we hold off sending a RST until all pending data is ack'd");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "tlpmethod", CTLFLAG_RW,
	    &rack_tlp_threshold_use, TLP_USE_TWO_ONE,
	    "What method do we do for TLP time calc 0=no-de-ack-comp, 1=ID, 2=2.1, 3=2.2");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "min_pace_time", CTLFLAG_RW,
	    &rack_min_pace_time, 0,
	    "Should we enforce a minimum pace time of 1ms");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "min_pace_segs", CTLFLAG_RW,
	    &rack_min_pace_time_seg_req, 6,
	    "How many segments have to be in the len to enforce min-pace-time");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "idle_reduce_high", CTLFLAG_RW,
	    &rack_reduce_largest_on_idle, 0,
	    "Should we reduce the largest cwnd seen to IW on idle reduction");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "bb_verbose", CTLFLAG_RW,
	    &rack_verbose_logging, 0,
	    "Should RACK black box logging be verbose");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "sackfiltering", CTLFLAG_RW,
	    &rack_use_sack_filter, 1,
	    "Do we use sack filtering?");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "delayed_ack", CTLFLAG_RW,
	    &rack_delayed_ack_time, 200,
	    "Delayed ack time (200ms)");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "tlpminto", CTLFLAG_RW,
	    &rack_tlp_min, 10,
	    "TLP minimum timeout per the specification (10ms)");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "precache", CTLFLAG_RW,
	    &rack_precache, 0,
	    "Where should we precache the mcopy (0 is not at all)");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "sblklimit", CTLFLAG_RW,
	    &rack_sack_block_limit, 128,
	    "When do we start paying attention to small sack blocks");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "send_oldest", CTLFLAG_RW,
	    &rack_always_send_oldest, 1,
	    "Should we always send the oldest TLP and RACK-TLP");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "rack_tlp_in_recovery", CTLFLAG_RW,
	    &rack_tlp_in_recovery, 1,
	    "Can we do a TLP during recovery?");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "rack_tlimit", CTLFLAG_RW,
	    &rack_limited_retran, 0,
	    "How many times can a rack timeout drive out sends");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "minrto", CTLFLAG_RW,
	    &rack_rto_min, 0,
	    "Minimum RTO in ms -- set with caution below 1000 due to TLP");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "maxrto", CTLFLAG_RW,
	    &rack_rto_max, 0,
	    "Maxiumum RTO in ms -- should be at least as large as min_rto");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "tlp_retry", CTLFLAG_RW,
	    &rack_tlp_max_resend, 2,
	    "How many times does TLP retry a single segment or multiple with no ACK");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "recovery_loss_prop", CTLFLAG_RW,
	    &rack_use_proportional_reduce, 0,
	    "Should we proportionaly reduce cwnd based on the number of losses ");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "recovery_prop", CTLFLAG_RW,
	    &rack_proportional_rate, 10,
	    "What percent reduction per loss");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "tlp_cwnd_flag", CTLFLAG_RW,
	    &rack_lower_cwnd_at_tlp, 0,
	    "When a TLP completes a retran should we enter recovery?");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "hptsi_reduces", CTLFLAG_RW,
	    &rack_slot_reduction, 4,
	    "When setting a slot should we reduce by divisor");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "hptsi_every_seg", CTLFLAG_RW,
	    &rack_pace_every_seg, 1,
	    "Should we pace out every segment hptsi");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "hptsi_seg_max", CTLFLAG_RW,
	    &rack_hptsi_segments, 6,
	    "Should we pace out only a limited size of segments");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "prr_sendalot", CTLFLAG_RW,
	    &rack_send_a_lot_in_prr, 1,
	    "Send a lot in prr");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "minto", CTLFLAG_RW,
	    &rack_min_to, 1,
	    "Minimum rack timeout in milliseconds");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "earlyrecoveryseg", CTLFLAG_RW,
	    &rack_early_recovery_max_seg, 6,
	    "Max segments in early recovery");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "earlyrecovery", CTLFLAG_RW,
	    &rack_early_recovery, 1,
	    "Do we do early recovery with rack");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "reorder_thresh", CTLFLAG_RW,
	    &rack_reorder_thresh, 2,
	    "What factor for rack will be added when seeing reordering (shift right)");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "rtt_tlp_thresh", CTLFLAG_RW,
	    &rack_tlp_thresh, 1,
	    "what divisor for TLP rtt/retran will be added (1=rtt, 2=1/2 rtt etc)");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "reorder_fade", CTLFLAG_RW,
	    &rack_reorder_fade, 0,
	    "Does reorder detection fade, if so how many ms (0 means never)");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "pktdelay", CTLFLAG_RW,
	    &rack_pkt_delay, 1,
	    "Extra RACK time (in ms) besides reordering thresh");
	SYSCTL_ADD_S32(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "inc_var", CTLFLAG_RW,
	    &rack_inc_var, 0,
	    "Should rack add to the TLP timer the variance in rtt calculation");
	rack_badfr = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "badfr", CTLFLAG_RD,
	    &rack_badfr, "Total number of bad FRs");
	rack_badfr_bytes = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "badfr_bytes", CTLFLAG_RD,
	    &rack_badfr_bytes, "Total number of bad FRs");
	rack_rtm_prr_retran = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "prrsndret", CTLFLAG_RD,
	    &rack_rtm_prr_retran,
	    "Total number of prr based retransmits");
	rack_rtm_prr_newdata = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "prrsndnew", CTLFLAG_RD,
	    &rack_rtm_prr_newdata,
	    "Total number of prr based new transmits");
	rack_timestamp_mismatch = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "tsnf", CTLFLAG_RD,
	    &rack_timestamp_mismatch,
	    "Total number of timestamps that we could not find the reported ts");
	rack_find_high = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "findhigh", CTLFLAG_RD,
	    &rack_find_high,
	    "Total number of FIN causing find-high");
	rack_reorder_seen = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "reordering", CTLFLAG_RD,
	    &rack_reorder_seen,
	    "Total number of times we added delay due to reordering");
	rack_tlp_tot = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "tlp_to_total", CTLFLAG_RD,
	    &rack_tlp_tot,
	    "Total number of tail loss probe expirations");
	rack_tlp_newdata = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "tlp_new", CTLFLAG_RD,
	    &rack_tlp_newdata,
	    "Total number of tail loss probe sending new data");

	rack_tlp_retran = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "tlp_retran", CTLFLAG_RD,
	    &rack_tlp_retran,
	    "Total number of tail loss probe sending retransmitted data");
	rack_tlp_retran_bytes = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "tlp_retran_bytes", CTLFLAG_RD,
	    &rack_tlp_retran_bytes,
	    "Total bytes of tail loss probe sending retransmitted data");
	rack_tlp_retran_fail = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "tlp_retran_fail", CTLFLAG_RD,
	    &rack_tlp_retran_fail,
	    "Total number of tail loss probe sending retransmitted data that failed (wait for t3)");
	rack_to_tot = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "rack_to_tot", CTLFLAG_RD,
	    &rack_to_tot,
	    "Total number of times the rack to expired?");
	rack_to_arm_rack = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "arm_rack", CTLFLAG_RD,
	    &rack_to_arm_rack,
	    "Total number of times the rack timer armed?");
	rack_to_arm_tlp = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "arm_tlp", CTLFLAG_RD,
	    &rack_to_arm_tlp,
	    "Total number of times the tlp timer armed?");
	rack_paced_segments = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "paced", CTLFLAG_RD,
	    &rack_paced_segments,
	    "Total number of times a segment send caused hptsi");
	rack_unpaced_segments = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "unpaced", CTLFLAG_RD,
	    &rack_unpaced_segments,
	    "Total number of times a segment did not cause hptsi");
	rack_saw_enobuf = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "saw_enobufs", CTLFLAG_RD,
	    &rack_saw_enobuf,
	    "Total number of times a segment did not cause hptsi");
	rack_saw_enetunreach = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "saw_enetunreach", CTLFLAG_RD,
	    &rack_saw_enetunreach,
	    "Total number of times a segment did not cause hptsi");
	rack_to_alloc = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "allocs", CTLFLAG_RD,
	    &rack_to_alloc,
	    "Total allocations of tracking structures");
	rack_to_alloc_hard = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "allochard", CTLFLAG_RD,
	    &rack_to_alloc_hard,
	    "Total allocations done with sleeping the hard way");
	rack_to_alloc_emerg = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "allocemerg", CTLFLAG_RD,
	    &rack_to_alloc_emerg,
	    "Total alocations done from emergency cache");
	rack_sack_proc_all = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "sack_long", CTLFLAG_RD,
	    &rack_sack_proc_all,
	    "Total times we had to walk whole list for sack processing");

	rack_sack_proc_restart = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "sack_restart", CTLFLAG_RD,
	    &rack_sack_proc_restart,
	    "Total times we had to walk whole list due to a restart");
	rack_sack_proc_short = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "sack_short", CTLFLAG_RD,
	    &rack_sack_proc_short,
	    "Total times we took shortcut for sack processing");
	rack_enter_tlp_calc = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "tlp_calc_entered", CTLFLAG_RD,
	    &rack_enter_tlp_calc,
	    "Total times we called calc-tlp");
	rack_used_tlpmethod = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "hit_tlp_method", CTLFLAG_RD,
	    &rack_used_tlpmethod,
	    "Total number of runt sacks");
	rack_used_tlpmethod2 = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "hit_tlp_method2", CTLFLAG_RD,
	    &rack_used_tlpmethod2,
	    "Total number of runt sacks 2");
	rack_runt_sacks = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "runtsacks", CTLFLAG_RD,
	    &rack_runt_sacks,
	    "Total number of runt sacks");
	rack_progress_drops = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "prog_drops", CTLFLAG_RD,
	    &rack_progress_drops,
	    "Total number of progress drops");
	rack_input_idle_reduces = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "idle_reduce_oninput", CTLFLAG_RD,
	    &rack_input_idle_reduces,
	    "Total number of idle reductions on input");
	rack_tlp_does_nada = counter_u64_alloc(M_WAITOK);
	SYSCTL_ADD_COUNTER_U64(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "tlp_nada", CTLFLAG_RD,
	    &rack_tlp_does_nada,
	    "Total number of nada tlp calls");
	COUNTER_ARRAY_ALLOC(rack_out_size, TCP_MSS_ACCT_SIZE, M_WAITOK);
	SYSCTL_ADD_COUNTER_U64_ARRAY(&rack_sysctl_ctx, SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "outsize", CTLFLAG_RD,
	    rack_out_size, TCP_MSS_ACCT_SIZE, "MSS send sizes");
	COUNTER_ARRAY_ALLOC(rack_opts_arry, RACK_OPTS_SIZE, M_WAITOK);
	SYSCTL_ADD_COUNTER_U64_ARRAY(&rack_sysctl_ctx, SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "opts", CTLFLAG_RD,
	    rack_opts_arry, RACK_OPTS_SIZE, "RACK Option Stats");
	SYSCTL_ADD_PROC(&rack_sysctl_ctx,
	    SYSCTL_CHILDREN(rack_sysctl_root),
	    OID_AUTO, "clear", CTLTYPE_UINT | CTLFLAG_RW | CTLFLAG_MPSAFE,
	    &rack_clear_counter, 0, sysctl_rack_clear, "IU", "Clear counters");
}

static inline int32_t
rack_progress_timeout_check(struct tcpcb *tp)
{
	if (tp->t_maxunacktime && tp->t_acktime && TSTMP_GT(ticks, tp->t_acktime)) {
		if ((ticks - tp->t_acktime) >= tp->t_maxunacktime) {
			/*
			 * There is an assumption that the caller
			 * will drop the connection so we will
			 * increment the counters here.
			 */
			struct tcp_rack *rack;
			rack = (struct tcp_rack *)tp->t_fb_ptr;
			counter_u64_add(rack_progress_drops, 1);
#ifdef NETFLIX_STATS
			TCPSTAT_INC(tcps_progdrops);
#endif
			rack_log_progress_event(rack, tp, ticks, PROGRESS_DROP, __LINE__);
			return (1);
		}
	}
	return (0);
}


static void
rack_log_to_start(struct tcp_rack *rack, uint32_t cts, uint32_t to, int32_t slot, uint8_t which)
{
	if (rack->rc_tp->t_logstate != TCP_LOG_STATE_OFF) {
		union tcp_log_stackspecific log;

		memset(&log.u_bbr, 0, sizeof(log.u_bbr));
		log.u_bbr.flex1 = TICKS_2_MSEC(rack->rc_tp->t_srtt >> TCP_RTT_SHIFT);
		log.u_bbr.flex2 = to;
		log.u_bbr.flex3 = rack->r_ctl.rc_hpts_flags;
		log.u_bbr.flex4 = slot;
		log.u_bbr.flex5 = rack->rc_inp->inp_hptsslot;
		log.u_bbr.flex6 = rack->rc_tp->t_rxtcur;
		log.u_bbr.flex8 = which;
		log.u_bbr.inhpts = rack->rc_inp->inp_in_hpts;
		log.u_bbr.ininput = rack->rc_inp->inp_in_input;
		TCP_LOG_EVENT(rack->rc_tp, NULL,
		    &rack->rc_inp->inp_socket->so_rcv,
		    &rack->rc_inp->inp_socket->so_snd,
		    BBR_LOG_TIMERSTAR, 0,
		    0, &log, false);
	}
}

static void
rack_log_to_event(struct tcp_rack *rack, int32_t to_num)
{
	if (rack->rc_tp->t_logstate != TCP_LOG_STATE_OFF) {
		union tcp_log_stackspecific log;

		memset(&log.u_bbr, 0, sizeof(log.u_bbr));
		log.u_bbr.inhpts = rack->rc_inp->inp_in_hpts;
		log.u_bbr.ininput = rack->rc_inp->inp_in_input;
		log.u_bbr.flex8 = to_num;
		log.u_bbr.flex1 = rack->r_ctl.rc_rack_min_rtt;
		log.u_bbr.flex2 = rack->rc_rack_rtt;
		TCP_LOG_EVENT(rack->rc_tp, NULL,
		    &rack->rc_inp->inp_socket->so_rcv,
		    &rack->rc_inp->inp_socket->so_snd,
		    BBR_LOG_RTO, 0,
		    0, &log, false);
	}
}

static void
rack_log_rtt_upd(struct tcpcb *tp, struct tcp_rack *rack, int32_t t,
    uint32_t o_srtt, uint32_t o_var)
{
	if (tp->t_logstate != TCP_LOG_STATE_OFF) {
		union tcp_log_stackspecific log;

		memset(&log.u_bbr, 0, sizeof(log.u_bbr));
		log.u_bbr.inhpts = rack->rc_inp->inp_in_hpts;
		log.u_bbr.ininput = rack->rc_inp->inp_in_input;
		log.u_bbr.flex1 = t;
		log.u_bbr.flex2 = o_srtt;
		log.u_bbr.flex3 = o_var;
		log.u_bbr.flex4 = rack->r_ctl.rack_rs.rs_rtt_lowest;
		log.u_bbr.flex5 = rack->r_ctl.rack_rs.rs_rtt_highest;		
		log.u_bbr.flex6 = rack->r_ctl.rack_rs.rs_rtt_cnt;
		log.u_bbr.rttProp = rack->r_ctl.rack_rs.rs_rtt_tot;
		log.u_bbr.flex8 = rack->r_ctl.rc_rate_sample_method;
		TCP_LOG_EVENT(tp, NULL,
		    &rack->rc_inp->inp_socket->so_rcv,
		    &rack->rc_inp->inp_socket->so_snd,
		    BBR_LOG_BBRRTT, 0,
		    0, &log, false);
	}
}

static void
rack_log_rtt_sample(struct tcp_rack *rack, uint32_t rtt)
{
	/* 
	 * Log the rtt sample we are
	 * applying to the srtt algorithm in
	 * useconds.
	 */
	if (rack->rc_tp->t_logstate != TCP_LOG_STATE_OFF) {
		union tcp_log_stackspecific log;
		struct timeval tv;
		
		/* Convert our ms to a microsecond */
		log.u_bbr.flex1 = rtt * 1000;
		log.u_bbr.timeStamp = tcp_get_usecs(&tv);
		TCP_LOG_EVENTP(rack->rc_tp, NULL,
		    &rack->rc_inp->inp_socket->so_rcv,
		    &rack->rc_inp->inp_socket->so_snd,
		    TCP_LOG_RTT, 0,
		    0, &log, false, &tv);
	}
}


static inline void
rack_log_progress_event(struct tcp_rack *rack, struct tcpcb *tp, uint32_t tick,  int event, int line)
{
	if (rack_verbose_logging && (tp->t_logstate != TCP_LOG_STATE_OFF)) {
		union tcp_log_stackspecific log;

		memset(&log.u_bbr, 0, sizeof(log.u_bbr));
		log.u_bbr.inhpts = rack->rc_inp->inp_in_hpts;
		log.u_bbr.ininput = rack->rc_inp->inp_in_input;
		log.u_bbr.flex1 = line;
		log.u_bbr.flex2 = tick;
		log.u_bbr.flex3 = tp->t_maxunacktime;
		log.u_bbr.flex4 = tp->t_acktime;
		log.u_bbr.flex8 = event;
		TCP_LOG_EVENT(tp, NULL,
		    &rack->rc_inp->inp_socket->so_rcv,
		    &rack->rc_inp->inp_socket->so_snd,
		    BBR_LOG_PROGRESS, 0,
		    0, &log, false);
	}
}

static void
rack_log_type_bbrsnd(struct tcp_rack *rack, uint32_t len, uint32_t slot, uint32_t cts)
{
	if (rack->rc_tp->t_logstate != TCP_LOG_STATE_OFF) {
		union tcp_log_stackspecific log;

		memset(&log.u_bbr, 0, sizeof(log.u_bbr));
		log.u_bbr.inhpts = rack->rc_inp->inp_in_hpts;
		log.u_bbr.ininput = rack->rc_inp->inp_in_input;
		log.u_bbr.flex1 = slot;
		log.u_bbr.flex7 = (0x0000ffff & rack->r_ctl.rc_hpts_flags);
		log.u_bbr.flex8 = rack->rc_in_persist;
		TCP_LOG_EVENT(rack->rc_tp, NULL,
		    &rack->rc_inp->inp_socket->so_rcv,
		    &rack->rc_inp->inp_socket->so_snd,
		    BBR_LOG_BBRSND, 0,
		    0, &log, false);
	}
}

static void
rack_log_doseg_done(struct tcp_rack *rack, uint32_t cts, int32_t nxt_pkt, int32_t did_out, int way_out)
{
	if (rack->rc_tp->t_logstate != TCP_LOG_STATE_OFF) {
		union tcp_log_stackspecific log;
		log.u_bbr.flex1 = did_out;
		log.u_bbr.flex2 = nxt_pkt;
		log.u_bbr.flex3 = way_out;
		log.u_bbr.flex4 = rack->r_ctl.rc_hpts_flags;
		log.u_bbr.flex7 = rack->r_wanted_output;
		log.u_bbr.flex8 = rack->rc_in_persist;
		TCP_LOG_EVENT(rack->rc_tp, NULL,
		    &rack->rc_inp->inp_socket->so_rcv,
		    &rack->rc_inp->inp_socket->so_snd,
		    BBR_LOG_DOSEG_DONE, 0,
		    0, &log, false);
	}
}


static void
rack_log_type_just_return(struct tcp_rack *rack, uint32_t cts, uint32_t tlen, uint32_t slot, uint8_t hpts_calling)
{
	if (rack->rc_tp->t_logstate != TCP_LOG_STATE_OFF) {
		union tcp_log_stackspecific log;

		memset(&log.u_bbr, 0, sizeof(log.u_bbr));
		log.u_bbr.inhpts = rack->rc_inp->inp_in_hpts;
		log.u_bbr.ininput = rack->rc_inp->inp_in_input;
		log.u_bbr.flex1 = slot;
		log.u_bbr.flex2 = rack->r_ctl.rc_hpts_flags;
		log.u_bbr.flex7 = hpts_calling;
		log.u_bbr.flex8 = rack->rc_in_persist;
		TCP_LOG_EVENT(rack->rc_tp, NULL,
		    &rack->rc_inp->inp_socket->so_rcv,
		    &rack->rc_inp->inp_socket->so_snd,
		    BBR_LOG_JUSTRET, 0,
		    tlen, &log, false);
	}
}

static void
rack_log_to_cancel(struct tcp_rack *rack, int32_t hpts_removed, int line)
{
	if (rack->rc_tp->t_logstate != TCP_LOG_STATE_OFF) {
		union tcp_log_stackspecific log;

		memset(&log.u_bbr, 0, sizeof(log.u_bbr));
		log.u_bbr.inhpts = rack->rc_inp->inp_in_hpts;
		log.u_bbr.ininput = rack->rc_inp->inp_in_input;
		log.u_bbr.flex1 = line;
		log.u_bbr.flex2 = 0;
		log.u_bbr.flex3 = rack->r_ctl.rc_hpts_flags;
		log.u_bbr.flex4 = 0;
		log.u_bbr.flex6 = rack->rc_tp->t_rxtcur;
		log.u_bbr.flex8 = hpts_removed;
		TCP_LOG_EVENT(rack->rc_tp, NULL,
		    &rack->rc_inp->inp_socket->so_rcv,
		    &rack->rc_inp->inp_socket->so_snd,
		    BBR_LOG_TIMERCANC, 0,
		    0, &log, false);
	}
}

static void
rack_log_to_processing(struct tcp_rack *rack, uint32_t cts, int32_t ret, int32_t timers)
{
	if (rack->rc_tp->t_logstate != TCP_LOG_STATE_OFF) {
		union tcp_log_stackspecific log;

		memset(&log.u_bbr, 0, sizeof(log.u_bbr));
		log.u_bbr.flex1 = timers;
		log.u_bbr.flex2 = ret;
		log.u_bbr.flex3 = rack->r_ctl.rc_timer_exp;
		log.u_bbr.flex4 = rack->r_ctl.rc_hpts_flags;
		log.u_bbr.flex5 = cts;
		TCP_LOG_EVENT(rack->rc_tp, NULL,
		    &rack->rc_inp->inp_socket->so_rcv,
		    &rack->rc_inp->inp_socket->so_snd,
		    BBR_LOG_TO_PROCESS, 0,
		    0, &log, false);
	}
}

static void
rack_counter_destroy()
{
	counter_u64_free(rack_badfr);
	counter_u64_free(rack_badfr_bytes);
	counter_u64_free(rack_rtm_prr_retran);
	counter_u64_free(rack_rtm_prr_newdata);
	counter_u64_free(rack_timestamp_mismatch);
	counter_u64_free(rack_reorder_seen);
	counter_u64_free(rack_tlp_tot);
	counter_u64_free(rack_tlp_newdata);
	counter_u64_free(rack_tlp_retran);
	counter_u64_free(rack_tlp_retran_bytes);
	counter_u64_free(rack_tlp_retran_fail);
	counter_u64_free(rack_to_tot);
	counter_u64_free(rack_to_arm_rack);
	counter_u64_free(rack_to_arm_tlp);
	counter_u64_free(rack_paced_segments);
	counter_u64_free(rack_unpaced_segments);
	counter_u64_free(rack_saw_enobuf);
	counter_u64_free(rack_saw_enetunreach);
	counter_u64_free(rack_to_alloc_hard);
	counter_u64_free(rack_to_alloc_emerg);
	counter_u64_free(rack_sack_proc_all);
	counter_u64_free(rack_sack_proc_short);
	counter_u64_free(rack_sack_proc_restart);
	counter_u64_free(rack_to_alloc);
	counter_u64_free(rack_find_high);
	counter_u64_free(rack_runt_sacks);
	counter_u64_free(rack_enter_tlp_calc);
	counter_u64_free(rack_used_tlpmethod);
	counter_u64_free(rack_used_tlpmethod2);
	counter_u64_free(rack_progress_drops);
	counter_u64_free(rack_input_idle_reduces);
	counter_u64_free(rack_tlp_does_nada);
	COUNTER_ARRAY_FREE(rack_out_size, TCP_MSS_ACCT_SIZE);
	COUNTER_ARRAY_FREE(rack_opts_arry, RACK_OPTS_SIZE);
}

static struct rack_sendmap *
rack_alloc(struct tcp_rack *rack)
{
	struct rack_sendmap *rsm;

	counter_u64_add(rack_to_alloc, 1);
	rack->r_ctl.rc_num_maps_alloced++;
	rsm = uma_zalloc(rack_zone, M_NOWAIT);
	if (rsm) {
		return (rsm);
	}
	if (rack->rc_free_cnt) {
		counter_u64_add(rack_to_alloc_emerg, 1);
		rsm = TAILQ_FIRST(&rack->r_ctl.rc_free);
		TAILQ_REMOVE(&rack->r_ctl.rc_free, rsm, r_next);
		rack->rc_free_cnt--;
		return (rsm);
	}
	return (NULL);
}

static void
rack_free(struct tcp_rack *rack, struct rack_sendmap *rsm)
{
	rack->r_ctl.rc_num_maps_alloced--;
	if (rack->r_ctl.rc_tlpsend == rsm)
		rack->r_ctl.rc_tlpsend = NULL;
	if (rack->r_ctl.rc_next == rsm)
		rack->r_ctl.rc_next = NULL;
	if (rack->r_ctl.rc_sacklast == rsm)
		rack->r_ctl.rc_sacklast = NULL;
	if (rack->rc_free_cnt < rack_free_cache) {
		memset(rsm, 0, sizeof(struct rack_sendmap));
		TAILQ_INSERT_TAIL(&rack->r_ctl.rc_free, rsm, r_next);
		rack->rc_free_cnt++;
		return;
	}
	uma_zfree(rack_zone, rsm);
}

/*
 * CC wrapper hook functions
 */
static void
rack_ack_received(struct tcpcb *tp, struct tcp_rack *rack, struct tcphdr *th, uint16_t nsegs,
    uint16_t type, int32_t recovery)
{
#ifdef NETFLIX_STATS
	int32_t gput;
#endif
#ifdef NETFLIX_CWV
	u_long old_cwnd = tp->snd_cwnd;
#endif

	INP_WLOCK_ASSERT(tp->t_inpcb);
	tp->ccv->nsegs = nsegs;
	tp->ccv->bytes_this_ack = BYTES_THIS_ACK(tp, th);
	if ((recovery) && (rack->r_ctl.rc_early_recovery_segs)) {
		uint32_t max;

		max = rack->r_ctl.rc_early_recovery_segs * tp->t_maxseg;
		if (tp->ccv->bytes_this_ack > max) {
			tp->ccv->bytes_this_ack = max;
		}
	}
	if (tp->snd_cwnd <= tp->snd_wnd)
		tp->ccv->flags |= CCF_CWND_LIMITED;
	else
		tp->ccv->flags &= ~CCF_CWND_LIMITED;

	if (type == CC_ACK) {
#ifdef NETFLIX_STATS
		stats_voi_update_abs_s32(tp->t_stats, VOI_TCP_CALCFRWINDIFF,
		    ((int32_t) tp->snd_cwnd) - tp->snd_wnd);
		if ((tp->t_flags & TF_GPUTINPROG) &&
		    SEQ_GEQ(th->th_ack, tp->gput_ack)) {
			gput = (((int64_t) (th->th_ack - tp->gput_seq)) << 3) /
			    max(1, tcp_ts_getticks() - tp->gput_ts);
			stats_voi_update_abs_u32(tp->t_stats, VOI_TCP_GPUT,
			    gput);
			/*
			 * XXXLAS: This is a temporary hack, and should be
			 * chained off VOI_TCP_GPUT when stats(9) grows an
			 * API to deal with chained VOIs.
			 */
			if (tp->t_stats_gput_prev > 0)
				stats_voi_update_abs_s32(tp->t_stats,
				    VOI_TCP_GPUT_ND,
				    ((gput - tp->t_stats_gput_prev) * 100) /
				    tp->t_stats_gput_prev);
			tp->t_flags &= ~TF_GPUTINPROG;
			tp->t_stats_gput_prev = gput;
#ifdef NETFLIX_CWV
			if (tp->t_maxpeakrate) {
				/*
				 * We update t_peakrate_thr. This gives us roughly
				 * one update per round trip time.
				 */
				tcp_update_peakrate_thr(tp);
			}
#endif
		}
#endif
		if (tp->snd_cwnd > tp->snd_ssthresh) {
			tp->t_bytes_acked += min(tp->ccv->bytes_this_ack,
			    nsegs * V_tcp_abc_l_var * tp->t_maxseg);
			if (tp->t_bytes_acked >= tp->snd_cwnd) {
				tp->t_bytes_acked -= tp->snd_cwnd;
				tp->ccv->flags |= CCF_ABC_SENTAWND;
			}
		} else {
			tp->ccv->flags &= ~CCF_ABC_SENTAWND;
			tp->t_bytes_acked = 0;
		}
	}
	if (CC_ALGO(tp)->ack_received != NULL) {
		/* XXXLAS: Find a way to live without this */
		tp->ccv->curack = th->th_ack;
		CC_ALGO(tp)->ack_received(tp->ccv, type);
	}
#ifdef NETFLIX_STATS
	stats_voi_update_abs_ulong(tp->t_stats, VOI_TCP_LCWIN, tp->snd_cwnd);
#endif
	if (rack->r_ctl.rc_rack_largest_cwnd < tp->snd_cwnd) {
		rack->r_ctl.rc_rack_largest_cwnd = tp->snd_cwnd;
	}
#ifdef NETFLIX_CWV
	if (tp->cwv_enabled) {
		/*
		 * Per RFC 7661: The behaviour in the non-validated phase is
		 * specified as: o  A sender determines whether to increase
		 * the cwnd based upon whether it is cwnd-limited (see
		 * Section 4.5.3): * A sender that is cwnd-limited MAY use
		 * the standard TCP method to increase cwnd (i.e., the
		 * standard method permits a TCP sender that fully utilises
		 * the cwnd to increase the cwnd each time it receives an
		 * ACK). * A sender that is not cwnd-limited MUST NOT
		 * increase the cwnd when ACK packets are received in this
		 * phase (i.e., needs to avoid growing the cwnd when it has
		 * not recently sent using the current size of cwnd).
		 */
		if ((tp->snd_cwnd > old_cwnd) &&
		    (tp->cwv_cwnd_valid == 0) &&
		    (!(tp->ccv->flags & CCF_CWND_LIMITED))) {
			tp->snd_cwnd = old_cwnd;
		}
		/* Try to update pipeAck and NCWV state */
		if (TCPS_HAVEESTABLISHED(tp->t_state) &&
		    !IN_RECOVERY(tp->t_flags)) {
			uint32_t data = sbavail(&(tp->t_inpcb->inp_socket->so_snd));

			tcp_newcwv_update_pipeack(tp, data);
		}
	}
	/* we enforce max peak rate if it is set. */
	if (tp->t_peakrate_thr && tp->snd_cwnd > tp->t_peakrate_thr) {
		tp->snd_cwnd = tp->t_peakrate_thr;
	}
#endif
}

static void
tcp_rack_partialack(struct tcpcb *tp, struct tcphdr *th)
{
	struct tcp_rack *rack;

	rack = (struct tcp_rack *)tp->t_fb_ptr;
	INP_WLOCK_ASSERT(tp->t_inpcb);
	if (rack->r_ctl.rc_prr_sndcnt > 0)
		rack->r_wanted_output++;
}

static void
rack_post_recovery(struct tcpcb *tp, struct tcphdr *th)
{
	struct tcp_rack *rack;

	INP_WLOCK_ASSERT(tp->t_inpcb);
	rack = (struct tcp_rack *)tp->t_fb_ptr;
	if (CC_ALGO(tp)->post_recovery != NULL) {
		tp->ccv->curack = th->th_ack;
		CC_ALGO(tp)->post_recovery(tp->ccv);
	}
	/*
	 * Here we can in theory adjust cwnd to be based on the number of
	 * losses in the window (rack->r_ctl.rc_loss_count). This is done
	 * based on the rack_use_proportional flag.
	 */
	if (rack->r_ctl.rc_prop_reduce && rack->r_ctl.rc_prop_rate) {
		int32_t reduce;

		reduce = (rack->r_ctl.rc_loss_count * rack->r_ctl.rc_prop_rate);
		if (reduce > 50) {
			reduce = 50;
		}
		tp->snd_cwnd -= ((reduce * tp->snd_cwnd) / 100);
	} else {
		if (tp->snd_cwnd > tp->snd_ssthresh) {
			/* Drop us down to the ssthresh (1/2 cwnd at loss) */
			tp->snd_cwnd = tp->snd_ssthresh;
		}
	}
	if (rack->r_ctl.rc_prr_sndcnt > 0) {
		/* Suck the next prr cnt back into cwnd */
		tp->snd_cwnd += rack->r_ctl.rc_prr_sndcnt;
		rack->r_ctl.rc_prr_sndcnt = 0;
	}
	EXIT_RECOVERY(tp->t_flags);


#ifdef NETFLIX_CWV
	if (tp->cwv_enabled) {
		if ((tp->cwv_cwnd_valid == 0) &&
		    (tp->snd_cwv.in_recovery))
			tcp_newcwv_end_recovery(tp);
	}
#endif
}

static void
rack_cong_signal(struct tcpcb *tp, struct tcphdr *th, uint32_t type)
{
	struct tcp_rack *rack;

	INP_WLOCK_ASSERT(tp->t_inpcb);

	rack = (struct tcp_rack *)tp->t_fb_ptr;
	switch (type) {
	case CC_NDUPACK:
/*		rack->r_ctl.rc_ssthresh_set = 1;*/
		if (!IN_FASTRECOVERY(tp->t_flags)) {
			rack->r_ctl.rc_tlp_rtx_out = 0;
			rack->r_ctl.rc_prr_delivered = 0;
			rack->r_ctl.rc_prr_out = 0;
			rack->r_ctl.rc_loss_count = 0;
			rack->r_ctl.rc_prr_sndcnt = tp->t_maxseg;
			rack->r_ctl.rc_prr_recovery_fs = tp->snd_max - tp->snd_una;
			tp->snd_recover = tp->snd_max;
			if (tp->t_flags & TF_ECN_PERMIT)
				tp->t_flags |= TF_ECN_SND_CWR;
		}
		break;
	case CC_ECN:
		if (!IN_CONGRECOVERY(tp->t_flags)) {
			TCPSTAT_INC(tcps_ecn_rcwnd);
			tp->snd_recover = tp->snd_max;
			if (tp->t_flags & TF_ECN_PERMIT)
				tp->t_flags |= TF_ECN_SND_CWR;
		}
		break;
	case CC_RTO:
		tp->t_dupacks = 0;
		tp->t_bytes_acked = 0;
		EXIT_RECOVERY(tp->t_flags);
		tp->snd_ssthresh = max(2, min(tp->snd_wnd, tp->snd_cwnd) / 2 /
		    tp->t_maxseg) * tp->t_maxseg;
		tp->snd_cwnd = tp->t_maxseg;
		break;
	case CC_RTO_ERR:
		TCPSTAT_INC(tcps_sndrexmitbad);
		/* RTO was unnecessary, so reset everything. */
		tp->snd_cwnd = tp->snd_cwnd_prev;
		tp->snd_ssthresh = tp->snd_ssthresh_prev;
		tp->snd_recover = tp->snd_recover_prev;
		if (tp->t_flags & TF_WASFRECOVERY)
			ENTER_FASTRECOVERY(tp->t_flags);
		if (tp->t_flags & TF_WASCRECOVERY)
			ENTER_CONGRECOVERY(tp->t_flags);
		tp->snd_nxt = tp->snd_max;
		tp->t_badrxtwin = 0;
		break;
	}

	if (CC_ALGO(tp)->cong_signal != NULL) {
		if (th != NULL)
			tp->ccv->curack = th->th_ack;
		CC_ALGO(tp)->cong_signal(tp->ccv, type);
	}
#ifdef NETFLIX_CWV
	if (tp->cwv_enabled) {
		if (tp->snd_cwv.in_recovery == 0 && IN_RECOVERY(tp->t_flags)) {
			tcp_newcwv_enter_recovery(tp);
		}
		if (type == CC_RTO) {
			tcp_newcwv_reset(tp);
		}
	}
#endif
}



static inline void
rack_cc_after_idle(struct tcpcb *tp, int reduce_largest)
{
	uint32_t i_cwnd;

	INP_WLOCK_ASSERT(tp->t_inpcb);

#ifdef NETFLIX_STATS
	TCPSTAT_INC(tcps_idle_restarts);
	if (tp->t_state == TCPS_ESTABLISHED)
		TCPSTAT_INC(tcps_idle_estrestarts);
#endif
	if (CC_ALGO(tp)->after_idle != NULL)
		CC_ALGO(tp)->after_idle(tp->ccv);

	if (tp->snd_cwnd == 1)
		i_cwnd = tp->t_maxseg;		/* SYN(-ACK) lost */
	else 
		i_cwnd = tcp_compute_initwnd(tcp_maxseg(tp));

	if (reduce_largest) {
		/*
		 * Do we reduce the largest cwnd to make 
		 * rack play nice on restart hptsi wise?
		 */
		if (((struct tcp_rack *)tp->t_fb_ptr)->r_ctl.rc_rack_largest_cwnd  > i_cwnd)
			((struct tcp_rack *)tp->t_fb_ptr)->r_ctl.rc_rack_largest_cwnd = i_cwnd;
	}
	/*
	 * Being idle is no differnt than the initial window. If the cc
	 * clamps it down below the initial window raise it to the initial
	 * window.
	 */
	if (tp->snd_cwnd < i_cwnd) {
		tp->snd_cwnd = i_cwnd;
	}
}


/*
 * Indicate whether this ack should be delayed.  We can delay the ack if
 * following conditions are met:
 *	- There is no delayed ack timer in progress.
 *	- Our last ack wasn't a 0-sized window. We never want to delay
 *	  the ack that opens up a 0-sized window.
 *	- LRO wasn't used for this segment. We make sure by checking that the
 *	  segment size is not larger than the MSS.
 *	- Delayed acks are enabled or this is a half-synchronized T/TCP
 *	  connection.
 */
#define DELAY_ACK(tp, tlen)			 \
	(((tp->t_flags & TF_RXWIN0SENT) == 0) && \
	((tp->t_flags & TF_DELACK) == 0) && 	 \
	(tlen <= tp->t_maxseg) &&		 \
	(tp->t_delayed_ack || (tp->t_flags & TF_NEEDSYN)))

static inline void
rack_calc_rwin(struct socket *so, struct tcpcb *tp)
{
	int32_t win;

	/*
	 * Calculate amount of space in receive window, and then do TCP
	 * input processing. Receive window is amount of space in rcv queue,
	 * but not less than advertised window.
	 */
	win = sbspace(&so->so_rcv);
	if (win < 0)
		win = 0;
	tp->rcv_wnd = imax(win, (int)(tp->rcv_adv - tp->rcv_nxt));
}

static void
rack_do_drop(struct mbuf *m, struct tcpcb *tp)
{
	/*
	 * Drop space held by incoming segment and return.
	 */
	if (tp != NULL)
		INP_WUNLOCK(tp->t_inpcb);
	if (m)
		m_freem(m);
}

static void
rack_do_dropwithreset(struct mbuf *m, struct tcpcb *tp, struct tcphdr *th,
    int32_t rstreason, int32_t tlen)
{
	if (tp != NULL) {
		tcp_dropwithreset(m, th, tp, tlen, rstreason);
		INP_WUNLOCK(tp->t_inpcb);
	} else
		tcp_dropwithreset(m, th, NULL, tlen, rstreason);
}

/*
 * The value in ret_val informs the caller
 * if we dropped the tcb (and lock) or not.
 * 1 = we dropped it, 0 = the TCB is still locked
 * and valid.
 */
static void
rack_do_dropafterack(struct mbuf *m, struct tcpcb *tp, struct tcphdr *th, int32_t thflags, int32_t tlen, int32_t * ret_val)
{
	/*
	 * Generate an ACK dropping incoming segment if it occupies sequence
	 * space, where the ACK reflects our state.
	 *
	 * We can now skip the test for the RST flag since all paths to this
	 * code happen after packets containing RST have been dropped.
	 *
	 * In the SYN-RECEIVED state, don't send an ACK unless the segment
	 * we received passes the SYN-RECEIVED ACK test. If it fails send a
	 * RST.  This breaks the loop in the "LAND" DoS attack, and also
	 * prevents an ACK storm between two listening ports that have been
	 * sent forged SYN segments, each with the source address of the
	 * other.
	 */
	struct tcp_rack *rack;

	if (tp->t_state == TCPS_SYN_RECEIVED && (thflags & TH_ACK) &&
	    (SEQ_GT(tp->snd_una, th->th_ack) ||
	    SEQ_GT(th->th_ack, tp->snd_max))) {
		*ret_val = 1;
		rack_do_dropwithreset(m, tp, th, BANDLIM_RST_OPENPORT, tlen);
		return;
	} else
		*ret_val = 0;
	rack = (struct tcp_rack *)tp->t_fb_ptr;
	rack->r_wanted_output++;
	tp->t_flags |= TF_ACKNOW;
	if (m)
		m_freem(m);
}


static int
rack_process_rst(struct mbuf *m, struct tcphdr *th, struct socket *so, struct tcpcb *tp)
{
	/*
	 * RFC5961 Section 3.2
	 *
	 * - RST drops connection only if SEG.SEQ == RCV.NXT. - If RST is in
	 * window, we send challenge ACK.
	 *
	 * Note: to take into account delayed ACKs, we should test against
	 * last_ack_sent instead of rcv_nxt. Note 2: we handle special case
	 * of closed window, not covered by the RFC.
	 */
	int dropped = 0;

	if ((SEQ_GEQ(th->th_seq, (tp->last_ack_sent - 1)) &&
	    SEQ_LT(th->th_seq, tp->last_ack_sent + tp->rcv_wnd)) ||
	    (tp->rcv_wnd == 0 && tp->last_ack_sent == th->th_seq)) {

		INP_INFO_RLOCK_ASSERT(&V_tcbinfo);
		KASSERT(tp->t_state != TCPS_SYN_SENT,
		    ("%s: TH_RST for TCPS_SYN_SENT th %p tp %p",
		    __func__, th, tp));

		if (V_tcp_insecure_rst ||
		    (tp->last_ack_sent == th->th_seq) ||
		    (tp->rcv_nxt == th->th_seq) ||
		    ((tp->last_ack_sent - 1) == th->th_seq)) {
			TCPSTAT_INC(tcps_drops);
			/* Drop the connection. */
			switch (tp->t_state) {
			case TCPS_SYN_RECEIVED:
				so->so_error = ECONNREFUSED;
				goto close;
			case TCPS_ESTABLISHED:
			case TCPS_FIN_WAIT_1:
			case TCPS_FIN_WAIT_2:
			case TCPS_CLOSE_WAIT:
			case TCPS_CLOSING:
			case TCPS_LAST_ACK:
				so->so_error = ECONNRESET;
		close:
				tcp_state_change(tp, TCPS_CLOSED);
				/* FALLTHROUGH */
			default:
				tp = tcp_close(tp);
			}
			dropped = 1;
			rack_do_drop(m, tp);
		} else {
			TCPSTAT_INC(tcps_badrst);
			/* Send challenge ACK. */
			tcp_respond(tp, mtod(m, void *), th, m,
			    tp->rcv_nxt, tp->snd_nxt, TH_ACK);
			tp->last_ack_sent = tp->rcv_nxt;
		}
	} else {
		m_freem(m);
	}
	return (dropped);
}

/*
 * The value in ret_val informs the caller
 * if we dropped the tcb (and lock) or not.
 * 1 = we dropped it, 0 = the TCB is still locked
 * and valid.
 */
static void
rack_challenge_ack(struct mbuf *m, struct tcphdr *th, struct tcpcb *tp, int32_t * ret_val)
{
	INP_INFO_RLOCK_ASSERT(&V_tcbinfo);

	TCPSTAT_INC(tcps_badsyn);
	if (V_tcp_insecure_syn &&
	    SEQ_GEQ(th->th_seq, tp->last_ack_sent) &&
	    SEQ_LT(th->th_seq, tp->last_ack_sent + tp->rcv_wnd)) {
		tp = tcp_drop(tp, ECONNRESET);
		*ret_val = 1;
		rack_do_drop(m, tp);
	} else {
		/* Send challenge ACK. */
		tcp_respond(tp, mtod(m, void *), th, m, tp->rcv_nxt,
		    tp->snd_nxt, TH_ACK);
		tp->last_ack_sent = tp->rcv_nxt;
		m = NULL;
		*ret_val = 0;
		rack_do_drop(m, NULL);
	}
}

/*
 * rack_ts_check returns 1 for you should not proceed. It places
 * in ret_val what should be returned 1/0 by the caller. The 1 indicates
 * that the TCB is unlocked and probably dropped. The 0 indicates the
 * TCB is still valid and locked.
 */
static int
rack_ts_check(struct mbuf *m, struct tcphdr *th, struct tcpcb *tp, int32_t tlen, int32_t thflags, int32_t * ret_val)
{

	/* Check to see if ts_recent is over 24 days old.  */
	if (tcp_ts_getticks() - tp->ts_recent_age > TCP_PAWS_IDLE) {
		/*
		 * Invalidate ts_recent.  If this segment updates ts_recent,
		 * the age will be reset later and ts_recent will get a
		 * valid value.  If it does not, setting ts_recent to zero
		 * will at least satisfy the requirement that zero be placed
		 * in the timestamp echo reply when ts_recent isn't valid.
		 * The age isn't reset until we get a valid ts_recent
		 * because we don't want out-of-order segments to be dropped
		 * when ts_recent is old.
		 */
		tp->ts_recent = 0;
	} else {
		TCPSTAT_INC(tcps_rcvduppack);
		TCPSTAT_ADD(tcps_rcvdupbyte, tlen);
		TCPSTAT_INC(tcps_pawsdrop);
		*ret_val = 0;
		if (tlen) {
			rack_do_dropafterack(m, tp, th, thflags, tlen, ret_val);
		} else {
			rack_do_drop(m, NULL);
		}
		return (1);
	}
	return (0);
}

/*
 * rack_drop_checks returns 1 for you should not proceed. It places
 * in ret_val what should be returned 1/0 by the caller. The 1 indicates
 * that the TCB is unlocked and probably dropped. The 0 indicates the
 * TCB is still valid and locked.
 */
static int
rack_drop_checks(struct tcpopt *to, struct mbuf *m, struct tcphdr *th, struct tcpcb *tp, int32_t * tlenp,  int32_t * thf, int32_t * drop_hdrlen, int32_t * ret_val)
{
	int32_t todrop;
	int32_t thflags;
	int32_t tlen;

	thflags = *thf;
	tlen = *tlenp;
	todrop = tp->rcv_nxt - th->th_seq;
	if (todrop > 0) {
		if (thflags & TH_SYN) {
			thflags &= ~TH_SYN;
			th->th_seq++;
			if (th->th_urp > 1)
				th->th_urp--;
			else
				thflags &= ~TH_URG;
			todrop--;
		}
		/*
		 * Following if statement from Stevens, vol. 2, p. 960.
		 */
		if (todrop > tlen
		    || (todrop == tlen && (thflags & TH_FIN) == 0)) {
			/*
			 * Any valid FIN must be to the left of the window.
			 * At this point the FIN must be a duplicate or out
			 * of sequence; drop it.
			 */
			thflags &= ~TH_FIN;
			/*
			 * Send an ACK to resynchronize and drop any data.
			 * But keep on processing for RST or ACK.
			 */
			tp->t_flags |= TF_ACKNOW;
			todrop = tlen;
			TCPSTAT_INC(tcps_rcvduppack);
			TCPSTAT_ADD(tcps_rcvdupbyte, todrop);
		} else {
			TCPSTAT_INC(tcps_rcvpartduppack);
			TCPSTAT_ADD(tcps_rcvpartdupbyte, todrop);
		}
		*drop_hdrlen += todrop;	/* drop from the top afterwards */
		th->th_seq += todrop;
		tlen -= todrop;
		if (th->th_urp > todrop)
			th->th_urp -= todrop;
		else {
			thflags &= ~TH_URG;
			th->th_urp = 0;
		}
	}
	/*
	 * If segment ends after window, drop trailing data (and PUSH and
	 * FIN); if nothing left, just ACK.
	 */
	todrop = (th->th_seq + tlen) - (tp->rcv_nxt + tp->rcv_wnd);
	if (todrop > 0) {
		TCPSTAT_INC(tcps_rcvpackafterwin);
		if (todrop >= tlen) {
			TCPSTAT_ADD(tcps_rcvbyteafterwin, tlen);
			/*
			 * If window is closed can only take segments at
			 * window edge, and have to drop data and PUSH from
			 * incoming segments.  Continue processing, but
			 * remember to ack.  Otherwise, drop segment and
			 * ack.
			 */
			if (tp->rcv_wnd == 0 && th->th_seq == tp->rcv_nxt) {
				tp->t_flags |= TF_ACKNOW;
				TCPSTAT_INC(tcps_rcvwinprobe);
			} else {
				rack_do_dropafterack(m, tp, th, thflags, tlen, ret_val);
				return (1);
			}
		} else
			TCPSTAT_ADD(tcps_rcvbyteafterwin, todrop);
		m_adj(m, -todrop);
		tlen -= todrop;
		thflags &= ~(TH_PUSH | TH_FIN);
	}
	*thf = thflags;
	*tlenp = tlen;
	return (0);
}

static struct rack_sendmap *
rack_find_lowest_rsm(struct tcp_rack *rack)
{
	struct rack_sendmap *rsm;

	/*
	 * Walk the time-order transmitted list looking for an rsm that is
	 * not acked. This will be the one that was sent the longest time
	 * ago that is still outstanding.
	 */
	TAILQ_FOREACH(rsm, &rack->r_ctl.rc_tmap, r_tnext) {
		if (rsm->r_flags & RACK_ACKED) {
			continue;
		}
		goto finish;
	}
finish:
	return (rsm);
}

static struct rack_sendmap *
rack_find_high_nonack(struct tcp_rack *rack, struct rack_sendmap *rsm)
{
	struct rack_sendmap *prsm;

	/*
	 * Walk the sequence order list backward until we hit and arrive at
	 * the highest seq not acked. In theory when this is called it
	 * should be the last segment (which it was not).
	 */
	counter_u64_add(rack_find_high, 1);
	prsm = rsm;
	TAILQ_FOREACH_REVERSE_FROM(prsm, &rack->r_ctl.rc_map, rack_head, r_next) {
		if (prsm->r_flags & (RACK_ACKED | RACK_HAS_FIN)) {
			continue;
		}
		return (prsm);
	}
	return (NULL);
}


static uint32_t
rack_calc_thresh_rack(struct tcp_rack *rack, uint32_t srtt, uint32_t cts)
{
	int32_t lro;
	uint32_t thresh;

	/*
	 * lro is the flag we use to determine if we have seen reordering.
	 * If it gets set we have seen reordering. The reorder logic either
	 * works in one of two ways:
	 *
	 * If reorder-fade is configured, then we track the last time we saw
	 * re-ordering occur. If we reach the point where enough time as
	 * passed we no longer consider reordering has occuring.
	 *
	 * Or if reorder-face is 0, then once we see reordering we consider
	 * the connection to alway be subject to reordering and just set lro
	 * to 1.
	 *
	 * In the end if lro is non-zero we add the extra time for
	 * reordering in.
	 */
	if (srtt == 0)
		srtt = 1;
	if (rack->r_ctl.rc_reorder_ts) {
		if (rack->r_ctl.rc_reorder_fade) {
			if (SEQ_GEQ(cts, rack->r_ctl.rc_reorder_ts)) {
				lro = cts - rack->r_ctl.rc_reorder_ts;
				if (lro == 0) {
					/*
					 * No time as passed since the last
					 * reorder, mark it as reordering.
					 */
					lro = 1;
				}
			} else {
				/* Negative time? */
				lro = 0;
			}
			if (lro > rack->r_ctl.rc_reorder_fade) {
				/* Turn off reordering seen too */
				rack->r_ctl.rc_reorder_ts = 0;
				lro = 0;
			}
		} else {
			/* Reodering does not fade */
			lro = 1;
		}
	} else {
		lro = 0;
	}
	thresh = srtt + rack->r_ctl.rc_pkt_delay;
	if (lro) {
		/* It must be set, if not you get 1/4 rtt */
		if (rack->r_ctl.rc_reorder_shift)
			thresh += (srtt >> rack->r_ctl.rc_reorder_shift);
		else
			thresh += (srtt >> 2);
	} else {
		thresh += 1;
	}
	/* We don't let the rack timeout be above a RTO */
	
	if (thresh > TICKS_2_MSEC(rack->rc_tp->t_rxtcur)) {
		thresh = TICKS_2_MSEC(rack->rc_tp->t_rxtcur);
	}
	/* And we don't want it above the RTO max either */
	if (thresh > rack_rto_max) {
		thresh = rack_rto_max;
	}
	return (thresh);
}

static uint32_t
rack_calc_thresh_tlp(struct tcpcb *tp, struct tcp_rack *rack,
		     struct rack_sendmap *rsm, uint32_t srtt)
{
	struct rack_sendmap *prsm;
	uint32_t thresh, len;
	int maxseg;
	
	if (srtt == 0)
		srtt = 1;
	if (rack->r_ctl.rc_tlp_threshold)
		thresh = srtt + (srtt / rack->r_ctl.rc_tlp_threshold);
	else
		thresh = (srtt * 2);
	
	/* Get the previous sent packet, if any  */
	maxseg = tcp_maxseg(tp);
	counter_u64_add(rack_enter_tlp_calc, 1);
	len = rsm->r_end - rsm->r_start;
	if (rack->rack_tlp_threshold_use == TLP_USE_ID) {
		/* Exactly like the ID */
		if (((tp->snd_max - tp->snd_una) - rack->r_ctl.rc_sacked + rack->r_ctl.rc_holes_rxt) <= maxseg) {
			uint32_t alt_thresh;
			/*
			 * Compensate for delayed-ack with the d-ack time.
			 */
			counter_u64_add(rack_used_tlpmethod, 1);
			alt_thresh = srtt + (srtt / 2) + rack_delayed_ack_time;
			if (alt_thresh > thresh)
				thresh = alt_thresh;
		}
	} else if (rack->rack_tlp_threshold_use == TLP_USE_TWO_ONE) {
		/* 2.1 behavior */
		prsm = TAILQ_PREV(rsm, rack_head, r_tnext);
		if (prsm && (len <= maxseg)) {
			/*
			 * Two packets outstanding, thresh should be (2*srtt) +
			 * possible inter-packet delay (if any).
			 */
			uint32_t inter_gap = 0;
			int idx, nidx;
			
			counter_u64_add(rack_used_tlpmethod, 1);
			idx = rsm->r_rtr_cnt - 1;
			nidx = prsm->r_rtr_cnt - 1;
			if (TSTMP_GEQ(rsm->r_tim_lastsent[nidx], prsm->r_tim_lastsent[idx])) {
				/* Yes it was sent later (or at the same time) */
				inter_gap = rsm->r_tim_lastsent[idx] - prsm->r_tim_lastsent[nidx];
			}
			thresh += inter_gap;
		} else 	if (len <= maxseg) {
			/*
			 * Possibly compensate for delayed-ack.
			 */
			uint32_t alt_thresh;
			
			counter_u64_add(rack_used_tlpmethod2, 1);
			alt_thresh = srtt + (srtt / 2) + rack_delayed_ack_time;
			if (alt_thresh > thresh)
				thresh = alt_thresh;
		}
	} else if (rack->rack_tlp_threshold_use == TLP_USE_TWO_TWO) {
		/* 2.2 behavior */
		if (len <= maxseg) {
			uint32_t alt_thresh;
			/*
			 * Compensate for delayed-ack with the d-ack time.
			 */
			counter_u64_add(rack_used_tlpmethod, 1);
			alt_thresh = srtt + (srtt / 2) + rack_delayed_ack_time;
			if (alt_thresh > thresh)
				thresh = alt_thresh;
		}
	}
 	/* Not above an RTO */
	if (thresh > TICKS_2_MSEC(tp->t_rxtcur)) {
		thresh = TICKS_2_MSEC(tp->t_rxtcur);
	}
	/* Not above a RTO max */
	if (thresh > rack_rto_max) {
		thresh = rack_rto_max;
	}
	/* Apply user supplied min TLP */
	if (thresh < rack_tlp_min) {
		thresh = rack_tlp_min;
	}
	return (thresh);
}

static struct rack_sendmap *
rack_check_recovery_mode(struct tcpcb *tp, uint32_t tsused)
{
	/*
	 * Check to see that we don't need to fall into recovery. We will
	 * need to do so if our oldest transmit is past the time we should
	 * have had an ack.
	 */
	struct tcp_rack *rack;
	struct rack_sendmap *rsm;
	int32_t idx;
	uint32_t srtt_cur, srtt, thresh;

	rack = (struct tcp_rack *)tp->t_fb_ptr;
	if (TAILQ_EMPTY(&rack->r_ctl.rc_map)) {
		return (NULL);
	}
	srtt_cur = tp->t_srtt >> TCP_RTT_SHIFT;
	srtt = TICKS_2_MSEC(srtt_cur);
	if (rack->rc_rack_rtt && (srtt > rack->rc_rack_rtt))
		srtt = rack->rc_rack_rtt;

	rsm = TAILQ_FIRST(&rack->r_ctl.rc_tmap);
	if (rsm == NULL)
		return (NULL);

	if (rsm->r_flags & RACK_ACKED) {
		rsm = rack_find_lowest_rsm(rack);
		if (rsm == NULL)
			return (NULL);
	}
	idx = rsm->r_rtr_cnt - 1;
	thresh = rack_calc_thresh_rack(rack, srtt, tsused);
	if (tsused < rsm->r_tim_lastsent[idx]) {
		return (NULL);
	}
	if ((tsused - rsm->r_tim_lastsent[idx]) < thresh) {
		return (NULL);
	}
	/* Ok if we reach here we are over-due */
	rack->r_ctl.rc_rsm_start = rsm->r_start;
	rack->r_ctl.rc_cwnd_at = tp->snd_cwnd;
	rack->r_ctl.rc_ssthresh_at = tp->snd_ssthresh;
	rack_cong_signal(tp, NULL, CC_NDUPACK);
	return (rsm);
}

static uint32_t
rack_get_persists_timer_val(struct tcpcb *tp, struct tcp_rack *rack)
{
	int32_t t;
	int32_t tt;
	uint32_t ret_val;

	t = TICKS_2_MSEC((tp->t_srtt >> TCP_RTT_SHIFT) + ((tp->t_rttvar * 4) >> TCP_RTT_SHIFT));
	TCPT_RANGESET(tt, t * tcp_backoff[tp->t_rxtshift],
	    tcp_persmin, tcp_persmax);
	if (tp->t_rxtshift < TCP_MAXRXTSHIFT)
		tp->t_rxtshift++;
	rack->r_ctl.rc_hpts_flags |= PACE_TMR_PERSIT;
	ret_val = (uint32_t)tt;
	return (ret_val);
}

static uint32_t
rack_timer_start(struct tcpcb *tp, struct tcp_rack *rack, uint32_t cts)
{
	/*
	 * Start the FR timer, we do this based on getting the first one in
	 * the rc_tmap. Note that if its NULL we must stop the timer. in all
	 * events we need to stop the running timer (if its running) before
	 * starting the new one.
	 */
	uint32_t thresh, exp, to, srtt, time_since_sent;
	uint32_t srtt_cur;
	int32_t idx;
	int32_t is_tlp_timer = 0;
	struct rack_sendmap *rsm;
	
	if (rack->t_timers_stopped) {
		/* All timers have been stopped none are to run */
		return (0);
	}
	if (rack->rc_in_persist) {
		/* We can't start any timer in persists */
		return (rack_get_persists_timer_val(tp, rack));
	}
	if (tp->t_state < TCPS_ESTABLISHED)
		goto activate_rxt;
	rsm = TAILQ_FIRST(&rack->r_ctl.rc_tmap);
	if (rsm == NULL) {
		/* Nothing on the send map */
activate_rxt:
		if (SEQ_LT(tp->snd_una, tp->snd_max) || sbavail(&(tp->t_inpcb->inp_socket->so_snd))) {
			rack->r_ctl.rc_hpts_flags |= PACE_TMR_RXT;
			to = TICKS_2_MSEC(tp->t_rxtcur);
			if (to == 0)
				to = 1;
			return (to);
		}
		return (0);
	}
	if (rsm->r_flags & RACK_ACKED) {
		rsm = rack_find_lowest_rsm(rack);
		if (rsm == NULL) {
			/* No lowest? */
			goto activate_rxt;
		}
	}
	/* Convert from ms to usecs */
	if (rsm->r_flags & RACK_SACK_PASSED) {
		if ((tp->t_flags & TF_SENTFIN) &&
		    ((tp->snd_max - tp->snd_una) == 1) &&
		    (rsm->r_flags & RACK_HAS_FIN)) {
			/*
			 * We don't start a rack timer if all we have is a
			 * FIN outstanding.
			 */
			goto activate_rxt;
		}
		if (tp->t_srtt) {
			srtt_cur = (tp->t_srtt >> TCP_RTT_SHIFT);
			srtt = TICKS_2_MSEC(srtt_cur);
		} else
			srtt = RACK_INITIAL_RTO;

		thresh = rack_calc_thresh_rack(rack, srtt, cts);
		idx = rsm->r_rtr_cnt - 1;
		exp = rsm->r_tim_lastsent[idx] + thresh;
		if (SEQ_GEQ(exp, cts)) {
			to = exp - cts;
			if (to < rack->r_ctl.rc_min_to) {
				to = rack->r_ctl.rc_min_to;
			}
		} else {
			to = rack->r_ctl.rc_min_to;
		}
	} else {
		/* Ok we need to do a TLP not RACK */
		if ((rack->rc_tlp_in_progress != 0) ||
		    (rack->r_ctl.rc_tlp_rtx_out != 0)) {
			/*
			 * The previous send was a TLP or a tlp_rtx is in
			 * process.
			 */
			goto activate_rxt;
		}
		rsm = TAILQ_LAST_FAST(&rack->r_ctl.rc_tmap, rack_sendmap, r_tnext);
		if (rsm == NULL) {
			/* We found no rsm to TLP with. */
			goto activate_rxt;
		}
		if (rsm->r_flags & RACK_HAS_FIN) {
			/* If its a FIN we dont do TLP */
			rsm = NULL;
			goto activate_rxt;
		}
		idx = rsm->r_rtr_cnt - 1;
		if (TSTMP_GT(cts,  rsm->r_tim_lastsent[idx])) 
			time_since_sent = cts - rsm->r_tim_lastsent[idx];
		else
			time_since_sent = 0;
		is_tlp_timer = 1;
		if (tp->t_srtt) {
			srtt_cur = (tp->t_srtt >> TCP_RTT_SHIFT);
			srtt = TICKS_2_MSEC(srtt_cur);
		} else
			srtt = RACK_INITIAL_RTO;
		thresh = rack_calc_thresh_tlp(tp, rack, rsm, srtt);
		if (thresh > time_since_sent)
			to = thresh - time_since_sent;
		else
			to = rack->r_ctl.rc_min_to;
		if (to > TCPTV_REXMTMAX) {
			/*
			 * If the TLP time works out to larger than the max
			 * RTO lets not do TLP.. just RTO.
			 */
			goto activate_rxt;
		}
		if (rsm->r_start != rack->r_ctl.rc_last_tlp_seq) {
			/*
			 * The tail is no longer the last one I did a probe
			 * on
			 */
			rack->r_ctl.rc_tlp_seg_send_cnt = 0;
			rack->r_ctl.rc_last_tlp_seq = rsm->r_start;
		}
	}
	if (is_tlp_timer == 0) {
		rack->r_ctl.rc_hpts_flags |= PACE_TMR_RACK;
	} else {
		if ((rack->r_ctl.rc_tlp_send_cnt > rack_tlp_max_resend) ||
		    (rack->r_ctl.rc_tlp_seg_send_cnt > rack_tlp_max_resend)) {
			/*
			 * We have exceeded how many times we can retran the
			 * current TLP timer, switch to the RTO timer.
			 */
			goto activate_rxt;
		} else {
			rack->r_ctl.rc_hpts_flags |= PACE_TMR_TLP;
		}
	}
	if (to == 0)
		to = 1;
	return (to);
}

static void
rack_enter_persist(struct tcpcb *tp, struct tcp_rack *rack, uint32_t cts)
{
	if (rack->rc_in_persist == 0) {
		if (((tp->t_flags & TF_SENTFIN) == 0) &&
		    (tp->snd_max - tp->snd_una) >= sbavail(&rack->rc_inp->inp_socket->so_snd))
			/* Must need to send more data to enter persist */
			return;
		rack->r_ctl.rc_went_idle_time = cts;
		rack_timer_cancel(tp, rack, cts, __LINE__);
		tp->t_rxtshift = 0;
		rack->rc_in_persist = 1;
	}
}

static void
rack_exit_persist(struct tcpcb *tp, struct tcp_rack *rack)
{
	if (rack->rc_inp->inp_in_hpts)  {
		tcp_hpts_remove(rack->rc_inp, HPTS_REMOVE_OUTPUT);
		rack->r_ctl.rc_hpts_flags  = 0;
	}
	rack->rc_in_persist = 0;
	rack->r_ctl.rc_went_idle_time = 0;
	tp->t_flags &= ~TF_FORCEDATA;
	tp->t_rxtshift = 0;
}

static void
rack_start_hpts_timer(struct tcp_rack *rack, struct tcpcb *tp, uint32_t cts, int32_t line,
    int32_t slot, uint32_t tot_len_this_send, int32_t frm_out_sbavail)
{
	struct inpcb *inp;
	uint32_t delayed_ack = 0;
	uint32_t hpts_timeout;
	uint8_t stopped;
	uint32_t left = 0;

	inp = tp->t_inpcb;
	if (inp->inp_in_hpts) {
		/* A previous call is already set up */
		return;
	}
	if (tp->t_state == TCPS_CLOSED) {
		return;
	}
	stopped = rack->rc_tmr_stopped;
	if (stopped && TSTMP_GT(rack->r_ctl.rc_timer_exp, cts)) {
		left = rack->r_ctl.rc_timer_exp - cts;
	}
	rack->r_ctl.rc_timer_exp = 0;
	if (rack->rc_inp->inp_in_hpts == 0) {
		rack->r_ctl.rc_hpts_flags = 0;
	} 
	if (slot) {
		/* We are hptsi too */
		rack->r_ctl.rc_hpts_flags |= PACE_PKT_OUTPUT;
	} else if (rack->r_ctl.rc_hpts_flags & PACE_PKT_OUTPUT) {
		/* 
		 * We are still left on the hpts when the to goes
		 * it will be for output.
		 */
		if (TSTMP_GT(cts, rack->r_ctl.rc_last_output_to))
			slot = cts - rack->r_ctl.rc_last_output_to;
		else
			slot = 1;
	}
	if ((tp->snd_wnd == 0) && TCPS_HAVEESTABLISHED(tp->t_state)) {
		/* No send window.. we must enter persist */
		rack_enter_persist(tp, rack, cts);
	} else if ((frm_out_sbavail &&
		    (frm_out_sbavail > (tp->snd_max - tp->snd_una)) &&
		    (tp->snd_wnd < tp->t_maxseg)) &&
	    TCPS_HAVEESTABLISHED(tp->t_state)) {
		/*
		 * If we have no window or we can't send a segment (and have
		 * data to send.. we cheat here and frm_out_sbavail is
		 * passed in with the sbavail(sb) only from bbr_output) and
		 * we are established, then we must enter persits (if not
		 * already in persits).
		 */
		rack_enter_persist(tp, rack, cts);
	}
	hpts_timeout = rack_timer_start(tp, rack, cts);
	if (tp->t_flags & TF_DELACK) {
		delayed_ack = TICKS_2_MSEC(tcp_delacktime);
		rack->r_ctl.rc_hpts_flags |= PACE_TMR_DELACK;
	}
	if (delayed_ack && ((hpts_timeout == 0) ||
			    (delayed_ack < hpts_timeout)))
		hpts_timeout = delayed_ack;
	else 
		rack->r_ctl.rc_hpts_flags &= ~PACE_TMR_DELACK;
	/*
	 * If no timers are going to run and we will fall off the hptsi
	 * wheel, we resort to a keep-alive timer if its configured.
	 */
	if ((hpts_timeout == 0) &&
	    (slot == 0)) {
		if ((tcp_always_keepalive || inp->inp_socket->so_options & SO_KEEPALIVE) &&
		    (tp->t_state <= TCPS_CLOSING)) {
			/*
			 * Ok we have no timer (persists, rack, tlp, rxt  or
			 * del-ack), we don't have segments being paced. So
			 * all that is left is the keepalive timer.
			 */
			if (TCPS_HAVEESTABLISHED(tp->t_state)) {
				/* Get the established keep-alive time */
				hpts_timeout = TP_KEEPIDLE(tp);
			} else {
				/* Get the initial setup keep-alive time */
				hpts_timeout = TP_KEEPINIT(tp);
			}
			rack->r_ctl.rc_hpts_flags |= PACE_TMR_KEEP;
		}
	}
	if (left && (stopped & (PACE_TMR_KEEP | PACE_TMR_DELACK)) ==
	    (rack->r_ctl.rc_hpts_flags & PACE_TMR_MASK)) {
		/*
		 * RACK, TLP, persists and RXT timers all are restartable
		 * based on actions input .. i.e we received a packet (ack
		 * or sack) and that changes things (rw, or snd_una etc).
		 * Thus we can restart them with a new value. For
		 * keep-alive, delayed_ack we keep track of what was left
		 * and restart the timer with a smaller value.
		 */
		if (left < hpts_timeout)
			hpts_timeout = left;
	}
	if (hpts_timeout) {
		/*
		 * Hack alert for now we can't time-out over 2,147,483
		 * seconds (a bit more than 596 hours), which is probably ok
		 * :).
		 */
		if (hpts_timeout > 0x7ffffffe)
			hpts_timeout = 0x7ffffffe;
		rack->r_ctl.rc_timer_exp = cts + hpts_timeout;
	}
	if (slot) {
		rack->r_ctl.rc_last_output_to = cts + slot;
		if ((hpts_timeout == 0) || (hpts_timeout > slot)) {
			if (rack->rc_inp->inp_in_hpts == 0)
				tcp_hpts_insert(tp->t_inpcb, HPTS_MS_TO_SLOTS(slot));
			rack_log_to_start(rack, cts, hpts_timeout, slot, 1);
		} else {
			/*
			 * Arrange for the hpts to kick back in after the
			 * t-o if the t-o does not cause a send.
			 */
			if (rack->rc_inp->inp_in_hpts == 0)
				tcp_hpts_insert(tp->t_inpcb, HPTS_MS_TO_SLOTS(hpts_timeout));
			rack_log_to_start(rack, cts, hpts_timeout, slot, 0);
		}
	} else if (hpts_timeout) {
		if (rack->rc_inp->inp_in_hpts == 0)
			tcp_hpts_insert(tp->t_inpcb, HPTS_MS_TO_SLOTS(hpts_timeout));
		rack_log_to_start(rack, cts, hpts_timeout, slot, 0);
	} else {
		/* No timer starting */
#ifdef INVARIANTS
		if (SEQ_GT(tp->snd_max, tp->snd_una)) {
			panic("tp:%p rack:%p tlts:%d cts:%u slot:%u pto:%u -- no timer started?",
			    tp, rack, tot_len_this_send, cts, slot, hpts_timeout);
		}
#endif
	}
	rack->rc_tmr_stopped = 0;
	if (slot)
		rack_log_type_bbrsnd(rack, tot_len_this_send, slot, cts);
}

/*
 * RACK Timer, here we simply do logging and house keeping.
 * the normal rack_output() function will call the
 * appropriate thing to check if we need to do a RACK retransmit.
 * We return 1, saying don't proceed with rack_output only
 * when all timers have been stopped (destroyed PCB?).
 */
static int
rack_timeout_rack(struct tcpcb *tp, struct tcp_rack *rack, uint32_t cts)
{
	/*
	 * This timer simply provides an internal trigger to send out data.
	 * The check_recovery_mode call will see if there are needed
	 * retransmissions, if so we will enter fast-recovery. The output
	 * call may or may not do the same thing depending on sysctl
	 * settings.
	 */
	struct rack_sendmap *rsm;
	int32_t recovery;

	if (tp->t_timers->tt_flags & TT_STOPPED) {
		return (1);
	}
	if (TSTMP_LT(cts, rack->r_ctl.rc_timer_exp)) {
		/* Its not time yet */
		return (0);
	}
	rack_log_to_event(rack, RACK_TO_FRM_RACK);
	recovery = IN_RECOVERY(tp->t_flags);
	counter_u64_add(rack_to_tot, 1);
	if (rack->r_state && (rack->r_state != tp->t_state))
		rack_set_state(tp, rack);
	rsm = rack_check_recovery_mode(tp, cts);
	if (rsm) {
		uint32_t rtt;

		rtt = rack->rc_rack_rtt;
		if (rtt == 0)
			rtt = 1;
		if ((recovery == 0) &&
		    (rack->r_ctl.rc_prr_sndcnt < tp->t_maxseg)) {
			/*
			 * The rack-timeout that enter's us into recovery
			 * will force out one MSS and set us up so that we
			 * can do one more send in 2*rtt (transitioning the
			 * rack timeout into a rack-tlp).
			 */
			rack->r_ctl.rc_prr_sndcnt = tp->t_maxseg;
		} else if ((rack->r_ctl.rc_prr_sndcnt < tp->t_maxseg) &&
		    ((rsm->r_end - rsm->r_start) > rack->r_ctl.rc_prr_sndcnt)) {
			/*
			 * When a rack timer goes, we have to send at 
			 * least one segment. They will be paced a min of 1ms
			 * apart via the next rack timer (or further
			 * if the rack timer dictates it).
			 */
			rack->r_ctl.rc_prr_sndcnt = tp->t_maxseg;
		}
	} else {
		/* This is a case that should happen rarely if ever */
		counter_u64_add(rack_tlp_does_nada, 1);
#ifdef TCP_BLACKBOX
		tcp_log_dump_tp_logbuf(tp, "nada counter trips", M_NOWAIT, true);
#endif
		rack->r_ctl.rc_resend = TAILQ_FIRST(&rack->r_ctl.rc_tmap);
	}
	rack->r_ctl.rc_hpts_flags &= ~PACE_TMR_RACK;
	return (0);
}

/*
 * TLP Timer, here we simply setup what segment we want to
 * have the TLP expire on, the normal rack_output() will then
 * send it out.
 *
 * We return 1, saying don't proceed with rack_output only
 * when all timers have been stopped (destroyed PCB?).
 */
static int
rack_timeout_tlp(struct tcpcb *tp, struct tcp_rack *rack, uint32_t cts)
{
	/*
	 * Tail Loss Probe.
	 */
	struct rack_sendmap *rsm = NULL;
	struct socket *so;
	uint32_t amm, old_prr_snd = 0;
	uint32_t out, avail;

	if (tp->t_timers->tt_flags & TT_STOPPED) {
		return (1);
	}
	if (TSTMP_LT(cts, rack->r_ctl.rc_timer_exp)) {
		/* Its not time yet */
		return (0);
	}
	if (rack_progress_timeout_check(tp)) {
		tcp_set_inp_to_drop(tp->t_inpcb, ETIMEDOUT);
		return (1);
	}
	/*
	 * A TLP timer has expired. We have been idle for 2 rtts. So we now
	 * need to figure out how to force a full MSS segment out.
	 */
	rack_log_to_event(rack, RACK_TO_FRM_TLP);
	counter_u64_add(rack_tlp_tot, 1);
	if (rack->r_state && (rack->r_state != tp->t_state))
		rack_set_state(tp, rack);
	so = tp->t_inpcb->inp_socket;
	avail = sbavail(&so->so_snd);
	out = tp->snd_max - tp->snd_una;
	rack->rc_timer_up = 1;
	/*
	 * If we are in recovery we can jazz out a segment if new data is
	 * present simply by setting rc_prr_sndcnt to a segment.
	 */
	if ((avail > out) &&
	    ((rack_always_send_oldest == 0) || (TAILQ_EMPTY(&rack->r_ctl.rc_tmap)))) {
		/* New data is available */
		amm = avail - out;
		if (amm > tp->t_maxseg) {
			amm = tp->t_maxseg;
		} else if ((amm < tp->t_maxseg) && ((tp->t_flags & TF_NODELAY) == 0)) {
			/* not enough to fill a MTU and no-delay is off */
			goto need_retran;
		}
		if (IN_RECOVERY(tp->t_flags)) {
			/* Unlikely */
			old_prr_snd = rack->r_ctl.rc_prr_sndcnt;
			if (out + amm <= tp->snd_wnd)
				rack->r_ctl.rc_prr_sndcnt = amm;
			else
				goto need_retran;
		} else {
			/* Set the send-new override */
			if (out + amm <= tp->snd_wnd)
				rack->r_ctl.rc_tlp_new_data = amm;
			else
				goto need_retran;
		}
		rack->r_ctl.rc_tlp_seg_send_cnt = 0;
		rack->r_ctl.rc_last_tlp_seq = tp->snd_max;
		rack->r_ctl.rc_tlpsend = NULL;
		counter_u64_add(rack_tlp_newdata, 1);
		goto send;
	}
need_retran:
	/*
	 * Ok we need to arrange the last un-acked segment to be re-sent, or
	 * optionally the first un-acked segment.
	 */
	if (rack_always_send_oldest)
		rsm = TAILQ_FIRST(&rack->r_ctl.rc_tmap);
	else {
		rsm = TAILQ_LAST_FAST(&rack->r_ctl.rc_map, rack_sendmap, r_next);
		if (rsm && (rsm->r_flags & (RACK_ACKED | RACK_HAS_FIN))) {
			rsm = rack_find_high_nonack(rack, rsm);
		}
	}
	if (rsm == NULL) {
		counter_u64_add(rack_tlp_does_nada, 1);
#ifdef TCP_BLACKBOX
		tcp_log_dump_tp_logbuf(tp, "nada counter trips", M_NOWAIT, true);
#endif
		goto out;
	}
	if ((rsm->r_end - rsm->r_start) > tp->t_maxseg) {
		/*
		 * We need to split this the last segment in two.
		 */
		int32_t idx;
		struct rack_sendmap *nrsm;

		nrsm = rack_alloc(rack);
		if (nrsm == NULL) {
			/*
			 * No memory to split, we will just exit and punt
			 * off to the RXT timer.
			 */
			counter_u64_add(rack_tlp_does_nada, 1);
			goto out;
		}
		nrsm->r_start = (rsm->r_end - tp->t_maxseg);
		nrsm->r_end = rsm->r_end;
		nrsm->r_rtr_cnt = rsm->r_rtr_cnt;
		nrsm->r_flags = rsm->r_flags;
		nrsm->r_sndcnt = rsm->r_sndcnt;
		nrsm->r_rtr_bytes = 0;
		rsm->r_end = nrsm->r_start;
		for (idx = 0; idx < nrsm->r_rtr_cnt; idx++) {
			nrsm->r_tim_lastsent[idx] = rsm->r_tim_lastsent[idx];
		}
		TAILQ_INSERT_AFTER(&rack->r_ctl.rc_map, rsm, nrsm, r_next);
		if (rsm->r_in_tmap) {
			TAILQ_INSERT_AFTER(&rack->r_ctl.rc_tmap, rsm, nrsm, r_tnext);
			nrsm->r_in_tmap = 1;
		}
		rsm->r_flags &= (~RACK_HAS_FIN);
		rsm = nrsm;
	}
	rack->r_ctl.rc_tlpsend = rsm;
	rack->r_ctl.rc_tlp_rtx_out = 1;
	if (rsm->r_start == rack->r_ctl.rc_last_tlp_seq) {
		rack->r_ctl.rc_tlp_seg_send_cnt++;
		tp->t_rxtshift++;
	} else {
		rack->r_ctl.rc_last_tlp_seq = rsm->r_start;
		rack->r_ctl.rc_tlp_seg_send_cnt = 1;
	}
send:
	rack->r_ctl.rc_tlp_send_cnt++;
	if (rack->r_ctl.rc_tlp_send_cnt > rack_tlp_max_resend) {
		/*
		 * Can't [re]/transmit a segment we have not heard from the
		 * peer in max times. We need the retransmit timer to take
		 * over.
		 */
restore:
		rack->r_ctl.rc_tlpsend = NULL;
		if (rsm)
			rsm->r_flags &= ~RACK_TLP;
		rack->r_ctl.rc_prr_sndcnt = old_prr_snd;
		counter_u64_add(rack_tlp_retran_fail, 1);
		goto out;
	} else if (rsm) {
		rsm->r_flags |= RACK_TLP;
	}
	if (rsm && (rsm->r_start == rack->r_ctl.rc_last_tlp_seq) &&
	    (rack->r_ctl.rc_tlp_seg_send_cnt > rack_tlp_max_resend)) {
		/*
		 * We don't want to send a single segment more than the max
		 * either.
		 */
		goto restore;
	}
	rack->r_timer_override = 1;
	rack->r_tlp_running = 1;
	rack->rc_tlp_in_progress = 1;
	rack->r_ctl.rc_hpts_flags &= ~PACE_TMR_TLP;
	return (0);
out:
	rack->rc_timer_up = 0;
	rack->r_ctl.rc_hpts_flags &= ~PACE_TMR_TLP;
	return (0);
}

/*
 * Delayed ack Timer, here we simply need to setup the
 * ACK_NOW flag and remove the DELACK flag. From there
 * the output routine will send the ack out.
 *
 * We only return 1, saying don't proceed, if all timers
 * are stopped (destroyed PCB?).
 */
static int
rack_timeout_delack(struct tcpcb *tp, struct tcp_rack *rack, uint32_t cts)
{
	if (tp->t_timers->tt_flags & TT_STOPPED) {
		return (1);
	}
	rack_log_to_event(rack, RACK_TO_FRM_DELACK);
	tp->t_flags &= ~TF_DELACK;
	tp->t_flags |= TF_ACKNOW;
	TCPSTAT_INC(tcps_delack);
	rack->r_ctl.rc_hpts_flags &= ~PACE_TMR_DELACK;
	return (0);
}

/*
 * Persists timer, here we simply need to setup the
 * FORCE-DATA flag the output routine will send
 * the one byte send.
 *
 * We only return 1, saying don't proceed, if all timers
 * are stopped (destroyed PCB?).
 */
static int
rack_timeout_persist(struct tcpcb *tp, struct tcp_rack *rack, uint32_t cts)
{
	struct inpcb *inp;
	int32_t retval = 0;

	inp = tp->t_inpcb;

	if (tp->t_timers->tt_flags & TT_STOPPED) {
		return (1);
	}
	if (rack->rc_in_persist == 0)
		return (0);
	if (rack_progress_timeout_check(tp)) {
		tcp_set_inp_to_drop(inp, ETIMEDOUT);
		return (1);
	}
	KASSERT(inp != NULL, ("%s: tp %p tp->t_inpcb == NULL", __func__, tp));
	/*
	 * Persistence timer into zero window. Force a byte to be output, if
	 * possible.
	 */
	TCPSTAT_INC(tcps_persisttimeo);
	/*
	 * Hack: if the peer is dead/unreachable, we do not time out if the
	 * window is closed.  After a full backoff, drop the connection if
	 * the idle time (no responses to probes) reaches the maximum
	 * backoff that we would use if retransmitting.
	 */
	if (tp->t_rxtshift == TCP_MAXRXTSHIFT &&
	    (ticks - tp->t_rcvtime >= tcp_maxpersistidle ||
	    ticks - tp->t_rcvtime >= TCP_REXMTVAL(tp) * tcp_totbackoff)) {
		TCPSTAT_INC(tcps_persistdrop);
		retval = 1;
		tcp_set_inp_to_drop(rack->rc_inp, ETIMEDOUT);
		goto out;
	}
	if ((sbavail(&rack->rc_inp->inp_socket->so_snd) == 0) &&
	    tp->snd_una == tp->snd_max)
		rack_exit_persist(tp, rack);
	rack->r_ctl.rc_hpts_flags &= ~PACE_TMR_PERSIT;
	/*
	 * If the user has closed the socket then drop a persisting
	 * connection after a much reduced timeout.
	 */
	if (tp->t_state > TCPS_CLOSE_WAIT &&
	    (ticks - tp->t_rcvtime) >= TCPTV_PERSMAX) {
		retval = 1;
		TCPSTAT_INC(tcps_persistdrop);
		tcp_set_inp_to_drop(rack->rc_inp, ETIMEDOUT);
		goto out;
	}
	tp->t_flags |= TF_FORCEDATA;
out:
	rack_log_to_event(rack, RACK_TO_FRM_PERSIST);
	return (retval);
}

/*
 * If a keepalive goes off, we had no other timers
 * happening. We always return 1 here since this
 * routine either drops the connection or sends
 * out a segment with respond.
 */
static int
rack_timeout_keepalive(struct tcpcb *tp, struct tcp_rack *rack, uint32_t cts)
{
	struct tcptemp *t_template;
	struct inpcb *inp;

	if (tp->t_timers->tt_flags & TT_STOPPED) {
		return (1);
	}
	rack->r_ctl.rc_hpts_flags &= ~PACE_TMR_KEEP;
	inp = tp->t_inpcb;
	rack_log_to_event(rack, RACK_TO_FRM_KEEP);
	/*
	 * Keep-alive timer went off; send something or drop connection if
	 * idle for too long.
	 */
	TCPSTAT_INC(tcps_keeptimeo);
	if (tp->t_state < TCPS_ESTABLISHED)
		goto dropit;
	if ((tcp_always_keepalive || inp->inp_socket->so_options & SO_KEEPALIVE) &&
	    tp->t_state <= TCPS_CLOSING) {
		if (ticks - tp->t_rcvtime >= TP_KEEPIDLE(tp) + TP_MAXIDLE(tp))
			goto dropit;
		/*
		 * Send a packet designed to force a response if the peer is
		 * up and reachable: either an ACK if the connection is
		 * still alive, or an RST if the peer has closed the
		 * connection due to timeout or reboot. Using sequence
		 * number tp->snd_una-1 causes the transmitted zero-length
		 * segment to lie outside the receive window; by the
		 * protocol spec, this requires the correspondent TCP to
		 * respond.
		 */
		TCPSTAT_INC(tcps_keepprobe);
		t_template = tcpip_maketemplate(inp);
		if (t_template) {
			tcp_respond(tp, t_template->tt_ipgen,
			    &t_template->tt_t, (struct mbuf *)NULL,
			    tp->rcv_nxt, tp->snd_una - 1, 0);
			free(t_template, M_TEMP);
		}
	}
	rack_start_hpts_timer(rack, tp, cts, __LINE__, 0, 0, 0);
	return (1);
dropit:
	TCPSTAT_INC(tcps_keepdrops);
	tcp_set_inp_to_drop(rack->rc_inp, ETIMEDOUT);
	return (1);
}

/*
 * Retransmit helper function, clear up all the ack
 * flags and take care of important book keeping.
 */
static void
rack_remxt_tmr(struct tcpcb *tp)
{
	/*
	 * The retransmit timer went off, all sack'd blocks must be
	 * un-acked.
	 */
	struct rack_sendmap *rsm, *trsm = NULL;
	struct tcp_rack *rack;
	int32_t cnt = 0;

	rack = (struct tcp_rack *)tp->t_fb_ptr;
	rack_timer_cancel(tp, rack, tcp_ts_getticks(), __LINE__);
	rack_log_to_event(rack, RACK_TO_FRM_TMR);
	if (rack->r_state && (rack->r_state != tp->t_state))
		rack_set_state(tp, rack);
	/*
	 * Ideally we would like to be able to
	 * mark SACK-PASS on anything not acked here.
	 * However, if we do that we would burst out
	 * all that data 1ms apart. This would be unwise,
	 * so for now we will just let the normal rxt timer
	 * and tlp timer take care of it.
	 */
	TAILQ_FOREACH(rsm, &rack->r_ctl.rc_map, r_next) {
		if (rsm->r_flags & RACK_ACKED) {
			cnt++;
			rsm->r_sndcnt = 0;
			if (rsm->r_in_tmap == 0) {
				/* We must re-add it back to the tlist */
				if (trsm == NULL) {
					TAILQ_INSERT_HEAD(&rack->r_ctl.rc_tmap, rsm, r_tnext);
				} else {
					TAILQ_INSERT_AFTER(&rack->r_ctl.rc_tmap, trsm, rsm, r_tnext);
				}
				rsm->r_in_tmap = 1;
				trsm = rsm;
			}
		}
		rsm->r_flags &= ~(RACK_ACKED | RACK_SACK_PASSED | RACK_WAS_SACKPASS);
	}
	/* Clear the count (we just un-acked them) */
	rack->r_ctl.rc_sacked = 0;
	/* Clear the tlp rtx mark */
	rack->r_ctl.rc_tlp_rtx_out = 0;
	rack->r_ctl.rc_tlp_seg_send_cnt = 0;
	rack->r_ctl.rc_resend = TAILQ_FIRST(&rack->r_ctl.rc_map);
	/* Setup so we send one segment */
	if (rack->r_ctl.rc_prr_sndcnt < tp->t_maxseg)
		rack->r_ctl.rc_prr_sndcnt = tp->t_maxseg;
	rack->r_timer_override = 1;
}

/*
 * Re-transmit timeout! If we drop the PCB we will return 1, otherwise
 * we will setup to retransmit the lowest seq number outstanding.
 */
static int
rack_timeout_rxt(struct tcpcb *tp, struct tcp_rack *rack, uint32_t cts)
{
	int32_t rexmt;
	struct inpcb *inp;
	int32_t retval = 0;

	inp = tp->t_inpcb;
	if (tp->t_timers->tt_flags & TT_STOPPED) {
		return (1);
	}
	if (rack_progress_timeout_check(tp)) {
		tcp_set_inp_to_drop(inp, ETIMEDOUT);
		return (1);
	}
	rack->r_ctl.rc_hpts_flags &= ~PACE_TMR_RXT;
	if (TCPS_HAVEESTABLISHED(tp->t_state) &&
	    (tp->snd_una == tp->snd_max)) {
		/* Nothing outstanding .. nothing to do */
		return (0);
	}
	/*
	 * Retransmission timer went off.  Message has not been acked within
	 * retransmit interval.  Back off to a longer retransmit interval
	 * and retransmit one segment.
	 */
	if (++tp->t_rxtshift > TCP_MAXRXTSHIFT) {
		tp->t_rxtshift = TCP_MAXRXTSHIFT;
		TCPSTAT_INC(tcps_timeoutdrop);
		retval = 1;
		tcp_set_inp_to_drop(rack->rc_inp,
		    (tp->t_softerror ? (uint16_t) tp->t_softerror : ETIMEDOUT));
		goto out;
	}
	rack_remxt_tmr(tp);
	if (tp->t_state == TCPS_SYN_SENT) {
		/*
		 * If the SYN was retransmitted, indicate CWND to be limited
		 * to 1 segment in cc_conn_init().
		 */
		tp->snd_cwnd = 1;
	} else if (tp->t_rxtshift == 1) {
		/*
		 * first retransmit; record ssthresh and cwnd so they can be
		 * recovered if this turns out to be a "bad" retransmit. A
		 * retransmit is considered "bad" if an ACK for this segment
		 * is received within RTT/2 interval; the assumption here is
		 * that the ACK was already in flight.  See "On Estimating
		 * End-to-End Network Path Properties" by Allman and Paxson
		 * for more details.
		 */
		tp->snd_cwnd_prev = tp->snd_cwnd;
		tp->snd_ssthresh_prev = tp->snd_ssthresh;
		tp->snd_recover_prev = tp->snd_recover;
		if (IN_FASTRECOVERY(tp->t_flags))
			tp->t_flags |= TF_WASFRECOVERY;
		else
			tp->t_flags &= ~TF_WASFRECOVERY;
		if (IN_CONGRECOVERY(tp->t_flags))
			tp->t_flags |= TF_WASCRECOVERY;
		else
			tp->t_flags &= ~TF_WASCRECOVERY;
		tp->t_badrxtwin = ticks + (tp->t_srtt >> (TCP_RTT_SHIFT + 1));
		tp->t_flags |= TF_PREVVALID;
	} else
		tp->t_flags &= ~TF_PREVVALID;
	TCPSTAT_INC(tcps_rexmttimeo);
	if ((tp->t_state == TCPS_SYN_SENT) ||
	    (tp->t_state == TCPS_SYN_RECEIVED))
		rexmt = MSEC_2_TICKS(RACK_INITIAL_RTO * tcp_backoff[tp->t_rxtshift]);
	else
		rexmt = TCP_REXMTVAL(tp) * tcp_backoff[tp->t_rxtshift];
	TCPT_RANGESET(tp->t_rxtcur, rexmt,
	   max(MSEC_2_TICKS(rack_rto_min), rexmt),
	   MSEC_2_TICKS(rack_rto_max));
	/*
	 * We enter the path for PLMTUD if connection is established or, if
	 * connection is FIN_WAIT_1 status, reason for the last is that if
	 * amount of data we send is very small, we could send it in couple
	 * of packets and process straight to FIN. In that case we won't
	 * catch ESTABLISHED state.
	 */
	if (V_tcp_pmtud_blackhole_detect && (((tp->t_state == TCPS_ESTABLISHED))
	    || (tp->t_state == TCPS_FIN_WAIT_1))) {
#ifdef INET6
		int32_t isipv6;
#endif

		/*
		 * Idea here is that at each stage of mtu probe (usually,
		 * 1448 -> 1188 -> 524) should be given 2 chances to recover
		 * before further clamping down. 'tp->t_rxtshift % 2 == 0'
		 * should take care of that.
		 */
		if (((tp->t_flags2 & (TF2_PLPMTU_PMTUD | TF2_PLPMTU_MAXSEGSNT)) ==
		    (TF2_PLPMTU_PMTUD | TF2_PLPMTU_MAXSEGSNT)) &&
		    (tp->t_rxtshift >= 2 && tp->t_rxtshift < 6 &&
		    tp->t_rxtshift % 2 == 0)) {
			/*
			 * Enter Path MTU Black-hole Detection mechanism: -
			 * Disable Path MTU Discovery (IP "DF" bit). -
			 * Reduce MTU to lower value than what we negotiated
			 * with peer.
			 */
			if ((tp->t_flags2 & TF2_PLPMTU_BLACKHOLE) == 0) {
				/* Record that we may have found a black hole. */
				tp->t_flags2 |= TF2_PLPMTU_BLACKHOLE;
				/* Keep track of previous MSS. */
				tp->t_pmtud_saved_maxseg = tp->t_maxseg;
			}

			/*
			 * Reduce the MSS to blackhole value or to the
			 * default in an attempt to retransmit.
			 */
#ifdef INET6
			isipv6 = (tp->t_inpcb->inp_vflag & INP_IPV6) ? 1 : 0;
			if (isipv6 &&
			    tp->t_maxseg > V_tcp_v6pmtud_blackhole_mss) {
				/* Use the sysctl tuneable blackhole MSS. */
				tp->t_maxseg = V_tcp_v6pmtud_blackhole_mss;
				TCPSTAT_INC(tcps_pmtud_blackhole_activated);
			} else if (isipv6) {
				/* Use the default MSS. */
				tp->t_maxseg = V_tcp_v6mssdflt;
				/*
				 * Disable Path MTU Discovery when we switch
				 * to minmss.
				 */
				tp->t_flags2 &= ~TF2_PLPMTU_PMTUD;
				TCPSTAT_INC(tcps_pmtud_blackhole_activated_min_mss);
			}
#endif
#if defined(INET6) && defined(INET)
			else
#endif
#ifdef INET
			if (tp->t_maxseg > V_tcp_pmtud_blackhole_mss) {
				/* Use the sysctl tuneable blackhole MSS. */
				tp->t_maxseg = V_tcp_pmtud_blackhole_mss;
				TCPSTAT_INC(tcps_pmtud_blackhole_activated);
			} else {
				/* Use the default MSS. */
				tp->t_maxseg = V_tcp_mssdflt;
				/*
				 * Disable Path MTU Discovery when we switch
				 * to minmss.
				 */
				tp->t_flags2 &= ~TF2_PLPMTU_PMTUD;
				TCPSTAT_INC(tcps_pmtud_blackhole_activated_min_mss);
			}
#endif
		} else {
			/*
			 * If further retransmissions are still unsuccessful
			 * with a lowered MTU, maybe this isn't a blackhole
			 * and we restore the previous MSS and blackhole
			 * detection flags. The limit '6' is determined by
			 * giving each probe stage (1448, 1188, 524) 2
			 * chances to recover.
			 */
			if ((tp->t_flags2 & TF2_PLPMTU_BLACKHOLE) &&
			    (tp->t_rxtshift >= 6)) {
				tp->t_flags2 |= TF2_PLPMTU_PMTUD;
				tp->t_flags2 &= ~TF2_PLPMTU_BLACKHOLE;
				tp->t_maxseg = tp->t_pmtud_saved_maxseg;
				TCPSTAT_INC(tcps_pmtud_blackhole_failed);
			}
		}
	}
	/*
	 * Disable RFC1323 and SACK if we haven't got any response to our
	 * third SYN to work-around some broken terminal servers (most of
	 * which have hopefully been retired) that have bad VJ header
	 * compression code which trashes TCP segments containing
	 * unknown-to-them TCP options.
	 */
	if (tcp_rexmit_drop_options && (tp->t_state == TCPS_SYN_SENT) &&
	    (tp->t_rxtshift == 3))
		tp->t_flags &= ~(TF_REQ_SCALE | TF_REQ_TSTMP | TF_SACK_PERMIT);
	/*
	 * If we backed off this far, our srtt estimate is probably bogus.
	 * Clobber it so we'll take the next rtt measurement as our srtt;
	 * move the current srtt into rttvar to keep the current retransmit
	 * times until then.
	 */
	if (tp->t_rxtshift > TCP_MAXRXTSHIFT / 4) {
#ifdef INET6
		if ((tp->t_inpcb->inp_vflag & INP_IPV6) != 0)
			in6_losing(tp->t_inpcb);
		else
#endif
			in_losing(tp->t_inpcb);
		tp->t_rttvar += (tp->t_srtt >> TCP_RTT_SHIFT);
		tp->t_srtt = 0;
	}
	if (rack_use_sack_filter)
		sack_filter_clear(&rack->r_ctl.rack_sf, tp->snd_una);
	tp->snd_recover = tp->snd_max;
	tp->t_flags |= TF_ACKNOW;
	tp->t_rtttime = 0;
	rack_cong_signal(tp, NULL, CC_RTO);
out:
	return (retval);
}

static int
rack_process_timers(struct tcpcb *tp, struct tcp_rack *rack, uint32_t cts, uint8_t hpts_calling)
{
	int32_t ret = 0;
	int32_t timers = (rack->r_ctl.rc_hpts_flags & PACE_TMR_MASK);

	if (timers == 0) {
		return (0);
	}
	if (tp->t_state == TCPS_LISTEN) {
		/* no timers on listen sockets */
		if (rack->r_ctl.rc_hpts_flags & PACE_PKT_OUTPUT)
			return (0);
		return (1);
	}
	if (TSTMP_LT(cts, rack->r_ctl.rc_timer_exp)) {
		uint32_t left;

		if (rack->r_ctl.rc_hpts_flags & PACE_PKT_OUTPUT) {
			ret = -1;
			rack_log_to_processing(rack, cts, ret, 0);
			return (0);
		}
		if (hpts_calling == 0) {
			ret = -2;
			rack_log_to_processing(rack, cts, ret, 0);
			return (0);
		}
		/*
		 * Ok our timer went off early and we are not paced false
		 * alarm, go back to sleep.
		 */
		ret = -3;
		left = rack->r_ctl.rc_timer_exp - cts;
		tcp_hpts_insert(tp->t_inpcb, HPTS_MS_TO_SLOTS(left));
		rack_log_to_processing(rack, cts, ret, left);
		rack->rc_last_pto_set = 0;
		return (1);
	}
	rack->rc_tmr_stopped = 0;
	rack->r_ctl.rc_hpts_flags &= ~PACE_TMR_MASK;
	if (timers & PACE_TMR_DELACK) {
		ret = rack_timeout_delack(tp, rack, cts);
	} else if (timers & PACE_TMR_RACK) {
		ret = rack_timeout_rack(tp, rack, cts);
	} else if (timers & PACE_TMR_TLP) {
		ret = rack_timeout_tlp(tp, rack, cts);
	} else if (timers & PACE_TMR_RXT) {
		ret = rack_timeout_rxt(tp, rack, cts);
	} else if (timers & PACE_TMR_PERSIT) {
		ret = rack_timeout_persist(tp, rack, cts);
	} else if (timers & PACE_TMR_KEEP) {
		ret = rack_timeout_keepalive(tp, rack, cts);
	}
	rack_log_to_processing(rack, cts, ret, timers);
	return (ret);
}

static void
rack_timer_cancel(struct tcpcb *tp, struct tcp_rack *rack, uint32_t cts, int line)
{
	uint8_t hpts_removed = 0;

	if ((rack->r_ctl.rc_hpts_flags & PACE_PKT_OUTPUT) &&
	    TSTMP_GEQ(cts, rack->r_ctl.rc_last_output_to)) {
		tcp_hpts_remove(rack->rc_inp, HPTS_REMOVE_OUTPUT);
		hpts_removed = 1;
	}
	if (rack->r_ctl.rc_hpts_flags & PACE_TMR_MASK) {
		rack->rc_tmr_stopped = rack->r_ctl.rc_hpts_flags & PACE_TMR_MASK;
		if (rack->rc_inp->inp_in_hpts &&
		    ((rack->r_ctl.rc_hpts_flags & PACE_PKT_OUTPUT) == 0)) {
			/*
			 * Canceling timer's when we have no output being
			 * paced. We also must remove ourselves from the
			 * hpts.
			 */
			tcp_hpts_remove(rack->rc_inp, HPTS_REMOVE_OUTPUT);
			hpts_removed = 1;
		}
		rack_log_to_cancel(rack, hpts_removed, line);
		rack->r_ctl.rc_hpts_flags &= ~(PACE_TMR_MASK);
	}
}

static void
rack_timer_stop(struct tcpcb *tp, uint32_t timer_type)
{
	return;
}

static int
rack_stopall(struct tcpcb *tp)
{
	struct tcp_rack *rack;
	rack = (struct tcp_rack *)tp->t_fb_ptr;
	rack->t_timers_stopped = 1;
	return (0);
}

static void
rack_timer_activate(struct tcpcb *tp, uint32_t timer_type, uint32_t delta)
{
	return;
}

static int
rack_timer_active(struct tcpcb *tp, uint32_t timer_type)
{
	return (0);
}

static void
rack_stop_all_timers(struct tcpcb *tp)
{
	struct tcp_rack *rack;

	/*
	 * Assure no timers are running.
	 */
	if (tcp_timer_active(tp, TT_PERSIST)) {
		/* We enter in persists, set the flag appropriately */
		rack = (struct tcp_rack *)tp->t_fb_ptr;
		rack->rc_in_persist = 1;
	}
	tcp_timer_suspend(tp, TT_PERSIST);
	tcp_timer_suspend(tp, TT_REXMT);
	tcp_timer_suspend(tp, TT_KEEP);
	tcp_timer_suspend(tp, TT_DELACK);
}

static void
rack_update_rsm(struct tcpcb *tp, struct tcp_rack *rack,
    struct rack_sendmap *rsm, uint32_t ts)
{
	int32_t idx;

	rsm->r_rtr_cnt++;
	rsm->r_sndcnt++;
	if (rsm->r_rtr_cnt > RACK_NUM_OF_RETRANS) {
		rsm->r_rtr_cnt = RACK_NUM_OF_RETRANS;
		rsm->r_flags |= RACK_OVERMAX;
	}
	if ((rsm->r_rtr_cnt > 1) && (rack->r_tlp_running == 0)) {
		rack->r_ctl.rc_holes_rxt += (rsm->r_end - rsm->r_start);
		rsm->r_rtr_bytes += (rsm->r_end - rsm->r_start);
	}
	idx = rsm->r_rtr_cnt - 1;
	rsm->r_tim_lastsent[idx] = ts;
	if (rsm->r_flags & RACK_ACKED) {
		/* Problably MTU discovery messing with us */
		rsm->r_flags &= ~RACK_ACKED;
		rack->r_ctl.rc_sacked -= (rsm->r_end - rsm->r_start);
	}
	if (rsm->r_in_tmap) {
		TAILQ_REMOVE(&rack->r_ctl.rc_tmap, rsm, r_tnext);
	}
	TAILQ_INSERT_TAIL(&rack->r_ctl.rc_tmap, rsm, r_tnext);
	rsm->r_in_tmap = 1;
	if (rsm->r_flags & RACK_SACK_PASSED) {
		/* We have retransmitted due to the SACK pass */
		rsm->r_flags &= ~RACK_SACK_PASSED;
		rsm->r_flags |= RACK_WAS_SACKPASS;
	}
	/* Update memory for next rtr */
	rack->r_ctl.rc_next = TAILQ_NEXT(rsm, r_next);
}


static uint32_t
rack_update_entry(struct tcpcb *tp, struct tcp_rack *rack,
    struct rack_sendmap *rsm, uint32_t ts, int32_t * lenp)
{
	/*
	 * We (re-)transmitted starting at rsm->r_start for some length
	 * (possibly less than r_end.
	 */
	struct rack_sendmap *nrsm;
	uint32_t c_end;
	int32_t len;
	int32_t idx;

	len = *lenp;
	c_end = rsm->r_start + len;
	if (SEQ_GEQ(c_end, rsm->r_end)) {
		/*
		 * We retransmitted the whole piece or more than the whole
		 * slopping into the next rsm.
		 */
		rack_update_rsm(tp, rack, rsm, ts);
		if (c_end == rsm->r_end) {
			*lenp = 0;
			return (0);
		} else {
			int32_t act_len;

			/* Hangs over the end return whats left */
			act_len = rsm->r_end - rsm->r_start;
			*lenp = (len - act_len);
			return (rsm->r_end);
		}
		/* We don't get out of this block. */
	}
	/*
	 * Here we retransmitted less than the whole thing which means we
	 * have to split this into what was transmitted and what was not.
	 */
	nrsm = rack_alloc(rack);
	if (nrsm == NULL) {
		/*
		 * We can't get memory, so lets not proceed.
		 */
		*lenp = 0;
		return (0);
	}
	/*
	 * So here we are going to take the original rsm and make it what we
	 * retransmitted. nrsm will be the tail portion we did not
	 * retransmit. For example say the chunk was 1, 11 (10 bytes). And
	 * we retransmitted 5 bytes i.e. 1, 5. The original piece shrinks to
	 * 1, 6 and the new piece will be 6, 11.
	 */
	nrsm->r_start = c_end;
	nrsm->r_end = rsm->r_end;
	nrsm->r_rtr_cnt = rsm->r_rtr_cnt;
	nrsm->r_flags = rsm->r_flags;
	nrsm->r_sndcnt = rsm->r_sndcnt;
	nrsm->r_rtr_bytes = 0;
	rsm->r_end = c_end;
	for (idx = 0; idx < nrsm->r_rtr_cnt; idx++) {
		nrsm->r_tim_lastsent[idx] = rsm->r_tim_lastsent[idx];
	}
	TAILQ_INSERT_AFTER(&rack->r_ctl.rc_map, rsm, nrsm, r_next);
	if (rsm->r_in_tmap) {
		TAILQ_INSERT_AFTER(&rack->r_ctl.rc_tmap, rsm, nrsm, r_tnext);
		nrsm->r_in_tmap = 1;
	}
	rsm->r_flags &= (~RACK_HAS_FIN);
	rack_update_rsm(tp, rack, rsm, ts);
	*lenp = 0;
	return (0);
}


static void
rack_log_output(struct tcpcb *tp, struct tcpopt *to, int32_t len,
    uint32_t seq_out, uint8_t th_flags, int32_t err, uint32_t ts,
    uint8_t pass, struct rack_sendmap *hintrsm)
{
	struct tcp_rack *rack;
	struct rack_sendmap *rsm, *nrsm;
	register uint32_t snd_max, snd_una;
	int32_t idx;

	/*
	 * Add to the RACK log of packets in flight or retransmitted. If
	 * there is a TS option we will use the TS echoed, if not we will
	 * grab a TS.
	 *
	 * Retransmissions will increment the count and move the ts to its
	 * proper place. Note that if options do not include TS's then we
	 * won't be able to effectively use the ACK for an RTT on a retran.
	 *
	 * Notes about r_start and r_end. Lets consider a send starting at
	 * sequence 1 for 10 bytes. In such an example the r_start would be
	 * 1 (starting sequence) but the r_end would be r_start+len i.e. 11.
	 * This means that r_end is actually the first sequence for the next
	 * slot (11).
	 *
	 */
	/*
	 * If err is set what do we do XXXrrs? should we not add the thing?
	 * -- i.e. return if err != 0 or should we pretend we sent it? --
	 * i.e. proceed with add ** do this for now.
	 */
	INP_WLOCK_ASSERT(tp->t_inpcb);
	if (err)
		/*
		 * We don't log errors -- we could but snd_max does not
		 * advance in this case either.
		 */
		return;

	if (th_flags & TH_RST) {
		/*
		 * We don't log resets and we return immediately from
		 * sending
		 */
		return;
	}
	rack = (struct tcp_rack *)tp->t_fb_ptr;
	snd_una = tp->snd_una;
	if (SEQ_LEQ((seq_out + len), snd_una)) {
		/* Are sending an old segment to induce an ack (keep-alive)? */
		return;
	}
	if (SEQ_LT(seq_out, snd_una)) {
		/* huh? should we panic? */
		uint32_t end;

		end = seq_out + len;
		seq_out = snd_una;
		len = end - seq_out;
	}
	snd_max = tp->snd_max;
	if (th_flags & (TH_SYN | TH_FIN)) {
		/*
		 * The call to rack_log_output is made before bumping
		 * snd_max. This means we can record one extra byte on a SYN
		 * or FIN if seq_out is adding more on and a FIN is present
		 * (and we are not resending).
		 */
		if (th_flags & TH_SYN)
			len++;
		if (th_flags & TH_FIN)
			len++;
		if (SEQ_LT(snd_max, tp->snd_nxt)) {
			/*
			 * The add/update as not been done for the FIN/SYN
			 * yet.
			 */
			snd_max = tp->snd_nxt;
		}
	}
	if (len == 0) {
		/* We don't log zero window probes */
		return;
	}
	rack->r_ctl.rc_time_last_sent = ts;
	if (IN_RECOVERY(tp->t_flags)) {
		rack->r_ctl.rc_prr_out += len;
	}
	/* First question is it a retransmission? */
	if (seq_out == snd_max) {
again:
		rsm = rack_alloc(rack);
		if (rsm == NULL) {
			/*
			 * Hmm out of memory and the tcb got destroyed while
			 * we tried to wait.
			 */
#ifdef INVARIANTS
			panic("Out of memory when we should not be rack:%p", rack);
#endif
			return;
		}
		if (th_flags & TH_FIN) {
			rsm->r_flags = RACK_HAS_FIN;
		} else {
			rsm->r_flags = 0;
		}
		rsm->r_tim_lastsent[0] = ts;
		rsm->r_rtr_cnt = 1;
		rsm->r_rtr_bytes = 0;
		if (th_flags & TH_SYN) {
			/* The data space is one beyond snd_una */
			rsm->r_start = seq_out + 1;
			rsm->r_end = rsm->r_start + (len - 1);
		} else {
			/* Normal case */
			rsm->r_start = seq_out;
			rsm->r_end = rsm->r_start + len;
		}
		rsm->r_sndcnt = 0;
		TAILQ_INSERT_TAIL(&rack->r_ctl.rc_map, rsm, r_next);
		TAILQ_INSERT_TAIL(&rack->r_ctl.rc_tmap, rsm, r_tnext);
		rsm->r_in_tmap = 1;
		return;
	}
	/*
	 * If we reach here its a retransmission and we need to find it.
	 */
more:
	if (hintrsm && (hintrsm->r_start == seq_out)) {
		rsm = hintrsm;
		hintrsm = NULL;
	} else if (rack->r_ctl.rc_next) {
		/* We have a hint from a previous run */
		rsm = rack->r_ctl.rc_next;
	} else {
		/* No hints sorry */
		rsm = NULL;
	}
	if ((rsm) && (rsm->r_start == seq_out)) {
		/*
		 * We used rc_next or hintrsm  to retransmit, hopefully the
		 * likely case.
		 */
		seq_out = rack_update_entry(tp, rack, rsm, ts, &len);
		if (len == 0) {
			return;
		} else {
			goto more;
		}
	}
	/* Ok it was not the last pointer go through it the hard way. */
	TAILQ_FOREACH(rsm, &rack->r_ctl.rc_map, r_next) {
		if (rsm->r_start == seq_out) {
			seq_out = rack_update_entry(tp, rack, rsm, ts, &len);
			rack->r_ctl.rc_next = TAILQ_NEXT(rsm, r_next);
			if (len == 0) {
				return;
			} else {
				continue;
			}
		}
		if (SEQ_GEQ(seq_out, rsm->r_start) && SEQ_LT(seq_out, rsm->r_end)) {
			/* Transmitted within this piece */
			/*
			 * Ok we must split off the front and then let the
			 * update do the rest
			 */
			nrsm = rack_alloc(rack);
			if (nrsm == NULL) {
#ifdef INVARIANTS
				panic("Ran out of memory that was preallocated? rack:%p", rack);
#endif
				rack_update_rsm(tp, rack, rsm, ts);
				return;
			}
			/*
			 * copy rsm to nrsm and then trim the front of rsm
			 * to not include this part.
			 */
			nrsm->r_start = seq_out;
			nrsm->r_end = rsm->r_end;
			nrsm->r_rtr_cnt = rsm->r_rtr_cnt;
			nrsm->r_flags = rsm->r_flags;
			nrsm->r_sndcnt = rsm->r_sndcnt;
			nrsm->r_rtr_bytes = 0;
			for (idx = 0; idx < nrsm->r_rtr_cnt; idx++) {
				nrsm->r_tim_lastsent[idx] = rsm->r_tim_lastsent[idx];
			}
			rsm->r_end = nrsm->r_start;
			TAILQ_INSERT_AFTER(&rack->r_ctl.rc_map, rsm, nrsm, r_next);
			if (rsm->r_in_tmap) {
				TAILQ_INSERT_AFTER(&rack->r_ctl.rc_tmap, rsm, nrsm, r_tnext);
				nrsm->r_in_tmap = 1;
			}
			rsm->r_flags &= (~RACK_HAS_FIN);
			seq_out = rack_update_entry(tp, rack, nrsm, ts, &len);
			if (len == 0) {
				return;
			}
		}
	}
	/*
	 * Hmm not found in map did they retransmit both old and on into the
	 * new?
	 */
	if (seq_out == tp->snd_max) {
		goto again;
	} else if (SEQ_LT(seq_out, tp->snd_max)) {
#ifdef INVARIANTS
		printf("seq_out:%u len:%d snd_una:%u snd_max:%u -- but rsm not found?\n",
		    seq_out, len, tp->snd_una, tp->snd_max);
		printf("Starting Dump of all rack entries\n");
		TAILQ_FOREACH(rsm, &rack->r_ctl.rc_map, r_next) {
			printf("rsm:%p start:%u end:%u\n",
			    rsm, rsm->r_start, rsm->r_end);
		}
		printf("Dump complete\n");
		panic("seq_out not found rack:%p tp:%p",
		    rack, tp);
#endif
	} else {
#ifdef INVARIANTS
		/*
		 * Hmm beyond sndmax? (only if we are using the new rtt-pack
		 * flag)
		 */
		panic("seq_out:%u(%d) is beyond snd_max:%u tp:%p",
		    seq_out, len, tp->snd_max, tp);
#endif
	}
}

/*
 * Record one of the RTT updates from an ack into
 * our sample structure.
 */
static void
tcp_rack_xmit_timer(struct tcp_rack *rack, int32_t rtt)
{
	if ((rack->r_ctl.rack_rs.rs_flags & RACK_RTT_EMPTY) ||
	    (rack->r_ctl.rack_rs.rs_rtt_lowest > rtt)) {
		rack->r_ctl.rack_rs.rs_rtt_lowest = rtt;
	}
	if ((rack->r_ctl.rack_rs.rs_flags & RACK_RTT_EMPTY) ||
	    (rack->r_ctl.rack_rs.rs_rtt_highest < rtt)) {
		rack->r_ctl.rack_rs.rs_rtt_highest = rtt;
	}
	rack->r_ctl.rack_rs.rs_flags = RACK_RTT_VALID;
	rack->r_ctl.rack_rs.rs_rtt_tot += rtt;
	rack->r_ctl.rack_rs.rs_rtt_cnt++;
}

/*
 * Collect new round-trip time estimate
 * and update averages and current timeout.
 */
static void
tcp_rack_xmit_timer_commit(struct tcp_rack *rack, struct tcpcb *tp)
{
	int32_t delta;
	uint32_t o_srtt, o_var;
	int32_t rtt;

	if (rack->r_ctl.rack_rs.rs_flags & RACK_RTT_EMPTY)
		/* No valid sample */
		return;
	if (rack->r_ctl.rc_rate_sample_method == USE_RTT_LOW) {
		/* We are to use the lowest RTT seen in a single ack */
		rtt = rack->r_ctl.rack_rs.rs_rtt_lowest;
	} else if (rack->r_ctl.rc_rate_sample_method == USE_RTT_HIGH) {
		/* We are to use the highest RTT seen in a single ack */
		rtt = rack->r_ctl.rack_rs.rs_rtt_highest;
	} else if (rack->r_ctl.rc_rate_sample_method == USE_RTT_AVG) {
		/* We are to use the average RTT seen in a single ack */
		rtt = (int32_t)(rack->r_ctl.rack_rs.rs_rtt_tot /
				(uint64_t)rack->r_ctl.rack_rs.rs_rtt_cnt);
	} else {
#ifdef INVARIANTS
		panic("Unknown rtt variant %d", rack->r_ctl.rc_rate_sample_method);
#endif		
		return;
	}
	if (rtt == 0)
		rtt = 1;
	rack_log_rtt_sample(rack, rtt);
	o_srtt = tp->t_srtt;
	o_var = tp->t_rttvar;
	rack = (struct tcp_rack *)tp->t_fb_ptr;
	if (tp->t_srtt != 0) {
		/*
		 * srtt is stored as fixed point with 5 bits after the
		 * binary point (i.e., scaled by 8).  The following magic is
		 * equivalent to the smoothing algorithm in rfc793 with an
		 * alpha of .875 (srtt = rtt/8 + srtt*7/8 in fixed point).
		 * Adjust rtt to origin 0.
		 */
		delta = ((rtt - 1) << TCP_DELTA_SHIFT)
		    - (tp->t_srtt >> (TCP_RTT_SHIFT - TCP_DELTA_SHIFT));

		tp->t_srtt += delta;
		if (tp->t_srtt <= 0)
			tp->t_srtt = 1;

		/*
		 * We accumulate a smoothed rtt variance (actually, a
		 * smoothed mean difference), then set the retransmit timer
		 * to smoothed rtt + 4 times the smoothed variance. rttvar
		 * is stored as fixed point with 4 bits after the binary
		 * point (scaled by 16).  The following is equivalent to
		 * rfc793 smoothing with an alpha of .75 (rttvar =
		 * rttvar*3/4 + |delta| / 4).  This replaces rfc793's
		 * wired-in beta.
		 */
		if (delta < 0)
			delta = -delta;
		delta -= tp->t_rttvar >> (TCP_RTTVAR_SHIFT - TCP_DELTA_SHIFT);
		tp->t_rttvar += delta;
		if (tp->t_rttvar <= 0)
			tp->t_rttvar = 1;
		if (tp->t_rttbest > tp->t_srtt + tp->t_rttvar)
			tp->t_rttbest = tp->t_srtt + tp->t_rttvar;
	} else {
		/*
		 * No rtt measurement yet - use the unsmoothed rtt. Set the
		 * variance to half the rtt (so our first retransmit happens
		 * at 3*rtt).
		 */
		tp->t_srtt = rtt << TCP_RTT_SHIFT;
		tp->t_rttvar = rtt << (TCP_RTTVAR_SHIFT - 1);
		tp->t_rttbest = tp->t_srtt + tp->t_rttvar;
	}
	TCPSTAT_INC(tcps_rttupdated);
	rack_log_rtt_upd(tp, rack, rtt, o_srtt, o_var);
	tp->t_rttupdated++;
#ifdef NETFLIX_STATS
	stats_voi_update_abs_u32(tp->t_stats, VOI_TCP_RTT, imax(0, rtt));
#endif
	tp->t_rxtshift = 0;

	/*
	 * the retransmit should happen at rtt + 4 * rttvar. Because of the
	 * way we do the smoothing, srtt and rttvar will each average +1/2
	 * tick of bias.  When we compute the retransmit timer, we want 1/2
	 * tick of rounding and 1 extra tick because of +-1/2 tick
	 * uncertainty in the firing of the timer.  The bias will give us
	 * exactly the 1.5 tick we need.  But, because the bias is
	 * statistical, we have to test that we don't drop below the minimum
	 * feasible timer (which is 2 ticks).
	 */
	TCPT_RANGESET(tp->t_rxtcur, TCP_REXMTVAL(tp),
	   max(MSEC_2_TICKS(rack_rto_min), rtt + 2), MSEC_2_TICKS(rack_rto_max));
	tp->t_softerror = 0;
}

static void
rack_earlier_retran(struct tcpcb *tp, struct rack_sendmap *rsm,
    uint32_t t, uint32_t cts)
{
	/*
	 * For this RSM, we acknowledged the data from a previous
	 * transmission, not the last one we made. This means we did a false
	 * retransmit.
	 */
	struct tcp_rack *rack;

	if (rsm->r_flags & RACK_HAS_FIN) {
		/*
		 * The sending of the FIN often is multiple sent when we
		 * have everything outstanding ack'd. We ignore this case
		 * since its over now.
		 */
		return;
	}
	if (rsm->r_flags & RACK_TLP) {
		/*
		 * We expect TLP's to have this occur.
		 */
		return;
	}
	rack = (struct tcp_rack *)tp->t_fb_ptr;
	/* should we undo cc changes and exit recovery? */
	if (IN_RECOVERY(tp->t_flags)) {
		if (rack->r_ctl.rc_rsm_start == rsm->r_start) {
			/*
			 * Undo what we ratched down and exit recovery if
			 * possible
			 */
			EXIT_RECOVERY(tp->t_flags);
			tp->snd_recover = tp->snd_una;
			if (rack->r_ctl.rc_cwnd_at > tp->snd_cwnd)
				tp->snd_cwnd = rack->r_ctl.rc_cwnd_at;
			if (rack->r_ctl.rc_ssthresh_at > tp->snd_ssthresh)
				tp->snd_ssthresh = rack->r_ctl.rc_ssthresh_at;
		}
	}
	if (rsm->r_flags & RACK_WAS_SACKPASS) {
		/*
		 * We retransmitted based on a sack and the earlier
		 * retransmission ack'd it - re-ordering is occuring.
		 */
		counter_u64_add(rack_reorder_seen, 1);
		rack->r_ctl.rc_reorder_ts = cts;
	}
	counter_u64_add(rack_badfr, 1);
	counter_u64_add(rack_badfr_bytes, (rsm->r_end - rsm->r_start));
}


static int
rack_update_rtt(struct tcpcb *tp, struct tcp_rack *rack,
    struct rack_sendmap *rsm, struct tcpopt *to, uint32_t cts, int32_t ack_type)
{
	int32_t i;
	uint32_t t;

	if (rsm->r_flags & RACK_ACKED)
		/* Already done */
		return (0);


	if ((rsm->r_rtr_cnt == 1) ||
	    ((ack_type == CUM_ACKED) &&
	    (to->to_flags & TOF_TS) &&
	    (to->to_tsecr) &&
	    (rsm->r_tim_lastsent[rsm->r_rtr_cnt - 1] == to->to_tsecr))
	    ) {
		/*
		 * We will only find a matching timestamp if its cum-acked.
		 * But if its only one retransmission its for-sure matching
		 * :-)
		 */
		t = cts - rsm->r_tim_lastsent[(rsm->r_rtr_cnt - 1)];
		if ((int)t <= 0)
			t = 1;
		if (!tp->t_rttlow || tp->t_rttlow > t)
			tp->t_rttlow = t;
		if (!rack->r_ctl.rc_rack_min_rtt ||
		    SEQ_LT(t, rack->r_ctl.rc_rack_min_rtt)) {
			rack->r_ctl.rc_rack_min_rtt = t;
			if (rack->r_ctl.rc_rack_min_rtt == 0) {
				rack->r_ctl.rc_rack_min_rtt = 1;
			}
		}
		tcp_rack_xmit_timer(rack, TCP_TS_TO_TICKS(t) + 1);
		if ((rsm->r_flags & RACK_TLP) &&
		    (!IN_RECOVERY(tp->t_flags))) {
			/* Segment was a TLP and our retrans matched */
			if (rack->r_ctl.rc_tlp_cwnd_reduce) {
				rack->r_ctl.rc_rsm_start = tp->snd_max;
				rack->r_ctl.rc_cwnd_at = tp->snd_cwnd;
				rack->r_ctl.rc_ssthresh_at = tp->snd_ssthresh;
				rack_cong_signal(tp, NULL, CC_NDUPACK);
				/*
				 * When we enter recovery we need to assure
				 * we send one packet.
				 */
				rack->r_ctl.rc_prr_sndcnt = tp->t_maxseg;
			} else
				rack->r_ctl.rc_tlp_rtx_out = 0;
		}
		if (SEQ_LT(rack->r_ctl.rc_rack_tmit_time, rsm->r_tim_lastsent[(rsm->r_rtr_cnt - 1)])) {
			/* New more recent rack_tmit_time */
			rack->r_ctl.rc_rack_tmit_time = rsm->r_tim_lastsent[(rsm->r_rtr_cnt - 1)];
			rack->rc_rack_rtt = t;
		}
		return (1);
	}
	/* 
	 * We clear the soft/rxtshift since we got an ack. 
	 * There is no assurance we will call the commit() function
	 * so we need to clear these to avoid incorrect handling.
	 */
	tp->t_rxtshift = 0;
	tp->t_softerror = 0;
	if ((to->to_flags & TOF_TS) &&
	    (ack_type == CUM_ACKED) &&
	    (to->to_tsecr) &&
	    ((rsm->r_flags & (RACK_DEFERRED | RACK_OVERMAX)) == 0)) {
		/*
		 * Now which timestamp does it match? In this block the ACK
		 * must be coming from a previous transmission.
		 */
		for (i = 0; i < rsm->r_rtr_cnt; i++) {
			if (rsm->r_tim_lastsent[i] == to->to_tsecr) {
				t = cts - rsm->r_tim_lastsent[i];
				if ((int)t <= 0)
					t = 1;
				if ((i + 1) < rsm->r_rtr_cnt) {
					/* Likely */
					rack_earlier_retran(tp, rsm, t, cts);
				}
				if (!tp->t_rttlow || tp->t_rttlow > t)
					tp->t_rttlow = t;
				if (!rack->r_ctl.rc_rack_min_rtt || SEQ_LT(t, rack->r_ctl.rc_rack_min_rtt)) {
					rack->r_ctl.rc_rack_min_rtt = t;
					if (rack->r_ctl.rc_rack_min_rtt == 0) {
						rack->r_ctl.rc_rack_min_rtt = 1;
					}
				}
                                /*
				 * Note the following calls to
				 * tcp_rack_xmit_timer() are being commented
				 * out for now. They give us no more accuracy
				 * and often lead to a wrong choice. We have
				 * enough samples that have not been 
				 * retransmitted. I leave the commented out
				 * code in here in case in the future we
				 * decide to add it back (though I can't forsee
				 * doing that). That way we will easily see
				 * where they need to be placed.
				 */
				if (SEQ_LT(rack->r_ctl.rc_rack_tmit_time,
				    rsm->r_tim_lastsent[(rsm->r_rtr_cnt - 1)])) {
					/* New more recent rack_tmit_time */
					rack->r_ctl.rc_rack_tmit_time = rsm->r_tim_lastsent[(rsm->r_rtr_cnt - 1)];
					rack->rc_rack_rtt = t;
				}
				return (1);
			}
		}
		goto ts_not_found;
	} else {
		/*
		 * Ok its a SACK block that we retransmitted. or a windows
		 * machine without timestamps. We can tell nothing from the
		 * time-stamp since its not there or the time the peer last
		 * recieved a segment that moved forward its cum-ack point.
		 */
ts_not_found:
		i = rsm->r_rtr_cnt - 1;
		t = cts - rsm->r_tim_lastsent[i];
		if ((int)t <= 0)
			t = 1;
		if (rack->r_ctl.rc_rack_min_rtt && SEQ_LT(t, rack->r_ctl.rc_rack_min_rtt)) {
			/*
			 * We retransmitted and the ack came back in less
			 * than the smallest rtt we have observed. We most
			 * likey did an improper retransmit as outlined in
			 * 4.2 Step 3 point 2 in the rack-draft.
			 */
			i = rsm->r_rtr_cnt - 2;
			t = cts - rsm->r_tim_lastsent[i];
			rack_earlier_retran(tp, rsm, t, cts);
		} else if (rack->r_ctl.rc_rack_min_rtt) {
			/*
			 * We retransmitted it and the retransmit did the
			 * job.
			 */
			if (!rack->r_ctl.rc_rack_min_rtt ||
			    SEQ_LT(t, rack->r_ctl.rc_rack_min_rtt)) {
				rack->r_ctl.rc_rack_min_rtt = t;
				if (rack->r_ctl.rc_rack_min_rtt == 0) {
					rack->r_ctl.rc_rack_min_rtt = 1;
				}
			}
			if (SEQ_LT(rack->r_ctl.rc_rack_tmit_time, rsm->r_tim_lastsent[i])) {
				/* New more recent rack_tmit_time */
				rack->r_ctl.rc_rack_tmit_time = rsm->r_tim_lastsent[i];
				rack->rc_rack_rtt = t;
			}
			return (1);
		}
	}
	return (0);
}

/*
 * Mark the SACK_PASSED flag on all entries prior to rsm send wise.
 */
static void
rack_log_sack_passed(struct tcpcb *tp,
    struct tcp_rack *rack, struct rack_sendmap *rsm)
{
	struct rack_sendmap *nrsm;
	uint32_t ts;
	int32_t idx;

	idx = rsm->r_rtr_cnt - 1;
	ts = rsm->r_tim_lastsent[idx];
	nrsm = rsm;
	TAILQ_FOREACH_REVERSE_FROM(nrsm, &rack->r_ctl.rc_tmap,
	    rack_head, r_tnext) {
		if (nrsm == rsm) {
			/* Skip orginal segment he is acked */
			continue;
		}
		if (nrsm->r_flags & RACK_ACKED) {
			/* Skip ack'd segments */
			continue;
		}
		idx = nrsm->r_rtr_cnt - 1;
		if (ts == nrsm->r_tim_lastsent[idx]) {
			/*
			 * For this case lets use seq no, if we sent in a
			 * big block (TSO) we would have a bunch of segments
			 * sent at the same time.
			 *
			 * We would only get a report if its SEQ is earlier.
			 * If we have done multiple retransmits the times
			 * would not be equal.
			 */
			if (SEQ_LT(nrsm->r_start, rsm->r_start)) {
				nrsm->r_flags |= RACK_SACK_PASSED;
				nrsm->r_flags &= ~RACK_WAS_SACKPASS;
			}
		} else {
			/*
			 * Here they were sent at different times, not a big
			 * block. Since we transmitted this one later and
			 * see it sack'd then this must also be missing (or
			 * we would have gotten a sack block for it)
			 */
			nrsm->r_flags |= RACK_SACK_PASSED;
			nrsm->r_flags &= ~RACK_WAS_SACKPASS;
		}
	}
}

static uint32_t
rack_proc_sack_blk(struct tcpcb *tp, struct tcp_rack *rack, struct sackblk *sack,
    struct tcpopt *to, struct rack_sendmap **prsm, uint32_t cts)
{
	int32_t idx;
	int32_t times = 0;
	uint32_t start, end, changed = 0;
	struct rack_sendmap *rsm, *nrsm;
	int32_t used_ref = 1;

	start = sack->start;
	end = sack->end;
	rsm = *prsm;
	if (rsm && SEQ_LT(start, rsm->r_start)) {
		TAILQ_FOREACH_REVERSE_FROM(rsm, &rack->r_ctl.rc_map, rack_head, r_next) {
			if (SEQ_GEQ(start, rsm->r_start) &&
			    SEQ_LT(start, rsm->r_end)) {
				goto do_rest_ofb;
			}
		}
	}
	if (rsm == NULL) {
start_at_beginning:
		rsm = NULL;
		used_ref = 0;
	}
	/* First lets locate the block where this guy is */
	TAILQ_FOREACH_FROM(rsm, &rack->r_ctl.rc_map, r_next) {
		if (SEQ_GEQ(start, rsm->r_start) &&
		    SEQ_LT(start, rsm->r_end)) {
			break;
		}
	}
do_rest_ofb:
	if (rsm == NULL) {
		/*
		 * This happens when we get duplicate sack blocks with the
		 * same end. For example SACK 4: 100 SACK 3: 100 The sort
		 * will not change there location so we would just start at
		 * the end of the first one and get lost.
		 */
		if (tp->t_flags & TF_SENTFIN) {
			/*
			 * Check to see if we have not logged the FIN that
			 * went out.
			 */
			nrsm = TAILQ_LAST_FAST(&rack->r_ctl.rc_map, rack_sendmap, r_next);
			if (nrsm && (nrsm->r_end + 1) == tp->snd_max) {
				/*
				 * Ok we did not get the FIN logged.
				 */
				nrsm->r_end++;
				rsm = nrsm;
				goto do_rest_ofb;
			}
		}
		if (times == 1) {
#ifdef INVARIANTS
			panic("tp:%p rack:%p sack:%p to:%p prsm:%p",
			    tp, rack, sack, to, prsm);
#else
			goto out;
#endif
		}
		times++;
		counter_u64_add(rack_sack_proc_restart, 1);
		goto start_at_beginning;
	}
	/* Ok we have an ACK for some piece of rsm */
	if (rsm->r_start != start) {
		/*
		 * Need to split this in two pieces the before and after.
		 */
		nrsm = rack_alloc(rack);
		if (nrsm == NULL) {
			/*
			 * failed XXXrrs what can we do but loose the sack
			 * info?
			 */
			goto out;
		}
		nrsm->r_start = start;
		nrsm->r_rtr_bytes = 0;
		nrsm->r_end = rsm->r_end;
		nrsm->r_rtr_cnt = rsm->r_rtr_cnt;
		nrsm->r_flags = rsm->r_flags;
		nrsm->r_sndcnt = rsm->r_sndcnt;
		for (idx = 0; idx < nrsm->r_rtr_cnt; idx++) {
			nrsm->r_tim_lastsent[idx] = rsm->r_tim_lastsent[idx];
		}
		rsm->r_end = nrsm->r_start;
		TAILQ_INSERT_AFTER(&rack->r_ctl.rc_map, rsm, nrsm, r_next);
		if (rsm->r_in_tmap) {
			TAILQ_INSERT_AFTER(&rack->r_ctl.rc_tmap, rsm, nrsm, r_tnext);
			nrsm->r_in_tmap = 1;
		}
		rsm->r_flags &= (~RACK_HAS_FIN);
		rsm = nrsm;
	}
	if (SEQ_GEQ(end, rsm->r_end)) {
		/*
		 * The end of this block is either beyond this guy or right
		 * at this guy.
		 */

		if ((rsm->r_flags & RACK_ACKED) == 0) {
			rack_update_rtt(tp, rack, rsm, to, cts, SACKED);
			changed += (rsm->r_end - rsm->r_start);
			rack->r_ctl.rc_sacked += (rsm->r_end - rsm->r_start);
			rack_log_sack_passed(tp, rack, rsm);
			/* Is Reordering occuring? */
			if (rsm->r_flags & RACK_SACK_PASSED) {
				counter_u64_add(rack_reorder_seen, 1);
				rack->r_ctl.rc_reorder_ts = cts;
			}
			rsm->r_flags |= RACK_ACKED;
			rsm->r_flags &= ~RACK_TLP;
			if (rsm->r_in_tmap) {
				TAILQ_REMOVE(&rack->r_ctl.rc_tmap, rsm, r_tnext);
				rsm->r_in_tmap = 0;
			}
		}
		if (end == rsm->r_end) {
			/* This block only - done */
			goto out;
		}
		/* There is more not coverend by this rsm move on */
		start = rsm->r_end;
		nrsm = TAILQ_NEXT(rsm, r_next);
		rsm = nrsm;
		times = 0;
		goto do_rest_ofb;
	}
	/* Ok we need to split off this one at the tail */
	nrsm = rack_alloc(rack);
	if (nrsm == NULL) {
		/* failed rrs what can we do but loose the sack info? */
		goto out;
	}
	/* Clone it */
	nrsm->r_start = end;
	nrsm->r_end = rsm->r_end;
	nrsm->r_rtr_bytes = 0;
	nrsm->r_rtr_cnt = rsm->r_rtr_cnt;
	nrsm->r_flags = rsm->r_flags;
	nrsm->r_sndcnt = rsm->r_sndcnt;
	for (idx = 0; idx < nrsm->r_rtr_cnt; idx++) {
		nrsm->r_tim_lastsent[idx] = rsm->r_tim_lastsent[idx];
	}
	/* The sack block does not cover this guy fully */
	rsm->r_flags &= (~RACK_HAS_FIN);
	rsm->r_end = end;
	TAILQ_INSERT_AFTER(&rack->r_ctl.rc_map, rsm, nrsm, r_next);
	if (rsm->r_in_tmap) {
		TAILQ_INSERT_AFTER(&rack->r_ctl.rc_tmap, rsm, nrsm, r_tnext);
		nrsm->r_in_tmap = 1;
	}
	if (rsm->r_flags & RACK_ACKED) {
		/* Been here done that */
		goto out;
	}
	rack_update_rtt(tp, rack, rsm, to, cts, SACKED);
	changed += (rsm->r_end - rsm->r_start);
	rack->r_ctl.rc_sacked += (rsm->r_end - rsm->r_start);
	rack_log_sack_passed(tp, rack, rsm);
	/* Is Reordering occuring? */
	if (rsm->r_flags & RACK_SACK_PASSED) {
		counter_u64_add(rack_reorder_seen, 1);
		rack->r_ctl.rc_reorder_ts = cts;
	}
	rsm->r_flags |= RACK_ACKED;
	rsm->r_flags &= ~RACK_TLP;
	if (rsm->r_in_tmap) {
		TAILQ_REMOVE(&rack->r_ctl.rc_tmap, rsm, r_tnext);
		rsm->r_in_tmap = 0;
	}
out:
	if (used_ref == 0) {
		counter_u64_add(rack_sack_proc_all, 1);
	} else {
		counter_u64_add(rack_sack_proc_short, 1);
	}
	/* Save off where we last were */
	if (rsm)
		rack->r_ctl.rc_sacklast = TAILQ_NEXT(rsm, r_next);
	else
		rack->r_ctl.rc_sacklast = NULL;
	*prsm = rsm;
	return (changed);
}

static void inline 
rack_peer_reneges(struct tcp_rack *rack, struct rack_sendmap *rsm, tcp_seq th_ack)
{
	struct rack_sendmap *tmap;

	tmap = NULL;
	while (rsm && (rsm->r_flags & RACK_ACKED)) {
		/* Its no longer sacked, mark it so */
		rack->r_ctl.rc_sacked -= (rsm->r_end - rsm->r_start);
#ifdef INVARIANTS
		if (rsm->r_in_tmap) {
			panic("rack:%p rsm:%p flags:0x%x in tmap?",
			      rack, rsm, rsm->r_flags);
		}
#endif
		rsm->r_flags &= ~(RACK_ACKED|RACK_SACK_PASSED|RACK_WAS_SACKPASS);
		/* Rebuild it into our tmap */
		if (tmap == NULL) {
			TAILQ_INSERT_HEAD(&rack->r_ctl.rc_tmap, rsm, r_tnext);
			tmap = rsm;
		} else {
			TAILQ_INSERT_AFTER(&rack->r_ctl.rc_tmap, tmap, rsm, r_tnext);
			tmap = rsm;
		}
		tmap->r_in_tmap = 1;
		rsm = TAILQ_NEXT(rsm, r_next);
	}
	/* 
	 * Now lets possibly clear the sack filter so we start 
	 * recognizing sacks that cover this area.
	 */
	if (rack_use_sack_filter)
		sack_filter_clear(&rack->r_ctl.rack_sf, th_ack);

}

static void
rack_log_ack(struct tcpcb *tp, struct tcpopt *to, struct tcphdr *th)
{
	uint32_t changed, last_seq, entered_recovery = 0;
	struct tcp_rack *rack;
	struct rack_sendmap *rsm;
	struct sackblk sack, sack_blocks[TCP_MAX_SACK + 1];
	register uint32_t th_ack;
	int32_t i, j, k, num_sack_blks = 0;
	uint32_t cts, acked, ack_point, sack_changed = 0;

	INP_WLOCK_ASSERT(tp->t_inpcb);
	if (th->th_flags & TH_RST) {
		/* We don't log resets */
		return;
	}
	rack = (struct tcp_rack *)tp->t_fb_ptr;
	cts = tcp_ts_getticks();
	rsm = TAILQ_FIRST(&rack->r_ctl.rc_map);
	changed = 0;
	th_ack = th->th_ack;

	if (SEQ_GT(th_ack, tp->snd_una)) {
		rack_log_progress_event(rack, tp, ticks, PROGRESS_UPDATE, __LINE__);
		tp->t_acktime = ticks;
	}
	if (rsm && SEQ_GT(th_ack, rsm->r_start))
		changed = th_ack - rsm->r_start;
	if (changed) {
		/*
		 * The ACK point is advancing to th_ack, we must drop off
		 * the packets in the rack log and calculate any eligble
		 * RTT's.
		 */
		rack->r_wanted_output++;
more:
		rsm = TAILQ_FIRST(&rack->r_ctl.rc_map);
		if (rsm == NULL) {
			if ((th_ack - 1) == tp->iss) {
				/*
				 * For the SYN incoming case we will not
				 * have called tcp_output for the sending of
				 * the SYN, so there will be no map. All
				 * other cases should probably be a panic.
				 */
				goto proc_sack;
			}
			if (tp->t_flags & TF_SENTFIN) {
				/* if we send a FIN we will not hav a map */
				goto proc_sack;
			}
#ifdef INVARIANTS
			panic("No rack map tp:%p for th:%p state:%d rack:%p snd_una:%u snd_max:%u snd_nxt:%u chg:%d\n",
			    tp,
			    th, tp->t_state, rack,
			    tp->snd_una, tp->snd_max, tp->snd_nxt, changed);
#endif
			goto proc_sack;
		}
		if (SEQ_LT(th_ack, rsm->r_start)) {
			/* Huh map is missing this */
#ifdef INVARIANTS
			printf("Rack map starts at r_start:%u for th_ack:%u huh? ts:%d rs:%d\n",
			    rsm->r_start,
			    th_ack, tp->t_state, rack->r_state);
#endif
			goto proc_sack;
		}
		rack_update_rtt(tp, rack, rsm, to, cts, CUM_ACKED);
		/* Now do we consume the whole thing? */
		if (SEQ_GEQ(th_ack, rsm->r_end)) {
			/* Its all consumed. */
			uint32_t left;

			rack->r_ctl.rc_holes_rxt -= rsm->r_rtr_bytes;
			rsm->r_rtr_bytes = 0;
			TAILQ_REMOVE(&rack->r_ctl.rc_map, rsm, r_next);
			if (rsm->r_in_tmap) {
				TAILQ_REMOVE(&rack->r_ctl.rc_tmap, rsm, r_tnext);
				rsm->r_in_tmap = 0;
			}
			if (rack->r_ctl.rc_next == rsm) {
				/* scoot along the marker */
				rack->r_ctl.rc_next = TAILQ_FIRST(&rack->r_ctl.rc_map);
			}
			if (rsm->r_flags & RACK_ACKED) {
				/*
				 * It was acked on the scoreboard -- remove
				 * it from total
				 */
				rack->r_ctl.rc_sacked -= (rsm->r_end - rsm->r_start);
			} else if (rsm->r_flags & RACK_SACK_PASSED) {
				/*
				 * There are acked segments ACKED on the
				 * scoreboard further up. We are seeing
				 * reordering.
				 */
				counter_u64_add(rack_reorder_seen, 1);
				rsm->r_flags |= RACK_ACKED;
				rack->r_ctl.rc_reorder_ts = cts;
			}
			left = th_ack - rsm->r_end;
			if (rsm->r_rtr_cnt > 1) {
				/*
				 * Technically we should make r_rtr_cnt be
				 * monotonicly increasing and just mod it to
				 * the timestamp it is replacing.. that way
				 * we would have the last 3 retransmits. Now
				 * rc_loss_count will be wrong if we
				 * retransmit something more than 2 times in
				 * recovery :(
				 */
				rack->r_ctl.rc_loss_count += (rsm->r_rtr_cnt - 1);
			}
			/* Free back to zone */
			rack_free(rack, rsm);
			if (left) {
				goto more;
			}
			goto proc_sack;
		}
		if (rsm->r_flags & RACK_ACKED) {
			/*
			 * It was acked on the scoreboard -- remove it from
			 * total for the part being cum-acked.
			 */
			rack->r_ctl.rc_sacked -= (th_ack - rsm->r_start);
		}
		rack->r_ctl.rc_holes_rxt -= rsm->r_rtr_bytes;
		rsm->r_rtr_bytes = 0;
		rsm->r_start = th_ack;
	}
proc_sack:
	/* Check for reneging */
	rsm = TAILQ_FIRST(&rack->r_ctl.rc_map);
	if (rsm && (rsm->r_flags & RACK_ACKED) && (th_ack == rsm->r_start)) {
		/*
		 * The peer has moved snd_una up to
		 * the edge of this send, i.e. one
		 * that it had previously acked. The only
		 * way that can be true if the peer threw
		 * away data (space issues) that it had
		 * previously sacked (else it would have 
		 * given us snd_una up to (rsm->r_end).
		 * We need to undo the acked markings here.
		 *
		 * Note we have to look to make sure th_ack is
		 * our rsm->r_start in case we get an old ack
		 * where th_ack is behind snd_una.
		 */
		rack_peer_reneges(rack, rsm, th->th_ack);
	}
	if ((to->to_flags & TOF_SACK) == 0) {
		/* We are done nothing left to log */
		goto out;
	}
	rsm = TAILQ_LAST_FAST(&rack->r_ctl.rc_map, rack_sendmap, r_next);
	if (rsm) {
		last_seq = rsm->r_end;
	} else {
		last_seq = tp->snd_max;
	}
	/* Sack block processing */
	if (SEQ_GT(th_ack, tp->snd_una))
		ack_point = th_ack;
	else
		ack_point = tp->snd_una;
	for (i = 0; i < to->to_nsacks; i++) {
		bcopy((to->to_sacks + i * TCPOLEN_SACK),
		    &sack, sizeof(sack));
		sack.start = ntohl(sack.start);
		sack.end = ntohl(sack.end);
		if (SEQ_GT(sack.end, sack.start) &&
		    SEQ_GT(sack.start, ack_point) &&
		    SEQ_LT(sack.start, tp->snd_max) &&
		    SEQ_GT(sack.end, ack_point) &&
		    SEQ_LEQ(sack.end, tp->snd_max)) {
			if ((rack->r_ctl.rc_num_maps_alloced > rack_sack_block_limit) &&
			    (SEQ_LT(sack.end, last_seq)) &&
			    ((sack.end - sack.start) < (tp->t_maxseg / 8))) {
				/*
				 * Not the last piece and its smaller than
				 * 1/8th of a MSS. We ignore this.
				 */
				counter_u64_add(rack_runt_sacks, 1);
				continue;
			}
			sack_blocks[num_sack_blks] = sack;
			num_sack_blks++;
#ifdef NETFLIX_STATS
		} else if (SEQ_LEQ(sack.start, th_ack) &&
			   SEQ_LEQ(sack.end, th_ack)) {
			/*
			 * Its a D-SACK block.
			 */
			tcp_record_dsack(sack.start, sack.end);
#endif
		}

	}
	if (num_sack_blks == 0)
		goto out;
	/*
	 * Sort the SACK blocks so we can update the rack scoreboard with
	 * just one pass.
	 */
	if (rack_use_sack_filter) {
		num_sack_blks = sack_filter_blks(&rack->r_ctl.rack_sf, sack_blocks, num_sack_blks, th->th_ack);
	}
	if (num_sack_blks < 2) {
		goto do_sack_work;
	}
	/* Sort the sacks */
	for (i = 0; i < num_sack_blks; i++) {
		for (j = i + 1; j < num_sack_blks; j++) {
			if (SEQ_GT(sack_blocks[i].end, sack_blocks[j].end)) {
				sack = sack_blocks[i];
				sack_blocks[i] = sack_blocks[j];
				sack_blocks[j] = sack;
			}
		}
	}
	/*
	 * Now are any of the sack block ends the same (yes some
	 * implememtations send these)?
	 */
again:
	if (num_sack_blks > 1) {
		for (i = 0; i < num_sack_blks; i++) {
			for (j = i + 1; j < num_sack_blks; j++) {
				if (sack_blocks[i].end == sack_blocks[j].end) {
					/*
					 * Ok these two have the same end we
					 * want the smallest end and then
					 * throw away the larger and start
					 * again.
					 */
					if (SEQ_LT(sack_blocks[j].start, sack_blocks[i].start)) {
						/*
						 * The second block covers
						 * more area use that
						 */
						sack_blocks[i].start = sack_blocks[j].start;
					}
					/*
					 * Now collapse out the dup-sack and
					 * lower the count
					 */
					for (k = (j + 1); k < num_sack_blks; k++) {
						sack_blocks[j].start = sack_blocks[k].start;
						sack_blocks[j].end = sack_blocks[k].end;
						j++;
					}
					num_sack_blks--;
					goto again;
				}
			}
		}
	}
do_sack_work:
	rsm = rack->r_ctl.rc_sacklast;
	for (i = 0; i < num_sack_blks; i++) {
		acked = rack_proc_sack_blk(tp, rack, &sack_blocks[i], to, &rsm, cts);
		if (acked) {
			rack->r_wanted_output++;
			changed += acked;
			sack_changed += acked;
		}
	}
out:
	if (changed) {
		/* Something changed cancel the rack timer */
		rack_timer_cancel(tp, rack, rack->r_ctl.rc_rcvtime, __LINE__);
	}
	if ((sack_changed) && (!IN_RECOVERY(tp->t_flags))) {
		/*
		 * Ok we have a high probability that we need to go in to
		 * recovery since we have data sack'd
		 */
		struct rack_sendmap *rsm;
		uint32_t tsused;

		tsused = tcp_ts_getticks();
		rsm = tcp_rack_output(tp, rack, tsused);
		if (rsm) {
			/* Enter recovery */
			rack->r_ctl.rc_rsm_start = rsm->r_start;
			rack->r_ctl.rc_cwnd_at = tp->snd_cwnd;
			rack->r_ctl.rc_ssthresh_at = tp->snd_ssthresh;
			entered_recovery = 1;
			rack_cong_signal(tp, NULL, CC_NDUPACK);
			/*
			 * When we enter recovery we need to assure we send
			 * one packet.
			 */
			rack->r_ctl.rc_prr_sndcnt = tp->t_maxseg;
			rack->r_timer_override = 1;
		}
	}
	if (IN_RECOVERY(tp->t_flags) && (entered_recovery == 0)) {
		/* Deal with changed an PRR here (in recovery only) */
		uint32_t pipe, snd_una;

		rack->r_ctl.rc_prr_delivered += changed;
		/* Compute prr_sndcnt */
		if (SEQ_GT(tp->snd_una, th_ack)) {
			snd_una = tp->snd_una;
		} else {
			snd_una = th_ack;
		}
		pipe = ((tp->snd_max - snd_una) - rack->r_ctl.rc_sacked) + rack->r_ctl.rc_holes_rxt;
		if (pipe > tp->snd_ssthresh) {
			long sndcnt;

			sndcnt = rack->r_ctl.rc_prr_delivered * tp->snd_ssthresh;
			if (rack->r_ctl.rc_prr_recovery_fs > 0)
				sndcnt /= (long)rack->r_ctl.rc_prr_recovery_fs;
			else {
				rack->r_ctl.rc_prr_sndcnt = 0;
				sndcnt = 0;
			}
			sndcnt++;
			if (sndcnt > (long)rack->r_ctl.rc_prr_out)
				sndcnt -= rack->r_ctl.rc_prr_out;
			else
				sndcnt = 0;
			rack->r_ctl.rc_prr_sndcnt = sndcnt;
		} else {
			uint32_t limit;

			if (rack->r_ctl.rc_prr_delivered > rack->r_ctl.rc_prr_out)
				limit = (rack->r_ctl.rc_prr_delivered - rack->r_ctl.rc_prr_out);
			else
				limit = 0;
			if (changed > limit)
				limit = changed;
			limit += tp->t_maxseg;
			if (tp->snd_ssthresh > pipe) {
				rack->r_ctl.rc_prr_sndcnt = min((tp->snd_ssthresh - pipe), limit);
			} else {
				rack->r_ctl.rc_prr_sndcnt = min(0, limit);
			}
		}
		if (rack->r_ctl.rc_prr_sndcnt >= tp->t_maxseg) {
			rack->r_timer_override = 1;
		}
	}
}

/*
 * Return value of 1, we do not need to call rack_process_data().
 * return value of 0, rack_process_data can be called.
 * For ret_val if its 0 the TCP is locked, if its non-zero
 * its unlocked and probably unsafe to touch the TCB.
 */
static int
rack_process_ack(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, struct tcpopt *to,
    uint32_t tiwin, int32_t tlen,
    int32_t * ofia, int32_t thflags, int32_t * ret_val)
{
	int32_t ourfinisacked = 0;
	int32_t nsegs, acked_amount;
	int32_t acked;
	struct mbuf *mfree;
	struct tcp_rack *rack;
	int32_t recovery = 0;

	rack = (struct tcp_rack *)tp->t_fb_ptr;
	if (SEQ_GT(th->th_ack, tp->snd_max)) {
		rack_do_dropafterack(m, tp, th, thflags, tlen, ret_val);
		return (1);
	}
	if (SEQ_GEQ(th->th_ack, tp->snd_una) || to->to_nsacks) {
		rack_log_ack(tp, to, th);
	}
	if (__predict_false(SEQ_LEQ(th->th_ack, tp->snd_una))) {
		/*
		 * Old ack, behind (or duplicate to) the last one rcv'd
		 * Note: Should mark reordering is occuring! We should also
		 * look for sack blocks arriving e.g. ack 1, 4-4 then ack 1,
		 * 3-3, 4-4 would be reording. As well as ack 1, 3-3 <no
		 * retran and> ack 3
		 */
		return (0);
	}
	/*
	 * If we reach this point, ACK is not a duplicate, i.e., it ACKs
	 * something we sent.
	 */
	if (tp->t_flags & TF_NEEDSYN) {
		/*
		 * T/TCP: Connection was half-synchronized, and our SYN has
		 * been ACK'd (so connection is now fully synchronized).  Go
		 * to non-starred state, increment snd_una for ACK of SYN,
		 * and check if we can do window scaling.
		 */
		tp->t_flags &= ~TF_NEEDSYN;
		tp->snd_una++;
		/* Do window scaling? */
		if ((tp->t_flags & (TF_RCVD_SCALE | TF_REQ_SCALE)) ==
		    (TF_RCVD_SCALE | TF_REQ_SCALE)) {
			tp->rcv_scale = tp->request_r_scale;
			/* Send window already scaled. */
		}
	}
	nsegs = max(1, m->m_pkthdr.lro_nsegs);
	INP_WLOCK_ASSERT(tp->t_inpcb);

	acked = BYTES_THIS_ACK(tp, th);
	TCPSTAT_ADD(tcps_rcvackpack, nsegs);
	TCPSTAT_ADD(tcps_rcvackbyte, acked);

	/*
	 * If we just performed our first retransmit, and the ACK arrives
	 * within our recovery window, then it was a mistake to do the
	 * retransmit in the first place.  Recover our original cwnd and
	 * ssthresh, and proceed to transmit where we left off.
	 */
	if (tp->t_flags & TF_PREVVALID) {
		tp->t_flags &= ~TF_PREVVALID;
		if (tp->t_rxtshift == 1 &&
		    (int)(ticks - tp->t_badrxtwin) < 0)
			rack_cong_signal(tp, th, CC_RTO_ERR);
	}
	/*
	 * If we have a timestamp reply, update smoothed round trip time. If
	 * no timestamp is present but transmit timer is running and timed
	 * sequence number was acked, update smoothed round trip time. Since
	 * we now have an rtt measurement, cancel the timer backoff (cf.,
	 * Phil Karn's retransmit alg.). Recompute the initial retransmit
	 * timer.
	 *
	 * Some boxes send broken timestamp replies during the SYN+ACK
	 * phase, ignore timestamps of 0 or we could calculate a huge RTT
	 * and blow up the retransmit timer.
	 */
	/*
	 * If all outstanding data is acked, stop retransmit timer and
	 * remember to restart (more output or persist). If there is more
	 * data to be acked, restart retransmit timer, using current
	 * (possibly backed-off) value.
	 */
	if (th->th_ack == tp->snd_max) {
		rack_timer_cancel(tp, rack, rack->r_ctl.rc_rcvtime, __LINE__);
		rack->r_wanted_output++;
	}
	/*
	 * If no data (only SYN) was ACK'd, skip rest of ACK processing.
	 */
	if (acked == 0) {
		if (ofia)
			*ofia = ourfinisacked;
		return (0);
	}
	if (rack->r_ctl.rc_early_recovery) {
		if (IN_FASTRECOVERY(tp->t_flags)) {
			if (SEQ_LT(th->th_ack, tp->snd_recover)) {
				tcp_rack_partialack(tp, th);
			} else {
				rack_post_recovery(tp, th);
				recovery = 1;
			}
		}
	}
	/*
	 * Let the congestion control algorithm update congestion control
	 * related information. This typically means increasing the
	 * congestion window.
	 */
	rack_ack_received(tp, rack, th, nsegs, CC_ACK, recovery);
	SOCKBUF_LOCK(&so->so_snd);
	acked_amount = min(acked, (int)sbavail(&so->so_snd));
	tp->snd_wnd -= acked_amount;
	mfree = sbcut_locked(&so->so_snd, acked_amount);
	if ((sbused(&so->so_snd) == 0) &&
	    (acked > acked_amount) &&
	    (tp->t_state >= TCPS_FIN_WAIT_1)) {
		ourfinisacked = 1;
	}
	/* NB: sowwakeup_locked() does an implicit unlock. */
	sowwakeup_locked(so);
	m_freem(mfree);
	if (rack->r_ctl.rc_early_recovery == 0) {
		if (IN_FASTRECOVERY(tp->t_flags)) {
			if (SEQ_LT(th->th_ack, tp->snd_recover)) {
				tcp_rack_partialack(tp, th);
			} else {
				rack_post_recovery(tp, th);
			}
		}
	}
	tp->snd_una = th->th_ack;
	if (SEQ_GT(tp->snd_una, tp->snd_recover))
		tp->snd_recover = tp->snd_una;

	if (SEQ_LT(tp->snd_nxt, tp->snd_una)) {
		tp->snd_nxt = tp->snd_una;
	}
	if (tp->snd_una == tp->snd_max) {
		/* Nothing left outstanding */
		rack_log_progress_event(rack, tp, 0, PROGRESS_CLEAR, __LINE__);
		tp->t_acktime = 0;
		rack_timer_cancel(tp, rack, rack->r_ctl.rc_rcvtime, __LINE__);
		/* Set need output so persist might get set */
		rack->r_wanted_output++;
		if (rack_use_sack_filter)
			sack_filter_clear(&rack->r_ctl.rack_sf, tp->snd_una);
		if ((tp->t_state >= TCPS_FIN_WAIT_1) &&
		    (sbavail(&so->so_snd) == 0) &&
		    (tp->t_flags2 & TF2_DROP_AF_DATA)) {
			/* 
			 * The socket was gone and the
			 * peer sent data, time to
			 * reset him.
			 */
			*ret_val = 1;
			tp = tcp_close(tp);
			rack_do_dropwithreset(m, tp, th, BANDLIM_UNLIMITED, tlen);
			return (1);
		}
	}
	if (ofia)
		*ofia = ourfinisacked;
	return (0);
}


/*
 * Return value of 1, the TCB is unlocked and most
 * likely gone, return value of 0, the TCP is still
 * locked.
 */
static int
rack_process_data(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, int32_t drop_hdrlen, int32_t tlen,
    uint32_t tiwin, int32_t thflags, int32_t nxt_pkt)
{
	/*
	 * Update window information. Don't look at window if no ACK: TAC's
	 * send garbage on first SYN.
	 */
	int32_t nsegs;
	int32_t tfo_syn;
	struct tcp_rack *rack;

	rack = (struct tcp_rack *)tp->t_fb_ptr;
	INP_WLOCK_ASSERT(tp->t_inpcb);
	nsegs = max(1, m->m_pkthdr.lro_nsegs);
	if ((thflags & TH_ACK) &&
	    (SEQ_LT(tp->snd_wl1, th->th_seq) ||
	    (tp->snd_wl1 == th->th_seq && (SEQ_LT(tp->snd_wl2, th->th_ack) ||
	    (tp->snd_wl2 == th->th_ack && tiwin > tp->snd_wnd))))) {
		/* keep track of pure window updates */
		if (tlen == 0 &&
		    tp->snd_wl2 == th->th_ack && tiwin > tp->snd_wnd)
			TCPSTAT_INC(tcps_rcvwinupd);
		tp->snd_wnd = tiwin;
		tp->snd_wl1 = th->th_seq;
		tp->snd_wl2 = th->th_ack;
		if (tp->snd_wnd > tp->max_sndwnd)
			tp->max_sndwnd = tp->snd_wnd;
		rack->r_wanted_output++;
	} else if (thflags & TH_ACK) {
		if ((tp->snd_wl2 == th->th_ack) && (tiwin < tp->snd_wnd)) {
			tp->snd_wnd = tiwin;
			tp->snd_wl1 = th->th_seq;
			tp->snd_wl2 = th->th_ack;
		}
	}
	/* Was persist timer active and now we have window space? */
	if ((rack->rc_in_persist != 0) && tp->snd_wnd) {
		rack_exit_persist(tp, rack);
		tp->snd_nxt = tp->snd_max;
		/* Make sure we output to start the timer */
		rack->r_wanted_output++;
	}
	if (tp->t_flags2 & TF2_DROP_AF_DATA) {
		m_freem(m);
		return (0);
	}
	/*
	 * Process segments with URG.
	 */
	if ((thflags & TH_URG) && th->th_urp &&
	    TCPS_HAVERCVDFIN(tp->t_state) == 0) {
		/*
		 * This is a kludge, but if we receive and accept random
		 * urgent pointers, we'll crash in soreceive.  It's hard to
		 * imagine someone actually wanting to send this much urgent
		 * data.
		 */
		SOCKBUF_LOCK(&so->so_rcv);
		if (th->th_urp + sbavail(&so->so_rcv) > sb_max) {
			th->th_urp = 0;	/* XXX */
			thflags &= ~TH_URG;	/* XXX */
			SOCKBUF_UNLOCK(&so->so_rcv);	/* XXX */
			goto dodata;	/* XXX */
		}
		/*
		 * If this segment advances the known urgent pointer, then
		 * mark the data stream.  This should not happen in
		 * CLOSE_WAIT, CLOSING, LAST_ACK or TIME_WAIT STATES since a
		 * FIN has been received from the remote side. In these
		 * states we ignore the URG.
		 *
		 * According to RFC961 (Assigned Protocols), the urgent
		 * pointer points to the last octet of urgent data.  We
		 * continue, however, to consider it to indicate the first
		 * octet of data past the urgent section as the original
		 * spec states (in one of two places).
		 */
		if (SEQ_GT(th->th_seq + th->th_urp, tp->rcv_up)) {
			tp->rcv_up = th->th_seq + th->th_urp;
			so->so_oobmark = sbavail(&so->so_rcv) +
			    (tp->rcv_up - tp->rcv_nxt) - 1;
			if (so->so_oobmark == 0)
				so->so_rcv.sb_state |= SBS_RCVATMARK;
			sohasoutofband(so);
			tp->t_oobflags &= ~(TCPOOB_HAVEDATA | TCPOOB_HADDATA);
		}
		SOCKBUF_UNLOCK(&so->so_rcv);
		/*
		 * Remove out of band data so doesn't get presented to user.
		 * This can happen independent of advancing the URG pointer,
		 * but if two URG's are pending at once, some out-of-band
		 * data may creep in... ick.
		 */
		if (th->th_urp <= (uint32_t) tlen &&
		    !(so->so_options & SO_OOBINLINE)) {
			/* hdr drop is delayed */
			tcp_pulloutofband(so, th, m, drop_hdrlen);
		}
	} else {
		/*
		 * If no out of band data is expected, pull receive urgent
		 * pointer along with the receive window.
		 */
		if (SEQ_GT(tp->rcv_nxt, tp->rcv_up))
			tp->rcv_up = tp->rcv_nxt;
	}
dodata:				/* XXX */
	INP_WLOCK_ASSERT(tp->t_inpcb);

	/*
	 * Process the segment text, merging it into the TCP sequencing
	 * queue, and arranging for acknowledgment of receipt if necessary.
	 * This process logically involves adjusting tp->rcv_wnd as data is
	 * presented to the user (this happens in tcp_usrreq.c, case
	 * PRU_RCVD).  If a FIN has already been received on this connection
	 * then we just ignore the text.
	 */
	tfo_syn = ((tp->t_state == TCPS_SYN_RECEIVED) &&
		   IS_FASTOPEN(tp->t_flags));
	if ((tlen || (thflags & TH_FIN) || tfo_syn) &&
	    TCPS_HAVERCVDFIN(tp->t_state) == 0) {
		tcp_seq save_start = th->th_seq;

		m_adj(m, drop_hdrlen);	/* delayed header drop */
		/*
		 * Insert segment which includes th into TCP reassembly
		 * queue with control block tp.  Set thflags to whether
		 * reassembly now includes a segment with FIN.  This handles
		 * the common case inline (segment is the next to be
		 * received on an established connection, and the queue is
		 * empty), avoiding linkage into and removal from the queue
		 * and repetition of various conversions. Set DELACK for
		 * segments received in order, but ack immediately when
		 * segments are out of order (so fast retransmit can work).
		 */
		if (th->th_seq == tp->rcv_nxt &&
		    SEGQ_EMPTY(tp) &&
		    (TCPS_HAVEESTABLISHED(tp->t_state) ||
		    tfo_syn)) {
			if (DELAY_ACK(tp, tlen) || tfo_syn) {
				rack_timer_cancel(tp, rack, rack->r_ctl.rc_rcvtime, __LINE__);
				tp->t_flags |= TF_DELACK;
			} else {
				rack->r_wanted_output++;
				tp->t_flags |= TF_ACKNOW;
			}
			tp->rcv_nxt += tlen;
			thflags = th->th_flags & TH_FIN;
			TCPSTAT_ADD(tcps_rcvpack, nsegs);
			TCPSTAT_ADD(tcps_rcvbyte, tlen);
			SOCKBUF_LOCK(&so->so_rcv);
			if (so->so_rcv.sb_state & SBS_CANTRCVMORE)
				m_freem(m);
			else
				sbappendstream_locked(&so->so_rcv, m, 0);
			/* NB: sorwakeup_locked() does an implicit unlock. */
			sorwakeup_locked(so);
		} else {
			/*
			 * XXX: Due to the header drop above "th" is
			 * theoretically invalid by now.  Fortunately
			 * m_adj() doesn't actually frees any mbufs when
			 * trimming from the head.
			 */
			thflags = tcp_reass(tp, th, &save_start, &tlen, m);
			tp->t_flags |= TF_ACKNOW;
		}
		if (tlen > 0)
			tcp_update_sack_list(tp, save_start, save_start + tlen);
	} else {
		m_freem(m);
		thflags &= ~TH_FIN;
	}

	/*
	 * If FIN is received ACK the FIN and let the user know that the
	 * connection is closing.
	 */
	if (thflags & TH_FIN) {
		if (TCPS_HAVERCVDFIN(tp->t_state) == 0) {
			socantrcvmore(so);
			/*
			 * If connection is half-synchronized (ie NEEDSYN
			 * flag on) then delay ACK, so it may be piggybacked
			 * when SYN is sent. Otherwise, since we received a
			 * FIN then no more input can be expected, send ACK
			 * now.
			 */
			if (tp->t_flags & TF_NEEDSYN) {
				rack_timer_cancel(tp, rack, rack->r_ctl.rc_rcvtime, __LINE__);
				tp->t_flags |= TF_DELACK;
			} else {
				tp->t_flags |= TF_ACKNOW;
			}
			tp->rcv_nxt++;
		}
		switch (tp->t_state) {

			/*
			 * In SYN_RECEIVED and ESTABLISHED STATES enter the
			 * CLOSE_WAIT state.
			 */
		case TCPS_SYN_RECEIVED:
			tp->t_starttime = ticks;
			/* FALLTHROUGH */
		case TCPS_ESTABLISHED:
			rack_timer_cancel(tp, rack, rack->r_ctl.rc_rcvtime, __LINE__);
			tcp_state_change(tp, TCPS_CLOSE_WAIT);
			break;

			/*
			 * If still in FIN_WAIT_1 STATE FIN has not been
			 * acked so enter the CLOSING state.
			 */
		case TCPS_FIN_WAIT_1:
			rack_timer_cancel(tp, rack, rack->r_ctl.rc_rcvtime, __LINE__);
			tcp_state_change(tp, TCPS_CLOSING);
			break;

			/*
			 * In FIN_WAIT_2 state enter the TIME_WAIT state,
			 * starting the time-wait timer, turning off the
			 * other standard timers.
			 */
		case TCPS_FIN_WAIT_2:
			rack_timer_cancel(tp, rack, rack->r_ctl.rc_rcvtime, __LINE__);
			INP_INFO_RLOCK_ASSERT(&V_tcbinfo);
			tcp_twstart(tp);
			return (1);
		}
	}
	/*
	 * Return any desired output.
	 */
	if ((tp->t_flags & TF_ACKNOW) || (sbavail(&so->so_snd) > (tp->snd_max - tp->snd_una))) {
		rack->r_wanted_output++;
	}
	INP_WLOCK_ASSERT(tp->t_inpcb);
	return (0);
}

/*
 * Here nothing is really faster, its just that we
 * have broken out the fast-data path also just like
 * the fast-ack.
 */
static int
rack_do_fastnewdata(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen, int32_t tlen,
    uint32_t tiwin, int32_t nxt_pkt)
{
	int32_t nsegs;
	int32_t newsize = 0;	/* automatic sockbuf scaling */
	struct tcp_rack *rack;
#ifdef TCPDEBUG
	/*
	 * The size of tcp_saveipgen must be the size of the max ip header,
	 * now IPv6.
	 */
	u_char tcp_saveipgen[IP6_HDR_LEN];
	struct tcphdr tcp_savetcp;
	short ostate = 0;

#endif
	/*
	 * If last ACK falls within this segment's sequence numbers, record
	 * the timestamp. NOTE that the test is modified according to the
	 * latest proposal of the tcplw@cray.com list (Braden 1993/04/26).
	 */
	if (__predict_false(th->th_seq != tp->rcv_nxt)) {
		return (0);
	}
	if (__predict_false(tp->snd_nxt != tp->snd_max)) {
		return (0);
	}
	if (tiwin && tiwin != tp->snd_wnd) {
		return (0);
	}
	if (__predict_false((tp->t_flags & (TF_NEEDSYN | TF_NEEDFIN)))) {
		return (0);
	}
	if (__predict_false((to->to_flags & TOF_TS) &&
	    (TSTMP_LT(to->to_tsval, tp->ts_recent)))) {
		return (0);
	}
	if (__predict_false((th->th_ack != tp->snd_una))) {
		return (0);
	}
	if (__predict_false(tlen > sbspace(&so->so_rcv))) {
		return (0);
	}
	if ((to->to_flags & TOF_TS) != 0 &&
	    SEQ_LEQ(th->th_seq, tp->last_ack_sent)) {
		tp->ts_recent_age = tcp_ts_getticks();
		tp->ts_recent = to->to_tsval;
	}
	rack = (struct tcp_rack *)tp->t_fb_ptr;
	/*
	 * This is a pure, in-sequence data packet with nothing on the
	 * reassembly queue and we have enough buffer space to take it.
	 */
	nsegs = max(1, m->m_pkthdr.lro_nsegs);


	/* Clean receiver SACK report if present */
	if (tp->rcv_numsacks)
		tcp_clean_sackreport(tp);
	TCPSTAT_INC(tcps_preddat);
	tp->rcv_nxt += tlen;
	/*
	 * Pull snd_wl1 up to prevent seq wrap relative to th_seq.
	 */
	tp->snd_wl1 = th->th_seq;
	/*
	 * Pull rcv_up up to prevent seq wrap relative to rcv_nxt.
	 */
	tp->rcv_up = tp->rcv_nxt;
	TCPSTAT_ADD(tcps_rcvpack, nsegs);
	TCPSTAT_ADD(tcps_rcvbyte, tlen);
#ifdef TCPDEBUG
	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_INPUT, ostate, tp,
		    (void *)tcp_saveipgen, &tcp_savetcp, 0);
#endif
	newsize = tcp_autorcvbuf(m, th, so, tp, tlen);

	/* Add data to socket buffer. */
	SOCKBUF_LOCK(&so->so_rcv);
	if (so->so_rcv.sb_state & SBS_CANTRCVMORE) {
		m_freem(m);
	} else {
		/*
		 * Set new socket buffer size. Give up when limit is
		 * reached.
		 */
		if (newsize)
			if (!sbreserve_locked(&so->so_rcv,
			    newsize, so, NULL))
				so->so_rcv.sb_flags &= ~SB_AUTOSIZE;
		m_adj(m, drop_hdrlen);	/* delayed header drop */
		sbappendstream_locked(&so->so_rcv, m, 0);
		rack_calc_rwin(so, tp);
	}
	/* NB: sorwakeup_locked() does an implicit unlock. */
	sorwakeup_locked(so);
	if (DELAY_ACK(tp, tlen)) {
		rack_timer_cancel(tp, rack, rack->r_ctl.rc_rcvtime, __LINE__);
		tp->t_flags |= TF_DELACK;
	} else {
		tp->t_flags |= TF_ACKNOW;
		rack->r_wanted_output++;
	}
	if ((tp->snd_una == tp->snd_max) && rack_use_sack_filter)
		sack_filter_clear(&rack->r_ctl.rack_sf, tp->snd_una);
	return (1);
}

/*
 * This subfunction is used to try to highly optimize the
 * fast path. We again allow window updates that are
 * in sequence to remain in the fast-path. We also add
 * in the __predict's to attempt to help the compiler.
 * Note that if we return a 0, then we can *not* process
 * it and the caller should push the packet into the
 * slow-path.
 */
static int
rack_fastack(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen, int32_t tlen,
    uint32_t tiwin, int32_t nxt_pkt, uint32_t cts)
{
	int32_t acked;
	int32_t nsegs;

#ifdef TCPDEBUG
	/*
	 * The size of tcp_saveipgen must be the size of the max ip header,
	 * now IPv6.
	 */
	u_char tcp_saveipgen[IP6_HDR_LEN];
	struct tcphdr tcp_savetcp;
	short ostate = 0;

#endif
	struct tcp_rack *rack;

	if (__predict_false(SEQ_LEQ(th->th_ack, tp->snd_una))) {
		/* Old ack, behind (or duplicate to) the last one rcv'd */
		return (0);
	}
	if (__predict_false(SEQ_GT(th->th_ack, tp->snd_max))) {
		/* Above what we have sent? */
		return (0);
	}
	if (__predict_false(tp->snd_nxt != tp->snd_max)) {
		/* We are retransmitting */
		return (0);
	}
	if (__predict_false(tiwin == 0)) {
		/* zero window */
		return (0);
	}
	if (__predict_false(tp->t_flags & (TF_NEEDSYN | TF_NEEDFIN))) {
		/* We need a SYN or a FIN, unlikely.. */
		return (0);
	}
	if ((to->to_flags & TOF_TS) && __predict_false(TSTMP_LT(to->to_tsval, tp->ts_recent))) {
		/* Timestamp is behind .. old ack with seq wrap? */
		return (0);
	}
	if (__predict_false(IN_RECOVERY(tp->t_flags))) {
		/* Still recovering */
		return (0);
	}
	rack = (struct tcp_rack *)tp->t_fb_ptr;
	if (rack->r_ctl.rc_sacked) {
		/* We have sack holes on our scoreboard */
		return (0);
	}
	/* Ok if we reach here, we can process a fast-ack */
	nsegs = max(1, m->m_pkthdr.lro_nsegs);
	rack_log_ack(tp, to, th);
	/* Did the window get updated? */
	if (tiwin != tp->snd_wnd) {
		tp->snd_wnd = tiwin;
		tp->snd_wl1 = th->th_seq;
		if (tp->snd_wnd > tp->max_sndwnd)
			tp->max_sndwnd = tp->snd_wnd;
	}
	if ((rack->rc_in_persist != 0) && (tp->snd_wnd >= tp->t_maxseg)) {
		rack_exit_persist(tp, rack);
	}
	/*
	 * If last ACK falls within this segment's sequence numbers, record
	 * the timestamp. NOTE that the test is modified according to the
	 * latest proposal of the tcplw@cray.com list (Braden 1993/04/26).
	 */
	if ((to->to_flags & TOF_TS) != 0 &&
	    SEQ_LEQ(th->th_seq, tp->last_ack_sent)) {
		tp->ts_recent_age = tcp_ts_getticks();
		tp->ts_recent = to->to_tsval;
	}
	/*
	 * This is a pure ack for outstanding data.
	 */
	TCPSTAT_INC(tcps_predack);

	/*
	 * "bad retransmit" recovery.
	 */
	if (tp->t_flags & TF_PREVVALID) {
		tp->t_flags &= ~TF_PREVVALID;
		if (tp->t_rxtshift == 1 &&
		    (int)(ticks - tp->t_badrxtwin) < 0)
			rack_cong_signal(tp, th, CC_RTO_ERR);
	}
	/*
	 * Recalculate the transmit timer / rtt.
	 *
	 * Some boxes send broken timestamp replies during the SYN+ACK
	 * phase, ignore timestamps of 0 or we could calculate a huge RTT
	 * and blow up the retransmit timer.
	 */
	acked = BYTES_THIS_ACK(tp, th);

#ifdef TCP_HHOOK
	/* Run HHOOK_TCP_ESTABLISHED_IN helper hooks. */
	hhook_run_tcp_est_in(tp, th, to);
#endif

	TCPSTAT_ADD(tcps_rcvackpack, nsegs);
	TCPSTAT_ADD(tcps_rcvackbyte, acked);
	sbdrop(&so->so_snd, acked);
	/*
	 * Let the congestion control algorithm update congestion control
	 * related information. This typically means increasing the
	 * congestion window.
	 */
	rack_ack_received(tp, rack, th, nsegs, CC_ACK, 0);

	tp->snd_una = th->th_ack;
	/*
	 * Pull snd_wl2 up to prevent seq wrap relative to th_ack.
	 */
	tp->snd_wl2 = th->th_ack;
	tp->t_dupacks = 0;
	m_freem(m);
	/* ND6_HINT(tp);	 *//* Some progress has been made. */

	/*
	 * If all outstanding data are acked, stop retransmit timer,
	 * otherwise restart timer using current (possibly backed-off)
	 * value. If process is waiting for space, wakeup/selwakeup/signal.
	 * If data are ready to send, let tcp_output decide between more
	 * output or persist.
	 */
#ifdef TCPDEBUG
	if (so->so_options & SO_DEBUG)
		tcp_trace(TA_INPUT, ostate, tp,
		    (void *)tcp_saveipgen,
		    &tcp_savetcp, 0);
#endif
	if (tp->snd_una == tp->snd_max) {
		rack_log_progress_event(rack, tp, 0, PROGRESS_CLEAR, __LINE__);
		tp->t_acktime = 0;
		rack_timer_cancel(tp, rack, rack->r_ctl.rc_rcvtime, __LINE__);
	}
	/* Wake up the socket if we have room to write more */
	sowwakeup(so);
	if (sbavail(&so->so_snd)) {
		rack->r_wanted_output++;
	}
	return (1);
}

/*
 * Return value of 1, the TCB is unlocked and most
 * likely gone, return value of 0, the TCP is still
 * locked.
 */
static int
rack_do_syn_sent(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen, int32_t tlen,
    uint32_t tiwin, int32_t thflags, int32_t nxt_pkt)
{
	int32_t ret_val = 0;
	int32_t todrop;
	int32_t ourfinisacked = 0;

	rack_calc_rwin(so, tp);
	/*
	 * If the state is SYN_SENT: if seg contains an ACK, but not for our
	 * SYN, drop the input. if seg contains a RST, then drop the
	 * connection. if seg does not contain SYN, then drop it. Otherwise
	 * this is an acceptable SYN segment initialize tp->rcv_nxt and
	 * tp->irs if seg contains ack then advance tp->snd_una if seg
	 * contains an ECE and ECN support is enabled, the stream is ECN
	 * capable. if SYN has been acked change to ESTABLISHED else
	 * SYN_RCVD state arrange for segment to be acked (eventually)
	 * continue processing rest of data/controls, beginning with URG
	 */
	if ((thflags & TH_ACK) &&
	    (SEQ_LEQ(th->th_ack, tp->iss) ||
	    SEQ_GT(th->th_ack, tp->snd_max))) {
		rack_do_dropwithreset(m, tp, th, BANDLIM_RST_OPENPORT, tlen);
		return (1);
	}
	if ((thflags & (TH_ACK | TH_RST)) == (TH_ACK | TH_RST)) {
		TCP_PROBE5(connect__refused, NULL, tp,
		    mtod(m, const char *), tp, th);
		tp = tcp_drop(tp, ECONNREFUSED);
		rack_do_drop(m, tp);
		return (1);
	}
	if (thflags & TH_RST) {
		rack_do_drop(m, tp);
		return (1);
	}
	if (!(thflags & TH_SYN)) {
		rack_do_drop(m, tp);
		return (1);
	}
	tp->irs = th->th_seq;
	tcp_rcvseqinit(tp);
	if (thflags & TH_ACK) {
		int tfo_partial = 0;
		
		TCPSTAT_INC(tcps_connects);
		soisconnected(so);
#ifdef MAC
		mac_socketpeer_set_from_mbuf(m, so);
#endif
		/* Do window scaling on this connection? */
		if ((tp->t_flags & (TF_RCVD_SCALE | TF_REQ_SCALE)) ==
		    (TF_RCVD_SCALE | TF_REQ_SCALE)) {
			tp->rcv_scale = tp->request_r_scale;
		}
		tp->rcv_adv += min(tp->rcv_wnd,
		    TCP_MAXWIN << tp->rcv_scale);
		/*
		 * If not all the data that was sent in the TFO SYN
		 * has been acked, resend the remainder right away.
		 */
		if (IS_FASTOPEN(tp->t_flags) &&
		    (tp->snd_una != tp->snd_max)) {
			tp->snd_nxt = th->th_ack;
			tfo_partial = 1;
		}
		/*
		 * If there's data, delay ACK; if there's also a FIN ACKNOW
		 * will be turned on later.
		 */
		if (DELAY_ACK(tp, tlen) && tlen != 0 && (tfo_partial == 0)) {
			rack_timer_cancel(tp, (struct tcp_rack *)tp->t_fb_ptr,
					  ((struct tcp_rack *)tp->t_fb_ptr)->r_ctl.rc_rcvtime, __LINE__);
			tp->t_flags |= TF_DELACK;
		} else {
			((struct tcp_rack *)tp->t_fb_ptr)->r_wanted_output++;
			tp->t_flags |= TF_ACKNOW;
		}

		if (((thflags & (TH_CWR | TH_ECE)) == TH_ECE) &&
		    V_tcp_do_ecn) {
			tp->t_flags |= TF_ECN_PERMIT;
			TCPSTAT_INC(tcps_ecn_shs);
		}
		if (SEQ_GT(th->th_ack, tp->snd_una)) {
			/* 
			 * We advance snd_una for the 
			 * fast open case. If th_ack is
			 * acknowledging data beyond 
			 * snd_una we can't just call
			 * ack-processing since the 
			 * data stream in our send-map
			 * will start at snd_una + 1 (one
			 * beyond the SYN). If its just
			 * equal we don't need to do that
			 * and there is no send_map.
			 */
			tp->snd_una++;
		}
		/*
		 * Received <SYN,ACK> in SYN_SENT[*] state. Transitions:
		 * SYN_SENT  --> ESTABLISHED SYN_SENT* --> FIN_WAIT_1
		 */
		tp->t_starttime = ticks;
		if (tp->t_flags & TF_NEEDFIN) {
			tcp_state_change(tp, TCPS_FIN_WAIT_1);
			tp->t_flags &= ~TF_NEEDFIN;
			thflags &= ~TH_SYN;
		} else {
			tcp_state_change(tp, TCPS_ESTABLISHED);
			TCP_PROBE5(connect__established, NULL, tp,
			    mtod(m, const char *), tp, th);
			cc_conn_init(tp);
		}
	} else {
		/*
		 * Received initial SYN in SYN-SENT[*] state => simultaneous
		 * open.  If segment contains CC option and there is a
		 * cached CC, apply TAO test. If it succeeds, connection is *
		 * half-synchronized. Otherwise, do 3-way handshake:
		 * SYN-SENT -> SYN-RECEIVED SYN-SENT* -> SYN-RECEIVED* If
		 * there was no CC option, clear cached CC value.
		 */
		tp->t_flags |= (TF_ACKNOW | TF_NEEDSYN);
		tcp_state_change(tp, TCPS_SYN_RECEIVED);
	}
	INP_INFO_RLOCK_ASSERT(&V_tcbinfo);
	INP_WLOCK_ASSERT(tp->t_inpcb);
	/*
	 * Advance th->th_seq to correspond to first data byte. If data,
	 * trim to stay within window, dropping FIN if necessary.
	 */
	th->th_seq++;
	if (tlen > tp->rcv_wnd) {
		todrop = tlen - tp->rcv_wnd;
		m_adj(m, -todrop);
		tlen = tp->rcv_wnd;
		thflags &= ~TH_FIN;
		TCPSTAT_INC(tcps_rcvpackafterwin);
		TCPSTAT_ADD(tcps_rcvbyteafterwin, todrop);
	}
	tp->snd_wl1 = th->th_seq - 1;
	tp->rcv_up = th->th_seq;
	/*
	 * Client side of transaction: already sent SYN and data. If the
	 * remote host used T/TCP to validate the SYN, our data will be
	 * ACK'd; if so, enter normal data segment processing in the middle
	 * of step 5, ack processing. Otherwise, goto step 6.
	 */
	if (thflags & TH_ACK) {
		if (rack_process_ack(m, th, so, tp, to, tiwin, tlen, &ourfinisacked, thflags, &ret_val))
			return (ret_val);
		/* We may have changed to FIN_WAIT_1 above */
		if (tp->t_state == TCPS_FIN_WAIT_1) {
			/*
			 * In FIN_WAIT_1 STATE in addition to the processing
			 * for the ESTABLISHED state if our FIN is now
			 * acknowledged then enter FIN_WAIT_2.
			 */
			if (ourfinisacked) {
				/*
				 * If we can't receive any more data, then
				 * closing user can proceed. Starting the
				 * timer is contrary to the specification,
				 * but if we don't get a FIN we'll hang
				 * forever.
				 *
				 * XXXjl: we should release the tp also, and
				 * use a compressed state.
				 */
				if (so->so_rcv.sb_state & SBS_CANTRCVMORE) {
					soisdisconnected(so);
					tcp_timer_activate(tp, TT_2MSL,
					    (tcp_fast_finwait2_recycle ?
					    tcp_finwait2_timeout :
					    TP_MAXIDLE(tp)));
				}
				tcp_state_change(tp, TCPS_FIN_WAIT_2);
			}
		}
	}
	return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
	   tiwin, thflags, nxt_pkt));
}

/*
 * Return value of 1, the TCB is unlocked and most
 * likely gone, return value of 0, the TCP is still
 * locked.
 */
static int
rack_do_syn_recv(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen, int32_t tlen,
    uint32_t tiwin, int32_t thflags, int32_t nxt_pkt)
{
	int32_t ret_val = 0;
	int32_t ourfinisacked = 0;

	rack_calc_rwin(so, tp);

	if ((thflags & TH_ACK) &&
	    (SEQ_LEQ(th->th_ack, tp->snd_una) ||
	    SEQ_GT(th->th_ack, tp->snd_max))) {
		rack_do_dropwithreset(m, tp, th, BANDLIM_RST_OPENPORT, tlen);
		return (1);
	}
	if (IS_FASTOPEN(tp->t_flags)) {
		/*
		 * When a TFO connection is in SYN_RECEIVED, the
		 * only valid packets are the initial SYN, a
		 * retransmit/copy of the initial SYN (possibly with
		 * a subset of the original data), a valid ACK, a
		 * FIN, or a RST.
		 */
		if ((thflags & (TH_SYN | TH_ACK)) == (TH_SYN | TH_ACK)) {
			rack_do_dropwithreset(m, tp, th, BANDLIM_RST_OPENPORT, tlen);
			return (1);
		} else if (thflags & TH_SYN) {
			/* non-initial SYN is ignored */
			struct tcp_rack *rack;

			rack = (struct tcp_rack *)tp->t_fb_ptr;
			if ((rack->r_ctl.rc_hpts_flags & PACE_TMR_RXT) ||
			    (rack->r_ctl.rc_hpts_flags & PACE_TMR_TLP) ||
			    (rack->r_ctl.rc_hpts_flags & PACE_TMR_RACK)) {
				rack_do_drop(m, NULL);
				return (0);
			}
		} else if (!(thflags & (TH_ACK | TH_FIN | TH_RST))) {
			rack_do_drop(m, NULL);
			return (0);
		}
	}
	if (thflags & TH_RST)
		return (rack_process_rst(m, th, so, tp));
	/*
	 * RFC 1323 PAWS: If we have a timestamp reply on this segment and
	 * it's less than ts_recent, drop it.
	 */
	if ((to->to_flags & TOF_TS) != 0 && tp->ts_recent &&
	    TSTMP_LT(to->to_tsval, tp->ts_recent)) {
		if (rack_ts_check(m, th, tp, tlen, thflags, &ret_val))
			return (ret_val);
	}
	/*
	 * In the SYN-RECEIVED state, validate that the packet belongs to
	 * this connection before trimming the data to fit the receive
	 * window.  Check the sequence number versus IRS since we know the
	 * sequence numbers haven't wrapped.  This is a partial fix for the
	 * "LAND" DoS attack.
	 */
	if (SEQ_LT(th->th_seq, tp->irs)) {
		rack_do_dropwithreset(m, tp, th, BANDLIM_RST_OPENPORT, tlen);
		return (1);
	}
	if (rack_drop_checks(to, m, th, tp, &tlen, &thflags, &drop_hdrlen, &ret_val)) {
		return (ret_val);
	}
	/*
	 * If last ACK falls within this segment's sequence numbers, record
	 * its timestamp. NOTE: 1) That the test incorporates suggestions
	 * from the latest proposal of the tcplw@cray.com list (Braden
	 * 1993/04/26). 2) That updating only on newer timestamps interferes
	 * with our earlier PAWS tests, so this check should be solely
	 * predicated on the sequence space of this segment. 3) That we
	 * modify the segment boundary check to be Last.ACK.Sent <= SEG.SEQ
	 * + SEG.Len  instead of RFC1323's Last.ACK.Sent < SEG.SEQ +
	 * SEG.Len, This modified check allows us to overcome RFC1323's
	 * limitations as described in Stevens TCP/IP Illustrated Vol. 2
	 * p.869. In such cases, we can still calculate the RTT correctly
	 * when RCV.NXT == Last.ACK.Sent.
	 */
	if ((to->to_flags & TOF_TS) != 0 &&
	    SEQ_LEQ(th->th_seq, tp->last_ack_sent) &&
	    SEQ_LEQ(tp->last_ack_sent, th->th_seq + tlen +
	    ((thflags & (TH_SYN | TH_FIN)) != 0))) {
		tp->ts_recent_age = tcp_ts_getticks();
		tp->ts_recent = to->to_tsval;
	}
	tp->snd_wnd = tiwin;
	/*
	 * If the ACK bit is off:  if in SYN-RECEIVED state or SENDSYN flag
	 * is on (half-synchronized state), then queue data for later
	 * processing; else drop segment and return.
	 */
	if ((thflags & TH_ACK) == 0) {
		if (IS_FASTOPEN(tp->t_flags)) {
			cc_conn_init(tp);
		}
		return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
		    tiwin, thflags, nxt_pkt));
	}
	TCPSTAT_INC(tcps_connects);
	soisconnected(so);
	/* Do window scaling? */
	if ((tp->t_flags & (TF_RCVD_SCALE | TF_REQ_SCALE)) ==
	    (TF_RCVD_SCALE | TF_REQ_SCALE)) {
		tp->rcv_scale = tp->request_r_scale;
	}
	/*
	 * Make transitions: SYN-RECEIVED  -> ESTABLISHED SYN-RECEIVED* ->
	 * FIN-WAIT-1
	 */
	tp->t_starttime = ticks;
	if (IS_FASTOPEN(tp->t_flags) && tp->t_tfo_pending) {
		tcp_fastopen_decrement_counter(tp->t_tfo_pending);
		tp->t_tfo_pending = NULL;

		/*
		 * Account for the ACK of our SYN prior to
		 * regular ACK processing below.
		 */ 
		tp->snd_una++;
	}
	if (tp->t_flags & TF_NEEDFIN) {
		tcp_state_change(tp, TCPS_FIN_WAIT_1);
		tp->t_flags &= ~TF_NEEDFIN;
	} else {
		tcp_state_change(tp, TCPS_ESTABLISHED);
		TCP_PROBE5(accept__established, NULL, tp,
		    mtod(m, const char *), tp, th);
		/*
		 * TFO connections call cc_conn_init() during SYN
		 * processing.  Calling it again here for such connections
		 * is not harmless as it would undo the snd_cwnd reduction
		 * that occurs when a TFO SYN|ACK is retransmitted.
		 */
		if (!IS_FASTOPEN(tp->t_flags))
			cc_conn_init(tp);
	}
	/*
	 * If segment contains data or ACK, will call tcp_reass() later; if
	 * not, do so now to pass queued data to user.
	 */
	if (tlen == 0 && (thflags & TH_FIN) == 0)
		(void) tcp_reass(tp, (struct tcphdr *)0, NULL, 0,
		    (struct mbuf *)0);
	tp->snd_wl1 = th->th_seq - 1;
	if (rack_process_ack(m, th, so, tp, to, tiwin, tlen, &ourfinisacked, thflags, &ret_val)) {
		return (ret_val);
	}
	if (tp->t_state == TCPS_FIN_WAIT_1) {
		/* We could have went to FIN_WAIT_1 (or EST) above */
		/*
		 * In FIN_WAIT_1 STATE in addition to the processing for the
		 * ESTABLISHED state if our FIN is now acknowledged then
		 * enter FIN_WAIT_2.
		 */
		if (ourfinisacked) {
			/*
			 * If we can't receive any more data, then closing
			 * user can proceed. Starting the timer is contrary
			 * to the specification, but if we don't get a FIN
			 * we'll hang forever.
			 *
			 * XXXjl: we should release the tp also, and use a
			 * compressed state.
			 */
			if (so->so_rcv.sb_state & SBS_CANTRCVMORE) {
				soisdisconnected(so);
				tcp_timer_activate(tp, TT_2MSL,
				    (tcp_fast_finwait2_recycle ?
				    tcp_finwait2_timeout :
				    TP_MAXIDLE(tp)));
			}
			tcp_state_change(tp, TCPS_FIN_WAIT_2);
		}
	}
	return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
	    tiwin, thflags, nxt_pkt));
}

/*
 * Return value of 1, the TCB is unlocked and most
 * likely gone, return value of 0, the TCP is still
 * locked.
 */
static int
rack_do_established(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen, int32_t tlen,
    uint32_t tiwin, int32_t thflags, int32_t nxt_pkt)
{
	int32_t ret_val = 0;

	/*
	 * Header prediction: check for the two common cases of a
	 * uni-directional data xfer.  If the packet has no control flags,
	 * is in-sequence, the window didn't change and we're not
	 * retransmitting, it's a candidate.  If the length is zero and the
	 * ack moved forward, we're the sender side of the xfer.  Just free
	 * the data acked & wake any higher level process that was blocked
	 * waiting for space.  If the length is non-zero and the ack didn't
	 * move, we're the receiver side.  If we're getting packets in-order
	 * (the reassembly queue is empty), add the data toc The socket
	 * buffer and note that we need a delayed ack. Make sure that the
	 * hidden state-flags are also off. Since we check for
	 * TCPS_ESTABLISHED first, it can only be TH_NEEDSYN.
	 */
	if (__predict_true(((to->to_flags & TOF_SACK) == 0)) &&
	    __predict_true((thflags & (TH_SYN | TH_FIN | TH_RST | TH_URG | TH_ACK)) == TH_ACK) &&
	    __predict_true(SEGQ_EMPTY(tp)) &&
	    __predict_true(th->th_seq == tp->rcv_nxt)) {
		struct tcp_rack *rack;

		rack = (struct tcp_rack *)tp->t_fb_ptr;
		if (tlen == 0) {
			if (rack_fastack(m, th, so, tp, to, drop_hdrlen, tlen,
			    tiwin, nxt_pkt, rack->r_ctl.rc_rcvtime)) {
				return (0);
			}
		} else {
			if (rack_do_fastnewdata(m, th, so, tp, to, drop_hdrlen, tlen,
			    tiwin, nxt_pkt)) {
				return (0);
			}
		}
	}
	rack_calc_rwin(so, tp);

	if (thflags & TH_RST)
		return (rack_process_rst(m, th, so, tp));

	/*
	 * RFC5961 Section 4.2 Send challenge ACK for any SYN in
	 * synchronized state.
	 */
	if (thflags & TH_SYN) {
		rack_challenge_ack(m, th, tp, &ret_val);
		return (ret_val);
	}
	/*
	 * RFC 1323 PAWS: If we have a timestamp reply on this segment and
	 * it's less than ts_recent, drop it.
	 */
	if ((to->to_flags & TOF_TS) != 0 && tp->ts_recent &&
	    TSTMP_LT(to->to_tsval, tp->ts_recent)) {
		if (rack_ts_check(m, th, tp, tlen, thflags, &ret_val))
			return (ret_val);
	}
	if (rack_drop_checks(to, m, th, tp, &tlen, &thflags, &drop_hdrlen, &ret_val)) {
		return (ret_val);
	}
	/*
	 * If last ACK falls within this segment's sequence numbers, record
	 * its timestamp. NOTE: 1) That the test incorporates suggestions
	 * from the latest proposal of the tcplw@cray.com list (Braden
	 * 1993/04/26). 2) That updating only on newer timestamps interferes
	 * with our earlier PAWS tests, so this check should be solely
	 * predicated on the sequence space of this segment. 3) That we
	 * modify the segment boundary check to be Last.ACK.Sent <= SEG.SEQ
	 * + SEG.Len  instead of RFC1323's Last.ACK.Sent < SEG.SEQ +
	 * SEG.Len, This modified check allows us to overcome RFC1323's
	 * limitations as described in Stevens TCP/IP Illustrated Vol. 2
	 * p.869. In such cases, we can still calculate the RTT correctly
	 * when RCV.NXT == Last.ACK.Sent.
	 */
	if ((to->to_flags & TOF_TS) != 0 &&
	    SEQ_LEQ(th->th_seq, tp->last_ack_sent) &&
	    SEQ_LEQ(tp->last_ack_sent, th->th_seq + tlen +
	    ((thflags & (TH_SYN | TH_FIN)) != 0))) {
		tp->ts_recent_age = tcp_ts_getticks();
		tp->ts_recent = to->to_tsval;
	}
	/*
	 * If the ACK bit is off:  if in SYN-RECEIVED state or SENDSYN flag
	 * is on (half-synchronized state), then queue data for later
	 * processing; else drop segment and return.
	 */
	if ((thflags & TH_ACK) == 0) {
		if (tp->t_flags & TF_NEEDSYN) {

			return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
			    tiwin, thflags, nxt_pkt));

		} else if (tp->t_flags & TF_ACKNOW) {
			rack_do_dropafterack(m, tp, th, thflags, tlen, &ret_val);
			return (ret_val);
		} else {
			rack_do_drop(m, NULL);
			return (0);
		}
	}
	/*
	 * Ack processing.
	 */
	if (rack_process_ack(m, th, so, tp, to, tiwin, tlen, NULL, thflags, &ret_val)) {
		return (ret_val);
	}
	if (sbavail(&so->so_snd)) {
		if (rack_progress_timeout_check(tp)) {
			tcp_set_inp_to_drop(tp->t_inpcb, ETIMEDOUT);
			rack_do_dropwithreset(m, tp, th, BANDLIM_RST_OPENPORT, tlen);
			return (1);
		}
	}
	/* State changes only happen in rack_process_data() */
	return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
	    tiwin, thflags, nxt_pkt));
}

/*
 * Return value of 1, the TCB is unlocked and most
 * likely gone, return value of 0, the TCP is still
 * locked.
 */
static int
rack_do_close_wait(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen, int32_t tlen,
    uint32_t tiwin, int32_t thflags, int32_t nxt_pkt)
{
	int32_t ret_val = 0;

	rack_calc_rwin(so, tp);
	if (thflags & TH_RST)
		return (rack_process_rst(m, th, so, tp));
	/*
	 * RFC5961 Section 4.2 Send challenge ACK for any SYN in
	 * synchronized state.
	 */
	if (thflags & TH_SYN) {
		rack_challenge_ack(m, th, tp, &ret_val);
		return (ret_val);
	}
	/*
	 * RFC 1323 PAWS: If we have a timestamp reply on this segment and
	 * it's less than ts_recent, drop it.
	 */
	if ((to->to_flags & TOF_TS) != 0 && tp->ts_recent &&
	    TSTMP_LT(to->to_tsval, tp->ts_recent)) {
		if (rack_ts_check(m, th, tp, tlen, thflags, &ret_val))
			return (ret_val);
	}
	if (rack_drop_checks(to, m, th, tp, &tlen, &thflags, &drop_hdrlen, &ret_val)) {
		return (ret_val);
	}
	/*
	 * If last ACK falls within this segment's sequence numbers, record
	 * its timestamp. NOTE: 1) That the test incorporates suggestions
	 * from the latest proposal of the tcplw@cray.com list (Braden
	 * 1993/04/26). 2) That updating only on newer timestamps interferes
	 * with our earlier PAWS tests, so this check should be solely
	 * predicated on the sequence space of this segment. 3) That we
	 * modify the segment boundary check to be Last.ACK.Sent <= SEG.SEQ
	 * + SEG.Len  instead of RFC1323's Last.ACK.Sent < SEG.SEQ +
	 * SEG.Len, This modified check allows us to overcome RFC1323's
	 * limitations as described in Stevens TCP/IP Illustrated Vol. 2
	 * p.869. In such cases, we can still calculate the RTT correctly
	 * when RCV.NXT == Last.ACK.Sent.
	 */
	if ((to->to_flags & TOF_TS) != 0 &&
	    SEQ_LEQ(th->th_seq, tp->last_ack_sent) &&
	    SEQ_LEQ(tp->last_ack_sent, th->th_seq + tlen +
	    ((thflags & (TH_SYN | TH_FIN)) != 0))) {
		tp->ts_recent_age = tcp_ts_getticks();
		tp->ts_recent = to->to_tsval;
	}
	/*
	 * If the ACK bit is off:  if in SYN-RECEIVED state or SENDSYN flag
	 * is on (half-synchronized state), then queue data for later
	 * processing; else drop segment and return.
	 */
	if ((thflags & TH_ACK) == 0) {
		if (tp->t_flags & TF_NEEDSYN) {
			return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
			    tiwin, thflags, nxt_pkt));

		} else if (tp->t_flags & TF_ACKNOW) {
			rack_do_dropafterack(m, tp, th, thflags, tlen, &ret_val);
			return (ret_val);
		} else {
			rack_do_drop(m, NULL);
			return (0);
		}
	}
	/*
	 * Ack processing.
	 */
	if (rack_process_ack(m, th, so, tp, to, tiwin, tlen, NULL, thflags, &ret_val)) {
		return (ret_val);
	}
	if (sbavail(&so->so_snd)) {
		if (rack_progress_timeout_check(tp)) {
			tcp_set_inp_to_drop(tp->t_inpcb, ETIMEDOUT);
			rack_do_dropwithreset(m, tp, th, BANDLIM_RST_OPENPORT, tlen);
			return (1);
		}
	}
	return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
	    tiwin, thflags, nxt_pkt));
}

static int
rack_check_data_after_close(struct mbuf *m, 
    struct tcpcb *tp, int32_t *tlen, struct tcphdr *th, struct socket *so)
{
	struct tcp_rack *rack;

	INP_INFO_RLOCK_ASSERT(&V_tcbinfo);
	rack = (struct tcp_rack *)tp->t_fb_ptr;
	if (rack->rc_allow_data_af_clo == 0) {
	close_now:
		tp = tcp_close(tp);
		TCPSTAT_INC(tcps_rcvafterclose);
		rack_do_dropwithreset(m, tp, th, BANDLIM_UNLIMITED, (*tlen));
		return (1);
	}
	if (sbavail(&so->so_snd) == 0)
		goto close_now;
	/* Ok we allow data that is ignored and a followup reset */
	tp->rcv_nxt = th->th_seq + *tlen;
	tp->t_flags2 |= TF2_DROP_AF_DATA;
	rack->r_wanted_output = 1;
	*tlen = 0;
	return (0);
}

/*
 * Return value of 1, the TCB is unlocked and most
 * likely gone, return value of 0, the TCP is still
 * locked.
 */
static int
rack_do_fin_wait_1(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen, int32_t tlen,
    uint32_t tiwin, int32_t thflags, int32_t nxt_pkt)
{
	int32_t ret_val = 0;
	int32_t ourfinisacked = 0;

	rack_calc_rwin(so, tp);

	if (thflags & TH_RST)
		return (rack_process_rst(m, th, so, tp));
	/*
	 * RFC5961 Section 4.2 Send challenge ACK for any SYN in
	 * synchronized state.
	 */
	if (thflags & TH_SYN) {
		rack_challenge_ack(m, th, tp, &ret_val);
		return (ret_val);
	}
	/*
	 * RFC 1323 PAWS: If we have a timestamp reply on this segment and
	 * it's less than ts_recent, drop it.
	 */
	if ((to->to_flags & TOF_TS) != 0 && tp->ts_recent &&
	    TSTMP_LT(to->to_tsval, tp->ts_recent)) {
		if (rack_ts_check(m, th, tp, tlen, thflags, &ret_val))
			return (ret_val);
	}
	if (rack_drop_checks(to, m, th, tp, &tlen, &thflags, &drop_hdrlen, &ret_val)) {
		return (ret_val);
	}
	/*
	 * If new data are received on a connection after the user processes
	 * are gone, then RST the other end.
	 */
	if ((so->so_state & SS_NOFDREF) && tlen) {
		if (rack_check_data_after_close(m, tp, &tlen, th, so))
			return (1);
	}
	/*
	 * If last ACK falls within this segment's sequence numbers, record
	 * its timestamp. NOTE: 1) That the test incorporates suggestions
	 * from the latest proposal of the tcplw@cray.com list (Braden
	 * 1993/04/26). 2) That updating only on newer timestamps interferes
	 * with our earlier PAWS tests, so this check should be solely
	 * predicated on the sequence space of this segment. 3) That we
	 * modify the segment boundary check to be Last.ACK.Sent <= SEG.SEQ
	 * + SEG.Len  instead of RFC1323's Last.ACK.Sent < SEG.SEQ +
	 * SEG.Len, This modified check allows us to overcome RFC1323's
	 * limitations as described in Stevens TCP/IP Illustrated Vol. 2
	 * p.869. In such cases, we can still calculate the RTT correctly
	 * when RCV.NXT == Last.ACK.Sent.
	 */
	if ((to->to_flags & TOF_TS) != 0 &&
	    SEQ_LEQ(th->th_seq, tp->last_ack_sent) &&
	    SEQ_LEQ(tp->last_ack_sent, th->th_seq + tlen +
	    ((thflags & (TH_SYN | TH_FIN)) != 0))) {
		tp->ts_recent_age = tcp_ts_getticks();
		tp->ts_recent = to->to_tsval;
	}
	/*
	 * If the ACK bit is off:  if in SYN-RECEIVED state or SENDSYN flag
	 * is on (half-synchronized state), then queue data for later
	 * processing; else drop segment and return.
	 */
	if ((thflags & TH_ACK) == 0) {
		if (tp->t_flags & TF_NEEDSYN) {
			return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
			    tiwin, thflags, nxt_pkt));
		} else if (tp->t_flags & TF_ACKNOW) {
			rack_do_dropafterack(m, tp, th, thflags, tlen, &ret_val);
			return (ret_val);
		} else {
			rack_do_drop(m, NULL);
			return (0);
		}
	}
	/*
	 * Ack processing.
	 */
	if (rack_process_ack(m, th, so, tp, to, tiwin, tlen, &ourfinisacked, thflags, &ret_val)) {
		return (ret_val);
	}
	if (ourfinisacked) {
		/*
		 * If we can't receive any more data, then closing user can
		 * proceed. Starting the timer is contrary to the
		 * specification, but if we don't get a FIN we'll hang
		 * forever.
		 *
		 * XXXjl: we should release the tp also, and use a
		 * compressed state.
		 */
		if (so->so_rcv.sb_state & SBS_CANTRCVMORE) {
			soisdisconnected(so);
			tcp_timer_activate(tp, TT_2MSL,
			    (tcp_fast_finwait2_recycle ?
			    tcp_finwait2_timeout :
			    TP_MAXIDLE(tp)));
		}
		tcp_state_change(tp, TCPS_FIN_WAIT_2);
	}
	if (sbavail(&so->so_snd)) {
		if (rack_progress_timeout_check(tp)) {
			tcp_set_inp_to_drop(tp->t_inpcb, ETIMEDOUT);
			rack_do_dropwithreset(m, tp, th, BANDLIM_RST_OPENPORT, tlen);
			return (1);
		}
	}
	return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
	    tiwin, thflags, nxt_pkt));
}

/*
 * Return value of 1, the TCB is unlocked and most
 * likely gone, return value of 0, the TCP is still
 * locked.
 */
static int
rack_do_closing(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen, int32_t tlen,
    uint32_t tiwin, int32_t thflags, int32_t nxt_pkt)
{
	int32_t ret_val = 0;
	int32_t ourfinisacked = 0;

	rack_calc_rwin(so, tp);

	if (thflags & TH_RST)
		return (rack_process_rst(m, th, so, tp));
	/*
	 * RFC5961 Section 4.2 Send challenge ACK for any SYN in
	 * synchronized state.
	 */
	if (thflags & TH_SYN) {
		rack_challenge_ack(m, th, tp, &ret_val);
		return (ret_val);
	}
	/*
	 * RFC 1323 PAWS: If we have a timestamp reply on this segment and
	 * it's less than ts_recent, drop it.
	 */
	if ((to->to_flags & TOF_TS) != 0 && tp->ts_recent &&
	    TSTMP_LT(to->to_tsval, tp->ts_recent)) {
		if (rack_ts_check(m, th, tp, tlen, thflags, &ret_val))
			return (ret_val);
	}
	if (rack_drop_checks(to, m, th, tp, &tlen, &thflags, &drop_hdrlen, &ret_val)) {
		return (ret_val);
	}
	/*
	 * If new data are received on a connection after the user processes
	 * are gone, then RST the other end.
	 */
	if ((so->so_state & SS_NOFDREF) && tlen) {
		if (rack_check_data_after_close(m, tp, &tlen, th, so))
			return (1);
	}
	/*
	 * If last ACK falls within this segment's sequence numbers, record
	 * its timestamp. NOTE: 1) That the test incorporates suggestions
	 * from the latest proposal of the tcplw@cray.com list (Braden
	 * 1993/04/26). 2) That updating only on newer timestamps interferes
	 * with our earlier PAWS tests, so this check should be solely
	 * predicated on the sequence space of this segment. 3) That we
	 * modify the segment boundary check to be Last.ACK.Sent <= SEG.SEQ
	 * + SEG.Len  instead of RFC1323's Last.ACK.Sent < SEG.SEQ +
	 * SEG.Len, This modified check allows us to overcome RFC1323's
	 * limitations as described in Stevens TCP/IP Illustrated Vol. 2
	 * p.869. In such cases, we can still calculate the RTT correctly
	 * when RCV.NXT == Last.ACK.Sent.
	 */
	if ((to->to_flags & TOF_TS) != 0 &&
	    SEQ_LEQ(th->th_seq, tp->last_ack_sent) &&
	    SEQ_LEQ(tp->last_ack_sent, th->th_seq + tlen +
	    ((thflags & (TH_SYN | TH_FIN)) != 0))) {
		tp->ts_recent_age = tcp_ts_getticks();
		tp->ts_recent = to->to_tsval;
	}
	/*
	 * If the ACK bit is off:  if in SYN-RECEIVED state or SENDSYN flag
	 * is on (half-synchronized state), then queue data for later
	 * processing; else drop segment and return.
	 */
	if ((thflags & TH_ACK) == 0) {
		if (tp->t_flags & TF_NEEDSYN) {
			return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
			    tiwin, thflags, nxt_pkt));
		} else if (tp->t_flags & TF_ACKNOW) {
			rack_do_dropafterack(m, tp, th, thflags, tlen, &ret_val);
			return (ret_val);
		} else {
			rack_do_drop(m, NULL);
			return (0);
		}
	}
	/*
	 * Ack processing.
	 */
	if (rack_process_ack(m, th, so, tp, to, tiwin, tlen, &ourfinisacked, thflags, &ret_val)) {
		return (ret_val);
	}
	if (ourfinisacked) {
		INP_INFO_RLOCK_ASSERT(&V_tcbinfo);
		tcp_twstart(tp);
		m_freem(m);
		return (1);
	}
	if (sbavail(&so->so_snd)) {
		if (rack_progress_timeout_check(tp)) {
			tcp_set_inp_to_drop(tp->t_inpcb, ETIMEDOUT);
			rack_do_dropwithreset(m, tp, th, BANDLIM_RST_OPENPORT, tlen);
			return (1);
		}
	}
	return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
	    tiwin, thflags, nxt_pkt));
}

/*
 * Return value of 1, the TCB is unlocked and most
 * likely gone, return value of 0, the TCP is still
 * locked.
 */
static int
rack_do_lastack(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen, int32_t tlen,
    uint32_t tiwin, int32_t thflags, int32_t nxt_pkt)
{
	int32_t ret_val = 0;
	int32_t ourfinisacked = 0;

	rack_calc_rwin(so, tp);

	if (thflags & TH_RST)
		return (rack_process_rst(m, th, so, tp));
	/*
	 * RFC5961 Section 4.2 Send challenge ACK for any SYN in
	 * synchronized state.
	 */
	if (thflags & TH_SYN) {
		rack_challenge_ack(m, th, tp, &ret_val);
		return (ret_val);
	}
	/*
	 * RFC 1323 PAWS: If we have a timestamp reply on this segment and
	 * it's less than ts_recent, drop it.
	 */
	if ((to->to_flags & TOF_TS) != 0 && tp->ts_recent &&
	    TSTMP_LT(to->to_tsval, tp->ts_recent)) {
		if (rack_ts_check(m, th, tp, tlen, thflags, &ret_val))
			return (ret_val);
	}
	if (rack_drop_checks(to, m, th, tp, &tlen, &thflags, &drop_hdrlen, &ret_val)) {
		return (ret_val);
	}
	/*
	 * If new data are received on a connection after the user processes
	 * are gone, then RST the other end.
	 */
	if ((so->so_state & SS_NOFDREF) && tlen) {
		if (rack_check_data_after_close(m, tp, &tlen, th, so))
			return (1);
	}
	/*
	 * If last ACK falls within this segment's sequence numbers, record
	 * its timestamp. NOTE: 1) That the test incorporates suggestions
	 * from the latest proposal of the tcplw@cray.com list (Braden
	 * 1993/04/26). 2) That updating only on newer timestamps interferes
	 * with our earlier PAWS tests, so this check should be solely
	 * predicated on the sequence space of this segment. 3) That we
	 * modify the segment boundary check to be Last.ACK.Sent <= SEG.SEQ
	 * + SEG.Len  instead of RFC1323's Last.ACK.Sent < SEG.SEQ +
	 * SEG.Len, This modified check allows us to overcome RFC1323's
	 * limitations as described in Stevens TCP/IP Illustrated Vol. 2
	 * p.869. In such cases, we can still calculate the RTT correctly
	 * when RCV.NXT == Last.ACK.Sent.
	 */
	if ((to->to_flags & TOF_TS) != 0 &&
	    SEQ_LEQ(th->th_seq, tp->last_ack_sent) &&
	    SEQ_LEQ(tp->last_ack_sent, th->th_seq + tlen +
	    ((thflags & (TH_SYN | TH_FIN)) != 0))) {
		tp->ts_recent_age = tcp_ts_getticks();
		tp->ts_recent = to->to_tsval;
	}
	/*
	 * If the ACK bit is off:  if in SYN-RECEIVED state or SENDSYN flag
	 * is on (half-synchronized state), then queue data for later
	 * processing; else drop segment and return.
	 */
	if ((thflags & TH_ACK) == 0) {
		if (tp->t_flags & TF_NEEDSYN) {
			return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
			    tiwin, thflags, nxt_pkt));
		} else if (tp->t_flags & TF_ACKNOW) {
			rack_do_dropafterack(m, tp, th, thflags, tlen, &ret_val);
			return (ret_val);
		} else {
			rack_do_drop(m, NULL);
			return (0);
		}
	}
	/*
	 * case TCPS_LAST_ACK: Ack processing.
	 */
	if (rack_process_ack(m, th, so, tp, to, tiwin, tlen, &ourfinisacked, thflags, &ret_val)) {
		return (ret_val);
	}
	if (ourfinisacked) {
		INP_INFO_RLOCK_ASSERT(&V_tcbinfo);
		tp = tcp_close(tp);
		rack_do_drop(m, tp);
		return (1);
	}
	if (sbavail(&so->so_snd)) {
		if (rack_progress_timeout_check(tp)) {
			tcp_set_inp_to_drop(tp->t_inpcb, ETIMEDOUT);
			rack_do_dropwithreset(m, tp, th, BANDLIM_RST_OPENPORT, tlen);
			return (1);
		}
	}
	return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
	    tiwin, thflags, nxt_pkt));
}


/*
 * Return value of 1, the TCB is unlocked and most
 * likely gone, return value of 0, the TCP is still
 * locked.
 */
static int
rack_do_fin_wait_2(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, struct tcpopt *to, int32_t drop_hdrlen, int32_t tlen,
    uint32_t tiwin, int32_t thflags, int32_t nxt_pkt)
{
	int32_t ret_val = 0;
	int32_t ourfinisacked = 0;

	rack_calc_rwin(so, tp);

	/* Reset receive buffer auto scaling when not in bulk receive mode. */
	if (thflags & TH_RST)
		return (rack_process_rst(m, th, so, tp));
	/*
	 * RFC5961 Section 4.2 Send challenge ACK for any SYN in
	 * synchronized state.
	 */
	if (thflags & TH_SYN) {
		rack_challenge_ack(m, th, tp, &ret_val);
		return (ret_val);
	}
	/*
	 * RFC 1323 PAWS: If we have a timestamp reply on this segment and
	 * it's less than ts_recent, drop it.
	 */
	if ((to->to_flags & TOF_TS) != 0 && tp->ts_recent &&
	    TSTMP_LT(to->to_tsval, tp->ts_recent)) {
		if (rack_ts_check(m, th, tp, tlen, thflags, &ret_val))
			return (ret_val);
	}
	if (rack_drop_checks(to, m, th, tp, &tlen, &thflags, &drop_hdrlen, &ret_val)) {
		return (ret_val);
	}
	/*
	 * If new data are received on a connection after the user processes
	 * are gone, then RST the other end.
	 */
	if ((so->so_state & SS_NOFDREF) &&
	    tlen) {
		if (rack_check_data_after_close(m, tp, &tlen, th, so))
			return (1);
	}
	/*
	 * If last ACK falls within this segment's sequence numbers, record
	 * its timestamp. NOTE: 1) That the test incorporates suggestions
	 * from the latest proposal of the tcplw@cray.com list (Braden
	 * 1993/04/26). 2) That updating only on newer timestamps interferes
	 * with our earlier PAWS tests, so this check should be solely
	 * predicated on the sequence space of this segment. 3) That we
	 * modify the segment boundary check to be Last.ACK.Sent <= SEG.SEQ
	 * + SEG.Len  instead of RFC1323's Last.ACK.Sent < SEG.SEQ +
	 * SEG.Len, This modified check allows us to overcome RFC1323's
	 * limitations as described in Stevens TCP/IP Illustrated Vol. 2
	 * p.869. In such cases, we can still calculate the RTT correctly
	 * when RCV.NXT == Last.ACK.Sent.
	 */
	if ((to->to_flags & TOF_TS) != 0 &&
	    SEQ_LEQ(th->th_seq, tp->last_ack_sent) &&
	    SEQ_LEQ(tp->last_ack_sent, th->th_seq + tlen +
	    ((thflags & (TH_SYN | TH_FIN)) != 0))) {
		tp->ts_recent_age = tcp_ts_getticks();
		tp->ts_recent = to->to_tsval;
	}
	/*
	 * If the ACK bit is off:  if in SYN-RECEIVED state or SENDSYN flag
	 * is on (half-synchronized state), then queue data for later
	 * processing; else drop segment and return.
	 */
	if ((thflags & TH_ACK) == 0) {
		if (tp->t_flags & TF_NEEDSYN) {
			return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
			    tiwin, thflags, nxt_pkt));
		} else if (tp->t_flags & TF_ACKNOW) {
			rack_do_dropafterack(m, tp, th, thflags, tlen, &ret_val);
			return (ret_val);
		} else {
			rack_do_drop(m, NULL);
			return (0);
		}
	}
	/*
	 * Ack processing.
	 */
	if (rack_process_ack(m, th, so, tp, to, tiwin, tlen, &ourfinisacked, thflags, &ret_val)) {
		return (ret_val);
	}
	if (sbavail(&so->so_snd)) {
		if (rack_progress_timeout_check(tp)) {
			tcp_set_inp_to_drop(tp->t_inpcb, ETIMEDOUT);
			rack_do_dropwithreset(m, tp, th, BANDLIM_RST_OPENPORT, tlen);
			return (1);
		}
	}
	return (rack_process_data(m, th, so, tp, drop_hdrlen, tlen,
	    tiwin, thflags, nxt_pkt));
}


static void inline
rack_clear_rate_sample(struct tcp_rack *rack)
{
	rack->r_ctl.rack_rs.rs_flags = RACK_RTT_EMPTY;
	rack->r_ctl.rack_rs.rs_rtt_cnt = 0;
	rack->r_ctl.rack_rs.rs_rtt_tot = 0;
}

static int
rack_init(struct tcpcb *tp)
{
	struct tcp_rack *rack = NULL;

	tp->t_fb_ptr = uma_zalloc(rack_pcb_zone, M_NOWAIT);
	if (tp->t_fb_ptr == NULL) {
		/*
		 * We need to allocate memory but cant. The INP and INP_INFO
		 * locks and they are recusive (happens during setup. So a
		 * scheme to drop the locks fails :(
		 *
		 */
		return (ENOMEM);
	}
	memset(tp->t_fb_ptr, 0, sizeof(struct tcp_rack));

	rack = (struct tcp_rack *)tp->t_fb_ptr;
	TAILQ_INIT(&rack->r_ctl.rc_map);
	TAILQ_INIT(&rack->r_ctl.rc_free);
	TAILQ_INIT(&rack->r_ctl.rc_tmap);
	rack->rc_tp = tp;
	if (tp->t_inpcb) {
		rack->rc_inp = tp->t_inpcb;
	}
	/* Probably not needed but lets be sure */
	rack_clear_rate_sample(rack);
	rack->r_cpu = 0;
	rack->r_ctl.rc_reorder_fade = rack_reorder_fade;
	rack->rc_allow_data_af_clo = rack_ignore_data_after_close;
	rack->r_ctl.rc_tlp_threshold = rack_tlp_thresh;
	rack->rc_pace_reduce = rack_slot_reduction;
	if (V_tcp_delack_enabled)
		tp->t_delayed_ack = 1;
	else
		tp->t_delayed_ack = 0;
	rack->rc_pace_max_segs = rack_hptsi_segments;
	rack->r_ctl.rc_early_recovery_segs = rack_early_recovery_max_seg;
	rack->r_ctl.rc_reorder_shift = rack_reorder_thresh;
	rack->r_ctl.rc_pkt_delay = rack_pkt_delay;
	rack->r_ctl.rc_prop_reduce = rack_use_proportional_reduce;
	rack->r_idle_reduce_largest  = rack_reduce_largest_on_idle;
	rack->r_enforce_min_pace = rack_min_pace_time;
	rack->r_min_pace_seg_thresh = rack_min_pace_time_seg_req;
	rack->r_ctl.rc_prop_rate = rack_proportional_rate;
	rack->r_ctl.rc_tlp_cwnd_reduce = rack_lower_cwnd_at_tlp;
	rack->r_ctl.rc_early_recovery = rack_early_recovery;
	rack->rc_always_pace = rack_pace_every_seg;
	rack->r_ctl.rc_rate_sample_method = rack_rate_sample_method;
	rack->rack_tlp_threshold_use = rack_tlp_threshold_use;
	rack->r_ctl.rc_prr_sendalot = rack_send_a_lot_in_prr;
	rack->r_ctl.rc_min_to = rack_min_to;
	rack->r_ctl.rc_prr_inc_var = rack_inc_var;
	rack_start_hpts_timer(rack, tp, tcp_ts_getticks(), __LINE__, 0, 0, 0);
	if (tp->snd_una != tp->snd_max) {
		/* Create a send map for the current outstanding data */
		struct rack_sendmap *rsm;

		rsm = rack_alloc(rack);
		if (rsm == NULL) {
			uma_zfree(rack_pcb_zone, tp->t_fb_ptr);
			tp->t_fb_ptr = NULL;
			return (ENOMEM);
		}
		rsm->r_flags = RACK_OVERMAX;
		rsm->r_tim_lastsent[0] = tcp_ts_getticks();
		rsm->r_rtr_cnt = 1;
		rsm->r_rtr_bytes = 0;
		rsm->r_start = tp->snd_una;
		rsm->r_end = tp->snd_max;
		rsm->r_sndcnt = 0;
		TAILQ_INSERT_TAIL(&rack->r_ctl.rc_map, rsm, r_next);
		TAILQ_INSERT_TAIL(&rack->r_ctl.rc_tmap, rsm, r_tnext);
		rsm->r_in_tmap = 1;
	}
	return (0);
}

static int
rack_handoff_ok(struct tcpcb *tp)
{
	if ((tp->t_state == TCPS_CLOSED) ||
	    (tp->t_state == TCPS_LISTEN)) {
		/* Sure no problem though it may not stick */
		return (0);
	}
	if ((tp->t_state == TCPS_SYN_SENT) ||
	    (tp->t_state == TCPS_SYN_RECEIVED)) {
		/*
		 * We really don't know you have to get to ESTAB or beyond
		 * to tell.
		 */
		return (EAGAIN);
	}
	if (tp->t_flags & TF_SACK_PERMIT) {
		return (0);
	}
	/*
	 * If we reach here we don't do SACK on this connection so we can
	 * never do rack.
	 */
	return (EINVAL);
}

static void
rack_fini(struct tcpcb *tp, int32_t tcb_is_purged)
{
	if (tp->t_fb_ptr) {
		struct tcp_rack *rack;
		struct rack_sendmap *rsm;

		rack = (struct tcp_rack *)tp->t_fb_ptr;
#ifdef TCP_BLACKBOX
		tcp_log_flowend(tp);
#endif
		rsm = TAILQ_FIRST(&rack->r_ctl.rc_map);
		while (rsm) {
			TAILQ_REMOVE(&rack->r_ctl.rc_map, rsm, r_next);
			uma_zfree(rack_zone, rsm);
			rsm = TAILQ_FIRST(&rack->r_ctl.rc_map);
		}
		rsm = TAILQ_FIRST(&rack->r_ctl.rc_free);
		while (rsm) {
			TAILQ_REMOVE(&rack->r_ctl.rc_free, rsm, r_next);
			uma_zfree(rack_zone, rsm);
			rsm = TAILQ_FIRST(&rack->r_ctl.rc_free);
		}
		rack->rc_free_cnt = 0;
		uma_zfree(rack_pcb_zone, tp->t_fb_ptr);
		tp->t_fb_ptr = NULL;
	}
}

static void
rack_set_state(struct tcpcb *tp, struct tcp_rack *rack)
{
	switch (tp->t_state) {
	case TCPS_SYN_SENT:
		rack->r_state = TCPS_SYN_SENT;
		rack->r_substate = rack_do_syn_sent;
		break;
	case TCPS_SYN_RECEIVED:
		rack->r_state = TCPS_SYN_RECEIVED;
		rack->r_substate = rack_do_syn_recv;
		break;
	case TCPS_ESTABLISHED:
		rack->r_state = TCPS_ESTABLISHED;
		rack->r_substate = rack_do_established;
		break;
	case TCPS_CLOSE_WAIT:
		rack->r_state = TCPS_CLOSE_WAIT;
		rack->r_substate = rack_do_close_wait;
		break;
	case TCPS_FIN_WAIT_1:
		rack->r_state = TCPS_FIN_WAIT_1;
		rack->r_substate = rack_do_fin_wait_1;
		break;
	case TCPS_CLOSING:
		rack->r_state = TCPS_CLOSING;
		rack->r_substate = rack_do_closing;
		break;
	case TCPS_LAST_ACK:
		rack->r_state = TCPS_LAST_ACK;
		rack->r_substate = rack_do_lastack;
		break;
	case TCPS_FIN_WAIT_2:
		rack->r_state = TCPS_FIN_WAIT_2;
		rack->r_substate = rack_do_fin_wait_2;
		break;
	case TCPS_LISTEN:
	case TCPS_CLOSED:
	case TCPS_TIME_WAIT:
	default:
#ifdef INVARIANTS
		panic("tcp tp:%p state:%d sees impossible state?", tp, tp->t_state);
#endif
		break;
	};
}


static void
rack_timer_audit(struct tcpcb *tp, struct tcp_rack *rack, struct sockbuf *sb)
{
	/*
	 * We received an ack, and then did not
	 * call send or were bounced out due to the
	 * hpts was running. Now a timer is up as well, is
	 * it the right timer?
	 */
	struct rack_sendmap *rsm;
	int tmr_up;
	
	tmr_up = rack->r_ctl.rc_hpts_flags & PACE_TMR_MASK;
	if (rack->rc_in_persist && (tmr_up == PACE_TMR_PERSIT))
		return;
	rsm = TAILQ_FIRST(&rack->r_ctl.rc_tmap);
	if (((rsm == NULL) || (tp->t_state < TCPS_ESTABLISHED)) &&
	    (tmr_up == PACE_TMR_RXT)) {
		/* Should be an RXT */
		return;
	}
	if (rsm == NULL) {
		/* Nothing outstanding? */
		if (tp->t_flags & TF_DELACK) {
			if (tmr_up == PACE_TMR_DELACK)
				/* We are supposed to have delayed ack up and we do */
				return;
		} else if (sbavail(&tp->t_inpcb->inp_socket->so_snd) && (tmr_up == PACE_TMR_RXT)) {
			/* 
			 * if we hit enobufs then we would expect the possiblity
			 * of nothing outstanding and the RXT up (and the hptsi timer).
			 */
			return;
		} else if (((tcp_always_keepalive ||
			     rack->rc_inp->inp_socket->so_options & SO_KEEPALIVE) &&
			    (tp->t_state <= TCPS_CLOSING)) &&
			   (tmr_up == PACE_TMR_KEEP) &&
			   (tp->snd_max == tp->snd_una)) {
			/* We should have keep alive up and we do */
			return;
		}
	}
	if (rsm && (rsm->r_flags & RACK_SACK_PASSED)) {
		if ((tp->t_flags & TF_SENTFIN) &&
		    ((tp->snd_max - tp->snd_una) == 1) &&
		    (rsm->r_flags & RACK_HAS_FIN)) {
			/* needs to be a RXT */
			if (tmr_up == PACE_TMR_RXT)
				return;
		} else if (tmr_up == PACE_TMR_RACK)
			return;
	} else if (SEQ_GT(tp->snd_max,tp->snd_una) &&
		   ((tmr_up == PACE_TMR_TLP) ||
		    (tmr_up == PACE_TMR_RXT))) {
		/* 
		 * Either a TLP or RXT is fine if no sack-passed 
		 * is in place and data is outstanding.
		 */
		return;
	} else if (tmr_up == PACE_TMR_DELACK) {
		/*
		 * If the delayed ack was going to go off
		 * before the rtx/tlp/rack timer were going to
		 * expire, then that would be the timer in control.
		 * Note we don't check the time here trusting the
		 * code is correct.
		 */
		return;
	}
	/* 
	 * Ok the timer originally started is not what we want now.
	 * We will force the hpts to be stopped if any, and restart
	 * with the slot set to what was in the saved slot.
	 */
	rack_timer_cancel(tp, rack, rack->r_ctl.rc_rcvtime, __LINE__);
	rack_start_hpts_timer(rack, tp, tcp_ts_getticks(), __LINE__, 0, 0, 0);
}

static void
rack_hpts_do_segment(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, int32_t drop_hdrlen, int32_t tlen, uint8_t iptos,
    int32_t nxt_pkt, struct timeval *tv)
{
	int32_t thflags, retval, did_out = 0;
	int32_t way_out = 0;
	uint32_t cts;
	uint32_t tiwin;
	struct tcpopt to;
	struct tcp_rack *rack;
	struct rack_sendmap *rsm;
	int32_t prev_state = 0;

	cts = tcp_tv_to_mssectick(tv);
	rack = (struct tcp_rack *)tp->t_fb_ptr;

	kern_prefetch(rack, &prev_state);
	prev_state = 0;
	thflags = th->th_flags;
	/*
	 * If this is either a state-changing packet or current state isn't
	 * established, we require a read lock on tcbinfo.  Otherwise, we
	 * allow the tcbinfo to be in either locked or unlocked, as the
	 * caller may have unnecessarily acquired a lock due to a race.
	 */
	if ((thflags & (TH_SYN | TH_FIN | TH_RST)) != 0 ||
	    tp->t_state != TCPS_ESTABLISHED) {
		INP_INFO_RLOCK_ASSERT(&V_tcbinfo);
	}
	INP_WLOCK_ASSERT(tp->t_inpcb);
	KASSERT(tp->t_state > TCPS_LISTEN, ("%s: TCPS_LISTEN",
	    __func__));
	KASSERT(tp->t_state != TCPS_TIME_WAIT, ("%s: TCPS_TIME_WAIT",
	    __func__));
	{
		union tcp_log_stackspecific log;

		memset(&log.u_bbr, 0, sizeof(log.u_bbr));
		log.u_bbr.inhpts = rack->rc_inp->inp_in_hpts;
		log.u_bbr.ininput = rack->rc_inp->inp_in_input;
		TCP_LOG_EVENT(tp, th, &so->so_rcv, &so->so_snd, TCP_LOG_IN, 0,
		    tlen, &log, true);
	}
	if ((thflags & TH_SYN) && (thflags & TH_FIN) && V_drop_synfin) {
		way_out = 4;
		goto done_with_input;
	}
	/*
	 * If a segment with the ACK-bit set arrives in the SYN-SENT state
	 * check SEQ.ACK first as described on page 66 of RFC 793, section 3.9.
	 */
	if ((tp->t_state == TCPS_SYN_SENT) && (thflags & TH_ACK) &&
	    (SEQ_LEQ(th->th_ack, tp->iss) || SEQ_GT(th->th_ack, tp->snd_max))) {
		rack_do_dropwithreset(m, tp, th, BANDLIM_RST_OPENPORT, tlen);
		return;
	}
	/*
	 * Segment received on connection. Reset idle time and keep-alive
	 * timer. XXX: This should be done after segment validation to
	 * ignore broken/spoofed segs.
	 */
	if  (tp->t_idle_reduce && (tp->snd_max == tp->snd_una)) {
#ifdef NETFLIX_CWV
		if ((tp->cwv_enabled) &&
		    ((tp->cwv_cwnd_valid == 0) &&
		     TCPS_HAVEESTABLISHED(tp->t_state) &&
		     (tp->snd_cwnd > tp->snd_cwv.init_cwnd))) {
			tcp_newcwv_nvp_closedown(tp);
		} else 
#endif
		       if ((ticks - tp->t_rcvtime) >= tp->t_rxtcur) {
			counter_u64_add(rack_input_idle_reduces, 1);
			rack_cc_after_idle(tp,
			    (rack->r_idle_reduce_largest ? 1 :0));
		}
	}
	rack->r_ctl.rc_rcvtime = cts;
	tp->t_rcvtime = ticks;

#ifdef NETFLIX_CWV
	if (tp->cwv_enabled) {
		if ((tp->cwv_cwnd_valid == 0) &&
		    TCPS_HAVEESTABLISHED(tp->t_state) &&
		    (tp->snd_cwnd > tp->snd_cwv.init_cwnd))
			tcp_newcwv_nvp_closedown(tp);
	}
#endif
	/*
	 * Unscale the window into a 32-bit value. For the SYN_SENT state
	 * the scale is zero.
	 */
	tiwin = th->th_win << tp->snd_scale;
#ifdef NETFLIX_STATS
	stats_voi_update_abs_ulong(tp->t_stats, VOI_TCP_FRWIN, tiwin);
#endif
	/*
	 * TCP ECN processing. XXXJTL: If we ever use ECN, we need to move
	 * this to occur after we've validated the segment.
	 */
	if (tp->t_flags & TF_ECN_PERMIT) {
		if (thflags & TH_CWR)
			tp->t_flags &= ~TF_ECN_SND_ECE;
		switch (iptos & IPTOS_ECN_MASK) {
		case IPTOS_ECN_CE:
			tp->t_flags |= TF_ECN_SND_ECE;
			TCPSTAT_INC(tcps_ecn_ce);
			break;
		case IPTOS_ECN_ECT0:
			TCPSTAT_INC(tcps_ecn_ect0);
			break;
		case IPTOS_ECN_ECT1:
			TCPSTAT_INC(tcps_ecn_ect1);
			break;
		}
		/* Congestion experienced. */
		if (thflags & TH_ECE) {
			rack_cong_signal(tp, th, CC_ECN);
		}
	}
	/*
	 * Parse options on any incoming segment.
	 */
	tcp_dooptions(&to, (u_char *)(th + 1),
	    (th->th_off << 2) - sizeof(struct tcphdr),
	    (thflags & TH_SYN) ? TO_SYN : 0);

	/*
	 * If echoed timestamp is later than the current time, fall back to
	 * non RFC1323 RTT calculation.  Normalize timestamp if syncookies
	 * were used when this connection was established.
	 */
	if ((to.to_flags & TOF_TS) && (to.to_tsecr != 0)) {
		to.to_tsecr -= tp->ts_offset;
		if (TSTMP_GT(to.to_tsecr, cts))
			to.to_tsecr = 0;
	}
	/*
	 * If its the first time in we need to take care of options and
	 * verify we can do SACK for rack!
	 */
	if (rack->r_state == 0) {
		/* Should be init'd by rack_init() */
		KASSERT(rack->rc_inp != NULL,
		    ("%s: rack->rc_inp unexpectedly NULL", __func__));
		if (rack->rc_inp == NULL) {
			rack->rc_inp = tp->t_inpcb;
		}

		/*
		 * Process options only when we get SYN/ACK back. The SYN
		 * case for incoming connections is handled in tcp_syncache.
		 * According to RFC1323 the window field in a SYN (i.e., a
		 * <SYN> or <SYN,ACK>) segment itself is never scaled. XXX
		 * this is traditional behavior, may need to be cleaned up.
		 */
		rack->r_cpu = inp_to_cpuid(tp->t_inpcb);
		if (tp->t_state == TCPS_SYN_SENT && (thflags & TH_SYN)) {
			if ((to.to_flags & TOF_SCALE) &&
			    (tp->t_flags & TF_REQ_SCALE)) {
				tp->t_flags |= TF_RCVD_SCALE;
				tp->snd_scale = to.to_wscale;
			}
			/*
			 * Initial send window.  It will be updated with the
			 * next incoming segment to the scaled value.
			 */
			tp->snd_wnd = th->th_win;
			if (to.to_flags & TOF_TS) {
				tp->t_flags |= TF_RCVD_TSTMP;
				tp->ts_recent = to.to_tsval;
				tp->ts_recent_age = cts;
			}
			if (to.to_flags & TOF_MSS)
				tcp_mss(tp, to.to_mss);
			if ((tp->t_flags & TF_SACK_PERMIT) &&
			    (to.to_flags & TOF_SACKPERM) == 0)
				tp->t_flags &= ~TF_SACK_PERMIT;
			if (IS_FASTOPEN(tp->t_flags)) {
				if (to.to_flags & TOF_FASTOPEN) {
					uint16_t mss;

					if (to.to_flags & TOF_MSS)
						mss = to.to_mss;
					else
						if ((tp->t_inpcb->inp_vflag & INP_IPV6) != 0)
							mss = TCP6_MSS;
						else
							mss = TCP_MSS;
					tcp_fastopen_update_cache(tp, mss,
					    to.to_tfo_len, to.to_tfo_cookie);
				} else
					tcp_fastopen_disable_path(tp);
			}
		}
		/*
		 * At this point we are at the initial call. Here we decide
		 * if we are doing RACK or not. We do this by seeing if
		 * TF_SACK_PERMIT is set, if not rack is *not* possible and
		 * we switch to the default code.
		 */
		if ((tp->t_flags & TF_SACK_PERMIT) == 0) {
			tcp_switch_back_to_default(tp);
			(*tp->t_fb->tfb_tcp_do_segment) (m, th, so, tp, drop_hdrlen,
			    tlen, iptos);
			return;
		}
		/* Set the flag */
		rack->r_is_v6 = (tp->t_inpcb->inp_vflag & INP_IPV6) != 0;
		tcp_set_hpts(tp->t_inpcb);
		rack_stop_all_timers(tp);
		sack_filter_clear(&rack->r_ctl.rack_sf, th->th_ack);
	}
	/*
	 * This is the one exception case where we set the rack state
	 * always. All other times (timers etc) we must have a rack-state
	 * set (so we assure we have done the checks above for SACK).
	 */
	if (rack->r_state != tp->t_state)
		rack_set_state(tp, rack);
	if (SEQ_GT(th->th_ack, tp->snd_una) && (rsm = TAILQ_FIRST(&rack->r_ctl.rc_map)) != NULL)
		kern_prefetch(rsm, &prev_state);
	prev_state = rack->r_state;
	rack->r_ctl.rc_tlp_send_cnt = 0;
	rack_clear_rate_sample(rack);
	retval = (*rack->r_substate) (m, th, so,
	    tp, &to, drop_hdrlen,
	    tlen, tiwin, thflags, nxt_pkt);
#ifdef INVARIANTS
	if ((retval == 0) &&
	    (tp->t_inpcb == NULL)) {
		panic("retval:%d tp:%p t_inpcb:NULL state:%d",
		    retval, tp, prev_state);
	}
#endif
	if (retval == 0) {
		/*
		 * If retval is 1 the tcb is unlocked and most likely the tp
		 * is gone.
		 */
		INP_WLOCK_ASSERT(tp->t_inpcb);
		tcp_rack_xmit_timer_commit(rack, tp);
		if (((tp->snd_max - tp->snd_una) > tp->snd_wnd) &&
		    (rack->rc_in_persist == 0)){
			/* 
			 * The peer shrunk its window on us to the point
			 * where we have sent too much. The only thing
			 * we can do here is stop any timers and
			 * enter persist. We most likely lost the last
			 * bytes we sent but oh well, we will have to
			 * retransmit them after the peer is caught up.
			 */
			if (rack->rc_inp->inp_in_hpts)
				tcp_hpts_remove(rack->rc_inp, HPTS_REMOVE_OUTPUT);
			rack_timer_cancel(tp, rack, cts, __LINE__);
			rack_enter_persist(tp, rack, cts);
			rack_start_hpts_timer(rack, tp, tcp_ts_getticks(), __LINE__, 0, 0, 0);
			way_out = 3;
			goto done_with_input;
		}
		if (nxt_pkt == 0) {
			if (rack->r_wanted_output != 0) {
				did_out = 1;
				(void)tp->t_fb->tfb_tcp_output(tp);
			}
			rack_start_hpts_timer(rack, tp, cts, __LINE__, 0, 0, 0);
		}
		if (((rack->r_ctl.rc_hpts_flags & PACE_TMR_MASK) == 0) &&
		    (SEQ_GT(tp->snd_max, tp->snd_una) ||
		     (tp->t_flags & TF_DELACK) ||
		     ((tcp_always_keepalive || rack->rc_inp->inp_socket->so_options & SO_KEEPALIVE) &&
		      (tp->t_state <= TCPS_CLOSING)))) {
			/* We could not send (probably in the hpts but stopped the timer earlier)? */
			if ((tp->snd_max == tp->snd_una) &&
			    ((tp->t_flags & TF_DELACK) == 0) &&
			    (rack->r_ctl.rc_hpts_flags & PACE_PKT_OUTPUT)) {
				/* keep alive not needed if we are hptsi output yet */
				;
			} else {
				if (rack->rc_inp->inp_in_hpts)
					tcp_hpts_remove(rack->rc_inp, HPTS_REMOVE_OUTPUT);
				rack_start_hpts_timer(rack, tp, tcp_ts_getticks(), __LINE__, 0, 0, 0);
			}
			way_out = 1;
		} else {
			/* Do we have the correct timer running? */
			rack_timer_audit(tp, rack, &so->so_snd);
			way_out = 2;
		}
	done_with_input:
		rack_log_doseg_done(rack, cts, nxt_pkt, did_out, way_out);
		if (did_out)
			rack->r_wanted_output = 0;
#ifdef INVARIANTS
		if (tp->t_inpcb == NULL) {
			panic("OP:%d retval:%d tp:%p t_inpcb:NULL state:%d",
			      did_out,
			      retval, tp, prev_state);
		}
#endif
		INP_WUNLOCK(tp->t_inpcb);
	}
}

void
rack_do_segment(struct mbuf *m, struct tcphdr *th, struct socket *so,
    struct tcpcb *tp, int32_t drop_hdrlen, int32_t tlen, uint8_t iptos)
{
	struct timeval tv;
#ifdef RSS
	struct tcp_function_block *tfb;
	struct tcp_rack *rack;
	struct epoch_tracker et;

	rack = (struct tcp_rack *)tp->t_fb_ptr;
	if (rack->r_state == 0) {
		/*
		 * Initial input (ACK to SYN-ACK etc)lets go ahead and get
		 * it processed
		 */
		INP_INFO_RLOCK_ET(&V_tcbinfo, et);
		tcp_get_usecs(&tv);
		rack_hpts_do_segment(m, th, so, tp, drop_hdrlen,
		    tlen, iptos, 0, &tv);
		INP_INFO_RUNLOCK_ET(&V_tcbinfo, et);
		return;
	}
	tcp_queue_to_input(tp, m, th, tlen, drop_hdrlen, iptos);
	INP_WUNLOCK(tp->t_inpcb);
#else
	tcp_get_usecs(&tv);
	rack_hpts_do_segment(m, th, so, tp, drop_hdrlen,
	    tlen, iptos, 0, &tv);
#endif
}

struct rack_sendmap *
tcp_rack_output(struct tcpcb *tp, struct tcp_rack *rack, uint32_t tsused)
{
	struct rack_sendmap *rsm = NULL;
	int32_t idx;
	uint32_t srtt_cur, srtt = 0, thresh = 0, ts_low = 0;

	/* Return the next guy to be re-transmitted */
	if (TAILQ_EMPTY(&rack->r_ctl.rc_map)) {
		return (NULL);
	}
	if (tp->t_flags & TF_SENTFIN) {
		/* retran the end FIN? */
		return (NULL);
	}
	/* ok lets look at this one */
	rsm = TAILQ_FIRST(&rack->r_ctl.rc_tmap);
	if (rsm && ((rsm->r_flags & RACK_ACKED) == 0)) {
		goto check_it;
	}
	rsm = rack_find_lowest_rsm(rack);
	if (rsm == NULL) {
		return (NULL);
	}
check_it:
	srtt_cur = tp->t_srtt >> TCP_RTT_SHIFT;
	srtt = TICKS_2_MSEC(srtt_cur);
	if (rack->rc_rack_rtt && (srtt > rack->rc_rack_rtt))
		srtt = rack->rc_rack_rtt;
	if (rsm->r_flags & RACK_ACKED) {
		return (NULL);
	}
	if ((rsm->r_flags & RACK_SACK_PASSED) == 0) {
		/* Its not yet ready */
		return (NULL);
	}
	idx = rsm->r_rtr_cnt - 1;
	ts_low = rsm->r_tim_lastsent[idx];
	thresh = rack_calc_thresh_rack(rack, srtt, tsused);
	if (tsused <= ts_low) {
		return (NULL);
	}
	if ((tsused - ts_low) >= thresh) {
		return (rsm);
	}
	return (NULL);
}

static int
rack_output(struct tcpcb *tp)
{
	struct socket *so;
	uint32_t recwin, sendwin;
	uint32_t sb_offset;
	int32_t len, flags, error = 0;
	struct mbuf *m;
	struct mbuf *mb;
	uint32_t if_hw_tsomaxsegcount = 0;
	uint32_t if_hw_tsomaxsegsize;
	long tot_len_this_send = 0;
	struct ip *ip = NULL;
#ifdef TCPDEBUG
	struct ipovly *ipov = NULL;
#endif
	struct udphdr *udp = NULL;
	struct tcp_rack *rack;
	struct tcphdr *th;
	uint8_t pass = 0;
	uint8_t wanted_cookie = 0;
	u_char opt[TCP_MAXOLEN];
	unsigned ipoptlen, optlen, hdrlen, ulen=0;
	uint32_t rack_seq;

#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	unsigned ipsec_optlen = 0;

#endif
	int32_t idle, sendalot;
	int32_t sub_from_prr = 0;
	volatile int32_t sack_rxmit;
	struct rack_sendmap *rsm = NULL;
	int32_t tso, mtu, would_have_fin = 0;
	struct tcpopt to;
	int32_t slot = 0;
	uint32_t cts;
	uint8_t hpts_calling, doing_tlp = 0;
	int32_t do_a_prefetch;
	int32_t prefetch_rsm = 0;
	int32_t prefetch_so_done = 0;
	struct tcp_log_buffer *lgb = NULL;
	struct inpcb *inp;
	struct sockbuf *sb;
#ifdef INET6
	struct ip6_hdr *ip6 = NULL;
	int32_t isipv6;
#endif
	/* setup and take the cache hits here */
	rack = (struct tcp_rack *)tp->t_fb_ptr;
	inp = rack->rc_inp;
	so = inp->inp_socket;
	sb = &so->so_snd;
	kern_prefetch(sb, &do_a_prefetch);
	do_a_prefetch = 1;
	
	INP_WLOCK_ASSERT(inp);
#ifdef TCP_OFFLOAD
	if (tp->t_flags & TF_TOE)
		return (tcp_offload_output(tp));
#endif
#ifdef INET6
	if (rack->r_state) {
		/* Use the cache line loaded if possible */
		isipv6 = rack->r_is_v6;
	} else {
		isipv6 = (inp->inp_vflag & INP_IPV6) != 0;
	}
#endif
	cts = tcp_ts_getticks();
	if (((rack->r_ctl.rc_hpts_flags & PACE_PKT_OUTPUT) == 0) &&
	    inp->inp_in_hpts) {
		/*
		 * We are on the hpts for some timer but not hptsi output.
		 * Remove from the hpts unconditionally.
		 */
		rack_timer_cancel(tp, rack, cts, __LINE__);
	}
	/* Mark that we have called rack_output(). */
	if ((rack->r_timer_override) ||
	    (tp->t_flags & TF_FORCEDATA) ||
	    (tp->t_state < TCPS_ESTABLISHED)) {
		if (tp->t_inpcb->inp_in_hpts)
			tcp_hpts_remove(tp->t_inpcb, HPTS_REMOVE_OUTPUT);
	} else if (tp->t_inpcb->inp_in_hpts) {
		/*
		 * On the hpts you can't pass even if ACKNOW is on, we will
		 * when the hpts fires.
		 */
		counter_u64_add(rack_out_size[TCP_MSS_ACCT_INPACE], 1);
		return (0);
	}
	hpts_calling = inp->inp_hpts_calls;
	inp->inp_hpts_calls = 0;
	if (rack->r_ctl.rc_hpts_flags & PACE_TMR_MASK) {
		if (rack_process_timers(tp, rack, cts, hpts_calling)) {
			counter_u64_add(rack_out_size[TCP_MSS_ACCT_ATIMER], 1);
			return (0);
		}
	}
	rack->r_wanted_output = 0;
	rack->r_timer_override = 0;
	/*
	 * For TFO connections in SYN_SENT or SYN_RECEIVED,
	 * only allow the initial SYN or SYN|ACK and those sent
	 * by the retransmit timer.
	 */
	if (IS_FASTOPEN(tp->t_flags) &&
	    ((tp->t_state == TCPS_SYN_RECEIVED) ||
	     (tp->t_state == TCPS_SYN_SENT)) &&
	    SEQ_GT(tp->snd_max, tp->snd_una) && /* initial SYN or SYN|ACK sent */
	    (tp->t_rxtshift == 0))              /* not a retransmit */
		return (0);
	/*
	 * Determine length of data that should be transmitted, and flags
	 * that will be used. If there is some data or critical controls
	 * (SYN, RST) to send, then transmit; otherwise, investigate
	 * further.
	 */
	idle = (tp->t_flags & TF_LASTIDLE) || (tp->snd_max == tp->snd_una);
#ifdef NETFLIX_CWV
	if (tp->cwv_enabled) {
		if ((tp->cwv_cwnd_valid == 0) &&
		    TCPS_HAVEESTABLISHED(tp->t_state) &&
		    (tp->snd_cwnd > tp->snd_cwv.init_cwnd))
			tcp_newcwv_nvp_closedown(tp);
	} else
#endif
	if (tp->t_idle_reduce) {
		if (idle && ((ticks - tp->t_rcvtime) >= tp->t_rxtcur))
			rack_cc_after_idle(tp,
		            (rack->r_idle_reduce_largest ? 1 :0));
	}
	tp->t_flags &= ~TF_LASTIDLE;
	if (idle) {
		if (tp->t_flags & TF_MORETOCOME) {
			tp->t_flags |= TF_LASTIDLE;
			idle = 0;
		}
	}
again:
	/*
	 * If we've recently taken a timeout, snd_max will be greater than
	 * snd_nxt.  There may be SACK information that allows us to avoid
	 * resending already delivered data.  Adjust snd_nxt accordingly.
	 */
	sendalot = 0;
	cts = tcp_ts_getticks();
	tso = 0;
	mtu = 0;
	sb_offset = tp->snd_max - tp->snd_una;
	sendwin = min(tp->snd_wnd, tp->snd_cwnd);

	flags = tcp_outflags[tp->t_state];
	/*
	 * Send any SACK-generated retransmissions.  If we're explicitly
	 * trying to send out new data (when sendalot is 1), bypass this
	 * function. If we retransmit in fast recovery mode, decrement
	 * snd_cwnd, since we're replacing a (future) new transmission with
	 * a retransmission now, and we previously incremented snd_cwnd in
	 * tcp_input().
	 */
	/*
	 * Still in sack recovery , reset rxmit flag to zero.
	 */
	while (rack->rc_free_cnt < rack_free_cache) {
		rsm = rack_alloc(rack);
		if (rsm == NULL) {
			if (inp->inp_hpts_calls)
				/* Retry in a ms */
				slot = 1;
			goto just_return_nolock;
		}
		TAILQ_INSERT_TAIL(&rack->r_ctl.rc_free, rsm, r_next);
		rack->rc_free_cnt++;
		rsm = NULL;
	}
	if (inp->inp_hpts_calls)
		inp->inp_hpts_calls = 0;
	sack_rxmit = 0;
	len = 0;
	rsm = NULL;
	if (flags & TH_RST) {
		SOCKBUF_LOCK(sb);
		goto send;
	}
	if (rack->r_ctl.rc_tlpsend) {
		/* Tail loss probe */
		long cwin;
		long tlen;

		doing_tlp = 1;
		rsm = rack->r_ctl.rc_tlpsend;
		rack->r_ctl.rc_tlpsend = NULL;
		sack_rxmit = 1;
		tlen = rsm->r_end - rsm->r_start;
		if (tlen > tp->t_maxseg)
			tlen = tp->t_maxseg;
		KASSERT(SEQ_LEQ(tp->snd_una, rsm->r_start),
		    ("%s:%d: r.start:%u < SND.UNA:%u; tp:%p, rack:%p, rsm:%p",
		    __func__, __LINE__,
		    rsm->r_start, tp->snd_una, tp, rack, rsm));
		sb_offset = rsm->r_start - tp->snd_una;
		cwin = min(tp->snd_wnd, tlen);
		len = cwin;
	} else if (rack->r_ctl.rc_resend) {
		/* Retransmit timer */
		rsm = rack->r_ctl.rc_resend;
		rack->r_ctl.rc_resend = NULL;
		len = rsm->r_end - rsm->r_start;
		sack_rxmit = 1;
		sendalot = 0;
		KASSERT(SEQ_LEQ(tp->snd_una, rsm->r_start),
		    ("%s:%d: r.start:%u < SND.UNA:%u; tp:%p, rack:%p, rsm:%p",
		    __func__, __LINE__,
		    rsm->r_start, tp->snd_una, tp, rack, rsm));
		sb_offset = rsm->r_start - tp->snd_una;
		if (len >= tp->t_maxseg) {
			len = tp->t_maxseg;
		}
	} else if ((rack->rc_in_persist == 0) &&
	    ((rsm = tcp_rack_output(tp, rack, cts)) != NULL)) {
		long tlen;

		if ((!IN_RECOVERY(tp->t_flags)) &&
		    ((tp->t_flags & (TF_WASFRECOVERY | TF_WASCRECOVERY)) == 0)) {
			/* Enter recovery if not induced by a time-out */
			rack->r_ctl.rc_rsm_start = rsm->r_start;
			rack->r_ctl.rc_cwnd_at = tp->snd_cwnd;
			rack->r_ctl.rc_ssthresh_at = tp->snd_ssthresh;
			rack_cong_signal(tp, NULL, CC_NDUPACK);
			/*
			 * When we enter recovery we need to assure we send
			 * one packet.
			 */
			rack->r_ctl.rc_prr_sndcnt = tp->t_maxseg;
		}
#ifdef INVARIANTS
		if (SEQ_LT(rsm->r_start, tp->snd_una)) {
			panic("Huh, tp:%p rack:%p rsm:%p start:%u < snd_una:%u\n",
			    tp, rack, rsm, rsm->r_start, tp->snd_una);
		}
#endif
		tlen = rsm->r_end - rsm->r_start;
		KASSERT(SEQ_LEQ(tp->snd_una, rsm->r_start),
		    ("%s:%d: r.start:%u < SND.UNA:%u; tp:%p, rack:%p, rsm:%p",
		    __func__, __LINE__,
		    rsm->r_start, tp->snd_una, tp, rack, rsm));
		sb_offset = rsm->r_start - tp->snd_una;
		if (tlen > rack->r_ctl.rc_prr_sndcnt) {
			len = rack->r_ctl.rc_prr_sndcnt;
		} else {
			len = tlen;
		}
		if (len >= tp->t_maxseg) {
			sendalot = 1;
			len = tp->t_maxseg;
		} else {
			sendalot = 0;
			if ((rack->rc_timer_up == 0) &&
			    (len < tlen)) {
				/*
				 * If its not a timer don't send a partial
				 * segment.
				 */
				len = 0;
				goto just_return_nolock;
			}
		}
		if (len > 0) {
			sub_from_prr = 1;
			sack_rxmit = 1;
			TCPSTAT_INC(tcps_sack_rexmits);
			TCPSTAT_ADD(tcps_sack_rexmit_bytes,
			    min(len, tp->t_maxseg));
			counter_u64_add(rack_rtm_prr_retran, 1);
		}
	}
	if (rsm && (rsm->r_flags & RACK_HAS_FIN)) {
		/* we are retransmitting the fin */
		len--;
		if (len) {
			/*
			 * When retransmitting data do *not* include the
			 * FIN. This could happen from a TLP probe.
			 */
			flags &= ~TH_FIN;
		}
	}
#ifdef INVARIANTS
	/* For debugging */
	rack->r_ctl.rc_rsm_at_retran = rsm;
#endif
	/*
	 * Get standard flags, and add SYN or FIN if requested by 'hidden'
	 * state flags.
	 */
	if (tp->t_flags & TF_NEEDFIN)
		flags |= TH_FIN;
	if (tp->t_flags & TF_NEEDSYN)
		flags |= TH_SYN;
	if ((sack_rxmit == 0) && (prefetch_rsm == 0)) {
		void *end_rsm;
		end_rsm = TAILQ_LAST_FAST(&rack->r_ctl.rc_tmap, rack_sendmap, r_tnext);
		if (end_rsm)
			kern_prefetch(end_rsm, &prefetch_rsm);
		prefetch_rsm = 1;
	}
	SOCKBUF_LOCK(sb);
	/*
	 * If in persist timeout with window of 0, send 1 byte. Otherwise,
	 * if window is small but nonzero and time TF_SENTFIN expired, we
	 * will send what we can and go to transmit state.
	 */
	if (tp->t_flags & TF_FORCEDATA) {
		if (sendwin == 0) {
			/*
			 * If we still have some data to send, then clear
			 * the FIN bit.  Usually this would happen below
			 * when it realizes that we aren't sending all the
			 * data.  However, if we have exactly 1 byte of
			 * unsent data, then it won't clear the FIN bit
			 * below, and if we are in persist state, we wind up
			 * sending the packet without recording that we sent
			 * the FIN bit.
			 *
			 * We can't just blindly clear the FIN bit, because
			 * if we don't have any more data to send then the
			 * probe will be the FIN itself.
			 */
			if (sb_offset < sbused(sb))
				flags &= ~TH_FIN;
			sendwin = 1;
		} else {
			if (rack->rc_in_persist)
				rack_exit_persist(tp, rack);
			/*
			 * If we are dropping persist mode then we need to
			 * correct snd_nxt/snd_max and off.
			 */
			tp->snd_nxt = tp->snd_max;
			sb_offset = tp->snd_nxt - tp->snd_una;
		}
	}
	/*
	 * If snd_nxt == snd_max and we have transmitted a FIN, the
	 * sb_offset will be > 0 even if so_snd.sb_cc is 0, resulting in a
	 * negative length.  This can also occur when TCP opens up its
	 * congestion window while receiving additional duplicate acks after
	 * fast-retransmit because TCP will reset snd_nxt to snd_max after
	 * the fast-retransmit.
	 *
	 * In the normal retransmit-FIN-only case, however, snd_nxt will be
	 * set to snd_una, the sb_offset will be 0, and the length may wind
	 * up 0.
	 *
	 * If sack_rxmit is true we are retransmitting from the scoreboard
	 * in which case len is already set.
	 */
	if (sack_rxmit == 0) {
		uint32_t avail;

		avail = sbavail(sb);
		if (SEQ_GT(tp->snd_nxt, tp->snd_una) && avail)
			sb_offset = tp->snd_nxt - tp->snd_una;
		else
			sb_offset = 0;
		if (IN_RECOVERY(tp->t_flags) == 0) {
			if (rack->r_ctl.rc_tlp_new_data) {
				/* TLP is forcing out new data */
				if (rack->r_ctl.rc_tlp_new_data > (uint32_t) (avail - sb_offset)) {
					rack->r_ctl.rc_tlp_new_data = (uint32_t) (avail - sb_offset);
				}
				if (rack->r_ctl.rc_tlp_new_data > tp->snd_wnd)
					len = tp->snd_wnd;
				else
					len = rack->r_ctl.rc_tlp_new_data;
				rack->r_ctl.rc_tlp_new_data = 0;
				doing_tlp = 1;
			} else {
				if (sendwin > avail) {
					/* use the available */
					if (avail > sb_offset) {
						len = (int32_t)(avail - sb_offset);
					} else {
						len = 0;
					}
				} else {
					if (sendwin > sb_offset) {
						len = (int32_t)(sendwin - sb_offset);
					} else {
						len = 0;
					}
				}
			}
		} else {
			uint32_t outstanding;

			/*
			 * We are inside of a SACK recovery episode and are
			 * sending new data, having retransmitted all the
			 * data possible so far in the scoreboard.
			 */
			outstanding = tp->snd_max - tp->snd_una;
			if ((rack->r_ctl.rc_prr_sndcnt + outstanding) > tp->snd_wnd)
				len = 0;
			else if (avail > sb_offset)
				len = avail - sb_offset;
			else
				len = 0;
			if (len > 0) {
				if (len > rack->r_ctl.rc_prr_sndcnt)
					len = rack->r_ctl.rc_prr_sndcnt;

				if (len > 0) {
					sub_from_prr = 1;
					counter_u64_add(rack_rtm_prr_newdata, 1);
				}
			}
			if (len > tp->t_maxseg) {
				/*
				 * We should never send more than a MSS when
				 * retransmitting or sending new data in prr
				 * mode unless the override flag is on. Most
				 * likely the PRR algorithm is not going to
				 * let us send a lot as well :-)
				 */
				if (rack->r_ctl.rc_prr_sendalot == 0)
					len = tp->t_maxseg;
			} else if (len < tp->t_maxseg) {
				/*
				 * Do we send any? The idea here is if the
				 * send empty's the socket buffer we want to
				 * do it. However if not then lets just wait
				 * for our prr_sndcnt to get bigger.
				 */
				long leftinsb;

				leftinsb = sbavail(sb) - sb_offset;
				if (leftinsb > len) {
					/* This send does not empty the sb */
					len = 0;
				}
			}
		}
	}
	if (prefetch_so_done == 0) {
		kern_prefetch(so, &prefetch_so_done);
		prefetch_so_done = 1;
	}
	/*
	 * Lop off SYN bit if it has already been sent.  However, if this is
	 * SYN-SENT state and if segment contains data and if we don't know
	 * that foreign host supports TAO, suppress sending segment.
	 */
	if ((flags & TH_SYN) && SEQ_GT(tp->snd_nxt, tp->snd_una) &&
	    ((sack_rxmit == 0) && (tp->t_rxtshift == 0))) {
		if (tp->t_state != TCPS_SYN_RECEIVED)
			flags &= ~TH_SYN;
		/*
		 * When sending additional segments following a TFO SYN|ACK,
		 * do not include the SYN bit.
		 */
		if (IS_FASTOPEN(tp->t_flags) &&
		    (tp->t_state == TCPS_SYN_RECEIVED))
			flags &= ~TH_SYN;
		sb_offset--, len++;
	}
	/*
	 * Be careful not to send data and/or FIN on SYN segments. This
	 * measure is needed to prevent interoperability problems with not
	 * fully conformant TCP implementations.
	 */
	if ((flags & TH_SYN) && (tp->t_flags & TF_NOOPT)) {
		len = 0;
		flags &= ~TH_FIN;
	}
	/*
	 * On TFO sockets, ensure no data is sent in the following cases:
	 *
	 *  - When retransmitting SYN|ACK on a passively-created socket
	 *
	 *  - When retransmitting SYN on an actively created socket
	 *
	 *  - When sending a zero-length cookie (cookie request) on an
	 *    actively created socket
	 *
	 *  - When the socket is in the CLOSED state (RST is being sent)
	 */
	if (IS_FASTOPEN(tp->t_flags) &&
	    (((flags & TH_SYN) && (tp->t_rxtshift > 0)) ||
	     ((tp->t_state == TCPS_SYN_SENT) &&
	      (tp->t_tfo_client_cookie_len == 0)) ||
	     (flags & TH_RST))) {
		sack_rxmit = 0;
		len = 0;
	}
	/* Without fast-open there should never be data sent on a SYN */
	if ((flags & TH_SYN) && (!IS_FASTOPEN(tp->t_flags)))
		len = 0;
	if (len <= 0) {
		/*
		 * If FIN has been sent but not acked, but we haven't been
		 * called to retransmit, len will be < 0.  Otherwise, window
		 * shrank after we sent into it.  If window shrank to 0,
		 * cancel pending retransmit, pull snd_nxt back to (closed)
		 * window, and set the persist timer if it isn't already
		 * going.  If the window didn't close completely, just wait
		 * for an ACK.
		 *
		 * We also do a general check here to ensure that we will
		 * set the persist timer when we have data to send, but a
		 * 0-byte window. This makes sure the persist timer is set
		 * even if the packet hits one of the "goto send" lines
		 * below.
		 */
		len = 0;
		if ((tp->snd_wnd == 0) &&
		    (TCPS_HAVEESTABLISHED(tp->t_state)) &&
		    (sb_offset < (int)sbavail(sb))) {
			tp->snd_nxt = tp->snd_una;
			rack_enter_persist(tp, rack, cts);
		}
	}
	/* len will be >= 0 after this point. */
	KASSERT(len >= 0, ("[%s:%d]: len < 0", __func__, __LINE__));
	tcp_sndbuf_autoscale(tp, so, sendwin);
	/*
	 * Decide if we can use TCP Segmentation Offloading (if supported by
	 * hardware).
	 *
	 * TSO may only be used if we are in a pure bulk sending state.  The
	 * presence of TCP-MD5, SACK retransmits, SACK advertizements and IP
	 * options prevent using TSO.  With TSO the TCP header is the same
	 * (except for the sequence number) for all generated packets.  This
	 * makes it impossible to transmit any options which vary per
	 * generated segment or packet.
	 *
	 * IPv4 handling has a clear separation of ip options and ip header
	 * flags while IPv6 combines both in in6p_outputopts. ip6_optlen() does
	 * the right thing below to provide length of just ip options and thus
	 * checking for ipoptlen is enough to decide if ip options are present.
	 */

#ifdef INET6
	if (isipv6)
		ipoptlen = ip6_optlen(tp->t_inpcb);
	else
#endif
		if (tp->t_inpcb->inp_options)
			ipoptlen = tp->t_inpcb->inp_options->m_len -
			    offsetof(struct ipoption, ipopt_list);
		else
			ipoptlen = 0;
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	/*
	 * Pre-calculate here as we save another lookup into the darknesses
	 * of IPsec that way and can actually decide if TSO is ok.
	 */
#ifdef INET6
	if (isipv6 && IPSEC_ENABLED(ipv6))
		ipsec_optlen = IPSEC_HDRSIZE(ipv6, tp->t_inpcb);
#ifdef INET
	else
#endif
#endif				/* INET6 */
#ifdef INET
	if (IPSEC_ENABLED(ipv4))
		ipsec_optlen = IPSEC_HDRSIZE(ipv4, tp->t_inpcb);
#endif				/* INET */
#endif

#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	ipoptlen += ipsec_optlen;
#endif
	if ((tp->t_flags & TF_TSO) && V_tcp_do_tso && len > tp->t_maxseg &&
	    (tp->t_port == 0) &&
	    ((tp->t_flags & TF_SIGNATURE) == 0) &&
	    tp->rcv_numsacks == 0 && sack_rxmit == 0 &&
	    ipoptlen == 0)
		tso = 1;
	{
		uint32_t outstanding;

		outstanding = tp->snd_max - tp->snd_una;
		if (tp->t_flags & TF_SENTFIN) {
			/*
			 * If we sent a fin, snd_max is 1 higher than
			 * snd_una
			 */
			outstanding--;
		}
		if (outstanding > 0) {
			/*
			 * This is sub-optimal. We only send a stand alone
			 * FIN on its own segment.
			 */
			if (flags & TH_FIN) {
				flags &= ~TH_FIN;
				would_have_fin = 1;
			}
		} else if (sack_rxmit) {
			if ((rsm->r_flags & RACK_HAS_FIN) == 0)
				flags &= ~TH_FIN;
		} else {
			if (SEQ_LT(tp->snd_nxt + len, tp->snd_una +
			    sbused(sb)))
				flags &= ~TH_FIN;
		}
	}
	recwin = sbspace(&so->so_rcv);

	/*
	 * Sender silly window avoidance.   We transmit under the following
	 * conditions when len is non-zero:
	 *
	 * - We have a full segment (or more with TSO) - This is the last
	 * buffer in a write()/send() and we are either idle or running
	 * NODELAY - we've timed out (e.g. persist timer) - we have more
	 * then 1/2 the maximum send window's worth of data (receiver may be
	 * limited the window size) - we need to retransmit
	 */
	if (len) {
		if (len >= tp->t_maxseg) {
			pass = 1;
			goto send;
		}
		/*
		 * NOTE! on localhost connections an 'ack' from the remote
		 * end may occur synchronously with the output and cause us
		 * to flush a buffer queued with moretocome.  XXX
		 *
		 */
		if (!(tp->t_flags & TF_MORETOCOME) &&	/* normal case */
		    (idle || (tp->t_flags & TF_NODELAY)) &&
		    ((uint32_t)len + (uint32_t)sb_offset >= sbavail(&so->so_snd)) && 
		    (tp->t_flags & TF_NOPUSH) == 0) {
			pass = 2;
			goto send;
		}
		if (tp->t_flags & TF_FORCEDATA) {	/* typ. timeout case */
			pass = 3;
			goto send;
		}
		if ((tp->snd_una == tp->snd_max) && len) {	/* Nothing outstanding */
			goto send;
		}
		if (len >= tp->max_sndwnd / 2 && tp->max_sndwnd > 0) {
			pass = 4;
			goto send;
		}
		if (SEQ_LT(tp->snd_nxt, tp->snd_max)) {	/* retransmit case */
			pass = 5;
			goto send;
		}
		if (sack_rxmit) {
			pass = 6;
			goto send;
		}
	}
	/*
	 * Sending of standalone window updates.
	 *
	 * Window updates are important when we close our window due to a
	 * full socket buffer and are opening it again after the application
	 * reads data from it.  Once the window has opened again and the
	 * remote end starts to send again the ACK clock takes over and
	 * provides the most current window information.
	 *
	 * We must avoid the silly window syndrome whereas every read from
	 * the receive buffer, no matter how small, causes a window update
	 * to be sent.  We also should avoid sending a flurry of window
	 * updates when the socket buffer had queued a lot of data and the
	 * application is doing small reads.
	 *
	 * Prevent a flurry of pointless window updates by only sending an
	 * update when we can increase the advertized window by more than
	 * 1/4th of the socket buffer capacity.  When the buffer is getting
	 * full or is very small be more aggressive and send an update
	 * whenever we can increase by two mss sized segments. In all other
	 * situations the ACK's to new incoming data will carry further
	 * window increases.
	 *
	 * Don't send an independent window update if a delayed ACK is
	 * pending (it will get piggy-backed on it) or the remote side
	 * already has done a half-close and won't send more data.  Skip
	 * this if the connection is in T/TCP half-open state.
	 */
	if (recwin > 0 && !(tp->t_flags & TF_NEEDSYN) &&
	    !(tp->t_flags & TF_DELACK) &&
	    !TCPS_HAVERCVDFIN(tp->t_state)) {
		/*
		 * "adv" is the amount we could increase the window, taking
		 * into account that we are limited by TCP_MAXWIN <<
		 * tp->rcv_scale.
		 */
		int32_t adv;
		int oldwin;

		adv = min(recwin, (long)TCP_MAXWIN << tp->rcv_scale);
		if (SEQ_GT(tp->rcv_adv, tp->rcv_nxt)) {
			oldwin = (tp->rcv_adv - tp->rcv_nxt);
			adv -= oldwin;
		} else
			oldwin = 0;

		/*
		 * If the new window size ends up being the same as the old
		 * size when it is scaled, then don't force a window update.
		 */
		if (oldwin >> tp->rcv_scale == (adv + oldwin) >> tp->rcv_scale)
			goto dontupdate;

		if (adv >= (int32_t)(2 * tp->t_maxseg) &&
		    (adv >= (int32_t)(so->so_rcv.sb_hiwat / 4) ||
		    recwin <= (int32_t)(so->so_rcv.sb_hiwat / 8) ||
		    so->so_rcv.sb_hiwat <= 8 * tp->t_maxseg)) {
			pass = 7;
			goto send;
		}
		if (2 * adv >= (int32_t) so->so_rcv.sb_hiwat)
			goto send;
	}
dontupdate:

	/*
	 * Send if we owe the peer an ACK, RST, SYN, or urgent data.  ACKNOW
	 * is also a catch-all for the retransmit timer timeout case.
	 */
	if (tp->t_flags & TF_ACKNOW) {
		pass = 8;
		goto send;
	}
	if (((flags & TH_SYN) && (tp->t_flags & TF_NEEDSYN) == 0)) {
		pass = 9;
		goto send;
	}
	if (SEQ_GT(tp->snd_up, tp->snd_una)) {
		pass = 10;
		goto send;
	}
	/*
	 * If our state indicates that FIN should be sent and we have not
	 * yet done so, then we need to send.
	 */
	if ((flags & TH_FIN) &&
	    (tp->snd_nxt == tp->snd_una)) {
		pass = 11;
		goto send;
	}
	/*
	 * No reason to send a segment, just return.
	 */
just_return:
	SOCKBUF_UNLOCK(sb);
just_return_nolock:
	if (tot_len_this_send == 0)
		counter_u64_add(rack_out_size[TCP_MSS_ACCT_JUSTRET], 1);
	rack_start_hpts_timer(rack, tp, cts, __LINE__, slot, tot_len_this_send, 1);
	rack_log_type_just_return(rack, cts, tot_len_this_send, slot, hpts_calling);
	tp->t_flags &= ~TF_FORCEDATA;
	return (0);

send:
	if (doing_tlp == 0) {
		/*
		 * Data not a TLP, and its not the rxt firing. If it is the
		 * rxt firing, we want to leave the tlp_in_progress flag on
		 * so we don't send another TLP. It has to be a rack timer
		 * or normal send (response to acked data) to clear the tlp
		 * in progress flag.
		 */
		rack->rc_tlp_in_progress = 0;
	}
	SOCKBUF_LOCK_ASSERT(sb);
	if (len > 0) {
		if (len >= tp->t_maxseg)
			tp->t_flags2 |= TF2_PLPMTU_MAXSEGSNT;
		else
			tp->t_flags2 &= ~TF2_PLPMTU_MAXSEGSNT;
	}
	/*
	 * Before ESTABLISHED, force sending of initial options unless TCP
	 * set not to do any options. NOTE: we assume that the IP/TCP header
	 * plus TCP options always fit in a single mbuf, leaving room for a
	 * maximum link header, i.e. max_linkhdr + sizeof (struct tcpiphdr)
	 * + optlen <= MCLBYTES
	 */
	optlen = 0;
#ifdef INET6
	if (isipv6)
		hdrlen = sizeof(struct ip6_hdr) + sizeof(struct tcphdr);
	else
#endif
		hdrlen = sizeof(struct tcpiphdr);

	/*
	 * Compute options for segment. We only have to care about SYN and
	 * established connection segments.  Options for SYN-ACK segments
	 * are handled in TCP syncache.
	 */
	to.to_flags = 0;
	if ((tp->t_flags & TF_NOOPT) == 0) {
		/* Maximum segment size. */
		if (flags & TH_SYN) {
			tp->snd_nxt = tp->iss;
			to.to_mss = tcp_mssopt(&inp->inp_inc);
#ifdef NETFLIX_TCPOUDP
			if (tp->t_port)
				to.to_mss -= V_tcp_udp_tunneling_overhead;
#endif
			to.to_flags |= TOF_MSS;

			/*
			 * On SYN or SYN|ACK transmits on TFO connections,
			 * only include the TFO option if it is not a
			 * retransmit, as the presence of the TFO option may
			 * have caused the original SYN or SYN|ACK to have
			 * been dropped by a middlebox.
			 */
			if (IS_FASTOPEN(tp->t_flags) &&
			    (tp->t_rxtshift == 0)) {
				if (tp->t_state == TCPS_SYN_RECEIVED) {
					to.to_tfo_len = TCP_FASTOPEN_COOKIE_LEN;
					to.to_tfo_cookie =
					    (u_int8_t *)&tp->t_tfo_cookie.server;
					to.to_flags |= TOF_FASTOPEN;
					wanted_cookie = 1;
				} else if (tp->t_state == TCPS_SYN_SENT) {
					to.to_tfo_len =
					    tp->t_tfo_client_cookie_len;
					to.to_tfo_cookie =
					    tp->t_tfo_cookie.client;
					to.to_flags |= TOF_FASTOPEN;
					wanted_cookie = 1;
					/*
					 * If we wind up having more data to
					 * send with the SYN than can fit in
					 * one segment, don't send any more
					 * until the SYN|ACK comes back from
					 * the other end.
					 */
					sendalot = 0;
				}
			}
		}
		/* Window scaling. */
		if ((flags & TH_SYN) && (tp->t_flags & TF_REQ_SCALE)) {
			to.to_wscale = tp->request_r_scale;
			to.to_flags |= TOF_SCALE;
		}
		/* Timestamps. */
		if ((tp->t_flags & TF_RCVD_TSTMP) ||
		    ((flags & TH_SYN) && (tp->t_flags & TF_REQ_TSTMP))) {
			to.to_tsval = cts + tp->ts_offset;
			to.to_tsecr = tp->ts_recent;
			to.to_flags |= TOF_TS;
		}
		/* Set receive buffer autosizing timestamp. */
		if (tp->rfbuf_ts == 0 &&
		    (so->so_rcv.sb_flags & SB_AUTOSIZE))
			tp->rfbuf_ts = tcp_ts_getticks();
		/* Selective ACK's. */
		if (flags & TH_SYN)
			to.to_flags |= TOF_SACKPERM;
		else if (TCPS_HAVEESTABLISHED(tp->t_state) &&
		    tp->rcv_numsacks > 0) {
			to.to_flags |= TOF_SACK;
			to.to_nsacks = tp->rcv_numsacks;
			to.to_sacks = (u_char *)tp->sackblks;
		}
#if defined(IPSEC_SUPPORT) || defined(TCP_SIGNATURE)
		/* TCP-MD5 (RFC2385). */
		if (tp->t_flags & TF_SIGNATURE)
			to.to_flags |= TOF_SIGNATURE;
#endif				/* TCP_SIGNATURE */

		/* Processing the options. */
		hdrlen += optlen = tcp_addoptions(&to, opt);
		/*
		 * If we wanted a TFO option to be added, but it was unable
		 * to fit, ensure no data is sent.
		 */
		if (IS_FASTOPEN(tp->t_flags) && wanted_cookie &&
		    !(to.to_flags & TOF_FASTOPEN))
			len = 0;
	}
#ifdef NETFLIX_TCPOUDP
	if (tp->t_port) {
		if (V_tcp_udp_tunneling_port == 0) {
			/* The port was removed?? */
			SOCKBUF_UNLOCK(&so->so_snd);
			return (EHOSTUNREACH);
		}
		hdrlen += sizeof(struct udphdr);
	}
#endif
	ipoptlen = 0;
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	ipoptlen += ipsec_optlen;
#endif

	/*
	 * Adjust data length if insertion of options will bump the packet
	 * length beyond the t_maxseg length. Clear the FIN bit because we
	 * cut off the tail of the segment.
	 */
	if (len + optlen + ipoptlen > tp->t_maxseg) {
		if (flags & TH_FIN) {
			would_have_fin = 1;
			flags &= ~TH_FIN;
		}
		if (tso) {
			uint32_t if_hw_tsomax;
			uint32_t moff;
			int32_t max_len;

			/* extract TSO information */
			if_hw_tsomax = tp->t_tsomax;
			if_hw_tsomaxsegcount = tp->t_tsomaxsegcount;
			if_hw_tsomaxsegsize = tp->t_tsomaxsegsize;
			KASSERT(ipoptlen == 0,
			    ("%s: TSO can't do IP options", __func__));

			/*
			 * Check if we should limit by maximum payload
			 * length:
			 */
			if (if_hw_tsomax != 0) {
				/* compute maximum TSO length */
				max_len = (if_hw_tsomax - hdrlen -
				    max_linkhdr);
				if (max_len <= 0) {
					len = 0;
				} else if (len > max_len) {
					sendalot = 1;
					len = max_len;
				}
			}
			/*
			 * Prevent the last segment from being fractional
			 * unless the send sockbuf can be emptied:
			 */
			max_len = (tp->t_maxseg - optlen);
			if ((sb_offset + len) < sbavail(sb)) {
				moff = len % (u_int)max_len;
				if (moff != 0) {
					len -= moff;
					sendalot = 1;
				}
			}
			/*
			 * In case there are too many small fragments don't
			 * use TSO:
			 */
			if (len <= max_len) {
				len = max_len;
				sendalot = 1;
				tso = 0;
			}
			/*
			 * Send the FIN in a separate segment after the bulk
			 * sending is done. We don't trust the TSO
			 * implementations to clear the FIN flag on all but
			 * the last segment.
			 */
			if (tp->t_flags & TF_NEEDFIN)
				sendalot = 1;

		} else {
			len = tp->t_maxseg - optlen - ipoptlen;
			sendalot = 1;
		}
	} else
		tso = 0;
	KASSERT(len + hdrlen + ipoptlen <= IP_MAXPACKET,
	    ("%s: len > IP_MAXPACKET", __func__));
#ifdef DIAGNOSTIC
#ifdef INET6
	if (max_linkhdr + hdrlen > MCLBYTES)
#else
	if (max_linkhdr + hdrlen > MHLEN)
#endif
		panic("tcphdr too big");
#endif

	/*
	 * This KASSERT is here to catch edge cases at a well defined place.
	 * Before, those had triggered (random) panic conditions further
	 * down.
	 */
	KASSERT(len >= 0, ("[%s:%d]: len < 0", __func__, __LINE__));
	if ((len == 0) &&
	    (flags & TH_FIN) &&
	    (sbused(sb))) {
		/*
		 * We have outstanding data, don't send a fin by itself!.
		 */
		goto just_return;
	}
	/*
	 * Grab a header mbuf, attaching a copy of data to be transmitted,
	 * and initialize the header from the template for sends on this
	 * connection.
	 */
	if (len) {
		uint32_t max_val;
		uint32_t moff;

		if (rack->rc_pace_max_segs)
			max_val = rack->rc_pace_max_segs * tp->t_maxseg;
		else
			max_val = len;
		/*
		 * We allow a limit on sending with hptsi.
		 */
		if (len > max_val) {
			len = max_val;
		}
#ifdef INET6
		if (MHLEN < hdrlen + max_linkhdr)
			m = m_getcl(M_NOWAIT, MT_DATA, M_PKTHDR);
		else
#endif
			m = m_gethdr(M_NOWAIT, MT_DATA);

		if (m == NULL) {
			SOCKBUF_UNLOCK(sb);
			error = ENOBUFS;
			sack_rxmit = 0;
			goto out;
		}
		m->m_data += max_linkhdr;
		m->m_len = hdrlen;

		/*
		 * Start the m_copy functions from the closest mbuf to the
		 * sb_offset in the socket buffer chain.
		 */
		mb = sbsndptr_noadv(sb, sb_offset, &moff);
		if (len <= MHLEN - hdrlen - max_linkhdr) {
			m_copydata(mb, moff, (int)len,
			    mtod(m, caddr_t)+hdrlen);
			if (SEQ_LT(tp->snd_nxt, tp->snd_max))
				sbsndptr_adv(sb, mb, len);
			m->m_len += len;
		} else {
			struct sockbuf *msb;

			if (SEQ_LT(tp->snd_nxt, tp->snd_max))
				msb = NULL;
			else
				msb = sb;
			m->m_next = tcp_m_copym(mb, moff, &len,
			    if_hw_tsomaxsegcount, if_hw_tsomaxsegsize, msb);
			if (len <= (tp->t_maxseg - optlen)) {
				/* 
				 * Must have ran out of mbufs for the copy
				 * shorten it to no longer need tso. Lets
				 * not put on sendalot since we are low on
				 * mbufs.
				 */
				tso = 0;
			}
			if (m->m_next == NULL) {
				SOCKBUF_UNLOCK(sb);
				(void)m_free(m);
				error = ENOBUFS;
				sack_rxmit = 0;
				goto out;
			}
		}
		if ((tp->t_flags & TF_FORCEDATA) && len == 1) {
			TCPSTAT_INC(tcps_sndprobe);
#ifdef NETFLIX_STATS
			if (SEQ_LT(tp->snd_nxt, tp->snd_max))
				stats_voi_update_abs_u32(tp->t_stats,
				    VOI_TCP_RETXPB, len);
			else
				stats_voi_update_abs_u64(tp->t_stats,
				    VOI_TCP_TXPB, len);
#endif
		} else if (SEQ_LT(tp->snd_nxt, tp->snd_max) || sack_rxmit) {
			if (rsm && (rsm->r_flags & RACK_TLP)) {
				/*
				 * TLP should not count in retran count, but
				 * in its own bin
				 */
				counter_u64_add(rack_tlp_retran, 1);
				counter_u64_add(rack_tlp_retran_bytes, len);
			} else {
				tp->t_sndrexmitpack++;
				TCPSTAT_INC(tcps_sndrexmitpack);
				TCPSTAT_ADD(tcps_sndrexmitbyte, len);
			}
#ifdef NETFLIX_STATS
			stats_voi_update_abs_u32(tp->t_stats, VOI_TCP_RETXPB,
			    len);
#endif
		} else {
			TCPSTAT_INC(tcps_sndpack);
			TCPSTAT_ADD(tcps_sndbyte, len);
#ifdef NETFLIX_STATS
			stats_voi_update_abs_u64(tp->t_stats, VOI_TCP_TXPB,
			    len);
#endif
		}
		/*
		 * If we're sending everything we've got, set PUSH. (This
		 * will keep happy those implementations which only give
		 * data to the user when a buffer fills or a PUSH comes in.)
		 */
		if (sb_offset + len == sbused(sb) &&
		    sbused(sb) &&
		    !(flags & TH_SYN))
			flags |= TH_PUSH;

		/*
		 * Are we doing hptsi, if so we must calculate the slot. We
		 * only do hptsi in ESTABLISHED and with no RESET being
		 * sent where we have data to send.
		 */
		if (((tp->t_state == TCPS_ESTABLISHED) ||
		    (tp->t_state == TCPS_CLOSE_WAIT) ||
		    ((tp->t_state == TCPS_FIN_WAIT_1) &&
		    ((tp->t_flags & TF_SENTFIN) == 0) &&
		    ((flags & TH_FIN) == 0))) &&
		    ((flags & TH_RST) == 0) &&
		    (rack->rc_always_pace)) {
			/*
			 * We use the most optimistic possible cwnd/srtt for
			 * sending calculations. This will make our
			 * calculation anticipate getting more through
			 * quicker then possible. But thats ok we don't want
			 * the peer to have a gap in data sending.
			 */
			uint32_t srtt, cwnd, tr_perms = 0;
	
			if (rack->r_ctl.rc_rack_min_rtt)
				srtt = rack->r_ctl.rc_rack_min_rtt;
			else
				srtt = TICKS_2_MSEC((tp->t_srtt >> TCP_RTT_SHIFT));
			if (rack->r_ctl.rc_rack_largest_cwnd)
				cwnd = rack->r_ctl.rc_rack_largest_cwnd;
			else
				cwnd = tp->snd_cwnd;
			tr_perms = cwnd / srtt;
			if (tr_perms == 0) {
				tr_perms = tp->t_maxseg;
			}
			tot_len_this_send += len;
			/*
			 * Calculate how long this will take to drain, if
			 * the calculation comes out to zero, thats ok we
			 * will use send_a_lot to possibly spin around for
			 * more increasing tot_len_this_send to the point
			 * that its going to require a pace, or we hit the
			 * cwnd. Which in that case we are just waiting for
			 * a ACK.
			 */
			slot = tot_len_this_send / tr_perms;
			/* Now do we reduce the time so we don't run dry? */
			if (slot && rack->rc_pace_reduce) {
				int32_t reduce;

				reduce = (slot / rack->rc_pace_reduce);
				if (reduce < slot) {
					slot -= reduce;
				} else
					slot = 0;
			}
			if (rack->r_enforce_min_pace &&
			    (slot == 0) &&
			    (tot_len_this_send >= (rack->r_min_pace_seg_thresh * tp->t_maxseg))) {
				/* We are enforcing a minimum pace time of 1ms */
				slot = rack->r_enforce_min_pace;
			}
		}
		SOCKBUF_UNLOCK(sb);
	} else {
		SOCKBUF_UNLOCK(sb);
		if (tp->t_flags & TF_ACKNOW)
			TCPSTAT_INC(tcps_sndacks);
		else if (flags & (TH_SYN | TH_FIN | TH_RST))
			TCPSTAT_INC(tcps_sndctrl);
		else if (SEQ_GT(tp->snd_up, tp->snd_una))
			TCPSTAT_INC(tcps_sndurg);
		else
			TCPSTAT_INC(tcps_sndwinup);

		m = m_gethdr(M_NOWAIT, MT_DATA);
		if (m == NULL) {
			error = ENOBUFS;
			sack_rxmit = 0;
			goto out;
		}
#ifdef INET6
		if (isipv6 && (MHLEN < hdrlen + max_linkhdr) &&
		    MHLEN >= hdrlen) {
			M_ALIGN(m, hdrlen);
		} else
#endif
			m->m_data += max_linkhdr;
		m->m_len = hdrlen;
	}
	SOCKBUF_UNLOCK_ASSERT(sb);
	m->m_pkthdr.rcvif = (struct ifnet *)0;
#ifdef MAC
	mac_inpcb_create_mbuf(inp, m);
#endif
#ifdef INET6
	if (isipv6) {
		ip6 = mtod(m, struct ip6_hdr *);
#ifdef NETFLIX_TCPOUDP
		if (tp->t_port) {
			udp = (struct udphdr *)((caddr_t)ip6 + ipoptlen + sizeof(struct ip6_hdr));
			udp->uh_sport = htons(V_tcp_udp_tunneling_port);
			udp->uh_dport = tp->t_port;
			ulen = hdrlen + len - sizeof(struct ip6_hdr);
			udp->uh_ulen = htons(ulen);
			th = (struct tcphdr *)(udp + 1);
		} else
#endif
			th = (struct tcphdr *)(ip6 + 1);
		tcpip_fillheaders(inp, ip6, th);
	} else
#endif				/* INET6 */
	{
		ip = mtod(m, struct ip *);
#ifdef TCPDEBUG
		ipov = (struct ipovly *)ip;
#endif
#ifdef NETFLIX_TCPOUDP
		if (tp->t_port) {
			udp = (struct udphdr *)((caddr_t)ip + ipoptlen + sizeof(struct ip));
			udp->uh_sport = htons(V_tcp_udp_tunneling_port);
			udp->uh_dport = tp->t_port;
			ulen = hdrlen + len - sizeof(struct ip);
			udp->uh_ulen = htons(ulen);
			th = (struct tcphdr *)(udp + 1);
		} else
#endif
			th = (struct tcphdr *)(ip + 1);
		tcpip_fillheaders(inp, ip, th);
	}
	/*
	 * Fill in fields, remembering maximum advertised window for use in
	 * delaying messages about window sizes. If resending a FIN, be sure
	 * not to use a new sequence number.
	 */
	if (flags & TH_FIN && tp->t_flags & TF_SENTFIN &&
	    tp->snd_nxt == tp->snd_max)
		tp->snd_nxt--;
	/*
	 * If we are starting a connection, send ECN setup SYN packet. If we
	 * are on a retransmit, we may resend those bits a number of times
	 * as per RFC 3168.
	 */
	if (tp->t_state == TCPS_SYN_SENT && V_tcp_do_ecn == 1) {
		if (tp->t_rxtshift >= 1) {
			if (tp->t_rxtshift <= V_tcp_ecn_maxretries)
				flags |= TH_ECE | TH_CWR;
		} else
			flags |= TH_ECE | TH_CWR;
	}
	if (tp->t_state == TCPS_ESTABLISHED &&
	    (tp->t_flags & TF_ECN_PERMIT)) {
		/*
		 * If the peer has ECN, mark data packets with ECN capable
		 * transmission (ECT). Ignore pure ack packets,
		 * retransmissions and window probes.
		 */
		if (len > 0 && SEQ_GEQ(tp->snd_nxt, tp->snd_max) &&
		    !((tp->t_flags & TF_FORCEDATA) && len == 1)) {
#ifdef INET6
			if (isipv6)
				ip6->ip6_flow |= htonl(IPTOS_ECN_ECT0 << 20);
			else
#endif
				ip->ip_tos |= IPTOS_ECN_ECT0;
			TCPSTAT_INC(tcps_ecn_ect0);
		}
		/*
		 * Reply with proper ECN notifications.
		 */
		if (tp->t_flags & TF_ECN_SND_CWR) {
			flags |= TH_CWR;
			tp->t_flags &= ~TF_ECN_SND_CWR;
		}
		if (tp->t_flags & TF_ECN_SND_ECE)
			flags |= TH_ECE;
	}
	/*
	 * If we are doing retransmissions, then snd_nxt will not reflect
	 * the first unsent octet.  For ACK only packets, we do not want the
	 * sequence number of the retransmitted packet, we want the sequence
	 * number of the next unsent octet.  So, if there is no data (and no
	 * SYN or FIN), use snd_max instead of snd_nxt when filling in
	 * ti_seq.  But if we are in persist state, snd_max might reflect
	 * one byte beyond the right edge of the window, so use snd_nxt in
	 * that case, since we know we aren't doing a retransmission.
	 * (retransmit and persist are mutually exclusive...)
	 */
	if (sack_rxmit == 0) {
		if (len || (flags & (TH_SYN | TH_FIN)) ||
		    rack->rc_in_persist) {
			th->th_seq = htonl(tp->snd_nxt);
			rack_seq = tp->snd_nxt;
		} else if (flags & TH_RST) {
			/*
			 * For a Reset send the last cum ack in sequence
			 * (this like any other choice may still generate a
			 * challenge ack, if a ack-update packet is in
			 * flight).
			 */
			th->th_seq = htonl(tp->snd_una);
			rack_seq = tp->snd_una;
		} else {
			th->th_seq = htonl(tp->snd_max);
			rack_seq = tp->snd_max;
		}
	} else {
		th->th_seq = htonl(rsm->r_start);
		rack_seq = rsm->r_start;
	}
	th->th_ack = htonl(tp->rcv_nxt);
	if (optlen) {
		bcopy(opt, th + 1, optlen);
		th->th_off = (sizeof(struct tcphdr) + optlen) >> 2;
	}
	th->th_flags = flags;
	/*
	 * Calculate receive window.  Don't shrink window, but avoid silly
	 * window syndrome.
	 * If a RST segment is sent, advertise a window of zero.
	 */
	if (flags & TH_RST) {
		recwin = 0;
	} else {
		if (recwin < (long)(so->so_rcv.sb_hiwat / 4) &&
		    recwin < (long)tp->t_maxseg)
			recwin = 0;
		if (SEQ_GT(tp->rcv_adv, tp->rcv_nxt) &&
		    recwin < (long)(tp->rcv_adv - tp->rcv_nxt))
			recwin = (long)(tp->rcv_adv - tp->rcv_nxt);
		if (recwin > (long)TCP_MAXWIN << tp->rcv_scale)
			recwin = (long)TCP_MAXWIN << tp->rcv_scale;
	}

	/*
	 * According to RFC1323 the window field in a SYN (i.e., a <SYN> or
	 * <SYN,ACK>) segment itself is never scaled.  The <SYN,ACK> case is
	 * handled in syncache.
	 */
	if (flags & TH_SYN)
		th->th_win = htons((u_short)
		    (min(sbspace(&so->so_rcv), TCP_MAXWIN)));
	else
		th->th_win = htons((u_short)(recwin >> tp->rcv_scale));
	/*
	 * Adjust the RXWIN0SENT flag - indicate that we have advertised a 0
	 * window.  This may cause the remote transmitter to stall.  This
	 * flag tells soreceive() to disable delayed acknowledgements when
	 * draining the buffer.  This can occur if the receiver is
	 * attempting to read more data than can be buffered prior to
	 * transmitting on the connection.
	 */
	if (th->th_win == 0) {
		tp->t_sndzerowin++;
		tp->t_flags |= TF_RXWIN0SENT;
	} else
		tp->t_flags &= ~TF_RXWIN0SENT;
	if (SEQ_GT(tp->snd_up, tp->snd_nxt)) {
		th->th_urp = htons((u_short)(tp->snd_up - tp->snd_nxt));
		th->th_flags |= TH_URG;
	} else
		/*
		 * If no urgent pointer to send, then we pull the urgent
		 * pointer to the left edge of the send window so that it
		 * doesn't drift into the send window on sequence number
		 * wraparound.
		 */
		tp->snd_up = tp->snd_una;	/* drag it along */

#if defined(IPSEC_SUPPORT) || defined(TCP_SIGNATURE)
	if (to.to_flags & TOF_SIGNATURE) {
		/*
		 * Calculate MD5 signature and put it into the place
		 * determined before.
		 * NOTE: since TCP options buffer doesn't point into
		 * mbuf's data, calculate offset and use it.
		 */
		if (!TCPMD5_ENABLED() || TCPMD5_OUTPUT(m, th,
		    (u_char *)(th + 1) + (to.to_signature - opt)) != 0) {
			/*
			 * Do not send segment if the calculation of MD5
			 * digest has failed.
			 */
			goto out;
		}
	}
#endif

	/*
	 * Put TCP length in extended header, and then checksum extended
	 * header and data.
	 */
	m->m_pkthdr.len = hdrlen + len;	/* in6_cksum() need this */
#ifdef INET6
	if (isipv6) {
		/*
		 * ip6_plen is not need to be filled now, and will be filled
		 * in ip6_output.
		 */
		if (tp->t_port) {
			m->m_pkthdr.csum_flags = CSUM_UDP_IPV6;
			m->m_pkthdr.csum_data = offsetof(struct udphdr, uh_sum);
			udp->uh_sum = in6_cksum_pseudo(ip6, ulen, IPPROTO_UDP, 0);
			th->th_sum = htons(0);
		} else {
			m->m_pkthdr.csum_flags = CSUM_TCP_IPV6;
			m->m_pkthdr.csum_data = offsetof(struct tcphdr, th_sum);
			th->th_sum = in6_cksum_pseudo(ip6,
			    sizeof(struct tcphdr) + optlen + len, IPPROTO_TCP,
			    0);
		}
	}
#endif
#if defined(INET6) && defined(INET)
	else
#endif
#ifdef INET
	{
		if (tp->t_port) {
			m->m_pkthdr.csum_flags = CSUM_UDP;
			m->m_pkthdr.csum_data = offsetof(struct udphdr, uh_sum);
			udp->uh_sum = in_pseudo(ip->ip_src.s_addr,
			   ip->ip_dst.s_addr, htons(ulen + IPPROTO_UDP));
			th->th_sum = htons(0);
		} else {
			m->m_pkthdr.csum_flags = CSUM_TCP;
			m->m_pkthdr.csum_data = offsetof(struct tcphdr, th_sum);
			th->th_sum = in_pseudo(ip->ip_src.s_addr,
			    ip->ip_dst.s_addr, htons(sizeof(struct tcphdr) +
			    IPPROTO_TCP + len + optlen));
		}
		/* IP version must be set here for ipv4/ipv6 checking later */
		KASSERT(ip->ip_v == IPVERSION,
		    ("%s: IP version incorrect: %d", __func__, ip->ip_v));
	}
#endif

	/*
	 * Enable TSO and specify the size of the segments. The TCP pseudo
	 * header checksum is always provided. XXX: Fixme: This is currently
	 * not the case for IPv6.
	 */
	if (tso) {
		KASSERT(len > tp->t_maxseg - optlen,
		    ("%s: len <= tso_segsz", __func__));
		m->m_pkthdr.csum_flags |= CSUM_TSO;
		m->m_pkthdr.tso_segsz = tp->t_maxseg - optlen;
	}
#if defined(IPSEC) || defined(IPSEC_SUPPORT)
	KASSERT(len + hdrlen + ipoptlen - ipsec_optlen == m_length(m, NULL),
	    ("%s: mbuf chain shorter than expected: %d + %u + %u - %u != %u",
	    __func__, len, hdrlen, ipoptlen, ipsec_optlen, m_length(m, NULL)));
#else
	KASSERT(len + hdrlen + ipoptlen == m_length(m, NULL),
	    ("%s: mbuf chain shorter than expected: %d + %u + %u != %u",
	    __func__, len, hdrlen, ipoptlen, m_length(m, NULL)));
#endif

#ifdef TCP_HHOOK
	/* Run HHOOK_TCP_ESTABLISHED_OUT helper hooks. */
	hhook_run_tcp_est_out(tp, th, &to, len, tso);
#endif

#ifdef TCPDEBUG
	/*
	 * Trace.
	 */
	if (so->so_options & SO_DEBUG) {
		u_short save = 0;

#ifdef INET6
		if (!isipv6)
#endif
		{
			save = ipov->ih_len;
			ipov->ih_len = htons(m->m_pkthdr.len	/* - hdrlen +
			      * (th->th_off << 2) */ );
		}
		tcp_trace(TA_OUTPUT, tp->t_state, tp, mtod(m, void *), th, 0);
#ifdef INET6
		if (!isipv6)
#endif
			ipov->ih_len = save;
	}
#endif				/* TCPDEBUG */

	/* We're getting ready to send; log now. */
	if (tp->t_logstate != TCP_LOG_STATE_OFF) {
		union tcp_log_stackspecific log;

		memset(&log.u_bbr, 0, sizeof(log.u_bbr));
		log.u_bbr.inhpts = rack->rc_inp->inp_in_hpts;
		log.u_bbr.ininput = rack->rc_inp->inp_in_input;
		log.u_bbr.flex1 = rack->r_ctl.rc_prr_sndcnt;
		if (rsm || sack_rxmit) {
			log.u_bbr.flex8 = 1;
		} else {
			log.u_bbr.flex8 = 0;
		}
		lgb = tcp_log_event_(tp, th, &so->so_rcv, &so->so_snd, TCP_LOG_OUT, ERRNO_UNK,
		    len, &log, false, NULL, NULL, 0, NULL);
	} else
		lgb = NULL;

	/*
	 * Fill in IP length and desired time to live and send to IP level.
	 * There should be a better way to handle ttl and tos; we could keep
	 * them in the template, but need a way to checksum without them.
	 */
	/*
	 * m->m_pkthdr.len should have been set before cksum calcuration,
	 * because in6_cksum() need it.
	 */
#ifdef INET6
	if (isipv6) {
		/*
		 * we separately set hoplimit for every segment, since the
		 * user might want to change the value via setsockopt. Also,
		 * desired default hop limit might be changed via Neighbor
		 * Discovery.
		 */
		ip6->ip6_hlim = in6_selecthlim(inp, NULL);

		/*
		 * Set the packet size here for the benefit of DTrace
		 * probes. ip6_output() will set it properly; it's supposed
		 * to include the option header lengths as well.
		 */
		ip6->ip6_plen = htons(m->m_pkthdr.len - sizeof(*ip6));

		if (V_path_mtu_discovery && tp->t_maxseg > V_tcp_minmss)
			tp->t_flags2 |= TF2_PLPMTU_PMTUD;
		else
			tp->t_flags2 &= ~TF2_PLPMTU_PMTUD;

		if (tp->t_state == TCPS_SYN_SENT)
			TCP_PROBE5(connect__request, NULL, tp, ip6, tp, th);

		TCP_PROBE5(send, NULL, tp, ip6, tp, th);
		/* TODO: IPv6 IP6TOS_ECT bit on */
		error = ip6_output(m, tp->t_inpcb->in6p_outputopts,
		    &inp->inp_route6,
		    ((so->so_options & SO_DONTROUTE) ? IP_ROUTETOIF : 0),
		    NULL, NULL, inp);

		if (error == EMSGSIZE && inp->inp_route6.ro_rt != NULL)
			mtu = inp->inp_route6.ro_rt->rt_mtu;
	}
#endif				/* INET6 */
#if defined(INET) && defined(INET6)
	else
#endif
#ifdef INET
	{
		ip->ip_len = htons(m->m_pkthdr.len);
#ifdef INET6
		if (inp->inp_vflag & INP_IPV6PROTO)
			ip->ip_ttl = in6_selecthlim(inp, NULL);
#endif				/* INET6 */
		/*
		 * If we do path MTU discovery, then we set DF on every
		 * packet. This might not be the best thing to do according
		 * to RFC3390 Section 2. However the tcp hostcache migitates
		 * the problem so it affects only the first tcp connection
		 * with a host.
		 *
		 * NB: Don't set DF on small MTU/MSS to have a safe
		 * fallback.
		 */
		if (V_path_mtu_discovery && tp->t_maxseg > V_tcp_minmss) {
			tp->t_flags2 |= TF2_PLPMTU_PMTUD;
			if (tp->t_port == 0 || len < V_tcp_minmss) {
				ip->ip_off |= htons(IP_DF);
			}
		} else {
			tp->t_flags2 &= ~TF2_PLPMTU_PMTUD;
		}

		if (tp->t_state == TCPS_SYN_SENT)
			TCP_PROBE5(connect__request, NULL, tp, ip, tp, th);

		TCP_PROBE5(send, NULL, tp, ip, tp, th);

		error = ip_output(m, tp->t_inpcb->inp_options, &inp->inp_route,
		    ((so->so_options & SO_DONTROUTE) ? IP_ROUTETOIF : 0), 0,
		    inp);
		if (error == EMSGSIZE && inp->inp_route.ro_rt != NULL)
			mtu = inp->inp_route.ro_rt->rt_mtu;
	}
#endif				/* INET */

out:
	if (lgb) {
		lgb->tlb_errno = error;
		lgb = NULL;
	}
	/*
	 * In transmit state, time the transmission and arrange for the
	 * retransmit.  In persist state, just set snd_max.
	 */
	if (error == 0) {
		if (len == 0)
			counter_u64_add(rack_out_size[TCP_MSS_ACCT_SNDACK], 1);
		else if (len == 1) {
			counter_u64_add(rack_out_size[TCP_MSS_ACCT_PERSIST], 1);
		} else if (len > 1) {
			int idx;

			idx = (len / tp->t_maxseg) + 3;
			if (idx >= TCP_MSS_ACCT_ATIMER)
				counter_u64_add(rack_out_size[(TCP_MSS_ACCT_ATIMER-1)], 1);
			else
				counter_u64_add(rack_out_size[idx], 1);
		}
	}
	if (sub_from_prr && (error == 0)) {
		rack->r_ctl.rc_prr_sndcnt -= len;
	}
	sub_from_prr = 0;
	rack_log_output(tp, &to, len, rack_seq, (uint8_t) flags, error, cts,
	    pass, rsm);
	if ((tp->t_flags & TF_FORCEDATA) == 0 ||
	    (rack->rc_in_persist == 0)) {
		tcp_seq startseq = tp->snd_nxt;

		/*
		 * Advance snd_nxt over sequence space of this segment.
		 */
		if (error)
			/* We don't log or do anything with errors */
			goto timer;

		if (flags & (TH_SYN | TH_FIN)) {
			if (flags & TH_SYN)
				tp->snd_nxt++;
			if (flags & TH_FIN) {
				tp->snd_nxt++;
				tp->t_flags |= TF_SENTFIN;
			}
		}
		/* In the ENOBUFS case we do *not* update snd_max */
		if (sack_rxmit)
			goto timer;

		tp->snd_nxt += len;
		if (SEQ_GT(tp->snd_nxt, tp->snd_max)) {
			if (tp->snd_una == tp->snd_max) {
				/*
				 * Update the time we just added data since
				 * none was outstanding.
				 */
				rack_log_progress_event(rack, tp, ticks, PROGRESS_START, __LINE__);
				tp->t_acktime = ticks;
			}
			tp->snd_max = tp->snd_nxt;
			/*
			 * Time this transmission if not a retransmission and
			 * not currently timing anything.
			 * This is only relevant in case of switching back to
			 * the base stack.
			 */
			if (tp->t_rtttime == 0) {
				tp->t_rtttime = ticks;
				tp->t_rtseq = startseq;
				TCPSTAT_INC(tcps_segstimed);
			}
#ifdef NETFLIX_STATS
			if (!(tp->t_flags & TF_GPUTINPROG) && len) {
				tp->t_flags |= TF_GPUTINPROG;
				tp->gput_seq = startseq;
				tp->gput_ack = startseq +
				    ulmin(sbavail(sb) - sb_offset, sendwin);
				tp->gput_ts = tcp_ts_getticks();
			}
#endif
		}
		/*
		 * Set retransmit timer if not currently set, and not doing
		 * a pure ack or a keep-alive probe. Initial value for
		 * retransmit timer is smoothed round-trip time + 2 *
		 * round-trip time variance. Initialize shift counter which
		 * is used for backoff of retransmit time.
		 */
timer:
		if ((tp->snd_wnd == 0) &&
		    TCPS_HAVEESTABLISHED(tp->t_state)) {
			/*
			 * If the persists timer was set above (right before
			 * the goto send), and still needs to be on. Lets
			 * make sure all is canceled. If the persist timer
			 * is not running, we want to get it up.
			 */
			if (rack->rc_in_persist == 0) {
				rack_enter_persist(tp, rack, cts);
			}
		}
	} else {
		/*
		 * Persist case, update snd_max but since we are in persist
		 * mode (no window) we do not update snd_nxt.
		 */
		int32_t xlen = len;

		if (error)
			goto nomore;

		if (flags & TH_SYN)
			++xlen;
		if (flags & TH_FIN) {
			++xlen;
			tp->t_flags |= TF_SENTFIN;
		}
		/* In the ENOBUFS case we do *not* update snd_max */
		if (SEQ_GT(tp->snd_nxt + xlen, tp->snd_max)) {
			if (tp->snd_una == tp->snd_max) {
				/*
				 * Update the time we just added data since
				 * none was outstanding.
				 */
				rack_log_progress_event(rack, tp, ticks, PROGRESS_START, __LINE__);
				tp->t_acktime = ticks;
			}
			tp->snd_max = tp->snd_nxt + len;
		}
	}
nomore:
	if (error) {
		SOCKBUF_UNLOCK_ASSERT(sb);	/* Check gotos. */
		/*
		 * Failures do not advance the seq counter above. For the
		 * case of ENOBUFS we will fall out and retry in 1ms with
		 * the hpts. Everything else will just have to retransmit
		 * with the timer.
		 *
		 * In any case, we do not want to loop around for another
		 * send without a good reason.
		 */
		sendalot = 0;
		switch (error) {
		case EPERM:
			tp->t_flags &= ~TF_FORCEDATA;
			tp->t_softerror = error;
			return (error);
		case ENOBUFS:
			if (slot == 0) {
				/*
				 * Pace us right away to retry in a some
				 * time
				 */
				slot = 1 + rack->rc_enobuf;
				if (rack->rc_enobuf < 255)
					rack->rc_enobuf++;
				if (slot > (rack->rc_rack_rtt / 2)) {
					slot = rack->rc_rack_rtt / 2;
				}
				if (slot < 10)
					slot = 10;
			}
			counter_u64_add(rack_saw_enobuf, 1);
			error = 0;
			goto enobufs;
		case EMSGSIZE:
			/*
			 * For some reason the interface we used initially
			 * to send segments changed to another or lowered
			 * its MTU. If TSO was active we either got an
			 * interface without TSO capabilits or TSO was
			 * turned off. If we obtained mtu from ip_output()
			 * then update it and try again.
			 */
			if (tso)
				tp->t_flags &= ~TF_TSO;
			if (mtu != 0) {
				tcp_mss_update(tp, -1, mtu, NULL, NULL);
				goto again;
			}
			slot = 10;
			rack_start_hpts_timer(rack, tp, cts, __LINE__, slot, 0, 1);
			tp->t_flags &= ~TF_FORCEDATA;
			return (error);
		case ENETUNREACH:
			counter_u64_add(rack_saw_enetunreach, 1);
		case EHOSTDOWN:
		case EHOSTUNREACH:
		case ENETDOWN:
			if (TCPS_HAVERCVDSYN(tp->t_state)) {
				tp->t_softerror = error;
			}
			/* FALLTHROUGH */
		default:
			slot = 10;
			rack_start_hpts_timer(rack, tp, cts, __LINE__, slot, 0, 1);
			tp->t_flags &= ~TF_FORCEDATA;
			return (error);
		}
	} else {
		rack->rc_enobuf = 0;
	}
	TCPSTAT_INC(tcps_sndtotal);

	/*
	 * Data sent (as far as we can tell). If this advertises a larger
	 * window than any other segment, then remember the size of the
	 * advertised window. Any pending ACK has now been sent.
	 */
	if (recwin > 0 && SEQ_GT(tp->rcv_nxt + recwin, tp->rcv_adv))
		tp->rcv_adv = tp->rcv_nxt + recwin;
	tp->last_ack_sent = tp->rcv_nxt;
	tp->t_flags &= ~(TF_ACKNOW | TF_DELACK);
enobufs:
	rack->r_tlp_running = 0;
	if ((flags & TH_RST) || (would_have_fin == 1)) {
		/*
		 * We don't send again after a RST. We also do *not* send
		 * again if we would have had a find, but now have
		 * outstanding data.
		 */
		slot = 0;
		sendalot = 0;
	}
	if (slot) {
		/* set the rack tcb into the slot N */
		counter_u64_add(rack_paced_segments, 1);
	} else if (sendalot) {
		if (len)
			counter_u64_add(rack_unpaced_segments, 1);
		sack_rxmit = 0;
		tp->t_flags &= ~TF_FORCEDATA;
		goto again;
	} else if (len) {
		counter_u64_add(rack_unpaced_segments, 1);
	}
	tp->t_flags &= ~TF_FORCEDATA;
	rack_start_hpts_timer(rack, tp, cts, __LINE__, slot, tot_len_this_send, 1);
	return (error);
}

/*
 * rack_ctloutput() must drop the inpcb lock before performing copyin on
 * socket option arguments.  When it re-acquires the lock after the copy, it
 * has to revalidate that the connection is still valid for the socket
 * option.
 */
static int
rack_set_sockopt(struct socket *so, struct sockopt *sopt,
    struct inpcb *inp, struct tcpcb *tp, struct tcp_rack *rack)
{
	int32_t error = 0, optval;

	switch (sopt->sopt_name) {
	case TCP_RACK_PROP_RATE:
	case TCP_RACK_PROP:
	case TCP_RACK_TLP_REDUCE:
	case TCP_RACK_EARLY_RECOV:
	case TCP_RACK_PACE_ALWAYS:
	case TCP_DELACK:
	case TCP_RACK_PACE_REDUCE:
	case TCP_RACK_PACE_MAX_SEG:
	case TCP_RACK_PRR_SENDALOT:
	case TCP_RACK_MIN_TO:
	case TCP_RACK_EARLY_SEG:
	case TCP_RACK_REORD_THRESH:
	case TCP_RACK_REORD_FADE:
	case TCP_RACK_TLP_THRESH:
	case TCP_RACK_PKT_DELAY:
	case TCP_RACK_TLP_USE:
	case TCP_RACK_TLP_INC_VAR:
	case TCP_RACK_IDLE_REDUCE_HIGH:
	case TCP_RACK_MIN_PACE:
	case TCP_RACK_MIN_PACE_SEG:
	case TCP_BBR_RACK_RTT_USE:
	case TCP_DATA_AFTER_CLOSE:
		break;
	default:
		return (tcp_default_ctloutput(so, sopt, inp, tp));
		break;
	}
	INP_WUNLOCK(inp);
	error = sooptcopyin(sopt, &optval, sizeof(optval), sizeof(optval));
	if (error)
		return (error);
	INP_WLOCK(inp);
	if (inp->inp_flags & (INP_TIMEWAIT | INP_DROPPED)) {
		INP_WUNLOCK(inp);
		return (ECONNRESET);
	}
	tp = intotcpcb(inp);
	rack = (struct tcp_rack *)tp->t_fb_ptr;
	switch (sopt->sopt_name) {
	case TCP_RACK_PROP_RATE:
		if ((optval <= 0) || (optval >= 100)) {
			error = EINVAL;
			break;
		}
		RACK_OPTS_INC(tcp_rack_prop_rate);
		rack->r_ctl.rc_prop_rate = optval;
		break;
	case TCP_RACK_TLP_USE:
		if ((optval < TLP_USE_ID) || (optval > TLP_USE_TWO_TWO)) {
			error = EINVAL;
			break;
		}
		RACK_OPTS_INC(tcp_tlp_use);
		rack->rack_tlp_threshold_use = optval;
		break;
	case TCP_RACK_PROP:
		/* RACK proportional rate reduction (bool) */
		RACK_OPTS_INC(tcp_rack_prop);
		rack->r_ctl.rc_prop_reduce = optval;
		break;
	case TCP_RACK_TLP_REDUCE:
		/* RACK TLP cwnd reduction (bool) */
		RACK_OPTS_INC(tcp_rack_tlp_reduce);
		rack->r_ctl.rc_tlp_cwnd_reduce = optval;
		break;
	case TCP_RACK_EARLY_RECOV:
		/* Should recovery happen early (bool) */
		RACK_OPTS_INC(tcp_rack_early_recov);
		rack->r_ctl.rc_early_recovery = optval;
		break;
	case TCP_RACK_PACE_ALWAYS:
		/* Use the always pace method (bool)  */
		RACK_OPTS_INC(tcp_rack_pace_always);
		if (optval > 0)
			rack->rc_always_pace = 1;
		else
			rack->rc_always_pace = 0;
		break;
	case TCP_RACK_PACE_REDUCE:
		/* RACK Hptsi reduction factor (divisor) */
		RACK_OPTS_INC(tcp_rack_pace_reduce);
		if (optval)
			/* Must be non-zero */
			rack->rc_pace_reduce = optval;
		else
			error = EINVAL;
		break;
	case TCP_RACK_PACE_MAX_SEG:
		/* Max segments in a pace */
		RACK_OPTS_INC(tcp_rack_max_seg);
		rack->rc_pace_max_segs = optval;
		break;
	case TCP_RACK_PRR_SENDALOT:
		/* Allow PRR to send more than one seg */
		RACK_OPTS_INC(tcp_rack_prr_sendalot);
		rack->r_ctl.rc_prr_sendalot = optval;
		break;
	case TCP_RACK_MIN_TO:
		/* Minimum time between rack t-o's in ms */
		RACK_OPTS_INC(tcp_rack_min_to);
		rack->r_ctl.rc_min_to = optval;
		break;
	case TCP_RACK_EARLY_SEG:
		/* If early recovery max segments */
		RACK_OPTS_INC(tcp_rack_early_seg);
		rack->r_ctl.rc_early_recovery_segs = optval;
		break;
	case TCP_RACK_REORD_THRESH:
		/* RACK reorder threshold (shift amount) */
		RACK_OPTS_INC(tcp_rack_reord_thresh);
		if ((optval > 0) && (optval < 31))
			rack->r_ctl.rc_reorder_shift = optval;
		else
			error = EINVAL;
		break;
	case TCP_RACK_REORD_FADE:
		/* Does reordering fade after ms time */
		RACK_OPTS_INC(tcp_rack_reord_fade);
		rack->r_ctl.rc_reorder_fade = optval;
		break;
	case TCP_RACK_TLP_THRESH:
		/* RACK TLP theshold i.e. srtt+(srtt/N) */
		RACK_OPTS_INC(tcp_rack_tlp_thresh);
		if (optval)
			rack->r_ctl.rc_tlp_threshold = optval;
		else
			error = EINVAL;
		break;
	case TCP_RACK_PKT_DELAY:
		/* RACK added ms i.e. rack-rtt + reord + N */
		RACK_OPTS_INC(tcp_rack_pkt_delay);
		rack->r_ctl.rc_pkt_delay = optval;
		break;
	case TCP_RACK_TLP_INC_VAR:
		/* Does TLP include rtt variance in t-o */
		RACK_OPTS_INC(tcp_rack_tlp_inc_var);
		rack->r_ctl.rc_prr_inc_var = optval;
		break;
	case TCP_RACK_IDLE_REDUCE_HIGH:
		RACK_OPTS_INC(tcp_rack_idle_reduce_high);
		if (optval)
			rack->r_idle_reduce_largest = 1;
		else
			rack->r_idle_reduce_largest = 0;
		break;
	case TCP_DELACK:
		if (optval == 0)
			tp->t_delayed_ack = 0;
		else
			tp->t_delayed_ack = 1;
		if (tp->t_flags & TF_DELACK) {
			tp->t_flags &= ~TF_DELACK;
			tp->t_flags |= TF_ACKNOW;
			rack_output(tp);
		}
		break;
	case TCP_RACK_MIN_PACE:
		RACK_OPTS_INC(tcp_rack_min_pace);
		if (optval > 3)
			rack->r_enforce_min_pace = 3;
		else
			rack->r_enforce_min_pace = optval;
		break;
	case TCP_RACK_MIN_PACE_SEG:
		RACK_OPTS_INC(tcp_rack_min_pace_seg);
		if (optval >= 16)
			rack->r_min_pace_seg_thresh = 15;
		else
			rack->r_min_pace_seg_thresh = optval;
		break;
	case TCP_BBR_RACK_RTT_USE:
		if ((optval != USE_RTT_HIGH) &&
		    (optval != USE_RTT_LOW) &&
		    (optval != USE_RTT_AVG))
			error = EINVAL;
		else
			rack->r_ctl.rc_rate_sample_method = optval;
		break;
	case TCP_DATA_AFTER_CLOSE:
		if (optval)
			rack->rc_allow_data_af_clo = 1;
		else
			rack->rc_allow_data_af_clo = 0;
		break;
	default:
		return (tcp_default_ctloutput(so, sopt, inp, tp));
		break;
	}
#ifdef NETFLIX_STATS
	tcp_log_socket_option(tp, sopt->sopt_name, optval, error);
#endif
	INP_WUNLOCK(inp);
	return (error);
}

static int
rack_get_sockopt(struct socket *so, struct sockopt *sopt,
    struct inpcb *inp, struct tcpcb *tp, struct tcp_rack *rack)
{
	int32_t error, optval;

	/*
	 * Because all our options are either boolean or an int, we can just
	 * pull everything into optval and then unlock and copy. If we ever
	 * add a option that is not a int, then this will have quite an
	 * impact to this routine.
	 */
	switch (sopt->sopt_name) {
	case TCP_RACK_PROP_RATE:
		optval = rack->r_ctl.rc_prop_rate;
		break;
	case TCP_RACK_PROP:
		/* RACK proportional rate reduction (bool) */
		optval = rack->r_ctl.rc_prop_reduce;
		break;
	case TCP_RACK_TLP_REDUCE:
		/* RACK TLP cwnd reduction (bool) */
		optval = rack->r_ctl.rc_tlp_cwnd_reduce;
		break;
	case TCP_RACK_EARLY_RECOV:
		/* Should recovery happen early (bool) */
		optval = rack->r_ctl.rc_early_recovery;
		break;
	case TCP_RACK_PACE_REDUCE:
		/* RACK Hptsi reduction factor (divisor) */
		optval = rack->rc_pace_reduce;
		break;
	case TCP_RACK_PACE_MAX_SEG:
		/* Max segments in a pace */
		optval = rack->rc_pace_max_segs;
		break;
	case TCP_RACK_PACE_ALWAYS:
		/* Use the always pace method */
		optval = rack->rc_always_pace;
		break;
	case TCP_RACK_PRR_SENDALOT:
		/* Allow PRR to send more than one seg */
		optval = rack->r_ctl.rc_prr_sendalot;
		break;
	case TCP_RACK_MIN_TO:
		/* Minimum time between rack t-o's in ms */
		optval = rack->r_ctl.rc_min_to;
		break;
	case TCP_RACK_EARLY_SEG:
		/* If early recovery max segments */
		optval = rack->r_ctl.rc_early_recovery_segs;
		break;
	case TCP_RACK_REORD_THRESH:
		/* RACK reorder threshold (shift amount) */
		optval = rack->r_ctl.rc_reorder_shift;
		break;
	case TCP_RACK_REORD_FADE:
		/* Does reordering fade after ms time */
		optval = rack->r_ctl.rc_reorder_fade;
		break;
	case TCP_RACK_TLP_THRESH:
		/* RACK TLP theshold i.e. srtt+(srtt/N) */
		optval = rack->r_ctl.rc_tlp_threshold;
		break;
	case TCP_RACK_PKT_DELAY:
		/* RACK added ms i.e. rack-rtt + reord + N */
		optval = rack->r_ctl.rc_pkt_delay;
		break;
	case TCP_RACK_TLP_USE:
		optval = rack->rack_tlp_threshold_use;
		break;
	case TCP_RACK_TLP_INC_VAR:
		/* Does TLP include rtt variance in t-o */
		optval = rack->r_ctl.rc_prr_inc_var;
		break;
	case TCP_RACK_IDLE_REDUCE_HIGH:
		optval = rack->r_idle_reduce_largest;
		break;
	case TCP_RACK_MIN_PACE:
		optval = rack->r_enforce_min_pace;
		break;
	case TCP_RACK_MIN_PACE_SEG:
		optval = rack->r_min_pace_seg_thresh;
		break;
	case TCP_BBR_RACK_RTT_USE:
		optval = rack->r_ctl.rc_rate_sample_method;
		break;
	case TCP_DELACK:
		optval = tp->t_delayed_ack;
		break;
	case TCP_DATA_AFTER_CLOSE:
		optval = rack->rc_allow_data_af_clo;
		break;
	default:
		return (tcp_default_ctloutput(so, sopt, inp, tp));
		break;
	}
	INP_WUNLOCK(inp);
	error = sooptcopyout(sopt, &optval, sizeof optval);
	return (error);
}

static int
rack_ctloutput(struct socket *so, struct sockopt *sopt, struct inpcb *inp, struct tcpcb *tp)
{
	int32_t error = EINVAL;
	struct tcp_rack *rack;

	rack = (struct tcp_rack *)tp->t_fb_ptr;
	if (rack == NULL) {
		/* Huh? */
		goto out;
	}
	if (sopt->sopt_dir == SOPT_SET) {
		return (rack_set_sockopt(so, sopt, inp, tp, rack));
	} else if (sopt->sopt_dir == SOPT_GET) {
		return (rack_get_sockopt(so, sopt, inp, tp, rack));
	}
out:
	INP_WUNLOCK(inp);
	return (error);
}


struct tcp_function_block __tcp_rack = {
	.tfb_tcp_block_name = __XSTRING(STACKNAME),
	.tfb_tcp_output = rack_output,
	.tfb_tcp_do_segment = rack_do_segment,
	.tfb_tcp_hpts_do_segment = rack_hpts_do_segment,
	.tfb_tcp_ctloutput = rack_ctloutput,
	.tfb_tcp_fb_init = rack_init,
	.tfb_tcp_fb_fini = rack_fini,
	.tfb_tcp_timer_stop_all = rack_stopall,
	.tfb_tcp_timer_activate = rack_timer_activate,
	.tfb_tcp_timer_active = rack_timer_active,
	.tfb_tcp_timer_stop = rack_timer_stop,
	.tfb_tcp_rexmit_tmr = rack_remxt_tmr,
	.tfb_tcp_handoff_ok = rack_handoff_ok
};

static const char *rack_stack_names[] = {
	__XSTRING(STACKNAME),
#ifdef STACKALIAS
	__XSTRING(STACKALIAS),
#endif
};

static int
rack_ctor(void *mem, int32_t size, void *arg, int32_t how)
{
	memset(mem, 0, size);
	return (0);
}

static void
rack_dtor(void *mem, int32_t size, void *arg)
{

}

static bool rack_mod_inited = false;

static int
tcp_addrack(module_t mod, int32_t type, void *data)
{
	int32_t err = 0;
	int num_stacks;

	switch (type) {
	case MOD_LOAD:
		rack_zone = uma_zcreate(__XSTRING(MODNAME) "_map",
		    sizeof(struct rack_sendmap),
		    rack_ctor, rack_dtor, NULL, NULL, UMA_ALIGN_PTR, 0);

		rack_pcb_zone = uma_zcreate(__XSTRING(MODNAME) "_pcb",
		    sizeof(struct tcp_rack),
		    rack_ctor, NULL, NULL, NULL, UMA_ALIGN_CACHE, 0);

		sysctl_ctx_init(&rack_sysctl_ctx);
		rack_sysctl_root = SYSCTL_ADD_NODE(&rack_sysctl_ctx,
		    SYSCTL_STATIC_CHILDREN(_net_inet_tcp),
		    OID_AUTO,
		    __XSTRING(STACKNAME),
		    CTLFLAG_RW, 0,
		    "");
		if (rack_sysctl_root == NULL) {
			printf("Failed to add sysctl node\n");
			err = EFAULT;
			goto free_uma;
		}
		rack_init_sysctls();
		num_stacks = nitems(rack_stack_names);
		err = register_tcp_functions_as_names(&__tcp_rack, M_WAITOK,
		    rack_stack_names, &num_stacks);
		if (err) {
			printf("Failed to register %s stack name for "
			    "%s module\n", rack_stack_names[num_stacks],
			    __XSTRING(MODNAME));
			sysctl_ctx_free(&rack_sysctl_ctx);
free_uma:
			uma_zdestroy(rack_zone);
			uma_zdestroy(rack_pcb_zone);
			rack_counter_destroy();
			printf("Failed to register rack module -- err:%d\n", err);
			return (err);
		}
		rack_mod_inited = true;
		break;
	case MOD_QUIESCE:
		err = deregister_tcp_functions(&__tcp_rack, true, false);
		break;
	case MOD_UNLOAD:
		err = deregister_tcp_functions(&__tcp_rack, false, true);
		if (err == EBUSY)
			break;
		if (rack_mod_inited) {
			uma_zdestroy(rack_zone);
			uma_zdestroy(rack_pcb_zone);
			sysctl_ctx_free(&rack_sysctl_ctx);
			rack_counter_destroy();
			rack_mod_inited = false;
		}
		err = 0;
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (err);
}

static moduledata_t tcp_rack = {
	.name = __XSTRING(MODNAME),
	.evhand = tcp_addrack,
	.priv = 0
};

MODULE_VERSION(MODNAME, 1);
DECLARE_MODULE(MODNAME, tcp_rack, SI_SUB_PROTO_DOMAIN, SI_ORDER_ANY);
MODULE_DEPEND(MODNAME, tcphpts, 1, 1, 1);
