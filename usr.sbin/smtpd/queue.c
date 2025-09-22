/*	$OpenBSD: queue.c,v 1.196 2024/01/20 09:01:03 claudio Exp $	*/

/*
 * Copyright (c) 2008 Gilles Chehade <gilles@poolp.org>
 * Copyright (c) 2008 Pierre-Yves Ritschard <pyr@openbsd.org>
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
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "smtpd.h"
#include "log.h"

static void queue_imsg(struct mproc *, struct imsg *);
static void queue_timeout(int, short, void *);
static void queue_bounce(struct envelope *, struct delivery_bounce *);
static void queue_shutdown(void);
static void queue_log(const struct envelope *, const char *, const char *);
static void queue_msgid_walk(int, short, void *);


static void
queue_imsg(struct mproc *p, struct imsg *imsg)
{
	struct delivery_bounce	 bounce;
	struct msg_walkinfo	*wi;
	struct timeval		 tv;
	struct bounce_req_msg	*req_bounce;
	struct envelope		 evp;
	struct msg		 m;
	const char		*reason;
	uint64_t		 reqid, evpid, holdq;
	uint32_t		 msgid;
	time_t			 nexttry;
	size_t			 n_evp;
	int			 fd, mta_ext, ret, v, flags, code;

	if (imsg == NULL)
		queue_shutdown();

	memset(&bounce, 0, sizeof(struct delivery_bounce));

	switch (imsg->hdr.type) {
	case IMSG_SMTP_MESSAGE_CREATE:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_end(&m);

		ret = queue_message_create(&msgid);

		m_create(p, IMSG_SMTP_MESSAGE_CREATE, 0, 0, -1);
		m_add_id(p, reqid);
		if (ret == 0)
			m_add_int(p, 0);
		else {
			m_add_int(p, 1);
			m_add_msgid(p, msgid);
		}
		m_close(p);
		return;

	case IMSG_SMTP_MESSAGE_ROLLBACK:
		m_msg(&m, imsg);
		m_get_msgid(&m, &msgid);
		m_end(&m);

		queue_message_delete(msgid);

		m_create(p_scheduler, IMSG_QUEUE_MESSAGE_ROLLBACK,
		    0, 0, -1);
		m_add_msgid(p_scheduler, msgid);
		m_close(p_scheduler);
		return;

	case IMSG_SMTP_MESSAGE_COMMIT:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_msgid(&m, &msgid);
		m_end(&m);

		ret = queue_message_commit(msgid);

		m_create(p, IMSG_SMTP_MESSAGE_COMMIT, 0, 0, -1);
		m_add_id(p, reqid);
		m_add_int(p, (ret == 0) ? 0 : 1);
		m_close(p);

		if (ret) {
			m_create(p_scheduler, IMSG_QUEUE_MESSAGE_COMMIT,
			    0, 0, -1);
			m_add_msgid(p_scheduler, msgid);
			m_close(p_scheduler);
		}
		return;

	case IMSG_SMTP_MESSAGE_OPEN:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_msgid(&m, &msgid);
		m_end(&m);

		fd = queue_message_fd_rw(msgid);

		m_create(p, IMSG_SMTP_MESSAGE_OPEN, 0, 0, fd);
		m_add_id(p, reqid);
		m_add_int(p, (fd == -1) ? 0 : 1);
		m_close(p);
		return;

	case IMSG_QUEUE_SMTP_SESSION:
		bounce_fd(imsg_get_fd(imsg));
		return;

	case IMSG_LKA_ENVELOPE_SUBMIT:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_envelope(&m, &evp);
		m_end(&m);

		if (evp.id == 0)
			log_warnx("warn: imsg_queue_submit_envelope: evpid=0");
		if (evpid_to_msgid(evp.id) == 0)
			log_warnx("warn: imsg_queue_submit_envelope: msgid=0, "
			    "evpid=%016"PRIx64, evp.id);
		ret = queue_envelope_create(&evp);
		m_create(p_dispatcher, IMSG_QUEUE_ENVELOPE_SUBMIT, 0, 0, -1);
		m_add_id(p_dispatcher, reqid);
		if (ret == 0)
			m_add_int(p_dispatcher, 0);
		else {
			m_add_int(p_dispatcher, 1);
			m_add_evpid(p_dispatcher, evp.id);
		}
		m_close(p_dispatcher);
		if (ret) {
			m_create(p_scheduler,
			    IMSG_QUEUE_ENVELOPE_SUBMIT, 0, 0, -1);
			m_add_envelope(p_scheduler, &evp);
			m_close(p_scheduler);
		}
		return;

	case IMSG_LKA_ENVELOPE_COMMIT:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_end(&m);
		m_create(p_dispatcher, IMSG_QUEUE_ENVELOPE_COMMIT, 0, 0, -1);
		m_add_id(p_dispatcher, reqid);
		m_add_int(p_dispatcher, 1);
		m_close(p_dispatcher);
		return;

	case IMSG_SCHED_ENVELOPE_REMOVE:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_end(&m);

		m_create(p_scheduler, IMSG_QUEUE_ENVELOPE_ACK, 0, 0, -1);
		m_add_evpid(p_scheduler, evpid);
		m_close(p_scheduler);

		/* already removed by scheduler */
		if (queue_envelope_load(evpid, &evp) == 0)
			return;

		queue_log(&evp, "Remove", "Removed by administrator");
		queue_envelope_delete(evpid);
		return;

	case IMSG_SCHED_ENVELOPE_EXPIRE:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_end(&m);

		m_create(p_scheduler, IMSG_QUEUE_ENVELOPE_ACK, 0, 0, -1);
		m_add_evpid(p_scheduler, evpid);
		m_close(p_scheduler);

		/* already removed by scheduler*/
		if (queue_envelope_load(evpid, &evp) == 0)
			return;

		bounce.type = B_FAILED;
		envelope_set_errormsg(&evp, "Envelope expired");
		envelope_set_esc_class(&evp, ESC_STATUS_PERMFAIL);
		envelope_set_esc_code(&evp, ESC_DELIVERY_TIME_EXPIRED);
		queue_bounce(&evp, &bounce);
		queue_log(&evp, "Expire", "Envelope expired");
		queue_envelope_delete(evpid);
		return;

	case IMSG_SCHED_ENVELOPE_BOUNCE:
		CHECK_IMSG_DATA_SIZE(imsg, sizeof *req_bounce);
		req_bounce = imsg->data;
		evpid = req_bounce->evpid;

		if (queue_envelope_load(evpid, &evp) == 0) {
			log_warnx("queue: bounce: failed to load envelope");
			m_create(p_scheduler, IMSG_QUEUE_ENVELOPE_REMOVE, 0, 0, -1);
			m_add_evpid(p_scheduler, evpid);
			m_add_u32(p_scheduler, 0); /* not in-flight */
			m_close(p_scheduler);
			return;
		}
		queue_bounce(&evp, &req_bounce->bounce);
		evp.lastbounce = req_bounce->timestamp;
		if (!queue_envelope_update(&evp))
			log_warnx("warn: could not update envelope %016"PRIx64, evpid);
		return;

	case IMSG_SCHED_ENVELOPE_DELIVER:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_end(&m);
		if (queue_envelope_load(evpid, &evp) == 0) {
			log_warnx("queue: deliver: failed to load envelope");
			m_create(p_scheduler, IMSG_QUEUE_ENVELOPE_REMOVE, 0, 0, -1);
			m_add_evpid(p_scheduler, evpid);
			m_add_u32(p_scheduler, 1); /* in-flight */
			m_close(p_scheduler);
			return;
		}
		evp.lasttry = time(NULL);
		m_create(p_dispatcher, IMSG_QUEUE_DELIVER, 0, 0, -1);
		m_add_envelope(p_dispatcher, &evp);
		m_close(p_dispatcher);
		return;

	case IMSG_SCHED_ENVELOPE_INJECT:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_end(&m);
		bounce_add(evpid);
		return;

	case IMSG_SCHED_ENVELOPE_TRANSFER:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_end(&m);
		if (queue_envelope_load(evpid, &evp) == 0) {
			log_warnx("queue: failed to load envelope");
			m_create(p_scheduler, IMSG_QUEUE_ENVELOPE_REMOVE, 0, 0, -1);
			m_add_evpid(p_scheduler, evpid);
			m_add_u32(p_scheduler, 1); /* in-flight */
			m_close(p_scheduler);
			return;
		}
		evp.lasttry = time(NULL);
		m_create(p_dispatcher, IMSG_QUEUE_TRANSFER, 0, 0, -1);
		m_add_envelope(p_dispatcher, &evp);
		m_close(p_dispatcher);
		return;

	case IMSG_CTL_LIST_ENVELOPES:
		if (imsg->hdr.len == sizeof imsg->hdr) {
			m_forward(p_control, imsg);
			return;
		}

		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_get_int(&m, &flags);
		m_get_time(&m, &nexttry);
		m_end(&m);

		if (queue_envelope_load(evpid, &evp) == 0)
			return; /* Envelope is gone, drop it */

		/*
		 * XXX consistency: The envelope might already be on
		 * its way back to the scheduler.  We need to detect
		 * this properly and report that state.
		 */
		if (flags & EF_INFLIGHT) {
			/*
			 * Not exactly correct but pretty close: The
			 * value is not recorded on the envelope unless
			 * a tempfail occurs.
			 */
			evp.lasttry = nexttry;
		}

		m_create(p_control, IMSG_CTL_LIST_ENVELOPES,
		    imsg->hdr.peerid, 0, -1);
		m_add_int(p_control, flags);
		m_add_time(p_control, nexttry);
		m_add_envelope(p_control, &evp);
		m_close(p_control);
		return;

	case IMSG_MDA_OPEN_MESSAGE:
	case IMSG_MTA_OPEN_MESSAGE:
		m_msg(&m, imsg);
		m_get_id(&m, &reqid);
		m_get_msgid(&m, &msgid);
		m_end(&m);
		fd = queue_message_fd_r(msgid);
		m_create(p, imsg->hdr.type, 0, 0, fd);
		m_add_id(p, reqid);
		m_close(p);
		return;

	case IMSG_MDA_DELIVERY_OK:
	case IMSG_MTA_DELIVERY_OK:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		if (imsg->hdr.type == IMSG_MTA_DELIVERY_OK)
			m_get_int(&m, &mta_ext);
		m_end(&m);
		if (queue_envelope_load(evpid, &evp) == 0) {
			log_warn("queue: dsn: failed to load envelope");
			return;
		}
		if (evp.dsn_notify & DSN_SUCCESS) {
			bounce.type = B_DELIVERED;
			bounce.dsn_ret = evp.dsn_ret;
			envelope_set_esc_class(&evp, ESC_STATUS_OK);
			if (imsg->hdr.type == IMSG_MDA_DELIVERY_OK)
				queue_bounce(&evp, &bounce);
			else if (imsg->hdr.type == IMSG_MTA_DELIVERY_OK &&
			    (mta_ext & MTA_EXT_DSN) == 0) {
				bounce.mta_without_dsn = 1;
				queue_bounce(&evp, &bounce);
			}
		}
		queue_envelope_delete(evpid);
		m_create(p_scheduler, IMSG_QUEUE_DELIVERY_OK, 0, 0, -1);
		m_add_evpid(p_scheduler, evpid);
		m_close(p_scheduler);
		return;

	case IMSG_MDA_DELIVERY_TEMPFAIL:
	case IMSG_MTA_DELIVERY_TEMPFAIL:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_get_string(&m, &reason);
		m_get_int(&m, &code);
		m_end(&m);
		if (queue_envelope_load(evpid, &evp) == 0) {
			log_warnx("queue: tempfail: failed to load envelope");
			m_create(p_scheduler, IMSG_QUEUE_ENVELOPE_REMOVE, 0, 0, -1);
			m_add_evpid(p_scheduler, evpid);
			m_add_u32(p_scheduler, 1); /* in-flight */
			m_close(p_scheduler);
			return;
		}
		envelope_set_errormsg(&evp, "%s", reason);
		envelope_set_esc_class(&evp, ESC_STATUS_TEMPFAIL);
		envelope_set_esc_code(&evp, code);
		evp.retry++;
		if (!queue_envelope_update(&evp))
			log_warnx("warn: could not update envelope %016"PRIx64, evpid);
		m_create(p_scheduler, IMSG_QUEUE_DELIVERY_TEMPFAIL, 0, 0, -1);
		m_add_envelope(p_scheduler, &evp);
		m_close(p_scheduler);
		return;

	case IMSG_MDA_DELIVERY_PERMFAIL:
	case IMSG_MTA_DELIVERY_PERMFAIL:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_get_string(&m, &reason);
		m_get_int(&m, &code);
		m_end(&m);
		if (queue_envelope_load(evpid, &evp) == 0) {
			log_warnx("queue: permfail: failed to load envelope");
			m_create(p_scheduler, IMSG_QUEUE_ENVELOPE_REMOVE, 0, 0, -1);
			m_add_evpid(p_scheduler, evpid);
			m_add_u32(p_scheduler, 1); /* in-flight */
			m_close(p_scheduler);
			return;
		}
		bounce.type = B_FAILED;
		envelope_set_errormsg(&evp, "%s", reason);
		envelope_set_esc_class(&evp, ESC_STATUS_PERMFAIL);
		envelope_set_esc_code(&evp, code);
		queue_bounce(&evp, &bounce);
		queue_envelope_delete(evpid);
		m_create(p_scheduler, IMSG_QUEUE_DELIVERY_PERMFAIL, 0, 0, -1);
		m_add_evpid(p_scheduler, evpid);
		m_close(p_scheduler);
		return;

	case IMSG_MDA_DELIVERY_LOOP:
	case IMSG_MTA_DELIVERY_LOOP:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_end(&m);
		if (queue_envelope_load(evpid, &evp) == 0) {
			log_warnx("queue: loop: failed to load envelope");
			m_create(p_scheduler, IMSG_QUEUE_ENVELOPE_REMOVE, 0, 0, -1);
			m_add_evpid(p_scheduler, evpid);
			m_add_u32(p_scheduler, 1); /* in-flight */
			m_close(p_scheduler);
			return;
		}
		envelope_set_errormsg(&evp, "%s", "Loop detected");
		envelope_set_esc_class(&evp, ESC_STATUS_TEMPFAIL);
		envelope_set_esc_code(&evp, ESC_ROUTING_LOOP_DETECTED);
		bounce.type = B_FAILED;
		queue_bounce(&evp, &bounce);
		queue_envelope_delete(evp.id);
		m_create(p_scheduler, IMSG_QUEUE_DELIVERY_LOOP, 0, 0, -1);
		m_add_evpid(p_scheduler, evp.id);
		m_close(p_scheduler);
		return;

	case IMSG_MTA_DELIVERY_HOLD:
	case IMSG_MDA_DELIVERY_HOLD:
		imsg->hdr.type = IMSG_QUEUE_HOLDQ_HOLD;
		m_forward(p_scheduler, imsg);
		return;

	case IMSG_MTA_SCHEDULE:
		imsg->hdr.type = IMSG_QUEUE_ENVELOPE_SCHEDULE;
		m_forward(p_scheduler, imsg);
		return;

	case IMSG_MTA_HOLDQ_RELEASE:
	case IMSG_MDA_HOLDQ_RELEASE:
		m_msg(&m, imsg);
		m_get_id(&m, &holdq);
		m_get_int(&m, &v);
		m_end(&m);
		m_create(p_scheduler, IMSG_QUEUE_HOLDQ_RELEASE, 0, 0, -1);
		if (imsg->hdr.type == IMSG_MTA_HOLDQ_RELEASE)
			m_add_int(p_scheduler, D_MTA);
		else
			m_add_int(p_scheduler, D_MDA);
		m_add_id(p_scheduler, holdq);
		m_add_int(p_scheduler, v);
		m_close(p_scheduler);
		return;

	case IMSG_CTL_PAUSE_MDA:
	case IMSG_CTL_PAUSE_MTA:
	case IMSG_CTL_RESUME_MDA:
	case IMSG_CTL_RESUME_MTA:
		m_forward(p_scheduler, imsg);
		return;

	case IMSG_CTL_VERBOSE:
		m_msg(&m, imsg);
		m_get_int(&m, &v);
		m_end(&m);
		log_trace_verbose(v);
		return;

	case IMSG_CTL_PROFILE:
		m_msg(&m, imsg);
		m_get_int(&m, &v);
		m_end(&m);
		profiling = v;
		return;

	case IMSG_CTL_DISCOVER_EVPID:
		m_msg(&m, imsg);
		m_get_evpid(&m, &evpid);
		m_end(&m);
		if (queue_envelope_load(evpid, &evp) == 0) {
			log_warnx("queue: discover: failed to load "
			    "envelope %016" PRIx64, evpid);
			n_evp = 0;
			m_compose(p_control, imsg->hdr.type,
			    imsg->hdr.peerid, 0, -1,
			    &n_evp, sizeof n_evp);
			return;
		}

		m_create(p_scheduler, IMSG_QUEUE_DISCOVER_EVPID,
		    0, 0, -1);
		m_add_envelope(p_scheduler, &evp);
		m_close(p_scheduler);

		m_create(p_scheduler, IMSG_QUEUE_DISCOVER_MSGID,
		    0, 0, -1);
		m_add_msgid(p_scheduler, evpid_to_msgid(evpid));
		m_close(p_scheduler);
		n_evp = 1;
		m_compose(p_control, imsg->hdr.type, imsg->hdr.peerid,
		    0, -1, &n_evp, sizeof n_evp);
		return;

	case IMSG_CTL_DISCOVER_MSGID:
		m_msg(&m, imsg);
		m_get_msgid(&m, &msgid);
		m_end(&m);
		/* handle concurrent walk requests */
		wi = xcalloc(1, sizeof *wi);
		wi->msgid = msgid;
		wi->peerid = imsg->hdr.peerid;
		evtimer_set(&wi->ev, queue_msgid_walk, wi);
		tv.tv_sec = 0;
		tv.tv_usec = 10;
		evtimer_add(&wi->ev, &tv);
		return;
	}

	fatalx("queue_imsg: unexpected %s imsg", imsg_to_str(imsg->hdr.type));
}

static void
queue_msgid_walk(int fd, short event, void *arg)
{
	struct envelope		 evp;
	struct timeval		 tv;
	struct msg_walkinfo	*wi = arg;
	int			 r;

	r = queue_message_walk(&evp, wi->msgid, &wi->done, &wi->data);
	if (r == -1) {
		if (wi->n_evp) {
			m_create(p_scheduler, IMSG_QUEUE_DISCOVER_MSGID,
			    0, 0, -1);
			m_add_msgid(p_scheduler, wi->msgid);
			m_close(p_scheduler);
		}

		m_compose(p_control, IMSG_CTL_DISCOVER_MSGID, wi->peerid, 0, -1,
		    &wi->n_evp, sizeof wi->n_evp);
		evtimer_del(&wi->ev);
		free(wi);
		return;
	}

	if (r) {
		m_create(p_scheduler, IMSG_QUEUE_DISCOVER_EVPID, 0, 0, -1);
		m_add_envelope(p_scheduler, &evp);
		m_close(p_scheduler);
		wi->n_evp += 1;
	}

	tv.tv_sec = 0;
	tv.tv_usec = 10;
	evtimer_set(&wi->ev, queue_msgid_walk, wi);
	evtimer_add(&wi->ev, &tv);
}

static void
queue_bounce(struct envelope *e, struct delivery_bounce *d)
{
	struct envelope	b;

	b = *e;
	b.type = D_BOUNCE;
	b.agent.bounce = *d;
	b.retry = 0;
	b.lasttry = 0;
	b.creation = time(NULL);
	b.ttl = 3600 * 24 * 7;

	if (e->dsn_notify & DSN_NEVER)
		return;

	if (b.id == 0)
		log_warnx("warn: queue_bounce: evpid=0");
	if (evpid_to_msgid(b.id) == 0)
		log_warnx("warn: queue_bounce: msgid=0, evpid=%016"PRIx64,
			b.id);
	if (e->type == D_BOUNCE) {
		log_warnx("warn: queue: double bounce!");
	} else if (e->sender.user[0] == '\0') {
		log_warnx("warn: queue: no return path!");
	} else if (!queue_envelope_create(&b)) {
		log_warnx("warn: queue: cannot bounce!");
	} else {
		log_debug("debug: queue: bouncing evp:%016" PRIx64
		    " as evp:%016" PRIx64, e->id, b.id);

		m_create(p_scheduler, IMSG_QUEUE_ENVELOPE_SUBMIT, 0, 0, -1);
		m_add_envelope(p_scheduler, &b);
		m_close(p_scheduler);

		m_create(p_scheduler, IMSG_QUEUE_MESSAGE_COMMIT, 0, 0, -1);
		m_add_msgid(p_scheduler, evpid_to_msgid(b.id));
		m_close(p_scheduler);

		stat_increment("queue.bounce", 1);
	}
}

static void
queue_shutdown(void)
{
	log_debug("debug: queue agent exiting");
	queue_close();
	_exit(0);
}

int
queue(void)
{
	struct passwd	*pw;
	struct timeval	 tv;
	struct event	 ev_qload;

	purge_config(PURGE_EVERYTHING & ~PURGE_DISPATCHERS);

	if ((pw = getpwnam(SMTPD_QUEUE_USER)) == NULL)
		if ((pw = getpwnam(SMTPD_USER)) == NULL)
			fatalx("unknown user " SMTPD_USER);

	env->sc_queue_flags |= QUEUE_EVPCACHE;
	env->sc_queue_evpcache_size = 1024;

	if (chroot(PATH_SPOOL) == -1)
		fatal("queue: chroot");
	if (chdir("/") == -1)
		fatal("queue: chdir(\"/\")");

	config_process(PROC_QUEUE);

	if (env->sc_queue_flags & QUEUE_COMPRESSION)
		log_info("queue: queue compression enabled");

	if (env->sc_queue_key) {
		if (!crypto_setup(env->sc_queue_key, strlen(env->sc_queue_key)))
			fatalx("crypto_setup: invalid key for queue encryption");
		log_info("queue: queue encryption enabled");
	}

	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid))
		fatal("queue: cannot drop privileges");

	imsg_callback = queue_imsg;
	event_init();

	signal(SIGINT, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	config_peer(PROC_PARENT);
	config_peer(PROC_CONTROL);
	config_peer(PROC_LKA);
	config_peer(PROC_SCHEDULER);
	config_peer(PROC_DISPATCHER);

	/* setup queue loading task */
	evtimer_set(&ev_qload, queue_timeout, &ev_qload);
	tv.tv_sec = 0;
	tv.tv_usec = 10;
	evtimer_add(&ev_qload, &tv);

	if (pledge("stdio rpath wpath cpath flock recvfd sendfd", NULL) == -1)
		fatal("pledge");

	event_dispatch();
	fatalx("exited event loop");

	return (0);
}

static void
queue_timeout(int fd, short event, void *p)
{
	static uint32_t	 msgid = 0;
	struct envelope	 evp;
	struct event	*ev = p;
	struct timeval	 tv;
	int		 r;

	r = queue_envelope_walk(&evp);
	if (r == -1) {
		if (msgid) {
			m_create(p_scheduler, IMSG_QUEUE_MESSAGE_COMMIT,
			    0, 0, -1);
			m_add_msgid(p_scheduler, msgid);
			m_close(p_scheduler);
		}
		log_debug("debug: queue: done loading queue into scheduler");
		return;
	}

	if (r) {
		if (msgid && evpid_to_msgid(evp.id) != msgid) {
			m_create(p_scheduler, IMSG_QUEUE_MESSAGE_COMMIT,
			    0, 0, -1);
			m_add_msgid(p_scheduler, msgid);
			m_close(p_scheduler);
		}
		msgid = evpid_to_msgid(evp.id);
		m_create(p_scheduler, IMSG_QUEUE_ENVELOPE_SUBMIT, 0, 0, -1);
		m_add_envelope(p_scheduler, &evp);
		m_close(p_scheduler);
	}

	tv.tv_sec = 0;
	tv.tv_usec = 10;
	evtimer_add(ev, &tv);
}

static void
queue_log(const struct envelope *e, const char *prefix, const char *status)
{
	char rcpt[LINE_MAX];

	(void)strlcpy(rcpt, "-", sizeof rcpt);
	if (strcmp(e->rcpt.user, e->dest.user) ||
	    strcmp(e->rcpt.domain, e->dest.domain))
		(void)snprintf(rcpt, sizeof rcpt, "%s@%s",
		    e->rcpt.user, e->rcpt.domain);

	log_info("%s: %s for %016" PRIx64 ": from=<%s@%s>, to=<%s@%s>, "
	    "rcpt=<%s>, delay=%s, stat=%s",
	    e->type == D_MDA ? "delivery" : "relay",
	    prefix,
	    e->id, e->sender.user, e->sender.domain,
	    e->dest.user, e->dest.domain,
	    rcpt,
	    duration_to_text(time(NULL) - e->creation),
	    status);
}
