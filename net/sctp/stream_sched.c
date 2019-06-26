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
 *    Marcelo Ricardo Leitner <marcelo.leitner@gmail.com>
 */

#include <linux/list.h>
#include <net/sctp/sctp.h>
#include <net/sctp/sm.h>
#include <net/sctp/stream_sched.h>

/* First Come First Serve (a.k.a. FIFO)
 * RFC DRAFT ndata Section 3.1
 */
static int sctp_sched_fcfs_set(struct sctp_stream *stream, __u16 sid,
			       __u16 value, gfp_t gfp)
{
	return 0;
}

static int sctp_sched_fcfs_get(struct sctp_stream *stream, __u16 sid,
			       __u16 *value)
{
	*value = 0;
	return 0;
}

static int sctp_sched_fcfs_init(struct sctp_stream *stream)
{
	return 0;
}

static int sctp_sched_fcfs_init_sid(struct sctp_stream *stream, __u16 sid,
				    gfp_t gfp)
{
	return 0;
}

static void sctp_sched_fcfs_free(struct sctp_stream *stream)
{
}

static void sctp_sched_fcfs_enqueue(struct sctp_outq *q,
				    struct sctp_datamsg *msg)
{
}

static struct sctp_chunk *sctp_sched_fcfs_dequeue(struct sctp_outq *q)
{
	struct sctp_stream *stream = &q->asoc->stream;
	struct sctp_chunk *ch = NULL;
	struct list_head *entry;

	if (list_empty(&q->out_chunk_list))
		goto out;

	if (stream->out_curr) {
		ch = list_entry(stream->out_curr->ext->outq.next,
				struct sctp_chunk, stream_list);
	} else {
		entry = q->out_chunk_list.next;
		ch = list_entry(entry, struct sctp_chunk, list);
	}

	sctp_sched_dequeue_common(q, ch);

out:
	return ch;
}

static void sctp_sched_fcfs_dequeue_done(struct sctp_outq *q,
					 struct sctp_chunk *chunk)
{
}

static void sctp_sched_fcfs_sched_all(struct sctp_stream *stream)
{
}

static void sctp_sched_fcfs_unsched_all(struct sctp_stream *stream)
{
}

static struct sctp_sched_ops sctp_sched_fcfs = {
	.set = sctp_sched_fcfs_set,
	.get = sctp_sched_fcfs_get,
	.init = sctp_sched_fcfs_init,
	.init_sid = sctp_sched_fcfs_init_sid,
	.free = sctp_sched_fcfs_free,
	.enqueue = sctp_sched_fcfs_enqueue,
	.dequeue = sctp_sched_fcfs_dequeue,
	.dequeue_done = sctp_sched_fcfs_dequeue_done,
	.sched_all = sctp_sched_fcfs_sched_all,
	.unsched_all = sctp_sched_fcfs_unsched_all,
};

static void sctp_sched_ops_fcfs_init(void)
{
	sctp_sched_ops_register(SCTP_SS_FCFS, &sctp_sched_fcfs);
}

/* API to other parts of the stack */

static struct sctp_sched_ops *sctp_sched_ops[SCTP_SS_MAX + 1];

void sctp_sched_ops_register(enum sctp_sched_type sched,
			     struct sctp_sched_ops *sched_ops)
{
	sctp_sched_ops[sched] = sched_ops;
}

void sctp_sched_ops_init(void)
{
	sctp_sched_ops_fcfs_init();
	sctp_sched_ops_prio_init();
	sctp_sched_ops_rr_init();
}

int sctp_sched_set_sched(struct sctp_association *asoc,
			 enum sctp_sched_type sched)
{
	struct sctp_sched_ops *n = sctp_sched_ops[sched];
	struct sctp_sched_ops *old = asoc->outqueue.sched;
	struct sctp_datamsg *msg = NULL;
	struct sctp_chunk *ch;
	int i, ret = 0;

	if (old == n)
		return ret;

	if (sched > SCTP_SS_MAX)
		return -EINVAL;

	if (old) {
		old->free(&asoc->stream);

		/* Give the next scheduler a clean slate. */
		for (i = 0; i < asoc->stream.outcnt; i++) {
			void *p = SCTP_SO(&asoc->stream, i)->ext;

			if (!p)
				continue;

			p += offsetofend(struct sctp_stream_out_ext, outq);
			memset(p, 0, sizeof(struct sctp_stream_out_ext) -
				     offsetofend(struct sctp_stream_out_ext, outq));
		}
	}

	asoc->outqueue.sched = n;
	n->init(&asoc->stream);
	for (i = 0; i < asoc->stream.outcnt; i++) {
		if (!SCTP_SO(&asoc->stream, i)->ext)
			continue;

		ret = n->init_sid(&asoc->stream, i, GFP_KERNEL);
		if (ret)
			goto err;
	}

	/* We have to requeue all chunks already queued. */
	list_for_each_entry(ch, &asoc->outqueue.out_chunk_list, list) {
		if (ch->msg == msg)
			continue;
		msg = ch->msg;
		n->enqueue(&asoc->outqueue, msg);
	}

	return ret;

err:
	n->free(&asoc->stream);
	asoc->outqueue.sched = &sctp_sched_fcfs; /* Always safe */

	return ret;
}

int sctp_sched_get_sched(struct sctp_association *asoc)
{
	int i;

	for (i = 0; i <= SCTP_SS_MAX; i++)
		if (asoc->outqueue.sched == sctp_sched_ops[i])
			return i;

	return 0;
}

int sctp_sched_set_value(struct sctp_association *asoc, __u16 sid,
			 __u16 value, gfp_t gfp)
{
	if (sid >= asoc->stream.outcnt)
		return -EINVAL;

	if (!SCTP_SO(&asoc->stream, sid)->ext) {
		int ret;

		ret = sctp_stream_init_ext(&asoc->stream, sid);
		if (ret)
			return ret;
	}

	return asoc->outqueue.sched->set(&asoc->stream, sid, value, gfp);
}

int sctp_sched_get_value(struct sctp_association *asoc, __u16 sid,
			 __u16 *value)
{
	if (sid >= asoc->stream.outcnt)
		return -EINVAL;

	if (!SCTP_SO(&asoc->stream, sid)->ext)
		return 0;

	return asoc->outqueue.sched->get(&asoc->stream, sid, value);
}

void sctp_sched_dequeue_done(struct sctp_outq *q, struct sctp_chunk *ch)
{
	if (!list_is_last(&ch->frag_list, &ch->msg->chunks) &&
	    !q->asoc->intl_enable) {
		struct sctp_stream_out *sout;
		__u16 sid;

		/* datamsg is not finish, so save it as current one,
		 * in case application switch scheduler or a higher
		 * priority stream comes in.
		 */
		sid = sctp_chunk_stream_no(ch);
		sout = SCTP_SO(&q->asoc->stream, sid);
		q->asoc->stream.out_curr = sout;
		return;
	}

	q->asoc->stream.out_curr = NULL;
	q->sched->dequeue_done(q, ch);
}

/* Auxiliary functions for the schedulers */
void sctp_sched_dequeue_common(struct sctp_outq *q, struct sctp_chunk *ch)
{
	list_del_init(&ch->list);
	list_del_init(&ch->stream_list);
	q->out_qlen -= ch->skb->len;
}

int sctp_sched_init_sid(struct sctp_stream *stream, __u16 sid, gfp_t gfp)
{
	struct sctp_sched_ops *sched = sctp_sched_ops_from_stream(stream);
	struct sctp_stream_out_ext *ext = SCTP_SO(stream, sid)->ext;

	INIT_LIST_HEAD(&ext->outq);
	return sched->init_sid(stream, sid, gfp);
}

struct sctp_sched_ops *sctp_sched_ops_from_stream(struct sctp_stream *stream)
{
	struct sctp_association *asoc;

	asoc = container_of(stream, struct sctp_association, stream);

	return asoc->outqueue.sched;
}
