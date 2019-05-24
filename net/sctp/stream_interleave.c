/* SCTP kernel implementation
 * (C) Copyright Red Hat Inc. 2017
 *
 * This file is part of the SCTP kernel implementation
 *
 * These functions implement sctp stream message interleaving, mostly
 * including I-DATA and I-FORWARD-TSN chunks process.
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
		__u32 mid;

		lchunk->has_mid = 1;

		hdr = lchunk->subh.idata_hdr;

		if (lchunk->chunk_hdr->flags & SCTP_DATA_FIRST_FRAG)
			hdr->ppid = lchunk->sinfo.sinfo_ppid;
		else
			hdr->fsn = htonl(cfsn++);

		if (lchunk->chunk_hdr->flags & SCTP_DATA_UNORDERED) {
			mid = lchunk->chunk_hdr->flags & SCTP_DATA_LAST_FRAG ?
				sctp_mid_uo_next(stream, out, sid) :
				sctp_mid_uo_peek(stream, out, sid);
		} else {
			mid = lchunk->chunk_hdr->flags & SCTP_DATA_LAST_FRAG ?
				sctp_mid_next(stream, out, sid) :
				sctp_mid_peek(stream, out, sid);
		}
		hdr->mid = htonl(mid);
	}
}

static bool sctp_validate_data(struct sctp_chunk *chunk)
{
	struct sctp_stream *stream;
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
	struct sk_buff *pos, *loc;

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

	loc = NULL;
	skb_queue_walk(&ulpq->reasm, pos) {
		cevent = sctp_skb2event(pos);

		if (event->stream < cevent->stream ||
		    (event->stream == cevent->stream &&
		     MID_lt(event->mid, cevent->mid))) {
			loc = pos;
			break;
		}
		if (event->stream == cevent->stream &&
		    event->mid == cevent->mid &&
		    !(cevent->msg_flags & SCTP_DATA_FIRST_FRAG) &&
		    (event->msg_flags & SCTP_DATA_FIRST_FRAG ||
		     event->fsn < cevent->fsn)) {
			loc = pos;
			break;
		}
	}

	if (!loc)
		__skb_queue_tail(&ulpq->reasm, sctp_event2skb(event));
	else
		__skb_queue_before(&ulpq->reasm, loc, sctp_event2skb(event));
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

	sin = sctp_stream_in(&ulpq->asoc->stream, event->stream);

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

	sin = sctp_stream_in(&ulpq->asoc->stream, event->stream);

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

	sin = sctp_stream_in(&ulpq->asoc->stream, event->stream);
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
	struct sk_buff *pos, *loc;

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

	loc = NULL;
	skb_queue_walk(&ulpq->lobby, pos) {
		cevent = (struct sctp_ulpevent *)pos->cb;

		if (cevent->stream > event->stream) {
			loc = pos;
			break;
		}
		if (cevent->stream == event->stream &&
		    MID_lt(event->mid, cevent->mid)) {
			loc = pos;
			break;
		}
	}

	if (!loc)
		__skb_queue_tail(&ulpq->lobby, sctp_event2skb(event));
	else
		__skb_queue_before(&ulpq->lobby, loc, sctp_event2skb(event));
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
			      struct sk_buff_head *skb_list)
{
	struct sock *sk = ulpq->asoc->base.sk;
	struct sctp_sock *sp = sctp_sk(sk);
	struct sctp_ulpevent *event;
	struct sk_buff *skb;

	skb = __skb_peek(skb_list);
	event = sctp_skb2event(skb);

	if (sk->sk_shutdown & RCV_SHUTDOWN &&
	    (sk->sk_shutdown & SEND_SHUTDOWN ||
	     !sctp_ulpevent_is_notification(event)))
		goto out_free;

	if (!sctp_ulpevent_is_notification(event)) {
		sk_mark_napi_id(sk, skb);
		sk_incoming_cpu_update(sk);
	}

	if (!sctp_ulpevent_is_enabled(event, ulpq->asoc->subscribe))
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

static void sctp_intl_store_reasm_uo(struct sctp_ulpq *ulpq,
				     struct sctp_ulpevent *event)
{
	struct sctp_ulpevent *cevent;
	struct sk_buff *pos;

	pos = skb_peek_tail(&ulpq->reasm_uo);
	if (!pos) {
		__skb_queue_tail(&ulpq->reasm_uo, sctp_event2skb(event));
		return;
	}

	cevent = sctp_skb2event(pos);

	if (event->stream == cevent->stream &&
	    event->mid == cevent->mid &&
	    (cevent->msg_flags & SCTP_DATA_FIRST_FRAG ||
	     (!(event->msg_flags & SCTP_DATA_FIRST_FRAG) &&
	      event->fsn > cevent->fsn))) {
		__skb_queue_tail(&ulpq->reasm_uo, sctp_event2skb(event));
		return;
	}

	if ((event->stream == cevent->stream &&
	     MID_lt(cevent->mid, event->mid)) ||
	    event->stream > cevent->stream) {
		__skb_queue_tail(&ulpq->reasm_uo, sctp_event2skb(event));
		return;
	}

	skb_queue_walk(&ulpq->reasm_uo, pos) {
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

	__skb_queue_before(&ulpq->reasm_uo, pos, sctp_event2skb(event));
}

static struct sctp_ulpevent *sctp_intl_retrieve_partial_uo(
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

	sin = sctp_stream_in(&ulpq->asoc->stream, event->stream);

	skb_queue_walk(&ulpq->reasm_uo, pos) {
		struct sctp_ulpevent *cevent = sctp_skb2event(pos);

		if (cevent->stream < event->stream)
			continue;
		if (cevent->stream > event->stream)
			break;

		if (MID_lt(cevent->mid, sin->mid_uo))
			continue;
		if (MID_lt(sin->mid_uo, cevent->mid))
			break;

		switch (cevent->msg_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_FIRST_FRAG:
			goto out;
		case SCTP_DATA_MIDDLE_FRAG:
			if (!first_frag) {
				if (cevent->fsn == sin->fsn_uo) {
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
				if (cevent->fsn == sin->fsn_uo) {
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
					     &ulpq->reasm_uo, first_frag,
					     last_frag);
	if (retval) {
		sin->fsn_uo = next_fsn;
		if (is_last) {
			retval->msg_flags |= MSG_EOR;
			sin->pd_mode_uo = 0;
		}
	}

	return retval;
}

static struct sctp_ulpevent *sctp_intl_retrieve_reassembled_uo(
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

	sin = sctp_stream_in(&ulpq->asoc->stream, event->stream);

	skb_queue_walk(&ulpq->reasm_uo, pos) {
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
			if (!sin->pd_mode_uo) {
				sin->mid_uo = cevent->mid;
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
						     &ulpq->reasm_uo,
						     pd_first, pd_last);
		if (retval) {
			sin->fsn_uo = next_fsn;
			sin->pd_mode_uo = 1;
		}
	}
	goto out;

found:
	retval = sctp_make_reassembled_event(sock_net(asoc->base.sk),
					     &ulpq->reasm_uo,
					     first_frag, pos);
	if (retval)
		retval->msg_flags |= MSG_EOR;

out:
	return retval;
}

static struct sctp_ulpevent *sctp_intl_reasm_uo(struct sctp_ulpq *ulpq,
						struct sctp_ulpevent *event)
{
	struct sctp_ulpevent *retval = NULL;
	struct sctp_stream_in *sin;

	if (SCTP_DATA_NOT_FRAG == (event->msg_flags & SCTP_DATA_FRAG_MASK)) {
		event->msg_flags |= MSG_EOR;
		return event;
	}

	sctp_intl_store_reasm_uo(ulpq, event);

	sin = sctp_stream_in(&ulpq->asoc->stream, event->stream);
	if (sin->pd_mode_uo && event->mid == sin->mid_uo &&
	    event->fsn == sin->fsn_uo)
		retval = sctp_intl_retrieve_partial_uo(ulpq, event);

	if (!retval)
		retval = sctp_intl_retrieve_reassembled_uo(ulpq, event);

	return retval;
}

static struct sctp_ulpevent *sctp_intl_retrieve_first_uo(struct sctp_ulpq *ulpq)
{
	struct sctp_stream_in *csin, *sin = NULL;
	struct sk_buff *first_frag = NULL;
	struct sk_buff *last_frag = NULL;
	struct sctp_ulpevent *retval;
	struct sk_buff *pos;
	__u32 next_fsn = 0;
	__u16 sid = 0;

	skb_queue_walk(&ulpq->reasm_uo, pos) {
		struct sctp_ulpevent *cevent = sctp_skb2event(pos);

		csin = sctp_stream_in(&ulpq->asoc->stream, cevent->stream);
		if (csin->pd_mode_uo)
			continue;

		switch (cevent->msg_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_FIRST_FRAG:
			if (first_frag)
				goto out;
			first_frag = pos;
			last_frag = pos;
			next_fsn = 0;
			sin = csin;
			sid = cevent->stream;
			sin->mid_uo = cevent->mid;
			break;
		case SCTP_DATA_MIDDLE_FRAG:
			if (!first_frag)
				break;
			if (cevent->stream == sid &&
			    cevent->mid == sin->mid_uo &&
			    cevent->fsn == next_fsn) {
				next_fsn++;
				last_frag = pos;
			} else {
				goto out;
			}
			break;
		case SCTP_DATA_LAST_FRAG:
			if (first_frag)
				goto out;
			break;
		default:
			break;
		}
	}

	if (!first_frag)
		return NULL;

out:
	retval = sctp_make_reassembled_event(sock_net(ulpq->asoc->base.sk),
					     &ulpq->reasm_uo, first_frag,
					     last_frag);
	if (retval) {
		sin->fsn_uo = next_fsn;
		sin->pd_mode_uo = 1;
	}

	return retval;
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

	if (!(event->msg_flags & SCTP_DATA_UNORDERED)) {
		event = sctp_intl_reasm(ulpq, event);
		if (event) {
			skb_queue_head_init(&temp);
			__skb_queue_tail(&temp, sctp_event2skb(event));

			if (event->msg_flags & MSG_EOR)
				event = sctp_intl_order(ulpq, event);
		}
	} else {
		event = sctp_intl_reasm_uo(ulpq, event);
		if (event) {
			skb_queue_head_init(&temp);
			__skb_queue_tail(&temp, sctp_event2skb(event));
		}
	}

	if (event) {
		event_eor = (event->msg_flags & MSG_EOR) ? 1 : 0;
		sctp_enqueue_event(ulpq, &temp);
	}

	return event_eor;
}

static struct sctp_ulpevent *sctp_intl_retrieve_first(struct sctp_ulpq *ulpq)
{
	struct sctp_stream_in *csin, *sin = NULL;
	struct sk_buff *first_frag = NULL;
	struct sk_buff *last_frag = NULL;
	struct sctp_ulpevent *retval;
	struct sk_buff *pos;
	__u32 next_fsn = 0;
	__u16 sid = 0;

	skb_queue_walk(&ulpq->reasm, pos) {
		struct sctp_ulpevent *cevent = sctp_skb2event(pos);

		csin = sctp_stream_in(&ulpq->asoc->stream, cevent->stream);
		if (csin->pd_mode)
			continue;

		switch (cevent->msg_flags & SCTP_DATA_FRAG_MASK) {
		case SCTP_DATA_FIRST_FRAG:
			if (first_frag)
				goto out;
			if (cevent->mid == csin->mid) {
				first_frag = pos;
				last_frag = pos;
				next_fsn = 0;
				sin = csin;
				sid = cevent->stream;
			}
			break;
		case SCTP_DATA_MIDDLE_FRAG:
			if (!first_frag)
				break;
			if (cevent->stream == sid &&
			    cevent->mid == sin->mid &&
			    cevent->fsn == next_fsn) {
				next_fsn++;
				last_frag = pos;
			} else {
				goto out;
			}
			break;
		case SCTP_DATA_LAST_FRAG:
			if (first_frag)
				goto out;
			break;
		default:
			break;
		}
	}

	if (!first_frag)
		return NULL;

out:
	retval = sctp_make_reassembled_event(sock_net(ulpq->asoc->base.sk),
					     &ulpq->reasm, first_frag,
					     last_frag);
	if (retval) {
		sin->fsn = next_fsn;
		sin->pd_mode = 1;
	}

	return retval;
}

static void sctp_intl_start_pd(struct sctp_ulpq *ulpq, gfp_t gfp)
{
	struct sctp_ulpevent *event;
	struct sk_buff_head temp;

	if (!skb_queue_empty(&ulpq->reasm)) {
		do {
			event = sctp_intl_retrieve_first(ulpq);
			if (event) {
				skb_queue_head_init(&temp);
				__skb_queue_tail(&temp, sctp_event2skb(event));
				sctp_enqueue_event(ulpq, &temp);
			}
		} while (event);
	}

	if (!skb_queue_empty(&ulpq->reasm_uo)) {
		do {
			event = sctp_intl_retrieve_first_uo(ulpq);
			if (event) {
				skb_queue_head_init(&temp);
				__skb_queue_tail(&temp, sctp_event2skb(event));
				sctp_enqueue_event(ulpq, &temp);
			}
		} while (event);
	}
}

static void sctp_renege_events(struct sctp_ulpq *ulpq, struct sctp_chunk *chunk,
			       gfp_t gfp)
{
	struct sctp_association *asoc = ulpq->asoc;
	__u32 freed = 0;
	__u16 needed;

	needed = ntohs(chunk->chunk_hdr->length) -
		 sizeof(struct sctp_idata_chunk);

	if (skb_queue_empty(&asoc->base.sk->sk_receive_queue)) {
		freed = sctp_ulpq_renege_list(ulpq, &ulpq->lobby, needed);
		if (freed < needed)
			freed += sctp_ulpq_renege_list(ulpq, &ulpq->reasm,
						       needed);
		if (freed < needed)
			freed += sctp_ulpq_renege_list(ulpq, &ulpq->reasm_uo,
						       needed);
	}

	if (freed >= needed && sctp_ulpevent_idata(ulpq, chunk, gfp) <= 0)
		sctp_intl_start_pd(ulpq, gfp);

	sk_mem_reclaim(asoc->base.sk);
}

static void sctp_intl_stream_abort_pd(struct sctp_ulpq *ulpq, __u16 sid,
				      __u32 mid, __u16 flags, gfp_t gfp)
{
	struct sock *sk = ulpq->asoc->base.sk;
	struct sctp_ulpevent *ev = NULL;

	if (!sctp_ulpevent_type_enabled(ulpq->asoc->subscribe,
					SCTP_PARTIAL_DELIVERY_EVENT))
		return;

	ev = sctp_ulpevent_make_pdapi(ulpq->asoc, SCTP_PARTIAL_DELIVERY_ABORTED,
				      sid, mid, flags, gfp);
	if (ev) {
		struct sctp_sock *sp = sctp_sk(sk);

		__skb_queue_tail(&sk->sk_receive_queue, sctp_event2skb(ev));

		if (!sp->data_ready_signalled) {
			sp->data_ready_signalled = 1;
			sk->sk_data_ready(sk);
		}
	}
}

static void sctp_intl_reap_ordered(struct sctp_ulpq *ulpq, __u16 sid)
{
	struct sctp_stream *stream = &ulpq->asoc->stream;
	struct sctp_ulpevent *cevent, *event = NULL;
	struct sk_buff_head *lobby = &ulpq->lobby;
	struct sk_buff *pos, *tmp;
	struct sk_buff_head temp;
	__u16 csid;
	__u32 cmid;

	skb_queue_head_init(&temp);
	sctp_skb_for_each(pos, lobby, tmp) {
		cevent = (struct sctp_ulpevent *)pos->cb;
		csid = cevent->stream;
		cmid = cevent->mid;

		if (csid > sid)
			break;

		if (csid < sid)
			continue;

		if (!MID_lt(cmid, sctp_mid_peek(stream, in, csid)))
			break;

		__skb_unlink(pos, lobby);
		if (!event)
			event = sctp_skb2event(pos);

		__skb_queue_tail(&temp, pos);
	}

	if (!event && pos != (struct sk_buff *)lobby) {
		cevent = (struct sctp_ulpevent *)pos->cb;
		csid = cevent->stream;
		cmid = cevent->mid;

		if (csid == sid && cmid == sctp_mid_peek(stream, in, csid)) {
			sctp_mid_next(stream, in, csid);
			__skb_unlink(pos, lobby);
			__skb_queue_tail(&temp, pos);
			event = sctp_skb2event(pos);
		}
	}

	if (event) {
		sctp_intl_retrieve_ordered(ulpq, event);
		sctp_enqueue_event(ulpq, &temp);
	}
}

static void sctp_intl_abort_pd(struct sctp_ulpq *ulpq, gfp_t gfp)
{
	struct sctp_stream *stream = &ulpq->asoc->stream;
	__u16 sid;

	for (sid = 0; sid < stream->incnt; sid++) {
		struct sctp_stream_in *sin = SCTP_SI(stream, sid);
		__u32 mid;

		if (sin->pd_mode_uo) {
			sin->pd_mode_uo = 0;

			mid = sin->mid_uo;
			sctp_intl_stream_abort_pd(ulpq, sid, mid, 0x1, gfp);
		}

		if (sin->pd_mode) {
			sin->pd_mode = 0;

			mid = sin->mid;
			sctp_intl_stream_abort_pd(ulpq, sid, mid, 0, gfp);
			sctp_mid_skip(stream, in, sid, mid);

			sctp_intl_reap_ordered(ulpq, sid);
		}
	}

	/* intl abort pd happens only when all data needs to be cleaned */
	sctp_ulpq_flush(ulpq);
}

static inline int sctp_get_skip_pos(struct sctp_ifwdtsn_skip *skiplist,
				    int nskips, __be16 stream, __u8 flags)
{
	int i;

	for (i = 0; i < nskips; i++)
		if (skiplist[i].stream == stream &&
		    skiplist[i].flags == flags)
			return i;

	return i;
}

#define SCTP_FTSN_U_BIT	0x1
static void sctp_generate_iftsn(struct sctp_outq *q, __u32 ctsn)
{
	struct sctp_ifwdtsn_skip ftsn_skip_arr[10];
	struct sctp_association *asoc = q->asoc;
	struct sctp_chunk *ftsn_chunk = NULL;
	struct list_head *lchunk, *temp;
	int nskips = 0, skip_pos;
	struct sctp_chunk *chunk;
	__u32 tsn;

	if (!asoc->peer.prsctp_capable)
		return;

	if (TSN_lt(asoc->adv_peer_ack_point, ctsn))
		asoc->adv_peer_ack_point = ctsn;

	list_for_each_safe(lchunk, temp, &q->abandoned) {
		chunk = list_entry(lchunk, struct sctp_chunk, transmitted_list);
		tsn = ntohl(chunk->subh.data_hdr->tsn);

		if (TSN_lte(tsn, ctsn)) {
			list_del_init(lchunk);
			sctp_chunk_free(chunk);
		} else if (TSN_lte(tsn, asoc->adv_peer_ack_point + 1)) {
			__be16 sid = chunk->subh.idata_hdr->stream;
			__be32 mid = chunk->subh.idata_hdr->mid;
			__u8 flags = 0;

			if (chunk->chunk_hdr->flags & SCTP_DATA_UNORDERED)
				flags |= SCTP_FTSN_U_BIT;

			asoc->adv_peer_ack_point = tsn;
			skip_pos = sctp_get_skip_pos(&ftsn_skip_arr[0], nskips,
						     sid, flags);
			ftsn_skip_arr[skip_pos].stream = sid;
			ftsn_skip_arr[skip_pos].reserved = 0;
			ftsn_skip_arr[skip_pos].flags = flags;
			ftsn_skip_arr[skip_pos].mid = mid;
			if (skip_pos == nskips)
				nskips++;
			if (nskips == 10)
				break;
		} else {
			break;
		}
	}

	if (asoc->adv_peer_ack_point > ctsn)
		ftsn_chunk = sctp_make_ifwdtsn(asoc, asoc->adv_peer_ack_point,
					       nskips, &ftsn_skip_arr[0]);

	if (ftsn_chunk) {
		list_add_tail(&ftsn_chunk->list, &q->control_chunk_list);
		SCTP_INC_STATS(sock_net(asoc->base.sk), SCTP_MIB_OUTCTRLCHUNKS);
	}
}

#define _sctp_walk_ifwdtsn(pos, chunk, end) \
	for (pos = chunk->subh.ifwdtsn_hdr->skip; \
	     (void *)pos < (void *)chunk->subh.ifwdtsn_hdr->skip + (end); pos++)

#define sctp_walk_ifwdtsn(pos, ch) \
	_sctp_walk_ifwdtsn((pos), (ch), ntohs((ch)->chunk_hdr->length) - \
					sizeof(struct sctp_ifwdtsn_chunk))

static bool sctp_validate_fwdtsn(struct sctp_chunk *chunk)
{
	struct sctp_fwdtsn_skip *skip;
	__u16 incnt;

	if (chunk->chunk_hdr->type != SCTP_CID_FWD_TSN)
		return false;

	incnt = chunk->asoc->stream.incnt;
	sctp_walk_fwdtsn(skip, chunk)
		if (ntohs(skip->stream) >= incnt)
			return false;

	return true;
}

static bool sctp_validate_iftsn(struct sctp_chunk *chunk)
{
	struct sctp_ifwdtsn_skip *skip;
	__u16 incnt;

	if (chunk->chunk_hdr->type != SCTP_CID_I_FWD_TSN)
		return false;

	incnt = chunk->asoc->stream.incnt;
	sctp_walk_ifwdtsn(skip, chunk)
		if (ntohs(skip->stream) >= incnt)
			return false;

	return true;
}

static void sctp_report_fwdtsn(struct sctp_ulpq *ulpq, __u32 ftsn)
{
	/* Move the Cumulattive TSN Ack ahead. */
	sctp_tsnmap_skip(&ulpq->asoc->peer.tsn_map, ftsn);
	/* purge the fragmentation queue */
	sctp_ulpq_reasm_flushtsn(ulpq, ftsn);
	/* Abort any in progress partial delivery. */
	sctp_ulpq_abort_pd(ulpq, GFP_ATOMIC);
}

static void sctp_intl_reasm_flushtsn(struct sctp_ulpq *ulpq, __u32 ftsn)
{
	struct sk_buff *pos, *tmp;

	skb_queue_walk_safe(&ulpq->reasm, pos, tmp) {
		struct sctp_ulpevent *event = sctp_skb2event(pos);
		__u32 tsn = event->tsn;

		if (TSN_lte(tsn, ftsn)) {
			__skb_unlink(pos, &ulpq->reasm);
			sctp_ulpevent_free(event);
		}
	}

	skb_queue_walk_safe(&ulpq->reasm_uo, pos, tmp) {
		struct sctp_ulpevent *event = sctp_skb2event(pos);
		__u32 tsn = event->tsn;

		if (TSN_lte(tsn, ftsn)) {
			__skb_unlink(pos, &ulpq->reasm_uo);
			sctp_ulpevent_free(event);
		}
	}
}

static void sctp_report_iftsn(struct sctp_ulpq *ulpq, __u32 ftsn)
{
	/* Move the Cumulattive TSN Ack ahead. */
	sctp_tsnmap_skip(&ulpq->asoc->peer.tsn_map, ftsn);
	/* purge the fragmentation queue */
	sctp_intl_reasm_flushtsn(ulpq, ftsn);
	/* abort only when it's for all data */
	if (ftsn == sctp_tsnmap_get_max_tsn_seen(&ulpq->asoc->peer.tsn_map))
		sctp_intl_abort_pd(ulpq, GFP_ATOMIC);
}

static void sctp_handle_fwdtsn(struct sctp_ulpq *ulpq, struct sctp_chunk *chunk)
{
	struct sctp_fwdtsn_skip *skip;

	/* Walk through all the skipped SSNs */
	sctp_walk_fwdtsn(skip, chunk)
		sctp_ulpq_skip(ulpq, ntohs(skip->stream), ntohs(skip->ssn));
}

static void sctp_intl_skip(struct sctp_ulpq *ulpq, __u16 sid, __u32 mid,
			   __u8 flags)
{
	struct sctp_stream_in *sin = sctp_stream_in(&ulpq->asoc->stream, sid);
	struct sctp_stream *stream  = &ulpq->asoc->stream;

	if (flags & SCTP_FTSN_U_BIT) {
		if (sin->pd_mode_uo && MID_lt(sin->mid_uo, mid)) {
			sin->pd_mode_uo = 0;
			sctp_intl_stream_abort_pd(ulpq, sid, mid, 0x1,
						  GFP_ATOMIC);
		}
		return;
	}

	if (MID_lt(mid, sctp_mid_peek(stream, in, sid)))
		return;

	if (sin->pd_mode) {
		sin->pd_mode = 0;
		sctp_intl_stream_abort_pd(ulpq, sid, mid, 0x0, GFP_ATOMIC);
	}

	sctp_mid_skip(stream, in, sid, mid);

	sctp_intl_reap_ordered(ulpq, sid);
}

static void sctp_handle_iftsn(struct sctp_ulpq *ulpq, struct sctp_chunk *chunk)
{
	struct sctp_ifwdtsn_skip *skip;

	/* Walk through all the skipped MIDs and abort stream pd if possible */
	sctp_walk_ifwdtsn(skip, chunk)
		sctp_intl_skip(ulpq, ntohs(skip->stream),
			       ntohl(skip->mid), skip->flags);
}

static int do_ulpq_tail_event(struct sctp_ulpq *ulpq, struct sctp_ulpevent *event)
{
	struct sk_buff_head temp;

	skb_queue_head_init(&temp);
	__skb_queue_tail(&temp, sctp_event2skb(event));
	return sctp_ulpq_tail_event(ulpq, &temp);
}

static struct sctp_stream_interleave sctp_stream_interleave_0 = {
	.data_chunk_len		= sizeof(struct sctp_data_chunk),
	.ftsn_chunk_len		= sizeof(struct sctp_fwdtsn_chunk),
	/* DATA process functions */
	.make_datafrag		= sctp_make_datafrag_empty,
	.assign_number		= sctp_chunk_assign_ssn,
	.validate_data		= sctp_validate_data,
	.ulpevent_data		= sctp_ulpq_tail_data,
	.enqueue_event		= do_ulpq_tail_event,
	.renege_events		= sctp_ulpq_renege,
	.start_pd		= sctp_ulpq_partial_delivery,
	.abort_pd		= sctp_ulpq_abort_pd,
	/* FORWARD-TSN process functions */
	.generate_ftsn		= sctp_generate_fwdtsn,
	.validate_ftsn		= sctp_validate_fwdtsn,
	.report_ftsn		= sctp_report_fwdtsn,
	.handle_ftsn		= sctp_handle_fwdtsn,
};

static int do_sctp_enqueue_event(struct sctp_ulpq *ulpq,
				 struct sctp_ulpevent *event)
{
	struct sk_buff_head temp;

	skb_queue_head_init(&temp);
	__skb_queue_tail(&temp, sctp_event2skb(event));
	return sctp_enqueue_event(ulpq, &temp);
}

static struct sctp_stream_interleave sctp_stream_interleave_1 = {
	.data_chunk_len		= sizeof(struct sctp_idata_chunk),
	.ftsn_chunk_len		= sizeof(struct sctp_ifwdtsn_chunk),
	/* I-DATA process functions */
	.make_datafrag		= sctp_make_idatafrag_empty,
	.assign_number		= sctp_chunk_assign_mid,
	.validate_data		= sctp_validate_idata,
	.ulpevent_data		= sctp_ulpevent_idata,
	.enqueue_event		= do_sctp_enqueue_event,
	.renege_events		= sctp_renege_events,
	.start_pd		= sctp_intl_start_pd,
	.abort_pd		= sctp_intl_abort_pd,
	/* I-FORWARD-TSN process functions */
	.generate_ftsn		= sctp_generate_iftsn,
	.validate_ftsn		= sctp_validate_iftsn,
	.report_ftsn		= sctp_report_iftsn,
	.handle_ftsn		= sctp_handle_iftsn,
};

void sctp_stream_interleave_init(struct sctp_stream *stream)
{
	struct sctp_association *asoc;

	asoc = container_of(stream, struct sctp_association, stream);
	stream->si = asoc->intl_enable ? &sctp_stream_interleave_1
				       : &sctp_stream_interleave_0;
}
