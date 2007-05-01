/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2003 International Business Machines Corp.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * This module provides the abstraction for an SCTP tranport representing
 * a remote transport address.  For local transport addresses, we just use
 * union sctp_addr.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Xingang Guo           <xingang.guo@intel.com>
 *    Hui Huang             <hui.huang@nokia.com>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *    Ardelle Fan	    <ardelle.fan@intel.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/types.h>
#include <linux/random.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

/* 1st Level Abstractions.  */

/* Initialize a new transport from provided memory.  */
static struct sctp_transport *sctp_transport_init(struct sctp_transport *peer,
						  const union sctp_addr *addr,
						  gfp_t gfp)
{
	/* Copy in the address.  */
	peer->ipaddr = *addr;
	peer->af_specific = sctp_get_af_specific(addr->sa.sa_family);
	peer->asoc = NULL;

	peer->dst = NULL;
	memset(&peer->saddr, 0, sizeof(union sctp_addr));

	/* From 6.3.1 RTO Calculation:
	 *
	 * C1) Until an RTT measurement has been made for a packet sent to the
	 * given destination transport address, set RTO to the protocol
	 * parameter 'RTO.Initial'.
	 */
	peer->rtt = 0;
	peer->rto = msecs_to_jiffies(sctp_rto_initial);
	peer->rttvar = 0;
	peer->srtt = 0;
	peer->rto_pending = 0;

	peer->last_time_heard = jiffies;
	peer->last_time_used = jiffies;
	peer->last_time_ecne_reduced = jiffies;

	peer->init_sent_count = 0;

	peer->param_flags = SPP_HB_DISABLE |
			    SPP_PMTUD_ENABLE |
			    SPP_SACKDELAY_ENABLE;
	peer->hbinterval  = 0;

	/* Initialize the default path max_retrans.  */
	peer->pathmaxrxt  = sctp_max_retrans_path;
	peer->error_count = 0;

	INIT_LIST_HEAD(&peer->transmitted);
	INIT_LIST_HEAD(&peer->send_ready);
	INIT_LIST_HEAD(&peer->transports);

	/* Set up the retransmission timer.  */
	init_timer(&peer->T3_rtx_timer);
	peer->T3_rtx_timer.function = sctp_generate_t3_rtx_event;
	peer->T3_rtx_timer.data = (unsigned long)peer;

	/* Set up the heartbeat timer. */
	init_timer(&peer->hb_timer);
	peer->hb_timer.function = sctp_generate_heartbeat_event;
	peer->hb_timer.data = (unsigned long)peer;

	/* Initialize the 64-bit random nonce sent with heartbeat. */
	get_random_bytes(&peer->hb_nonce, sizeof(peer->hb_nonce));

	atomic_set(&peer->refcnt, 1);
	peer->dead = 0;

	peer->malloced = 0;

	/* Initialize the state information for SFR-CACC */
	peer->cacc.changeover_active = 0;
	peer->cacc.cycling_changeover = 0;
	peer->cacc.next_tsn_at_change = 0;
	peer->cacc.cacc_saw_newack = 0;

	return peer;
}

/* Allocate and initialize a new transport.  */
struct sctp_transport *sctp_transport_new(const union sctp_addr *addr,
					  gfp_t gfp)
{
	struct sctp_transport *transport;

	transport = t_new(struct sctp_transport, gfp);
	if (!transport)
		goto fail;

	if (!sctp_transport_init(transport, addr, gfp))
		goto fail_init;

	transport->malloced = 1;
	SCTP_DBG_OBJCNT_INC(transport);

	return transport;

fail_init:
	kfree(transport);

fail:
	return NULL;
}

/* This transport is no longer needed.  Free up if possible, or
 * delay until it last reference count.
 */
void sctp_transport_free(struct sctp_transport *transport)
{
	transport->dead = 1;

	/* Try to delete the heartbeat timer.  */
	if (del_timer(&transport->hb_timer))
		sctp_transport_put(transport);

	/* Delete the T3_rtx timer if it's active.
	 * There is no point in not doing this now and letting
	 * structure hang around in memory since we know
	 * the tranport is going away.
	 */
	if (timer_pending(&transport->T3_rtx_timer) &&
	    del_timer(&transport->T3_rtx_timer))
		sctp_transport_put(transport);


	sctp_transport_put(transport);
}

/* Destroy the transport data structure.
 * Assumes there are no more users of this structure.
 */
static void sctp_transport_destroy(struct sctp_transport *transport)
{
	SCTP_ASSERT(transport->dead, "Transport is not dead", return);

	if (transport->asoc)
		sctp_association_put(transport->asoc);

	sctp_packet_free(&transport->packet);

	dst_release(transport->dst);
	kfree(transport);
	SCTP_DBG_OBJCNT_DEC(transport);
}

/* Start T3_rtx timer if it is not already running and update the heartbeat
 * timer.  This routine is called every time a DATA chunk is sent.
 */
void sctp_transport_reset_timers(struct sctp_transport *transport)
{
	/* RFC 2960 6.3.2 Retransmission Timer Rules
	 *
	 * R1) Every time a DATA chunk is sent to any address(including a
	 * retransmission), if the T3-rtx timer of that address is not running
	 * start it running so that it will expire after the RTO of that
	 * address.
	 */

	if (!timer_pending(&transport->T3_rtx_timer))
		if (!mod_timer(&transport->T3_rtx_timer,
			       jiffies + transport->rto))
			sctp_transport_hold(transport);

	/* When a data chunk is sent, reset the heartbeat interval.  */
	if (!mod_timer(&transport->hb_timer,
		       sctp_transport_timeout(transport)))
	    sctp_transport_hold(transport);
}

/* This transport has been assigned to an association.
 * Initialize fields from the association or from the sock itself.
 * Register the reference count in the association.
 */
void sctp_transport_set_owner(struct sctp_transport *transport,
			      struct sctp_association *asoc)
{
	transport->asoc = asoc;
	sctp_association_hold(asoc);
}

/* Initialize the pmtu of a transport. */
void sctp_transport_pmtu(struct sctp_transport *transport)
{
	struct dst_entry *dst;

	dst = transport->af_specific->get_dst(NULL, &transport->ipaddr, NULL);

	if (dst) {
		transport->pathmtu = dst_mtu(dst);
		dst_release(dst);
	} else
		transport->pathmtu = SCTP_DEFAULT_MAXSEGMENT;
}

/* Caches the dst entry and source address for a transport's destination
 * address.
 */
void sctp_transport_route(struct sctp_transport *transport,
			  union sctp_addr *saddr, struct sctp_sock *opt)
{
	struct sctp_association *asoc = transport->asoc;
	struct sctp_af *af = transport->af_specific;
	union sctp_addr *daddr = &transport->ipaddr;
	struct dst_entry *dst;

	dst = af->get_dst(asoc, daddr, saddr);

	if (saddr)
		memcpy(&transport->saddr, saddr, sizeof(union sctp_addr));
	else
		af->get_saddr(asoc, dst, daddr, &transport->saddr);

	transport->dst = dst;
	if ((transport->param_flags & SPP_PMTUD_DISABLE) && transport->pathmtu) {
		return;
	}
	if (dst) {
		transport->pathmtu = dst_mtu(dst);

		/* Initialize sk->sk_rcv_saddr, if the transport is the
		 * association's active path for getsockname().
		 */
		if (asoc && (transport == asoc->peer.active_path))
			opt->pf->af->to_sk_saddr(&transport->saddr,
						 asoc->base.sk);
	} else
		transport->pathmtu = SCTP_DEFAULT_MAXSEGMENT;
}

/* Hold a reference to a transport.  */
void sctp_transport_hold(struct sctp_transport *transport)
{
	atomic_inc(&transport->refcnt);
}

/* Release a reference to a transport and clean up
 * if there are no more references.
 */
void sctp_transport_put(struct sctp_transport *transport)
{
	if (atomic_dec_and_test(&transport->refcnt))
		sctp_transport_destroy(transport);
}

/* Update transport's RTO based on the newly calculated RTT. */
void sctp_transport_update_rto(struct sctp_transport *tp, __u32 rtt)
{
	/* Check for valid transport.  */
	SCTP_ASSERT(tp, "NULL transport", return);

	/* We should not be doing any RTO updates unless rto_pending is set.  */
	SCTP_ASSERT(tp->rto_pending, "rto_pending not set", return);

	if (tp->rttvar || tp->srtt) {
		/* 6.3.1 C3) When a new RTT measurement R' is made, set
		 * RTTVAR <- (1 - RTO.Beta) * RTTVAR + RTO.Beta * |SRTT - R'|
		 * SRTT <- (1 - RTO.Alpha) * SRTT + RTO.Alpha * R'
		 */

		/* Note:  The above algorithm has been rewritten to
		 * express rto_beta and rto_alpha as inverse powers
		 * of two.
		 * For example, assuming the default value of RTO.Alpha of
		 * 1/8, rto_alpha would be expressed as 3.
		 */
		tp->rttvar = tp->rttvar - (tp->rttvar >> sctp_rto_beta)
			+ ((abs(tp->srtt - rtt)) >> sctp_rto_beta);
		tp->srtt = tp->srtt - (tp->srtt >> sctp_rto_alpha)
			+ (rtt >> sctp_rto_alpha);
	} else {
		/* 6.3.1 C2) When the first RTT measurement R is made, set
		 * SRTT <- R, RTTVAR <- R/2.
		 */
		tp->srtt = rtt;
		tp->rttvar = rtt >> 1;
	}

	/* 6.3.1 G1) Whenever RTTVAR is computed, if RTTVAR = 0, then
	 * adjust RTTVAR <- G, where G is the CLOCK GRANULARITY.
	 */
	if (tp->rttvar == 0)
		tp->rttvar = SCTP_CLOCK_GRANULARITY;

	/* 6.3.1 C3) After the computation, update RTO <- SRTT + 4 * RTTVAR. */
	tp->rto = tp->srtt + (tp->rttvar << 2);

	/* 6.3.1 C6) Whenever RTO is computed, if it is less than RTO.Min
	 * seconds then it is rounded up to RTO.Min seconds.
	 */
	if (tp->rto < tp->asoc->rto_min)
		tp->rto = tp->asoc->rto_min;

	/* 6.3.1 C7) A maximum value may be placed on RTO provided it is
	 * at least RTO.max seconds.
	 */
	if (tp->rto > tp->asoc->rto_max)
		tp->rto = tp->asoc->rto_max;

	tp->rtt = rtt;

	/* Reset rto_pending so that a new RTT measurement is started when a
	 * new data chunk is sent.
	 */
	tp->rto_pending = 0;

	SCTP_DEBUG_PRINTK("%s: transport: %p, rtt: %d, srtt: %d "
			  "rttvar: %d, rto: %ld\n", __FUNCTION__,
			  tp, rtt, tp->srtt, tp->rttvar, tp->rto);
}

/* This routine updates the transport's cwnd and partial_bytes_acked
 * parameters based on the bytes acked in the received SACK.
 */
void sctp_transport_raise_cwnd(struct sctp_transport *transport,
			       __u32 sack_ctsn, __u32 bytes_acked)
{
	__u32 cwnd, ssthresh, flight_size, pba, pmtu;

	cwnd = transport->cwnd;
	flight_size = transport->flight_size;

	/* The appropriate cwnd increase algorithm is performed if, and only
	 * if the cumulative TSN has advanced and the congestion window is
	 * being fully utilized.
	 */
	if ((transport->asoc->ctsn_ack_point >= sack_ctsn) ||
	    (flight_size < cwnd))
		return;

	ssthresh = transport->ssthresh;
	pba = transport->partial_bytes_acked;
	pmtu = transport->asoc->pathmtu;

	if (cwnd <= ssthresh) {
		/* RFC 2960 7.2.1, sctpimpguide-05 2.14.2 When cwnd is less
		 * than or equal to ssthresh an SCTP endpoint MUST use the
		 * slow start algorithm to increase cwnd only if the current
		 * congestion window is being fully utilized and an incoming
		 * SACK advances the Cumulative TSN Ack Point. Only when these
		 * two conditions are met can the cwnd be increased otherwise
		 * the cwnd MUST not be increased. If these conditions are met
		 * then cwnd MUST be increased by at most the lesser of
		 * 1) the total size of the previously outstanding DATA
		 * chunk(s) acknowledged, and 2) the destination's path MTU.
		 */
		if (bytes_acked > pmtu)
			cwnd += pmtu;
		else
			cwnd += bytes_acked;
		SCTP_DEBUG_PRINTK("%s: SLOW START: transport: %p, "
				  "bytes_acked: %d, cwnd: %d, ssthresh: %d, "
				  "flight_size: %d, pba: %d\n",
				  __FUNCTION__,
				  transport, bytes_acked, cwnd,
				  ssthresh, flight_size, pba);
	} else {
		/* RFC 2960 7.2.2 Whenever cwnd is greater than ssthresh,
		 * upon each SACK arrival that advances the Cumulative TSN Ack
		 * Point, increase partial_bytes_acked by the total number of
		 * bytes of all new chunks acknowledged in that SACK including
		 * chunks acknowledged by the new Cumulative TSN Ack and by
		 * Gap Ack Blocks.
		 *
		 * When partial_bytes_acked is equal to or greater than cwnd
		 * and before the arrival of the SACK the sender had cwnd or
		 * more bytes of data outstanding (i.e., before arrival of the
		 * SACK, flightsize was greater than or equal to cwnd),
		 * increase cwnd by MTU, and reset partial_bytes_acked to
		 * (partial_bytes_acked - cwnd).
		 */
		pba += bytes_acked;
		if (pba >= cwnd) {
			cwnd += pmtu;
			pba = ((cwnd < pba) ? (pba - cwnd) : 0);
		}
		SCTP_DEBUG_PRINTK("%s: CONGESTION AVOIDANCE: "
				  "transport: %p, bytes_acked: %d, cwnd: %d, "
				  "ssthresh: %d, flight_size: %d, pba: %d\n",
				  __FUNCTION__,
				  transport, bytes_acked, cwnd,
				  ssthresh, flight_size, pba);
	}

	transport->cwnd = cwnd;
	transport->partial_bytes_acked = pba;
}

/* This routine is used to lower the transport's cwnd when congestion is
 * detected.
 */
void sctp_transport_lower_cwnd(struct sctp_transport *transport,
			       sctp_lower_cwnd_t reason)
{
	switch (reason) {
	case SCTP_LOWER_CWND_T3_RTX:
		/* RFC 2960 Section 7.2.3, sctpimpguide
		 * When the T3-rtx timer expires on an address, SCTP should
		 * perform slow start by:
		 *      ssthresh = max(cwnd/2, 4*MTU)
		 *      cwnd = 1*MTU
		 *      partial_bytes_acked = 0
		 */
		transport->ssthresh = max(transport->cwnd/2,
					  4*transport->asoc->pathmtu);
		transport->cwnd = transport->asoc->pathmtu;
		break;

	case SCTP_LOWER_CWND_FAST_RTX:
		/* RFC 2960 7.2.4 Adjust the ssthresh and cwnd of the
		 * destination address(es) to which the missing DATA chunks
		 * were last sent, according to the formula described in
		 * Section 7.2.3.
		 *
		 * RFC 2960 7.2.3, sctpimpguide Upon detection of packet
		 * losses from SACK (see Section 7.2.4), An endpoint
		 * should do the following:
		 *      ssthresh = max(cwnd/2, 4*MTU)
		 *      cwnd = ssthresh
		 *      partial_bytes_acked = 0
		 */
		transport->ssthresh = max(transport->cwnd/2,
					  4*transport->asoc->pathmtu);
		transport->cwnd = transport->ssthresh;
		break;

	case SCTP_LOWER_CWND_ECNE:
		/* RFC 2481 Section 6.1.2.
		 * If the sender receives an ECN-Echo ACK packet
		 * then the sender knows that congestion was encountered in the
		 * network on the path from the sender to the receiver. The
		 * indication of congestion should be treated just as a
		 * congestion loss in non-ECN Capable TCP. That is, the TCP
		 * source halves the congestion window "cwnd" and reduces the
		 * slow start threshold "ssthresh".
		 * A critical condition is that TCP does not react to
		 * congestion indications more than once every window of
		 * data (or more loosely more than once every round-trip time).
		 */
		if ((jiffies - transport->last_time_ecne_reduced) >
		    transport->rtt) {
			transport->ssthresh = max(transport->cwnd/2,
						  4*transport->asoc->pathmtu);
			transport->cwnd = transport->ssthresh;
			transport->last_time_ecne_reduced = jiffies;
		}
		break;

	case SCTP_LOWER_CWND_INACTIVE:
		/* RFC 2960 Section 7.2.1, sctpimpguide
		 * When the endpoint does not transmit data on a given
		 * transport address, the cwnd of the transport address
		 * should be adjusted to max(cwnd/2, 4*MTU) per RTO.
		 * NOTE: Although the draft recommends that this check needs
		 * to be done every RTO interval, we do it every hearbeat
		 * interval.
		 */
		if ((jiffies - transport->last_time_used) > transport->rto)
			transport->cwnd = max(transport->cwnd/2,
						 4*transport->asoc->pathmtu);
		break;
	}

	transport->partial_bytes_acked = 0;
	SCTP_DEBUG_PRINTK("%s: transport: %p reason: %d cwnd: "
			  "%d ssthresh: %d\n", __FUNCTION__,
			  transport, reason,
			  transport->cwnd, transport->ssthresh);
}

/* What is the next timeout value for this transport? */
unsigned long sctp_transport_timeout(struct sctp_transport *t)
{
	unsigned long timeout;
	timeout = t->rto + sctp_jitter(t->rto);
	if (t->state != SCTP_UNCONFIRMED)
		timeout += t->hbinterval;
	timeout += jiffies;
	return timeout;
}

/* Reset transport variables to their initial values */
void sctp_transport_reset(struct sctp_transport *t)
{
	struct sctp_association *asoc = t->asoc;

	/* RFC 2960 (bis), Section 5.2.4
	 * All the congestion control parameters (e.g., cwnd, ssthresh)
	 * related to this peer MUST be reset to their initial values
	 * (see Section 6.2.1)
	 */
	t->cwnd = min(4*asoc->pathmtu, max_t(__u32, 2*asoc->pathmtu, 4380));
	t->ssthresh = asoc->peer.i.a_rwnd;
	t->rto = asoc->rto_initial;
	t->rtt = 0;
	t->srtt = 0;
	t->rttvar = 0;

	/* Reset these additional varibles so that we have a clean
	 * slate.
	 */
	t->partial_bytes_acked = 0;
	t->flight_size = 0;
	t->error_count = 0;
	t->rto_pending = 0;

	/* Initialize the state information for SFR-CACC */
	t->cacc.changeover_active = 0;
	t->cacc.cycling_changeover = 0;
	t->cacc.next_tsn_at_change = 0;
	t->cacc.cacc_saw_newack = 0;
}
