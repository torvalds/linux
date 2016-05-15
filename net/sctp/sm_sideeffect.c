/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 *
 * This file is part of the SCTP kernel implementation
 *
 * These functions work with the state functions in sctp_sm_statefuns.c
 * to implement that state operations.  These functions implement the
 * steps which require modifying existing data structures.
 *
 * This SCTP implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This SCTP implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Karl Knutson          <karl@athena.chicago.il.us>
 *    Jon Grimm             <jgrimm@austin.ibm.com>
 *    Hui Huang		    <hui.huang@nokia.com>
 *    Dajiang Zhang	    <dajiang.zhang@nokia.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *    Ardelle Fan	    <ardelle.fan@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/skbuff.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/ip.h>
#include <linux/gfp.h>
#include <net/sock.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>

static int sctp_cmd_interpreter(sctp_event_t event_type,
				sctp_subtype_t subtype,
				sctp_state_t state,
				struct sctp_endpoint *ep,
				struct sctp_association *asoc,
				void *event_arg,
				sctp_disposition_t status,
				sctp_cmd_seq_t *commands,
				gfp_t gfp);
static int sctp_side_effects(sctp_event_t event_type, sctp_subtype_t subtype,
			     sctp_state_t state,
			     struct sctp_endpoint *ep,
			     struct sctp_association **asoc,
			     void *event_arg,
			     sctp_disposition_t status,
			     sctp_cmd_seq_t *commands,
			     gfp_t gfp);

/********************************************************************
 * Helper functions
 ********************************************************************/

/* A helper function for delayed processing of INET ECN CE bit. */
static void sctp_do_ecn_ce_work(struct sctp_association *asoc,
				__u32 lowest_tsn)
{
	/* Save the TSN away for comparison when we receive CWR */

	asoc->last_ecne_tsn = lowest_tsn;
	asoc->need_ecne = 1;
}

/* Helper function for delayed processing of SCTP ECNE chunk.  */
/* RFC 2960 Appendix A
 *
 * RFC 2481 details a specific bit for a sender to send in
 * the header of its next outbound TCP segment to indicate to
 * its peer that it has reduced its congestion window.  This
 * is termed the CWR bit.  For SCTP the same indication is made
 * by including the CWR chunk.  This chunk contains one data
 * element, i.e. the TSN number that was sent in the ECNE chunk.
 * This element represents the lowest TSN number in the datagram
 * that was originally marked with the CE bit.
 */
static struct sctp_chunk *sctp_do_ecn_ecne_work(struct sctp_association *asoc,
					   __u32 lowest_tsn,
					   struct sctp_chunk *chunk)
{
	struct sctp_chunk *repl;

	/* Our previously transmitted packet ran into some congestion
	 * so we should take action by reducing cwnd and ssthresh
	 * and then ACK our peer that we we've done so by
	 * sending a CWR.
	 */

	/* First, try to determine if we want to actually lower
	 * our cwnd variables.  Only lower them if the ECNE looks more
	 * recent than the last response.
	 */
	if (TSN_lt(asoc->last_cwr_tsn, lowest_tsn)) {
		struct sctp_transport *transport;

		/* Find which transport's congestion variables
		 * need to be adjusted.
		 */
		transport = sctp_assoc_lookup_tsn(asoc, lowest_tsn);

		/* Update the congestion variables. */
		if (transport)
			sctp_transport_lower_cwnd(transport,
						  SCTP_LOWER_CWND_ECNE);
		asoc->last_cwr_tsn = lowest_tsn;
	}

	/* Always try to quiet the other end.  In case of lost CWR,
	 * resend last_cwr_tsn.
	 */
	repl = sctp_make_cwr(asoc, asoc->last_cwr_tsn, chunk);

	/* If we run out of memory, it will look like a lost CWR.  We'll
	 * get back in sync eventually.
	 */
	return repl;
}

/* Helper function to do delayed processing of ECN CWR chunk.  */
static void sctp_do_ecn_cwr_work(struct sctp_association *asoc,
				 __u32 lowest_tsn)
{
	/* Turn off ECNE getting auto-prepended to every outgoing
	 * packet
	 */
	asoc->need_ecne = 0;
}

/* Generate SACK if necessary.  We call this at the end of a packet.  */
static int sctp_gen_sack(struct sctp_association *asoc, int force,
			 sctp_cmd_seq_t *commands)
{
	__u32 ctsn, max_tsn_seen;
	struct sctp_chunk *sack;
	struct sctp_transport *trans = asoc->peer.last_data_from;
	int error = 0;

	if (force ||
	    (!trans && (asoc->param_flags & SPP_SACKDELAY_DISABLE)) ||
	    (trans && (trans->param_flags & SPP_SACKDELAY_DISABLE)))
		asoc->peer.sack_needed = 1;

	ctsn = sctp_tsnmap_get_ctsn(&asoc->peer.tsn_map);
	max_tsn_seen = sctp_tsnmap_get_max_tsn_seen(&asoc->peer.tsn_map);

	/* From 12.2 Parameters necessary per association (i.e. the TCB):
	 *
	 * Ack State : This flag indicates if the next received packet
	 * 	     : is to be responded to with a SACK. ...
	 *	     : When DATA chunks are out of order, SACK's
	 *           : are not delayed (see Section 6).
	 *
	 * [This is actually not mentioned in Section 6, but we
	 * implement it here anyway. --piggy]
	 */
	if (max_tsn_seen != ctsn)
		asoc->peer.sack_needed = 1;

	/* From 6.2  Acknowledgement on Reception of DATA Chunks:
	 *
	 * Section 4.2 of [RFC2581] SHOULD be followed. Specifically,
	 * an acknowledgement SHOULD be generated for at least every
	 * second packet (not every second DATA chunk) received, and
	 * SHOULD be generated within 200 ms of the arrival of any
	 * unacknowledged DATA chunk. ...
	 */
	if (!asoc->peer.sack_needed) {
		asoc->peer.sack_cnt++;

		/* Set the SACK delay timeout based on the
		 * SACK delay for the last transport
		 * data was received from, or the default
		 * for the association.
		 */
		if (trans) {
			/* We will need a SACK for the next packet.  */
			if (asoc->peer.sack_cnt >= trans->sackfreq - 1)
				asoc->peer.sack_needed = 1;

			asoc->timeouts[SCTP_EVENT_TIMEOUT_SACK] =
				trans->sackdelay;
		} else {
			/* We will need a SACK for the next packet.  */
			if (asoc->peer.sack_cnt >= asoc->sackfreq - 1)
				asoc->peer.sack_needed = 1;

			asoc->timeouts[SCTP_EVENT_TIMEOUT_SACK] =
				asoc->sackdelay;
		}

		/* Restart the SACK timer. */
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
				SCTP_TO(SCTP_EVENT_TIMEOUT_SACK));
	} else {
		__u32 old_a_rwnd = asoc->a_rwnd;

		asoc->a_rwnd = asoc->rwnd;
		sack = sctp_make_sack(asoc);
		if (!sack) {
			asoc->a_rwnd = old_a_rwnd;
			goto nomem;
		}

		asoc->peer.sack_needed = 0;
		asoc->peer.sack_cnt = 0;

		sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(sack));

		/* Stop the SACK timer.  */
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
				SCTP_TO(SCTP_EVENT_TIMEOUT_SACK));
	}

	return error;
nomem:
	error = -ENOMEM;
	return error;
}

/* When the T3-RTX timer expires, it calls this function to create the
 * relevant state machine event.
 */
void sctp_generate_t3_rtx_event(unsigned long peer)
{
	int error;
	struct sctp_transport *transport = (struct sctp_transport *) peer;
	struct sctp_association *asoc = transport->asoc;
	struct sock *sk = asoc->base.sk;
	struct net *net = sock_net(sk);

	/* Check whether a task is in the sock.  */

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		pr_debug("%s: sock is busy\n", __func__);

		/* Try again later.  */
		if (!mod_timer(&transport->T3_rtx_timer, jiffies + (HZ/20)))
			sctp_transport_hold(transport);
		goto out_unlock;
	}

	/* Run through the state machine.  */
	error = sctp_do_sm(net, SCTP_EVENT_T_TIMEOUT,
			   SCTP_ST_TIMEOUT(SCTP_EVENT_TIMEOUT_T3_RTX),
			   asoc->state,
			   asoc->ep, asoc,
			   transport, GFP_ATOMIC);

	if (error)
		sk->sk_err = -error;

out_unlock:
	bh_unlock_sock(sk);
	sctp_transport_put(transport);
}

/* This is a sa interface for producing timeout events.  It works
 * for timeouts which use the association as their parameter.
 */
static void sctp_generate_timeout_event(struct sctp_association *asoc,
					sctp_event_timeout_t timeout_type)
{
	struct sock *sk = asoc->base.sk;
	struct net *net = sock_net(sk);
	int error = 0;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		pr_debug("%s: sock is busy: timer %d\n", __func__,
			 timeout_type);

		/* Try again later.  */
		if (!mod_timer(&asoc->timers[timeout_type], jiffies + (HZ/20)))
			sctp_association_hold(asoc);
		goto out_unlock;
	}

	/* Is this association really dead and just waiting around for
	 * the timer to let go of the reference?
	 */
	if (asoc->base.dead)
		goto out_unlock;

	/* Run through the state machine.  */
	error = sctp_do_sm(net, SCTP_EVENT_T_TIMEOUT,
			   SCTP_ST_TIMEOUT(timeout_type),
			   asoc->state, asoc->ep, asoc,
			   (void *)timeout_type, GFP_ATOMIC);

	if (error)
		sk->sk_err = -error;

out_unlock:
	bh_unlock_sock(sk);
	sctp_association_put(asoc);
}

static void sctp_generate_t1_cookie_event(unsigned long data)
{
	struct sctp_association *asoc = (struct sctp_association *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_T1_COOKIE);
}

static void sctp_generate_t1_init_event(unsigned long data)
{
	struct sctp_association *asoc = (struct sctp_association *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_T1_INIT);
}

static void sctp_generate_t2_shutdown_event(unsigned long data)
{
	struct sctp_association *asoc = (struct sctp_association *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_T2_SHUTDOWN);
}

static void sctp_generate_t4_rto_event(unsigned long data)
{
	struct sctp_association *asoc = (struct sctp_association *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_T4_RTO);
}

static void sctp_generate_t5_shutdown_guard_event(unsigned long data)
{
	struct sctp_association *asoc = (struct sctp_association *)data;
	sctp_generate_timeout_event(asoc,
				    SCTP_EVENT_TIMEOUT_T5_SHUTDOWN_GUARD);

} /* sctp_generate_t5_shutdown_guard_event() */

static void sctp_generate_autoclose_event(unsigned long data)
{
	struct sctp_association *asoc = (struct sctp_association *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_AUTOCLOSE);
}

/* Generate a heart beat event.  If the sock is busy, reschedule.   Make
 * sure that the transport is still valid.
 */
void sctp_generate_heartbeat_event(unsigned long data)
{
	int error = 0;
	struct sctp_transport *transport = (struct sctp_transport *) data;
	struct sctp_association *asoc = transport->asoc;
	struct sock *sk = asoc->base.sk;
	struct net *net = sock_net(sk);
	u32 elapsed, timeout;

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		pr_debug("%s: sock is busy\n", __func__);

		/* Try again later.  */
		if (!mod_timer(&transport->hb_timer, jiffies + (HZ/20)))
			sctp_transport_hold(transport);
		goto out_unlock;
	}

	/* Check if we should still send the heartbeat or reschedule */
	elapsed = jiffies - transport->last_time_sent;
	timeout = sctp_transport_timeout(transport);
	if (elapsed < timeout) {
		elapsed = timeout - elapsed;
		if (!mod_timer(&transport->hb_timer, jiffies + elapsed))
			sctp_transport_hold(transport);
		goto out_unlock;
	}

	error = sctp_do_sm(net, SCTP_EVENT_T_TIMEOUT,
			   SCTP_ST_TIMEOUT(SCTP_EVENT_TIMEOUT_HEARTBEAT),
			   asoc->state, asoc->ep, asoc,
			   transport, GFP_ATOMIC);

	if (error)
		sk->sk_err = -error;

out_unlock:
	bh_unlock_sock(sk);
	sctp_transport_put(transport);
}

/* Handle the timeout of the ICMP protocol unreachable timer.  Trigger
 * the correct state machine transition that will close the association.
 */
void sctp_generate_proto_unreach_event(unsigned long data)
{
	struct sctp_transport *transport = (struct sctp_transport *) data;
	struct sctp_association *asoc = transport->asoc;
	struct sock *sk = asoc->base.sk;
	struct net *net = sock_net(sk);

	bh_lock_sock(sk);
	if (sock_owned_by_user(sk)) {
		pr_debug("%s: sock is busy\n", __func__);

		/* Try again later.  */
		if (!mod_timer(&transport->proto_unreach_timer,
				jiffies + (HZ/20)))
			sctp_association_hold(asoc);
		goto out_unlock;
	}

	/* Is this structure just waiting around for us to actually
	 * get destroyed?
	 */
	if (asoc->base.dead)
		goto out_unlock;

	sctp_do_sm(net, SCTP_EVENT_T_OTHER,
		   SCTP_ST_OTHER(SCTP_EVENT_ICMP_PROTO_UNREACH),
		   asoc->state, asoc->ep, asoc, transport, GFP_ATOMIC);

out_unlock:
	bh_unlock_sock(sk);
	sctp_association_put(asoc);
}


/* Inject a SACK Timeout event into the state machine.  */
static void sctp_generate_sack_event(unsigned long data)
{
	struct sctp_association *asoc = (struct sctp_association *) data;
	sctp_generate_timeout_event(asoc, SCTP_EVENT_TIMEOUT_SACK);
}

sctp_timer_event_t *sctp_timer_events[SCTP_NUM_TIMEOUT_TYPES] = {
	NULL,
	sctp_generate_t1_cookie_event,
	sctp_generate_t1_init_event,
	sctp_generate_t2_shutdown_event,
	NULL,
	sctp_generate_t4_rto_event,
	sctp_generate_t5_shutdown_guard_event,
	NULL,
	sctp_generate_sack_event,
	sctp_generate_autoclose_event,
};


/* RFC 2960 8.2 Path Failure Detection
 *
 * When its peer endpoint is multi-homed, an endpoint should keep a
 * error counter for each of the destination transport addresses of the
 * peer endpoint.
 *
 * Each time the T3-rtx timer expires on any address, or when a
 * HEARTBEAT sent to an idle address is not acknowledged within a RTO,
 * the error counter of that destination address will be incremented.
 * When the value in the error counter exceeds the protocol parameter
 * 'Path.Max.Retrans' of that destination address, the endpoint should
 * mark the destination transport address as inactive, and a
 * notification SHOULD be sent to the upper layer.
 *
 */
static void sctp_do_8_2_transport_strike(sctp_cmd_seq_t *commands,
					 struct sctp_association *asoc,
					 struct sctp_transport *transport,
					 int is_hb)
{
	struct net *net = sock_net(asoc->base.sk);

	/* The check for association's overall error counter exceeding the
	 * threshold is done in the state function.
	 */
	/* We are here due to a timer expiration.  If the timer was
	 * not a HEARTBEAT, then normal error tracking is done.
	 * If the timer was a heartbeat, we only increment error counts
	 * when we already have an outstanding HEARTBEAT that has not
	 * been acknowledged.
	 * Additionally, some tranport states inhibit error increments.
	 */
	if (!is_hb) {
		asoc->overall_error_count++;
		if (transport->state != SCTP_INACTIVE)
			transport->error_count++;
	 } else if (transport->hb_sent) {
		if (transport->state != SCTP_UNCONFIRMED)
			asoc->overall_error_count++;
		if (transport->state != SCTP_INACTIVE)
			transport->error_count++;
	}

	/* If the transport error count is greater than the pf_retrans
	 * threshold, and less than pathmaxrtx, and if the current state
	 * is SCTP_ACTIVE, then mark this transport as Partially Failed,
	 * see SCTP Quick Failover Draft, section 5.1
	 */
	if (net->sctp.pf_enable &&
	   (transport->state == SCTP_ACTIVE) &&
	   (asoc->pf_retrans < transport->pathmaxrxt) &&
	   (transport->error_count > asoc->pf_retrans)) {

		sctp_assoc_control_transport(asoc, transport,
					     SCTP_TRANSPORT_PF,
					     0);

		/* Update the hb timer to resend a heartbeat every rto */
		sctp_transport_reset_hb_timer(transport);
	}

	if (transport->state != SCTP_INACTIVE &&
	    (transport->error_count > transport->pathmaxrxt)) {
		pr_debug("%s: association:%p transport addr:%pISpc failed\n",
			 __func__, asoc, &transport->ipaddr.sa);

		sctp_assoc_control_transport(asoc, transport,
					     SCTP_TRANSPORT_DOWN,
					     SCTP_FAILED_THRESHOLD);
	}

	/* E2) For the destination address for which the timer
	 * expires, set RTO <- RTO * 2 ("back off the timer").  The
	 * maximum value discussed in rule C7 above (RTO.max) may be
	 * used to provide an upper bound to this doubling operation.
	 *
	 * Special Case:  the first HB doesn't trigger exponential backoff.
	 * The first unacknowledged HB triggers it.  We do this with a flag
	 * that indicates that we have an outstanding HB.
	 */
	if (!is_hb || transport->hb_sent) {
		transport->rto = min((transport->rto * 2), transport->asoc->rto_max);
		sctp_max_rto(asoc, transport);
	}
}

/* Worker routine to handle INIT command failure.  */
static void sctp_cmd_init_failed(sctp_cmd_seq_t *commands,
				 struct sctp_association *asoc,
				 unsigned int error)
{
	struct sctp_ulpevent *event;

	event = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_CANT_STR_ASSOC,
						(__u16)error, 0, 0, NULL,
						GFP_ATOMIC);

	if (event)
		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
				SCTP_ULPEVENT(event));

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));

	/* SEND_FAILED sent later when cleaning up the association. */
	asoc->outqueue.error = error;
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
}

/* Worker routine to handle SCTP_CMD_ASSOC_FAILED.  */
static void sctp_cmd_assoc_failed(sctp_cmd_seq_t *commands,
				  struct sctp_association *asoc,
				  sctp_event_t event_type,
				  sctp_subtype_t subtype,
				  struct sctp_chunk *chunk,
				  unsigned int error)
{
	struct sctp_ulpevent *event;
	struct sctp_chunk *abort;
	/* Cancel any partial delivery in progress. */
	sctp_ulpq_abort_pd(&asoc->ulpq, GFP_ATOMIC);

	if (event_type == SCTP_EVENT_T_CHUNK && subtype.chunk == SCTP_CID_ABORT)
		event = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_COMM_LOST,
						(__u16)error, 0, 0, chunk,
						GFP_ATOMIC);
	else
		event = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_COMM_LOST,
						(__u16)error, 0, 0, NULL,
						GFP_ATOMIC);
	if (event)
		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
				SCTP_ULPEVENT(event));

	if (asoc->overall_error_count >= asoc->max_retrans) {
		abort = sctp_make_violation_max_retrans(asoc, chunk);
		if (abort)
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(abort));
	}

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));

	/* SEND_FAILED sent later when cleaning up the association. */
	asoc->outqueue.error = error;
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
}

/* Process an init chunk (may be real INIT/INIT-ACK or an embedded INIT
 * inside the cookie.  In reality, this is only used for INIT-ACK processing
 * since all other cases use "temporary" associations and can do all
 * their work in statefuns directly.
 */
static int sctp_cmd_process_init(sctp_cmd_seq_t *commands,
				 struct sctp_association *asoc,
				 struct sctp_chunk *chunk,
				 sctp_init_chunk_t *peer_init,
				 gfp_t gfp)
{
	int error;

	/* We only process the init as a sideeffect in a single
	 * case.   This is when we process the INIT-ACK.   If we
	 * fail during INIT processing (due to malloc problems),
	 * just return the error and stop processing the stack.
	 */
	if (!sctp_process_init(asoc, chunk, sctp_source(chunk), peer_init, gfp))
		error = -ENOMEM;
	else
		error = 0;

	return error;
}

/* Helper function to break out starting up of heartbeat timers.  */
static void sctp_cmd_hb_timers_start(sctp_cmd_seq_t *cmds,
				     struct sctp_association *asoc)
{
	struct sctp_transport *t;

	/* Start a heartbeat timer for each transport on the association.
	 * hold a reference on the transport to make sure none of
	 * the needed data structures go away.
	 */
	list_for_each_entry(t, &asoc->peer.transport_addr_list, transports)
		sctp_transport_reset_hb_timer(t);
}

static void sctp_cmd_hb_timers_stop(sctp_cmd_seq_t *cmds,
				    struct sctp_association *asoc)
{
	struct sctp_transport *t;

	/* Stop all heartbeat timers. */

	list_for_each_entry(t, &asoc->peer.transport_addr_list,
			transports) {
		if (del_timer(&t->hb_timer))
			sctp_transport_put(t);
	}
}

/* Helper function to stop any pending T3-RTX timers */
static void sctp_cmd_t3_rtx_timers_stop(sctp_cmd_seq_t *cmds,
					struct sctp_association *asoc)
{
	struct sctp_transport *t;

	list_for_each_entry(t, &asoc->peer.transport_addr_list,
			transports) {
		if (del_timer(&t->T3_rtx_timer))
			sctp_transport_put(t);
	}
}


/* Helper function to handle the reception of an HEARTBEAT ACK.  */
static void sctp_cmd_transport_on(sctp_cmd_seq_t *cmds,
				  struct sctp_association *asoc,
				  struct sctp_transport *t,
				  struct sctp_chunk *chunk)
{
	sctp_sender_hb_info_t *hbinfo;
	int was_unconfirmed = 0;

	/* 8.3 Upon the receipt of the HEARTBEAT ACK, the sender of the
	 * HEARTBEAT should clear the error counter of the destination
	 * transport address to which the HEARTBEAT was sent.
	 */
	t->error_count = 0;

	/*
	 * Although RFC4960 specifies that the overall error count must
	 * be cleared when a HEARTBEAT ACK is received, we make an
	 * exception while in SHUTDOWN PENDING. If the peer keeps its
	 * window shut forever, we may never be able to transmit our
	 * outstanding data and rely on the retransmission limit be reached
	 * to shutdown the association.
	 */
	if (t->asoc->state < SCTP_STATE_SHUTDOWN_PENDING)
		t->asoc->overall_error_count = 0;

	/* Clear the hb_sent flag to signal that we had a good
	 * acknowledgement.
	 */
	t->hb_sent = 0;

	/* Mark the destination transport address as active if it is not so
	 * marked.
	 */
	if ((t->state == SCTP_INACTIVE) || (t->state == SCTP_UNCONFIRMED)) {
		was_unconfirmed = 1;
		sctp_assoc_control_transport(asoc, t, SCTP_TRANSPORT_UP,
					     SCTP_HEARTBEAT_SUCCESS);
	}

	if (t->state == SCTP_PF)
		sctp_assoc_control_transport(asoc, t, SCTP_TRANSPORT_UP,
					     SCTP_HEARTBEAT_SUCCESS);

	/* HB-ACK was received for a the proper HB.  Consider this
	 * forward progress.
	 */
	if (t->dst)
		dst_confirm(t->dst);

	/* The receiver of the HEARTBEAT ACK should also perform an
	 * RTT measurement for that destination transport address
	 * using the time value carried in the HEARTBEAT ACK chunk.
	 * If the transport's rto_pending variable has been cleared,
	 * it was most likely due to a retransmit.  However, we want
	 * to re-enable it to properly update the rto.
	 */
	if (t->rto_pending == 0)
		t->rto_pending = 1;

	hbinfo = (sctp_sender_hb_info_t *) chunk->skb->data;
	sctp_transport_update_rto(t, (jiffies - hbinfo->sent_at));

	/* Update the heartbeat timer.  */
	sctp_transport_reset_hb_timer(t);

	if (was_unconfirmed && asoc->peer.transport_count == 1)
		sctp_transport_immediate_rtx(t);
}


/* Helper function to process the process SACK command.  */
static int sctp_cmd_process_sack(sctp_cmd_seq_t *cmds,
				 struct sctp_association *asoc,
				 struct sctp_chunk *chunk)
{
	int err = 0;

	if (sctp_outq_sack(&asoc->outqueue, chunk)) {
		struct net *net = sock_net(asoc->base.sk);

		/* There are no more TSNs awaiting SACK.  */
		err = sctp_do_sm(net, SCTP_EVENT_T_OTHER,
				 SCTP_ST_OTHER(SCTP_EVENT_NO_PENDING_TSN),
				 asoc->state, asoc->ep, asoc, NULL,
				 GFP_ATOMIC);
	}

	return err;
}

/* Helper function to set the timeout value for T2-SHUTDOWN timer and to set
 * the transport for a shutdown chunk.
 */
static void sctp_cmd_setup_t2(sctp_cmd_seq_t *cmds,
			      struct sctp_association *asoc,
			      struct sctp_chunk *chunk)
{
	struct sctp_transport *t;

	if (chunk->transport)
		t = chunk->transport;
	else {
		t = sctp_assoc_choose_alter_transport(asoc,
					      asoc->shutdown_last_sent_to);
		chunk->transport = t;
	}
	asoc->shutdown_last_sent_to = t;
	asoc->timeouts[SCTP_EVENT_TIMEOUT_T2_SHUTDOWN] = t->rto;
}

/* Helper function to change the state of an association. */
static void sctp_cmd_new_state(sctp_cmd_seq_t *cmds,
			       struct sctp_association *asoc,
			       sctp_state_t state)
{
	struct sock *sk = asoc->base.sk;

	asoc->state = state;

	pr_debug("%s: asoc:%p[%s]\n", __func__, asoc, sctp_state_tbl[state]);

	if (sctp_style(sk, TCP)) {
		/* Change the sk->sk_state of a TCP-style socket that has
		 * successfully completed a connect() call.
		 */
		if (sctp_state(asoc, ESTABLISHED) && sctp_sstate(sk, CLOSED))
			sk->sk_state = SCTP_SS_ESTABLISHED;

		/* Set the RCV_SHUTDOWN flag when a SHUTDOWN is received. */
		if (sctp_state(asoc, SHUTDOWN_RECEIVED) &&
		    sctp_sstate(sk, ESTABLISHED))
			sk->sk_shutdown |= RCV_SHUTDOWN;
	}

	if (sctp_state(asoc, COOKIE_WAIT)) {
		/* Reset init timeouts since they may have been
		 * increased due to timer expirations.
		 */
		asoc->timeouts[SCTP_EVENT_TIMEOUT_T1_INIT] =
						asoc->rto_initial;
		asoc->timeouts[SCTP_EVENT_TIMEOUT_T1_COOKIE] =
						asoc->rto_initial;
	}

	if (sctp_state(asoc, ESTABLISHED) ||
	    sctp_state(asoc, CLOSED) ||
	    sctp_state(asoc, SHUTDOWN_RECEIVED)) {
		/* Wake up any processes waiting in the asoc's wait queue in
		 * sctp_wait_for_connect() or sctp_wait_for_sndbuf().
		 */
		if (waitqueue_active(&asoc->wait))
			wake_up_interruptible(&asoc->wait);

		/* Wake up any processes waiting in the sk's sleep queue of
		 * a TCP-style or UDP-style peeled-off socket in
		 * sctp_wait_for_accept() or sctp_wait_for_packet().
		 * For a UDP-style socket, the waiters are woken up by the
		 * notifications.
		 */
		if (!sctp_style(sk, UDP))
			sk->sk_state_change(sk);
	}
}

/* Helper function to delete an association. */
static void sctp_cmd_delete_tcb(sctp_cmd_seq_t *cmds,
				struct sctp_association *asoc)
{
	struct sock *sk = asoc->base.sk;

	/* If it is a non-temporary association belonging to a TCP-style
	 * listening socket that is not closed, do not free it so that accept()
	 * can pick it up later.
	 */
	if (sctp_style(sk, TCP) && sctp_sstate(sk, LISTENING) &&
	    (!asoc->temp) && (sk->sk_shutdown != SHUTDOWN_MASK))
		return;

	sctp_association_free(asoc);
}

/*
 * ADDIP Section 4.1 ASCONF Chunk Procedures
 * A4) Start a T-4 RTO timer, using the RTO value of the selected
 * destination address (we use active path instead of primary path just
 * because primary path may be inactive.
 */
static void sctp_cmd_setup_t4(sctp_cmd_seq_t *cmds,
				struct sctp_association *asoc,
				struct sctp_chunk *chunk)
{
	struct sctp_transport *t;

	t = sctp_assoc_choose_alter_transport(asoc, chunk->transport);
	asoc->timeouts[SCTP_EVENT_TIMEOUT_T4_RTO] = t->rto;
	chunk->transport = t;
}

/* Process an incoming Operation Error Chunk. */
static void sctp_cmd_process_operr(sctp_cmd_seq_t *cmds,
				   struct sctp_association *asoc,
				   struct sctp_chunk *chunk)
{
	struct sctp_errhdr *err_hdr;
	struct sctp_ulpevent *ev;

	while (chunk->chunk_end > chunk->skb->data) {
		err_hdr = (struct sctp_errhdr *)(chunk->skb->data);

		ev = sctp_ulpevent_make_remote_error(asoc, chunk, 0,
						     GFP_ATOMIC);
		if (!ev)
			return;

		sctp_ulpq_tail_event(&asoc->ulpq, ev);

		switch (err_hdr->cause) {
		case SCTP_ERROR_UNKNOWN_CHUNK:
		{
			sctp_chunkhdr_t *unk_chunk_hdr;

			unk_chunk_hdr = (sctp_chunkhdr_t *)err_hdr->variable;
			switch (unk_chunk_hdr->type) {
			/* ADDIP 4.1 A9) If the peer responds to an ASCONF with
			 * an ERROR chunk reporting that it did not recognized
			 * the ASCONF chunk type, the sender of the ASCONF MUST
			 * NOT send any further ASCONF chunks and MUST stop its
			 * T-4 timer.
			 */
			case SCTP_CID_ASCONF:
				if (asoc->peer.asconf_capable == 0)
					break;

				asoc->peer.asconf_capable = 0;
				sctp_add_cmd_sf(cmds, SCTP_CMD_TIMER_STOP,
					SCTP_TO(SCTP_EVENT_TIMEOUT_T4_RTO));
				break;
			default:
				break;
			}
			break;
		}
		default:
			break;
		}
	}
}

/* Process variable FWDTSN chunk information. */
static void sctp_cmd_process_fwdtsn(struct sctp_ulpq *ulpq,
				    struct sctp_chunk *chunk)
{
	struct sctp_fwdtsn_skip *skip;
	/* Walk through all the skipped SSNs */
	sctp_walk_fwdtsn(skip, chunk) {
		sctp_ulpq_skip(ulpq, ntohs(skip->stream), ntohs(skip->ssn));
	}
}

/* Helper function to remove the association non-primary peer
 * transports.
 */
static void sctp_cmd_del_non_primary(struct sctp_association *asoc)
{
	struct sctp_transport *t;
	struct list_head *pos;
	struct list_head *temp;

	list_for_each_safe(pos, temp, &asoc->peer.transport_addr_list) {
		t = list_entry(pos, struct sctp_transport, transports);
		if (!sctp_cmp_addr_exact(&t->ipaddr,
					 &asoc->peer.primary_addr)) {
			sctp_assoc_rm_peer(asoc, t);
		}
	}
}

/* Helper function to set sk_err on a 1-1 style socket. */
static void sctp_cmd_set_sk_err(struct sctp_association *asoc, int error)
{
	struct sock *sk = asoc->base.sk;

	if (!sctp_style(sk, UDP))
		sk->sk_err = error;
}

/* Helper function to generate an association change event */
static void sctp_cmd_assoc_change(sctp_cmd_seq_t *commands,
				 struct sctp_association *asoc,
				 u8 state)
{
	struct sctp_ulpevent *ev;

	ev = sctp_ulpevent_make_assoc_change(asoc, 0, state, 0,
					    asoc->c.sinit_num_ostreams,
					    asoc->c.sinit_max_instreams,
					    NULL, GFP_ATOMIC);
	if (ev)
		sctp_ulpq_tail_event(&asoc->ulpq, ev);
}

/* Helper function to generate an adaptation indication event */
static void sctp_cmd_adaptation_ind(sctp_cmd_seq_t *commands,
				    struct sctp_association *asoc)
{
	struct sctp_ulpevent *ev;

	ev = sctp_ulpevent_make_adaptation_indication(asoc, GFP_ATOMIC);

	if (ev)
		sctp_ulpq_tail_event(&asoc->ulpq, ev);
}


static void sctp_cmd_t1_timer_update(struct sctp_association *asoc,
				    sctp_event_timeout_t timer,
				    char *name)
{
	struct sctp_transport *t;

	t = asoc->init_last_sent_to;
	asoc->init_err_counter++;

	if (t->init_sent_count > (asoc->init_cycle + 1)) {
		asoc->timeouts[timer] *= 2;
		if (asoc->timeouts[timer] > asoc->max_init_timeo) {
			asoc->timeouts[timer] = asoc->max_init_timeo;
		}
		asoc->init_cycle++;

		pr_debug("%s: T1[%s] timeout adjustment init_err_counter:%d"
			 " cycle:%d timeout:%ld\n", __func__, name,
			 asoc->init_err_counter, asoc->init_cycle,
			 asoc->timeouts[timer]);
	}

}

/* Send the whole message, chunk by chunk, to the outqueue.
 * This way the whole message is queued up and bundling if
 * encouraged for small fragments.
 */
static int sctp_cmd_send_msg(struct sctp_association *asoc,
				struct sctp_datamsg *msg, gfp_t gfp)
{
	struct sctp_chunk *chunk;
	int error = 0;

	list_for_each_entry(chunk, &msg->chunks, frag_list) {
		error = sctp_outq_tail(&asoc->outqueue, chunk, gfp);
		if (error)
			break;
	}

	return error;
}


/* Sent the next ASCONF packet currently stored in the association.
 * This happens after the ASCONF_ACK was succeffully processed.
 */
static void sctp_cmd_send_asconf(struct sctp_association *asoc)
{
	struct net *net = sock_net(asoc->base.sk);

	/* Send the next asconf chunk from the addip chunk
	 * queue.
	 */
	if (!list_empty(&asoc->addip_chunk_list)) {
		struct list_head *entry = asoc->addip_chunk_list.next;
		struct sctp_chunk *asconf = list_entry(entry,
						struct sctp_chunk, list);
		list_del_init(entry);

		/* Hold the chunk until an ASCONF_ACK is received. */
		sctp_chunk_hold(asconf);
		if (sctp_primitive_ASCONF(net, asoc, asconf))
			sctp_chunk_free(asconf);
		else
			asoc->addip_last_asconf = asconf;
	}
}


/* These three macros allow us to pull the debugging code out of the
 * main flow of sctp_do_sm() to keep attention focused on the real
 * functionality there.
 */
#define debug_pre_sfn() \
	pr_debug("%s[pre-fn]: ep:%p, %s, %s, asoc:%p[%s], %s\n", __func__, \
		 ep, sctp_evttype_tbl[event_type], (*debug_fn)(subtype),   \
		 asoc, sctp_state_tbl[state], state_fn->name)

#define debug_post_sfn() \
	pr_debug("%s[post-fn]: asoc:%p, status:%s\n", __func__, asoc, \
		 sctp_status_tbl[status])

#define debug_post_sfx() \
	pr_debug("%s[post-sfx]: error:%d, asoc:%p[%s]\n", __func__, error, \
		 asoc, sctp_state_tbl[(asoc && sctp_id2assoc(ep->base.sk, \
		 sctp_assoc2id(asoc))) ? asoc->state : SCTP_STATE_CLOSED])

/*
 * This is the master state machine processing function.
 *
 * If you want to understand all of lksctp, this is a
 * good place to start.
 */
int sctp_do_sm(struct net *net, sctp_event_t event_type, sctp_subtype_t subtype,
	       sctp_state_t state,
	       struct sctp_endpoint *ep,
	       struct sctp_association *asoc,
	       void *event_arg,
	       gfp_t gfp)
{
	sctp_cmd_seq_t commands;
	const sctp_sm_table_entry_t *state_fn;
	sctp_disposition_t status;
	int error = 0;
	typedef const char *(printfn_t)(sctp_subtype_t);
	static printfn_t *table[] = {
		NULL, sctp_cname, sctp_tname, sctp_oname, sctp_pname,
	};
	printfn_t *debug_fn  __attribute__ ((unused)) = table[event_type];

	/* Look up the state function, run it, and then process the
	 * side effects.  These three steps are the heart of lksctp.
	 */
	state_fn = sctp_sm_lookup_event(net, event_type, state, subtype);

	sctp_init_cmd_seq(&commands);

	debug_pre_sfn();
	status = state_fn->fn(net, ep, asoc, subtype, event_arg, &commands);
	debug_post_sfn();

	error = sctp_side_effects(event_type, subtype, state,
				  ep, &asoc, event_arg, status,
				  &commands, gfp);
	debug_post_sfx();

	return error;
}

/*****************************************************************
 * This the master state function side effect processing function.
 *****************************************************************/
static int sctp_side_effects(sctp_event_t event_type, sctp_subtype_t subtype,
			     sctp_state_t state,
			     struct sctp_endpoint *ep,
			     struct sctp_association **asoc,
			     void *event_arg,
			     sctp_disposition_t status,
			     sctp_cmd_seq_t *commands,
			     gfp_t gfp)
{
	int error;

	/* FIXME - Most of the dispositions left today would be categorized
	 * as "exceptional" dispositions.  For those dispositions, it
	 * may not be proper to run through any of the commands at all.
	 * For example, the command interpreter might be run only with
	 * disposition SCTP_DISPOSITION_CONSUME.
	 */
	if (0 != (error = sctp_cmd_interpreter(event_type, subtype, state,
					       ep, *asoc,
					       event_arg, status,
					       commands, gfp)))
		goto bail;

	switch (status) {
	case SCTP_DISPOSITION_DISCARD:
		pr_debug("%s: ignored sctp protocol event - state:%d, "
			 "event_type:%d, event_id:%d\n", __func__, state,
			 event_type, subtype.chunk);
		break;

	case SCTP_DISPOSITION_NOMEM:
		/* We ran out of memory, so we need to discard this
		 * packet.
		 */
		/* BUG--we should now recover some memory, probably by
		 * reneging...
		 */
		error = -ENOMEM;
		break;

	case SCTP_DISPOSITION_DELETE_TCB:
	case SCTP_DISPOSITION_ABORT:
		/* This should now be a command. */
		*asoc = NULL;
		break;

	case SCTP_DISPOSITION_CONSUME:
		/*
		 * We should no longer have much work to do here as the
		 * real work has been done as explicit commands above.
		 */
		break;

	case SCTP_DISPOSITION_VIOLATION:
		net_err_ratelimited("protocol violation state %d chunkid %d\n",
				    state, subtype.chunk);
		break;

	case SCTP_DISPOSITION_NOT_IMPL:
		pr_warn("unimplemented feature in state %d, event_type %d, event_id %d\n",
			state, event_type, subtype.chunk);
		break;

	case SCTP_DISPOSITION_BUG:
		pr_err("bug in state %d, event_type %d, event_id %d\n",
		       state, event_type, subtype.chunk);
		BUG();
		break;

	default:
		pr_err("impossible disposition %d in state %d, event_type %d, event_id %d\n",
		       status, state, event_type, subtype.chunk);
		BUG();
		break;
	}

bail:
	return error;
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

/* This is the side-effect interpreter.  */
static int sctp_cmd_interpreter(sctp_event_t event_type,
				sctp_subtype_t subtype,
				sctp_state_t state,
				struct sctp_endpoint *ep,
				struct sctp_association *asoc,
				void *event_arg,
				sctp_disposition_t status,
				sctp_cmd_seq_t *commands,
				gfp_t gfp)
{
	int error = 0;
	int force;
	sctp_cmd_t *cmd;
	struct sctp_chunk *new_obj;
	struct sctp_chunk *chunk = NULL;
	struct sctp_packet *packet;
	struct timer_list *timer;
	unsigned long timeout;
	struct sctp_transport *t;
	struct sctp_sackhdr sackh;
	int local_cork = 0;

	if (SCTP_EVENT_T_TIMEOUT != event_type)
		chunk = event_arg;

	/* Note:  This whole file is a huge candidate for rework.
	 * For example, each command could either have its own handler, so
	 * the loop would look like:
	 *     while (cmds)
	 *         cmd->handle(x, y, z)
	 * --jgrimm
	 */
	while (NULL != (cmd = sctp_next_cmd(commands))) {
		switch (cmd->verb) {
		case SCTP_CMD_NOP:
			/* Do nothing. */
			break;

		case SCTP_CMD_NEW_ASOC:
			/* Register a new association.  */
			if (local_cork) {
				sctp_outq_uncork(&asoc->outqueue, gfp);
				local_cork = 0;
			}

			/* Register with the endpoint.  */
			asoc = cmd->obj.asoc;
			BUG_ON(asoc->peer.primary_path == NULL);
			sctp_endpoint_add_asoc(ep, asoc);
			break;

		case SCTP_CMD_UPDATE_ASSOC:
		       sctp_assoc_update(asoc, cmd->obj.asoc);
		       break;

		case SCTP_CMD_PURGE_OUTQUEUE:
		       sctp_outq_teardown(&asoc->outqueue);
		       break;

		case SCTP_CMD_DELETE_TCB:
			if (local_cork) {
				sctp_outq_uncork(&asoc->outqueue, gfp);
				local_cork = 0;
			}
			/* Delete the current association.  */
			sctp_cmd_delete_tcb(commands, asoc);
			asoc = NULL;
			break;

		case SCTP_CMD_NEW_STATE:
			/* Enter a new state.  */
			sctp_cmd_new_state(commands, asoc, cmd->obj.state);
			break;

		case SCTP_CMD_REPORT_TSN:
			/* Record the arrival of a TSN.  */
			error = sctp_tsnmap_mark(&asoc->peer.tsn_map,
						 cmd->obj.u32, NULL);
			break;

		case SCTP_CMD_REPORT_FWDTSN:
			/* Move the Cumulattive TSN Ack ahead. */
			sctp_tsnmap_skip(&asoc->peer.tsn_map, cmd->obj.u32);

			/* purge the fragmentation queue */
			sctp_ulpq_reasm_flushtsn(&asoc->ulpq, cmd->obj.u32);

			/* Abort any in progress partial delivery. */
			sctp_ulpq_abort_pd(&asoc->ulpq, GFP_ATOMIC);
			break;

		case SCTP_CMD_PROCESS_FWDTSN:
			sctp_cmd_process_fwdtsn(&asoc->ulpq, cmd->obj.chunk);
			break;

		case SCTP_CMD_GEN_SACK:
			/* Generate a Selective ACK.
			 * The argument tells us whether to just count
			 * the packet and MAYBE generate a SACK, or
			 * force a SACK out.
			 */
			force = cmd->obj.i32;
			error = sctp_gen_sack(asoc, force, commands);
			break;

		case SCTP_CMD_PROCESS_SACK:
			/* Process an inbound SACK.  */
			error = sctp_cmd_process_sack(commands, asoc,
						      cmd->obj.chunk);
			break;

		case SCTP_CMD_GEN_INIT_ACK:
			/* Generate an INIT ACK chunk.  */
			new_obj = sctp_make_init_ack(asoc, chunk, GFP_ATOMIC,
						     0);
			if (!new_obj)
				goto nomem;

			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(new_obj));
			break;

		case SCTP_CMD_PEER_INIT:
			/* Process a unified INIT from the peer.
			 * Note: Only used during INIT-ACK processing.  If
			 * there is an error just return to the outter
			 * layer which will bail.
			 */
			error = sctp_cmd_process_init(commands, asoc, chunk,
						      cmd->obj.init, gfp);
			break;

		case SCTP_CMD_GEN_COOKIE_ECHO:
			/* Generate a COOKIE ECHO chunk.  */
			new_obj = sctp_make_cookie_echo(asoc, chunk);
			if (!new_obj) {
				if (cmd->obj.chunk)
					sctp_chunk_free(cmd->obj.chunk);
				goto nomem;
			}
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(new_obj));

			/* If there is an ERROR chunk to be sent along with
			 * the COOKIE_ECHO, send it, too.
			 */
			if (cmd->obj.chunk)
				sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
						SCTP_CHUNK(cmd->obj.chunk));

			if (new_obj->transport) {
				new_obj->transport->init_sent_count++;
				asoc->init_last_sent_to = new_obj->transport;
			}

			/* FIXME - Eventually come up with a cleaner way to
			 * enabling COOKIE-ECHO + DATA bundling during
			 * multihoming stale cookie scenarios, the following
			 * command plays with asoc->peer.retran_path to
			 * avoid the problem of sending the COOKIE-ECHO and
			 * DATA in different paths, which could result
			 * in the association being ABORTed if the DATA chunk
			 * is processed first by the server.  Checking the
			 * init error counter simply causes this command
			 * to be executed only during failed attempts of
			 * association establishment.
			 */
			if ((asoc->peer.retran_path !=
			     asoc->peer.primary_path) &&
			    (asoc->init_err_counter > 0)) {
				sctp_add_cmd_sf(commands,
						SCTP_CMD_FORCE_PRIM_RETRAN,
						SCTP_NULL());
			}

			break;

		case SCTP_CMD_GEN_SHUTDOWN:
			/* Generate SHUTDOWN when in SHUTDOWN_SENT state.
			 * Reset error counts.
			 */
			asoc->overall_error_count = 0;

			/* Generate a SHUTDOWN chunk.  */
			new_obj = sctp_make_shutdown(asoc, chunk);
			if (!new_obj)
				goto nomem;
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(new_obj));
			break;

		case SCTP_CMD_CHUNK_ULP:
			/* Send a chunk to the sockets layer.  */
			pr_debug("%s: sm_sideff: chunk_up:%p, ulpq:%p\n",
				 __func__, cmd->obj.chunk, &asoc->ulpq);

			sctp_ulpq_tail_data(&asoc->ulpq, cmd->obj.chunk,
					    GFP_ATOMIC);
			break;

		case SCTP_CMD_EVENT_ULP:
			/* Send a notification to the sockets layer.  */
			pr_debug("%s: sm_sideff: event_up:%p, ulpq:%p\n",
				 __func__, cmd->obj.ulpevent, &asoc->ulpq);

			sctp_ulpq_tail_event(&asoc->ulpq, cmd->obj.ulpevent);
			break;

		case SCTP_CMD_REPLY:
			/* If an caller has not already corked, do cork. */
			if (!asoc->outqueue.cork) {
				sctp_outq_cork(&asoc->outqueue);
				local_cork = 1;
			}
			/* Send a chunk to our peer.  */
			error = sctp_outq_tail(&asoc->outqueue, cmd->obj.chunk,
					       gfp);
			break;

		case SCTP_CMD_SEND_PKT:
			/* Send a full packet to our peer.  */
			packet = cmd->obj.packet;
			sctp_packet_transmit(packet, gfp);
			sctp_ootb_pkt_free(packet);
			break;

		case SCTP_CMD_T1_RETRAN:
			/* Mark a transport for retransmission.  */
			sctp_retransmit(&asoc->outqueue, cmd->obj.transport,
					SCTP_RTXR_T1_RTX);
			break;

		case SCTP_CMD_RETRAN:
			/* Mark a transport for retransmission.  */
			sctp_retransmit(&asoc->outqueue, cmd->obj.transport,
					SCTP_RTXR_T3_RTX);
			break;

		case SCTP_CMD_ECN_CE:
			/* Do delayed CE processing.   */
			sctp_do_ecn_ce_work(asoc, cmd->obj.u32);
			break;

		case SCTP_CMD_ECN_ECNE:
			/* Do delayed ECNE processing. */
			new_obj = sctp_do_ecn_ecne_work(asoc, cmd->obj.u32,
							chunk);
			if (new_obj)
				sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
						SCTP_CHUNK(new_obj));
			break;

		case SCTP_CMD_ECN_CWR:
			/* Do delayed CWR processing.  */
			sctp_do_ecn_cwr_work(asoc, cmd->obj.u32);
			break;

		case SCTP_CMD_SETUP_T2:
			sctp_cmd_setup_t2(commands, asoc, cmd->obj.chunk);
			break;

		case SCTP_CMD_TIMER_START_ONCE:
			timer = &asoc->timers[cmd->obj.to];

			if (timer_pending(timer))
				break;
			/* fall through */

		case SCTP_CMD_TIMER_START:
			timer = &asoc->timers[cmd->obj.to];
			timeout = asoc->timeouts[cmd->obj.to];
			BUG_ON(!timeout);

			timer->expires = jiffies + timeout;
			sctp_association_hold(asoc);
			add_timer(timer);
			break;

		case SCTP_CMD_TIMER_RESTART:
			timer = &asoc->timers[cmd->obj.to];
			timeout = asoc->timeouts[cmd->obj.to];
			if (!mod_timer(timer, jiffies + timeout))
				sctp_association_hold(asoc);
			break;

		case SCTP_CMD_TIMER_STOP:
			timer = &asoc->timers[cmd->obj.to];
			if (del_timer(timer))
				sctp_association_put(asoc);
			break;

		case SCTP_CMD_INIT_CHOOSE_TRANSPORT:
			chunk = cmd->obj.chunk;
			t = sctp_assoc_choose_alter_transport(asoc,
						asoc->init_last_sent_to);
			asoc->init_last_sent_to = t;
			chunk->transport = t;
			t->init_sent_count++;
			/* Set the new transport as primary */
			sctp_assoc_set_primary(asoc, t);
			break;

		case SCTP_CMD_INIT_RESTART:
			/* Do the needed accounting and updates
			 * associated with restarting an initialization
			 * timer. Only multiply the timeout by two if
			 * all transports have been tried at the current
			 * timeout.
			 */
			sctp_cmd_t1_timer_update(asoc,
						SCTP_EVENT_TIMEOUT_T1_INIT,
						"INIT");

			sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
					SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));
			break;

		case SCTP_CMD_COOKIEECHO_RESTART:
			/* Do the needed accounting and updates
			 * associated with restarting an initialization
			 * timer. Only multiply the timeout by two if
			 * all transports have been tried at the current
			 * timeout.
			 */
			sctp_cmd_t1_timer_update(asoc,
						SCTP_EVENT_TIMEOUT_T1_COOKIE,
						"COOKIE");

			/* If we've sent any data bundled with
			 * COOKIE-ECHO we need to resend.
			 */
			list_for_each_entry(t, &asoc->peer.transport_addr_list,
					transports) {
				sctp_retransmit_mark(&asoc->outqueue, t,
					    SCTP_RTXR_T1_RTX);
			}

			sctp_add_cmd_sf(commands,
					SCTP_CMD_TIMER_RESTART,
					SCTP_TO(SCTP_EVENT_TIMEOUT_T1_COOKIE));
			break;

		case SCTP_CMD_INIT_FAILED:
			sctp_cmd_init_failed(commands, asoc, cmd->obj.err);
			break;

		case SCTP_CMD_ASSOC_FAILED:
			sctp_cmd_assoc_failed(commands, asoc, event_type,
					      subtype, chunk, cmd->obj.err);
			break;

		case SCTP_CMD_INIT_COUNTER_INC:
			asoc->init_err_counter++;
			break;

		case SCTP_CMD_INIT_COUNTER_RESET:
			asoc->init_err_counter = 0;
			asoc->init_cycle = 0;
			list_for_each_entry(t, &asoc->peer.transport_addr_list,
					    transports) {
				t->init_sent_count = 0;
			}
			break;

		case SCTP_CMD_REPORT_DUP:
			sctp_tsnmap_mark_dup(&asoc->peer.tsn_map,
					     cmd->obj.u32);
			break;

		case SCTP_CMD_REPORT_BAD_TAG:
			pr_debug("%s: vtag mismatch!\n", __func__);
			break;

		case SCTP_CMD_STRIKE:
			/* Mark one strike against a transport.  */
			sctp_do_8_2_transport_strike(commands, asoc,
						    cmd->obj.transport, 0);
			break;

		case SCTP_CMD_TRANSPORT_IDLE:
			t = cmd->obj.transport;
			sctp_transport_lower_cwnd(t, SCTP_LOWER_CWND_INACTIVE);
			break;

		case SCTP_CMD_TRANSPORT_HB_SENT:
			t = cmd->obj.transport;
			sctp_do_8_2_transport_strike(commands, asoc,
						     t, 1);
			t->hb_sent = 1;
			break;

		case SCTP_CMD_TRANSPORT_ON:
			t = cmd->obj.transport;
			sctp_cmd_transport_on(commands, asoc, t, chunk);
			break;

		case SCTP_CMD_HB_TIMERS_START:
			sctp_cmd_hb_timers_start(commands, asoc);
			break;

		case SCTP_CMD_HB_TIMER_UPDATE:
			t = cmd->obj.transport;
			sctp_transport_reset_hb_timer(t);
			break;

		case SCTP_CMD_HB_TIMERS_STOP:
			sctp_cmd_hb_timers_stop(commands, asoc);
			break;

		case SCTP_CMD_REPORT_ERROR:
			error = cmd->obj.error;
			break;

		case SCTP_CMD_PROCESS_CTSN:
			/* Dummy up a SACK for processing. */
			sackh.cum_tsn_ack = cmd->obj.be32;
			sackh.a_rwnd = asoc->peer.rwnd +
					asoc->outqueue.outstanding_bytes;
			sackh.num_gap_ack_blocks = 0;
			sackh.num_dup_tsns = 0;
			chunk->subh.sack_hdr = &sackh;
			sctp_add_cmd_sf(commands, SCTP_CMD_PROCESS_SACK,
					SCTP_CHUNK(chunk));
			break;

		case SCTP_CMD_DISCARD_PACKET:
			/* We need to discard the whole packet.
			 * Uncork the queue since there might be
			 * responses pending
			 */
			chunk->pdiscard = 1;
			if (asoc) {
				sctp_outq_uncork(&asoc->outqueue, gfp);
				local_cork = 0;
			}
			break;

		case SCTP_CMD_RTO_PENDING:
			t = cmd->obj.transport;
			t->rto_pending = 1;
			break;

		case SCTP_CMD_PART_DELIVER:
			sctp_ulpq_partial_delivery(&asoc->ulpq, GFP_ATOMIC);
			break;

		case SCTP_CMD_RENEGE:
			sctp_ulpq_renege(&asoc->ulpq, cmd->obj.chunk,
					 GFP_ATOMIC);
			break;

		case SCTP_CMD_SETUP_T4:
			sctp_cmd_setup_t4(commands, asoc, cmd->obj.chunk);
			break;

		case SCTP_CMD_PROCESS_OPERR:
			sctp_cmd_process_operr(commands, asoc, chunk);
			break;
		case SCTP_CMD_CLEAR_INIT_TAG:
			asoc->peer.i.init_tag = 0;
			break;
		case SCTP_CMD_DEL_NON_PRIMARY:
			sctp_cmd_del_non_primary(asoc);
			break;
		case SCTP_CMD_T3_RTX_TIMERS_STOP:
			sctp_cmd_t3_rtx_timers_stop(commands, asoc);
			break;
		case SCTP_CMD_FORCE_PRIM_RETRAN:
			t = asoc->peer.retran_path;
			asoc->peer.retran_path = asoc->peer.primary_path;
			error = sctp_outq_uncork(&asoc->outqueue, gfp);
			local_cork = 0;
			asoc->peer.retran_path = t;
			break;
		case SCTP_CMD_SET_SK_ERR:
			sctp_cmd_set_sk_err(asoc, cmd->obj.error);
			break;
		case SCTP_CMD_ASSOC_CHANGE:
			sctp_cmd_assoc_change(commands, asoc,
					      cmd->obj.u8);
			break;
		case SCTP_CMD_ADAPTATION_IND:
			sctp_cmd_adaptation_ind(commands, asoc);
			break;

		case SCTP_CMD_ASSOC_SHKEY:
			error = sctp_auth_asoc_init_active_key(asoc,
						GFP_ATOMIC);
			break;
		case SCTP_CMD_UPDATE_INITTAG:
			asoc->peer.i.init_tag = cmd->obj.u32;
			break;
		case SCTP_CMD_SEND_MSG:
			if (!asoc->outqueue.cork) {
				sctp_outq_cork(&asoc->outqueue);
				local_cork = 1;
			}
			error = sctp_cmd_send_msg(asoc, cmd->obj.msg, gfp);
			break;
		case SCTP_CMD_SEND_NEXT_ASCONF:
			sctp_cmd_send_asconf(asoc);
			break;
		case SCTP_CMD_PURGE_ASCONF_QUEUE:
			sctp_asconf_queue_teardown(asoc);
			break;

		case SCTP_CMD_SET_ASOC:
			asoc = cmd->obj.asoc;
			break;

		default:
			pr_warn("Impossible command: %u\n",
				cmd->verb);
			break;
		}

		if (error)
			break;
	}

out:
	/* If this is in response to a received chunk, wait until
	 * we are done with the packet to open the queue so that we don't
	 * send multiple packets in response to a single request.
	 */
	if (asoc && SCTP_EVENT_T_CHUNK == event_type && chunk) {
		if (chunk->end_of_packet || chunk->singleton)
			error = sctp_outq_uncork(&asoc->outqueue, gfp);
	} else if (local_cork)
		error = sctp_outq_uncork(&asoc->outqueue, gfp);
	return error;
nomem:
	error = -ENOMEM;
	goto out;
}

