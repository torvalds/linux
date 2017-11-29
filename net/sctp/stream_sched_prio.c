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

/* Priority handling
 * RFC DRAFT ndata section 3.4
 */

static void sctp_sched_prio_unsched_all(struct sctp_stream *stream);

static struct sctp_stream_priorities *sctp_sched_prio_new_head(
			struct sctp_stream *stream, int prio, gfp_t gfp)
{
	struct sctp_stream_priorities *p;

	p = kmalloc(sizeof(*p), gfp);
	if (!p)
		return NULL;

	INIT_LIST_HEAD(&p->prio_sched);
	INIT_LIST_HEAD(&p->active);
	p->next = NULL;
	p->prio = prio;

	return p;
}

static struct sctp_stream_priorities *sctp_sched_prio_get_head(
			struct sctp_stream *stream, int prio, gfp_t gfp)
{
	struct sctp_stream_priorities *p;
	int i;

	/* Look into scheduled priorities first, as they are sorted and
	 * we can find it fast IF it's scheduled.
	 */
	list_for_each_entry(p, &stream->prio_list, prio_sched) {
		if (p->prio == prio)
			return p;
		if (p->prio > prio)
			break;
	}

	/* No luck. So we search on all streams now. */
	for (i = 0; i < stream->outcnt; i++) {
		if (!stream->out[i].ext)
			continue;

		p = stream->out[i].ext->prio_head;
		if (!p)
			/* Means all other streams won't be initialized
			 * as well.
			 */
			break;
		if (p->prio == prio)
			return p;
	}

	/* If not even there, allocate a new one. */
	return sctp_sched_prio_new_head(stream, prio, gfp);
}

static void sctp_sched_prio_next_stream(struct sctp_stream_priorities *p)
{
	struct list_head *pos;

	pos = p->next->prio_list.next;
	if (pos == &p->active)
		pos = pos->next;
	p->next = list_entry(pos, struct sctp_stream_out_ext, prio_list);
}

static bool sctp_sched_prio_unsched(struct sctp_stream_out_ext *soute)
{
	bool scheduled = false;

	if (!list_empty(&soute->prio_list)) {
		struct sctp_stream_priorities *prio_head = soute->prio_head;

		/* Scheduled */
		scheduled = true;

		if (prio_head->next == soute)
			/* Try to move to the next stream */
			sctp_sched_prio_next_stream(prio_head);

		list_del_init(&soute->prio_list);

		/* Also unsched the priority if this was the last stream */
		if (list_empty(&prio_head->active)) {
			list_del_init(&prio_head->prio_sched);
			/* If there is no stream left, clear next */
			prio_head->next = NULL;
		}
	}

	return scheduled;
}

static void sctp_sched_prio_sched(struct sctp_stream *stream,
				  struct sctp_stream_out_ext *soute)
{
	struct sctp_stream_priorities *prio, *prio_head;

	prio_head = soute->prio_head;

	/* Nothing to do if already scheduled */
	if (!list_empty(&soute->prio_list))
		return;

	/* Schedule the stream. If there is a next, we schedule the new
	 * one before it, so it's the last in round robin order.
	 * If there isn't, we also have to schedule the priority.
	 */
	if (prio_head->next) {
		list_add(&soute->prio_list, prio_head->next->prio_list.prev);
		return;
	}

	list_add(&soute->prio_list, &prio_head->active);
	prio_head->next = soute;

	list_for_each_entry(prio, &stream->prio_list, prio_sched) {
		if (prio->prio > prio_head->prio) {
			list_add(&prio_head->prio_sched, prio->prio_sched.prev);
			return;
		}
	}

	list_add_tail(&prio_head->prio_sched, &stream->prio_list);
}

static int sctp_sched_prio_set(struct sctp_stream *stream, __u16 sid,
			       __u16 prio, gfp_t gfp)
{
	struct sctp_stream_out *sout = &stream->out[sid];
	struct sctp_stream_out_ext *soute = sout->ext;
	struct sctp_stream_priorities *prio_head, *old;
	bool reschedule = false;
	int i;

	prio_head = sctp_sched_prio_get_head(stream, prio, gfp);
	if (!prio_head)
		return -ENOMEM;

	reschedule = sctp_sched_prio_unsched(soute);
	old = soute->prio_head;
	soute->prio_head = prio_head;
	if (reschedule)
		sctp_sched_prio_sched(stream, soute);

	if (!old)
		/* Happens when we set the priority for the first time */
		return 0;

	for (i = 0; i < stream->outcnt; i++) {
		soute = stream->out[i].ext;
		if (soute && soute->prio_head == old)
			/* It's still in use, nothing else to do here. */
			return 0;
	}

	/* No hits, we are good to free it. */
	kfree(old);

	return 0;
}

static int sctp_sched_prio_get(struct sctp_stream *stream, __u16 sid,
			       __u16 *value)
{
	*value = stream->out[sid].ext->prio_head->prio;
	return 0;
}

static int sctp_sched_prio_init(struct sctp_stream *stream)
{
	INIT_LIST_HEAD(&stream->prio_list);

	return 0;
}

static int sctp_sched_prio_init_sid(struct sctp_stream *stream, __u16 sid,
				    gfp_t gfp)
{
	INIT_LIST_HEAD(&stream->out[sid].ext->prio_list);
	return sctp_sched_prio_set(stream, sid, 0, gfp);
}

static void sctp_sched_prio_free(struct sctp_stream *stream)
{
	struct sctp_stream_priorities *prio, *n;
	LIST_HEAD(list);
	int i;

	/* As we don't keep a list of priorities, to avoid multiple
	 * frees we have to do it in 3 steps:
	 *   1. unsched everyone, so the lists are free to use in 2.
	 *   2. build the list of the priorities
	 *   3. free the list
	 */
	sctp_sched_prio_unsched_all(stream);
	for (i = 0; i < stream->outcnt; i++) {
		if (!stream->out[i].ext)
			continue;
		prio = stream->out[i].ext->prio_head;
		if (prio && list_empty(&prio->prio_sched))
			list_add(&prio->prio_sched, &list);
	}
	list_for_each_entry_safe(prio, n, &list, prio_sched) {
		list_del_init(&prio->prio_sched);
		kfree(prio);
	}
}

static void sctp_sched_prio_enqueue(struct sctp_outq *q,
				    struct sctp_datamsg *msg)
{
	struct sctp_stream *stream;
	struct sctp_chunk *ch;
	__u16 sid;

	ch = list_first_entry(&msg->chunks, struct sctp_chunk, frag_list);
	sid = sctp_chunk_stream_no(ch);
	stream = &q->asoc->stream;
	sctp_sched_prio_sched(stream, stream->out[sid].ext);
}

static struct sctp_chunk *sctp_sched_prio_dequeue(struct sctp_outq *q)
{
	struct sctp_stream *stream = &q->asoc->stream;
	struct sctp_stream_priorities *prio;
	struct sctp_stream_out_ext *soute;
	struct sctp_chunk *ch = NULL;

	/* Bail out quickly if queue is empty */
	if (list_empty(&q->out_chunk_list))
		goto out;

	/* Find which chunk is next. It's easy, it's either the current
	 * one or the first chunk on the next active stream.
	 */
	if (stream->out_curr) {
		soute = stream->out_curr->ext;
	} else {
		prio = list_entry(stream->prio_list.next,
				  struct sctp_stream_priorities, prio_sched);
		soute = prio->next;
	}
	ch = list_entry(soute->outq.next, struct sctp_chunk, stream_list);
	sctp_sched_dequeue_common(q, ch);

out:
	return ch;
}

static void sctp_sched_prio_dequeue_done(struct sctp_outq *q,
					 struct sctp_chunk *ch)
{
	struct sctp_stream_priorities *prio;
	struct sctp_stream_out_ext *soute;
	__u16 sid;

	/* Last chunk on that msg, move to the next stream on
	 * this priority.
	 */
	sid = sctp_chunk_stream_no(ch);
	soute = q->asoc->stream.out[sid].ext;
	prio = soute->prio_head;

	sctp_sched_prio_next_stream(prio);

	if (list_empty(&soute->outq))
		sctp_sched_prio_unsched(soute);
}

static void sctp_sched_prio_sched_all(struct sctp_stream *stream)
{
	struct sctp_association *asoc;
	struct sctp_stream_out *sout;
	struct sctp_chunk *ch;

	asoc = container_of(stream, struct sctp_association, stream);
	list_for_each_entry(ch, &asoc->outqueue.out_chunk_list, list) {
		__u16 sid;

		sid = sctp_chunk_stream_no(ch);
		sout = &stream->out[sid];
		if (sout->ext)
			sctp_sched_prio_sched(stream, sout->ext);
	}
}

static void sctp_sched_prio_unsched_all(struct sctp_stream *stream)
{
	struct sctp_stream_priorities *p, *tmp;
	struct sctp_stream_out_ext *soute, *souttmp;

	list_for_each_entry_safe(p, tmp, &stream->prio_list, prio_sched)
		list_for_each_entry_safe(soute, souttmp, &p->active, prio_list)
			sctp_sched_prio_unsched(soute);
}

struct sctp_sched_ops sctp_sched_prio = {
	.set = sctp_sched_prio_set,
	.get = sctp_sched_prio_get,
	.init = sctp_sched_prio_init,
	.init_sid = sctp_sched_prio_init_sid,
	.free = sctp_sched_prio_free,
	.enqueue = sctp_sched_prio_enqueue,
	.dequeue = sctp_sched_prio_dequeue,
	.dequeue_done = sctp_sched_prio_dequeue_done,
	.sched_all = sctp_sched_prio_sched_all,
	.unsched_all = sctp_sched_prio_unsched_all,
};
