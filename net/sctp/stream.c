// SPDX-License-Identifier: GPL-2.0-or-later
/* SCTP kernel implementation
 * (C) Copyright IBM Corp. 2001, 2004
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 Intel Corp.
 *
 * This file is part of the SCTP kernel implementation
 *
 * This file contains sctp stream maniuplation primitives and helpers.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    Xin Long <lucien.xin@gmail.com>
 */

#include <linux/list.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>
#include <net/sctp/stream_sched.h>

/* Migrates chunks from stream queues to new stream queues if needed,
 * but not across associations. Also, removes those chunks to streams
 * higher than the new max.
 */
static void sctp_stream_outq_migrate(struct sctp_stream *stream,
				     struct sctp_stream *new, __u16 outcnt)
{
	struct sctp_association *asoc;
	struct sctp_chunk *ch, *temp;
	struct sctp_outq *outq;
	int i;

	asoc = container_of(stream, struct sctp_association, stream);
	outq = &asoc->outqueue;

	list_for_each_entry_safe(ch, temp, &outq->out_chunk_list, list) {
		__u16 sid = sctp_chunk_stream_no(ch);

		if (sid < outcnt)
			continue;

		sctp_sched_dequeue_common(outq, ch);
		/* No need to call dequeue_done here because
		 * the chunks are not scheduled by now.
		 */

		/* Mark as failed send. */
		sctp_chunk_fail(ch, (__force __u32)SCTP_ERROR_INV_STRM);
		if (asoc->peer.prsctp_capable &&
		    SCTP_PR_PRIO_ENABLED(ch->sinfo.sinfo_flags))
			asoc->sent_cnt_removable--;

		sctp_chunk_free(ch);
	}

	if (new) {
		/* Here we actually move the old ext stuff into the new
		 * buffer, because we want to keep it. Then
		 * sctp_stream_update will swap ->out pointers.
		 */
		for (i = 0; i < outcnt; i++) {
			kfree(SCTP_SO(new, i)->ext);
			SCTP_SO(new, i)->ext = SCTP_SO(stream, i)->ext;
			SCTP_SO(stream, i)->ext = NULL;
		}
	}

	for (i = outcnt; i < stream->outcnt; i++) {
		kfree(SCTP_SO(stream, i)->ext);
		SCTP_SO(stream, i)->ext = NULL;
	}
}

static int sctp_stream_alloc_out(struct sctp_stream *stream, __u16 outcnt,
				 gfp_t gfp)
{
	int ret;

	if (outcnt <= stream->outcnt)
		return 0;

	ret = genradix_prealloc(&stream->out, outcnt, gfp);
	if (ret)
		return ret;

	stream->outcnt = outcnt;
	return 0;
}

static int sctp_stream_alloc_in(struct sctp_stream *stream, __u16 incnt,
				gfp_t gfp)
{
	int ret;

	if (incnt <= stream->incnt)
		return 0;

	ret = genradix_prealloc(&stream->in, incnt, gfp);
	if (ret)
		return ret;

	stream->incnt = incnt;
	return 0;
}

int sctp_stream_init(struct sctp_stream *stream, __u16 outcnt, __u16 incnt,
		     gfp_t gfp)
{
	struct sctp_sched_ops *sched = sctp_sched_ops_from_stream(stream);
	int i, ret = 0;

	gfp |= __GFP_NOWARN;

	/* Initial stream->out size may be very big, so free it and alloc
	 * a new one with new outcnt to save memory if needed.
	 */
	if (outcnt == stream->outcnt)
		goto in;

	/* Filter out chunks queued on streams that won't exist anymore */
	sched->unsched_all(stream);
	sctp_stream_outq_migrate(stream, NULL, outcnt);
	sched->sched_all(stream);

	ret = sctp_stream_alloc_out(stream, outcnt, gfp);
	if (ret)
		goto out;

	for (i = 0; i < stream->outcnt; i++)
		SCTP_SO(stream, i)->state = SCTP_STREAM_OPEN;

in:
	sctp_stream_interleave_init(stream);
	if (!incnt)
		goto out;

	ret = sctp_stream_alloc_in(stream, incnt, gfp);
	if (ret) {
		sched->free(stream);
		genradix_free(&stream->out);
		stream->outcnt = 0;
		goto out;
	}

out:
	return ret;
}

int sctp_stream_init_ext(struct sctp_stream *stream, __u16 sid)
{
	struct sctp_stream_out_ext *soute;

	soute = kzalloc(sizeof(*soute), GFP_KERNEL);
	if (!soute)
		return -ENOMEM;
	SCTP_SO(stream, sid)->ext = soute;

	return sctp_sched_init_sid(stream, sid, GFP_KERNEL);
}

void sctp_stream_free(struct sctp_stream *stream)
{
	struct sctp_sched_ops *sched = sctp_sched_ops_from_stream(stream);
	int i;

	sched->free(stream);
	for (i = 0; i < stream->outcnt; i++)
		kfree(SCTP_SO(stream, i)->ext);
	genradix_free(&stream->out);
	genradix_free(&stream->in);
}

void sctp_stream_clear(struct sctp_stream *stream)
{
	int i;

	for (i = 0; i < stream->outcnt; i++) {
		SCTP_SO(stream, i)->mid = 0;
		SCTP_SO(stream, i)->mid_uo = 0;
	}

	for (i = 0; i < stream->incnt; i++)
		SCTP_SI(stream, i)->mid = 0;
}

void sctp_stream_update(struct sctp_stream *stream, struct sctp_stream *new)
{
	struct sctp_sched_ops *sched = sctp_sched_ops_from_stream(stream);

	sched->unsched_all(stream);
	sctp_stream_outq_migrate(stream, new, new->outcnt);
	sctp_stream_free(stream);

	stream->out = new->out;
	stream->in  = new->in;
	stream->outcnt = new->outcnt;
	stream->incnt  = new->incnt;

	sched->sched_all(stream);

	new->out.tree.root = NULL;
	new->in.tree.root  = NULL;
	new->outcnt = 0;
	new->incnt  = 0;
}

static int sctp_send_reconf(struct sctp_association *asoc,
			    struct sctp_chunk *chunk)
{
	struct net *net = sock_net(asoc->base.sk);
	int retval = 0;

	retval = sctp_primitive_RECONF(net, asoc, chunk);
	if (retval)
		sctp_chunk_free(chunk);

	return retval;
}

static bool sctp_stream_outq_is_empty(struct sctp_stream *stream,
				      __u16 str_nums, __be16 *str_list)
{
	struct sctp_association *asoc;
	__u16 i;

	asoc = container_of(stream, struct sctp_association, stream);
	if (!asoc->outqueue.out_qlen)
		return true;

	if (!str_nums)
		return false;

	for (i = 0; i < str_nums; i++) {
		__u16 sid = ntohs(str_list[i]);

		if (SCTP_SO(stream, sid)->ext &&
		    !list_empty(&SCTP_SO(stream, sid)->ext->outq))
			return false;
	}

	return true;
}

int sctp_send_reset_streams(struct sctp_association *asoc,
			    struct sctp_reset_streams *params)
{
	struct sctp_stream *stream = &asoc->stream;
	__u16 i, str_nums, *str_list;
	struct sctp_chunk *chunk;
	int retval = -EINVAL;
	__be16 *nstr_list;
	bool out, in;

	if (!asoc->peer.reconf_capable ||
	    !(asoc->strreset_enable & SCTP_ENABLE_RESET_STREAM_REQ)) {
		retval = -ENOPROTOOPT;
		goto out;
	}

	if (asoc->strreset_outstanding) {
		retval = -EINPROGRESS;
		goto out;
	}

	out = params->srs_flags & SCTP_STREAM_RESET_OUTGOING;
	in  = params->srs_flags & SCTP_STREAM_RESET_INCOMING;
	if (!out && !in)
		goto out;

	str_nums = params->srs_number_streams;
	str_list = params->srs_stream_list;
	if (str_nums) {
		int param_len = 0;

		if (out) {
			for (i = 0; i < str_nums; i++)
				if (str_list[i] >= stream->outcnt)
					goto out;

			param_len = str_nums * sizeof(__u16) +
				    sizeof(struct sctp_strreset_outreq);
		}

		if (in) {
			for (i = 0; i < str_nums; i++)
				if (str_list[i] >= stream->incnt)
					goto out;

			param_len += str_nums * sizeof(__u16) +
				     sizeof(struct sctp_strreset_inreq);
		}

		if (param_len > SCTP_MAX_CHUNK_LEN -
				sizeof(struct sctp_reconf_chunk))
			goto out;
	}

	nstr_list = kcalloc(str_nums, sizeof(__be16), GFP_KERNEL);
	if (!nstr_list) {
		retval = -ENOMEM;
		goto out;
	}

	for (i = 0; i < str_nums; i++)
		nstr_list[i] = htons(str_list[i]);

	if (out && !sctp_stream_outq_is_empty(stream, str_nums, nstr_list)) {
		retval = -EAGAIN;
		goto out;
	}

	chunk = sctp_make_strreset_req(asoc, str_nums, nstr_list, out, in);

	kfree(nstr_list);

	if (!chunk) {
		retval = -ENOMEM;
		goto out;
	}

	if (out) {
		if (str_nums)
			for (i = 0; i < str_nums; i++)
				SCTP_SO(stream, str_list[i])->state =
						       SCTP_STREAM_CLOSED;
		else
			for (i = 0; i < stream->outcnt; i++)
				SCTP_SO(stream, i)->state = SCTP_STREAM_CLOSED;
	}

	asoc->strreset_chunk = chunk;
	sctp_chunk_hold(asoc->strreset_chunk);

	retval = sctp_send_reconf(asoc, chunk);
	if (retval) {
		sctp_chunk_put(asoc->strreset_chunk);
		asoc->strreset_chunk = NULL;
		if (!out)
			goto out;

		if (str_nums)
			for (i = 0; i < str_nums; i++)
				SCTP_SO(stream, str_list[i])->state =
						       SCTP_STREAM_OPEN;
		else
			for (i = 0; i < stream->outcnt; i++)
				SCTP_SO(stream, i)->state = SCTP_STREAM_OPEN;

		goto out;
	}

	asoc->strreset_outstanding = out + in;

out:
	return retval;
}

int sctp_send_reset_assoc(struct sctp_association *asoc)
{
	struct sctp_stream *stream = &asoc->stream;
	struct sctp_chunk *chunk = NULL;
	int retval;
	__u16 i;

	if (!asoc->peer.reconf_capable ||
	    !(asoc->strreset_enable & SCTP_ENABLE_RESET_ASSOC_REQ))
		return -ENOPROTOOPT;

	if (asoc->strreset_outstanding)
		return -EINPROGRESS;

	if (!sctp_outq_is_empty(&asoc->outqueue))
		return -EAGAIN;

	chunk = sctp_make_strreset_tsnreq(asoc);
	if (!chunk)
		return -ENOMEM;

	/* Block further xmit of data until this request is completed */
	for (i = 0; i < stream->outcnt; i++)
		SCTP_SO(stream, i)->state = SCTP_STREAM_CLOSED;

	asoc->strreset_chunk = chunk;
	sctp_chunk_hold(asoc->strreset_chunk);

	retval = sctp_send_reconf(asoc, chunk);
	if (retval) {
		sctp_chunk_put(asoc->strreset_chunk);
		asoc->strreset_chunk = NULL;

		for (i = 0; i < stream->outcnt; i++)
			SCTP_SO(stream, i)->state = SCTP_STREAM_OPEN;

		return retval;
	}

	asoc->strreset_outstanding = 1;

	return 0;
}

int sctp_send_add_streams(struct sctp_association *asoc,
			  struct sctp_add_streams *params)
{
	struct sctp_stream *stream = &asoc->stream;
	struct sctp_chunk *chunk = NULL;
	int retval;
	__u32 outcnt, incnt;
	__u16 out, in;

	if (!asoc->peer.reconf_capable ||
	    !(asoc->strreset_enable & SCTP_ENABLE_CHANGE_ASSOC_REQ)) {
		retval = -ENOPROTOOPT;
		goto out;
	}

	if (asoc->strreset_outstanding) {
		retval = -EINPROGRESS;
		goto out;
	}

	out = params->sas_outstrms;
	in  = params->sas_instrms;
	outcnt = stream->outcnt + out;
	incnt = stream->incnt + in;
	if (outcnt > SCTP_MAX_STREAM || incnt > SCTP_MAX_STREAM ||
	    (!out && !in)) {
		retval = -EINVAL;
		goto out;
	}

	if (out) {
		retval = sctp_stream_alloc_out(stream, outcnt, GFP_KERNEL);
		if (retval)
			goto out;
	}

	chunk = sctp_make_strreset_addstrm(asoc, out, in);
	if (!chunk) {
		retval = -ENOMEM;
		goto out;
	}

	asoc->strreset_chunk = chunk;
	sctp_chunk_hold(asoc->strreset_chunk);

	retval = sctp_send_reconf(asoc, chunk);
	if (retval) {
		sctp_chunk_put(asoc->strreset_chunk);
		asoc->strreset_chunk = NULL;
		goto out;
	}

	asoc->strreset_outstanding = !!out + !!in;

out:
	return retval;
}

static struct sctp_paramhdr *sctp_chunk_lookup_strreset_param(
			struct sctp_association *asoc, __be32 resp_seq,
			__be16 type)
{
	struct sctp_chunk *chunk = asoc->strreset_chunk;
	struct sctp_reconf_chunk *hdr;
	union sctp_params param;

	if (!chunk)
		return NULL;

	hdr = (struct sctp_reconf_chunk *)chunk->chunk_hdr;
	sctp_walk_params(param, hdr, params) {
		/* sctp_strreset_tsnreq is actually the basic structure
		 * of all stream reconf params, so it's safe to use it
		 * to access request_seq.
		 */
		struct sctp_strreset_tsnreq *req = param.v;

		if ((!resp_seq || req->request_seq == resp_seq) &&
		    (!type || type == req->param_hdr.type))
			return param.v;
	}

	return NULL;
}

static void sctp_update_strreset_result(struct sctp_association *asoc,
					__u32 result)
{
	asoc->strreset_result[1] = asoc->strreset_result[0];
	asoc->strreset_result[0] = result;
}

struct sctp_chunk *sctp_process_strreset_outreq(
				struct sctp_association *asoc,
				union sctp_params param,
				struct sctp_ulpevent **evp)
{
	struct sctp_strreset_outreq *outreq = param.v;
	struct sctp_stream *stream = &asoc->stream;
	__u32 result = SCTP_STRRESET_DENIED;
	__be16 *str_p = NULL;
	__u32 request_seq;
	__u16 i, nums;

	request_seq = ntohl(outreq->request_seq);

	if (ntohl(outreq->send_reset_at_tsn) >
	    sctp_tsnmap_get_ctsn(&asoc->peer.tsn_map)) {
		result = SCTP_STRRESET_IN_PROGRESS;
		goto err;
	}

	if (TSN_lt(asoc->strreset_inseq, request_seq) ||
	    TSN_lt(request_seq, asoc->strreset_inseq - 2)) {
		result = SCTP_STRRESET_ERR_BAD_SEQNO;
		goto err;
	} else if (TSN_lt(request_seq, asoc->strreset_inseq)) {
		i = asoc->strreset_inseq - request_seq - 1;
		result = asoc->strreset_result[i];
		goto err;
	}
	asoc->strreset_inseq++;

	/* Check strreset_enable after inseq inc, as sender cannot tell
	 * the peer doesn't enable strreset after receiving response with
	 * result denied, as well as to keep consistent with bsd.
	 */
	if (!(asoc->strreset_enable & SCTP_ENABLE_RESET_STREAM_REQ))
		goto out;

	nums = (ntohs(param.p->length) - sizeof(*outreq)) / sizeof(__u16);
	str_p = outreq->list_of_streams;
	for (i = 0; i < nums; i++) {
		if (ntohs(str_p[i]) >= stream->incnt) {
			result = SCTP_STRRESET_ERR_WRONG_SSN;
			goto out;
		}
	}

	if (asoc->strreset_chunk) {
		if (!sctp_chunk_lookup_strreset_param(
				asoc, outreq->response_seq,
				SCTP_PARAM_RESET_IN_REQUEST)) {
			/* same process with outstanding isn't 0 */
			result = SCTP_STRRESET_ERR_IN_PROGRESS;
			goto out;
		}

		asoc->strreset_outstanding--;
		asoc->strreset_outseq++;

		if (!asoc->strreset_outstanding) {
			struct sctp_transport *t;

			t = asoc->strreset_chunk->transport;
			if (del_timer(&t->reconf_timer))
				sctp_transport_put(t);

			sctp_chunk_put(asoc->strreset_chunk);
			asoc->strreset_chunk = NULL;
		}
	}

	if (nums)
		for (i = 0; i < nums; i++)
			SCTP_SI(stream, ntohs(str_p[i]))->mid = 0;
	else
		for (i = 0; i < stream->incnt; i++)
			SCTP_SI(stream, i)->mid = 0;

	result = SCTP_STRRESET_PERFORMED;

	*evp = sctp_ulpevent_make_stream_reset_event(asoc,
		SCTP_STREAM_RESET_INCOMING_SSN, nums, str_p, GFP_ATOMIC);

out:
	sctp_update_strreset_result(asoc, result);
err:
	return sctp_make_strreset_resp(asoc, result, request_seq);
}

struct sctp_chunk *sctp_process_strreset_inreq(
				struct sctp_association *asoc,
				union sctp_params param,
				struct sctp_ulpevent **evp)
{
	struct sctp_strreset_inreq *inreq = param.v;
	struct sctp_stream *stream = &asoc->stream;
	__u32 result = SCTP_STRRESET_DENIED;
	struct sctp_chunk *chunk = NULL;
	__u32 request_seq;
	__u16 i, nums;
	__be16 *str_p;

	request_seq = ntohl(inreq->request_seq);
	if (TSN_lt(asoc->strreset_inseq, request_seq) ||
	    TSN_lt(request_seq, asoc->strreset_inseq - 2)) {
		result = SCTP_STRRESET_ERR_BAD_SEQNO;
		goto err;
	} else if (TSN_lt(request_seq, asoc->strreset_inseq)) {
		i = asoc->strreset_inseq - request_seq - 1;
		result = asoc->strreset_result[i];
		if (result == SCTP_STRRESET_PERFORMED)
			return NULL;
		goto err;
	}
	asoc->strreset_inseq++;

	if (!(asoc->strreset_enable & SCTP_ENABLE_RESET_STREAM_REQ))
		goto out;

	if (asoc->strreset_outstanding) {
		result = SCTP_STRRESET_ERR_IN_PROGRESS;
		goto out;
	}

	nums = (ntohs(param.p->length) - sizeof(*inreq)) / sizeof(__u16);
	str_p = inreq->list_of_streams;
	for (i = 0; i < nums; i++) {
		if (ntohs(str_p[i]) >= stream->outcnt) {
			result = SCTP_STRRESET_ERR_WRONG_SSN;
			goto out;
		}
	}

	if (!sctp_stream_outq_is_empty(stream, nums, str_p)) {
		result = SCTP_STRRESET_IN_PROGRESS;
		asoc->strreset_inseq--;
		goto err;
	}

	chunk = sctp_make_strreset_req(asoc, nums, str_p, 1, 0);
	if (!chunk)
		goto out;

	if (nums)
		for (i = 0; i < nums; i++)
			SCTP_SO(stream, ntohs(str_p[i]))->state =
					       SCTP_STREAM_CLOSED;
	else
		for (i = 0; i < stream->outcnt; i++)
			SCTP_SO(stream, i)->state = SCTP_STREAM_CLOSED;

	asoc->strreset_chunk = chunk;
	asoc->strreset_outstanding = 1;
	sctp_chunk_hold(asoc->strreset_chunk);

	result = SCTP_STRRESET_PERFORMED;

out:
	sctp_update_strreset_result(asoc, result);
err:
	if (!chunk)
		chunk =  sctp_make_strreset_resp(asoc, result, request_seq);

	return chunk;
}

struct sctp_chunk *sctp_process_strreset_tsnreq(
				struct sctp_association *asoc,
				union sctp_params param,
				struct sctp_ulpevent **evp)
{
	__u32 init_tsn = 0, next_tsn = 0, max_tsn_seen;
	struct sctp_strreset_tsnreq *tsnreq = param.v;
	struct sctp_stream *stream = &asoc->stream;
	__u32 result = SCTP_STRRESET_DENIED;
	__u32 request_seq;
	__u16 i;

	request_seq = ntohl(tsnreq->request_seq);
	if (TSN_lt(asoc->strreset_inseq, request_seq) ||
	    TSN_lt(request_seq, asoc->strreset_inseq - 2)) {
		result = SCTP_STRRESET_ERR_BAD_SEQNO;
		goto err;
	} else if (TSN_lt(request_seq, asoc->strreset_inseq)) {
		i = asoc->strreset_inseq - request_seq - 1;
		result = asoc->strreset_result[i];
		if (result == SCTP_STRRESET_PERFORMED) {
			next_tsn = asoc->ctsn_ack_point + 1;
			init_tsn =
				sctp_tsnmap_get_ctsn(&asoc->peer.tsn_map) + 1;
		}
		goto err;
	}

	if (!sctp_outq_is_empty(&asoc->outqueue)) {
		result = SCTP_STRRESET_IN_PROGRESS;
		goto err;
	}

	asoc->strreset_inseq++;

	if (!(asoc->strreset_enable & SCTP_ENABLE_RESET_ASSOC_REQ))
		goto out;

	if (asoc->strreset_outstanding) {
		result = SCTP_STRRESET_ERR_IN_PROGRESS;
		goto out;
	}

	/* G4: The same processing as though a FWD-TSN chunk (as defined in
	 *     [RFC3758]) with all streams affected and a new cumulative TSN
	 *     ACK of the Receiver's Next TSN minus 1 were received MUST be
	 *     performed.
	 */
	max_tsn_seen = sctp_tsnmap_get_max_tsn_seen(&asoc->peer.tsn_map);
	asoc->stream.si->report_ftsn(&asoc->ulpq, max_tsn_seen);

	/* G1: Compute an appropriate value for the Receiver's Next TSN -- the
	 *     TSN that the peer should use to send the next DATA chunk.  The
	 *     value SHOULD be the smallest TSN not acknowledged by the
	 *     receiver of the request plus 2^31.
	 */
	init_tsn = sctp_tsnmap_get_ctsn(&asoc->peer.tsn_map) + (1 << 31);
	sctp_tsnmap_init(&asoc->peer.tsn_map, SCTP_TSN_MAP_INITIAL,
			 init_tsn, GFP_ATOMIC);

	/* G3: The same processing as though a SACK chunk with no gap report
	 *     and a cumulative TSN ACK of the Sender's Next TSN minus 1 were
	 *     received MUST be performed.
	 */
	sctp_outq_free(&asoc->outqueue);

	/* G2: Compute an appropriate value for the local endpoint's next TSN,
	 *     i.e., the next TSN assigned by the receiver of the SSN/TSN reset
	 *     chunk.  The value SHOULD be the highest TSN sent by the receiver
	 *     of the request plus 1.
	 */
	next_tsn = asoc->next_tsn;
	asoc->ctsn_ack_point = next_tsn - 1;
	asoc->adv_peer_ack_point = asoc->ctsn_ack_point;

	/* G5:  The next expected and outgoing SSNs MUST be reset to 0 for all
	 *      incoming and outgoing streams.
	 */
	for (i = 0; i < stream->outcnt; i++) {
		SCTP_SO(stream, i)->mid = 0;
		SCTP_SO(stream, i)->mid_uo = 0;
	}
	for (i = 0; i < stream->incnt; i++)
		SCTP_SI(stream, i)->mid = 0;

	result = SCTP_STRRESET_PERFORMED;

	*evp = sctp_ulpevent_make_assoc_reset_event(asoc, 0, init_tsn,
						    next_tsn, GFP_ATOMIC);

out:
	sctp_update_strreset_result(asoc, result);
err:
	return sctp_make_strreset_tsnresp(asoc, result, request_seq,
					  next_tsn, init_tsn);
}

struct sctp_chunk *sctp_process_strreset_addstrm_out(
				struct sctp_association *asoc,
				union sctp_params param,
				struct sctp_ulpevent **evp)
{
	struct sctp_strreset_addstrm *addstrm = param.v;
	struct sctp_stream *stream = &asoc->stream;
	__u32 result = SCTP_STRRESET_DENIED;
	__u32 request_seq, incnt;
	__u16 in, i;

	request_seq = ntohl(addstrm->request_seq);
	if (TSN_lt(asoc->strreset_inseq, request_seq) ||
	    TSN_lt(request_seq, asoc->strreset_inseq - 2)) {
		result = SCTP_STRRESET_ERR_BAD_SEQNO;
		goto err;
	} else if (TSN_lt(request_seq, asoc->strreset_inseq)) {
		i = asoc->strreset_inseq - request_seq - 1;
		result = asoc->strreset_result[i];
		goto err;
	}
	asoc->strreset_inseq++;

	if (!(asoc->strreset_enable & SCTP_ENABLE_CHANGE_ASSOC_REQ))
		goto out;

	in = ntohs(addstrm->number_of_streams);
	incnt = stream->incnt + in;
	if (!in || incnt > SCTP_MAX_STREAM)
		goto out;

	if (sctp_stream_alloc_in(stream, incnt, GFP_ATOMIC))
		goto out;

	if (asoc->strreset_chunk) {
		if (!sctp_chunk_lookup_strreset_param(
			asoc, 0, SCTP_PARAM_RESET_ADD_IN_STREAMS)) {
			/* same process with outstanding isn't 0 */
			result = SCTP_STRRESET_ERR_IN_PROGRESS;
			goto out;
		}

		asoc->strreset_outstanding--;
		asoc->strreset_outseq++;

		if (!asoc->strreset_outstanding) {
			struct sctp_transport *t;

			t = asoc->strreset_chunk->transport;
			if (del_timer(&t->reconf_timer))
				sctp_transport_put(t);

			sctp_chunk_put(asoc->strreset_chunk);
			asoc->strreset_chunk = NULL;
		}
	}

	stream->incnt = incnt;

	result = SCTP_STRRESET_PERFORMED;

	*evp = sctp_ulpevent_make_stream_change_event(asoc,
		0, ntohs(addstrm->number_of_streams), 0, GFP_ATOMIC);

out:
	sctp_update_strreset_result(asoc, result);
err:
	return sctp_make_strreset_resp(asoc, result, request_seq);
}

struct sctp_chunk *sctp_process_strreset_addstrm_in(
				struct sctp_association *asoc,
				union sctp_params param,
				struct sctp_ulpevent **evp)
{
	struct sctp_strreset_addstrm *addstrm = param.v;
	struct sctp_stream *stream = &asoc->stream;
	__u32 result = SCTP_STRRESET_DENIED;
	struct sctp_chunk *chunk = NULL;
	__u32 request_seq, outcnt;
	__u16 out, i;
	int ret;

	request_seq = ntohl(addstrm->request_seq);
	if (TSN_lt(asoc->strreset_inseq, request_seq) ||
	    TSN_lt(request_seq, asoc->strreset_inseq - 2)) {
		result = SCTP_STRRESET_ERR_BAD_SEQNO;
		goto err;
	} else if (TSN_lt(request_seq, asoc->strreset_inseq)) {
		i = asoc->strreset_inseq - request_seq - 1;
		result = asoc->strreset_result[i];
		if (result == SCTP_STRRESET_PERFORMED)
			return NULL;
		goto err;
	}
	asoc->strreset_inseq++;

	if (!(asoc->strreset_enable & SCTP_ENABLE_CHANGE_ASSOC_REQ))
		goto out;

	if (asoc->strreset_outstanding) {
		result = SCTP_STRRESET_ERR_IN_PROGRESS;
		goto out;
	}

	out = ntohs(addstrm->number_of_streams);
	outcnt = stream->outcnt + out;
	if (!out || outcnt > SCTP_MAX_STREAM)
		goto out;

	ret = sctp_stream_alloc_out(stream, outcnt, GFP_ATOMIC);
	if (ret)
		goto out;

	chunk = sctp_make_strreset_addstrm(asoc, out, 0);
	if (!chunk)
		goto out;

	asoc->strreset_chunk = chunk;
	asoc->strreset_outstanding = 1;
	sctp_chunk_hold(asoc->strreset_chunk);

	stream->outcnt = outcnt;

	result = SCTP_STRRESET_PERFORMED;

out:
	sctp_update_strreset_result(asoc, result);
err:
	if (!chunk)
		chunk = sctp_make_strreset_resp(asoc, result, request_seq);

	return chunk;
}

struct sctp_chunk *sctp_process_strreset_resp(
				struct sctp_association *asoc,
				union sctp_params param,
				struct sctp_ulpevent **evp)
{
	struct sctp_stream *stream = &asoc->stream;
	struct sctp_strreset_resp *resp = param.v;
	struct sctp_transport *t;
	__u16 i, nums, flags = 0;
	struct sctp_paramhdr *req;
	__u32 result;

	req = sctp_chunk_lookup_strreset_param(asoc, resp->response_seq, 0);
	if (!req)
		return NULL;

	result = ntohl(resp->result);
	if (result != SCTP_STRRESET_PERFORMED) {
		/* if in progress, do nothing but retransmit */
		if (result == SCTP_STRRESET_IN_PROGRESS)
			return NULL;
		else if (result == SCTP_STRRESET_DENIED)
			flags = SCTP_STREAM_RESET_DENIED;
		else
			flags = SCTP_STREAM_RESET_FAILED;
	}

	if (req->type == SCTP_PARAM_RESET_OUT_REQUEST) {
		struct sctp_strreset_outreq *outreq;
		__be16 *str_p;

		outreq = (struct sctp_strreset_outreq *)req;
		str_p = outreq->list_of_streams;
		nums = (ntohs(outreq->param_hdr.length) - sizeof(*outreq)) /
		       sizeof(__u16);

		if (result == SCTP_STRRESET_PERFORMED) {
			struct sctp_stream_out *sout;
			if (nums) {
				for (i = 0; i < nums; i++) {
					sout = SCTP_SO(stream, ntohs(str_p[i]));
					sout->mid = 0;
					sout->mid_uo = 0;
				}
			} else {
				for (i = 0; i < stream->outcnt; i++) {
					sout = SCTP_SO(stream, i);
					sout->mid = 0;
					sout->mid_uo = 0;
				}
			}
		}

		flags |= SCTP_STREAM_RESET_OUTGOING_SSN;

		for (i = 0; i < stream->outcnt; i++)
			SCTP_SO(stream, i)->state = SCTP_STREAM_OPEN;

		*evp = sctp_ulpevent_make_stream_reset_event(asoc, flags,
			nums, str_p, GFP_ATOMIC);
	} else if (req->type == SCTP_PARAM_RESET_IN_REQUEST) {
		struct sctp_strreset_inreq *inreq;
		__be16 *str_p;

		/* if the result is performed, it's impossible for inreq */
		if (result == SCTP_STRRESET_PERFORMED)
			return NULL;

		inreq = (struct sctp_strreset_inreq *)req;
		str_p = inreq->list_of_streams;
		nums = (ntohs(inreq->param_hdr.length) - sizeof(*inreq)) /
		       sizeof(__u16);

		flags |= SCTP_STREAM_RESET_INCOMING_SSN;

		*evp = sctp_ulpevent_make_stream_reset_event(asoc, flags,
			nums, str_p, GFP_ATOMIC);
	} else if (req->type == SCTP_PARAM_RESET_TSN_REQUEST) {
		struct sctp_strreset_resptsn *resptsn;
		__u32 stsn, rtsn;

		/* check for resptsn, as sctp_verify_reconf didn't do it*/
		if (ntohs(param.p->length) != sizeof(*resptsn))
			return NULL;

		resptsn = (struct sctp_strreset_resptsn *)resp;
		stsn = ntohl(resptsn->senders_next_tsn);
		rtsn = ntohl(resptsn->receivers_next_tsn);

		if (result == SCTP_STRRESET_PERFORMED) {
			__u32 mtsn = sctp_tsnmap_get_max_tsn_seen(
						&asoc->peer.tsn_map);
			LIST_HEAD(temp);

			asoc->stream.si->report_ftsn(&asoc->ulpq, mtsn);

			sctp_tsnmap_init(&asoc->peer.tsn_map,
					 SCTP_TSN_MAP_INITIAL,
					 stsn, GFP_ATOMIC);

			/* Clean up sacked and abandoned queues only. As the
			 * out_chunk_list may not be empty, splice it to temp,
			 * then get it back after sctp_outq_free is done.
			 */
			list_splice_init(&asoc->outqueue.out_chunk_list, &temp);
			sctp_outq_free(&asoc->outqueue);
			list_splice_init(&temp, &asoc->outqueue.out_chunk_list);

			asoc->next_tsn = rtsn;
			asoc->ctsn_ack_point = asoc->next_tsn - 1;
			asoc->adv_peer_ack_point = asoc->ctsn_ack_point;

			for (i = 0; i < stream->outcnt; i++) {
				SCTP_SO(stream, i)->mid = 0;
				SCTP_SO(stream, i)->mid_uo = 0;
			}
			for (i = 0; i < stream->incnt; i++)
				SCTP_SI(stream, i)->mid = 0;
		}

		for (i = 0; i < stream->outcnt; i++)
			SCTP_SO(stream, i)->state = SCTP_STREAM_OPEN;

		*evp = sctp_ulpevent_make_assoc_reset_event(asoc, flags,
			stsn, rtsn, GFP_ATOMIC);
	} else if (req->type == SCTP_PARAM_RESET_ADD_OUT_STREAMS) {
		struct sctp_strreset_addstrm *addstrm;
		__u16 number;

		addstrm = (struct sctp_strreset_addstrm *)req;
		nums = ntohs(addstrm->number_of_streams);
		number = stream->outcnt - nums;

		if (result == SCTP_STRRESET_PERFORMED)
			for (i = number; i < stream->outcnt; i++)
				SCTP_SO(stream, i)->state = SCTP_STREAM_OPEN;
		else
			stream->outcnt = number;

		*evp = sctp_ulpevent_make_stream_change_event(asoc, flags,
			0, nums, GFP_ATOMIC);
	} else if (req->type == SCTP_PARAM_RESET_ADD_IN_STREAMS) {
		struct sctp_strreset_addstrm *addstrm;

		/* if the result is performed, it's impossible for addstrm in
		 * request.
		 */
		if (result == SCTP_STRRESET_PERFORMED)
			return NULL;

		addstrm = (struct sctp_strreset_addstrm *)req;
		nums = ntohs(addstrm->number_of_streams);

		*evp = sctp_ulpevent_make_stream_change_event(asoc, flags,
			nums, 0, GFP_ATOMIC);
	}

	asoc->strreset_outstanding--;
	asoc->strreset_outseq++;

	/* remove everything for this reconf request */
	if (!asoc->strreset_outstanding) {
		t = asoc->strreset_chunk->transport;
		if (del_timer(&t->reconf_timer))
			sctp_transport_put(t);

		sctp_chunk_put(asoc->strreset_chunk);
		asoc->strreset_chunk = NULL;
	}

	return NULL;
}
