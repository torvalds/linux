/*	$OpenBSD: scheduler.c,v 1.62 2021/06/14 17:58:16 eric Exp $	*/

/*
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2008-2009 Jacek Masiulaniec <jacekm@dobremiasto.net>
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
#include <pwd.h>
#include <signal.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"

static void scheduler_imsg(struct mproc *, struct imsg *);
static void scheduler_shutdown(void);
static void scheduler_reset_events(void);
static void scheduler_timeout(int, short, void *);

static struct scheduler_backend *backend = NULL;
static struct event		 ev;
static size_t			 ninflight = 0;
static int			*types;
static uint64_t			*evpids;
static uint32_t			*msgids;
static struct evpstate		*state;

extern const char *backend_scheduler;

void
scheduler_imsg(struct mproc *p, struct imsg *imsg)
{
	struct bounce_req_msg	 req;
	struct envelope		 evp;
	struct scheduler_info	 si;
	struct msg		 m;
	uint64_t		 evpid, id, holdq;
	uint32_t		 msgid;
	uint32_t       		 inflight;
	size_t			 n, i;
	time_t			 timestamp;
	int			 v, r, type;

	if (imsg == NULL)
		scheduler_shutdown();

	switch (imsg->hdr.type) {

	case IMSG_QUEUE_ENVELOPE_SUBMIT:
		m_msg(&m, imsg);
		m_get_envelope(&m, &evp);
		m_end(&m);
		log_trace(TRACE_SCHEDULER,
		    "scheduler: inserting evp:%016" PRIx64, evp.id);
		scheduler_info(&si, &evp);
		stat_increment("scheduler.envelope.incoming", 1);
		backend->insert(&si);
		return;

	case IMSG_QUEUE_MESSAGE_COMMIT:
		m_msg(&m, imsg);
		m_get_msgid(&m, &msgid);
		m_end(&m);
		log_trace(TRACE_SCHEDULER,
		    "scheduler: committing msg:%08" PRIx32, msgid);
		n = backend->commit(msgid);
		stat_decrement("scheduler.envelope.incoming", n);
		stat_increment("scheduler.envelope", n);
		scheduler_reset_events();
		return;

	case IMSG_QUEUE_DISCOVER_EVPID:
		m_msg(&m, imsg);
		m_get_envelope(&m, &evp);
		m_end(&m);
		r = backend->query(evp.id);
		if (r) {
			log_debug("debug: scheduler: evp:%016" PRIx64
			    " already scheduled", evp.id);
			return;
		}
		log_trace(TRACE_SCHEDULER,
		    "scheduler: discovering evp:%016" PRIx64, evp.id);
		scheduler_info(&si, &evp);
		stat_increment("scheduler.envelope.incoming", 1);
		backend->insert(&si);
		return;

	case IMSG_QUEUE_DISCOVER_MSGID:
		m_msg(&m, imsg);
		m_get_msgid(&m, &msgid);
		m_end(&m);
		r = backend->query(msgid);
		if (r) {
			log_debug("debug: scheduler: msgid:%08" PRIx32
			    " already scheduled", msgid);
			return;
		}
		log_trace(TRACE_SCHEDULER,
		    "scheduler: committing msg:%08" PRIx32, msgid);
		n = backend->commit(msgid);
		stat_decrement("scheduler.envelope.incoming", n);
		stat_increment("scheduler.envelope", n);
		scheduler_reset_events();
		return;

	case IMSG_QUEUE_MESSAGE_ROLLBACK:
		m_msg(&m, imsg);
		m_get_msgid(&m, &msgid);
		m_end(&m);
		log_trace(TRACE_SCHEDULER, "scheduler: aborting msg:%08" PRIx32,
		    msgid);
		n = backend->rollback(msgid);
		stat_decrement("scheduler.envelope.incoming", n);
		scheduler_reset_events();
		return;

	case IMSG_QUEUE_ENVELOPE_REMOVE:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_get_u32(&m, &inflight);
		m_end(&m);
		log_trace(TRACE_SCHEDULER,
		    "scheduler: queue requested removal of evp:%016" PRIx64,
		    evpid);
		stat_decrement("scheduler.envelope", 1);
		if (!inflight)
			backend->remove(evpid);
		else {
			backend->delete(evpid);
			ninflight -= 1;
			stat_decrement("scheduler.envelope.inflight", 1);
		}

		scheduler_reset_events();
		return;

	case IMSG_QUEUE_ENVELOPE_ACK:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_end(&m);
		log_trace(TRACE_SCHEDULER,
		    "scheduler: queue ack removal of evp:%016" PRIx64,
		    evpid);
		ninflight -= 1;
		stat_decrement("scheduler.envelope.inflight", 1);
		scheduler_reset_events();
		return;

	case IMSG_QUEUE_DELIVERY_OK:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_end(&m);
		log_trace(TRACE_SCHEDULER,
		    "scheduler: deleting evp:%016" PRIx64 " (ok)", evpid);
		backend->delete(evpid);
		ninflight -= 1;
		stat_increment("scheduler.delivery.ok", 1);
		stat_decrement("scheduler.envelope.inflight", 1);
		stat_decrement("scheduler.envelope", 1);
		scheduler_reset_events();
		return;

	case IMSG_QUEUE_DELIVERY_TEMPFAIL:
		m_msg(&m, imsg);
		m_get_envelope(&m, &evp);
		m_end(&m);
		log_trace(TRACE_SCHEDULER,
		    "scheduler: updating evp:%016" PRIx64, evp.id);
		scheduler_info(&si, &evp);
		backend->update(&si);
		ninflight -= 1;
		stat_increment("scheduler.delivery.tempfail", 1);
		stat_decrement("scheduler.envelope.inflight", 1);

		for (i = 0; i < MAX_BOUNCE_WARN; i++) {
			if (env->sc_bounce_warn[i] == 0)
				break;
			timestamp = si.creation + env->sc_bounce_warn[i];
			if (si.nexttry >= timestamp &&
			    si.lastbounce < timestamp) {
	    			req.evpid = evp.id;
				req.timestamp = timestamp;
				req.bounce.type = B_DELAYED;
				req.bounce.delay = env->sc_bounce_warn[i];
				req.bounce.ttl = si.ttl;
				m_compose(p, IMSG_SCHED_ENVELOPE_BOUNCE, 0, 0, -1,
				    &req, sizeof req);
				break;
			}
		}
		scheduler_reset_events();
		return;

	case IMSG_QUEUE_DELIVERY_PERMFAIL:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_end(&m);
		log_trace(TRACE_SCHEDULER,
		    "scheduler: deleting evp:%016" PRIx64 " (fail)", evpid);
		backend->delete(evpid);
		ninflight -= 1;
		stat_increment("scheduler.delivery.permfail", 1);
		stat_decrement("scheduler.envelope.inflight", 1);
		stat_decrement("scheduler.envelope", 1);
		scheduler_reset_events();
		return;

	case IMSG_QUEUE_DELIVERY_LOOP:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_end(&m);
		log_trace(TRACE_SCHEDULER,
		    "scheduler: deleting evp:%016" PRIx64 " (loop)", evpid);
		backend->delete(evpid);
		ninflight -= 1;
		stat_increment("scheduler.delivery.loop", 1);
		stat_decrement("scheduler.envelope.inflight", 1);
		stat_decrement("scheduler.envelope", 1);
		scheduler_reset_events();
		return;

	case IMSG_QUEUE_HOLDQ_HOLD:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_get_id(&m, &holdq);
		m_end(&m);
		log_trace(TRACE_SCHEDULER,
		    "scheduler: holding evp:%016" PRIx64 " on %016" PRIx64,
		    evpid, holdq);
		backend->hold(evpid, holdq);
		ninflight -= 1;
		stat_decrement("scheduler.envelope.inflight", 1);
		scheduler_reset_events();
		return;

	case IMSG_QUEUE_HOLDQ_RELEASE:
		m_msg(&m, imsg);
		m_get_int(&m, &type);
		m_get_id(&m, &holdq);
		m_get_int(&m, &r);
		m_end(&m);
		log_trace(TRACE_SCHEDULER,
		    "scheduler: releasing %d on holdq (%d, %016" PRIx64 ")",
		    r, type, holdq);
		backend->release(type, holdq, r);
		scheduler_reset_events();
		return;

	case IMSG_CTL_PAUSE_MDA:
		log_trace(TRACE_SCHEDULER, "scheduler: pausing mda");
		env->sc_flags |= SMTPD_MDA_PAUSED;
		return;

	case IMSG_CTL_RESUME_MDA:
		log_trace(TRACE_SCHEDULER, "scheduler: resuming mda");
		env->sc_flags &= ~SMTPD_MDA_PAUSED;
		scheduler_reset_events();
		return;

	case IMSG_CTL_PAUSE_MTA:
		log_trace(TRACE_SCHEDULER, "scheduler: pausing mta");
		env->sc_flags |= SMTPD_MTA_PAUSED;
		return;

	case IMSG_CTL_RESUME_MTA:
		log_trace(TRACE_SCHEDULER, "scheduler: resuming mta");
		env->sc_flags &= ~SMTPD_MTA_PAUSED;
		scheduler_reset_events();
		return;

	case IMSG_CTL_VERBOSE:
		m_msg(&m, imsg);
		m_get_int(&m, &v);
		m_end(&m);
		log_setverbose(v);
		return;

	case IMSG_CTL_PROFILE:
		m_msg(&m, imsg);
		m_get_int(&m, &v);
		m_end(&m);
		profiling = v;
		return;

	case IMSG_CTL_LIST_MESSAGES:
		msgid = *(uint32_t *)(imsg->data);
		n = backend->messages(msgid, msgids, env->sc_scheduler_max_msg_batch_size);
		m_compose(p, IMSG_CTL_LIST_MESSAGES, imsg->hdr.peerid, 0, -1,
		    msgids, n * sizeof (*msgids));
		return;

	case IMSG_CTL_LIST_ENVELOPES:
		id = *(uint64_t *)(imsg->data);
		n = backend->envelopes(id, state, env->sc_scheduler_max_evp_batch_size);
		for (i = 0; i < n; i++) {
			m_create(p_queue, IMSG_CTL_LIST_ENVELOPES,
			    imsg->hdr.peerid, 0, -1);
			m_add_evpid(p_queue, state[i].evpid);
			m_add_int(p_queue, state[i].flags);
			m_add_time(p_queue, state[i].time);
			m_close(p_queue);
		}
		m_compose(p_queue, IMSG_CTL_LIST_ENVELOPES,
		    imsg->hdr.peerid, 0, -1, NULL, 0);
		return;

	case IMSG_CTL_SCHEDULE:
		id = *(uint64_t *)(imsg->data);
		if (id <= 0xffffffffL)
			log_debug("debug: scheduler: "
			    "scheduling msg:%08" PRIx64, id);
		else
			log_debug("debug: scheduler: "
			    "scheduling evp:%016" PRIx64, id);
		r = backend->schedule(id);
		scheduler_reset_events();
		m_compose(p, r ? IMSG_CTL_OK : IMSG_CTL_FAIL, imsg->hdr.peerid,
		    0, -1, NULL, 0);
		return;

	case IMSG_QUEUE_ENVELOPE_SCHEDULE:
		id = *(uint64_t *)(imsg->data);
		backend->schedule(id);
		scheduler_reset_events();
		return;

	case IMSG_CTL_REMOVE:
		id = *(uint64_t *)(imsg->data);
		if (id <= 0xffffffffL)
			log_debug("debug: scheduler: "
			    "removing msg:%08" PRIx64, id);
		else
			log_debug("debug: scheduler: "
			    "removing evp:%016" PRIx64, id);
		r = backend->remove(id);
		scheduler_reset_events();
		m_compose(p, r ? IMSG_CTL_OK : IMSG_CTL_FAIL, imsg->hdr.peerid,
		    0, -1, NULL, 0);
		return;

	case IMSG_CTL_PAUSE_EVP:
		id = *(uint64_t *)(imsg->data);
		if (id <= 0xffffffffL)
			log_debug("debug: scheduler: "
			    "suspending msg:%08" PRIx64, id);
		else
			log_debug("debug: scheduler: "
			    "suspending evp:%016" PRIx64, id);
		r = backend->suspend(id);
		scheduler_reset_events();
		m_compose(p, r ? IMSG_CTL_OK : IMSG_CTL_FAIL, imsg->hdr.peerid,
		    0, -1, NULL, 0);
		return;

	case IMSG_CTL_RESUME_EVP:
		id = *(uint64_t *)(imsg->data);
		if (id <= 0xffffffffL)
			log_debug("debug: scheduler: "
			    "resuming msg:%08" PRIx64, id);
		else
			log_debug("debug: scheduler: "
			    "resuming evp:%016" PRIx64, id);
		r = backend->resume(id);
		scheduler_reset_events();
		m_compose(p, r ? IMSG_CTL_OK : IMSG_CTL_FAIL, imsg->hdr.peerid,
		    0, -1, NULL, 0);
		return;
	}

	fatalx("scheduler_imsg: unexpected %s imsg",
	    imsg_to_str(imsg->hdr.type));
}

static void
scheduler_shutdown(void)
{
	log_debug("debug: scheduler agent exiting");
	_exit(0);
}

static void
scheduler_reset_events(void)
{
	struct timeval	 tv;

	evtimer_del(&ev);
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	evtimer_add(&ev, &tv);
}

int
scheduler(void)
{
	struct passwd	*pw;

	backend = scheduler_backend_lookup(backend_scheduler);
	if (backend == NULL)
		fatalx("cannot find scheduler backend \"%s\"",
		    backend_scheduler);

	purge_config(PURGE_EVERYTHING & ~PURGE_DISPATCHERS);

	if ((pw = getpwnam(SMTPD_USER)) == NULL)
		fatalx("unknown user " SMTPD_USER);

	config_process(PROC_SCHEDULER);

	backend->init(backend_scheduler);

	if (chroot(PATH_CHROOT) == -1)
		fatal("scheduler: chroot");
	if (chdir("/") == -1)
		fatal("scheduler: chdir(\"/\")");

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("scheduler: cannot drop privileges");

	evpids = xcalloc(env->sc_scheduler_max_schedule, sizeof *evpids);
	types = xcalloc(env->sc_scheduler_max_schedule, sizeof *types);
	msgids = xcalloc(env->sc_scheduler_max_msg_batch_size, sizeof *msgids);
	state = xcalloc(env->sc_scheduler_max_evp_batch_size, sizeof *state);

	imsg_callback = scheduler_imsg;
	event_init();

	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	config_peer(PROC_CONTROL);
	config_peer(PROC_QUEUE);

	evtimer_set(&ev, scheduler_timeout, NULL);
	scheduler_reset_events();

	if (pledge("stdio", NULL) == -1)
		fatal("pledge");

	event_dispatch();
	fatalx("exited event loop");

	return (0);
}

static void
scheduler_timeout(int fd, short event, void *p)
{
	struct timeval		tv;
	size_t			i;
	size_t			d_inflight;
	size_t			d_envelope;
	size_t			d_removed;
	size_t			d_expired;
	size_t			d_updated;
	size_t			count;
	int			mask, r, delay;

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	mask = SCHED_UPDATE;

	if (ninflight <  env->sc_scheduler_max_inflight) {
		mask |= SCHED_EXPIRE | SCHED_REMOVE | SCHED_BOUNCE;
		if (!(env->sc_flags & SMTPD_MDA_PAUSED))
			mask |= SCHED_MDA;
		if (!(env->sc_flags & SMTPD_MTA_PAUSED))
			mask |= SCHED_MTA;
	}

	count = env->sc_scheduler_max_schedule;

	log_trace(TRACE_SCHEDULER, "scheduler: getting batch: mask=0x%x, count=%zu", mask, count);

	r = backend->batch(mask, &delay, &count, evpids, types);

	log_trace(TRACE_SCHEDULER, "scheduler: got r=%i, delay=%i, count=%zu", r, delay, count);

	if (r < 0)
		fatalx("scheduler: error in batch handler");

	if (r == 0) {

		if (delay < -1)
			fatalx("scheduler: invalid delay %d", delay);

		if (delay == -1) {
			log_trace(TRACE_SCHEDULER, "scheduler: sleeping");
			return;
		}

		tv.tv_sec = delay;
		tv.tv_usec = 0;
		log_trace(TRACE_SCHEDULER,
		    "scheduler: waiting for %s", duration_to_text(tv.tv_sec));
		evtimer_add(&ev, &tv);
		return;
	}

	d_inflight = 0;
	d_envelope = 0;
	d_removed = 0;
	d_expired = 0;
	d_updated = 0;

	for (i = 0; i < count; i++) {
		switch(types[i]) {
		case SCHED_REMOVE:
			log_debug("debug: scheduler: evp:%016" PRIx64
			    " removed", evpids[i]);
			m_create(p_queue, IMSG_SCHED_ENVELOPE_REMOVE, 0, 0, -1);
			m_add_evpid(p_queue, evpids[i]);
			m_close(p_queue);
			d_envelope += 1;
			d_removed += 1;
			d_inflight += 1;
			break;

		case SCHED_EXPIRE:
			log_debug("debug: scheduler: evp:%016" PRIx64
			    " expired", evpids[i]);
			m_create(p_queue, IMSG_SCHED_ENVELOPE_EXPIRE, 0, 0, -1);
			m_add_evpid(p_queue, evpids[i]);
			m_close(p_queue);
			d_envelope += 1;
			d_expired += 1;
			d_inflight += 1;
			break;

		case SCHED_UPDATE:
			log_debug("debug: scheduler: evp:%016" PRIx64
			    " scheduled (update)", evpids[i]);
			d_updated += 1;
			break;

		case SCHED_BOUNCE:
			log_debug("debug: scheduler: evp:%016" PRIx64
			    " scheduled (bounce)", evpids[i]);
			m_create(p_queue, IMSG_SCHED_ENVELOPE_INJECT, 0, 0, -1);
			m_add_evpid(p_queue, evpids[i]);
			m_close(p_queue);
			d_inflight += 1;
			break;

		case SCHED_MDA:
			log_debug("debug: scheduler: evp:%016" PRIx64
			    " scheduled (mda)", evpids[i]);
			m_create(p_queue, IMSG_SCHED_ENVELOPE_DELIVER, 0, 0, -1);
			m_add_evpid(p_queue, evpids[i]);
			m_close(p_queue);
			d_inflight += 1;
			break;

		case SCHED_MTA:
			log_debug("debug: scheduler: evp:%016" PRIx64
			    " scheduled (mta)", evpids[i]);
			m_create(p_queue, IMSG_SCHED_ENVELOPE_TRANSFER, 0, 0, -1);
			m_add_evpid(p_queue, evpids[i]);
			m_close(p_queue);
			d_inflight += 1;
			break;
		}
	}

	stat_decrement("scheduler.envelope", d_envelope);
	stat_increment("scheduler.envelope.inflight", d_inflight);
	stat_increment("scheduler.envelope.expired", d_expired);
	stat_increment("scheduler.envelope.removed", d_removed);
	stat_increment("scheduler.envelope.updated", d_updated);

	ninflight += d_inflight;

	tv.tv_sec = 0;
	tv.tv_usec = 0;
	evtimer_add(&ev, &tv);
}
