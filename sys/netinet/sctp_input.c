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

#include <netinet/sctp_os.h>
#include <netinet/sctp_var.h>
#include <netinet/sctp_sysctl.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctp_header.h>
#include <netinet/sctputil.h>
#include <netinet/sctp_output.h>
#include <netinet/sctp_input.h>
#include <netinet/sctp_auth.h>
#include <netinet/sctp_indata.h>
#include <netinet/sctp_asconf.h>
#include <netinet/sctp_bsd_addr.h>
#include <netinet/sctp_timer.h>
#include <netinet/sctp_crc32.h>
#if defined(INET) || defined(INET6)
#include <netinet/udp.h>
#endif
#include <netinet/in_kdtrace.h>
#include <sys/smp.h>



static void
sctp_stop_all_cookie_timers(struct sctp_tcb *stcb)
{
	struct sctp_nets *net;

	/*
	 * This now not only stops all cookie timers it also stops any INIT
	 * timers as well. This will make sure that the timers are stopped
	 * in all collision cases.
	 */
	SCTP_TCB_LOCK_ASSERT(stcb);
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		if (net->rxt_timer.type == SCTP_TIMER_TYPE_COOKIE) {
			sctp_timer_stop(SCTP_TIMER_TYPE_COOKIE,
			    stcb->sctp_ep,
			    stcb,
			    net, SCTP_FROM_SCTP_INPUT + SCTP_LOC_1);
		} else if (net->rxt_timer.type == SCTP_TIMER_TYPE_INIT) {
			sctp_timer_stop(SCTP_TIMER_TYPE_INIT,
			    stcb->sctp_ep,
			    stcb,
			    net, SCTP_FROM_SCTP_INPUT + SCTP_LOC_2);
		}
	}
}

/* INIT handler */
static void
sctp_handle_init(struct mbuf *m, int iphlen, int offset,
    struct sockaddr *src, struct sockaddr *dst, struct sctphdr *sh,
    struct sctp_init_chunk *cp, struct sctp_inpcb *inp,
    struct sctp_tcb *stcb, struct sctp_nets *net, int *abort_no_unlock,
    uint8_t mflowtype, uint32_t mflowid,
    uint32_t vrf_id, uint16_t port)
{
	struct sctp_init *init;
	struct mbuf *op_err;

	SCTPDBG(SCTP_DEBUG_INPUT2, "sctp_handle_init: handling INIT tcb:%p\n",
	    (void *)stcb);
	if (stcb == NULL) {
		SCTP_INP_RLOCK(inp);
	}
	/* validate length */
	if (ntohs(cp->ch.chunk_length) < sizeof(struct sctp_init_chunk)) {
		op_err = sctp_generate_cause(SCTP_CAUSE_INVALID_PARAM, "");
		sctp_abort_association(inp, stcb, m, iphlen, src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, port);
		if (stcb)
			*abort_no_unlock = 1;
		goto outnow;
	}
	/* validate parameters */
	init = &cp->init;
	if (init->initiate_tag == 0) {
		/* protocol error... send abort */
		op_err = sctp_generate_cause(SCTP_CAUSE_INVALID_PARAM, "");
		sctp_abort_association(inp, stcb, m, iphlen, src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, port);
		if (stcb)
			*abort_no_unlock = 1;
		goto outnow;
	}
	if (ntohl(init->a_rwnd) < SCTP_MIN_RWND) {
		/* invalid parameter... send abort */
		op_err = sctp_generate_cause(SCTP_CAUSE_INVALID_PARAM, "");
		sctp_abort_association(inp, stcb, m, iphlen, src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, port);
		if (stcb)
			*abort_no_unlock = 1;
		goto outnow;
	}
	if (init->num_inbound_streams == 0) {
		/* protocol error... send abort */
		op_err = sctp_generate_cause(SCTP_CAUSE_INVALID_PARAM, "");
		sctp_abort_association(inp, stcb, m, iphlen, src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, port);
		if (stcb)
			*abort_no_unlock = 1;
		goto outnow;
	}
	if (init->num_outbound_streams == 0) {
		/* protocol error... send abort */
		op_err = sctp_generate_cause(SCTP_CAUSE_INVALID_PARAM, "");
		sctp_abort_association(inp, stcb, m, iphlen, src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, port);
		if (stcb)
			*abort_no_unlock = 1;
		goto outnow;
	}
	if (sctp_validate_init_auth_params(m, offset + sizeof(*cp),
	    offset + ntohs(cp->ch.chunk_length))) {
		/* auth parameter(s) error... send abort */
		op_err = sctp_generate_cause(SCTP_BASE_SYSCTL(sctp_diag_info_code),
		    "Problem with AUTH parameters");
		sctp_abort_association(inp, stcb, m, iphlen, src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, port);
		if (stcb)
			*abort_no_unlock = 1;
		goto outnow;
	}
	/* We are only accepting if we have a listening socket. */
	if ((stcb == NULL) &&
	    ((inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
	    (!SCTP_IS_LISTENING(inp)))) {
		/*
		 * FIX ME ?? What about TCP model and we have a
		 * match/restart case? Actually no fix is needed. the lookup
		 * will always find the existing assoc so stcb would not be
		 * NULL. It may be questionable to do this since we COULD
		 * just send back the INIT-ACK and hope that the app did
		 * accept()'s by the time the COOKIE was sent. But there is
		 * a price to pay for COOKIE generation and I don't want to
		 * pay it on the chance that the app will actually do some
		 * accepts(). The App just looses and should NOT be in this
		 * state :-)
		 */
		if (SCTP_BASE_SYSCTL(sctp_blackhole) == 0) {
			op_err = sctp_generate_cause(SCTP_BASE_SYSCTL(sctp_diag_info_code),
			    "No listener");
			sctp_send_abort(m, iphlen, src, dst, sh, 0, op_err,
			    mflowtype, mflowid, inp->fibnum,
			    vrf_id, port);
		}
		goto outnow;
	}
	if ((stcb != NULL) &&
	    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_ACK_SENT)) {
		SCTPDBG(SCTP_DEBUG_INPUT3, "sctp_handle_init: sending SHUTDOWN-ACK\n");
		sctp_send_shutdown_ack(stcb, NULL);
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_CONTROL_PROC, SCTP_SO_NOT_LOCKED);
	} else {
		SCTPDBG(SCTP_DEBUG_INPUT3, "sctp_handle_init: sending INIT-ACK\n");
		sctp_send_initiate_ack(inp, stcb, net, m, iphlen, offset,
		    src, dst, sh, cp,
		    mflowtype, mflowid,
		    vrf_id, port);
	}
outnow:
	if (stcb == NULL) {
		SCTP_INP_RUNLOCK(inp);
	}
}

/*
 * process peer "INIT/INIT-ACK" chunk returns value < 0 on error
 */

int
sctp_is_there_unsent_data(struct sctp_tcb *stcb, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
)
{
	int unsent_data;
	unsigned int i;
	struct sctp_stream_queue_pending *sp;
	struct sctp_association *asoc;

	/*
	 * This function returns if any stream has true unsent data on it.
	 * Note that as it looks through it will clean up any places that
	 * have old data that has been sent but left at top of stream queue.
	 */
	asoc = &stcb->asoc;
	unsent_data = 0;
	SCTP_TCB_SEND_LOCK(stcb);
	if (!stcb->asoc.ss_functions.sctp_ss_is_empty(stcb, asoc)) {
		/* Check to see if some data queued */
		for (i = 0; i < stcb->asoc.streamoutcnt; i++) {
			/* sa_ignore FREED_MEMORY */
			sp = TAILQ_FIRST(&stcb->asoc.strmout[i].outqueue);
			if (sp == NULL) {
				continue;
			}
			if ((sp->msg_is_complete) &&
			    (sp->length == 0) &&
			    (sp->sender_all_done)) {
				/*
				 * We are doing differed cleanup. Last time
				 * through when we took all the data the
				 * sender_all_done was not set.
				 */
				if (sp->put_last_out == 0) {
					SCTP_PRINTF("Gak, put out entire msg with NO end!-1\n");
					SCTP_PRINTF("sender_done:%d len:%d msg_comp:%d put_last_out:%d\n",
					    sp->sender_all_done,
					    sp->length,
					    sp->msg_is_complete,
					    sp->put_last_out);
				}
				atomic_subtract_int(&stcb->asoc.stream_queue_cnt, 1);
				TAILQ_REMOVE(&stcb->asoc.strmout[i].outqueue, sp, next);
				stcb->asoc.ss_functions.sctp_ss_remove_from_stream(stcb, asoc, &asoc->strmout[i], sp, 1);
				if (sp->net) {
					sctp_free_remote_addr(sp->net);
					sp->net = NULL;
				}
				if (sp->data) {
					sctp_m_freem(sp->data);
					sp->data = NULL;
				}
				sctp_free_a_strmoq(stcb, sp, so_locked);
				if (!TAILQ_EMPTY(&stcb->asoc.strmout[i].outqueue)) {
					unsent_data++;
				}
			} else {
				unsent_data++;
			}
			if (unsent_data > 0) {
				break;
			}
		}
	}
	SCTP_TCB_SEND_UNLOCK(stcb);
	return (unsent_data);
}

static int
sctp_process_init(struct sctp_init_chunk *cp, struct sctp_tcb *stcb)
{
	struct sctp_init *init;
	struct sctp_association *asoc;
	struct sctp_nets *lnet;
	unsigned int i;

	init = &cp->init;
	asoc = &stcb->asoc;
	/* save off parameters */
	asoc->peer_vtag = ntohl(init->initiate_tag);
	asoc->peers_rwnd = ntohl(init->a_rwnd);
	/* init tsn's */
	asoc->highest_tsn_inside_map = asoc->asconf_seq_in = ntohl(init->initial_tsn) - 1;

	if (!TAILQ_EMPTY(&asoc->nets)) {
		/* update any ssthresh's that may have a default */
		TAILQ_FOREACH(lnet, &asoc->nets, sctp_next) {
			lnet->ssthresh = asoc->peers_rwnd;
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & (SCTP_CWND_MONITOR_ENABLE | SCTP_CWND_LOGGING_ENABLE)) {
				sctp_log_cwnd(stcb, lnet, 0, SCTP_CWND_INITIALIZATION);
			}

		}
	}
	SCTP_TCB_SEND_LOCK(stcb);
	if (asoc->pre_open_streams > ntohs(init->num_inbound_streams)) {
		unsigned int newcnt;
		struct sctp_stream_out *outs;
		struct sctp_stream_queue_pending *sp, *nsp;
		struct sctp_tmit_chunk *chk, *nchk;

		/* abandon the upper streams */
		newcnt = ntohs(init->num_inbound_streams);
		TAILQ_FOREACH_SAFE(chk, &asoc->send_queue, sctp_next, nchk) {
			if (chk->rec.data.sid >= newcnt) {
				TAILQ_REMOVE(&asoc->send_queue, chk, sctp_next);
				asoc->send_queue_cnt--;
				if (asoc->strmout[chk->rec.data.sid].chunks_on_queues > 0) {
					asoc->strmout[chk->rec.data.sid].chunks_on_queues--;
#ifdef INVARIANTS
				} else {
					panic("No chunks on the queues for sid %u.", chk->rec.data.sid);
#endif
				}
				if (chk->data != NULL) {
					sctp_free_bufspace(stcb, asoc, chk, 1);
					sctp_ulp_notify(SCTP_NOTIFY_UNSENT_DG_FAIL, stcb,
					    0, chk, SCTP_SO_NOT_LOCKED);
					if (chk->data) {
						sctp_m_freem(chk->data);
						chk->data = NULL;
					}
				}
				sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
				/* sa_ignore FREED_MEMORY */
			}
		}
		if (asoc->strmout) {
			for (i = newcnt; i < asoc->pre_open_streams; i++) {
				outs = &asoc->strmout[i];
				TAILQ_FOREACH_SAFE(sp, &outs->outqueue, next, nsp) {
					atomic_subtract_int(&stcb->asoc.stream_queue_cnt, 1);
					TAILQ_REMOVE(&outs->outqueue, sp, next);
					stcb->asoc.ss_functions.sctp_ss_remove_from_stream(stcb, asoc, outs, sp, 1);
					sctp_ulp_notify(SCTP_NOTIFY_SPECIAL_SP_FAIL,
					    stcb, 0, sp, SCTP_SO_NOT_LOCKED);
					if (sp->data) {
						sctp_m_freem(sp->data);
						sp->data = NULL;
					}
					if (sp->net) {
						sctp_free_remote_addr(sp->net);
						sp->net = NULL;
					}
					/* Free the chunk */
					sctp_free_a_strmoq(stcb, sp, SCTP_SO_NOT_LOCKED);
					/* sa_ignore FREED_MEMORY */
				}
				outs->state = SCTP_STREAM_CLOSED;
			}
		}
		/* cut back the count */
		asoc->pre_open_streams = newcnt;
	}
	SCTP_TCB_SEND_UNLOCK(stcb);
	asoc->streamoutcnt = asoc->pre_open_streams;
	if (asoc->strmout) {
		for (i = 0; i < asoc->streamoutcnt; i++) {
			asoc->strmout[i].state = SCTP_STREAM_OPEN;
		}
	}
	/* EY - nr_sack: initialize highest tsn in nr_mapping_array */
	asoc->highest_tsn_inside_nr_map = asoc->highest_tsn_inside_map;
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MAP_LOGGING_ENABLE) {
		sctp_log_map(0, 5, asoc->highest_tsn_inside_map, SCTP_MAP_SLIDE_RESULT);
	}
	/* This is the next one we expect */
	asoc->str_reset_seq_in = asoc->asconf_seq_in + 1;

	asoc->mapping_array_base_tsn = ntohl(init->initial_tsn);
	asoc->tsn_last_delivered = asoc->cumulative_tsn = asoc->asconf_seq_in;

	asoc->advanced_peer_ack_point = asoc->last_acked_seq;
	/* open the requested streams */

	if (asoc->strmin != NULL) {
		/* Free the old ones */
		for (i = 0; i < asoc->streamincnt; i++) {
			sctp_clean_up_stream(stcb, &asoc->strmin[i].inqueue);
			sctp_clean_up_stream(stcb, &asoc->strmin[i].uno_inqueue);
		}
		SCTP_FREE(asoc->strmin, SCTP_M_STRMI);
	}
	if (asoc->max_inbound_streams > ntohs(init->num_outbound_streams)) {
		asoc->streamincnt = ntohs(init->num_outbound_streams);
	} else {
		asoc->streamincnt = asoc->max_inbound_streams;
	}
	SCTP_MALLOC(asoc->strmin, struct sctp_stream_in *, asoc->streamincnt *
	    sizeof(struct sctp_stream_in), SCTP_M_STRMI);
	if (asoc->strmin == NULL) {
		/* we didn't get memory for the streams! */
		SCTPDBG(SCTP_DEBUG_INPUT2, "process_init: couldn't get memory for the streams!\n");
		return (-1);
	}
	for (i = 0; i < asoc->streamincnt; i++) {
		asoc->strmin[i].sid = i;
		asoc->strmin[i].last_mid_delivered = 0xffffffff;
		TAILQ_INIT(&asoc->strmin[i].inqueue);
		TAILQ_INIT(&asoc->strmin[i].uno_inqueue);
		asoc->strmin[i].pd_api_started = 0;
		asoc->strmin[i].delivery_started = 0;
	}
	/*
	 * load_address_from_init will put the addresses into the
	 * association when the COOKIE is processed or the INIT-ACK is
	 * processed. Both types of COOKIE's existing and new call this
	 * routine. It will remove addresses that are no longer in the
	 * association (for the restarting case where addresses are
	 * removed). Up front when the INIT arrives we will discard it if it
	 * is a restart and new addresses have been added.
	 */
	/* sa_ignore MEMLEAK */
	return (0);
}

/*
 * INIT-ACK message processing/consumption returns value < 0 on error
 */
static int
sctp_process_init_ack(struct mbuf *m, int iphlen, int offset,
    struct sockaddr *src, struct sockaddr *dst, struct sctphdr *sh,
    struct sctp_init_ack_chunk *cp, struct sctp_tcb *stcb,
    struct sctp_nets *net, int *abort_no_unlock,
    uint8_t mflowtype, uint32_t mflowid,
    uint32_t vrf_id)
{
	struct sctp_association *asoc;
	struct mbuf *op_err;
	int retval, abort_flag;
	uint32_t initack_limit;
	int nat_friendly = 0;

	/* First verify that we have no illegal param's */
	abort_flag = 0;

	op_err = sctp_arethere_unrecognized_parameters(m,
	    (offset + sizeof(struct sctp_init_chunk)),
	    &abort_flag, (struct sctp_chunkhdr *)cp, &nat_friendly);
	if (abort_flag) {
		/* Send an abort and notify peer */
		sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
		*abort_no_unlock = 1;
		return (-1);
	}
	asoc = &stcb->asoc;
	asoc->peer_supports_nat = (uint8_t)nat_friendly;
	/* process the peer's parameters in the INIT-ACK */
	retval = sctp_process_init((struct sctp_init_chunk *)cp, stcb);
	if (retval < 0) {
		return (retval);
	}
	initack_limit = offset + ntohs(cp->ch.chunk_length);
	/* load all addresses */
	if ((retval = sctp_load_addresses_from_init(stcb, m,
	    (offset + sizeof(struct sctp_init_chunk)), initack_limit,
	    src, dst, NULL, stcb->asoc.port))) {
		op_err = sctp_generate_cause(SCTP_BASE_SYSCTL(sctp_diag_info_code),
		    "Problem with address parameters");
		SCTPDBG(SCTP_DEBUG_INPUT1,
		    "Load addresses from INIT causes an abort %d\n",
		    retval);
		sctp_abort_association(stcb->sctp_ep, stcb, m, iphlen,
		    src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, net->port);
		*abort_no_unlock = 1;
		return (-1);
	}
	/* if the peer doesn't support asconf, flush the asconf queue */
	if (asoc->asconf_supported == 0) {
		struct sctp_asconf_addr *param, *nparam;

		TAILQ_FOREACH_SAFE(param, &asoc->asconf_queue, next, nparam) {
			TAILQ_REMOVE(&asoc->asconf_queue, param, next);
			SCTP_FREE(param, SCTP_M_ASC_ADDR);
		}
	}

	stcb->asoc.peer_hmac_id = sctp_negotiate_hmacid(stcb->asoc.peer_hmacs,
	    stcb->asoc.local_hmacs);
	if (op_err) {
		sctp_queue_op_err(stcb, op_err);
		/* queuing will steal away the mbuf chain to the out queue */
		op_err = NULL;
	}
	/* extract the cookie and queue it to "echo" it back... */
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_THRESHOLD_LOGGING) {
		sctp_misc_ints(SCTP_THRESHOLD_CLEAR,
		    stcb->asoc.overall_error_count,
		    0,
		    SCTP_FROM_SCTP_INPUT,
		    __LINE__);
	}
	stcb->asoc.overall_error_count = 0;
	net->error_count = 0;

	/*
	 * Cancel the INIT timer, We do this first before queueing the
	 * cookie. We always cancel at the primary to assue that we are
	 * canceling the timer started by the INIT which always goes to the
	 * primary.
	 */
	sctp_timer_stop(SCTP_TIMER_TYPE_INIT, stcb->sctp_ep, stcb,
	    asoc->primary_destination, SCTP_FROM_SCTP_INPUT + SCTP_LOC_3);

	/* calculate the RTO */
	net->RTO = sctp_calculate_rto(stcb, asoc, net, &asoc->time_entered,
	    SCTP_RTT_FROM_NON_DATA);
	retval = sctp_send_cookie_echo(m, offset, stcb, net);
	if (retval < 0) {
		/*
		 * No cookie, we probably should send a op error. But in any
		 * case if there is no cookie in the INIT-ACK, we can
		 * abandon the peer, its broke.
		 */
		if (retval == -3) {
			uint16_t len;

			len = (uint16_t)(sizeof(struct sctp_error_missing_param) + sizeof(uint16_t));
			/* We abort with an error of missing mandatory param */
			op_err = sctp_get_mbuf_for_msg(len, 0, M_NOWAIT, 1, MT_DATA);
			if (op_err != NULL) {
				struct sctp_error_missing_param *cause;

				SCTP_BUF_LEN(op_err) = len;
				cause = mtod(op_err, struct sctp_error_missing_param *);
				/* Subtract the reserved param */
				cause->cause.code = htons(SCTP_CAUSE_MISSING_PARAM);
				cause->cause.length = htons(len);
				cause->num_missing_params = htonl(1);
				cause->type[0] = htons(SCTP_STATE_COOKIE);
			}
			sctp_abort_association(stcb->sctp_ep, stcb, m, iphlen,
			    src, dst, sh, op_err,
			    mflowtype, mflowid,
			    vrf_id, net->port);
			*abort_no_unlock = 1;
		}
		return (retval);
	}

	return (0);
}

static void
sctp_handle_heartbeat_ack(struct sctp_heartbeat_chunk *cp,
    struct sctp_tcb *stcb, struct sctp_nets *net)
{
	union sctp_sockstore store;
	struct sctp_nets *r_net, *f_net;
	struct timeval tv;
	int req_prim = 0;
	uint16_t old_error_counter;

	if (ntohs(cp->ch.chunk_length) != sizeof(struct sctp_heartbeat_chunk)) {
		/* Invalid length */
		return;
	}

	memset(&store, 0, sizeof(store));
	switch (cp->heartbeat.hb_info.addr_family) {
#ifdef INET
	case AF_INET:
		if (cp->heartbeat.hb_info.addr_len == sizeof(struct sockaddr_in)) {
			store.sin.sin_family = cp->heartbeat.hb_info.addr_family;
			store.sin.sin_len = cp->heartbeat.hb_info.addr_len;
			store.sin.sin_port = stcb->rport;
			memcpy(&store.sin.sin_addr, cp->heartbeat.hb_info.address,
			    sizeof(store.sin.sin_addr));
		} else {
			return;
		}
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if (cp->heartbeat.hb_info.addr_len == sizeof(struct sockaddr_in6)) {
			store.sin6.sin6_family = cp->heartbeat.hb_info.addr_family;
			store.sin6.sin6_len = cp->heartbeat.hb_info.addr_len;
			store.sin6.sin6_port = stcb->rport;
			memcpy(&store.sin6.sin6_addr, cp->heartbeat.hb_info.address, sizeof(struct in6_addr));
		} else {
			return;
		}
		break;
#endif
	default:
		return;
	}
	r_net = sctp_findnet(stcb, &store.sa);
	if (r_net == NULL) {
		SCTPDBG(SCTP_DEBUG_INPUT1, "Huh? I can't find the address I sent it to, discard\n");
		return;
	}
	if ((r_net && (r_net->dest_state & SCTP_ADDR_UNCONFIRMED)) &&
	    (r_net->heartbeat_random1 == cp->heartbeat.hb_info.random_value1) &&
	    (r_net->heartbeat_random2 == cp->heartbeat.hb_info.random_value2)) {
		/*
		 * If the its a HB and it's random value is correct when can
		 * confirm the destination.
		 */
		r_net->dest_state &= ~SCTP_ADDR_UNCONFIRMED;
		if (r_net->dest_state & SCTP_ADDR_REQ_PRIMARY) {
			stcb->asoc.primary_destination = r_net;
			r_net->dest_state &= ~SCTP_ADDR_REQ_PRIMARY;
			f_net = TAILQ_FIRST(&stcb->asoc.nets);
			if (f_net != r_net) {
				/*
				 * first one on the list is NOT the primary
				 * sctp_cmpaddr() is much more efficient if
				 * the primary is the first on the list,
				 * make it so.
				 */
				TAILQ_REMOVE(&stcb->asoc.nets, r_net, sctp_next);
				TAILQ_INSERT_HEAD(&stcb->asoc.nets, r_net, sctp_next);
			}
			req_prim = 1;
		}
		sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_CONFIRMED,
		    stcb, 0, (void *)r_net, SCTP_SO_NOT_LOCKED);
		sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb,
		    r_net, SCTP_FROM_SCTP_INPUT + SCTP_LOC_4);
		sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, r_net);
	}
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_THRESHOLD_LOGGING) {
		sctp_misc_ints(SCTP_THRESHOLD_CLEAR,
		    stcb->asoc.overall_error_count,
		    0,
		    SCTP_FROM_SCTP_INPUT,
		    __LINE__);
	}
	stcb->asoc.overall_error_count = 0;
	old_error_counter = r_net->error_count;
	r_net->error_count = 0;
	r_net->hb_responded = 1;
	tv.tv_sec = cp->heartbeat.hb_info.time_value_1;
	tv.tv_usec = cp->heartbeat.hb_info.time_value_2;
	/* Now lets do a RTO with this */
	r_net->RTO = sctp_calculate_rto(stcb, &stcb->asoc, r_net, &tv,
	    SCTP_RTT_FROM_NON_DATA);
	if (!(r_net->dest_state & SCTP_ADDR_REACHABLE)) {
		r_net->dest_state |= SCTP_ADDR_REACHABLE;
		sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_UP, stcb,
		    0, (void *)r_net, SCTP_SO_NOT_LOCKED);
	}
	if (r_net->dest_state & SCTP_ADDR_PF) {
		r_net->dest_state &= ~SCTP_ADDR_PF;
		stcb->asoc.cc_functions.sctp_cwnd_update_exit_pf(stcb, net);
	}
	if (old_error_counter > 0) {
		sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep,
		    stcb, r_net, SCTP_FROM_SCTP_INPUT + SCTP_LOC_5);
		sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, r_net);
	}
	if (r_net == stcb->asoc.primary_destination) {
		if (stcb->asoc.alternate) {
			/* release the alternate, primary is good */
			sctp_free_remote_addr(stcb->asoc.alternate);
			stcb->asoc.alternate = NULL;
		}
	}
	/* Mobility adaptation */
	if (req_prim) {
		if ((sctp_is_mobility_feature_on(stcb->sctp_ep,
		    SCTP_MOBILITY_BASE) ||
		    sctp_is_mobility_feature_on(stcb->sctp_ep,
		    SCTP_MOBILITY_FASTHANDOFF)) &&
		    sctp_is_mobility_feature_on(stcb->sctp_ep,
		    SCTP_MOBILITY_PRIM_DELETED)) {

			sctp_timer_stop(SCTP_TIMER_TYPE_PRIM_DELETED,
			    stcb->sctp_ep, stcb, NULL,
			    SCTP_FROM_SCTP_INPUT + SCTP_LOC_6);
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
	}
}

static int
sctp_handle_nat_colliding_state(struct sctp_tcb *stcb)
{
	/*
	 * return 0 means we want you to proceed with the abort non-zero
	 * means no abort processing
	 */
	struct sctpasochead *head;

	if ((SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_WAIT) ||
	    (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED)) {
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_INP_INFO_WLOCK();
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
	}
	if (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_WAIT) {
		/* generate a new vtag and send init */
		LIST_REMOVE(stcb, sctp_asocs);
		stcb->asoc.my_vtag = sctp_select_a_tag(stcb->sctp_ep, stcb->sctp_ep->sctp_lport, stcb->rport, 1);
		head = &SCTP_BASE_INFO(sctp_asochash)[SCTP_PCBHASH_ASOC(stcb->asoc.my_vtag, SCTP_BASE_INFO(hashasocmark))];
		/*
		 * put it in the bucket in the vtag hash of assoc's for the
		 * system
		 */
		LIST_INSERT_HEAD(head, stcb, sctp_asocs);
		sctp_send_initiate(stcb->sctp_ep, stcb, SCTP_SO_NOT_LOCKED);
		SCTP_INP_INFO_WUNLOCK();
		return (1);
	}
	if (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED) {
		/*
		 * treat like a case where the cookie expired i.e.: - dump
		 * current cookie. - generate a new vtag. - resend init.
		 */
		/* generate a new vtag and send init */
		LIST_REMOVE(stcb, sctp_asocs);
		SCTP_SET_STATE(stcb, SCTP_STATE_COOKIE_WAIT);
		sctp_stop_all_cookie_timers(stcb);
		sctp_toss_old_cookies(stcb, &stcb->asoc);
		stcb->asoc.my_vtag = sctp_select_a_tag(stcb->sctp_ep, stcb->sctp_ep->sctp_lport, stcb->rport, 1);
		head = &SCTP_BASE_INFO(sctp_asochash)[SCTP_PCBHASH_ASOC(stcb->asoc.my_vtag, SCTP_BASE_INFO(hashasocmark))];
		/*
		 * put it in the bucket in the vtag hash of assoc's for the
		 * system
		 */
		LIST_INSERT_HEAD(head, stcb, sctp_asocs);
		sctp_send_initiate(stcb->sctp_ep, stcb, SCTP_SO_NOT_LOCKED);
		SCTP_INP_INFO_WUNLOCK();
		return (1);
	}
	return (0);
}

static int
sctp_handle_nat_missing_state(struct sctp_tcb *stcb,
    struct sctp_nets *net)
{
	/*
	 * return 0 means we want you to proceed with the abort non-zero
	 * means no abort processing
	 */
	if (stcb->asoc.auth_supported == 0) {
		SCTPDBG(SCTP_DEBUG_INPUT2, "sctp_handle_nat_missing_state: Peer does not support AUTH, cannot send an asconf\n");
		return (0);
	}
	sctp_asconf_send_nat_state_update(stcb, net);
	return (1);
}


/* Returns 1 if the stcb was aborted, 0 otherwise */
static int
sctp_handle_abort(struct sctp_abort_chunk *abort,
    struct sctp_tcb *stcb, struct sctp_nets *net)
{
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;
#endif
	uint16_t len;
	uint16_t error;

	SCTPDBG(SCTP_DEBUG_INPUT2, "sctp_handle_abort: handling ABORT\n");
	if (stcb == NULL)
		return (0);

	len = ntohs(abort->ch.chunk_length);
	if (len >= sizeof(struct sctp_chunkhdr) + sizeof(struct sctp_error_cause)) {
		/*
		 * Need to check the cause codes for our two magic nat
		 * aborts which don't kill the assoc necessarily.
		 */
		struct sctp_error_cause *cause;

		cause = (struct sctp_error_cause *)(abort + 1);
		error = ntohs(cause->code);
		if (error == SCTP_CAUSE_NAT_COLLIDING_STATE) {
			SCTPDBG(SCTP_DEBUG_INPUT2, "Received Colliding state abort flags:%x\n",
			    abort->ch.chunk_flags);
			if (sctp_handle_nat_colliding_state(stcb)) {
				return (0);
			}
		} else if (error == SCTP_CAUSE_NAT_MISSING_STATE) {
			SCTPDBG(SCTP_DEBUG_INPUT2, "Received missing state abort flags:%x\n",
			    abort->ch.chunk_flags);
			if (sctp_handle_nat_missing_state(stcb, net)) {
				return (0);
			}
		}
	} else {
		error = 0;
	}
	/* stop any receive timers */
	sctp_timer_stop(SCTP_TIMER_TYPE_RECV, stcb->sctp_ep, stcb, net,
	    SCTP_FROM_SCTP_INPUT + SCTP_LOC_7);
	/* notify user of the abort and clean up... */
	sctp_abort_notification(stcb, 1, error, abort, SCTP_SO_NOT_LOCKED);
	/* free the tcb */
	SCTP_STAT_INCR_COUNTER32(sctps_aborted);
	if ((SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) ||
	    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
		SCTP_STAT_DECR_GAUGE32(sctps_currestab);
	}
#ifdef SCTP_ASOCLOG_OF_TSNS
	sctp_print_out_track_log(stcb);
#endif
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	so = SCTP_INP_SO(stcb->sctp_ep);
	atomic_add_int(&stcb->asoc.refcnt, 1);
	SCTP_TCB_UNLOCK(stcb);
	SCTP_SOCKET_LOCK(so, 1);
	SCTP_TCB_LOCK(stcb);
	atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
	SCTP_ADD_SUBSTATE(stcb, SCTP_STATE_WAS_ABORTED);
	(void)sctp_free_assoc(stcb->sctp_ep, stcb, SCTP_NORMAL_PROC,
	    SCTP_FROM_SCTP_INPUT + SCTP_LOC_8);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	SCTP_SOCKET_UNLOCK(so, 1);
#endif
	SCTPDBG(SCTP_DEBUG_INPUT2, "sctp_handle_abort: finished\n");
	return (1);
}

static void
sctp_start_net_timers(struct sctp_tcb *stcb)
{
	uint32_t cnt_hb_sent;
	struct sctp_nets *net;

	cnt_hb_sent = 0;
	TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
		/*
		 * For each network start: 1) A pmtu timer. 2) A HB timer 3)
		 * If the dest in unconfirmed send a hb as well if under
		 * max_hb_burst have been sent.
		 */
		sctp_timer_start(SCTP_TIMER_TYPE_PATHMTURAISE, stcb->sctp_ep, stcb, net);
		sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, net);
		if ((net->dest_state & SCTP_ADDR_UNCONFIRMED) &&
		    (cnt_hb_sent < SCTP_BASE_SYSCTL(sctp_hb_maxburst))) {
			sctp_send_hb(stcb, net, SCTP_SO_NOT_LOCKED);
			cnt_hb_sent++;
		}
	}
	if (cnt_hb_sent) {
		sctp_chunk_output(stcb->sctp_ep, stcb,
		    SCTP_OUTPUT_FROM_COOKIE_ACK,
		    SCTP_SO_NOT_LOCKED);
	}
}


static void
sctp_handle_shutdown(struct sctp_shutdown_chunk *cp,
    struct sctp_tcb *stcb, struct sctp_nets *net, int *abort_flag)
{
	struct sctp_association *asoc;
	int some_on_streamwheel;
	int old_state;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;
#endif

	SCTPDBG(SCTP_DEBUG_INPUT2,
	    "sctp_handle_shutdown: handling SHUTDOWN\n");
	if (stcb == NULL)
		return;
	asoc = &stcb->asoc;
	if ((SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_WAIT) ||
	    (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED)) {
		return;
	}
	if (ntohs(cp->ch.chunk_length) != sizeof(struct sctp_shutdown_chunk)) {
		/* Shutdown NOT the expected size */
		return;
	}
	old_state = SCTP_GET_STATE(stcb);
	sctp_update_acked(stcb, cp, abort_flag);
	if (*abort_flag) {
		return;
	}
	if (asoc->control_pdapi) {
		/*
		 * With a normal shutdown we assume the end of last record.
		 */
		SCTP_INP_READ_LOCK(stcb->sctp_ep);
		if (asoc->control_pdapi->on_strm_q) {
			struct sctp_stream_in *strm;

			strm = &asoc->strmin[asoc->control_pdapi->sinfo_stream];
			if (asoc->control_pdapi->on_strm_q == SCTP_ON_UNORDERED) {
				/* Unordered */
				TAILQ_REMOVE(&strm->uno_inqueue, asoc->control_pdapi, next_instrm);
				asoc->control_pdapi->on_strm_q = 0;
			} else if (asoc->control_pdapi->on_strm_q == SCTP_ON_ORDERED) {
				/* Ordered */
				TAILQ_REMOVE(&strm->inqueue, asoc->control_pdapi, next_instrm);
				asoc->control_pdapi->on_strm_q = 0;
#ifdef INVARIANTS
			} else {
				panic("Unknown state on ctrl:%p on_strm_q:%d",
				    asoc->control_pdapi,
				    asoc->control_pdapi->on_strm_q);
#endif
			}
		}
		asoc->control_pdapi->end_added = 1;
		asoc->control_pdapi->pdapi_aborted = 1;
		asoc->control_pdapi = NULL;
		SCTP_INP_READ_UNLOCK(stcb->sctp_ep);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		so = SCTP_INP_SO(stcb->sctp_ep);
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
		if (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET) {
			/* assoc was freed while we were unlocked */
			SCTP_SOCKET_UNLOCK(so, 1);
			return;
		}
#endif
		if (stcb->sctp_socket) {
			sctp_sorwakeup(stcb->sctp_ep, stcb->sctp_socket);
		}
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
	}
	/* goto SHUTDOWN_RECEIVED state to block new requests */
	if (stcb->sctp_socket) {
		if ((SCTP_GET_STATE(stcb) != SCTP_STATE_SHUTDOWN_RECEIVED) &&
		    (SCTP_GET_STATE(stcb) != SCTP_STATE_SHUTDOWN_ACK_SENT) &&
		    (SCTP_GET_STATE(stcb) != SCTP_STATE_SHUTDOWN_SENT)) {
			SCTP_SET_STATE(stcb, SCTP_STATE_SHUTDOWN_RECEIVED);
			/*
			 * notify upper layer that peer has initiated a
			 * shutdown
			 */
			sctp_ulp_notify(SCTP_NOTIFY_PEER_SHUTDOWN, stcb, 0, NULL, SCTP_SO_NOT_LOCKED);

			/* reset time */
			(void)SCTP_GETTIME_TIMEVAL(&asoc->time_entered);
		}
	}
	if (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_SENT) {
		/*
		 * stop the shutdown timer, since we WILL move to
		 * SHUTDOWN-ACK-SENT.
		 */
		sctp_timer_stop(SCTP_TIMER_TYPE_SHUTDOWN, stcb->sctp_ep, stcb,
		    net, SCTP_FROM_SCTP_INPUT + SCTP_LOC_9);
	}
	/* Now is there unsent data on a stream somewhere? */
	some_on_streamwheel = sctp_is_there_unsent_data(stcb, SCTP_SO_NOT_LOCKED);

	if (!TAILQ_EMPTY(&asoc->send_queue) ||
	    !TAILQ_EMPTY(&asoc->sent_queue) ||
	    some_on_streamwheel) {
		/* By returning we will push more data out */
		return;
	} else {
		/* no outstanding data to send, so move on... */
		/* send SHUTDOWN-ACK */
		/* move to SHUTDOWN-ACK-SENT state */
		if ((SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) ||
		    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
			SCTP_STAT_DECR_GAUGE32(sctps_currestab);
		}
		if (SCTP_GET_STATE(stcb) != SCTP_STATE_SHUTDOWN_ACK_SENT) {
			SCTP_SET_STATE(stcb, SCTP_STATE_SHUTDOWN_ACK_SENT);
			sctp_stop_timers_for_shutdown(stcb);
			sctp_send_shutdown_ack(stcb, net);
			sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNACK,
			    stcb->sctp_ep, stcb, net);
		} else if (old_state == SCTP_STATE_SHUTDOWN_ACK_SENT) {
			sctp_send_shutdown_ack(stcb, net);
		}
	}
}

static void
sctp_handle_shutdown_ack(struct sctp_shutdown_ack_chunk *cp SCTP_UNUSED,
    struct sctp_tcb *stcb,
    struct sctp_nets *net)
{
	struct sctp_association *asoc;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;

	so = SCTP_INP_SO(stcb->sctp_ep);
#endif
	SCTPDBG(SCTP_DEBUG_INPUT2,
	    "sctp_handle_shutdown_ack: handling SHUTDOWN ACK\n");
	if (stcb == NULL)
		return;

	asoc = &stcb->asoc;
	/* process according to association state */
	if ((SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_WAIT) ||
	    (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED)) {
		/* unexpected SHUTDOWN-ACK... do OOTB handling... */
		sctp_send_shutdown_complete(stcb, net, 1);
		SCTP_TCB_UNLOCK(stcb);
		return;
	}
	if ((SCTP_GET_STATE(stcb) != SCTP_STATE_SHUTDOWN_SENT) &&
	    (SCTP_GET_STATE(stcb) != SCTP_STATE_SHUTDOWN_ACK_SENT)) {
		/* unexpected SHUTDOWN-ACK... so ignore... */
		SCTP_TCB_UNLOCK(stcb);
		return;
	}
	if (asoc->control_pdapi) {
		/*
		 * With a normal shutdown we assume the end of last record.
		 */
		SCTP_INP_READ_LOCK(stcb->sctp_ep);
		asoc->control_pdapi->end_added = 1;
		asoc->control_pdapi->pdapi_aborted = 1;
		asoc->control_pdapi = NULL;
		SCTP_INP_READ_UNLOCK(stcb->sctp_ep);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
		if (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET) {
			/* assoc was freed while we were unlocked */
			SCTP_SOCKET_UNLOCK(so, 1);
			return;
		}
#endif
		sctp_sorwakeup(stcb->sctp_ep, stcb->sctp_socket);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
	}
#ifdef INVARIANTS
	if (!TAILQ_EMPTY(&asoc->send_queue) ||
	    !TAILQ_EMPTY(&asoc->sent_queue) ||
	    sctp_is_there_unsent_data(stcb, SCTP_SO_NOT_LOCKED)) {
		panic("Queues are not empty when handling SHUTDOWN-ACK");
	}
#endif
	/* stop the timer */
	sctp_timer_stop(SCTP_TIMER_TYPE_SHUTDOWN, stcb->sctp_ep, stcb, net,
	    SCTP_FROM_SCTP_INPUT + SCTP_LOC_10);
	/* send SHUTDOWN-COMPLETE */
	sctp_send_shutdown_complete(stcb, net, 0);
	/* notify upper layer protocol */
	if (stcb->sctp_socket) {
		if ((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
		    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) {
			stcb->sctp_socket->so_snd.sb_cc = 0;
		}
		sctp_ulp_notify(SCTP_NOTIFY_ASSOC_DOWN, stcb, 0, NULL, SCTP_SO_NOT_LOCKED);
	}
	SCTP_STAT_INCR_COUNTER32(sctps_shutdown);
	/* free the TCB but first save off the ep */
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	atomic_add_int(&stcb->asoc.refcnt, 1);
	SCTP_TCB_UNLOCK(stcb);
	SCTP_SOCKET_LOCK(so, 1);
	SCTP_TCB_LOCK(stcb);
	atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
	(void)sctp_free_assoc(stcb->sctp_ep, stcb, SCTP_NORMAL_PROC,
	    SCTP_FROM_SCTP_INPUT + SCTP_LOC_11);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	SCTP_SOCKET_UNLOCK(so, 1);
#endif
}

static void
sctp_process_unrecog_chunk(struct sctp_tcb *stcb, uint8_t chunk_type,
    struct sctp_nets *net)
{
	switch (chunk_type) {
	case SCTP_ASCONF_ACK:
	case SCTP_ASCONF:
		sctp_asconf_cleanup(stcb, net);
		break;
	case SCTP_IFORWARD_CUM_TSN:
	case SCTP_FORWARD_CUM_TSN:
		stcb->asoc.prsctp_supported = 0;
		break;
	default:
		SCTPDBG(SCTP_DEBUG_INPUT2,
		    "Peer does not support chunk type %d (0x%x).\n",
		    chunk_type, chunk_type);
		break;
	}
}

/*
 * Skip past the param header and then we will find the param that caused the
 * problem.  There are a number of param's in a ASCONF OR the prsctp param
 * these will turn of specific features.
 * XXX: Is this the right thing to do?
 */
static void
sctp_process_unrecog_param(struct sctp_tcb *stcb, uint16_t parameter_type)
{
	switch (parameter_type) {
		/* pr-sctp draft */
	case SCTP_PRSCTP_SUPPORTED:
		stcb->asoc.prsctp_supported = 0;
		break;
	case SCTP_SUPPORTED_CHUNK_EXT:
		break;
		/* draft-ietf-tsvwg-addip-sctp */
	case SCTP_HAS_NAT_SUPPORT:
		stcb->asoc.peer_supports_nat = 0;
		break;
	case SCTP_ADD_IP_ADDRESS:
	case SCTP_DEL_IP_ADDRESS:
	case SCTP_SET_PRIM_ADDR:
		stcb->asoc.asconf_supported = 0;
		break;
	case SCTP_SUCCESS_REPORT:
	case SCTP_ERROR_CAUSE_IND:
		SCTPDBG(SCTP_DEBUG_INPUT2, "Huh, the peer does not support success? or error cause?\n");
		SCTPDBG(SCTP_DEBUG_INPUT2,
		    "Turning off ASCONF to this strange peer\n");
		stcb->asoc.asconf_supported = 0;
		break;
	default:
		SCTPDBG(SCTP_DEBUG_INPUT2,
		    "Peer does not support param type %d (0x%x)??\n",
		    parameter_type, parameter_type);
		break;
	}
}

static int
sctp_handle_error(struct sctp_chunkhdr *ch,
    struct sctp_tcb *stcb, struct sctp_nets *net, uint32_t limit)
{
	struct sctp_error_cause *cause;
	struct sctp_association *asoc;
	uint32_t remaining_length, adjust;
	uint16_t code, cause_code, cause_length;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;
#endif

	/* parse through all of the errors and process */
	asoc = &stcb->asoc;
	cause = (struct sctp_error_cause *)((caddr_t)ch +
	    sizeof(struct sctp_chunkhdr));
	remaining_length = ntohs(ch->chunk_length);
	if (remaining_length > limit) {
		remaining_length = limit;
	}
	if (remaining_length >= sizeof(struct sctp_chunkhdr)) {
		remaining_length -= sizeof(struct sctp_chunkhdr);
	} else {
		remaining_length = 0;
	}
	code = 0;
	while (remaining_length >= sizeof(struct sctp_error_cause)) {
		/* Process an Error Cause */
		cause_code = ntohs(cause->code);
		cause_length = ntohs(cause->length);
		if ((cause_length > remaining_length) || (cause_length == 0)) {
			/* Invalid cause length, possibly due to truncation. */
			SCTPDBG(SCTP_DEBUG_INPUT1, "Bogus length in cause - bytes left: %u cause length: %u\n",
			    remaining_length, cause_length);
			return (0);
		}
		if (code == 0) {
			/* report the first error cause */
			code = cause_code;
		}
		switch (cause_code) {
		case SCTP_CAUSE_INVALID_STREAM:
		case SCTP_CAUSE_MISSING_PARAM:
		case SCTP_CAUSE_INVALID_PARAM:
		case SCTP_CAUSE_NO_USER_DATA:
			SCTPDBG(SCTP_DEBUG_INPUT1, "Software error we got a %u back? We have a bug :/ (or do they?)\n",
			    cause_code);
			break;
		case SCTP_CAUSE_NAT_COLLIDING_STATE:
			SCTPDBG(SCTP_DEBUG_INPUT2, "Received Colliding state abort flags: %x\n",
			    ch->chunk_flags);
			if (sctp_handle_nat_colliding_state(stcb)) {
				return (0);
			}
			break;
		case SCTP_CAUSE_NAT_MISSING_STATE:
			SCTPDBG(SCTP_DEBUG_INPUT2, "Received missing state abort flags: %x\n",
			    ch->chunk_flags);
			if (sctp_handle_nat_missing_state(stcb, net)) {
				return (0);
			}
			break;
		case SCTP_CAUSE_STALE_COOKIE:
			/*
			 * We only act if we have echoed a cookie and are
			 * waiting.
			 */
			if ((cause_length >= sizeof(struct sctp_error_stale_cookie)) &&
			    (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED)) {
				struct sctp_error_stale_cookie *stale_cookie;

				stale_cookie = (struct sctp_error_stale_cookie *)cause;
				asoc->cookie_preserve_req = ntohl(stale_cookie->stale_time);
				/* Double it to be more robust on RTX */
				if (asoc->cookie_preserve_req <= UINT32_MAX / 2) {
					asoc->cookie_preserve_req *= 2;
				} else {
					asoc->cookie_preserve_req = UINT32_MAX;
				}
				asoc->stale_cookie_count++;
				if (asoc->stale_cookie_count >
				    asoc->max_init_times) {
					sctp_abort_notification(stcb, 0, 0, NULL, SCTP_SO_NOT_LOCKED);
					/* now free the asoc */
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
					so = SCTP_INP_SO(stcb->sctp_ep);
					atomic_add_int(&stcb->asoc.refcnt, 1);
					SCTP_TCB_UNLOCK(stcb);
					SCTP_SOCKET_LOCK(so, 1);
					SCTP_TCB_LOCK(stcb);
					atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
					(void)sctp_free_assoc(stcb->sctp_ep, stcb, SCTP_NORMAL_PROC,
					    SCTP_FROM_SCTP_INPUT + SCTP_LOC_12);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
					SCTP_SOCKET_UNLOCK(so, 1);
#endif
					return (-1);
				}
				/* blast back to INIT state */
				sctp_toss_old_cookies(stcb, &stcb->asoc);
				SCTP_SET_STATE(stcb, SCTP_STATE_COOKIE_WAIT);
				sctp_stop_all_cookie_timers(stcb);
				sctp_send_initiate(stcb->sctp_ep, stcb, SCTP_SO_NOT_LOCKED);
			}
			break;
		case SCTP_CAUSE_UNRESOLVABLE_ADDR:
			/*
			 * Nothing we can do here, we don't do hostname
			 * addresses so if the peer does not like my IPv6
			 * (or IPv4 for that matter) it does not matter. If
			 * they don't support that type of address, they can
			 * NOT possibly get that packet type... i.e. with no
			 * IPv6 you can't receive a IPv6 packet. so we can
			 * safely ignore this one. If we ever added support
			 * for HOSTNAME Addresses, then we would need to do
			 * something here.
			 */
			break;
		case SCTP_CAUSE_UNRECOG_CHUNK:
			if (cause_length >= sizeof(struct sctp_error_unrecognized_chunk)) {
				struct sctp_error_unrecognized_chunk *unrec_chunk;

				unrec_chunk = (struct sctp_error_unrecognized_chunk *)cause;
				sctp_process_unrecog_chunk(stcb, unrec_chunk->ch.chunk_type, net);
			}
			break;
		case SCTP_CAUSE_UNRECOG_PARAM:
			/* XXX: We only consider the first parameter */
			if (cause_length >= sizeof(struct sctp_error_cause) + sizeof(struct sctp_paramhdr)) {
				struct sctp_paramhdr *unrec_parameter;

				unrec_parameter = (struct sctp_paramhdr *)(cause + 1);
				sctp_process_unrecog_param(stcb, ntohs(unrec_parameter->param_type));
			}
			break;
		case SCTP_CAUSE_COOKIE_IN_SHUTDOWN:
			/*
			 * We ignore this since the timer will drive out a
			 * new cookie anyway and there timer will drive us
			 * to send a SHUTDOWN_COMPLETE. We can't send one
			 * here since we don't have their tag.
			 */
			break;
		case SCTP_CAUSE_DELETING_LAST_ADDR:
		case SCTP_CAUSE_RESOURCE_SHORTAGE:
		case SCTP_CAUSE_DELETING_SRC_ADDR:
			/*
			 * We should NOT get these here, but in a
			 * ASCONF-ACK.
			 */
			SCTPDBG(SCTP_DEBUG_INPUT2, "Peer sends ASCONF errors in a error cause with code %u.\n",
			    cause_code);
			break;
		case SCTP_CAUSE_OUT_OF_RESC:
			/*
			 * And what, pray tell do we do with the fact that
			 * the peer is out of resources? Not really sure we
			 * could do anything but abort. I suspect this
			 * should have came WITH an abort instead of in a
			 * OP-ERROR.
			 */
			break;
		default:
			SCTPDBG(SCTP_DEBUG_INPUT1, "sctp_handle_error: unknown code 0x%x\n",
			    cause_code);
			break;
		}
		adjust = SCTP_SIZE32(cause_length);
		if (remaining_length >= adjust) {
			remaining_length -= adjust;
		} else {
			remaining_length = 0;
		}
		cause = (struct sctp_error_cause *)((caddr_t)cause + adjust);
	}
	sctp_ulp_notify(SCTP_NOTIFY_REMOTE_ERROR, stcb, code, ch, SCTP_SO_NOT_LOCKED);
	return (0);
}

static int
sctp_handle_init_ack(struct mbuf *m, int iphlen, int offset,
    struct sockaddr *src, struct sockaddr *dst, struct sctphdr *sh,
    struct sctp_init_ack_chunk *cp, struct sctp_tcb *stcb,
    struct sctp_nets *net, int *abort_no_unlock,
    uint8_t mflowtype, uint32_t mflowid,
    uint32_t vrf_id)
{
	struct sctp_init_ack *init_ack;
	struct mbuf *op_err;

	SCTPDBG(SCTP_DEBUG_INPUT2,
	    "sctp_handle_init_ack: handling INIT-ACK\n");

	if (stcb == NULL) {
		SCTPDBG(SCTP_DEBUG_INPUT2,
		    "sctp_handle_init_ack: TCB is null\n");
		return (-1);
	}
	if (ntohs(cp->ch.chunk_length) < sizeof(struct sctp_init_ack_chunk)) {
		/* Invalid length */
		op_err = sctp_generate_cause(SCTP_CAUSE_INVALID_PARAM, "");
		sctp_abort_association(stcb->sctp_ep, stcb, m, iphlen,
		    src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, net->port);
		*abort_no_unlock = 1;
		return (-1);
	}
	init_ack = &cp->init;
	/* validate parameters */
	if (init_ack->initiate_tag == 0) {
		/* protocol error... send an abort */
		op_err = sctp_generate_cause(SCTP_CAUSE_INVALID_PARAM, "");
		sctp_abort_association(stcb->sctp_ep, stcb, m, iphlen,
		    src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, net->port);
		*abort_no_unlock = 1;
		return (-1);
	}
	if (ntohl(init_ack->a_rwnd) < SCTP_MIN_RWND) {
		/* protocol error... send an abort */
		op_err = sctp_generate_cause(SCTP_CAUSE_INVALID_PARAM, "");
		sctp_abort_association(stcb->sctp_ep, stcb, m, iphlen,
		    src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, net->port);
		*abort_no_unlock = 1;
		return (-1);
	}
	if (init_ack->num_inbound_streams == 0) {
		/* protocol error... send an abort */
		op_err = sctp_generate_cause(SCTP_CAUSE_INVALID_PARAM, "");
		sctp_abort_association(stcb->sctp_ep, stcb, m, iphlen,
		    src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, net->port);
		*abort_no_unlock = 1;
		return (-1);
	}
	if (init_ack->num_outbound_streams == 0) {
		/* protocol error... send an abort */
		op_err = sctp_generate_cause(SCTP_CAUSE_INVALID_PARAM, "");
		sctp_abort_association(stcb->sctp_ep, stcb, m, iphlen,
		    src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, net->port);
		*abort_no_unlock = 1;
		return (-1);
	}
	/* process according to association state... */
	switch (SCTP_GET_STATE(stcb)) {
	case SCTP_STATE_COOKIE_WAIT:
		/* this is the expected state for this chunk */
		/* process the INIT-ACK parameters */
		if (stcb->asoc.primary_destination->dest_state &
		    SCTP_ADDR_UNCONFIRMED) {
			/*
			 * The primary is where we sent the INIT, we can
			 * always consider it confirmed when the INIT-ACK is
			 * returned. Do this before we load addresses
			 * though.
			 */
			stcb->asoc.primary_destination->dest_state &=
			    ~SCTP_ADDR_UNCONFIRMED;
			sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_CONFIRMED,
			    stcb, 0, (void *)stcb->asoc.primary_destination, SCTP_SO_NOT_LOCKED);
		}
		if (sctp_process_init_ack(m, iphlen, offset, src, dst, sh, cp, stcb,
		    net, abort_no_unlock,
		    mflowtype, mflowid,
		    vrf_id) < 0) {
			/* error in parsing parameters */
			return (-1);
		}
		/* update our state */
		SCTPDBG(SCTP_DEBUG_INPUT2, "moving to COOKIE-ECHOED state\n");
		SCTP_SET_STATE(stcb, SCTP_STATE_COOKIE_ECHOED);

		/* reset the RTO calc */
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_THRESHOLD_LOGGING) {
			sctp_misc_ints(SCTP_THRESHOLD_CLEAR,
			    stcb->asoc.overall_error_count,
			    0,
			    SCTP_FROM_SCTP_INPUT,
			    __LINE__);
		}
		stcb->asoc.overall_error_count = 0;
		(void)SCTP_GETTIME_TIMEVAL(&stcb->asoc.time_entered);
		/*
		 * collapse the init timer back in case of a exponential
		 * backoff
		 */
		sctp_timer_start(SCTP_TIMER_TYPE_COOKIE, stcb->sctp_ep,
		    stcb, net);
		/*
		 * the send at the end of the inbound data processing will
		 * cause the cookie to be sent
		 */
		break;
	case SCTP_STATE_SHUTDOWN_SENT:
		/* incorrect state... discard */
		break;
	case SCTP_STATE_COOKIE_ECHOED:
		/* incorrect state... discard */
		break;
	case SCTP_STATE_OPEN:
		/* incorrect state... discard */
		break;
	case SCTP_STATE_EMPTY:
	case SCTP_STATE_INUSE:
	default:
		/* incorrect state... discard */
		return (-1);
		break;
	}
	SCTPDBG(SCTP_DEBUG_INPUT1, "Leaving handle-init-ack end\n");
	return (0);
}

static struct sctp_tcb *
sctp_process_cookie_new(struct mbuf *m, int iphlen, int offset,
    struct sockaddr *src, struct sockaddr *dst,
    struct sctphdr *sh, struct sctp_state_cookie *cookie, int cookie_len,
    struct sctp_inpcb *inp, struct sctp_nets **netp,
    struct sockaddr *init_src, int *notification,
    int auth_skipped, uint32_t auth_offset, uint32_t auth_len,
    uint8_t mflowtype, uint32_t mflowid,
    uint32_t vrf_id, uint16_t port);


/*
 * handle a state cookie for an existing association m: input packet mbuf
 * chain-- assumes a pullup on IP/SCTP/COOKIE-ECHO chunk note: this is a
 * "split" mbuf and the cookie signature does not exist offset: offset into
 * mbuf to the cookie-echo chunk
 */
static struct sctp_tcb *
sctp_process_cookie_existing(struct mbuf *m, int iphlen, int offset,
    struct sockaddr *src, struct sockaddr *dst,
    struct sctphdr *sh, struct sctp_state_cookie *cookie, int cookie_len,
    struct sctp_inpcb *inp, struct sctp_tcb *stcb, struct sctp_nets **netp,
    struct sockaddr *init_src, int *notification,
    int auth_skipped, uint32_t auth_offset, uint32_t auth_len,
    uint8_t mflowtype, uint32_t mflowid,
    uint32_t vrf_id, uint16_t port)
{
	struct sctp_association *asoc;
	struct sctp_init_chunk *init_cp, init_buf;
	struct sctp_init_ack_chunk *initack_cp, initack_buf;
	struct sctp_nets *net;
	struct mbuf *op_err;
	struct timeval old;
	int init_offset, initack_offset, i;
	int retval;
	int spec_flag = 0;
	uint32_t how_indx;
#if defined(SCTP_DETAILED_STR_STATS)
	int j;
#endif

	net = *netp;
	/* I know that the TCB is non-NULL from the caller */
	asoc = &stcb->asoc;
	for (how_indx = 0; how_indx < sizeof(asoc->cookie_how); how_indx++) {
		if (asoc->cookie_how[how_indx] == 0)
			break;
	}
	if (how_indx < sizeof(asoc->cookie_how)) {
		asoc->cookie_how[how_indx] = 1;
	}
	if (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_ACK_SENT) {
		/* SHUTDOWN came in after sending INIT-ACK */
		sctp_send_shutdown_ack(stcb, stcb->asoc.primary_destination);
		op_err = sctp_generate_cause(SCTP_CAUSE_COOKIE_IN_SHUTDOWN, "");
		sctp_send_operr_to(src, dst, sh, cookie->peers_vtag, op_err,
		    mflowtype, mflowid, inp->fibnum,
		    vrf_id, net->port);
		if (how_indx < sizeof(asoc->cookie_how))
			asoc->cookie_how[how_indx] = 2;
		return (NULL);
	}
	/*
	 * find and validate the INIT chunk in the cookie (peer's info) the
	 * INIT should start after the cookie-echo header struct (chunk
	 * header, state cookie header struct)
	 */
	init_offset = offset += sizeof(struct sctp_cookie_echo_chunk);

	init_cp = (struct sctp_init_chunk *)
	    sctp_m_getptr(m, init_offset, sizeof(struct sctp_init_chunk),
	    (uint8_t *)&init_buf);
	if (init_cp == NULL) {
		/* could not pull a INIT chunk in cookie */
		return (NULL);
	}
	if (init_cp->ch.chunk_type != SCTP_INITIATION) {
		return (NULL);
	}
	/*
	 * find and validate the INIT-ACK chunk in the cookie (my info) the
	 * INIT-ACK follows the INIT chunk
	 */
	initack_offset = init_offset + SCTP_SIZE32(ntohs(init_cp->ch.chunk_length));
	initack_cp = (struct sctp_init_ack_chunk *)
	    sctp_m_getptr(m, initack_offset, sizeof(struct sctp_init_ack_chunk),
	    (uint8_t *)&initack_buf);
	if (initack_cp == NULL) {
		/* could not pull INIT-ACK chunk in cookie */
		return (NULL);
	}
	if (initack_cp->ch.chunk_type != SCTP_INITIATION_ACK) {
		return (NULL);
	}
	if ((ntohl(initack_cp->init.initiate_tag) == asoc->my_vtag) &&
	    (ntohl(init_cp->init.initiate_tag) == asoc->peer_vtag)) {
		/*
		 * case D in Section 5.2.4 Table 2: MMAA process accordingly
		 * to get into the OPEN state
		 */
		if (ntohl(initack_cp->init.initial_tsn) != asoc->init_seq_number) {
			/*-
			 * Opps, this means that we somehow generated two vtag's
			 * the same. I.e. we did:
			 *  Us               Peer
			 *   <---INIT(tag=a)------
			 *   ----INIT-ACK(tag=t)-->
			 *   ----INIT(tag=t)------> *1
			 *   <---INIT-ACK(tag=a)---
                         *   <----CE(tag=t)------------- *2
			 *
			 * At point *1 we should be generating a different
			 * tag t'. Which means we would throw away the CE and send
			 * ours instead. Basically this is case C (throw away side).
			 */
			if (how_indx < sizeof(asoc->cookie_how))
				asoc->cookie_how[how_indx] = 17;
			return (NULL);

		}
		switch (SCTP_GET_STATE(stcb)) {
		case SCTP_STATE_COOKIE_WAIT:
		case SCTP_STATE_COOKIE_ECHOED:
			/*
			 * INIT was sent but got a COOKIE_ECHO with the
			 * correct tags... just accept it...but we must
			 * process the init so that we can make sure we have
			 * the right seq no's.
			 */
			/* First we must process the INIT !! */
			retval = sctp_process_init(init_cp, stcb);
			if (retval < 0) {
				if (how_indx < sizeof(asoc->cookie_how))
					asoc->cookie_how[how_indx] = 3;
				return (NULL);
			}
			/* we have already processed the INIT so no problem */
			sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, inp,
			    stcb, net,
			    SCTP_FROM_SCTP_INPUT + SCTP_LOC_13);
			sctp_timer_stop(SCTP_TIMER_TYPE_INIT, inp,
			    stcb, net,
			    SCTP_FROM_SCTP_INPUT + SCTP_LOC_14);
			/* update current state */
			if (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED)
				SCTP_STAT_INCR_COUNTER32(sctps_activeestab);
			else
				SCTP_STAT_INCR_COUNTER32(sctps_collisionestab);

			SCTP_SET_STATE(stcb, SCTP_STATE_OPEN);
			if (asoc->state & SCTP_STATE_SHUTDOWN_PENDING) {
				sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD,
				    stcb->sctp_ep, stcb, asoc->primary_destination);
			}
			SCTP_STAT_INCR_GAUGE32(sctps_currestab);
			sctp_stop_all_cookie_timers(stcb);
			if (((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) &&
			    (!SCTP_IS_LISTENING(inp))) {
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
				struct socket *so;
#endif
				/*
				 * Here is where collision would go if we
				 * did a connect() and instead got a
				 * init/init-ack/cookie done before the
				 * init-ack came back..
				 */
				stcb->sctp_ep->sctp_flags |=
				    SCTP_PCB_FLAGS_CONNECTED;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
				so = SCTP_INP_SO(stcb->sctp_ep);
				atomic_add_int(&stcb->asoc.refcnt, 1);
				SCTP_TCB_UNLOCK(stcb);
				SCTP_SOCKET_LOCK(so, 1);
				SCTP_TCB_LOCK(stcb);
				atomic_add_int(&stcb->asoc.refcnt, -1);
				if (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET) {
					SCTP_SOCKET_UNLOCK(so, 1);
					return (NULL);
				}
#endif
				soisconnected(stcb->sctp_socket);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
				SCTP_SOCKET_UNLOCK(so, 1);
#endif
			}
			/* notify upper layer */
			*notification = SCTP_NOTIFY_ASSOC_UP;
			/*
			 * since we did not send a HB make sure we don't
			 * double things
			 */
			old.tv_sec = cookie->time_entered.tv_sec;
			old.tv_usec = cookie->time_entered.tv_usec;
			net->hb_responded = 1;
			net->RTO = sctp_calculate_rto(stcb, asoc, net,
			    &old,
			    SCTP_RTT_FROM_NON_DATA);

			if (stcb->asoc.sctp_autoclose_ticks &&
			    (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_AUTOCLOSE))) {
				sctp_timer_start(SCTP_TIMER_TYPE_AUTOCLOSE,
				    inp, stcb, NULL);
			}
			break;
		default:
			/*
			 * we're in the OPEN state (or beyond), so peer must
			 * have simply lost the COOKIE-ACK
			 */
			break;
		}		/* end switch */
		sctp_stop_all_cookie_timers(stcb);
		/*
		 * We ignore the return code here.. not sure if we should
		 * somehow abort.. but we do have an existing asoc. This
		 * really should not fail.
		 */
		if (sctp_load_addresses_from_init(stcb, m,
		    init_offset + sizeof(struct sctp_init_chunk),
		    initack_offset, src, dst, init_src, stcb->asoc.port)) {
			if (how_indx < sizeof(asoc->cookie_how))
				asoc->cookie_how[how_indx] = 4;
			return (NULL);
		}
		/* respond with a COOKIE-ACK */
		sctp_toss_old_cookies(stcb, asoc);
		sctp_send_cookie_ack(stcb);
		if (how_indx < sizeof(asoc->cookie_how))
			asoc->cookie_how[how_indx] = 5;
		return (stcb);
	}

	if (ntohl(initack_cp->init.initiate_tag) != asoc->my_vtag &&
	    ntohl(init_cp->init.initiate_tag) == asoc->peer_vtag &&
	    cookie->tie_tag_my_vtag == 0 &&
	    cookie->tie_tag_peer_vtag == 0) {
		/*
		 * case C in Section 5.2.4 Table 2: XMOO silently discard
		 */
		if (how_indx < sizeof(asoc->cookie_how))
			asoc->cookie_how[how_indx] = 6;
		return (NULL);
	}
	/*
	 * If nat support, and the below and stcb is established, send back
	 * a ABORT(colliding state) if we are established.
	 */
	if ((SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) &&
	    (asoc->peer_supports_nat) &&
	    ((ntohl(initack_cp->init.initiate_tag) == asoc->my_vtag) &&
	    ((ntohl(init_cp->init.initiate_tag) != asoc->peer_vtag) ||
	    (asoc->peer_vtag == 0)))) {
		/*
		 * Special case - Peer's support nat. We may have two init's
		 * that we gave out the same tag on since one was not
		 * established.. i.e. we get INIT from host-1 behind the nat
		 * and we respond tag-a, we get a INIT from host-2 behind
		 * the nat and we get tag-a again. Then we bring up host-1
		 * (or 2's) assoc, Then comes the cookie from hsot-2 (or 1).
		 * Now we have colliding state. We must send an abort here
		 * with colliding state indication.
		 */
		op_err = sctp_generate_cause(SCTP_CAUSE_NAT_COLLIDING_STATE, "");
		sctp_send_abort(m, iphlen, src, dst, sh, 0, op_err,
		    mflowtype, mflowid, inp->fibnum,
		    vrf_id, port);
		return (NULL);
	}
	if ((ntohl(initack_cp->init.initiate_tag) == asoc->my_vtag) &&
	    ((ntohl(init_cp->init.initiate_tag) != asoc->peer_vtag) ||
	    (asoc->peer_vtag == 0))) {
		/*
		 * case B in Section 5.2.4 Table 2: MXAA or MOAA my info
		 * should be ok, re-accept peer info
		 */
		if (ntohl(initack_cp->init.initial_tsn) != asoc->init_seq_number) {
			/*
			 * Extension of case C. If we hit this, then the
			 * random number generator returned the same vtag
			 * when we first sent our INIT-ACK and when we later
			 * sent our INIT. The side with the seq numbers that
			 * are different will be the one that normnally
			 * would have hit case C. This in effect "extends"
			 * our vtags in this collision case to be 64 bits.
			 * The same collision could occur aka you get both
			 * vtag and seq number the same twice in a row.. but
			 * is much less likely. If it did happen then we
			 * would proceed through and bring up the assoc.. we
			 * may end up with the wrong stream setup however..
			 * which would be bad.. but there is no way to
			 * tell.. until we send on a stream that does not
			 * exist :-)
			 */
			if (how_indx < sizeof(asoc->cookie_how))
				asoc->cookie_how[how_indx] = 7;

			return (NULL);
		}
		if (how_indx < sizeof(asoc->cookie_how))
			asoc->cookie_how[how_indx] = 8;
		sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, inp, stcb, net,
		    SCTP_FROM_SCTP_INPUT + SCTP_LOC_15);
		sctp_stop_all_cookie_timers(stcb);
		/*
		 * since we did not send a HB make sure we don't double
		 * things
		 */
		net->hb_responded = 1;
		if (stcb->asoc.sctp_autoclose_ticks &&
		    sctp_is_feature_on(inp, SCTP_PCB_FLAGS_AUTOCLOSE)) {
			sctp_timer_start(SCTP_TIMER_TYPE_AUTOCLOSE, inp, stcb,
			    NULL);
		}
		asoc->my_rwnd = ntohl(initack_cp->init.a_rwnd);
		asoc->pre_open_streams = ntohs(initack_cp->init.num_outbound_streams);

		if (ntohl(init_cp->init.initiate_tag) != asoc->peer_vtag) {
			/*
			 * Ok the peer probably discarded our data (if we
			 * echoed a cookie+data). So anything on the
			 * sent_queue should be marked for retransmit, we
			 * may not get something to kick us so it COULD
			 * still take a timeout to move these.. but it can't
			 * hurt to mark them.
			 */
			struct sctp_tmit_chunk *chk;

			TAILQ_FOREACH(chk, &stcb->asoc.sent_queue, sctp_next) {
				if (chk->sent < SCTP_DATAGRAM_RESEND) {
					chk->sent = SCTP_DATAGRAM_RESEND;
					sctp_flight_size_decrease(chk);
					sctp_total_flight_decrease(stcb, chk);
					sctp_ucount_incr(stcb->asoc.sent_queue_retran_cnt);
					spec_flag++;
				}
			}

		}
		/* process the INIT info (peer's info) */
		retval = sctp_process_init(init_cp, stcb);
		if (retval < 0) {
			if (how_indx < sizeof(asoc->cookie_how))
				asoc->cookie_how[how_indx] = 9;
			return (NULL);
		}
		if (sctp_load_addresses_from_init(stcb, m,
		    init_offset + sizeof(struct sctp_init_chunk),
		    initack_offset, src, dst, init_src, stcb->asoc.port)) {
			if (how_indx < sizeof(asoc->cookie_how))
				asoc->cookie_how[how_indx] = 10;
			return (NULL);
		}
		if ((SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_WAIT) ||
		    (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED)) {
			*notification = SCTP_NOTIFY_ASSOC_UP;

			if (((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) &&
			    (!SCTP_IS_LISTENING(inp))) {
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
				struct socket *so;
#endif
				stcb->sctp_ep->sctp_flags |=
				    SCTP_PCB_FLAGS_CONNECTED;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
				so = SCTP_INP_SO(stcb->sctp_ep);
				atomic_add_int(&stcb->asoc.refcnt, 1);
				SCTP_TCB_UNLOCK(stcb);
				SCTP_SOCKET_LOCK(so, 1);
				SCTP_TCB_LOCK(stcb);
				atomic_add_int(&stcb->asoc.refcnt, -1);
				if (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET) {
					SCTP_SOCKET_UNLOCK(so, 1);
					return (NULL);
				}
#endif
				soisconnected(stcb->sctp_socket);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
				SCTP_SOCKET_UNLOCK(so, 1);
#endif
			}
			if (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED)
				SCTP_STAT_INCR_COUNTER32(sctps_activeestab);
			else
				SCTP_STAT_INCR_COUNTER32(sctps_collisionestab);
			SCTP_STAT_INCR_GAUGE32(sctps_currestab);
		} else if (SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) {
			SCTP_STAT_INCR_COUNTER32(sctps_restartestab);
		} else {
			SCTP_STAT_INCR_COUNTER32(sctps_collisionestab);
		}
		SCTP_SET_STATE(stcb, SCTP_STATE_OPEN);
		if (asoc->state & SCTP_STATE_SHUTDOWN_PENDING) {
			sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD,
			    stcb->sctp_ep, stcb, asoc->primary_destination);
		}
		sctp_stop_all_cookie_timers(stcb);
		sctp_toss_old_cookies(stcb, asoc);
		sctp_send_cookie_ack(stcb);
		if (spec_flag) {
			/*
			 * only if we have retrans set do we do this. What
			 * this call does is get only the COOKIE-ACK out and
			 * then when we return the normal call to
			 * sctp_chunk_output will get the retrans out behind
			 * this.
			 */
			sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_COOKIE_ACK, SCTP_SO_NOT_LOCKED);
		}
		if (how_indx < sizeof(asoc->cookie_how))
			asoc->cookie_how[how_indx] = 11;

		return (stcb);
	}
	if ((ntohl(initack_cp->init.initiate_tag) != asoc->my_vtag &&
	    ntohl(init_cp->init.initiate_tag) != asoc->peer_vtag) &&
	    cookie->tie_tag_my_vtag == asoc->my_vtag_nonce &&
	    cookie->tie_tag_peer_vtag == asoc->peer_vtag_nonce &&
	    cookie->tie_tag_peer_vtag != 0) {
		struct sctpasochead *head;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		struct socket *so;
#endif

		if (asoc->peer_supports_nat) {
			/*
			 * This is a gross gross hack. Just call the
			 * cookie_new code since we are allowing a duplicate
			 * association. I hope this works...
			 */
			return (sctp_process_cookie_new(m, iphlen, offset, src, dst,
			    sh, cookie, cookie_len,
			    inp, netp, init_src, notification,
			    auth_skipped, auth_offset, auth_len,
			    mflowtype, mflowid,
			    vrf_id, port));
		}
		/*
		 * case A in Section 5.2.4 Table 2: XXMM (peer restarted)
		 */
		/* temp code */
		if (how_indx < sizeof(asoc->cookie_how))
			asoc->cookie_how[how_indx] = 12;
		sctp_timer_stop(SCTP_TIMER_TYPE_INIT, inp, stcb, net,
		    SCTP_FROM_SCTP_INPUT + SCTP_LOC_16);
		sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT, inp, stcb, net,
		    SCTP_FROM_SCTP_INPUT + SCTP_LOC_17);

		/* notify upper layer */
		*notification = SCTP_NOTIFY_ASSOC_RESTART;
		atomic_add_int(&stcb->asoc.refcnt, 1);
		if ((SCTP_GET_STATE(stcb) != SCTP_STATE_OPEN) &&
		    (SCTP_GET_STATE(stcb) != SCTP_STATE_SHUTDOWN_RECEIVED) &&
		    (SCTP_GET_STATE(stcb) != SCTP_STATE_SHUTDOWN_SENT)) {
			SCTP_STAT_INCR_GAUGE32(sctps_currestab);
		}
		if (SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) {
			SCTP_STAT_INCR_GAUGE32(sctps_restartestab);
		} else if (SCTP_GET_STATE(stcb) != SCTP_STATE_SHUTDOWN_SENT) {
			SCTP_STAT_INCR_GAUGE32(sctps_collisionestab);
		}
		if (asoc->state & SCTP_STATE_SHUTDOWN_PENDING) {
			SCTP_SET_STATE(stcb, SCTP_STATE_OPEN);
			sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD,
			    stcb->sctp_ep, stcb, asoc->primary_destination);

		} else if (SCTP_GET_STATE(stcb) != SCTP_STATE_SHUTDOWN_SENT) {
			/* move to OPEN state, if not in SHUTDOWN_SENT */
			SCTP_SET_STATE(stcb, SCTP_STATE_OPEN);
		}
		asoc->pre_open_streams =
		    ntohs(initack_cp->init.num_outbound_streams);
		asoc->init_seq_number = ntohl(initack_cp->init.initial_tsn);
		asoc->sending_seq = asoc->asconf_seq_out = asoc->str_reset_seq_out = asoc->init_seq_number;
		asoc->asconf_seq_out_acked = asoc->asconf_seq_out - 1;

		asoc->asconf_seq_in = asoc->last_acked_seq = asoc->init_seq_number - 1;

		asoc->str_reset_seq_in = asoc->init_seq_number;

		asoc->advanced_peer_ack_point = asoc->last_acked_seq;
		if (asoc->mapping_array) {
			memset(asoc->mapping_array, 0,
			    asoc->mapping_array_size);
		}
		if (asoc->nr_mapping_array) {
			memset(asoc->nr_mapping_array, 0,
			    asoc->mapping_array_size);
		}
		SCTP_TCB_UNLOCK(stcb);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		so = SCTP_INP_SO(stcb->sctp_ep);
		SCTP_SOCKET_LOCK(so, 1);
#endif
		SCTP_INP_INFO_WLOCK();
		SCTP_INP_WLOCK(stcb->sctp_ep);
		SCTP_TCB_LOCK(stcb);
		atomic_add_int(&stcb->asoc.refcnt, -1);
		/* send up all the data */
		SCTP_TCB_SEND_LOCK(stcb);

		sctp_report_all_outbound(stcb, 0, 1, SCTP_SO_LOCKED);
		for (i = 0; i < stcb->asoc.streamoutcnt; i++) {
			stcb->asoc.strmout[i].chunks_on_queues = 0;
#if defined(SCTP_DETAILED_STR_STATS)
			for (j = 0; j < SCTP_PR_SCTP_MAX + 1; j++) {
				asoc->strmout[i].abandoned_sent[j] = 0;
				asoc->strmout[i].abandoned_unsent[j] = 0;
			}
#else
			asoc->strmout[i].abandoned_sent[0] = 0;
			asoc->strmout[i].abandoned_unsent[0] = 0;
#endif
			stcb->asoc.strmout[i].sid = i;
			stcb->asoc.strmout[i].next_mid_ordered = 0;
			stcb->asoc.strmout[i].next_mid_unordered = 0;
			stcb->asoc.strmout[i].last_msg_incomplete = 0;
		}
		/* process the INIT-ACK info (my info) */
		asoc->my_vtag = ntohl(initack_cp->init.initiate_tag);
		asoc->my_rwnd = ntohl(initack_cp->init.a_rwnd);

		/* pull from vtag hash */
		LIST_REMOVE(stcb, sctp_asocs);
		/* re-insert to new vtag position */
		head = &SCTP_BASE_INFO(sctp_asochash)[SCTP_PCBHASH_ASOC(stcb->asoc.my_vtag,
		    SCTP_BASE_INFO(hashasocmark))];
		/*
		 * put it in the bucket in the vtag hash of assoc's for the
		 * system
		 */
		LIST_INSERT_HEAD(head, stcb, sctp_asocs);

		SCTP_TCB_SEND_UNLOCK(stcb);
		SCTP_INP_WUNLOCK(stcb->sctp_ep);
		SCTP_INP_INFO_WUNLOCK();
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
		asoc->total_flight = 0;
		asoc->total_flight_count = 0;
		/* process the INIT info (peer's info) */
		retval = sctp_process_init(init_cp, stcb);
		if (retval < 0) {
			if (how_indx < sizeof(asoc->cookie_how))
				asoc->cookie_how[how_indx] = 13;

			return (NULL);
		}
		/*
		 * since we did not send a HB make sure we don't double
		 * things
		 */
		net->hb_responded = 1;

		if (sctp_load_addresses_from_init(stcb, m,
		    init_offset + sizeof(struct sctp_init_chunk),
		    initack_offset, src, dst, init_src, stcb->asoc.port)) {
			if (how_indx < sizeof(asoc->cookie_how))
				asoc->cookie_how[how_indx] = 14;

			return (NULL);
		}
		/* respond with a COOKIE-ACK */
		sctp_stop_all_cookie_timers(stcb);
		sctp_toss_old_cookies(stcb, asoc);
		sctp_send_cookie_ack(stcb);
		if (how_indx < sizeof(asoc->cookie_how))
			asoc->cookie_how[how_indx] = 15;

		return (stcb);
	}
	if (how_indx < sizeof(asoc->cookie_how))
		asoc->cookie_how[how_indx] = 16;
	/* all other cases... */
	return (NULL);
}


/*
 * handle a state cookie for a new association m: input packet mbuf chain--
 * assumes a pullup on IP/SCTP/COOKIE-ECHO chunk note: this is a "split" mbuf
 * and the cookie signature does not exist offset: offset into mbuf to the
 * cookie-echo chunk length: length of the cookie chunk to: where the init
 * was from returns a new TCB
 */
static struct sctp_tcb *
sctp_process_cookie_new(struct mbuf *m, int iphlen, int offset,
    struct sockaddr *src, struct sockaddr *dst,
    struct sctphdr *sh, struct sctp_state_cookie *cookie, int cookie_len,
    struct sctp_inpcb *inp, struct sctp_nets **netp,
    struct sockaddr *init_src, int *notification,
    int auth_skipped, uint32_t auth_offset, uint32_t auth_len,
    uint8_t mflowtype, uint32_t mflowid,
    uint32_t vrf_id, uint16_t port)
{
	struct sctp_tcb *stcb;
	struct sctp_init_chunk *init_cp, init_buf;
	struct sctp_init_ack_chunk *initack_cp, initack_buf;
	union sctp_sockstore store;
	struct sctp_association *asoc;
	int init_offset, initack_offset, initack_limit;
	int retval;
	int error = 0;
	uint8_t auth_chunk_buf[SCTP_PARAM_BUFFER_SIZE];
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;

	so = SCTP_INP_SO(inp);
#endif

	/*
	 * find and validate the INIT chunk in the cookie (peer's info) the
	 * INIT should start after the cookie-echo header struct (chunk
	 * header, state cookie header struct)
	 */
	init_offset = offset + sizeof(struct sctp_cookie_echo_chunk);
	init_cp = (struct sctp_init_chunk *)
	    sctp_m_getptr(m, init_offset, sizeof(struct sctp_init_chunk),
	    (uint8_t *)&init_buf);
	if (init_cp == NULL) {
		/* could not pull a INIT chunk in cookie */
		SCTPDBG(SCTP_DEBUG_INPUT1,
		    "process_cookie_new: could not pull INIT chunk hdr\n");
		return (NULL);
	}
	if (init_cp->ch.chunk_type != SCTP_INITIATION) {
		SCTPDBG(SCTP_DEBUG_INPUT1, "HUH? process_cookie_new: could not find INIT chunk!\n");
		return (NULL);
	}
	initack_offset = init_offset + SCTP_SIZE32(ntohs(init_cp->ch.chunk_length));
	/*
	 * find and validate the INIT-ACK chunk in the cookie (my info) the
	 * INIT-ACK follows the INIT chunk
	 */
	initack_cp = (struct sctp_init_ack_chunk *)
	    sctp_m_getptr(m, initack_offset, sizeof(struct sctp_init_ack_chunk),
	    (uint8_t *)&initack_buf);
	if (initack_cp == NULL) {
		/* could not pull INIT-ACK chunk in cookie */
		SCTPDBG(SCTP_DEBUG_INPUT1, "process_cookie_new: could not pull INIT-ACK chunk hdr\n");
		return (NULL);
	}
	if (initack_cp->ch.chunk_type != SCTP_INITIATION_ACK) {
		return (NULL);
	}
	/*
	 * NOTE: We can't use the INIT_ACK's chk_length to determine the
	 * "initack_limit" value.  This is because the chk_length field
	 * includes the length of the cookie, but the cookie is omitted when
	 * the INIT and INIT_ACK are tacked onto the cookie...
	 */
	initack_limit = offset + cookie_len;

	/*
	 * now that we know the INIT/INIT-ACK are in place, create a new TCB
	 * and popluate
	 */

	/*
	 * Here we do a trick, we set in NULL for the proc/thread argument.
	 * We do this since in effect we only use the p argument when the
	 * socket is unbound and we must do an implicit bind. Since we are
	 * getting a cookie, we cannot be unbound.
	 */
	stcb = sctp_aloc_assoc(inp, init_src, &error,
	    ntohl(initack_cp->init.initiate_tag), vrf_id,
	    ntohs(initack_cp->init.num_outbound_streams),
	    port,
	    (struct thread *)NULL
	    );
	if (stcb == NULL) {
		struct mbuf *op_err;

		/* memory problem? */
		SCTPDBG(SCTP_DEBUG_INPUT1,
		    "process_cookie_new: no room for another TCB!\n");
		op_err = sctp_generate_cause(SCTP_CAUSE_OUT_OF_RESC, "");
		sctp_abort_association(inp, (struct sctp_tcb *)NULL, m, iphlen,
		    src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, port);
		return (NULL);
	}
	/* get the correct sctp_nets */
	if (netp)
		*netp = sctp_findnet(stcb, init_src);

	asoc = &stcb->asoc;
	/* get scope variables out of cookie */
	asoc->scope.ipv4_local_scope = cookie->ipv4_scope;
	asoc->scope.site_scope = cookie->site_scope;
	asoc->scope.local_scope = cookie->local_scope;
	asoc->scope.loopback_scope = cookie->loopback_scope;

	if ((asoc->scope.ipv4_addr_legal != cookie->ipv4_addr_legal) ||
	    (asoc->scope.ipv6_addr_legal != cookie->ipv6_addr_legal)) {
		struct mbuf *op_err;

		/*
		 * Houston we have a problem. The EP changed while the
		 * cookie was in flight. Only recourse is to abort the
		 * association.
		 */
		op_err = sctp_generate_cause(SCTP_CAUSE_OUT_OF_RESC, "");
		sctp_abort_association(inp, (struct sctp_tcb *)NULL, m, iphlen,
		    src, dst, sh, op_err,
		    mflowtype, mflowid,
		    vrf_id, port);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
		(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
		    SCTP_FROM_SCTP_INPUT + SCTP_LOC_18);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
		return (NULL);
	}
	/* process the INIT-ACK info (my info) */
	asoc->my_vtag = ntohl(initack_cp->init.initiate_tag);
	asoc->my_rwnd = ntohl(initack_cp->init.a_rwnd);
	asoc->pre_open_streams = ntohs(initack_cp->init.num_outbound_streams);
	asoc->init_seq_number = ntohl(initack_cp->init.initial_tsn);
	asoc->sending_seq = asoc->asconf_seq_out = asoc->str_reset_seq_out = asoc->init_seq_number;
	asoc->asconf_seq_out_acked = asoc->asconf_seq_out - 1;
	asoc->asconf_seq_in = asoc->last_acked_seq = asoc->init_seq_number - 1;
	asoc->str_reset_seq_in = asoc->init_seq_number;

	asoc->advanced_peer_ack_point = asoc->last_acked_seq;

	/* process the INIT info (peer's info) */
	if (netp)
		retval = sctp_process_init(init_cp, stcb);
	else
		retval = 0;
	if (retval < 0) {
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
		(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
		    SCTP_FROM_SCTP_INPUT + SCTP_LOC_19);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
		return (NULL);
	}
	/* load all addresses */
	if (sctp_load_addresses_from_init(stcb, m,
	    init_offset + sizeof(struct sctp_init_chunk), initack_offset,
	    src, dst, init_src, port)) {
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
		(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
		    SCTP_FROM_SCTP_INPUT + SCTP_LOC_20);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
		return (NULL);
	}
	/*
	 * verify any preceding AUTH chunk that was skipped
	 */
	/* pull the local authentication parameters from the cookie/init-ack */
	sctp_auth_get_cookie_params(stcb, m,
	    initack_offset + sizeof(struct sctp_init_ack_chunk),
	    initack_limit - (initack_offset + sizeof(struct sctp_init_ack_chunk)));
	if (auth_skipped) {
		struct sctp_auth_chunk *auth;

		auth = (struct sctp_auth_chunk *)
		    sctp_m_getptr(m, auth_offset, auth_len, auth_chunk_buf);
		if ((auth == NULL) || sctp_handle_auth(stcb, auth, m, auth_offset)) {
			/* auth HMAC failed, dump the assoc and packet */
			SCTPDBG(SCTP_DEBUG_AUTH1,
			    "COOKIE-ECHO: AUTH failed\n");
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
			atomic_add_int(&stcb->asoc.refcnt, 1);
			SCTP_TCB_UNLOCK(stcb);
			SCTP_SOCKET_LOCK(so, 1);
			SCTP_TCB_LOCK(stcb);
			atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
			(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
			    SCTP_FROM_SCTP_INPUT + SCTP_LOC_21);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
			SCTP_SOCKET_UNLOCK(so, 1);
#endif
			return (NULL);
		} else {
			/* remaining chunks checked... good to go */
			stcb->asoc.authenticated = 1;
		}
	}

	/*
	 * if we're doing ASCONFs, check to see if we have any new local
	 * addresses that need to get added to the peer (eg. addresses
	 * changed while cookie echo in flight).  This needs to be done
	 * after we go to the OPEN state to do the correct asconf
	 * processing. else, make sure we have the correct addresses in our
	 * lists
	 */

	/* warning, we re-use sin, sin6, sa_store here! */
	/* pull in local_address (our "from" address) */
	switch (cookie->laddr_type) {
#ifdef INET
	case SCTP_IPV4_ADDRESS:
		/* source addr is IPv4 */
		memset(&store.sin, 0, sizeof(struct sockaddr_in));
		store.sin.sin_family = AF_INET;
		store.sin.sin_len = sizeof(struct sockaddr_in);
		store.sin.sin_addr.s_addr = cookie->laddress[0];
		break;
#endif
#ifdef INET6
	case SCTP_IPV6_ADDRESS:
		/* source addr is IPv6 */
		memset(&store.sin6, 0, sizeof(struct sockaddr_in6));
		store.sin6.sin6_family = AF_INET6;
		store.sin6.sin6_len = sizeof(struct sockaddr_in6);
		store.sin6.sin6_scope_id = cookie->scope_id;
		memcpy(&store.sin6.sin6_addr, cookie->laddress, sizeof(struct in6_addr));
		break;
#endif
	default:
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
		(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
		    SCTP_FROM_SCTP_INPUT + SCTP_LOC_22);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
		return (NULL);
	}

	/* update current state */
	SCTPDBG(SCTP_DEBUG_INPUT2, "moving to OPEN state\n");
	SCTP_SET_STATE(stcb, SCTP_STATE_OPEN);
	if (asoc->state & SCTP_STATE_SHUTDOWN_PENDING) {
		sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD,
		    stcb->sctp_ep, stcb, asoc->primary_destination);
	}
	sctp_stop_all_cookie_timers(stcb);
	SCTP_STAT_INCR_COUNTER32(sctps_passiveestab);
	SCTP_STAT_INCR_GAUGE32(sctps_currestab);

	/* set up to notify upper layer */
	*notification = SCTP_NOTIFY_ASSOC_UP;
	if (((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) &&
	    (!SCTP_IS_LISTENING(inp))) {
		/*
		 * This is an endpoint that called connect() how it got a
		 * cookie that is NEW is a bit of a mystery. It must be that
		 * the INIT was sent, but before it got there.. a complete
		 * INIT/INIT-ACK/COOKIE arrived. But of course then it
		 * should have went to the other code.. not here.. oh well..
		 * a bit of protection is worth having..
		 */
		stcb->sctp_ep->sctp_flags |= SCTP_PCB_FLAGS_CONNECTED;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
		if (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET) {
			SCTP_SOCKET_UNLOCK(so, 1);
			return (NULL);
		}
#endif
		soisconnected(stcb->sctp_socket);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
	} else if ((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) &&
	    (SCTP_IS_LISTENING(inp))) {
		/*
		 * We don't want to do anything with this one. Since it is
		 * the listening guy. The timer will get started for
		 * accepted connections in the caller.
		 */
		;
	}
	/* since we did not send a HB make sure we don't double things */
	if ((netp) && (*netp))
		(*netp)->hb_responded = 1;

	if (stcb->asoc.sctp_autoclose_ticks &&
	    sctp_is_feature_on(inp, SCTP_PCB_FLAGS_AUTOCLOSE)) {
		sctp_timer_start(SCTP_TIMER_TYPE_AUTOCLOSE, inp, stcb, NULL);
	}
	(void)SCTP_GETTIME_TIMEVAL(&stcb->asoc.time_entered);
	if ((netp != NULL) && (*netp != NULL)) {
		struct timeval old;

		/* calculate the RTT and set the encaps port */
		old.tv_sec = cookie->time_entered.tv_sec;
		old.tv_usec = cookie->time_entered.tv_usec;
		(*netp)->RTO = sctp_calculate_rto(stcb, asoc, *netp,
		    &old, SCTP_RTT_FROM_NON_DATA);
	}
	/* respond with a COOKIE-ACK */
	sctp_send_cookie_ack(stcb);

	/*
	 * check the address lists for any ASCONFs that need to be sent
	 * AFTER the cookie-ack is sent
	 */
	sctp_check_address_list(stcb, m,
	    initack_offset + sizeof(struct sctp_init_ack_chunk),
	    initack_limit - (initack_offset + sizeof(struct sctp_init_ack_chunk)),
	    &store.sa, cookie->local_scope, cookie->site_scope,
	    cookie->ipv4_scope, cookie->loopback_scope);


	return (stcb);
}

/*
 * CODE LIKE THIS NEEDS TO RUN IF the peer supports the NAT extension, i.e
 * we NEED to make sure we are not already using the vtag. If so we
 * need to send back an ABORT-TRY-AGAIN-WITH-NEW-TAG No middle box bit!
	head = &SCTP_BASE_INFO(sctp_asochash)[SCTP_PCBHASH_ASOC(tag,
							    SCTP_BASE_INFO(hashasocmark))];
	LIST_FOREACH(stcb, head, sctp_asocs) {
	        if ((stcb->asoc.my_vtag == tag) && (stcb->rport == rport) && (inp == stcb->sctp_ep))  {
		       -- SEND ABORT - TRY AGAIN --
		}
	}
*/

/*
 * handles a COOKIE-ECHO message stcb: modified to either a new or left as
 * existing (non-NULL) TCB
 */
static struct mbuf *
sctp_handle_cookie_echo(struct mbuf *m, int iphlen, int offset,
    struct sockaddr *src, struct sockaddr *dst,
    struct sctphdr *sh, struct sctp_cookie_echo_chunk *cp,
    struct sctp_inpcb **inp_p, struct sctp_tcb **stcb, struct sctp_nets **netp,
    int auth_skipped, uint32_t auth_offset, uint32_t auth_len,
    struct sctp_tcb **locked_tcb,
    uint8_t mflowtype, uint32_t mflowid,
    uint32_t vrf_id, uint16_t port)
{
	struct sctp_state_cookie *cookie;
	struct sctp_tcb *l_stcb = *stcb;
	struct sctp_inpcb *l_inp;
	struct sockaddr *to;
	struct sctp_pcb *ep;
	struct mbuf *m_sig;
	uint8_t calc_sig[SCTP_SIGNATURE_SIZE], tmp_sig[SCTP_SIGNATURE_SIZE];
	uint8_t *sig;
	uint8_t cookie_ok = 0;
	unsigned int sig_offset, cookie_offset;
	unsigned int cookie_len;
	struct timeval now;
	struct timeval time_expires;
	int notification = 0;
	struct sctp_nets *netl;
	int had_a_existing_tcb = 0;
	int send_int_conf = 0;
#ifdef INET
	struct sockaddr_in sin;
#endif
#ifdef INET6
	struct sockaddr_in6 sin6;
#endif

	SCTPDBG(SCTP_DEBUG_INPUT2,
	    "sctp_handle_cookie: handling COOKIE-ECHO\n");

	if (inp_p == NULL) {
		return (NULL);
	}
	cookie = &cp->cookie;
	cookie_offset = offset + sizeof(struct sctp_chunkhdr);
	cookie_len = ntohs(cp->ch.chunk_length);

	if (cookie_len < sizeof(struct sctp_cookie_echo_chunk) +
	    sizeof(struct sctp_init_chunk) +
	    sizeof(struct sctp_init_ack_chunk) + SCTP_SIGNATURE_SIZE) {
		/* cookie too small */
		return (NULL);
	}
	if ((cookie->peerport != sh->src_port) ||
	    (cookie->myport != sh->dest_port) ||
	    (cookie->my_vtag != sh->v_tag)) {
		/*
		 * invalid ports or bad tag.  Note that we always leave the
		 * v_tag in the header in network order and when we stored
		 * it in the my_vtag slot we also left it in network order.
		 * This maintains the match even though it may be in the
		 * opposite byte order of the machine :->
		 */
		return (NULL);
	}
	/*
	 * split off the signature into its own mbuf (since it should not be
	 * calculated in the sctp_hmac_m() call).
	 */
	sig_offset = offset + cookie_len - SCTP_SIGNATURE_SIZE;
	m_sig = m_split(m, sig_offset, M_NOWAIT);
	if (m_sig == NULL) {
		/* out of memory or ?? */
		return (NULL);
	}
#ifdef SCTP_MBUF_LOGGING
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
		sctp_log_mbc(m_sig, SCTP_MBUF_SPLIT);
	}
#endif

	/*
	 * compute the signature/digest for the cookie
	 */
	ep = &(*inp_p)->sctp_ep;
	l_inp = *inp_p;
	if (l_stcb) {
		SCTP_TCB_UNLOCK(l_stcb);
	}
	SCTP_INP_RLOCK(l_inp);
	if (l_stcb) {
		SCTP_TCB_LOCK(l_stcb);
	}
	/* which cookie is it? */
	if ((cookie->time_entered.tv_sec < (long)ep->time_of_secret_change) &&
	    (ep->current_secret_number != ep->last_secret_number)) {
		/* it's the old cookie */
		(void)sctp_hmac_m(SCTP_HMAC,
		    (uint8_t *)ep->secret_key[(int)ep->last_secret_number],
		    SCTP_SECRET_SIZE, m, cookie_offset, calc_sig, 0);
	} else {
		/* it's the current cookie */
		(void)sctp_hmac_m(SCTP_HMAC,
		    (uint8_t *)ep->secret_key[(int)ep->current_secret_number],
		    SCTP_SECRET_SIZE, m, cookie_offset, calc_sig, 0);
	}
	/* get the signature */
	SCTP_INP_RUNLOCK(l_inp);
	sig = (uint8_t *)sctp_m_getptr(m_sig, 0, SCTP_SIGNATURE_SIZE, (uint8_t *)&tmp_sig);
	if (sig == NULL) {
		/* couldn't find signature */
		sctp_m_freem(m_sig);
		return (NULL);
	}
	/* compare the received digest with the computed digest */
	if (timingsafe_bcmp(calc_sig, sig, SCTP_SIGNATURE_SIZE) != 0) {
		/* try the old cookie? */
		if ((cookie->time_entered.tv_sec == (long)ep->time_of_secret_change) &&
		    (ep->current_secret_number != ep->last_secret_number)) {
			/* compute digest with old */
			(void)sctp_hmac_m(SCTP_HMAC,
			    (uint8_t *)ep->secret_key[(int)ep->last_secret_number],
			    SCTP_SECRET_SIZE, m, cookie_offset, calc_sig, 0);
			/* compare */
			if (timingsafe_bcmp(calc_sig, sig, SCTP_SIGNATURE_SIZE) == 0)
				cookie_ok = 1;
		}
	} else {
		cookie_ok = 1;
	}

	/*
	 * Now before we continue we must reconstruct our mbuf so that
	 * normal processing of any other chunks will work.
	 */
	{
		struct mbuf *m_at;

		m_at = m;
		while (SCTP_BUF_NEXT(m_at) != NULL) {
			m_at = SCTP_BUF_NEXT(m_at);
		}
		SCTP_BUF_NEXT(m_at) = m_sig;
	}

	if (cookie_ok == 0) {
		SCTPDBG(SCTP_DEBUG_INPUT2, "handle_cookie_echo: cookie signature validation failed!\n");
		SCTPDBG(SCTP_DEBUG_INPUT2,
		    "offset = %u, cookie_offset = %u, sig_offset = %u\n",
		    (uint32_t)offset, cookie_offset, sig_offset);
		return (NULL);
	}

	/*
	 * check the cookie timestamps to be sure it's not stale
	 */
	(void)SCTP_GETTIME_TIMEVAL(&now);
	/* Expire time is in Ticks, so we convert to seconds */
	time_expires.tv_sec = cookie->time_entered.tv_sec + TICKS_TO_SEC(cookie->cookie_life);
	time_expires.tv_usec = cookie->time_entered.tv_usec;
	if (timevalcmp(&now, &time_expires, >)) {
		/* cookie is stale! */
		struct mbuf *op_err;
		struct sctp_error_stale_cookie *cause;
		struct timeval diff;
		uint32_t staleness;

		op_err = sctp_get_mbuf_for_msg(sizeof(struct sctp_error_stale_cookie),
		    0, M_NOWAIT, 1, MT_DATA);
		if (op_err == NULL) {
			/* FOOBAR */
			return (NULL);
		}
		/* Set the len */
		SCTP_BUF_LEN(op_err) = sizeof(struct sctp_error_stale_cookie);
		cause = mtod(op_err, struct sctp_error_stale_cookie *);
		cause->cause.code = htons(SCTP_CAUSE_STALE_COOKIE);
		cause->cause.length = htons((sizeof(struct sctp_paramhdr) +
		    (sizeof(uint32_t))));
		diff = now;
		timevalsub(&diff, &time_expires);
		if ((uint32_t)diff.tv_sec > UINT32_MAX / 1000000) {
			staleness = UINT32_MAX;
		} else {
			staleness = diff.tv_sec * 1000000;
		}
		if (UINT32_MAX - staleness >= (uint32_t)diff.tv_usec) {
			staleness += diff.tv_usec;
		} else {
			staleness = UINT32_MAX;
		}
		cause->stale_time = htonl(staleness);
		sctp_send_operr_to(src, dst, sh, cookie->peers_vtag, op_err,
		    mflowtype, mflowid, l_inp->fibnum,
		    vrf_id, port);
		return (NULL);
	}
	/*
	 * Now we must see with the lookup address if we have an existing
	 * asoc. This will only happen if we were in the COOKIE-WAIT state
	 * and a INIT collided with us and somewhere the peer sent the
	 * cookie on another address besides the single address our assoc
	 * had for him. In this case we will have one of the tie-tags set at
	 * least AND the address field in the cookie can be used to look it
	 * up.
	 */
	to = NULL;
	switch (cookie->addr_type) {
#ifdef INET6
	case SCTP_IPV6_ADDRESS:
		memset(&sin6, 0, sizeof(sin6));
		sin6.sin6_family = AF_INET6;
		sin6.sin6_len = sizeof(sin6);
		sin6.sin6_port = sh->src_port;
		sin6.sin6_scope_id = cookie->scope_id;
		memcpy(&sin6.sin6_addr.s6_addr, cookie->address,
		    sizeof(sin6.sin6_addr.s6_addr));
		to = (struct sockaddr *)&sin6;
		break;
#endif
#ifdef INET
	case SCTP_IPV4_ADDRESS:
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_len = sizeof(sin);
		sin.sin_port = sh->src_port;
		sin.sin_addr.s_addr = cookie->address[0];
		to = (struct sockaddr *)&sin;
		break;
#endif
	default:
		/* This should not happen */
		return (NULL);
	}
	if (*stcb == NULL) {
		/* Yep, lets check */
		*stcb = sctp_findassociation_ep_addr(inp_p, to, netp, dst, NULL);
		if (*stcb == NULL) {
			/*
			 * We should have only got back the same inp. If we
			 * got back a different ep we have a problem. The
			 * original findep got back l_inp and now
			 */
			if (l_inp != *inp_p) {
				SCTP_PRINTF("Bad problem find_ep got a diff inp then special_locate?\n");
			}
		} else {
			if (*locked_tcb == NULL) {
				/*
				 * In this case we found the assoc only
				 * after we locked the create lock. This
				 * means we are in a colliding case and we
				 * must make sure that we unlock the tcb if
				 * its one of the cases where we throw away
				 * the incoming packets.
				 */
				*locked_tcb = *stcb;

				/*
				 * We must also increment the inp ref count
				 * since the ref_count flags was set when we
				 * did not find the TCB, now we found it
				 * which reduces the refcount.. we must
				 * raise it back out to balance it all :-)
				 */
				SCTP_INP_INCR_REF((*stcb)->sctp_ep);
				if ((*stcb)->sctp_ep != l_inp) {
					SCTP_PRINTF("Huh? ep:%p diff then l_inp:%p?\n",
					    (void *)(*stcb)->sctp_ep, (void *)l_inp);
				}
			}
		}
	}

	cookie_len -= SCTP_SIGNATURE_SIZE;
	if (*stcb == NULL) {
		/* this is the "normal" case... get a new TCB */
		*stcb = sctp_process_cookie_new(m, iphlen, offset, src, dst, sh,
		    cookie, cookie_len, *inp_p,
		    netp, to, &notification,
		    auth_skipped, auth_offset, auth_len,
		    mflowtype, mflowid,
		    vrf_id, port);
	} else {
		/* this is abnormal... cookie-echo on existing TCB */
		had_a_existing_tcb = 1;
		*stcb = sctp_process_cookie_existing(m, iphlen, offset,
		    src, dst, sh,
		    cookie, cookie_len, *inp_p, *stcb, netp, to,
		    &notification, auth_skipped, auth_offset, auth_len,
		    mflowtype, mflowid,
		    vrf_id, port);
	}

	if (*stcb == NULL) {
		/* still no TCB... must be bad cookie-echo */
		return (NULL);
	}
	if (*netp != NULL) {
		(*netp)->flowtype = mflowtype;
		(*netp)->flowid = mflowid;
	}
	/*
	 * Ok, we built an association so confirm the address we sent the
	 * INIT-ACK to.
	 */
	netl = sctp_findnet(*stcb, to);
	/*
	 * This code should in theory NOT run but
	 */
	if (netl == NULL) {
		/* TSNH! Huh, why do I need to add this address here? */
		if (sctp_add_remote_addr(*stcb, to, NULL, port,
		    SCTP_DONOT_SETSCOPE, SCTP_IN_COOKIE_PROC)) {
			return (NULL);
		}
		netl = sctp_findnet(*stcb, to);
	}
	if (netl) {
		if (netl->dest_state & SCTP_ADDR_UNCONFIRMED) {
			netl->dest_state &= ~SCTP_ADDR_UNCONFIRMED;
			(void)sctp_set_primary_addr((*stcb), (struct sockaddr *)NULL,
			    netl);
			send_int_conf = 1;
		}
	}
	sctp_start_net_timers(*stcb);
	if ((*inp_p)->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) {
		if (!had_a_existing_tcb ||
		    (((*inp_p)->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) == 0)) {
			/*
			 * If we have a NEW cookie or the connect never
			 * reached the connected state during collision we
			 * must do the TCP accept thing.
			 */
			struct socket *so, *oso;
			struct sctp_inpcb *inp;

			if (notification == SCTP_NOTIFY_ASSOC_RESTART) {
				/*
				 * For a restart we will keep the same
				 * socket, no need to do anything. I THINK!!
				 */
				sctp_ulp_notify(notification, *stcb, 0, NULL, SCTP_SO_NOT_LOCKED);
				if (send_int_conf) {
					sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_CONFIRMED,
					    (*stcb), 0, (void *)netl, SCTP_SO_NOT_LOCKED);
				}
				return (m);
			}
			oso = (*inp_p)->sctp_socket;
			atomic_add_int(&(*stcb)->asoc.refcnt, 1);
			SCTP_TCB_UNLOCK((*stcb));
			CURVNET_SET(oso->so_vnet);
			so = sonewconn(oso, 0
			    );
			CURVNET_RESTORE();
			SCTP_TCB_LOCK((*stcb));
			atomic_subtract_int(&(*stcb)->asoc.refcnt, 1);

			if (so == NULL) {
				struct mbuf *op_err;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
				struct socket *pcb_so;
#endif
				/* Too many sockets */
				SCTPDBG(SCTP_DEBUG_INPUT1, "process_cookie_new: no room for another socket!\n");
				op_err = sctp_generate_cause(SCTP_CAUSE_OUT_OF_RESC, "");
				sctp_abort_association(*inp_p, NULL, m, iphlen,
				    src, dst, sh, op_err,
				    mflowtype, mflowid,
				    vrf_id, port);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
				pcb_so = SCTP_INP_SO(*inp_p);
				atomic_add_int(&(*stcb)->asoc.refcnt, 1);
				SCTP_TCB_UNLOCK((*stcb));
				SCTP_SOCKET_LOCK(pcb_so, 1);
				SCTP_TCB_LOCK((*stcb));
				atomic_subtract_int(&(*stcb)->asoc.refcnt, 1);
#endif
				(void)sctp_free_assoc(*inp_p, *stcb, SCTP_NORMAL_PROC,
				    SCTP_FROM_SCTP_INPUT + SCTP_LOC_23);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
				SCTP_SOCKET_UNLOCK(pcb_so, 1);
#endif
				return (NULL);
			}
			inp = (struct sctp_inpcb *)so->so_pcb;
			SCTP_INP_INCR_REF(inp);
			/*
			 * We add the unbound flag here so that if we get an
			 * soabort() before we get the move_pcb done, we
			 * will properly cleanup.
			 */
			inp->sctp_flags = (SCTP_PCB_FLAGS_TCPTYPE |
			    SCTP_PCB_FLAGS_CONNECTED |
			    SCTP_PCB_FLAGS_IN_TCPPOOL |
			    SCTP_PCB_FLAGS_UNBOUND |
			    (SCTP_PCB_COPY_FLAGS & (*inp_p)->sctp_flags) |
			    SCTP_PCB_FLAGS_DONT_WAKE);
			inp->sctp_features = (*inp_p)->sctp_features;
			inp->sctp_mobility_features = (*inp_p)->sctp_mobility_features;
			inp->sctp_socket = so;
			inp->sctp_frag_point = (*inp_p)->sctp_frag_point;
			inp->max_cwnd = (*inp_p)->max_cwnd;
			inp->sctp_cmt_on_off = (*inp_p)->sctp_cmt_on_off;
			inp->ecn_supported = (*inp_p)->ecn_supported;
			inp->prsctp_supported = (*inp_p)->prsctp_supported;
			inp->auth_supported = (*inp_p)->auth_supported;
			inp->asconf_supported = (*inp_p)->asconf_supported;
			inp->reconfig_supported = (*inp_p)->reconfig_supported;
			inp->nrsack_supported = (*inp_p)->nrsack_supported;
			inp->pktdrop_supported = (*inp_p)->pktdrop_supported;
			inp->partial_delivery_point = (*inp_p)->partial_delivery_point;
			inp->sctp_context = (*inp_p)->sctp_context;
			inp->local_strreset_support = (*inp_p)->local_strreset_support;
			inp->fibnum = (*inp_p)->fibnum;
			inp->inp_starting_point_for_iterator = NULL;
			/*
			 * copy in the authentication parameters from the
			 * original endpoint
			 */
			if (inp->sctp_ep.local_hmacs)
				sctp_free_hmaclist(inp->sctp_ep.local_hmacs);
			inp->sctp_ep.local_hmacs =
			    sctp_copy_hmaclist((*inp_p)->sctp_ep.local_hmacs);
			if (inp->sctp_ep.local_auth_chunks)
				sctp_free_chunklist(inp->sctp_ep.local_auth_chunks);
			inp->sctp_ep.local_auth_chunks =
			    sctp_copy_chunklist((*inp_p)->sctp_ep.local_auth_chunks);

			/*
			 * Now we must move it from one hash table to
			 * another and get the tcb in the right place.
			 */

			/*
			 * This is where the one-2-one socket is put into
			 * the accept state waiting for the accept!
			 */
			if (*stcb) {
				SCTP_ADD_SUBSTATE(*stcb, SCTP_STATE_IN_ACCEPT_QUEUE);
			}
			sctp_move_pcb_and_assoc(*inp_p, inp, *stcb);

			atomic_add_int(&(*stcb)->asoc.refcnt, 1);
			SCTP_TCB_UNLOCK((*stcb));

			sctp_pull_off_control_to_new_inp((*inp_p), inp, *stcb,
			    0);
			SCTP_TCB_LOCK((*stcb));
			atomic_subtract_int(&(*stcb)->asoc.refcnt, 1);


			/*
			 * now we must check to see if we were aborted while
			 * the move was going on and the lock/unlock
			 * happened.
			 */
			if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
				/*
				 * yep it was, we leave the assoc attached
				 * to the socket since the sctp_inpcb_free()
				 * call will send an abort for us.
				 */
				SCTP_INP_DECR_REF(inp);
				return (NULL);
			}
			SCTP_INP_DECR_REF(inp);
			/* Switch over to the new guy */
			*inp_p = inp;
			sctp_ulp_notify(notification, *stcb, 0, NULL, SCTP_SO_NOT_LOCKED);
			if (send_int_conf) {
				sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_CONFIRMED,
				    (*stcb), 0, (void *)netl, SCTP_SO_NOT_LOCKED);
			}

			/*
			 * Pull it from the incomplete queue and wake the
			 * guy
			 */
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
			atomic_add_int(&(*stcb)->asoc.refcnt, 1);
			SCTP_TCB_UNLOCK((*stcb));
			SCTP_SOCKET_LOCK(so, 1);
#endif
			soisconnected(so);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
			SCTP_TCB_LOCK((*stcb));
			atomic_subtract_int(&(*stcb)->asoc.refcnt, 1);
			SCTP_SOCKET_UNLOCK(so, 1);
#endif
			return (m);
		}
	}
	if (notification) {
		sctp_ulp_notify(notification, *stcb, 0, NULL, SCTP_SO_NOT_LOCKED);
	}
	if (send_int_conf) {
		sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_CONFIRMED,
		    (*stcb), 0, (void *)netl, SCTP_SO_NOT_LOCKED);
	}
	return (m);
}

static void
sctp_handle_cookie_ack(struct sctp_cookie_ack_chunk *cp SCTP_UNUSED,
    struct sctp_tcb *stcb, struct sctp_nets *net)
{
	/* cp must not be used, others call this without a c-ack :-) */
	struct sctp_association *asoc;

	SCTPDBG(SCTP_DEBUG_INPUT2,
	    "sctp_handle_cookie_ack: handling COOKIE-ACK\n");
	if ((stcb == NULL) || (net == NULL)) {
		return;
	}

	asoc = &stcb->asoc;
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_THRESHOLD_LOGGING) {
		sctp_misc_ints(SCTP_THRESHOLD_CLEAR,
		    asoc->overall_error_count,
		    0,
		    SCTP_FROM_SCTP_INPUT,
		    __LINE__);
	}
	asoc->overall_error_count = 0;
	sctp_stop_all_cookie_timers(stcb);
	/* process according to association state */
	if (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED) {
		/* state change only needed when I am in right state */
		SCTPDBG(SCTP_DEBUG_INPUT2, "moving to OPEN state\n");
		SCTP_SET_STATE(stcb, SCTP_STATE_OPEN);
		sctp_start_net_timers(stcb);
		if (asoc->state & SCTP_STATE_SHUTDOWN_PENDING) {
			sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD,
			    stcb->sctp_ep, stcb, asoc->primary_destination);

		}
		/* update RTO */
		SCTP_STAT_INCR_COUNTER32(sctps_activeestab);
		SCTP_STAT_INCR_GAUGE32(sctps_currestab);
		if (asoc->overall_error_count == 0) {
			net->RTO = sctp_calculate_rto(stcb, asoc, net,
			    &asoc->time_entered,
			    SCTP_RTT_FROM_NON_DATA);
		}
		(void)SCTP_GETTIME_TIMEVAL(&asoc->time_entered);
		sctp_ulp_notify(SCTP_NOTIFY_ASSOC_UP, stcb, 0, NULL, SCTP_SO_NOT_LOCKED);
		if ((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
		    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) {
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
			struct socket *so;

#endif
			stcb->sctp_ep->sctp_flags |= SCTP_PCB_FLAGS_CONNECTED;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
			so = SCTP_INP_SO(stcb->sctp_ep);
			atomic_add_int(&stcb->asoc.refcnt, 1);
			SCTP_TCB_UNLOCK(stcb);
			SCTP_SOCKET_LOCK(so, 1);
			SCTP_TCB_LOCK(stcb);
			atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
			if ((stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET) == 0) {
				soisconnected(stcb->sctp_socket);
			}
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
			SCTP_SOCKET_UNLOCK(so, 1);
#endif
		}
		/*
		 * since we did not send a HB make sure we don't double
		 * things
		 */
		net->hb_responded = 1;

		if (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET) {
			/*
			 * We don't need to do the asconf thing, nor hb or
			 * autoclose if the socket is closed.
			 */
			goto closed_socket;
		}

		sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep,
		    stcb, net);


		if (stcb->asoc.sctp_autoclose_ticks &&
		    sctp_is_feature_on(stcb->sctp_ep, SCTP_PCB_FLAGS_AUTOCLOSE)) {
			sctp_timer_start(SCTP_TIMER_TYPE_AUTOCLOSE,
			    stcb->sctp_ep, stcb, NULL);
		}
		/*
		 * send ASCONF if parameters are pending and ASCONFs are
		 * allowed (eg. addresses changed when init/cookie echo were
		 * in flight)
		 */
		if ((sctp_is_feature_on(stcb->sctp_ep, SCTP_PCB_FLAGS_DO_ASCONF)) &&
		    (stcb->asoc.asconf_supported == 1) &&
		    (!TAILQ_EMPTY(&stcb->asoc.asconf_queue))) {
#ifdef SCTP_TIMER_BASED_ASCONF
			sctp_timer_start(SCTP_TIMER_TYPE_ASCONF,
			    stcb->sctp_ep, stcb,
			    stcb->asoc.primary_destination);
#else
			sctp_send_asconf(stcb, stcb->asoc.primary_destination,
			    SCTP_ADDR_NOT_LOCKED);
#endif
		}
	}
closed_socket:
	/* Toss the cookie if I can */
	sctp_toss_old_cookies(stcb, asoc);
	if (!TAILQ_EMPTY(&asoc->sent_queue)) {
		/* Restart the timer if we have pending data */
		struct sctp_tmit_chunk *chk;

		chk = TAILQ_FIRST(&asoc->sent_queue);
		sctp_timer_start(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep, stcb, chk->whoTo);
	}
}

static void
sctp_handle_ecn_echo(struct sctp_ecne_chunk *cp,
    struct sctp_tcb *stcb)
{
	struct sctp_nets *net;
	struct sctp_tmit_chunk *lchk;
	struct sctp_ecne_chunk bkup;
	uint8_t override_bit;
	uint32_t tsn, window_data_tsn;
	int len;
	unsigned int pkt_cnt;

	len = ntohs(cp->ch.chunk_length);
	if ((len != sizeof(struct sctp_ecne_chunk)) &&
	    (len != sizeof(struct old_sctp_ecne_chunk))) {
		return;
	}
	if (len == sizeof(struct old_sctp_ecne_chunk)) {
		/* Its the old format */
		memcpy(&bkup, cp, sizeof(struct old_sctp_ecne_chunk));
		bkup.num_pkts_since_cwr = htonl(1);
		cp = &bkup;
	}
	SCTP_STAT_INCR(sctps_recvecne);
	tsn = ntohl(cp->tsn);
	pkt_cnt = ntohl(cp->num_pkts_since_cwr);
	lchk = TAILQ_LAST(&stcb->asoc.send_queue, sctpchunk_listhead);
	if (lchk == NULL) {
		window_data_tsn = stcb->asoc.sending_seq - 1;
	} else {
		window_data_tsn = lchk->rec.data.tsn;
	}

	/* Find where it was sent to if possible. */
	net = NULL;
	TAILQ_FOREACH(lchk, &stcb->asoc.sent_queue, sctp_next) {
		if (lchk->rec.data.tsn == tsn) {
			net = lchk->whoTo;
			net->ecn_prev_cwnd = lchk->rec.data.cwnd_at_send;
			break;
		}
		if (SCTP_TSN_GT(lchk->rec.data.tsn, tsn)) {
			break;
		}
	}
	if (net == NULL) {
		/*
		 * What to do. A previous send of a CWR was possibly lost.
		 * See how old it is, we may have it marked on the actual
		 * net.
		 */
		TAILQ_FOREACH(net, &stcb->asoc.nets, sctp_next) {
			if (tsn == net->last_cwr_tsn) {
				/* Found him, send it off */
				break;
			}
		}
		if (net == NULL) {
			/*
			 * If we reach here, we need to send a special CWR
			 * that says hey, we did this a long time ago and
			 * you lost the response.
			 */
			net = TAILQ_FIRST(&stcb->asoc.nets);
			if (net == NULL) {
				/* TSNH */
				return;
			}
			override_bit = SCTP_CWR_REDUCE_OVERRIDE;
		} else {
			override_bit = 0;
		}
	} else {
		override_bit = 0;
	}
	if (SCTP_TSN_GT(tsn, net->cwr_window_tsn) &&
	    ((override_bit & SCTP_CWR_REDUCE_OVERRIDE) == 0)) {
		/*
		 * JRS - Use the congestion control given in the pluggable
		 * CC module
		 */
		stcb->asoc.cc_functions.sctp_cwnd_update_after_ecn_echo(stcb, net, 0, pkt_cnt);
		/*
		 * We reduce once every RTT. So we will only lower cwnd at
		 * the next sending seq i.e. the window_data_tsn
		 */
		net->cwr_window_tsn = window_data_tsn;
		net->ecn_ce_pkt_cnt += pkt_cnt;
		net->lost_cnt = pkt_cnt;
		net->last_cwr_tsn = tsn;
	} else {
		override_bit |= SCTP_CWR_IN_SAME_WINDOW;
		if (SCTP_TSN_GT(tsn, net->last_cwr_tsn) &&
		    ((override_bit & SCTP_CWR_REDUCE_OVERRIDE) == 0)) {
			/*
			 * Another loss in the same window update how many
			 * marks/packets lost we have had.
			 */
			int cnt = 1;

			if (pkt_cnt > net->lost_cnt) {
				/* Should be the case */
				cnt = (pkt_cnt - net->lost_cnt);
				net->ecn_ce_pkt_cnt += cnt;
			}
			net->lost_cnt = pkt_cnt;
			net->last_cwr_tsn = tsn;
			/*
			 * Most CC functions will ignore this call, since we
			 * are in-window yet of the initial CE the peer saw.
			 */
			stcb->asoc.cc_functions.sctp_cwnd_update_after_ecn_echo(stcb, net, 1, cnt);
		}
	}
	/*
	 * We always send a CWR this way if our previous one was lost our
	 * peer will get an update, or if it is not time again to reduce we
	 * still get the cwr to the peer. Note we set the override when we
	 * could not find the TSN on the chunk or the destination network.
	 */
	sctp_send_cwr(stcb, net, net->last_cwr_tsn, override_bit);
}

static void
sctp_handle_ecn_cwr(struct sctp_cwr_chunk *cp, struct sctp_tcb *stcb, struct sctp_nets *net)
{
	/*
	 * Here we get a CWR from the peer. We must look in the outqueue and
	 * make sure that we have a covered ECNE in the control chunk part.
	 * If so remove it.
	 */
	struct sctp_tmit_chunk *chk, *nchk;
	struct sctp_ecne_chunk *ecne;
	int override;
	uint32_t cwr_tsn;

	cwr_tsn = ntohl(cp->tsn);
	override = cp->ch.chunk_flags & SCTP_CWR_REDUCE_OVERRIDE;
	TAILQ_FOREACH_SAFE(chk, &stcb->asoc.control_send_queue, sctp_next, nchk) {
		if (chk->rec.chunk_id.id != SCTP_ECN_ECHO) {
			continue;
		}
		if ((override == 0) && (chk->whoTo != net)) {
			/* Must be from the right src unless override is set */
			continue;
		}
		ecne = mtod(chk->data, struct sctp_ecne_chunk *);
		if (SCTP_TSN_GE(cwr_tsn, ntohl(ecne->tsn))) {
			/* this covers this ECNE, we can remove it */
			stcb->asoc.ecn_echo_cnt_onq--;
			TAILQ_REMOVE(&stcb->asoc.control_send_queue, chk,
			    sctp_next);
			stcb->asoc.ctrl_queue_cnt--;
			sctp_m_freem(chk->data);
			chk->data = NULL;
			sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
			if (override == 0) {
				break;
			}
		}
	}
}

static void
sctp_handle_shutdown_complete(struct sctp_shutdown_complete_chunk *cp SCTP_UNUSED,
    struct sctp_tcb *stcb, struct sctp_nets *net)
{
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;
#endif

	SCTPDBG(SCTP_DEBUG_INPUT2,
	    "sctp_handle_shutdown_complete: handling SHUTDOWN-COMPLETE\n");
	if (stcb == NULL)
		return;

	/* process according to association state */
	if (SCTP_GET_STATE(stcb) != SCTP_STATE_SHUTDOWN_ACK_SENT) {
		/* unexpected SHUTDOWN-COMPLETE... so ignore... */
		SCTPDBG(SCTP_DEBUG_INPUT2,
		    "sctp_handle_shutdown_complete: not in SCTP_STATE_SHUTDOWN_ACK_SENT --- ignore\n");
		SCTP_TCB_UNLOCK(stcb);
		return;
	}
	/* notify upper layer protocol */
	if (stcb->sctp_socket) {
		sctp_ulp_notify(SCTP_NOTIFY_ASSOC_DOWN, stcb, 0, NULL, SCTP_SO_NOT_LOCKED);
	}
#ifdef INVARIANTS
	if (!TAILQ_EMPTY(&stcb->asoc.send_queue) ||
	    !TAILQ_EMPTY(&stcb->asoc.sent_queue) ||
	    sctp_is_there_unsent_data(stcb, SCTP_SO_NOT_LOCKED)) {
		panic("Queues are not empty when handling SHUTDOWN-COMPLETE");
	}
#endif
	/* stop the timer */
	sctp_timer_stop(SCTP_TIMER_TYPE_SHUTDOWNACK, stcb->sctp_ep, stcb, net,
	    SCTP_FROM_SCTP_INPUT + SCTP_LOC_24);
	SCTP_STAT_INCR_COUNTER32(sctps_shutdown);
	/* free the TCB */
	SCTPDBG(SCTP_DEBUG_INPUT2,
	    "sctp_handle_shutdown_complete: calls free-asoc\n");
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	so = SCTP_INP_SO(stcb->sctp_ep);
	atomic_add_int(&stcb->asoc.refcnt, 1);
	SCTP_TCB_UNLOCK(stcb);
	SCTP_SOCKET_LOCK(so, 1);
	SCTP_TCB_LOCK(stcb);
	atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
	(void)sctp_free_assoc(stcb->sctp_ep, stcb, SCTP_NORMAL_PROC,
	    SCTP_FROM_SCTP_INPUT + SCTP_LOC_25);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	SCTP_SOCKET_UNLOCK(so, 1);
#endif
	return;
}

static int
process_chunk_drop(struct sctp_tcb *stcb, struct sctp_chunk_desc *desc,
    struct sctp_nets *net, uint8_t flg)
{
	switch (desc->chunk_type) {
	case SCTP_DATA:
		/* find the tsn to resend (possibly */
		{
			uint32_t tsn;
			struct sctp_tmit_chunk *tp1;

			tsn = ntohl(desc->tsn_ifany);
			TAILQ_FOREACH(tp1, &stcb->asoc.sent_queue, sctp_next) {
				if (tp1->rec.data.tsn == tsn) {
					/* found it */
					break;
				}
				if (SCTP_TSN_GT(tp1->rec.data.tsn, tsn)) {
					/* not found */
					tp1 = NULL;
					break;
				}
			}
			if (tp1 == NULL) {
				/*
				 * Do it the other way , aka without paying
				 * attention to queue seq order.
				 */
				SCTP_STAT_INCR(sctps_pdrpdnfnd);
				TAILQ_FOREACH(tp1, &stcb->asoc.sent_queue, sctp_next) {
					if (tp1->rec.data.tsn == tsn) {
						/* found it */
						break;
					}
				}
			}
			if (tp1 == NULL) {
				SCTP_STAT_INCR(sctps_pdrptsnnf);
			}
			if ((tp1) && (tp1->sent < SCTP_DATAGRAM_ACKED)) {
				uint8_t *ddp;

				if (((flg & SCTP_BADCRC) == 0) &&
				    ((flg & SCTP_FROM_MIDDLE_BOX) == 0)) {
					return (0);
				}
				if ((stcb->asoc.peers_rwnd == 0) &&
				    ((flg & SCTP_FROM_MIDDLE_BOX) == 0)) {
					SCTP_STAT_INCR(sctps_pdrpdiwnp);
					return (0);
				}
				if (stcb->asoc.peers_rwnd == 0 &&
				    (flg & SCTP_FROM_MIDDLE_BOX)) {
					SCTP_STAT_INCR(sctps_pdrpdizrw);
					return (0);
				}
				ddp = (uint8_t *)(mtod(tp1->data, caddr_t)+
				    sizeof(struct sctp_data_chunk));
				{
					unsigned int iii;

					for (iii = 0; iii < sizeof(desc->data_bytes);
					    iii++) {
						if (ddp[iii] != desc->data_bytes[iii]) {
							SCTP_STAT_INCR(sctps_pdrpbadd);
							return (-1);
						}
					}
				}

				if (tp1->do_rtt) {
					/*
					 * this guy had a RTO calculation
					 * pending on it, cancel it
					 */
					if (tp1->whoTo->rto_needed == 0) {
						tp1->whoTo->rto_needed = 1;
					}
					tp1->do_rtt = 0;
				}
				SCTP_STAT_INCR(sctps_pdrpmark);
				if (tp1->sent != SCTP_DATAGRAM_RESEND)
					sctp_ucount_incr(stcb->asoc.sent_queue_retran_cnt);
				/*
				 * mark it as if we were doing a FR, since
				 * we will be getting gap ack reports behind
				 * the info from the router.
				 */
				tp1->rec.data.doing_fast_retransmit = 1;
				/*
				 * mark the tsn with what sequences can
				 * cause a new FR.
				 */
				if (TAILQ_EMPTY(&stcb->asoc.send_queue)) {
					tp1->rec.data.fast_retran_tsn = stcb->asoc.sending_seq;
				} else {
					tp1->rec.data.fast_retran_tsn = (TAILQ_FIRST(&stcb->asoc.send_queue))->rec.data.tsn;
				}

				/* restart the timer */
				sctp_timer_stop(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep,
				    stcb, tp1->whoTo,
				    SCTP_FROM_SCTP_INPUT + SCTP_LOC_26);
				sctp_timer_start(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep,
				    stcb, tp1->whoTo);

				/* fix counts and things */
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FLIGHT_LOGGING_ENABLE) {
					sctp_misc_ints(SCTP_FLIGHT_LOG_DOWN_PDRP,
					    tp1->whoTo->flight_size,
					    tp1->book_size,
					    (uint32_t)(uintptr_t)stcb,
					    tp1->rec.data.tsn);
				}
				if (tp1->sent < SCTP_DATAGRAM_RESEND) {
					sctp_flight_size_decrease(tp1);
					sctp_total_flight_decrease(stcb, tp1);
				}
				tp1->sent = SCTP_DATAGRAM_RESEND;
			} {
				/* audit code */
				unsigned int audit;

				audit = 0;
				TAILQ_FOREACH(tp1, &stcb->asoc.sent_queue, sctp_next) {
					if (tp1->sent == SCTP_DATAGRAM_RESEND)
						audit++;
				}
				TAILQ_FOREACH(tp1, &stcb->asoc.control_send_queue,
				    sctp_next) {
					if (tp1->sent == SCTP_DATAGRAM_RESEND)
						audit++;
				}
				if (audit != stcb->asoc.sent_queue_retran_cnt) {
					SCTP_PRINTF("**Local Audit finds cnt:%d asoc cnt:%d\n",
					    audit, stcb->asoc.sent_queue_retran_cnt);
#ifndef SCTP_AUDITING_ENABLED
					stcb->asoc.sent_queue_retran_cnt = audit;
#endif
				}
			}
		}
		break;
	case SCTP_ASCONF:
		{
			struct sctp_tmit_chunk *asconf;

			TAILQ_FOREACH(asconf, &stcb->asoc.control_send_queue,
			    sctp_next) {
				if (asconf->rec.chunk_id.id == SCTP_ASCONF) {
					break;
				}
			}
			if (asconf) {
				if (asconf->sent != SCTP_DATAGRAM_RESEND)
					sctp_ucount_incr(stcb->asoc.sent_queue_retran_cnt);
				asconf->sent = SCTP_DATAGRAM_RESEND;
				asconf->snd_count--;
			}
		}
		break;
	case SCTP_INITIATION:
		/* resend the INIT */
		stcb->asoc.dropped_special_cnt++;
		if (stcb->asoc.dropped_special_cnt < SCTP_RETRY_DROPPED_THRESH) {
			/*
			 * If we can get it in, in a few attempts we do
			 * this, otherwise we let the timer fire.
			 */
			sctp_timer_stop(SCTP_TIMER_TYPE_INIT, stcb->sctp_ep,
			    stcb, net,
			    SCTP_FROM_SCTP_INPUT + SCTP_LOC_27);
			sctp_send_initiate(stcb->sctp_ep, stcb, SCTP_SO_NOT_LOCKED);
		}
		break;
	case SCTP_SELECTIVE_ACK:
	case SCTP_NR_SELECTIVE_ACK:
		/* resend the sack */
		sctp_send_sack(stcb, SCTP_SO_NOT_LOCKED);
		break;
	case SCTP_HEARTBEAT_REQUEST:
		/* resend a demand HB */
		if ((stcb->asoc.overall_error_count + 3) < stcb->asoc.max_send_times) {
			/*
			 * Only retransmit if we KNOW we wont destroy the
			 * tcb
			 */
			sctp_send_hb(stcb, net, SCTP_SO_NOT_LOCKED);
		}
		break;
	case SCTP_SHUTDOWN:
		sctp_send_shutdown(stcb, net);
		break;
	case SCTP_SHUTDOWN_ACK:
		sctp_send_shutdown_ack(stcb, net);
		break;
	case SCTP_COOKIE_ECHO:
		{
			struct sctp_tmit_chunk *cookie;

			cookie = NULL;
			TAILQ_FOREACH(cookie, &stcb->asoc.control_send_queue,
			    sctp_next) {
				if (cookie->rec.chunk_id.id == SCTP_COOKIE_ECHO) {
					break;
				}
			}
			if (cookie) {
				if (cookie->sent != SCTP_DATAGRAM_RESEND)
					sctp_ucount_incr(stcb->asoc.sent_queue_retran_cnt);
				cookie->sent = SCTP_DATAGRAM_RESEND;
				sctp_stop_all_cookie_timers(stcb);
			}
		}
		break;
	case SCTP_COOKIE_ACK:
		sctp_send_cookie_ack(stcb);
		break;
	case SCTP_ASCONF_ACK:
		/* resend last asconf ack */
		sctp_send_asconf_ack(stcb);
		break;
	case SCTP_IFORWARD_CUM_TSN:
	case SCTP_FORWARD_CUM_TSN:
		send_forward_tsn(stcb, &stcb->asoc);
		break;
		/* can't do anything with these */
	case SCTP_PACKET_DROPPED:
	case SCTP_INITIATION_ACK:	/* this should not happen */
	case SCTP_HEARTBEAT_ACK:
	case SCTP_ABORT_ASSOCIATION:
	case SCTP_OPERATION_ERROR:
	case SCTP_SHUTDOWN_COMPLETE:
	case SCTP_ECN_ECHO:
	case SCTP_ECN_CWR:
	default:
		break;
	}
	return (0);
}

void
sctp_reset_in_stream(struct sctp_tcb *stcb, uint32_t number_entries, uint16_t *list)
{
	uint32_t i;
	uint16_t temp;

	/*
	 * We set things to 0xffffffff since this is the last delivered
	 * sequence and we will be sending in 0 after the reset.
	 */

	if (number_entries) {
		for (i = 0; i < number_entries; i++) {
			temp = ntohs(list[i]);
			if (temp >= stcb->asoc.streamincnt) {
				continue;
			}
			stcb->asoc.strmin[temp].last_mid_delivered = 0xffffffff;
		}
	} else {
		list = NULL;
		for (i = 0; i < stcb->asoc.streamincnt; i++) {
			stcb->asoc.strmin[i].last_mid_delivered = 0xffffffff;
		}
	}
	sctp_ulp_notify(SCTP_NOTIFY_STR_RESET_RECV, stcb, number_entries, (void *)list, SCTP_SO_NOT_LOCKED);
}

static void
sctp_reset_out_streams(struct sctp_tcb *stcb, uint32_t number_entries, uint16_t *list)
{
	uint32_t i;
	uint16_t temp;

	if (number_entries > 0) {
		for (i = 0; i < number_entries; i++) {
			temp = ntohs(list[i]);
			if (temp >= stcb->asoc.streamoutcnt) {
				/* no such stream */
				continue;
			}
			stcb->asoc.strmout[temp].next_mid_ordered = 0;
			stcb->asoc.strmout[temp].next_mid_unordered = 0;
		}
	} else {
		for (i = 0; i < stcb->asoc.streamoutcnt; i++) {
			stcb->asoc.strmout[i].next_mid_ordered = 0;
			stcb->asoc.strmout[i].next_mid_unordered = 0;
		}
	}
	sctp_ulp_notify(SCTP_NOTIFY_STR_RESET_SEND, stcb, number_entries, (void *)list, SCTP_SO_NOT_LOCKED);
}

static void
sctp_reset_clear_pending(struct sctp_tcb *stcb, uint32_t number_entries, uint16_t *list)
{
	uint32_t i;
	uint16_t temp;

	if (number_entries > 0) {
		for (i = 0; i < number_entries; i++) {
			temp = ntohs(list[i]);
			if (temp >= stcb->asoc.streamoutcnt) {
				/* no such stream */
				continue;
			}
			stcb->asoc.strmout[temp].state = SCTP_STREAM_OPEN;
		}
	} else {
		for (i = 0; i < stcb->asoc.streamoutcnt; i++) {
			stcb->asoc.strmout[i].state = SCTP_STREAM_OPEN;
		}
	}
}


struct sctp_stream_reset_request *
sctp_find_stream_reset(struct sctp_tcb *stcb, uint32_t seq, struct sctp_tmit_chunk **bchk)
{
	struct sctp_association *asoc;
	struct sctp_chunkhdr *ch;
	struct sctp_stream_reset_request *r;
	struct sctp_tmit_chunk *chk;
	int len, clen;

	asoc = &stcb->asoc;
	if (TAILQ_EMPTY(&stcb->asoc.control_send_queue)) {
		asoc->stream_reset_outstanding = 0;
		return (NULL);
	}
	if (stcb->asoc.str_reset == NULL) {
		asoc->stream_reset_outstanding = 0;
		return (NULL);
	}
	chk = stcb->asoc.str_reset;
	if (chk->data == NULL) {
		return (NULL);
	}
	if (bchk) {
		/* he wants a copy of the chk pointer */
		*bchk = chk;
	}
	clen = chk->send_size;
	ch = mtod(chk->data, struct sctp_chunkhdr *);
	r = (struct sctp_stream_reset_request *)(ch + 1);
	if (ntohl(r->request_seq) == seq) {
		/* found it */
		return (r);
	}
	len = SCTP_SIZE32(ntohs(r->ph.param_length));
	if (clen > (len + (int)sizeof(struct sctp_chunkhdr))) {
		/* move to the next one, there can only be a max of two */
		r = (struct sctp_stream_reset_request *)((caddr_t)r + len);
		if (ntohl(r->request_seq) == seq) {
			return (r);
		}
	}
	/* that seq is not here */
	return (NULL);
}

static void
sctp_clean_up_stream_reset(struct sctp_tcb *stcb)
{
	struct sctp_association *asoc;
	struct sctp_tmit_chunk *chk;

	asoc = &stcb->asoc;
	chk = asoc->str_reset;
	if (chk == NULL) {
		return;
	}
	asoc->str_reset = NULL;
	sctp_timer_stop(SCTP_TIMER_TYPE_STRRESET, stcb->sctp_ep, stcb,
	    chk->whoTo, SCTP_FROM_SCTP_INPUT + SCTP_LOC_28);
	TAILQ_REMOVE(&asoc->control_send_queue, chk, sctp_next);
	asoc->ctrl_queue_cnt--;
	if (chk->data) {
		sctp_m_freem(chk->data);
		chk->data = NULL;
	}
	sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
}


static int
sctp_handle_stream_reset_response(struct sctp_tcb *stcb,
    uint32_t seq, uint32_t action,
    struct sctp_stream_reset_response *respin)
{
	uint16_t type;
	int lparam_len;
	struct sctp_association *asoc = &stcb->asoc;
	struct sctp_tmit_chunk *chk;
	struct sctp_stream_reset_request *req_param;
	struct sctp_stream_reset_out_request *req_out_param;
	struct sctp_stream_reset_in_request *req_in_param;
	uint32_t number_entries;

	if (asoc->stream_reset_outstanding == 0) {
		/* duplicate */
		return (0);
	}
	if (seq == stcb->asoc.str_reset_seq_out) {
		req_param = sctp_find_stream_reset(stcb, seq, &chk);
		if (req_param != NULL) {
			stcb->asoc.str_reset_seq_out++;
			type = ntohs(req_param->ph.param_type);
			lparam_len = ntohs(req_param->ph.param_length);
			if (type == SCTP_STR_RESET_OUT_REQUEST) {
				int no_clear = 0;

				req_out_param = (struct sctp_stream_reset_out_request *)req_param;
				number_entries = (lparam_len - sizeof(struct sctp_stream_reset_out_request)) / sizeof(uint16_t);
				asoc->stream_reset_out_is_outstanding = 0;
				if (asoc->stream_reset_outstanding)
					asoc->stream_reset_outstanding--;
				if (action == SCTP_STREAM_RESET_RESULT_PERFORMED) {
					/* do it */
					sctp_reset_out_streams(stcb, number_entries, req_out_param->list_of_streams);
				} else if (action == SCTP_STREAM_RESET_RESULT_DENIED) {
					sctp_ulp_notify(SCTP_NOTIFY_STR_RESET_DENIED_OUT, stcb, number_entries, req_out_param->list_of_streams, SCTP_SO_NOT_LOCKED);
				} else if (action == SCTP_STREAM_RESET_RESULT_IN_PROGRESS) {
					/*
					 * Set it up so we don't stop
					 * retransmitting
					 */
					asoc->stream_reset_outstanding++;
					stcb->asoc.str_reset_seq_out--;
					asoc->stream_reset_out_is_outstanding = 1;
					no_clear = 1;
				} else {
					sctp_ulp_notify(SCTP_NOTIFY_STR_RESET_FAILED_OUT, stcb, number_entries, req_out_param->list_of_streams, SCTP_SO_NOT_LOCKED);
				}
				if (no_clear == 0) {
					sctp_reset_clear_pending(stcb, number_entries, req_out_param->list_of_streams);
				}
			} else if (type == SCTP_STR_RESET_IN_REQUEST) {
				req_in_param = (struct sctp_stream_reset_in_request *)req_param;
				number_entries = (lparam_len - sizeof(struct sctp_stream_reset_in_request)) / sizeof(uint16_t);
				if (asoc->stream_reset_outstanding)
					asoc->stream_reset_outstanding--;
				if (action == SCTP_STREAM_RESET_RESULT_DENIED) {
					sctp_ulp_notify(SCTP_NOTIFY_STR_RESET_DENIED_IN, stcb,
					    number_entries, req_in_param->list_of_streams, SCTP_SO_NOT_LOCKED);
				} else if (action != SCTP_STREAM_RESET_RESULT_PERFORMED) {
					sctp_ulp_notify(SCTP_NOTIFY_STR_RESET_FAILED_IN, stcb,
					    number_entries, req_in_param->list_of_streams, SCTP_SO_NOT_LOCKED);
				}
			} else if (type == SCTP_STR_RESET_ADD_OUT_STREAMS) {
				/* Ok we now may have more streams */
				int num_stream;

				num_stream = stcb->asoc.strm_pending_add_size;
				if (num_stream > (stcb->asoc.strm_realoutsize - stcb->asoc.streamoutcnt)) {
					/* TSNH */
					num_stream = stcb->asoc.strm_realoutsize - stcb->asoc.streamoutcnt;
				}
				stcb->asoc.strm_pending_add_size = 0;
				if (asoc->stream_reset_outstanding)
					asoc->stream_reset_outstanding--;
				if (action == SCTP_STREAM_RESET_RESULT_PERFORMED) {
					/* Put the new streams into effect */
					int i;

					for (i = asoc->streamoutcnt; i < (asoc->streamoutcnt + num_stream); i++) {
						asoc->strmout[i].state = SCTP_STREAM_OPEN;
					}
					asoc->streamoutcnt += num_stream;
					sctp_notify_stream_reset_add(stcb, stcb->asoc.streamincnt, stcb->asoc.streamoutcnt, 0);
				} else if (action == SCTP_STREAM_RESET_RESULT_DENIED) {
					sctp_notify_stream_reset_add(stcb, stcb->asoc.streamincnt, stcb->asoc.streamoutcnt,
					    SCTP_STREAM_CHANGE_DENIED);
				} else {
					sctp_notify_stream_reset_add(stcb, stcb->asoc.streamincnt, stcb->asoc.streamoutcnt,
					    SCTP_STREAM_CHANGE_FAILED);
				}
			} else if (type == SCTP_STR_RESET_ADD_IN_STREAMS) {
				if (asoc->stream_reset_outstanding)
					asoc->stream_reset_outstanding--;
				if (action == SCTP_STREAM_RESET_RESULT_DENIED) {
					sctp_notify_stream_reset_add(stcb, stcb->asoc.streamincnt, stcb->asoc.streamoutcnt,
					    SCTP_STREAM_CHANGE_DENIED);
				} else if (action != SCTP_STREAM_RESET_RESULT_PERFORMED) {
					sctp_notify_stream_reset_add(stcb, stcb->asoc.streamincnt, stcb->asoc.streamoutcnt,
					    SCTP_STREAM_CHANGE_FAILED);
				}
			} else if (type == SCTP_STR_RESET_TSN_REQUEST) {
				/**
				 * a) Adopt the new in tsn.
				 * b) reset the map
				 * c) Adopt the new out-tsn
				 */
				struct sctp_stream_reset_response_tsn *resp;
				struct sctp_forward_tsn_chunk fwdtsn;
				int abort_flag = 0;

				if (respin == NULL) {
					/* huh ? */
					return (0);
				}
				if (ntohs(respin->ph.param_length) < sizeof(struct sctp_stream_reset_response_tsn)) {
					return (0);
				}
				if (action == SCTP_STREAM_RESET_RESULT_PERFORMED) {
					resp = (struct sctp_stream_reset_response_tsn *)respin;
					asoc->stream_reset_outstanding--;
					fwdtsn.ch.chunk_length = htons(sizeof(struct sctp_forward_tsn_chunk));
					fwdtsn.ch.chunk_type = SCTP_FORWARD_CUM_TSN;
					fwdtsn.new_cumulative_tsn = htonl(ntohl(resp->senders_next_tsn) - 1);
					sctp_handle_forward_tsn(stcb, &fwdtsn, &abort_flag, NULL, 0);
					if (abort_flag) {
						return (1);
					}
					stcb->asoc.highest_tsn_inside_map = (ntohl(resp->senders_next_tsn) - 1);
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MAP_LOGGING_ENABLE) {
						sctp_log_map(0, 7, asoc->highest_tsn_inside_map, SCTP_MAP_SLIDE_RESULT);
					}

					stcb->asoc.tsn_last_delivered = stcb->asoc.cumulative_tsn = stcb->asoc.highest_tsn_inside_map;
					stcb->asoc.mapping_array_base_tsn = ntohl(resp->senders_next_tsn);
					memset(stcb->asoc.mapping_array, 0, stcb->asoc.mapping_array_size);

					stcb->asoc.highest_tsn_inside_nr_map = stcb->asoc.highest_tsn_inside_map;
					memset(stcb->asoc.nr_mapping_array, 0, stcb->asoc.mapping_array_size);

					stcb->asoc.sending_seq = ntohl(resp->receivers_next_tsn);
					stcb->asoc.last_acked_seq = stcb->asoc.cumulative_tsn;

					sctp_reset_out_streams(stcb, 0, (uint16_t *)NULL);
					sctp_reset_in_stream(stcb, 0, (uint16_t *)NULL);
					sctp_notify_stream_reset_tsn(stcb, stcb->asoc.sending_seq, (stcb->asoc.mapping_array_base_tsn + 1), 0);
				} else if (action == SCTP_STREAM_RESET_RESULT_DENIED) {
					sctp_notify_stream_reset_tsn(stcb, stcb->asoc.sending_seq, (stcb->asoc.mapping_array_base_tsn + 1),
					    SCTP_ASSOC_RESET_DENIED);
				} else {
					sctp_notify_stream_reset_tsn(stcb, stcb->asoc.sending_seq, (stcb->asoc.mapping_array_base_tsn + 1),
					    SCTP_ASSOC_RESET_FAILED);
				}
			}
			/* get rid of the request and get the request flags */
			if (asoc->stream_reset_outstanding == 0) {
				sctp_clean_up_stream_reset(stcb);
			}
		}
	}
	if (asoc->stream_reset_outstanding == 0) {
		sctp_send_stream_reset_out_if_possible(stcb, SCTP_SO_NOT_LOCKED);
	}
	return (0);
}

static void
sctp_handle_str_reset_request_in(struct sctp_tcb *stcb,
    struct sctp_tmit_chunk *chk,
    struct sctp_stream_reset_in_request *req, int trunc)
{
	uint32_t seq;
	int len, i;
	int number_entries;
	uint16_t temp;

	/*
	 * peer wants me to send a str-reset to him for my outgoing seq's if
	 * seq_in is right.
	 */
	struct sctp_association *asoc = &stcb->asoc;

	seq = ntohl(req->request_seq);
	if (asoc->str_reset_seq_in == seq) {
		asoc->last_reset_action[1] = asoc->last_reset_action[0];
		if (!(asoc->local_strreset_support & SCTP_ENABLE_RESET_STREAM_REQ)) {
			asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_DENIED;
		} else if (trunc) {
			/* Can't do it, since they exceeded our buffer size  */
			asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_DENIED;
		} else if (stcb->asoc.stream_reset_out_is_outstanding == 0) {
			len = ntohs(req->ph.param_length);
			number_entries = ((len - sizeof(struct sctp_stream_reset_in_request)) / sizeof(uint16_t));
			if (number_entries) {
				for (i = 0; i < number_entries; i++) {
					temp = ntohs(req->list_of_streams[i]);
					if (temp >= stcb->asoc.streamoutcnt) {
						asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_DENIED;
						goto bad_boy;
					}
					req->list_of_streams[i] = temp;
				}
				for (i = 0; i < number_entries; i++) {
					if (stcb->asoc.strmout[req->list_of_streams[i]].state == SCTP_STREAM_OPEN) {
						stcb->asoc.strmout[req->list_of_streams[i]].state = SCTP_STREAM_RESET_PENDING;
					}
				}
			} else {
				/* Its all */
				for (i = 0; i < stcb->asoc.streamoutcnt; i++) {
					if (stcb->asoc.strmout[i].state == SCTP_STREAM_OPEN)
						stcb->asoc.strmout[i].state = SCTP_STREAM_RESET_PENDING;
				}
			}
			asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_PERFORMED;
		} else {
			/* Can't do it, since we have sent one out */
			asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_ERR_IN_PROGRESS;
		}
bad_boy:
		sctp_add_stream_reset_result(chk, seq, asoc->last_reset_action[0]);
		asoc->str_reset_seq_in++;
	} else if (asoc->str_reset_seq_in - 1 == seq) {
		sctp_add_stream_reset_result(chk, seq, asoc->last_reset_action[0]);
	} else if (asoc->str_reset_seq_in - 2 == seq) {
		sctp_add_stream_reset_result(chk, seq, asoc->last_reset_action[1]);
	} else {
		sctp_add_stream_reset_result(chk, seq, SCTP_STREAM_RESET_RESULT_ERR_BAD_SEQNO);
	}
	sctp_send_stream_reset_out_if_possible(stcb, SCTP_SO_NOT_LOCKED);
}

static int
sctp_handle_str_reset_request_tsn(struct sctp_tcb *stcb,
    struct sctp_tmit_chunk *chk,
    struct sctp_stream_reset_tsn_request *req)
{
	/* reset all in and out and update the tsn */
	/*
	 * A) reset my str-seq's on in and out. B) Select a receive next,
	 * and set cum-ack to it. Also process this selected number as a
	 * fwd-tsn as well. C) set in the response my next sending seq.
	 */
	struct sctp_forward_tsn_chunk fwdtsn;
	struct sctp_association *asoc = &stcb->asoc;
	int abort_flag = 0;
	uint32_t seq;

	seq = ntohl(req->request_seq);
	if (asoc->str_reset_seq_in == seq) {
		asoc->last_reset_action[1] = stcb->asoc.last_reset_action[0];
		if (!(asoc->local_strreset_support & SCTP_ENABLE_CHANGE_ASSOC_REQ)) {
			asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_DENIED;
		} else {
			fwdtsn.ch.chunk_length = htons(sizeof(struct sctp_forward_tsn_chunk));
			fwdtsn.ch.chunk_type = SCTP_FORWARD_CUM_TSN;
			fwdtsn.ch.chunk_flags = 0;
			fwdtsn.new_cumulative_tsn = htonl(stcb->asoc.highest_tsn_inside_map + 1);
			sctp_handle_forward_tsn(stcb, &fwdtsn, &abort_flag, NULL, 0);
			if (abort_flag) {
				return (1);
			}
			asoc->highest_tsn_inside_map += SCTP_STREAM_RESET_TSN_DELTA;
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MAP_LOGGING_ENABLE) {
				sctp_log_map(0, 10, asoc->highest_tsn_inside_map, SCTP_MAP_SLIDE_RESULT);
			}
			asoc->tsn_last_delivered = asoc->cumulative_tsn = asoc->highest_tsn_inside_map;
			asoc->mapping_array_base_tsn = asoc->highest_tsn_inside_map + 1;
			memset(asoc->mapping_array, 0, asoc->mapping_array_size);
			asoc->highest_tsn_inside_nr_map = asoc->highest_tsn_inside_map;
			memset(asoc->nr_mapping_array, 0, asoc->mapping_array_size);
			atomic_add_int(&asoc->sending_seq, 1);
			/* save off historical data for retrans */
			asoc->last_sending_seq[1] = asoc->last_sending_seq[0];
			asoc->last_sending_seq[0] = asoc->sending_seq;
			asoc->last_base_tsnsent[1] = asoc->last_base_tsnsent[0];
			asoc->last_base_tsnsent[0] = asoc->mapping_array_base_tsn;
			sctp_reset_out_streams(stcb, 0, (uint16_t *)NULL);
			sctp_reset_in_stream(stcb, 0, (uint16_t *)NULL);
			asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_PERFORMED;
			sctp_notify_stream_reset_tsn(stcb, asoc->sending_seq, (asoc->mapping_array_base_tsn + 1), 0);
		}
		sctp_add_stream_reset_result_tsn(chk, seq, asoc->last_reset_action[0],
		    asoc->last_sending_seq[0], asoc->last_base_tsnsent[0]);
		asoc->str_reset_seq_in++;
	} else if (asoc->str_reset_seq_in - 1 == seq) {
		sctp_add_stream_reset_result_tsn(chk, seq, asoc->last_reset_action[0],
		    asoc->last_sending_seq[0], asoc->last_base_tsnsent[0]);
	} else if (asoc->str_reset_seq_in - 2 == seq) {
		sctp_add_stream_reset_result_tsn(chk, seq, asoc->last_reset_action[1],
		    asoc->last_sending_seq[1], asoc->last_base_tsnsent[1]);
	} else {
		sctp_add_stream_reset_result(chk, seq, SCTP_STREAM_RESET_RESULT_ERR_BAD_SEQNO);
	}
	return (0);
}

static void
sctp_handle_str_reset_request_out(struct sctp_tcb *stcb,
    struct sctp_tmit_chunk *chk,
    struct sctp_stream_reset_out_request *req, int trunc)
{
	uint32_t seq, tsn;
	int number_entries, len;
	struct sctp_association *asoc = &stcb->asoc;

	seq = ntohl(req->request_seq);

	/* now if its not a duplicate we process it */
	if (asoc->str_reset_seq_in == seq) {
		len = ntohs(req->ph.param_length);
		number_entries = ((len - sizeof(struct sctp_stream_reset_out_request)) / sizeof(uint16_t));
		/*
		 * the sender is resetting, handle the list issue.. we must
		 * a) verify if we can do the reset, if so no problem b) If
		 * we can't do the reset we must copy the request. c) queue
		 * it, and setup the data in processor to trigger it off
		 * when needed and dequeue all the queued data.
		 */
		tsn = ntohl(req->send_reset_at_tsn);

		/* move the reset action back one */
		asoc->last_reset_action[1] = asoc->last_reset_action[0];
		if (!(asoc->local_strreset_support & SCTP_ENABLE_RESET_STREAM_REQ)) {
			asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_DENIED;
		} else if (trunc) {
			asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_DENIED;
		} else if (SCTP_TSN_GE(asoc->cumulative_tsn, tsn)) {
			/* we can do it now */
			sctp_reset_in_stream(stcb, number_entries, req->list_of_streams);
			asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_PERFORMED;
		} else {
			/*
			 * we must queue it up and thus wait for the TSN's
			 * to arrive that are at or before tsn
			 */
			struct sctp_stream_reset_list *liste;
			int siz;

			siz = sizeof(struct sctp_stream_reset_list) + (number_entries * sizeof(uint16_t));
			SCTP_MALLOC(liste, struct sctp_stream_reset_list *,
			    siz, SCTP_M_STRESET);
			if (liste == NULL) {
				/* gak out of memory */
				asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_DENIED;
				sctp_add_stream_reset_result(chk, seq, asoc->last_reset_action[0]);
				return;
			}
			liste->seq = seq;
			liste->tsn = tsn;
			liste->number_entries = number_entries;
			memcpy(&liste->list_of_streams, req->list_of_streams, number_entries * sizeof(uint16_t));
			TAILQ_INSERT_TAIL(&asoc->resetHead, liste, next_resp);
			asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_IN_PROGRESS;
		}
		sctp_add_stream_reset_result(chk, seq, asoc->last_reset_action[0]);
		asoc->str_reset_seq_in++;
	} else if ((asoc->str_reset_seq_in - 1) == seq) {
		/*
		 * one seq back, just echo back last action since my
		 * response was lost.
		 */
		sctp_add_stream_reset_result(chk, seq, asoc->last_reset_action[0]);
	} else if ((asoc->str_reset_seq_in - 2) == seq) {
		/*
		 * two seq back, just echo back last action since my
		 * response was lost.
		 */
		sctp_add_stream_reset_result(chk, seq, asoc->last_reset_action[1]);
	} else {
		sctp_add_stream_reset_result(chk, seq, SCTP_STREAM_RESET_RESULT_ERR_BAD_SEQNO);
	}
}

static void
sctp_handle_str_reset_add_strm(struct sctp_tcb *stcb, struct sctp_tmit_chunk *chk,
    struct sctp_stream_reset_add_strm *str_add)
{
	/*
	 * Peer is requesting to add more streams. If its within our
	 * max-streams we will allow it.
	 */
	uint32_t num_stream, i;
	uint32_t seq;
	struct sctp_association *asoc = &stcb->asoc;
	struct sctp_queued_to_read *ctl, *nctl;

	/* Get the number. */
	seq = ntohl(str_add->request_seq);
	num_stream = ntohs(str_add->number_of_streams);
	/* Now what would be the new total? */
	if (asoc->str_reset_seq_in == seq) {
		num_stream += stcb->asoc.streamincnt;
		stcb->asoc.last_reset_action[1] = stcb->asoc.last_reset_action[0];
		if (!(asoc->local_strreset_support & SCTP_ENABLE_CHANGE_ASSOC_REQ)) {
			asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_DENIED;
		} else if ((num_stream > stcb->asoc.max_inbound_streams) ||
		    (num_stream > 0xffff)) {
			/* We must reject it they ask for to many */
	denied:
			stcb->asoc.last_reset_action[0] = SCTP_STREAM_RESET_RESULT_DENIED;
		} else {
			/* Ok, we can do that :-) */
			struct sctp_stream_in *oldstrm;

			/* save off the old */
			oldstrm = stcb->asoc.strmin;
			SCTP_MALLOC(stcb->asoc.strmin, struct sctp_stream_in *,
			    (num_stream * sizeof(struct sctp_stream_in)),
			    SCTP_M_STRMI);
			if (stcb->asoc.strmin == NULL) {
				stcb->asoc.strmin = oldstrm;
				goto denied;
			}
			/* copy off the old data */
			for (i = 0; i < stcb->asoc.streamincnt; i++) {
				TAILQ_INIT(&stcb->asoc.strmin[i].inqueue);
				TAILQ_INIT(&stcb->asoc.strmin[i].uno_inqueue);
				stcb->asoc.strmin[i].sid = i;
				stcb->asoc.strmin[i].last_mid_delivered = oldstrm[i].last_mid_delivered;
				stcb->asoc.strmin[i].delivery_started = oldstrm[i].delivery_started;
				stcb->asoc.strmin[i].pd_api_started = oldstrm[i].pd_api_started;
				/* now anything on those queues? */
				TAILQ_FOREACH_SAFE(ctl, &oldstrm[i].inqueue, next_instrm, nctl) {
					TAILQ_REMOVE(&oldstrm[i].inqueue, ctl, next_instrm);
					TAILQ_INSERT_TAIL(&stcb->asoc.strmin[i].inqueue, ctl, next_instrm);
				}
				TAILQ_FOREACH_SAFE(ctl, &oldstrm[i].uno_inqueue, next_instrm, nctl) {
					TAILQ_REMOVE(&oldstrm[i].uno_inqueue, ctl, next_instrm);
					TAILQ_INSERT_TAIL(&stcb->asoc.strmin[i].uno_inqueue, ctl, next_instrm);
				}
			}
			/* Init the new streams */
			for (i = stcb->asoc.streamincnt; i < num_stream; i++) {
				TAILQ_INIT(&stcb->asoc.strmin[i].inqueue);
				TAILQ_INIT(&stcb->asoc.strmin[i].uno_inqueue);
				stcb->asoc.strmin[i].sid = i;
				stcb->asoc.strmin[i].last_mid_delivered = 0xffffffff;
				stcb->asoc.strmin[i].pd_api_started = 0;
				stcb->asoc.strmin[i].delivery_started = 0;
			}
			SCTP_FREE(oldstrm, SCTP_M_STRMI);
			/* update the size */
			stcb->asoc.streamincnt = num_stream;
			stcb->asoc.last_reset_action[0] = SCTP_STREAM_RESET_RESULT_PERFORMED;
			sctp_notify_stream_reset_add(stcb, stcb->asoc.streamincnt, stcb->asoc.streamoutcnt, 0);
		}
		sctp_add_stream_reset_result(chk, seq, asoc->last_reset_action[0]);
		asoc->str_reset_seq_in++;
	} else if ((asoc->str_reset_seq_in - 1) == seq) {
		/*
		 * one seq back, just echo back last action since my
		 * response was lost.
		 */
		sctp_add_stream_reset_result(chk, seq, asoc->last_reset_action[0]);
	} else if ((asoc->str_reset_seq_in - 2) == seq) {
		/*
		 * two seq back, just echo back last action since my
		 * response was lost.
		 */
		sctp_add_stream_reset_result(chk, seq, asoc->last_reset_action[1]);
	} else {
		sctp_add_stream_reset_result(chk, seq, SCTP_STREAM_RESET_RESULT_ERR_BAD_SEQNO);

	}
}

static void
sctp_handle_str_reset_add_out_strm(struct sctp_tcb *stcb, struct sctp_tmit_chunk *chk,
    struct sctp_stream_reset_add_strm *str_add)
{
	/*
	 * Peer is requesting to add more streams. If its within our
	 * max-streams we will allow it.
	 */
	uint16_t num_stream;
	uint32_t seq;
	struct sctp_association *asoc = &stcb->asoc;

	/* Get the number. */
	seq = ntohl(str_add->request_seq);
	num_stream = ntohs(str_add->number_of_streams);
	/* Now what would be the new total? */
	if (asoc->str_reset_seq_in == seq) {
		stcb->asoc.last_reset_action[1] = stcb->asoc.last_reset_action[0];
		if (!(asoc->local_strreset_support & SCTP_ENABLE_CHANGE_ASSOC_REQ)) {
			asoc->last_reset_action[0] = SCTP_STREAM_RESET_RESULT_DENIED;
		} else if (stcb->asoc.stream_reset_outstanding) {
			/* We must reject it we have something pending */
			stcb->asoc.last_reset_action[0] = SCTP_STREAM_RESET_RESULT_ERR_IN_PROGRESS;
		} else {
			/* Ok, we can do that :-) */
			int mychk;

			mychk = stcb->asoc.streamoutcnt;
			mychk += num_stream;
			if (mychk < 0x10000) {
				stcb->asoc.last_reset_action[0] = SCTP_STREAM_RESET_RESULT_PERFORMED;
				if (sctp_send_str_reset_req(stcb, 0, NULL, 0, 0, 1, num_stream, 0, 1)) {
					stcb->asoc.last_reset_action[0] = SCTP_STREAM_RESET_RESULT_DENIED;
				}
			} else {
				stcb->asoc.last_reset_action[0] = SCTP_STREAM_RESET_RESULT_DENIED;
			}
		}
		sctp_add_stream_reset_result(chk, seq, stcb->asoc.last_reset_action[0]);
		asoc->str_reset_seq_in++;
	} else if ((asoc->str_reset_seq_in - 1) == seq) {
		/*
		 * one seq back, just echo back last action since my
		 * response was lost.
		 */
		sctp_add_stream_reset_result(chk, seq, asoc->last_reset_action[0]);
	} else if ((asoc->str_reset_seq_in - 2) == seq) {
		/*
		 * two seq back, just echo back last action since my
		 * response was lost.
		 */
		sctp_add_stream_reset_result(chk, seq, asoc->last_reset_action[1]);
	} else {
		sctp_add_stream_reset_result(chk, seq, SCTP_STREAM_RESET_RESULT_ERR_BAD_SEQNO);
	}
}

#ifdef __GNUC__
__attribute__((noinline))
#endif
static int
sctp_handle_stream_reset(struct sctp_tcb *stcb, struct mbuf *m, int offset,
    struct sctp_chunkhdr *ch_req)
{
	uint16_t remaining_length, param_len, ptype;
	struct sctp_paramhdr pstore;
	uint8_t cstore[SCTP_CHUNK_BUFFER_SIZE];
	uint32_t seq = 0;
	int num_req = 0;
	int trunc = 0;
	struct sctp_tmit_chunk *chk;
	struct sctp_chunkhdr *ch;
	struct sctp_paramhdr *ph;
	int ret_code = 0;
	int num_param = 0;

	/* now it may be a reset or a reset-response */
	remaining_length = ntohs(ch_req->chunk_length) - sizeof(struct sctp_chunkhdr);

	/* setup for adding the response */
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		return (ret_code);
	}
	chk->copy_by_ref = 0;
	chk->rec.chunk_id.id = SCTP_STREAM_RESET;
	chk->rec.chunk_id.can_take_data = 0;
	chk->flags = 0;
	chk->asoc = &stcb->asoc;
	chk->no_fr_allowed = 0;
	chk->book_size = chk->send_size = sizeof(struct sctp_chunkhdr);
	chk->book_size_scale = 0;
	chk->data = sctp_get_mbuf_for_msg(MCLBYTES, 0, M_NOWAIT, 1, MT_DATA);
	if (chk->data == NULL) {
strres_nochunk:
		if (chk->data) {
			sctp_m_freem(chk->data);
			chk->data = NULL;
		}
		sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
		return (ret_code);
	}
	SCTP_BUF_RESV_UF(chk->data, SCTP_MIN_OVERHEAD);

	/* setup chunk parameters */
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	chk->whoTo = NULL;

	ch = mtod(chk->data, struct sctp_chunkhdr *);
	ch->chunk_type = SCTP_STREAM_RESET;
	ch->chunk_flags = 0;
	ch->chunk_length = htons(chk->send_size);
	SCTP_BUF_LEN(chk->data) = SCTP_SIZE32(chk->send_size);
	offset += sizeof(struct sctp_chunkhdr);
	while (remaining_length >= sizeof(struct sctp_paramhdr)) {
		ph = (struct sctp_paramhdr *)sctp_m_getptr(m, offset, sizeof(pstore), (uint8_t *)&pstore);
		if (ph == NULL) {
			/* TSNH */
			break;
		}
		param_len = ntohs(ph->param_length);
		if ((param_len > remaining_length) ||
		    (param_len < (sizeof(struct sctp_paramhdr) + sizeof(uint32_t)))) {
			/* bad parameter length */
			break;
		}
		ph = (struct sctp_paramhdr *)sctp_m_getptr(m, offset, min(param_len, sizeof(cstore)),
		    (uint8_t *)&cstore);
		if (ph == NULL) {
			/* TSNH */
			break;
		}
		ptype = ntohs(ph->param_type);
		num_param++;
		if (param_len > sizeof(cstore)) {
			trunc = 1;
		} else {
			trunc = 0;
		}
		if (num_param > SCTP_MAX_RESET_PARAMS) {
			/* hit the max of parameters already sorry.. */
			break;
		}
		if (ptype == SCTP_STR_RESET_OUT_REQUEST) {
			struct sctp_stream_reset_out_request *req_out;

			if (param_len < sizeof(struct sctp_stream_reset_out_request)) {
				break;
			}
			req_out = (struct sctp_stream_reset_out_request *)ph;
			num_req++;
			if (stcb->asoc.stream_reset_outstanding) {
				seq = ntohl(req_out->response_seq);
				if (seq == stcb->asoc.str_reset_seq_out) {
					/* implicit ack */
					(void)sctp_handle_stream_reset_response(stcb, seq, SCTP_STREAM_RESET_RESULT_PERFORMED, NULL);
				}
			}
			sctp_handle_str_reset_request_out(stcb, chk, req_out, trunc);
		} else if (ptype == SCTP_STR_RESET_ADD_OUT_STREAMS) {
			struct sctp_stream_reset_add_strm *str_add;

			if (param_len < sizeof(struct sctp_stream_reset_add_strm)) {
				break;
			}
			str_add = (struct sctp_stream_reset_add_strm *)ph;
			num_req++;
			sctp_handle_str_reset_add_strm(stcb, chk, str_add);
		} else if (ptype == SCTP_STR_RESET_ADD_IN_STREAMS) {
			struct sctp_stream_reset_add_strm *str_add;

			if (param_len < sizeof(struct sctp_stream_reset_add_strm)) {
				break;
			}
			str_add = (struct sctp_stream_reset_add_strm *)ph;
			num_req++;
			sctp_handle_str_reset_add_out_strm(stcb, chk, str_add);
		} else if (ptype == SCTP_STR_RESET_IN_REQUEST) {
			struct sctp_stream_reset_in_request *req_in;

			num_req++;
			req_in = (struct sctp_stream_reset_in_request *)ph;
			sctp_handle_str_reset_request_in(stcb, chk, req_in, trunc);
		} else if (ptype == SCTP_STR_RESET_TSN_REQUEST) {
			struct sctp_stream_reset_tsn_request *req_tsn;

			num_req++;
			req_tsn = (struct sctp_stream_reset_tsn_request *)ph;
			if (sctp_handle_str_reset_request_tsn(stcb, chk, req_tsn)) {
				ret_code = 1;
				goto strres_nochunk;
			}
			/* no more */
			break;
		} else if (ptype == SCTP_STR_RESET_RESPONSE) {
			struct sctp_stream_reset_response *resp;
			uint32_t result;

			if (param_len < sizeof(struct sctp_stream_reset_response)) {
				break;
			}
			resp = (struct sctp_stream_reset_response *)ph;
			seq = ntohl(resp->response_seq);
			result = ntohl(resp->result);
			if (sctp_handle_stream_reset_response(stcb, seq, result, resp)) {
				ret_code = 1;
				goto strres_nochunk;
			}
		} else {
			break;
		}
		offset += SCTP_SIZE32(param_len);
		if (remaining_length >= SCTP_SIZE32(param_len)) {
			remaining_length -= SCTP_SIZE32(param_len);
		} else {
			remaining_length = 0;
		}
	}
	if (num_req == 0) {
		/* we have no response free the stuff */
		goto strres_nochunk;
	}
	/* ok we have a chunk to link in */
	TAILQ_INSERT_TAIL(&stcb->asoc.control_send_queue,
	    chk,
	    sctp_next);
	stcb->asoc.ctrl_queue_cnt++;
	return (ret_code);
}

/*
 * Handle a router or endpoints report of a packet loss, there are two ways
 * to handle this, either we get the whole packet and must disect it
 * ourselves (possibly with truncation and or corruption) or it is a summary
 * from a middle box that did the disectting for us.
 */
static void
sctp_handle_packet_dropped(struct sctp_pktdrop_chunk *cp,
    struct sctp_tcb *stcb, struct sctp_nets *net, uint32_t limit)
{
	uint32_t bottle_bw, on_queue;
	uint16_t trunc_len;
	unsigned int chlen;
	unsigned int at;
	struct sctp_chunk_desc desc;
	struct sctp_chunkhdr *ch;

	chlen = ntohs(cp->ch.chunk_length);
	chlen -= sizeof(struct sctp_pktdrop_chunk);
	/* XXX possible chlen underflow */
	if (chlen == 0) {
		ch = NULL;
		if (cp->ch.chunk_flags & SCTP_FROM_MIDDLE_BOX)
			SCTP_STAT_INCR(sctps_pdrpbwrpt);
	} else {
		ch = (struct sctp_chunkhdr *)(cp->data + sizeof(struct sctphdr));
		chlen -= sizeof(struct sctphdr);
		/* XXX possible chlen underflow */
		memset(&desc, 0, sizeof(desc));
	}
	trunc_len = (uint16_t)ntohs(cp->trunc_len);
	if (trunc_len > limit) {
		trunc_len = limit;
	}

	/* now the chunks themselves */
	while ((ch != NULL) && (chlen >= sizeof(struct sctp_chunkhdr))) {
		desc.chunk_type = ch->chunk_type;
		/* get amount we need to move */
		at = ntohs(ch->chunk_length);
		if (at < sizeof(struct sctp_chunkhdr)) {
			/* corrupt chunk, maybe at the end? */
			SCTP_STAT_INCR(sctps_pdrpcrupt);
			break;
		}
		if (trunc_len == 0) {
			/* we are supposed to have all of it */
			if (at > chlen) {
				/* corrupt skip it */
				SCTP_STAT_INCR(sctps_pdrpcrupt);
				break;
			}
		} else {
			/* is there enough of it left ? */
			if (desc.chunk_type == SCTP_DATA) {
				if (chlen < (sizeof(struct sctp_data_chunk) +
				    sizeof(desc.data_bytes))) {
					break;
				}
			} else {
				if (chlen < sizeof(struct sctp_chunkhdr)) {
					break;
				}
			}
		}
		if (desc.chunk_type == SCTP_DATA) {
			/* can we get out the tsn? */
			if ((cp->ch.chunk_flags & SCTP_FROM_MIDDLE_BOX))
				SCTP_STAT_INCR(sctps_pdrpmbda);

			if (chlen >= (sizeof(struct sctp_data_chunk) + sizeof(uint32_t))) {
				/* yep */
				struct sctp_data_chunk *dcp;
				uint8_t *ddp;
				unsigned int iii;

				dcp = (struct sctp_data_chunk *)ch;
				ddp = (uint8_t *)(dcp + 1);
				for (iii = 0; iii < sizeof(desc.data_bytes); iii++) {
					desc.data_bytes[iii] = ddp[iii];
				}
				desc.tsn_ifany = dcp->dp.tsn;
			} else {
				/* nope we are done. */
				SCTP_STAT_INCR(sctps_pdrpnedat);
				break;
			}
		} else {
			if ((cp->ch.chunk_flags & SCTP_FROM_MIDDLE_BOX))
				SCTP_STAT_INCR(sctps_pdrpmbct);
		}

		if (process_chunk_drop(stcb, &desc, net, cp->ch.chunk_flags)) {
			SCTP_STAT_INCR(sctps_pdrppdbrk);
			break;
		}
		if (SCTP_SIZE32(at) > chlen) {
			break;
		}
		chlen -= SCTP_SIZE32(at);
		if (chlen < sizeof(struct sctp_chunkhdr)) {
			/* done, none left */
			break;
		}
		ch = (struct sctp_chunkhdr *)((caddr_t)ch + SCTP_SIZE32(at));
	}
	/* Now update any rwnd --- possibly */
	if ((cp->ch.chunk_flags & SCTP_FROM_MIDDLE_BOX) == 0) {
		/* From a peer, we get a rwnd report */
		uint32_t a_rwnd;

		SCTP_STAT_INCR(sctps_pdrpfehos);

		bottle_bw = ntohl(cp->bottle_bw);
		on_queue = ntohl(cp->current_onq);
		if (bottle_bw && on_queue) {
			/* a rwnd report is in here */
			if (bottle_bw > on_queue)
				a_rwnd = bottle_bw - on_queue;
			else
				a_rwnd = 0;

			if (a_rwnd == 0)
				stcb->asoc.peers_rwnd = 0;
			else {
				if (a_rwnd > stcb->asoc.total_flight) {
					stcb->asoc.peers_rwnd =
					    a_rwnd - stcb->asoc.total_flight;
				} else {
					stcb->asoc.peers_rwnd = 0;
				}
				if (stcb->asoc.peers_rwnd <
				    stcb->sctp_ep->sctp_ep.sctp_sws_sender) {
					/* SWS sender side engages */
					stcb->asoc.peers_rwnd = 0;
				}
			}
		}
	} else {
		SCTP_STAT_INCR(sctps_pdrpfmbox);
	}

	/* now middle boxes in sat networks get a cwnd bump */
	if ((cp->ch.chunk_flags & SCTP_FROM_MIDDLE_BOX) &&
	    (stcb->asoc.sat_t3_loss_recovery == 0) &&
	    (stcb->asoc.sat_network)) {
		/*
		 * This is debatable but for sat networks it makes sense
		 * Note if a T3 timer has went off, we will prohibit any
		 * changes to cwnd until we exit the t3 loss recovery.
		 */
		stcb->asoc.cc_functions.sctp_cwnd_update_after_packet_dropped(stcb,
		    net, cp, &bottle_bw, &on_queue);
	}
}

/*
 * handles all control chunks in a packet inputs: - m: mbuf chain, assumed to
 * still contain IP/SCTP header - stcb: is the tcb found for this packet -
 * offset: offset into the mbuf chain to first chunkhdr - length: is the
 * length of the complete packet outputs: - length: modified to remaining
 * length after control processing - netp: modified to new sctp_nets after
 * cookie-echo processing - return NULL to discard the packet (ie. no asoc,
 * bad packet,...) otherwise return the tcb for this packet
 */
#ifdef __GNUC__
__attribute__((noinline))
#endif
static struct sctp_tcb *
sctp_process_control(struct mbuf *m, int iphlen, int *offset, int length,
    struct sockaddr *src, struct sockaddr *dst,
    struct sctphdr *sh, struct sctp_chunkhdr *ch, struct sctp_inpcb *inp,
    struct sctp_tcb *stcb, struct sctp_nets **netp, int *fwd_tsn_seen,
    uint8_t mflowtype, uint32_t mflowid, uint16_t fibnum,
    uint32_t vrf_id, uint16_t port)
{
	struct sctp_association *asoc;
	struct mbuf *op_err;
	char msg[SCTP_DIAG_INFO_LEN];
	uint32_t vtag_in;
	int num_chunks = 0;	/* number of control chunks processed */
	uint32_t chk_length, contiguous;
	int ret;
	int abort_no_unlock = 0;
	int ecne_seen = 0;

	/*
	 * How big should this be, and should it be alloc'd? Lets try the
	 * d-mtu-ceiling for now (2k) and that should hopefully work ...
	 * until we get into jumbo grams and such..
	 */
	uint8_t chunk_buf[SCTP_CHUNK_BUFFER_SIZE];
	int got_auth = 0;
	uint32_t auth_offset = 0, auth_len = 0;
	int auth_skipped = 0;
	int asconf_cnt = 0;
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so;
#endif

	SCTPDBG(SCTP_DEBUG_INPUT1, "sctp_process_control: iphlen=%u, offset=%u, length=%u stcb:%p\n",
	    iphlen, *offset, length, (void *)stcb);

	if (stcb) {
		SCTP_TCB_LOCK_ASSERT(stcb);
	}
	/* validate chunk header length... */
	if (ntohs(ch->chunk_length) < sizeof(*ch)) {
		SCTPDBG(SCTP_DEBUG_INPUT1, "Invalid header length %d\n",
		    ntohs(ch->chunk_length));
		*offset = length;
		return (stcb);
	}
	/*
	 * validate the verification tag
	 */
	vtag_in = ntohl(sh->v_tag);

	if (ch->chunk_type == SCTP_INITIATION) {
		SCTPDBG(SCTP_DEBUG_INPUT1, "Its an INIT of len:%d vtag:%x\n",
		    ntohs(ch->chunk_length), vtag_in);
		if (vtag_in != 0) {
			/* protocol error- silently discard... */
			SCTP_STAT_INCR(sctps_badvtag);
			if (stcb != NULL) {
				SCTP_TCB_UNLOCK(stcb);
			}
			return (NULL);
		}
	} else if (ch->chunk_type != SCTP_COOKIE_ECHO) {
		/*
		 * If there is no stcb, skip the AUTH chunk and process
		 * later after a stcb is found (to validate the lookup was
		 * valid.
		 */
		if ((ch->chunk_type == SCTP_AUTHENTICATION) &&
		    (stcb == NULL) &&
		    (inp->auth_supported == 1)) {
			/* save this chunk for later processing */
			auth_skipped = 1;
			auth_offset = *offset;
			auth_len = ntohs(ch->chunk_length);

			/* (temporarily) move past this chunk */
			*offset += SCTP_SIZE32(auth_len);
			if (*offset >= length) {
				/* no more data left in the mbuf chain */
				*offset = length;
				return (NULL);
			}
			ch = (struct sctp_chunkhdr *)sctp_m_getptr(m, *offset,
			    sizeof(struct sctp_chunkhdr), chunk_buf);
		}
		if (ch == NULL) {
			/* Help */
			*offset = length;
			return (stcb);
		}
		if (ch->chunk_type == SCTP_COOKIE_ECHO) {
			goto process_control_chunks;
		}
		/*
		 * first check if it's an ASCONF with an unknown src addr we
		 * need to look inside to find the association
		 */
		if (ch->chunk_type == SCTP_ASCONF && stcb == NULL) {
			struct sctp_chunkhdr *asconf_ch = ch;
			uint32_t asconf_offset = 0, asconf_len = 0;

			/* inp's refcount may be reduced */
			SCTP_INP_INCR_REF(inp);

			asconf_offset = *offset;
			do {
				asconf_len = ntohs(asconf_ch->chunk_length);
				if (asconf_len < sizeof(struct sctp_asconf_paramhdr))
					break;
				stcb = sctp_findassociation_ep_asconf(m,
				    *offset,
				    dst,
				    sh, &inp, netp, vrf_id);
				if (stcb != NULL)
					break;
				asconf_offset += SCTP_SIZE32(asconf_len);
				asconf_ch = (struct sctp_chunkhdr *)sctp_m_getptr(m, asconf_offset,
				    sizeof(struct sctp_chunkhdr), chunk_buf);
			} while (asconf_ch != NULL && asconf_ch->chunk_type == SCTP_ASCONF);
			if (stcb == NULL) {
				/*
				 * reduce inp's refcount if not reduced in
				 * sctp_findassociation_ep_asconf().
				 */
				SCTP_INP_DECR_REF(inp);
			}

			/* now go back and verify any auth chunk to be sure */
			if (auth_skipped && (stcb != NULL)) {
				struct sctp_auth_chunk *auth;

				auth = (struct sctp_auth_chunk *)
				    sctp_m_getptr(m, auth_offset,
				    auth_len, chunk_buf);
				got_auth = 1;
				auth_skipped = 0;
				if ((auth == NULL) || sctp_handle_auth(stcb, auth, m,
				    auth_offset)) {
					/* auth HMAC failed so dump it */
					*offset = length;
					return (stcb);
				} else {
					/* remaining chunks are HMAC checked */
					stcb->asoc.authenticated = 1;
				}
			}
		}
		if (stcb == NULL) {
			snprintf(msg, sizeof(msg), "OOTB, %s:%d at %s", __FILE__, __LINE__, __func__);
			op_err = sctp_generate_cause(SCTP_BASE_SYSCTL(sctp_diag_info_code),
			    msg);
			/* no association, so it's out of the blue... */
			sctp_handle_ootb(m, iphlen, *offset, src, dst, sh, inp, op_err,
			    mflowtype, mflowid, inp->fibnum,
			    vrf_id, port);
			*offset = length;
			return (NULL);
		}
		asoc = &stcb->asoc;
		/* ABORT and SHUTDOWN can use either v_tag... */
		if ((ch->chunk_type == SCTP_ABORT_ASSOCIATION) ||
		    (ch->chunk_type == SCTP_SHUTDOWN_COMPLETE) ||
		    (ch->chunk_type == SCTP_PACKET_DROPPED)) {
			/* Take the T-bit always into account. */
			if ((((ch->chunk_flags & SCTP_HAD_NO_TCB) == 0) &&
			    (vtag_in == asoc->my_vtag)) ||
			    (((ch->chunk_flags & SCTP_HAD_NO_TCB) == SCTP_HAD_NO_TCB) &&
			    (asoc->peer_vtag != htonl(0)) &&
			    (vtag_in == asoc->peer_vtag))) {
				/* this is valid */
			} else {
				/* drop this packet... */
				SCTP_STAT_INCR(sctps_badvtag);
				if (stcb != NULL) {
					SCTP_TCB_UNLOCK(stcb);
				}
				return (NULL);
			}
		} else if (ch->chunk_type == SCTP_SHUTDOWN_ACK) {
			if (vtag_in != asoc->my_vtag) {
				/*
				 * this could be a stale SHUTDOWN-ACK or the
				 * peer never got the SHUTDOWN-COMPLETE and
				 * is still hung; we have started a new asoc
				 * but it won't complete until the shutdown
				 * is completed
				 */
				if (stcb != NULL) {
					SCTP_TCB_UNLOCK(stcb);
				}
				snprintf(msg, sizeof(msg), "OOTB, %s:%d at %s", __FILE__, __LINE__, __func__);
				op_err = sctp_generate_cause(SCTP_BASE_SYSCTL(sctp_diag_info_code),
				    msg);
				sctp_handle_ootb(m, iphlen, *offset, src, dst,
				    sh, inp, op_err,
				    mflowtype, mflowid, fibnum,
				    vrf_id, port);
				return (NULL);
			}
		} else {
			/* for all other chunks, vtag must match */
			if (vtag_in != asoc->my_vtag) {
				/* invalid vtag... */
				SCTPDBG(SCTP_DEBUG_INPUT3,
				    "invalid vtag: %xh, expect %xh\n",
				    vtag_in, asoc->my_vtag);
				SCTP_STAT_INCR(sctps_badvtag);
				if (stcb != NULL) {
					SCTP_TCB_UNLOCK(stcb);
				}
				*offset = length;
				return (NULL);
			}
		}
	}			/* end if !SCTP_COOKIE_ECHO */
	/*
	 * process all control chunks...
	 */
	if (((ch->chunk_type == SCTP_SELECTIVE_ACK) ||
	    (ch->chunk_type == SCTP_NR_SELECTIVE_ACK) ||
	    (ch->chunk_type == SCTP_HEARTBEAT_REQUEST)) &&
	    (SCTP_GET_STATE(stcb) == SCTP_STATE_COOKIE_ECHOED)) {
		/* implied cookie-ack.. we must have lost the ack */
		sctp_handle_cookie_ack((struct sctp_cookie_ack_chunk *)ch, stcb,
		    *netp);
	}

process_control_chunks:
	while (IS_SCTP_CONTROL(ch)) {
		/* validate chunk length */
		chk_length = ntohs(ch->chunk_length);
		SCTPDBG(SCTP_DEBUG_INPUT2, "sctp_process_control: processing a chunk type=%u, len=%u\n",
		    ch->chunk_type, chk_length);
		SCTP_LTRACE_CHK(inp, stcb, ch->chunk_type, chk_length);
		if (chk_length < sizeof(*ch) ||
		    (*offset + (int)chk_length) > length) {
			*offset = length;
			return (stcb);
		}
		SCTP_STAT_INCR_COUNTER64(sctps_incontrolchunks);
		/*
		 * INIT and INIT-ACK only gets the init ack "header" portion
		 * only because we don't have to process the peer's COOKIE.
		 * All others get a complete chunk.
		 */
		switch (ch->chunk_type) {
		case SCTP_INITIATION:
			contiguous = sizeof(struct sctp_init_chunk);
			break;
		case SCTP_INITIATION_ACK:
			contiguous = sizeof(struct sctp_init_ack_chunk);
			break;
		default:
			contiguous = min(chk_length, sizeof(chunk_buf));
			break;
		}
		ch = (struct sctp_chunkhdr *)sctp_m_getptr(m, *offset,
		    contiguous,
		    chunk_buf);
		if (ch == NULL) {
			*offset = length;
			if (stcb != NULL) {
				SCTP_TCB_UNLOCK(stcb);
			}
			return (NULL);
		}

		num_chunks++;
		/* Save off the last place we got a control from */
		if (stcb != NULL) {
			if (((netp != NULL) && (*netp != NULL)) || (ch->chunk_type == SCTP_ASCONF)) {
				/*
				 * allow last_control to be NULL if
				 * ASCONF... ASCONF processing will find the
				 * right net later
				 */
				if ((netp != NULL) && (*netp != NULL))
					stcb->asoc.last_control_chunk_from = *netp;
			}
		}
#ifdef SCTP_AUDITING_ENABLED
		sctp_audit_log(0xB0, ch->chunk_type);
#endif

		/* check to see if this chunk required auth, but isn't */
		if ((stcb != NULL) &&
		    sctp_auth_is_required_chunk(ch->chunk_type, stcb->asoc.local_auth_chunks) &&
		    !stcb->asoc.authenticated) {
			/* "silently" ignore */
			SCTP_STAT_INCR(sctps_recvauthmissing);
			goto next_chunk;
		}
		switch (ch->chunk_type) {
		case SCTP_INITIATION:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_INIT\n");
			/* The INIT chunk must be the only chunk. */
			if ((num_chunks > 1) ||
			    (length - *offset > (int)SCTP_SIZE32(chk_length))) {
				/* RFC 4960 requires that no ABORT is sent */
				*offset = length;
				if (stcb != NULL) {
					SCTP_TCB_UNLOCK(stcb);
				}
				return (NULL);
			}
			/* Honor our resource limit. */
			if (chk_length > SCTP_LARGEST_INIT_ACCEPTED) {
				op_err = sctp_generate_cause(SCTP_CAUSE_OUT_OF_RESC, "");
				sctp_abort_association(inp, stcb, m, iphlen,
				    src, dst, sh, op_err,
				    mflowtype, mflowid,
				    vrf_id, port);
				*offset = length;
				return (NULL);
			}
			sctp_handle_init(m, iphlen, *offset, src, dst, sh,
			    (struct sctp_init_chunk *)ch, inp,
			    stcb, *netp, &abort_no_unlock,
			    mflowtype, mflowid,
			    vrf_id, port);
			*offset = length;
			if ((!abort_no_unlock) && (stcb != NULL)) {
				SCTP_TCB_UNLOCK(stcb);
			}
			return (NULL);
			break;
		case SCTP_PAD_CHUNK:
			break;
		case SCTP_INITIATION_ACK:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_INIT_ACK\n");
			if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
				/* We are not interested anymore */
				if ((stcb != NULL) && (stcb->asoc.total_output_queue_size)) {
					;
				} else {
					*offset = length;
					if (stcb != NULL) {
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
						so = SCTP_INP_SO(inp);
						atomic_add_int(&stcb->asoc.refcnt, 1);
						SCTP_TCB_UNLOCK(stcb);
						SCTP_SOCKET_LOCK(so, 1);
						SCTP_TCB_LOCK(stcb);
						atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
						(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
						    SCTP_FROM_SCTP_INPUT + SCTP_LOC_29);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
						SCTP_SOCKET_UNLOCK(so, 1);
#endif
					}
					return (NULL);
				}
			}
			/* The INIT-ACK chunk must be the only chunk. */
			if ((num_chunks > 1) ||
			    (length - *offset > (int)SCTP_SIZE32(chk_length))) {
				*offset = length;
				return (stcb);
			}
			if ((netp != NULL) && (*netp != NULL)) {
				ret = sctp_handle_init_ack(m, iphlen, *offset,
				    src, dst, sh,
				    (struct sctp_init_ack_chunk *)ch,
				    stcb, *netp,
				    &abort_no_unlock,
				    mflowtype, mflowid,
				    vrf_id);
			} else {
				ret = -1;
			}
			*offset = length;
			if (abort_no_unlock) {
				return (NULL);
			}
			/*
			 * Special case, I must call the output routine to
			 * get the cookie echoed
			 */
			if ((stcb != NULL) && (ret == 0)) {
				sctp_chunk_output(stcb->sctp_ep, stcb, SCTP_OUTPUT_FROM_CONTROL_PROC, SCTP_SO_NOT_LOCKED);
			}
			return (stcb);
			break;
		case SCTP_SELECTIVE_ACK:
		case SCTP_NR_SELECTIVE_ACK:
			{
				int abort_now = 0;
				uint32_t a_rwnd, cum_ack;
				uint16_t num_seg, num_nr_seg, num_dup;
				uint8_t flags;
				int offset_seg, offset_dup;

				SCTPDBG(SCTP_DEBUG_INPUT3, "%s\n",
				    ch->chunk_type == SCTP_SELECTIVE_ACK ? "SCTP_SACK" : "SCTP_NR_SACK");
				SCTP_STAT_INCR(sctps_recvsacks);
				if (stcb == NULL) {
					SCTPDBG(SCTP_DEBUG_INDATA1, "No stcb when processing %s chunk\n",
					    (ch->chunk_type == SCTP_SELECTIVE_ACK) ? "SCTP_SACK" : "SCTP_NR_SACK");
					break;
				}
				if (ch->chunk_type == SCTP_SELECTIVE_ACK) {
					if (chk_length < sizeof(struct sctp_sack_chunk)) {
						SCTPDBG(SCTP_DEBUG_INDATA1, "Bad size on SACK chunk, too small\n");
						break;
					}
				} else {
					if (stcb->asoc.nrsack_supported == 0) {
						goto unknown_chunk;
					}
					if (chk_length < sizeof(struct sctp_nr_sack_chunk)) {
						SCTPDBG(SCTP_DEBUG_INDATA1, "Bad size on NR_SACK chunk, too small\n");
						break;
					}
				}
				if (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_ACK_SENT) {
					/*-
					 * If we have sent a shutdown-ack, we will pay no
					 * attention to a sack sent in to us since
					 * we don't care anymore.
					 */
					break;
				}
				flags = ch->chunk_flags;
				if (ch->chunk_type == SCTP_SELECTIVE_ACK) {
					struct sctp_sack_chunk *sack;

					sack = (struct sctp_sack_chunk *)ch;
					cum_ack = ntohl(sack->sack.cum_tsn_ack);
					num_seg = ntohs(sack->sack.num_gap_ack_blks);
					num_nr_seg = 0;
					num_dup = ntohs(sack->sack.num_dup_tsns);
					a_rwnd = ntohl(sack->sack.a_rwnd);
					if (sizeof(struct sctp_sack_chunk) +
					    num_seg * sizeof(struct sctp_gap_ack_block) +
					    num_dup * sizeof(uint32_t) != chk_length) {
						SCTPDBG(SCTP_DEBUG_INDATA1, "Bad size of SACK chunk\n");
						break;
					}
					offset_seg = *offset + sizeof(struct sctp_sack_chunk);
					offset_dup = offset_seg + num_seg * sizeof(struct sctp_gap_ack_block);
				} else {
					struct sctp_nr_sack_chunk *nr_sack;

					nr_sack = (struct sctp_nr_sack_chunk *)ch;
					cum_ack = ntohl(nr_sack->nr_sack.cum_tsn_ack);
					num_seg = ntohs(nr_sack->nr_sack.num_gap_ack_blks);
					num_nr_seg = ntohs(nr_sack->nr_sack.num_nr_gap_ack_blks);
					num_dup = ntohs(nr_sack->nr_sack.num_dup_tsns);
					a_rwnd = ntohl(nr_sack->nr_sack.a_rwnd);
					if (sizeof(struct sctp_nr_sack_chunk) +
					    (num_seg + num_nr_seg) * sizeof(struct sctp_gap_ack_block) +
					    num_dup * sizeof(uint32_t) != chk_length) {
						SCTPDBG(SCTP_DEBUG_INDATA1, "Bad size of NR_SACK chunk\n");
						break;
					}
					offset_seg = *offset + sizeof(struct sctp_nr_sack_chunk);
					offset_dup = offset_seg + (num_seg + num_nr_seg) * sizeof(struct sctp_gap_ack_block);
				}
				SCTPDBG(SCTP_DEBUG_INPUT3, "%s process cum_ack:%x num_seg:%d a_rwnd:%d\n",
				    (ch->chunk_type == SCTP_SELECTIVE_ACK) ? "SCTP_SACK" : "SCTP_NR_SACK",
				    cum_ack, num_seg, a_rwnd);
				stcb->asoc.seen_a_sack_this_pkt = 1;
				if ((stcb->asoc.pr_sctp_cnt == 0) &&
				    (num_seg == 0) && (num_nr_seg == 0) &&
				    SCTP_TSN_GE(cum_ack, stcb->asoc.last_acked_seq) &&
				    (stcb->asoc.saw_sack_with_frags == 0) &&
				    (stcb->asoc.saw_sack_with_nr_frags == 0) &&
				    (!TAILQ_EMPTY(&stcb->asoc.sent_queue))) {
					/*
					 * We have a SIMPLE sack having no
					 * prior segments and data on sent
					 * queue to be acked. Use the faster
					 * path sack processing. We also
					 * allow window update sacks with no
					 * missing segments to go this way
					 * too.
					 */
					sctp_express_handle_sack(stcb, cum_ack, a_rwnd,
					    &abort_now, ecne_seen);
				} else {
					if ((netp != NULL) && (*netp != NULL)) {
						sctp_handle_sack(m, offset_seg, offset_dup, stcb,
						    num_seg, num_nr_seg, num_dup, &abort_now, flags,
						    cum_ack, a_rwnd, ecne_seen);
					}
				}
				if (abort_now) {
					/* ABORT signal from sack processing */
					*offset = length;
					return (NULL);
				}
				if (TAILQ_EMPTY(&stcb->asoc.send_queue) &&
				    TAILQ_EMPTY(&stcb->asoc.sent_queue) &&
				    (stcb->asoc.stream_queue_cnt == 0)) {
					sctp_ulp_notify(SCTP_NOTIFY_SENDER_DRY, stcb, 0, NULL, SCTP_SO_NOT_LOCKED);
				}
				break;
			}
		case SCTP_HEARTBEAT_REQUEST:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_HEARTBEAT\n");
			if ((stcb != NULL) && (netp != NULL) && (*netp != NULL)) {
				SCTP_STAT_INCR(sctps_recvheartbeat);
				sctp_send_heartbeat_ack(stcb, m, *offset,
				    chk_length, *netp);
			}
			break;
		case SCTP_HEARTBEAT_ACK:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_HEARTBEAT_ACK\n");
			if ((stcb == NULL) || (chk_length != sizeof(struct sctp_heartbeat_chunk))) {
				/* Its not ours */
				*offset = length;
				return (stcb);
			}
			SCTP_STAT_INCR(sctps_recvheartbeatack);
			if ((netp != NULL) && (*netp != NULL)) {
				sctp_handle_heartbeat_ack((struct sctp_heartbeat_chunk *)ch,
				    stcb, *netp);
			}
			break;
		case SCTP_ABORT_ASSOCIATION:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_ABORT, stcb %p\n",
			    (void *)stcb);
			*offset = length;
			if ((stcb != NULL) && (netp != NULL) && (*netp != NULL)) {
				if (sctp_handle_abort((struct sctp_abort_chunk *)ch, stcb, *netp)) {
					return (NULL);
				} else {
					return (stcb);
				}
			} else {
				return (NULL);
			}
			break;
		case SCTP_SHUTDOWN:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_SHUTDOWN, stcb %p\n",
			    (void *)stcb);
			if ((stcb == NULL) || (chk_length != sizeof(struct sctp_shutdown_chunk))) {
				*offset = length;
				return (stcb);
			}
			if ((netp != NULL) && (*netp != NULL)) {
				int abort_flag = 0;

				sctp_handle_shutdown((struct sctp_shutdown_chunk *)ch,
				    stcb, *netp, &abort_flag);
				if (abort_flag) {
					*offset = length;
					return (NULL);
				}
			}
			break;
		case SCTP_SHUTDOWN_ACK:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_SHUTDOWN_ACK, stcb %p\n", (void *)stcb);
			if ((stcb != NULL) && (netp != NULL) && (*netp != NULL)) {
				sctp_handle_shutdown_ack((struct sctp_shutdown_ack_chunk *)ch, stcb, *netp);
			}
			*offset = length;
			return (NULL);
			break;
		case SCTP_OPERATION_ERROR:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_OP_ERR\n");
			if ((stcb != NULL) && (netp != NULL) && (*netp != NULL) &&
			    sctp_handle_error(ch, stcb, *netp, contiguous) < 0) {
				*offset = length;
				return (NULL);
			}
			break;
		case SCTP_COOKIE_ECHO:
			SCTPDBG(SCTP_DEBUG_INPUT3,
			    "SCTP_COOKIE_ECHO, stcb %p\n", (void *)stcb);
			if ((stcb != NULL) && (stcb->asoc.total_output_queue_size > 0)) {
				;
			} else {
				if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
					/* We are not interested anymore */
			abend:
					if (stcb != NULL) {
						SCTP_TCB_UNLOCK(stcb);
					}
					*offset = length;
					return (NULL);
				}
			}
			/*-
			 * First are we accepting? We do this again here
			 * since it is possible that a previous endpoint WAS
			 * listening responded to a INIT-ACK and then
			 * closed. We opened and bound.. and are now no
			 * longer listening.
			 *
			 * XXXGL: notes on checking listen queue length.
			 * 1) SCTP_IS_LISTENING() doesn't necessarily mean
			 *    SOLISTENING(), because a listening "UDP type"
			 *    socket isn't listening in terms of the socket
			 *    layer.  It is a normal data flow socket, that
			 *    can fork off new connections.  Thus, we should
			 *    look into sol_qlen only in case we are !UDP.
			 * 2) Checking sol_qlen in general requires locking
			 *    the socket, and this code lacks that.
			 */
			if ((stcb == NULL) &&
			    (!SCTP_IS_LISTENING(inp) ||
			    (!(inp->sctp_flags & SCTP_PCB_FLAGS_UDPTYPE) &&
			    inp->sctp_socket->sol_qlen >= inp->sctp_socket->sol_qlimit))) {
				if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) &&
				    (SCTP_BASE_SYSCTL(sctp_abort_if_one_2_one_hits_limit))) {
					op_err = sctp_generate_cause(SCTP_CAUSE_OUT_OF_RESC, "");
					sctp_abort_association(inp, stcb, m, iphlen,
					    src, dst, sh, op_err,
					    mflowtype, mflowid,
					    vrf_id, port);
				}
				*offset = length;
				return (NULL);
			} else {
				struct mbuf *ret_buf;
				struct sctp_inpcb *linp;

				if (stcb) {
					linp = NULL;
				} else {
					linp = inp;
				}

				if (linp != NULL) {
					SCTP_ASOC_CREATE_LOCK(linp);
					if ((inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) ||
					    (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE)) {
						SCTP_ASOC_CREATE_UNLOCK(linp);
						goto abend;
					}
				}

				if (netp != NULL) {
					struct sctp_tcb *locked_stcb;

					locked_stcb = stcb;
					ret_buf =
					    sctp_handle_cookie_echo(m, iphlen,
					    *offset,
					    src, dst,
					    sh,
					    (struct sctp_cookie_echo_chunk *)ch,
					    &inp, &stcb, netp,
					    auth_skipped,
					    auth_offset,
					    auth_len,
					    &locked_stcb,
					    mflowtype,
					    mflowid,
					    vrf_id,
					    port);
					if ((locked_stcb != NULL) && (locked_stcb != stcb)) {
						SCTP_TCB_UNLOCK(locked_stcb);
					}
					if (stcb != NULL) {
						SCTP_TCB_LOCK_ASSERT(stcb);
					}
				} else {
					ret_buf = NULL;
				}
				if (linp != NULL) {
					SCTP_ASOC_CREATE_UNLOCK(linp);
				}
				if (ret_buf == NULL) {
					if (stcb != NULL) {
						SCTP_TCB_UNLOCK(stcb);
					}
					SCTPDBG(SCTP_DEBUG_INPUT3,
					    "GAK, null buffer\n");
					*offset = length;
					return (NULL);
				}
				/* if AUTH skipped, see if it verified... */
				if (auth_skipped) {
					got_auth = 1;
					auth_skipped = 0;
				}
				if (!TAILQ_EMPTY(&stcb->asoc.sent_queue)) {
					/*
					 * Restart the timer if we have
					 * pending data
					 */
					struct sctp_tmit_chunk *chk;

					chk = TAILQ_FIRST(&stcb->asoc.sent_queue);
					sctp_timer_start(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep, stcb, chk->whoTo);
				}
			}
			break;
		case SCTP_COOKIE_ACK:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_COOKIE_ACK, stcb %p\n", (void *)stcb);
			if ((stcb == NULL) || chk_length != sizeof(struct sctp_cookie_ack_chunk)) {
				return (stcb);
			}
			if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
				/* We are not interested anymore */
				if ((stcb) && (stcb->asoc.total_output_queue_size)) {
					;
				} else if (stcb) {
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
					so = SCTP_INP_SO(inp);
					atomic_add_int(&stcb->asoc.refcnt, 1);
					SCTP_TCB_UNLOCK(stcb);
					SCTP_SOCKET_LOCK(so, 1);
					SCTP_TCB_LOCK(stcb);
					atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
					(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
					    SCTP_FROM_SCTP_INPUT + SCTP_LOC_30);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
					SCTP_SOCKET_UNLOCK(so, 1);
#endif
					*offset = length;
					return (NULL);
				}
			}
			if ((netp != NULL) && (*netp != NULL)) {
				sctp_handle_cookie_ack((struct sctp_cookie_ack_chunk *)ch, stcb, *netp);
			}
			break;
		case SCTP_ECN_ECHO:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_ECN_ECHO\n");
			if ((stcb == NULL) || (chk_length != sizeof(struct sctp_ecne_chunk))) {
				/* Its not ours */
				*offset = length;
				return (stcb);
			}
			if (stcb->asoc.ecn_supported == 0) {
				goto unknown_chunk;
			}
			sctp_handle_ecn_echo((struct sctp_ecne_chunk *)ch, stcb);
			ecne_seen = 1;
			break;
		case SCTP_ECN_CWR:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_ECN_CWR\n");
			if ((stcb == NULL) || (chk_length != sizeof(struct sctp_cwr_chunk))) {
				*offset = length;
				return (stcb);
			}
			if (stcb->asoc.ecn_supported == 0) {
				goto unknown_chunk;
			}
			sctp_handle_ecn_cwr((struct sctp_cwr_chunk *)ch, stcb, *netp);
			break;
		case SCTP_SHUTDOWN_COMPLETE:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_SHUTDOWN_COMPLETE, stcb %p\n", (void *)stcb);
			/* must be first and only chunk */
			if ((num_chunks > 1) ||
			    (length - *offset > (int)SCTP_SIZE32(chk_length))) {
				*offset = length;
				return (stcb);
			}
			if ((stcb != NULL) && (netp != NULL) && (*netp != NULL)) {
				sctp_handle_shutdown_complete((struct sctp_shutdown_complete_chunk *)ch,
				    stcb, *netp);
			}
			*offset = length;
			return (NULL);
			break;
		case SCTP_ASCONF:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_ASCONF\n");
			if (stcb != NULL) {
				if (stcb->asoc.asconf_supported == 0) {
					goto unknown_chunk;
				}
				sctp_handle_asconf(m, *offset, src,
				    (struct sctp_asconf_chunk *)ch, stcb, asconf_cnt == 0);
				asconf_cnt++;
			}
			break;
		case SCTP_ASCONF_ACK:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_ASCONF_ACK\n");
			if (chk_length < sizeof(struct sctp_asconf_ack_chunk)) {
				/* Its not ours */
				*offset = length;
				return (stcb);
			}
			if ((stcb != NULL) && (netp != NULL) && (*netp != NULL)) {
				if (stcb->asoc.asconf_supported == 0) {
					goto unknown_chunk;
				}
				/* He's alive so give him credit */
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_THRESHOLD_LOGGING) {
					sctp_misc_ints(SCTP_THRESHOLD_CLEAR,
					    stcb->asoc.overall_error_count,
					    0,
					    SCTP_FROM_SCTP_INPUT,
					    __LINE__);
				}
				stcb->asoc.overall_error_count = 0;
				sctp_handle_asconf_ack(m, *offset,
				    (struct sctp_asconf_ack_chunk *)ch, stcb, *netp, &abort_no_unlock);
				if (abort_no_unlock)
					return (NULL);
			}
			break;
		case SCTP_FORWARD_CUM_TSN:
		case SCTP_IFORWARD_CUM_TSN:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_FWD_TSN\n");
			if (chk_length < sizeof(struct sctp_forward_tsn_chunk)) {
				/* Its not ours */
				*offset = length;
				return (stcb);
			}

			if (stcb != NULL) {
				int abort_flag = 0;

				if (stcb->asoc.prsctp_supported == 0) {
					goto unknown_chunk;
				}
				*fwd_tsn_seen = 1;
				if (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
					/* We are not interested anymore */
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
					so = SCTP_INP_SO(inp);
					atomic_add_int(&stcb->asoc.refcnt, 1);
					SCTP_TCB_UNLOCK(stcb);
					SCTP_SOCKET_LOCK(so, 1);
					SCTP_TCB_LOCK(stcb);
					atomic_subtract_int(&stcb->asoc.refcnt, 1);
#endif
					(void)sctp_free_assoc(inp, stcb, SCTP_NORMAL_PROC,
					    SCTP_FROM_SCTP_INPUT + SCTP_LOC_31);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
					SCTP_SOCKET_UNLOCK(so, 1);
#endif
					*offset = length;
					return (NULL);
				}
				/*
				 * For sending a SACK this looks like DATA
				 * chunks.
				 */
				stcb->asoc.last_data_chunk_from = stcb->asoc.last_control_chunk_from;
				sctp_handle_forward_tsn(stcb,
				    (struct sctp_forward_tsn_chunk *)ch, &abort_flag, m, *offset);
				if (abort_flag) {
					*offset = length;
					return (NULL);
				}
			}
			break;
		case SCTP_STREAM_RESET:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_STREAM_RESET\n");
			if (((stcb == NULL) || (ch == NULL) || (chk_length < sizeof(struct sctp_stream_reset_tsn_req)))) {
				/* Its not ours */
				*offset = length;
				return (stcb);
			}
			if (stcb->asoc.reconfig_supported == 0) {
				goto unknown_chunk;
			}
			if (sctp_handle_stream_reset(stcb, m, *offset, ch)) {
				/* stop processing */
				*offset = length;
				return (NULL);
			}
			break;
		case SCTP_PACKET_DROPPED:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_PACKET_DROPPED\n");
			/* re-get it all please */
			if (chk_length < sizeof(struct sctp_pktdrop_chunk)) {
				/* Its not ours */
				*offset = length;
				return (stcb);
			}

			if ((ch != NULL) && (stcb != NULL) && (netp != NULL) && (*netp != NULL)) {
				if (stcb->asoc.pktdrop_supported == 0) {
					goto unknown_chunk;
				}
				sctp_handle_packet_dropped((struct sctp_pktdrop_chunk *)ch,
				    stcb, *netp,
				    min(chk_length, contiguous));
			}
			break;
		case SCTP_AUTHENTICATION:
			SCTPDBG(SCTP_DEBUG_INPUT3, "SCTP_AUTHENTICATION\n");
			if (stcb == NULL) {
				/* save the first AUTH for later processing */
				if (auth_skipped == 0) {
					auth_offset = *offset;
					auth_len = chk_length;
					auth_skipped = 1;
				}
				/* skip this chunk (temporarily) */
				goto next_chunk;
			}
			if (stcb->asoc.auth_supported == 0) {
				goto unknown_chunk;
			}
			if ((chk_length < (sizeof(struct sctp_auth_chunk))) ||
			    (chk_length > (sizeof(struct sctp_auth_chunk) +
			    SCTP_AUTH_DIGEST_LEN_MAX))) {
				/* Its not ours */
				*offset = length;
				return (stcb);
			}
			if (got_auth == 1) {
				/* skip this chunk... it's already auth'd */
				goto next_chunk;
			}
			got_auth = 1;
			if ((ch == NULL) || sctp_handle_auth(stcb, (struct sctp_auth_chunk *)ch,
			    m, *offset)) {
				/* auth HMAC failed so dump the packet */
				*offset = length;
				return (stcb);
			} else {
				/* remaining chunks are HMAC checked */
				stcb->asoc.authenticated = 1;
			}
			break;

		default:
	unknown_chunk:
			/* it's an unknown chunk! */
			if ((ch->chunk_type & 0x40) && (stcb != NULL)) {
				struct sctp_gen_error_cause *cause;
				int len;

				op_err = sctp_get_mbuf_for_msg(sizeof(struct sctp_gen_error_cause),
				    0, M_NOWAIT, 1, MT_DATA);
				if (op_err != NULL) {
					len = min(SCTP_SIZE32(chk_length), (uint32_t)(length - *offset));
					cause = mtod(op_err, struct sctp_gen_error_cause *);
					cause->code = htons(SCTP_CAUSE_UNRECOG_CHUNK);
					cause->length = htons((uint16_t)(len + sizeof(struct sctp_gen_error_cause)));
					SCTP_BUF_LEN(op_err) = sizeof(struct sctp_gen_error_cause);
					SCTP_BUF_NEXT(op_err) = SCTP_M_COPYM(m, *offset, len, M_NOWAIT);
					if (SCTP_BUF_NEXT(op_err) != NULL) {
#ifdef SCTP_MBUF_LOGGING
						if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
							sctp_log_mbc(SCTP_BUF_NEXT(op_err), SCTP_MBUF_ICOPY);
						}
#endif
						sctp_queue_op_err(stcb, op_err);
					} else {
						sctp_m_freem(op_err);
					}
				}
			}
			if ((ch->chunk_type & 0x80) == 0) {
				/* discard this packet */
				*offset = length;
				return (stcb);
			}	/* else skip this bad chunk and continue... */
			break;
		}		/* switch (ch->chunk_type) */


next_chunk:
		/* get the next chunk */
		*offset += SCTP_SIZE32(chk_length);
		if (*offset >= length) {
			/* no more data left in the mbuf chain */
			break;
		}
		ch = (struct sctp_chunkhdr *)sctp_m_getptr(m, *offset,
		    sizeof(struct sctp_chunkhdr), chunk_buf);
		if (ch == NULL) {
			*offset = length;
			return (stcb);
		}
	}			/* while */

	if ((asconf_cnt > 0) && (stcb != NULL)) {
		sctp_send_asconf_ack(stcb);
	}
	return (stcb);
}


/*
 * common input chunk processing (v4 and v6)
 */
void
sctp_common_input_processing(struct mbuf **mm, int iphlen, int offset, int length,
    struct sockaddr *src, struct sockaddr *dst,
    struct sctphdr *sh, struct sctp_chunkhdr *ch,
    uint8_t compute_crc,
    uint8_t ecn_bits,
    uint8_t mflowtype, uint32_t mflowid, uint16_t fibnum,
    uint32_t vrf_id, uint16_t port)
{
	uint32_t high_tsn;
	int fwd_tsn_seen = 0, data_processed = 0;
	struct mbuf *m = *mm, *op_err;
	char msg[SCTP_DIAG_INFO_LEN];
	int un_sent;
	int cnt_ctrl_ready = 0;
	struct sctp_inpcb *inp = NULL, *inp_decr = NULL;
	struct sctp_tcb *stcb = NULL;
	struct sctp_nets *net = NULL;

	SCTP_STAT_INCR(sctps_recvdatagrams);
#ifdef SCTP_AUDITING_ENABLED
	sctp_audit_log(0xE0, 1);
	sctp_auditing(0, inp, stcb, net);
#endif
	if (compute_crc != 0) {
		uint32_t check, calc_check;

		check = sh->checksum;
		sh->checksum = 0;
		calc_check = sctp_calculate_cksum(m, iphlen);
		sh->checksum = check;
		if (calc_check != check) {
			SCTPDBG(SCTP_DEBUG_INPUT1, "Bad CSUM on SCTP packet calc_check:%x check:%x  m:%p mlen:%d iphlen:%d\n",
			    calc_check, check, (void *)m, length, iphlen);
			stcb = sctp_findassociation_addr(m, offset, src, dst,
			    sh, ch, &inp, &net, vrf_id);
#if defined(INET) || defined(INET6)
			if ((ch->chunk_type != SCTP_INITIATION) &&
			    (net != NULL) && (net->port != port)) {
				if (net->port == 0) {
					/* UDP encapsulation turned on. */
					net->mtu -= sizeof(struct udphdr);
					if (stcb->asoc.smallest_mtu > net->mtu) {
						sctp_pathmtu_adjustment(stcb, net->mtu);
					}
				} else if (port == 0) {
					/* UDP encapsulation turned off. */
					net->mtu += sizeof(struct udphdr);
					/* XXX Update smallest_mtu */
				}
				net->port = port;
			}
#endif
			if (net != NULL) {
				net->flowtype = mflowtype;
				net->flowid = mflowid;
			}
			SCTP_PROBE5(receive, NULL, stcb, m, stcb, sh);
			if ((inp != NULL) && (stcb != NULL)) {
				sctp_send_packet_dropped(stcb, net, m, length, iphlen, 1);
				sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_INPUT_ERROR, SCTP_SO_NOT_LOCKED);
			} else if ((inp != NULL) && (stcb == NULL)) {
				inp_decr = inp;
			}
			SCTP_STAT_INCR(sctps_badsum);
			SCTP_STAT_INCR_COUNTER32(sctps_checksumerrors);
			goto out;
		}
	}
	/* Destination port of 0 is illegal, based on RFC4960. */
	if (sh->dest_port == 0) {
		SCTP_STAT_INCR(sctps_hdrops);
		goto out;
	}
	stcb = sctp_findassociation_addr(m, offset, src, dst,
	    sh, ch, &inp, &net, vrf_id);
#if defined(INET) || defined(INET6)
	if ((ch->chunk_type != SCTP_INITIATION) &&
	    (net != NULL) && (net->port != port)) {
		if (net->port == 0) {
			/* UDP encapsulation turned on. */
			net->mtu -= sizeof(struct udphdr);
			if (stcb->asoc.smallest_mtu > net->mtu) {
				sctp_pathmtu_adjustment(stcb, net->mtu);
			}
		} else if (port == 0) {
			/* UDP encapsulation turned off. */
			net->mtu += sizeof(struct udphdr);
			/* XXX Update smallest_mtu */
		}
		net->port = port;
	}
#endif
	if (net != NULL) {
		net->flowtype = mflowtype;
		net->flowid = mflowid;
	}
	if (inp == NULL) {
		SCTP_PROBE5(receive, NULL, stcb, m, stcb, sh);
		SCTP_STAT_INCR(sctps_noport);
		if (badport_bandlim(BANDLIM_SCTP_OOTB) < 0) {
			goto out;
		}
		if (ch->chunk_type == SCTP_SHUTDOWN_ACK) {
			sctp_send_shutdown_complete2(src, dst, sh,
			    mflowtype, mflowid, fibnum,
			    vrf_id, port);
			goto out;
		}
		if (ch->chunk_type == SCTP_SHUTDOWN_COMPLETE) {
			goto out;
		}
		if (ch->chunk_type != SCTP_ABORT_ASSOCIATION) {
			if ((SCTP_BASE_SYSCTL(sctp_blackhole) == 0) ||
			    ((SCTP_BASE_SYSCTL(sctp_blackhole) == 1) &&
			    (ch->chunk_type != SCTP_INIT))) {
				op_err = sctp_generate_cause(SCTP_BASE_SYSCTL(sctp_diag_info_code),
				    "Out of the blue");
				sctp_send_abort(m, iphlen, src, dst,
				    sh, 0, op_err,
				    mflowtype, mflowid, fibnum,
				    vrf_id, port);
			}
		}
		goto out;
	} else if (stcb == NULL) {
		inp_decr = inp;
	}
	SCTPDBG(SCTP_DEBUG_INPUT1, "Ok, Common input processing called, m:%p iphlen:%d offset:%d length:%d stcb:%p\n",
	    (void *)m, iphlen, offset, length, (void *)stcb);
	if (stcb) {
		/* always clear this before beginning a packet */
		stcb->asoc.authenticated = 0;
		stcb->asoc.seen_a_sack_this_pkt = 0;
		SCTPDBG(SCTP_DEBUG_INPUT1, "stcb:%p state:%x\n",
		    (void *)stcb, stcb->asoc.state);

		if ((stcb->asoc.state & SCTP_STATE_WAS_ABORTED) ||
		    (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED)) {
			/*-
			 * If we hit here, we had a ref count
			 * up when the assoc was aborted and the
			 * timer is clearing out the assoc, we should
			 * NOT respond to any packet.. its OOTB.
			 */
			SCTP_TCB_UNLOCK(stcb);
			stcb = NULL;
			SCTP_PROBE5(receive, NULL, stcb, m, stcb, sh);
			snprintf(msg, sizeof(msg), "OOTB, %s:%d at %s", __FILE__, __LINE__, __func__);
			op_err = sctp_generate_cause(SCTP_BASE_SYSCTL(sctp_diag_info_code),
			    msg);
			sctp_handle_ootb(m, iphlen, offset, src, dst, sh, inp, op_err,
			    mflowtype, mflowid, inp->fibnum,
			    vrf_id, port);
			goto out;
		}
	}
	if (IS_SCTP_CONTROL(ch)) {
		/* process the control portion of the SCTP packet */
		/* sa_ignore NO_NULL_CHK */
		stcb = sctp_process_control(m, iphlen, &offset, length,
		    src, dst, sh, ch,
		    inp, stcb, &net, &fwd_tsn_seen,
		    mflowtype, mflowid, fibnum,
		    vrf_id, port);
		if (stcb) {
			/*
			 * This covers us if the cookie-echo was there and
			 * it changes our INP.
			 */
			inp = stcb->sctp_ep;
#if defined(INET) || defined(INET6)
			if ((ch->chunk_type != SCTP_INITIATION) &&
			    (net != NULL) && (net->port != port)) {
				if (net->port == 0) {
					/* UDP encapsulation turned on. */
					net->mtu -= sizeof(struct udphdr);
					if (stcb->asoc.smallest_mtu > net->mtu) {
						sctp_pathmtu_adjustment(stcb, net->mtu);
					}
				} else if (port == 0) {
					/* UDP encapsulation turned off. */
					net->mtu += sizeof(struct udphdr);
					/* XXX Update smallest_mtu */
				}
				net->port = port;
			}
#endif
		}
	} else {
		/*
		 * no control chunks, so pre-process DATA chunks (these
		 * checks are taken care of by control processing)
		 */

		/*
		 * if DATA only packet, and auth is required, then punt...
		 * can't have authenticated without any AUTH (control)
		 * chunks
		 */
		if ((stcb != NULL) &&
		    sctp_auth_is_required_chunk(SCTP_DATA, stcb->asoc.local_auth_chunks)) {
			/* "silently" ignore */
			SCTP_PROBE5(receive, NULL, stcb, m, stcb, sh);
			SCTP_STAT_INCR(sctps_recvauthmissing);
			goto out;
		}
		if (stcb == NULL) {
			/* out of the blue DATA chunk */
			SCTP_PROBE5(receive, NULL, NULL, m, NULL, sh);
			snprintf(msg, sizeof(msg), "OOTB, %s:%d at %s", __FILE__, __LINE__, __func__);
			op_err = sctp_generate_cause(SCTP_BASE_SYSCTL(sctp_diag_info_code),
			    msg);
			sctp_handle_ootb(m, iphlen, offset, src, dst, sh, inp, op_err,
			    mflowtype, mflowid, fibnum,
			    vrf_id, port);
			goto out;
		}
		if (stcb->asoc.my_vtag != ntohl(sh->v_tag)) {
			/* v_tag mismatch! */
			SCTP_PROBE5(receive, NULL, stcb, m, stcb, sh);
			SCTP_STAT_INCR(sctps_badvtag);
			goto out;
		}
	}

	SCTP_PROBE5(receive, NULL, stcb, m, stcb, sh);
	if (stcb == NULL) {
		/*
		 * no valid TCB for this packet, or we found it's a bad
		 * packet while processing control, or we're done with this
		 * packet (done or skip rest of data), so we drop it...
		 */
		goto out;
	}

	/*
	 * DATA chunk processing
	 */
	/* plow through the data chunks while length > offset */

	/*
	 * Rest should be DATA only.  Check authentication state if AUTH for
	 * DATA is required.
	 */
	if ((length > offset) &&
	    (stcb != NULL) &&
	    sctp_auth_is_required_chunk(SCTP_DATA, stcb->asoc.local_auth_chunks) &&
	    !stcb->asoc.authenticated) {
		/* "silently" ignore */
		SCTP_STAT_INCR(sctps_recvauthmissing);
		SCTPDBG(SCTP_DEBUG_AUTH1,
		    "Data chunk requires AUTH, skipped\n");
		goto trigger_send;
	}
	if (length > offset) {
		int retval;

		/*
		 * First check to make sure our state is correct. We would
		 * not get here unless we really did have a tag, so we don't
		 * abort if this happens, just dump the chunk silently.
		 */
		switch (SCTP_GET_STATE(stcb)) {
		case SCTP_STATE_COOKIE_ECHOED:
			/*
			 * we consider data with valid tags in this state
			 * shows us the cookie-ack was lost. Imply it was
			 * there.
			 */
			sctp_handle_cookie_ack((struct sctp_cookie_ack_chunk *)ch, stcb, net);
			break;
		case SCTP_STATE_COOKIE_WAIT:
			/*
			 * We consider OOTB any data sent during asoc setup.
			 */
			snprintf(msg, sizeof(msg), "OOTB, %s:%d at %s", __FILE__, __LINE__, __func__);
			op_err = sctp_generate_cause(SCTP_BASE_SYSCTL(sctp_diag_info_code),
			    msg);
			sctp_handle_ootb(m, iphlen, offset, src, dst, sh, inp, op_err,
			    mflowtype, mflowid, inp->fibnum,
			    vrf_id, port);
			goto out;
			/* sa_ignore NOTREACHED */
			break;
		case SCTP_STATE_EMPTY:	/* should not happen */
		case SCTP_STATE_INUSE:	/* should not happen */
		case SCTP_STATE_SHUTDOWN_RECEIVED:	/* This is a peer error */
		case SCTP_STATE_SHUTDOWN_ACK_SENT:
		default:
			goto out;
			/* sa_ignore NOTREACHED */
			break;
		case SCTP_STATE_OPEN:
		case SCTP_STATE_SHUTDOWN_SENT:
			break;
		}
		/* plow through the data chunks while length > offset */
		retval = sctp_process_data(mm, iphlen, &offset, length,
		    inp, stcb, net, &high_tsn);
		if (retval == 2) {
			/*
			 * The association aborted, NO UNLOCK needed since
			 * the association is destroyed.
			 */
			stcb = NULL;
			goto out;
		}
		data_processed = 1;
		/*
		 * Anything important needs to have been m_copy'ed in
		 * process_data
		 */
	}

	/* take care of ecn */
	if ((data_processed == 1) &&
	    (stcb->asoc.ecn_supported == 1) &&
	    ((ecn_bits & SCTP_CE_BITS) == SCTP_CE_BITS)) {
		/* Yep, we need to add a ECNE */
		sctp_send_ecn_echo(stcb, net, high_tsn);
	}

	if ((data_processed == 0) && (fwd_tsn_seen)) {
		int was_a_gap;
		uint32_t highest_tsn;

		if (SCTP_TSN_GT(stcb->asoc.highest_tsn_inside_nr_map, stcb->asoc.highest_tsn_inside_map)) {
			highest_tsn = stcb->asoc.highest_tsn_inside_nr_map;
		} else {
			highest_tsn = stcb->asoc.highest_tsn_inside_map;
		}
		was_a_gap = SCTP_TSN_GT(highest_tsn, stcb->asoc.cumulative_tsn);
		stcb->asoc.send_sack = 1;
		sctp_sack_check(stcb, was_a_gap);
	} else if (fwd_tsn_seen) {
		stcb->asoc.send_sack = 1;
	}
	/* trigger send of any chunks in queue... */
trigger_send:
#ifdef SCTP_AUDITING_ENABLED
	sctp_audit_log(0xE0, 2);
	sctp_auditing(1, inp, stcb, net);
#endif
	SCTPDBG(SCTP_DEBUG_INPUT1,
	    "Check for chunk output prw:%d tqe:%d tf=%d\n",
	    stcb->asoc.peers_rwnd,
	    TAILQ_EMPTY(&stcb->asoc.control_send_queue),
	    stcb->asoc.total_flight);
	un_sent = (stcb->asoc.total_output_queue_size - stcb->asoc.total_flight);
	if (!TAILQ_EMPTY(&stcb->asoc.control_send_queue)) {
		cnt_ctrl_ready = stcb->asoc.ctrl_queue_cnt - stcb->asoc.ecn_echo_cnt_onq;
	}
	if (!TAILQ_EMPTY(&stcb->asoc.asconf_send_queue) ||
	    cnt_ctrl_ready ||
	    stcb->asoc.trigger_reset ||
	    ((un_sent) &&
	    (stcb->asoc.peers_rwnd > 0 ||
	    (stcb->asoc.peers_rwnd <= 0 && stcb->asoc.total_flight == 0)))) {
		SCTPDBG(SCTP_DEBUG_INPUT3, "Calling chunk OUTPUT\n");
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_CONTROL_PROC, SCTP_SO_NOT_LOCKED);
		SCTPDBG(SCTP_DEBUG_INPUT3, "chunk OUTPUT returns\n");
	}
#ifdef SCTP_AUDITING_ENABLED
	sctp_audit_log(0xE0, 3);
	sctp_auditing(2, inp, stcb, net);
#endif
out:
	if (stcb != NULL) {
		SCTP_TCB_UNLOCK(stcb);
	}
	if (inp_decr != NULL) {
		/* reduce ref-count */
		SCTP_INP_WLOCK(inp_decr);
		SCTP_INP_DECR_REF(inp_decr);
		SCTP_INP_WUNLOCK(inp_decr);
	}
	return;
}

#ifdef INET
void
sctp_input_with_port(struct mbuf *i_pak, int off, uint16_t port)
{
	struct mbuf *m;
	int iphlen;
	uint32_t vrf_id = 0;
	uint8_t ecn_bits;
	struct sockaddr_in src, dst;
	struct ip *ip;
	struct sctphdr *sh;
	struct sctp_chunkhdr *ch;
	int length, offset;
	uint8_t compute_crc;
	uint32_t mflowid;
	uint8_t mflowtype;
	uint16_t fibnum;

	iphlen = off;
	if (SCTP_GET_PKT_VRFID(i_pak, vrf_id)) {
		SCTP_RELEASE_PKT(i_pak);
		return;
	}
	m = SCTP_HEADER_TO_CHAIN(i_pak);
#ifdef SCTP_MBUF_LOGGING
	/* Log in any input mbufs */
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
		sctp_log_mbc(m, SCTP_MBUF_INPUT);
	}
#endif
#ifdef SCTP_PACKET_LOGGING
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LAST_PACKET_TRACING) {
		sctp_packet_log(m);
	}
#endif
	SCTPDBG(SCTP_DEBUG_CRCOFFLOAD,
	    "sctp_input(): Packet of length %d received on %s with csum_flags 0x%b.\n",
	    m->m_pkthdr.len,
	    if_name(m->m_pkthdr.rcvif),
	    (int)m->m_pkthdr.csum_flags, CSUM_BITS);
	mflowid = m->m_pkthdr.flowid;
	mflowtype = M_HASHTYPE_GET(m);
	fibnum = M_GETFIB(m);
	SCTP_STAT_INCR(sctps_recvpackets);
	SCTP_STAT_INCR_COUNTER64(sctps_inpackets);
	/* Get IP, SCTP, and first chunk header together in the first mbuf. */
	offset = iphlen + sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr);
	if (SCTP_BUF_LEN(m) < offset) {
		if ((m = m_pullup(m, offset)) == NULL) {
			SCTP_STAT_INCR(sctps_hdrops);
			return;
		}
	}
	ip = mtod(m, struct ip *);
	sh = (struct sctphdr *)((caddr_t)ip + iphlen);
	ch = (struct sctp_chunkhdr *)((caddr_t)sh + sizeof(struct sctphdr));
	offset -= sizeof(struct sctp_chunkhdr);
	memset(&src, 0, sizeof(struct sockaddr_in));
	src.sin_family = AF_INET;
	src.sin_len = sizeof(struct sockaddr_in);
	src.sin_port = sh->src_port;
	src.sin_addr = ip->ip_src;
	memset(&dst, 0, sizeof(struct sockaddr_in));
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);
	dst.sin_port = sh->dest_port;
	dst.sin_addr = ip->ip_dst;
	length = ntohs(ip->ip_len);
	/* Validate mbuf chain length with IP payload length. */
	if (SCTP_HEADER_LEN(m) != length) {
		SCTPDBG(SCTP_DEBUG_INPUT1,
		    "sctp_input() length:%d reported length:%d\n", length, SCTP_HEADER_LEN(m));
		SCTP_STAT_INCR(sctps_hdrops);
		goto out;
	}
	/* SCTP does not allow broadcasts or multicasts */
	if (IN_MULTICAST(ntohl(dst.sin_addr.s_addr))) {
		goto out;
	}
	if (SCTP_IS_IT_BROADCAST(dst.sin_addr, m)) {
		goto out;
	}
	ecn_bits = ip->ip_tos;
	if (m->m_pkthdr.csum_flags & CSUM_SCTP_VALID) {
		SCTP_STAT_INCR(sctps_recvhwcrc);
		compute_crc = 0;
	} else {
		SCTP_STAT_INCR(sctps_recvswcrc);
		compute_crc = 1;
	}
	sctp_common_input_processing(&m, iphlen, offset, length,
	    (struct sockaddr *)&src,
	    (struct sockaddr *)&dst,
	    sh, ch,
	    compute_crc,
	    ecn_bits,
	    mflowtype, mflowid, fibnum,
	    vrf_id, port);
out:
	if (m) {
		sctp_m_freem(m);
	}
	return;
}

#if defined(__FreeBSD__) && defined(SCTP_MCORE_INPUT) && defined(SMP)
extern int *sctp_cpuarry;
#endif

int
sctp_input(struct mbuf **mp, int *offp, int proto SCTP_UNUSED)
{
	struct mbuf *m;
	int off;

	m = *mp;
	off = *offp;
#if defined(__FreeBSD__) && defined(SCTP_MCORE_INPUT) && defined(SMP)
	if (mp_ncpus > 1) {
		struct ip *ip;
		struct sctphdr *sh;
		int offset;
		int cpu_to_use;
		uint32_t flowid, tag;

		if (M_HASHTYPE_GET(m) != M_HASHTYPE_NONE) {
			flowid = m->m_pkthdr.flowid;
		} else {
			/*
			 * No flow id built by lower layers fix it so we
			 * create one.
			 */
			offset = off + sizeof(struct sctphdr);
			if (SCTP_BUF_LEN(m) < offset) {
				if ((m = m_pullup(m, offset)) == NULL) {
					SCTP_STAT_INCR(sctps_hdrops);
					return (IPPROTO_DONE);
				}
			}
			ip = mtod(m, struct ip *);
			sh = (struct sctphdr *)((caddr_t)ip + off);
			tag = htonl(sh->v_tag);
			flowid = tag ^ ntohs(sh->dest_port) ^ ntohs(sh->src_port);
			m->m_pkthdr.flowid = flowid;
			M_HASHTYPE_SET(m, M_HASHTYPE_OPAQUE_HASH);
		}
		cpu_to_use = sctp_cpuarry[flowid % mp_ncpus];
		sctp_queue_to_mcore(m, off, cpu_to_use);
		return (IPPROTO_DONE);
	}
#endif
	sctp_input_with_port(m, off, 0);
	return (IPPROTO_DONE);
}
#endif
