/* SCTP kernel implementation
 * (C) Copyright Red Hat Inc. 2017
 *
 * This file is part of the SCTP kernel implementation
 *
 * These functions manipulate sctp stream queue/scheduling.
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
 * email addresched(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    Xin Long <lucien.xin@gmail.com>
 */

#include <net/busy_poll.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>
#include <net/sctp/ulpevent.h>
#include <linux/sctp.h>

static struct sctp_chunk *sctp_make_idatafrag_empty(
					const struct sctp_association *asoc,
					const struct sctp_sndrcvinfo *sinfo,
					int len, __u8 flags, gfp_t gfp)
{
	struct sctp_chunk *retval;
	struct sctp_idatahdr dp;

	memset(&dp, 0, sizeof(dp));
	dp.stream = htons(sinfo->sinfo_stream);

	if (sinfo->sinfo_flags & SCTP_UNORDERED)
		flags |= SCTP_DATA_UNORDERED;

	retval = sctp_make_idata(asoc, flags, sizeof(dp) + len, gfp);
	if (!retval)
		return NULL;

	retval->subh.idata_hdr = sctp_addto_chunk(retval, sizeof(dp), &dp);
	memcpy(&retval->sinfo, sinfo, sizeof(struct sctp_sndrcvinfo));

	return retval;
}

static void sctp_chunk_assign_mid(struct sctp_chunk *chunk)
{
	struct sctp_stream *stream;
	struct sctp_chunk *lchunk;
	__u32 cfsn = 0;
	__u16 sid;

	if (chunk->has_mid)
		return;

	sid = sctp_chunk_stream_no(chunk);
	stream = &chunk->asoc->stream;

	list_for_each_entry(lchunk, &chunk->msg->chunks, frag_list) {
		struct sctp_idatahdr *hdr;

		lchunk->has_mid = 1;

		if (lchunk->chunk_hdr->flags & SCTP_DATA_UNORDERED)
			continue;

		hdr = lchunk->subh.idata_hdr;

		if (lchunk->chunk_hdr->flags & SCTP_DATA_FIRST_FRAG)
			hdr->ppid = lchunk->sinfo.sinfo_ppid;
		else
			hdr->fsn = htonl(cfsn++);

		if (lchunk->chunk_hdr->flags & SCTP_DATA_LAST_FRAG)
			hdr->mid = htonl(sctp_mid_next(stream, out, sid));
		else
			hdr->mid = htonl(sctp_mid_peek(stream, out, sid));
	}
}

static bool sctp_validate_data(struct sctp_chunk *chunk)
{
	const struct sctp_stream *stream;
	__u16 sid, ssn;

	if (chunk->chunk_hdr->type != SCTP_CID_DATA)
		return false;

	if (chunk->chunk_hdr->flags & SCTP_DATA_UNORDERED)
		return true;

	stream = &chunk->asoc->stream;
	sid = sctp_chunk_stream_no(chunk);
	ssn = ntohs(chunk->subh.data_hdr->ssn);

	return !SSN_lt(ssn, sctp_ssn_peek(stream, in, sid));
}

static bool sctp_validate_idata(struct sctp_chunk *chunk)
{
	struct sctp_stream *stream;
	__u32 mid;
	__u16 sid;

	if (chunk->chunk_hdr->type != SCTP_CID_I_DATA)
		return false;

	if (chunk->chunk_hdr->flags & SCTP_DATA_UNORDERED)
		return true;

	stream = &chunk->asoc->stream;
	sid = sctp_chunk_stream_no(chunk);
	mid = ntohl(chunk->subh.idata_hdr->mid);

	return !MID_lt(mid, sctp_mid_peek(stream, in, sid));
}

static void sctp_intl_store_reasm(struct sctp_ulpq *ulpq,
				  struct sctp_ulpevent *event)
{
	struct sctp_ulpevent *cevent;
	struct sk_buff *pos;

	pos = skb_peek_tail(&ulpq->reasm);
	if (!pos) {
		__skb_queue_tail(&ulpq->reasm, sctp_event2skb(event));
		return;
	}

	cevent = sctp_skb2event(pos);

	if (event->stream == cevent->stream &&
	    event->mid == cevent->mid &&
	    (cevent->msg_flags & SCTP_DATA_FIRST_FRAG ||
	     (!(event->msg_flags & SCTP_DATA_FIRST_FRAG) &&
	      event->fsn > cevent->fsn))) {
		__skb_queue_tail(&ulpq->reasm, sctp_event2skb(event));
		return;
	}

	if ((event->stream == cevent->stream &&
	     MID_lt(cevent->mid, event->mid)) ||
	    event->stream > cevent->stream) {
		__skb_queue_tail(&ulpq->reasm, sctp_event2skb(event));
		return;
	}

	skb_queue_walk(&ulpq->reasm, pos) {
		cevent = sctp_skb2event(pos);

		if (event->stream < cevent->stream ||
		    (event->stream == cevent->stream &&
		     MID_lt(event->mid, cevent->mid)))
			break;

		if (event->stream == cevent->stream &&
		    event->mid == cevent->mid &&
		    !(cevent->msg_flags & SCTP_DATA_FIRST_FRAG) &&
		    (event->msg_flags & SCTP_DATA_FIRST_FRAG ||
		     event->fsn < cevent->fsn))
			break;
	}

	__skb_queue_before(&ulpq->reasm, pos, sctp_event2skb(event));
}

static struct sctp_ulpevent *sctp_intl_retrieve_partial(
						struct sctp_ulpq *ulpq,
						struct sctp_ulpevent *event)
{
	struct sk_buff *first_frag = NULL;
	struct sk_buff *last_frag = NULL;
	struct sctp_ulpevent *retval;
	struct sctp_stream_in *sin;
	struct sk_buff *pos;
	__u32 next_fsn = 0;
	int is_last = 0;

	sin = sctp_stream_in(ulpq->asoc, event->stream);

	skb_queue_walk(&ulpq->reasm, pos) {
		struct sctp_ulpevent *cevent = sctp_skb2event(pos);

		if (cevent->stream < event->stream)
			continue;

		if (cevent->stream > event->stream ||
		    cevent->mid != sin->mid)
			break;

		switch (cevent->msg_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_FIRST_FRAG:
			goto out;
		case SCTP_DATA_MIDDLE_FRAG:
			if (!first_frag) {
				if (cevent->fsn == sin->fsn) {
					first_frag = pos;
					last_frag = pos;
					next_fsn = cevent->fsn + 1;
				}
			} else if (cevent->fsn == next_fsn) {
				last_frag = pos;
				next_fsn++;
			} else {
				goto out;
			}
			break;
		case SCTP_DATA_LAST_FRAG:
			if (!first_frag) {
				if (cevent->fsn == sin->fsn) {
					first_frag = pos;
					last_frag = pos;
					next_fsn = 0;
					is_last = 1;
				}
			} else if (cevent->fsn == next_fsn) {
				last_frag = pos;
				next_fsn = 0;
				is_last = 1;
			}
			goto out;
		default:
			goto out;
		}
	}

out:
	if (!first_frag)
		return NULL;

	retval = sctp_make_reassembled_event(sock_net(ulpq->asoc->base.sk),
					     &ulpq->reasm, first_frag,
					     last_frag);
	if (retval) {
		sin->fsn = next_fsn;
		if (is_last) {
			retval->msg_flags |= MSG_EOR;
			sin->pd_mode = 0;
		}
	}

	return retval;
}

static struct sctp_ulpevent *sctp_intl_retrieve_reassembled(
						struct sctp_ulpq *ulpq,
						struct sctp_ulpevent *event)
{
	struct sctp_association *asoc = ulpq->asoc;
	struct sk_buff *pos, *first_frag = NULL;
	struct sctp_ulpevent *retval = NULL;
	struct sk_buff *pd_first = NULL;
	struct sk_buff *pd_last = NULL;
	struct sctp_stream_in *sin;
	__u32 next_fsn = 0;
	__u32 pd_point = 0;
	__u32 pd_len = 0;
	__u32 mid = 0;

	sin = sctp_stream_in(ulpq->asoc, event->stream);

	skb_queue_walk(&ulpq->reasm, pos) {
		struct sctp_ulpevent *cevent = sctp_skb2event(pos);

		if (cevent->stream < event->stream)
			continue;
		if (cevent->stream > event->stream)
			break;

		if (MID_lt(cevent->mid, event->mid))
			continue;
		if (MID_lt(event->mid, cevent->mid))
			break;

		switch (cevent->msg_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_FIRST_FRAG:
			if (cevent->mid == sin->mid) {
				pd_first = pos;
				pd_last = pos;
				pd_len = pos->len;
			}

			first_frag = pos;
			next_fsn = 0;
			mid = cevent->mid;
			break;

		case SCTP_DATA_MIDDLE_FRAG:
			if (first_frag && cevent->mid == mid &&
			    cevent->fsn == next_fsn) {
				next_fsn++;
				if (pd_first) {
					pd_last = pos;
					pd_len += pos->len;
				}
			} else {
				first_frag = NULL;
			}
			break;

		case SCTP_DATA_LAST_FRAG:
			if (first_frag && cevent->mid == mid &&
			    cevent->fsn == next_fsn)
				goto found;
			else
				first_frag = NULL;
			break;
		}
	}

	if (!pd_first)
		goto out;

	pd_point = sctp_sk(asoc->base.sk)->pd_point;
	if (pd_point && pd_point <= pd_len) {
		retval = sctp_make_reassembled_event(sock_net(asoc->base.sk),
						     &ulpq->reasm,
						     pd_first, pd_last);
		if (retval) {
			sin->fsn = next_fsn;
			sin->pd_mode = 1;
		}
	}
	goto out;

found:
	retval = sctp_make_reassembled_event(sock_net(asoc->base.sk),
					     &ulpq->reasm,
					     first_frag, pos);
	if (retval)
		retval->msg_flags |= MSG_EOR;

out:
	return retval;
}

static struct sctp_ulpevent *sctp_intl_reasm(struct sctp_ulpq *ulpq,
					     struct sctp_ulpevent *event)
{
	struct sctp_ulpevent *retval = NULL;
	struct sctp_stream_in *sin;

	if (SCTP_DATA_NOT_FRAG == (event->msg_flags & SCTP_DATA_FRAG_MASK)) {
		event->msg_flags |= MSG_EOR;
		return event;
	}

	sctp_intl_store_reasm(ulpq, event);

	sin = sctp_stream_in(ulpq->asoc, event->stream);
	if (sin->pd_mode && event->mid == sin->mid &&
	    event->fsn == sin->fsn)
		retval = sctp_intl_retrieve_partial(ulpq, event);

	if (!retval)
		retval = sctp_intl_retrieve_reassembled(ulpq, event);

	return retval;
}

static void sctp_intl_store_ordered(struct sctp_ulpq *ulpq,
				    struct sctp_ulpevent *event)
{
	struct sctp_ulpevent *cevent;
	struct sk_buff *pos;

	pos = skb_peek_tail(&ulpq->lobby);
	if (!pos) {
		__skb_queue_tail(&ulpq->lobby, sctp_event2skb(event));
		return;
	}

	cevent = (struct sctp_ulpevent *)pos->cb;
	if (event->stream == cevent->stream &&
	    MID_lt(cevent->mid, event->mid)) {
		__skb_queue_tail(&ulpq->lobby, sctp_event2skb(event));
		return;
	}

	if (event->stream > cevent->stream) {
		__skb_queue_tail(&ulpq->lobby, sctp_event2skb(event));
		return;
	}

	skb_queue_walk(&ulpq->lobby, pos) {
		cevent = (struct sctp_ulpevent *)pos->cb;

		if (cevent->stream > event->stream)
			break;

		if (cevent->stream == event->stream &&
		    MID_lt(event->mid, cevent->mid))
			break;
	}

	__skb_queue_before(&ulpq->lobby, pos, sctp_event2skb(event));
}

static void sctp_intl_retrieve_ordered(struct sctp_ulpq *ulpq,
				       struct sctp_ulpevent *event)
{
	struct sk_buff_head *event_list;
	struct sctp_stream *stream;
	struct sk_buff *pos, *tmp;
	__u16 sid = event->stream;

	stream  = &ulpq->asoc->stream;
	event_list = (struct sk_buff_head *)sctp_event2skb(event)->prev;

	sctp_skb_for_each(pos, &ulpq->lobby, tmp) {
		struct sctp_ulpevent *cevent = (struct sctp_ulpevent *)pos->cb;

		if (cevent->stream > sid)
			break;

		if (cevent->stream < sid)
			continue;

		if (cevent->mid != sctp_mid_peek(stream, in, sid))
			break;

		sctp_mid_next(stream, in, sid);

		__skb_unlink(pos, &ulpq->lobby);

		__skb_queue_tail(event_list, pos);
	}
}

static struct sctp_ulpevent *sctp_intl_order(struct sctp_ulpq *ulpq,
					     struct sctp_ulpevent *event)
{
	struct sctp_stream *stream;
	__u16 sid;

	if (event->msg_flags & SCTP_DATA_UNORDERED)
		return event;

	stream  = &ulpq->asoc->stream;
	sid = event->stream;

	if (event->mid != sctp_mid_peek(stream, in, sid)) {
		sctp_intl_store_ordered(ulpq, event);
		return NULL;
	}

	sctp_mid_next(stream, in, sid);

	sctp_intl_retrieve_ordered(ulpq, event);

	return event;
}

static int sctp_enqueue_event(struct sctp_ulpq *ulpq,
			      struct sctp_ulpevent *event)
{
	struct sk_buff *skb = sctp_event2skb(event);
	struct sock *sk = ulpq->asoc->base.sk;
	struct sctp_sock *sp = sctp_sk(sk);
	struct sk_buff_head *skb_list;

	skb_list = (struct sk_buff_head *)skb->prev;

	if (sk->sk_shutdown & RCV_SHUTDOWN &&
	    (sk->sk_shutdown & SEND_SHUTDOWN ||
	     !sctp_ulpevent_is_notification(event)))
		goto out_free;

	if (!sctp_ulpevent_is_notification(event)) {
		sk_mark_napi_id(sk, skb);
		sk_incoming_cpu_update(sk);
	}

	if (!sctp_ulpevent_is_enabled(event, &sp->subscribe))
		goto out_free;

	if (skb_list)
		skb_queue_splice_tail_init(skb_list,
					   &sk->sk_receive_queue);
	else
		__skb_queue_tail(&sk->sk_receive_queue, skb);

	if (!sp->data_ready_signalled) {
		sp->data_ready_signalled = 1;
		sk->sk_data_ready(sk);
	}

	return 1;

out_free:
	if (skb_list)
		sctp_queue_purge_ulpevents(skb_list);
	else
		sctp_ulpevent_free(event);

	return 0;
}

static int sctp_ulpevent_idata(struct sctp_ulpq *ulpq,
			       struct sctp_chunk *chunk, gfp_t gfp)
{
	struct sctp_ulpevent *event;
	struct sk_buff_head temp;
	int event_eor = 0;

	event = sctp_ulpevent_make_rcvmsg(chunk->asoc, chunk, gfp);
	if (!event)
		return -ENOMEM;

	event->mid = ntohl(chunk->subh.idata_hdr->mid);
	if (event->msg_flags & SCTP_DATA_FIRST_FRAG)
		event->ppid = chunk->subh.idata_hdr->ppid;
	else
		event->fsn = ntohl(chunk->subh.idata_hdr->fsn);

	event = sctp_intl_reasm(ulpq, event);
	if (event && event->msg_flags & MSG_EOR) {
		skb_queue_head_init(&temp);
		__skb_queue_tail(&temp, sctp_event2skb(event));

		event = sctp_intl_order(ulpq, event);
	}

	if (event) {
		event_eor = (event->msg_flags & MSG_EOR) ? 1 : 0;
		sctp_enqueue_event(ulpq, event);
	}

	return event_eor;
}

static struct sctp_stream_interleave sctp_stream_interleave_0 = {
	.data_chunk_len		= sizeof(struct sctp_data_chunk),
	/* DATA process functions */
	.make_datafrag		= sctp_make_datafrag_empty,
	.assign_number		= sctp_chunk_assign_ssn,
	.validate_data		= sctp_validate_data,
	.ulpevent_data		= sctp_ulpq_tail_data,
	.enqueue_event		= sctp_ulpq_tail_event,
};

static struct sctp_stream_interleave sctp_stream_interleave_1 = {
	.data_chunk_len		= sizeof(struct sctp_idata_chunk),
	/* I-DATA process functions */
	.make_datafrag		= sctp_make_idatafrag_empty,
	.assign_number		= sctp_chunk_assign_mid,
	.validate_data		= sctp_validate_idata,
	.ulpevent_data		= sctp_ulpevent_idata,
	.enqueue_event		= sctp_enqueue_event,
};

void sctp_stream_interleave_init(struct sctp_stream *stream)
{
	struct sctp_association *asoc;

	asoc = container_of(stream, struct sctp_association, stream);
	stream->si = asoc->intl_enable ? &sctp_stream_interleave_1
				       : &sctp_stream_interleave_0;
}
