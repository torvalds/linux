/*	$OpenBSD: scheduler_ramqueue.c,v 1.49 2024/09/03 18:27:04 op Exp $	*/

/*
 * Copyright (c) 2012 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "smtpd.h"
#include "log.h"

TAILQ_HEAD(evplist, rq_envelope);

struct rq_message {
	uint32_t		 msgid;
	struct tree		 envelopes;
};

struct rq_envelope {
	TAILQ_ENTRY(rq_envelope) entry;
	SPLAY_ENTRY(rq_envelope) t_entry;

	uint64_t		 evpid;
	uint64_t		 holdq;
	enum delivery_type	 type;

#define	RQ_EVPSTATE_PENDING	 0
#define	RQ_EVPSTATE_SCHEDULED	 1
#define	RQ_EVPSTATE_INFLIGHT	 2
#define	RQ_EVPSTATE_HELD	 3
	uint8_t			 state;

#define	RQ_ENVELOPE_EXPIRED	 0x01
#define	RQ_ENVELOPE_REMOVED	 0x02
#define	RQ_ENVELOPE_SUSPEND	 0x04
#define	RQ_ENVELOPE_UPDATE	 0x08
#define	RQ_ENVELOPE_OVERFLOW	 0x10
	uint8_t			 flags;

	time_t			 ctime;
	time_t			 sched;
	time_t			 expire;

	struct rq_message	*message;

	time_t			 t_inflight;
	time_t			 t_scheduled;
};

struct rq_holdq {
	struct evplist		 q;
	size_t			 count;
};

struct rq_queue {
	size_t			 evpcount;
	struct tree		 messages;
	SPLAY_HEAD(prioqtree, rq_envelope)	q_priotree;

	struct evplist		 q_pending;
	struct evplist		 q_inflight;

	struct evplist		 q_mta;
	struct evplist		 q_mda;
	struct evplist		 q_bounce;
	struct evplist		 q_update;
	struct evplist		 q_expired;
	struct evplist		 q_removed;
};

static int rq_envelope_cmp(struct rq_envelope *, struct rq_envelope *);

SPLAY_PROTOTYPE(prioqtree, rq_envelope, t_entry, rq_envelope_cmp);
static int scheduler_ram_init(const char *);
static int scheduler_ram_insert(struct scheduler_info *);
static size_t scheduler_ram_commit(uint32_t);
static size_t scheduler_ram_rollback(uint32_t);
static int scheduler_ram_update(struct scheduler_info *);
static int scheduler_ram_delete(uint64_t);
static int scheduler_ram_hold(uint64_t, uint64_t);
static int scheduler_ram_release(int, uint64_t, int);
static int scheduler_ram_batch(int, int *, size_t *, uint64_t *, int *);
static size_t scheduler_ram_messages(uint32_t, uint32_t *, size_t);
static size_t scheduler_ram_envelopes(uint64_t, struct evpstate *, size_t);
static int scheduler_ram_schedule(uint64_t);
static int scheduler_ram_remove(uint64_t);
static int scheduler_ram_suspend(uint64_t);
static int scheduler_ram_resume(uint64_t);
static int scheduler_ram_query(uint64_t);

static void sorted_insert(struct rq_queue *, struct rq_envelope *);

static void rq_queue_init(struct rq_queue *);
static void rq_queue_merge(struct rq_queue *, struct rq_queue *);
static void rq_queue_dump(struct rq_queue *, const char *);
static void rq_queue_schedule(struct rq_queue *rq);
static struct evplist *rq_envelope_list(struct rq_queue *, struct rq_envelope *);
static void rq_envelope_schedule(struct rq_queue *, struct rq_envelope *);
static int rq_envelope_remove(struct rq_queue *, struct rq_envelope *);
static int rq_envelope_suspend(struct rq_queue *, struct rq_envelope *);
static int rq_envelope_resume(struct rq_queue *, struct rq_envelope *);
static void rq_envelope_delete(struct rq_queue *, struct rq_envelope *);
static const char *rq_envelope_to_text(struct rq_envelope *);

struct scheduler_backend scheduler_backend_ramqueue = {
	scheduler_ram_init,

	scheduler_ram_insert,
	scheduler_ram_commit,
	scheduler_ram_rollback,

	scheduler_ram_update,
	scheduler_ram_delete,
	scheduler_ram_hold,
	scheduler_ram_release,

	scheduler_ram_batch,

	scheduler_ram_messages,
	scheduler_ram_envelopes,
	scheduler_ram_schedule,
	scheduler_ram_remove,
	scheduler_ram_suspend,
	scheduler_ram_resume,
	scheduler_ram_query,
};

static struct rq_queue	ramqueue;
static struct tree	updates;
static struct tree	holdqs[3]; /* delivery type */

static time_t		currtime;

#define BACKOFF_TRANSFER	400
#define BACKOFF_DELIVERY	10
#define BACKOFF_OVERFLOW	3

static time_t
scheduler_backoff(time_t t0, time_t base, uint32_t step)
{
	return (t0 + base * step * step);
}

static time_t
scheduler_next(time_t t0, time_t base, uint32_t step)
{
	time_t t;

	/* XXX be more efficient */
	while ((t = scheduler_backoff(t0, base, step)) <= currtime)
		step++;

	return (t);
}

static int
scheduler_ram_init(const char *arg)
{
	rq_queue_init(&ramqueue);
	tree_init(&updates);
	tree_init(&holdqs[D_MDA]);
	tree_init(&holdqs[D_MTA]);
	tree_init(&holdqs[D_BOUNCE]);

	return (1);
}

static int
scheduler_ram_insert(struct scheduler_info *si)
{
	struct rq_queue		*update;
	struct rq_message	*message;
	struct rq_envelope	*envelope;
	uint32_t		 msgid;

	currtime = time(NULL);

	msgid = evpid_to_msgid(si->evpid);

	/* find/prepare a ramqueue update */
	if ((update = tree_get(&updates, msgid)) == NULL) {
		update = xcalloc(1, sizeof *update);
		stat_increment("scheduler.ramqueue.update", 1);
		rq_queue_init(update);
		tree_xset(&updates, msgid, update);
	}

	/* find/prepare the msgtree message in ramqueue update */
	if ((message = tree_get(&update->messages, msgid)) == NULL) {
		message = xcalloc(1, sizeof *message);
		message->msgid = msgid;
		tree_init(&message->envelopes);
		tree_xset(&update->messages, msgid, message);
		stat_increment("scheduler.ramqueue.message", 1);
	}

	/* create envelope in ramqueue message */
	envelope = xcalloc(1, sizeof *envelope);
	envelope->evpid = si->evpid;
	envelope->type = si->type;
	envelope->message = message;
	envelope->ctime = si->creation;
	envelope->expire = si->creation + si->ttl;
	envelope->sched = scheduler_backoff(si->creation,
	    (si->type == D_MTA) ? BACKOFF_TRANSFER : BACKOFF_DELIVERY, si->retry);
	tree_xset(&message->envelopes, envelope->evpid, envelope);

	update->evpcount++;
	stat_increment("scheduler.ramqueue.envelope", 1);

	envelope->state = RQ_EVPSTATE_PENDING;
	TAILQ_INSERT_TAIL(&update->q_pending, envelope, entry);

	si->nexttry = envelope->sched;

	return (1);
}

static size_t
scheduler_ram_commit(uint32_t msgid)
{
	struct rq_queue	*update;
	size_t		 r;

	currtime = time(NULL);

	update = tree_xpop(&updates, msgid);
	r = update->evpcount;

	if (tracing & TRACE_SCHEDULER)
		rq_queue_dump(update, "update to commit");

	rq_queue_merge(&ramqueue, update);

	if (tracing & TRACE_SCHEDULER)
		rq_queue_dump(&ramqueue, "resulting queue");

	rq_queue_schedule(&ramqueue);

	free(update);
	stat_decrement("scheduler.ramqueue.update", 1);

	return (r);
}

static size_t
scheduler_ram_rollback(uint32_t msgid)
{
	struct rq_queue		*update;
	struct rq_envelope	*evp;
	size_t			 r;

	currtime = time(NULL);

	if ((update = tree_pop(&updates, msgid)) == NULL)
		return (0);
	r = update->evpcount;

	while ((evp = TAILQ_FIRST(&update->q_pending))) {
		TAILQ_REMOVE(&update->q_pending, evp, entry);
		rq_envelope_delete(update, evp);
	}

	free(update);
	stat_decrement("scheduler.ramqueue.update", 1);

	return (r);
}

static int
scheduler_ram_update(struct scheduler_info *si)
{
	struct rq_message	*msg;
	struct rq_envelope	*evp;
	uint32_t		 msgid;

	currtime = time(NULL);

	msgid = evpid_to_msgid(si->evpid);
	msg = tree_xget(&ramqueue.messages, msgid);
	evp = tree_xget(&msg->envelopes, si->evpid);

	/* it *must* be in-flight */
	if (evp->state != RQ_EVPSTATE_INFLIGHT)
		fatalx("evp:%016" PRIx64 " not in-flight", si->evpid);

	TAILQ_REMOVE(&ramqueue.q_inflight, evp, entry);

	/*
	 * If the envelope was removed while inflight,  schedule it for
	 * removal immediately.
	 */
	if (evp->flags & RQ_ENVELOPE_REMOVED) {
		TAILQ_INSERT_TAIL(&ramqueue.q_removed, evp, entry);
		evp->state = RQ_EVPSTATE_SCHEDULED;
		evp->t_scheduled = currtime;
		return (1);
	}

	evp->sched = scheduler_next(evp->ctime,
	    (si->type == D_MTA) ? BACKOFF_TRANSFER : BACKOFF_DELIVERY, si->retry);

	evp->state = RQ_EVPSTATE_PENDING;
	if (!(evp->flags & RQ_ENVELOPE_SUSPEND))
		sorted_insert(&ramqueue, evp);

	si->nexttry = evp->sched;

	return (1);
}

static int
scheduler_ram_delete(uint64_t evpid)
{
	struct rq_message	*msg;
	struct rq_envelope	*evp;
	uint32_t		 msgid;

	currtime = time(NULL);

	msgid = evpid_to_msgid(evpid);
	msg = tree_xget(&ramqueue.messages, msgid);
	evp = tree_xget(&msg->envelopes, evpid);

	/* it *must* be in-flight */
	if (evp->state != RQ_EVPSTATE_INFLIGHT)
		fatalx("evp:%016" PRIx64 " not in-flight", evpid);

	TAILQ_REMOVE(&ramqueue.q_inflight, evp, entry);

	rq_envelope_delete(&ramqueue, evp);

	return (1);
}

#define HOLDQ_MAXSIZE	1000

static int
scheduler_ram_hold(uint64_t evpid, uint64_t holdq)
{
	struct rq_holdq		*hq;
	struct rq_message	*msg;
	struct rq_envelope	*evp;
	uint32_t		 msgid;

	currtime = time(NULL);

	msgid = evpid_to_msgid(evpid);
	msg = tree_xget(&ramqueue.messages, msgid);
	evp = tree_xget(&msg->envelopes, evpid);

	/* it *must* be in-flight */
	if (evp->state != RQ_EVPSTATE_INFLIGHT)
		fatalx("evp:%016" PRIx64 " not in-flight", evpid);

	TAILQ_REMOVE(&ramqueue.q_inflight, evp, entry);

	/* If the envelope is suspended, just mark it as pending */
	if (evp->flags & RQ_ENVELOPE_SUSPEND) {
		evp->state = RQ_EVPSTATE_PENDING;
		return (0);
	}

	hq = tree_get(&holdqs[evp->type], holdq);
	if (hq == NULL) {
		hq = xcalloc(1, sizeof(*hq));
		TAILQ_INIT(&hq->q);
		tree_xset(&holdqs[evp->type], holdq, hq);
		stat_increment("scheduler.ramqueue.holdq", 1);
	}

	/* If the holdq is full, just "tempfail" the envelope */
	if (hq->count >= HOLDQ_MAXSIZE) {
		evp->state = RQ_EVPSTATE_PENDING;
		evp->flags |= RQ_ENVELOPE_UPDATE;
		evp->flags |= RQ_ENVELOPE_OVERFLOW;
		sorted_insert(&ramqueue, evp);
		stat_increment("scheduler.ramqueue.hold-overflow", 1);
		return (0);
	}

	evp->state = RQ_EVPSTATE_HELD;
	evp->holdq = holdq;
	/* This is an optimization: upon release, the envelopes will be
	 * inserted in the pending queue from the first element to the last.
	 * Since elements already in the queue were received first, they
	 * were scheduled first, so they will be reinserted before the
	 * current element.
	 */
	TAILQ_INSERT_HEAD(&hq->q, evp, entry);
	hq->count += 1;
	stat_increment("scheduler.ramqueue.hold", 1);

	return (1);
}

static int
scheduler_ram_release(int type, uint64_t holdq, int n)
{
	struct rq_holdq		*hq;
	struct rq_envelope	*evp;
	int			 i, update;

	currtime = time(NULL);

	hq = tree_get(&holdqs[type], holdq);
	if (hq == NULL)
		return (0);

	if (n == -1) {
		n = 0;
		update = 1;
	}
	else
		update = 0;

	for (i = 0; n == 0 || i < n; i++) {
		evp = TAILQ_FIRST(&hq->q);
		if (evp == NULL)
			break;

		TAILQ_REMOVE(&hq->q, evp, entry);
		hq->count -= 1;
		evp->holdq = 0;

		/* When released, all envelopes are put in the pending queue
		 * and will be rescheduled immediately.  As an optimization,
		 * we could just schedule them directly.
		 */
		evp->state = RQ_EVPSTATE_PENDING;
		if (update)
			evp->flags |= RQ_ENVELOPE_UPDATE;
		sorted_insert(&ramqueue, evp);
	}

	if (TAILQ_EMPTY(&hq->q)) {
		tree_xpop(&holdqs[type], holdq);
		free(hq);
		stat_decrement("scheduler.ramqueue.holdq", 1);
	}
	stat_decrement("scheduler.ramqueue.hold", i);

	return (i);
}

static int
scheduler_ram_batch(int mask, int *delay, size_t *count, uint64_t *evpids, int *types)
{
	struct rq_envelope	*evp;
	size_t			 i, n;
	time_t			 t;

	currtime = time(NULL);

	rq_queue_schedule(&ramqueue);
	if (tracing & TRACE_SCHEDULER)
		rq_queue_dump(&ramqueue, "scheduler_ram_batch()");

	i = 0;
	n = 0;

	for (;;) {

		if (mask & SCHED_REMOVE && (evp = TAILQ_FIRST(&ramqueue.q_removed))) {
			TAILQ_REMOVE(&ramqueue.q_removed, evp, entry);
			types[i] = SCHED_REMOVE;
			evpids[i] = evp->evpid;
			rq_envelope_delete(&ramqueue, evp);

			if (++i == *count)
				break;
		}

		if (mask & SCHED_EXPIRE && (evp = TAILQ_FIRST(&ramqueue.q_expired))) {
			TAILQ_REMOVE(&ramqueue.q_expired, evp, entry);
			types[i] = SCHED_EXPIRE;
			evpids[i] = evp->evpid;
			rq_envelope_delete(&ramqueue, evp);

			if (++i == *count)
				break;
		}

		if (mask & SCHED_UPDATE && (evp = TAILQ_FIRST(&ramqueue.q_update))) {
			TAILQ_REMOVE(&ramqueue.q_update, evp, entry);
			types[i] = SCHED_UPDATE;
			evpids[i] = evp->evpid;

			if (evp->flags & RQ_ENVELOPE_OVERFLOW)
				t = BACKOFF_OVERFLOW;
			else if (evp->type == D_MTA)
				t = BACKOFF_TRANSFER;
			else
				t = BACKOFF_DELIVERY;

			evp->sched = scheduler_next(evp->ctime, t, 0);
			evp->flags &= ~(RQ_ENVELOPE_UPDATE|RQ_ENVELOPE_OVERFLOW);
			evp->state = RQ_EVPSTATE_PENDING;
			if (!(evp->flags & RQ_ENVELOPE_SUSPEND))
				sorted_insert(&ramqueue, evp);

			if (++i == *count)
				break;
		}

		if (mask & SCHED_BOUNCE && (evp = TAILQ_FIRST(&ramqueue.q_bounce))) {
			TAILQ_REMOVE(&ramqueue.q_bounce, evp, entry);
			types[i] = SCHED_BOUNCE;
			evpids[i] = evp->evpid;

			TAILQ_INSERT_TAIL(&ramqueue.q_inflight, evp, entry);
			evp->state = RQ_EVPSTATE_INFLIGHT;
			evp->t_inflight = currtime;

			if (++i == *count)
				break;
		}

		if (mask & SCHED_MDA && (evp = TAILQ_FIRST(&ramqueue.q_mda))) {
			TAILQ_REMOVE(&ramqueue.q_mda, evp, entry);
			types[i] = SCHED_MDA;
			evpids[i] = evp->evpid;

			TAILQ_INSERT_TAIL(&ramqueue.q_inflight, evp, entry);
			evp->state = RQ_EVPSTATE_INFLIGHT;
			evp->t_inflight = currtime;

			if (++i == *count)
				break;
		}

		if (mask & SCHED_MTA && (evp = TAILQ_FIRST(&ramqueue.q_mta))) {
			TAILQ_REMOVE(&ramqueue.q_mta, evp, entry);
			types[i] = SCHED_MTA;
			evpids[i] = evp->evpid;

			TAILQ_INSERT_TAIL(&ramqueue.q_inflight, evp, entry);
			evp->state = RQ_EVPSTATE_INFLIGHT;
			evp->t_inflight = currtime;

			if (++i == *count)
				break;
		}

		/* nothing seen this round */
		if (i == n)
			break;

		n = i;
	}

	if (i) {
		*count = i;
		return (1);
	}

	if ((evp = TAILQ_FIRST(&ramqueue.q_pending))) {
		if (evp->sched < evp->expire)
			t = evp->sched;
		else
			t = evp->expire;
		*delay = (t < currtime) ? 0 : (t - currtime);
	}
	else
		*delay = -1;

	return (0);
}

static size_t
scheduler_ram_messages(uint32_t from, uint32_t *dst, size_t size)
{
	uint64_t id;
	size_t	 n;
	void	*i;

	for (n = 0, i = NULL; n < size; n++) {
		if (tree_iterfrom(&ramqueue.messages, &i, from, &id, NULL) == 0)
			break;
		dst[n] = id;
	}

	return (n);
}

static size_t
scheduler_ram_envelopes(uint64_t from, struct evpstate *dst, size_t size)
{
	struct rq_message	*msg;
	struct rq_envelope	*evp;
	void			*i;
	size_t			 n;

	if ((msg = tree_get(&ramqueue.messages, evpid_to_msgid(from))) == NULL)
		return (0);

	for (n = 0, i = NULL; n < size; ) {

		if (tree_iterfrom(&msg->envelopes, &i, from, NULL,
		    (void**)&evp) == 0)
			break;

		if (evp->flags & (RQ_ENVELOPE_REMOVED | RQ_ENVELOPE_EXPIRED))
			continue;

		dst[n].evpid = evp->evpid;
		dst[n].flags = 0;
		dst[n].retry = 0;
		dst[n].time = 0;

		if (evp->state == RQ_EVPSTATE_PENDING) {
			dst[n].time = evp->sched;
			dst[n].flags = EF_PENDING;
		}
		else if (evp->state == RQ_EVPSTATE_SCHEDULED) {
			dst[n].time = evp->t_scheduled;
			dst[n].flags = EF_PENDING;
		}
		else if (evp->state == RQ_EVPSTATE_INFLIGHT) {
			dst[n].time = evp->t_inflight;
			dst[n].flags = EF_INFLIGHT;
		}
		else if (evp->state == RQ_EVPSTATE_HELD) {
			/* same as scheduled */
			dst[n].time = evp->t_scheduled;
			dst[n].flags = EF_PENDING;
			dst[n].flags |= EF_HOLD;
		}
		if (evp->flags & RQ_ENVELOPE_SUSPEND)
			dst[n].flags |= EF_SUSPEND;

		n++;
	}

	return (n);
}

static int
scheduler_ram_schedule(uint64_t evpid)
{
	struct rq_message	*msg;
	struct rq_envelope	*evp;
	uint32_t		 msgid;
	void			*i;
	int			 r;

	currtime = time(NULL);

	if (evpid > 0xffffffff) {
		msgid = evpid_to_msgid(evpid);
		if ((msg = tree_get(&ramqueue.messages, msgid)) == NULL)
			return (0);
		if ((evp = tree_get(&msg->envelopes, evpid)) == NULL)
			return (0);
		if (evp->state == RQ_EVPSTATE_INFLIGHT)
			return (0);
		rq_envelope_schedule(&ramqueue, evp);
		return (1);
	}
	else {
		msgid = evpid;
		if ((msg = tree_get(&ramqueue.messages, msgid)) == NULL)
			return (0);
		i = NULL;
		r = 0;
		while (tree_iter(&msg->envelopes, &i, NULL, (void*)(&evp))) {
			if (evp->state == RQ_EVPSTATE_INFLIGHT)
				continue;
			rq_envelope_schedule(&ramqueue, evp);
			r++;
		}
		return (r);
	}
}

static int
scheduler_ram_remove(uint64_t evpid)
{
	struct rq_message	*msg;
	struct rq_envelope	*evp;
	uint32_t		 msgid;
	void			*i;
	int			 r;

	currtime = time(NULL);

	if (evpid > 0xffffffff) {
		msgid = evpid_to_msgid(evpid);
		if ((msg = tree_get(&ramqueue.messages, msgid)) == NULL)
			return (0);
		if ((evp = tree_get(&msg->envelopes, evpid)) == NULL)
			return (0);
		if (rq_envelope_remove(&ramqueue, evp))
			return (1);
		return (0);
	}
	else {
		msgid = evpid;
		if ((msg = tree_get(&ramqueue.messages, msgid)) == NULL)
			return (0);
		i = NULL;
		r = 0;
		while (tree_iter(&msg->envelopes, &i, NULL, (void*)(&evp)))
			if (rq_envelope_remove(&ramqueue, evp))
				r++;
		return (r);
	}
}

static int
scheduler_ram_suspend(uint64_t evpid)
{
	struct rq_message	*msg;
	struct rq_envelope	*evp;
	uint32_t		 msgid;
	void			*i;
	int			 r;

	currtime = time(NULL);

	if (evpid > 0xffffffff) {
		msgid = evpid_to_msgid(evpid);
		if ((msg = tree_get(&ramqueue.messages, msgid)) == NULL)
			return (0);
		if ((evp = tree_get(&msg->envelopes, evpid)) == NULL)
			return (0);
		if (rq_envelope_suspend(&ramqueue, evp))
			return (1);
		return (0);
	}
	else {
		msgid = evpid;
		if ((msg = tree_get(&ramqueue.messages, msgid)) == NULL)
			return (0);
		i = NULL;
		r = 0;
		while (tree_iter(&msg->envelopes, &i, NULL, (void*)(&evp)))
			if (rq_envelope_suspend(&ramqueue, evp))
				r++;
		return (r);
	}
}

static int
scheduler_ram_resume(uint64_t evpid)
{
	struct rq_message	*msg;
	struct rq_envelope	*evp;
	uint32_t		 msgid;
	void			*i;
	int			 r;

	currtime = time(NULL);

	if (evpid > 0xffffffff) {
		msgid = evpid_to_msgid(evpid);
		if ((msg = tree_get(&ramqueue.messages, msgid)) == NULL)
			return (0);
		if ((evp = tree_get(&msg->envelopes, evpid)) == NULL)
			return (0);
		if (rq_envelope_resume(&ramqueue, evp))
			return (1);
		return (0);
	}
	else {
		msgid = evpid;
		if ((msg = tree_get(&ramqueue.messages, msgid)) == NULL)
			return (0);
		i = NULL;
		r = 0;
		while (tree_iter(&msg->envelopes, &i, NULL, (void*)(&evp)))
			if (rq_envelope_resume(&ramqueue, evp))
				r++;
		return (r);
	}
}

static int
scheduler_ram_query(uint64_t evpid)
{
	uint32_t msgid;

	if (evpid > 0xffffffff)
		msgid = evpid_to_msgid(evpid);
	else
		msgid = evpid;

	if (tree_get(&ramqueue.messages, msgid) == NULL)
		return (0);

	return (1);
}

static void
sorted_insert(struct rq_queue *rq, struct rq_envelope *evp)
{
	struct rq_envelope	*evp2;

	SPLAY_INSERT(prioqtree, &rq->q_priotree, evp);
	evp2 = SPLAY_NEXT(prioqtree, &rq->q_priotree, evp);
	if (evp2)
		TAILQ_INSERT_BEFORE(evp2, evp, entry);
	else
		TAILQ_INSERT_TAIL(&rq->q_pending, evp, entry);
}

static void
rq_queue_init(struct rq_queue *rq)
{
	memset(rq, 0, sizeof *rq);
	tree_init(&rq->messages);
	TAILQ_INIT(&rq->q_pending);
	TAILQ_INIT(&rq->q_inflight);
	TAILQ_INIT(&rq->q_mta);
	TAILQ_INIT(&rq->q_mda);
	TAILQ_INIT(&rq->q_bounce);
	TAILQ_INIT(&rq->q_update);
	TAILQ_INIT(&rq->q_expired);
	TAILQ_INIT(&rq->q_removed);
	SPLAY_INIT(&rq->q_priotree);
}

static void
rq_queue_merge(struct rq_queue *rq, struct rq_queue *update)
{
	struct rq_message	*message, *tomessage;
	struct rq_envelope	*envelope;
	uint64_t		 id;
	void			*i;

	while (tree_poproot(&update->messages, &id, (void*)&message)) {
		if ((tomessage = tree_get(&rq->messages, id)) == NULL) {
			/* message does not exist. reuse structure */
			tree_xset(&rq->messages, id, message);
			continue;
		}
		/* need to re-link all envelopes before merging them */
		i = NULL;
		while ((tree_iter(&message->envelopes, &i, &id,
		    (void*)&envelope)))
			envelope->message = tomessage;
		tree_merge(&tomessage->envelopes, &message->envelopes);
		free(message);
		stat_decrement("scheduler.ramqueue.message", 1);
	}

	/* Sorted insert in the pending queue */
	while ((envelope = TAILQ_FIRST(&update->q_pending))) {
		TAILQ_REMOVE(&update->q_pending, envelope, entry);
		sorted_insert(rq, envelope);
	}

	rq->evpcount += update->evpcount;
}

#define SCHEDULEMAX	1024

static void
rq_queue_schedule(struct rq_queue *rq)
{
	struct rq_envelope	*evp;
	size_t			 n;

	n = 0;
	while ((evp = TAILQ_FIRST(&rq->q_pending))) {
		if (evp->sched > currtime && evp->expire > currtime)
			break;

		if (n == SCHEDULEMAX)
			break;

		if (evp->state != RQ_EVPSTATE_PENDING)
			fatalx("evp:%016" PRIx64 " flags=0x%x", evp->evpid,
			    evp->flags);

		if (evp->expire <= currtime) {
			TAILQ_REMOVE(&rq->q_pending, evp, entry);
			SPLAY_REMOVE(prioqtree, &rq->q_priotree, evp);
			TAILQ_INSERT_TAIL(&rq->q_expired, evp, entry);
			evp->state = RQ_EVPSTATE_SCHEDULED;
			evp->flags |= RQ_ENVELOPE_EXPIRED;
			evp->t_scheduled = currtime;
			continue;
		}
		rq_envelope_schedule(rq, evp);
		n += 1;
	}
}

static struct evplist *
rq_envelope_list(struct rq_queue *rq, struct rq_envelope *evp)
{
	switch (evp->state) {
	case RQ_EVPSTATE_PENDING:
		return &rq->q_pending;

	case RQ_EVPSTATE_SCHEDULED:
		if (evp->flags & RQ_ENVELOPE_EXPIRED)
			return &rq->q_expired;
		if (evp->flags & RQ_ENVELOPE_REMOVED)
			return &rq->q_removed;
		if (evp->flags & RQ_ENVELOPE_UPDATE)
			return &rq->q_update;
		if (evp->type == D_MTA)
			return &rq->q_mta;
		if (evp->type == D_MDA)
			return &rq->q_mda;
		if (evp->type == D_BOUNCE)
			return &rq->q_bounce;
		fatalx("%016" PRIx64 " bad evp type %d", evp->evpid, evp->type);

	case RQ_EVPSTATE_INFLIGHT:
		return &rq->q_inflight;

	case RQ_EVPSTATE_HELD:
		return (NULL);
	}

	fatalx("%016" PRIx64 " bad state %d", evp->evpid, evp->state);
	return (NULL);
}

static void
rq_envelope_schedule(struct rq_queue *rq, struct rq_envelope *evp)
{
	struct rq_holdq	*hq;
	struct evplist	*q = NULL;

	switch (evp->type) {
	case D_MTA:
		q = &rq->q_mta;
		break;
	case D_MDA:
		q = &rq->q_mda;
		break;
	case D_BOUNCE:
		q = &rq->q_bounce;
		break;
	}

	if (evp->flags & RQ_ENVELOPE_UPDATE)
		q = &rq->q_update;

	if (evp->state == RQ_EVPSTATE_HELD) {
		hq = tree_xget(&holdqs[evp->type], evp->holdq);
		TAILQ_REMOVE(&hq->q, evp, entry);
		hq->count -= 1;
		if (TAILQ_EMPTY(&hq->q)) {
			tree_xpop(&holdqs[evp->type], evp->holdq);
			free(hq);
		}
		evp->holdq = 0;
		stat_decrement("scheduler.ramqueue.hold", 1);
	}
	else if (!(evp->flags & RQ_ENVELOPE_SUSPEND)) {
		TAILQ_REMOVE(&rq->q_pending, evp, entry);
		SPLAY_REMOVE(prioqtree, &rq->q_priotree, evp);
	}

	TAILQ_INSERT_TAIL(q, evp, entry);
	evp->state = RQ_EVPSTATE_SCHEDULED;
	evp->t_scheduled = currtime;
}

static int
rq_envelope_remove(struct rq_queue *rq, struct rq_envelope *evp)
{
	struct rq_holdq	*hq;
	struct evplist	*evl;

	if (evp->flags & (RQ_ENVELOPE_REMOVED | RQ_ENVELOPE_EXPIRED))
		return (0);
	/*
	 * If envelope is inflight, mark it envelope for removal.
	 */
	if (evp->state == RQ_EVPSTATE_INFLIGHT) {
		evp->flags |= RQ_ENVELOPE_REMOVED;
		return (1);
	}

	if (evp->state == RQ_EVPSTATE_HELD) {
		hq = tree_xget(&holdqs[evp->type], evp->holdq);
		TAILQ_REMOVE(&hq->q, evp, entry);
		hq->count -= 1;
		if (TAILQ_EMPTY(&hq->q)) {
			tree_xpop(&holdqs[evp->type], evp->holdq);
			free(hq);
		}
		evp->holdq = 0;
		stat_decrement("scheduler.ramqueue.hold", 1);
	}
	else if (!(evp->flags & RQ_ENVELOPE_SUSPEND)) {
		evl = rq_envelope_list(rq, evp);
		TAILQ_REMOVE(evl, evp, entry);
		if (evl == &rq->q_pending)
			SPLAY_REMOVE(prioqtree, &rq->q_priotree, evp);
	}

	TAILQ_INSERT_TAIL(&rq->q_removed, evp, entry);
	evp->state = RQ_EVPSTATE_SCHEDULED;
	evp->flags |= RQ_ENVELOPE_REMOVED;
	evp->t_scheduled = currtime;

	return (1);
}

static int
rq_envelope_suspend(struct rq_queue *rq, struct rq_envelope *evp)
{
	struct rq_holdq	*hq;
	struct evplist	*evl;

	if (evp->flags & RQ_ENVELOPE_SUSPEND)
		return (0);

	if (evp->state == RQ_EVPSTATE_HELD) {
		hq = tree_xget(&holdqs[evp->type], evp->holdq);
		TAILQ_REMOVE(&hq->q, evp, entry);
		hq->count -= 1;
		if (TAILQ_EMPTY(&hq->q)) {
			tree_xpop(&holdqs[evp->type], evp->holdq);
			free(hq);
		}
		evp->holdq = 0;
		evp->state = RQ_EVPSTATE_PENDING;
		stat_decrement("scheduler.ramqueue.hold", 1);
	}
	else if (evp->state != RQ_EVPSTATE_INFLIGHT) {
		evl = rq_envelope_list(rq, evp);
		TAILQ_REMOVE(evl, evp, entry);
		if (evl == &rq->q_pending)
			SPLAY_REMOVE(prioqtree, &rq->q_priotree, evp);
	}

	evp->flags |= RQ_ENVELOPE_SUSPEND;

	return (1);
}

static int
rq_envelope_resume(struct rq_queue *rq, struct rq_envelope *evp)
{
	struct evplist	*evl;

	if (!(evp->flags & RQ_ENVELOPE_SUSPEND))
		return (0);

	if (evp->state != RQ_EVPSTATE_INFLIGHT) {
		evl = rq_envelope_list(rq, evp);
		if (evl == &rq->q_pending)
			sorted_insert(rq, evp);
		else
			TAILQ_INSERT_TAIL(evl, evp, entry);
	}

	evp->flags &= ~RQ_ENVELOPE_SUSPEND;

	return (1);
}

static void
rq_envelope_delete(struct rq_queue *rq, struct rq_envelope *evp)
{
	tree_xpop(&evp->message->envelopes, evp->evpid);
	if (tree_empty(&evp->message->envelopes)) {
		tree_xpop(&rq->messages, evp->message->msgid);
		free(evp->message);
		stat_decrement("scheduler.ramqueue.message", 1);
	}

	free(evp);
	rq->evpcount--;
	stat_decrement("scheduler.ramqueue.envelope", 1);
}

static const char *
rq_envelope_to_text(struct rq_envelope *e)
{
	static char	buf[256];
	char		t[64];

	(void)snprintf(buf, sizeof buf, "evp:%016" PRIx64 " [", e->evpid);

	if (e->type == D_BOUNCE)
		(void)strlcat(buf, "bounce", sizeof buf);
	else if (e->type == D_MDA)
		(void)strlcat(buf, "mda", sizeof buf);
	else if (e->type == D_MTA)
		(void)strlcat(buf, "mta", sizeof buf);

	(void)snprintf(t, sizeof t, ",expire=%s",
	    duration_to_text(e->expire - currtime));
	(void)strlcat(buf, t, sizeof buf);


	switch (e->state) {
	case RQ_EVPSTATE_PENDING:
		(void)snprintf(t, sizeof t, ",pending=%s",
		    duration_to_text(e->sched - currtime));
		(void)strlcat(buf, t, sizeof buf);
		break;

	case RQ_EVPSTATE_SCHEDULED:
		(void)snprintf(t, sizeof t, ",scheduled=%s",
		    duration_to_text(currtime - e->t_scheduled));
		(void)strlcat(buf, t, sizeof buf);
		break;

	case RQ_EVPSTATE_INFLIGHT:
		(void)snprintf(t, sizeof t, ",inflight=%s",
		    duration_to_text(currtime - e->t_inflight));
		(void)strlcat(buf, t, sizeof buf);
		break;

	case RQ_EVPSTATE_HELD:
		(void)snprintf(t, sizeof t, ",held=%s",
		    duration_to_text(currtime - e->t_inflight));
		(void)strlcat(buf, t, sizeof buf);
		break;
	default:
		fatalx("%016" PRIx64 " bad state %d", e->evpid, e->state);
	}

	if (e->flags & RQ_ENVELOPE_REMOVED)
		(void)strlcat(buf, ",removed", sizeof buf);
	if (e->flags & RQ_ENVELOPE_EXPIRED)
		(void)strlcat(buf, ",expired", sizeof buf);
	if (e->flags & RQ_ENVELOPE_SUSPEND)
		(void)strlcat(buf, ",suspended", sizeof buf);

	(void)strlcat(buf, "]", sizeof buf);

	return (buf);
}

static void
rq_queue_dump(struct rq_queue *rq, const char * name)
{
	struct rq_message	*message;
	struct rq_envelope	*envelope;
	void			*i, *j;
	uint64_t		 id;

	log_debug("debug: /--- ramqueue: %s", name);

	i = NULL;
	while ((tree_iter(&rq->messages, &i, &id, (void*)&message))) {
		log_debug("debug: | msg:%08" PRIx32, message->msgid);
		j = NULL;
		while ((tree_iter(&message->envelopes, &j, &id,
		    (void*)&envelope)))
			log_debug("debug: |   %s",
			    rq_envelope_to_text(envelope));
	}
	log_debug("debug: \\---");
}

static int
rq_envelope_cmp(struct rq_envelope *e1, struct rq_envelope *e2)
{
	time_t	ref1, ref2;

	ref1 = (e1->sched < e1->expire) ? e1->sched : e1->expire;
	ref2 = (e2->sched < e2->expire) ? e2->sched : e2->expire;
	if (ref1 != ref2)
		return (ref1 < ref2) ? -1 : 1;

	if (e1->evpid != e2->evpid)
		return (e1->evpid < e2->evpid) ? -1 : 1;

	return 0;
}

SPLAY_GENERATE(prioqtree, rq_envelope, t_entry, rq_envelope_cmp);
