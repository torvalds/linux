/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001-2007, by Cisco Systems, Inc. All rights reserved.
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

#include <netinet/sctp_os.h>
#include <netinet/sctp_var.h>
#include <netinet/sctp_sysctl.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctp_header.h>
#include <netinet/sctputil.h>
#include <netinet/sctp_output.h>
#include <netinet/sctp_asconf.h>
#include <netinet/sctp_timer.h>

/*
 * debug flags:
 * SCTP_DEBUG_ASCONF1: protocol info, general info and errors
 * SCTP_DEBUG_ASCONF2: detailed info
 */


/*
 * RFC 5061
 *
 * An ASCONF parameter queue exists per asoc which holds the pending address
 * operations.  Lists are updated upon receipt of ASCONF-ACK.
 *
 * A restricted_addrs list exists per assoc to hold local addresses that are
 * not (yet) usable by the assoc as a source address.  These addresses are
 * either pending an ASCONF operation (and exist on the ASCONF parameter
 * queue), or they are permanently restricted (the peer has returned an
 * ERROR indication to an ASCONF(ADD), or the peer does not support ASCONF).
 *
 * Deleted addresses are always immediately removed from the lists as they will
 * (shortly) no longer exist in the kernel.  We send ASCONFs as a courtesy,
 * only if allowed.
 */

/*
 * ASCONF parameter processing.
 * response_required: set if a reply is required (eg. SUCCESS_REPORT).
 * returns a mbuf to an "error" response parameter or NULL/"success" if ok.
 * FIX: allocating this many mbufs on the fly is pretty inefficient...
 */
static struct mbuf *
sctp_asconf_success_response(uint32_t id)
{
	struct mbuf *m_reply = NULL;
	struct sctp_asconf_paramhdr *aph;

	m_reply = sctp_get_mbuf_for_msg(sizeof(struct sctp_asconf_paramhdr),
	    0, M_NOWAIT, 1, MT_DATA);
	if (m_reply == NULL) {
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "asconf_success_response: couldn't get mbuf!\n");
		return (NULL);
	}
	aph = mtod(m_reply, struct sctp_asconf_paramhdr *);
	aph->correlation_id = id;
	aph->ph.param_type = htons(SCTP_SUCCESS_REPORT);
	aph->ph.param_length = sizeof(struct sctp_asconf_paramhdr);
	SCTP_BUF_LEN(m_reply) = aph->ph.param_length;
	aph->ph.param_length = htons(aph->ph.param_length);

	return (m_reply);
}

static struct mbuf *
sctp_asconf_error_response(uint32_t id, uint16_t cause, uint8_t *error_tlv,
    uint16_t tlv_length)
{
	struct mbuf *m_reply = NULL;
	struct sctp_asconf_paramhdr *aph;
	struct sctp_error_cause *error;
	uint8_t *tlv;

	m_reply = sctp_get_mbuf_for_msg((sizeof(struct sctp_asconf_paramhdr) +
	    tlv_length +
	    sizeof(struct sctp_error_cause)),
	    0, M_NOWAIT, 1, MT_DATA);
	if (m_reply == NULL) {
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "asconf_error_response: couldn't get mbuf!\n");
		return (NULL);
	}
	aph = mtod(m_reply, struct sctp_asconf_paramhdr *);
	error = (struct sctp_error_cause *)(aph + 1);

	aph->correlation_id = id;
	aph->ph.param_type = htons(SCTP_ERROR_CAUSE_IND);
	error->code = htons(cause);
	error->length = tlv_length + sizeof(struct sctp_error_cause);
	aph->ph.param_length = error->length +
	    sizeof(struct sctp_asconf_paramhdr);

	if (aph->ph.param_length > MLEN) {
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "asconf_error_response: tlv_length (%xh) too big\n",
		    tlv_length);
		sctp_m_freem(m_reply);	/* discard */
		return (NULL);
	}
	if (error_tlv != NULL) {
		tlv = (uint8_t *)(error + 1);
		memcpy(tlv, error_tlv, tlv_length);
	}
	SCTP_BUF_LEN(m_reply) = aph->ph.param_length;
	error->length = htons(error->length);
	aph->ph.param_length = htons(aph->ph.param_length);

	return (m_reply);
}

static struct mbuf *
sctp_process_asconf_add_ip(struct sockaddr *src, struct sctp_asconf_paramhdr *aph,
    struct sctp_tcb *stcb, int send_hb, int response_required)
{
	struct sctp_nets *net;
	struct mbuf *m_reply = NULL;
	union sctp_sockstore store;
	struct sctp_paramhdr *ph;
	uint16_t param_type, aparam_length;
#if defined(INET) || defined(INET6)
	uint16_t param_length;
#endif
	struct sockaddr *sa;
	int zero_address = 0;
	int bad_address = 0;
#ifdef INET
	struct sockaddr_in *sin;
	struct sctp_ipv4addr_param *v4addr;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6;
	struct sctp_ipv6addr_param *v6addr;
#endif

	aparam_length = ntohs(aph->ph.param_length);
	ph = (struct sctp_paramhdr *)(aph + 1);
	param_type = ntohs(ph->param_type);
#if defined(INET) || defined(INET6)
	param_length = ntohs(ph->param_length);
#endif
	sa = &store.sa;
	switch (param_type) {
#ifdef INET
	case SCTP_IPV4_ADDRESS:
		if (param_length != sizeof(struct sctp_ipv4addr_param)) {
			/* invalid param size */
			return (NULL);
		}
		v4addr = (struct sctp_ipv4addr_param *)ph;
		sin = &store.sin;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(struct sockaddr_in);
		sin->sin_port = stcb->rport;
		sin->sin_addr.s_addr = v4addr->addr;
		if ((sin->sin_addr.s_addr == INADDR_BROADCAST) ||
		    IN_MULTICAST(ntohl(sin->sin_addr.s_addr))) {
			bad_address = 1;
		}
		if (sin->sin_addr.s_addr == INADDR_ANY)
			zero_address = 1;
		SCTPDBG(SCTP_DEBUG_ASCONF1, "process_asconf_add_ip: adding ");
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, sa);
		break;
#endif
#ifdef INET6
	case SCTP_IPV6_ADDRESS:
		if (param_length != sizeof(struct sctp_ipv6addr_param)) {
			/* invalid param size */
			return (NULL);
		}
		v6addr = (struct sctp_ipv6addr_param *)ph;
		sin6 = &store.sin6;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(struct sockaddr_in6);
		sin6->sin6_port = stcb->rport;
		memcpy((caddr_t)&sin6->sin6_addr, v6addr->addr,
		    sizeof(struct in6_addr));
		if (IN6_IS_ADDR_MULTICAST(&sin6->sin6_addr)) {
			bad_address = 1;
		}
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr))
			zero_address = 1;
		SCTPDBG(SCTP_DEBUG_ASCONF1, "process_asconf_add_ip: adding ");
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, sa);
		break;
#endif
	default:
		m_reply = sctp_asconf_error_response(aph->correlation_id,
		    SCTP_CAUSE_INVALID_PARAM, (uint8_t *)aph,
		    aparam_length);
		return (m_reply);
	}			/* end switch */

	/* if 0.0.0.0/::0, add the source address instead */
	if (zero_address && SCTP_BASE_SYSCTL(sctp_nat_friendly)) {
		sa = src;
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "process_asconf_add_ip: using source addr ");
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, src);
	}
	/* add the address */
	if (bad_address) {
		m_reply = sctp_asconf_error_response(aph->correlation_id,
		    SCTP_CAUSE_INVALID_PARAM, (uint8_t *)aph,
		    aparam_length);
	} else if (sctp_add_remote_addr(stcb, sa, &net, stcb->asoc.port,
		    SCTP_DONOT_SETSCOPE,
	    SCTP_ADDR_DYNAMIC_ADDED) != 0) {
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "process_asconf_add_ip: error adding address\n");
		m_reply = sctp_asconf_error_response(aph->correlation_id,
		    SCTP_CAUSE_RESOURCE_SHORTAGE, (uint8_t *)aph,
		    aparam_length);
	} else {
		/* notify upper layer */
		sctp_ulp_notify(SCTP_NOTIFY_ASCONF_ADD_IP, stcb, 0, sa, SCTP_SO_NOT_LOCKED);
		if (response_required) {
			m_reply =
			    sctp_asconf_success_response(aph->correlation_id);
		}
		sctp_timer_start(SCTP_TIMER_TYPE_PATHMTURAISE, stcb->sctp_ep, stcb, net);
		sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep,
		    stcb, net);
		if (send_hb) {
			sctp_send_hb(stcb, net, SCTP_SO_NOT_LOCKED);
		}
	}
	return (m_reply);
}

static int
sctp_asconf_del_remote_addrs_except(struct sctp_tcb *stcb, struct sockaddr *src)
{
	struct sctp_nets *src_net, *net;

	/* make sure the source address exists as a destination net */
	src_net = sctp_findnet(stcb, src);
	if (src_net == NULL) {
		/* not found */
		return (-1);
	}

	/* delete all destination addresses except the source */
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		if (net != src_net) {
			/* delete this address */
			sctp_remove_net(stcb, net);
			SCTPDBG(SCTP_DEBUG_ASCONF1,
			    "asconf_del_remote_addrs_except: deleting ");
			SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1,
			    (struct sockaddr *)&net->ro._l_addr);
			/* notify upper layer */
			sctp_ulp_notify(SCTP_NOTIFY_ASCONF_DELETE_IP, stcb, 0,
			    (struct sockaddr *)&net->ro._l_addr, SCTP_SO_NOT_LOCKED);
		}
	}
	return (0);
}

static struct mbuf *
sctp_process_asconf_delete_ip(struct sockaddr *src,
    struct sctp_asconf_paramhdr *aph,
    struct sctp_tcb *stcb, int response_required)
{
	struct mbuf *m_reply = NULL;
	union sctp_sockstore store;
	struct sctp_paramhdr *ph;
	uint16_t param_type, aparam_length;
#if defined(INET) || defined(INET6)
	uint16_t param_length;
#endif
	struct sockaddr *sa;
	int zero_address = 0;
	int result;
#ifdef INET
	struct sockaddr_in *sin;
	struct sctp_ipv4addr_param *v4addr;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6;
	struct sctp_ipv6addr_param *v6addr;
#endif

	aparam_length = ntohs(aph->ph.param_length);
	ph = (struct sctp_paramhdr *)(aph + 1);
	param_type = ntohs(ph->param_type);
#if defined(INET) || defined(INET6)
	param_length = ntohs(ph->param_length);
#endif
	sa = &store.sa;
	switch (param_type) {
#ifdef INET
	case SCTP_IPV4_ADDRESS:
		if (param_length != sizeof(struct sctp_ipv4addr_param)) {
			/* invalid param size */
			return (NULL);
		}
		v4addr = (struct sctp_ipv4addr_param *)ph;
		sin = &store.sin;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(struct sockaddr_in);
		sin->sin_port = stcb->rport;
		sin->sin_addr.s_addr = v4addr->addr;
		if (sin->sin_addr.s_addr == INADDR_ANY)
			zero_address = 1;
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "process_asconf_delete_ip: deleting ");
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, sa);
		break;
#endif
#ifdef INET6
	case SCTP_IPV6_ADDRESS:
		if (param_length != sizeof(struct sctp_ipv6addr_param)) {
			/* invalid param size */
			return (NULL);
		}
		v6addr = (struct sctp_ipv6addr_param *)ph;
		sin6 = &store.sin6;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(struct sockaddr_in6);
		sin6->sin6_port = stcb->rport;
		memcpy(&sin6->sin6_addr, v6addr->addr,
		    sizeof(struct in6_addr));
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr))
			zero_address = 1;
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "process_asconf_delete_ip: deleting ");
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, sa);
		break;
#endif
	default:
		m_reply = sctp_asconf_error_response(aph->correlation_id,
		    SCTP_CAUSE_UNRESOLVABLE_ADDR, (uint8_t *)aph,
		    aparam_length);
		return (m_reply);
	}

	/* make sure the source address is not being deleted */
	if (sctp_cmpaddr(sa, src)) {
		/* trying to delete the source address! */
		SCTPDBG(SCTP_DEBUG_ASCONF1, "process_asconf_delete_ip: tried to delete source addr\n");
		m_reply = sctp_asconf_error_response(aph->correlation_id,
		    SCTP_CAUSE_DELETING_SRC_ADDR, (uint8_t *)aph,
		    aparam_length);
		return (m_reply);
	}

	/* if deleting 0.0.0.0/::0, delete all addresses except src addr */
	if (zero_address && SCTP_BASE_SYSCTL(sctp_nat_friendly)) {
		result = sctp_asconf_del_remote_addrs_except(stcb, src);

		if (result) {
			/* src address did not exist? */
			SCTPDBG(SCTP_DEBUG_ASCONF1, "process_asconf_delete_ip: src addr does not exist?\n");
			/* what error to reply with?? */
			m_reply =
			    sctp_asconf_error_response(aph->correlation_id,
			    SCTP_CAUSE_REQUEST_REFUSED, (uint8_t *)aph,
			    aparam_length);
		} else if (response_required) {
			m_reply =
			    sctp_asconf_success_response(aph->correlation_id);
		}
		return (m_reply);
	}

	/* delete the address */
	result = sctp_del_remote_addr(stcb, sa);
	/*
	 * note if result == -2, the address doesn't exist in the asoc but
	 * since it's being deleted anyways, we just ack the delete -- but
	 * this probably means something has already gone awry
	 */
	if (result == -1) {
		/* only one address in the asoc */
		SCTPDBG(SCTP_DEBUG_ASCONF1, "process_asconf_delete_ip: tried to delete last IP addr!\n");
		m_reply = sctp_asconf_error_response(aph->correlation_id,
		    SCTP_CAUSE_DELETING_LAST_ADDR, (uint8_t *)aph,
		    aparam_length);
	} else {
		if (response_required) {
			m_reply = sctp_asconf_success_response(aph->correlation_id);
		}
		/* notify upper layer */
		sctp_ulp_notify(SCTP_NOTIFY_ASCONF_DELETE_IP, stcb, 0, sa, SCTP_SO_NOT_LOCKED);
	}
	return (m_reply);
}

static struct mbuf *
sctp_process_asconf_set_primary(struct sockaddr *src,
    struct sctp_asconf_paramhdr *aph,
    struct sctp_tcb *stcb, int response_required)
{
	struct mbuf *m_reply = NULL;
	union sctp_sockstore store;
	struct sctp_paramhdr *ph;
	uint16_t param_type, aparam_length;
#if defined(INET) || defined(INET6)
	uint16_t param_length;
#endif
	struct sockaddr *sa;
	int zero_address = 0;
#ifdef INET
	struct sockaddr_in *sin;
	struct sctp_ipv4addr_param *v4addr;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6;
	struct sctp_ipv6addr_param *v6addr;
#endif

	aparam_length = ntohs(aph->ph.param_length);
	ph = (struct sctp_paramhdr *)(aph + 1);
	param_type = ntohs(ph->param_type);
#if defined(INET) || defined(INET6)
	param_length = ntohs(ph->param_length);
#endif
	sa = &store.sa;
	switch (param_type) {
#ifdef INET
	case SCTP_IPV4_ADDRESS:
		if (param_length != sizeof(struct sctp_ipv4addr_param)) {
			/* invalid param size */
			return (NULL);
		}
		v4addr = (struct sctp_ipv4addr_param *)ph;
		sin = &store.sin;
		memset(sin, 0, sizeof(*sin));
		sin->sin_family = AF_INET;
		sin->sin_len = sizeof(struct sockaddr_in);
		sin->sin_addr.s_addr = v4addr->addr;
		if (sin->sin_addr.s_addr == INADDR_ANY)
			zero_address = 1;
		SCTPDBG(SCTP_DEBUG_ASCONF1, "process_asconf_set_primary: ");
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, sa);
		break;
#endif
#ifdef INET6
	case SCTP_IPV6_ADDRESS:
		if (param_length != sizeof(struct sctp_ipv6addr_param)) {
			/* invalid param size */
			return (NULL);
		}
		v6addr = (struct sctp_ipv6addr_param *)ph;
		sin6 = &store.sin6;
		memset(sin6, 0, sizeof(*sin6));
		sin6->sin6_family = AF_INET6;
		sin6->sin6_len = sizeof(struct sockaddr_in6);
		memcpy((caddr_t)&sin6->sin6_addr, v6addr->addr,
		    sizeof(struct in6_addr));
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr))
			zero_address = 1;
		SCTPDBG(SCTP_DEBUG_ASCONF1, "process_asconf_set_primary: ");
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, sa);
		break;
#endif
	default:
		m_reply = sctp_asconf_error_response(aph->correlation_id,
		    SCTP_CAUSE_UNRESOLVABLE_ADDR, (uint8_t *)aph,
		    aparam_length);
		return (m_reply);
	}

	/* if 0.0.0.0/::0, use the source address instead */
	if (zero_address && SCTP_BASE_SYSCTL(sctp_nat_friendly)) {
		sa = src;
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "process_asconf_set_primary: using source addr ");
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, src);
	}
	/* set the primary address */
	if (sctp_set_primary_addr(stcb, sa, NULL) == 0) {
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "process_asconf_set_primary: primary address set\n");
		/* notify upper layer */
		sctp_ulp_notify(SCTP_NOTIFY_ASCONF_SET_PRIMARY, stcb, 0, sa, SCTP_SO_NOT_LOCKED);
		if ((stcb->asoc.primary_destination->dest_state & SCTP_ADDR_REACHABLE) &&
		    (!(stcb->asoc.primary_destination->dest_state & SCTP_ADDR_PF)) &&
		    (stcb->asoc.alternate)) {
			sctp_free_remote_addr(stcb->asoc.alternate);
			stcb->asoc.alternate = NULL;
		}
		if (response_required) {
			m_reply = sctp_asconf_success_response(aph->correlation_id);
		}
		/*
		 * Mobility adaptation. Ideally, when the reception of SET
		 * PRIMARY with DELETE IP ADDRESS of the previous primary
		 * destination, unacknowledged DATA are retransmitted
		 * immediately to the new primary destination for seamless
		 * handover. If the destination is UNCONFIRMED and marked to
		 * REQ_PRIM, The retransmission occur when reception of the
		 * HEARTBEAT-ACK.  (See sctp_handle_heartbeat_ack in
		 * sctp_input.c) Also, when change of the primary
		 * destination, it is better that all subsequent new DATA
		 * containing already queued DATA are transmitted to the new
		 * primary destination. (by micchie)
		 */
		if ((sctp_is_mobility_feature_on(stcb->sctp_ep,
		    SCTP_MOBILITY_BASE) ||
		    sctp_is_mobility_feature_on(stcb->sctp_ep,
		    SCTP_MOBILITY_FASTHANDOFF)) &&
		    sctp_is_mobility_feature_on(stcb->sctp_ep,
		    SCTP_MOBILITY_PRIM_DELETED) &&
		    (stcb->asoc.primary_destination->dest_state &
		    SCTP_ADDR_UNCONFIRMED) == 0) {

			sctp_timer_stop(SCTP_TIMER_TYPE_PRIM_DELETED,
			    stcb->sctp_ep, stcb, NULL,
			    SCTP_FROM_SCTP_ASCONF + SCTP_LOC_1);
			if (sctp_is_mobility_feature_on(stcb->sctp_ep,
			    SCTP_MOBILITY_FASTHANDOFF)) {
				sctp_assoc_immediate_retrans(stcb,
				    stcb->asoc.primary_destination);
			}
			if (sctp_is_mobility_feature_on(stcb->sctp_ep,
			    SCTP_MOBILITY_BASE)) {
				sctp_move_chunks_from_net(stcb,
				    stcb->asoc.deleted_primary);
			}
			sctp_delete_prim_timer(stcb->sctp_ep, stcb,
			    stcb->asoc.deleted_primary);
		}
	} else {
		/* couldn't set the requested primary address! */
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "process_asconf_set_primary: set primary failed!\n");
		/* must have been an invalid address, so report */
		m_reply = sctp_asconf_error_response(aph->correlation_id,
		    SCTP_CAUSE_UNRESOLVABLE_ADDR, (uint8_t *)aph,
		    aparam_length);
	}

	return (m_reply);
}

/*
 * handles an ASCONF chunk.
 * if all parameters are processed ok, send a plain (empty) ASCONF-ACK
 */
void
sctp_handle_asconf(struct mbuf *m, unsigned int offset,
    struct sockaddr *src,
    struct sctp_asconf_chunk *cp, struct sctp_tcb *stcb,
    int first)
{
	struct sctp_association *asoc;
	uint32_t serial_num;
	struct mbuf *n, *m_ack, *m_result, *m_tail;
	struct sctp_asconf_ack_chunk *ack_cp;
	struct sctp_asconf_paramhdr *aph;
	struct sctp_ipv6addr_param *p_addr;
	unsigned int asconf_limit, cnt;
	int error = 0;		/* did an error occur? */

	/* asconf param buffer */
	uint8_t aparam_buf[SCTP_PARAM_BUFFER_SIZE];
	struct sctp_asconf_ack *ack, *ack_next;

	/* verify minimum length */
	if (ntohs(cp->ch.chunk_length) < sizeof(struct sctp_asconf_chunk)) {
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "handle_asconf: chunk too small = %xh\n",
		    ntohs(cp->ch.chunk_length));
		return;
	}
	asoc = &stcb->asoc;
	serial_num = ntohl(cp->serial_number);

	if (SCTP_TSN_GE(asoc->asconf_seq_in, serial_num)) {
		/* got a duplicate ASCONF */
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "handle_asconf: got duplicate serial number = %xh\n",
		    serial_num);
		return;
	} else if (serial_num != (asoc->asconf_seq_in + 1)) {
		SCTPDBG(SCTP_DEBUG_ASCONF1, "handle_asconf: incorrect serial number = %xh (expected next = %xh)\n",
		    serial_num, asoc->asconf_seq_in + 1);
		return;
	}

	/* it's the expected "next" sequence number, so process it */
	asoc->asconf_seq_in = serial_num;	/* update sequence */
	/* get length of all the param's in the ASCONF */
	asconf_limit = offset + ntohs(cp->ch.chunk_length);
	SCTPDBG(SCTP_DEBUG_ASCONF1,
	    "handle_asconf: asconf_limit=%u, sequence=%xh\n",
	    asconf_limit, serial_num);

	if (first) {
		/* delete old cache */
		SCTPDBG(SCTP_DEBUG_ASCONF1, "handle_asconf: Now processing first ASCONF. Try to delete old cache\n");

		TAILQ_FOREACH_SAFE(ack, &asoc->asconf_ack_sent, next, ack_next) {
			if (ack->serial_number == serial_num)
				break;
			SCTPDBG(SCTP_DEBUG_ASCONF1, "handle_asconf: delete old(%u) < first(%u)\n",
			    ack->serial_number, serial_num);
			TAILQ_REMOVE(&asoc->asconf_ack_sent, ack, next);
			if (ack->data != NULL) {
				sctp_m_freem(ack->data);
			}
			SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_asconf_ack), ack);
		}
	}

	m_ack = sctp_get_mbuf_for_msg(sizeof(struct sctp_asconf_ack_chunk), 0,
	    M_NOWAIT, 1, MT_DATA);
	if (m_ack == NULL) {
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "handle_asconf: couldn't get mbuf!\n");
		return;
	}
	m_tail = m_ack;		/* current reply chain's tail */

	/* fill in ASCONF-ACK header */
	ack_cp = mtod(m_ack, struct sctp_asconf_ack_chunk *);
	ack_cp->ch.chunk_type = SCTP_ASCONF_ACK;
	ack_cp->ch.chunk_flags = 0;
	ack_cp->serial_number = htonl(serial_num);
	/* set initial lengths (eg. just an ASCONF-ACK), ntohx at the end! */
	SCTP_BUF_LEN(m_ack) = sizeof(struct sctp_asconf_ack_chunk);
	ack_cp->ch.chunk_length = sizeof(struct sctp_asconf_ack_chunk);

	/* skip the lookup address parameter */
	offset += sizeof(struct sctp_asconf_chunk);
	p_addr = (struct sctp_ipv6addr_param *)sctp_m_getptr(m, offset, sizeof(struct sctp_paramhdr), (uint8_t *)&aparam_buf);
	if (p_addr == NULL) {
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "handle_asconf: couldn't get lookup addr!\n");
		/* respond with a missing/invalid mandatory parameter error */
		sctp_m_freem(m_ack);
		return;
	}
	/* param_length is already validated in process_control... */
	offset += ntohs(p_addr->ph.param_length);	/* skip lookup addr */
	/* get pointer to first asconf param in ASCONF */
	aph = (struct sctp_asconf_paramhdr *)sctp_m_getptr(m, offset, sizeof(struct sctp_asconf_paramhdr), (uint8_t *)&aparam_buf);
	if (aph == NULL) {
		SCTPDBG(SCTP_DEBUG_ASCONF1, "Empty ASCONF received?\n");
		goto send_reply;
	}
	/* process through all parameters */
	cnt = 0;
	while (aph != NULL) {
		unsigned int param_length, param_type;

		param_type = ntohs(aph->ph.param_type);
		param_length = ntohs(aph->ph.param_length);
		if (offset + param_length > asconf_limit) {
			/* parameter goes beyond end of chunk! */
			sctp_m_freem(m_ack);
			return;
		}
		m_result = NULL;

		if (param_length > sizeof(aparam_buf)) {
			SCTPDBG(SCTP_DEBUG_ASCONF1, "handle_asconf: param length (%u) larger than buffer size!\n", param_length);
			sctp_m_freem(m_ack);
			return;
		}
		if (param_length <= sizeof(struct sctp_paramhdr)) {
			SCTPDBG(SCTP_DEBUG_ASCONF1, "handle_asconf: param length (%u) too short\n", param_length);
			sctp_m_freem(m_ack);
		}
		/* get the entire parameter */
		aph = (struct sctp_asconf_paramhdr *)sctp_m_getptr(m, offset, param_length, aparam_buf);
		if (aph == NULL) {
			SCTPDBG(SCTP_DEBUG_ASCONF1, "handle_asconf: couldn't get entire param\n");
			sctp_m_freem(m_ack);
			return;
		}
		switch (param_type) {
		case SCTP_ADD_IP_ADDRESS:
			m_result = sctp_process_asconf_add_ip(src, aph, stcb,
			    (cnt < SCTP_BASE_SYSCTL(sctp_hb_maxburst)), error);
			cnt++;
			break;
		case SCTP_DEL_IP_ADDRESS:
			m_result = sctp_process_asconf_delete_ip(src, aph, stcb,
			    error);
			break;
		case SCTP_ERROR_CAUSE_IND:
			/* not valid in an ASCONF chunk */
			break;
		case SCTP_SET_PRIM_ADDR:
			m_result = sctp_process_asconf_set_primary(src, aph,
			    stcb, error);
			break;
		case SCTP_NAT_VTAGS:
			SCTPDBG(SCTP_DEBUG_ASCONF1, "handle_asconf: sees a NAT VTAG state parameter\n");
			break;
		case SCTP_SUCCESS_REPORT:
			/* not valid in an ASCONF chunk */
			break;
		case SCTP_ULP_ADAPTATION:
			/* FIX */
			break;
		default:
			if ((param_type & 0x8000) == 0) {
				/* Been told to STOP at this param */
				asconf_limit = offset;
				/*
				 * FIX FIX - We need to call
				 * sctp_arethere_unrecognized_parameters()
				 * to get a operr and send it for any
				 * param's with the 0x4000 bit set OR do it
				 * here ourselves... note we still must STOP
				 * if the 0x8000 bit is clear.
				 */
			}
			/* unknown/invalid param type */
			break;
		}		/* switch */

		/* add any (error) result to the reply mbuf chain */
		if (m_result != NULL) {
			SCTP_BUF_NEXT(m_tail) = m_result;
			m_tail = m_result;
			/* update lengths, make sure it's aligned too */
			SCTP_BUF_LEN(m_result) = SCTP_SIZE32(SCTP_BUF_LEN(m_result));
			ack_cp->ch.chunk_length += SCTP_BUF_LEN(m_result);
			/* set flag to force success reports */
			error = 1;
		}
		offset += SCTP_SIZE32(param_length);
		/* update remaining ASCONF message length to process */
		if (offset >= asconf_limit) {
			/* no more data in the mbuf chain */
			break;
		}
		/* get pointer to next asconf param */
		aph = (struct sctp_asconf_paramhdr *)sctp_m_getptr(m, offset,
		    sizeof(struct sctp_asconf_paramhdr),
		    (uint8_t *)&aparam_buf);
		if (aph == NULL) {
			/* can't get an asconf paramhdr */
			SCTPDBG(SCTP_DEBUG_ASCONF1, "handle_asconf: can't get asconf param hdr!\n");
			/* FIX ME - add error here... */
		}
	}

send_reply:
	ack_cp->ch.chunk_length = htons(ack_cp->ch.chunk_length);
	/* save the ASCONF-ACK reply */
	ack = SCTP_ZONE_GET(SCTP_BASE_INFO(ipi_zone_asconf_ack),
	    struct sctp_asconf_ack);
	if (ack == NULL) {
		sctp_m_freem(m_ack);
		return;
	}
	ack->serial_number = serial_num;
	ack->last_sent_to = NULL;
	ack->data = m_ack;
	ack->len = 0;
	for (n = m_ack; n != NULL; n = SCTP_BUF_NEXT(n)) {
		ack->len += SCTP_BUF_LEN(n);
	}
	TAILQ_INSERT_TAIL(&stcb->asoc.asconf_ack_sent, ack, next);

	/* see if last_control_chunk_from is set properly (use IP src addr) */
	if (stcb->asoc.last_control_chunk_from == NULL) {
		/*
		 * this could happen if the source address was just newly
		 * added
		 */
		SCTPDBG(SCTP_DEBUG_ASCONF1, "handle_asconf: looking up net for IP source address\n");
		SCTPDBG(SCTP_DEBUG_ASCONF1, "Looking for IP source: ");
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, src);
		/* look up the from address */
		stcb->asoc.last_control_chunk_from = sctp_findnet(stcb, src);
#ifdef SCTP_DEBUG
		if (stcb->asoc.last_control_chunk_from == NULL) {
			SCTPDBG(SCTP_DEBUG_ASCONF1, "handle_asconf: IP source address not found?!\n");
		}
#endif
	}
}

/*
 * does the address match? returns 0 if not, 1 if so
 */
static uint32_t
sctp_asconf_addr_match(struct sctp_asconf_addr *aa, struct sockaddr *sa)
{
	switch (sa->sa_family) {
#ifdef INET6
	case AF_INET6:
		{
			/* XXX scopeid */
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

			if ((aa->ap.addrp.ph.param_type == SCTP_IPV6_ADDRESS) &&
			    (memcmp(&aa->ap.addrp.addr, &sin6->sin6_addr,
			    sizeof(struct in6_addr)) == 0)) {
				return (1);
			}
			break;
		}
#endif
#ifdef INET
	case AF_INET:
		{
			struct sockaddr_in *sin = (struct sockaddr_in *)sa;

			if ((aa->ap.addrp.ph.param_type == SCTP_IPV4_ADDRESS) &&
			    (memcmp(&aa->ap.addrp.addr, &sin->sin_addr,
			    sizeof(struct in_addr)) == 0)) {
				return (1);
			}
			break;
		}
#endif
	default:
		break;
	}
	return (0);
}

/*
 * does the address match? returns 0 if not, 1 if so
 */
static uint32_t
sctp_addr_match(struct sctp_paramhdr *ph, struct sockaddr *sa)
{
#if defined(INET) || defined(INET6)
	uint16_t param_type, param_length;

	param_type = ntohs(ph->param_type);
	param_length = ntohs(ph->param_length);
#endif
	switch (sa->sa_family) {
#ifdef INET6
	case AF_INET6:
		{
			/* XXX scopeid */
			struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;
			struct sctp_ipv6addr_param *v6addr;

			v6addr = (struct sctp_ipv6addr_param *)ph;
			if ((param_type == SCTP_IPV6_ADDRESS) &&
			    (param_length == sizeof(struct sctp_ipv6addr_param)) &&
			    (memcmp(&v6addr->addr, &sin6->sin6_addr,
			    sizeof(struct in6_addr)) == 0)) {
				return (1);
			}
			break;
		}
#endif
#ifdef INET
	case AF_INET:
		{
			struct sockaddr_in *sin = (struct sockaddr_in *)sa;
			struct sctp_ipv4addr_param *v4addr;

			v4addr = (struct sctp_ipv4addr_param *)ph;
			if ((param_type == SCTP_IPV4_ADDRESS) &&
			    (param_length == sizeof(struct sctp_ipv4addr_param)) &&
			    (memcmp(&v4addr->addr, &sin->sin_addr,
			    sizeof(struct in_addr)) == 0)) {
				return (1);
			}
			break;
		}
#endif
	default:
		break;
	}
	return (0);
}

/*
 * Cleanup for non-responded/OP ERR'd ASCONF
 */
void
sctp_asconf_cleanup(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	/*
	 * clear out any existing asconfs going out
	 */
	sctp_timer_stop(SCTP_TIMER_TYPE_ASCONF, stcb->sctp_ep, stcb, net,
	    SCTP_FROM_SCTP_ASCONF + SCTP_LOC_2);
	stcb->asoc.asconf_seq_out_acked = stcb->asoc.asconf_seq_out;
	/* remove the old ASCONF on our outbound queue */
	sctp_toss_old_asconf(stcb);
}

/*
 * cleanup any cached source addresses that may be topologically
 * incorrect after a new address has been added to this interface.
 */
static void
sctp_asconf_nets_cleanup(struct sctp_tcb *stcb, struct sctp_ifn *ifn)
{
	struct sctp_nets *net;

	/*
	 * Ideally, we want to only clear cached routes and source addresses
	 * that are topologically incorrect.  But since there is no easy way
	 * to know whether the newly added address on the ifn would cause a
	 * routing change (i.e. a new egress interface would be chosen)
	 * without doing a new routing lookup and source address selection,
	 * we will (for now) just flush any cached route using a different
	 * ifn (and cached source addrs) and let output re-choose them
	 * during the next send on that net.
	 */
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		/*
		 * clear any cached route (and cached source address) if the
		 * route's interface is NOT the same as the address change.
		 * If it's the same interface, just clear the cached source
		 * address.
		 */
		if (SCTP_ROUTE_HAS_VALID_IFN(&net->ro) &&
		    ((ifn == NULL) ||
		    (SCTP_GET_IF_INDEX_FROM_ROUTE(&net->ro) != ifn->ifn_index))) {
			/* clear any cached route */
			RTFREE(net->ro.ro_rt);
			net->ro.ro_rt = NULL;
		}
		/* clear any cached source address */
		if (net->src_addr_selected) {
			sctp_free_ifa(net->ro._s_addr);
			net->ro._s_addr = NULL;
			net->src_addr_selected = 0;
		}
	}
}


void
sctp_assoc_immediate_retrans(struct sctp_tcb *stcb, struct sctp_nets *dstnet)
{
	int error;

	if (dstnet->dest_state & SCTP_ADDR_UNCONFIRMED) {
		return;
	}
	if (stcb->asoc.deleted_primary == NULL) {
		return;
	}

	if (!TAILQ_EMPTY(&stcb->asoc.sent_queue)) {
		SCTPDBG(SCTP_DEBUG_ASCONF1, "assoc_immediate_retrans: Deleted primary is ");
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, &stcb->asoc.deleted_primary->ro._l_addr.sa);
		SCTPDBG(SCTP_DEBUG_ASCONF1, "Current Primary is ");
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, &stcb->asoc.primary_destination->ro._l_addr.sa);
		sctp_timer_stop(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep, stcb,
		    stcb->asoc.deleted_primary,
		    SCTP_FROM_SCTP_ASCONF + SCTP_LOC_3);
		stcb->asoc.num_send_timers_up--;
		if (stcb->asoc.num_send_timers_up < 0) {
			stcb->asoc.num_send_timers_up = 0;
		}
		SCTP_TCB_LOCK_ASSERT(stcb);
		error = sctp_t3rxt_timer(stcb->sctp_ep, stcb,
		    stcb->asoc.deleted_primary);
		if (error) {
			SCTP_INP_DECR_REF(stcb->sctp_ep);
			return;
		}
		SCTP_TCB_LOCK_ASSERT(stcb);
#ifdef SCTP_AUDITING_ENABLED
		sctp_auditing(4, stcb->sctp_ep, stcb, stcb->asoc.deleted_primary);
#endif
		sctp_chunk_output(stcb->sctp_ep, stcb, SCTP_OUTPUT_FROM_T3, SCTP_SO_NOT_LOCKED);
		if ((stcb->asoc.num_send_timers_up == 0) &&
		    (stcb->asoc.sent_queue_cnt > 0)) {
			struct sctp_tmit_chunk *chk;

			chk = TAILQ_FIRST(&stcb->asoc.sent_queue);
			sctp_timer_start(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep,
			    stcb, chk->whoTo);
		}
	}
	return;
}

static int
    sctp_asconf_queue_mgmt(struct sctp_tcb *, struct sctp_ifa *, uint16_t);

void
sctp_net_immediate_retrans(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	struct sctp_tmit_chunk *chk;

	SCTPDBG(SCTP_DEBUG_ASCONF1, "net_immediate_retrans: RTO is %d\n", net->RTO);
	sctp_timer_stop(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep, stcb, net,
	    SCTP_FROM_SCTP_ASCONF + SCTP_LOC_4);
	stcb->asoc.cc_functions.sctp_set_initial_cc_param(stcb, net);
	net->error_count = 0;
	TAILQ_FOREACH(chk, &stcb->asoc.sent_queue, sctp_next) {
		if (chk->whoTo == net) {
			if (chk->sent < SCTP_DATAGRAM_RESEND) {
				chk->sent = SCTP_DATAGRAM_RESEND;
				sctp_ucount_incr(stcb->asoc.sent_queue_retran_cnt);
				sctp_flight_size_decrease(chk);
				sctp_total_flight_decrease(stcb, chk);
				net->marked_retrans++;
				stcb->asoc.marked_retrans++;
			}
		}
	}
	if (net->marked_retrans) {
		sctp_chunk_output(stcb->sctp_ep, stcb, SCTP_OUTPUT_FROM_T3, SCTP_SO_NOT_LOCKED);
	}
}

static void
sctp_path_check_and_react(struct sctp_tcb *stcb, struct sctp_ifa *newifa)
{
	struct sctp_nets *net;
	int addrnum, changed;

	/*
	 * If number of local valid addresses is 1, the valid address is
	 * probably newly added address. Several valid addresses in this
	 * association.  A source address may not be changed.  Additionally,
	 * they can be configured on a same interface as "alias" addresses.
	 * (by micchie)
	 */
	addrnum = sctp_local_addr_count(stcb);
	SCTPDBG(SCTP_DEBUG_ASCONF1, "p_check_react(): %d local addresses\n",
	    addrnum);
	if (addrnum == 1) {
		TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
			/* clear any cached route and source address */
			if (net->ro.ro_rt) {
				RTFREE(net->ro.ro_rt);
				net->ro.ro_rt = NULL;
			}
			if (net->src_addr_selected) {
				sctp_free_ifa(net->ro._s_addr);
				net->ro._s_addr = NULL;
				net->src_addr_selected = 0;
			}
			/* Retransmit unacknowledged DATA chunks immediately */
			if (sctp_is_mobility_feature_on(stcb->sctp_ep,
			    SCTP_MOBILITY_FASTHANDOFF)) {
				sctp_net_immediate_retrans(stcb, net);
			}
			/* also, SET PRIMARY is maybe already sent */
		}
		return;
	}

	/* Multiple local addresses exsist in the association.  */
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		/* clear any cached route and source address */
		if (net->ro.ro_rt) {
			RTFREE(net->ro.ro_rt);
			net->ro.ro_rt = NULL;
		}
		if (net->src_addr_selected) {
			sctp_free_ifa(net->ro._s_addr);
			net->ro._s_addr = NULL;
			net->src_addr_selected = 0;
		}
		/*
		 * Check if the nexthop is corresponding to the new address.
		 * If the new address is corresponding to the current
		 * nexthop, the path will be changed. If the new address is
		 * NOT corresponding to the current nexthop, the path will
		 * not be changed.
		 */
		SCTP_RTALLOC((sctp_route_t *)&net->ro,
		    stcb->sctp_ep->def_vrf_id,
		    stcb->sctp_ep->fibnum);
		if (net->ro.ro_rt == NULL)
			continue;

		changed = 0;
		switch (net->ro._l_addr.sa.sa_family) {
#ifdef INET
		case AF_INET:
			if (sctp_v4src_match_nexthop(newifa, (sctp_route_t *)&net->ro)) {
				changed = 1;
			}
			break;
#endif
#ifdef INET6
		case AF_INET6:
			if (sctp_v6src_match_nexthop(
			    &newifa->address.sin6, (sctp_route_t *)&net->ro)) {
				changed = 1;
			}
			break;
#endif
		default:
			break;
		}
		/*
		 * if the newly added address does not relate routing
		 * information, we skip.
		 */
		if (changed == 0)
			continue;
		/* Retransmit unacknowledged DATA chunks immediately */
		if (sctp_is_mobility_feature_on(stcb->sctp_ep,
		    SCTP_MOBILITY_FASTHANDOFF)) {
			sctp_net_immediate_retrans(stcb, net);
		}
		/* Send SET PRIMARY for this new address */
		if (net == stcb->asoc.primary_destination) {
			(void)sctp_asconf_queue_mgmt(stcb, newifa,
			    SCTP_SET_PRIM_ADDR);
		}
	}
}

/*
 * process an ADD/DELETE IP ack from peer.
 * addr: corresponding sctp_ifa to the address being added/deleted.
 * type: SCTP_ADD_IP_ADDRESS or SCTP_DEL_IP_ADDRESS.
 * flag: 1=success, 0=failure.
 */
static void
sctp_asconf_addr_mgmt_ack(struct sctp_tcb *stcb, struct sctp_ifa *addr, uint32_t flag)
{
	/*
	 * do the necessary asoc list work- if we get a failure indication,
	 * leave the address on the assoc's restricted list.  If we get a
	 * success indication, remove the address from the restricted list.
	 */
	/*
	 * Note: this will only occur for ADD_IP_ADDRESS, since
	 * DEL_IP_ADDRESS is never actually added to the list...
	 */
	if (flag) {
		/* success case, so remove from the restricted list */
		sctp_del_local_addr_restricted(stcb, addr);

		if (sctp_is_mobility_feature_on(stcb->sctp_ep,
		    SCTP_MOBILITY_BASE) ||
		    sctp_is_mobility_feature_on(stcb->sctp_ep,
		    SCTP_MOBILITY_FASTHANDOFF)) {
			sctp_path_check_and_react(stcb, addr);
			return;
		}
		/* clear any cached/topologically incorrect source addresses */
		sctp_asconf_nets_cleanup(stcb, addr->ifn_p);
	}
	/* else, leave it on the list */
}

/*
 * add an asconf add/delete/set primary IP address parameter to the queue.
 * type = SCTP_ADD_IP_ADDRESS, SCTP_DEL_IP_ADDRESS, SCTP_SET_PRIM_ADDR.
 * returns 0 if queued, -1 if not queued/removed.
 * NOTE: if adding, but a delete for the same address is already scheduled
 * (and not yet sent out), simply remove it from queue.  Same for deleting
 * an address already scheduled for add.  If a duplicate operation is found,
 * ignore the new one.
 */
static int
sctp_asconf_queue_mgmt(struct sctp_tcb *stcb, struct sctp_ifa *ifa,
    uint16_t type)
{
	struct sctp_asconf_addr *aa, *aa_next;

	/* make sure the request isn't already in the queue */
	TAILQ_FOREACH_SAFE(aa, &stcb->asoc.asconf_queue, next, aa_next) {
		/* address match? */
		if (sctp_asconf_addr_match(aa, &ifa->address.sa) == 0)
			continue;
		/*
		 * is the request already in queue but not sent? pass the
		 * request already sent in order to resolve the following
		 * case: 1. arrival of ADD, then sent 2. arrival of DEL. we
		 * can't remove the ADD request already sent 3. arrival of
		 * ADD
		 */
		if (aa->ap.aph.ph.param_type == type && aa->sent == 0) {
			return (-1);
		}
		/* is the negative request already in queue, and not sent */
		if ((aa->sent == 0) && (type == SCTP_ADD_IP_ADDRESS) &&
		    (aa->ap.aph.ph.param_type == SCTP_DEL_IP_ADDRESS)) {
			/* add requested, delete already queued */
			TAILQ_REMOVE(&stcb->asoc.asconf_queue, aa, next);
			/* remove the ifa from the restricted list */
			sctp_del_local_addr_restricted(stcb, ifa);
			/* free the asconf param */
			SCTP_FREE(aa, SCTP_M_ASC_ADDR);
			SCTPDBG(SCTP_DEBUG_ASCONF2, "asconf_queue_mgmt: add removes queued entry\n");
			return (-1);
		}
		if ((aa->sent == 0) && (type == SCTP_DEL_IP_ADDRESS) &&
		    (aa->ap.aph.ph.param_type == SCTP_ADD_IP_ADDRESS)) {
			/* delete requested, add already queued */
			TAILQ_REMOVE(&stcb->asoc.asconf_queue, aa, next);
			/* remove the aa->ifa from the restricted list */
			sctp_del_local_addr_restricted(stcb, aa->ifa);
			/* free the asconf param */
			SCTP_FREE(aa, SCTP_M_ASC_ADDR);
			SCTPDBG(SCTP_DEBUG_ASCONF2, "asconf_queue_mgmt: delete removes queued entry\n");
			return (-1);
		}
	}			/* for each aa */

	/* adding new request to the queue */
	SCTP_MALLOC(aa, struct sctp_asconf_addr *, sizeof(*aa),
	    SCTP_M_ASC_ADDR);
	if (aa == NULL) {
		/* didn't get memory */
		SCTPDBG(SCTP_DEBUG_ASCONF1, "asconf_queue_mgmt: failed to get memory!\n");
		return (-1);
	}
	aa->special_del = 0;
	/* fill in asconf address parameter fields */
	/* top level elements are "networked" during send */
	aa->ap.aph.ph.param_type = type;
	aa->ifa = ifa;
	atomic_add_int(&ifa->refcount, 1);
	/* correlation_id filled in during send routine later... */
	switch (ifa->address.sa.sa_family) {
#ifdef INET6
	case AF_INET6:
		{
			struct sockaddr_in6 *sin6;

			sin6 = &ifa->address.sin6;
			aa->ap.addrp.ph.param_type = SCTP_IPV6_ADDRESS;
			aa->ap.addrp.ph.param_length = (sizeof(struct sctp_ipv6addr_param));
			aa->ap.aph.ph.param_length = sizeof(struct sctp_asconf_paramhdr) +
			    sizeof(struct sctp_ipv6addr_param);
			memcpy(&aa->ap.addrp.addr, &sin6->sin6_addr,
			    sizeof(struct in6_addr));
			break;
		}
#endif
#ifdef INET
	case AF_INET:
		{
			struct sockaddr_in *sin;

			sin = &ifa->address.sin;
			aa->ap.addrp.ph.param_type = SCTP_IPV4_ADDRESS;
			aa->ap.addrp.ph.param_length = (sizeof(struct sctp_ipv4addr_param));
			aa->ap.aph.ph.param_length = sizeof(struct sctp_asconf_paramhdr) +
			    sizeof(struct sctp_ipv4addr_param);
			memcpy(&aa->ap.addrp.addr, &sin->sin_addr,
			    sizeof(struct in_addr));
			break;
		}
#endif
	default:
		/* invalid family! */
		SCTP_FREE(aa, SCTP_M_ASC_ADDR);
		sctp_free_ifa(ifa);
		return (-1);
	}
	aa->sent = 0;		/* clear sent flag */

	TAILQ_INSERT_TAIL(&stcb->asoc.asconf_queue, aa, next);
#ifdef SCTP_DEBUG
	if (SCTP_BASE_SYSCTL(sctp_debug_on) & SCTP_DEBUG_ASCONF2) {
		if (type == SCTP_ADD_IP_ADDRESS) {
			SCTP_PRINTF("asconf_queue_mgmt: inserted asconf ADD_IP_ADDRESS: ");
			SCTPDBG_ADDR(SCTP_DEBUG_ASCONF2, &ifa->address.sa);
		} else if (type == SCTP_DEL_IP_ADDRESS) {
			SCTP_PRINTF("asconf_queue_mgmt: appended asconf DEL_IP_ADDRESS: ");
			SCTPDBG_ADDR(SCTP_DEBUG_ASCONF2, &ifa->address.sa);
		} else {
			SCTP_PRINTF("asconf_queue_mgmt: appended asconf SET_PRIM_ADDR: ");
			SCTPDBG_ADDR(SCTP_DEBUG_ASCONF2, &ifa->address.sa);
		}
	}
#endif

	return (0);
}


/*
 * add an asconf operation for the given ifa and type.
 * type = SCTP_ADD_IP_ADDRESS, SCTP_DEL_IP_ADDRESS, SCTP_SET_PRIM_ADDR.
 * returns 0 if completed, -1 if not completed, 1 if immediate send is
 * advisable.
 */
static int
sctp_asconf_queue_add(struct sctp_tcb *stcb, struct sctp_ifa *ifa,
    uint16_t type)
{
	uint32_t status;
	int pending_delete_queued = 0;
	int last;

	/* see if peer supports ASCONF */
	if (stcb->asoc.asconf_supported == 0) {
		return (-1);
	}

	/*
	 * if this is deleting the last address from the assoc, mark it as
	 * pending.
	 */
	if ((type == SCTP_DEL_IP_ADDRESS) && !stcb->asoc.asconf_del_pending) {
		if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
			last = (sctp_local_addr_count(stcb) == 0);
		} else {
			last = (sctp_local_addr_count(stcb) == 1);
		}
		if (last) {
			/* set the pending delete info only */
			stcb->asoc.asconf_del_pending = 1;
			stcb->asoc.asconf_addr_del_pending = ifa;
			atomic_add_int(&ifa->refcount, 1);
			SCTPDBG(SCTP_DEBUG_ASCONF2,
			    "asconf_queue_add: mark delete last address pending\n");
			return (-1);
		}
	}

	/* queue an asconf parameter */
	status = sctp_asconf_queue_mgmt(stcb, ifa, type);

	/*
	 * if this is an add, and there is a delete also pending (i.e. the
	 * last local address is being changed), queue the pending delete
	 * too.
	 */
	if ((type == SCTP_ADD_IP_ADDRESS) && stcb->asoc.asconf_del_pending && (status == 0)) {
		/* queue in the pending delete */
		if (sctp_asconf_queue_mgmt(stcb,
		    stcb->asoc.asconf_addr_del_pending,
		    SCTP_DEL_IP_ADDRESS) == 0) {
			SCTPDBG(SCTP_DEBUG_ASCONF2, "asconf_queue_add: queing pending delete\n");
			pending_delete_queued = 1;
			/* clear out the pending delete info */
			stcb->asoc.asconf_del_pending = 0;
			sctp_free_ifa(stcb->asoc.asconf_addr_del_pending);
			stcb->asoc.asconf_addr_del_pending = NULL;
		}
	}

	if (pending_delete_queued) {
		struct sctp_nets *net;

		/*
		 * since we know that the only/last address is now being
		 * changed in this case, reset the cwnd/rto on all nets to
		 * start as a new address and path.  Also clear the error
		 * counts to give the assoc the best chance to complete the
		 * address change.
		 */
		TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
			stcb->asoc.cc_functions.sctp_set_initial_cc_param(stcb,
			    net);
			net->RTO = 0;
			net->error_count = 0;
		}
		stcb->asoc.overall_error_count = 0;
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_THRESHOLD_LOGGING) {
			sctp_misc_ints(SCTP_THRESHOLD_CLEAR,
			    stcb->asoc.overall_error_count,
			    0,
			    SCTP_FROM_SCTP_ASCONF,
			    __LINE__);
		}

		/* queue in an advisory set primary too */
		(void)sctp_asconf_queue_mgmt(stcb, ifa, SCTP_SET_PRIM_ADDR);
		/* let caller know we should send this out immediately */
		status = 1;
	}
	return (status);
}

/*-
 * add an asconf delete IP address parameter to the queue by sockaddr and
 * possibly with no sctp_ifa available.  This is only called by the routine
 * that checks the addresses in an INIT-ACK against the current address list.
 * returns 0 if completed, non-zero if not completed.
 * NOTE: if an add is already scheduled (and not yet sent out), simply
 * remove it from queue.  If a duplicate operation is found, ignore the
 * new one.
 */
static int
sctp_asconf_queue_sa_delete(struct sctp_tcb *stcb, struct sockaddr *sa)
{
	struct sctp_ifa *ifa;
	struct sctp_asconf_addr *aa, *aa_next;

	if (stcb == NULL) {
		return (-1);
	}
	/* see if peer supports ASCONF */
	if (stcb->asoc.asconf_supported == 0) {
		return (-1);
	}
	/* make sure the request isn't already in the queue */
	TAILQ_FOREACH_SAFE(aa, &stcb->asoc.asconf_queue, next, aa_next) {
		/* address match? */
		if (sctp_asconf_addr_match(aa, sa) == 0)
			continue;
		/* is the request already in queue (sent or not) */
		if (aa->ap.aph.ph.param_type == SCTP_DEL_IP_ADDRESS) {
			return (-1);
		}
		/* is the negative request already in queue, and not sent */
		if (aa->sent == 1)
			continue;
		if (aa->ap.aph.ph.param_type == SCTP_ADD_IP_ADDRESS) {
			/* add already queued, so remove existing entry */
			TAILQ_REMOVE(&stcb->asoc.asconf_queue, aa, next);
			sctp_del_local_addr_restricted(stcb, aa->ifa);
			/* free the entry */
			SCTP_FREE(aa, SCTP_M_ASC_ADDR);
			return (-1);
		}
	}			/* for each aa */

	/* find any existing ifa-- NOTE ifa CAN be allowed to be NULL */
	ifa = sctp_find_ifa_by_addr(sa, stcb->asoc.vrf_id, SCTP_ADDR_NOT_LOCKED);

	/* adding new request to the queue */
	SCTP_MALLOC(aa, struct sctp_asconf_addr *, sizeof(*aa),
	    SCTP_M_ASC_ADDR);
	if (aa == NULL) {
		/* didn't get memory */
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "sctp_asconf_queue_sa_delete: failed to get memory!\n");
		return (-1);
	}
	aa->special_del = 0;
	/* fill in asconf address parameter fields */
	/* top level elements are "networked" during send */
	aa->ap.aph.ph.param_type = SCTP_DEL_IP_ADDRESS;
	aa->ifa = ifa;
	if (ifa)
		atomic_add_int(&ifa->refcount, 1);
	/* correlation_id filled in during send routine later... */
	switch (sa->sa_family) {
#ifdef INET6
	case AF_INET6:
		{
			/* IPv6 address */
			struct sockaddr_in6 *sin6;

			sin6 = (struct sockaddr_in6 *)sa;
			aa->ap.addrp.ph.param_type = SCTP_IPV6_ADDRESS;
			aa->ap.addrp.ph.param_length = (sizeof(struct sctp_ipv6addr_param));
			aa->ap.aph.ph.param_length = sizeof(struct sctp_asconf_paramhdr) + sizeof(struct sctp_ipv6addr_param);
			memcpy(&aa->ap.addrp.addr, &sin6->sin6_addr,
			    sizeof(struct in6_addr));
			break;
		}
#endif
#ifdef INET
	case AF_INET:
		{
			/* IPv4 address */
			struct sockaddr_in *sin = (struct sockaddr_in *)sa;

			aa->ap.addrp.ph.param_type = SCTP_IPV4_ADDRESS;
			aa->ap.addrp.ph.param_length = (sizeof(struct sctp_ipv4addr_param));
			aa->ap.aph.ph.param_length = sizeof(struct sctp_asconf_paramhdr) + sizeof(struct sctp_ipv4addr_param);
			memcpy(&aa->ap.addrp.addr, &sin->sin_addr,
			    sizeof(struct in_addr));
			break;
		}
#endif
	default:
		/* invalid family! */
		SCTP_FREE(aa, SCTP_M_ASC_ADDR);
		if (ifa)
			sctp_free_ifa(ifa);
		return (-1);
	}
	aa->sent = 0;		/* clear sent flag */

	/* delete goes to the back of the queue */
	TAILQ_INSERT_TAIL(&stcb->asoc.asconf_queue, aa, next);

	/* sa_ignore MEMLEAK {memory is put on the tailq} */
	return (0);
}

/*
 * find a specific asconf param on our "sent" queue
 */
static struct sctp_asconf_addr *
sctp_asconf_find_param(struct sctp_tcb *stcb, uint32_t correlation_id)
{
	struct sctp_asconf_addr *aa;

	TAILQ_FOREACH(aa, &stcb->asoc.asconf_queue, next) {
		if (aa->ap.aph.correlation_id == correlation_id &&
		    aa->sent == 1) {
			/* found it */
			return (aa);
		}
	}
	/* didn't find it */
	return (NULL);
}

/*
 * process an SCTP_ERROR_CAUSE_IND for a ASCONF-ACK parameter and do
 * notifications based on the error response
 */
static void
sctp_asconf_process_error(struct sctp_tcb *stcb SCTP_UNUSED,
    struct sctp_asconf_paramhdr *aph)
{
	struct sctp_error_cause *eh;
	struct sctp_paramhdr *ph;
	uint16_t param_type;
	uint16_t error_code;

	eh = (struct sctp_error_cause *)(aph + 1);
	ph = (struct sctp_paramhdr *)(eh + 1);
	/* validate lengths */
	if (htons(eh->length) + sizeof(struct sctp_error_cause) >
	    htons(aph->ph.param_length)) {
		/* invalid error cause length */
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "asconf_process_error: cause element too long\n");
		return;
	}
	if (htons(ph->param_length) + sizeof(struct sctp_paramhdr) >
	    htons(eh->length)) {
		/* invalid included TLV length */
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "asconf_process_error: included TLV too long\n");
		return;
	}
	/* which error code ? */
	error_code = ntohs(eh->code);
	param_type = ntohs(aph->ph.param_type);
	/* FIX: this should go back up the REMOTE_ERROR ULP notify */
	switch (error_code) {
	case SCTP_CAUSE_RESOURCE_SHORTAGE:
		/* we allow ourselves to "try again" for this error */
		break;
	default:
		/* peer can't handle it... */
		switch (param_type) {
		case SCTP_ADD_IP_ADDRESS:
		case SCTP_DEL_IP_ADDRESS:
		case SCTP_SET_PRIM_ADDR:
			break;
		default:
			break;
		}
	}
}

/*
 * process an asconf queue param.
 * aparam: parameter to process, will be removed from the queue.
 * flag: 1=success case, 0=failure case
 */
static void
sctp_asconf_process_param_ack(struct sctp_tcb *stcb,
    struct sctp_asconf_addr *aparam, uint32_t flag)
{
	uint16_t param_type;

	/* process this param */
	param_type = aparam->ap.aph.ph.param_type;
	switch (param_type) {
	case SCTP_ADD_IP_ADDRESS:
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "process_param_ack: added IP address\n");
		sctp_asconf_addr_mgmt_ack(stcb, aparam->ifa, flag);
		break;
	case SCTP_DEL_IP_ADDRESS:
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "process_param_ack: deleted IP address\n");
		/* nothing really to do... lists already updated */
		break;
	case SCTP_SET_PRIM_ADDR:
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "process_param_ack: set primary IP address\n");
		/* nothing to do... peer may start using this addr */
		break;
	default:
		/* should NEVER happen */
		break;
	}

	/* remove the param and free it */
	TAILQ_REMOVE(&stcb->asoc.asconf_queue, aparam, next);
	if (aparam->ifa)
		sctp_free_ifa(aparam->ifa);
	SCTP_FREE(aparam, SCTP_M_ASC_ADDR);
}

/*
 * cleanup from a bad asconf ack parameter
 */
static void
sctp_asconf_ack_clear(struct sctp_tcb *stcb SCTP_UNUSED)
{
	/* assume peer doesn't really know how to do asconfs */
	/* XXX we could free the pending queue here */

}

void
sctp_handle_asconf_ack(struct mbuf *m, int offset,
    struct sctp_asconf_ack_chunk *cp, struct sctp_tcb *stcb,
    struct sctp_nets *net, int *abort_no_unlock)
{
	struct sctp_association *asoc;
	uint32_t serial_num;
	uint16_t ack_length;
	struct sctp_asconf_paramhdr *aph;
	struct sctp_asconf_addr *aa, *aa_next;
	uint32_t last_error_id = 0;	/* last error correlation id */
	uint32_t id;
	struct sctp_asconf_addr *ap;

	/* asconf param buffer */
	uint8_t aparam_buf[SCTP_PARAM_BUFFER_SIZE];

	/* verify minimum length */
	if (ntohs(cp->ch.chunk_length) < sizeof(struct sctp_asconf_ack_chunk)) {
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "handle_asconf_ack: chunk too small = %xh\n",
		    ntohs(cp->ch.chunk_length));
		return;
	}
	asoc = &stcb->asoc;
	serial_num = ntohl(cp->serial_number);

	/*
	 * NOTE: we may want to handle this differently- currently, we will
	 * abort when we get an ack for the expected serial number + 1 (eg.
	 * we didn't send it), process an ack normally if it is the expected
	 * serial number, and re-send the previous ack for *ALL* other
	 * serial numbers
	 */

	/*
	 * if the serial number is the next expected, but I didn't send it,
	 * abort the asoc, since someone probably just hijacked us...
	 */
	if (serial_num == (asoc->asconf_seq_out + 1)) {
		struct mbuf *op_err;
		char msg[SCTP_DIAG_INFO_LEN];

		SCTPDBG(SCTP_DEBUG_ASCONF1, "handle_asconf_ack: got unexpected next serial number! Aborting asoc!\n");
		snprintf(msg, sizeof(msg), "Never sent serial number %8.8x",
		    serial_num);
		op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
		sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
		*abort_no_unlock = 1;
		return;
	}
	if (serial_num != asoc->asconf_seq_out_acked + 1) {
		/* got a duplicate/unexpected ASCONF-ACK */
		SCTPDBG(SCTP_DEBUG_ASCONF1, "handle_asconf_ack: got duplicate/unexpected serial number = %xh (expected = %xh)\n",
		    serial_num, asoc->asconf_seq_out_acked + 1);
		return;
	}

	if (serial_num == asoc->asconf_seq_out - 1) {
		/* stop our timer */
		sctp_timer_stop(SCTP_TIMER_TYPE_ASCONF, stcb->sctp_ep, stcb, net,
		    SCTP_FROM_SCTP_ASCONF + SCTP_LOC_5);
	}

	/* process the ASCONF-ACK contents */
	ack_length = ntohs(cp->ch.chunk_length) -
	    sizeof(struct sctp_asconf_ack_chunk);
	offset += sizeof(struct sctp_asconf_ack_chunk);
	/* process through all parameters */
	while (ack_length >= sizeof(struct sctp_asconf_paramhdr)) {
		unsigned int param_length, param_type;

		/* get pointer to next asconf parameter */
		aph = (struct sctp_asconf_paramhdr *)sctp_m_getptr(m, offset,
		    sizeof(struct sctp_asconf_paramhdr), aparam_buf);
		if (aph == NULL) {
			/* can't get an asconf paramhdr */
			sctp_asconf_ack_clear(stcb);
			return;
		}
		param_type = ntohs(aph->ph.param_type);
		param_length = ntohs(aph->ph.param_length);
		if (param_length > ack_length) {
			sctp_asconf_ack_clear(stcb);
			return;
		}
		if (param_length < sizeof(struct sctp_paramhdr)) {
			sctp_asconf_ack_clear(stcb);
			return;
		}
		/* get the complete parameter... */
		if (param_length > sizeof(aparam_buf)) {
			SCTPDBG(SCTP_DEBUG_ASCONF1,
			    "param length (%u) larger than buffer size!\n", param_length);
			sctp_asconf_ack_clear(stcb);
			return;
		}
		aph = (struct sctp_asconf_paramhdr *)sctp_m_getptr(m, offset, param_length, aparam_buf);
		if (aph == NULL) {
			sctp_asconf_ack_clear(stcb);
			return;
		}
		/* correlation_id is transparent to peer, no ntohl needed */
		id = aph->correlation_id;

		switch (param_type) {
		case SCTP_ERROR_CAUSE_IND:
			last_error_id = id;
			/* find the corresponding asconf param in our queue */
			ap = sctp_asconf_find_param(stcb, id);
			if (ap == NULL) {
				/* hmm... can't find this in our queue! */
				break;
			}
			/* process the parameter, failed flag */
			sctp_asconf_process_param_ack(stcb, ap, 0);
			/* process the error response */
			sctp_asconf_process_error(stcb, aph);
			break;
		case SCTP_SUCCESS_REPORT:
			/* find the corresponding asconf param in our queue */
			ap = sctp_asconf_find_param(stcb, id);
			if (ap == NULL) {
				/* hmm... can't find this in our queue! */
				break;
			}
			/* process the parameter, success flag */
			sctp_asconf_process_param_ack(stcb, ap, 1);
			break;
		default:
			break;
		}		/* switch */

		/* update remaining ASCONF-ACK message length to process */
		ack_length -= SCTP_SIZE32(param_length);
		if (ack_length <= 0) {
			/* no more data in the mbuf chain */
			break;
		}
		offset += SCTP_SIZE32(param_length);
	}			/* while */

	/*
	 * if there are any "sent" params still on the queue, these are
	 * implicitly "success", or "failed" (if we got an error back) ...
	 * so process these appropriately
	 *
	 * we assume that the correlation_id's are monotonically increasing
	 * beginning from 1 and that we don't have *that* many outstanding
	 * at any given time
	 */
	if (last_error_id == 0)
		last_error_id--;	/* set to "max" value */
	TAILQ_FOREACH_SAFE(aa, &stcb->asoc.asconf_queue, next, aa_next) {
		if (aa->sent == 1) {
			/*
			 * implicitly successful or failed if correlation_id
			 * < last_error_id, then success else, failure
			 */
			if (aa->ap.aph.correlation_id < last_error_id)
				sctp_asconf_process_param_ack(stcb, aa, 1);
			else
				sctp_asconf_process_param_ack(stcb, aa, 0);
		} else {
			/*
			 * since we always process in order (FIFO queue) if
			 * we reach one that hasn't been sent, the rest
			 * should not have been sent either. so, we're
			 * done...
			 */
			break;
		}
	}

	/* update the next sequence number to use */
	asoc->asconf_seq_out_acked++;
	/* remove the old ASCONF on our outbound queue */
	sctp_toss_old_asconf(stcb);
	if (!TAILQ_EMPTY(&stcb->asoc.asconf_queue)) {
#ifdef SCTP_TIMER_BASED_ASCONF
		/* we have more params, so restart our timer */
		sctp_timer_start(SCTP_TIMER_TYPE_ASCONF, stcb->sctp_ep,
		    stcb, net);
#else
		/* we have more params, so send out more */
		sctp_send_asconf(stcb, net, SCTP_ADDR_NOT_LOCKED);
#endif
	}
}

#ifdef INET6
static uint32_t
sctp_is_scopeid_in_nets(struct sctp_tcb *stcb, struct sockaddr *sa)
{
	struct sockaddr_in6 *sin6, *net6;
	struct sctp_nets *net;

	if (sa->sa_family != AF_INET6) {
		/* wrong family */
		return (0);
	}
	sin6 = (struct sockaddr_in6 *)sa;
	if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr) == 0) {
		/* not link local address */
		return (0);
	}
	/* hunt through our destination nets list for this scope_id */
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		if (((struct sockaddr *)(&net->ro._l_addr))->sa_family !=
		    AF_INET6)
			continue;
		net6 = (struct sockaddr_in6 *)&net->ro._l_addr;
		if (IN6_IS_ADDR_LINKLOCAL(&net6->sin6_addr) == 0)
			continue;
		if (sctp_is_same_scope(sin6, net6)) {
			/* found one */
			return (1);
		}
	}
	/* didn't find one */
	return (0);
}
#endif

/*
 * address management functions
 */
static void
sctp_addr_mgmt_assoc(struct sctp_inpcb *inp, struct sctp_tcb *stcb,
    struct sctp_ifa *ifa, uint16_t type, int addr_locked)
{
	int status;

	if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) == 0 ||
	    sctp_is_feature_off(inp, SCTP_PCB_FLAGS_DO_ASCONF)) {
		/* subset bound, no ASCONF allowed case, so ignore */
		return;
	}
	/*
	 * note: we know this is not the subset bound, no ASCONF case eg.
	 * this is boundall or subset bound w/ASCONF allowed
	 */

	/* first, make sure that the address is IPv4 or IPv6 and not jailed */
	switch (ifa->address.sa.sa_family) {
#ifdef INET6
	case AF_INET6:
		if (prison_check_ip6(inp->ip_inp.inp.inp_cred,
		    &ifa->address.sin6.sin6_addr) != 0) {
			return;
		}
		break;
#endif
#ifdef INET
	case AF_INET:
		if (prison_check_ip4(inp->ip_inp.inp.inp_cred,
		    &ifa->address.sin.sin_addr) != 0) {
			return;
		}
		break;
#endif
	default:
		return;
	}
#ifdef INET6
	/* make sure we're "allowed" to add this type of addr */
	if (ifa->address.sa.sa_family == AF_INET6) {
		/* invalid if we're not a v6 endpoint */
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) == 0)
			return;
		/* is the v6 addr really valid ? */
		if (ifa->localifa_flags & SCTP_ADDR_IFA_UNUSEABLE) {
			return;
		}
	}
#endif
	/* put this address on the "pending/do not use yet" list */
	sctp_add_local_addr_restricted(stcb, ifa);
	/*
	 * check address scope if address is out of scope, don't queue
	 * anything... note: this would leave the address on both inp and
	 * asoc lists
	 */
	switch (ifa->address.sa.sa_family) {
#ifdef INET6
	case AF_INET6:
		{
			struct sockaddr_in6 *sin6;

			sin6 = &ifa->address.sin6;
			if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
				/* we skip unspecifed addresses */
				return;
			}
			if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
				if (stcb->asoc.scope.local_scope == 0) {
					return;
				}
				/* is it the right link local scope? */
				if (sctp_is_scopeid_in_nets(stcb, &ifa->address.sa) == 0) {
					return;
				}
			}
			if (stcb->asoc.scope.site_scope == 0 &&
			    IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr)) {
				return;
			}
			break;
		}
#endif
#ifdef INET
	case AF_INET:
		{
			struct sockaddr_in *sin;
			struct in6pcb *inp6;

			inp6 = (struct in6pcb *)&inp->ip_inp.inp;
			/* invalid if we are a v6 only endpoint */
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
			    SCTP_IPV6_V6ONLY(inp6))
				return;

			sin = &ifa->address.sin;
			if (sin->sin_addr.s_addr == 0) {
				/* we skip unspecifed addresses */
				return;
			}
			if (stcb->asoc.scope.ipv4_local_scope == 0 &&
			    IN4_ISPRIVATE_ADDRESS(&sin->sin_addr)) {
				return;
			}
			break;
		}
#endif
	default:
		/* else, not AF_INET or AF_INET6, so skip */
		return;
	}

	/* queue an asconf for this address add/delete */
	if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_DO_ASCONF)) {
		/* does the peer do asconf? */
		if (stcb->asoc.asconf_supported) {
			/* queue an asconf for this addr */
			status = sctp_asconf_queue_add(stcb, ifa, type);

			/*
			 * if queued ok, and in the open state, send out the
			 * ASCONF.  If in the non-open state, these will be
			 * sent when the state goes open.
			 */
			if (status == 0 &&
			    ((SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) ||
			    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED))) {
#ifdef SCTP_TIMER_BASED_ASCONF
				sctp_timer_start(SCTP_TIMER_TYPE_ASCONF, inp,
				    stcb, stcb->asoc.primary_destination);
#else
				sctp_send_asconf(stcb, NULL, addr_locked);
#endif
			}
		}
	}
}


int
sctp_asconf_iterator_ep(struct sctp_inpcb *inp, void *ptr, uint32_t val SCTP_UNUSED)
{
	struct sctp_asconf_iterator *asc;
	struct sctp_ifa *ifa;
	struct sctp_laddr *l;
	int cnt_invalid = 0;

	asc = (struct sctp_asconf_iterator *)ptr;
	LIST_FOREACH(l, &asc->list_of_work, sctp_nxt_addr) {
		ifa = l->ifa;
		switch (ifa->address.sa.sa_family) {
#ifdef INET6
		case AF_INET6:
			/* invalid if we're not a v6 endpoint */
			if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) == 0) {
				cnt_invalid++;
				if (asc->cnt == cnt_invalid)
					return (1);
			}
			break;
#endif
#ifdef INET
		case AF_INET:
			{
				/* invalid if we are a v6 only endpoint */
				struct in6pcb *inp6;

				inp6 = (struct in6pcb *)&inp->ip_inp.inp;
				if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
				    SCTP_IPV6_V6ONLY(inp6)) {
					cnt_invalid++;
					if (asc->cnt == cnt_invalid)
						return (1);
				}
				break;
			}
#endif
		default:
			/* invalid address family */
			cnt_invalid++;
			if (asc->cnt == cnt_invalid)
				return (1);
		}
	}
	return (0);
}

static int
sctp_asconf_iterator_ep_end(struct sctp_inpcb *inp, void *ptr, uint32_t val SCTP_UNUSED)
{
	struct sctp_ifa *ifa;
	struct sctp_asconf_iterator *asc;
	struct sctp_laddr *laddr, *nladdr, *l;

	/* Only for specific case not bound all */
	asc = (struct sctp_asconf_iterator *)ptr;
	LIST_FOREACH(l, &asc->list_of_work, sctp_nxt_addr) {
		ifa = l->ifa;
		if (l->action == SCTP_ADD_IP_ADDRESS) {
			LIST_FOREACH(laddr, &inp->sctp_addr_list,
			    sctp_nxt_addr) {
				if (laddr->ifa == ifa) {
					laddr->action = 0;
					break;
				}

			}
		} else if (l->action == SCTP_DEL_IP_ADDRESS) {
			LIST_FOREACH_SAFE(laddr, &inp->sctp_addr_list, sctp_nxt_addr, nladdr) {
				/* remove only after all guys are done */
				if (laddr->ifa == ifa) {
					sctp_del_local_addr_ep(inp, ifa);
				}
			}
		}
	}
	return (0);
}

void
sctp_asconf_iterator_stcb(struct sctp_inpcb *inp, struct sctp_tcb *stcb,
    void *ptr, uint32_t val SCTP_UNUSED)
{
	struct sctp_asconf_iterator *asc;
	struct sctp_ifa *ifa;
	struct sctp_laddr *l;
	int cnt_invalid = 0;
	int type, status;
	int num_queued = 0;

	asc = (struct sctp_asconf_iterator *)ptr;
	LIST_FOREACH(l, &asc->list_of_work, sctp_nxt_addr) {
		ifa = l->ifa;
		type = l->action;

		/* address's vrf_id must be the vrf_id of the assoc */
		if (ifa->vrf_id != stcb->asoc.vrf_id) {
			continue;
		}

		/* Same checks again for assoc */
		switch (ifa->address.sa.sa_family) {
#ifdef INET6
		case AF_INET6:
			{
				/* invalid if we're not a v6 endpoint */
				struct sockaddr_in6 *sin6;

				if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) == 0) {
					cnt_invalid++;
					if (asc->cnt == cnt_invalid)
						return;
					else
						continue;
				}
				sin6 = &ifa->address.sin6;
				if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
					/* we skip unspecifed addresses */
					continue;
				}
				if (prison_check_ip6(inp->ip_inp.inp.inp_cred,
				    &sin6->sin6_addr) != 0) {
					continue;
				}
				if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
					if (stcb->asoc.scope.local_scope == 0) {
						continue;
					}
					/* is it the right link local scope? */
					if (sctp_is_scopeid_in_nets(stcb, &ifa->address.sa) == 0) {
						continue;
					}
				}
				break;
			}
#endif
#ifdef INET
		case AF_INET:
			{
				/* invalid if we are a v6 only endpoint */
				struct in6pcb *inp6;
				struct sockaddr_in *sin;

				inp6 = (struct in6pcb *)&inp->ip_inp.inp;
				/* invalid if we are a v6 only endpoint */
				if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
				    SCTP_IPV6_V6ONLY(inp6))
					continue;

				sin = &ifa->address.sin;
				if (sin->sin_addr.s_addr == 0) {
					/* we skip unspecifed addresses */
					continue;
				}
				if (prison_check_ip4(inp->ip_inp.inp.inp_cred,
				    &sin->sin_addr) != 0) {
					continue;
				}
				if (stcb->asoc.scope.ipv4_local_scope == 0 &&
				    IN4_ISPRIVATE_ADDRESS(&sin->sin_addr)) {
					continue;
				}
				if ((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) &&
				    SCTP_IPV6_V6ONLY(inp6)) {
					cnt_invalid++;
					if (asc->cnt == cnt_invalid)
						return;
					else
						continue;
				}
				break;
			}
#endif
		default:
			/* invalid address family */
			cnt_invalid++;
			if (asc->cnt == cnt_invalid)
				return;
			else
				continue;
			break;
		}

		if (type == SCTP_ADD_IP_ADDRESS) {
			/* prevent this address from being used as a source */
			sctp_add_local_addr_restricted(stcb, ifa);
		} else if (type == SCTP_DEL_IP_ADDRESS) {
			struct sctp_nets *net;

			TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
				sctp_rtentry_t *rt;

				/* delete this address if cached */
				if (net->ro._s_addr == ifa) {
					sctp_free_ifa(net->ro._s_addr);
					net->ro._s_addr = NULL;
					net->src_addr_selected = 0;
					rt = net->ro.ro_rt;
					if (rt) {
						RTFREE(rt);
						net->ro.ro_rt = NULL;
					}
					/*
					 * Now we deleted our src address,
					 * should we not also now reset the
					 * cwnd/rto to start as if its a new
					 * address?
					 */
					stcb->asoc.cc_functions.sctp_set_initial_cc_param(stcb, net);
					net->RTO = 0;

				}
			}
		} else if (type == SCTP_SET_PRIM_ADDR) {
			if ((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) == 0) {
				/* must validate the ifa is in the ep */
				if (sctp_is_addr_in_ep(stcb->sctp_ep, ifa) == 0) {
					continue;
				}
			} else {
				/* Need to check scopes for this guy */
				if (sctp_is_address_in_scope(ifa, &stcb->asoc.scope, 0) == 0) {
					continue;
				}
			}
		}
		/* queue an asconf for this address add/delete */
		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_DO_ASCONF) &&
		    stcb->asoc.asconf_supported == 1) {
			/* queue an asconf for this addr */
			status = sctp_asconf_queue_add(stcb, ifa, type);
			/*
			 * if queued ok, and in the open state, update the
			 * count of queued params.  If in the non-open
			 * state, these get sent when the assoc goes open.
			 */
			if ((SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) ||
			    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
				if (status >= 0) {
					num_queued++;
				}
			}
		}
	}
	/*
	 * If we have queued params in the open state, send out an ASCONF.
	 */
	if (num_queued > 0) {
		sctp_send_asconf(stcb, NULL, SCTP_ADDR_NOT_LOCKED);
	}
}

void
sctp_asconf_iterator_end(void *ptr, uint32_t val SCTP_UNUSED)
{
	struct sctp_asconf_iterator *asc;
	struct sctp_ifa *ifa;
	struct sctp_laddr *l, *nl;

	asc = (struct sctp_asconf_iterator *)ptr;
	LIST_FOREACH_SAFE(l, &asc->list_of_work, sctp_nxt_addr, nl) {
		ifa = l->ifa;
		if (l->action == SCTP_ADD_IP_ADDRESS) {
			/* Clear the defer use flag */
			ifa->localifa_flags &= ~SCTP_ADDR_DEFER_USE;
		}
		sctp_free_ifa(ifa);
		SCTP_ZONE_FREE(SCTP_BASE_INFO(ipi_zone_laddr), l);
		SCTP_DECR_LADDR_COUNT();
	}
	SCTP_FREE(asc, SCTP_M_ASC_IT);
}

/*
 * sa is the sockaddr to ask the peer to set primary to.
 * returns: 0 = completed, -1 = error
 */
int32_t
sctp_set_primary_ip_address_sa(struct sctp_tcb *stcb, struct sockaddr *sa)
{
	uint32_t vrf_id;
	struct sctp_ifa *ifa;

	/* find the ifa for the desired set primary */
	vrf_id = stcb->asoc.vrf_id;
	ifa = sctp_find_ifa_by_addr(sa, vrf_id, SCTP_ADDR_NOT_LOCKED);
	if (ifa == NULL) {
		/* Invalid address */
		return (-1);
	}

	/* queue an ASCONF:SET_PRIM_ADDR to be sent */
	if (!sctp_asconf_queue_add(stcb, ifa, SCTP_SET_PRIM_ADDR)) {
		/* set primary queuing succeeded */
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "set_primary_ip_address_sa: queued on tcb=%p, ",
		    (void *)stcb);
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, sa);
		if ((SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) ||
		    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
#ifdef SCTP_TIMER_BASED_ASCONF
			sctp_timer_start(SCTP_TIMER_TYPE_ASCONF,
			    stcb->sctp_ep, stcb,
			    stcb->asoc.primary_destination);
#else
			sctp_send_asconf(stcb, NULL, SCTP_ADDR_NOT_LOCKED);
#endif
		}
	} else {
		SCTPDBG(SCTP_DEBUG_ASCONF1, "set_primary_ip_address_sa: failed to add to queue on tcb=%p, ",
		    (void *)stcb);
		SCTPDBG_ADDR(SCTP_DEBUG_ASCONF1, sa);
		return (-1);
	}
	return (0);
}

int
sctp_is_addr_pending(struct sctp_tcb *stcb, struct sctp_ifa *sctp_ifa)
{
	struct sctp_tmit_chunk *chk, *nchk;
	unsigned int offset, asconf_limit;
	struct sctp_asconf_chunk *acp;
	struct sctp_asconf_paramhdr *aph;
	uint8_t aparam_buf[SCTP_PARAM_BUFFER_SIZE];
	struct sctp_paramhdr *ph;
	int add_cnt, del_cnt;
	uint16_t last_param_type;

	add_cnt = del_cnt = 0;
	last_param_type = 0;
	TAILQ_FOREACH_SAFE(chk, &stcb->asoc.asconf_send_queue, sctp_next, nchk) {
		if (chk->data == NULL) {
			SCTPDBG(SCTP_DEBUG_ASCONF1, "is_addr_pending: No mbuf data?\n");
			continue;
		}
		offset = 0;
		acp = mtod(chk->data, struct sctp_asconf_chunk *);
		offset += sizeof(struct sctp_asconf_chunk);
		asconf_limit = ntohs(acp->ch.chunk_length);
		ph = (struct sctp_paramhdr *)sctp_m_getptr(chk->data, offset, sizeof(struct sctp_paramhdr), aparam_buf);
		if (ph == NULL) {
			SCTPDBG(SCTP_DEBUG_ASCONF1, "is_addr_pending: couldn't get lookup addr!\n");
			continue;
		}
		offset += ntohs(ph->param_length);

		aph = (struct sctp_asconf_paramhdr *)sctp_m_getptr(chk->data, offset, sizeof(struct sctp_asconf_paramhdr), aparam_buf);
		if (aph == NULL) {
			SCTPDBG(SCTP_DEBUG_ASCONF1, "is_addr_pending: Empty ASCONF will be sent?\n");
			continue;
		}
		while (aph != NULL) {
			unsigned int param_length, param_type;

			param_type = ntohs(aph->ph.param_type);
			param_length = ntohs(aph->ph.param_length);
			if (offset + param_length > asconf_limit) {
				/* parameter goes beyond end of chunk! */
				break;
			}
			if (param_length > sizeof(aparam_buf)) {
				SCTPDBG(SCTP_DEBUG_ASCONF1, "is_addr_pending: param length (%u) larger than buffer size!\n", param_length);
				break;
			}
			if (param_length <= sizeof(struct sctp_paramhdr)) {
				SCTPDBG(SCTP_DEBUG_ASCONF1, "is_addr_pending: param length(%u) too short\n", param_length);
				break;
			}

			aph = (struct sctp_asconf_paramhdr *)sctp_m_getptr(chk->data, offset, param_length, aparam_buf);
			if (aph == NULL) {
				SCTPDBG(SCTP_DEBUG_ASCONF1, "is_addr_pending: couldn't get entire param\n");
				break;
			}

			ph = (struct sctp_paramhdr *)(aph + 1);
			if (sctp_addr_match(ph, &sctp_ifa->address.sa) != 0) {
				switch (param_type) {
				case SCTP_ADD_IP_ADDRESS:
					add_cnt++;
					break;
				case SCTP_DEL_IP_ADDRESS:
					del_cnt++;
					break;
				default:
					break;
				}
				last_param_type = param_type;
			}

			offset += SCTP_SIZE32(param_length);
			if (offset >= asconf_limit) {
				/* no more data in the mbuf chain */
				break;
			}
			/* get pointer to next asconf param */
			aph = (struct sctp_asconf_paramhdr *)sctp_m_getptr(chk->data, offset, sizeof(struct sctp_asconf_paramhdr), aparam_buf);
		}
	}

	/*
	 * we want to find the sequences which consist of ADD -> DEL -> ADD
	 * or DEL -> ADD
	 */
	if (add_cnt > del_cnt ||
	    (add_cnt == del_cnt && last_param_type == SCTP_ADD_IP_ADDRESS)) {
		return (1);
	}
	return (0);
}

static struct sockaddr *
sctp_find_valid_localaddr(struct sctp_tcb *stcb, int addr_locked)
{
	struct sctp_vrf *vrf = NULL;
	struct sctp_ifn *sctp_ifn;
	struct sctp_ifa *sctp_ifa;

	if (addr_locked == SCTP_ADDR_NOT_LOCKED)
		SCTP_IPI_ADDR_RLOCK();
	vrf = sctp_find_vrf(stcb->asoc.vrf_id);
	if (vrf == NULL) {
		if (addr_locked == SCTP_ADDR_NOT_LOCKED)
			SCTP_IPI_ADDR_RUNLOCK();
		return (NULL);
	}
	LIST_FOREACH(sctp_ifn, &vrf->ifnlist, next_ifn) {
		if (stcb->asoc.scope.loopback_scope == 0 &&
		    SCTP_IFN_IS_IFT_LOOP(sctp_ifn)) {
			/* Skip if loopback_scope not set */
			continue;
		}
		LIST_FOREACH(sctp_ifa, &sctp_ifn->ifalist, next_ifa) {
			switch (sctp_ifa->address.sa.sa_family) {
#ifdef INET
			case AF_INET:
				if (stcb->asoc.scope.ipv4_addr_legal) {
					struct sockaddr_in *sin;

					sin = &sctp_ifa->address.sin;
					if (sin->sin_addr.s_addr == 0) {
						/* skip unspecifed addresses */
						continue;
					}
					if (prison_check_ip4(stcb->sctp_ep->ip_inp.inp.inp_cred,
					    &sin->sin_addr) != 0) {
						continue;
					}
					if (stcb->asoc.scope.ipv4_local_scope == 0 &&
					    IN4_ISPRIVATE_ADDRESS(&sin->sin_addr))
						continue;

					if (sctp_is_addr_restricted(stcb, sctp_ifa) &&
					    (!sctp_is_addr_pending(stcb, sctp_ifa)))
						continue;
					/*
					 * found a valid local v4 address to
					 * use
					 */
					if (addr_locked == SCTP_ADDR_NOT_LOCKED)
						SCTP_IPI_ADDR_RUNLOCK();
					return (&sctp_ifa->address.sa);
				}
				break;
#endif
#ifdef INET6
			case AF_INET6:
				if (stcb->asoc.scope.ipv6_addr_legal) {
					struct sockaddr_in6 *sin6;

					if (sctp_ifa->localifa_flags & SCTP_ADDR_IFA_UNUSEABLE) {
						continue;
					}

					sin6 = &sctp_ifa->address.sin6;
					if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
						/*
						 * we skip unspecifed
						 * addresses
						 */
						continue;
					}
					if (prison_check_ip6(stcb->sctp_ep->ip_inp.inp.inp_cred,
					    &sin6->sin6_addr) != 0) {
						continue;
					}
					if (stcb->asoc.scope.local_scope == 0 &&
					    IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))
						continue;
					if (stcb->asoc.scope.site_scope == 0 &&
					    IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr))
						continue;

					if (sctp_is_addr_restricted(stcb, sctp_ifa) &&
					    (!sctp_is_addr_pending(stcb, sctp_ifa)))
						continue;
					/*
					 * found a valid local v6 address to
					 * use
					 */
					if (addr_locked == SCTP_ADDR_NOT_LOCKED)
						SCTP_IPI_ADDR_RUNLOCK();
					return (&sctp_ifa->address.sa);
				}
				break;
#endif
			default:
				break;
			}
		}
	}
	/* no valid addresses found */
	if (addr_locked == SCTP_ADDR_NOT_LOCKED)
		SCTP_IPI_ADDR_RUNLOCK();
	return (NULL);
}

static struct sockaddr *
sctp_find_valid_localaddr_ep(struct sctp_tcb *stcb)
{
	struct sctp_laddr *laddr;

	LIST_FOREACH(laddr, &stcb->sctp_ep->sctp_addr_list, sctp_nxt_addr) {
		if (laddr->ifa == NULL) {
			continue;
		}
		/* is the address restricted ? */
		if (sctp_is_addr_restricted(stcb, laddr->ifa) &&
		    (!sctp_is_addr_pending(stcb, laddr->ifa)))
			continue;

		/* found a valid local address to use */
		return (&laddr->ifa->address.sa);
	}
	/* no valid addresses found */
	return (NULL);
}

/*
 * builds an ASCONF chunk from queued ASCONF params.
 * returns NULL on error (no mbuf, no ASCONF params queued, etc).
 */
struct mbuf *
sctp_compose_asconf(struct sctp_tcb *stcb, int *retlen, int addr_locked)
{
	struct mbuf *m_asconf, *m_asconf_chk;
	struct sctp_asconf_addr *aa;
	struct sctp_asconf_chunk *acp;
	struct sctp_asconf_paramhdr *aph;
	struct sctp_asconf_addr_param *aap;
	uint32_t p_length;
	uint32_t correlation_id = 1;	/* 0 is reserved... */
	caddr_t ptr, lookup_ptr;
	uint8_t lookup_used = 0;

	/* are there any asconf params to send? */
	TAILQ_FOREACH(aa, &stcb->asoc.asconf_queue, next) {
		if (aa->sent == 0)
			break;
	}
	if (aa == NULL)
		return (NULL);

	/*
	 * get a chunk header mbuf and a cluster for the asconf params since
	 * it's simpler to fill in the asconf chunk header lookup address on
	 * the fly
	 */
	m_asconf_chk = sctp_get_mbuf_for_msg(sizeof(struct sctp_asconf_chunk), 0, M_NOWAIT, 1, MT_DATA);
	if (m_asconf_chk == NULL) {
		/* no mbuf's */
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "compose_asconf: couldn't get chunk mbuf!\n");
		return (NULL);
	}
	m_asconf = sctp_get_mbuf_for_msg(MCLBYTES, 0, M_NOWAIT, 1, MT_DATA);
	if (m_asconf == NULL) {
		/* no mbuf's */
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "compose_asconf: couldn't get mbuf!\n");
		sctp_m_freem(m_asconf_chk);
		return (NULL);
	}
	SCTP_BUF_LEN(m_asconf_chk) = sizeof(struct sctp_asconf_chunk);
	SCTP_BUF_LEN(m_asconf) = 0;
	acp = mtod(m_asconf_chk, struct sctp_asconf_chunk *);
	memset(acp, 0, sizeof(struct sctp_asconf_chunk));
	/* save pointers to lookup address and asconf params */
	lookup_ptr = (caddr_t)(acp + 1);	/* after the header */
	ptr = mtod(m_asconf, caddr_t);	/* beginning of cluster */

	/* fill in chunk header info */
	acp->ch.chunk_type = SCTP_ASCONF;
	acp->ch.chunk_flags = 0;
	acp->serial_number = htonl(stcb->asoc.asconf_seq_out);
	stcb->asoc.asconf_seq_out++;

	/* add parameters... up to smallest MTU allowed */
	TAILQ_FOREACH(aa, &stcb->asoc.asconf_queue, next) {
		if (aa->sent)
			continue;
		/* get the parameter length */
		p_length = SCTP_SIZE32(aa->ap.aph.ph.param_length);
		/* will it fit in current chunk? */
		if ((SCTP_BUF_LEN(m_asconf) + p_length > stcb->asoc.smallest_mtu) ||
		    (SCTP_BUF_LEN(m_asconf) + p_length > MCLBYTES)) {
			/* won't fit, so we're done with this chunk */
			break;
		}
		/* assign (and store) a correlation id */
		aa->ap.aph.correlation_id = correlation_id++;

		/*
		 * fill in address if we're doing a delete this is a simple
		 * way for us to fill in the correlation address, which
		 * should only be used by the peer if we're deleting our
		 * source address and adding a new address (e.g. renumbering
		 * case)
		 */
		if (lookup_used == 0 &&
		    (aa->special_del == 0) &&
		    aa->ap.aph.ph.param_type == SCTP_DEL_IP_ADDRESS) {
			struct sctp_ipv6addr_param *lookup;
			uint16_t p_size, addr_size;

			lookup = (struct sctp_ipv6addr_param *)lookup_ptr;
			lookup->ph.param_type =
			    htons(aa->ap.addrp.ph.param_type);
			if (aa->ap.addrp.ph.param_type == SCTP_IPV6_ADDRESS) {
				/* copy IPv6 address */
				p_size = sizeof(struct sctp_ipv6addr_param);
				addr_size = sizeof(struct in6_addr);
			} else {
				/* copy IPv4 address */
				p_size = sizeof(struct sctp_ipv4addr_param);
				addr_size = sizeof(struct in_addr);
			}
			lookup->ph.param_length = htons(SCTP_SIZE32(p_size));
			memcpy(lookup->addr, &aa->ap.addrp.addr, addr_size);
			SCTP_BUF_LEN(m_asconf_chk) += SCTP_SIZE32(p_size);
			lookup_used = 1;
		}
		/* copy into current space */
		memcpy(ptr, &aa->ap, p_length);

		/* network elements and update lengths */
		aph = (struct sctp_asconf_paramhdr *)ptr;
		aap = (struct sctp_asconf_addr_param *)ptr;
		/* correlation_id is transparent to peer, no htonl needed */
		aph->ph.param_type = htons(aph->ph.param_type);
		aph->ph.param_length = htons(aph->ph.param_length);
		aap->addrp.ph.param_type = htons(aap->addrp.ph.param_type);
		aap->addrp.ph.param_length = htons(aap->addrp.ph.param_length);

		SCTP_BUF_LEN(m_asconf) += SCTP_SIZE32(p_length);
		ptr += SCTP_SIZE32(p_length);

		/*
		 * these params are removed off the pending list upon
		 * getting an ASCONF-ACK back from the peer, just set flag
		 */
		aa->sent = 1;
	}
	/* check to see if the lookup addr has been populated yet */
	if (lookup_used == 0) {
		/* NOTE: if the address param is optional, can skip this... */
		/* add any valid (existing) address... */
		struct sctp_ipv6addr_param *lookup;
		uint16_t p_size, addr_size;
		struct sockaddr *found_addr;
		caddr_t addr_ptr;

		if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL)
			found_addr = sctp_find_valid_localaddr(stcb,
			    addr_locked);
		else
			found_addr = sctp_find_valid_localaddr_ep(stcb);

		lookup = (struct sctp_ipv6addr_param *)lookup_ptr;
		if (found_addr != NULL) {
			switch (found_addr->sa_family) {
#ifdef INET6
			case AF_INET6:
				/* copy IPv6 address */
				lookup->ph.param_type =
				    htons(SCTP_IPV6_ADDRESS);
				p_size = sizeof(struct sctp_ipv6addr_param);
				addr_size = sizeof(struct in6_addr);
				addr_ptr = (caddr_t)&((struct sockaddr_in6 *)
				    found_addr)->sin6_addr;
				break;
#endif
#ifdef INET
			case AF_INET:
				/* copy IPv4 address */
				lookup->ph.param_type =
				    htons(SCTP_IPV4_ADDRESS);
				p_size = sizeof(struct sctp_ipv4addr_param);
				addr_size = sizeof(struct in_addr);
				addr_ptr = (caddr_t)&((struct sockaddr_in *)
				    found_addr)->sin_addr;
				break;
#endif
			default:
				p_size = 0;
				addr_size = 0;
				addr_ptr = NULL;
				break;
			}
			lookup->ph.param_length = htons(SCTP_SIZE32(p_size));
			memcpy(lookup->addr, addr_ptr, addr_size);
			SCTP_BUF_LEN(m_asconf_chk) += SCTP_SIZE32(p_size);
		} else {
			/* uh oh... don't have any address?? */
			SCTPDBG(SCTP_DEBUG_ASCONF1,
			    "compose_asconf: no lookup addr!\n");
			/* XXX for now, we send a IPv4 address of 0.0.0.0 */
			lookup->ph.param_type = htons(SCTP_IPV4_ADDRESS);
			lookup->ph.param_length = htons(SCTP_SIZE32(sizeof(struct sctp_ipv4addr_param)));
			memset(lookup->addr, 0, sizeof(struct in_addr));
			SCTP_BUF_LEN(m_asconf_chk) += SCTP_SIZE32(sizeof(struct sctp_ipv4addr_param));
		}
	}
	/* chain it all together */
	SCTP_BUF_NEXT(m_asconf_chk) = m_asconf;
	*retlen = SCTP_BUF_LEN(m_asconf_chk) + SCTP_BUF_LEN(m_asconf);
	acp->ch.chunk_length = htons(*retlen);

	return (m_asconf_chk);
}

/*
 * section to handle address changes before an association is up eg. changes
 * during INIT/INIT-ACK/COOKIE-ECHO handshake
 */

/*
 * processes the (local) addresses in the INIT-ACK chunk
 */
static void
sctp_process_initack_addresses(struct sctp_tcb *stcb, struct mbuf *m,
    unsigned int offset, unsigned int length)
{
	struct sctp_paramhdr tmp_param, *ph;
	uint16_t plen, ptype;
	struct sctp_ifa *sctp_ifa;
	union sctp_sockstore store;
#ifdef INET6
	struct sctp_ipv6addr_param addr6_store;
#endif
#ifdef INET
	struct sctp_ipv4addr_param addr4_store;
#endif

	SCTPDBG(SCTP_DEBUG_ASCONF2, "processing init-ack addresses\n");
	if (stcb == NULL)	/* Un-needed check for SA */
		return;

	/* convert to upper bound */
	length += offset;

	if ((offset + sizeof(struct sctp_paramhdr)) > length) {
		return;
	}
	/* go through the addresses in the init-ack */
	ph = (struct sctp_paramhdr *)
	    sctp_m_getptr(m, offset, sizeof(struct sctp_paramhdr),
	    (uint8_t *)&tmp_param);
	while (ph != NULL) {
		ptype = ntohs(ph->param_type);
		plen = ntohs(ph->param_length);
		switch (ptype) {
#ifdef INET6
		case SCTP_IPV6_ADDRESS:
			{
				struct sctp_ipv6addr_param *a6p;

				/* get the entire IPv6 address param */
				a6p = (struct sctp_ipv6addr_param *)
				    sctp_m_getptr(m, offset,
				    sizeof(struct sctp_ipv6addr_param),
				    (uint8_t *)&addr6_store);
				if (plen != sizeof(struct sctp_ipv6addr_param) ||
				    a6p == NULL) {
					return;
				}
				memset(&store, 0, sizeof(union sctp_sockstore));
				store.sin6.sin6_family = AF_INET6;
				store.sin6.sin6_len = sizeof(struct sockaddr_in6);
				store.sin6.sin6_port = stcb->rport;
				memcpy(&store.sin6.sin6_addr, a6p->addr, sizeof(struct in6_addr));
				break;
			}
#endif
#ifdef INET
		case SCTP_IPV4_ADDRESS:
			{
				struct sctp_ipv4addr_param *a4p;

				/* get the entire IPv4 address param */
				a4p = (struct sctp_ipv4addr_param *)sctp_m_getptr(m, offset,
				    sizeof(struct sctp_ipv4addr_param),
				    (uint8_t *)&addr4_store);
				if (plen != sizeof(struct sctp_ipv4addr_param) ||
				    a4p == NULL) {
					return;
				}
				memset(&store, 0, sizeof(union sctp_sockstore));
				store.sin.sin_family = AF_INET;
				store.sin.sin_len = sizeof(struct sockaddr_in);
				store.sin.sin_port = stcb->rport;
				store.sin.sin_addr.s_addr = a4p->addr;
				break;
			}
#endif
		default:
			goto next_addr;
		}

		/* see if this address really (still) exists */
		sctp_ifa = sctp_find_ifa_by_addr(&store.sa, stcb->asoc.vrf_id,
		    SCTP_ADDR_NOT_LOCKED);
		if (sctp_ifa == NULL) {
			/* address doesn't exist anymore */
			int status;

			/* are ASCONFs allowed ? */
			if ((sctp_is_feature_on(stcb->sctp_ep,
			    SCTP_PCB_FLAGS_DO_ASCONF)) &&
			    stcb->asoc.asconf_supported) {
				/* queue an ASCONF DEL_IP_ADDRESS */
				status = sctp_asconf_queue_sa_delete(stcb, &store.sa);
				/*
				 * if queued ok, and in correct state, send
				 * out the ASCONF.
				 */
				if (status == 0 &&
				    SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) {
#ifdef SCTP_TIMER_BASED_ASCONF
					sctp_timer_start(SCTP_TIMER_TYPE_ASCONF,
					    stcb->sctp_ep, stcb,
					    stcb->asoc.primary_destination);
#else
					sctp_send_asconf(stcb, NULL, SCTP_ADDR_NOT_LOCKED);
#endif
				}
			}
		}

next_addr:
		/*
		 * Sanity check:  Make sure the length isn't 0, otherwise
		 * we'll be stuck in this loop for a long time...
		 */
		if (SCTP_SIZE32(plen) == 0) {
			SCTP_PRINTF("process_initack_addrs: bad len (%d) type=%xh\n",
			    plen, ptype);
			return;
		}
		/* get next parameter */
		offset += SCTP_SIZE32(plen);
		if ((offset + sizeof(struct sctp_paramhdr)) > length)
			return;
		ph = (struct sctp_paramhdr *)sctp_m_getptr(m, offset,
		    sizeof(struct sctp_paramhdr), (uint8_t *)&tmp_param);
	}			/* while */
}

/* FIX ME: need to verify return result for v6 address type if v6 disabled */
/*
 * checks to see if a specific address is in the initack address list returns
 * 1 if found, 0 if not
 */
static uint32_t
sctp_addr_in_initack(struct mbuf *m, uint32_t offset, uint32_t length, struct sockaddr *sa)
{
	struct sctp_paramhdr tmp_param, *ph;
	uint16_t plen, ptype;
#ifdef INET
	struct sockaddr_in *sin;
	struct sctp_ipv4addr_param *a4p;
	struct sctp_ipv6addr_param addr4_store;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6;
	struct sctp_ipv6addr_param *a6p;
	struct sctp_ipv6addr_param addr6_store;
	struct sockaddr_in6 sin6_tmp;
#endif

	switch (sa->sa_family) {
#ifdef INET
	case AF_INET:
		break;
#endif
#ifdef INET6
	case AF_INET6:
		break;
#endif
	default:
		return (0);
	}

	SCTPDBG(SCTP_DEBUG_ASCONF2, "find_initack_addr: starting search for ");
	SCTPDBG_ADDR(SCTP_DEBUG_ASCONF2, sa);
	/* convert to upper bound */
	length += offset;

	if ((offset + sizeof(struct sctp_paramhdr)) > length) {
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "find_initack_addr: invalid offset?\n");
		return (0);
	}
	/* go through the addresses in the init-ack */
	ph = (struct sctp_paramhdr *)sctp_m_getptr(m, offset,
	    sizeof(struct sctp_paramhdr), (uint8_t *)&tmp_param);
	while (ph != NULL) {
		ptype = ntohs(ph->param_type);
		plen = ntohs(ph->param_length);
		switch (ptype) {
#ifdef INET6
		case SCTP_IPV6_ADDRESS:
			if (sa->sa_family == AF_INET6) {
				/* get the entire IPv6 address param */
				if (plen != sizeof(struct sctp_ipv6addr_param)) {
					break;
				}
				/* get the entire IPv6 address param */
				a6p = (struct sctp_ipv6addr_param *)
				    sctp_m_getptr(m, offset,
				    sizeof(struct sctp_ipv6addr_param),
				    (uint8_t *)&addr6_store);
				if (a6p == NULL) {
					return (0);
				}
				sin6 = (struct sockaddr_in6 *)sa;
				if (IN6_IS_SCOPE_LINKLOCAL(&sin6->sin6_addr)) {
					/* create a copy and clear scope */
					memcpy(&sin6_tmp, sin6,
					    sizeof(struct sockaddr_in6));
					sin6 = &sin6_tmp;
					in6_clearscope(&sin6->sin6_addr);
				}
				if (memcmp(&sin6->sin6_addr, a6p->addr,
				    sizeof(struct in6_addr)) == 0) {
					/* found it */
					return (1);
				}
			}
			break;
#endif				/* INET6 */
#ifdef INET
		case SCTP_IPV4_ADDRESS:
			if (sa->sa_family == AF_INET) {
				if (plen != sizeof(struct sctp_ipv4addr_param)) {
					break;
				}
				/* get the entire IPv4 address param */
				a4p = (struct sctp_ipv4addr_param *)
				    sctp_m_getptr(m, offset,
				    sizeof(struct sctp_ipv4addr_param),
				    (uint8_t *)&addr4_store);
				if (a4p == NULL) {
					return (0);
				}
				sin = (struct sockaddr_in *)sa;
				if (sin->sin_addr.s_addr == a4p->addr) {
					/* found it */
					return (1);
				}
			}
			break;
#endif
		default:
			break;
		}
		/* get next parameter */
		offset += SCTP_SIZE32(plen);
		if (offset + sizeof(struct sctp_paramhdr) > length) {
			return (0);
		}
		ph = (struct sctp_paramhdr *)
		    sctp_m_getptr(m, offset, sizeof(struct sctp_paramhdr),
		    (uint8_t *)&tmp_param);
	}			/* while */
	/* not found! */
	return (0);
}

/*
 * makes sure that the current endpoint local addr list is consistent with
 * the new association (eg. subset bound, asconf allowed) adds addresses as
 * necessary
 */
static void
sctp_check_address_list_ep(struct sctp_tcb *stcb, struct mbuf *m, int offset,
    int length, struct sockaddr *init_addr)
{
	struct sctp_laddr *laddr;

	/* go through the endpoint list */
	LIST_FOREACH(laddr, &stcb->sctp_ep->sctp_addr_list, sctp_nxt_addr) {
		/* be paranoid and validate the laddr */
		if (laddr->ifa == NULL) {
			SCTPDBG(SCTP_DEBUG_ASCONF1,
			    "check_addr_list_ep: laddr->ifa is NULL");
			continue;
		}
		if (laddr->ifa == NULL) {
			SCTPDBG(SCTP_DEBUG_ASCONF1, "check_addr_list_ep: laddr->ifa->ifa_addr is NULL");
			continue;
		}
		/* do i have it implicitly? */
		if (sctp_cmpaddr(&laddr->ifa->address.sa, init_addr)) {
			continue;
		}
		/* check to see if in the init-ack */
		if (!sctp_addr_in_initack(m, offset, length, &laddr->ifa->address.sa)) {
			/* try to add it */
			sctp_addr_mgmt_assoc(stcb->sctp_ep, stcb, laddr->ifa,
			    SCTP_ADD_IP_ADDRESS, SCTP_ADDR_NOT_LOCKED);
		}
	}
}

/*
 * makes sure that the current kernel address list is consistent with the new
 * association (with all addrs bound) adds addresses as necessary
 */
static void
sctp_check_address_list_all(struct sctp_tcb *stcb, struct mbuf *m, int offset,
    int length, struct sockaddr *init_addr,
    uint16_t local_scope, uint16_t site_scope,
    uint16_t ipv4_scope, uint16_t loopback_scope)
{
	struct sctp_vrf *vrf = NULL;
	struct sctp_ifn *sctp_ifn;
	struct sctp_ifa *sctp_ifa;
	uint32_t vrf_id;
#ifdef INET
	struct sockaddr_in *sin;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6;
#endif

	if (stcb) {
		vrf_id = stcb->asoc.vrf_id;
	} else {
		return;
	}
	SCTP_IPI_ADDR_RLOCK();
	vrf = sctp_find_vrf(vrf_id);
	if (vrf == NULL) {
		SCTP_IPI_ADDR_RUNLOCK();
		return;
	}
	/* go through all our known interfaces */
	LIST_FOREACH(sctp_ifn, &vrf->ifnlist, next_ifn) {
		if (loopback_scope == 0 && SCTP_IFN_IS_IFT_LOOP(sctp_ifn)) {
			/* skip loopback interface */
			continue;
		}
		/* go through each interface address */
		LIST_FOREACH(sctp_ifa, &sctp_ifn->ifalist, next_ifa) {
			/* do i have it implicitly? */
			if (sctp_cmpaddr(&sctp_ifa->address.sa, init_addr)) {
				continue;
			}
			switch (sctp_ifa->address.sa.sa_family) {
#ifdef INET
			case AF_INET:
				sin = &sctp_ifa->address.sin;
				if (prison_check_ip4(stcb->sctp_ep->ip_inp.inp.inp_cred,
				    &sin->sin_addr) != 0) {
					continue;
				}
				if ((ipv4_scope == 0) &&
				    (IN4_ISPRIVATE_ADDRESS(&sin->sin_addr))) {
					/* private address not in scope */
					continue;
				}
				break;
#endif
#ifdef INET6
			case AF_INET6:
				sin6 = &sctp_ifa->address.sin6;
				if (prison_check_ip6(stcb->sctp_ep->ip_inp.inp.inp_cred,
				    &sin6->sin6_addr) != 0) {
					continue;
				}
				if ((local_scope == 0) &&
				    (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))) {
					continue;
				}
				if ((site_scope == 0) &&
				    (IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr))) {
					continue;
				}
				break;
#endif
			default:
				break;
			}
			/* check to see if in the init-ack */
			if (!sctp_addr_in_initack(m, offset, length, &sctp_ifa->address.sa)) {
				/* try to add it */
				sctp_addr_mgmt_assoc(stcb->sctp_ep, stcb,
				    sctp_ifa, SCTP_ADD_IP_ADDRESS,
				    SCTP_ADDR_LOCKED);
			}
		}		/* end foreach ifa */
	}			/* end foreach ifn */
	SCTP_IPI_ADDR_RUNLOCK();
}

/*
 * validates an init-ack chunk (from a cookie-echo) with current addresses
 * adds addresses from the init-ack into our local address list, if needed
 * queues asconf adds/deletes addresses as needed and makes appropriate list
 * changes for source address selection m, offset: points to the start of the
 * address list in an init-ack chunk length: total length of the address
 * params only init_addr: address where my INIT-ACK was sent from
 */
void
sctp_check_address_list(struct sctp_tcb *stcb, struct mbuf *m, int offset,
    int length, struct sockaddr *init_addr,
    uint16_t local_scope, uint16_t site_scope,
    uint16_t ipv4_scope, uint16_t loopback_scope)
{
	/* process the local addresses in the initack */
	sctp_process_initack_addresses(stcb, m, offset, length);

	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		/* bound all case */
		sctp_check_address_list_all(stcb, m, offset, length, init_addr,
		    local_scope, site_scope, ipv4_scope, loopback_scope);
	} else {
		/* subset bound case */
		if (sctp_is_feature_on(stcb->sctp_ep,
		    SCTP_PCB_FLAGS_DO_ASCONF)) {
			/* asconf's allowed */
			sctp_check_address_list_ep(stcb, m, offset, length,
			    init_addr);
		}
		/* else, no asconfs allowed, so what we sent is what we get */
	}
}

/*
 * sctp_bindx() support
 */
uint32_t
sctp_addr_mgmt_ep_sa(struct sctp_inpcb *inp, struct sockaddr *sa,
    uint32_t type, uint32_t vrf_id, struct sctp_ifa *sctp_ifap)
{
	struct sctp_ifa *ifa;
	struct sctp_laddr *laddr, *nladdr;

	if (sa->sa_len == 0) {
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_ASCONF, EINVAL);
		return (EINVAL);
	}
	if (sctp_ifap) {
		ifa = sctp_ifap;
	} else if (type == SCTP_ADD_IP_ADDRESS) {
		/* For an add the address MUST be on the system */
		ifa = sctp_find_ifa_by_addr(sa, vrf_id, SCTP_ADDR_NOT_LOCKED);
	} else if (type == SCTP_DEL_IP_ADDRESS) {
		/* For a delete we need to find it in the inp */
		ifa = sctp_find_ifa_in_ep(inp, sa, SCTP_ADDR_NOT_LOCKED);
	} else {
		ifa = NULL;
	}
	if (ifa != NULL) {
		if (type == SCTP_ADD_IP_ADDRESS) {
			sctp_add_local_addr_ep(inp, ifa, type);
		} else if (type == SCTP_DEL_IP_ADDRESS) {
			if (inp->laddr_count < 2) {
				/* can't delete the last local address */
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_ASCONF, EINVAL);
				return (EINVAL);
			}
			LIST_FOREACH(laddr, &inp->sctp_addr_list,
			    sctp_nxt_addr) {
				if (ifa == laddr->ifa) {
					/* Mark in the delete */
					laddr->action = type;
				}
			}
		}
		if (LIST_EMPTY(&inp->sctp_asoc_list)) {
			/*
			 * There is no need to start the iterator if the inp
			 * has no associations.
			 */
			if (type == SCTP_DEL_IP_ADDRESS) {
				LIST_FOREACH_SAFE(laddr, &inp->sctp_addr_list, sctp_nxt_addr, nladdr) {
					if (laddr->ifa == ifa) {
						sctp_del_local_addr_ep(inp, ifa);
					}
				}
			}
		} else {
			struct sctp_asconf_iterator *asc;
			struct sctp_laddr *wi;
			int ret;

			SCTP_MALLOC(asc, struct sctp_asconf_iterator *,
			    sizeof(struct sctp_asconf_iterator),
			    SCTP_M_ASC_IT);
			if (asc == NULL) {
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_ASCONF, ENOMEM);
				return (ENOMEM);
			}
			wi = SCTP_ZONE_GET(SCTP_BASE_INFO(ipi_zone_laddr), struct sctp_laddr);
			if (wi == NULL) {
				SCTP_FREE(asc, SCTP_M_ASC_IT);
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_ASCONF, ENOMEM);
				return (ENOMEM);
			}
			LIST_INIT(&asc->list_of_work);
			asc->cnt = 1;
			SCTP_INCR_LADDR_COUNT();
			wi->ifa = ifa;
			wi->action = type;
			atomic_add_int(&ifa->refcount, 1);
			LIST_INSERT_HEAD(&asc->list_of_work, wi, sctp_nxt_addr);
			ret = sctp_initiate_iterator(sctp_asconf_iterator_ep,
			    sctp_asconf_iterator_stcb,
			    sctp_asconf_iterator_ep_end,
			    SCTP_PCB_ANY_FLAGS,
			    SCTP_PCB_ANY_FEATURES,
			    SCTP_ASOC_ANY_STATE,
			    (void *)asc, 0,
			    sctp_asconf_iterator_end, inp, 0);
			if (ret) {
				SCTP_PRINTF("Failed to initiate iterator for addr_mgmt_ep_sa\n");
				SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_ASCONF, EFAULT);
				sctp_asconf_iterator_end(asc, 0);
				return (EFAULT);
			}
		}
		return (0);
	} else {
		/* invalid address! */
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_ASCONF, EADDRNOTAVAIL);
		return (EADDRNOTAVAIL);
	}
}

void
sctp_asconf_send_nat_state_update(struct sctp_tcb *stcb,
    struct sctp_nets *net)
{
	struct sctp_asconf_addr *aa;
	struct sctp_ifa *sctp_ifap;
	struct sctp_asconf_tag_param *vtag;
#ifdef INET
	struct sockaddr_in *to;
#endif
#ifdef INET6
	struct sockaddr_in6 *to6;
#endif
	if (net == NULL) {
		SCTPDBG(SCTP_DEBUG_ASCONF1, "sctp_asconf_send_nat_state_update: Missing net\n");
		return;
	}
	if (stcb == NULL) {
		SCTPDBG(SCTP_DEBUG_ASCONF1, "sctp_asconf_send_nat_state_update: Missing stcb\n");
		return;
	}
	/*
	 * Need to have in the asconf: - vtagparam(my_vtag/peer_vtag) -
	 * add(0.0.0.0) - del(0.0.0.0) - Any global addresses add(addr)
	 */
	SCTP_MALLOC(aa, struct sctp_asconf_addr *, sizeof(*aa),
	    SCTP_M_ASC_ADDR);
	if (aa == NULL) {
		/* didn't get memory */
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "sctp_asconf_send_nat_state_update: failed to get memory!\n");
		return;
	}
	aa->special_del = 0;
	/* fill in asconf address parameter fields */
	/* top level elements are "networked" during send */
	aa->ifa = NULL;
	aa->sent = 0;		/* clear sent flag */
	vtag = (struct sctp_asconf_tag_param *)&aa->ap.aph;
	vtag->aph.ph.param_type = SCTP_NAT_VTAGS;
	vtag->aph.ph.param_length = sizeof(struct sctp_asconf_tag_param);
	vtag->local_vtag = htonl(stcb->asoc.my_vtag);
	vtag->remote_vtag = htonl(stcb->asoc.peer_vtag);
	TAILQ_INSERT_TAIL(&stcb->asoc.asconf_queue, aa, next);

	SCTP_MALLOC(aa, struct sctp_asconf_addr *, sizeof(*aa),
	    SCTP_M_ASC_ADDR);
	if (aa == NULL) {
		/* didn't get memory */
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "sctp_asconf_send_nat_state_update: failed to get memory!\n");
		return;
	}
	memset(aa, 0, sizeof(struct sctp_asconf_addr));
	/* fill in asconf address parameter fields */
	/* ADD(0.0.0.0) */
	switch (net->ro._l_addr.sa.sa_family) {
#ifdef INET
	case AF_INET:
		aa->ap.aph.ph.param_type = SCTP_ADD_IP_ADDRESS;
		aa->ap.aph.ph.param_length = sizeof(struct sctp_asconf_addrv4_param);
		aa->ap.addrp.ph.param_type = SCTP_IPV4_ADDRESS;
		aa->ap.addrp.ph.param_length = sizeof(struct sctp_ipv4addr_param);
		/* No need to add an address, we are using 0.0.0.0 */
		TAILQ_INSERT_TAIL(&stcb->asoc.asconf_queue, aa, next);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		aa->ap.aph.ph.param_type = SCTP_ADD_IP_ADDRESS;
		aa->ap.aph.ph.param_length = sizeof(struct sctp_asconf_addr_param);
		aa->ap.addrp.ph.param_type = SCTP_IPV6_ADDRESS;
		aa->ap.addrp.ph.param_length = sizeof(struct sctp_ipv6addr_param);
		/* No need to add an address, we are using 0.0.0.0 */
		TAILQ_INSERT_TAIL(&stcb->asoc.asconf_queue, aa, next);
		break;
#endif
	default:
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "sctp_asconf_send_nat_state_update: unknown address family\n");
		SCTP_FREE(aa, SCTP_M_ASC_ADDR);
		return;
	}
	SCTP_MALLOC(aa, struct sctp_asconf_addr *, sizeof(*aa),
	    SCTP_M_ASC_ADDR);
	if (aa == NULL) {
		/* didn't get memory */
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "sctp_asconf_send_nat_state_update: failed to get memory!\n");
		return;
	}
	memset(aa, 0, sizeof(struct sctp_asconf_addr));
	/* fill in asconf address parameter fields */
	/* ADD(0.0.0.0) */
	switch (net->ro._l_addr.sa.sa_family) {
#ifdef INET
	case AF_INET:
		aa->ap.aph.ph.param_type = SCTP_ADD_IP_ADDRESS;
		aa->ap.aph.ph.param_length = sizeof(struct sctp_asconf_addrv4_param);
		aa->ap.addrp.ph.param_type = SCTP_IPV4_ADDRESS;
		aa->ap.addrp.ph.param_length = sizeof(struct sctp_ipv4addr_param);
		/* No need to add an address, we are using 0.0.0.0 */
		TAILQ_INSERT_TAIL(&stcb->asoc.asconf_queue, aa, next);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		aa->ap.aph.ph.param_type = SCTP_DEL_IP_ADDRESS;
		aa->ap.aph.ph.param_length = sizeof(struct sctp_asconf_addr_param);
		aa->ap.addrp.ph.param_type = SCTP_IPV6_ADDRESS;
		aa->ap.addrp.ph.param_length = sizeof(struct sctp_ipv6addr_param);
		/* No need to add an address, we are using 0.0.0.0 */
		TAILQ_INSERT_TAIL(&stcb->asoc.asconf_queue, aa, next);
		break;
#endif
	default:
		SCTPDBG(SCTP_DEBUG_ASCONF1,
		    "sctp_asconf_send_nat_state_update: unknown address family\n");
		SCTP_FREE(aa, SCTP_M_ASC_ADDR);
		return;
	}
	/* Now we must hunt the addresses and add all global addresses */
	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		struct sctp_vrf *vrf = NULL;
		struct sctp_ifn *sctp_ifnp;
		uint32_t vrf_id;

		vrf_id = stcb->sctp_ep->def_vrf_id;
		vrf = sctp_find_vrf(vrf_id);
		if (vrf == NULL) {
			goto skip_rest;
		}

		SCTP_IPI_ADDR_RLOCK();
		LIST_FOREACH(sctp_ifnp, &vrf->ifnlist, next_ifn) {
			LIST_FOREACH(sctp_ifap, &sctp_ifnp->ifalist, next_ifa) {
				switch (sctp_ifap->address.sa.sa_family) {
#ifdef INET
				case AF_INET:
					to = &sctp_ifap->address.sin;
					if (prison_check_ip4(stcb->sctp_ep->ip_inp.inp.inp_cred,
					    &to->sin_addr) != 0) {
						continue;
					}
					if (IN4_ISPRIVATE_ADDRESS(&to->sin_addr)) {
						continue;
					}
					if (IN4_ISLOOPBACK_ADDRESS(&to->sin_addr)) {
						continue;
					}
					break;
#endif
#ifdef INET6
				case AF_INET6:
					to6 = &sctp_ifap->address.sin6;
					if (prison_check_ip6(stcb->sctp_ep->ip_inp.inp.inp_cred,
					    &to6->sin6_addr) != 0) {
						continue;
					}
					if (IN6_IS_ADDR_LOOPBACK(&to6->sin6_addr)) {
						continue;
					}
					if (IN6_IS_ADDR_LINKLOCAL(&to6->sin6_addr)) {
						continue;
					}
					break;
#endif
				default:
					continue;
				}
				sctp_asconf_queue_mgmt(stcb, sctp_ifap, SCTP_ADD_IP_ADDRESS);
			}
		}
		SCTP_IPI_ADDR_RUNLOCK();
	} else {
		struct sctp_laddr *laddr;

		LIST_FOREACH(laddr, &stcb->sctp_ep->sctp_addr_list, sctp_nxt_addr) {
			if (laddr->ifa == NULL) {
				continue;
			}
			if (laddr->ifa->localifa_flags & SCTP_BEING_DELETED)
				/*
				 * Address being deleted by the system, dont
				 * list.
				 */
				continue;
			if (laddr->action == SCTP_DEL_IP_ADDRESS) {
				/*
				 * Address being deleted on this ep don't
				 * list.
				 */
				continue;
			}
			sctp_ifap = laddr->ifa;
			switch (sctp_ifap->address.sa.sa_family) {
#ifdef INET
			case AF_INET:
				to = &sctp_ifap->address.sin;
				if (IN4_ISPRIVATE_ADDRESS(&to->sin_addr)) {
					continue;
				}
				if (IN4_ISLOOPBACK_ADDRESS(&to->sin_addr)) {
					continue;
				}
				break;
#endif
#ifdef INET6
			case AF_INET6:
				to6 = &sctp_ifap->address.sin6;
				if (IN6_IS_ADDR_LOOPBACK(&to6->sin6_addr)) {
					continue;
				}
				if (IN6_IS_ADDR_LINKLOCAL(&to6->sin6_addr)) {
					continue;
				}
				break;
#endif
			default:
				continue;
			}
			sctp_asconf_queue_mgmt(stcb, sctp_ifap, SCTP_ADD_IP_ADDRESS);
		}
	}
skip_rest:
	/* Now we must send the asconf into the queue */
	sctp_send_asconf(stcb, net, SCTP_ADDR_NOT_LOCKED);
}
