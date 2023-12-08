// SPDX-License-Identifier: GPL-2.0-or-later
/* SCTP kernel implementation
 * (C) Copyright Red Hat Inc. 2017
 *
 * This file is part of the SCTP kernel implementation
 *
 * These functions manipulate sctp stream queue/scheduling.
 *
 * Please send any bug reports or fixes you make to the
 * email addresched(es):
 *    lksctp developers <linux-sctp@vger.kernel.org>
 *
 * Written or modified by:
 *    Marcelo Ricardo Leitner <marcelo.leitner@gmail.com>
 */

#include <linux/list.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>
#include <net/sctp/stream_sched.h>

/* Priority handling
 * RFC DRAFT ndata section 3.2
 */
static void sctp_sched_rr_unsched_all(struct sctp_stream *stream);

static void sctp_sched_rr_next_stream(struct sctp_stream *stream)
{
	struct list_head *pos;

	pos = stream->rr_next->rr_list.next;
	if (pos == &stream->rr_list)
		pos = pos->next;
	stream->rr_next = list_entry(pos, struct sctp_stream_out_ext, rr_list);
}

static void sctp_sched_rr_unsched(struct sctp_stream *stream,
				  struct sctp_stream_out_ext *soute)
{
	if (stream->rr_next == soute)
		/* Try to move to the next stream */
		sctp_sched_rr_next_stream(stream);

	list_del_init(&soute->rr_list);

	/* If we have no other stream queued, clear next */
	if (list_empty(&stream->rr_list))
		stream->rr_next = NULL;
}

static void sctp_sched_rr_sched(struct sctp_stream *stream,
				struct sctp_stream_out_ext *soute)
{
	if (!list_empty(&soute->rr_list))
		/* Already scheduled. */
		return;

	/* Schedule the stream */
	list_add_tail(&soute->rr_list, &stream->rr_list);

	if (!stream->rr_next)
		stream->rr_next = soute;
}

static int sctp_sched_rr_set(struct sctp_stream *stream, __u16 sid,
			     __u16 prio, gfp_t gfp)
{
	return 0;
}

static int sctp_sched_rr_get(struct sctp_stream *stream, __u16 sid,
			     __u16 *value)
{
	return 0;
}

static int sctp_sched_rr_init(struct sctp_stream *stream)
{
	INIT_LIST_HEAD(&stream->rr_list);
	stream->rr_next = NULL;

	return 0;
}

static int sctp_sched_rr_init_sid(struct sctp_stream *stream, __u16 sid,
				  gfp_t gfp)
{
	INIT_LIST_HEAD(&SCTP_SO(stream, sid)->ext->rr_list);

	return 0;
}

static void sctp_sched_rr_free_sid(struct sctp_stream *stream, __u16 sid)
{
}

static void sctp_sched_rr_free(struct sctp_stream *stream)
{
	sctp_sched_rr_unsched_all(stream);
}

static void sctp_sched_rr_enqueue(struct sctp_outq *q,
				  struct sctp_datamsg *msg)
{
	struct sctp_stream *stream;
	struct sctp_chunk *ch;
	__u16 sid;

	ch = list_first_entry(&msg->chunks, struct sctp_chunk, frag_list);
	sid = sctp_chunk_stream_no(ch);
	stream = &q->asoc->stream;
	sctp_sched_rr_sched(stream, SCTP_SO(stream, sid)->ext);
}

static struct sctp_chunk *sctp_sched_rr_dequeue(struct sctp_outq *q)
{
	struct sctp_stream *stream = &q->asoc->stream;
	struct sctp_stream_out_ext *soute;
	struct sctp_chunk *ch = NULL;

	/* Bail out quickly if queue is empty */
	if (list_empty(&q->out_chunk_list))
		goto out;

	/* Find which chunk is next */
	if (stream->out_curr)
		soute = stream->out_curr->ext;
	else
		soute = stream->rr_next;
	ch = list_entry(soute->outq.next, struct sctp_chunk, stream_list);

	sctp_sched_dequeue_common(q, ch);

out:
	return ch;
}

static void sctp_sched_rr_dequeue_done(struct sctp_outq *q,
				       struct sctp_chunk *ch)
{
	struct sctp_stream_out_ext *soute;
	__u16 sid;

	/* Last chunk on that msg, move to the next stream */
	sid = sctp_chunk_stream_no(ch);
	soute = SCTP_SO(&q->asoc->stream, sid)->ext;

	sctp_sched_rr_next_stream(&q->asoc->stream);

	if (list_empty(&soute->outq))
		sctp_sched_rr_unsched(&q->asoc->stream, soute);
}

static void sctp_sched_rr_sched_all(struct sctp_stream *stream)
{
	struct sctp_association *asoc;
	struct sctp_stream_out_ext *soute;
	struct sctp_chunk *ch;

	asoc = container_of(stream, struct sctp_association, stream);
	list_for_each_entry(ch, &asoc->outqueue.out_chunk_list, list) {
		__u16 sid;

		sid = sctp_chunk_stream_no(ch);
		soute = SCTP_SO(stream, sid)->ext;
		if (soute)
			sctp_sched_rr_sched(stream, soute);
	}
}

static void sctp_sched_rr_unsched_all(struct sctp_stream *stream)
{
	struct sctp_stream_out_ext *soute, *tmp;

	list_for_each_entry_safe(soute, tmp, &stream->rr_list, rr_list)
		sctp_sched_rr_unsched(stream, soute);
}

static struct sctp_sched_ops sctp_sched_rr = {
	.set = sctp_sched_rr_set,
	.get = sctp_sched_rr_get,
	.init = sctp_sched_rr_init,
	.init_sid = sctp_sched_rr_init_sid,
	.free_sid = sctp_sched_rr_free_sid,
	.free = sctp_sched_rr_free,
	.enqueue = sctp_sched_rr_enqueue,
	.dequeue = sctp_sched_rr_dequeue,
	.dequeue_done = sctp_sched_rr_dequeue_done,
	.sched_all = sctp_sched_rr_sched_all,
	.unsched_all = sctp_sched_rr_unsched_all,
};

void sctp_sched_ops_rr_init(void)
{
	sctp_sched_ops_register(SCTP_SS_RR, &sctp_sched_rr);
}
