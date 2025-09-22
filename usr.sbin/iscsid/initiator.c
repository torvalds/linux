/*	$OpenBSD: initiator.c,v 1.21 2025/01/28 20:41:44 claudio Exp $ */

/*
 * Copyright (c) 2009 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <scsi/iscsi.h>

#include <event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "iscsid.h"
#include "log.h"

static struct initiator *initiator;

struct task_login {
	struct task		 task;
	struct connection	*c;
	u_int16_t		 tsih;
	u_int8_t		 stage;
};

struct task_logout {
	struct task		 task;
	struct connection	*c;
	u_int8_t		 reason;
};

int		 conn_is_leading(struct connection *);
struct kvp	*initiator_login_kvp(struct connection *, u_int8_t);
struct pdu	*initiator_login_build(struct connection *,
		    struct task_login *);
struct pdu	*initiator_text_build(struct task *, struct session *,
		    struct kvp *);

void	initiator_login_cb(struct connection *, void *, struct pdu *);
void	initiator_discovery_cb(struct connection *, void *, struct pdu *);
void	initiator_logout_cb(struct connection *, void *, struct pdu *);

struct session_params		initiator_sess_defaults;
struct connection_params	initiator_conn_defaults;

void
initiator_init(void)
{
	if (!(initiator = calloc(1, sizeof(*initiator))))
		fatal("initiator_init");

	initiator->config.isid_base =
	    arc4random_uniform(0xffffff) | ISCSI_ISID_RAND;
	initiator->config.isid_qual = arc4random_uniform(0xffff);
	TAILQ_INIT(&initiator->sessions);

	/* initialize initiator defaults */
	initiator_sess_defaults = iscsi_sess_defaults;
	initiator_conn_defaults = iscsi_conn_defaults;
	initiator_sess_defaults.MaxConnections = ISCSID_DEF_CONNS;
	initiator_conn_defaults.MaxRecvDataSegmentLength = 65536;
}

void
initiator_cleanup(void)
{
	struct session *s;

	while ((s = TAILQ_FIRST(&initiator->sessions)) != NULL) {
		TAILQ_REMOVE(&initiator->sessions, s, entry);
		session_cleanup(s);
	}
	free(initiator);
}

void
initiator_set_config(struct initiator_config *ic)
{
	initiator->config = *ic;
}

struct initiator_config *
initiator_get_config(void)
{
	return &initiator->config;
}

void
initiator_shutdown(void)
{
	struct session *s;

	log_debug("initiator_shutdown: going down");

	TAILQ_FOREACH(s, &initiator->sessions, entry)
		session_shutdown(s);	
}

int
initiator_isdown(void)
{
	struct session *s;
	int inprogres = 0;

	TAILQ_FOREACH(s, &initiator->sessions, entry) {
		if ((s->state & SESS_RUNNING) && !(s->state & SESS_FREE))
			inprogres = 1;
	}
	return !inprogres;
}

struct session *
initiator_new_session(u_int8_t st)
{
	struct session *s;

	if (!(s = calloc(1, sizeof(*s))))
		return NULL;

	/* use the same qualifier unless there is a conflict */
	s->isid_base = initiator->config.isid_base;
	s->isid_qual = initiator->config.isid_qual;
	s->cmdseqnum = arc4random();
	s->itt = arc4random();
	s->state = SESS_INIT;

	s->sev.sess = s;
	evtimer_set(&s->sev.ev, session_fsm_callback, &s->sev);

	if (st == SESSION_TYPE_DISCOVERY)
		s->target = 0;
	else
		s->target = initiator->target++;

	TAILQ_INIT(&s->connections);
	TAILQ_INIT(&s->tasks);

	TAILQ_INSERT_HEAD(&initiator->sessions, s, entry);

	return s;
}

struct session *
initiator_find_session(char *name)
{
	struct session *s;

	TAILQ_FOREACH(s, &initiator->sessions, entry) {
		if (strcmp(s->config.SessionName, name) == 0)
			return s;
	}
	return NULL;
}

struct session *
initiator_t2s(u_int target)
{
	struct session *s;

	TAILQ_FOREACH(s, &initiator->sessions, entry) {
		if (s->target == target)
			return s;
	}
	return NULL;
}

struct session_head *
initiator_get_sessions(void)
{
	return &initiator->sessions;
}

void
initiator_login(struct connection *c)
{
	struct task_login *tl;
	struct pdu *p;

	if (!(tl = calloc(1, sizeof(*tl)))) {
		log_warn("initiator_login");
		conn_fail(c);
		return;
	}
	tl->c = c;
	tl->stage = ISCSI_LOGIN_STG_SECNEG;

	if (!(p = initiator_login_build(c, tl))) {
		log_warn("initiator_login_build failed");
		free(tl);
		conn_fail(c);
		return;
	}

	task_init(&tl->task, c->session, 1, tl, initiator_login_cb, NULL);
	task_pdu_add(&tl->task, p);
	conn_task_issue(c, &tl->task);
}

void
initiator_discovery(struct session *s)
{
	struct task *t;
	struct pdu *p;
	struct kvp kvp[] = {
		{ "SendTargets", "All" },
		{ NULL, NULL }
	};

	if (!(t = calloc(1, sizeof(*t)))) {
		log_warn("initiator_discovery");
		/* XXX sess_fail(c); */
		return;
	}

	if (!(p = initiator_text_build(t, s, kvp))) {
		log_warnx("initiator_text_build failed");
		free(t);
		/* XXX sess_fail(c); */
		return;
	}

	task_init(t, s, 0, t, initiator_discovery_cb, NULL);
	task_pdu_add(t, p);
	session_task_issue(s, t);
}

void
initiator_logout(struct session *s, struct connection *c, u_int8_t reason)
{
	struct task_logout *tl;
	struct pdu *p;
	struct iscsi_pdu_logout_request *loreq;

	if (!(tl = calloc(1, sizeof(*tl)))) {
		log_warn("initiator_logout");
		/* XXX sess_fail */
		return;
	}
	tl->c = c;
	tl->reason = reason;

	if (!(p = pdu_new())) {
		log_warn("initiator_logout");
		/* XXX sess_fail */
		free(tl);
		return;
	}
	if (!(loreq = pdu_gethdr(p))) {
		log_warn("initiator_logout");
		/* XXX sess_fail */
		pdu_free(p);
		free(tl);
		return;
	}

	loreq->opcode = ISCSI_OP_LOGOUT_REQUEST;
	loreq->flags = ISCSI_LOGOUT_F | reason;
	if (reason != ISCSI_LOGOUT_CLOSE_SESS)
		loreq->cid = c->cid;

	task_init(&tl->task, s, 0, tl, initiator_logout_cb, NULL);
	task_pdu_add(&tl->task, p);
	if (c && (c->state & CONN_RUNNING))
		conn_task_issue(c, &tl->task);
	else
		session_logout_issue(s, &tl->task);
}

void
initiator_nop_in_imm(struct connection *c, struct pdu *p)
{
	struct iscsi_pdu_nop_in *nopin;
	struct task *t;

	/* fixup NOP-IN to make it a NOP-OUT */
	nopin = pdu_getbuf(p, NULL, PDU_HEADER);
	nopin->maxcmdsn = 0;
	nopin->opcode = ISCSI_OP_I_NOP | ISCSI_OP_F_IMMEDIATE;

	/* and schedule an immediate task */
	if (!(t = calloc(1, sizeof(*t)))) {
		log_warn("initiator_nop_in_imm");
		pdu_free(p);
		return;
	}

	task_init(t, c->session, 1, NULL, NULL, NULL);
	t->itt = 0xffffffff; /* change ITT because it is just a ping reply */
	task_pdu_add(t, p);
	conn_task_issue(c, t);
}

int
conn_is_leading(struct connection *c)
{
	return c == TAILQ_FIRST(&c->session->connections);
}

#define MINE_NOT_DEFAULT(c, k) ((c)->mine.k != iscsi_conn_defaults.k)

struct kvp *
initiator_login_kvp(struct connection *c, u_int8_t stage)
{
	struct kvp *kvp = NULL;
	size_t i = 0, len;
	const char *discovery[] = {"SessionType", "InitiatorName",
	    "AuthMethod", NULL};
	const char *leading_only[] = {"MaxConnections", "InitialR2T",
	    "ImmediateData", "MaxBurstLength", "FirstBurstLength",
	    "DefaultTime2Wait", "DefaultTime2Retain", "MaxOutstandingR2T",
	    "DataPDUInOrder", "DataSequenceInOrder", "ErrorRecoveryLevel",
	    NULL};
	const char *opneg_always[] = {"HeaderDigest", "DataDigest", NULL};
	const char *secneg[] = {"SessionType", "InitiatorName", "TargetName",
	    "AuthMethod", NULL};
	const char **p, **q;

	switch (stage) {
	case ISCSI_LOGIN_STG_SECNEG:
		if (c->session->config.SessionType == SESSION_TYPE_DISCOVERY) {
			len = sizeof(discovery) / sizeof(*discovery);
			q = discovery;
		} else {
			len = sizeof(secneg) / sizeof(*secneg);
			q = secneg;
		}
		if (!(kvp = calloc(len + 1, sizeof(*kvp))))
			return NULL;
		for (p = q; *p != NULL; i++, p++)
			if (kvp_set_from_mine(&kvp[i], *p, c))
				goto fail;
		break;
	case ISCSI_LOGIN_STG_OPNEG:
		len = sizeof(opneg_always) / sizeof(*opneg_always);
		if (conn_is_leading(c))
			len += sizeof(leading_only) / sizeof(*leading_only);
		if (MINE_NOT_DEFAULT(c, MaxRecvDataSegmentLength))
			len++;
		if (!(kvp = calloc(len + 1, sizeof(*kvp))))
			return NULL;
		for (p = opneg_always; *p != NULL; i++, p++)
			if (kvp_set_from_mine(&kvp[i], *p, c))
				goto fail;
		if (conn_is_leading(c))
			for (p = leading_only; *p != NULL; i++, p++)
				if (kvp_set_from_mine(&kvp[i], *p, c))
					goto fail;
		if (MINE_NOT_DEFAULT(c, MaxRecvDataSegmentLength) &&
		    kvp_set_from_mine(&kvp[i], "MaxRecvDataSegmentLength", c))
			goto fail;
		break;
	default:
		log_warnx("initiator_login_kvp: exit stage left");
		return NULL;
	} 
	return kvp;
fail:
	kvp_free(kvp);
	return NULL;
}

#undef MINE_NOT_DEFAULT
struct pdu *
initiator_login_build(struct connection *c, struct task_login *tl)
{
	struct pdu *p;
	struct kvp *kvp;
	struct iscsi_pdu_login_request *lreq;
	int n;

	if (!(p = pdu_new()))
		return NULL;
	if (!(lreq = pdu_gethdr(p))) {
		pdu_free(p);
		return NULL;
	}

	lreq->opcode = ISCSI_OP_LOGIN_REQUEST | ISCSI_OP_F_IMMEDIATE;
	if (tl->stage == ISCSI_LOGIN_STG_SECNEG)
		lreq->flags = ISCSI_LOGIN_F_T |
		    ISCSI_LOGIN_F_CSG(ISCSI_LOGIN_STG_SECNEG) |
		    ISCSI_LOGIN_F_NSG(ISCSI_LOGIN_STG_OPNEG);
	else if (tl->stage == ISCSI_LOGIN_STG_OPNEG)
		lreq->flags = ISCSI_LOGIN_F_T |
		    ISCSI_LOGIN_F_CSG(ISCSI_LOGIN_STG_OPNEG) |
		    ISCSI_LOGIN_F_NSG(ISCSI_LOGIN_STG_FULL);

	lreq->isid_base = htonl(tl->c->session->isid_base);
	lreq->isid_qual = htons(tl->c->session->isid_qual);
	lreq->tsih = tl->tsih;
	lreq->cid = htons(tl->c->cid);
	lreq->expstatsn = htonl(tl->c->expstatsn);

	if (!(kvp = initiator_login_kvp(c, tl->stage))) {
		log_warn("initiator_login_kvp failed");
		return NULL;
	}
	if ((n = text_to_pdu(kvp, p)) == -1) {
		kvp_free(kvp);
		return NULL;
	}
	kvp_free(kvp);

	if (n > 8192) {
		log_warn("initiator_login_build: help, I'm too verbose");
		pdu_free(p);
		return NULL;
	}
	n = htonl(n);
	/* copy 32bit value over ahslen and datalen */
	memcpy(&lreq->ahslen, &n, sizeof(n));

	return p;
}

struct pdu *
initiator_text_build(struct task *t, struct session *s, struct kvp *kvp)
{
	struct pdu *p;
	struct iscsi_pdu_text_request *lreq;
	int n;

	if (!(p = pdu_new()))
		return NULL;
	if (!(lreq = pdu_gethdr(p)))
		return NULL;

	lreq->opcode = ISCSI_OP_TEXT_REQUEST;
	lreq->flags = ISCSI_TEXT_F_F;
	lreq->ttt = 0xffffffff;

	if ((n = text_to_pdu(kvp, p)) == -1)
		return NULL;
	n = htonl(n);
	memcpy(&lreq->ahslen, &n, sizeof(n));

	return p;
}

void
initiator_login_cb(struct connection *c, void *arg, struct pdu *p)
{
	struct task_login *tl = arg;
	struct iscsi_pdu_login_response *lresp;
	u_char *buf = NULL;
	struct kvp *kvp;
	size_t n, size;

	lresp = pdu_getbuf(p, NULL, PDU_HEADER);

	if (ISCSI_PDU_OPCODE(lresp->opcode) != ISCSI_OP_LOGIN_RESPONSE) {
		log_warnx("Unexpected login response type %x",
		    ISCSI_PDU_OPCODE(lresp->opcode));
		conn_fail(c);
		goto done;
	}

	if (lresp->flags & ISCSI_LOGIN_F_C) {
		log_warnx("Incomplete login responses are unsupported");
		conn_fail(c);
		goto done;
	}

	size = lresp->datalen[0] << 16 | lresp->datalen[1] << 8 |
	    lresp->datalen[2];
	buf = pdu_getbuf(p, &n, PDU_DATA);
	if (size > n) {
		log_warnx("Bad login response");
		conn_fail(c);
		goto done;
	}

	if (buf) {
		kvp = pdu_to_text(buf, size);
		if (kvp == NULL) {
			conn_fail(c);
			goto done;
		}

		if (conn_parse_kvp(c, kvp) == -1) {
			kvp_free(kvp);
			conn_fail(c);
			goto done;
		}
		kvp_free(kvp);
	}

	/* advance FSM if possible */
	if (lresp->flags & ISCSI_LOGIN_F_T)
		tl->stage = ISCSI_LOGIN_F_NSG(lresp->flags);

	switch (tl->stage) {
	case ISCSI_LOGIN_STG_SECNEG:
	case ISCSI_LOGIN_STG_OPNEG:
		/* free no longer used pdu */
		pdu_free(p);
		p = initiator_login_build(c, tl);
		if (p == NULL) {
			conn_fail(c);
			goto done;
		}
		break;
	case ISCSI_LOGIN_STG_FULL:
		conn_fsm(c, CONN_EV_LOGGED_IN);
		conn_task_cleanup(c, &tl->task);
		free(tl);
		goto done;
	default:
		log_warnx("initiator_login_cb: exit stage left");
		conn_fail(c);
		goto done;
	}
	conn_task_cleanup(c, &tl->task);
	/* add new pdu and re-issue the task */
	task_pdu_add(&tl->task, p);
	conn_task_issue(c, &tl->task);
	return;
done:
	if (p)
		pdu_free(p);
}

void
initiator_discovery_cb(struct connection *c, void *arg, struct pdu *p)
{
	struct task *t = arg;
	struct iscsi_pdu_text_response *lresp;
	u_char *buf = NULL;
	struct kvp *kvp, *k;
	size_t n, size;

	lresp = pdu_getbuf(p, NULL, PDU_HEADER);
	switch (ISCSI_PDU_OPCODE(lresp->opcode)) {
	case ISCSI_OP_TEXT_RESPONSE:
		size = lresp->datalen[0] << 16 | lresp->datalen[1] << 8 |
		    lresp->datalen[2];
		if (size == 0) {
			/* empty response */
			session_shutdown(c->session);
			break;
		}
		buf = pdu_getbuf(p, &n, PDU_DATA);
		if (size > n || buf == NULL)
			goto fail;
		kvp = pdu_to_text(buf, size);
		if (kvp == NULL)
			goto fail;
		log_debug("ISCSI_OP_TEXT_RESPONSE");
		for (k = kvp; k->key; k++) {
			log_debug("%s\t=>\t%s", k->key, k->value);
		}
		kvp_free(kvp);
		session_shutdown(c->session);
		break;
	default:
		log_debug("initiator_discovery_cb: unexpected message type %x",
		    ISCSI_PDU_OPCODE(lresp->opcode));
fail:
		conn_fail(c);
		pdu_free(p);
		return;
	}
	conn_task_cleanup(c, t);
	free(t);
	pdu_free(p);
}

void
initiator_logout_cb(struct connection *c, void *arg, struct pdu *p)
{
	struct task_logout *tl = arg;
	struct iscsi_pdu_logout_response *loresp;

	loresp = pdu_getbuf(p, NULL, PDU_HEADER);
	log_debug("initiator_logout_cb: "
	    "response %d, Time2Wait %d, Time2Retain %d",
	    loresp->response, ntohs(loresp->time2wait),
	    ntohs(loresp->time2retain));

	switch (loresp->response) {
	case ISCSI_LOGOUT_RESP_SUCCESS:
		if (tl->reason == ISCSI_LOGOUT_CLOSE_SESS) {
			conn_fsm(c, CONN_EV_LOGGED_OUT);
			session_fsm(&c->session->sev, SESS_EV_CLOSED, 0);
		} else {
			conn_fsm(tl->c, CONN_EV_LOGGED_OUT);
			session_fsm(&tl->c->sev, SESS_EV_CONN_CLOSED, 0);
		}
		break;
	case ISCSI_LOGOUT_RESP_UNKN_CID:
		/* connection ID not found, retry will not help */
		log_warnx("%s: logout failed, cid %d unknown, giving up\n",
		    tl->c->session->config.SessionName,
		    tl->c->cid);
		conn_fsm(tl->c, CONN_EV_FREE);
		break;
	case ISCSI_LOGOUT_RESP_NO_SUPPORT:
	case ISCSI_LOGOUT_RESP_ERROR:
	default:
		/* need to retry logout after loresp->time2wait secs */
		conn_fail(tl->c);
		pdu_free(p);
		return;
	}

	conn_task_cleanup(c, &tl->task);
	free(tl);
	pdu_free(p);
}

char *
default_initiator_name(void)
{
	char *s, hostname[HOST_NAME_MAX+1];

	if (gethostname(hostname, sizeof(hostname)))
		strlcpy(hostname, "initiator", sizeof(hostname));
	if ((s = strchr(hostname, '.')))
		*s = '\0';
	if (asprintf(&s, "%s:%s", ISCSID_BASE_NAME, hostname) == -1)
		return ISCSID_BASE_NAME ":initiator";
	return s;
}
