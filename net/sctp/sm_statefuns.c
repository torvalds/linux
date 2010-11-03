/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001-2002 Intel Corp.
 * Copyright (c) 2002      Nokia Corp.
 *
 * This is part of the SCTP Linux Kernel Implementation.
 *
 * These are the state functions for the state machine.
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
 *    Mathew Kotowsky       <kotowsky@sctp.org>
 *    Sridhar Samudrala     <samudrala@us.ibm.com>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *    Hui Huang 	    <hui.huang@nokia.com>
 *    Dajiang Zhang 	    <dajiang.zhang@nokia.com>
 *    Daisy Chang	    <daisyc@us.ibm.com>
 *    Ardelle Fan	    <ardelle.fan@intel.com>
 *    Ryan Layer	    <rmlayer@us.ibm.com>
 *    Kevin Gao		    <kevin.gao@intel.com>
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <net/inet_ecn.h>
#include <linux/skbuff.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>
#include <net/sctp/structs.h>

static struct sctp_packet *sctp_abort_pkt_new(const struct sctp_endpoint *ep,
				  const struct sctp_association *asoc,
				  struct sctp_chunk *chunk,
				  const void *payload,
				  size_t paylen);
static int sctp_eat_data(const struct sctp_association *asoc,
			 struct sctp_chunk *chunk,
			 sctp_cmd_seq_t *commands);
static struct sctp_packet *sctp_ootb_pkt_new(const struct sctp_association *asoc,
					     const struct sctp_chunk *chunk);
static void sctp_send_stale_cookie_err(const struct sctp_endpoint *ep,
				       const struct sctp_association *asoc,
				       const struct sctp_chunk *chunk,
				       sctp_cmd_seq_t *commands,
				       struct sctp_chunk *err_chunk);
static sctp_disposition_t sctp_sf_do_5_2_6_stale(const struct sctp_endpoint *ep,
						 const struct sctp_association *asoc,
						 const sctp_subtype_t type,
						 void *arg,
						 sctp_cmd_seq_t *commands);
static sctp_disposition_t sctp_sf_shut_8_4_5(const struct sctp_endpoint *ep,
					     const struct sctp_association *asoc,
					     const sctp_subtype_t type,
					     void *arg,
					     sctp_cmd_seq_t *commands);
static sctp_disposition_t sctp_sf_tabort_8_4_8(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands);
static struct sctp_sackhdr *sctp_sm_pull_sack(struct sctp_chunk *chunk);

static sctp_disposition_t sctp_stop_t1_and_abort(sctp_cmd_seq_t *commands,
					   __be16 error, int sk_err,
					   const struct sctp_association *asoc,
					   struct sctp_transport *transport);

static sctp_disposition_t sctp_sf_abort_violation(
				     const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     void *arg,
				     sctp_cmd_seq_t *commands,
				     const __u8 *payload,
				     const size_t paylen);

static sctp_disposition_t sctp_sf_violation_chunklen(
				     const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type,
				     void *arg,
				     sctp_cmd_seq_t *commands);

static sctp_disposition_t sctp_sf_violation_paramlen(
				     const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type,
				     void *arg, void *ext,
				     sctp_cmd_seq_t *commands);

static sctp_disposition_t sctp_sf_violation_ctsn(
				     const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type,
				     void *arg,
				     sctp_cmd_seq_t *commands);

static sctp_disposition_t sctp_sf_violation_chunk(
				     const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type,
				     void *arg,
				     sctp_cmd_seq_t *commands);

static sctp_ierror_t sctp_sf_authenticate(const struct sctp_endpoint *ep,
				    const struct sctp_association *asoc,
				    const sctp_subtype_t type,
				    struct sctp_chunk *chunk);

static sctp_disposition_t __sctp_sf_do_9_1_abort(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands);

/* Small helper function that checks if the chunk length
 * is of the appropriate length.  The 'required_length' argument
 * is set to be the size of a specific chunk we are testing.
 * Return Values:  1 = Valid length
 * 		   0 = Invalid length
 *
 */
static inline int
sctp_chunk_length_valid(struct sctp_chunk *chunk,
			   __u16 required_length)
{
	__u16 chunk_length = ntohs(chunk->chunk_hdr->length);

	if (unlikely(chunk_length < required_length))
		return 0;

	return 1;
}

/**********************************************************
 * These are the state functions for handling chunk events.
 **********************************************************/

/*
 * Process the final SHUTDOWN COMPLETE.
 *
 * Section: 4 (C) (diagram), 9.2
 * Upon reception of the SHUTDOWN COMPLETE chunk the endpoint will verify
 * that it is in SHUTDOWN-ACK-SENT state, if it is not the chunk should be
 * discarded. If the endpoint is in the SHUTDOWN-ACK-SENT state the endpoint
 * should stop the T2-shutdown timer and remove all knowledge of the
 * association (and thus the association enters the CLOSED state).
 *
 * Verification Tag: 8.5.1(C), sctpimpguide 2.41.
 * C) Rules for packet carrying SHUTDOWN COMPLETE:
 * ...
 * - The receiver of a SHUTDOWN COMPLETE shall accept the packet
 *   if the Verification Tag field of the packet matches its own tag and
 *   the T bit is not set
 *   OR
 *   it is set to its peer's tag and the T bit is set in the Chunk
 *   Flags.
 *   Otherwise, the receiver MUST silently discard the packet
 *   and take no further action.  An endpoint MUST ignore the
 *   SHUTDOWN COMPLETE if it is not in the SHUTDOWN-ACK-SENT state.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_4_C(const struct sctp_endpoint *ep,
				  const struct sctp_association *asoc,
				  const sctp_subtype_t type,
				  void *arg,
				  sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	struct sctp_ulpevent *ev;

	if (!sctp_vtag_verify_either(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* RFC 2960 6.10 Bundling
	 *
	 * An endpoint MUST NOT bundle INIT, INIT ACK or
	 * SHUTDOWN COMPLETE with any other chunks.
	 */
	if (!chunk->singleton)
		return sctp_sf_violation_chunk(ep, asoc, type, arg, commands);

	/* Make sure that the SHUTDOWN_COMPLETE chunk has a valid length. */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_chunkhdr_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	/* RFC 2960 10.2 SCTP-to-ULP
	 *
	 * H) SHUTDOWN COMPLETE notification
	 *
	 * When SCTP completes the shutdown procedures (section 9.2) this
	 * notification is passed to the upper layer.
	 */
	ev = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_SHUTDOWN_COMP,
					     0, 0, 0, NULL, GFP_ATOMIC);
	if (ev)
		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
				SCTP_ULPEVENT(ev));

	/* Upon reception of the SHUTDOWN COMPLETE chunk the endpoint
	 * will verify that it is in SHUTDOWN-ACK-SENT state, if it is
	 * not the chunk should be discarded. If the endpoint is in
	 * the SHUTDOWN-ACK-SENT state the endpoint should stop the
	 * T2-shutdown timer and remove all knowledge of the
	 * association (and thus the association enters the CLOSED
	 * state).
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));

	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T5_SHUTDOWN_GUARD));

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));

	SCTP_INC_STATS(SCTP_MIB_SHUTDOWNS);
	SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);

	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());

	return SCTP_DISPOSITION_DELETE_TCB;
}

/*
 * Respond to a normal INIT chunk.
 * We are the side that is being asked for an association.
 *
 * Section: 5.1 Normal Establishment of an Association, B
 * B) "Z" shall respond immediately with an INIT ACK chunk.  The
 *    destination IP address of the INIT ACK MUST be set to the source
 *    IP address of the INIT to which this INIT ACK is responding.  In
 *    the response, besides filling in other parameters, "Z" must set the
 *    Verification Tag field to Tag_A, and also provide its own
 *    Verification Tag (Tag_Z) in the Initiate Tag field.
 *
 * Verification Tag: Must be 0.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_1B_init(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	struct sctp_chunk *repl;
	struct sctp_association *new_asoc;
	struct sctp_chunk *err_chunk;
	struct sctp_packet *packet;
	sctp_unrecognized_param_t *unk_param;
	int len;

	/* 6.10 Bundling
	 * An endpoint MUST NOT bundle INIT, INIT ACK or
	 * SHUTDOWN COMPLETE with any other chunks.
	 *
	 * IG Section 2.11.2
	 * Furthermore, we require that the receiver of an INIT chunk MUST
	 * enforce these rules by silently discarding an arriving packet
	 * with an INIT chunk that is bundled with other chunks.
	 */
	if (!chunk->singleton)
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* If the packet is an OOTB packet which is temporarily on the
	 * control endpoint, respond with an ABORT.
	 */
	if (ep == sctp_sk((sctp_get_ctl_sock()))->ep) {
		SCTP_INC_STATS(SCTP_MIB_OUTOFBLUES);
		return sctp_sf_tabort_8_4_8(ep, asoc, type, arg, commands);
	}

	/* 3.1 A packet containing an INIT chunk MUST have a zero Verification
	 * Tag.
	 */
	if (chunk->sctp_hdr->vtag != 0)
		return sctp_sf_tabort_8_4_8(ep, asoc, type, arg, commands);

	/* Make sure that the INIT chunk has a valid length.
	 * Normally, this would cause an ABORT with a Protocol Violation
	 * error, but since we don't have an association, we'll
	 * just discard the packet.
	 */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_init_chunk_t)))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* If the INIT is coming toward a closing socket, we'll send back
	 * and ABORT.  Essentially, this catches the race of INIT being
	 * backloged to the socket at the same time as the user isses close().
	 * Since the socket and all its associations are going away, we
	 * can treat this OOTB
	 */
	if (sctp_sstate(ep->base.sk, CLOSING))
		return sctp_sf_tabort_8_4_8(ep, asoc, type, arg, commands);

	/* Verify the INIT chunk before processing it. */
	err_chunk = NULL;
	if (!sctp_verify_init(asoc, chunk->chunk_hdr->type,
			      (sctp_init_chunk_t *)chunk->chunk_hdr, chunk,
			      &err_chunk)) {
		/* This chunk contains fatal error. It is to be discarded.
		 * Send an ABORT, with causes if there is any.
		 */
		if (err_chunk) {
			packet = sctp_abort_pkt_new(ep, asoc, arg,
					(__u8 *)(err_chunk->chunk_hdr) +
					sizeof(sctp_chunkhdr_t),
					ntohs(err_chunk->chunk_hdr->length) -
					sizeof(sctp_chunkhdr_t));

			sctp_chunk_free(err_chunk);

			if (packet) {
				sctp_add_cmd_sf(commands, SCTP_CMD_SEND_PKT,
						SCTP_PACKET(packet));
				SCTP_INC_STATS(SCTP_MIB_OUTCTRLCHUNKS);
				return SCTP_DISPOSITION_CONSUME;
			} else {
				return SCTP_DISPOSITION_NOMEM;
			}
		} else {
			return sctp_sf_tabort_8_4_8(ep, asoc, type, arg,
						    commands);
		}
	}

	/* Grab the INIT header.  */
	chunk->subh.init_hdr = (sctp_inithdr_t *)chunk->skb->data;

	/* Tag the variable length parameters.  */
	chunk->param_hdr.v = skb_pull(chunk->skb, sizeof(sctp_inithdr_t));

	new_asoc = sctp_make_temp_asoc(ep, chunk, GFP_ATOMIC);
	if (!new_asoc)
		goto nomem;

	if (sctp_assoc_set_bind_addr_from_ep(new_asoc,
					     sctp_scope(sctp_source(chunk)),
					     GFP_ATOMIC) < 0)
		goto nomem_init;

	/* The call, sctp_process_init(), can fail on memory allocation.  */
	if (!sctp_process_init(new_asoc, chunk->chunk_hdr->type,
			       sctp_source(chunk),
			       (sctp_init_chunk_t *)chunk->chunk_hdr,
			       GFP_ATOMIC))
		goto nomem_init;

	/* B) "Z" shall respond immediately with an INIT ACK chunk.  */

	/* If there are errors need to be reported for unknown parameters,
	 * make sure to reserve enough room in the INIT ACK for them.
	 */
	len = 0;
	if (err_chunk)
		len = ntohs(err_chunk->chunk_hdr->length) -
			sizeof(sctp_chunkhdr_t);

	repl = sctp_make_init_ack(new_asoc, chunk, GFP_ATOMIC, len);
	if (!repl)
		goto nomem_init;

	/* If there are errors need to be reported for unknown parameters,
	 * include them in the outgoing INIT ACK as "Unrecognized parameter"
	 * parameter.
	 */
	if (err_chunk) {
		/* Get the "Unrecognized parameter" parameter(s) out of the
		 * ERROR chunk generated by sctp_verify_init(). Since the
		 * error cause code for "unknown parameter" and the
		 * "Unrecognized parameter" type is the same, we can
		 * construct the parameters in INIT ACK by copying the
		 * ERROR causes over.
		 */
		unk_param = (sctp_unrecognized_param_t *)
			    ((__u8 *)(err_chunk->chunk_hdr) +
			    sizeof(sctp_chunkhdr_t));
		/* Replace the cause code with the "Unrecognized parameter"
		 * parameter type.
		 */
		sctp_addto_chunk(repl, len, unk_param);
		sctp_chunk_free(err_chunk);
	}

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_ASOC, SCTP_ASOC(new_asoc));

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));

	/*
	 * Note:  After sending out INIT ACK with the State Cookie parameter,
	 * "Z" MUST NOT allocate any resources, nor keep any states for the
	 * new association.  Otherwise, "Z" will be vulnerable to resource
	 * attacks.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());

	return SCTP_DISPOSITION_DELETE_TCB;

nomem_init:
	sctp_association_free(new_asoc);
nomem:
	if (err_chunk)
		sctp_chunk_free(err_chunk);
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Respond to a normal INIT ACK chunk.
 * We are the side that is initiating the association.
 *
 * Section: 5.1 Normal Establishment of an Association, C
 * C) Upon reception of the INIT ACK from "Z", "A" shall stop the T1-init
 *    timer and leave COOKIE-WAIT state. "A" shall then send the State
 *    Cookie received in the INIT ACK chunk in a COOKIE ECHO chunk, start
 *    the T1-cookie timer, and enter the COOKIE-ECHOED state.
 *
 *    Note: The COOKIE ECHO chunk can be bundled with any pending outbound
 *    DATA chunks, but it MUST be the first chunk in the packet and
 *    until the COOKIE ACK is returned the sender MUST NOT send any
 *    other packets to the peer.
 *
 * Verification Tag: 3.3.3
 *   If the value of the Initiate Tag in a received INIT ACK chunk is
 *   found to be 0, the receiver MUST treat it as an error and close the
 *   association by transmitting an ABORT.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_1C_ack(const struct sctp_endpoint *ep,
				       const struct sctp_association *asoc,
				       const sctp_subtype_t type,
				       void *arg,
				       sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	sctp_init_chunk_t *initchunk;
	struct sctp_chunk *err_chunk;
	struct sctp_packet *packet;

	if (!sctp_vtag_verify(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* 6.10 Bundling
	 * An endpoint MUST NOT bundle INIT, INIT ACK or
	 * SHUTDOWN COMPLETE with any other chunks.
	 */
	if (!chunk->singleton)
		return sctp_sf_violation_chunk(ep, asoc, type, arg, commands);

	/* Make sure that the INIT-ACK chunk has a valid length */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_initack_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);
	/* Grab the INIT header.  */
	chunk->subh.init_hdr = (sctp_inithdr_t *) chunk->skb->data;

	/* Verify the INIT chunk before processing it. */
	err_chunk = NULL;
	if (!sctp_verify_init(asoc, chunk->chunk_hdr->type,
			      (sctp_init_chunk_t *)chunk->chunk_hdr, chunk,
			      &err_chunk)) {

		sctp_error_t error = SCTP_ERROR_NO_RESOURCE;

		/* This chunk contains fatal error. It is to be discarded.
		 * Send an ABORT, with causes.  If there are no causes,
		 * then there wasn't enough memory.  Just terminate
		 * the association.
		 */
		if (err_chunk) {
			packet = sctp_abort_pkt_new(ep, asoc, arg,
					(__u8 *)(err_chunk->chunk_hdr) +
					sizeof(sctp_chunkhdr_t),
					ntohs(err_chunk->chunk_hdr->length) -
					sizeof(sctp_chunkhdr_t));

			sctp_chunk_free(err_chunk);

			if (packet) {
				sctp_add_cmd_sf(commands, SCTP_CMD_SEND_PKT,
						SCTP_PACKET(packet));
				SCTP_INC_STATS(SCTP_MIB_OUTCTRLCHUNKS);
				error = SCTP_ERROR_INV_PARAM;
			}
		}

		/* SCTP-AUTH, Section 6.3:
		 *    It should be noted that if the receiver wants to tear
		 *    down an association in an authenticated way only, the
		 *    handling of malformed packets should not result in
		 *    tearing down the association.
		 *
		 * This means that if we only want to abort associations
		 * in an authenticated way (i.e AUTH+ABORT), then we
		 * can't destroy this association just becuase the packet
		 * was malformed.
		 */
		if (sctp_auth_recv_cid(SCTP_CID_ABORT, asoc))
			return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

		SCTP_INC_STATS(SCTP_MIB_ABORTEDS);
		return sctp_stop_t1_and_abort(commands, error, ECONNREFUSED,
						asoc, chunk->transport);
	}

	/* Tag the variable length parameters.  Note that we never
	 * convert the parameters in an INIT chunk.
	 */
	chunk->param_hdr.v = skb_pull(chunk->skb, sizeof(sctp_inithdr_t));

	initchunk = (sctp_init_chunk_t *) chunk->chunk_hdr;

	sctp_add_cmd_sf(commands, SCTP_CMD_PEER_INIT,
			SCTP_PEER_INIT(initchunk));

	/* Reset init error count upon receipt of INIT-ACK.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_INIT_COUNTER_RESET, SCTP_NULL());

	/* 5.1 C) "A" shall stop the T1-init timer and leave
	 * COOKIE-WAIT state.  "A" shall then ... start the T1-cookie
	 * timer, and enter the COOKIE-ECHOED state.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_COOKIE));
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_COOKIE_ECHOED));

	/* SCTP-AUTH: genereate the assocition shared keys so that
	 * we can potentially signe the COOKIE-ECHO.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_SHKEY, SCTP_NULL());

	/* 5.1 C) "A" shall then send the State Cookie received in the
	 * INIT ACK chunk in a COOKIE ECHO chunk, ...
	 */
	/* If there is any errors to report, send the ERROR chunk generated
	 * for unknown parameters as well.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_GEN_COOKIE_ECHO,
			SCTP_CHUNK(err_chunk));

	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Respond to a normal COOKIE ECHO chunk.
 * We are the side that is being asked for an association.
 *
 * Section: 5.1 Normal Establishment of an Association, D
 * D) Upon reception of the COOKIE ECHO chunk, Endpoint "Z" will reply
 *    with a COOKIE ACK chunk after building a TCB and moving to
 *    the ESTABLISHED state. A COOKIE ACK chunk may be bundled with
 *    any pending DATA chunks (and/or SACK chunks), but the COOKIE ACK
 *    chunk MUST be the first chunk in the packet.
 *
 *   IMPLEMENTATION NOTE: An implementation may choose to send the
 *   Communication Up notification to the SCTP user upon reception
 *   of a valid COOKIE ECHO chunk.
 *
 * Verification Tag: 8.5.1 Exceptions in Verification Tag Rules
 * D) Rules for packet carrying a COOKIE ECHO
 *
 * - When sending a COOKIE ECHO, the endpoint MUST use the value of the
 *   Initial Tag received in the INIT ACK.
 *
 * - The receiver of a COOKIE ECHO follows the procedures in Section 5.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_1D_ce(const struct sctp_endpoint *ep,
				      const struct sctp_association *asoc,
				      const sctp_subtype_t type, void *arg,
				      sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	struct sctp_association *new_asoc;
	sctp_init_chunk_t *peer_init;
	struct sctp_chunk *repl;
	struct sctp_ulpevent *ev, *ai_ev = NULL;
	int error = 0;
	struct sctp_chunk *err_chk_p;
	struct sock *sk;

	/* If the packet is an OOTB packet which is temporarily on the
	 * control endpoint, respond with an ABORT.
	 */
	if (ep == sctp_sk((sctp_get_ctl_sock()))->ep) {
		SCTP_INC_STATS(SCTP_MIB_OUTOFBLUES);
		return sctp_sf_tabort_8_4_8(ep, asoc, type, arg, commands);
	}

	/* Make sure that the COOKIE_ECHO chunk has a valid length.
	 * In this case, we check that we have enough for at least a
	 * chunk header.  More detailed verification is done
	 * in sctp_unpack_cookie().
	 */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_chunkhdr_t)))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* If the endpoint is not listening or if the number of associations
	 * on the TCP-style socket exceed the max backlog, respond with an
	 * ABORT.
	 */
	sk = ep->base.sk;
	if (!sctp_sstate(sk, LISTENING) ||
	    (sctp_style(sk, TCP) && sk_acceptq_is_full(sk)))
		return sctp_sf_tabort_8_4_8(ep, asoc, type, arg, commands);

	/* "Decode" the chunk.  We have no optional parameters so we
	 * are in good shape.
	 */
	chunk->subh.cookie_hdr =
		(struct sctp_signed_cookie *)chunk->skb->data;
	if (!pskb_pull(chunk->skb, ntohs(chunk->chunk_hdr->length) -
					 sizeof(sctp_chunkhdr_t)))
		goto nomem;

	/* 5.1 D) Upon reception of the COOKIE ECHO chunk, Endpoint
	 * "Z" will reply with a COOKIE ACK chunk after building a TCB
	 * and moving to the ESTABLISHED state.
	 */
	new_asoc = sctp_unpack_cookie(ep, asoc, chunk, GFP_ATOMIC, &error,
				      &err_chk_p);

	/* FIXME:
	 * If the re-build failed, what is the proper error path
	 * from here?
	 *
	 * [We should abort the association. --piggy]
	 */
	if (!new_asoc) {
		/* FIXME: Several errors are possible.  A bad cookie should
		 * be silently discarded, but think about logging it too.
		 */
		switch (error) {
		case -SCTP_IERROR_NOMEM:
			goto nomem;

		case -SCTP_IERROR_STALE_COOKIE:
			sctp_send_stale_cookie_err(ep, asoc, chunk, commands,
						   err_chk_p);
			return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

		case -SCTP_IERROR_BAD_SIG:
		default:
			return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
		}
	}


	/* Delay state machine commands until later.
	 *
	 * Re-build the bind address for the association is done in
	 * the sctp_unpack_cookie() already.
	 */
	/* This is a brand-new association, so these are not yet side
	 * effects--it is safe to run them here.
	 */
	peer_init = &chunk->subh.cookie_hdr->c.peer_init[0];

	if (!sctp_process_init(new_asoc, chunk->chunk_hdr->type,
			       &chunk->subh.cookie_hdr->c.peer_addr,
			       peer_init, GFP_ATOMIC))
		goto nomem_init;

	/* SCTP-AUTH:  Now that we've populate required fields in
	 * sctp_process_init, set up the assocaition shared keys as
	 * necessary so that we can potentially authenticate the ACK
	 */
	error = sctp_auth_asoc_init_active_key(new_asoc, GFP_ATOMIC);
	if (error)
		goto nomem_init;

	/* SCTP-AUTH:  auth_chunk pointer is only set when the cookie-echo
	 * is supposed to be authenticated and we have to do delayed
	 * authentication.  We've just recreated the association using
	 * the information in the cookie and now it's much easier to
	 * do the authentication.
	 */
	if (chunk->auth_chunk) {
		struct sctp_chunk auth;
		sctp_ierror_t ret;

		/* set-up our fake chunk so that we can process it */
		auth.skb = chunk->auth_chunk;
		auth.asoc = chunk->asoc;
		auth.sctp_hdr = chunk->sctp_hdr;
		auth.chunk_hdr = (sctp_chunkhdr_t *)skb_push(chunk->auth_chunk,
					    sizeof(sctp_chunkhdr_t));
		skb_pull(chunk->auth_chunk, sizeof(sctp_chunkhdr_t));
		auth.transport = chunk->transport;

		ret = sctp_sf_authenticate(ep, new_asoc, type, &auth);

		/* We can now safely free the auth_chunk clone */
		kfree_skb(chunk->auth_chunk);

		if (ret != SCTP_IERROR_NO_ERROR) {
			sctp_association_free(new_asoc);
			return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
		}
	}

	repl = sctp_make_cookie_ack(new_asoc, chunk);
	if (!repl)
		goto nomem_init;

	/* RFC 2960 5.1 Normal Establishment of an Association
	 *
	 * D) IMPLEMENTATION NOTE: An implementation may choose to
	 * send the Communication Up notification to the SCTP user
	 * upon reception of a valid COOKIE ECHO chunk.
	 */
	ev = sctp_ulpevent_make_assoc_change(new_asoc, 0, SCTP_COMM_UP, 0,
					     new_asoc->c.sinit_num_ostreams,
					     new_asoc->c.sinit_max_instreams,
					     NULL, GFP_ATOMIC);
	if (!ev)
		goto nomem_ev;

	/* Sockets API Draft Section 5.3.1.6
	 * When a peer sends a Adaptation Layer Indication parameter , SCTP
	 * delivers this notification to inform the application that of the
	 * peers requested adaptation layer.
	 */
	if (new_asoc->peer.adaptation_ind) {
		ai_ev = sctp_ulpevent_make_adaptation_indication(new_asoc,
							    GFP_ATOMIC);
		if (!ai_ev)
			goto nomem_aiev;
	}

	/* Add all the state machine commands now since we've created
	 * everything.  This way we don't introduce memory corruptions
	 * during side-effect processing and correclty count established
	 * associations.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_ASOC, SCTP_ASOC(new_asoc));
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_ESTABLISHED));
	SCTP_INC_STATS(SCTP_MIB_CURRESTAB);
	SCTP_INC_STATS(SCTP_MIB_PASSIVEESTABS);
	sctp_add_cmd_sf(commands, SCTP_CMD_HB_TIMERS_START, SCTP_NULL());

	if (new_asoc->autoclose)
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START,
				SCTP_TO(SCTP_EVENT_TIMEOUT_AUTOCLOSE));

	/* This will send the COOKIE ACK */
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));

	/* Queue the ASSOC_CHANGE event */
	sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP, SCTP_ULPEVENT(ev));

	/* Send up the Adaptation Layer Indication event */
	if (ai_ev)
		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
				SCTP_ULPEVENT(ai_ev));

	return SCTP_DISPOSITION_CONSUME;

nomem_aiev:
	sctp_ulpevent_free(ev);
nomem_ev:
	sctp_chunk_free(repl);
nomem_init:
	sctp_association_free(new_asoc);
nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Respond to a normal COOKIE ACK chunk.
 * We are the side that is being asked for an association.
 *
 * RFC 2960 5.1 Normal Establishment of an Association
 *
 * E) Upon reception of the COOKIE ACK, endpoint "A" will move from the
 *    COOKIE-ECHOED state to the ESTABLISHED state, stopping the T1-cookie
 *    timer. It may also notify its ULP about the successful
 *    establishment of the association with a Communication Up
 *    notification (see Section 10).
 *
 * Verification Tag:
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_1E_ca(const struct sctp_endpoint *ep,
				      const struct sctp_association *asoc,
				      const sctp_subtype_t type, void *arg,
				      sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	struct sctp_ulpevent *ev;

	if (!sctp_vtag_verify(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Verify that the chunk length for the COOKIE-ACK is OK.
	 * If we don't do this, any bundled chunks may be junked.
	 */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_chunkhdr_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	/* Reset init error count upon receipt of COOKIE-ACK,
	 * to avoid problems with the managemement of this
	 * counter in stale cookie situations when a transition back
	 * from the COOKIE-ECHOED state to the COOKIE-WAIT
	 * state is performed.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_INIT_COUNTER_RESET, SCTP_NULL());

	/* RFC 2960 5.1 Normal Establishment of an Association
	 *
	 * E) Upon reception of the COOKIE ACK, endpoint "A" will move
	 * from the COOKIE-ECHOED state to the ESTABLISHED state,
	 * stopping the T1-cookie timer.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_COOKIE));
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_ESTABLISHED));
	SCTP_INC_STATS(SCTP_MIB_CURRESTAB);
	SCTP_INC_STATS(SCTP_MIB_ACTIVEESTABS);
	sctp_add_cmd_sf(commands, SCTP_CMD_HB_TIMERS_START, SCTP_NULL());
	if (asoc->autoclose)
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START,
				SCTP_TO(SCTP_EVENT_TIMEOUT_AUTOCLOSE));

	/* It may also notify its ULP about the successful
	 * establishment of the association with a Communication Up
	 * notification (see Section 10).
	 */
	ev = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_COMM_UP,
					     0, asoc->c.sinit_num_ostreams,
					     asoc->c.sinit_max_instreams,
					     NULL, GFP_ATOMIC);

	if (!ev)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP, SCTP_ULPEVENT(ev));

	/* Sockets API Draft Section 5.3.1.6
	 * When a peer sends a Adaptation Layer Indication parameter , SCTP
	 * delivers this notification to inform the application that of the
	 * peers requested adaptation layer.
	 */
	if (asoc->peer.adaptation_ind) {
		ev = sctp_ulpevent_make_adaptation_indication(asoc, GFP_ATOMIC);
		if (!ev)
			goto nomem;

		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
				SCTP_ULPEVENT(ev));
	}

	return SCTP_DISPOSITION_CONSUME;
nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/* Generate and sendout a heartbeat packet.  */
static sctp_disposition_t sctp_sf_heartbeat(const struct sctp_endpoint *ep,
					    const struct sctp_association *asoc,
					    const sctp_subtype_t type,
					    void *arg,
					    sctp_cmd_seq_t *commands)
{
	struct sctp_transport *transport = (struct sctp_transport *) arg;
	struct sctp_chunk *reply;
	sctp_sender_hb_info_t hbinfo;
	size_t paylen = 0;

	hbinfo.param_hdr.type = SCTP_PARAM_HEARTBEAT_INFO;
	hbinfo.param_hdr.length = htons(sizeof(sctp_sender_hb_info_t));
	hbinfo.daddr = transport->ipaddr;
	hbinfo.sent_at = jiffies;
	hbinfo.hb_nonce = transport->hb_nonce;

	/* Send a heartbeat to our peer.  */
	paylen = sizeof(sctp_sender_hb_info_t);
	reply = sctp_make_heartbeat(asoc, transport, &hbinfo, paylen);
	if (!reply)
		return SCTP_DISPOSITION_NOMEM;

	/* Set rto_pending indicating that an RTT measurement
	 * is started with this heartbeat chunk.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_RTO_PENDING,
			SCTP_TRANSPORT(transport));

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));
	return SCTP_DISPOSITION_CONSUME;
}

/* Generate a HEARTBEAT packet on the given transport.  */
sctp_disposition_t sctp_sf_sendbeat_8_3(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_transport *transport = (struct sctp_transport *) arg;

	if (asoc->overall_error_count >= asoc->max_retrans) {
		sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
				SCTP_ERROR(ETIMEDOUT));
		/* CMD_ASSOC_FAILED calls CMD_DELETE_TCB. */
		sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED,
				SCTP_PERR(SCTP_ERROR_NO_ERROR));
		SCTP_INC_STATS(SCTP_MIB_ABORTEDS);
		SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);
		return SCTP_DISPOSITION_DELETE_TCB;
	}

	/* Section 3.3.5.
	 * The Sender-specific Heartbeat Info field should normally include
	 * information about the sender's current time when this HEARTBEAT
	 * chunk is sent and the destination transport address to which this
	 * HEARTBEAT is sent (see Section 8.3).
	 */

	if (transport->param_flags & SPP_HB_ENABLE) {
		if (SCTP_DISPOSITION_NOMEM ==
				sctp_sf_heartbeat(ep, asoc, type, arg,
						  commands))
			return SCTP_DISPOSITION_NOMEM;

		/* Set transport error counter and association error counter
		 * when sending heartbeat.
		 */
		sctp_add_cmd_sf(commands, SCTP_CMD_TRANSPORT_HB_SENT,
				SCTP_TRANSPORT(transport));
	}
	sctp_add_cmd_sf(commands, SCTP_CMD_TRANSPORT_IDLE,
			SCTP_TRANSPORT(transport));
	sctp_add_cmd_sf(commands, SCTP_CMD_HB_TIMER_UPDATE,
			SCTP_TRANSPORT(transport));

	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Process an heartbeat request.
 *
 * Section: 8.3 Path Heartbeat
 * The receiver of the HEARTBEAT should immediately respond with a
 * HEARTBEAT ACK that contains the Heartbeat Information field copied
 * from the received HEARTBEAT chunk.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 * When receiving an SCTP packet, the endpoint MUST ensure that the
 * value in the Verification Tag field of the received SCTP packet
 * matches its own Tag. If the received Verification Tag value does not
 * match the receiver's own tag value, the receiver shall silently
 * discard the packet and shall not process it any further except for
 * those cases listed in Section 8.5.1 below.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_beat_8_3(const struct sctp_endpoint *ep,
				    const struct sctp_association *asoc,
				    const sctp_subtype_t type,
				    void *arg,
				    sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	struct sctp_chunk *reply;
	size_t paylen = 0;

	if (!sctp_vtag_verify(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the HEARTBEAT chunk has a valid length. */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_heartbeat_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	/* 8.3 The receiver of the HEARTBEAT should immediately
	 * respond with a HEARTBEAT ACK that contains the Heartbeat
	 * Information field copied from the received HEARTBEAT chunk.
	 */
	chunk->subh.hb_hdr = (sctp_heartbeathdr_t *) chunk->skb->data;
	paylen = ntohs(chunk->chunk_hdr->length) - sizeof(sctp_chunkhdr_t);
	if (!pskb_pull(chunk->skb, paylen))
		goto nomem;

	reply = sctp_make_heartbeat_ack(asoc, chunk,
					chunk->subh.hb_hdr, paylen);
	if (!reply)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));
	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Process the returning HEARTBEAT ACK.
 *
 * Section: 8.3 Path Heartbeat
 * Upon the receipt of the HEARTBEAT ACK, the sender of the HEARTBEAT
 * should clear the error counter of the destination transport
 * address to which the HEARTBEAT was sent, and mark the destination
 * transport address as active if it is not so marked. The endpoint may
 * optionally report to the upper layer when an inactive destination
 * address is marked as active due to the reception of the latest
 * HEARTBEAT ACK. The receiver of the HEARTBEAT ACK must also
 * clear the association overall error count as well (as defined
 * in section 8.1).
 *
 * The receiver of the HEARTBEAT ACK should also perform an RTT
 * measurement for that destination transport address using the time
 * value carried in the HEARTBEAT ACK chunk.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_backbeat_8_3(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	union sctp_addr from_addr;
	struct sctp_transport *link;
	sctp_sender_hb_info_t *hbinfo;
	unsigned long max_interval;

	if (!sctp_vtag_verify(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the HEARTBEAT-ACK chunk has a valid length.  */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_chunkhdr_t) +
					    sizeof(sctp_sender_hb_info_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	hbinfo = (sctp_sender_hb_info_t *) chunk->skb->data;
	/* Make sure that the length of the parameter is what we expect */
	if (ntohs(hbinfo->param_hdr.length) !=
				    sizeof(sctp_sender_hb_info_t)) {
		return SCTP_DISPOSITION_DISCARD;
	}

	from_addr = hbinfo->daddr;
	link = sctp_assoc_lookup_paddr(asoc, &from_addr);

	/* This should never happen, but lets log it if so.  */
	if (unlikely(!link)) {
		if (from_addr.sa.sa_family == AF_INET6) {
			if (net_ratelimit())
				pr_warn("%s association %p could not find address %pI6\n",
					__func__,
					asoc,
					&from_addr.v6.sin6_addr);
		} else {
			if (net_ratelimit())
				pr_warn("%s association %p could not find address %pI4\n",
					__func__,
					asoc,
					&from_addr.v4.sin_addr.s_addr);
		}
		return SCTP_DISPOSITION_DISCARD;
	}

	/* Validate the 64-bit random nonce. */
	if (hbinfo->hb_nonce != link->hb_nonce)
		return SCTP_DISPOSITION_DISCARD;

	max_interval = link->hbinterval + link->rto;

	/* Check if the timestamp looks valid.  */
	if (time_after(hbinfo->sent_at, jiffies) ||
	    time_after(jiffies, hbinfo->sent_at + max_interval)) {
		SCTP_DEBUG_PRINTK("%s: HEARTBEAT ACK with invalid timestamp "
				  "received for transport: %p\n",
				   __func__, link);
		return SCTP_DISPOSITION_DISCARD;
	}

	/* 8.3 Upon the receipt of the HEARTBEAT ACK, the sender of
	 * the HEARTBEAT should clear the error counter of the
	 * destination transport address to which the HEARTBEAT was
	 * sent and mark the destination transport address as active if
	 * it is not so marked.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TRANSPORT_ON, SCTP_TRANSPORT(link));

	return SCTP_DISPOSITION_CONSUME;
}

/* Helper function to send out an abort for the restart
 * condition.
 */
static int sctp_sf_send_restart_abort(union sctp_addr *ssa,
				      struct sctp_chunk *init,
				      sctp_cmd_seq_t *commands)
{
	int len;
	struct sctp_packet *pkt;
	union sctp_addr_param *addrparm;
	struct sctp_errhdr *errhdr;
	struct sctp_endpoint *ep;
	char buffer[sizeof(struct sctp_errhdr)+sizeof(union sctp_addr_param)];
	struct sctp_af *af = sctp_get_af_specific(ssa->v4.sin_family);

	/* Build the error on the stack.   We are way to malloc crazy
	 * throughout the code today.
	 */
	errhdr = (struct sctp_errhdr *)buffer;
	addrparm = (union sctp_addr_param *)errhdr->variable;

	/* Copy into a parm format. */
	len = af->to_addr_param(ssa, addrparm);
	len += sizeof(sctp_errhdr_t);

	errhdr->cause = SCTP_ERROR_RESTART;
	errhdr->length = htons(len);

	/* Assign to the control socket. */
	ep = sctp_sk((sctp_get_ctl_sock()))->ep;

	/* Association is NULL since this may be a restart attack and we
	 * want to send back the attacker's vtag.
	 */
	pkt = sctp_abort_pkt_new(ep, NULL, init, errhdr, len);

	if (!pkt)
		goto out;
	sctp_add_cmd_sf(commands, SCTP_CMD_SEND_PKT, SCTP_PACKET(pkt));

	SCTP_INC_STATS(SCTP_MIB_OUTCTRLCHUNKS);

	/* Discard the rest of the inbound packet. */
	sctp_add_cmd_sf(commands, SCTP_CMD_DISCARD_PACKET, SCTP_NULL());

out:
	/* Even if there is no memory, treat as a failure so
	 * the packet will get dropped.
	 */
	return 0;
}

static bool list_has_sctp_addr(const struct list_head *list,
			       union sctp_addr *ipaddr)
{
	struct sctp_transport *addr;

	list_for_each_entry(addr, list, transports) {
		if (sctp_cmp_addr_exact(ipaddr, &addr->ipaddr))
			return true;
	}

	return false;
}
/* A restart is occurring, check to make sure no new addresses
 * are being added as we may be under a takeover attack.
 */
static int sctp_sf_check_restart_addrs(const struct sctp_association *new_asoc,
				       const struct sctp_association *asoc,
				       struct sctp_chunk *init,
				       sctp_cmd_seq_t *commands)
{
	struct sctp_transport *new_addr;
	int ret = 1;

	/* Implementor's Guide - Section 5.2.2
	 * ...
	 * Before responding the endpoint MUST check to see if the
	 * unexpected INIT adds new addresses to the association. If new
	 * addresses are added to the association, the endpoint MUST respond
	 * with an ABORT..
	 */

	/* Search through all current addresses and make sure
	 * we aren't adding any new ones.
	 */
	list_for_each_entry(new_addr, &new_asoc->peer.transport_addr_list,
			    transports) {
		if (!list_has_sctp_addr(&asoc->peer.transport_addr_list,
					&new_addr->ipaddr)) {
			sctp_sf_send_restart_abort(&new_addr->ipaddr, init,
						   commands);
			ret = 0;
			break;
		}
	}

	/* Return success if all addresses were found. */
	return ret;
}

/* Populate the verification/tie tags based on overlapping INIT
 * scenario.
 *
 * Note: Do not use in CLOSED or SHUTDOWN-ACK-SENT state.
 */
static void sctp_tietags_populate(struct sctp_association *new_asoc,
				  const struct sctp_association *asoc)
{
	switch (asoc->state) {

	/* 5.2.1 INIT received in COOKIE-WAIT or COOKIE-ECHOED State */

	case SCTP_STATE_COOKIE_WAIT:
		new_asoc->c.my_vtag     = asoc->c.my_vtag;
		new_asoc->c.my_ttag     = asoc->c.my_vtag;
		new_asoc->c.peer_ttag   = 0;
		break;

	case SCTP_STATE_COOKIE_ECHOED:
		new_asoc->c.my_vtag     = asoc->c.my_vtag;
		new_asoc->c.my_ttag     = asoc->c.my_vtag;
		new_asoc->c.peer_ttag   = asoc->c.peer_vtag;
		break;

	/* 5.2.2 Unexpected INIT in States Other than CLOSED, COOKIE-ECHOED,
	 * COOKIE-WAIT and SHUTDOWN-ACK-SENT
	 */
	default:
		new_asoc->c.my_ttag   = asoc->c.my_vtag;
		new_asoc->c.peer_ttag = asoc->c.peer_vtag;
		break;
	}

	/* Other parameters for the endpoint SHOULD be copied from the
	 * existing parameters of the association (e.g. number of
	 * outbound streams) into the INIT ACK and cookie.
	 */
	new_asoc->rwnd                  = asoc->rwnd;
	new_asoc->c.sinit_num_ostreams  = asoc->c.sinit_num_ostreams;
	new_asoc->c.sinit_max_instreams = asoc->c.sinit_max_instreams;
	new_asoc->c.initial_tsn         = asoc->c.initial_tsn;
}

/*
 * Compare vtag/tietag values to determine unexpected COOKIE-ECHO
 * handling action.
 *
 * RFC 2960 5.2.4 Handle a COOKIE ECHO when a TCB exists.
 *
 * Returns value representing action to be taken.   These action values
 * correspond to Action/Description values in RFC 2960, Table 2.
 */
static char sctp_tietags_compare(struct sctp_association *new_asoc,
				 const struct sctp_association *asoc)
{
	/* In this case, the peer may have restarted.  */
	if ((asoc->c.my_vtag != new_asoc->c.my_vtag) &&
	    (asoc->c.peer_vtag != new_asoc->c.peer_vtag) &&
	    (asoc->c.my_vtag == new_asoc->c.my_ttag) &&
	    (asoc->c.peer_vtag == new_asoc->c.peer_ttag))
		return 'A';

	/* Collision case B. */
	if ((asoc->c.my_vtag == new_asoc->c.my_vtag) &&
	    ((asoc->c.peer_vtag != new_asoc->c.peer_vtag) ||
	     (0 == asoc->c.peer_vtag))) {
		return 'B';
	}

	/* Collision case D. */
	if ((asoc->c.my_vtag == new_asoc->c.my_vtag) &&
	    (asoc->c.peer_vtag == new_asoc->c.peer_vtag))
		return 'D';

	/* Collision case C. */
	if ((asoc->c.my_vtag != new_asoc->c.my_vtag) &&
	    (asoc->c.peer_vtag == new_asoc->c.peer_vtag) &&
	    (0 == new_asoc->c.my_ttag) &&
	    (0 == new_asoc->c.peer_ttag))
		return 'C';

	/* No match to any of the special cases; discard this packet. */
	return 'E';
}

/* Common helper routine for both duplicate and simulataneous INIT
 * chunk handling.
 */
static sctp_disposition_t sctp_sf_do_unexpected_init(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg, sctp_cmd_seq_t *commands)
{
	sctp_disposition_t retval;
	struct sctp_chunk *chunk = arg;
	struct sctp_chunk *repl;
	struct sctp_association *new_asoc;
	struct sctp_chunk *err_chunk;
	struct sctp_packet *packet;
	sctp_unrecognized_param_t *unk_param;
	int len;

	/* 6.10 Bundling
	 * An endpoint MUST NOT bundle INIT, INIT ACK or
	 * SHUTDOWN COMPLETE with any other chunks.
	 *
	 * IG Section 2.11.2
	 * Furthermore, we require that the receiver of an INIT chunk MUST
	 * enforce these rules by silently discarding an arriving packet
	 * with an INIT chunk that is bundled with other chunks.
	 */
	if (!chunk->singleton)
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* 3.1 A packet containing an INIT chunk MUST have a zero Verification
	 * Tag.
	 */
	if (chunk->sctp_hdr->vtag != 0)
		return sctp_sf_tabort_8_4_8(ep, asoc, type, arg, commands);

	/* Make sure that the INIT chunk has a valid length.
	 * In this case, we generate a protocol violation since we have
	 * an association established.
	 */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_init_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);
	/* Grab the INIT header.  */
	chunk->subh.init_hdr = (sctp_inithdr_t *) chunk->skb->data;

	/* Tag the variable length parameters.  */
	chunk->param_hdr.v = skb_pull(chunk->skb, sizeof(sctp_inithdr_t));

	/* Verify the INIT chunk before processing it. */
	err_chunk = NULL;
	if (!sctp_verify_init(asoc, chunk->chunk_hdr->type,
			      (sctp_init_chunk_t *)chunk->chunk_hdr, chunk,
			      &err_chunk)) {
		/* This chunk contains fatal error. It is to be discarded.
		 * Send an ABORT, with causes if there is any.
		 */
		if (err_chunk) {
			packet = sctp_abort_pkt_new(ep, asoc, arg,
					(__u8 *)(err_chunk->chunk_hdr) +
					sizeof(sctp_chunkhdr_t),
					ntohs(err_chunk->chunk_hdr->length) -
					sizeof(sctp_chunkhdr_t));

			if (packet) {
				sctp_add_cmd_sf(commands, SCTP_CMD_SEND_PKT,
						SCTP_PACKET(packet));
				SCTP_INC_STATS(SCTP_MIB_OUTCTRLCHUNKS);
				retval = SCTP_DISPOSITION_CONSUME;
			} else {
				retval = SCTP_DISPOSITION_NOMEM;
			}
			goto cleanup;
		} else {
			return sctp_sf_tabort_8_4_8(ep, asoc, type, arg,
						    commands);
		}
	}

	/*
	 * Other parameters for the endpoint SHOULD be copied from the
	 * existing parameters of the association (e.g. number of
	 * outbound streams) into the INIT ACK and cookie.
	 * FIXME:  We are copying parameters from the endpoint not the
	 * association.
	 */
	new_asoc = sctp_make_temp_asoc(ep, chunk, GFP_ATOMIC);
	if (!new_asoc)
		goto nomem;

	if (sctp_assoc_set_bind_addr_from_ep(new_asoc,
				sctp_scope(sctp_source(chunk)), GFP_ATOMIC) < 0)
		goto nomem;

	/* In the outbound INIT ACK the endpoint MUST copy its current
	 * Verification Tag and Peers Verification tag into a reserved
	 * place (local tie-tag and per tie-tag) within the state cookie.
	 */
	if (!sctp_process_init(new_asoc, chunk->chunk_hdr->type,
			       sctp_source(chunk),
			       (sctp_init_chunk_t *)chunk->chunk_hdr,
			       GFP_ATOMIC))
		goto nomem;

	/* Make sure no new addresses are being added during the
	 * restart.   Do not do this check for COOKIE-WAIT state,
	 * since there are no peer addresses to check against.
	 * Upon return an ABORT will have been sent if needed.
	 */
	if (!sctp_state(asoc, COOKIE_WAIT)) {
		if (!sctp_sf_check_restart_addrs(new_asoc, asoc, chunk,
						 commands)) {
			retval = SCTP_DISPOSITION_CONSUME;
			goto nomem_retval;
		}
	}

	sctp_tietags_populate(new_asoc, asoc);

	/* B) "Z" shall respond immediately with an INIT ACK chunk.  */

	/* If there are errors need to be reported for unknown parameters,
	 * make sure to reserve enough room in the INIT ACK for them.
	 */
	len = 0;
	if (err_chunk) {
		len = ntohs(err_chunk->chunk_hdr->length) -
			sizeof(sctp_chunkhdr_t);
	}

	repl = sctp_make_init_ack(new_asoc, chunk, GFP_ATOMIC, len);
	if (!repl)
		goto nomem;

	/* If there are errors need to be reported for unknown parameters,
	 * include them in the outgoing INIT ACK as "Unrecognized parameter"
	 * parameter.
	 */
	if (err_chunk) {
		/* Get the "Unrecognized parameter" parameter(s) out of the
		 * ERROR chunk generated by sctp_verify_init(). Since the
		 * error cause code for "unknown parameter" and the
		 * "Unrecognized parameter" type is the same, we can
		 * construct the parameters in INIT ACK by copying the
		 * ERROR causes over.
		 */
		unk_param = (sctp_unrecognized_param_t *)
			    ((__u8 *)(err_chunk->chunk_hdr) +
			    sizeof(sctp_chunkhdr_t));
		/* Replace the cause code with the "Unrecognized parameter"
		 * parameter type.
		 */
		sctp_addto_chunk(repl, len, unk_param);
	}

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_ASOC, SCTP_ASOC(new_asoc));
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));

	/*
	 * Note: After sending out INIT ACK with the State Cookie parameter,
	 * "Z" MUST NOT allocate any resources for this new association.
	 * Otherwise, "Z" will be vulnerable to resource attacks.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
	retval = SCTP_DISPOSITION_CONSUME;

	return retval;

nomem:
	retval = SCTP_DISPOSITION_NOMEM;
nomem_retval:
	if (new_asoc)
		sctp_association_free(new_asoc);
cleanup:
	if (err_chunk)
		sctp_chunk_free(err_chunk);
	return retval;
}

/*
 * Handle simultanous INIT.
 * This means we started an INIT and then we got an INIT request from
 * our peer.
 *
 * Section: 5.2.1 INIT received in COOKIE-WAIT or COOKIE-ECHOED State (Item B)
 * This usually indicates an initialization collision, i.e., each
 * endpoint is attempting, at about the same time, to establish an
 * association with the other endpoint.
 *
 * Upon receipt of an INIT in the COOKIE-WAIT or COOKIE-ECHOED state, an
 * endpoint MUST respond with an INIT ACK using the same parameters it
 * sent in its original INIT chunk (including its Verification Tag,
 * unchanged). These original parameters are combined with those from the
 * newly received INIT chunk. The endpoint shall also generate a State
 * Cookie with the INIT ACK. The endpoint uses the parameters sent in its
 * INIT to calculate the State Cookie.
 *
 * After that, the endpoint MUST NOT change its state, the T1-init
 * timer shall be left running and the corresponding TCB MUST NOT be
 * destroyed. The normal procedures for handling State Cookies when
 * a TCB exists will resolve the duplicate INITs to a single association.
 *
 * For an endpoint that is in the COOKIE-ECHOED state it MUST populate
 * its Tie-Tags with the Tag information of itself and its peer (see
 * section 5.2.2 for a description of the Tie-Tags).
 *
 * Verification Tag: Not explicit, but an INIT can not have a valid
 * verification tag, so we skip the check.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_2_1_siminit(const struct sctp_endpoint *ep,
				    const struct sctp_association *asoc,
				    const sctp_subtype_t type,
				    void *arg,
				    sctp_cmd_seq_t *commands)
{
	/* Call helper to do the real work for both simulataneous and
	 * duplicate INIT chunk handling.
	 */
	return sctp_sf_do_unexpected_init(ep, asoc, type, arg, commands);
}

/*
 * Handle duplicated INIT messages.  These are usually delayed
 * restransmissions.
 *
 * Section: 5.2.2 Unexpected INIT in States Other than CLOSED,
 * COOKIE-ECHOED and COOKIE-WAIT
 *
 * Unless otherwise stated, upon reception of an unexpected INIT for
 * this association, the endpoint shall generate an INIT ACK with a
 * State Cookie.  In the outbound INIT ACK the endpoint MUST copy its
 * current Verification Tag and peer's Verification Tag into a reserved
 * place within the state cookie.  We shall refer to these locations as
 * the Peer's-Tie-Tag and the Local-Tie-Tag.  The outbound SCTP packet
 * containing this INIT ACK MUST carry a Verification Tag value equal to
 * the Initiation Tag found in the unexpected INIT.  And the INIT ACK
 * MUST contain a new Initiation Tag (randomly generated see Section
 * 5.3.1).  Other parameters for the endpoint SHOULD be copied from the
 * existing parameters of the association (e.g. number of outbound
 * streams) into the INIT ACK and cookie.
 *
 * After sending out the INIT ACK, the endpoint shall take no further
 * actions, i.e., the existing association, including its current state,
 * and the corresponding TCB MUST NOT be changed.
 *
 * Note: Only when a TCB exists and the association is not in a COOKIE-
 * WAIT state are the Tie-Tags populated.  For a normal association INIT
 * (i.e. the endpoint is in a COOKIE-WAIT state), the Tie-Tags MUST be
 * set to 0 (indicating that no previous TCB existed).  The INIT ACK and
 * State Cookie are populated as specified in section 5.2.1.
 *
 * Verification Tag: Not specified, but an INIT has no way of knowing
 * what the verification tag could be, so we ignore it.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_2_2_dupinit(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	/* Call helper to do the real work for both simulataneous and
	 * duplicate INIT chunk handling.
	 */
	return sctp_sf_do_unexpected_init(ep, asoc, type, arg, commands);
}


/*
 * Unexpected INIT-ACK handler.
 *
 * Section 5.2.3
 * If an INIT ACK received by an endpoint in any state other than the
 * COOKIE-WAIT state, the endpoint should discard the INIT ACK chunk.
 * An unexpected INIT ACK usually indicates the processing of an old or
 * duplicated INIT chunk.
*/
sctp_disposition_t sctp_sf_do_5_2_3_initack(const struct sctp_endpoint *ep,
					    const struct sctp_association *asoc,
					    const sctp_subtype_t type,
					    void *arg, sctp_cmd_seq_t *commands)
{
	/* Per the above section, we'll discard the chunk if we have an
	 * endpoint.  If this is an OOTB INIT-ACK, treat it as such.
	 */
	if (ep == sctp_sk((sctp_get_ctl_sock()))->ep)
		return sctp_sf_ootb(ep, asoc, type, arg, commands);
	else
		return sctp_sf_discard_chunk(ep, asoc, type, arg, commands);
}

/* Unexpected COOKIE-ECHO handler for peer restart (Table 2, action 'A')
 *
 * Section 5.2.4
 *  A)  In this case, the peer may have restarted.
 */
static sctp_disposition_t sctp_sf_do_dupcook_a(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					struct sctp_chunk *chunk,
					sctp_cmd_seq_t *commands,
					struct sctp_association *new_asoc)
{
	sctp_init_chunk_t *peer_init;
	struct sctp_ulpevent *ev;
	struct sctp_chunk *repl;
	struct sctp_chunk *err;
	sctp_disposition_t disposition;

	/* new_asoc is a brand-new association, so these are not yet
	 * side effects--it is safe to run them here.
	 */
	peer_init = &chunk->subh.cookie_hdr->c.peer_init[0];

	if (!sctp_process_init(new_asoc, chunk->chunk_hdr->type,
			       sctp_source(chunk), peer_init,
			       GFP_ATOMIC))
		goto nomem;

	/* Make sure no new addresses are being added during the
	 * restart.  Though this is a pretty complicated attack
	 * since you'd have to get inside the cookie.
	 */
	if (!sctp_sf_check_restart_addrs(new_asoc, asoc, chunk, commands)) {
		return SCTP_DISPOSITION_CONSUME;
	}

	/* If the endpoint is in the SHUTDOWN-ACK-SENT state and recognizes
	 * the peer has restarted (Action A), it MUST NOT setup a new
	 * association but instead resend the SHUTDOWN ACK and send an ERROR
	 * chunk with a "Cookie Received while Shutting Down" error cause to
	 * its peer.
	*/
	if (sctp_state(asoc, SHUTDOWN_ACK_SENT)) {
		disposition = sctp_sf_do_9_2_reshutack(ep, asoc,
				SCTP_ST_CHUNK(chunk->chunk_hdr->type),
				chunk, commands);
		if (SCTP_DISPOSITION_NOMEM == disposition)
			goto nomem;

		err = sctp_make_op_error(asoc, chunk,
					 SCTP_ERROR_COOKIE_IN_SHUTDOWN,
					 NULL, 0, 0);
		if (err)
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(err));

		return SCTP_DISPOSITION_CONSUME;
	}

	/* For now, fail any unsent/unacked data.  Consider the optional
	 * choice of resending of this data.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_PURGE_OUTQUEUE, SCTP_NULL());

	repl = sctp_make_cookie_ack(new_asoc, chunk);
	if (!repl)
		goto nomem;

	/* Report association restart to upper layer. */
	ev = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_RESTART, 0,
					     new_asoc->c.sinit_num_ostreams,
					     new_asoc->c.sinit_max_instreams,
					     NULL, GFP_ATOMIC);
	if (!ev)
		goto nomem_ev;

	/* Update the content of current association. */
	sctp_add_cmd_sf(commands, SCTP_CMD_UPDATE_ASSOC, SCTP_ASOC(new_asoc));
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));
	sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP, SCTP_ULPEVENT(ev));
	return SCTP_DISPOSITION_CONSUME;

nomem_ev:
	sctp_chunk_free(repl);
nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/* Unexpected COOKIE-ECHO handler for setup collision (Table 2, action 'B')
 *
 * Section 5.2.4
 *   B) In this case, both sides may be attempting to start an association
 *      at about the same time but the peer endpoint started its INIT
 *      after responding to the local endpoint's INIT
 */
/* This case represents an initialization collision.  */
static sctp_disposition_t sctp_sf_do_dupcook_b(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					struct sctp_chunk *chunk,
					sctp_cmd_seq_t *commands,
					struct sctp_association *new_asoc)
{
	sctp_init_chunk_t *peer_init;
	struct sctp_chunk *repl;

	/* new_asoc is a brand-new association, so these are not yet
	 * side effects--it is safe to run them here.
	 */
	peer_init = &chunk->subh.cookie_hdr->c.peer_init[0];
	if (!sctp_process_init(new_asoc, chunk->chunk_hdr->type,
			       sctp_source(chunk), peer_init,
			       GFP_ATOMIC))
		goto nomem;

	/* Update the content of current association.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_UPDATE_ASSOC, SCTP_ASOC(new_asoc));
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_ESTABLISHED));
	SCTP_INC_STATS(SCTP_MIB_CURRESTAB);
	sctp_add_cmd_sf(commands, SCTP_CMD_HB_TIMERS_START, SCTP_NULL());

	repl = sctp_make_cookie_ack(new_asoc, chunk);
	if (!repl)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));

	/* RFC 2960 5.1 Normal Establishment of an Association
	 *
	 * D) IMPLEMENTATION NOTE: An implementation may choose to
	 * send the Communication Up notification to the SCTP user
	 * upon reception of a valid COOKIE ECHO chunk.
	 *
	 * Sadly, this needs to be implemented as a side-effect, because
	 * we are not guaranteed to have set the association id of the real
	 * association and so these notifications need to be delayed until
	 * the association id is allocated.
	 */

	sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_CHANGE, SCTP_U8(SCTP_COMM_UP));

	/* Sockets API Draft Section 5.3.1.6
	 * When a peer sends a Adaptation Layer Indication parameter , SCTP
	 * delivers this notification to inform the application that of the
	 * peers requested adaptation layer.
	 *
	 * This also needs to be done as a side effect for the same reason as
	 * above.
	 */
	if (asoc->peer.adaptation_ind)
		sctp_add_cmd_sf(commands, SCTP_CMD_ADAPTATION_IND, SCTP_NULL());

	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/* Unexpected COOKIE-ECHO handler for setup collision (Table 2, action 'C')
 *
 * Section 5.2.4
 *  C) In this case, the local endpoint's cookie has arrived late.
 *     Before it arrived, the local endpoint sent an INIT and received an
 *     INIT-ACK and finally sent a COOKIE ECHO with the peer's same tag
 *     but a new tag of its own.
 */
/* This case represents an initialization collision.  */
static sctp_disposition_t sctp_sf_do_dupcook_c(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					struct sctp_chunk *chunk,
					sctp_cmd_seq_t *commands,
					struct sctp_association *new_asoc)
{
	/* The cookie should be silently discarded.
	 * The endpoint SHOULD NOT change states and should leave
	 * any timers running.
	 */
	return SCTP_DISPOSITION_DISCARD;
}

/* Unexpected COOKIE-ECHO handler lost chunk (Table 2, action 'D')
 *
 * Section 5.2.4
 *
 * D) When both local and remote tags match the endpoint should always
 *    enter the ESTABLISHED state, if it has not already done so.
 */
/* This case represents an initialization collision.  */
static sctp_disposition_t sctp_sf_do_dupcook_d(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					struct sctp_chunk *chunk,
					sctp_cmd_seq_t *commands,
					struct sctp_association *new_asoc)
{
	struct sctp_ulpevent *ev = NULL, *ai_ev = NULL;
	struct sctp_chunk *repl;

	/* Clarification from Implementor's Guide:
	 * D) When both local and remote tags match the endpoint should
	 * enter the ESTABLISHED state, if it is in the COOKIE-ECHOED state.
	 * It should stop any cookie timer that may be running and send
	 * a COOKIE ACK.
	 */

	/* Don't accidentally move back into established state. */
	if (asoc->state < SCTP_STATE_ESTABLISHED) {
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
				SCTP_TO(SCTP_EVENT_TIMEOUT_T1_COOKIE));
		sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
				SCTP_STATE(SCTP_STATE_ESTABLISHED));
		SCTP_INC_STATS(SCTP_MIB_CURRESTAB);
		sctp_add_cmd_sf(commands, SCTP_CMD_HB_TIMERS_START,
				SCTP_NULL());

		/* RFC 2960 5.1 Normal Establishment of an Association
		 *
		 * D) IMPLEMENTATION NOTE: An implementation may choose
		 * to send the Communication Up notification to the
		 * SCTP user upon reception of a valid COOKIE
		 * ECHO chunk.
		 */
		ev = sctp_ulpevent_make_assoc_change(asoc, 0,
					     SCTP_COMM_UP, 0,
					     asoc->c.sinit_num_ostreams,
					     asoc->c.sinit_max_instreams,
					     NULL, GFP_ATOMIC);
		if (!ev)
			goto nomem;

		/* Sockets API Draft Section 5.3.1.6
		 * When a peer sends a Adaptation Layer Indication parameter,
		 * SCTP delivers this notification to inform the application
		 * that of the peers requested adaptation layer.
		 */
		if (asoc->peer.adaptation_ind) {
			ai_ev = sctp_ulpevent_make_adaptation_indication(asoc,
								 GFP_ATOMIC);
			if (!ai_ev)
				goto nomem;

		}
	}

	repl = sctp_make_cookie_ack(new_asoc, chunk);
	if (!repl)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));

	if (ev)
		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
				SCTP_ULPEVENT(ev));
	if (ai_ev)
		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
					SCTP_ULPEVENT(ai_ev));

	return SCTP_DISPOSITION_CONSUME;

nomem:
	if (ai_ev)
		sctp_ulpevent_free(ai_ev);
	if (ev)
		sctp_ulpevent_free(ev);
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Handle a duplicate COOKIE-ECHO.  This usually means a cookie-carrying
 * chunk was retransmitted and then delayed in the network.
 *
 * Section: 5.2.4 Handle a COOKIE ECHO when a TCB exists
 *
 * Verification Tag: None.  Do cookie validation.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_5_2_4_dupcook(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	sctp_disposition_t retval;
	struct sctp_chunk *chunk = arg;
	struct sctp_association *new_asoc;
	int error = 0;
	char action;
	struct sctp_chunk *err_chk_p;

	/* Make sure that the chunk has a valid length from the protocol
	 * perspective.  In this case check to make sure we have at least
	 * enough for the chunk header.  Cookie length verification is
	 * done later.
	 */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_chunkhdr_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	/* "Decode" the chunk.  We have no optional parameters so we
	 * are in good shape.
	 */
	chunk->subh.cookie_hdr = (struct sctp_signed_cookie *)chunk->skb->data;
	if (!pskb_pull(chunk->skb, ntohs(chunk->chunk_hdr->length) -
					sizeof(sctp_chunkhdr_t)))
		goto nomem;

	/* In RFC 2960 5.2.4 3, if both Verification Tags in the State Cookie
	 * of a duplicate COOKIE ECHO match the Verification Tags of the
	 * current association, consider the State Cookie valid even if
	 * the lifespan is exceeded.
	 */
	new_asoc = sctp_unpack_cookie(ep, asoc, chunk, GFP_ATOMIC, &error,
				      &err_chk_p);

	/* FIXME:
	 * If the re-build failed, what is the proper error path
	 * from here?
	 *
	 * [We should abort the association. --piggy]
	 */
	if (!new_asoc) {
		/* FIXME: Several errors are possible.  A bad cookie should
		 * be silently discarded, but think about logging it too.
		 */
		switch (error) {
		case -SCTP_IERROR_NOMEM:
			goto nomem;

		case -SCTP_IERROR_STALE_COOKIE:
			sctp_send_stale_cookie_err(ep, asoc, chunk, commands,
						   err_chk_p);
			return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
		case -SCTP_IERROR_BAD_SIG:
		default:
			return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
		}
	}

	/* Compare the tie_tag in cookie with the verification tag of
	 * current association.
	 */
	action = sctp_tietags_compare(new_asoc, asoc);

	switch (action) {
	case 'A': /* Association restart. */
		retval = sctp_sf_do_dupcook_a(ep, asoc, chunk, commands,
					      new_asoc);
		break;

	case 'B': /* Collision case B. */
		retval = sctp_sf_do_dupcook_b(ep, asoc, chunk, commands,
					      new_asoc);
		break;

	case 'C': /* Collision case C. */
		retval = sctp_sf_do_dupcook_c(ep, asoc, chunk, commands,
					      new_asoc);
		break;

	case 'D': /* Collision case D. */
		retval = sctp_sf_do_dupcook_d(ep, asoc, chunk, commands,
					      new_asoc);
		break;

	default: /* Discard packet for all others. */
		retval = sctp_sf_pdiscard(ep, asoc, type, arg, commands);
		break;
	}

	/* Delete the tempory new association. */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_ASOC, SCTP_ASOC(new_asoc));
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());

	return retval;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Process an ABORT.  (SHUTDOWN-PENDING state)
 *
 * See sctp_sf_do_9_1_abort().
 */
sctp_disposition_t sctp_sf_shutdown_pending_abort(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;

	if (!sctp_vtag_verify_either(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the ABORT chunk has a valid length.
	 * Since this is an ABORT chunk, we have to discard it
	 * because of the following text:
	 * RFC 2960, Section 3.3.7
	 *    If an endpoint receives an ABORT with a format error or for an
	 *    association that doesn't exist, it MUST silently discard it.
	 * Becasue the length is "invalid", we can't really discard just
	 * as we do not know its true length.  So, to be safe, discard the
	 * packet.
	 */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_abort_chunk_t)))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* ADD-IP: Special case for ABORT chunks
	 * F4)  One special consideration is that ABORT Chunks arriving
	 * destined to the IP address being deleted MUST be
	 * ignored (see Section 5.3.1 for further details).
	 */
	if (SCTP_ADDR_DEL ==
		    sctp_bind_addr_state(&asoc->base.bind_addr, &chunk->dest))
		return sctp_sf_discard_chunk(ep, asoc, type, arg, commands);

	return __sctp_sf_do_9_1_abort(ep, asoc, type, arg, commands);
}

/*
 * Process an ABORT.  (SHUTDOWN-SENT state)
 *
 * See sctp_sf_do_9_1_abort().
 */
sctp_disposition_t sctp_sf_shutdown_sent_abort(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;

	if (!sctp_vtag_verify_either(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the ABORT chunk has a valid length.
	 * Since this is an ABORT chunk, we have to discard it
	 * because of the following text:
	 * RFC 2960, Section 3.3.7
	 *    If an endpoint receives an ABORT with a format error or for an
	 *    association that doesn't exist, it MUST silently discard it.
	 * Becasue the length is "invalid", we can't really discard just
	 * as we do not know its true length.  So, to be safe, discard the
	 * packet.
	 */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_abort_chunk_t)))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* ADD-IP: Special case for ABORT chunks
	 * F4)  One special consideration is that ABORT Chunks arriving
	 * destined to the IP address being deleted MUST be
	 * ignored (see Section 5.3.1 for further details).
	 */
	if (SCTP_ADDR_DEL ==
		    sctp_bind_addr_state(&asoc->base.bind_addr, &chunk->dest))
		return sctp_sf_discard_chunk(ep, asoc, type, arg, commands);

	/* Stop the T2-shutdown timer. */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));

	/* Stop the T5-shutdown guard timer.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T5_SHUTDOWN_GUARD));

	return __sctp_sf_do_9_1_abort(ep, asoc, type, arg, commands);
}

/*
 * Process an ABORT.  (SHUTDOWN-ACK-SENT state)
 *
 * See sctp_sf_do_9_1_abort().
 */
sctp_disposition_t sctp_sf_shutdown_ack_sent_abort(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	/* The same T2 timer, so we should be able to use
	 * common function with the SHUTDOWN-SENT state.
	 */
	return sctp_sf_shutdown_sent_abort(ep, asoc, type, arg, commands);
}

/*
 * Handle an Error received in COOKIE_ECHOED state.
 *
 * Only handle the error type of stale COOKIE Error, the other errors will
 * be ignored.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_cookie_echoed_err(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	sctp_errhdr_t *err;

	if (!sctp_vtag_verify(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the ERROR chunk has a valid length.
	 * The parameter walking depends on this as well.
	 */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_operr_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	/* Process the error here */
	/* FUTURE FIXME:  When PR-SCTP related and other optional
	 * parms are emitted, this will have to change to handle multiple
	 * errors.
	 */
	sctp_walk_errors(err, chunk->chunk_hdr) {
		if (SCTP_ERROR_STALE_COOKIE == err->cause)
			return sctp_sf_do_5_2_6_stale(ep, asoc, type,
							arg, commands);
	}

	/* It is possible to have malformed error causes, and that
	 * will cause us to end the walk early.  However, since
	 * we are discarding the packet, there should be no adverse
	 * affects.
	 */
	return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
}

/*
 * Handle a Stale COOKIE Error
 *
 * Section: 5.2.6 Handle Stale COOKIE Error
 * If the association is in the COOKIE-ECHOED state, the endpoint may elect
 * one of the following three alternatives.
 * ...
 * 3) Send a new INIT chunk to the endpoint, adding a Cookie
 *    Preservative parameter requesting an extension to the lifetime of
 *    the State Cookie. When calculating the time extension, an
 *    implementation SHOULD use the RTT information measured based on the
 *    previous COOKIE ECHO / ERROR exchange, and should add no more
 *    than 1 second beyond the measured RTT, due to long State Cookie
 *    lifetimes making the endpoint more subject to a replay attack.
 *
 * Verification Tag:  Not explicit, but safe to ignore.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
static sctp_disposition_t sctp_sf_do_5_2_6_stale(const struct sctp_endpoint *ep,
						 const struct sctp_association *asoc,
						 const sctp_subtype_t type,
						 void *arg,
						 sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	time_t stale;
	sctp_cookie_preserve_param_t bht;
	sctp_errhdr_t *err;
	struct sctp_chunk *reply;
	struct sctp_bind_addr *bp;
	int attempts = asoc->init_err_counter + 1;

	if (attempts > asoc->max_init_attempts) {
		sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
				SCTP_ERROR(ETIMEDOUT));
		sctp_add_cmd_sf(commands, SCTP_CMD_INIT_FAILED,
				SCTP_PERR(SCTP_ERROR_STALE_COOKIE));
		return SCTP_DISPOSITION_DELETE_TCB;
	}

	err = (sctp_errhdr_t *)(chunk->skb->data);

	/* When calculating the time extension, an implementation
	 * SHOULD use the RTT information measured based on the
	 * previous COOKIE ECHO / ERROR exchange, and should add no
	 * more than 1 second beyond the measured RTT, due to long
	 * State Cookie lifetimes making the endpoint more subject to
	 * a replay attack.
	 * Measure of Staleness's unit is usec. (1/1000000 sec)
	 * Suggested Cookie Life-span Increment's unit is msec.
	 * (1/1000 sec)
	 * In general, if you use the suggested cookie life, the value
	 * found in the field of measure of staleness should be doubled
	 * to give ample time to retransmit the new cookie and thus
	 * yield a higher probability of success on the reattempt.
	 */
	stale = ntohl(*(__be32 *)((u8 *)err + sizeof(sctp_errhdr_t)));
	stale = (stale * 2) / 1000;

	bht.param_hdr.type = SCTP_PARAM_COOKIE_PRESERVATIVE;
	bht.param_hdr.length = htons(sizeof(bht));
	bht.lifespan_increment = htonl(stale);

	/* Build that new INIT chunk.  */
	bp = (struct sctp_bind_addr *) &asoc->base.bind_addr;
	reply = sctp_make_init(asoc, bp, GFP_ATOMIC, sizeof(bht));
	if (!reply)
		goto nomem;

	sctp_addto_chunk(reply, sizeof(bht), &bht);

	/* Clear peer's init_tag cached in assoc as we are sending a new INIT */
	sctp_add_cmd_sf(commands, SCTP_CMD_CLEAR_INIT_TAG, SCTP_NULL());

	/* Stop pending T3-rtx and heartbeat timers */
	sctp_add_cmd_sf(commands, SCTP_CMD_T3_RTX_TIMERS_STOP, SCTP_NULL());
	sctp_add_cmd_sf(commands, SCTP_CMD_HB_TIMERS_STOP, SCTP_NULL());

	/* Delete non-primary peer ip addresses since we are transitioning
	 * back to the COOKIE-WAIT state
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_DEL_NON_PRIMARY, SCTP_NULL());

	/* If we've sent any data bundled with COOKIE-ECHO we will need to
	 * resend
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_T1_RETRAN,
			SCTP_TRANSPORT(asoc->peer.primary_path));

	/* Cast away the const modifier, as we want to just
	 * rerun it through as a sideffect.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_INIT_COUNTER_INC, SCTP_NULL());

	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_COOKIE));
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_COOKIE_WAIT));
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));

	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Process an ABORT.
 *
 * Section: 9.1
 * After checking the Verification Tag, the receiving endpoint shall
 * remove the association from its record, and shall report the
 * termination to its upper layer.
 *
 * Verification Tag: 8.5.1 Exceptions in Verification Tag Rules
 * B) Rules for packet carrying ABORT:
 *
 *  - The endpoint shall always fill in the Verification Tag field of the
 *    outbound packet with the destination endpoint's tag value if it
 *    is known.
 *
 *  - If the ABORT is sent in response to an OOTB packet, the endpoint
 *    MUST follow the procedure described in Section 8.4.
 *
 *  - The receiver MUST accept the packet if the Verification Tag
 *    matches either its own tag, OR the tag of its peer. Otherwise, the
 *    receiver MUST silently discard the packet and take no further
 *    action.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_9_1_abort(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;

	if (!sctp_vtag_verify_either(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the ABORT chunk has a valid length.
	 * Since this is an ABORT chunk, we have to discard it
	 * because of the following text:
	 * RFC 2960, Section 3.3.7
	 *    If an endpoint receives an ABORT with a format error or for an
	 *    association that doesn't exist, it MUST silently discard it.
	 * Becasue the length is "invalid", we can't really discard just
	 * as we do not know its true length.  So, to be safe, discard the
	 * packet.
	 */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_abort_chunk_t)))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* ADD-IP: Special case for ABORT chunks
	 * F4)  One special consideration is that ABORT Chunks arriving
	 * destined to the IP address being deleted MUST be
	 * ignored (see Section 5.3.1 for further details).
	 */
	if (SCTP_ADDR_DEL ==
		    sctp_bind_addr_state(&asoc->base.bind_addr, &chunk->dest))
		return sctp_sf_discard_chunk(ep, asoc, type, arg, commands);

	return __sctp_sf_do_9_1_abort(ep, asoc, type, arg, commands);
}

static sctp_disposition_t __sctp_sf_do_9_1_abort(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	unsigned len;
	__be16 error = SCTP_ERROR_NO_ERROR;

	/* See if we have an error cause code in the chunk.  */
	len = ntohs(chunk->chunk_hdr->length);
	if (len >= sizeof(struct sctp_chunkhdr) + sizeof(struct sctp_errhdr))
		error = ((sctp_errhdr_t *)chunk->skb->data)->cause;

	sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR, SCTP_ERROR(ECONNRESET));
	/* ASSOC_FAILED will DELETE_TCB. */
	sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED, SCTP_PERR(error));
	SCTP_INC_STATS(SCTP_MIB_ABORTEDS);
	SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);

	return SCTP_DISPOSITION_ABORT;
}

/*
 * Process an ABORT.  (COOKIE-WAIT state)
 *
 * See sctp_sf_do_9_1_abort() above.
 */
sctp_disposition_t sctp_sf_cookie_wait_abort(const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type,
				     void *arg,
				     sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	unsigned len;
	__be16 error = SCTP_ERROR_NO_ERROR;

	if (!sctp_vtag_verify_either(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the ABORT chunk has a valid length.
	 * Since this is an ABORT chunk, we have to discard it
	 * because of the following text:
	 * RFC 2960, Section 3.3.7
	 *    If an endpoint receives an ABORT with a format error or for an
	 *    association that doesn't exist, it MUST silently discard it.
	 * Becasue the length is "invalid", we can't really discard just
	 * as we do not know its true length.  So, to be safe, discard the
	 * packet.
	 */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_abort_chunk_t)))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* See if we have an error cause code in the chunk.  */
	len = ntohs(chunk->chunk_hdr->length);
	if (len >= sizeof(struct sctp_chunkhdr) + sizeof(struct sctp_errhdr))
		error = ((sctp_errhdr_t *)chunk->skb->data)->cause;

	return sctp_stop_t1_and_abort(commands, error, ECONNREFUSED, asoc,
				      chunk->transport);
}

/*
 * Process an incoming ICMP as an ABORT.  (COOKIE-WAIT state)
 */
sctp_disposition_t sctp_sf_cookie_wait_icmp_abort(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	return sctp_stop_t1_and_abort(commands, SCTP_ERROR_NO_ERROR,
				      ENOPROTOOPT, asoc,
				      (struct sctp_transport *)arg);
}

/*
 * Process an ABORT.  (COOKIE-ECHOED state)
 */
sctp_disposition_t sctp_sf_cookie_echoed_abort(const struct sctp_endpoint *ep,
					       const struct sctp_association *asoc,
					       const sctp_subtype_t type,
					       void *arg,
					       sctp_cmd_seq_t *commands)
{
	/* There is a single T1 timer, so we should be able to use
	 * common function with the COOKIE-WAIT state.
	 */
	return sctp_sf_cookie_wait_abort(ep, asoc, type, arg, commands);
}

/*
 * Stop T1 timer and abort association with "INIT failed".
 *
 * This is common code called by several sctp_sf_*_abort() functions above.
 */
static sctp_disposition_t sctp_stop_t1_and_abort(sctp_cmd_seq_t *commands,
					   __be16 error, int sk_err,
					   const struct sctp_association *asoc,
					   struct sctp_transport *transport)
{
	SCTP_DEBUG_PRINTK("ABORT received (INIT).\n");
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));
	SCTP_INC_STATS(SCTP_MIB_ABORTEDS);
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));
	sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR, SCTP_ERROR(sk_err));
	/* CMD_INIT_FAILED will DELETE_TCB. */
	sctp_add_cmd_sf(commands, SCTP_CMD_INIT_FAILED,
			SCTP_PERR(error));
	return SCTP_DISPOSITION_ABORT;
}

/*
 * sctp_sf_do_9_2_shut
 *
 * Section: 9.2
 * Upon the reception of the SHUTDOWN, the peer endpoint shall
 *  - enter the SHUTDOWN-RECEIVED state,
 *
 *  - stop accepting new data from its SCTP user
 *
 *  - verify, by checking the Cumulative TSN Ack field of the chunk,
 *    that all its outstanding DATA chunks have been received by the
 *    SHUTDOWN sender.
 *
 * Once an endpoint as reached the SHUTDOWN-RECEIVED state it MUST NOT
 * send a SHUTDOWN in response to a ULP request. And should discard
 * subsequent SHUTDOWN chunks.
 *
 * If there are still outstanding DATA chunks left, the SHUTDOWN
 * receiver shall continue to follow normal data transmission
 * procedures defined in Section 6 until all outstanding DATA chunks
 * are acknowledged; however, the SHUTDOWN receiver MUST NOT accept
 * new data from its SCTP user.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_9_2_shutdown(const struct sctp_endpoint *ep,
					   const struct sctp_association *asoc,
					   const sctp_subtype_t type,
					   void *arg,
					   sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	sctp_shutdownhdr_t *sdh;
	sctp_disposition_t disposition;
	struct sctp_ulpevent *ev;
	__u32 ctsn;

	if (!sctp_vtag_verify(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the SHUTDOWN chunk has a valid length. */
	if (!sctp_chunk_length_valid(chunk,
				      sizeof(struct sctp_shutdown_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	/* Convert the elaborate header.  */
	sdh = (sctp_shutdownhdr_t *)chunk->skb->data;
	skb_pull(chunk->skb, sizeof(sctp_shutdownhdr_t));
	chunk->subh.shutdown_hdr = sdh;
	ctsn = ntohl(sdh->cum_tsn_ack);

	if (TSN_lt(ctsn, asoc->ctsn_ack_point)) {
		SCTP_DEBUG_PRINTK("ctsn %x\n", ctsn);
		SCTP_DEBUG_PRINTK("ctsn_ack_point %x\n", asoc->ctsn_ack_point);
		return SCTP_DISPOSITION_DISCARD;
	}

	/* If Cumulative TSN Ack beyond the max tsn currently
	 * send, terminating the association and respond to the
	 * sender with an ABORT.
	 */
	if (!TSN_lt(ctsn, asoc->next_tsn))
		return sctp_sf_violation_ctsn(ep, asoc, type, arg, commands);

	/* API 5.3.1.5 SCTP_SHUTDOWN_EVENT
	 * When a peer sends a SHUTDOWN, SCTP delivers this notification to
	 * inform the application that it should cease sending data.
	 */
	ev = sctp_ulpevent_make_shutdown_event(asoc, 0, GFP_ATOMIC);
	if (!ev) {
		disposition = SCTP_DISPOSITION_NOMEM;
		goto out;
	}
	sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP, SCTP_ULPEVENT(ev));

	/* Upon the reception of the SHUTDOWN, the peer endpoint shall
	 *  - enter the SHUTDOWN-RECEIVED state,
	 *  - stop accepting new data from its SCTP user
	 *
	 * [This is implicit in the new state.]
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_SHUTDOWN_RECEIVED));
	disposition = SCTP_DISPOSITION_CONSUME;

	if (sctp_outq_is_empty(&asoc->outqueue)) {
		disposition = sctp_sf_do_9_2_shutdown_ack(ep, asoc, type,
							  arg, commands);
	}

	if (SCTP_DISPOSITION_NOMEM == disposition)
		goto out;

	/*  - verify, by checking the Cumulative TSN Ack field of the
	 *    chunk, that all its outstanding DATA chunks have been
	 *    received by the SHUTDOWN sender.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_PROCESS_CTSN,
			SCTP_BE32(chunk->subh.shutdown_hdr->cum_tsn_ack));

out:
	return disposition;
}

/*
 * sctp_sf_do_9_2_shut_ctsn
 *
 * Once an endpoint has reached the SHUTDOWN-RECEIVED state,
 * it MUST NOT send a SHUTDOWN in response to a ULP request.
 * The Cumulative TSN Ack of the received SHUTDOWN chunk
 * MUST be processed.
 */
sctp_disposition_t sctp_sf_do_9_2_shut_ctsn(const struct sctp_endpoint *ep,
					   const struct sctp_association *asoc,
					   const sctp_subtype_t type,
					   void *arg,
					   sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	sctp_shutdownhdr_t *sdh;
	__u32 ctsn;

	if (!sctp_vtag_verify(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the SHUTDOWN chunk has a valid length. */
	if (!sctp_chunk_length_valid(chunk,
				      sizeof(struct sctp_shutdown_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	sdh = (sctp_shutdownhdr_t *)chunk->skb->data;
	ctsn = ntohl(sdh->cum_tsn_ack);

	if (TSN_lt(ctsn, asoc->ctsn_ack_point)) {
		SCTP_DEBUG_PRINTK("ctsn %x\n", ctsn);
		SCTP_DEBUG_PRINTK("ctsn_ack_point %x\n", asoc->ctsn_ack_point);
		return SCTP_DISPOSITION_DISCARD;
	}

	/* If Cumulative TSN Ack beyond the max tsn currently
	 * send, terminating the association and respond to the
	 * sender with an ABORT.
	 */
	if (!TSN_lt(ctsn, asoc->next_tsn))
		return sctp_sf_violation_ctsn(ep, asoc, type, arg, commands);

	/* verify, by checking the Cumulative TSN Ack field of the
	 * chunk, that all its outstanding DATA chunks have been
	 * received by the SHUTDOWN sender.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_PROCESS_CTSN,
			SCTP_BE32(sdh->cum_tsn_ack));

	return SCTP_DISPOSITION_CONSUME;
}

/* RFC 2960 9.2
 * If an endpoint is in SHUTDOWN-ACK-SENT state and receives an INIT chunk
 * (e.g., if the SHUTDOWN COMPLETE was lost) with source and destination
 * transport addresses (either in the IP addresses or in the INIT chunk)
 * that belong to this association, it should discard the INIT chunk and
 * retransmit the SHUTDOWN ACK chunk.
 */
sctp_disposition_t sctp_sf_do_9_2_reshutack(const struct sctp_endpoint *ep,
				    const struct sctp_association *asoc,
				    const sctp_subtype_t type,
				    void *arg,
				    sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = (struct sctp_chunk *) arg;
	struct sctp_chunk *reply;

	/* Make sure that the chunk has a valid length */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_chunkhdr_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	/* Since we are not going to really process this INIT, there
	 * is no point in verifying chunk boundries.  Just generate
	 * the SHUTDOWN ACK.
	 */
	reply = sctp_make_shutdown_ack(asoc, chunk);
	if (NULL == reply)
		goto nomem;

	/* Set the transport for the SHUTDOWN ACK chunk and the timeout for
	 * the T2-SHUTDOWN timer.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_SETUP_T2, SCTP_CHUNK(reply));

	/* and restart the T2-shutdown timer. */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));

	return SCTP_DISPOSITION_CONSUME;
nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * sctp_sf_do_ecn_cwr
 *
 * Section:  Appendix A: Explicit Congestion Notification
 *
 * CWR:
 *
 * RFC 2481 details a specific bit for a sender to send in the header of
 * its next outbound TCP segment to indicate to its peer that it has
 * reduced its congestion window.  This is termed the CWR bit.  For
 * SCTP the same indication is made by including the CWR chunk.
 * This chunk contains one data element, i.e. the TSN number that
 * was sent in the ECNE chunk.  This element represents the lowest
 * TSN number in the datagram that was originally marked with the
 * CE bit.
 *
 * Verification Tag: 8.5 Verification Tag [Normal verification]
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_ecn_cwr(const struct sctp_endpoint *ep,
				      const struct sctp_association *asoc,
				      const sctp_subtype_t type,
				      void *arg,
				      sctp_cmd_seq_t *commands)
{
	sctp_cwrhdr_t *cwr;
	struct sctp_chunk *chunk = arg;
	u32 lowest_tsn;

	if (!sctp_vtag_verify(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_ecne_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	cwr = (sctp_cwrhdr_t *) chunk->skb->data;
	skb_pull(chunk->skb, sizeof(sctp_cwrhdr_t));

	lowest_tsn = ntohl(cwr->lowest_tsn);

	/* Does this CWR ack the last sent congestion notification? */
	if (TSN_lte(asoc->last_ecne_tsn, lowest_tsn)) {
		/* Stop sending ECNE. */
		sctp_add_cmd_sf(commands,
				SCTP_CMD_ECN_CWR,
				SCTP_U32(lowest_tsn));
	}
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * sctp_sf_do_ecne
 *
 * Section:  Appendix A: Explicit Congestion Notification
 *
 * ECN-Echo
 *
 * RFC 2481 details a specific bit for a receiver to send back in its
 * TCP acknowledgements to notify the sender of the Congestion
 * Experienced (CE) bit having arrived from the network.  For SCTP this
 * same indication is made by including the ECNE chunk.  This chunk
 * contains one data element, i.e. the lowest TSN associated with the IP
 * datagram marked with the CE bit.....
 *
 * Verification Tag: 8.5 Verification Tag [Normal verification]
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_ecne(const struct sctp_endpoint *ep,
				   const struct sctp_association *asoc,
				   const sctp_subtype_t type,
				   void *arg,
				   sctp_cmd_seq_t *commands)
{
	sctp_ecnehdr_t *ecne;
	struct sctp_chunk *chunk = arg;

	if (!sctp_vtag_verify(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_ecne_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	ecne = (sctp_ecnehdr_t *) chunk->skb->data;
	skb_pull(chunk->skb, sizeof(sctp_ecnehdr_t));

	/* If this is a newer ECNE than the last CWR packet we sent out */
	sctp_add_cmd_sf(commands, SCTP_CMD_ECN_ECNE,
			SCTP_U32(ntohl(ecne->lowest_tsn)));

	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Section: 6.2  Acknowledgement on Reception of DATA Chunks
 *
 * The SCTP endpoint MUST always acknowledge the reception of each valid
 * DATA chunk.
 *
 * The guidelines on delayed acknowledgement algorithm specified in
 * Section 4.2 of [RFC2581] SHOULD be followed. Specifically, an
 * acknowledgement SHOULD be generated for at least every second packet
 * (not every second DATA chunk) received, and SHOULD be generated within
 * 200 ms of the arrival of any unacknowledged DATA chunk. In some
 * situations it may be beneficial for an SCTP transmitter to be more
 * conservative than the algorithms detailed in this document allow.
 * However, an SCTP transmitter MUST NOT be more aggressive than the
 * following algorithms allow.
 *
 * A SCTP receiver MUST NOT generate more than one SACK for every
 * incoming packet, other than to update the offered window as the
 * receiving application consumes new data.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_eat_data_6_2(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	sctp_arg_t force = SCTP_NOFORCE();
	int error;

	if (!sctp_vtag_verify(chunk, asoc)) {
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_BAD_TAG,
				SCTP_NULL());
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
	}

	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_data_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	error = sctp_eat_data(asoc, chunk, commands );
	switch (error) {
	case SCTP_IERROR_NO_ERROR:
		break;
	case SCTP_IERROR_HIGH_TSN:
	case SCTP_IERROR_BAD_STREAM:
		SCTP_INC_STATS(SCTP_MIB_IN_DATA_CHUNK_DISCARDS);
		goto discard_noforce;
	case SCTP_IERROR_DUP_TSN:
	case SCTP_IERROR_IGNORE_TSN:
		SCTP_INC_STATS(SCTP_MIB_IN_DATA_CHUNK_DISCARDS);
		goto discard_force;
	case SCTP_IERROR_NO_DATA:
		goto consume;
	case SCTP_IERROR_PROTO_VIOLATION:
		return sctp_sf_abort_violation(ep, asoc, chunk, commands,
			(u8 *)chunk->subh.data_hdr, sizeof(sctp_datahdr_t));
	default:
		BUG();
	}

	if (chunk->chunk_hdr->flags & SCTP_DATA_SACK_IMM)
		force = SCTP_FORCE();

	if (asoc->autoclose) {
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
				SCTP_TO(SCTP_EVENT_TIMEOUT_AUTOCLOSE));
	}

	/* If this is the last chunk in a packet, we need to count it
	 * toward sack generation.  Note that we need to SACK every
	 * OTHER packet containing data chunks, EVEN IF WE DISCARD
	 * THEM.  We elect to NOT generate SACK's if the chunk fails
	 * the verification tag test.
	 *
	 * RFC 2960 6.2 Acknowledgement on Reception of DATA Chunks
	 *
	 * The SCTP endpoint MUST always acknowledge the reception of
	 * each valid DATA chunk.
	 *
	 * The guidelines on delayed acknowledgement algorithm
	 * specified in  Section 4.2 of [RFC2581] SHOULD be followed.
	 * Specifically, an acknowledgement SHOULD be generated for at
	 * least every second packet (not every second DATA chunk)
	 * received, and SHOULD be generated within 200 ms of the
	 * arrival of any unacknowledged DATA chunk.  In some
	 * situations it may be beneficial for an SCTP transmitter to
	 * be more conservative than the algorithms detailed in this
	 * document allow. However, an SCTP transmitter MUST NOT be
	 * more aggressive than the following algorithms allow.
	 */
	if (chunk->end_of_packet)
		sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SACK, force);

	return SCTP_DISPOSITION_CONSUME;

discard_force:
	/* RFC 2960 6.2 Acknowledgement on Reception of DATA Chunks
	 *
	 * When a packet arrives with duplicate DATA chunk(s) and with
	 * no new DATA chunk(s), the endpoint MUST immediately send a
	 * SACK with no delay.  If a packet arrives with duplicate
	 * DATA chunk(s) bundled with new DATA chunks, the endpoint
	 * MAY immediately send a SACK.  Normally receipt of duplicate
	 * DATA chunks will occur when the original SACK chunk was lost
	 * and the peer's RTO has expired.  The duplicate TSN number(s)
	 * SHOULD be reported in the SACK as duplicate.
	 */
	/* In our case, we split the MAY SACK advice up whether or not
	 * the last chunk is a duplicate.'
	 */
	if (chunk->end_of_packet)
		sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SACK, SCTP_FORCE());
	return SCTP_DISPOSITION_DISCARD;

discard_noforce:
	if (chunk->end_of_packet)
		sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SACK, force);

	return SCTP_DISPOSITION_DISCARD;
consume:
	return SCTP_DISPOSITION_CONSUME;

}

/*
 * sctp_sf_eat_data_fast_4_4
 *
 * Section: 4 (4)
 * (4) In SHUTDOWN-SENT state the endpoint MUST acknowledge any received
 *    DATA chunks without delay.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_eat_data_fast_4_4(const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type,
				     void *arg,
				     sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	int error;

	if (!sctp_vtag_verify(chunk, asoc)) {
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_BAD_TAG,
				SCTP_NULL());
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
	}

	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_data_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	error = sctp_eat_data(asoc, chunk, commands );
	switch (error) {
	case SCTP_IERROR_NO_ERROR:
	case SCTP_IERROR_HIGH_TSN:
	case SCTP_IERROR_DUP_TSN:
	case SCTP_IERROR_IGNORE_TSN:
	case SCTP_IERROR_BAD_STREAM:
		break;
	case SCTP_IERROR_NO_DATA:
		goto consume;
	case SCTP_IERROR_PROTO_VIOLATION:
		return sctp_sf_abort_violation(ep, asoc, chunk, commands,
			(u8 *)chunk->subh.data_hdr, sizeof(sctp_datahdr_t));
	default:
		BUG();
	}

	/* Go a head and force a SACK, since we are shutting down. */

	/* Implementor's Guide.
	 *
	 * While in SHUTDOWN-SENT state, the SHUTDOWN sender MUST immediately
	 * respond to each received packet containing one or more DATA chunk(s)
	 * with a SACK, a SHUTDOWN chunk, and restart the T2-shutdown timer
	 */
	if (chunk->end_of_packet) {
		/* We must delay the chunk creation since the cumulative
		 * TSN has not been updated yet.
		 */
		sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SHUTDOWN, SCTP_NULL());
		sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SACK, SCTP_FORCE());
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
				SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));
	}

consume:
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Section: 6.2  Processing a Received SACK
 * D) Any time a SACK arrives, the endpoint performs the following:
 *
 *     i) If Cumulative TSN Ack is less than the Cumulative TSN Ack Point,
 *     then drop the SACK.   Since Cumulative TSN Ack is monotonically
 *     increasing, a SACK whose Cumulative TSN Ack is less than the
 *     Cumulative TSN Ack Point indicates an out-of-order SACK.
 *
 *     ii) Set rwnd equal to the newly received a_rwnd minus the number
 *     of bytes still outstanding after processing the Cumulative TSN Ack
 *     and the Gap Ack Blocks.
 *
 *     iii) If the SACK is missing a TSN that was previously
 *     acknowledged via a Gap Ack Block (e.g., the data receiver
 *     reneged on the data), then mark the corresponding DATA chunk
 *     as available for retransmit:  Mark it as missing for fast
 *     retransmit as described in Section 7.2.4 and if no retransmit
 *     timer is running for the destination address to which the DATA
 *     chunk was originally transmitted, then T3-rtx is started for
 *     that destination address.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_eat_sack_6_2(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	sctp_sackhdr_t *sackh;
	__u32 ctsn;

	if (!sctp_vtag_verify(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the SACK chunk has a valid length. */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_sack_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	/* Pull the SACK chunk from the data buffer */
	sackh = sctp_sm_pull_sack(chunk);
	/* Was this a bogus SACK? */
	if (!sackh)
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
	chunk->subh.sack_hdr = sackh;
	ctsn = ntohl(sackh->cum_tsn_ack);

	/* i) If Cumulative TSN Ack is less than the Cumulative TSN
	 *     Ack Point, then drop the SACK.  Since Cumulative TSN
	 *     Ack is monotonically increasing, a SACK whose
	 *     Cumulative TSN Ack is less than the Cumulative TSN Ack
	 *     Point indicates an out-of-order SACK.
	 */
	if (TSN_lt(ctsn, asoc->ctsn_ack_point)) {
		SCTP_DEBUG_PRINTK("ctsn %x\n", ctsn);
		SCTP_DEBUG_PRINTK("ctsn_ack_point %x\n", asoc->ctsn_ack_point);
		return SCTP_DISPOSITION_DISCARD;
	}

	/* If Cumulative TSN Ack beyond the max tsn currently
	 * send, terminating the association and respond to the
	 * sender with an ABORT.
	 */
	if (!TSN_lt(ctsn, asoc->next_tsn))
		return sctp_sf_violation_ctsn(ep, asoc, type, arg, commands);

	/* Return this SACK for further processing.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_PROCESS_SACK, SCTP_SACKH(sackh));

	/* Note: We do the rest of the work on the PROCESS_SACK
	 * sideeffect.
	 */
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Generate an ABORT in response to a packet.
 *
 * Section: 8.4 Handle "Out of the blue" Packets, sctpimpguide 2.41
 *
 * 8) The receiver should respond to the sender of the OOTB packet with
 *    an ABORT.  When sending the ABORT, the receiver of the OOTB packet
 *    MUST fill in the Verification Tag field of the outbound packet
 *    with the value found in the Verification Tag field of the OOTB
 *    packet and set the T-bit in the Chunk Flags to indicate that the
 *    Verification Tag is reflected.  After sending this ABORT, the
 *    receiver of the OOTB packet shall discard the OOTB packet and take
 *    no further action.
 *
 * Verification Tag:
 *
 * The return value is the disposition of the chunk.
*/
static sctp_disposition_t sctp_sf_tabort_8_4_8(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_packet *packet = NULL;
	struct sctp_chunk *chunk = arg;
	struct sctp_chunk *abort;

	packet = sctp_ootb_pkt_new(asoc, chunk);

	if (packet) {
		/* Make an ABORT. The T bit will be set if the asoc
		 * is NULL.
		 */
		abort = sctp_make_abort(asoc, chunk, 0);
		if (!abort) {
			sctp_ootb_pkt_free(packet);
			return SCTP_DISPOSITION_NOMEM;
		}

		/* Reflect vtag if T-Bit is set */
		if (sctp_test_T_bit(abort))
			packet->vtag = ntohl(chunk->sctp_hdr->vtag);

		/* Set the skb to the belonging sock for accounting.  */
		abort->skb->sk = ep->base.sk;

		sctp_packet_append_chunk(packet, abort);

		sctp_add_cmd_sf(commands, SCTP_CMD_SEND_PKT,
				SCTP_PACKET(packet));

		SCTP_INC_STATS(SCTP_MIB_OUTCTRLCHUNKS);

		sctp_sf_pdiscard(ep, asoc, type, arg, commands);
		return SCTP_DISPOSITION_CONSUME;
	}

	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Received an ERROR chunk from peer.  Generate SCTP_REMOTE_ERROR
 * event as ULP notification for each cause included in the chunk.
 *
 * API 5.3.1.3 - SCTP_REMOTE_ERROR
 *
 * The return value is the disposition of the chunk.
*/
sctp_disposition_t sctp_sf_operr_notify(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;

	if (!sctp_vtag_verify(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the ERROR chunk has a valid length. */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_operr_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	sctp_add_cmd_sf(commands, SCTP_CMD_PROCESS_OPERR,
			SCTP_CHUNK(chunk));

	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Process an inbound SHUTDOWN ACK.
 *
 * From Section 9.2:
 * Upon the receipt of the SHUTDOWN ACK, the SHUTDOWN sender shall
 * stop the T2-shutdown timer, send a SHUTDOWN COMPLETE chunk to its
 * peer, and remove all record of the association.
 *
 * The return value is the disposition.
 */
sctp_disposition_t sctp_sf_do_9_2_final(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	struct sctp_chunk *reply;
	struct sctp_ulpevent *ev;

	if (!sctp_vtag_verify(chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the SHUTDOWN_ACK chunk has a valid length. */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_chunkhdr_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);
	/* 10.2 H) SHUTDOWN COMPLETE notification
	 *
	 * When SCTP completes the shutdown procedures (section 9.2) this
	 * notification is passed to the upper layer.
	 */
	ev = sctp_ulpevent_make_assoc_change(asoc, 0, SCTP_SHUTDOWN_COMP,
					     0, 0, 0, NULL, GFP_ATOMIC);
	if (!ev)
		goto nomem;

	/* ...send a SHUTDOWN COMPLETE chunk to its peer, */
	reply = sctp_make_shutdown_complete(asoc, chunk);
	if (!reply)
		goto nomem_chunk;

	/* Do all the commands now (after allocation), so that we
	 * have consistent state if memory allocation failes
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP, SCTP_ULPEVENT(ev));

	/* Upon the receipt of the SHUTDOWN ACK, the SHUTDOWN sender shall
	 * stop the T2-shutdown timer,
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));

	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T5_SHUTDOWN_GUARD));

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));
	SCTP_INC_STATS(SCTP_MIB_SHUTDOWNS);
	SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));

	/* ...and remove all record of the association. */
	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());
	return SCTP_DISPOSITION_DELETE_TCB;

nomem_chunk:
	sctp_ulpevent_free(ev);
nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * RFC 2960, 8.4 - Handle "Out of the blue" Packets, sctpimpguide 2.41.
 *
 * 5) If the packet contains a SHUTDOWN ACK chunk, the receiver should
 *    respond to the sender of the OOTB packet with a SHUTDOWN COMPLETE.
 *    When sending the SHUTDOWN COMPLETE, the receiver of the OOTB
 *    packet must fill in the Verification Tag field of the outbound
 *    packet with the Verification Tag received in the SHUTDOWN ACK and
 *    set the T-bit in the Chunk Flags to indicate that the Verification
 *    Tag is reflected.
 *
 * 8) The receiver should respond to the sender of the OOTB packet with
 *    an ABORT.  When sending the ABORT, the receiver of the OOTB packet
 *    MUST fill in the Verification Tag field of the outbound packet
 *    with the value found in the Verification Tag field of the OOTB
 *    packet and set the T-bit in the Chunk Flags to indicate that the
 *    Verification Tag is reflected.  After sending this ABORT, the
 *    receiver of the OOTB packet shall discard the OOTB packet and take
 *    no further action.
 */
sctp_disposition_t sctp_sf_ootb(const struct sctp_endpoint *ep,
				const struct sctp_association *asoc,
				const sctp_subtype_t type,
				void *arg,
				sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	struct sk_buff *skb = chunk->skb;
	sctp_chunkhdr_t *ch;
	__u8 *ch_end;
	int ootb_shut_ack = 0;

	SCTP_INC_STATS(SCTP_MIB_OUTOFBLUES);

	ch = (sctp_chunkhdr_t *) chunk->chunk_hdr;
	do {
		/* Report violation if the chunk is less then minimal */
		if (ntohs(ch->length) < sizeof(sctp_chunkhdr_t))
			return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

		/* Now that we know we at least have a chunk header,
		 * do things that are type appropriate.
		 */
		if (SCTP_CID_SHUTDOWN_ACK == ch->type)
			ootb_shut_ack = 1;

		/* RFC 2960, Section 3.3.7
		 *   Moreover, under any circumstances, an endpoint that
		 *   receives an ABORT  MUST NOT respond to that ABORT by
		 *   sending an ABORT of its own.
		 */
		if (SCTP_CID_ABORT == ch->type)
			return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

		/* Report violation if chunk len overflows */
		ch_end = ((__u8 *)ch) + WORD_ROUND(ntohs(ch->length));
		if (ch_end > skb_tail_pointer(skb))
			return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

		ch = (sctp_chunkhdr_t *) ch_end;
	} while (ch_end < skb_tail_pointer(skb));

	if (ootb_shut_ack)
		return sctp_sf_shut_8_4_5(ep, asoc, type, arg, commands);
	else
		return sctp_sf_tabort_8_4_8(ep, asoc, type, arg, commands);
}

/*
 * Handle an "Out of the blue" SHUTDOWN ACK.
 *
 * Section: 8.4 5, sctpimpguide 2.41.
 *
 * 5) If the packet contains a SHUTDOWN ACK chunk, the receiver should
 *    respond to the sender of the OOTB packet with a SHUTDOWN COMPLETE.
 *    When sending the SHUTDOWN COMPLETE, the receiver of the OOTB
 *    packet must fill in the Verification Tag field of the outbound
 *    packet with the Verification Tag received in the SHUTDOWN ACK and
 *    set the T-bit in the Chunk Flags to indicate that the Verification
 *    Tag is reflected.
 *
 * Inputs
 * (endpoint, asoc, type, arg, commands)
 *
 * Outputs
 * (sctp_disposition_t)
 *
 * The return value is the disposition of the chunk.
 */
static sctp_disposition_t sctp_sf_shut_8_4_5(const struct sctp_endpoint *ep,
					     const struct sctp_association *asoc,
					     const sctp_subtype_t type,
					     void *arg,
					     sctp_cmd_seq_t *commands)
{
	struct sctp_packet *packet = NULL;
	struct sctp_chunk *chunk = arg;
	struct sctp_chunk *shut;

	packet = sctp_ootb_pkt_new(asoc, chunk);

	if (packet) {
		/* Make an SHUTDOWN_COMPLETE.
		 * The T bit will be set if the asoc is NULL.
		 */
		shut = sctp_make_shutdown_complete(asoc, chunk);
		if (!shut) {
			sctp_ootb_pkt_free(packet);
			return SCTP_DISPOSITION_NOMEM;
		}

		/* Reflect vtag if T-Bit is set */
		if (sctp_test_T_bit(shut))
			packet->vtag = ntohl(chunk->sctp_hdr->vtag);

		/* Set the skb to the belonging sock for accounting.  */
		shut->skb->sk = ep->base.sk;

		sctp_packet_append_chunk(packet, shut);

		sctp_add_cmd_sf(commands, SCTP_CMD_SEND_PKT,
				SCTP_PACKET(packet));

		SCTP_INC_STATS(SCTP_MIB_OUTCTRLCHUNKS);

		/* If the chunk length is invalid, we don't want to process
		 * the reset of the packet.
		 */
		if (!sctp_chunk_length_valid(chunk, sizeof(sctp_chunkhdr_t)))
			return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

		/* We need to discard the rest of the packet to prevent
		 * potential bomming attacks from additional bundled chunks.
		 * This is documented in SCTP Threats ID.
		 */
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
	}

	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Handle SHUTDOWN ACK in COOKIE_ECHOED or COOKIE_WAIT state.
 *
 * Verification Tag:  8.5.1 E) Rules for packet carrying a SHUTDOWN ACK
 *   If the receiver is in COOKIE-ECHOED or COOKIE-WAIT state the
 *   procedures in section 8.4 SHOULD be followed, in other words it
 *   should be treated as an Out Of The Blue packet.
 *   [This means that we do NOT check the Verification Tag on these
 *   chunks. --piggy ]
 *
 */
sctp_disposition_t sctp_sf_do_8_5_1_E_sa(const struct sctp_endpoint *ep,
				      const struct sctp_association *asoc,
				      const sctp_subtype_t type,
				      void *arg,
				      sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;

	/* Make sure that the SHUTDOWN_ACK chunk has a valid length. */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_chunkhdr_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	/* Although we do have an association in this case, it corresponds
	 * to a restarted association. So the packet is treated as an OOTB
	 * packet and the state function that handles OOTB SHUTDOWN_ACK is
	 * called with a NULL association.
	 */
	SCTP_INC_STATS(SCTP_MIB_OUTOFBLUES);

	return sctp_sf_shut_8_4_5(ep, NULL, type, arg, commands);
}

/* ADDIP Section 4.2 Upon reception of an ASCONF Chunk.  */
sctp_disposition_t sctp_sf_do_asconf(const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type, void *arg,
				     sctp_cmd_seq_t *commands)
{
	struct sctp_chunk	*chunk = arg;
	struct sctp_chunk	*asconf_ack = NULL;
	struct sctp_paramhdr	*err_param = NULL;
	sctp_addiphdr_t		*hdr;
	union sctp_addr_param	*addr_param;
	__u32			serial;
	int			length;

	if (!sctp_vtag_verify(chunk, asoc)) {
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_BAD_TAG,
				SCTP_NULL());
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
	}

	/* ADD-IP: Section 4.1.1
	 * This chunk MUST be sent in an authenticated way by using
	 * the mechanism defined in [I-D.ietf-tsvwg-sctp-auth]. If this chunk
	 * is received unauthenticated it MUST be silently discarded as
	 * described in [I-D.ietf-tsvwg-sctp-auth].
	 */
	if (!sctp_addip_noauth && !chunk->auth)
		return sctp_sf_discard_chunk(ep, asoc, type, arg, commands);

	/* Make sure that the ASCONF ADDIP chunk has a valid length.  */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_addip_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	hdr = (sctp_addiphdr_t *)chunk->skb->data;
	serial = ntohl(hdr->serial);

	addr_param = (union sctp_addr_param *)hdr->params;
	length = ntohs(addr_param->p.length);
	if (length < sizeof(sctp_paramhdr_t))
		return sctp_sf_violation_paramlen(ep, asoc, type, arg,
			   (void *)addr_param, commands);

	/* Verify the ASCONF chunk before processing it. */
	if (!sctp_verify_asconf(asoc,
			    (sctp_paramhdr_t *)((void *)addr_param + length),
			    (void *)chunk->chunk_end,
			    &err_param))
		return sctp_sf_violation_paramlen(ep, asoc, type, arg,
						  (void *)err_param, commands);

	/* ADDIP 5.2 E1) Compare the value of the serial number to the value
	 * the endpoint stored in a new association variable
	 * 'Peer-Serial-Number'.
	 */
	if (serial == asoc->peer.addip_serial + 1) {
		/* If this is the first instance of ASCONF in the packet,
		 * we can clean our old ASCONF-ACKs.
		 */
		if (!chunk->has_asconf)
			sctp_assoc_clean_asconf_ack_cache(asoc);

		/* ADDIP 5.2 E4) When the Sequence Number matches the next one
		 * expected, process the ASCONF as described below and after
		 * processing the ASCONF Chunk, append an ASCONF-ACK Chunk to
		 * the response packet and cache a copy of it (in the event it
		 * later needs to be retransmitted).
		 *
		 * Essentially, do V1-V5.
		 */
		asconf_ack = sctp_process_asconf((struct sctp_association *)
						 asoc, chunk);
		if (!asconf_ack)
			return SCTP_DISPOSITION_NOMEM;
	} else if (serial < asoc->peer.addip_serial + 1) {
		/* ADDIP 5.2 E2)
		 * If the value found in the Sequence Number is less than the
		 * ('Peer- Sequence-Number' + 1), simply skip to the next
		 * ASCONF, and include in the outbound response packet
		 * any previously cached ASCONF-ACK response that was
		 * sent and saved that matches the Sequence Number of the
		 * ASCONF.  Note: It is possible that no cached ASCONF-ACK
		 * Chunk exists.  This will occur when an older ASCONF
		 * arrives out of order.  In such a case, the receiver
		 * should skip the ASCONF Chunk and not include ASCONF-ACK
		 * Chunk for that chunk.
		 */
		asconf_ack = sctp_assoc_lookup_asconf_ack(asoc, hdr->serial);
		if (!asconf_ack)
			return SCTP_DISPOSITION_DISCARD;

		/* Reset the transport so that we select the correct one
		 * this time around.  This is to make sure that we don't
		 * accidentally use a stale transport that's been removed.
		 */
		asconf_ack->transport = NULL;
	} else {
		/* ADDIP 5.2 E5) Otherwise, the ASCONF Chunk is discarded since
		 * it must be either a stale packet or from an attacker.
		 */
		return SCTP_DISPOSITION_DISCARD;
	}

	/* ADDIP 5.2 E6)  The destination address of the SCTP packet
	 * containing the ASCONF-ACK Chunks MUST be the source address of
	 * the SCTP packet that held the ASCONF Chunks.
	 *
	 * To do this properly, we'll set the destination address of the chunk
	 * and at the transmit time, will try look up the transport to use.
	 * Since ASCONFs may be bundled, the correct transport may not be
	 * created until we process the entire packet, thus this workaround.
	 */
	asconf_ack->dest = chunk->source;
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(asconf_ack));

	return SCTP_DISPOSITION_CONSUME;
}

/*
 * ADDIP Section 4.3 General rules for address manipulation
 * When building TLV parameters for the ASCONF Chunk that will add or
 * delete IP addresses the D0 to D13 rules should be applied:
 */
sctp_disposition_t sctp_sf_do_asconf_ack(const struct sctp_endpoint *ep,
					 const struct sctp_association *asoc,
					 const sctp_subtype_t type, void *arg,
					 sctp_cmd_seq_t *commands)
{
	struct sctp_chunk	*asconf_ack = arg;
	struct sctp_chunk	*last_asconf = asoc->addip_last_asconf;
	struct sctp_chunk	*abort;
	struct sctp_paramhdr	*err_param = NULL;
	sctp_addiphdr_t		*addip_hdr;
	__u32			sent_serial, rcvd_serial;

	if (!sctp_vtag_verify(asconf_ack, asoc)) {
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_BAD_TAG,
				SCTP_NULL());
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
	}

	/* ADD-IP, Section 4.1.2:
	 * This chunk MUST be sent in an authenticated way by using
	 * the mechanism defined in [I-D.ietf-tsvwg-sctp-auth]. If this chunk
	 * is received unauthenticated it MUST be silently discarded as
	 * described in [I-D.ietf-tsvwg-sctp-auth].
	 */
	if (!sctp_addip_noauth && !asconf_ack->auth)
		return sctp_sf_discard_chunk(ep, asoc, type, arg, commands);

	/* Make sure that the ADDIP chunk has a valid length.  */
	if (!sctp_chunk_length_valid(asconf_ack, sizeof(sctp_addip_chunk_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	addip_hdr = (sctp_addiphdr_t *)asconf_ack->skb->data;
	rcvd_serial = ntohl(addip_hdr->serial);

	/* Verify the ASCONF-ACK chunk before processing it. */
	if (!sctp_verify_asconf(asoc,
	    (sctp_paramhdr_t *)addip_hdr->params,
	    (void *)asconf_ack->chunk_end,
	    &err_param))
		return sctp_sf_violation_paramlen(ep, asoc, type, arg,
			   (void *)err_param, commands);

	if (last_asconf) {
		addip_hdr = (sctp_addiphdr_t *)last_asconf->subh.addip_hdr;
		sent_serial = ntohl(addip_hdr->serial);
	} else {
		sent_serial = asoc->addip_serial - 1;
	}

	/* D0) If an endpoint receives an ASCONF-ACK that is greater than or
	 * equal to the next serial number to be used but no ASCONF chunk is
	 * outstanding the endpoint MUST ABORT the association. Note that a
	 * sequence number is greater than if it is no more than 2^^31-1
	 * larger than the current sequence number (using serial arithmetic).
	 */
	if (ADDIP_SERIAL_gte(rcvd_serial, sent_serial + 1) &&
	    !(asoc->addip_last_asconf)) {
		abort = sctp_make_abort(asoc, asconf_ack,
					sizeof(sctp_errhdr_t));
		if (abort) {
			sctp_init_cause(abort, SCTP_ERROR_ASCONF_ACK, 0);
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(abort));
		}
		/* We are going to ABORT, so we might as well stop
		 * processing the rest of the chunks in the packet.
		 */
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
				SCTP_TO(SCTP_EVENT_TIMEOUT_T4_RTO));
		sctp_add_cmd_sf(commands, SCTP_CMD_DISCARD_PACKET,SCTP_NULL());
		sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
				SCTP_ERROR(ECONNABORTED));
		sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED,
				SCTP_PERR(SCTP_ERROR_ASCONF_ACK));
		SCTP_INC_STATS(SCTP_MIB_ABORTEDS);
		SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);
		return SCTP_DISPOSITION_ABORT;
	}

	if ((rcvd_serial == sent_serial) && asoc->addip_last_asconf) {
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
				SCTP_TO(SCTP_EVENT_TIMEOUT_T4_RTO));

		if (!sctp_process_asconf_ack((struct sctp_association *)asoc,
					     asconf_ack)) {
			/* Successfully processed ASCONF_ACK.  We can
			 * release the next asconf if we have one.
			 */
			sctp_add_cmd_sf(commands, SCTP_CMD_SEND_NEXT_ASCONF,
					SCTP_NULL());
			return SCTP_DISPOSITION_CONSUME;
		}

		abort = sctp_make_abort(asoc, asconf_ack,
					sizeof(sctp_errhdr_t));
		if (abort) {
			sctp_init_cause(abort, SCTP_ERROR_RSRC_LOW, 0);
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(abort));
		}
		/* We are going to ABORT, so we might as well stop
		 * processing the rest of the chunks in the packet.
		 */
		sctp_add_cmd_sf(commands, SCTP_CMD_DISCARD_PACKET,SCTP_NULL());
		sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
				SCTP_ERROR(ECONNABORTED));
		sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED,
				SCTP_PERR(SCTP_ERROR_ASCONF_ACK));
		SCTP_INC_STATS(SCTP_MIB_ABORTEDS);
		SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);
		return SCTP_DISPOSITION_ABORT;
	}

	return SCTP_DISPOSITION_DISCARD;
}

/*
 * PR-SCTP Section 3.6 Receiver Side Implementation of PR-SCTP
 *
 * When a FORWARD TSN chunk arrives, the data receiver MUST first update
 * its cumulative TSN point to the value carried in the FORWARD TSN
 * chunk, and then MUST further advance its cumulative TSN point locally
 * if possible.
 * After the above processing, the data receiver MUST stop reporting any
 * missing TSNs earlier than or equal to the new cumulative TSN point.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_eat_fwd_tsn(const struct sctp_endpoint *ep,
				       const struct sctp_association *asoc,
				       const sctp_subtype_t type,
				       void *arg,
				       sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	struct sctp_fwdtsn_hdr *fwdtsn_hdr;
	struct sctp_fwdtsn_skip *skip;
	__u16 len;
	__u32 tsn;

	if (!sctp_vtag_verify(chunk, asoc)) {
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_BAD_TAG,
				SCTP_NULL());
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
	}

	/* Make sure that the FORWARD_TSN chunk has valid length.  */
	if (!sctp_chunk_length_valid(chunk, sizeof(struct sctp_fwdtsn_chunk)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	fwdtsn_hdr = (struct sctp_fwdtsn_hdr *)chunk->skb->data;
	chunk->subh.fwdtsn_hdr = fwdtsn_hdr;
	len = ntohs(chunk->chunk_hdr->length);
	len -= sizeof(struct sctp_chunkhdr);
	skb_pull(chunk->skb, len);

	tsn = ntohl(fwdtsn_hdr->new_cum_tsn);
	SCTP_DEBUG_PRINTK("%s: TSN 0x%x.\n", __func__, tsn);

	/* The TSN is too high--silently discard the chunk and count on it
	 * getting retransmitted later.
	 */
	if (sctp_tsnmap_check(&asoc->peer.tsn_map, tsn) < 0)
		goto discard_noforce;

	/* Silently discard the chunk if stream-id is not valid */
	sctp_walk_fwdtsn(skip, chunk) {
		if (ntohs(skip->stream) >= asoc->c.sinit_max_instreams)
			goto discard_noforce;
	}

	sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_FWDTSN, SCTP_U32(tsn));
	if (len > sizeof(struct sctp_fwdtsn_hdr))
		sctp_add_cmd_sf(commands, SCTP_CMD_PROCESS_FWDTSN,
				SCTP_CHUNK(chunk));

	/* Count this as receiving DATA. */
	if (asoc->autoclose) {
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
				SCTP_TO(SCTP_EVENT_TIMEOUT_AUTOCLOSE));
	}

	/* FIXME: For now send a SACK, but DATA processing may
	 * send another.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SACK, SCTP_NOFORCE());

	return SCTP_DISPOSITION_CONSUME;

discard_noforce:
	return SCTP_DISPOSITION_DISCARD;
}

sctp_disposition_t sctp_sf_eat_fwd_tsn_fast(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;
	struct sctp_fwdtsn_hdr *fwdtsn_hdr;
	struct sctp_fwdtsn_skip *skip;
	__u16 len;
	__u32 tsn;

	if (!sctp_vtag_verify(chunk, asoc)) {
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_BAD_TAG,
				SCTP_NULL());
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
	}

	/* Make sure that the FORWARD_TSN chunk has a valid length.  */
	if (!sctp_chunk_length_valid(chunk, sizeof(struct sctp_fwdtsn_chunk)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	fwdtsn_hdr = (struct sctp_fwdtsn_hdr *)chunk->skb->data;
	chunk->subh.fwdtsn_hdr = fwdtsn_hdr;
	len = ntohs(chunk->chunk_hdr->length);
	len -= sizeof(struct sctp_chunkhdr);
	skb_pull(chunk->skb, len);

	tsn = ntohl(fwdtsn_hdr->new_cum_tsn);
	SCTP_DEBUG_PRINTK("%s: TSN 0x%x.\n", __func__, tsn);

	/* The TSN is too high--silently discard the chunk and count on it
	 * getting retransmitted later.
	 */
	if (sctp_tsnmap_check(&asoc->peer.tsn_map, tsn) < 0)
		goto gen_shutdown;

	/* Silently discard the chunk if stream-id is not valid */
	sctp_walk_fwdtsn(skip, chunk) {
		if (ntohs(skip->stream) >= asoc->c.sinit_max_instreams)
			goto gen_shutdown;
	}

	sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_FWDTSN, SCTP_U32(tsn));
	if (len > sizeof(struct sctp_fwdtsn_hdr))
		sctp_add_cmd_sf(commands, SCTP_CMD_PROCESS_FWDTSN,
				SCTP_CHUNK(chunk));

	/* Go a head and force a SACK, since we are shutting down. */
gen_shutdown:
	/* Implementor's Guide.
	 *
	 * While in SHUTDOWN-SENT state, the SHUTDOWN sender MUST immediately
	 * respond to each received packet containing one or more DATA chunk(s)
	 * with a SACK, a SHUTDOWN chunk, and restart the T2-shutdown timer
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SHUTDOWN, SCTP_NULL());
	sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SACK, SCTP_FORCE());
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));

	return SCTP_DISPOSITION_CONSUME;
}

/*
 * SCTP-AUTH Section 6.3 Receving authenticated chukns
 *
 *    The receiver MUST use the HMAC algorithm indicated in the HMAC
 *    Identifier field.  If this algorithm was not specified by the
 *    receiver in the HMAC-ALGO parameter in the INIT or INIT-ACK chunk
 *    during association setup, the AUTH chunk and all chunks after it MUST
 *    be discarded and an ERROR chunk SHOULD be sent with the error cause
 *    defined in Section 4.1.
 *
 *    If an endpoint with no shared key receives a Shared Key Identifier
 *    other than 0, it MUST silently discard all authenticated chunks.  If
 *    the endpoint has at least one endpoint pair shared key for the peer,
 *    it MUST use the key specified by the Shared Key Identifier if a
 *    key has been configured for that Shared Key Identifier.  If no
 *    endpoint pair shared key has been configured for that Shared Key
 *    Identifier, all authenticated chunks MUST be silently discarded.
 *
 * Verification Tag:  8.5 Verification Tag [Normal verification]
 *
 * The return value is the disposition of the chunk.
 */
static sctp_ierror_t sctp_sf_authenticate(const struct sctp_endpoint *ep,
				    const struct sctp_association *asoc,
				    const sctp_subtype_t type,
				    struct sctp_chunk *chunk)
{
	struct sctp_authhdr *auth_hdr;
	struct sctp_hmac *hmac;
	unsigned int sig_len;
	__u16 key_id;
	__u8 *save_digest;
	__u8 *digest;

	/* Pull in the auth header, so we can do some more verification */
	auth_hdr = (struct sctp_authhdr *)chunk->skb->data;
	chunk->subh.auth_hdr = auth_hdr;
	skb_pull(chunk->skb, sizeof(struct sctp_authhdr));

	/* Make sure that we suport the HMAC algorithm from the auth
	 * chunk.
	 */
	if (!sctp_auth_asoc_verify_hmac_id(asoc, auth_hdr->hmac_id))
		return SCTP_IERROR_AUTH_BAD_HMAC;

	/* Make sure that the provided shared key identifier has been
	 * configured
	 */
	key_id = ntohs(auth_hdr->shkey_id);
	if (key_id != asoc->active_key_id && !sctp_auth_get_shkey(asoc, key_id))
		return SCTP_IERROR_AUTH_BAD_KEYID;


	/* Make sure that the length of the signature matches what
	 * we expect.
	 */
	sig_len = ntohs(chunk->chunk_hdr->length) - sizeof(sctp_auth_chunk_t);
	hmac = sctp_auth_get_hmac(ntohs(auth_hdr->hmac_id));
	if (sig_len != hmac->hmac_len)
		return SCTP_IERROR_PROTO_VIOLATION;

	/* Now that we've done validation checks, we can compute and
	 * verify the hmac.  The steps involved are:
	 *  1. Save the digest from the chunk.
	 *  2. Zero out the digest in the chunk.
	 *  3. Compute the new digest
	 *  4. Compare saved and new digests.
	 */
	digest = auth_hdr->hmac;
	skb_pull(chunk->skb, sig_len);

	save_digest = kmemdup(digest, sig_len, GFP_ATOMIC);
	if (!save_digest)
		goto nomem;

	memset(digest, 0, sig_len);

	sctp_auth_calculate_hmac(asoc, chunk->skb,
				(struct sctp_auth_chunk *)chunk->chunk_hdr,
				GFP_ATOMIC);

	/* Discard the packet if the digests do not match */
	if (memcmp(save_digest, digest, sig_len)) {
		kfree(save_digest);
		return SCTP_IERROR_BAD_SIG;
	}

	kfree(save_digest);
	chunk->auth = 1;

	return SCTP_IERROR_NO_ERROR;
nomem:
	return SCTP_IERROR_NOMEM;
}

sctp_disposition_t sctp_sf_eat_auth(const struct sctp_endpoint *ep,
				    const struct sctp_association *asoc,
				    const sctp_subtype_t type,
				    void *arg,
				    sctp_cmd_seq_t *commands)
{
	struct sctp_authhdr *auth_hdr;
	struct sctp_chunk *chunk = arg;
	struct sctp_chunk *err_chunk;
	sctp_ierror_t error;

	/* Make sure that the peer has AUTH capable */
	if (!asoc->peer.auth_capable)
		return sctp_sf_unk_chunk(ep, asoc, type, arg, commands);

	if (!sctp_vtag_verify(chunk, asoc)) {
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_BAD_TAG,
				SCTP_NULL());
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
	}

	/* Make sure that the AUTH chunk has valid length.  */
	if (!sctp_chunk_length_valid(chunk, sizeof(struct sctp_auth_chunk)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	auth_hdr = (struct sctp_authhdr *)chunk->skb->data;
	error = sctp_sf_authenticate(ep, asoc, type, chunk);
	switch (error) {
		case SCTP_IERROR_AUTH_BAD_HMAC:
			/* Generate the ERROR chunk and discard the rest
			 * of the packet
			 */
			err_chunk = sctp_make_op_error(asoc, chunk,
							SCTP_ERROR_UNSUP_HMAC,
							&auth_hdr->hmac_id,
							sizeof(__u16), 0);
			if (err_chunk) {
				sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
						SCTP_CHUNK(err_chunk));
			}
			/* Fall Through */
		case SCTP_IERROR_AUTH_BAD_KEYID:
		case SCTP_IERROR_BAD_SIG:
			return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
			break;
		case SCTP_IERROR_PROTO_VIOLATION:
			return sctp_sf_violation_chunklen(ep, asoc, type, arg,
							  commands);
			break;
		case SCTP_IERROR_NOMEM:
			return SCTP_DISPOSITION_NOMEM;
		default:
			break;
	}

	if (asoc->active_key_id != ntohs(auth_hdr->shkey_id)) {
		struct sctp_ulpevent *ev;

		ev = sctp_ulpevent_make_authkey(asoc, ntohs(auth_hdr->shkey_id),
				    SCTP_AUTH_NEWKEY, GFP_ATOMIC);

		if (!ev)
			return -ENOMEM;

		sctp_add_cmd_sf(commands, SCTP_CMD_EVENT_ULP,
				SCTP_ULPEVENT(ev));
	}

	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Process an unknown chunk.
 *
 * Section: 3.2. Also, 2.1 in the implementor's guide.
 *
 * Chunk Types are encoded such that the highest-order two bits specify
 * the action that must be taken if the processing endpoint does not
 * recognize the Chunk Type.
 *
 * 00 - Stop processing this SCTP packet and discard it, do not process
 *      any further chunks within it.
 *
 * 01 - Stop processing this SCTP packet and discard it, do not process
 *      any further chunks within it, and report the unrecognized
 *      chunk in an 'Unrecognized Chunk Type'.
 *
 * 10 - Skip this chunk and continue processing.
 *
 * 11 - Skip this chunk and continue processing, but report in an ERROR
 *      Chunk using the 'Unrecognized Chunk Type' cause of error.
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_unk_chunk(const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type,
				     void *arg,
				     sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *unk_chunk = arg;
	struct sctp_chunk *err_chunk;
	sctp_chunkhdr_t *hdr;

	SCTP_DEBUG_PRINTK("Processing the unknown chunk id %d.\n", type.chunk);

	if (!sctp_vtag_verify(unk_chunk, asoc))
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

	/* Make sure that the chunk has a valid length.
	 * Since we don't know the chunk type, we use a general
	 * chunkhdr structure to make a comparison.
	 */
	if (!sctp_chunk_length_valid(unk_chunk, sizeof(sctp_chunkhdr_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	switch (type.chunk & SCTP_CID_ACTION_MASK) {
	case SCTP_CID_ACTION_DISCARD:
		/* Discard the packet.  */
		return sctp_sf_pdiscard(ep, asoc, type, arg, commands);
		break;
	case SCTP_CID_ACTION_DISCARD_ERR:
		/* Generate an ERROR chunk as response. */
		hdr = unk_chunk->chunk_hdr;
		err_chunk = sctp_make_op_error(asoc, unk_chunk,
					       SCTP_ERROR_UNKNOWN_CHUNK, hdr,
					       WORD_ROUND(ntohs(hdr->length)),
					       0);
		if (err_chunk) {
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(err_chunk));
		}

		/* Discard the packet.  */
		sctp_sf_pdiscard(ep, asoc, type, arg, commands);
		return SCTP_DISPOSITION_CONSUME;
		break;
	case SCTP_CID_ACTION_SKIP:
		/* Skip the chunk.  */
		return SCTP_DISPOSITION_DISCARD;
		break;
	case SCTP_CID_ACTION_SKIP_ERR:
		/* Generate an ERROR chunk as response. */
		hdr = unk_chunk->chunk_hdr;
		err_chunk = sctp_make_op_error(asoc, unk_chunk,
					       SCTP_ERROR_UNKNOWN_CHUNK, hdr,
					       WORD_ROUND(ntohs(hdr->length)),
					       0);
		if (err_chunk) {
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(err_chunk));
		}
		/* Skip the chunk.  */
		return SCTP_DISPOSITION_CONSUME;
		break;
	default:
		break;
	}

	return SCTP_DISPOSITION_DISCARD;
}

/*
 * Discard the chunk.
 *
 * Section: 0.2, 5.2.3, 5.2.5, 5.2.6, 6.0, 8.4.6, 8.5.1c, 9.2
 * [Too numerous to mention...]
 * Verification Tag: No verification needed.
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_discard_chunk(const struct sctp_endpoint *ep,
					 const struct sctp_association *asoc,
					 const sctp_subtype_t type,
					 void *arg,
					 sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;

	/* Make sure that the chunk has a valid length.
	 * Since we don't know the chunk type, we use a general
	 * chunkhdr structure to make a comparison.
	 */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_chunkhdr_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	SCTP_DEBUG_PRINTK("Chunk %d is discarded\n", type.chunk);
	return SCTP_DISPOSITION_DISCARD;
}

/*
 * Discard the whole packet.
 *
 * Section: 8.4 2)
 *
 * 2) If the OOTB packet contains an ABORT chunk, the receiver MUST
 *    silently discard the OOTB packet and take no further action.
 *
 * Verification Tag: No verification necessary
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_pdiscard(const struct sctp_endpoint *ep,
				    const struct sctp_association *asoc,
				    const sctp_subtype_t type,
				    void *arg,
				    sctp_cmd_seq_t *commands)
{
	SCTP_INC_STATS(SCTP_MIB_IN_PKT_DISCARDS);
	sctp_add_cmd_sf(commands, SCTP_CMD_DISCARD_PACKET, SCTP_NULL());

	return SCTP_DISPOSITION_CONSUME;
}


/*
 * The other end is violating protocol.
 *
 * Section: Not specified
 * Verification Tag: Not specified
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (asoc, reply_msg, msg_up, timers, counters)
 *
 * We simply tag the chunk as a violation.  The state machine will log
 * the violation and continue.
 */
sctp_disposition_t sctp_sf_violation(const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type,
				     void *arg,
				     sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;

	/* Make sure that the chunk has a valid length. */
	if (!sctp_chunk_length_valid(chunk, sizeof(sctp_chunkhdr_t)))
		return sctp_sf_violation_chunklen(ep, asoc, type, arg,
						  commands);

	return SCTP_DISPOSITION_VIOLATION;
}

/*
 * Common function to handle a protocol violation.
 */
static sctp_disposition_t sctp_sf_abort_violation(
				     const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     void *arg,
				     sctp_cmd_seq_t *commands,
				     const __u8 *payload,
				     const size_t paylen)
{
	struct sctp_packet *packet = NULL;
	struct sctp_chunk *chunk =  arg;
	struct sctp_chunk *abort = NULL;

	/* SCTP-AUTH, Section 6.3:
	 *    It should be noted that if the receiver wants to tear
	 *    down an association in an authenticated way only, the
	 *    handling of malformed packets should not result in
	 *    tearing down the association.
	 *
	 * This means that if we only want to abort associations
	 * in an authenticated way (i.e AUTH+ABORT), then we
	 * can't destroy this association just becuase the packet
	 * was malformed.
	 */
	if (sctp_auth_recv_cid(SCTP_CID_ABORT, asoc))
		goto discard;

	/* Make the abort chunk. */
	abort = sctp_make_abort_violation(asoc, chunk, payload, paylen);
	if (!abort)
		goto nomem;

	if (asoc) {
		/* Treat INIT-ACK as a special case during COOKIE-WAIT. */
		if (chunk->chunk_hdr->type == SCTP_CID_INIT_ACK &&
		    !asoc->peer.i.init_tag) {
			sctp_initack_chunk_t *initack;

			initack = (sctp_initack_chunk_t *)chunk->chunk_hdr;
			if (!sctp_chunk_length_valid(chunk,
						     sizeof(sctp_initack_chunk_t)))
				abort->chunk_hdr->flags |= SCTP_CHUNK_FLAG_T;
			else {
				unsigned int inittag;

				inittag = ntohl(initack->init_hdr.init_tag);
				sctp_add_cmd_sf(commands, SCTP_CMD_UPDATE_INITTAG,
						SCTP_U32(inittag));
			}
		}

		sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(abort));
		SCTP_INC_STATS(SCTP_MIB_OUTCTRLCHUNKS);

		if (asoc->state <= SCTP_STATE_COOKIE_ECHOED) {
			sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
					SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));
			sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
					SCTP_ERROR(ECONNREFUSED));
			sctp_add_cmd_sf(commands, SCTP_CMD_INIT_FAILED,
					SCTP_PERR(SCTP_ERROR_PROTO_VIOLATION));
		} else {
			sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
					SCTP_ERROR(ECONNABORTED));
			sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED,
					SCTP_PERR(SCTP_ERROR_PROTO_VIOLATION));
			SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);
		}
	} else {
		packet = sctp_ootb_pkt_new(asoc, chunk);

		if (!packet)
			goto nomem_pkt;

		if (sctp_test_T_bit(abort))
			packet->vtag = ntohl(chunk->sctp_hdr->vtag);

		abort->skb->sk = ep->base.sk;

		sctp_packet_append_chunk(packet, abort);

		sctp_add_cmd_sf(commands, SCTP_CMD_SEND_PKT,
			SCTP_PACKET(packet));

		SCTP_INC_STATS(SCTP_MIB_OUTCTRLCHUNKS);
	}

	SCTP_INC_STATS(SCTP_MIB_ABORTEDS);

discard:
	sctp_sf_pdiscard(ep, asoc, SCTP_ST_CHUNK(0), arg, commands);
	return SCTP_DISPOSITION_ABORT;

nomem_pkt:
	sctp_chunk_free(abort);
nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Handle a protocol violation when the chunk length is invalid.
 * "Invalid" length is identified as smaller than the minimal length a
 * given chunk can be.  For example, a SACK chunk has invalid length
 * if its length is set to be smaller than the size of sctp_sack_chunk_t.
 *
 * We inform the other end by sending an ABORT with a Protocol Violation
 * error code.
 *
 * Section: Not specified
 * Verification Tag:  Nothing to do
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * Outputs
 * (reply_msg, msg_up, counters)
 *
 * Generate an  ABORT chunk and terminate the association.
 */
static sctp_disposition_t sctp_sf_violation_chunklen(
				     const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type,
				     void *arg,
				     sctp_cmd_seq_t *commands)
{
	static const char err_str[]="The following chunk had invalid length:";

	return sctp_sf_abort_violation(ep, asoc, arg, commands, err_str,
					sizeof(err_str));
}

/*
 * Handle a protocol violation when the parameter length is invalid.
 * "Invalid" length is identified as smaller than the minimal length a
 * given parameter can be.
 */
static sctp_disposition_t sctp_sf_violation_paramlen(
				     const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type,
				     void *arg, void *ext,
				     sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk =  arg;
	struct sctp_paramhdr *param = ext;
	struct sctp_chunk *abort = NULL;

	if (sctp_auth_recv_cid(SCTP_CID_ABORT, asoc))
		goto discard;

	/* Make the abort chunk. */
	abort = sctp_make_violation_paramlen(asoc, chunk, param);
	if (!abort)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(abort));
	SCTP_INC_STATS(SCTP_MIB_OUTCTRLCHUNKS);

	sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
			SCTP_ERROR(ECONNABORTED));
	sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED,
			SCTP_PERR(SCTP_ERROR_PROTO_VIOLATION));
	SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);
	SCTP_INC_STATS(SCTP_MIB_ABORTEDS);

discard:
	sctp_sf_pdiscard(ep, asoc, SCTP_ST_CHUNK(0), arg, commands);
	return SCTP_DISPOSITION_ABORT;
nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/* Handle a protocol violation when the peer trying to advance the
 * cumulative tsn ack to a point beyond the max tsn currently sent.
 *
 * We inform the other end by sending an ABORT with a Protocol Violation
 * error code.
 */
static sctp_disposition_t sctp_sf_violation_ctsn(
				     const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type,
				     void *arg,
				     sctp_cmd_seq_t *commands)
{
	static const char err_str[]="The cumulative tsn ack beyond the max tsn currently sent:";

	return sctp_sf_abort_violation(ep, asoc, arg, commands, err_str,
					sizeof(err_str));
}

/* Handle protocol violation of an invalid chunk bundling.  For example,
 * when we have an association and we recieve bundled INIT-ACK, or
 * SHUDOWN-COMPLETE, our peer is clearly violationg the "MUST NOT bundle"
 * statement from the specs.  Additinally, there might be an attacker
 * on the path and we may not want to continue this communication.
 */
static sctp_disposition_t sctp_sf_violation_chunk(
				     const struct sctp_endpoint *ep,
				     const struct sctp_association *asoc,
				     const sctp_subtype_t type,
				     void *arg,
				     sctp_cmd_seq_t *commands)
{
	static const char err_str[]="The following chunk violates protocol:";

	if (!asoc)
		return sctp_sf_violation(ep, asoc, type, arg, commands);

	return sctp_sf_abort_violation(ep, asoc, arg, commands, err_str,
					sizeof(err_str));
}
/***************************************************************************
 * These are the state functions for handling primitive (Section 10) events.
 ***************************************************************************/
/*
 * sctp_sf_do_prm_asoc
 *
 * Section: 10.1 ULP-to-SCTP
 * B) Associate
 *
 * Format: ASSOCIATE(local SCTP instance name, destination transport addr,
 * outbound stream count)
 * -> association id [,destination transport addr list] [,outbound stream
 * count]
 *
 * This primitive allows the upper layer to initiate an association to a
 * specific peer endpoint.
 *
 * The peer endpoint shall be specified by one of the transport addresses
 * which defines the endpoint (see Section 1.4).  If the local SCTP
 * instance has not been initialized, the ASSOCIATE is considered an
 * error.
 * [This is not relevant for the kernel implementation since we do all
 * initialization at boot time.  It we hadn't initialized we wouldn't
 * get anywhere near this code.]
 *
 * An association id, which is a local handle to the SCTP association,
 * will be returned on successful establishment of the association. If
 * SCTP is not able to open an SCTP association with the peer endpoint,
 * an error is returned.
 * [In the kernel implementation, the struct sctp_association needs to
 * be created BEFORE causing this primitive to run.]
 *
 * Other association parameters may be returned, including the
 * complete destination transport addresses of the peer as well as the
 * outbound stream count of the local endpoint. One of the transport
 * address from the returned destination addresses will be selected by
 * the local endpoint as default primary path for sending SCTP packets
 * to this peer.  The returned "destination transport addr list" can
 * be used by the ULP to change the default primary path or to force
 * sending a packet to a specific transport address.  [All of this
 * stuff happens when the INIT ACK arrives.  This is a NON-BLOCKING
 * function.]
 *
 * Mandatory attributes:
 *
 * o local SCTP instance name - obtained from the INITIALIZE operation.
 *   [This is the argument asoc.]
 * o destination transport addr - specified as one of the transport
 * addresses of the peer endpoint with which the association is to be
 * established.
 *  [This is asoc->peer.active_path.]
 * o outbound stream count - the number of outbound streams the ULP
 * would like to open towards this peer endpoint.
 * [BUG: This is not currently implemented.]
 * Optional attributes:
 *
 * None.
 *
 * The return value is a disposition.
 */
sctp_disposition_t sctp_sf_do_prm_asoc(const struct sctp_endpoint *ep,
				       const struct sctp_association *asoc,
				       const sctp_subtype_t type,
				       void *arg,
				       sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *repl;
	struct sctp_association* my_asoc;

	/* The comment below says that we enter COOKIE-WAIT AFTER
	 * sending the INIT, but that doesn't actually work in our
	 * implementation...
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_COOKIE_WAIT));

	/* RFC 2960 5.1 Normal Establishment of an Association
	 *
	 * A) "A" first sends an INIT chunk to "Z".  In the INIT, "A"
	 * must provide its Verification Tag (Tag_A) in the Initiate
	 * Tag field.  Tag_A SHOULD be a random number in the range of
	 * 1 to 4294967295 (see 5.3.1 for Tag value selection). ...
	 */

	repl = sctp_make_init(asoc, &asoc->base.bind_addr, GFP_ATOMIC, 0);
	if (!repl)
		goto nomem;

	/* Cast away the const modifier, as we want to just
	 * rerun it through as a sideffect.
	 */
	my_asoc = (struct sctp_association *)asoc;
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_ASOC, SCTP_ASOC(my_asoc));

	/* Choose transport for INIT. */
	sctp_add_cmd_sf(commands, SCTP_CMD_INIT_CHOOSE_TRANSPORT,
			SCTP_CHUNK(repl));

	/* After sending the INIT, "A" starts the T1-init timer and
	 * enters the COOKIE-WAIT state.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));
	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Process the SEND primitive.
 *
 * Section: 10.1 ULP-to-SCTP
 * E) Send
 *
 * Format: SEND(association id, buffer address, byte count [,context]
 *         [,stream id] [,life time] [,destination transport address]
 *         [,unorder flag] [,no-bundle flag] [,payload protocol-id] )
 * -> result
 *
 * This is the main method to send user data via SCTP.
 *
 * Mandatory attributes:
 *
 *  o association id - local handle to the SCTP association
 *
 *  o buffer address - the location where the user message to be
 *    transmitted is stored;
 *
 *  o byte count - The size of the user data in number of bytes;
 *
 * Optional attributes:
 *
 *  o context - an optional 32 bit integer that will be carried in the
 *    sending failure notification to the ULP if the transportation of
 *    this User Message fails.
 *
 *  o stream id - to indicate which stream to send the data on. If not
 *    specified, stream 0 will be used.
 *
 *  o life time - specifies the life time of the user data. The user data
 *    will not be sent by SCTP after the life time expires. This
 *    parameter can be used to avoid efforts to transmit stale
 *    user messages. SCTP notifies the ULP if the data cannot be
 *    initiated to transport (i.e. sent to the destination via SCTP's
 *    send primitive) within the life time variable. However, the
 *    user data will be transmitted if SCTP has attempted to transmit a
 *    chunk before the life time expired.
 *
 *  o destination transport address - specified as one of the destination
 *    transport addresses of the peer endpoint to which this packet
 *    should be sent. Whenever possible, SCTP should use this destination
 *    transport address for sending the packets, instead of the current
 *    primary path.
 *
 *  o unorder flag - this flag, if present, indicates that the user
 *    would like the data delivered in an unordered fashion to the peer
 *    (i.e., the U flag is set to 1 on all DATA chunks carrying this
 *    message).
 *
 *  o no-bundle flag - instructs SCTP not to bundle this user data with
 *    other outbound DATA chunks. SCTP MAY still bundle even when
 *    this flag is present, when faced with network congestion.
 *
 *  o payload protocol-id - A 32 bit unsigned integer that is to be
 *    passed to the peer indicating the type of payload protocol data
 *    being transmitted. This value is passed as opaque data by SCTP.
 *
 * The return value is the disposition.
 */
sctp_disposition_t sctp_sf_do_prm_send(const struct sctp_endpoint *ep,
				       const struct sctp_association *asoc,
				       const sctp_subtype_t type,
				       void *arg,
				       sctp_cmd_seq_t *commands)
{
	struct sctp_datamsg *msg = arg;

	sctp_add_cmd_sf(commands, SCTP_CMD_SEND_MSG, SCTP_DATAMSG(msg));
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Process the SHUTDOWN primitive.
 *
 * Section: 10.1:
 * C) Shutdown
 *
 * Format: SHUTDOWN(association id)
 * -> result
 *
 * Gracefully closes an association. Any locally queued user data
 * will be delivered to the peer. The association will be terminated only
 * after the peer acknowledges all the SCTP packets sent.  A success code
 * will be returned on successful termination of the association. If
 * attempting to terminate the association results in a failure, an error
 * code shall be returned.
 *
 * Mandatory attributes:
 *
 *  o association id - local handle to the SCTP association
 *
 * Optional attributes:
 *
 * None.
 *
 * The return value is the disposition.
 */
sctp_disposition_t sctp_sf_do_9_2_prm_shutdown(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	int disposition;

	/* From 9.2 Shutdown of an Association
	 * Upon receipt of the SHUTDOWN primitive from its upper
	 * layer, the endpoint enters SHUTDOWN-PENDING state and
	 * remains there until all outstanding data has been
	 * acknowledged by its peer. The endpoint accepts no new data
	 * from its upper layer, but retransmits data to the far end
	 * if necessary to fill gaps.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_SHUTDOWN_PENDING));

	disposition = SCTP_DISPOSITION_CONSUME;
	if (sctp_outq_is_empty(&asoc->outqueue)) {
		disposition = sctp_sf_do_9_2_start_shutdown(ep, asoc, type,
							    arg, commands);
	}
	return disposition;
}

/*
 * Process the ABORT primitive.
 *
 * Section: 10.1:
 * C) Abort
 *
 * Format: Abort(association id [, cause code])
 * -> result
 *
 * Ungracefully closes an association. Any locally queued user data
 * will be discarded and an ABORT chunk is sent to the peer.  A success code
 * will be returned on successful abortion of the association. If
 * attempting to abort the association results in a failure, an error
 * code shall be returned.
 *
 * Mandatory attributes:
 *
 *  o association id - local handle to the SCTP association
 *
 * Optional attributes:
 *
 *  o cause code - reason of the abort to be passed to the peer
 *
 * None.
 *
 * The return value is the disposition.
 */
sctp_disposition_t sctp_sf_do_9_1_prm_abort(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	/* From 9.1 Abort of an Association
	 * Upon receipt of the ABORT primitive from its upper
	 * layer, the endpoint enters CLOSED state and
	 * discard all outstanding data has been
	 * acknowledged by its peer. The endpoint accepts no new data
	 * from its upper layer, but retransmits data to the far end
	 * if necessary to fill gaps.
	 */
	struct sctp_chunk *abort = arg;
	sctp_disposition_t retval;

	retval = SCTP_DISPOSITION_CONSUME;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(abort));

	/* Even if we can't send the ABORT due to low memory delete the
	 * TCB.  This is a departure from our typical NOMEM handling.
	 */

	sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
			SCTP_ERROR(ECONNABORTED));
	/* Delete the established association. */
	sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED,
			SCTP_PERR(SCTP_ERROR_USER_ABORT));

	SCTP_INC_STATS(SCTP_MIB_ABORTEDS);
	SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);

	return retval;
}

/* We tried an illegal operation on an association which is closed.  */
sctp_disposition_t sctp_sf_error_closed(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_ERROR, SCTP_ERROR(-EINVAL));
	return SCTP_DISPOSITION_CONSUME;
}

/* We tried an illegal operation on an association which is shutting
 * down.
 */
sctp_disposition_t sctp_sf_error_shutdown(const struct sctp_endpoint *ep,
					  const struct sctp_association *asoc,
					  const sctp_subtype_t type,
					  void *arg,
					  sctp_cmd_seq_t *commands)
{
	sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_ERROR,
			SCTP_ERROR(-ESHUTDOWN));
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * sctp_cookie_wait_prm_shutdown
 *
 * Section: 4 Note: 2
 * Verification Tag:
 * Inputs
 * (endpoint, asoc)
 *
 * The RFC does not explicitly address this issue, but is the route through the
 * state table when someone issues a shutdown while in COOKIE_WAIT state.
 *
 * Outputs
 * (timers)
 */
sctp_disposition_t sctp_sf_cookie_wait_prm_shutdown(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));

	SCTP_INC_STATS(SCTP_MIB_SHUTDOWNS);

	sctp_add_cmd_sf(commands, SCTP_CMD_DELETE_TCB, SCTP_NULL());

	return SCTP_DISPOSITION_DELETE_TCB;
}

/*
 * sctp_cookie_echoed_prm_shutdown
 *
 * Section: 4 Note: 2
 * Verification Tag:
 * Inputs
 * (endpoint, asoc)
 *
 * The RFC does not explcitly address this issue, but is the route through the
 * state table when someone issues a shutdown while in COOKIE_ECHOED state.
 *
 * Outputs
 * (timers)
 */
sctp_disposition_t sctp_sf_cookie_echoed_prm_shutdown(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg, sctp_cmd_seq_t *commands)
{
	/* There is a single T1 timer, so we should be able to use
	 * common function with the COOKIE-WAIT state.
	 */
	return sctp_sf_cookie_wait_prm_shutdown(ep, asoc, type, arg, commands);
}

/*
 * sctp_sf_cookie_wait_prm_abort
 *
 * Section: 4 Note: 2
 * Verification Tag:
 * Inputs
 * (endpoint, asoc)
 *
 * The RFC does not explicitly address this issue, but is the route through the
 * state table when someone issues an abort while in COOKIE_WAIT state.
 *
 * Outputs
 * (timers)
 */
sctp_disposition_t sctp_sf_cookie_wait_prm_abort(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *abort = arg;
	sctp_disposition_t retval;

	/* Stop T1-init timer */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));
	retval = SCTP_DISPOSITION_CONSUME;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(abort));

	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_CLOSED));

	SCTP_INC_STATS(SCTP_MIB_ABORTEDS);

	/* Even if we can't send the ABORT due to low memory delete the
	 * TCB.  This is a departure from our typical NOMEM handling.
	 */

	sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
			SCTP_ERROR(ECONNREFUSED));
	/* Delete the established association. */
	sctp_add_cmd_sf(commands, SCTP_CMD_INIT_FAILED,
			SCTP_PERR(SCTP_ERROR_USER_ABORT));

	return retval;
}

/*
 * sctp_sf_cookie_echoed_prm_abort
 *
 * Section: 4 Note: 3
 * Verification Tag:
 * Inputs
 * (endpoint, asoc)
 *
 * The RFC does not explcitly address this issue, but is the route through the
 * state table when someone issues an abort while in COOKIE_ECHOED state.
 *
 * Outputs
 * (timers)
 */
sctp_disposition_t sctp_sf_cookie_echoed_prm_abort(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	/* There is a single T1 timer, so we should be able to use
	 * common function with the COOKIE-WAIT state.
	 */
	return sctp_sf_cookie_wait_prm_abort(ep, asoc, type, arg, commands);
}

/*
 * sctp_sf_shutdown_pending_prm_abort
 *
 * Inputs
 * (endpoint, asoc)
 *
 * The RFC does not explicitly address this issue, but is the route through the
 * state table when someone issues an abort while in SHUTDOWN-PENDING state.
 *
 * Outputs
 * (timers)
 */
sctp_disposition_t sctp_sf_shutdown_pending_prm_abort(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	/* Stop the T5-shutdown guard timer.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T5_SHUTDOWN_GUARD));

	return sctp_sf_do_9_1_prm_abort(ep, asoc, type, arg, commands);
}

/*
 * sctp_sf_shutdown_sent_prm_abort
 *
 * Inputs
 * (endpoint, asoc)
 *
 * The RFC does not explicitly address this issue, but is the route through the
 * state table when someone issues an abort while in SHUTDOWN-SENT state.
 *
 * Outputs
 * (timers)
 */
sctp_disposition_t sctp_sf_shutdown_sent_prm_abort(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	/* Stop the T2-shutdown timer.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));

	/* Stop the T5-shutdown guard timer.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T5_SHUTDOWN_GUARD));

	return sctp_sf_do_9_1_prm_abort(ep, asoc, type, arg, commands);
}

/*
 * sctp_sf_cookie_echoed_prm_abort
 *
 * Inputs
 * (endpoint, asoc)
 *
 * The RFC does not explcitly address this issue, but is the route through the
 * state table when someone issues an abort while in COOKIE_ECHOED state.
 *
 * Outputs
 * (timers)
 */
sctp_disposition_t sctp_sf_shutdown_ack_sent_prm_abort(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	/* The same T2 timer, so we should be able to use
	 * common function with the SHUTDOWN-SENT state.
	 */
	return sctp_sf_shutdown_sent_prm_abort(ep, asoc, type, arg, commands);
}

/*
 * Process the REQUESTHEARTBEAT primitive
 *
 * 10.1 ULP-to-SCTP
 * J) Request Heartbeat
 *
 * Format: REQUESTHEARTBEAT(association id, destination transport address)
 *
 * -> result
 *
 * Instructs the local endpoint to perform a HeartBeat on the specified
 * destination transport address of the given association. The returned
 * result should indicate whether the transmission of the HEARTBEAT
 * chunk to the destination address is successful.
 *
 * Mandatory attributes:
 *
 * o association id - local handle to the SCTP association
 *
 * o destination transport address - the transport address of the
 *   association on which a heartbeat should be issued.
 */
sctp_disposition_t sctp_sf_do_prm_requestheartbeat(
					const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	if (SCTP_DISPOSITION_NOMEM == sctp_sf_heartbeat(ep, asoc, type,
				      (struct sctp_transport *)arg, commands))
		return SCTP_DISPOSITION_NOMEM;

	/*
	 * RFC 2960 (bis), section 8.3
	 *
	 *    D) Request an on-demand HEARTBEAT on a specific destination
	 *    transport address of a given association.
	 *
	 *    The endpoint should increment the respective error  counter of
	 *    the destination transport address each time a HEARTBEAT is sent
	 *    to that address and not acknowledged within one RTO.
	 *
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TRANSPORT_HB_SENT,
			SCTP_TRANSPORT(arg));
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * ADDIP Section 4.1 ASCONF Chunk Procedures
 * When an endpoint has an ASCONF signaled change to be sent to the
 * remote endpoint it should do A1 to A9
 */
sctp_disposition_t sctp_sf_do_prm_asconf(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = arg;

	sctp_add_cmd_sf(commands, SCTP_CMD_SETUP_T4, SCTP_CHUNK(chunk));
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T4_RTO));
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(chunk));
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Ignore the primitive event
 *
 * The return value is the disposition of the primitive.
 */
sctp_disposition_t sctp_sf_ignore_primitive(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	SCTP_DEBUG_PRINTK("Primitive type %d is ignored.\n", type.primitive);
	return SCTP_DISPOSITION_DISCARD;
}

/***************************************************************************
 * These are the state functions for the OTHER events.
 ***************************************************************************/

/*
 * Start the shutdown negotiation.
 *
 * From Section 9.2:
 * Once all its outstanding data has been acknowledged, the endpoint
 * shall send a SHUTDOWN chunk to its peer including in the Cumulative
 * TSN Ack field the last sequential TSN it has received from the peer.
 * It shall then start the T2-shutdown timer and enter the SHUTDOWN-SENT
 * state. If the timer expires, the endpoint must re-send the SHUTDOWN
 * with the updated last sequential TSN received from its peer.
 *
 * The return value is the disposition.
 */
sctp_disposition_t sctp_sf_do_9_2_start_shutdown(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *reply;

	/* Once all its outstanding data has been acknowledged, the
	 * endpoint shall send a SHUTDOWN chunk to its peer including
	 * in the Cumulative TSN Ack field the last sequential TSN it
	 * has received from the peer.
	 */
	reply = sctp_make_shutdown(asoc, NULL);
	if (!reply)
		goto nomem;

	/* Set the transport for the SHUTDOWN chunk and the timeout for the
	 * T2-shutdown timer.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_SETUP_T2, SCTP_CHUNK(reply));

	/* It shall then start the T2-shutdown timer */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));

	/* RFC 4960 Section 9.2
	 * The sender of the SHUTDOWN MAY also start an overall guard timer
	 * 'T5-shutdown-guard' to bound the overall time for shutdown sequence.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_START,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T5_SHUTDOWN_GUARD));

	if (asoc->autoclose)
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
				SCTP_TO(SCTP_EVENT_TIMEOUT_AUTOCLOSE));

	/* and enter the SHUTDOWN-SENT state.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_SHUTDOWN_SENT));

	/* sctp-implguide 2.10 Issues with Heartbeating and failover
	 *
	 * HEARTBEAT ... is discontinued after sending either SHUTDOWN
	 * or SHUTDOWN-ACK.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_HB_TIMERS_STOP, SCTP_NULL());

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));

	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Generate a SHUTDOWN ACK now that everything is SACK'd.
 *
 * From Section 9.2:
 *
 * If it has no more outstanding DATA chunks, the SHUTDOWN receiver
 * shall send a SHUTDOWN ACK and start a T2-shutdown timer of its own,
 * entering the SHUTDOWN-ACK-SENT state. If the timer expires, the
 * endpoint must re-send the SHUTDOWN ACK.
 *
 * The return value is the disposition.
 */
sctp_disposition_t sctp_sf_do_9_2_shutdown_ack(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = (struct sctp_chunk *) arg;
	struct sctp_chunk *reply;

	/* There are 2 ways of getting here:
	 *    1) called in response to a SHUTDOWN chunk
	 *    2) called when SCTP_EVENT_NO_PENDING_TSN event is issued.
	 *
	 * For the case (2), the arg parameter is set to NULL.  We need
	 * to check that we have a chunk before accessing it's fields.
	 */
	if (chunk) {
		if (!sctp_vtag_verify(chunk, asoc))
			return sctp_sf_pdiscard(ep, asoc, type, arg, commands);

		/* Make sure that the SHUTDOWN chunk has a valid length. */
		if (!sctp_chunk_length_valid(chunk, sizeof(struct sctp_shutdown_chunk_t)))
			return sctp_sf_violation_chunklen(ep, asoc, type, arg,
							  commands);
	}

	/* If it has no more outstanding DATA chunks, the SHUTDOWN receiver
	 * shall send a SHUTDOWN ACK ...
	 */
	reply = sctp_make_shutdown_ack(asoc, chunk);
	if (!reply)
		goto nomem;

	/* Set the transport for the SHUTDOWN ACK chunk and the timeout for
	 * the T2-shutdown timer.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_SETUP_T2, SCTP_CHUNK(reply));

	/* and start/restart a T2-shutdown timer of its own, */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));

	if (asoc->autoclose)
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
				SCTP_TO(SCTP_EVENT_TIMEOUT_AUTOCLOSE));

	/* Enter the SHUTDOWN-ACK-SENT state.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_SHUTDOWN_ACK_SENT));

	/* sctp-implguide 2.10 Issues with Heartbeating and failover
	 *
	 * HEARTBEAT ... is discontinued after sending either SHUTDOWN
	 * or SHUTDOWN-ACK.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_HB_TIMERS_STOP, SCTP_NULL());

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));

	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * Ignore the event defined as other
 *
 * The return value is the disposition of the event.
 */
sctp_disposition_t sctp_sf_ignore_other(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	SCTP_DEBUG_PRINTK("The event other type %d is ignored\n", type.other);
	return SCTP_DISPOSITION_DISCARD;
}

/************************************************************
 * These are the state functions for handling timeout events.
 ************************************************************/

/*
 * RTX Timeout
 *
 * Section: 6.3.3 Handle T3-rtx Expiration
 *
 * Whenever the retransmission timer T3-rtx expires for a destination
 * address, do the following:
 * [See below]
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_do_6_3_3_rtx(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	struct sctp_transport *transport = arg;

	SCTP_INC_STATS(SCTP_MIB_T3_RTX_EXPIREDS);

	if (asoc->overall_error_count >= asoc->max_retrans) {
		sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
				SCTP_ERROR(ETIMEDOUT));
		/* CMD_ASSOC_FAILED calls CMD_DELETE_TCB. */
		sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED,
				SCTP_PERR(SCTP_ERROR_NO_ERROR));
		SCTP_INC_STATS(SCTP_MIB_ABORTEDS);
		SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);
		return SCTP_DISPOSITION_DELETE_TCB;
	}

	/* E1) For the destination address for which the timer
	 * expires, adjust its ssthresh with rules defined in Section
	 * 7.2.3 and set the cwnd <- MTU.
	 */

	/* E2) For the destination address for which the timer
	 * expires, set RTO <- RTO * 2 ("back off the timer").  The
	 * maximum value discussed in rule C7 above (RTO.max) may be
	 * used to provide an upper bound to this doubling operation.
	 */

	/* E3) Determine how many of the earliest (i.e., lowest TSN)
	 * outstanding DATA chunks for the address for which the
	 * T3-rtx has expired will fit into a single packet, subject
	 * to the MTU constraint for the path corresponding to the
	 * destination transport address to which the retransmission
	 * is being sent (this may be different from the address for
	 * which the timer expires [see Section 6.4]).  Call this
	 * value K. Bundle and retransmit those K DATA chunks in a
	 * single packet to the destination endpoint.
	 *
	 * Note: Any DATA chunks that were sent to the address for
	 * which the T3-rtx timer expired but did not fit in one MTU
	 * (rule E3 above), should be marked for retransmission and
	 * sent as soon as cwnd allows (normally when a SACK arrives).
	 */

	/* Do some failure management (Section 8.2). */
	sctp_add_cmd_sf(commands, SCTP_CMD_STRIKE, SCTP_TRANSPORT(transport));

	/* NB: Rules E4 and F1 are implicit in R1.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_RETRAN, SCTP_TRANSPORT(transport));

	return SCTP_DISPOSITION_CONSUME;
}

/*
 * Generate delayed SACK on timeout
 *
 * Section: 6.2  Acknowledgement on Reception of DATA Chunks
 *
 * The guidelines on delayed acknowledgement algorithm specified in
 * Section 4.2 of [RFC2581] SHOULD be followed.  Specifically, an
 * acknowledgement SHOULD be generated for at least every second packet
 * (not every second DATA chunk) received, and SHOULD be generated
 * within 200 ms of the arrival of any unacknowledged DATA chunk.  In
 * some situations it may be beneficial for an SCTP transmitter to be
 * more conservative than the algorithms detailed in this document
 * allow. However, an SCTP transmitter MUST NOT be more aggressive than
 * the following algorithms allow.
 */
sctp_disposition_t sctp_sf_do_6_2_sack(const struct sctp_endpoint *ep,
				       const struct sctp_association *asoc,
				       const sctp_subtype_t type,
				       void *arg,
				       sctp_cmd_seq_t *commands)
{
	SCTP_INC_STATS(SCTP_MIB_DELAY_SACK_EXPIREDS);
	sctp_add_cmd_sf(commands, SCTP_CMD_GEN_SACK, SCTP_FORCE());
	return SCTP_DISPOSITION_CONSUME;
}

/*
 * sctp_sf_t1_init_timer_expire
 *
 * Section: 4 Note: 2
 * Verification Tag:
 * Inputs
 * (endpoint, asoc)
 *
 *  RFC 2960 Section 4 Notes
 *  2) If the T1-init timer expires, the endpoint MUST retransmit INIT
 *     and re-start the T1-init timer without changing state.  This MUST
 *     be repeated up to 'Max.Init.Retransmits' times.  After that, the
 *     endpoint MUST abort the initialization process and report the
 *     error to SCTP user.
 *
 * Outputs
 * (timers, events)
 *
 */
sctp_disposition_t sctp_sf_t1_init_timer_expire(const struct sctp_endpoint *ep,
					   const struct sctp_association *asoc,
					   const sctp_subtype_t type,
					   void *arg,
					   sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *repl = NULL;
	struct sctp_bind_addr *bp;
	int attempts = asoc->init_err_counter + 1;

	SCTP_DEBUG_PRINTK("Timer T1 expired (INIT).\n");
	SCTP_INC_STATS(SCTP_MIB_T1_INIT_EXPIREDS);

	if (attempts <= asoc->max_init_attempts) {
		bp = (struct sctp_bind_addr *) &asoc->base.bind_addr;
		repl = sctp_make_init(asoc, bp, GFP_ATOMIC, 0);
		if (!repl)
			return SCTP_DISPOSITION_NOMEM;

		/* Choose transport for INIT. */
		sctp_add_cmd_sf(commands, SCTP_CMD_INIT_CHOOSE_TRANSPORT,
				SCTP_CHUNK(repl));

		/* Issue a sideeffect to do the needed accounting. */
		sctp_add_cmd_sf(commands, SCTP_CMD_INIT_RESTART,
				SCTP_TO(SCTP_EVENT_TIMEOUT_T1_INIT));

		sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));
	} else {
		SCTP_DEBUG_PRINTK("Giving up on INIT, attempts: %d"
				  " max_init_attempts: %d\n",
				  attempts, asoc->max_init_attempts);
		sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
				SCTP_ERROR(ETIMEDOUT));
		sctp_add_cmd_sf(commands, SCTP_CMD_INIT_FAILED,
				SCTP_PERR(SCTP_ERROR_NO_ERROR));
		return SCTP_DISPOSITION_DELETE_TCB;
	}

	return SCTP_DISPOSITION_CONSUME;
}

/*
 * sctp_sf_t1_cookie_timer_expire
 *
 * Section: 4 Note: 2
 * Verification Tag:
 * Inputs
 * (endpoint, asoc)
 *
 *  RFC 2960 Section 4 Notes
 *  3) If the T1-cookie timer expires, the endpoint MUST retransmit
 *     COOKIE ECHO and re-start the T1-cookie timer without changing
 *     state.  This MUST be repeated up to 'Max.Init.Retransmits' times.
 *     After that, the endpoint MUST abort the initialization process and
 *     report the error to SCTP user.
 *
 * Outputs
 * (timers, events)
 *
 */
sctp_disposition_t sctp_sf_t1_cookie_timer_expire(const struct sctp_endpoint *ep,
					   const struct sctp_association *asoc,
					   const sctp_subtype_t type,
					   void *arg,
					   sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *repl = NULL;
	int attempts = asoc->init_err_counter + 1;

	SCTP_DEBUG_PRINTK("Timer T1 expired (COOKIE-ECHO).\n");
	SCTP_INC_STATS(SCTP_MIB_T1_COOKIE_EXPIREDS);

	if (attempts <= asoc->max_init_attempts) {
		repl = sctp_make_cookie_echo(asoc, NULL);
		if (!repl)
			return SCTP_DISPOSITION_NOMEM;

		sctp_add_cmd_sf(commands, SCTP_CMD_INIT_CHOOSE_TRANSPORT,
				SCTP_CHUNK(repl));
		/* Issue a sideeffect to do the needed accounting. */
		sctp_add_cmd_sf(commands, SCTP_CMD_COOKIEECHO_RESTART,
				SCTP_TO(SCTP_EVENT_TIMEOUT_T1_COOKIE));

		sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(repl));
	} else {
		sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
				SCTP_ERROR(ETIMEDOUT));
		sctp_add_cmd_sf(commands, SCTP_CMD_INIT_FAILED,
				SCTP_PERR(SCTP_ERROR_NO_ERROR));
		return SCTP_DISPOSITION_DELETE_TCB;
	}

	return SCTP_DISPOSITION_CONSUME;
}

/* RFC2960 9.2 If the timer expires, the endpoint must re-send the SHUTDOWN
 * with the updated last sequential TSN received from its peer.
 *
 * An endpoint should limit the number of retransmissions of the
 * SHUTDOWN chunk to the protocol parameter 'Association.Max.Retrans'.
 * If this threshold is exceeded the endpoint should destroy the TCB and
 * MUST report the peer endpoint unreachable to the upper layer (and
 * thus the association enters the CLOSED state).  The reception of any
 * packet from its peer (i.e. as the peer sends all of its queued DATA
 * chunks) should clear the endpoint's retransmission count and restart
 * the T2-Shutdown timer,  giving its peer ample opportunity to transmit
 * all of its queued DATA chunks that have not yet been sent.
 */
sctp_disposition_t sctp_sf_t2_timer_expire(const struct sctp_endpoint *ep,
					   const struct sctp_association *asoc,
					   const sctp_subtype_t type,
					   void *arg,
					   sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *reply = NULL;

	SCTP_DEBUG_PRINTK("Timer T2 expired.\n");
	SCTP_INC_STATS(SCTP_MIB_T2_SHUTDOWN_EXPIREDS);

	((struct sctp_association *)asoc)->shutdown_retries++;

	if (asoc->overall_error_count >= asoc->max_retrans) {
		sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
				SCTP_ERROR(ETIMEDOUT));
		/* Note:  CMD_ASSOC_FAILED calls CMD_DELETE_TCB. */
		sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED,
				SCTP_PERR(SCTP_ERROR_NO_ERROR));
		SCTP_INC_STATS(SCTP_MIB_ABORTEDS);
		SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);
		return SCTP_DISPOSITION_DELETE_TCB;
	}

	switch (asoc->state) {
	case SCTP_STATE_SHUTDOWN_SENT:
		reply = sctp_make_shutdown(asoc, NULL);
		break;

	case SCTP_STATE_SHUTDOWN_ACK_SENT:
		reply = sctp_make_shutdown_ack(asoc, NULL);
		break;

	default:
		BUG();
		break;
	}

	if (!reply)
		goto nomem;

	/* Do some failure management (Section 8.2).
	 * If we remove the transport an SHUTDOWN was last sent to, don't
	 * do failure management.
	 */
	if (asoc->shutdown_last_sent_to)
		sctp_add_cmd_sf(commands, SCTP_CMD_STRIKE,
				SCTP_TRANSPORT(asoc->shutdown_last_sent_to));

	/* Set the transport for the SHUTDOWN/ACK chunk and the timeout for
	 * the T2-shutdown timer.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_SETUP_T2, SCTP_CHUNK(reply));

	/* Restart the T2-shutdown timer.  */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T2_SHUTDOWN));
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));
	return SCTP_DISPOSITION_CONSUME;

nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/*
 * ADDIP Section 4.1 ASCONF CHunk Procedures
 * If the T4 RTO timer expires the endpoint should do B1 to B5
 */
sctp_disposition_t sctp_sf_t4_timer_expire(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *chunk = asoc->addip_last_asconf;
	struct sctp_transport *transport = chunk->transport;

	SCTP_INC_STATS(SCTP_MIB_T4_RTO_EXPIREDS);

	/* ADDIP 4.1 B1) Increment the error counters and perform path failure
	 * detection on the appropriate destination address as defined in
	 * RFC2960 [5] section 8.1 and 8.2.
	 */
	if (transport)
		sctp_add_cmd_sf(commands, SCTP_CMD_STRIKE,
				SCTP_TRANSPORT(transport));

	/* Reconfig T4 timer and transport. */
	sctp_add_cmd_sf(commands, SCTP_CMD_SETUP_T4, SCTP_CHUNK(chunk));

	/* ADDIP 4.1 B2) Increment the association error counters and perform
	 * endpoint failure detection on the association as defined in
	 * RFC2960 [5] section 8.1 and 8.2.
	 * association error counter is incremented in SCTP_CMD_STRIKE.
	 */
	if (asoc->overall_error_count >= asoc->max_retrans) {
		sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_STOP,
				SCTP_TO(SCTP_EVENT_TIMEOUT_T4_RTO));
		sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
				SCTP_ERROR(ETIMEDOUT));
		sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED,
				SCTP_PERR(SCTP_ERROR_NO_ERROR));
		SCTP_INC_STATS(SCTP_MIB_ABORTEDS);
		SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);
		return SCTP_DISPOSITION_ABORT;
	}

	/* ADDIP 4.1 B3) Back-off the destination address RTO value to which
	 * the ASCONF chunk was sent by doubling the RTO timer value.
	 * This is done in SCTP_CMD_STRIKE.
	 */

	/* ADDIP 4.1 B4) Re-transmit the ASCONF Chunk last sent and if possible
	 * choose an alternate destination address (please refer to RFC2960
	 * [5] section 6.4.1). An endpoint MUST NOT add new parameters to this
	 * chunk, it MUST be the same (including its serial number) as the last
	 * ASCONF sent.
	 */
	sctp_chunk_hold(asoc->addip_last_asconf);
	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
			SCTP_CHUNK(asoc->addip_last_asconf));

	/* ADDIP 4.1 B5) Restart the T-4 RTO timer. Note that if a different
	 * destination is selected, then the RTO used will be that of the new
	 * destination address.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_TIMER_RESTART,
			SCTP_TO(SCTP_EVENT_TIMEOUT_T4_RTO));

	return SCTP_DISPOSITION_CONSUME;
}

/* sctpimpguide-05 Section 2.12.2
 * The sender of the SHUTDOWN MAY also start an overall guard timer
 * 'T5-shutdown-guard' to bound the overall time for shutdown sequence.
 * At the expiration of this timer the sender SHOULD abort the association
 * by sending an ABORT chunk.
 */
sctp_disposition_t sctp_sf_t5_timer_expire(const struct sctp_endpoint *ep,
					   const struct sctp_association *asoc,
					   const sctp_subtype_t type,
					   void *arg,
					   sctp_cmd_seq_t *commands)
{
	struct sctp_chunk *reply = NULL;

	SCTP_DEBUG_PRINTK("Timer T5 expired.\n");
	SCTP_INC_STATS(SCTP_MIB_T5_SHUTDOWN_GUARD_EXPIREDS);

	reply = sctp_make_abort(asoc, NULL, 0);
	if (!reply)
		goto nomem;

	sctp_add_cmd_sf(commands, SCTP_CMD_REPLY, SCTP_CHUNK(reply));
	sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
			SCTP_ERROR(ETIMEDOUT));
	sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED,
			SCTP_PERR(SCTP_ERROR_NO_ERROR));

	SCTP_INC_STATS(SCTP_MIB_ABORTEDS);
	SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);

	return SCTP_DISPOSITION_DELETE_TCB;
nomem:
	return SCTP_DISPOSITION_NOMEM;
}

/* Handle expiration of AUTOCLOSE timer.  When the autoclose timer expires,
 * the association is automatically closed by starting the shutdown process.
 * The work that needs to be done is same as when SHUTDOWN is initiated by
 * the user.  So this routine looks same as sctp_sf_do_9_2_prm_shutdown().
 */
sctp_disposition_t sctp_sf_autoclose_timer_expire(
	const struct sctp_endpoint *ep,
	const struct sctp_association *asoc,
	const sctp_subtype_t type,
	void *arg,
	sctp_cmd_seq_t *commands)
{
	int disposition;

	SCTP_INC_STATS(SCTP_MIB_AUTOCLOSE_EXPIREDS);

	/* From 9.2 Shutdown of an Association
	 * Upon receipt of the SHUTDOWN primitive from its upper
	 * layer, the endpoint enters SHUTDOWN-PENDING state and
	 * remains there until all outstanding data has been
	 * acknowledged by its peer. The endpoint accepts no new data
	 * from its upper layer, but retransmits data to the far end
	 * if necessary to fill gaps.
	 */
	sctp_add_cmd_sf(commands, SCTP_CMD_NEW_STATE,
			SCTP_STATE(SCTP_STATE_SHUTDOWN_PENDING));

	disposition = SCTP_DISPOSITION_CONSUME;
	if (sctp_outq_is_empty(&asoc->outqueue)) {
		disposition = sctp_sf_do_9_2_start_shutdown(ep, asoc, type,
							    arg, commands);
	}
	return disposition;
}

/*****************************************************************************
 * These are sa state functions which could apply to all types of events.
 ****************************************************************************/

/*
 * This table entry is not implemented.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_not_impl(const struct sctp_endpoint *ep,
				    const struct sctp_association *asoc,
				    const sctp_subtype_t type,
				    void *arg,
				    sctp_cmd_seq_t *commands)
{
	return SCTP_DISPOSITION_NOT_IMPL;
}

/*
 * This table entry represents a bug.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_bug(const struct sctp_endpoint *ep,
			       const struct sctp_association *asoc,
			       const sctp_subtype_t type,
			       void *arg,
			       sctp_cmd_seq_t *commands)
{
	return SCTP_DISPOSITION_BUG;
}

/*
 * This table entry represents the firing of a timer in the wrong state.
 * Since timer deletion cannot be guaranteed a timer 'may' end up firing
 * when the association is in the wrong state.   This event should
 * be ignored, so as to prevent any rearming of the timer.
 *
 * Inputs
 * (endpoint, asoc, chunk)
 *
 * The return value is the disposition of the chunk.
 */
sctp_disposition_t sctp_sf_timer_ignore(const struct sctp_endpoint *ep,
					const struct sctp_association *asoc,
					const sctp_subtype_t type,
					void *arg,
					sctp_cmd_seq_t *commands)
{
	SCTP_DEBUG_PRINTK("Timer %d ignored.\n", type.chunk);
	return SCTP_DISPOSITION_CONSUME;
}

/********************************************************************
 * 2nd Level Abstractions
 ********************************************************************/

/* Pull the SACK chunk based on the SACK header. */
static struct sctp_sackhdr *sctp_sm_pull_sack(struct sctp_chunk *chunk)
{
	struct sctp_sackhdr *sack;
	unsigned int len;
	__u16 num_blocks;
	__u16 num_dup_tsns;

	/* Protect ourselves from reading too far into
	 * the skb from a bogus sender.
	 */
	sack = (struct sctp_sackhdr *) chunk->skb->data;

	num_blocks = ntohs(sack->num_gap_ack_blocks);
	num_dup_tsns = ntohs(sack->num_dup_tsns);
	len = sizeof(struct sctp_sackhdr);
	len += (num_blocks + num_dup_tsns) * sizeof(__u32);
	if (len > chunk->skb->len)
		return NULL;

	skb_pull(chunk->skb, len);

	return sack;
}

/* Create an ABORT packet to be sent as a response, with the specified
 * error causes.
 */
static struct sctp_packet *sctp_abort_pkt_new(const struct sctp_endpoint *ep,
				  const struct sctp_association *asoc,
				  struct sctp_chunk *chunk,
				  const void *payload,
				  size_t paylen)
{
	struct sctp_packet *packet;
	struct sctp_chunk *abort;

	packet = sctp_ootb_pkt_new(asoc, chunk);

	if (packet) {
		/* Make an ABORT.
		 * The T bit will be set if the asoc is NULL.
		 */
		abort = sctp_make_abort(asoc, chunk, paylen);
		if (!abort) {
			sctp_ootb_pkt_free(packet);
			return NULL;
		}

		/* Reflect vtag if T-Bit is set */
		if (sctp_test_T_bit(abort))
			packet->vtag = ntohl(chunk->sctp_hdr->vtag);

		/* Add specified error causes, i.e., payload, to the
		 * end of the chunk.
		 */
		sctp_addto_chunk(abort, paylen, payload);

		/* Set the skb to the belonging sock for accounting.  */
		abort->skb->sk = ep->base.sk;

		sctp_packet_append_chunk(packet, abort);

	}

	return packet;
}

/* Allocate a packet for responding in the OOTB conditions.  */
static struct sctp_packet *sctp_ootb_pkt_new(const struct sctp_association *asoc,
					     const struct sctp_chunk *chunk)
{
	struct sctp_packet *packet;
	struct sctp_transport *transport;
	__u16 sport;
	__u16 dport;
	__u32 vtag;

	/* Get the source and destination port from the inbound packet.  */
	sport = ntohs(chunk->sctp_hdr->dest);
	dport = ntohs(chunk->sctp_hdr->source);

	/* The V-tag is going to be the same as the inbound packet if no
	 * association exists, otherwise, use the peer's vtag.
	 */
	if (asoc) {
		/* Special case the INIT-ACK as there is no peer's vtag
		 * yet.
		 */
		switch(chunk->chunk_hdr->type) {
		case SCTP_CID_INIT_ACK:
		{
			sctp_initack_chunk_t *initack;

			initack = (sctp_initack_chunk_t *)chunk->chunk_hdr;
			vtag = ntohl(initack->init_hdr.init_tag);
			break;
		}
		default:
			vtag = asoc->peer.i.init_tag;
			break;
		}
	} else {
		/* Special case the INIT and stale COOKIE_ECHO as there is no
		 * vtag yet.
		 */
		switch(chunk->chunk_hdr->type) {
		case SCTP_CID_INIT:
		{
			sctp_init_chunk_t *init;

			init = (sctp_init_chunk_t *)chunk->chunk_hdr;
			vtag = ntohl(init->init_hdr.init_tag);
			break;
		}
		default:
			vtag = ntohl(chunk->sctp_hdr->vtag);
			break;
		}
	}

	/* Make a transport for the bucket, Eliza... */
	transport = sctp_transport_new(sctp_source(chunk), GFP_ATOMIC);
	if (!transport)
		goto nomem;

	/* Cache a route for the transport with the chunk's destination as
	 * the source address.
	 */
	sctp_transport_route(transport, (union sctp_addr *)&chunk->dest,
			     sctp_sk(sctp_get_ctl_sock()));

	packet = sctp_packet_init(&transport->packet, transport, sport, dport);
	packet = sctp_packet_config(packet, vtag, 0);

	return packet;

nomem:
	return NULL;
}

/* Free the packet allocated earlier for responding in the OOTB condition.  */
void sctp_ootb_pkt_free(struct sctp_packet *packet)
{
	sctp_transport_free(packet->transport);
}

/* Send a stale cookie error when a invalid COOKIE ECHO chunk is found  */
static void sctp_send_stale_cookie_err(const struct sctp_endpoint *ep,
				       const struct sctp_association *asoc,
				       const struct sctp_chunk *chunk,
				       sctp_cmd_seq_t *commands,
				       struct sctp_chunk *err_chunk)
{
	struct sctp_packet *packet;

	if (err_chunk) {
		packet = sctp_ootb_pkt_new(asoc, chunk);
		if (packet) {
			struct sctp_signed_cookie *cookie;

			/* Override the OOTB vtag from the cookie. */
			cookie = chunk->subh.cookie_hdr;
			packet->vtag = cookie->c.peer_vtag;

			/* Set the skb to the belonging sock for accounting. */
			err_chunk->skb->sk = ep->base.sk;
			sctp_packet_append_chunk(packet, err_chunk);
			sctp_add_cmd_sf(commands, SCTP_CMD_SEND_PKT,
					SCTP_PACKET(packet));
			SCTP_INC_STATS(SCTP_MIB_OUTCTRLCHUNKS);
		} else
			sctp_chunk_free (err_chunk);
	}
}


/* Process a data chunk */
static int sctp_eat_data(const struct sctp_association *asoc,
			 struct sctp_chunk *chunk,
			 sctp_cmd_seq_t *commands)
{
	sctp_datahdr_t *data_hdr;
	struct sctp_chunk *err;
	size_t datalen;
	sctp_verb_t deliver;
	int tmp;
	__u32 tsn;
	struct sctp_tsnmap *map = (struct sctp_tsnmap *)&asoc->peer.tsn_map;
	struct sock *sk = asoc->base.sk;
	u16 ssn;
	u16 sid;
	u8 ordered = 0;

	data_hdr = chunk->subh.data_hdr = (sctp_datahdr_t *)chunk->skb->data;
	skb_pull(chunk->skb, sizeof(sctp_datahdr_t));

	tsn = ntohl(data_hdr->tsn);
	SCTP_DEBUG_PRINTK("eat_data: TSN 0x%x.\n", tsn);

	/* ASSERT:  Now skb->data is really the user data.  */

	/* Process ECN based congestion.
	 *
	 * Since the chunk structure is reused for all chunks within
	 * a packet, we use ecn_ce_done to track if we've already
	 * done CE processing for this packet.
	 *
	 * We need to do ECN processing even if we plan to discard the
	 * chunk later.
	 */

	if (!chunk->ecn_ce_done) {
		struct sctp_af *af;
		chunk->ecn_ce_done = 1;

		af = sctp_get_af_specific(
			ipver2af(ip_hdr(chunk->skb)->version));

		if (af && af->is_ce(chunk->skb) && asoc->peer.ecn_capable) {
			/* Do real work as sideffect. */
			sctp_add_cmd_sf(commands, SCTP_CMD_ECN_CE,
					SCTP_U32(tsn));
		}
	}

	tmp = sctp_tsnmap_check(&asoc->peer.tsn_map, tsn);
	if (tmp < 0) {
		/* The TSN is too high--silently discard the chunk and
		 * count on it getting retransmitted later.
		 */
		return SCTP_IERROR_HIGH_TSN;
	} else if (tmp > 0) {
		/* This is a duplicate.  Record it.  */
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_DUP, SCTP_U32(tsn));
		return SCTP_IERROR_DUP_TSN;
	}

	/* This is a new TSN.  */

	/* Discard if there is no room in the receive window.
	 * Actually, allow a little bit of overflow (up to a MTU).
	 */
	datalen = ntohs(chunk->chunk_hdr->length);
	datalen -= sizeof(sctp_data_chunk_t);

	deliver = SCTP_CMD_CHUNK_ULP;

	/* Think about partial delivery. */
	if ((datalen >= asoc->rwnd) && (!asoc->ulpq.pd_mode)) {

		/* Even if we don't accept this chunk there is
		 * memory pressure.
		 */
		sctp_add_cmd_sf(commands, SCTP_CMD_PART_DELIVER, SCTP_NULL());
	}

	/* Spill over rwnd a little bit.  Note: While allowed, this spill over
	 * seems a bit troublesome in that frag_point varies based on
	 * PMTU.  In cases, such as loopback, this might be a rather
	 * large spill over.
	 */
	if ((!chunk->data_accepted) && (!asoc->rwnd || asoc->rwnd_over ||
	    (datalen > asoc->rwnd + asoc->frag_point))) {

		/* If this is the next TSN, consider reneging to make
		 * room.   Note: Playing nice with a confused sender.  A
		 * malicious sender can still eat up all our buffer
		 * space and in the future we may want to detect and
		 * do more drastic reneging.
		 */
		if (sctp_tsnmap_has_gap(map) &&
		    (sctp_tsnmap_get_ctsn(map) + 1) == tsn) {
			SCTP_DEBUG_PRINTK("Reneging for tsn:%u\n", tsn);
			deliver = SCTP_CMD_RENEGE;
		} else {
			SCTP_DEBUG_PRINTK("Discard tsn: %u len: %Zd, "
					  "rwnd: %d\n", tsn, datalen,
					  asoc->rwnd);
			return SCTP_IERROR_IGNORE_TSN;
		}
	}

	/*
	 * Also try to renege to limit our memory usage in the event that
	 * we are under memory pressure
	 * If we can't renege, don't worry about it, the sk_rmem_schedule
	 * in sctp_ulpevent_make_rcvmsg will drop the frame if we grow our
	 * memory usage too much
	 */
	if (*sk->sk_prot_creator->memory_pressure) {
		if (sctp_tsnmap_has_gap(map) &&
	           (sctp_tsnmap_get_ctsn(map) + 1) == tsn) {
			SCTP_DEBUG_PRINTK("Under Pressure! Reneging for tsn:%u\n", tsn);
			deliver = SCTP_CMD_RENEGE;
		 }
	}

	/*
	 * Section 3.3.10.9 No User Data (9)
	 *
	 * Cause of error
	 * ---------------
	 * No User Data:  This error cause is returned to the originator of a
	 * DATA chunk if a received DATA chunk has no user data.
	 */
	if (unlikely(0 == datalen)) {
		err = sctp_make_abort_no_data(asoc, chunk, tsn);
		if (err) {
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(err));
		}
		/* We are going to ABORT, so we might as well stop
		 * processing the rest of the chunks in the packet.
		 */
		sctp_add_cmd_sf(commands, SCTP_CMD_DISCARD_PACKET,SCTP_NULL());
		sctp_add_cmd_sf(commands, SCTP_CMD_SET_SK_ERR,
				SCTP_ERROR(ECONNABORTED));
		sctp_add_cmd_sf(commands, SCTP_CMD_ASSOC_FAILED,
				SCTP_PERR(SCTP_ERROR_NO_DATA));
		SCTP_INC_STATS(SCTP_MIB_ABORTEDS);
		SCTP_DEC_STATS(SCTP_MIB_CURRESTAB);
		return SCTP_IERROR_NO_DATA;
	}

	chunk->data_accepted = 1;

	/* Note: Some chunks may get overcounted (if we drop) or overcounted
	 * if we renege and the chunk arrives again.
	 */
	if (chunk->chunk_hdr->flags & SCTP_DATA_UNORDERED)
		SCTP_INC_STATS(SCTP_MIB_INUNORDERCHUNKS);
	else {
		SCTP_INC_STATS(SCTP_MIB_INORDERCHUNKS);
		ordered = 1;
	}

	/* RFC 2960 6.5 Stream Identifier and Stream Sequence Number
	 *
	 * If an endpoint receive a DATA chunk with an invalid stream
	 * identifier, it shall acknowledge the reception of the DATA chunk
	 * following the normal procedure, immediately send an ERROR chunk
	 * with cause set to "Invalid Stream Identifier" (See Section 3.3.10)
	 * and discard the DATA chunk.
	 */
	sid = ntohs(data_hdr->stream);
	if (sid >= asoc->c.sinit_max_instreams) {
		/* Mark tsn as received even though we drop it */
		sctp_add_cmd_sf(commands, SCTP_CMD_REPORT_TSN, SCTP_U32(tsn));

		err = sctp_make_op_error(asoc, chunk, SCTP_ERROR_INV_STRM,
					 &data_hdr->stream,
					 sizeof(data_hdr->stream),
					 sizeof(u16));
		if (err)
			sctp_add_cmd_sf(commands, SCTP_CMD_REPLY,
					SCTP_CHUNK(err));
		return SCTP_IERROR_BAD_STREAM;
	}

	/* Check to see if the SSN is possible for this TSN.
	 * The biggest gap we can record is 4K wide.  Since SSNs wrap
	 * at an unsigned short, there is no way that an SSN can
	 * wrap and for a valid TSN.  We can simply check if the current
	 * SSN is smaller then the next expected one.  If it is, it wrapped
	 * and is invalid.
	 */
	ssn = ntohs(data_hdr->ssn);
	if (ordered && SSN_lt(ssn, sctp_ssn_peek(&asoc->ssnmap->in, sid))) {
		return SCTP_IERROR_PROTO_VIOLATION;
	}

	/* Send the data up to the user.  Note:  Schedule  the
	 * SCTP_CMD_CHUNK_ULP cmd before the SCTP_CMD_GEN_SACK, as the SACK
	 * chunk needs the updated rwnd.
	 */
	sctp_add_cmd_sf(commands, deliver, SCTP_CHUNK(chunk));

	return SCTP_IERROR_NO_ERROR;
}
