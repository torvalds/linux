/*	$OpenBSD: session.c,v 1.13 2025/01/22 16:06:36 claudio Exp $ */

/*
 * Copyright (c) 2011 Claudio Jeker <claudio@openbsd.org>
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <scsi/iscsi.h>
#include <scsi/scsi_all.h>
#include <dev/vscsivar.h>

#include <event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iscsid.h"
#include "log.h"

int	sess_do_start(struct session *, struct sessev *);
int	sess_do_conn_loggedin(struct session *, struct sessev *);
int	sess_do_conn_fail(struct session *, struct sessev *);
int	sess_do_conn_closed(struct session *, struct sessev *);
int	sess_do_stop(struct session *, struct sessev *);
int	sess_do_free(struct session *, struct sessev *);
int	sess_do_reinstatement(struct session *, struct sessev *);

const char *sess_state(int);
const char *sess_event(enum s_event);

void
session_cleanup(struct session *s)
{
	struct connection *c;

	taskq_cleanup(&s->tasks);

	while ((c = TAILQ_FIRST(&s->connections)) != NULL)
		conn_free(c);

	free(s->config.TargetName);
	free(s->config.InitiatorName);
	free(s);
}

int
session_shutdown(struct session *s)
{
	log_debug("session[%s] going down", s->config.SessionName);

	s->action = SESS_ACT_DOWN;
	if (s->state & (SESS_INIT | SESS_FREE)) {
		/* no active session, so do a quick cleanup */
		struct connection *c;
		while ((c = TAILQ_FIRST(&s->connections)) != NULL)
			conn_free(c);
		return 0;
	}

	/* cleanup task queue and issue a logout */
	taskq_cleanup(&s->tasks);
	initiator_logout(s, NULL, ISCSI_LOGOUT_CLOSE_SESS);

	return 1;
}

void
session_config(struct session *s, struct session_config *sc)
{
	free(s->config.TargetName);
	s->config.TargetName = NULL;
	free(s->config.InitiatorName);
	s->config.InitiatorName = NULL;

	s->config = *sc;

	if (sc->TargetName) {
		s->config.TargetName = strdup(sc->TargetName);
		if (s->config.TargetName == NULL)
			fatal("strdup");
	}
	if (sc->InitiatorName) {
		s->config.InitiatorName = strdup(sc->InitiatorName);
		if (s->config.InitiatorName == NULL)
			fatal("strdup");
	} else
		s->config.InitiatorName = default_initiator_name();
}

void
session_task_issue(struct session *s, struct task *t)
{
	TAILQ_INSERT_TAIL(&s->tasks, t, entry);
	session_schedule(s);
}

void
session_logout_issue(struct session *s, struct task *t)
{
	struct connection *c, *rc = NULL;

	/* find first free session or first available session */
	TAILQ_FOREACH(c, &s->connections, entry) {
		if (conn_task_ready(c)) {
			conn_fsm(c, CONN_EV_LOGOUT);
			conn_task_issue(c, t);
			return;
		}
		if (c->state & CONN_RUNNING)
			rc = c;
	}

	if (rc) {
		conn_fsm(rc, CONN_EV_LOGOUT);
		conn_task_issue(rc, t);
		return;
	}

	/* XXX must open new connection, gulp */
	fatalx("session_logout_issue needs more work");
}

void
session_schedule(struct session *s)
{
	struct task *t = TAILQ_FIRST(&s->tasks);
	struct connection *c;

	if (!t)
		return;

	/* XXX IMMEDIATE TASK NEED SPECIAL HANDLING !!!! */

	/* wake up a idle connection or a not busy one */
	/* XXX this needs more work as it makes the daemon go wrooOOOMM */
	TAILQ_FOREACH(c, &s->connections, entry)
		if (conn_task_ready(c)) {
			TAILQ_REMOVE(&s->tasks, t, entry);
			conn_task_issue(c, t);
			return;
		}
}

/*
 * The session FSM runs from a callback so that the connection FSM can finish.
 */
void
session_fsm(struct sessev *sev, enum s_event event, unsigned int timeout)
{
	struct session *s = sev->sess;
	struct timeval tv;

	log_debug("session_fsm[%s]: %s ev %s timeout %d",
	    s->config.SessionName, sess_state(s->state),
	    sess_event(event), timeout);

	sev->event = event;

	timerclear(&tv);
	tv.tv_sec = timeout;
	if (evtimer_add(&sev->ev, &tv) == -1)
		fatal("session_fsm");
}

struct {
	int		state;
	enum s_event	event;
	int		(*action)(struct session *, struct sessev *);
} s_fsm[] = {
	{ SESS_INIT, SESS_EV_START, sess_do_start },
	{ SESS_FREE, SESS_EV_START, sess_do_start },
	{ SESS_FREE, SESS_EV_CONN_LOGGED_IN, sess_do_conn_loggedin },	/* N1 */
	{ SESS_FREE, SESS_EV_CLOSED, sess_do_stop },
	{ SESS_LOGGED_IN, SESS_EV_CONN_LOGGED_IN, sess_do_conn_loggedin },
	{ SESS_RUNNING, SESS_EV_CONN_CLOSED, sess_do_conn_closed },	/* N3 */
	{ SESS_RUNNING, SESS_EV_CONN_FAIL, sess_do_conn_fail },		/* N5 */
	{ SESS_RUNNING, SESS_EV_CLOSED, sess_do_free },		/* XXX */
	{ SESS_FAILED, SESS_EV_START, sess_do_start },
	{ SESS_FAILED, SESS_EV_TIMEOUT, sess_do_free },			/* N6 */
	{ SESS_FAILED, SESS_EV_FREE, sess_do_free },			/* N6 */
	{ SESS_FAILED, SESS_EV_CONN_LOGGED_IN, sess_do_reinstatement },	/* N4 */
	{ 0, 0, NULL }
};

void
session_fsm_callback(int fd, short event, void *arg)
{
	struct sessev *sev = arg;
	struct session *s = sev->sess;
	int	i, ns;

	for (i = 0; s_fsm[i].action != NULL; i++) {
		if (s->state & s_fsm[i].state &&
		    sev->event == s_fsm[i].event) {
			log_debug("sess_fsm[%s]: %s ev %s",
			    s->config.SessionName, sess_state(s->state),
			    sess_event(sev->event));
			ns = s_fsm[i].action(s, sev);
			if (ns == -1)
				/* XXX better please */
				fatalx("sess_fsm: action failed");
			log_debug("sess_fsm[%s]: new state %s",
			    s->config.SessionName,
			    sess_state(ns));
			s->state = ns;
			break;
		}
	}
	if (s_fsm[i].action == NULL) {
		log_warnx("sess_fsm[%s]: unhandled state transition "
		    "[%s, %s]", s->config.SessionName,
		    sess_state(s->state), sess_event(sev->event));
		fatalx("bjork bjork bjork");
	}
}

int
sess_do_start(struct session *s, struct sessev *sev)
{
	log_debug("new connection to %s",
	    log_sockaddr(&s->config.connection.TargetAddr));

	/* initialize the session params, and reset the active state */
	s->mine = initiator_sess_defaults;
	s->his = iscsi_sess_defaults;
	s->active = iscsi_sess_defaults;

	if (s->config.SessionType != SESSION_TYPE_DISCOVERY &&
	    s->config.MaxConnections)
		s->mine.MaxConnections = s->config.MaxConnections;

	conn_new(s, &s->config.connection);

	/* XXX kill SESS_FREE it seems to be bad */
	if (s->state == SESS_INIT)
		return SESS_FREE;
	else
		return s->state;
}

int
sess_do_conn_loggedin(struct session *s, struct sessev *sev)
{
	if (s->state & SESS_LOGGED_IN)
		return SESS_LOGGED_IN;

	if (s->config.SessionType == SESSION_TYPE_DISCOVERY) {
		initiator_discovery(s);
		return SESS_LOGGED_IN;
	}

	iscsi_merge_sess_params(&s->active, &s->mine, &s->his);
	vscsi_event(VSCSI_REQPROBE, s->target, -1);
	s->holdTimer = 0;

	return SESS_LOGGED_IN;
}

int
sess_do_conn_fail(struct session *s, struct sessev *sev)
{
	struct connection *c = sev->conn;
	int state = SESS_FREE;

	if (sev->conn == NULL) {
		log_warnx("Just what do you think you're doing, Dave?");
		return -1;
	}

	/*
	 * cleanup connections:
	 * Connections in state FREE can be removed.
	 * Connections in any error state will cause the session to enter
	 * the FAILED state. If no sessions are left and the session was
	 * not already FREE then implicit recovery needs to be done.
	 */

	switch (c->state) {
	case CONN_FREE:
		conn_free(c);
		break;
	case CONN_CLEANUP_WAIT:
		break;
	default:
		log_warnx("It can only be attributable to human error.");
		return -1;
	}

	TAILQ_FOREACH(c, &s->connections, entry) {
		if (c->state & CONN_FAILED) {
			state = SESS_FAILED;
			conn_fsm(c, CONN_EV_CLEANING_UP);
		} else if (c->state & CONN_RUNNING && state != SESS_FAILED)
			state = SESS_LOGGED_IN;
	}

	session_fsm(&s->sev, SESS_EV_START, s->holdTimer);
	/* exponential back-off on constant failure */
	if (s->holdTimer < ISCSID_HOLD_TIME_MAX)
		s->holdTimer = s->holdTimer ? s->holdTimer * 2 : 1;

	return state;
}

int
sess_do_conn_closed(struct session *s, struct sessev *sev)
{
	struct connection *c = sev->conn;
	int state = SESS_FREE;

	if (c == NULL || c->state != CONN_FREE) {
		log_warnx("Just what do you think you're doing, Dave?");
		return -1;
	}
	conn_free(c);

	TAILQ_FOREACH(c, &s->connections, entry) {
		if (c->state & CONN_FAILED) {
			state = SESS_FAILED;
			break;
		} else if (c->state & CONN_RUNNING)
			state = SESS_LOGGED_IN;
	}

	return state;
}

int
sess_do_stop(struct session *s, struct sessev *sev)
{
	struct connection *c;

	/* XXX do graceful closing of session and go to INIT state at the end */

	while ((c = TAILQ_FIRST(&s->connections)) != NULL)
		conn_free(c);

	/* XXX anything else to reset to initial state? */
	return SESS_INIT;
}

int
sess_do_free(struct session *s, struct sessev *sev)
{
	struct connection *c;

	while ((c = TAILQ_FIRST(&s->connections)) != NULL)
		conn_free(c);

	return SESS_FREE;
}

const char *conn_state(int);


int
sess_do_reinstatement(struct session *s, struct sessev *sev)
{
	struct connection *c, *nc;

	TAILQ_FOREACH_SAFE(c, &s->connections, entry, nc) {
		log_debug("sess reinstatement[%s]: %s",
		    s->config.SessionName, conn_state(c->state));

		if (c->state & CONN_FAILED) {
			conn_fsm(c, CONN_EV_FREE);
			conn_free(c);
		}
	}

	return SESS_LOGGED_IN;
}

const char *
sess_state(int s)
{
	static char buf[15];

	switch (s) {
	case SESS_INIT:
		return "INIT";
	case SESS_FREE:
		return "FREE";
	case SESS_LOGGED_IN:
		return "LOGGED_IN";
	case SESS_FAILED:
		return "FAILED";
	default:
		snprintf(buf, sizeof(buf), "UKNWN %x", s);
		return buf;
	}
	/* NOTREACHED */
}

const char *
sess_event(enum s_event e)
{
	static char buf[15];

	switch (e) {
	case SESS_EV_START:
		return "start";
	case SESS_EV_STOP:
		return "stop";
	case SESS_EV_CONN_LOGGED_IN:
		return "connection logged in";
	case SESS_EV_CONN_FAIL:
		return "connection fail";
	case SESS_EV_CONN_CLOSED:
		return "connection closed";
	case SESS_EV_REINSTATEMENT:
		return "connection reinstated";
	case SESS_EV_CLOSED:
		return "session closed";
	case SESS_EV_TIMEOUT:
		return "timeout";
	case SESS_EV_FREE:
		return "free";
	case SESS_EV_FAIL:
		return "fail";
	}

	snprintf(buf, sizeof(buf), "UKNWN %d", e);
	return buf;
}
