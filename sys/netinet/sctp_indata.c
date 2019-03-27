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
#include <sys/proc.h>
#include <netinet/sctp_var.h>
#include <netinet/sctp_sysctl.h>
#include <netinet/sctp_header.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctputil.h>
#include <netinet/sctp_output.h>
#include <netinet/sctp_uio.h>
#include <netinet/sctp_auth.h>
#include <netinet/sctp_timer.h>
#include <netinet/sctp_asconf.h>
#include <netinet/sctp_indata.h>
#include <netinet/sctp_bsd_addr.h>
#include <netinet/sctp_input.h>
#include <netinet/sctp_crc32.h>
#include <netinet/sctp_lock_bsd.h>
/*
 * NOTES: On the outbound side of things I need to check the sack timer to
 * see if I should generate a sack into the chunk queue (if I have data to
 * send that is and will be sending it .. for bundling.
 *
 * The callback in sctp_usrreq.c will get called when the socket is read from.
 * This will cause sctp_service_queues() to get called on the top entry in
 * the list.
 */
static uint32_t
sctp_add_chk_to_control(struct sctp_queued_to_read *control,
    struct sctp_stream_in *strm,
    struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    struct sctp_tmit_chunk *chk, int lock_held);


void
sctp_set_rwnd(struct sctp_tcb *stcb, struct sctp_association *asoc)
{
	asoc->my_rwnd = sctp_calc_rwnd(stcb, asoc);
}

/* Calculate what the rwnd would be */
uint32_t
sctp_calc_rwnd(struct sctp_tcb *stcb, struct sctp_association *asoc)
{
	uint32_t calc = 0;

	/*
	 * This is really set wrong with respect to a 1-2-m socket. Since
	 * the sb_cc is the count that everyone as put up. When we re-write
	 * sctp_soreceive then we will fix this so that ONLY this
	 * associations data is taken into account.
	 */
	if (stcb->sctp_socket == NULL) {
		return (calc);
	}

	KASSERT(asoc->cnt_on_reasm_queue > 0 || asoc->size_on_reasm_queue == 0,
	    ("size_on_reasm_queue is %u", asoc->size_on_reasm_queue));
	KASSERT(asoc->cnt_on_all_streams > 0 || asoc->size_on_all_streams == 0,
	    ("size_on_all_streams is %u", asoc->size_on_all_streams));
	if (stcb->asoc.sb_cc == 0 &&
	    asoc->cnt_on_reasm_queue == 0 &&
	    asoc->cnt_on_all_streams == 0) {
		/* Full rwnd granted */
		calc = max(SCTP_SB_LIMIT_RCV(stcb->sctp_socket), SCTP_MINIMAL_RWND);
		return (calc);
	}
	/* get actual space */
	calc = (uint32_t)sctp_sbspace(&stcb->asoc, &stcb->sctp_socket->so_rcv);
	/*
	 * take out what has NOT been put on socket queue and we yet hold
	 * for putting up.
	 */
	calc = sctp_sbspace_sub(calc, (uint32_t)(asoc->size_on_reasm_queue +
	    asoc->cnt_on_reasm_queue * MSIZE));
	calc = sctp_sbspace_sub(calc, (uint32_t)(asoc->size_on_all_streams +
	    asoc->cnt_on_all_streams * MSIZE));
	if (calc == 0) {
		/* out of space */
		return (calc);
	}

	/* what is the overhead of all these rwnd's */
	calc = sctp_sbspace_sub(calc, stcb->asoc.my_rwnd_control_len);
	/*
	 * If the window gets too small due to ctrl-stuff, reduce it to 1,
	 * even it is 0. SWS engaged
	 */
	if (calc < stcb->asoc.my_rwnd_control_len) {
		calc = 1;
	}
	return (calc);
}



/*
 * Build out our readq entry based on the incoming packet.
 */
struct sctp_queued_to_read *
sctp_build_readq_entry(struct sctp_tcb *stcb,
    struct sctp_nets *net,
    uint32_t tsn, uint32_t ppid,
    uint32_t context, uint16_t sid,
    uint32_t mid, uint8_t flags,
    struct mbuf *dm)
{
	struct sctp_queued_to_read *read_queue_e = NULL;

	sctp_alloc_a_readq(stcb, read_queue_e);
	if (read_queue_e == NULL) {
		goto failed_build;
	}
	memset(read_queue_e, 0, sizeof(struct sctp_queued_to_read));
	read_queue_e->sinfo_stream = sid;
	read_queue_e->sinfo_flags = (flags << 8);
	read_queue_e->sinfo_ppid = ppid;
	read_queue_e->sinfo_context = context;
	read_queue_e->sinfo_tsn = tsn;
	read_queue_e->sinfo_cumtsn = tsn;
	read_queue_e->sinfo_assoc_id = sctp_get_associd(stcb);
	read_queue_e->mid = mid;
	read_queue_e->top_fsn = read_queue_e->fsn_included = 0xffffffff;
	TAILQ_INIT(&read_queue_e->reasm);
	read_queue_e->whoFrom = net;
	atomic_add_int(&net->ref_count, 1);
	read_queue_e->data = dm;
	read_queue_e->stcb = stcb;
	read_queue_e->port_from = stcb->rport;
failed_build:
	return (read_queue_e);
}

struct mbuf *
sctp_build_ctl_nchunk(struct sctp_inpcb *inp, struct sctp_sndrcvinfo *sinfo)
{
	struct sctp_extrcvinfo *seinfo;
	struct sctp_sndrcvinfo *outinfo;
	struct sctp_rcvinfo *rcvinfo;
	struct sctp_nxtinfo *nxtinfo;
	struct cmsghdr *cmh;
	struct mbuf *ret;
	int len;
	int use_extended;
	int provide_nxt;

	if (sctp_is_feature_off(inp, SCTP_PCB_FLAGS_RECVDATAIOEVNT) &&
	    sctp_is_feature_off(inp, SCTP_PCB_FLAGS_RECVRCVINFO) &&
	    sctp_is_feature_off(inp, SCTP_PCB_FLAGS_RECVNXTINFO)) {
		/* user does not want any ancillary data */
		return (NULL);
	}

	len = 0;
	if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVRCVINFO)) {
		len += CMSG_SPACE(sizeof(struct sctp_rcvinfo));
	}
	seinfo = (struct sctp_extrcvinfo *)sinfo;
	if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVNXTINFO) &&
	    (seinfo->serinfo_next_flags & SCTP_NEXT_MSG_AVAIL)) {
		provide_nxt = 1;
		len += CMSG_SPACE(sizeof(struct sctp_nxtinfo));
	} else {
		provide_nxt = 0;
	}
	if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVDATAIOEVNT)) {
		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_EXT_RCVINFO)) {
			use_extended = 1;
			len += CMSG_SPACE(sizeof(struct sctp_extrcvinfo));
		} else {
			use_extended = 0;
			len += CMSG_SPACE(sizeof(struct sctp_sndrcvinfo));
		}
	} else {
		use_extended = 0;
	}

	ret = sctp_get_mbuf_for_msg(len, 0, M_NOWAIT, 1, MT_DATA);
	if (ret == NULL) {
		/* No space */
		return (ret);
	}
	SCTP_BUF_LEN(ret) = 0;

	/* We need a CMSG header followed by the struct */
	cmh = mtod(ret, struct cmsghdr *);
	/*
	 * Make sure that there is no un-initialized padding between the
	 * cmsg header and cmsg data and after the cmsg data.
	 */
	memset(cmh, 0, len);
	if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVRCVINFO)) {
		cmh->cmsg_level = IPPROTO_SCTP;
		cmh->cmsg_len = CMSG_LEN(sizeof(struct sctp_rcvinfo));
		cmh->cmsg_type = SCTP_RCVINFO;
		rcvinfo = (struct sctp_rcvinfo *)CMSG_DATA(cmh);
		rcvinfo->rcv_sid = sinfo->sinfo_stream;
		rcvinfo->rcv_ssn = sinfo->sinfo_ssn;
		rcvinfo->rcv_flags = sinfo->sinfo_flags;
		rcvinfo->rcv_ppid = sinfo->sinfo_ppid;
		rcvinfo->rcv_tsn = sinfo->sinfo_tsn;
		rcvinfo->rcv_cumtsn = sinfo->sinfo_cumtsn;
		rcvinfo->rcv_context = sinfo->sinfo_context;
		rcvinfo->rcv_assoc_id = sinfo->sinfo_assoc_id;
		cmh = (struct cmsghdr *)((caddr_t)cmh + CMSG_SPACE(sizeof(struct sctp_rcvinfo)));
		SCTP_BUF_LEN(ret) += CMSG_SPACE(sizeof(struct sctp_rcvinfo));
	}
	if (provide_nxt) {
		cmh->cmsg_level = IPPROTO_SCTP;
		cmh->cmsg_len = CMSG_LEN(sizeof(struct sctp_nxtinfo));
		cmh->cmsg_type = SCTP_NXTINFO;
		nxtinfo = (struct sctp_nxtinfo *)CMSG_DATA(cmh);
		nxtinfo->nxt_sid = seinfo->serinfo_next_stream;
		nxtinfo->nxt_flags = 0;
		if (seinfo->serinfo_next_flags & SCTP_NEXT_MSG_IS_UNORDERED) {
			nxtinfo->nxt_flags |= SCTP_UNORDERED;
		}
		if (seinfo->serinfo_next_flags & SCTP_NEXT_MSG_IS_NOTIFICATION) {
			nxtinfo->nxt_flags |= SCTP_NOTIFICATION;
		}
		if (seinfo->serinfo_next_flags & SCTP_NEXT_MSG_ISCOMPLETE) {
			nxtinfo->nxt_flags |= SCTP_COMPLETE;
		}
		nxtinfo->nxt_ppid = seinfo->serinfo_next_ppid;
		nxtinfo->nxt_length = seinfo->serinfo_next_length;
		nxtinfo->nxt_assoc_id = seinfo->serinfo_next_aid;
		cmh = (struct cmsghdr *)((caddr_t)cmh + CMSG_SPACE(sizeof(struct sctp_nxtinfo)));
		SCTP_BUF_LEN(ret) += CMSG_SPACE(sizeof(struct sctp_nxtinfo));
	}
	if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_RECVDATAIOEVNT)) {
		cmh->cmsg_level = IPPROTO_SCTP;
		outinfo = (struct sctp_sndrcvinfo *)CMSG_DATA(cmh);
		if (use_extended) {
			cmh->cmsg_len = CMSG_LEN(sizeof(struct sctp_extrcvinfo));
			cmh->cmsg_type = SCTP_EXTRCV;
			memcpy(outinfo, sinfo, sizeof(struct sctp_extrcvinfo));
			SCTP_BUF_LEN(ret) += CMSG_SPACE(sizeof(struct sctp_extrcvinfo));
		} else {
			cmh->cmsg_len = CMSG_LEN(sizeof(struct sctp_sndrcvinfo));
			cmh->cmsg_type = SCTP_SNDRCV;
			*outinfo = *sinfo;
			SCTP_BUF_LEN(ret) += CMSG_SPACE(sizeof(struct sctp_sndrcvinfo));
		}
	}
	return (ret);
}


static void
sctp_mark_non_revokable(struct sctp_association *asoc, uint32_t tsn)
{
	uint32_t gap, i, cumackp1;
	int fnd = 0;
	int in_r = 0, in_nr = 0;

	if (SCTP_BASE_SYSCTL(sctp_do_drain) == 0) {
		return;
	}
	cumackp1 = asoc->cumulative_tsn + 1;
	if (SCTP_TSN_GT(cumackp1, tsn)) {
		/*
		 * this tsn is behind the cum ack and thus we don't need to
		 * worry about it being moved from one to the other.
		 */
		return;
	}
	SCTP_CALC_TSN_TO_GAP(gap, tsn, asoc->mapping_array_base_tsn);
	in_r = SCTP_IS_TSN_PRESENT(asoc->mapping_array, gap);
	in_nr = SCTP_IS_TSN_PRESENT(asoc->nr_mapping_array, gap);
	if ((in_r == 0) && (in_nr == 0)) {
#ifdef INVARIANTS
		panic("Things are really messed up now");
#else
		SCTP_PRINTF("gap:%x tsn:%x\n", gap, tsn);
		sctp_print_mapping_array(asoc);
#endif
	}
	if (in_nr == 0)
		SCTP_SET_TSN_PRESENT(asoc->nr_mapping_array, gap);
	if (in_r)
		SCTP_UNSET_TSN_PRESENT(asoc->mapping_array, gap);
	if (SCTP_TSN_GT(tsn, asoc->highest_tsn_inside_nr_map)) {
		asoc->highest_tsn_inside_nr_map = tsn;
	}
	if (tsn == asoc->highest_tsn_inside_map) {
		/* We must back down to see what the new highest is */
		for (i = tsn - 1; SCTP_TSN_GE(i, asoc->mapping_array_base_tsn); i--) {
			SCTP_CALC_TSN_TO_GAP(gap, i, asoc->mapping_array_base_tsn);
			if (SCTP_IS_TSN_PRESENT(asoc->mapping_array, gap)) {
				asoc->highest_tsn_inside_map = i;
				fnd = 1;
				break;
			}
		}
		if (!fnd) {
			asoc->highest_tsn_inside_map = asoc->mapping_array_base_tsn - 1;
		}
	}
}

static int
sctp_place_control_in_stream(struct sctp_stream_in *strm,
    struct sctp_association *asoc,
    struct sctp_queued_to_read *control)
{
	struct sctp_queued_to_read *at;
	struct sctp_readhead *q;
	uint8_t flags, unordered;

	flags = (control->sinfo_flags >> 8);
	unordered = flags & SCTP_DATA_UNORDERED;
	if (unordered) {
		q = &strm->uno_inqueue;
		if (asoc->idata_supported == 0) {
			if (!TAILQ_EMPTY(q)) {
				/*
				 * Only one stream can be here in old style
				 * -- abort
				 */
				return (-1);
			}
			TAILQ_INSERT_TAIL(q, control, next_instrm);
			control->on_strm_q = SCTP_ON_UNORDERED;
			return (0);
		}
	} else {
		q = &strm->inqueue;
	}
	if ((flags & SCTP_DATA_NOT_FRAG) == SCTP_DATA_NOT_FRAG) {
		control->end_added = 1;
		control->first_frag_seen = 1;
		control->last_frag_seen = 1;
	}
	if (TAILQ_EMPTY(q)) {
		/* Empty queue */
		TAILQ_INSERT_HEAD(q, control, next_instrm);
		if (unordered) {
			control->on_strm_q = SCTP_ON_UNORDERED;
		} else {
			control->on_strm_q = SCTP_ON_ORDERED;
		}
		return (0);
	} else {
		TAILQ_FOREACH(at, q, next_instrm) {
			if (SCTP_MID_GT(asoc->idata_supported, at->mid, control->mid)) {
				/*
				 * one in queue is bigger than the new one,
				 * insert before this one
				 */
				TAILQ_INSERT_BEFORE(at, control, next_instrm);
				if (unordered) {
					control->on_strm_q = SCTP_ON_UNORDERED;
				} else {
					control->on_strm_q = SCTP_ON_ORDERED;
				}
				break;
			} else if (SCTP_MID_EQ(asoc->idata_supported, at->mid, control->mid)) {
				/*
				 * Gak, He sent me a duplicate msg id
				 * number?? return -1 to abort.
				 */
				return (-1);
			} else {
				if (TAILQ_NEXT(at, next_instrm) == NULL) {
					/*
					 * We are at the end, insert it
					 * after this one
					 */
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_STR_LOGGING_ENABLE) {
						sctp_log_strm_del(control, at,
						    SCTP_STR_LOG_FROM_INSERT_TL);
					}
					TAILQ_INSERT_AFTER(q, at, control, next_instrm);
					if (unordered) {
						control->on_strm_q = SCTP_ON_UNORDERED;
					} else {
						control->on_strm_q = SCTP_ON_ORDERED;
					}
					break;
				}
			}
		}
	}
	return (0);
}

static void
sctp_abort_in_reasm(struct sctp_tcb *stcb,
    struct sctp_queued_to_read *control,
    struct sctp_tmit_chunk *chk,
    int *abort_flag, int opspot)
{
	char msg[SCTP_DIAG_INFO_LEN];
	struct mbuf *oper;

	if (stcb->asoc.idata_supported) {
		snprintf(msg, sizeof(msg),
		    "Reass %x,CF:%x,TSN=%8.8x,SID=%4.4x,FSN=%8.8x,MID:%8.8x",
		    opspot,
		    control->fsn_included,
		    chk->rec.data.tsn,
		    chk->rec.data.sid,
		    chk->rec.data.fsn, chk->rec.data.mid);
	} else {
		snprintf(msg, sizeof(msg),
		    "Reass %x,CI:%x,TSN=%8.8x,SID=%4.4x,FSN=%4.4x,SSN:%4.4x",
		    opspot,
		    control->fsn_included,
		    chk->rec.data.tsn,
		    chk->rec.data.sid,
		    chk->rec.data.fsn,
		    (uint16_t)chk->rec.data.mid);
	}
	oper = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
	sctp_m_freem(chk->data);
	chk->data = NULL;
	sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
	stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_1;
	sctp_abort_an_association(stcb->sctp_ep, stcb, oper, SCTP_SO_NOT_LOCKED);
	*abort_flag = 1;
}

static void
sctp_clean_up_control(struct sctp_tcb *stcb, struct sctp_queued_to_read *control)
{
	/*
	 * The control could not be placed and must be cleaned.
	 */
	struct sctp_tmit_chunk *chk, *nchk;

	TAILQ_FOREACH_SAFE(chk, &control->reasm, sctp_next, nchk) {
		TAILQ_REMOVE(&control->reasm, chk, sctp_next);
		if (chk->data)
			sctp_m_freem(chk->data);
		chk->data = NULL;
		sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
	}
	sctp_free_a_readq(stcb, control);
}

/*
 * Queue the chunk either right into the socket buffer if it is the next one
 * to go OR put it in the correct place in the delivery queue.  If we do
 * append to the so_buf, keep doing so until we are out of order as
 * long as the control's entered are non-fragmented.
 */
static void
sctp_queue_data_to_stream(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    struct sctp_queued_to_read *control, int *abort_flag, int *need_reasm)
{
	/*
	 * FIX-ME maybe? What happens when the ssn wraps? If we are getting
	 * all the data in one stream this could happen quite rapidly. One
	 * could use the TSN to keep track of things, but this scheme breaks
	 * down in the other type of stream usage that could occur. Send a
	 * single msg to stream 0, send 4Billion messages to stream 1, now
	 * send a message to stream 0. You have a situation where the TSN
	 * has wrapped but not in the stream. Is this worth worrying about
	 * or should we just change our queue sort at the bottom to be by
	 * TSN.
	 *
	 * Could it also be legal for a peer to send ssn 1 with TSN 2 and
	 * ssn 2 with TSN 1? If the peer is doing some sort of funky TSN/SSN
	 * assignment this could happen... and I don't see how this would be
	 * a violation. So for now I am undecided an will leave the sort by
	 * SSN alone. Maybe a hybred approach is the answer
	 *
	 */
	struct sctp_queued_to_read *at;
	int queue_needed;
	uint32_t nxt_todel;
	struct mbuf *op_err;
	struct sctp_stream_in *strm;
	char msg[SCTP_DIAG_INFO_LEN];

	strm = &asoc->strmin[control->sinfo_stream];
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_STR_LOGGING_ENABLE) {
		sctp_log_strm_del(control, NULL, SCTP_STR_LOG_FROM_INTO_STRD);
	}
	if (SCTP_MID_GT((asoc->idata_supported), strm->last_mid_delivered, control->mid)) {
		/* The incoming sseq is behind where we last delivered? */
		SCTPDBG(SCTP_DEBUG_INDATA1, "Duplicate S-SEQ: %u delivered: %u from peer, Abort association\n",
		    strm->last_mid_delivered, control->mid);
		/*
		 * throw it in the stream so it gets cleaned up in
		 * association destruction
		 */
		TAILQ_INSERT_HEAD(&strm->inqueue, control, next_instrm);
		if (asoc->idata_supported) {
			snprintf(msg, sizeof(msg), "Delivered MID=%8.8x, got TSN=%8.8x, SID=%4.4x, MID=%8.8x",
			    strm->last_mid_delivered, control->sinfo_tsn,
			    control->sinfo_stream, control->mid);
		} else {
			snprintf(msg, sizeof(msg), "Delivered SSN=%4.4x, got TSN=%8.8x, SID=%4.4x, SSN=%4.4x",
			    (uint16_t)strm->last_mid_delivered,
			    control->sinfo_tsn,
			    control->sinfo_stream,
			    (uint16_t)control->mid);
		}
		op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
		stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_2;
		sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
		*abort_flag = 1;
		return;

	}
	queue_needed = 1;
	asoc->size_on_all_streams += control->length;
	sctp_ucount_incr(asoc->cnt_on_all_streams);
	nxt_todel = strm->last_mid_delivered + 1;
	if (SCTP_MID_EQ(asoc->idata_supported, nxt_todel, control->mid)) {
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		struct socket *so;

		so = SCTP_INP_SO(stcb->sctp_ep);
		atomic_add_int(&stcb->asoc.refcnt, 1);
		SCTP_TCB_UNLOCK(stcb);
		SCTP_SOCKET_LOCK(so, 1);
		SCTP_TCB_LOCK(stcb);
		atomic_subtract_int(&stcb->asoc.refcnt, 1);
		if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) {
			SCTP_SOCKET_UNLOCK(so, 1);
			return;
		}
#endif
		/* can be delivered right away? */
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_STR_LOGGING_ENABLE) {
			sctp_log_strm_del(control, NULL, SCTP_STR_LOG_FROM_IMMED_DEL);
		}
		/* EY it wont be queued if it could be delivered directly */
		queue_needed = 0;
		if (asoc->size_on_all_streams >= control->length) {
			asoc->size_on_all_streams -= control->length;
		} else {
#ifdef INVARIANTS
			panic("size_on_all_streams = %u smaller than control length %u", asoc->size_on_all_streams, control->length);
#else
			asoc->size_on_all_streams = 0;
#endif
		}
		sctp_ucount_decr(asoc->cnt_on_all_streams);
		strm->last_mid_delivered++;
		sctp_mark_non_revokable(asoc, control->sinfo_tsn);
		sctp_add_to_readq(stcb->sctp_ep, stcb,
		    control,
		    &stcb->sctp_socket->so_rcv, 1,
		    SCTP_READ_LOCK_NOT_HELD, SCTP_SO_LOCKED);
		TAILQ_FOREACH_SAFE(control, &strm->inqueue, next_instrm, at) {
			/* all delivered */
			nxt_todel = strm->last_mid_delivered + 1;
			if (SCTP_MID_EQ(asoc->idata_supported, nxt_todel, control->mid) &&
			    (((control->sinfo_flags >> 8) & SCTP_DATA_NOT_FRAG) == SCTP_DATA_NOT_FRAG)) {
				if (control->on_strm_q == SCTP_ON_ORDERED) {
					TAILQ_REMOVE(&strm->inqueue, control, next_instrm);
					if (asoc->size_on_all_streams >= control->length) {
						asoc->size_on_all_streams -= control->length;
					} else {
#ifdef INVARIANTS
						panic("size_on_all_streams = %u smaller than control length %u", asoc->size_on_all_streams, control->length);
#else
						asoc->size_on_all_streams = 0;
#endif
					}
					sctp_ucount_decr(asoc->cnt_on_all_streams);
#ifdef INVARIANTS
				} else {
					panic("Huh control: %p is on_strm_q: %d",
					    control, control->on_strm_q);
#endif
				}
				control->on_strm_q = 0;
				strm->last_mid_delivered++;
				/*
				 * We ignore the return of deliver_data here
				 * since we always can hold the chunk on the
				 * d-queue. And we have a finite number that
				 * can be delivered from the strq.
				 */
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_STR_LOGGING_ENABLE) {
					sctp_log_strm_del(control, NULL,
					    SCTP_STR_LOG_FROM_IMMED_DEL);
				}
				sctp_mark_non_revokable(asoc, control->sinfo_tsn);
				sctp_add_to_readq(stcb->sctp_ep, stcb,
				    control,
				    &stcb->sctp_socket->so_rcv, 1,
				    SCTP_READ_LOCK_NOT_HELD,
				    SCTP_SO_LOCKED);
				continue;
			} else if (SCTP_MID_EQ(asoc->idata_supported, nxt_todel, control->mid)) {
				*need_reasm = 1;
			}
			break;
		}
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
	}
	if (queue_needed) {
		/*
		 * Ok, we did not deliver this guy, find the correct place
		 * to put it on the queue.
		 */
		if (sctp_place_control_in_stream(strm, asoc, control)) {
			snprintf(msg, sizeof(msg),
			    "Queue to str MID: %u duplicate",
			    control->mid);
			sctp_clean_up_control(stcb, control);
			op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
			stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_3;
			sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
			*abort_flag = 1;
		}
	}
}


static void
sctp_setup_tail_pointer(struct sctp_queued_to_read *control)
{
	struct mbuf *m, *prev = NULL;
	struct sctp_tcb *stcb;

	stcb = control->stcb;
	control->held_length = 0;
	control->length = 0;
	m = control->data;
	while (m) {
		if (SCTP_BUF_LEN(m) == 0) {
			/* Skip mbufs with NO length */
			if (prev == NULL) {
				/* First one */
				control->data = sctp_m_free(m);
				m = control->data;
			} else {
				SCTP_BUF_NEXT(prev) = sctp_m_free(m);
				m = SCTP_BUF_NEXT(prev);
			}
			if (m == NULL) {
				control->tail_mbuf = prev;
			}
			continue;
		}
		prev = m;
		atomic_add_int(&control->length, SCTP_BUF_LEN(m));
		if (control->on_read_q) {
			/*
			 * On read queue so we must increment the SB stuff,
			 * we assume caller has done any locks of SB.
			 */
			sctp_sballoc(stcb, &stcb->sctp_socket->so_rcv, m);
		}
		m = SCTP_BUF_NEXT(m);
	}
	if (prev) {
		control->tail_mbuf = prev;
	}
}

static void
sctp_add_to_tail_pointer(struct sctp_queued_to_read *control, struct mbuf *m, uint32_t *added)
{
	struct mbuf *prev = NULL;
	struct sctp_tcb *stcb;

	stcb = control->stcb;
	if (stcb == NULL) {
#ifdef INVARIANTS
		panic("Control broken");
#else
		return;
#endif
	}
	if (control->tail_mbuf == NULL) {
		/* TSNH */
		control->data = m;
		sctp_setup_tail_pointer(control);
		return;
	}
	control->tail_mbuf->m_next = m;
	while (m) {
		if (SCTP_BUF_LEN(m) == 0) {
			/* Skip mbufs with NO length */
			if (prev == NULL) {
				/* First one */
				control->tail_mbuf->m_next = sctp_m_free(m);
				m = control->tail_mbuf->m_next;
			} else {
				SCTP_BUF_NEXT(prev) = sctp_m_free(m);
				m = SCTP_BUF_NEXT(prev);
			}
			if (m == NULL) {
				control->tail_mbuf = prev;
			}
			continue;
		}
		prev = m;
		if (control->on_read_q) {
			/*
			 * On read queue so we must increment the SB stuff,
			 * we assume caller has done any locks of SB.
			 */
			sctp_sballoc(stcb, &stcb->sctp_socket->so_rcv, m);
		}
		*added += SCTP_BUF_LEN(m);
		atomic_add_int(&control->length, SCTP_BUF_LEN(m));
		m = SCTP_BUF_NEXT(m);
	}
	if (prev) {
		control->tail_mbuf = prev;
	}
}

static void
sctp_build_readq_entry_from_ctl(struct sctp_queued_to_read *nc, struct sctp_queued_to_read *control)
{
	memset(nc, 0, sizeof(struct sctp_queued_to_read));
	nc->sinfo_stream = control->sinfo_stream;
	nc->mid = control->mid;
	TAILQ_INIT(&nc->reasm);
	nc->top_fsn = control->top_fsn;
	nc->mid = control->mid;
	nc->sinfo_flags = control->sinfo_flags;
	nc->sinfo_ppid = control->sinfo_ppid;
	nc->sinfo_context = control->sinfo_context;
	nc->fsn_included = 0xffffffff;
	nc->sinfo_tsn = control->sinfo_tsn;
	nc->sinfo_cumtsn = control->sinfo_cumtsn;
	nc->sinfo_assoc_id = control->sinfo_assoc_id;
	nc->whoFrom = control->whoFrom;
	atomic_add_int(&nc->whoFrom->ref_count, 1);
	nc->stcb = control->stcb;
	nc->port_from = control->port_from;
}

static void
sctp_reset_a_control(struct sctp_queued_to_read *control,
    struct sctp_inpcb *inp, uint32_t tsn)
{
	control->fsn_included = tsn;
	if (control->on_read_q) {
		/*
		 * We have to purge it from there, hopefully this will work
		 * :-)
		 */
		TAILQ_REMOVE(&inp->read_queue, control, next);
		control->on_read_q = 0;
	}
}

static int
sctp_handle_old_unordered_data(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    struct sctp_stream_in *strm,
    struct sctp_queued_to_read *control,
    uint32_t pd_point,
    int inp_read_lock_held)
{
	/*
	 * Special handling for the old un-ordered data chunk. All the
	 * chunks/TSN's go to mid 0. So we have to do the old style watching
	 * to see if we have it all. If you return one, no other control
	 * entries on the un-ordered queue will be looked at. In theory
	 * there should be no others entries in reality, unless the guy is
	 * sending both unordered NDATA and unordered DATA...
	 */
	struct sctp_tmit_chunk *chk, *lchk, *tchk;
	uint32_t fsn;
	struct sctp_queued_to_read *nc;
	int cnt_added;

	if (control->first_frag_seen == 0) {
		/* Nothing we can do, we have not seen the first piece yet */
		return (1);
	}
	/* Collapse any we can */
	cnt_added = 0;
restart:
	fsn = control->fsn_included + 1;
	/* Now what can we add? */
	TAILQ_FOREACH_SAFE(chk, &control->reasm, sctp_next, lchk) {
		if (chk->rec.data.fsn == fsn) {
			/* Ok lets add it */
			sctp_alloc_a_readq(stcb, nc);
			if (nc == NULL) {
				break;
			}
			memset(nc, 0, sizeof(struct sctp_queued_to_read));
			TAILQ_REMOVE(&control->reasm, chk, sctp_next);
			sctp_add_chk_to_control(control, strm, stcb, asoc, chk, SCTP_READ_LOCK_NOT_HELD);
			fsn++;
			cnt_added++;
			chk = NULL;
			if (control->end_added) {
				/* We are done */
				if (!TAILQ_EMPTY(&control->reasm)) {
					/*
					 * Ok we have to move anything left
					 * on the control queue to a new
					 * control.
					 */
					sctp_build_readq_entry_from_ctl(nc, control);
					tchk = TAILQ_FIRST(&control->reasm);
					if (tchk->rec.data.rcv_flags & SCTP_DATA_FIRST_FRAG) {
						TAILQ_REMOVE(&control->reasm, tchk, sctp_next);
						if (asoc->size_on_reasm_queue >= tchk->send_size) {
							asoc->size_on_reasm_queue -= tchk->send_size;
						} else {
#ifdef INVARIANTS
							panic("size_on_reasm_queue = %u smaller than chunk length %u", asoc->size_on_reasm_queue, tchk->send_size);
#else
							asoc->size_on_reasm_queue = 0;
#endif
						}
						sctp_ucount_decr(asoc->cnt_on_reasm_queue);
						nc->first_frag_seen = 1;
						nc->fsn_included = tchk->rec.data.fsn;
						nc->data = tchk->data;
						nc->sinfo_ppid = tchk->rec.data.ppid;
						nc->sinfo_tsn = tchk->rec.data.tsn;
						sctp_mark_non_revokable(asoc, tchk->rec.data.tsn);
						tchk->data = NULL;
						sctp_free_a_chunk(stcb, tchk, SCTP_SO_NOT_LOCKED);
						sctp_setup_tail_pointer(nc);
						tchk = TAILQ_FIRST(&control->reasm);
					}
					/* Spin the rest onto the queue */
					while (tchk) {
						TAILQ_REMOVE(&control->reasm, tchk, sctp_next);
						TAILQ_INSERT_TAIL(&nc->reasm, tchk, sctp_next);
						tchk = TAILQ_FIRST(&control->reasm);
					}
					/*
					 * Now lets add it to the queue
					 * after removing control
					 */
					TAILQ_INSERT_TAIL(&strm->uno_inqueue, nc, next_instrm);
					nc->on_strm_q = SCTP_ON_UNORDERED;
					if (control->on_strm_q) {
						TAILQ_REMOVE(&strm->uno_inqueue, control, next_instrm);
						control->on_strm_q = 0;
					}
				}
				if (control->pdapi_started) {
					strm->pd_api_started = 0;
					control->pdapi_started = 0;
				}
				if (control->on_strm_q) {
					TAILQ_REMOVE(&strm->uno_inqueue, control, next_instrm);
					control->on_strm_q = 0;
					SCTP_STAT_INCR_COUNTER64(sctps_reasmusrmsgs);
				}
				if (control->on_read_q == 0) {
					sctp_add_to_readq(stcb->sctp_ep, stcb, control,
					    &stcb->sctp_socket->so_rcv, control->end_added,
					    inp_read_lock_held, SCTP_SO_NOT_LOCKED);
				}
				sctp_wakeup_the_read_socket(stcb->sctp_ep, stcb, SCTP_SO_NOT_LOCKED);
				if ((nc->first_frag_seen) && !TAILQ_EMPTY(&nc->reasm)) {
					/*
					 * Switch to the new guy and
					 * continue
					 */
					control = nc;
					goto restart;
				} else {
					if (nc->on_strm_q == 0) {
						sctp_free_a_readq(stcb, nc);
					}
				}
				return (1);
			} else {
				sctp_free_a_readq(stcb, nc);
			}
		} else {
			/* Can't add more */
			break;
		}
	}
	if ((control->length > pd_point) && (strm->pd_api_started == 0)) {
		strm->pd_api_started = 1;
		control->pdapi_started = 1;
		sctp_add_to_readq(stcb->sctp_ep, stcb, control,
		    &stcb->sctp_socket->so_rcv, control->end_added,
		    inp_read_lock_held, SCTP_SO_NOT_LOCKED);
		sctp_wakeup_the_read_socket(stcb->sctp_ep, stcb, SCTP_SO_NOT_LOCKED);
		return (0);
	} else {
		return (1);
	}
}

static void
sctp_inject_old_unordered_data(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    struct sctp_queued_to_read *control,
    struct sctp_tmit_chunk *chk,
    int *abort_flag)
{
	struct sctp_tmit_chunk *at;
	int inserted;

	/*
	 * Here we need to place the chunk into the control structure sorted
	 * in the correct order.
	 */
	if (chk->rec.data.rcv_flags & SCTP_DATA_FIRST_FRAG) {
		/* Its the very first one. */
		SCTPDBG(SCTP_DEBUG_XXX,
		    "chunk is a first fsn: %u becomes fsn_included\n",
		    chk->rec.data.fsn);
		at = TAILQ_FIRST(&control->reasm);
		if (at && SCTP_TSN_GT(chk->rec.data.fsn, at->rec.data.fsn)) {
			/*
			 * The first chunk in the reassembly is a smaller
			 * TSN than this one, even though this has a first,
			 * it must be from a subsequent msg.
			 */
			goto place_chunk;
		}
		if (control->first_frag_seen) {
			/*
			 * In old un-ordered we can reassembly on one
			 * control multiple messages. As long as the next
			 * FIRST is greater then the old first (TSN i.e. FSN
			 * wise)
			 */
			struct mbuf *tdata;
			uint32_t tmp;

			if (SCTP_TSN_GT(chk->rec.data.fsn, control->fsn_included)) {
				/*
				 * Easy way the start of a new guy beyond
				 * the lowest
				 */
				goto place_chunk;
			}
			if ((chk->rec.data.fsn == control->fsn_included) ||
			    (control->pdapi_started)) {
				/*
				 * Ok this should not happen, if it does we
				 * started the pd-api on the higher TSN
				 * (since the equals part is a TSN failure
				 * it must be that).
				 *
				 * We are completly hosed in that case since
				 * I have no way to recover. This really
				 * will only happen if we can get more TSN's
				 * higher before the pd-api-point.
				 */
				sctp_abort_in_reasm(stcb, control, chk,
				    abort_flag,
				    SCTP_FROM_SCTP_INDATA + SCTP_LOC_4);

				return;
			}
			/*
			 * Ok we have two firsts and the one we just got is
			 * smaller than the one we previously placed.. yuck!
			 * We must swap them out.
			 */
			/* swap the mbufs */
			tdata = control->data;
			control->data = chk->data;
			chk->data = tdata;
			/* Save the lengths */
			chk->send_size = control->length;
			/* Recompute length of control and tail pointer */
			sctp_setup_tail_pointer(control);
			/* Fix the FSN included */
			tmp = control->fsn_included;
			control->fsn_included = chk->rec.data.fsn;
			chk->rec.data.fsn = tmp;
			/* Fix the TSN included */
			tmp = control->sinfo_tsn;
			control->sinfo_tsn = chk->rec.data.tsn;
			chk->rec.data.tsn = tmp;
			/* Fix the PPID included */
			tmp = control->sinfo_ppid;
			control->sinfo_ppid = chk->rec.data.ppid;
			chk->rec.data.ppid = tmp;
			/* Fix tail pointer */
			goto place_chunk;
		}
		control->first_frag_seen = 1;
		control->fsn_included = chk->rec.data.fsn;
		control->top_fsn = chk->rec.data.fsn;
		control->sinfo_tsn = chk->rec.data.tsn;
		control->sinfo_ppid = chk->rec.data.ppid;
		control->data = chk->data;
		sctp_mark_non_revokable(asoc, chk->rec.data.tsn);
		chk->data = NULL;
		sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
		sctp_setup_tail_pointer(control);
		return;
	}
place_chunk:
	inserted = 0;
	TAILQ_FOREACH(at, &control->reasm, sctp_next) {
		if (SCTP_TSN_GT(at->rec.data.fsn, chk->rec.data.fsn)) {
			/*
			 * This one in queue is bigger than the new one,
			 * insert the new one before at.
			 */
			asoc->size_on_reasm_queue += chk->send_size;
			sctp_ucount_incr(asoc->cnt_on_reasm_queue);
			inserted = 1;
			TAILQ_INSERT_BEFORE(at, chk, sctp_next);
			break;
		} else if (at->rec.data.fsn == chk->rec.data.fsn) {
			/*
			 * They sent a duplicate fsn number. This really
			 * should not happen since the FSN is a TSN and it
			 * should have been dropped earlier.
			 */
			sctp_abort_in_reasm(stcb, control, chk,
			    abort_flag,
			    SCTP_FROM_SCTP_INDATA + SCTP_LOC_5);
			return;
		}

	}
	if (inserted == 0) {
		/* Its at the end */
		asoc->size_on_reasm_queue += chk->send_size;
		sctp_ucount_incr(asoc->cnt_on_reasm_queue);
		control->top_fsn = chk->rec.data.fsn;
		TAILQ_INSERT_TAIL(&control->reasm, chk, sctp_next);
	}
}

static int
sctp_deliver_reasm_check(struct sctp_tcb *stcb, struct sctp_association *asoc,
    struct sctp_stream_in *strm, int inp_read_lock_held)
{
	/*
	 * Given a stream, strm, see if any of the SSN's on it that are
	 * fragmented are ready to deliver. If so go ahead and place them on
	 * the read queue. In so placing if we have hit the end, then we
	 * need to remove them from the stream's queue.
	 */
	struct sctp_queued_to_read *control, *nctl = NULL;
	uint32_t next_to_del;
	uint32_t pd_point;
	int ret = 0;

	if (stcb->sctp_socket) {
		pd_point = min(SCTP_SB_LIMIT_RCV(stcb->sctp_socket) >> SCTP_PARTIAL_DELIVERY_SHIFT,
		    stcb->sctp_ep->partial_delivery_point);
	} else {
		pd_point = stcb->sctp_ep->partial_delivery_point;
	}
	control = TAILQ_FIRST(&strm->uno_inqueue);

	if ((control != NULL) &&
	    (asoc->idata_supported == 0)) {
		/* Special handling needed for "old" data format */
		if (sctp_handle_old_unordered_data(stcb, asoc, strm, control, pd_point, inp_read_lock_held)) {
			goto done_un;
		}
	}
	if (strm->pd_api_started) {
		/* Can't add more */
		return (0);
	}
	while (control) {
		SCTPDBG(SCTP_DEBUG_XXX, "Looking at control: %p e(%d) ssn: %u top_fsn: %u inc_fsn: %u -uo\n",
		    control, control->end_added, control->mid, control->top_fsn, control->fsn_included);
		nctl = TAILQ_NEXT(control, next_instrm);
		if (control->end_added) {
			/* We just put the last bit on */
			if (control->on_strm_q) {
#ifdef INVARIANTS
				if (control->on_strm_q != SCTP_ON_UNORDERED) {
					panic("Huh control: %p on_q: %d -- not unordered?",
					    control, control->on_strm_q);
				}
#endif
				SCTP_STAT_INCR_COUNTER64(sctps_reasmusrmsgs);
				TAILQ_REMOVE(&strm->uno_inqueue, control, next_instrm);
				control->on_strm_q = 0;
			}
			if (control->on_read_q == 0) {
				sctp_add_to_readq(stcb->sctp_ep, stcb,
				    control,
				    &stcb->sctp_socket->so_rcv, control->end_added,
				    inp_read_lock_held, SCTP_SO_NOT_LOCKED);
			}
		} else {
			/* Can we do a PD-API for this un-ordered guy? */
			if ((control->length >= pd_point) && (strm->pd_api_started == 0)) {
				strm->pd_api_started = 1;
				control->pdapi_started = 1;
				sctp_add_to_readq(stcb->sctp_ep, stcb,
				    control,
				    &stcb->sctp_socket->so_rcv, control->end_added,
				    inp_read_lock_held, SCTP_SO_NOT_LOCKED);

				break;
			}
		}
		control = nctl;
	}
done_un:
	control = TAILQ_FIRST(&strm->inqueue);
	if (strm->pd_api_started) {
		/* Can't add more */
		return (0);
	}
	if (control == NULL) {
		return (ret);
	}
	if (SCTP_MID_EQ(asoc->idata_supported, strm->last_mid_delivered, control->mid)) {
		/*
		 * Ok the guy at the top was being partially delivered
		 * completed, so we remove it. Note the pd_api flag was
		 * taken off when the chunk was merged on in
		 * sctp_queue_data_for_reasm below.
		 */
		nctl = TAILQ_NEXT(control, next_instrm);
		SCTPDBG(SCTP_DEBUG_XXX,
		    "Looking at control: %p e(%d) ssn: %u top_fsn: %u inc_fsn: %u (lastdel: %u)- o\n",
		    control, control->end_added, control->mid,
		    control->top_fsn, control->fsn_included,
		    strm->last_mid_delivered);
		if (control->end_added) {
			if (control->on_strm_q) {
#ifdef INVARIANTS
				if (control->on_strm_q != SCTP_ON_ORDERED) {
					panic("Huh control: %p on_q: %d -- not ordered?",
					    control, control->on_strm_q);
				}
#endif
				SCTP_STAT_INCR_COUNTER64(sctps_reasmusrmsgs);
				TAILQ_REMOVE(&strm->inqueue, control, next_instrm);
				if (asoc->size_on_all_streams >= control->length) {
					asoc->size_on_all_streams -= control->length;
				} else {
#ifdef INVARIANTS
					panic("size_on_all_streams = %u smaller than control length %u", asoc->size_on_all_streams, control->length);
#else
					asoc->size_on_all_streams = 0;
#endif
				}
				sctp_ucount_decr(asoc->cnt_on_all_streams);
				control->on_strm_q = 0;
			}
			if (strm->pd_api_started && control->pdapi_started) {
				control->pdapi_started = 0;
				strm->pd_api_started = 0;
			}
			if (control->on_read_q == 0) {
				sctp_add_to_readq(stcb->sctp_ep, stcb,
				    control,
				    &stcb->sctp_socket->so_rcv, control->end_added,
				    inp_read_lock_held, SCTP_SO_NOT_LOCKED);
			}
			control = nctl;
		}
	}
	if (strm->pd_api_started) {
		/*
		 * Can't add more must have gotten an un-ordered above being
		 * partially delivered.
		 */
		return (0);
	}
deliver_more:
	next_to_del = strm->last_mid_delivered + 1;
	if (control) {
		SCTPDBG(SCTP_DEBUG_XXX,
		    "Looking at control: %p e(%d) ssn: %u top_fsn: %u inc_fsn: %u (nxtdel: %u)- o\n",
		    control, control->end_added, control->mid, control->top_fsn, control->fsn_included,
		    next_to_del);
		nctl = TAILQ_NEXT(control, next_instrm);
		if (SCTP_MID_EQ(asoc->idata_supported, control->mid, next_to_del) &&
		    (control->first_frag_seen)) {
			int done;

			/* Ok we can deliver it onto the stream. */
			if (control->end_added) {
				/* We are done with it afterwards */
				if (control->on_strm_q) {
#ifdef INVARIANTS
					if (control->on_strm_q != SCTP_ON_ORDERED) {
						panic("Huh control: %p on_q: %d -- not ordered?",
						    control, control->on_strm_q);
					}
#endif
					SCTP_STAT_INCR_COUNTER64(sctps_reasmusrmsgs);
					TAILQ_REMOVE(&strm->inqueue, control, next_instrm);
					if (asoc->size_on_all_streams >= control->length) {
						asoc->size_on_all_streams -= control->length;
					} else {
#ifdef INVARIANTS
						panic("size_on_all_streams = %u smaller than control length %u", asoc->size_on_all_streams, control->length);
#else
						asoc->size_on_all_streams = 0;
#endif
					}
					sctp_ucount_decr(asoc->cnt_on_all_streams);
					control->on_strm_q = 0;
				}
				ret++;
			}
			if (((control->sinfo_flags >> 8) & SCTP_DATA_NOT_FRAG) == SCTP_DATA_NOT_FRAG) {
				/*
				 * A singleton now slipping through - mark
				 * it non-revokable too
				 */
				sctp_mark_non_revokable(asoc, control->sinfo_tsn);
			} else if (control->end_added == 0) {
				/*
				 * Check if we can defer adding until its
				 * all there
				 */
				if ((control->length < pd_point) || (strm->pd_api_started)) {
					/*
					 * Don't need it or cannot add more
					 * (one being delivered that way)
					 */
					goto out;
				}
			}
			done = (control->end_added) && (control->last_frag_seen);
			if (control->on_read_q == 0) {
				if (!done) {
					if (asoc->size_on_all_streams >= control->length) {
						asoc->size_on_all_streams -= control->length;
					} else {
#ifdef INVARIANTS
						panic("size_on_all_streams = %u smaller than control length %u", asoc->size_on_all_streams, control->length);
#else
						asoc->size_on_all_streams = 0;
#endif
					}
					strm->pd_api_started = 1;
					control->pdapi_started = 1;
				}
				sctp_add_to_readq(stcb->sctp_ep, stcb,
				    control,
				    &stcb->sctp_socket->so_rcv, control->end_added,
				    inp_read_lock_held, SCTP_SO_NOT_LOCKED);
			}
			strm->last_mid_delivered = next_to_del;
			if (done) {
				control = nctl;
				goto deliver_more;
			}
		}
	}
out:
	return (ret);
}


uint32_t
sctp_add_chk_to_control(struct sctp_queued_to_read *control,
    struct sctp_stream_in *strm,
    struct sctp_tcb *stcb, struct sctp_association *asoc,
    struct sctp_tmit_chunk *chk, int hold_rlock)
{
	/*
	 * Given a control and a chunk, merge the data from the chk onto the
	 * control and free up the chunk resources.
	 */
	uint32_t added = 0;
	int i_locked = 0;

	if (control->on_read_q && (hold_rlock == 0)) {
		/*
		 * Its being pd-api'd so we must do some locks.
		 */
		SCTP_INP_READ_LOCK(stcb->sctp_ep);
		i_locked = 1;
	}
	if (control->data == NULL) {
		control->data = chk->data;
		sctp_setup_tail_pointer(control);
	} else {
		sctp_add_to_tail_pointer(control, chk->data, &added);
	}
	control->fsn_included = chk->rec.data.fsn;
	asoc->size_on_reasm_queue -= chk->send_size;
	sctp_ucount_decr(asoc->cnt_on_reasm_queue);
	sctp_mark_non_revokable(asoc, chk->rec.data.tsn);
	chk->data = NULL;
	if (chk->rec.data.rcv_flags & SCTP_DATA_FIRST_FRAG) {
		control->first_frag_seen = 1;
		control->sinfo_tsn = chk->rec.data.tsn;
		control->sinfo_ppid = chk->rec.data.ppid;
	}
	if (chk->rec.data.rcv_flags & SCTP_DATA_LAST_FRAG) {
		/* Its complete */
		if ((control->on_strm_q) && (control->on_read_q)) {
			if (control->pdapi_started) {
				control->pdapi_started = 0;
				strm->pd_api_started = 0;
			}
			if (control->on_strm_q == SCTP_ON_UNORDERED) {
				/* Unordered */
				TAILQ_REMOVE(&strm->uno_inqueue, control, next_instrm);
				control->on_strm_q = 0;
			} else if (control->on_strm_q == SCTP_ON_ORDERED) {
				/* Ordered */
				TAILQ_REMOVE(&strm->inqueue, control, next_instrm);
				/*
				 * Don't need to decrement
				 * size_on_all_streams, since control is on
				 * the read queue.
				 */
				sctp_ucount_decr(asoc->cnt_on_all_streams);
				control->on_strm_q = 0;
#ifdef INVARIANTS
			} else if (control->on_strm_q) {
				panic("Unknown state on ctrl: %p on_strm_q: %d", control,
				    control->on_strm_q);
#endif
			}
		}
		control->end_added = 1;
		control->last_frag_seen = 1;
	}
	if (i_locked) {
		SCTP_INP_READ_UNLOCK(stcb->sctp_ep);
	}
	sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
	return (added);
}

/*
 * Dump onto the re-assembly queue, in its proper place. After dumping on the
 * queue, see if anthing can be delivered. If so pull it off (or as much as
 * we can. If we run out of space then we must dump what we can and set the
 * appropriate flag to say we queued what we could.
 */
static void
sctp_queue_data_for_reasm(struct sctp_tcb *stcb, struct sctp_association *asoc,
    struct sctp_queued_to_read *control,
    struct sctp_tmit_chunk *chk,
    int created_control,
    int *abort_flag, uint32_t tsn)
{
	uint32_t next_fsn;
	struct sctp_tmit_chunk *at, *nat;
	struct sctp_stream_in *strm;
	int do_wakeup, unordered;
	uint32_t lenadded;

	strm = &asoc->strmin[control->sinfo_stream];
	/*
	 * For old un-ordered data chunks.
	 */
	if ((control->sinfo_flags >> 8) & SCTP_DATA_UNORDERED) {
		unordered = 1;
	} else {
		unordered = 0;
	}
	/* Must be added to the stream-in queue */
	if (created_control) {
		if (unordered == 0) {
			sctp_ucount_incr(asoc->cnt_on_all_streams);
		}
		if (sctp_place_control_in_stream(strm, asoc, control)) {
			/* Duplicate SSN? */
			sctp_abort_in_reasm(stcb, control, chk,
			    abort_flag,
			    SCTP_FROM_SCTP_INDATA + SCTP_LOC_6);
			sctp_clean_up_control(stcb, control);
			return;
		}
		if ((tsn == (asoc->cumulative_tsn + 1) && (asoc->idata_supported == 0))) {
			/*
			 * Ok we created this control and now lets validate
			 * that its legal i.e. there is a B bit set, if not
			 * and we have up to the cum-ack then its invalid.
			 */
			if ((chk->rec.data.rcv_flags & SCTP_DATA_FIRST_FRAG) == 0) {
				sctp_abort_in_reasm(stcb, control, chk,
				    abort_flag,
				    SCTP_FROM_SCTP_INDATA + SCTP_LOC_7);
				return;
			}
		}
	}
	if ((asoc->idata_supported == 0) && (unordered == 1)) {
		sctp_inject_old_unordered_data(stcb, asoc, control, chk, abort_flag);
		return;
	}
	/*
	 * Ok we must queue the chunk into the reasembly portion: o if its
	 * the first it goes to the control mbuf. o if its not first but the
	 * next in sequence it goes to the control, and each succeeding one
	 * in order also goes. o if its not in order we place it on the list
	 * in its place.
	 */
	if (chk->rec.data.rcv_flags & SCTP_DATA_FIRST_FRAG) {
		/* Its the very first one. */
		SCTPDBG(SCTP_DEBUG_XXX,
		    "chunk is a first fsn: %u becomes fsn_included\n",
		    chk->rec.data.fsn);
		if (control->first_frag_seen) {
			/*
			 * Error on senders part, they either sent us two
			 * data chunks with FIRST, or they sent two
			 * un-ordered chunks that were fragmented at the
			 * same time in the same stream.
			 */
			sctp_abort_in_reasm(stcb, control, chk,
			    abort_flag,
			    SCTP_FROM_SCTP_INDATA + SCTP_LOC_8);
			return;
		}
		control->first_frag_seen = 1;
		control->sinfo_ppid = chk->rec.data.ppid;
		control->sinfo_tsn = chk->rec.data.tsn;
		control->fsn_included = chk->rec.data.fsn;
		control->data = chk->data;
		sctp_mark_non_revokable(asoc, chk->rec.data.tsn);
		chk->data = NULL;
		sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
		sctp_setup_tail_pointer(control);
		asoc->size_on_all_streams += control->length;
	} else {
		/* Place the chunk in our list */
		int inserted = 0;

		if (control->last_frag_seen == 0) {
			/* Still willing to raise highest FSN seen */
			if (SCTP_TSN_GT(chk->rec.data.fsn, control->top_fsn)) {
				SCTPDBG(SCTP_DEBUG_XXX,
				    "We have a new top_fsn: %u\n",
				    chk->rec.data.fsn);
				control->top_fsn = chk->rec.data.fsn;
			}
			if (chk->rec.data.rcv_flags & SCTP_DATA_LAST_FRAG) {
				SCTPDBG(SCTP_DEBUG_XXX,
				    "The last fsn is now in place fsn: %u\n",
				    chk->rec.data.fsn);
				control->last_frag_seen = 1;
			}
			if (asoc->idata_supported || control->first_frag_seen) {
				/*
				 * For IDATA we always check since we know
				 * that the first fragment is 0. For old
				 * DATA we have to receive the first before
				 * we know the first FSN (which is the TSN).
				 */
				if (SCTP_TSN_GE(control->fsn_included, chk->rec.data.fsn)) {
					/*
					 * We have already delivered up to
					 * this so its a dup
					 */
					sctp_abort_in_reasm(stcb, control, chk,
					    abort_flag,
					    SCTP_FROM_SCTP_INDATA + SCTP_LOC_9);
					return;
				}
			}
		} else {
			if (chk->rec.data.rcv_flags & SCTP_DATA_LAST_FRAG) {
				/* Second last? huh? */
				SCTPDBG(SCTP_DEBUG_XXX,
				    "Duplicate last fsn: %u (top: %u) -- abort\n",
				    chk->rec.data.fsn, control->top_fsn);
				sctp_abort_in_reasm(stcb, control,
				    chk, abort_flag,
				    SCTP_FROM_SCTP_INDATA + SCTP_LOC_10);
				return;
			}
			if (asoc->idata_supported || control->first_frag_seen) {
				/*
				 * For IDATA we always check since we know
				 * that the first fragment is 0. For old
				 * DATA we have to receive the first before
				 * we know the first FSN (which is the TSN).
				 */

				if (SCTP_TSN_GE(control->fsn_included, chk->rec.data.fsn)) {
					/*
					 * We have already delivered up to
					 * this so its a dup
					 */
					SCTPDBG(SCTP_DEBUG_XXX,
					    "New fsn: %u is already seen in included_fsn: %u -- abort\n",
					    chk->rec.data.fsn, control->fsn_included);
					sctp_abort_in_reasm(stcb, control, chk,
					    abort_flag,
					    SCTP_FROM_SCTP_INDATA + SCTP_LOC_11);
					return;
				}
			}
			/*
			 * validate not beyond top FSN if we have seen last
			 * one
			 */
			if (SCTP_TSN_GT(chk->rec.data.fsn, control->top_fsn)) {
				SCTPDBG(SCTP_DEBUG_XXX,
				    "New fsn: %u is beyond or at top_fsn: %u -- abort\n",
				    chk->rec.data.fsn,
				    control->top_fsn);
				sctp_abort_in_reasm(stcb, control, chk,
				    abort_flag,
				    SCTP_FROM_SCTP_INDATA + SCTP_LOC_12);
				return;
			}
		}
		/*
		 * If we reach here, we need to place the new chunk in the
		 * reassembly for this control.
		 */
		SCTPDBG(SCTP_DEBUG_XXX,
		    "chunk is a not first fsn: %u needs to be inserted\n",
		    chk->rec.data.fsn);
		TAILQ_FOREACH(at, &control->reasm, sctp_next) {
			if (SCTP_TSN_GT(at->rec.data.fsn, chk->rec.data.fsn)) {
				/*
				 * This one in queue is bigger than the new
				 * one, insert the new one before at.
				 */
				SCTPDBG(SCTP_DEBUG_XXX,
				    "Insert it before fsn: %u\n",
				    at->rec.data.fsn);
				asoc->size_on_reasm_queue += chk->send_size;
				sctp_ucount_incr(asoc->cnt_on_reasm_queue);
				TAILQ_INSERT_BEFORE(at, chk, sctp_next);
				inserted = 1;
				break;
			} else if (at->rec.data.fsn == chk->rec.data.fsn) {
				/*
				 * Gak, He sent me a duplicate str seq
				 * number
				 */
				/*
				 * foo bar, I guess I will just free this
				 * new guy, should we abort too? FIX ME
				 * MAYBE? Or it COULD be that the SSN's have
				 * wrapped. Maybe I should compare to TSN
				 * somehow... sigh for now just blow away
				 * the chunk!
				 */
				SCTPDBG(SCTP_DEBUG_XXX,
				    "Duplicate to fsn: %u -- abort\n",
				    at->rec.data.fsn);
				sctp_abort_in_reasm(stcb, control,
				    chk, abort_flag,
				    SCTP_FROM_SCTP_INDATA + SCTP_LOC_13);
				return;
			}
		}
		if (inserted == 0) {
			/* Goes on the end */
			SCTPDBG(SCTP_DEBUG_XXX, "Inserting at tail of list fsn: %u\n",
			    chk->rec.data.fsn);
			asoc->size_on_reasm_queue += chk->send_size;
			sctp_ucount_incr(asoc->cnt_on_reasm_queue);
			TAILQ_INSERT_TAIL(&control->reasm, chk, sctp_next);
		}
	}
	/*
	 * Ok lets see if we can suck any up into the control structure that
	 * are in seq if it makes sense.
	 */
	do_wakeup = 0;
	/*
	 * If the first fragment has not been seen there is no sense in
	 * looking.
	 */
	if (control->first_frag_seen) {
		next_fsn = control->fsn_included + 1;
		TAILQ_FOREACH_SAFE(at, &control->reasm, sctp_next, nat) {
			if (at->rec.data.fsn == next_fsn) {
				/* We can add this one now to the control */
				SCTPDBG(SCTP_DEBUG_XXX,
				    "Adding more to control: %p at: %p fsn: %u next_fsn: %u included: %u\n",
				    control, at,
				    at->rec.data.fsn,
				    next_fsn, control->fsn_included);
				TAILQ_REMOVE(&control->reasm, at, sctp_next);
				lenadded = sctp_add_chk_to_control(control, strm, stcb, asoc, at, SCTP_READ_LOCK_NOT_HELD);
				if (control->on_read_q) {
					do_wakeup = 1;
				} else {
					/*
					 * We only add to the
					 * size-on-all-streams if its not on
					 * the read q. The read q flag will
					 * cause a sballoc so its accounted
					 * for there.
					 */
					asoc->size_on_all_streams += lenadded;
				}
				next_fsn++;
				if (control->end_added && control->pdapi_started) {
					if (strm->pd_api_started) {
						strm->pd_api_started = 0;
						control->pdapi_started = 0;
					}
					if (control->on_read_q == 0) {
						sctp_add_to_readq(stcb->sctp_ep, stcb,
						    control,
						    &stcb->sctp_socket->so_rcv, control->end_added,
						    SCTP_READ_LOCK_NOT_HELD, SCTP_SO_NOT_LOCKED);
					}
					break;
				}
			} else {
				break;
			}
		}
	}
	if (do_wakeup) {
		/* Need to wakeup the reader */
		sctp_wakeup_the_read_socket(stcb->sctp_ep, stcb, SCTP_SO_NOT_LOCKED);
	}
}

static struct sctp_queued_to_read *
sctp_find_reasm_entry(struct sctp_stream_in *strm, uint32_t mid, int ordered, int idata_supported)
{
	struct sctp_queued_to_read *control;

	if (ordered) {
		TAILQ_FOREACH(control, &strm->inqueue, next_instrm) {
			if (SCTP_MID_EQ(idata_supported, control->mid, mid)) {
				break;
			}
		}
	} else {
		if (idata_supported) {
			TAILQ_FOREACH(control, &strm->uno_inqueue, next_instrm) {
				if (SCTP_MID_EQ(idata_supported, control->mid, mid)) {
					break;
				}
			}
		} else {
			control = TAILQ_FIRST(&strm->uno_inqueue);
		}
	}
	return (control);
}

static int
sctp_process_a_data_chunk(struct sctp_tcb *stcb, struct sctp_association *asoc,
    struct mbuf **m, int offset, int chk_length,
    struct sctp_nets *net, uint32_t *high_tsn, int *abort_flag,
    int *break_flag, int last_chunk, uint8_t chk_type)
{
	struct sctp_tmit_chunk *chk = NULL;	/* make gcc happy */
	uint32_t tsn, fsn, gap, mid;
	struct mbuf *dmbuf;
	int the_len;
	int need_reasm_check = 0;
	uint16_t sid;
	struct mbuf *op_err;
	char msg[SCTP_DIAG_INFO_LEN];
	struct sctp_queued_to_read *control, *ncontrol;
	uint32_t ppid;
	uint8_t chk_flags;
	struct sctp_stream_reset_list *liste;
	int ordered;
	size_t clen;
	int created_control = 0;

	if (chk_type == SCTP_IDATA) {
		struct sctp_idata_chunk *chunk, chunk_buf;

		chunk = (struct sctp_idata_chunk *)sctp_m_getptr(*m, offset,
		    sizeof(struct sctp_idata_chunk), (uint8_t *)&chunk_buf);
		chk_flags = chunk->ch.chunk_flags;
		clen = sizeof(struct sctp_idata_chunk);
		tsn = ntohl(chunk->dp.tsn);
		sid = ntohs(chunk->dp.sid);
		mid = ntohl(chunk->dp.mid);
		if (chk_flags & SCTP_DATA_FIRST_FRAG) {
			fsn = 0;
			ppid = chunk->dp.ppid_fsn.ppid;
		} else {
			fsn = ntohl(chunk->dp.ppid_fsn.fsn);
			ppid = 0xffffffff;	/* Use as an invalid value. */
		}
	} else {
		struct sctp_data_chunk *chunk, chunk_buf;

		chunk = (struct sctp_data_chunk *)sctp_m_getptr(*m, offset,
		    sizeof(struct sctp_data_chunk), (uint8_t *)&chunk_buf);
		chk_flags = chunk->ch.chunk_flags;
		clen = sizeof(struct sctp_data_chunk);
		tsn = ntohl(chunk->dp.tsn);
		sid = ntohs(chunk->dp.sid);
		mid = (uint32_t)(ntohs(chunk->dp.ssn));
		fsn = tsn;
		ppid = chunk->dp.ppid;
	}
	if ((size_t)chk_length == clen) {
		/*
		 * Need to send an abort since we had a empty data chunk.
		 */
		op_err = sctp_generate_no_user_data_cause(tsn);
		stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_14;
		sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
		*abort_flag = 1;
		return (0);
	}
	if ((chk_flags & SCTP_DATA_SACK_IMMEDIATELY) == SCTP_DATA_SACK_IMMEDIATELY) {
		asoc->send_sack = 1;
	}
	ordered = ((chk_flags & SCTP_DATA_UNORDERED) == 0);
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MAP_LOGGING_ENABLE) {
		sctp_log_map(tsn, asoc->cumulative_tsn, asoc->highest_tsn_inside_map, SCTP_MAP_TSN_ENTERS);
	}
	if (stcb == NULL) {
		return (0);
	}
	SCTP_LTRACE_CHK(stcb->sctp_ep, stcb, chk_type, tsn);
	if (SCTP_TSN_GE(asoc->cumulative_tsn, tsn)) {
		/* It is a duplicate */
		SCTP_STAT_INCR(sctps_recvdupdata);
		if (asoc->numduptsns < SCTP_MAX_DUP_TSNS) {
			/* Record a dup for the next outbound sack */
			asoc->dup_tsns[asoc->numduptsns] = tsn;
			asoc->numduptsns++;
		}
		asoc->send_sack = 1;
		return (0);
	}
	/* Calculate the number of TSN's between the base and this TSN */
	SCTP_CALC_TSN_TO_GAP(gap, tsn, asoc->mapping_array_base_tsn);
	if (gap >= (SCTP_MAPPING_ARRAY << 3)) {
		/* Can't hold the bit in the mapping at max array, toss it */
		return (0);
	}
	if (gap >= (uint32_t)(asoc->mapping_array_size << 3)) {
		SCTP_TCB_LOCK_ASSERT(stcb);
		if (sctp_expand_mapping_array(asoc, gap)) {
			/* Can't expand, drop it */
			return (0);
		}
	}
	if (SCTP_TSN_GT(tsn, *high_tsn)) {
		*high_tsn = tsn;
	}
	/* See if we have received this one already */
	if (SCTP_IS_TSN_PRESENT(asoc->mapping_array, gap) ||
	    SCTP_IS_TSN_PRESENT(asoc->nr_mapping_array, gap)) {
		SCTP_STAT_INCR(sctps_recvdupdata);
		if (asoc->numduptsns < SCTP_MAX_DUP_TSNS) {
			/* Record a dup for the next outbound sack */
			asoc->dup_tsns[asoc->numduptsns] = tsn;
			asoc->numduptsns++;
		}
		asoc->send_sack = 1;
		return (0);
	}
	/*
	 * Check to see about the GONE flag, duplicates would cause a sack
	 * to be sent up above
	 */
	if (((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) ||
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE) ||
	    (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET))) {
		/*
		 * wait a minute, this guy is gone, there is no longer a
		 * receiver. Send peer an ABORT!
		 */
		op_err = sctp_generate_cause(SCTP_CAUSE_OUT_OF_RESC, "");
		sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
		*abort_flag = 1;
		return (0);
	}
	/*
	 * Now before going further we see if there is room. If NOT then we
	 * MAY let one through only IF this TSN is the one we are waiting
	 * for on a partial delivery API.
	 */

	/* Is the stream valid? */
	if (sid >= asoc->streamincnt) {
		struct sctp_error_invalid_stream *cause;

		op_err = sctp_get_mbuf_for_msg(sizeof(struct sctp_error_invalid_stream),
		    0, M_NOWAIT, 1, MT_DATA);
		if (op_err != NULL) {
			/* add some space up front so prepend will work well */
			SCTP_BUF_RESV_UF(op_err, sizeof(struct sctp_chunkhdr));
			cause = mtod(op_err, struct sctp_error_invalid_stream *);
			/*
			 * Error causes are just param's and this one has
			 * two back to back phdr, one with the error type
			 * and size, the other with the streamid and a rsvd
			 */
			SCTP_BUF_LEN(op_err) = sizeof(struct sctp_error_invalid_stream);
			cause->cause.code = htons(SCTP_CAUSE_INVALID_STREAM);
			cause->cause.length = htons(sizeof(struct sctp_error_invalid_stream));
			cause->stream_id = htons(sid);
			cause->reserved = htons(0);
			sctp_queue_op_err(stcb, op_err);
		}
		SCTP_STAT_INCR(sctps_badsid);
		SCTP_TCB_LOCK_ASSERT(stcb);
		SCTP_SET_TSN_PRESENT(asoc->nr_mapping_array, gap);
		if (SCTP_TSN_GT(tsn, asoc->highest_tsn_inside_nr_map)) {
			asoc->highest_tsn_inside_nr_map = tsn;
		}
		if (tsn == (asoc->cumulative_tsn + 1)) {
			/* Update cum-ack */
			asoc->cumulative_tsn = tsn;
		}
		return (0);
	}
	/*
	 * If its a fragmented message, lets see if we can find the control
	 * on the reassembly queues.
	 */
	if ((chk_type == SCTP_IDATA) &&
	    ((chk_flags & SCTP_DATA_FIRST_FRAG) == 0) &&
	    (fsn == 0)) {
		/*
		 * The first *must* be fsn 0, and other (middle/end) pieces
		 * can *not* be fsn 0. XXX: This can happen in case of a
		 * wrap around. Ignore is for now.
		 */
		snprintf(msg, sizeof(msg), "FSN zero for MID=%8.8x, but flags=%2.2x",
		    mid, chk_flags);
		goto err_out;
	}
	control = sctp_find_reasm_entry(&asoc->strmin[sid], mid, ordered, asoc->idata_supported);
	SCTPDBG(SCTP_DEBUG_XXX, "chunk_flags:0x%x look for control on queues %p\n",
	    chk_flags, control);
	if ((chk_flags & SCTP_DATA_NOT_FRAG) != SCTP_DATA_NOT_FRAG) {
		/* See if we can find the re-assembly entity */
		if (control != NULL) {
			/* We found something, does it belong? */
			if (ordered && (mid != control->mid)) {
				snprintf(msg, sizeof(msg), "Reassembly problem (MID=%8.8x)", mid);
		err_out:
				op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
				stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_15;
				sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
				*abort_flag = 1;
				return (0);
			}
			if (ordered && ((control->sinfo_flags >> 8) & SCTP_DATA_UNORDERED)) {
				/*
				 * We can't have a switched order with an
				 * unordered chunk
				 */
				snprintf(msg, sizeof(msg), "All fragments of a user message must be ordered or unordered (TSN=%8.8x)",
				    tsn);
				goto err_out;
			}
			if (!ordered && (((control->sinfo_flags >> 8) & SCTP_DATA_UNORDERED) == 0)) {
				/*
				 * We can't have a switched unordered with a
				 * ordered chunk
				 */
				snprintf(msg, sizeof(msg), "All fragments of a user message must be ordered or unordered (TSN=%8.8x)",
				    tsn);
				goto err_out;
			}
		}
	} else {
		/*
		 * Its a complete segment. Lets validate we don't have a
		 * re-assembly going on with the same Stream/Seq (for
		 * ordered) or in the same Stream for unordered.
		 */
		if (control != NULL) {
			if (ordered || asoc->idata_supported) {
				SCTPDBG(SCTP_DEBUG_XXX, "chunk_flags: 0x%x dup detected on MID: %u\n",
				    chk_flags, mid);
				snprintf(msg, sizeof(msg), "Duplicate MID=%8.8x detected.", mid);
				goto err_out;
			} else {
				if ((tsn == control->fsn_included + 1) &&
				    (control->end_added == 0)) {
					snprintf(msg, sizeof(msg), "Illegal message sequence, missing end for MID: %8.8x", control->fsn_included);
					goto err_out;
				} else {
					control = NULL;
				}
			}
		}
	}
	/* now do the tests */
	if (((asoc->cnt_on_all_streams +
	    asoc->cnt_on_reasm_queue +
	    asoc->cnt_msg_on_sb) >= SCTP_BASE_SYSCTL(sctp_max_chunks_on_queue)) ||
	    (((int)asoc->my_rwnd) <= 0)) {
		/*
		 * When we have NO room in the rwnd we check to make sure
		 * the reader is doing its job...
		 */
		if (stcb->sctp_socket->so_rcv.sb_cc) {
			/* some to read, wake-up */
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
			struct socket *so;

			so = SCTP_INP_SO(stcb->sctp_ep);
			atomic_add_int(&stcb->asoc.refcnt, 1);
			SCTP_TCB_UNLOCK(stcb);
			SCTP_SOCKET_LOCK(so, 1);
			SCTP_TCB_LOCK(stcb);
			atomic_subtract_int(&stcb->asoc.refcnt, 1);
			if (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET) {
				/* assoc was freed while we were unlocked */
				SCTP_SOCKET_UNLOCK(so, 1);
				return (0);
			}
#endif
			sctp_sorwakeup(stcb->sctp_ep, stcb->sctp_socket);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
			SCTP_SOCKET_UNLOCK(so, 1);
#endif
		}
		/* now is it in the mapping array of what we have accepted? */
		if (chk_type == SCTP_DATA) {
			if (SCTP_TSN_GT(tsn, asoc->highest_tsn_inside_map) &&
			    SCTP_TSN_GT(tsn, asoc->highest_tsn_inside_nr_map)) {
				/* Nope not in the valid range dump it */
		dump_packet:
				sctp_set_rwnd(stcb, asoc);
				if ((asoc->cnt_on_all_streams +
				    asoc->cnt_on_reasm_queue +
				    asoc->cnt_msg_on_sb) >= SCTP_BASE_SYSCTL(sctp_max_chunks_on_queue)) {
					SCTP_STAT_INCR(sctps_datadropchklmt);
				} else {
					SCTP_STAT_INCR(sctps_datadroprwnd);
				}
				*break_flag = 1;
				return (0);
			}
		} else {
			if (control == NULL) {
				goto dump_packet;
			}
			if (SCTP_TSN_GT(fsn, control->top_fsn)) {
				goto dump_packet;
			}
		}
	}
#ifdef SCTP_ASOCLOG_OF_TSNS
	SCTP_TCB_LOCK_ASSERT(stcb);
	if (asoc->tsn_in_at >= SCTP_TSN_LOG_SIZE) {
		asoc->tsn_in_at = 0;
		asoc->tsn_in_wrapped = 1;
	}
	asoc->in_tsnlog[asoc->tsn_in_at].tsn = tsn;
	asoc->in_tsnlog[asoc->tsn_in_at].strm = sid;
	asoc->in_tsnlog[asoc->tsn_in_at].seq = mid;
	asoc->in_tsnlog[asoc->tsn_in_at].sz = chk_length;
	asoc->in_tsnlog[asoc->tsn_in_at].flgs = chunk_flags;
	asoc->in_tsnlog[asoc->tsn_in_at].stcb = (void *)stcb;
	asoc->in_tsnlog[asoc->tsn_in_at].in_pos = asoc->tsn_in_at;
	asoc->in_tsnlog[asoc->tsn_in_at].in_out = 1;
	asoc->tsn_in_at++;
#endif
	/*
	 * Before we continue lets validate that we are not being fooled by
	 * an evil attacker. We can only have Nk chunks based on our TSN
	 * spread allowed by the mapping array N * 8 bits, so there is no
	 * way our stream sequence numbers could have wrapped. We of course
	 * only validate the FIRST fragment so the bit must be set.
	 */
	if ((chk_flags & SCTP_DATA_FIRST_FRAG) &&
	    (TAILQ_EMPTY(&asoc->resetHead)) &&
	    (chk_flags & SCTP_DATA_UNORDERED) == 0 &&
	    SCTP_MID_GE(asoc->idata_supported, asoc->strmin[sid].last_mid_delivered, mid)) {
		/* The incoming sseq is behind where we last delivered? */
		SCTPDBG(SCTP_DEBUG_INDATA1, "EVIL/Broken-Dup S-SEQ: %u delivered: %u from peer, Abort!\n",
		    mid, asoc->strmin[sid].last_mid_delivered);

		if (asoc->idata_supported) {
			snprintf(msg, sizeof(msg), "Delivered MID=%8.8x, got TSN=%8.8x, SID=%4.4x, MID=%8.8x",
			    asoc->strmin[sid].last_mid_delivered,
			    tsn,
			    sid,
			    mid);
		} else {
			snprintf(msg, sizeof(msg), "Delivered SSN=%4.4x, got TSN=%8.8x, SID=%4.4x, SSN=%4.4x",
			    (uint16_t)asoc->strmin[sid].last_mid_delivered,
			    tsn,
			    sid,
			    (uint16_t)mid);
		}
		op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
		stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_16;
		sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
		*abort_flag = 1;
		return (0);
	}
	if (chk_type == SCTP_IDATA) {
		the_len = (chk_length - sizeof(struct sctp_idata_chunk));
	} else {
		the_len = (chk_length - sizeof(struct sctp_data_chunk));
	}
	if (last_chunk == 0) {
		if (chk_type == SCTP_IDATA) {
			dmbuf = SCTP_M_COPYM(*m,
			    (offset + sizeof(struct sctp_idata_chunk)),
			    the_len, M_NOWAIT);
		} else {
			dmbuf = SCTP_M_COPYM(*m,
			    (offset + sizeof(struct sctp_data_chunk)),
			    the_len, M_NOWAIT);
		}
#ifdef SCTP_MBUF_LOGGING
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
			sctp_log_mbc(dmbuf, SCTP_MBUF_ICOPY);
		}
#endif
	} else {
		/* We can steal the last chunk */
		int l_len;

		dmbuf = *m;
		/* lop off the top part */
		if (chk_type == SCTP_IDATA) {
			m_adj(dmbuf, (offset + sizeof(struct sctp_idata_chunk)));
		} else {
			m_adj(dmbuf, (offset + sizeof(struct sctp_data_chunk)));
		}
		if (SCTP_BUF_NEXT(dmbuf) == NULL) {
			l_len = SCTP_BUF_LEN(dmbuf);
		} else {
			/*
			 * need to count up the size hopefully does not hit
			 * this to often :-0
			 */
			struct mbuf *lat;

			l_len = 0;
			for (lat = dmbuf; lat; lat = SCTP_BUF_NEXT(lat)) {
				l_len += SCTP_BUF_LEN(lat);
			}
		}
		if (l_len > the_len) {
			/* Trim the end round bytes off  too */
			m_adj(dmbuf, -(l_len - the_len));
		}
	}
	if (dmbuf == NULL) {
		SCTP_STAT_INCR(sctps_nomem);
		return (0);
	}
	/*
	 * Now no matter what, we need a control, get one if we don't have
	 * one (we may have gotten it above when we found the message was
	 * fragmented
	 */
	if (control == NULL) {
		sctp_alloc_a_readq(stcb, control);
		sctp_build_readq_entry_mac(control, stcb, asoc->context, net, tsn,
		    ppid,
		    sid,
		    chk_flags,
		    NULL, fsn, mid);
		if (control == NULL) {
			SCTP_STAT_INCR(sctps_nomem);
			return (0);
		}
		if ((chk_flags & SCTP_DATA_NOT_FRAG) == SCTP_DATA_NOT_FRAG) {
			struct mbuf *mm;

			control->data = dmbuf;
			for (mm = control->data; mm; mm = mm->m_next) {
				control->length += SCTP_BUF_LEN(mm);
			}
			control->tail_mbuf = NULL;
			control->end_added = 1;
			control->last_frag_seen = 1;
			control->first_frag_seen = 1;
			control->fsn_included = fsn;
			control->top_fsn = fsn;
		}
		created_control = 1;
	}
	SCTPDBG(SCTP_DEBUG_XXX, "chunk_flags: 0x%x ordered: %d MID: %u control: %p\n",
	    chk_flags, ordered, mid, control);
	if ((chk_flags & SCTP_DATA_NOT_FRAG) == SCTP_DATA_NOT_FRAG &&
	    TAILQ_EMPTY(&asoc->resetHead) &&
	    ((ordered == 0) ||
	    (SCTP_MID_EQ(asoc->idata_supported, asoc->strmin[sid].last_mid_delivered + 1, mid) &&
	    TAILQ_EMPTY(&asoc->strmin[sid].inqueue)))) {
		/* Candidate for express delivery */
		/*
		 * Its not fragmented, No PD-API is up, Nothing in the
		 * delivery queue, Its un-ordered OR ordered and the next to
		 * deliver AND nothing else is stuck on the stream queue,
		 * And there is room for it in the socket buffer. Lets just
		 * stuff it up the buffer....
		 */
		SCTP_SET_TSN_PRESENT(asoc->nr_mapping_array, gap);
		if (SCTP_TSN_GT(tsn, asoc->highest_tsn_inside_nr_map)) {
			asoc->highest_tsn_inside_nr_map = tsn;
		}
		SCTPDBG(SCTP_DEBUG_XXX, "Injecting control: %p to be read (MID: %u)\n",
		    control, mid);

		sctp_add_to_readq(stcb->sctp_ep, stcb,
		    control, &stcb->sctp_socket->so_rcv,
		    1, SCTP_READ_LOCK_NOT_HELD, SCTP_SO_NOT_LOCKED);

		if ((chk_flags & SCTP_DATA_UNORDERED) == 0) {
			/* for ordered, bump what we delivered */
			asoc->strmin[sid].last_mid_delivered++;
		}
		SCTP_STAT_INCR(sctps_recvexpress);
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_STR_LOGGING_ENABLE) {
			sctp_log_strm_del_alt(stcb, tsn, mid, sid,
			    SCTP_STR_LOG_FROM_EXPRS_DEL);
		}
		control = NULL;
		goto finish_express_del;
	}

	/* Now will we need a chunk too? */
	if ((chk_flags & SCTP_DATA_NOT_FRAG) != SCTP_DATA_NOT_FRAG) {
		sctp_alloc_a_chunk(stcb, chk);
		if (chk == NULL) {
			/* No memory so we drop the chunk */
			SCTP_STAT_INCR(sctps_nomem);
			if (last_chunk == 0) {
				/* we copied it, free the copy */
				sctp_m_freem(dmbuf);
			}
			return (0);
		}
		chk->rec.data.tsn = tsn;
		chk->no_fr_allowed = 0;
		chk->rec.data.fsn = fsn;
		chk->rec.data.mid = mid;
		chk->rec.data.sid = sid;
		chk->rec.data.ppid = ppid;
		chk->rec.data.context = stcb->asoc.context;
		chk->rec.data.doing_fast_retransmit = 0;
		chk->rec.data.rcv_flags = chk_flags;
		chk->asoc = asoc;
		chk->send_size = the_len;
		chk->whoTo = net;
		SCTPDBG(SCTP_DEBUG_XXX, "Building ck: %p for control: %p to be read (MID: %u)\n",
		    chk,
		    control, mid);
		atomic_add_int(&net->ref_count, 1);
		chk->data = dmbuf;
	}
	/* Set the appropriate TSN mark */
	if (SCTP_BASE_SYSCTL(sctp_do_drain) == 0) {
		SCTP_SET_TSN_PRESENT(asoc->nr_mapping_array, gap);
		if (SCTP_TSN_GT(tsn, asoc->highest_tsn_inside_nr_map)) {
			asoc->highest_tsn_inside_nr_map = tsn;
		}
	} else {
		SCTP_SET_TSN_PRESENT(asoc->mapping_array, gap);
		if (SCTP_TSN_GT(tsn, asoc->highest_tsn_inside_map)) {
			asoc->highest_tsn_inside_map = tsn;
		}
	}
	/* Now is it complete (i.e. not fragmented)? */
	if ((chk_flags & SCTP_DATA_NOT_FRAG) == SCTP_DATA_NOT_FRAG) {
		/*
		 * Special check for when streams are resetting. We could be
		 * more smart about this and check the actual stream to see
		 * if it is not being reset.. that way we would not create a
		 * HOLB when amongst streams being reset and those not being
		 * reset.
		 *
		 */
		if (((liste = TAILQ_FIRST(&asoc->resetHead)) != NULL) &&
		    SCTP_TSN_GT(tsn, liste->tsn)) {
			/*
			 * yep its past where we need to reset... go ahead
			 * and queue it.
			 */
			if (TAILQ_EMPTY(&asoc->pending_reply_queue)) {
				/* first one on */
				TAILQ_INSERT_TAIL(&asoc->pending_reply_queue, control, next);
			} else {
				struct sctp_queued_to_read *lcontrol, *nlcontrol;
				unsigned char inserted = 0;

				TAILQ_FOREACH_SAFE(lcontrol, &asoc->pending_reply_queue, next, nlcontrol) {
					if (SCTP_TSN_GT(control->sinfo_tsn, lcontrol->sinfo_tsn)) {

						continue;
					} else {
						/* found it */
						TAILQ_INSERT_BEFORE(lcontrol, control, next);
						inserted = 1;
						break;
					}
				}
				if (inserted == 0) {
					/*
					 * must be put at end, use prevP
					 * (all setup from loop) to setup
					 * nextP.
					 */
					TAILQ_INSERT_TAIL(&asoc->pending_reply_queue, control, next);
				}
			}
			goto finish_express_del;
		}
		if (chk_flags & SCTP_DATA_UNORDERED) {
			/* queue directly into socket buffer */
			SCTPDBG(SCTP_DEBUG_XXX, "Unordered data to be read control: %p MID: %u\n",
			    control, mid);
			sctp_mark_non_revokable(asoc, control->sinfo_tsn);
			sctp_add_to_readq(stcb->sctp_ep, stcb,
			    control,
			    &stcb->sctp_socket->so_rcv, 1,
			    SCTP_READ_LOCK_NOT_HELD, SCTP_SO_NOT_LOCKED);

		} else {
			SCTPDBG(SCTP_DEBUG_XXX, "Queue control: %p for reordering MID: %u\n", control,
			    mid);
			sctp_queue_data_to_stream(stcb, asoc, control, abort_flag, &need_reasm_check);
			if (*abort_flag) {
				if (last_chunk) {
					*m = NULL;
				}
				return (0);
			}
		}
		goto finish_express_del;
	}
	/* If we reach here its a reassembly */
	need_reasm_check = 1;
	SCTPDBG(SCTP_DEBUG_XXX,
	    "Queue data to stream for reasm control: %p MID: %u\n",
	    control, mid);
	sctp_queue_data_for_reasm(stcb, asoc, control, chk, created_control, abort_flag, tsn);
	if (*abort_flag) {
		/*
		 * the assoc is now gone and chk was put onto the reasm
		 * queue, which has all been freed.
		 */
		if (last_chunk) {
			*m = NULL;
		}
		return (0);
	}
finish_express_del:
	/* Here we tidy up things */
	if (tsn == (asoc->cumulative_tsn + 1)) {
		/* Update cum-ack */
		asoc->cumulative_tsn = tsn;
	}
	if (last_chunk) {
		*m = NULL;
	}
	if (ordered) {
		SCTP_STAT_INCR_COUNTER64(sctps_inorderchunks);
	} else {
		SCTP_STAT_INCR_COUNTER64(sctps_inunorderchunks);
	}
	SCTP_STAT_INCR(sctps_recvdata);
	/* Set it present please */
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_STR_LOGGING_ENABLE) {
		sctp_log_strm_del_alt(stcb, tsn, mid, sid, SCTP_STR_LOG_FROM_MARK_TSN);
	}
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MAP_LOGGING_ENABLE) {
		sctp_log_map(asoc->mapping_array_base_tsn, asoc->cumulative_tsn,
		    asoc->highest_tsn_inside_map, SCTP_MAP_PREPARE_SLIDE);
	}
	if (need_reasm_check) {
		(void)sctp_deliver_reasm_check(stcb, asoc, &asoc->strmin[sid], SCTP_READ_LOCK_NOT_HELD);
		need_reasm_check = 0;
	}
	/* check the special flag for stream resets */
	if (((liste = TAILQ_FIRST(&asoc->resetHead)) != NULL) &&
	    SCTP_TSN_GE(asoc->cumulative_tsn, liste->tsn)) {
		/*
		 * we have finished working through the backlogged TSN's now
		 * time to reset streams. 1: call reset function. 2: free
		 * pending_reply space 3: distribute any chunks in
		 * pending_reply_queue.
		 */
		sctp_reset_in_stream(stcb, liste->number_entries, liste->list_of_streams);
		TAILQ_REMOVE(&asoc->resetHead, liste, next_resp);
		sctp_send_deferred_reset_response(stcb, liste, SCTP_STREAM_RESET_RESULT_PERFORMED);
		SCTP_FREE(liste, SCTP_M_STRESET);
		/* sa_ignore FREED_MEMORY */
		liste = TAILQ_FIRST(&asoc->resetHead);
		if (TAILQ_EMPTY(&asoc->resetHead)) {
			/* All can be removed */
			TAILQ_FOREACH_SAFE(control, &asoc->pending_reply_queue, next, ncontrol) {
				TAILQ_REMOVE(&asoc->pending_reply_queue, control, next);
				sctp_queue_data_to_stream(stcb, asoc, control, abort_flag, &need_reasm_check);
				if (*abort_flag) {
					return (0);
				}
				if (need_reasm_check) {
					(void)sctp_deliver_reasm_check(stcb, asoc, &asoc->strmin[control->sinfo_stream], SCTP_READ_LOCK_NOT_HELD);
					need_reasm_check = 0;
				}
			}
		} else {
			TAILQ_FOREACH_SAFE(control, &asoc->pending_reply_queue, next, ncontrol) {
				if (SCTP_TSN_GT(control->sinfo_tsn, liste->tsn)) {
					break;
				}
				/*
				 * if control->sinfo_tsn is <= liste->tsn we
				 * can process it which is the NOT of
				 * control->sinfo_tsn > liste->tsn
				 */
				TAILQ_REMOVE(&asoc->pending_reply_queue, control, next);
				sctp_queue_data_to_stream(stcb, asoc, control, abort_flag, &need_reasm_check);
				if (*abort_flag) {
					return (0);
				}
				if (need_reasm_check) {
					(void)sctp_deliver_reasm_check(stcb, asoc, &asoc->strmin[control->sinfo_stream], SCTP_READ_LOCK_NOT_HELD);
					need_reasm_check = 0;
				}
			}
		}
	}
	return (1);
}

static const int8_t sctp_map_lookup_tab[256] = {
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 4,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 5,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 4,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 6,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 4,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 5,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 4,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 7,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 4,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 5,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 4,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 6,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 4,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 5,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 4,
	0, 1, 0, 2, 0, 1, 0, 3,
	0, 1, 0, 2, 0, 1, 0, 8
};


void
sctp_slide_mapping_arrays(struct sctp_tcb *stcb)
{
	/*
	 * Now we also need to check the mapping array in a couple of ways.
	 * 1) Did we move the cum-ack point?
	 *
	 * When you first glance at this you might think that all entries
	 * that make up the position of the cum-ack would be in the
	 * nr-mapping array only.. i.e. things up to the cum-ack are always
	 * deliverable. Thats true with one exception, when its a fragmented
	 * message we may not deliver the data until some threshold (or all
	 * of it) is in place. So we must OR the nr_mapping_array and
	 * mapping_array to get a true picture of the cum-ack.
	 */
	struct sctp_association *asoc;
	int at;
	uint8_t val;
	int slide_from, slide_end, lgap, distance;
	uint32_t old_cumack, old_base, old_highest, highest_tsn;

	asoc = &stcb->asoc;

	old_cumack = asoc->cumulative_tsn;
	old_base = asoc->mapping_array_base_tsn;
	old_highest = asoc->highest_tsn_inside_map;
	/*
	 * We could probably improve this a small bit by calculating the
	 * offset of the current cum-ack as the starting point.
	 */
	at = 0;
	for (slide_from = 0; slide_from < stcb->asoc.mapping_array_size; slide_from++) {
		val = asoc->nr_mapping_array[slide_from] | asoc->mapping_array[slide_from];
		if (val == 0xff) {
			at += 8;
		} else {
			/* there is a 0 bit */
			at += sctp_map_lookup_tab[val];
			break;
		}
	}
	asoc->cumulative_tsn = asoc->mapping_array_base_tsn + (at - 1);

	if (SCTP_TSN_GT(asoc->cumulative_tsn, asoc->highest_tsn_inside_map) &&
	    SCTP_TSN_GT(asoc->cumulative_tsn, asoc->highest_tsn_inside_nr_map)) {
#ifdef INVARIANTS
		panic("huh, cumack 0x%x greater than high-tsn 0x%x in map",
		    asoc->cumulative_tsn, asoc->highest_tsn_inside_map);
#else
		SCTP_PRINTF("huh, cumack 0x%x greater than high-tsn 0x%x in map - should panic?\n",
		    asoc->cumulative_tsn, asoc->highest_tsn_inside_map);
		sctp_print_mapping_array(asoc);
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MAP_LOGGING_ENABLE) {
			sctp_log_map(0, 6, asoc->highest_tsn_inside_map, SCTP_MAP_SLIDE_RESULT);
		}
		asoc->highest_tsn_inside_map = asoc->cumulative_tsn;
		asoc->highest_tsn_inside_nr_map = asoc->cumulative_tsn;
#endif
	}
	if (SCTP_TSN_GT(asoc->highest_tsn_inside_nr_map, asoc->highest_tsn_inside_map)) {
		highest_tsn = asoc->highest_tsn_inside_nr_map;
	} else {
		highest_tsn = asoc->highest_tsn_inside_map;
	}
	if ((asoc->cumulative_tsn == highest_tsn) && (at >= 8)) {
		/* The complete array was completed by a single FR */
		/* highest becomes the cum-ack */
		int clr;
#ifdef INVARIANTS
		unsigned int i;
#endif

		/* clear the array */
		clr = ((at + 7) >> 3);
		if (clr > asoc->mapping_array_size) {
			clr = asoc->mapping_array_size;
		}
		memset(asoc->mapping_array, 0, clr);
		memset(asoc->nr_mapping_array, 0, clr);
#ifdef INVARIANTS
		for (i = 0; i < asoc->mapping_array_size; i++) {
			if ((asoc->mapping_array[i]) || (asoc->nr_mapping_array[i])) {
				SCTP_PRINTF("Error Mapping array's not clean at clear\n");
				sctp_print_mapping_array(asoc);
			}
		}
#endif
		asoc->mapping_array_base_tsn = asoc->cumulative_tsn + 1;
		asoc->highest_tsn_inside_nr_map = asoc->highest_tsn_inside_map = asoc->cumulative_tsn;
	} else if (at >= 8) {
		/* we can slide the mapping array down */
		/* slide_from holds where we hit the first NON 0xff byte */

		/*
		 * now calculate the ceiling of the move using our highest
		 * TSN value
		 */
		SCTP_CALC_TSN_TO_GAP(lgap, highest_tsn, asoc->mapping_array_base_tsn);
		slide_end = (lgap >> 3);
		if (slide_end < slide_from) {
			sctp_print_mapping_array(asoc);
#ifdef INVARIANTS
			panic("impossible slide");
#else
			SCTP_PRINTF("impossible slide lgap: %x slide_end: %x slide_from: %x? at: %d\n",
			    lgap, slide_end, slide_from, at);
			return;
#endif
		}
		if (slide_end > asoc->mapping_array_size) {
#ifdef INVARIANTS
			panic("would overrun buffer");
#else
			SCTP_PRINTF("Gak, would have overrun map end: %d slide_end: %d\n",
			    asoc->mapping_array_size, slide_end);
			slide_end = asoc->mapping_array_size;
#endif
		}
		distance = (slide_end - slide_from) + 1;
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MAP_LOGGING_ENABLE) {
			sctp_log_map(old_base, old_cumack, old_highest,
			    SCTP_MAP_PREPARE_SLIDE);
			sctp_log_map((uint32_t)slide_from, (uint32_t)slide_end,
			    (uint32_t)lgap, SCTP_MAP_SLIDE_FROM);
		}
		if (distance + slide_from > asoc->mapping_array_size ||
		    distance < 0) {
			/*
			 * Here we do NOT slide forward the array so that
			 * hopefully when more data comes in to fill it up
			 * we will be able to slide it forward. Really I
			 * don't think this should happen :-0
			 */

			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MAP_LOGGING_ENABLE) {
				sctp_log_map((uint32_t)distance, (uint32_t)slide_from,
				    (uint32_t)asoc->mapping_array_size,
				    SCTP_MAP_SLIDE_NONE);
			}
		} else {
			int ii;

			for (ii = 0; ii < distance; ii++) {
				asoc->mapping_array[ii] = asoc->mapping_array[slide_from + ii];
				asoc->nr_mapping_array[ii] = asoc->nr_mapping_array[slide_from + ii];

			}
			for (ii = distance; ii < asoc->mapping_array_size; ii++) {
				asoc->mapping_array[ii] = 0;
				asoc->nr_mapping_array[ii] = 0;
			}
			if (asoc->highest_tsn_inside_map + 1 == asoc->mapping_array_base_tsn) {
				asoc->highest_tsn_inside_map += (slide_from << 3);
			}
			if (asoc->highest_tsn_inside_nr_map + 1 == asoc->mapping_array_base_tsn) {
				asoc->highest_tsn_inside_nr_map += (slide_from << 3);
			}
			asoc->mapping_array_base_tsn += (slide_from << 3);
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MAP_LOGGING_ENABLE) {
				sctp_log_map(asoc->mapping_array_base_tsn,
				    asoc->cumulative_tsn, asoc->highest_tsn_inside_map,
				    SCTP_MAP_SLIDE_RESULT);
			}
		}
	}
}

void
sctp_sack_check(struct sctp_tcb *stcb, int was_a_gap)
{
	struct sctp_association *asoc;
	uint32_t highest_tsn;
	int is_a_gap;

	sctp_slide_mapping_arrays(stcb);
	asoc = &stcb->asoc;
	if (SCTP_TSN_GT(asoc->highest_tsn_inside_nr_map, asoc->highest_tsn_inside_map)) {
		highest_tsn = asoc->highest_tsn_inside_nr_map;
	} else {
		highest_tsn = asoc->highest_tsn_inside_map;
	}
	/* Is there a gap now? */
	is_a_gap = SCTP_TSN_GT(highest_tsn, stcb->asoc.cumulative_tsn);

	/*
	 * Now we need to see if we need to queue a sack or just start the
	 * timer (if allowed).
	 */
	if (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_SENT) {
		/*
		 * Ok special case, in SHUTDOWN-SENT case. here we maker
		 * sure SACK timer is off and instead send a SHUTDOWN and a
		 * SACK
		 */
		if (SCTP_OS_TIMER_PENDING(&stcb->asoc.dack_timer.timer)) {
			sctp_timer_stop(SCTP_TIMER_TYPE_RECV,
			    stcb->sctp_ep, stcb, NULL,
			    SCTP_FROM_SCTP_INDATA + SCTP_LOC_17);
		}
		sctp_send_shutdown(stcb,
		    ((stcb->asoc.alternate) ? stcb->asoc.alternate : stcb->asoc.primary_destination));
		if (is_a_gap) {
			sctp_send_sack(stcb, SCTP_SO_NOT_LOCKED);
		}
	} else {
		/*
		 * CMT DAC algorithm: increase number of packets received
		 * since last ack
		 */
		stcb->asoc.cmt_dac_pkts_rcvd++;

		if ((stcb->asoc.send_sack == 1) ||	/* We need to send a
							 * SACK */
		    ((was_a_gap) && (is_a_gap == 0)) ||	/* was a gap, but no
							 * longer is one */
		    (stcb->asoc.numduptsns) ||	/* we have dup's */
		    (is_a_gap) ||	/* is still a gap */
		    (stcb->asoc.delayed_ack == 0) ||	/* Delayed sack disabled */
		    (stcb->asoc.data_pkts_seen >= stcb->asoc.sack_freq)	/* hit limit of pkts */
		    ) {

			if ((stcb->asoc.sctp_cmt_on_off > 0) &&
			    (SCTP_BASE_SYSCTL(sctp_cmt_use_dac)) &&
			    (stcb->asoc.send_sack == 0) &&
			    (stcb->asoc.numduptsns == 0) &&
			    (stcb->asoc.delayed_ack) &&
			    (!SCTP_OS_TIMER_PENDING(&stcb->asoc.dack_timer.timer))) {

				/*
				 * CMT DAC algorithm: With CMT, delay acks
				 * even in the face of
				 *
				 * reordering. Therefore, if acks that do
				 * not have to be sent because of the above
				 * reasons, will be delayed. That is, acks
				 * that would have been sent due to gap
				 * reports will be delayed with DAC. Start
				 * the delayed ack timer.
				 */
				sctp_timer_start(SCTP_TIMER_TYPE_RECV,
				    stcb->sctp_ep, stcb, NULL);
			} else {
				/*
				 * Ok we must build a SACK since the timer
				 * is pending, we got our first packet OR
				 * there are gaps or duplicates.
				 */
				(void)SCTP_OS_TIMER_STOP(&stcb->asoc.dack_timer.timer);
				sctp_send_sack(stcb, SCTP_SO_NOT_LOCKED);
			}
		} else {
			if (!SCTP_OS_TIMER_PENDING(&stcb->asoc.dack_timer.timer)) {
				sctp_timer_start(SCTP_TIMER_TYPE_RECV,
				    stcb->sctp_ep, stcb, NULL);
			}
		}
	}
}

int
sctp_process_data(struct mbuf **mm, int iphlen, int *offset, int length,
    struct sctp_inpcb *inp, struct sctp_tcb *stcb,
    struct sctp_nets *net, uint32_t *high_tsn)
{
	struct sctp_chunkhdr *ch, chunk_buf;
	struct sctp_association *asoc;
	int num_chunks = 0;	/* number of control chunks processed */
	int stop_proc = 0;
	int break_flag, last_chunk;
	int abort_flag = 0, was_a_gap;
	struct mbuf *m;
	uint32_t highest_tsn;
	uint16_t chk_length;

	/* set the rwnd */
	sctp_set_rwnd(stcb, &stcb->asoc);

	m = *mm;
	SCTP_TCB_LOCK_ASSERT(stcb);
	asoc = &stcb->asoc;
	if (SCTP_TSN_GT(asoc->highest_tsn_inside_nr_map, asoc->highest_tsn_inside_map)) {
		highest_tsn = asoc->highest_tsn_inside_nr_map;
	} else {
		highest_tsn = asoc->highest_tsn_inside_map;
	}
	was_a_gap = SCTP_TSN_GT(highest_tsn, stcb->asoc.cumulative_tsn);
	/*
	 * setup where we got the last DATA packet from for any SACK that
	 * may need to go out. Don't bump the net. This is done ONLY when a
	 * chunk is assigned.
	 */
	asoc->last_data_chunk_from = net;

	/*-
	 * Now before we proceed we must figure out if this is a wasted
	 * cluster... i.e. it is a small packet sent in and yet the driver
	 * underneath allocated a full cluster for it. If so we must copy it
	 * to a smaller mbuf and free up the cluster mbuf. This will help
	 * with cluster starvation. Note for __Panda__ we don't do this
	 * since it has clusters all the way down to 64 bytes.
	 */
	if (SCTP_BUF_LEN(m) < (long)MLEN && SCTP_BUF_NEXT(m) == NULL) {
		/* we only handle mbufs that are singletons.. not chains */
		m = sctp_get_mbuf_for_msg(SCTP_BUF_LEN(m), 0, M_NOWAIT, 1, MT_DATA);
		if (m) {
			/* ok lets see if we can copy the data up */
			caddr_t *from, *to;

			/* get the pointers and copy */
			to = mtod(m, caddr_t *);
			from = mtod((*mm), caddr_t *);
			memcpy(to, from, SCTP_BUF_LEN((*mm)));
			/* copy the length and free up the old */
			SCTP_BUF_LEN(m) = SCTP_BUF_LEN((*mm));
			sctp_m_freem(*mm);
			/* success, back copy */
			*mm = m;
		} else {
			/* We are in trouble in the mbuf world .. yikes */
			m = *mm;
		}
	}
	/* get pointer to the first chunk header */
	ch = (struct sctp_chunkhdr *)sctp_m_getptr(m, *offset,
	    sizeof(struct sctp_chunkhdr),
	    (uint8_t *)&chunk_buf);
	if (ch == NULL) {
		return (1);
	}
	/*
	 * process all DATA chunks...
	 */
	*high_tsn = asoc->cumulative_tsn;
	break_flag = 0;
	asoc->data_pkts_seen++;
	while (stop_proc == 0) {
		/* validate chunk length */
		chk_length = ntohs(ch->chunk_length);
		if (length - *offset < chk_length) {
			/* all done, mutulated chunk */
			stop_proc = 1;
			continue;
		}
		if ((asoc->idata_supported == 1) &&
		    (ch->chunk_type == SCTP_DATA)) {
			struct mbuf *op_err;
			char msg[SCTP_DIAG_INFO_LEN];

			snprintf(msg, sizeof(msg), "%s", "I-DATA chunk received when DATA was negotiated");
			op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
			stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_18;
			sctp_abort_an_association(inp, stcb, op_err, SCTP_SO_NOT_LOCKED);
			return (2);
		}
		if ((asoc->idata_supported == 0) &&
		    (ch->chunk_type == SCTP_IDATA)) {
			struct mbuf *op_err;
			char msg[SCTP_DIAG_INFO_LEN];

			snprintf(msg, sizeof(msg), "%s", "DATA chunk received when I-DATA was negotiated");
			op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
			stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_19;
			sctp_abort_an_association(inp, stcb, op_err, SCTP_SO_NOT_LOCKED);
			return (2);
		}
		if ((ch->chunk_type == SCTP_DATA) ||
		    (ch->chunk_type == SCTP_IDATA)) {
			uint16_t clen;

			if (ch->chunk_type == SCTP_DATA) {
				clen = sizeof(struct sctp_data_chunk);
			} else {
				clen = sizeof(struct sctp_idata_chunk);
			}
			if (chk_length < clen) {
				/*
				 * Need to send an abort since we had a
				 * invalid data chunk.
				 */
				struct mbuf *op_err;
				char msg[SCTP_DIAG_INFO_LEN];

				snprintf(msg, sizeof(msg), "%s chunk of length %u",
				    ch->chunk_type == SCTP_DATA ? "DATA" : "I-DATA",
				    chk_length);
				op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
				stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_20;
				sctp_abort_an_association(inp, stcb, op_err, SCTP_SO_NOT_LOCKED);
				return (2);
			}
#ifdef SCTP_AUDITING_ENABLED
			sctp_audit_log(0xB1, 0);
#endif
			if (SCTP_SIZE32(chk_length) == (length - *offset)) {
				last_chunk = 1;
			} else {
				last_chunk = 0;
			}
			if (sctp_process_a_data_chunk(stcb, asoc, mm, *offset,
			    chk_length, net, high_tsn, &abort_flag, &break_flag,
			    last_chunk, ch->chunk_type)) {
				num_chunks++;
			}
			if (abort_flag)
				return (2);

			if (break_flag) {
				/*
				 * Set because of out of rwnd space and no
				 * drop rep space left.
				 */
				stop_proc = 1;
				continue;
			}
		} else {
			/* not a data chunk in the data region */
			switch (ch->chunk_type) {
			case SCTP_INITIATION:
			case SCTP_INITIATION_ACK:
			case SCTP_SELECTIVE_ACK:
			case SCTP_NR_SELECTIVE_ACK:
			case SCTP_HEARTBEAT_REQUEST:
			case SCTP_HEARTBEAT_ACK:
			case SCTP_ABORT_ASSOCIATION:
			case SCTP_SHUTDOWN:
			case SCTP_SHUTDOWN_ACK:
			case SCTP_OPERATION_ERROR:
			case SCTP_COOKIE_ECHO:
			case SCTP_COOKIE_ACK:
			case SCTP_ECN_ECHO:
			case SCTP_ECN_CWR:
			case SCTP_SHUTDOWN_COMPLETE:
			case SCTP_AUTHENTICATION:
			case SCTP_ASCONF_ACK:
			case SCTP_PACKET_DROPPED:
			case SCTP_STREAM_RESET:
			case SCTP_FORWARD_CUM_TSN:
			case SCTP_ASCONF:
				{
					/*
					 * Now, what do we do with KNOWN
					 * chunks that are NOT in the right
					 * place?
					 *
					 * For now, I do nothing but ignore
					 * them. We may later want to add
					 * sysctl stuff to switch out and do
					 * either an ABORT() or possibly
					 * process them.
					 */
					struct mbuf *op_err;
					char msg[SCTP_DIAG_INFO_LEN];

					snprintf(msg, sizeof(msg), "DATA chunk followed by chunk of type %2.2x",
					    ch->chunk_type);
					op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
					sctp_abort_an_association(inp, stcb, op_err, SCTP_SO_NOT_LOCKED);
					return (2);
				}
			default:
				/*
				 * Unknown chunk type: use bit rules after
				 * checking length
				 */
				if (chk_length < sizeof(struct sctp_chunkhdr)) {
					/*
					 * Need to send an abort since we
					 * had a invalid chunk.
					 */
					struct mbuf *op_err;
					char msg[SCTP_DIAG_INFO_LEN];

					snprintf(msg, sizeof(msg), "Chunk of length %u",
					    chk_length);
					op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
					stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_20;
					sctp_abort_an_association(inp, stcb, op_err, SCTP_SO_NOT_LOCKED);
					return (2);
				}
				if (ch->chunk_type & 0x40) {
					/* Add a error report to the queue */
					struct mbuf *op_err;
					struct sctp_gen_error_cause *cause;

					op_err = sctp_get_mbuf_for_msg(sizeof(struct sctp_gen_error_cause),
					    0, M_NOWAIT, 1, MT_DATA);
					if (op_err != NULL) {
						cause = mtod(op_err, struct sctp_gen_error_cause *);
						cause->code = htons(SCTP_CAUSE_UNRECOG_CHUNK);
						cause->length = htons((uint16_t)(chk_length + sizeof(struct sctp_gen_error_cause)));
						SCTP_BUF_LEN(op_err) = sizeof(struct sctp_gen_error_cause);
						SCTP_BUF_NEXT(op_err) = SCTP_M_COPYM(m, *offset, chk_length, M_NOWAIT);
						if (SCTP_BUF_NEXT(op_err) != NULL) {
							sctp_queue_op_err(stcb, op_err);
						} else {
							sctp_m_freem(op_err);
						}
					}
				}
				if ((ch->chunk_type & 0x80) == 0) {
					/* discard the rest of this packet */
					stop_proc = 1;
				}	/* else skip this bad chunk and
					 * continue... */
				break;
			}	/* switch of chunk type */
		}
		*offset += SCTP_SIZE32(chk_length);
		if ((*offset >= length) || stop_proc) {
			/* no more data left in the mbuf chain */
			stop_proc = 1;
			continue;
		}
		ch = (struct sctp_chunkhdr *)sctp_m_getptr(m, *offset,
		    sizeof(struct sctp_chunkhdr),
		    (uint8_t *)&chunk_buf);
		if (ch == NULL) {
			*offset = length;
			stop_proc = 1;
			continue;
		}
	}
	if (break_flag) {
		/*
		 * we need to report rwnd overrun drops.
		 */
		sctp_send_packet_dropped(stcb, net, *mm, length, iphlen, 0);
	}
	if (num_chunks) {
		/*
		 * Did we get data, if so update the time for auto-close and
		 * give peer credit for being alive.
		 */
		SCTP_STAT_INCR(sctps_recvpktwithdata);
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_THRESHOLD_LOGGING) {
			sctp_misc_ints(SCTP_THRESHOLD_CLEAR,
			    stcb->asoc.overall_error_count,
			    0,
			    SCTP_FROM_SCTP_INDATA,
			    __LINE__);
		}
		stcb->asoc.overall_error_count = 0;
		(void)SCTP_GETTIME_TIMEVAL(&stcb->asoc.time_last_rcvd);
	}
	/* now service all of the reassm queue if needed */
	if (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_SENT) {
		/* Assure that we ack right away */
		stcb->asoc.send_sack = 1;
	}
	/* Start a sack timer or QUEUE a SACK for sending */
	sctp_sack_check(stcb, was_a_gap);
	return (0);
}

static int
sctp_process_segment_range(struct sctp_tcb *stcb, struct sctp_tmit_chunk **p_tp1, uint32_t last_tsn,
    uint16_t frag_strt, uint16_t frag_end, int nr_sacking,
    int *num_frs,
    uint32_t *biggest_newly_acked_tsn,
    uint32_t *this_sack_lowest_newack,
    int *rto_ok)
{
	struct sctp_tmit_chunk *tp1;
	unsigned int theTSN;
	int j, wake_him = 0, circled = 0;

	/* Recover the tp1 we last saw */
	tp1 = *p_tp1;
	if (tp1 == NULL) {
		tp1 = TAILQ_FIRST(&stcb->asoc.sent_queue);
	}
	for (j = frag_strt; j <= frag_end; j++) {
		theTSN = j + last_tsn;
		while (tp1) {
			if (tp1->rec.data.doing_fast_retransmit)
				(*num_frs) += 1;

			/*-
			 * CMT: CUCv2 algorithm. For each TSN being
			 * processed from the sent queue, track the
			 * next expected pseudo-cumack, or
			 * rtx_pseudo_cumack, if required. Separate
			 * cumack trackers for first transmissions,
			 * and retransmissions.
			 */
			if ((tp1->sent < SCTP_DATAGRAM_RESEND) &&
			    (tp1->whoTo->find_pseudo_cumack == 1) &&
			    (tp1->snd_count == 1)) {
				tp1->whoTo->pseudo_cumack = tp1->rec.data.tsn;
				tp1->whoTo->find_pseudo_cumack = 0;
			}
			if ((tp1->sent < SCTP_DATAGRAM_RESEND) &&
			    (tp1->whoTo->find_rtx_pseudo_cumack == 1) &&
			    (tp1->snd_count > 1)) {
				tp1->whoTo->rtx_pseudo_cumack = tp1->rec.data.tsn;
				tp1->whoTo->find_rtx_pseudo_cumack = 0;
			}
			if (tp1->rec.data.tsn == theTSN) {
				if (tp1->sent != SCTP_DATAGRAM_UNSENT) {
					/*-
					 * must be held until
					 * cum-ack passes
					 */
					if (tp1->sent < SCTP_DATAGRAM_RESEND) {
						/*-
						 * If it is less than RESEND, it is
						 * now no-longer in flight.
						 * Higher values may already be set
						 * via previous Gap Ack Blocks...
						 * i.e. ACKED or RESEND.
						 */
						if (SCTP_TSN_GT(tp1->rec.data.tsn,
						    *biggest_newly_acked_tsn)) {
							*biggest_newly_acked_tsn = tp1->rec.data.tsn;
						}
						/*-
						 * CMT: SFR algo (and HTNA) - set
						 * saw_newack to 1 for dest being
						 * newly acked. update
						 * this_sack_highest_newack if
						 * appropriate.
						 */
						if (tp1->rec.data.chunk_was_revoked == 0)
							tp1->whoTo->saw_newack = 1;

						if (SCTP_TSN_GT(tp1->rec.data.tsn,
						    tp1->whoTo->this_sack_highest_newack)) {
							tp1->whoTo->this_sack_highest_newack =
							    tp1->rec.data.tsn;
						}
						/*-
						 * CMT DAC algo: also update
						 * this_sack_lowest_newack
						 */
						if (*this_sack_lowest_newack == 0) {
							if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SACK_LOGGING_ENABLE) {
								sctp_log_sack(*this_sack_lowest_newack,
								    last_tsn,
								    tp1->rec.data.tsn,
								    0,
								    0,
								    SCTP_LOG_TSN_ACKED);
							}
							*this_sack_lowest_newack = tp1->rec.data.tsn;
						}
						/*-
						 * CMT: CUCv2 algorithm. If (rtx-)pseudo-cumack for corresp
						 * dest is being acked, then we have a new (rtx-)pseudo-cumack. Set
						 * new_(rtx_)pseudo_cumack to TRUE so that the cwnd for this dest can be
						 * updated. Also trigger search for the next expected (rtx-)pseudo-cumack.
						 * Separate pseudo_cumack trackers for first transmissions and
						 * retransmissions.
						 */
						if (tp1->rec.data.tsn == tp1->whoTo->pseudo_cumack) {
							if (tp1->rec.data.chunk_was_revoked == 0) {
								tp1->whoTo->new_pseudo_cumack = 1;
							}
							tp1->whoTo->find_pseudo_cumack = 1;
						}
						if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
							sctp_log_cwnd(stcb, tp1->whoTo, tp1->rec.data.tsn, SCTP_CWND_LOG_FROM_SACK);
						}
						if (tp1->rec.data.tsn == tp1->whoTo->rtx_pseudo_cumack) {
							if (tp1->rec.data.chunk_was_revoked == 0) {
								tp1->whoTo->new_pseudo_cumack = 1;
							}
							tp1->whoTo->find_rtx_pseudo_cumack = 1;
						}
						if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SACK_LOGGING_ENABLE) {
							sctp_log_sack(*biggest_newly_acked_tsn,
							    last_tsn,
							    tp1->rec.data.tsn,
							    frag_strt,
							    frag_end,
							    SCTP_LOG_TSN_ACKED);
						}
						if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FLIGHT_LOGGING_ENABLE) {
							sctp_misc_ints(SCTP_FLIGHT_LOG_DOWN_GAP,
							    tp1->whoTo->flight_size,
							    tp1->book_size,
							    (uint32_t)(uintptr_t)tp1->whoTo,
							    tp1->rec.data.tsn);
						}
						sctp_flight_size_decrease(tp1);
						if (stcb->asoc.cc_functions.sctp_cwnd_update_tsn_acknowledged) {
							(*stcb->asoc.cc_functions.sctp_cwnd_update_tsn_acknowledged) (tp1->whoTo,
							    tp1);
						}
						sctp_total_flight_decrease(stcb, tp1);

						tp1->whoTo->net_ack += tp1->send_size;
						if (tp1->snd_count < 2) {
							/*-
							 * True non-retransmitted chunk
							 */
							tp1->whoTo->net_ack2 += tp1->send_size;

							/*-
							 * update RTO too ?
							 */
							if (tp1->do_rtt) {
								if (*rto_ok) {
									tp1->whoTo->RTO =
									    sctp_calculate_rto(stcb,
									    &stcb->asoc,
									    tp1->whoTo,
									    &tp1->sent_rcv_time,
									    SCTP_RTT_FROM_DATA);
									*rto_ok = 0;
								}
								if (tp1->whoTo->rto_needed == 0) {
									tp1->whoTo->rto_needed = 1;
								}
								tp1->do_rtt = 0;
							}
						}

					}
					if (tp1->sent <= SCTP_DATAGRAM_RESEND) {
						if (SCTP_TSN_GT(tp1->rec.data.tsn,
						    stcb->asoc.this_sack_highest_gap)) {
							stcb->asoc.this_sack_highest_gap =
							    tp1->rec.data.tsn;
						}
						if (tp1->sent == SCTP_DATAGRAM_RESEND) {
							sctp_ucount_decr(stcb->asoc.sent_queue_retran_cnt);
#ifdef SCTP_AUDITING_ENABLED
							sctp_audit_log(0xB2,
							    (stcb->asoc.sent_queue_retran_cnt & 0x000000ff));
#endif
						}
					}
					/*-
					 * All chunks NOT UNSENT fall through here and are marked
					 * (leave PR-SCTP ones that are to skip alone though)
					 */
					if ((tp1->sent != SCTP_FORWARD_TSN_SKIP) &&
					    (tp1->sent != SCTP_DATAGRAM_NR_ACKED)) {
						tp1->sent = SCTP_DATAGRAM_MARKED;
					}
					if (tp1->rec.data.chunk_was_revoked) {
						/* deflate the cwnd */
						tp1->whoTo->cwnd -= tp1->book_size;
						tp1->rec.data.chunk_was_revoked = 0;
					}
					/* NR Sack code here */
					if (nr_sacking &&
					    (tp1->sent != SCTP_DATAGRAM_NR_ACKED)) {
						if (stcb->asoc.strmout[tp1->rec.data.sid].chunks_on_queues > 0) {
							stcb->asoc.strmout[tp1->rec.data.sid].chunks_on_queues--;
#ifdef INVARIANTS
						} else {
							panic("No chunks on the queues for sid %u.", tp1->rec.data.sid);
#endif
						}
						if ((stcb->asoc.strmout[tp1->rec.data.sid].chunks_on_queues == 0) &&
						    (stcb->asoc.strmout[tp1->rec.data.sid].state == SCTP_STREAM_RESET_PENDING) &&
						    TAILQ_EMPTY(&stcb->asoc.strmout[tp1->rec.data.sid].outqueue)) {
							stcb->asoc.trigger_reset = 1;
						}
						tp1->sent = SCTP_DATAGRAM_NR_ACKED;
						if (tp1->data) {
							/*
							 * sa_ignore
							 * NO_NULL_CHK
							 */
							sctp_free_bufspace(stcb, &stcb->asoc, tp1, 1);
							sctp_m_freem(tp1->data);
							tp1->data = NULL;
						}
						wake_him++;
					}
				}
				break;
			}	/* if (tp1->tsn == theTSN) */
			if (SCTP_TSN_GT(tp1->rec.data.tsn, theTSN)) {
				break;
			}
			tp1 = TAILQ_NEXT(tp1, sctp_next);
			if ((tp1 == NULL) && (circled == 0)) {
				circled++;
				tp1 = TAILQ_FIRST(&stcb->asoc.sent_queue);
			}
		}		/* end while (tp1) */
		if (tp1 == NULL) {
			circled = 0;
			tp1 = TAILQ_FIRST(&stcb->asoc.sent_queue);
		}
		/* In case the fragments were not in order we must reset */
	}			/* end for (j = fragStart */
	*p_tp1 = tp1;
	return (wake_him);	/* Return value only used for nr-sack */
}


static int
sctp_handle_segments(struct mbuf *m, int *offset, struct sctp_tcb *stcb, struct sctp_association *asoc,
    uint32_t last_tsn, uint32_t *biggest_tsn_acked,
    uint32_t *biggest_newly_acked_tsn, uint32_t *this_sack_lowest_newack,
    int num_seg, int num_nr_seg, int *rto_ok)
{
	struct sctp_gap_ack_block *frag, block;
	struct sctp_tmit_chunk *tp1;
	int i;
	int num_frs = 0;
	int chunk_freed;
	int non_revocable;
	uint16_t frag_strt, frag_end, prev_frag_end;

	tp1 = TAILQ_FIRST(&asoc->sent_queue);
	prev_frag_end = 0;
	chunk_freed = 0;

	for (i = 0; i < (num_seg + num_nr_seg); i++) {
		if (i == num_seg) {
			prev_frag_end = 0;
			tp1 = TAILQ_FIRST(&asoc->sent_queue);
		}
		frag = (struct sctp_gap_ack_block *)sctp_m_getptr(m, *offset,
		    sizeof(struct sctp_gap_ack_block), (uint8_t *)&block);
		*offset += sizeof(block);
		if (frag == NULL) {
			return (chunk_freed);
		}
		frag_strt = ntohs(frag->start);
		frag_end = ntohs(frag->end);

		if (frag_strt > frag_end) {
			/* This gap report is malformed, skip it. */
			continue;
		}
		if (frag_strt <= prev_frag_end) {
			/* This gap report is not in order, so restart. */
			tp1 = TAILQ_FIRST(&asoc->sent_queue);
		}
		if (SCTP_TSN_GT((last_tsn + frag_end), *biggest_tsn_acked)) {
			*biggest_tsn_acked = last_tsn + frag_end;
		}
		if (i < num_seg) {
			non_revocable = 0;
		} else {
			non_revocable = 1;
		}
		if (sctp_process_segment_range(stcb, &tp1, last_tsn, frag_strt, frag_end,
		    non_revocable, &num_frs, biggest_newly_acked_tsn,
		    this_sack_lowest_newack, rto_ok)) {
			chunk_freed = 1;
		}
		prev_frag_end = frag_end;
	}
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FR_LOGGING_ENABLE) {
		if (num_frs)
			sctp_log_fr(*biggest_tsn_acked,
			    *biggest_newly_acked_tsn,
			    last_tsn, SCTP_FR_LOG_BIGGEST_TSNS);
	}
	return (chunk_freed);
}

static void
sctp_check_for_revoked(struct sctp_tcb *stcb,
    struct sctp_association *asoc, uint32_t cumack,
    uint32_t biggest_tsn_acked)
{
	struct sctp_tmit_chunk *tp1;

	TAILQ_FOREACH(tp1, &asoc->sent_queue, sctp_next) {
		if (SCTP_TSN_GT(tp1->rec.data.tsn, cumack)) {
			/*
			 * ok this guy is either ACK or MARKED. If it is
			 * ACKED it has been previously acked but not this
			 * time i.e. revoked.  If it is MARKED it was ACK'ed
			 * again.
			 */
			if (SCTP_TSN_GT(tp1->rec.data.tsn, biggest_tsn_acked)) {
				break;
			}
			if (tp1->sent == SCTP_DATAGRAM_ACKED) {
				/* it has been revoked */
				tp1->sent = SCTP_DATAGRAM_SENT;
				tp1->rec.data.chunk_was_revoked = 1;
				/*
				 * We must add this stuff back in to assure
				 * timers and such get started.
				 */
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FLIGHT_LOGGING_ENABLE) {
					sctp_misc_ints(SCTP_FLIGHT_LOG_UP_REVOKE,
					    tp1->whoTo->flight_size,
					    tp1->book_size,
					    (uint32_t)(uintptr_t)tp1->whoTo,
					    tp1->rec.data.tsn);
				}
				sctp_flight_size_increase(tp1);
				sctp_total_flight_increase(stcb, tp1);
				/*
				 * We inflate the cwnd to compensate for our
				 * artificial inflation of the flight_size.
				 */
				tp1->whoTo->cwnd += tp1->book_size;
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SACK_LOGGING_ENABLE) {
					sctp_log_sack(asoc->last_acked_seq,
					    cumack,
					    tp1->rec.data.tsn,
					    0,
					    0,
					    SCTP_LOG_TSN_REVOKED);
				}
			} else if (tp1->sent == SCTP_DATAGRAM_MARKED) {
				/* it has been re-acked in this SACK */
				tp1->sent = SCTP_DATAGRAM_ACKED;
			}
		}
		if (tp1->sent == SCTP_DATAGRAM_UNSENT)
			break;
	}
}


static void
sctp_strike_gap_ack_chunks(struct sctp_tcb *stcb, struct sctp_association *asoc,
    uint32_t biggest_tsn_acked, uint32_t biggest_tsn_newly_acked, uint32_t this_sack_lowest_newack, int accum_moved)
{
	struct sctp_tmit_chunk *tp1;
	int strike_flag = 0;
	struct timeval now;
	int tot_retrans = 0;
	uint32_t sending_seq;
	struct sctp_nets *net;
	int num_dests_sacked = 0;

	/*
	 * select the sending_seq, this is either the next thing ready to be
	 * sent but not transmitted, OR, the next seq we assign.
	 */
	tp1 = TAILQ_FIRST(&stcb->asoc.send_queue);
	if (tp1 == NULL) {
		sending_seq = asoc->sending_seq;
	} else {
		sending_seq = tp1->rec.data.tsn;
	}

	/* CMT DAC algo: finding out if SACK is a mixed SACK */
	if ((asoc->sctp_cmt_on_off > 0) &&
	    SCTP_BASE_SYSCTL(sctp_cmt_use_dac)) {
		TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
			if (net->saw_newack)
				num_dests_sacked++;
		}
	}
	if (stcb->asoc.prsctp_supported) {
		(void)SCTP_GETTIME_TIMEVAL(&now);
	}
	TAILQ_FOREACH(tp1, &asoc->sent_queue, sctp_next) {
		strike_flag = 0;
		if (tp1->no_fr_allowed) {
			/* this one had a timeout or something */
			continue;
		}
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FR_LOGGING_ENABLE) {
			if (tp1->sent < SCTP_DATAGRAM_RESEND)
				sctp_log_fr(biggest_tsn_newly_acked,
				    tp1->rec.data.tsn,
				    tp1->sent,
				    SCTP_FR_LOG_CHECK_STRIKE);
		}
		if (SCTP_TSN_GT(tp1->rec.data.tsn, biggest_tsn_acked) ||
		    tp1->sent == SCTP_DATAGRAM_UNSENT) {
			/* done */
			break;
		}
		if (stcb->asoc.prsctp_supported) {
			if ((PR_SCTP_TTL_ENABLED(tp1->flags)) && tp1->sent < SCTP_DATAGRAM_ACKED) {
				/* Is it expired? */
				if (timevalcmp(&now, &tp1->rec.data.timetodrop, >)) {
					/* Yes so drop it */
					if (tp1->data != NULL) {
						(void)sctp_release_pr_sctp_chunk(stcb, tp1, 1,
						    SCTP_SO_NOT_LOCKED);
					}
					continue;
				}
			}

		}
		if (SCTP_TSN_GT(tp1->rec.data.tsn, asoc->this_sack_highest_gap) &&
		    !(accum_moved && asoc->fast_retran_loss_recovery)) {
			/* we are beyond the tsn in the sack  */
			break;
		}
		if (tp1->sent >= SCTP_DATAGRAM_RESEND) {
			/* either a RESEND, ACKED, or MARKED */
			/* skip */
			if (tp1->sent == SCTP_FORWARD_TSN_SKIP) {
				/* Continue strikin FWD-TSN chunks */
				tp1->rec.data.fwd_tsn_cnt++;
			}
			continue;
		}
		/*
		 * CMT : SFR algo (covers part of DAC and HTNA as well)
		 */
		if (tp1->whoTo && tp1->whoTo->saw_newack == 0) {
			/*
			 * No new acks were receieved for data sent to this
			 * dest. Therefore, according to the SFR algo for
			 * CMT, no data sent to this dest can be marked for
			 * FR using this SACK.
			 */
			continue;
		} else if (tp1->whoTo &&
			    SCTP_TSN_GT(tp1->rec.data.tsn,
			    tp1->whoTo->this_sack_highest_newack) &&
		    !(accum_moved && asoc->fast_retran_loss_recovery)) {
			/*
			 * CMT: New acks were receieved for data sent to
			 * this dest. But no new acks were seen for data
			 * sent after tp1. Therefore, according to the SFR
			 * algo for CMT, tp1 cannot be marked for FR using
			 * this SACK. This step covers part of the DAC algo
			 * and the HTNA algo as well.
			 */
			continue;
		}
		/*
		 * Here we check to see if we were have already done a FR
		 * and if so we see if the biggest TSN we saw in the sack is
		 * smaller than the recovery point. If so we don't strike
		 * the tsn... otherwise we CAN strike the TSN.
		 */
		/*
		 * @@@ JRI: Check for CMT if (accum_moved &&
		 * asoc->fast_retran_loss_recovery && (sctp_cmt_on_off ==
		 * 0)) {
		 */
		if (accum_moved && asoc->fast_retran_loss_recovery) {
			/*
			 * Strike the TSN if in fast-recovery and cum-ack
			 * moved.
			 */
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FR_LOGGING_ENABLE) {
				sctp_log_fr(biggest_tsn_newly_acked,
				    tp1->rec.data.tsn,
				    tp1->sent,
				    SCTP_FR_LOG_STRIKE_CHUNK);
			}
			if (tp1->sent < SCTP_DATAGRAM_RESEND) {
				tp1->sent++;
			}
			if ((asoc->sctp_cmt_on_off > 0) &&
			    SCTP_BASE_SYSCTL(sctp_cmt_use_dac)) {
				/*
				 * CMT DAC algorithm: If SACK flag is set to
				 * 0, then lowest_newack test will not pass
				 * because it would have been set to the
				 * cumack earlier. If not already to be
				 * rtx'd, If not a mixed sack and if tp1 is
				 * not between two sacked TSNs, then mark by
				 * one more. NOTE that we are marking by one
				 * additional time since the SACK DAC flag
				 * indicates that two packets have been
				 * received after this missing TSN.
				 */
				if ((tp1->sent < SCTP_DATAGRAM_RESEND) && (num_dests_sacked == 1) &&
				    SCTP_TSN_GT(this_sack_lowest_newack, tp1->rec.data.tsn)) {
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FR_LOGGING_ENABLE) {
						sctp_log_fr(16 + num_dests_sacked,
						    tp1->rec.data.tsn,
						    tp1->sent,
						    SCTP_FR_LOG_STRIKE_CHUNK);
					}
					tp1->sent++;
				}
			}
		} else if ((tp1->rec.data.doing_fast_retransmit) &&
		    (asoc->sctp_cmt_on_off == 0)) {
			/*
			 * For those that have done a FR we must take
			 * special consideration if we strike. I.e the
			 * biggest_newly_acked must be higher than the
			 * sending_seq at the time we did the FR.
			 */
			if (
#ifdef SCTP_FR_TO_ALTERNATE
			/*
			 * If FR's go to new networks, then we must only do
			 * this for singly homed asoc's. However if the FR's
			 * go to the same network (Armando's work) then its
			 * ok to FR multiple times.
			 */
			    (asoc->numnets < 2)
#else
			    (1)
#endif
			    ) {

				if (SCTP_TSN_GE(biggest_tsn_newly_acked,
				    tp1->rec.data.fast_retran_tsn)) {
					/*
					 * Strike the TSN, since this ack is
					 * beyond where things were when we
					 * did a FR.
					 */
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FR_LOGGING_ENABLE) {
						sctp_log_fr(biggest_tsn_newly_acked,
						    tp1->rec.data.tsn,
						    tp1->sent,
						    SCTP_FR_LOG_STRIKE_CHUNK);
					}
					if (tp1->sent < SCTP_DATAGRAM_RESEND) {
						tp1->sent++;
					}
					strike_flag = 1;
					if ((asoc->sctp_cmt_on_off > 0) &&
					    SCTP_BASE_SYSCTL(sctp_cmt_use_dac)) {
						/*
						 * CMT DAC algorithm: If
						 * SACK flag is set to 0,
						 * then lowest_newack test
						 * will not pass because it
						 * would have been set to
						 * the cumack earlier. If
						 * not already to be rtx'd,
						 * If not a mixed sack and
						 * if tp1 is not between two
						 * sacked TSNs, then mark by
						 * one more. NOTE that we
						 * are marking by one
						 * additional time since the
						 * SACK DAC flag indicates
						 * that two packets have
						 * been received after this
						 * missing TSN.
						 */
						if ((tp1->sent < SCTP_DATAGRAM_RESEND) &&
						    (num_dests_sacked == 1) &&
						    SCTP_TSN_GT(this_sack_lowest_newack,
						    tp1->rec.data.tsn)) {
							if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FR_LOGGING_ENABLE) {
								sctp_log_fr(32 + num_dests_sacked,
								    tp1->rec.data.tsn,
								    tp1->sent,
								    SCTP_FR_LOG_STRIKE_CHUNK);
							}
							if (tp1->sent < SCTP_DATAGRAM_RESEND) {
								tp1->sent++;
							}
						}
					}
				}
			}
			/*
			 * JRI: TODO: remove code for HTNA algo. CMT's SFR
			 * algo covers HTNA.
			 */
		} else if (SCTP_TSN_GT(tp1->rec.data.tsn,
		    biggest_tsn_newly_acked)) {
			/*
			 * We don't strike these: This is the  HTNA
			 * algorithm i.e. we don't strike If our TSN is
			 * larger than the Highest TSN Newly Acked.
			 */
			;
		} else {
			/* Strike the TSN */
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FR_LOGGING_ENABLE) {
				sctp_log_fr(biggest_tsn_newly_acked,
				    tp1->rec.data.tsn,
				    tp1->sent,
				    SCTP_FR_LOG_STRIKE_CHUNK);
			}
			if (tp1->sent < SCTP_DATAGRAM_RESEND) {
				tp1->sent++;
			}
			if ((asoc->sctp_cmt_on_off > 0) &&
			    SCTP_BASE_SYSCTL(sctp_cmt_use_dac)) {
				/*
				 * CMT DAC algorithm: If SACK flag is set to
				 * 0, then lowest_newack test will not pass
				 * because it would have been set to the
				 * cumack earlier. If not already to be
				 * rtx'd, If not a mixed sack and if tp1 is
				 * not between two sacked TSNs, then mark by
				 * one more. NOTE that we are marking by one
				 * additional time since the SACK DAC flag
				 * indicates that two packets have been
				 * received after this missing TSN.
				 */
				if ((tp1->sent < SCTP_DATAGRAM_RESEND) && (num_dests_sacked == 1) &&
				    SCTP_TSN_GT(this_sack_lowest_newack, tp1->rec.data.tsn)) {
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FR_LOGGING_ENABLE) {
						sctp_log_fr(48 + num_dests_sacked,
						    tp1->rec.data.tsn,
						    tp1->sent,
						    SCTP_FR_LOG_STRIKE_CHUNK);
					}
					tp1->sent++;
				}
			}
		}
		if (tp1->sent == SCTP_DATAGRAM_RESEND) {
			struct sctp_nets *alt;

			/* fix counts and things */
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FLIGHT_LOGGING_ENABLE) {
				sctp_misc_ints(SCTP_FLIGHT_LOG_DOWN_RSND,
				    (tp1->whoTo ? (tp1->whoTo->flight_size) : 0),
				    tp1->book_size,
				    (uint32_t)(uintptr_t)tp1->whoTo,
				    tp1->rec.data.tsn);
			}
			if (tp1->whoTo) {
				tp1->whoTo->net_ack++;
				sctp_flight_size_decrease(tp1);
				if (stcb->asoc.cc_functions.sctp_cwnd_update_tsn_acknowledged) {
					(*stcb->asoc.cc_functions.sctp_cwnd_update_tsn_acknowledged) (tp1->whoTo,
					    tp1);
				}
			}

			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_RWND_ENABLE) {
				sctp_log_rwnd(SCTP_INCREASE_PEER_RWND,
				    asoc->peers_rwnd, tp1->send_size, SCTP_BASE_SYSCTL(sctp_peer_chunk_oh));
			}
			/* add back to the rwnd */
			asoc->peers_rwnd += (tp1->send_size + SCTP_BASE_SYSCTL(sctp_peer_chunk_oh));

			/* remove from the total flight */
			sctp_total_flight_decrease(stcb, tp1);

			if ((stcb->asoc.prsctp_supported) &&
			    (PR_SCTP_RTX_ENABLED(tp1->flags))) {
				/*
				 * Has it been retransmitted tv_sec times? -
				 * we store the retran count there.
				 */
				if (tp1->snd_count > tp1->rec.data.timetodrop.tv_sec) {
					/* Yes, so drop it */
					if (tp1->data != NULL) {
						(void)sctp_release_pr_sctp_chunk(stcb, tp1, 1,
						    SCTP_SO_NOT_LOCKED);
					}
					/* Make sure to flag we had a FR */
					if (tp1->whoTo != NULL) {
						tp1->whoTo->net_ack++;
					}
					continue;
				}
			}
			/*
			 * SCTP_PRINTF("OK, we are now ready to FR this
			 * guy\n");
			 */
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FR_LOGGING_ENABLE) {
				sctp_log_fr(tp1->rec.data.tsn, tp1->snd_count,
				    0, SCTP_FR_MARKED);
			}
			if (strike_flag) {
				/* This is a subsequent FR */
				SCTP_STAT_INCR(sctps_sendmultfastretrans);
			}
			sctp_ucount_incr(stcb->asoc.sent_queue_retran_cnt);
			if (asoc->sctp_cmt_on_off > 0) {
				/*
				 * CMT: Using RTX_SSTHRESH policy for CMT.
				 * If CMT is being used, then pick dest with
				 * largest ssthresh for any retransmission.
				 */
				tp1->no_fr_allowed = 1;
				alt = tp1->whoTo;
				/* sa_ignore NO_NULL_CHK */
				if (asoc->sctp_cmt_pf > 0) {
					/*
					 * JRS 5/18/07 - If CMT PF is on,
					 * use the PF version of
					 * find_alt_net()
					 */
					alt = sctp_find_alternate_net(stcb, alt, 2);
				} else {
					/*
					 * JRS 5/18/07 - If only CMT is on,
					 * use the CMT version of
					 * find_alt_net()
					 */
					/* sa_ignore NO_NULL_CHK */
					alt = sctp_find_alternate_net(stcb, alt, 1);
				}
				if (alt == NULL) {
					alt = tp1->whoTo;
				}
				/*
				 * CUCv2: If a different dest is picked for
				 * the retransmission, then new
				 * (rtx-)pseudo_cumack needs to be tracked
				 * for orig dest. Let CUCv2 track new (rtx-)
				 * pseudo-cumack always.
				 */
				if (tp1->whoTo) {
					tp1->whoTo->find_pseudo_cumack = 1;
					tp1->whoTo->find_rtx_pseudo_cumack = 1;
				}

			} else {	/* CMT is OFF */

#ifdef SCTP_FR_TO_ALTERNATE
				/* Can we find an alternate? */
				alt = sctp_find_alternate_net(stcb, tp1->whoTo, 0);
#else
				/*
				 * default behavior is to NOT retransmit
				 * FR's to an alternate. Armando Caro's
				 * paper details why.
				 */
				alt = tp1->whoTo;
#endif
			}

			tp1->rec.data.doing_fast_retransmit = 1;
			tot_retrans++;
			/* mark the sending seq for possible subsequent FR's */
			/*
			 * SCTP_PRINTF("Marking TSN for FR new value %x\n",
			 * (uint32_t)tpi->rec.data.tsn);
			 */
			if (TAILQ_EMPTY(&asoc->send_queue)) {
				/*
				 * If the queue of send is empty then its
				 * the next sequence number that will be
				 * assigned so we subtract one from this to
				 * get the one we last sent.
				 */
				tp1->rec.data.fast_retran_tsn = sending_seq;
			} else {
				/*
				 * If there are chunks on the send queue
				 * (unsent data that has made it from the
				 * stream queues but not out the door, we
				 * take the first one (which will have the
				 * lowest TSN) and subtract one to get the
				 * one we last sent.
				 */
				struct sctp_tmit_chunk *ttt;

				ttt = TAILQ_FIRST(&asoc->send_queue);
				tp1->rec.data.fast_retran_tsn =
				    ttt->rec.data.tsn;
			}

			if (tp1->do_rtt) {
				/*
				 * this guy had a RTO calculation pending on
				 * it, cancel it
				 */
				if ((tp1->whoTo != NULL) &&
				    (tp1->whoTo->rto_needed == 0)) {
					tp1->whoTo->rto_needed = 1;
				}
				tp1->do_rtt = 0;
			}
			if (alt != tp1->whoTo) {
				/* yes, there is an alternate. */
				sctp_free_remote_addr(tp1->whoTo);
				/* sa_ignore FREED_MEMORY */
				tp1->whoTo = alt;
				atomic_add_int(&alt->ref_count, 1);
			}
		}
	}
}

struct sctp_tmit_chunk *
sctp_try_advance_peer_ack_point(struct sctp_tcb *stcb,
    struct sctp_association *asoc)
{
	struct sctp_tmit_chunk *tp1, *tp2, *a_adv = NULL;
	struct timeval now;
	int now_filled = 0;

	if (asoc->prsctp_supported == 0) {
		return (NULL);
	}
	TAILQ_FOREACH_SAFE(tp1, &asoc->sent_queue, sctp_next, tp2) {
		if (tp1->sent != SCTP_FORWARD_TSN_SKIP &&
		    tp1->sent != SCTP_DATAGRAM_RESEND &&
		    tp1->sent != SCTP_DATAGRAM_NR_ACKED) {
			/* no chance to advance, out of here */
			break;
		}
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_TRY_ADVANCE) {
			if ((tp1->sent == SCTP_FORWARD_TSN_SKIP) ||
			    (tp1->sent == SCTP_DATAGRAM_NR_ACKED)) {
				sctp_misc_ints(SCTP_FWD_TSN_CHECK,
				    asoc->advanced_peer_ack_point,
				    tp1->rec.data.tsn, 0, 0);
			}
		}
		if (!PR_SCTP_ENABLED(tp1->flags)) {
			/*
			 * We can't fwd-tsn past any that are reliable aka
			 * retransmitted until the asoc fails.
			 */
			break;
		}
		if (!now_filled) {
			(void)SCTP_GETTIME_TIMEVAL(&now);
			now_filled = 1;
		}
		/*
		 * now we got a chunk which is marked for another
		 * retransmission to a PR-stream but has run out its chances
		 * already maybe OR has been marked to skip now. Can we skip
		 * it if its a resend?
		 */
		if (tp1->sent == SCTP_DATAGRAM_RESEND &&
		    (PR_SCTP_TTL_ENABLED(tp1->flags))) {
			/*
			 * Now is this one marked for resend and its time is
			 * now up?
			 */
			if (timevalcmp(&now, &tp1->rec.data.timetodrop, >)) {
				/* Yes so drop it */
				if (tp1->data) {
					(void)sctp_release_pr_sctp_chunk(stcb, tp1,
					    1, SCTP_SO_NOT_LOCKED);
				}
			} else {
				/*
				 * No, we are done when hit one for resend
				 * whos time as not expired.
				 */
				break;
			}
		}
		/*
		 * Ok now if this chunk is marked to drop it we can clean up
		 * the chunk, advance our peer ack point and we can check
		 * the next chunk.
		 */
		if ((tp1->sent == SCTP_FORWARD_TSN_SKIP) ||
		    (tp1->sent == SCTP_DATAGRAM_NR_ACKED)) {
			/* advance PeerAckPoint goes forward */
			if (SCTP_TSN_GT(tp1->rec.data.tsn, asoc->advanced_peer_ack_point)) {
				asoc->advanced_peer_ack_point = tp1->rec.data.tsn;
				a_adv = tp1;
			} else if (tp1->rec.data.tsn == asoc->advanced_peer_ack_point) {
				/* No update but we do save the chk */
				a_adv = tp1;
			}
		} else {
			/*
			 * If it is still in RESEND we can advance no
			 * further
			 */
			break;
		}
	}
	return (a_adv);
}

static int
sctp_fs_audit(struct sctp_association *asoc)
{
	struct sctp_tmit_chunk *chk;
	int inflight = 0, resend = 0, inbetween = 0, acked = 0, above = 0;
	int ret;
#ifndef INVARIANTS
	int entry_flight, entry_cnt;
#endif

	ret = 0;
#ifndef INVARIANTS
	entry_flight = asoc->total_flight;
	entry_cnt = asoc->total_flight_count;
#endif
	if (asoc->pr_sctp_cnt >= asoc->sent_queue_cnt)
		return (0);

	TAILQ_FOREACH(chk, &asoc->sent_queue, sctp_next) {
		if (chk->sent < SCTP_DATAGRAM_RESEND) {
			SCTP_PRINTF("Chk TSN: %u size: %d inflight cnt: %d\n",
			    chk->rec.data.tsn,
			    chk->send_size,
			    chk->snd_count);
			inflight++;
		} else if (chk->sent == SCTP_DATAGRAM_RESEND) {
			resend++;
		} else if (chk->sent < SCTP_DATAGRAM_ACKED) {
			inbetween++;
		} else if (chk->sent > SCTP_DATAGRAM_ACKED) {
			above++;
		} else {
			acked++;
		}
	}

	if ((inflight > 0) || (inbetween > 0)) {
#ifdef INVARIANTS
		panic("Flight size-express incorrect? \n");
#else
		SCTP_PRINTF("asoc->total_flight: %d cnt: %d\n",
		    entry_flight, entry_cnt);

		SCTP_PRINTF("Flight size-express incorrect F: %d I: %d R: %d Ab: %d ACK: %d\n",
		    inflight, inbetween, resend, above, acked);
		ret = 1;
#endif
	}
	return (ret);
}


static void
sctp_window_probe_recovery(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    struct sctp_tmit_chunk *tp1)
{
	tp1->window_probe = 0;
	if ((tp1->sent >= SCTP_DATAGRAM_ACKED) || (tp1->data == NULL)) {
		/* TSN's skipped we do NOT move back. */
		sctp_misc_ints(SCTP_FLIGHT_LOG_DWN_WP_FWD,
		    tp1->whoTo ? tp1->whoTo->flight_size : 0,
		    tp1->book_size,
		    (uint32_t)(uintptr_t)tp1->whoTo,
		    tp1->rec.data.tsn);
		return;
	}
	/* First setup this by shrinking flight */
	if (stcb->asoc.cc_functions.sctp_cwnd_update_tsn_acknowledged) {
		(*stcb->asoc.cc_functions.sctp_cwnd_update_tsn_acknowledged) (tp1->whoTo,
		    tp1);
	}
	sctp_flight_size_decrease(tp1);
	sctp_total_flight_decrease(stcb, tp1);
	/* Now mark for resend */
	tp1->sent = SCTP_DATAGRAM_RESEND;
	sctp_ucount_incr(asoc->sent_queue_retran_cnt);

	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FLIGHT_LOGGING_ENABLE) {
		sctp_misc_ints(SCTP_FLIGHT_LOG_DOWN_WP,
		    tp1->whoTo->flight_size,
		    tp1->book_size,
		    (uint32_t)(uintptr_t)tp1->whoTo,
		    tp1->rec.data.tsn);
	}
}

void
sctp_express_handle_sack(struct sctp_tcb *stcb, uint32_t cumack,
    uint32_t rwnd, int *abort_now, int ecne_seen)
{
	struct sctp_nets *net;
	struct sctp_association *asoc;
	struct sctp_tmit_chunk *tp1, *tp2;
	uint32_t old_rwnd;
	int win_probe_recovery = 0;
	int win_probe_recovered = 0;
	int j, done_once = 0;
	int rto_ok = 1;
	uint32_t send_s;

	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_SACK_ARRIVALS_ENABLE) {
		sctp_misc_ints(SCTP_SACK_LOG_EXPRESS, cumack,
		    rwnd, stcb->asoc.last_acked_seq, stcb->asoc.peers_rwnd);
	}
	SCTP_TCB_LOCK_ASSERT(stcb);
#ifdef SCTP_ASOCLOG_OF_TSNS
	stcb->asoc.cumack_log[stcb->asoc.cumack_log_at] = cumack;
	stcb->asoc.cumack_log_at++;
	if (stcb->asoc.cumack_log_at > SCTP_TSN_LOG_SIZE) {
		stcb->asoc.cumack_log_at = 0;
	}
#endif
	asoc = &stcb->asoc;
	old_rwnd = asoc->peers_rwnd;
	if (SCTP_TSN_GT(asoc->last_acked_seq, cumack)) {
		/* old ack */
		return;
	} else if (asoc->last_acked_seq == cumack) {
		/* Window update sack */
		asoc->peers_rwnd = sctp_sbspace_sub(rwnd,
		    (uint32_t)(asoc->total_flight + (asoc->total_flight_count * SCTP_BASE_SYSCTL(sctp_peer_chunk_oh))));
		if (asoc->peers_rwnd < stcb->sctp_ep->sctp_ep.sctp_sws_sender) {
			/* SWS sender side engages */
			asoc->peers_rwnd = 0;
		}
		if (asoc->peers_rwnd > old_rwnd) {
			goto again;
		}
		return;
	}

	/* First setup for CC stuff */
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		if (SCTP_TSN_GT(cumack, net->cwr_window_tsn)) {
			/* Drag along the window_tsn for cwr's */
			net->cwr_window_tsn = cumack;
		}
		net->prev_cwnd = net->cwnd;
		net->net_ack = 0;
		net->net_ack2 = 0;

		/*
		 * CMT: Reset CUC and Fast recovery algo variables before
		 * SACK processing
		 */
		net->new_pseudo_cumack = 0;
		net->will_exit_fast_recovery = 0;
		if (stcb->asoc.cc_functions.sctp_cwnd_prepare_net_for_sack) {
			(*stcb->asoc.cc_functions.sctp_cwnd_prepare_net_for_sack) (stcb, net);
		}
	}
	if (!TAILQ_EMPTY(&asoc->sent_queue)) {
		tp1 = TAILQ_LAST(&asoc->sent_queue,
		    sctpchunk_listhead);
		send_s = tp1->rec.data.tsn + 1;
	} else {
		send_s = asoc->sending_seq;
	}
	if (SCTP_TSN_GE(cumack, send_s)) {
		struct mbuf *op_err;
		char msg[SCTP_DIAG_INFO_LEN];

		*abort_now = 1;
		/* XXX */
		snprintf(msg, sizeof(msg), "Cum ack %8.8x greater or equal than TSN %8.8x",
		    cumack, send_s);
		op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
		stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_21;
		sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
		return;
	}
	asoc->this_sack_highest_gap = cumack;
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_THRESHOLD_LOGGING) {
		sctp_misc_ints(SCTP_THRESHOLD_CLEAR,
		    stcb->asoc.overall_error_count,
		    0,
		    SCTP_FROM_SCTP_INDATA,
		    __LINE__);
	}
	stcb->asoc.overall_error_count = 0;
	if (SCTP_TSN_GT(cumack, asoc->last_acked_seq)) {
		/* process the new consecutive TSN first */
		TAILQ_FOREACH_SAFE(tp1, &asoc->sent_queue, sctp_next, tp2) {
			if (SCTP_TSN_GE(cumack, tp1->rec.data.tsn)) {
				if (tp1->sent == SCTP_DATAGRAM_UNSENT) {
					SCTP_PRINTF("Warning, an unsent is now acked?\n");
				}
				if (tp1->sent < SCTP_DATAGRAM_ACKED) {
					/*
					 * If it is less than ACKED, it is
					 * now no-longer in flight. Higher
					 * values may occur during marking
					 */
					if (tp1->sent < SCTP_DATAGRAM_RESEND) {
						if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FLIGHT_LOGGING_ENABLE) {
							sctp_misc_ints(SCTP_FLIGHT_LOG_DOWN_CA,
							    tp1->whoTo->flight_size,
							    tp1->book_size,
							    (uint32_t)(uintptr_t)tp1->whoTo,
							    tp1->rec.data.tsn);
						}
						sctp_flight_size_decrease(tp1);
						if (stcb->asoc.cc_functions.sctp_cwnd_update_tsn_acknowledged) {
							(*stcb->asoc.cc_functions.sctp_cwnd_update_tsn_acknowledged) (tp1->whoTo,
							    tp1);
						}
						/* sa_ignore NO_NULL_CHK */
						sctp_total_flight_decrease(stcb, tp1);
					}
					tp1->whoTo->net_ack += tp1->send_size;
					if (tp1->snd_count < 2) {
						/*
						 * True non-retransmitted
						 * chunk
						 */
						tp1->whoTo->net_ack2 +=
						    tp1->send_size;

						/* update RTO too? */
						if (tp1->do_rtt) {
							if (rto_ok) {
								tp1->whoTo->RTO =
								/*
								 * sa_ignore
								 * NO_NULL_CHK
								 */
								    sctp_calculate_rto(stcb,
								    asoc, tp1->whoTo,
								    &tp1->sent_rcv_time,
								    SCTP_RTT_FROM_DATA);
								rto_ok = 0;
							}
							if (tp1->whoTo->rto_needed == 0) {
								tp1->whoTo->rto_needed = 1;
							}
							tp1->do_rtt = 0;
						}
					}
					/*
					 * CMT: CUCv2 algorithm. From the
					 * cumack'd TSNs, for each TSN being
					 * acked for the first time, set the
					 * following variables for the
					 * corresp destination.
					 * new_pseudo_cumack will trigger a
					 * cwnd update.
					 * find_(rtx_)pseudo_cumack will
					 * trigger search for the next
					 * expected (rtx-)pseudo-cumack.
					 */
					tp1->whoTo->new_pseudo_cumack = 1;
					tp1->whoTo->find_pseudo_cumack = 1;
					tp1->whoTo->find_rtx_pseudo_cumack = 1;

					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
						/* sa_ignore NO_NULL_CHK */
						sctp_log_cwnd(stcb, tp1->whoTo, tp1->rec.data.tsn, SCTP_CWND_LOG_FROM_SACK);
					}
				}
				if (tp1->sent == SCTP_DATAGRAM_RESEND) {
					sctp_ucount_decr(asoc->sent_queue_retran_cnt);
				}
				if (tp1->rec.data.chunk_was_revoked) {
					/* deflate the cwnd */
					tp1->whoTo->cwnd -= tp1->book_size;
					tp1->rec.data.chunk_was_revoked = 0;
				}
				if (tp1->sent != SCTP_DATAGRAM_NR_ACKED) {
					if (asoc->strmout[tp1->rec.data.sid].chunks_on_queues > 0) {
						asoc->strmout[tp1->rec.data.sid].chunks_on_queues--;
#ifdef INVARIANTS
					} else {
						panic("No chunks on the queues for sid %u.", tp1->rec.data.sid);
#endif
					}
				}
				if ((asoc->strmout[tp1->rec.data.sid].chunks_on_queues == 0) &&
				    (asoc->strmout[tp1->rec.data.sid].state == SCTP_STREAM_RESET_PENDING) &&
				    TAILQ_EMPTY(&asoc->strmout[tp1->rec.data.sid].outqueue)) {
					asoc->trigger_reset = 1;
				}
				TAILQ_REMOVE(&asoc->sent_queue, tp1, sctp_next);
				if (tp1->data) {
					/* sa_ignore NO_NULL_CHK */
					sctp_free_bufspace(stcb, asoc, tp1, 1);
					sctp_m_freem(tp1->data);
					tp1->data = NULL;
				}
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SACK_LOGGING_ENABLE) {
					sctp_log_sack(asoc->last_acked_seq,
					    cumack,
					    tp1->rec.data.tsn,
					    0,
					    0,
					    SCTP_LOG_FREE_SENT);
				}
				asoc->sent_queue_cnt--;
				sctp_free_a_chunk(stcb, tp1, SCTP_SO_NOT_LOCKED);
			} else {
				break;
			}
		}

	}
	/* sa_ignore NO_NULL_CHK */
	if (stcb->sctp_socket) {
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		struct socket *so;

#endif
		SOCKBUF_LOCK(&stcb->sctp_socket->so_snd);
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_WAKE_LOGGING_ENABLE) {
			/* sa_ignore NO_NULL_CHK */
			sctp_wakeup_log(stcb, 1, SCTP_WAKESND_FROM_SACK);
		}
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
		sctp_sowwakeup_locked(stcb->sctp_ep, stcb->sctp_socket);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
	} else {
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_WAKE_LOGGING_ENABLE) {
			sctp_wakeup_log(stcb, 1, SCTP_NOWAKE_FROM_SACK);
		}
	}

	/* JRS - Use the congestion control given in the CC module */
	if ((asoc->last_acked_seq != cumack) && (ecne_seen == 0)) {
		TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
			if (net->net_ack2 > 0) {
				/*
				 * Karn's rule applies to clearing error
				 * count, this is optional.
				 */
				net->error_count = 0;
				if (!(net->dest_state & SCTP_ADDR_REACHABLE)) {
					/* addr came good */
					net->dest_state |= SCTP_ADDR_REACHABLE;
					sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_UP, stcb,
					    0, (void *)net, SCTP_SO_NOT_LOCKED);
				}
				if (net == stcb->asoc.primary_destination) {
					if (stcb->asoc.alternate) {
						/*
						 * release the alternate,
						 * primary is good
						 */
						sctp_free_remote_addr(stcb->asoc.alternate);
						stcb->asoc.alternate = NULL;
					}
				}
				if (net->dest_state & SCTP_ADDR_PF) {
					net->dest_state &= ~SCTP_ADDR_PF;
					sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT,
					    stcb->sctp_ep, stcb, net,
					    SCTP_FROM_SCTP_INDATA + SCTP_LOC_22);
					sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, net);
					asoc->cc_functions.sctp_cwnd_update_exit_pf(stcb, net);
					/* Done with this net */
					net->net_ack = 0;
				}
				/* restore any doubled timers */
				net->RTO = (net->lastsa >> SCTP_RTT_SHIFT) + net->lastsv;
				if (net->RTO < stcb->asoc.minrto) {
					net->RTO = stcb->asoc.minrto;
				}
				if (net->RTO > stcb->asoc.maxrto) {
					net->RTO = stcb->asoc.maxrto;
				}
			}
		}
		asoc->cc_functions.sctp_cwnd_update_after_sack(stcb, asoc, 1, 0, 0);
	}
	asoc->last_acked_seq = cumack;

	if (TAILQ_EMPTY(&asoc->sent_queue)) {
		/* nothing left in-flight */
		TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
			net->flight_size = 0;
			net->partial_bytes_acked = 0;
		}
		asoc->total_flight = 0;
		asoc->total_flight_count = 0;
	}

	/* RWND update */
	asoc->peers_rwnd = sctp_sbspace_sub(rwnd,
	    (uint32_t)(asoc->total_flight + (asoc->total_flight_count * SCTP_BASE_SYSCTL(sctp_peer_chunk_oh))));
	if (asoc->peers_rwnd < stcb->sctp_ep->sctp_ep.sctp_sws_sender) {
		/* SWS sender side engages */
		asoc->peers_rwnd = 0;
	}
	if (asoc->peers_rwnd > old_rwnd) {
		win_probe_recovery = 1;
	}
	/* Now assure a timer where data is queued at */
again:
	j = 0;
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		if (win_probe_recovery && (net->window_probe)) {
			win_probe_recovered = 1;
			/*
			 * Find first chunk that was used with window probe
			 * and clear the sent
			 */
			/* sa_ignore FREED_MEMORY */
			TAILQ_FOREACH(tp1, &asoc->sent_queue, sctp_next) {
				if (tp1->window_probe) {
					/* move back to data send queue */
					sctp_window_probe_recovery(stcb, asoc, tp1);
					break;
				}
			}
		}
		if (net->flight_size) {
			j++;
			sctp_timer_start(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep, stcb, net);
			if (net->window_probe) {
				net->window_probe = 0;
			}
		} else {
			if (net->window_probe) {
				/*
				 * In window probes we must assure a timer
				 * is still running there
				 */
				net->window_probe = 0;
				if (!SCTP_OS_TIMER_PENDING(&net->rxt_timer.timer)) {
					sctp_timer_start(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep, stcb, net);
				}
			} else if (SCTP_OS_TIMER_PENDING(&net->rxt_timer.timer)) {
				sctp_timer_stop(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep,
				    stcb, net,
				    SCTP_FROM_SCTP_INDATA + SCTP_LOC_23);
			}
		}
	}
	if ((j == 0) &&
	    (!TAILQ_EMPTY(&asoc->sent_queue)) &&
	    (asoc->sent_queue_retran_cnt == 0) &&
	    (win_probe_recovered == 0) &&
	    (done_once == 0)) {
		/*
		 * huh, this should not happen unless all packets are
		 * PR-SCTP and marked to skip of course.
		 */
		if (sctp_fs_audit(asoc)) {
			TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
				net->flight_size = 0;
			}
			asoc->total_flight = 0;
			asoc->total_flight_count = 0;
			asoc->sent_queue_retran_cnt = 0;
			TAILQ_FOREACH(tp1, &asoc->sent_queue, sctp_next) {
				if (tp1->sent < SCTP_DATAGRAM_RESEND) {
					sctp_flight_size_increase(tp1);
					sctp_total_flight_increase(stcb, tp1);
				} else if (tp1->sent == SCTP_DATAGRAM_RESEND) {
					sctp_ucount_incr(asoc->sent_queue_retran_cnt);
				}
			}
		}
		done_once = 1;
		goto again;
	}
	/**********************************/
	/* Now what about shutdown issues */
	/**********************************/
	if (TAILQ_EMPTY(&asoc->send_queue) && TAILQ_EMPTY(&asoc->sent_queue)) {
		/* nothing left on sendqueue.. consider done */
		/* clean up */
		if ((asoc->stream_queue_cnt == 1) &&
		    ((asoc->state & SCTP_STATE_SHUTDOWN_PENDING) ||
		    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED)) &&
		    ((*asoc->ss_functions.sctp_ss_is_user_msgs_incomplete) (stcb, asoc))) {
			SCTP_ADD_SUBSTATE(stcb, SCTP_STATE_PARTIAL_MSG_LEFT);
		}
		if (((asoc->state & SCTP_STATE_SHUTDOWN_PENDING) ||
		    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED)) &&
		    (asoc->stream_queue_cnt == 1) &&
		    (asoc->state & SCTP_STATE_PARTIAL_MSG_LEFT)) {
			struct mbuf *op_err;

			*abort_now = 1;
			/* XXX */
			op_err = sctp_generate_cause(SCTP_CAUSE_USER_INITIATED_ABT, "");
			stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_24;
			sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
			return;
		}
		if ((asoc->state & SCTP_STATE_SHUTDOWN_PENDING) &&
		    (asoc->stream_queue_cnt == 0)) {
			struct sctp_nets *netp;

			if ((SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) ||
			    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
				SCTP_STAT_DECR_GAUGE32(sctps_currestab);
			}
			SCTP_SET_STATE(stcb, SCTP_STATE_SHUTDOWN_SENT);
			sctp_stop_timers_for_shutdown(stcb);
			if (asoc->alternate) {
				netp = asoc->alternate;
			} else {
				netp = asoc->primary_destination;
			}
			sctp_send_shutdown(stcb, netp);
			sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWN,
			    stcb->sctp_ep, stcb, netp);
			sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD,
			    stcb->sctp_ep, stcb, netp);
		} else if ((SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED) &&
		    (asoc->stream_queue_cnt == 0)) {
			struct sctp_nets *netp;

			SCTP_STAT_DECR_GAUGE32(sctps_currestab);
			SCTP_SET_STATE(stcb, SCTP_STATE_SHUTDOWN_ACK_SENT);
			sctp_stop_timers_for_shutdown(stcb);
			if (asoc->alternate) {
				netp = asoc->alternate;
			} else {
				netp = asoc->primary_destination;
			}
			sctp_send_shutdown_ack(stcb, netp);
			sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNACK,
			    stcb->sctp_ep, stcb, netp);
		}
	}
	/*********************************************/
	/* Here we perform PR-SCTP procedures        */
	/* (section 4.2)                             */
	/*********************************************/
	/* C1. update advancedPeerAckPoint */
	if (SCTP_TSN_GT(cumack, asoc->advanced_peer_ack_point)) {
		asoc->advanced_peer_ack_point = cumack;
	}
	/* PR-Sctp issues need to be addressed too */
	if ((asoc->prsctp_supported) && (asoc->pr_sctp_cnt > 0)) {
		struct sctp_tmit_chunk *lchk;
		uint32_t old_adv_peer_ack_point;

		old_adv_peer_ack_point = asoc->advanced_peer_ack_point;
		lchk = sctp_try_advance_peer_ack_point(stcb, asoc);
		/* C3. See if we need to send a Fwd-TSN */
		if (SCTP_TSN_GT(asoc->advanced_peer_ack_point, cumack)) {
			/*
			 * ISSUE with ECN, see FWD-TSN processing.
			 */
			if (SCTP_TSN_GT(asoc->advanced_peer_ack_point, old_adv_peer_ack_point)) {
				send_forward_tsn(stcb, asoc);
			} else if (lchk) {
				/* try to FR fwd-tsn's that get lost too */
				if (lchk->rec.data.fwd_tsn_cnt >= 3) {
					send_forward_tsn(stcb, asoc);
				}
			}
		}
		if (lchk) {
			/* Assure a timer is up */
			sctp_timer_start(SCTP_TIMER_TYPE_SEND,
			    stcb->sctp_ep, stcb, lchk->whoTo);
		}
	}
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SACK_RWND_LOGGING_ENABLE) {
		sctp_misc_ints(SCTP_SACK_RWND_UPDATE,
		    rwnd,
		    stcb->asoc.peers_rwnd,
		    stcb->asoc.total_flight,
		    stcb->asoc.total_output_queue_size);
	}
}

void
sctp_handle_sack(struct mbuf *m, int offset_seg, int offset_dup,
    struct sctp_tcb *stcb,
    uint16_t num_seg, uint16_t num_nr_seg, uint16_t num_dup,
    int *abort_now, uint8_t flags,
    uint32_t cum_ack, uint32_t rwnd, int ecne_seen)
{
	struct sctp_association *asoc;
	struct sctp_tmit_chunk *tp1, *tp2;
	uint32_t last_tsn, biggest_tsn_acked, biggest_tsn_newly_acked, this_sack_lowest_newack;
	uint16_t wake_him = 0;
	uint32_t send_s = 0;
	long j;
	int accum_moved = 0;
	int will_exit_fast_recovery = 0;
	uint32_t a_rwnd, old_rwnd;
	int win_probe_recovery = 0;
	int win_probe_recovered = 0;
	struct sctp_nets *net = NULL;
	int done_once;
	int rto_ok = 1;
	uint8_t reneged_all = 0;
	uint8_t cmt_dac_flag;

	/*
	 * we take any chance we can to service our queues since we cannot
	 * get awoken when the socket is read from :<
	 */
	/*
	 * Now perform the actual SACK handling: 1) Verify that it is not an
	 * old sack, if so discard. 2) If there is nothing left in the send
	 * queue (cum-ack is equal to last acked) then you have a duplicate
	 * too, update any rwnd change and verify no timers are running.
	 * then return. 3) Process any new consequtive data i.e. cum-ack
	 * moved process these first and note that it moved. 4) Process any
	 * sack blocks. 5) Drop any acked from the queue. 6) Check for any
	 * revoked blocks and mark. 7) Update the cwnd. 8) Nothing left,
	 * sync up flightsizes and things, stop all timers and also check
	 * for shutdown_pending state. If so then go ahead and send off the
	 * shutdown. If in shutdown recv, send off the shutdown-ack and
	 * start that timer, Ret. 9) Strike any non-acked things and do FR
	 * procedure if needed being sure to set the FR flag. 10) Do pr-sctp
	 * procedures. 11) Apply any FR penalties. 12) Assure we will SACK
	 * if in shutdown_recv state.
	 */
	SCTP_TCB_LOCK_ASSERT(stcb);
	/* CMT DAC algo */
	this_sack_lowest_newack = 0;
	SCTP_STAT_INCR(sctps_slowpath_sack);
	last_tsn = cum_ack;
	cmt_dac_flag = flags & SCTP_SACK_CMT_DAC;
#ifdef SCTP_ASOCLOG_OF_TSNS
	stcb->asoc.cumack_log[stcb->asoc.cumack_log_at] = cum_ack;
	stcb->asoc.cumack_log_at++;
	if (stcb->asoc.cumack_log_at > SCTP_TSN_LOG_SIZE) {
		stcb->asoc.cumack_log_at = 0;
	}
#endif
	a_rwnd = rwnd;

	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_SACK_ARRIVALS_ENABLE) {
		sctp_misc_ints(SCTP_SACK_LOG_NORMAL, cum_ack,
		    rwnd, stcb->asoc.last_acked_seq, stcb->asoc.peers_rwnd);
	}

	old_rwnd = stcb->asoc.peers_rwnd;
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_THRESHOLD_LOGGING) {
		sctp_misc_ints(SCTP_THRESHOLD_CLEAR,
		    stcb->asoc.overall_error_count,
		    0,
		    SCTP_FROM_SCTP_INDATA,
		    __LINE__);
	}
	stcb->asoc.overall_error_count = 0;
	asoc = &stcb->asoc;
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SACK_LOGGING_ENABLE) {
		sctp_log_sack(asoc->last_acked_seq,
		    cum_ack,
		    0,
		    num_seg,
		    num_dup,
		    SCTP_LOG_NEW_SACK);
	}
	if ((num_dup) && (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FR_LOGGING_ENABLE)) {
		uint16_t i;
		uint32_t *dupdata, dblock;

		for (i = 0; i < num_dup; i++) {
			dupdata = (uint32_t *)sctp_m_getptr(m, offset_dup + i * sizeof(uint32_t),
			    sizeof(uint32_t), (uint8_t *)&dblock);
			if (dupdata == NULL) {
				break;
			}
			sctp_log_fr(*dupdata, 0, 0, SCTP_FR_DUPED);
		}
	}
	/* reality check */
	if (!TAILQ_EMPTY(&asoc->sent_queue)) {
		tp1 = TAILQ_LAST(&asoc->sent_queue,
		    sctpchunk_listhead);
		send_s = tp1->rec.data.tsn + 1;
	} else {
		tp1 = NULL;
		send_s = asoc->sending_seq;
	}
	if (SCTP_TSN_GE(cum_ack, send_s)) {
		struct mbuf *op_err;
		char msg[SCTP_DIAG_INFO_LEN];

		/*
		 * no way, we have not even sent this TSN out yet. Peer is
		 * hopelessly messed up with us.
		 */
		SCTP_PRINTF("NEW cum_ack:%x send_s:%x is smaller or equal\n",
		    cum_ack, send_s);
		if (tp1) {
			SCTP_PRINTF("Got send_s from tsn:%x + 1 of tp1: %p\n",
			    tp1->rec.data.tsn, (void *)tp1);
		}
hopeless_peer:
		*abort_now = 1;
		/* XXX */
		snprintf(msg, sizeof(msg), "Cum ack %8.8x greater or equal than TSN %8.8x",
		    cum_ack, send_s);
		op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
		stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_25;
		sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
		return;
	}
	/**********************/
	/* 1) check the range */
	/**********************/
	if (SCTP_TSN_GT(asoc->last_acked_seq, last_tsn)) {
		/* acking something behind */
		return;
	}

	/* update the Rwnd of the peer */
	if (TAILQ_EMPTY(&asoc->sent_queue) &&
	    TAILQ_EMPTY(&asoc->send_queue) &&
	    (asoc->stream_queue_cnt == 0)) {
		/* nothing left on send/sent and strmq */
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_RWND_ENABLE) {
			sctp_log_rwnd_set(SCTP_SET_PEER_RWND_VIA_SACK,
			    asoc->peers_rwnd, 0, 0, a_rwnd);
		}
		asoc->peers_rwnd = a_rwnd;
		if (asoc->sent_queue_retran_cnt) {
			asoc->sent_queue_retran_cnt = 0;
		}
		if (asoc->peers_rwnd < stcb->sctp_ep->sctp_ep.sctp_sws_sender) {
			/* SWS sender side engages */
			asoc->peers_rwnd = 0;
		}
		/* stop any timers */
		TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
			sctp_timer_stop(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep,
			    stcb, net, SCTP_FROM_SCTP_INDATA + SCTP_LOC_26);
			net->partial_bytes_acked = 0;
			net->flight_size = 0;
		}
		asoc->total_flight = 0;
		asoc->total_flight_count = 0;
		return;
	}
	/*
	 * We init netAckSz and netAckSz2 to 0. These are used to track 2
	 * things. The total byte count acked is tracked in netAckSz AND
	 * netAck2 is used to track the total bytes acked that are un-
	 * amibguious and were never retransmitted. We track these on a per
	 * destination address basis.
	 */
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		if (SCTP_TSN_GT(cum_ack, net->cwr_window_tsn)) {
			/* Drag along the window_tsn for cwr's */
			net->cwr_window_tsn = cum_ack;
		}
		net->prev_cwnd = net->cwnd;
		net->net_ack = 0;
		net->net_ack2 = 0;

		/*
		 * CMT: Reset CUC and Fast recovery algo variables before
		 * SACK processing
		 */
		net->new_pseudo_cumack = 0;
		net->will_exit_fast_recovery = 0;
		if (stcb->asoc.cc_functions.sctp_cwnd_prepare_net_for_sack) {
			(*stcb->asoc.cc_functions.sctp_cwnd_prepare_net_for_sack) (stcb, net);
		}

		/*
		 * CMT: SFR algo (and HTNA) - this_sack_highest_newack has
		 * to be greater than the cumack. Also reset saw_newack to 0
		 * for all dests.
		 */
		net->saw_newack = 0;
		net->this_sack_highest_newack = last_tsn;
	}
	/* process the new consecutive TSN first */
	TAILQ_FOREACH(tp1, &asoc->sent_queue, sctp_next) {
		if (SCTP_TSN_GE(last_tsn, tp1->rec.data.tsn)) {
			if (tp1->sent != SCTP_DATAGRAM_UNSENT) {
				accum_moved = 1;
				if (tp1->sent < SCTP_DATAGRAM_ACKED) {
					/*
					 * If it is less than ACKED, it is
					 * now no-longer in flight. Higher
					 * values may occur during marking
					 */
					if ((tp1->whoTo->dest_state &
					    SCTP_ADDR_UNCONFIRMED) &&
					    (tp1->snd_count < 2)) {
						/*
						 * If there was no retran
						 * and the address is
						 * un-confirmed and we sent
						 * there and are now
						 * sacked.. its confirmed,
						 * mark it so.
						 */
						tp1->whoTo->dest_state &=
						    ~SCTP_ADDR_UNCONFIRMED;
					}
					if (tp1->sent < SCTP_DATAGRAM_RESEND) {
						if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FLIGHT_LOGGING_ENABLE) {
							sctp_misc_ints(SCTP_FLIGHT_LOG_DOWN_CA,
							    tp1->whoTo->flight_size,
							    tp1->book_size,
							    (uint32_t)(uintptr_t)tp1->whoTo,
							    tp1->rec.data.tsn);
						}
						sctp_flight_size_decrease(tp1);
						sctp_total_flight_decrease(stcb, tp1);
						if (stcb->asoc.cc_functions.sctp_cwnd_update_tsn_acknowledged) {
							(*stcb->asoc.cc_functions.sctp_cwnd_update_tsn_acknowledged) (tp1->whoTo,
							    tp1);
						}
					}
					tp1->whoTo->net_ack += tp1->send_size;

					/* CMT SFR and DAC algos */
					this_sack_lowest_newack = tp1->rec.data.tsn;
					tp1->whoTo->saw_newack = 1;

					if (tp1->snd_count < 2) {
						/*
						 * True non-retransmitted
						 * chunk
						 */
						tp1->whoTo->net_ack2 +=
						    tp1->send_size;

						/* update RTO too? */
						if (tp1->do_rtt) {
							if (rto_ok) {
								tp1->whoTo->RTO =
								    sctp_calculate_rto(stcb,
								    asoc, tp1->whoTo,
								    &tp1->sent_rcv_time,
								    SCTP_RTT_FROM_DATA);
								rto_ok = 0;
							}
							if (tp1->whoTo->rto_needed == 0) {
								tp1->whoTo->rto_needed = 1;
							}
							tp1->do_rtt = 0;
						}
					}
					/*
					 * CMT: CUCv2 algorithm. From the
					 * cumack'd TSNs, for each TSN being
					 * acked for the first time, set the
					 * following variables for the
					 * corresp destination.
					 * new_pseudo_cumack will trigger a
					 * cwnd update.
					 * find_(rtx_)pseudo_cumack will
					 * trigger search for the next
					 * expected (rtx-)pseudo-cumack.
					 */
					tp1->whoTo->new_pseudo_cumack = 1;
					tp1->whoTo->find_pseudo_cumack = 1;
					tp1->whoTo->find_rtx_pseudo_cumack = 1;


					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SACK_LOGGING_ENABLE) {
						sctp_log_sack(asoc->last_acked_seq,
						    cum_ack,
						    tp1->rec.data.tsn,
						    0,
						    0,
						    SCTP_LOG_TSN_ACKED);
					}
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
						sctp_log_cwnd(stcb, tp1->whoTo, tp1->rec.data.tsn, SCTP_CWND_LOG_FROM_SACK);
					}
				}
				if (tp1->sent == SCTP_DATAGRAM_RESEND) {
					sctp_ucount_decr(asoc->sent_queue_retran_cnt);
#ifdef SCTP_AUDITING_ENABLED
					sctp_audit_log(0xB3,
					    (asoc->sent_queue_retran_cnt & 0x000000ff));
#endif
				}
				if (tp1->rec.data.chunk_was_revoked) {
					/* deflate the cwnd */
					tp1->whoTo->cwnd -= tp1->book_size;
					tp1->rec.data.chunk_was_revoked = 0;
				}
				if (tp1->sent != SCTP_DATAGRAM_NR_ACKED) {
					tp1->sent = SCTP_DATAGRAM_ACKED;
				}
			}
		} else {
			break;
		}
	}
	biggest_tsn_newly_acked = biggest_tsn_acked = last_tsn;
	/* always set this up to cum-ack */
	asoc->this_sack_highest_gap = last_tsn;

	if ((num_seg > 0) || (num_nr_seg > 0)) {

		/*
		 * thisSackHighestGap will increase while handling NEW
		 * segments this_sack_highest_newack will increase while
		 * handling NEWLY ACKED chunks. this_sack_lowest_newack is
		 * used for CMT DAC algo. saw_newack will also change.
		 */
		if (sctp_handle_segments(m, &offset_seg, stcb, asoc, last_tsn, &biggest_tsn_acked,
		    &biggest_tsn_newly_acked, &this_sack_lowest_newack,
		    num_seg, num_nr_seg, &rto_ok)) {
			wake_him++;
		}
		/*
		 * validate the biggest_tsn_acked in the gap acks if strict
		 * adherence is wanted.
		 */
		if (SCTP_TSN_GE(biggest_tsn_acked, send_s)) {
			/*
			 * peer is either confused or we are under attack.
			 * We must abort.
			 */
			SCTP_PRINTF("Hopeless peer! biggest_tsn_acked:%x largest seq:%x\n",
			    biggest_tsn_acked, send_s);
			goto hopeless_peer;
		}
	}
	/*******************************************/
	/* cancel ALL T3-send timer if accum moved */
	/*******************************************/
	if (asoc->sctp_cmt_on_off > 0) {
		TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
			if (net->new_pseudo_cumack)
				sctp_timer_stop(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep,
				    stcb, net,
				    SCTP_FROM_SCTP_INDATA + SCTP_LOC_27);

		}
	} else {
		if (accum_moved) {
			TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
				sctp_timer_stop(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep,
				    stcb, net, SCTP_FROM_SCTP_INDATA + SCTP_LOC_28);
			}
		}
	}
	/********************************************/
	/* drop the acked chunks from the sentqueue */
	/********************************************/
	asoc->last_acked_seq = cum_ack;

	TAILQ_FOREACH_SAFE(tp1, &asoc->sent_queue, sctp_next, tp2) {
		if (SCTP_TSN_GT(tp1->rec.data.tsn, cum_ack)) {
			break;
		}
		if (tp1->sent != SCTP_DATAGRAM_NR_ACKED) {
			if (asoc->strmout[tp1->rec.data.sid].chunks_on_queues > 0) {
				asoc->strmout[tp1->rec.data.sid].chunks_on_queues--;
#ifdef INVARIANTS
			} else {
				panic("No chunks on the queues for sid %u.", tp1->rec.data.sid);
#endif
			}
		}
		if ((asoc->strmout[tp1->rec.data.sid].chunks_on_queues == 0) &&
		    (asoc->strmout[tp1->rec.data.sid].state == SCTP_STREAM_RESET_PENDING) &&
		    TAILQ_EMPTY(&asoc->strmout[tp1->rec.data.sid].outqueue)) {
			asoc->trigger_reset = 1;
		}
		TAILQ_REMOVE(&asoc->sent_queue, tp1, sctp_next);
		if (PR_SCTP_ENABLED(tp1->flags)) {
			if (asoc->pr_sctp_cnt != 0)
				asoc->pr_sctp_cnt--;
		}
		asoc->sent_queue_cnt--;
		if (tp1->data) {
			/* sa_ignore NO_NULL_CHK */
			sctp_free_bufspace(stcb, asoc, tp1, 1);
			sctp_m_freem(tp1->data);
			tp1->data = NULL;
			if (asoc->prsctp_supported && PR_SCTP_BUF_ENABLED(tp1->flags)) {
				asoc->sent_queue_cnt_removeable--;
			}
		}
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SACK_LOGGING_ENABLE) {
			sctp_log_sack(asoc->last_acked_seq,
			    cum_ack,
			    tp1->rec.data.tsn,
			    0,
			    0,
			    SCTP_LOG_FREE_SENT);
		}
		sctp_free_a_chunk(stcb, tp1, SCTP_SO_NOT_LOCKED);
		wake_him++;
	}
	if (TAILQ_EMPTY(&asoc->sent_queue) && (asoc->total_flight > 0)) {
#ifdef INVARIANTS
		panic("Warning flight size is positive and should be 0");
#else
		SCTP_PRINTF("Warning flight size incorrect should be 0 is %d\n",
		    asoc->total_flight);
#endif
		asoc->total_flight = 0;
	}

	/* sa_ignore NO_NULL_CHK */
	if ((wake_him) && (stcb->sctp_socket)) {
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		struct socket *so;

#endif
		SOCKBUF_LOCK(&stcb->sctp_socket->so_snd);
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_WAKE_LOGGING_ENABLE) {
			sctp_wakeup_log(stcb, wake_him, SCTP_WAKESND_FROM_SACK);
		}
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
		sctp_sowwakeup_locked(stcb->sctp_ep, stcb->sctp_socket);
#if defined(__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		SCTP_SOCKET_UNLOCK(so, 1);
#endif
	} else {
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_WAKE_LOGGING_ENABLE) {
			sctp_wakeup_log(stcb, wake_him, SCTP_NOWAKE_FROM_SACK);
		}
	}

	if (asoc->fast_retran_loss_recovery && accum_moved) {
		if (SCTP_TSN_GE(asoc->last_acked_seq, asoc->fast_recovery_tsn)) {
			/* Setup so we will exit RFC2582 fast recovery */
			will_exit_fast_recovery = 1;
		}
	}
	/*
	 * Check for revoked fragments:
	 *
	 * if Previous sack - Had no frags then we can't have any revoked if
	 * Previous sack - Had frag's then - If we now have frags aka
	 * num_seg > 0 call sctp_check_for_revoked() to tell if peer revoked
	 * some of them. else - The peer revoked all ACKED fragments, since
	 * we had some before and now we have NONE.
	 */

	if (num_seg) {
		sctp_check_for_revoked(stcb, asoc, cum_ack, biggest_tsn_acked);
		asoc->saw_sack_with_frags = 1;
	} else if (asoc->saw_sack_with_frags) {
		int cnt_revoked = 0;

		/* Peer revoked all dg's marked or acked */
		TAILQ_FOREACH(tp1, &asoc->sent_queue, sctp_next) {
			if (tp1->sent == SCTP_DATAGRAM_ACKED) {
				tp1->sent = SCTP_DATAGRAM_SENT;
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FLIGHT_LOGGING_ENABLE) {
					sctp_misc_ints(SCTP_FLIGHT_LOG_UP_REVOKE,
					    tp1->whoTo->flight_size,
					    tp1->book_size,
					    (uint32_t)(uintptr_t)tp1->whoTo,
					    tp1->rec.data.tsn);
				}
				sctp_flight_size_increase(tp1);
				sctp_total_flight_increase(stcb, tp1);
				tp1->rec.data.chunk_was_revoked = 1;
				/*
				 * To ensure that this increase in
				 * flightsize, which is artificial, does not
				 * throttle the sender, we also increase the
				 * cwnd artificially.
				 */
				tp1->whoTo->cwnd += tp1->book_size;
				cnt_revoked++;
			}
		}
		if (cnt_revoked) {
			reneged_all = 1;
		}
		asoc->saw_sack_with_frags = 0;
	}
	if (num_nr_seg > 0)
		asoc->saw_sack_with_nr_frags = 1;
	else
		asoc->saw_sack_with_nr_frags = 0;

	/* JRS - Use the congestion control given in the CC module */
	if (ecne_seen == 0) {
		TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
			if (net->net_ack2 > 0) {
				/*
				 * Karn's rule applies to clearing error
				 * count, this is optional.
				 */
				net->error_count = 0;
				if (!(net->dest_state & SCTP_ADDR_REACHABLE)) {
					/* addr came good */
					net->dest_state |= SCTP_ADDR_REACHABLE;
					sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_UP, stcb,
					    0, (void *)net, SCTP_SO_NOT_LOCKED);
				}

				if (net == stcb->asoc.primary_destination) {
					if (stcb->asoc.alternate) {
						/*
						 * release the alternate,
						 * primary is good
						 */
						sctp_free_remote_addr(stcb->asoc.alternate);
						stcb->asoc.alternate = NULL;
					}
				}

				if (net->dest_state & SCTP_ADDR_PF) {
					net->dest_state &= ~SCTP_ADDR_PF;
					sctp_timer_stop(SCTP_TIMER_TYPE_HEARTBEAT,
					    stcb->sctp_ep, stcb, net,
					    SCTP_FROM_SCTP_INDATA + SCTP_LOC_29);
					sctp_timer_start(SCTP_TIMER_TYPE_HEARTBEAT, stcb->sctp_ep, stcb, net);
					asoc->cc_functions.sctp_cwnd_update_exit_pf(stcb, net);
					/* Done with this net */
					net->net_ack = 0;
				}
				/* restore any doubled timers */
				net->RTO = (net->lastsa >> SCTP_RTT_SHIFT) + net->lastsv;
				if (net->RTO < stcb->asoc.minrto) {
					net->RTO = stcb->asoc.minrto;
				}
				if (net->RTO > stcb->asoc.maxrto) {
					net->RTO = stcb->asoc.maxrto;
				}
			}
		}
		asoc->cc_functions.sctp_cwnd_update_after_sack(stcb, asoc, accum_moved, reneged_all, will_exit_fast_recovery);
	}

	if (TAILQ_EMPTY(&asoc->sent_queue)) {
		/* nothing left in-flight */
		TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
			/* stop all timers */
			sctp_timer_stop(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep,
			    stcb, net,
			    SCTP_FROM_SCTP_INDATA + SCTP_LOC_30);
			net->flight_size = 0;
			net->partial_bytes_acked = 0;
		}
		asoc->total_flight = 0;
		asoc->total_flight_count = 0;
	}

	/**********************************/
	/* Now what about shutdown issues */
	/**********************************/
	if (TAILQ_EMPTY(&asoc->send_queue) && TAILQ_EMPTY(&asoc->sent_queue)) {
		/* nothing left on sendqueue.. consider done */
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_RWND_ENABLE) {
			sctp_log_rwnd_set(SCTP_SET_PEER_RWND_VIA_SACK,
			    asoc->peers_rwnd, 0, 0, a_rwnd);
		}
		asoc->peers_rwnd = a_rwnd;
		if (asoc->peers_rwnd < stcb->sctp_ep->sctp_ep.sctp_sws_sender) {
			/* SWS sender side engages */
			asoc->peers_rwnd = 0;
		}
		/* clean up */
		if ((asoc->stream_queue_cnt == 1) &&
		    ((asoc->state & SCTP_STATE_SHUTDOWN_PENDING) ||
		    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED)) &&
		    ((*asoc->ss_functions.sctp_ss_is_user_msgs_incomplete) (stcb, asoc))) {
			SCTP_ADD_SUBSTATE(stcb, SCTP_STATE_PARTIAL_MSG_LEFT);
		}
		if (((asoc->state & SCTP_STATE_SHUTDOWN_PENDING) ||
		    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED)) &&
		    (asoc->stream_queue_cnt == 1) &&
		    (asoc->state & SCTP_STATE_PARTIAL_MSG_LEFT)) {
			struct mbuf *op_err;

			*abort_now = 1;
			/* XXX */
			op_err = sctp_generate_cause(SCTP_CAUSE_USER_INITIATED_ABT, "");
			stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_24;
			sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
			return;
		}
		if ((asoc->state & SCTP_STATE_SHUTDOWN_PENDING) &&
		    (asoc->stream_queue_cnt == 0)) {
			struct sctp_nets *netp;

			if ((SCTP_GET_STATE(stcb) == SCTP_STATE_OPEN) ||
			    (SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED)) {
				SCTP_STAT_DECR_GAUGE32(sctps_currestab);
			}
			SCTP_SET_STATE(stcb, SCTP_STATE_SHUTDOWN_SENT);
			sctp_stop_timers_for_shutdown(stcb);
			if (asoc->alternate) {
				netp = asoc->alternate;
			} else {
				netp = asoc->primary_destination;
			}
			sctp_send_shutdown(stcb, netp);
			sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWN,
			    stcb->sctp_ep, stcb, netp);
			sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD,
			    stcb->sctp_ep, stcb, netp);
			return;
		} else if ((SCTP_GET_STATE(stcb) == SCTP_STATE_SHUTDOWN_RECEIVED) &&
		    (asoc->stream_queue_cnt == 0)) {
			struct sctp_nets *netp;

			SCTP_STAT_DECR_GAUGE32(sctps_currestab);
			SCTP_SET_STATE(stcb, SCTP_STATE_SHUTDOWN_ACK_SENT);
			sctp_stop_timers_for_shutdown(stcb);
			if (asoc->alternate) {
				netp = asoc->alternate;
			} else {
				netp = asoc->primary_destination;
			}
			sctp_send_shutdown_ack(stcb, netp);
			sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNACK,
			    stcb->sctp_ep, stcb, netp);
			return;
		}
	}
	/*
	 * Now here we are going to recycle net_ack for a different use...
	 * HEADS UP.
	 */
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		net->net_ack = 0;
	}

	/*
	 * CMT DAC algorithm: If SACK DAC flag was 0, then no extra marking
	 * to be done. Setting this_sack_lowest_newack to the cum_ack will
	 * automatically ensure that.
	 */
	if ((asoc->sctp_cmt_on_off > 0) &&
	    SCTP_BASE_SYSCTL(sctp_cmt_use_dac) &&
	    (cmt_dac_flag == 0)) {
		this_sack_lowest_newack = cum_ack;
	}
	if ((num_seg > 0) || (num_nr_seg > 0)) {
		sctp_strike_gap_ack_chunks(stcb, asoc, biggest_tsn_acked,
		    biggest_tsn_newly_acked, this_sack_lowest_newack, accum_moved);
	}
	/* JRS - Use the congestion control given in the CC module */
	asoc->cc_functions.sctp_cwnd_update_after_fr(stcb, asoc);

	/* Now are we exiting loss recovery ? */
	if (will_exit_fast_recovery) {
		/* Ok, we must exit fast recovery */
		asoc->fast_retran_loss_recovery = 0;
	}
	if ((asoc->sat_t3_loss_recovery) &&
	    SCTP_TSN_GE(asoc->last_acked_seq, asoc->sat_t3_recovery_tsn)) {
		/* end satellite t3 loss recovery */
		asoc->sat_t3_loss_recovery = 0;
	}
	/*
	 * CMT Fast recovery
	 */
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		if (net->will_exit_fast_recovery) {
			/* Ok, we must exit fast recovery */
			net->fast_retran_loss_recovery = 0;
		}
	}

	/* Adjust and set the new rwnd value */
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_RWND_ENABLE) {
		sctp_log_rwnd_set(SCTP_SET_PEER_RWND_VIA_SACK,
		    asoc->peers_rwnd, asoc->total_flight, (asoc->total_flight_count * SCTP_BASE_SYSCTL(sctp_peer_chunk_oh)), a_rwnd);
	}
	asoc->peers_rwnd = sctp_sbspace_sub(a_rwnd,
	    (uint32_t)(asoc->total_flight + (asoc->total_flight_count * SCTP_BASE_SYSCTL(sctp_peer_chunk_oh))));
	if (asoc->peers_rwnd < stcb->sctp_ep->sctp_ep.sctp_sws_sender) {
		/* SWS sender side engages */
		asoc->peers_rwnd = 0;
	}
	if (asoc->peers_rwnd > old_rwnd) {
		win_probe_recovery = 1;
	}

	/*
	 * Now we must setup so we have a timer up for anyone with
	 * outstanding data.
	 */
	done_once = 0;
again:
	j = 0;
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		if (win_probe_recovery && (net->window_probe)) {
			win_probe_recovered = 1;
			/*-
			 * Find first chunk that was used with
			 * window probe and clear the event. Put
			 * it back into the send queue as if has
			 * not been sent.
			 */
			TAILQ_FOREACH(tp1, &asoc->sent_queue, sctp_next) {
				if (tp1->window_probe) {
					sctp_window_probe_recovery(stcb, asoc, tp1);
					break;
				}
			}
		}
		if (net->flight_size) {
			j++;
			if (!SCTP_OS_TIMER_PENDING(&net->rxt_timer.timer)) {
				sctp_timer_start(SCTP_TIMER_TYPE_SEND,
				    stcb->sctp_ep, stcb, net);
			}
			if (net->window_probe) {
				net->window_probe = 0;
			}
		} else {
			if (net->window_probe) {
				/*
				 * In window probes we must assure a timer
				 * is still running there
				 */
				if (!SCTP_OS_TIMER_PENDING(&net->rxt_timer.timer)) {
					sctp_timer_start(SCTP_TIMER_TYPE_SEND,
					    stcb->sctp_ep, stcb, net);

				}
			} else if (SCTP_OS_TIMER_PENDING(&net->rxt_timer.timer)) {
				sctp_timer_stop(SCTP_TIMER_TYPE_SEND, stcb->sctp_ep,
				    stcb, net,
				    SCTP_FROM_SCTP_INDATA + SCTP_LOC_32);
			}
		}
	}
	if ((j == 0) &&
	    (!TAILQ_EMPTY(&asoc->sent_queue)) &&
	    (asoc->sent_queue_retran_cnt == 0) &&
	    (win_probe_recovered == 0) &&
	    (done_once == 0)) {
		/*
		 * huh, this should not happen unless all packets are
		 * PR-SCTP and marked to skip of course.
		 */
		if (sctp_fs_audit(asoc)) {
			TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
				net->flight_size = 0;
			}
			asoc->total_flight = 0;
			asoc->total_flight_count = 0;
			asoc->sent_queue_retran_cnt = 0;
			TAILQ_FOREACH(tp1, &asoc->sent_queue, sctp_next) {
				if (tp1->sent < SCTP_DATAGRAM_RESEND) {
					sctp_flight_size_increase(tp1);
					sctp_total_flight_increase(stcb, tp1);
				} else if (tp1->sent == SCTP_DATAGRAM_RESEND) {
					sctp_ucount_incr(asoc->sent_queue_retran_cnt);
				}
			}
		}
		done_once = 1;
		goto again;
	}
	/*********************************************/
	/* Here we perform PR-SCTP procedures        */
	/* (section 4.2)                             */
	/*********************************************/
	/* C1. update advancedPeerAckPoint */
	if (SCTP_TSN_GT(cum_ack, asoc->advanced_peer_ack_point)) {
		asoc->advanced_peer_ack_point = cum_ack;
	}
	/* C2. try to further move advancedPeerAckPoint ahead */
	if ((asoc->prsctp_supported) && (asoc->pr_sctp_cnt > 0)) {
		struct sctp_tmit_chunk *lchk;
		uint32_t old_adv_peer_ack_point;

		old_adv_peer_ack_point = asoc->advanced_peer_ack_point;
		lchk = sctp_try_advance_peer_ack_point(stcb, asoc);
		/* C3. See if we need to send a Fwd-TSN */
		if (SCTP_TSN_GT(asoc->advanced_peer_ack_point, cum_ack)) {
			/*
			 * ISSUE with ECN, see FWD-TSN processing.
			 */
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_TRY_ADVANCE) {
				sctp_misc_ints(SCTP_FWD_TSN_CHECK,
				    0xee, cum_ack, asoc->advanced_peer_ack_point,
				    old_adv_peer_ack_point);
			}
			if (SCTP_TSN_GT(asoc->advanced_peer_ack_point, old_adv_peer_ack_point)) {
				send_forward_tsn(stcb, asoc);
			} else if (lchk) {
				/* try to FR fwd-tsn's that get lost too */
				if (lchk->rec.data.fwd_tsn_cnt >= 3) {
					send_forward_tsn(stcb, asoc);
				}
			}
		}
		if (lchk) {
			/* Assure a timer is up */
			sctp_timer_start(SCTP_TIMER_TYPE_SEND,
			    stcb->sctp_ep, stcb, lchk->whoTo);
		}
	}
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_SACK_RWND_LOGGING_ENABLE) {
		sctp_misc_ints(SCTP_SACK_RWND_UPDATE,
		    a_rwnd,
		    stcb->asoc.peers_rwnd,
		    stcb->asoc.total_flight,
		    stcb->asoc.total_output_queue_size);
	}
}

void
sctp_update_acked(struct sctp_tcb *stcb, struct sctp_shutdown_chunk *cp, int *abort_flag)
{
	/* Copy cum-ack */
	uint32_t cum_ack, a_rwnd;

	cum_ack = ntohl(cp->cumulative_tsn_ack);
	/* Arrange so a_rwnd does NOT change */
	a_rwnd = stcb->asoc.peers_rwnd + stcb->asoc.total_flight;

	/* Now call the express sack handling */
	sctp_express_handle_sack(stcb, cum_ack, a_rwnd, abort_flag, 0);
}

static void
sctp_kick_prsctp_reorder_queue(struct sctp_tcb *stcb,
    struct sctp_stream_in *strmin)
{
	struct sctp_queued_to_read *control, *ncontrol;
	struct sctp_association *asoc;
	uint32_t mid;
	int need_reasm_check = 0;

	asoc = &stcb->asoc;
	mid = strmin->last_mid_delivered;
	/*
	 * First deliver anything prior to and including the stream no that
	 * came in.
	 */
	TAILQ_FOREACH_SAFE(control, &strmin->inqueue, next_instrm, ncontrol) {
		if (SCTP_MID_GE(asoc->idata_supported, mid, control->mid)) {
			/* this is deliverable now */
			if (((control->sinfo_flags >> 8) & SCTP_DATA_NOT_FRAG) == SCTP_DATA_NOT_FRAG) {
				if (control->on_strm_q) {
					if (control->on_strm_q == SCTP_ON_ORDERED) {
						TAILQ_REMOVE(&strmin->inqueue, control, next_instrm);
					} else if (control->on_strm_q == SCTP_ON_UNORDERED) {
						TAILQ_REMOVE(&strmin->uno_inqueue, control, next_instrm);
#ifdef INVARIANTS
					} else {
						panic("strmin: %p ctl: %p unknown %d",
						    strmin, control, control->on_strm_q);
#endif
					}
					control->on_strm_q = 0;
				}
				/* subtract pending on streams */
				if (asoc->size_on_all_streams >= control->length) {
					asoc->size_on_all_streams -= control->length;
				} else {
#ifdef INVARIANTS
					panic("size_on_all_streams = %u smaller than control length %u", asoc->size_on_all_streams, control->length);
#else
					asoc->size_on_all_streams = 0;
#endif
				}
				sctp_ucount_decr(asoc->cnt_on_all_streams);
				/* deliver it to at least the delivery-q */
				if (stcb->sctp_socket) {
					sctp_mark_non_revokable(asoc, control->sinfo_tsn);
					sctp_add_to_readq(stcb->sctp_ep, stcb,
					    control,
					    &stcb->sctp_socket->so_rcv,
					    1, SCTP_READ_LOCK_HELD,
					    SCTP_SO_NOT_LOCKED);
				}
			} else {
				/* Its a fragmented message */
				if (control->first_frag_seen) {
					/*
					 * Make it so this is next to
					 * deliver, we restore later
					 */
					strmin->last_mid_delivered = control->mid - 1;
					need_reasm_check = 1;
					break;
				}
			}
		} else {
			/* no more delivery now. */
			break;
		}
	}
	if (need_reasm_check) {
		int ret;

		ret = sctp_deliver_reasm_check(stcb, &stcb->asoc, strmin, SCTP_READ_LOCK_HELD);
		if (SCTP_MID_GT(asoc->idata_supported, mid, strmin->last_mid_delivered)) {
			/* Restore the next to deliver unless we are ahead */
			strmin->last_mid_delivered = mid;
		}
		if (ret == 0) {
			/* Left the front Partial one on */
			return;
		}
		need_reasm_check = 0;
	}
	/*
	 * now we must deliver things in queue the normal way  if any are
	 * now ready.
	 */
	mid = strmin->last_mid_delivered + 1;
	TAILQ_FOREACH_SAFE(control, &strmin->inqueue, next_instrm, ncontrol) {
		if (SCTP_MID_EQ(asoc->idata_supported, mid, control->mid)) {
			if (((control->sinfo_flags >> 8) & SCTP_DATA_NOT_FRAG) == SCTP_DATA_NOT_FRAG) {
				/* this is deliverable now */
				if (control->on_strm_q) {
					if (control->on_strm_q == SCTP_ON_ORDERED) {
						TAILQ_REMOVE(&strmin->inqueue, control, next_instrm);
					} else if (control->on_strm_q == SCTP_ON_UNORDERED) {
						TAILQ_REMOVE(&strmin->uno_inqueue, control, next_instrm);
#ifdef INVARIANTS
					} else {
						panic("strmin: %p ctl: %p unknown %d",
						    strmin, control, control->on_strm_q);
#endif
					}
					control->on_strm_q = 0;
				}
				/* subtract pending on streams */
				if (asoc->size_on_all_streams >= control->length) {
					asoc->size_on_all_streams -= control->length;
				} else {
#ifdef INVARIANTS
					panic("size_on_all_streams = %u smaller than control length %u", asoc->size_on_all_streams, control->length);
#else
					asoc->size_on_all_streams = 0;
#endif
				}
				sctp_ucount_decr(asoc->cnt_on_all_streams);
				/* deliver it to at least the delivery-q */
				strmin->last_mid_delivered = control->mid;
				if (stcb->sctp_socket) {
					sctp_mark_non_revokable(asoc, control->sinfo_tsn);
					sctp_add_to_readq(stcb->sctp_ep, stcb,
					    control,
					    &stcb->sctp_socket->so_rcv, 1,
					    SCTP_READ_LOCK_HELD, SCTP_SO_NOT_LOCKED);

				}
				mid = strmin->last_mid_delivered + 1;
			} else {
				/* Its a fragmented message */
				if (control->first_frag_seen) {
					/*
					 * Make it so this is next to
					 * deliver
					 */
					strmin->last_mid_delivered = control->mid - 1;
					need_reasm_check = 1;
					break;
				}
			}
		} else {
			break;
		}
	}
	if (need_reasm_check) {
		(void)sctp_deliver_reasm_check(stcb, &stcb->asoc, strmin, SCTP_READ_LOCK_HELD);
	}
}



static void
sctp_flush_reassm_for_str_seq(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    uint16_t stream, uint32_t mid, int ordered, uint32_t cumtsn)
{
	struct sctp_queued_to_read *control;
	struct sctp_stream_in *strm;
	struct sctp_tmit_chunk *chk, *nchk;
	int cnt_removed = 0;

	/*
	 * For now large messages held on the stream reasm that are complete
	 * will be tossed too. We could in theory do more work to spin
	 * through and stop after dumping one msg aka seeing the start of a
	 * new msg at the head, and call the delivery function... to see if
	 * it can be delivered... But for now we just dump everything on the
	 * queue.
	 */
	strm = &asoc->strmin[stream];
	control = sctp_find_reasm_entry(strm, mid, ordered, asoc->idata_supported);
	if (control == NULL) {
		/* Not found */
		return;
	}
	if (!asoc->idata_supported && !ordered && SCTP_TSN_GT(control->fsn_included, cumtsn)) {
		return;
	}
	TAILQ_FOREACH_SAFE(chk, &control->reasm, sctp_next, nchk) {
		/* Purge hanging chunks */
		if (!asoc->idata_supported && (ordered == 0)) {
			if (SCTP_TSN_GT(chk->rec.data.tsn, cumtsn)) {
				break;
			}
		}
		cnt_removed++;
		TAILQ_REMOVE(&control->reasm, chk, sctp_next);
		if (asoc->size_on_reasm_queue >= chk->send_size) {
			asoc->size_on_reasm_queue -= chk->send_size;
		} else {
#ifdef INVARIANTS
			panic("size_on_reasm_queue = %u smaller than chunk length %u", asoc->size_on_reasm_queue, chk->send_size);
#else
			asoc->size_on_reasm_queue = 0;
#endif
		}
		sctp_ucount_decr(asoc->cnt_on_reasm_queue);
		if (chk->data) {
			sctp_m_freem(chk->data);
			chk->data = NULL;
		}
		sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
	}
	if (!TAILQ_EMPTY(&control->reasm)) {
		/* This has to be old data, unordered */
		if (control->data) {
			sctp_m_freem(control->data);
			control->data = NULL;
		}
		sctp_reset_a_control(control, stcb->sctp_ep, cumtsn);
		chk = TAILQ_FIRST(&control->reasm);
		if (chk->rec.data.rcv_flags & SCTP_DATA_FIRST_FRAG) {
			TAILQ_REMOVE(&control->reasm, chk, sctp_next);
			sctp_add_chk_to_control(control, strm, stcb, asoc,
			    chk, SCTP_READ_LOCK_HELD);
		}
		sctp_deliver_reasm_check(stcb, asoc, strm, SCTP_READ_LOCK_HELD);
		return;
	}
	if (control->on_strm_q == SCTP_ON_ORDERED) {
		TAILQ_REMOVE(&strm->inqueue, control, next_instrm);
		if (asoc->size_on_all_streams >= control->length) {
			asoc->size_on_all_streams -= control->length;
		} else {
#ifdef INVARIANTS
			panic("size_on_all_streams = %u smaller than control length %u", asoc->size_on_all_streams, control->length);
#else
			asoc->size_on_all_streams = 0;
#endif
		}
		sctp_ucount_decr(asoc->cnt_on_all_streams);
		control->on_strm_q = 0;
	} else if (control->on_strm_q == SCTP_ON_UNORDERED) {
		TAILQ_REMOVE(&strm->uno_inqueue, control, next_instrm);
		control->on_strm_q = 0;
#ifdef INVARIANTS
	} else if (control->on_strm_q) {
		panic("strm: %p ctl: %p unknown %d",
		    strm, control, control->on_strm_q);
#endif
	}
	control->on_strm_q = 0;
	if (control->on_read_q == 0) {
		sctp_free_remote_addr(control->whoFrom);
		if (control->data) {
			sctp_m_freem(control->data);
			control->data = NULL;
		}
		sctp_free_a_readq(stcb, control);
	}
}

void
sctp_handle_forward_tsn(struct sctp_tcb *stcb,
    struct sctp_forward_tsn_chunk *fwd,
    int *abort_flag, struct mbuf *m, int offset)
{
	/* The pr-sctp fwd tsn */
	/*
	 * here we will perform all the data receiver side steps for
	 * processing FwdTSN, as required in by pr-sctp draft:
	 *
	 * Assume we get FwdTSN(x):
	 *
	 * 1) update local cumTSN to x 2) try to further advance cumTSN to x
	 * + others we have 3) examine and update re-ordering queue on
	 * pr-in-streams 4) clean up re-assembly queue 5) Send a sack to
	 * report where we are.
	 */
	struct sctp_association *asoc;
	uint32_t new_cum_tsn, gap;
	unsigned int i, fwd_sz, m_size;
	uint32_t str_seq;
	struct sctp_stream_in *strm;
	struct sctp_queued_to_read *control, *sv;

	asoc = &stcb->asoc;
	if ((fwd_sz = ntohs(fwd->ch.chunk_length)) < sizeof(struct sctp_forward_tsn_chunk)) {
		SCTPDBG(SCTP_DEBUG_INDATA1,
		    "Bad size too small/big fwd-tsn\n");
		return;
	}
	m_size = (stcb->asoc.mapping_array_size << 3);
	/*************************************************************/
	/* 1. Here we update local cumTSN and shift the bitmap array */
	/*************************************************************/
	new_cum_tsn = ntohl(fwd->new_cumulative_tsn);

	if (SCTP_TSN_GE(asoc->cumulative_tsn, new_cum_tsn)) {
		/* Already got there ... */
		return;
	}
	/*
	 * now we know the new TSN is more advanced, let's find the actual
	 * gap
	 */
	SCTP_CALC_TSN_TO_GAP(gap, new_cum_tsn, asoc->mapping_array_base_tsn);
	asoc->cumulative_tsn = new_cum_tsn;
	if (gap >= m_size) {
		if ((long)gap > sctp_sbspace(&stcb->asoc, &stcb->sctp_socket->so_rcv)) {
			struct mbuf *op_err;
			char msg[SCTP_DIAG_INFO_LEN];

			/*
			 * out of range (of single byte chunks in the rwnd I
			 * give out). This must be an attacker.
			 */
			*abort_flag = 1;
			snprintf(msg, sizeof(msg),
			    "New cum ack %8.8x too high, highest TSN %8.8x",
			    new_cum_tsn, asoc->highest_tsn_inside_map);
			op_err = sctp_generate_cause(SCTP_CAUSE_PROTOCOL_VIOLATION, msg);
			stcb->sctp_ep->last_abort_code = SCTP_FROM_SCTP_INDATA + SCTP_LOC_33;
			sctp_abort_an_association(stcb->sctp_ep, stcb, op_err, SCTP_SO_NOT_LOCKED);
			return;
		}
		SCTP_STAT_INCR(sctps_fwdtsn_map_over);

		memset(stcb->asoc.mapping_array, 0, stcb->asoc.mapping_array_size);
		asoc->mapping_array_base_tsn = new_cum_tsn + 1;
		asoc->highest_tsn_inside_map = new_cum_tsn;

		memset(stcb->asoc.nr_mapping_array, 0, stcb->asoc.mapping_array_size);
		asoc->highest_tsn_inside_nr_map = new_cum_tsn;

		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MAP_LOGGING_ENABLE) {
			sctp_log_map(0, 3, asoc->highest_tsn_inside_map, SCTP_MAP_SLIDE_RESULT);
		}
	} else {
		SCTP_TCB_LOCK_ASSERT(stcb);
		for (i = 0; i <= gap; i++) {
			if (!SCTP_IS_TSN_PRESENT(asoc->mapping_array, i) &&
			    !SCTP_IS_TSN_PRESENT(asoc->nr_mapping_array, i)) {
				SCTP_SET_TSN_PRESENT(asoc->nr_mapping_array, i);
				if (SCTP_TSN_GT(asoc->mapping_array_base_tsn + i, asoc->highest_tsn_inside_nr_map)) {
					asoc->highest_tsn_inside_nr_map = asoc->mapping_array_base_tsn + i;
				}
			}
		}
	}
	/*************************************************************/
	/* 2. Clear up re-assembly queue                             */
	/*************************************************************/

	/* This is now done as part of clearing up the stream/seq */
	if (asoc->idata_supported == 0) {
		uint16_t sid;

		/* Flush all the un-ordered data based on cum-tsn */
		SCTP_INP_READ_LOCK(stcb->sctp_ep);
		for (sid = 0; sid < asoc->streamincnt; sid++) {
			sctp_flush_reassm_for_str_seq(stcb, asoc, sid, 0, 0, new_cum_tsn);
		}
		SCTP_INP_READ_UNLOCK(stcb->sctp_ep);
	}
	/*******************************************************/
	/* 3. Update the PR-stream re-ordering queues and fix  */
	/* delivery issues as needed.                       */
	/*******************************************************/
	fwd_sz -= sizeof(*fwd);
	if (m && fwd_sz) {
		/* New method. */
		unsigned int num_str;
		uint32_t mid, cur_mid;
		uint16_t sid;
		uint16_t ordered, flags;
		struct sctp_strseq *stseq, strseqbuf;
		struct sctp_strseq_mid *stseq_m, strseqbuf_m;

		offset += sizeof(*fwd);

		SCTP_INP_READ_LOCK(stcb->sctp_ep);
		if (asoc->idata_supported) {
			num_str = fwd_sz / sizeof(struct sctp_strseq_mid);
		} else {
			num_str = fwd_sz / sizeof(struct sctp_strseq);
		}
		for (i = 0; i < num_str; i++) {
			if (asoc->idata_supported) {
				stseq_m = (struct sctp_strseq_mid *)sctp_m_getptr(m, offset,
				    sizeof(struct sctp_strseq_mid),
				    (uint8_t *)&strseqbuf_m);
				offset += sizeof(struct sctp_strseq_mid);
				if (stseq_m == NULL) {
					break;
				}
				sid = ntohs(stseq_m->sid);
				mid = ntohl(stseq_m->mid);
				flags = ntohs(stseq_m->flags);
				if (flags & PR_SCTP_UNORDERED_FLAG) {
					ordered = 0;
				} else {
					ordered = 1;
				}
			} else {
				stseq = (struct sctp_strseq *)sctp_m_getptr(m, offset,
				    sizeof(struct sctp_strseq),
				    (uint8_t *)&strseqbuf);
				offset += sizeof(struct sctp_strseq);
				if (stseq == NULL) {
					break;
				}
				sid = ntohs(stseq->sid);
				mid = (uint32_t)ntohs(stseq->ssn);
				ordered = 1;
			}
			/* Convert */

			/* now process */

			/*
			 * Ok we now look for the stream/seq on the read
			 * queue where its not all delivered. If we find it
			 * we transmute the read entry into a PDI_ABORTED.
			 */
			if (sid >= asoc->streamincnt) {
				/* screwed up streams, stop!  */
				break;
			}
			if ((asoc->str_of_pdapi == sid) &&
			    (asoc->ssn_of_pdapi == mid)) {
				/*
				 * If this is the one we were partially
				 * delivering now then we no longer are.
				 * Note this will change with the reassembly
				 * re-write.
				 */
				asoc->fragmented_delivery_inprogress = 0;
			}
			strm = &asoc->strmin[sid];
			for (cur_mid = strm->last_mid_delivered; SCTP_MID_GE(asoc->idata_supported, mid, cur_mid); cur_mid++) {
				sctp_flush_reassm_for_str_seq(stcb, asoc, sid, cur_mid, ordered, new_cum_tsn);
			}
			TAILQ_FOREACH(control, &stcb->sctp_ep->read_queue, next) {
				if ((control->sinfo_stream == sid) &&
				    (SCTP_MID_EQ(asoc->idata_supported, control->mid, mid))) {
					str_seq = (sid << 16) | (0x0000ffff & mid);
					control->pdapi_aborted = 1;
					sv = stcb->asoc.control_pdapi;
					control->end_added = 1;
					if (control->on_strm_q == SCTP_ON_ORDERED) {
						TAILQ_REMOVE(&strm->inqueue, control, next_instrm);
						if (asoc->size_on_all_streams >= control->length) {
							asoc->size_on_all_streams -= control->length;
						} else {
#ifdef INVARIANTS
							panic("size_on_all_streams = %u smaller than control length %u", asoc->size_on_all_streams, control->length);
#else
							asoc->size_on_all_streams = 0;
#endif
						}
						sctp_ucount_decr(asoc->cnt_on_all_streams);
					} else if (control->on_strm_q == SCTP_ON_UNORDERED) {
						TAILQ_REMOVE(&strm->uno_inqueue, control, next_instrm);
#ifdef INVARIANTS
					} else if (control->on_strm_q) {
						panic("strm: %p ctl: %p unknown %d",
						    strm, control, control->on_strm_q);
#endif
					}
					control->on_strm_q = 0;
					stcb->asoc.control_pdapi = control;
					sctp_ulp_notify(SCTP_NOTIFY_PARTIAL_DELVIERY_INDICATION,
					    stcb,
					    SCTP_PARTIAL_DELIVERY_ABORTED,
					    (void *)&str_seq,
					    SCTP_SO_NOT_LOCKED);
					stcb->asoc.control_pdapi = sv;
					break;
				} else if ((control->sinfo_stream == sid) &&
				    SCTP_MID_GT(asoc->idata_supported, control->mid, mid)) {
					/* We are past our victim SSN */
					break;
				}
			}
			if (SCTP_MID_GT(asoc->idata_supported, mid, strm->last_mid_delivered)) {
				/* Update the sequence number */
				strm->last_mid_delivered = mid;
			}
			/* now kick the stream the new way */
			/* sa_ignore NO_NULL_CHK */
			sctp_kick_prsctp_reorder_queue(stcb, strm);
		}
		SCTP_INP_READ_UNLOCK(stcb->sctp_ep);
	}
	/*
	 * Now slide thing forward.
	 */
	sctp_slide_mapping_arrays(stcb);
}
