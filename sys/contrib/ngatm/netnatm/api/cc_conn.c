/*
 * Copyright (c) 2003-2007
 *	Hartmut Brandt
 *	All rights reserved.
 *
 * Copyright (c) 2001-2002
 *	Fraunhofer Institute for Open Communication Systems (FhG Fokus).
 *	All rights reserved.
 *
 * Author: Harti Brandt <harti@freebsd.org>
 *
 * Redistribution of this software and documentation and use in source and
 * binary forms, with or without modification, are permitted provided that
 * the following conditions are met:
 *
 * 1. Redistributions of source code or documentation must retain the above
 *    copyright notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE AND DOCUMENTATION IS PROVIDED BY THE AUTHOR
 * AND ITS CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHOR OR ITS CONTRIBUTORS  BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: cc_conn.c 1291 2007-07-10 10:35:38Z brandt_h $
 *
 * ATM API as defined per af-saa-0108
 *
 * Lower half - connection handling
 */
#include <netnatm/unimsg.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/api/unisap.h>
#include <netnatm/sig/unidef.h>
#include <netnatm/api/atmapi.h>
#include <netnatm/api/ccatm.h>
#include <netnatm/api/ccpriv.h>

static const char *stab[] = {
#define DEF(N) [N] = #N,
	CONN_STATES
#undef DEF
};

static const char *ptab[] = {
#define DEF(N) [PARTY_##N] = #N,
	PARTY_STATES
#undef DEF
};

const char *
cc_conn_state2str(u_int s)
{
	if (s >= sizeof(stab) / sizeof(stab[0]) || stab[s] == NULL)
		return ("?");
	return (stab[s]);
}

void
cc_conn_set_state(struct ccconn *conn, enum conn_state ns)
{
	if (conn->state != ns) {
		if (conn->cc->log & CCLOG_CONN_STATE)
			cc_conn_log(conn, "%s -> %s",
			    stab[conn->state], stab[ns]);
		conn->state = ns;
	}
}

const char *
cc_party_state2str(u_int s)
{
	if (s >= sizeof(ptab) / sizeof(ptab[0]) || ptab[s] == NULL)
		return ("?");
	return (ptab[s]);
}

void
cc_party_set_state(struct ccparty *party, enum party_state ns)
{

	if (party->state != ns) {
		if (party->conn->cc->log & CCLOG_PARTY_STATE)
			cc_party_log(party, "%s -> %s",
			    ptab[party->state], ptab[ns]);
		party->state = ns;
	}
}

/*
 * Remove connection from its user's queue
 */
void
cc_disconnect_from_user(struct ccconn *conn)
{

	if (conn->user == NULL)
		cc_conn_log(conn, "no %s", "user");
	else {
		TAILQ_REMOVE(&conn->user->connq, conn, connq_link);
		conn->user->queue_act--;
		conn->user = NULL;
	}
}

/*
 * Put connection on user queue
 */
void
cc_connect_to_user(struct ccconn *conn, struct ccuser *user)
{

	if (conn->user != NULL)
		cc_conn_log(conn, "still connected to %p", conn->user);
	conn->user = user;
	TAILQ_INSERT_TAIL(&user->connq, conn, connq_link);
	conn->user->queue_act++;
}

/*
 * Send a signal to the UNI stack for this connection
 */
static void
cc_send_uni(struct ccconn *conn, u_int op, struct uni_msg *msg)
{
	struct ccreq *r;

	r = CCZALLOC(sizeof(*r));
	if (r == NULL) {
		if (msg != NULL)
			uni_msg_destroy(msg);
		cc_conn_log(conn, "no memory for cookie op=%u", op);
		return;
	}

	if ((r->cookie = ++conn->port->cc->cookie) == 0)
		r->cookie = ++conn->port->cc->cookie;
	r->req = op;
	r->conn = conn;

	TAILQ_INSERT_TAIL(&conn->port->cookies, r, link);

	conn->port->cc->funcs->send_uni(conn, conn->port->uarg, op,
	    r->cookie, msg);
}

/*
 * Send a RELEASE.request for this connection.
 */
static void
do_release_request(struct ccconn *conn, const struct uni_ie_cause cause[2])
{
	struct uni_msg *u;
	struct uniapi_release_request *req;

	if ((u = uni_msg_alloc(sizeof(*req))) == NULL)
		return;
	req = uni_msg_wptr(u, struct uniapi_release_request *);
	memset(req, 0, sizeof(*req));
	u->b_wptr += sizeof(struct uniapi_release_request);

	req->release.hdr.cref = conn->cref;
	req->release.hdr.act = UNI_MSGACT_DEFAULT;

	if (cause == NULL) {
		IE_SETPRESENT(req->release.cause[0]);
		req->release.cause[0].h.act = UNI_IEACT_DEFAULT;
		req->release.cause[0].loc = UNI_CAUSE_LOC_USER;
		req->release.cause[0].cause = UNI_CAUSE_UNSPEC;
	} else {
		req->release.cause[0] = cause[0];
		req->release.cause[1] = cause[1];
	}

	cc_send_uni(conn, UNIAPI_RELEASE_request, u);
}

/*
 * Make a RELEASE.response for this connection
 */
static void
do_release_response(struct ccconn *conn, uint8_t cause, struct uni_ie_cause *ie)
{
	struct uni_msg *u;
	struct uniapi_release_response *resp;

	if ((u = uni_msg_alloc(sizeof(*resp))) == NULL)
		return;
	resp = uni_msg_wptr(u, struct uniapi_release_response *);
	memset(resp, 0, sizeof(*resp));
	u->b_wptr += sizeof(struct uniapi_release_response);

	resp->release_compl.hdr.cref = conn->cref;
	resp->release_compl.hdr.act = UNI_MSGACT_DEFAULT;

	if (ie != NULL)
		resp->release_compl.cause[0] = *ie;

	if (cause != 0) {
		IE_SETPRESENT(resp->release_compl.cause[0]);
		resp->release_compl.cause[0].h.act = UNI_IEACT_DEFAULT;
		resp->release_compl.cause[0].loc = UNI_CAUSE_LOC_USER;
		resp->release_compl.cause[0].cause = cause;
	}

	cc_send_uni(conn, UNIAPI_RELEASE_response, u);
}

/**********************************************************************
 *
 * INSTANCE handling
 */
struct ccconn *
cc_conn_create(struct ccdata *cc)
{
	struct ccconn *conn;

	conn = CCZALLOC(sizeof(*conn));
	if (conn == NULL)
		return (NULL);

	conn->state = CONN_NULL;
	conn->port = NULL;
	conn->cc = cc;
	LIST_INIT(&conn->parties);

	LIST_INSERT_HEAD(&cc->orphaned_conns, conn, port_link);

	if (conn->cc->log & CCLOG_CONN_INST)
		cc_conn_log(conn, "created %s", "orphaned");

	return (conn);
}

/*
 * assign to port
 */
void
cc_conn_ins_port(struct ccconn *conn, struct ccport *port)
{

	if (conn->port != NULL) {
		cc_conn_log(conn, "conn is already on port %u",
		    conn->port->param.port);
		cc_conn_rem_port(conn);
	}
	LIST_REMOVE(conn, port_link);

	conn->port = port;
	LIST_INSERT_HEAD(&port->conn_list, conn, port_link);

}

/*
 * remove from port
 */
void
cc_conn_rem_port(struct ccconn *conn)
{

	if (conn->port == NULL) {
		cc_conn_log(conn, "conn not on any %s", "port");
		return;
	}
	LIST_REMOVE(conn, port_link);
	conn->port = NULL;
	LIST_INSERT_HEAD(&conn->cc->orphaned_conns, conn, port_link);
}

static void
cc_conn_flush_cookies(struct ccconn *conn)
{
	struct ccreq *r, *r1;

	if (conn->port == NULL)
		return;
	TAILQ_FOREACH_SAFE(r, &conn->port->cookies, link, r1) {
		if (r->conn == conn) {
			TAILQ_REMOVE(&conn->port->cookies, r, link);
			CCFREE(r);
		}
	}
}

void
cc_conn_reset_acceptor(struct ccconn *conn)
{
	if (conn->acceptor != NULL) {
		conn->acceptor->accepted = NULL;
		conn->acceptor = NULL;
	}
}

/*
 * Destroy a connection
 */
void
cc_conn_destroy(struct ccconn *conn)
{
	struct ccparty *p;

	if (conn->cc->log & CCLOG_CONN_INST)
		cc_conn_log(conn, "destroy%s", "");

	if (conn->user != NULL) {
		cc_conn_log(conn, "still connected to user %p\n", conn->user);
		conn->user->queue_act--;
		TAILQ_REMOVE(&conn->user->connq, conn, connq_link);
	}

	if (conn->acceptor != NULL)
		conn->acceptor->accepted = NULL;

	cc_conn_flush_cookies(conn);
	cc_conn_sig_flush(conn);

	LIST_REMOVE(conn, port_link);
	while ((p = LIST_FIRST(&conn->parties)) != NULL) {
		LIST_REMOVE(p, link);
		CCFREE(p);
	}

	CCFREE(conn);
}

struct ccparty *
cc_party_create(struct ccconn *conn, u_int ident, u_int flag)
{
	struct ccparty *party;

	party = CCZALLOC(sizeof(*party));
	if (party == NULL)
		return (NULL);

	party->conn = conn;
	party->state = PARTY_NULL;
	IE_SETPRESENT(party->epref);
	party->epref.flag = flag;
	party->epref.epref = ident;
	LIST_INSERT_HEAD(&conn->parties, party, link);

	if (party->conn->cc->log & CCLOG_PARTY_INST)
		cc_party_log(party, "created %u.%u", flag, ident);

	return (party);
}

static void
cc_party_destroy(struct ccparty *party)
{

	if (party->conn->cc->log & CCLOG_PARTY_INST)
		cc_party_log(party, "destroyed %u.%u", party->epref.flag,
		    party->epref.epref);

	LIST_REMOVE(party, link);
	CCFREE(party);
}

static struct ccparty *
cc_party_find(struct ccconn *conn, u_int ident)
{
	struct ccparty *party;

	LIST_FOREACH(party, &conn->parties, link)
		if (party->epref.epref == ident)
			return (party);
	return (NULL);
}
/*
 * Abort connection from down stream (because of the UNI hook beeing
 * disconnected). This is called from two places:
 *  1) the shutdown code.
 *	In this case the connections should be already dissociated from
 *	users and be only in states waiting for the UNI stack.
 *  2) from the disconnect code.
 */
void
cc_conn_abort(struct ccconn *conn, int shutdown)
{
	struct ccuser *u = conn->user;
	struct ccparty *p, *p1;

	if (shutdown) {
		CCASSERT(u == NULL, ("still in use"));
		CCASSERT(conn->acceptor == NULL, ("still in use"));
		cc_conn_destroy(conn);
		return;
	}

	/*
	 * Look whether any parties are blocked waiting for a response
	 * from the stack. We don't use extra party states to handle
	 * user aborts, so check that there is a user before using it.
	 */
	if (u == NULL) {
		while ((p = LIST_FIRST(&conn->parties)) != NULL)
			cc_party_destroy(p);
	} else {
		LIST_FOREACH_SAFE(p, &conn->parties, link, p1) {
			switch (p->state) {

			  case PARTY_NULL:		/* P0 */
				/* should not happen */
				goto dpty;

			  case PARTY_ACTIVE:		/* P1 */
				/* don't send a drop - user'll get a rel */
				goto dpty;

			  case PARTY_ADD_WAIT_CREATE:	/* P2 */
			  case PARTY_ADD_WAIT_OK:	/* P3 */
				/* we're adding - synthesise an error */
				cc_user_sig(u, USER_SIG_ADD_PARTY_ERR,
				    NULL, ATMERR_BAD_PORT);
				goto dpty;

			  case PARTY_ADD_WAIT_ACK:	/* P4 */
				/* don't send a drop - user'll get a rel */
				goto dpty;

			  case PARTY_DROP_WAIT_OK:	/* P5 */
			  case PARTY_DROP_WAIT_ACK:	/* P6 */
			  case PARTY_ADD_DROP_WAIT_OK:	/* P11 */
				/* we're dropping - synthesis an ok */
				cc_user_sig(u, USER_SIG_DROP_PARTY_OK,
				    NULL, p->epref.epref);
				goto dpty;

			  case PARTY_WAIT_DESTROY:	/* P7 */
				goto dpty;

			  case PARTY_WAIT_SETUP_COMPL:	/* P8 */
			  case PARTY_WAIT_SETUP_CONF:	/* P10 */
				/* first party - nothing to do */
				goto dpty;

			  case PARTY_WAIT_DROP_ACK_OK:	/* P9 */
			  case PARTY_ADD_DROPACK_WAIT_OK:/* P12 */
				/* we're dropping - nothing to do */
				goto dpty;
			}
			cc_party_log(p, "bad uabort for party in state %s",
			    ptab[p->state]);
    dpty:
			cc_party_destroy(p);
		}
	}

	/*
	 * Now do what the connection needs
	 */
	switch (conn->state) {

	  case CONN_NULL:		/* 0 */
	  case CONN_OUT_PREPARING: 	/* 1 */
		/* may not happen because we're not associated with
		 * aport yet */
		break;

	  case CONN_OUT_WAIT_CREATE: 	/* 2 */
	  case CONN_OUT_WAIT_OK:	/* 3 */
	  case CONN_OUT_WAIT_DESTROY:	/* 37 */
		/* return an error to the user, go back to C1/U1
		 * reset cref (for C37, C3) and cookie */
		conn->cref.flag = 0;
		conn->cref.cref = 0;
		cc_conn_flush_cookies(conn);
		cc_conn_set_state(conn, CONN_OUT_PREPARING);
		cc_conn_rem_port(conn);
		cc_user_sig(u, USER_SIG_CONNECT_OUTGOING_ERR,
		    NULL, ATMERR_BAD_PORT);
		return;

	  case CONN_OUT_WAIT_CONF:	/* 4 */
	  case CONN_ACTIVE:		/* 5 */
	  case CONN_IN_WAIT_COMPL:	/* 13 */
		/* emulate a RELEASE.confirm */
		memset(&u->cause, 0, sizeof(u->cause));
		cc_user_sig(u, USER_SIG_RELEASE_CONFIRM, NULL, 0);
		cc_disconnect_from_user(conn);
		cc_conn_destroy(conn);
		return;

	  case CONN_IN_PREPARING:	/* 10 */
	  case CONN_AB_WAIT_REQ_OK:	/* 33 */
	  case CONN_AB_WAIT_RESP_OK:	/* 34 */
	  case CONN_AB_FLUSH_IND:	/* 35 */
		/* no user - destroy */
		cc_conn_destroy(conn);
		return;

	  case CONN_IN_ARRIVED:		/* 11 */
		u->aborted = 1;
		cc_disconnect_from_user(conn);
		cc_conn_destroy(conn);
		return;

	  case CONN_IN_WAIT_ACCEPT_OK:	/* 12 */
		/* return ACCEPT error */
		cc_disconnect_from_user(conn);
		cc_conn_reset_acceptor(conn);
		cc_user_sig(u, USER_SIG_ACCEPT_ERR,
		    u, ATMERR_PREVIOUSLY_ABORTED);
		cc_conn_destroy(conn);
		return;

	  case CONN_REJ_WAIT_OK:	/* 14 */
		/* return REJECT ok */
		cc_disconnect_from_user(conn);
		cc_conn_destroy(conn);
		cc_user_sig(u, USER_SIG_REJECT_OK, NULL, 0);
		return;

	  case CONN_REL_IN_WAIT_OK:	/* 15 */
	  case CONN_REL_WAIT_OK:	/* 20 */
		/* confirm destroy */
		if (u != NULL) {
			/* connection not aborted */
			memset(&u->cause, 0, sizeof(u->cause));
			cc_user_sig(u, USER_SIG_RELEASE_CONFIRM, NULL, 0);
			cc_disconnect_from_user(conn);
		}
		cc_conn_destroy(conn);
		return;

	  case CONN_IN_WAITING:		/* 21 */
		/* user has not seen the connection - destroy */
		cc_disconnect_from_user(conn);
		cc_conn_destroy(conn);
		return;
	}
	cc_conn_log(conn, "bad state %s", stab[conn->state]);
}

#ifdef DEBUG_MATCH
static void
print_sap(const struct uni_sap *sap)
{
	static const char *const tags[] = {
		[UNISVE_ABSENT] "absent",
		[UNISVE_PRESENT]"present",
		[UNISVE_ANY]	"any",
	};
	u_int i;

	printf("addr={%s", tags[sap->addr.tag]);
	if (sap->addr.tag == UNISVE_PRESENT) {
		printf(",%d-%d", sap->addr.type, sap->addr.plan);
		for (i = 0; i < sap->addr.len; i++)
			printf("%c%02x", ",:"[i!=0], sap->addr.addr[i]);
	}
	printf("}\n");

	printf("selector={%s", tags[sap->selector.tag]);
	if (sap->selector.tag == UNISVE_PRESENT)
		printf(",%02x", sap->selector.selector);
	printf("}\n");

	printf("blli_id2={%s", tags[sap->blli_id2.tag]);
	if (sap->blli_id2.tag == UNISVE_PRESENT)
		printf(",%02x,%02x", sap->blli_id2.proto, sap->blli_id2.user);
	printf("}\n");

	printf("blli_id3={%s", tags[sap->blli_id3.tag]);
	if (sap->blli_id3.tag == UNISVE_PRESENT)
		printf(",%02x,%02x,%02x,%06x,%04x,%d",
		    sap->blli_id3.proto, sap->blli_id3.user,
		    sap->blli_id3.ipi, sap->blli_id3.oui,
		    sap->blli_id3.pid, sap->blli_id3.noipi);
	printf("}\n");

	printf("bhli={%s", tags[sap->bhli.tag]);
	if (sap->bhli.tag == UNISVE_PRESENT) {
		printf(",%d", sap->bhli.type);
		for (i = 0; i < sap->bhli.len; i++)
			printf("%c%02x", ",:"[i!=0], sap->bhli.info[i]);
	}
	printf("}\n");
}
#endif

/*********************************************************************
 *
 * DISPATCH incoming call
 */
void
cc_conn_dispatch(struct ccconn *conn)
{
	struct ccdata *priv = conn->port->cc;
	struct ccuser *user;
	u_int blli_index;

#ifdef DEBUG_MATCH
	static char buf[1000];
	static struct unicx cx;
	static int init = 1;

	if (init) {
		uni_initcx(&cx);
		init = 0;
	}
#endif

	/*
	 * Do call dispatching according to 4.6
	 */
#ifdef DEBUG_MATCH
	printf("+++++ DISPATCH++++++\n");
#endif
	for (blli_index = 0; blli_index < UNI_NUM_IE_BLLI; blli_index++) {
		if (blli_index > 0 && !IE_ISGOOD(conn->blli[blli_index]))
			break;
#ifdef DEBUG_MATCH
		if (IE_ISPRESENT(conn->called)) {
			uni_print_ie(buf, sizeof(buf), UNI_IE_CALLED,
			    (union uni_ieall *)&conn->called, &cx);
			printf("called=%s\n", buf);
		}
		if (IE_ISPRESENT(conn->bhli)) {
			uni_print_ie(buf, sizeof(buf), UNI_IE_BHLI,
			    (union uni_ieall *)&conn->bhli, &cx);
			printf("bhli=%s\n", buf);
		}
		if (IE_ISPRESENT(conn->blli[blli_index])) {
			uni_print_ie(buf, sizeof(buf), UNI_IE_BLLI,
			    (union uni_ieall *)&conn->blli[blli_index], &cx);
			printf("%s\n", buf);
		}
#endif
		LIST_FOREACH(user, &priv->user_list, node_link) {
			if ((user->state == USER_IN_WAITING ||
			    user->state == USER_IN_ARRIVED ||
			    user->state == USER_IN_WAIT_ACC ||
			    user->state == USER_IN_WAIT_REJ) &&
			    !unisve_is_catchall(user->sap)) {
#ifdef DEBUG_MATCH
				printf("TRYING user=%p\n", user);
				print_sap(user->sap);
#endif
				if (unisve_match(user->sap, &conn->called,
				    &conn->blli[blli_index], &conn->bhli))
					goto found;
			}
		}
	}
#ifdef DEBUG_MATCH
	printf("TRYING CATCHALL\n");
#endif
	blli_index = 0;
	LIST_FOREACH(user, &priv->user_list, node_link) {
		if ((user->state == USER_IN_WAITING ||
		    user->state == USER_IN_ARRIVED ||
		    user->state == USER_IN_WAIT_ACC ||
		    user->state == USER_IN_WAIT_REJ) &&
		    unisve_is_catchall(user->sap))
			goto found;
	}
#ifdef DEBUG_MATCH
	printf("SORRY\n");
#endif

	/*
	 * No application found - reject call.
	 */
	do_release_response(conn, UNI_CAUSE_INCOMP, NULL);
	cc_conn_set_state(conn, CONN_AB_WAIT_RESP_OK);
	return;

  found:
#ifdef DEBUG_MATCH
	printf("MATCH\n");
#endif
	if (user->queue_max == user->queue_act) {
		do_release_response(conn, UNI_CAUSE_BUSY, NULL);
		cc_conn_set_state(conn, CONN_AB_WAIT_RESP_OK);
		return;
	}

	if (blli_index == 0 && !IE_ISGOOD(conn->blli[blli_index]))
		conn->blli_selector = 0;
	else
		conn->blli_selector = blli_index + 1;

	cc_conn_set_state(conn, CONN_IN_WAITING);
	cc_connect_to_user(conn, user);

	cc_user_sig(user, USER_SIG_SETUP_IND, NULL, 0);
}

static void
cc_party_setup_conf(struct ccconn *conn)
{
	struct ccparty *party;

	party = cc_party_find(conn, conn->epref.epref);
	if (party == NULL) {
		cc_party_log(party, "no party for %s",
		    cc_conn_sigtab[CONN_SIG_SETUP_CONFIRM]);
		return;
	}
	if (party->state != PARTY_WAIT_SETUP_CONF) {
		cc_party_log(party, "bad state=%s for signal=%s",
		    ptab[party->state], cc_conn_sigtab[CONN_SIG_SETUP_CONFIRM]);
		return;
	}
	cc_party_set_state(party, PARTY_ACTIVE);
}

static void
cc_party_add_ack_ind(struct ccconn *conn, const struct uni_ie_epref *epref)
{
	struct ccparty *party;

	party = cc_party_find(conn, epref->epref);
	if (party == NULL) {
		cc_party_log(party, "no party for %s",
		    cc_conn_sigtab[CONN_SIG_PARTY_ADD_ACK_IND]);
	}
	if (party->state != PARTY_ADD_WAIT_ACK) {
		cc_party_log(party, "bad state=%s for signal=%s",
		    ptab[party->state],
		    cc_conn_sigtab[CONN_SIG_PARTY_ADD_ACK_IND]);
		return;
	}
	cc_party_set_state(party, PARTY_ACTIVE);
	cc_user_sig(conn->user, USER_SIG_ADD_PARTY_ACK,
	    NULL, epref->epref);
}

static void
cc_party_add_rej_ind(struct ccconn *conn, const struct uni_ie_epref *epref)
{
	struct ccparty *party;

	party = cc_party_find(conn, epref->epref);
	if (party == NULL) {
		cc_party_log(party, "no party for %s",
		    cc_conn_sigtab[CONN_SIG_PARTY_ADD_REJ_IND]);
		return;
	}
	if (party->state != PARTY_ADD_WAIT_ACK) {
		cc_party_log(party, "bad state=%s for signal=%s",
		    ptab[party->state],
		    cc_conn_sigtab[CONN_SIG_PARTY_ADD_REJ_IND]);
		return;
	}
	cc_party_set_state(party, PARTY_WAIT_DESTROY);
	cc_user_sig(conn->user, USER_SIG_ADD_PARTY_REJ, NULL, epref->epref);
}

static void
cc_party_drop_ack_ind(struct ccconn *conn,
    const struct uni_drop_party *drop)
{
	struct ccparty *party;

	party = cc_party_find(conn, drop->epref.epref);
	if (party == NULL) {
		cc_party_log(party, "no party for %s",
		    cc_conn_sigtab[CONN_SIG_DROP_PARTY_ACK_IND]);
		return;
	}
	switch (party->state) {

	  case PARTY_ACTIVE:			/* P1 */
		memset(&conn->user->cause[1], 0, sizeof(conn->user->cause[1]));
		conn->user->cause[0] = drop->cause;
		cc_party_set_state(party, PARTY_WAIT_DESTROY);
		cc_user_sig(conn->user, USER_SIG_DROP_PARTY_IND,
		    NULL, party->epref.epref);
		break;

	  case PARTY_ADD_WAIT_ACK:		/* P4 */
		memset(&conn->user->cause[1], 0, sizeof(conn->user->cause[1]));
		conn->user->cause[0] = drop->cause;
		cc_party_set_state(party, PARTY_WAIT_DESTROY);
		cc_user_sig(conn->user, USER_SIG_ADD_PARTY_REJ,
		    NULL, party->epref.epref);
		break;

	  case PARTY_DROP_WAIT_ACK:		/* P6 */
		cc_party_set_state(party, PARTY_WAIT_DESTROY);
		cc_user_sig(conn->user, USER_SIG_DROP_PARTY_OK, NULL, 0);
		break;

	  case PARTY_WAIT_SETUP_COMPL:		/* P8 */
	  case PARTY_WAIT_SETUP_CONF:		/* P10 */
		cc_party_set_state(party, PARTY_WAIT_DESTROY);
		break;

	  default:
		cc_party_log(party, "bad state=%s for signal=%s",
		    ptab[party->state],
		    cc_conn_sigtab[CONN_SIG_DROP_PARTY_ACK_IND]);
		break;
	}
}

/*
 * Handle a signal to this connection
 */
void
cc_conn_sig_handle(struct ccconn *conn, enum conn_sig sig,
    void *arg, u_int iarg)
{
	struct ccparty *party;

	if (conn->cc->log & CCLOG_CONN_SIG)
		cc_conn_log(conn, "signal %s in state %s", cc_conn_sigtab[sig],
		    stab[conn->state]);

	switch (sig) {

	  case CONN_SIG_CONNECT_OUTGOING:
		/* Do SETUP */
	    {
		struct uni_msg *u;
		struct uniapi_setup_request *setup;

		if (conn->state != CONN_OUT_PREPARING)
			goto bad_state;

		if (IE_ISGOOD(conn->bearer) &&
		    conn->bearer.cfg == UNI_BEARER_MP) {
			IE_SETPRESENT(conn->epref);
			conn->epref.flag = 0;
			conn->epref.epref = 0;
		}

		/*
		 * Construct message to UNI.
		 */
		u = uni_msg_alloc(sizeof(struct uniapi_setup_request));
		if (u == NULL) {
			cc_user_sig(conn->user, USER_SIG_CONNECT_OUTGOING_ERR,
			    NULL, ATMERR_NOMEM);
			return;
		}
		setup = uni_msg_wptr(u, struct uniapi_setup_request *);
		memset(setup, 0, sizeof(*setup));
		u->b_wptr += sizeof(struct uniapi_setup_request);

		setup->setup.hdr.act = UNI_MSGACT_DEFAULT;
		memcpy(setup->setup.blli, conn->blli, sizeof(conn->blli));
		setup->setup.bearer = conn->bearer;
		setup->setup.traffic = conn->traffic;
		setup->setup.qos = conn->qos;
		setup->setup.exqos = conn->exqos;
		setup->setup.called = conn->called;
		setup->setup.calledsub[0] = conn->calledsub;
		setup->setup.aal = conn->aal;
		setup->setup.epref = conn->epref;
		setup->setup.eetd = conn->eetd;
		setup->setup.abrsetup = conn->abrsetup;
		setup->setup.abradd = conn->abradd;
		setup->setup.calling = conn->calling;
		setup->setup.callingsub[0] = conn->callingsub;
		setup->setup.connid = conn->connid;
		memcpy(setup->setup.tns, conn->tns, sizeof(conn->tns));
		setup->setup.atraffic = conn->atraffic;
		setup->setup.mintraffic = conn->mintraffic;
		setup->setup.cscope = conn->cscope;
		setup->setup.bhli = conn->bhli;
		setup->setup.mdcr = conn->mdcr;

		cc_conn_set_state(conn, CONN_OUT_WAIT_CREATE);
		cc_send_uni(conn, UNIAPI_SETUP_request, u);

		break;
	    }


	  case CONN_SIG_ARRIVAL:
		/* user informed of arrival of this call */
		if (conn->state != CONN_IN_WAITING)
			goto bad_state;
		cc_conn_set_state(conn, CONN_IN_ARRIVED);
		break;


	  case CONN_SIG_RELEASE:
	    {
		/* Release this call */
		struct uni_msg *u;
		struct uniapi_release_request *req;

		if (conn->state != CONN_ACTIVE &&
		    conn->state != CONN_IN_WAIT_COMPL)
			goto bad_state;

		if ((u = uni_msg_alloc(sizeof(*req))) == NULL)
			return;

		req = uni_msg_wptr(u, struct uniapi_release_request *);
		memset(req, 0, sizeof(*req));
		u->b_wptr += sizeof(struct uniapi_release_request);

		req->release.hdr.cref = conn->cref;
		req->release.hdr.act = UNI_MSGACT_DEFAULT;

		req->release.cause[0] = conn->cause[0];
		req->release.cause[1] = conn->cause[1];

		if (conn->state == CONN_ACTIVE)
			cc_conn_set_state(conn, CONN_REL_WAIT_OK);
		else
			cc_conn_set_state(conn, CONN_REL_IN_WAIT_OK);

		cc_send_uni(conn, UNIAPI_RELEASE_request, u);
		break;
	    }

	  case CONN_SIG_REJECT:
	    {
		/* reject from user */
		struct ccuser *user = conn->user;

		if (conn->state != CONN_IN_ARRIVED) {
			cc_user_sig(user, USER_SIG_REJECT_ERR,
			    NULL, ATMERR_BAD_STATE);
			break;
		}
		cc_conn_set_state(conn, CONN_REJ_WAIT_OK);
		do_release_response(conn, 0, conn->cause);
		break;
	    }


	  case CONN_SIG_ACCEPT:
	    {
		 /* User accepts. */
    		struct ccuser *newep = arg;
		struct uni_msg *u;
		struct uniapi_setup_response *resp;
		struct ccuser *user = conn->user;

		if (conn->state != CONN_IN_ARRIVED) {
			cc_user_sig(user, USER_SIG_ACCEPT_ERR,
			    NULL, ATMERR_PREVIOUSLY_ABORTED);
			break;
		}

		u = uni_msg_alloc(sizeof(struct uniapi_setup_response));
		if (u == NULL) {
			cc_user_sig(user, USER_SIG_ACCEPT_ERR,
			    NULL, ATMERR_NOMEM);
			return;
		}

		/*
		 * Link to the new endpoint
		 */
		conn->acceptor = newep;
		newep->accepted = conn;

		/*
		 * Construct connect message
		 */
		resp = uni_msg_wptr(u, struct uniapi_setup_response *);
		memset(resp, 0, sizeof(*resp));
		u->b_wptr += sizeof(*resp);

		resp->connect.hdr.act = UNI_MSGACT_DEFAULT;
		resp->connect.hdr.cref = conn->cref;

		/*
		 * attributes
		 */
		if (conn->dirty_attr & CCDIRTY_AAL)
			resp->connect.aal = conn->aal;
		if (conn->dirty_attr & CCDIRTY_BLLI)
			resp->connect.blli =
			    conn->blli[conn->blli_selector - 1];
		if (conn->dirty_attr & CCDIRTY_CONNID)
			resp->connect.connid = conn->connid;
		/* XXX NOTIFY */
		if (conn->dirty_attr & CCDIRTY_EETD)
			resp->connect.eetd = conn->eetd;
		/* XXX GIT */
		/* XXX UU */
		if (conn->dirty_attr & CCDIRTY_TRAFFIC)
			resp->connect.traffic = conn->traffic;
		if (conn->dirty_attr & CCDIRTY_EXQOS)
			resp->connect.exqos = conn->exqos;
		if (conn->dirty_attr & CCDIRTY_ABRSETUP)
			resp->connect.abrsetup = conn->abrsetup;
		if (conn->dirty_attr & CCDIRTY_ABRADD)
			resp->connect.abradd = conn->abradd;

		/*
		 * If the SETUP had an endpoint reference - echo it back
		 */
		if (IE_ISPRESENT(conn->epref)) {
			resp->connect.epref = conn->epref;
			resp->connect.epref.flag = !resp->connect.epref.flag;
		}

		cc_conn_set_state(conn, CONN_IN_WAIT_ACCEPT_OK);
		cc_send_uni(conn, UNIAPI_SETUP_response, u);
		break;
	    }


	  case CONN_SIG_ADD_PARTY:
	    {
		/* request to add party from user */
		struct uni_msg *u;
		struct uniapi_add_party_request *req;

		if (conn->state != CONN_ACTIVE)
			goto bad_state;

		/* create the party */
		party = cc_party_create(conn, (u_int)(uintptr_t)arg, 0);
		if (party == NULL) {
			cc_user_sig(conn->user, USER_SIG_ADD_PARTY_ERR,
			    NULL, ATMERR_NOMEM);
			return;
		}
		party->called = conn->called;

		/* Construct message to UNI. */
		u = uni_msg_alloc(sizeof(struct uniapi_setup_request));
		if (u == NULL) {
			cc_party_destroy(party);
			cc_user_sig(conn->user, USER_SIG_ADD_PARTY_ERR,
			    NULL, ATMERR_NOMEM);
			return;
		}

		req = uni_msg_wptr(u, struct uniapi_add_party_request *);
		memset(req, 0, sizeof(*req));
		u->b_wptr += sizeof(struct uniapi_add_party_request);

		req->add.hdr.act = UNI_MSGACT_DEFAULT;
		req->add.hdr.cref = conn->cref;
		req->add.epref = party->epref;
		req->add.called = party->called;

		cc_party_set_state(party, PARTY_ADD_WAIT_CREATE);
		cc_send_uni(conn, UNIAPI_ADD_PARTY_request, u);
		break;
	    }


	  case CONN_SIG_DROP_PARTY:
	    {
		/* user request to drop a party */
		struct uni_msg *u;
		struct uniapi_drop_party_request *req;

		if (conn->state != CONN_ACTIVE)
			goto bad_state;

		party = cc_party_find(conn, (u_int)(uintptr_t)arg);
		if (party == NULL) {
			cc_user_sig(conn->user, USER_SIG_DROP_PARTY_ERR,
			    NULL, ATMERR_BAD_PARTY);
			return;
		}

		switch (party->state) {

		  case PARTY_ACTIVE:
		  case PARTY_ADD_WAIT_ACK:
			break;

		  default:
			cc_user_sig(conn->user, USER_SIG_DROP_PARTY_ERR,
			    NULL, ATMERR_BAD_STATE);
			return;

		}
		/*
		 * Construct message to UNI.
		 */
		u = uni_msg_alloc(sizeof(*req));
		if (u == NULL) {
			cc_user_sig(conn->user, USER_SIG_DROP_PARTY_ERR,
			    NULL, ATMERR_NOMEM);
			return;
		}

		req = uni_msg_wptr(u, struct uniapi_drop_party_request *);
		memset(req, 0, sizeof(*req));
		u->b_wptr += sizeof(struct uniapi_drop_party_request);

		req->drop.hdr.act = UNI_MSGACT_DEFAULT;
		req->drop.hdr.cref = conn->cref;
		req->drop.epref = party->epref;
		req->drop.cause = conn->cause[0];

		if (party->state == PARTY_ACTIVE)
			cc_party_set_state(party, PARTY_DROP_WAIT_OK);
		else
			cc_party_set_state(party, PARTY_ADD_DROP_WAIT_OK);
		cc_send_uni(conn, UNIAPI_DROP_PARTY_request, u);
		break;
	    }

	  case CONN_SIG_DROP_PARTY_ACK_IND:
	    {
		struct uni_msg *msg = arg;
		struct uniapi_drop_party_ack_indication *ind = uni_msg_rptr(msg,
		    struct uniapi_drop_party_ack_indication *);

		cc_party_drop_ack_ind(conn, &ind->drop);
		break;
	    }


	  case CONN_SIG_USER_ABORT:
		/*
		 * Aborting a connection. This is callable in all states.
		 * The connection is already disconnected from the user.
		 * The cause is in cause[].
		 */
		switch (conn->state) {

		  case CONN_NULL:		/* C0 */
		  case CONN_OUT_PREPARING:	/* C1 */
			cc_conn_destroy(conn);
			break;

		  case CONN_OUT_WAIT_CONF:	/* C4 */
		  case CONN_ACTIVE:		/* C5 */
			do_release_request(conn, conn->cause);
			cc_conn_set_state(conn, CONN_AB_WAIT_REQ_OK);
			break;

		  case CONN_IN_WAITING:		/* C21 */
			/* that should not happen */
			goto bad_state;
			break;

		  case CONN_IN_ARRIVED:		/* C11 */
			/*
			 * This is called only for the first connection
			 * of the user - the others are re-dispatched.
			 */
			do_release_response(conn, 0, conn->cause);
			cc_conn_set_state(conn, CONN_AB_WAIT_RESP_OK);
			break;

		  case CONN_IN_WAIT_COMPL:	/* C13 */
			do_release_request(conn, conn->cause);
			cc_conn_set_state(conn, CONN_AB_WAIT_REQ_OK);
			break;

		  case CONN_OUT_WAIT_DESTROY:	/* C20 */
			cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
			break;

		  case CONN_IN_WAIT_ACCEPT_OK:	/* C12 */
		  case CONN_AB_WAIT_REQ_OK:	/* C33 */
		  case CONN_AB_WAIT_RESP_OK:	/* C34 */
		  case CONN_AB_FLUSH_IND:	/* C35 */
			/* just ignore */
			break;

		/*
		 * The following states may not happen, because
		 * we're waiting for a response from the UNI stack.
		 * As soon as the response comes the ABORT is undefered
		 * and will hit us (but in another state).
		 */
		  case CONN_OUT_WAIT_CREATE:	/* C2 */
		  case CONN_OUT_WAIT_OK:	/* C3 */
		  case CONN_IN_PREPARING:	/* C10 */
		  case CONN_REJ_WAIT_OK:	/* C14 */
		  case CONN_REL_IN_WAIT_OK:	/* C15 */
		  case CONN_REL_WAIT_OK:	/* C20 */
			goto bad_state;
		}
		break;


	  case CONN_SIG_CREATED:
	    {
		/*
		 * CALL_CREATED message from UNI. This can happen for either
		 * incoming or outgoing connections.
		 */
		struct uni_msg *msg = arg;
		struct uniapi_call_created *cr = uni_msg_rptr(msg,
		    struct uniapi_call_created *);

		switch (conn->state) {

		  case CONN_OUT_WAIT_CREATE:
			conn->cref = cr->cref;
			cc_conn_set_state(conn, CONN_OUT_WAIT_OK);
			break;

		  case CONN_NULL:
			conn->cref = cr->cref;
			cc_conn_set_state(conn, CONN_IN_PREPARING);
			break;

		  default:
			goto bad_state;
		}
		break;
	    }

	  case CONN_SIG_DESTROYED:
		/*
		 * CALL_DESTROYED message from UNI.
		 */
		switch (conn->state) {

		  case CONN_OUT_WAIT_DESTROY:
			cc_conn_rem_port(conn);
			cc_conn_set_state(conn, CONN_OUT_PREPARING);
			if (conn->user != NULL)
				cc_user_sig(conn->user,
				    USER_SIG_CONNECT_OUTGOING_ERR,
				    NULL, ATM_MKUNIERR(conn->reason));
			break;

		  case CONN_AB_FLUSH_IND:
			cc_conn_destroy(conn);
			break;

		  case CONN_IN_PREPARING:
			cc_conn_destroy(conn);
			break;

		  default:
			goto bad_state;
		}
		break;


	  case CONN_SIG_SETUP_CONFIRM:
		/* Setup confirm from the UNI. */
	    {
		struct uni_msg *msg = arg;
		struct uniapi_setup_confirm *conf = uni_msg_rptr(msg,
		    struct uniapi_setup_confirm *);

		switch (conn->state) {

		  case CONN_OUT_WAIT_CONF:
			/*
			 * Shuffle attributes and inform the user.
			 * Negotiable attributes are condititionally shuffled,
			 * because not returning it means accepting it
			 * (in case of blli the first instance of it).
			 * All others are shuffled unconditionally.
			 * Here we should also open the VCI in the driver. (XXX)
			 */
#define SHUFFLE(ATTR)	conn->ATTR = conf->connect.ATTR
#define COND_SHUFFLE(ATTR) if (IE_ISPRESENT(conf->connect.ATTR)) SHUFFLE(ATTR)

			COND_SHUFFLE(aal);
			(void)memset(conn->blli + 1, 0,
			    sizeof(conn->blli) - sizeof(conn->blli[0]));
			if (IE_ISPRESENT(conf->connect.blli))
				conn->blli[0] = conf->connect.blli;
			conn->blli_selector = 1;
			COND_SHUFFLE(epref);
			SHUFFLE(conned);
			SHUFFLE(connedsub);
			SHUFFLE(eetd);
			COND_SHUFFLE(traffic);
			COND_SHUFFLE(exqos);
			COND_SHUFFLE(abrsetup);
			COND_SHUFFLE(abradd);
			COND_SHUFFLE(connid);
#undef SHUFFLE
#undef COND_SHUFFLE
			if (IE_ISGOOD(conn->epref))
				cc_party_setup_conf(conn);

			cc_conn_set_state(conn, CONN_ACTIVE);
			cc_user_sig(conn->user, USER_SIG_SETUP_CONFIRM,
			    NULL, 0);
			break;

		  case CONN_AB_FLUSH_IND:
		  case CONN_AB_WAIT_RESP_OK:
			break;

		  default:
			goto bad_state;
		}
		break;
	    }

	  case CONN_SIG_SETUP_IND:
	    {
		/* SETUP indication */
		struct uni_msg *msg = arg;
		struct uniapi_setup_indication *ind = uni_msg_rptr(msg,
		    struct uniapi_setup_indication *);
		u_int i;

		if (conn->state != CONN_IN_PREPARING)
			goto bad_state;

		/*
		 * Shuffle information elements.
		 */
		for (i = 0; i < UNI_NUM_IE_BLLI; i++)
			conn->blli[i] = ind->setup.blli[i];
		conn->bearer = ind->setup.bearer;
		conn->traffic = ind->setup.traffic;
		conn->qos = ind->setup.qos;
		conn->exqos = ind->setup.exqos;
		conn->called = ind->setup.called;
		conn->calledsub = ind->setup.calledsub[0];
		conn->aal = ind->setup.aal;
		conn->epref = ind->setup.epref;
		conn->eetd = ind->setup.eetd;
		conn->abrsetup = ind->setup.abrsetup;
		conn->abradd = ind->setup.abradd;
		conn->calling = ind->setup.calling;
		conn->callingsub = ind->setup.callingsub[0];
		conn->connid = ind->setup.connid;
		for (i = 0; i < UNI_NUM_IE_TNS; i++)
			conn->tns[i] = ind->setup.tns[i];
		conn->atraffic = ind->setup.atraffic;
		conn->mintraffic = ind->setup.mintraffic;
		conn->cscope = ind->setup.cscope;
		conn->bhli = ind->setup.bhli;
		conn->mdcr = ind->setup.mdcr;

		cc_conn_dispatch(conn);
		break;
	    }


	  case CONN_SIG_SETUP_COMPL:
	    {
		struct uni_msg *msg = arg;
		struct uniapi_setup_indication *ind __unused =
		    uni_msg_rptr(msg, struct uniapi_setup_indication *);

		/* SETUP_COMPLETE.indication from UNI */
		if (conn->state == CONN_AB_FLUSH_IND ||
		    conn->state == CONN_AB_WAIT_RESP_OK)
			break;

		if (conn->state != CONN_IN_WAIT_COMPL)
			goto bad_state;

		cc_conn_set_state(conn, CONN_ACTIVE);

		LIST_FOREACH(party, &conn->parties, link) {
			if (party->state == PARTY_WAIT_SETUP_COMPL)
				cc_party_set_state(party, PARTY_ACTIVE);
			else
				cc_party_log(party, "bad state=%s for sig=%s",
				    ptab[party->state],
				    cc_conn_sigtab[CONN_SIG_SETUP_COMPL]);
		}

		cc_user_sig(conn->user, USER_SIG_SETUP_COMPL, NULL, 0);
		break;
	    }


	  case CONN_SIG_PROC_IND:
	    {
		/*
		 * ALERTING.indication and PROCEEDING.indication are entirly
		 * ignored by the specification. We need to at least save the
		 * connid information element.
		 */
		struct uni_msg *msg = arg;
		struct uniapi_proceeding_indication *ind = uni_msg_rptr(msg,
		    struct uniapi_proceeding_indication *);

		switch (conn->state) {

		  case CONN_OUT_WAIT_CONF:
			if (IE_ISGOOD(ind->call_proc.connid))
				conn->connid = ind->call_proc.connid;
			break;

		  case CONN_AB_FLUSH_IND:
		  case CONN_AB_WAIT_RESP_OK:
			break;

		  default:
			goto bad_state;
		}
		break;
	    }

	  case CONN_SIG_ALERTING_IND:
	    {
		struct uni_msg *msg = arg;
		struct uniapi_alerting_indication *ind = uni_msg_rptr(msg,
		    struct uniapi_alerting_indication *);

		switch (conn->state) {

		  case CONN_OUT_WAIT_CONF:
			if (IE_ISGOOD(ind->alerting.connid))
				conn->connid = ind->alerting.connid;
			break;

		  case CONN_AB_FLUSH_IND:
		  case CONN_AB_WAIT_RESP_OK:
			break;

		  default:
			goto bad_state;
		}
		break;
	  }

	  case CONN_SIG_REL_CONF:
	    {
		/* RELEASE.confirm from UNI */
		struct uni_msg *msg = arg;
		struct uniapi_release_confirm *conf = uni_msg_rptr(msg,
		    struct uniapi_release_confirm *);

		switch (conn->state) {

		  case CONN_OUT_WAIT_CONF:
		  case CONN_ACTIVE:
			cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
			memcpy(conn->user->cause, conf->release.cause,
			    sizeof(conn->user->cause));
			/*
			 * If any party is in P6, ok the user
			 */
			LIST_FOREACH(party, &conn->parties, link) {
				if (party->state == PARTY_DROP_WAIT_ACK) {
					cc_party_set_state(party,
					    PARTY_WAIT_DESTROY);
					cc_user_sig(conn->user,
					    USER_SIG_DROP_PARTY_OK,
					    NULL, party->epref.epref);
				}
			}
			cc_user_sig(conn->user, USER_SIG_RELEASE_CONFIRM,
			    NULL, 0);
			cc_disconnect_from_user(conn);
			break;

		  case CONN_AB_FLUSH_IND:
		  case CONN_AB_WAIT_RESP_OK:
			break;

		  case CONN_IN_WAITING:
			cc_disconnect_from_user(conn);
			cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
			break;

		  case CONN_IN_ARRIVED:
			conn->user->aborted = 1;
			memcpy(conn->user->cause, conf->release.cause,
			    sizeof(conn->user->cause));
			cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
			cc_disconnect_from_user(conn);
			break;

		  case CONN_IN_WAIT_COMPL:
			cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
			memcpy(conn->user->cause, conf->release.cause,
			    sizeof(conn->user->cause));
			cc_user_sig(conn->user, USER_SIG_RELEASE_CONFIRM,
			    NULL, 0);
			cc_disconnect_from_user(conn);
			break;
			
		  default:
			goto bad_state;
		}
		break;
	    }

	  case CONN_SIG_REL_IND:
	    {
		/* RELEASE.ind from UNI */
		struct uni_msg *msg = arg;
		struct uniapi_release_indication *conf = uni_msg_rptr(msg,
		    struct uniapi_release_indication *);

		switch (conn->state) {

		  case CONN_OUT_WAIT_CONF:
		  case CONN_ACTIVE:
			do_release_response(conn, 0, NULL);
			cc_conn_set_state(conn, CONN_AB_WAIT_RESP_OK);
			memcpy(conn->user->cause, conf->release.cause,
			    sizeof(conn->user->cause));
			/*
			 * If any party is in P6, ok the user
			 */
			LIST_FOREACH(party, &conn->parties, link) {
				if (party->state == PARTY_DROP_WAIT_ACK) {
					cc_party_set_state(party,
					    PARTY_WAIT_DESTROY);
					cc_user_sig(conn->user,
					    USER_SIG_DROP_PARTY_OK,
					    NULL, party->epref.epref);
				}
			}
			cc_user_sig(conn->user, USER_SIG_RELEASE_CONFIRM,
			    NULL, 0);
			cc_disconnect_from_user(conn);
			break;

		  case CONN_AB_FLUSH_IND:
		  case CONN_AB_WAIT_RESP_OK:
			break;

		  case CONN_IN_WAITING:
			cc_disconnect_from_user(conn);
			do_release_response(conn, 0, NULL);
			cc_conn_set_state(conn, CONN_AB_WAIT_RESP_OK);
			break;

		  case CONN_IN_ARRIVED:
			conn->user->aborted = 1;
			cc_disconnect_from_user(conn);
			do_release_response(conn, 0, NULL);
			cc_conn_set_state(conn, CONN_AB_WAIT_RESP_OK);
			break;

		  case CONN_IN_WAIT_COMPL:
			do_release_response(conn, 0, NULL);
			cc_conn_set_state(conn, CONN_AB_WAIT_RESP_OK);
			memcpy(conn->user->cause, conf->release.cause,
			    sizeof(conn->user->cause));
			cc_user_sig(conn->user, USER_SIG_RELEASE_CONFIRM,
			    NULL, 0);
			cc_disconnect_from_user(conn);
			break;
		  default:
			goto bad_state;
			break;
		}
		break;
	    }

	  case CONN_SIG_PARTY_ALERTING_IND:
		/* party alerting from UNI */
		if (conn->state == CONN_AB_FLUSH_IND)
			break;
		if (conn->state != CONN_ACTIVE)
			goto bad_state;
		/* ignore */
		break;

	  case CONN_SIG_PARTY_ADD_ACK_IND:
	    {
		/* ADD PARTY ACKNOWLEDGE from UNI */
		struct uni_msg *msg = arg;
		struct uniapi_add_party_ack_indication *ind = uni_msg_rptr(msg,
		    struct uniapi_add_party_ack_indication *);

		if (conn->state == CONN_AB_FLUSH_IND)
			break;
		if (conn->state != CONN_ACTIVE)
			goto bad_state;

		cc_party_add_ack_ind(conn, &ind->ack.epref);
		break;
	    }


	 case CONN_SIG_PARTY_ADD_REJ_IND:
	    {
		/* ADD PARTY REJECT indication */
		struct uni_msg *msg = arg;
		struct uniapi_add_party_rej_indication *ind = uni_msg_rptr(msg,
		    struct uniapi_add_party_rej_indication *);

		if (conn->state == CONN_AB_FLUSH_IND)
			break;
		if (conn->state != CONN_ACTIVE)
			goto bad_state;

		memset(&conn->user->cause[1], 0, sizeof(conn->user->cause[1]));
		conn->user->cause[0] = ind->rej.cause;

		cc_party_add_rej_ind(conn, &ind->rej.epref);
		break;
	    }


	  case CONN_SIG_DROP_PARTY_IND:
	    {
		/* DROP_PARTY.indication from UNI */
		struct uni_msg *msg = arg;
		struct uniapi_drop_party_indication *ind = uni_msg_rptr(msg,
		    struct uniapi_drop_party_indication *);
		struct uniapi_drop_party_ack_request *req;
		struct uni_msg *u;

		if (conn->state == CONN_AB_FLUSH_IND)
			break;
		if (conn->state != CONN_ACTIVE)
			goto bad_state;

		party = cc_party_find(conn, ind->drop.epref.epref);
		if (party == NULL) {
			cc_party_log(party, "no party for %s",
			    cc_conn_sigtab[sig]);
			break;
		}

		u = uni_msg_alloc(sizeof(*req));
		if (u == NULL)
			return;

		memset(&conn->user->cause[1], 0, sizeof(conn->user->cause[1]));
		conn->user->cause[0] = ind->drop.cause;

		switch (party->state) {

		  default:
			cc_party_log(party, "bad state %s for DROP.ind",
			    ptab[party->state]);
			/* FALLTHRU */

		  case PARTY_ACTIVE:		/* P1 -> P9 */
			cc_party_set_state(party, PARTY_WAIT_DROP_ACK_OK);
			break;

		  case PARTY_ADD_WAIT_ACK:	/* P4 -> P12 */
			cc_party_set_state(party, PARTY_ADD_DROPACK_WAIT_OK);
			break;
		}

		/*
		 * Construct message to UNI.
		 */
		req = uni_msg_wptr(u, struct uniapi_drop_party_ack_request *);
		memset(req, 0, sizeof(*req));
		u->b_wptr += sizeof(*req);

		IE_SETPRESENT(req->ack.epref);
		req->ack.hdr.act = UNI_MSGACT_DEFAULT;
		req->ack.hdr.cref = conn->cref;

		req->ack.epref.flag = 0;
		req->ack.epref.epref = ind->drop.epref.epref;

		cc_send_uni(conn, UNIAPI_DROP_PARTY_ACK_request, u);
		break;
	    }

	  case CONN_SIG_OK:
	    {
 		/* OK response from UNI */
		struct ccuser *user = conn->user;

		switch (conn->state) {

		  case CONN_OUT_WAIT_OK:		/* C3 */
			cc_conn_set_state(conn, CONN_OUT_WAIT_CONF);
			if (conn->user != NULL)
				cc_user_sig(conn->user,
				    USER_SIG_CONNECT_OUTGOING_OK, NULL, 0);
			break;

		  case CONN_AB_WAIT_RESP_OK:		/* C33 */
		  case CONN_AB_WAIT_REQ_OK:		/* C34 */
			cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
			break;

		  case CONN_REL_WAIT_OK:		/* C20 */
		  case CONN_REL_IN_WAIT_OK:		/* C15 */
			cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
			if (conn->user != NULL) {
				/* connection has not been aborted */
				memset(&conn->user->cause, 0,
				    sizeof(conn->user->cause));
				cc_user_sig(conn->user,
				    USER_SIG_RELEASE_CONFIRM, NULL, 0);
				cc_disconnect_from_user(conn);
			}
			break;

		  case CONN_IN_WAIT_ACCEPT_OK:		/* C12 */
			if (user == NULL) {
				/* has been aborted */
				do_release_request(conn, NULL);
				cc_conn_set_state(conn, CONN_AB_WAIT_REQ_OK);
				break;
			}
			cc_conn_set_state(conn, CONN_IN_WAIT_COMPL);
			cc_disconnect_from_user(conn);
			cc_user_sig(user, USER_SIG_ACCEPT_OK, NULL, 0);
			if (conn->acceptor == NULL) {
				do_release_request(conn, NULL);
				cc_conn_set_state(conn, CONN_AB_WAIT_REQ_OK);
				break;
			}
			cc_connect_to_user(conn, conn->acceptor);
			cc_conn_reset_acceptor(conn);
			cc_user_sig(conn->user, USER_SIG_ACCEPTING, NULL, 0);
			break;

		  case CONN_REJ_WAIT_OK:		/* C14 */
			cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
			if (user != NULL) {
				cc_disconnect_from_user(conn);
				cc_user_sig(user, USER_SIG_REJECT_OK, NULL, 0);
			}
			break;

		  default:
			/* maybe it's for a party */
			LIST_FOREACH(party, &conn->parties, link) {
				switch (party->state) {

				  case PARTY_ADD_WAIT_OK:	/* P3 */
					if (user != NULL)
						cc_user_sig(user,
						    USER_SIG_ADD_PARTY_OK,
						    NULL, 0);
					cc_party_set_state(party,
					    PARTY_ADD_WAIT_ACK);
					goto ex_party_ok;

				  case PARTY_DROP_WAIT_OK:	/* P5 */
					cc_party_set_state(party,
					    PARTY_DROP_WAIT_ACK);
					goto ex_party_ok;

				  case PARTY_WAIT_DROP_ACK_OK:	/* P9 */
				  case PARTY_ADD_DROPACK_WAIT_OK:/* P12 */
				     {
					struct ccparty *p1;

					cc_party_set_state(party,
					    PARTY_WAIT_DESTROY);
					/* signal to user only if there are any other parties */
					LIST_FOREACH(p1, &conn->parties, link)
						if (p1 != party)
							break;
					if (p1 != NULL && user != NULL)
						cc_user_sig(user,
						    USER_SIG_DROP_PARTY_IND,
						    NULL,
						    party->epref.epref);

					goto ex_party_ok;
				    }

				  case PARTY_ADD_DROP_WAIT_OK:	/* P11 */
					cc_party_set_state(party,
					    PARTY_DROP_WAIT_ACK);
					goto ex_party_ok;

				  default:
					break;
				}
			}
			goto bad_state;
		    ex_party_ok:
			break;
		}
		break;
	    }

	  case CONN_SIG_ERROR:
	    {
		/* error response from UNI */
		u_int reason = (iarg >> 16) & 0xffff;
		u_int state = iarg & 0xffff;
		struct ccuser *user = conn->user;

		switch (conn->state) {

		  case CONN_OUT_WAIT_CREATE:		/* C2 */
			cc_conn_rem_port(conn);
			cc_conn_set_state(conn, CONN_OUT_PREPARING);
			if (conn->user != NULL)
				cc_user_sig(conn->user,
				    USER_SIG_CONNECT_OUTGOING_ERR,
				    NULL, ATM_MKUNIERR(reason));
			break;

		  case CONN_OUT_WAIT_OK:		/* C3 */
			cc_conn_set_state(conn, CONN_OUT_WAIT_DESTROY);
			conn->reason = reason;
			break;

		  case CONN_AB_WAIT_REQ_OK:		/* C33 */
			if (state == UNI_CALLSTATE_U12) {
				do_release_response(conn, 0, conn->cause);
				cc_conn_set_state(conn, CONN_AB_WAIT_RESP_OK);
				break;
			}
			cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
			break;

		  case CONN_AB_WAIT_RESP_OK:		/* C34 */
			cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
			break;

		  case CONN_REL_WAIT_OK:		/* C20 */
			if (user == NULL) {
				/* connection has been aborted. */
				if (state == UNI_CALLSTATE_U10) {
					/* do what we can */
					do_release_request(conn, conn->cause);
					cc_conn_set_state(conn,
					    CONN_AB_WAIT_REQ_OK);
				} else if (state == UNI_CALLSTATE_U12) {
					do_release_response(conn, 0, NULL);
					cc_conn_set_state(conn,
					    CONN_AB_WAIT_RESP_OK);
				} else {
					cc_conn_set_state(conn,
					    CONN_AB_FLUSH_IND);
				}
				break;
			}
			if (state == UNI_CALLSTATE_U10) {
				cc_conn_set_state(conn, CONN_ACTIVE);
				cc_user_sig(conn->user, USER_SIG_RELEASE_ERR,
				    NULL, reason);
			} else if (state == UNI_CALLSTATE_U12) {
				do_release_response(conn, 0, NULL);
				cc_conn_set_state(conn, CONN_AB_WAIT_RESP_OK);
				memset(&conn->user->cause, 0,
				    sizeof(conn->user->cause));
				cc_user_sig(conn->user,
				    USER_SIG_RELEASE_CONFIRM, NULL, 0);
				cc_disconnect_from_user(conn);
			} else {
				cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
				memset(&conn->user->cause, 0,
				    sizeof(conn->user->cause));
				cc_user_sig(conn->user,
				    USER_SIG_RELEASE_CONFIRM, NULL, 0);
				cc_disconnect_from_user(conn);
			}
			break;

		  case CONN_IN_WAIT_ACCEPT_OK:		/* C12 */
			if (user == NULL) {
				/* connection was aborted */
				if (state == UNI_CALLSTATE_U6 ||
				    state == UNI_CALLSTATE_U7 ||
				    state == UNI_CALLSTATE_U9 ||
				    state == UNI_CALLSTATE_U12) {
					do_release_response(conn, 0, NULL);
					cc_conn_set_state(conn,
					    CONN_AB_WAIT_RESP_OK);
				} else {
					cc_conn_set_state(conn,
					    CONN_AB_FLUSH_IND);
				}
				break;
			}
			cc_conn_reset_acceptor(conn);
			if (state == UNI_CALLSTATE_U6 ||
			    state == UNI_CALLSTATE_U9 ||
			    state == UNI_CALLSTATE_U7) {
				cc_user_sig(user, USER_SIG_ACCEPT_ERR,
				    NULL, ATM_MKUNIERR(reason));
				cc_conn_set_state(conn, CONN_IN_ARRIVED);
			} else if (state == UNI_CALLSTATE_U12) {
				do_release_response(conn, 0, NULL);
				cc_disconnect_from_user(conn);
				cc_user_sig(user, USER_SIG_ACCEPT_ERR,
				    user, ATMERR_PREVIOUSLY_ABORTED);
				cc_conn_set_state(conn, CONN_AB_WAIT_RESP_OK);
			} else {
				cc_disconnect_from_user(conn);
				cc_user_sig(user, USER_SIG_ACCEPT_ERR,
				    user, ATMERR_PREVIOUSLY_ABORTED);
				cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
			}
			break;

		  case CONN_REJ_WAIT_OK:		/* C14 */
			if (user == NULL) {
				/* connection has been aborted. */
				if (state == UNI_CALLSTATE_U6 ||
				    state == UNI_CALLSTATE_U7 ||
				    state == UNI_CALLSTATE_U9 ||
				    state == UNI_CALLSTATE_U12) {
					/* do what we can */
					do_release_response(conn, 0, NULL);
					cc_conn_set_state(conn,
					    CONN_AB_WAIT_RESP_OK);
				} else {
					cc_conn_set_state(conn,
					    CONN_AB_FLUSH_IND);
				}
				break;
			}
			if (state == UNI_CALLSTATE_U6 ||
			    state == UNI_CALLSTATE_U9 ||
			    state == UNI_CALLSTATE_U7) {
				cc_user_sig(user, USER_SIG_REJECT_ERR,
				    NULL, ATM_MKUNIERR(reason));
				cc_conn_set_state(conn, CONN_IN_ARRIVED);
			} else {
				cc_disconnect_from_user(conn);
				cc_user_sig(user, USER_SIG_REJECT_OK, NULL, 0);
				cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
			}
			break;

		  case CONN_REL_IN_WAIT_OK:		/* C15 */
			if (user == NULL) {
				/* connection has been aborted. */
				if (state == UNI_CALLSTATE_U8) {
					/* do what we can */
					do_release_request(conn, conn->cause);
					cc_conn_set_state(conn,
					    CONN_AB_WAIT_REQ_OK);
				} else if (state == UNI_CALLSTATE_U12) {
					do_release_response(conn, 0, NULL);
					cc_conn_set_state(conn,
					    CONN_AB_WAIT_RESP_OK);
				} else {
					cc_conn_set_state(conn,
					    CONN_AB_FLUSH_IND);
				}
				break;
			}
			if (state == UNI_CALLSTATE_U8) {
				cc_conn_set_state(conn, CONN_IN_WAIT_COMPL);
				cc_user_sig(conn->user, USER_SIG_RELEASE_ERR,
				    NULL, reason);
			} else if (state == UNI_CALLSTATE_U12) {
				do_release_response(conn, 0, NULL);
				cc_conn_set_state(conn, CONN_AB_WAIT_RESP_OK);
				memset(&conn->user->cause, 0,
				    sizeof(conn->user->cause));
				cc_user_sig(conn->user,
				    USER_SIG_RELEASE_CONFIRM, NULL, 0);
				cc_disconnect_from_user(conn);
			} else {
				cc_conn_set_state(conn, CONN_AB_FLUSH_IND);
				memset(&conn->user->cause, 0,
				    sizeof(conn->user->cause));
				cc_user_sig(conn->user,
				    USER_SIG_RELEASE_CONFIRM, NULL, 0);
				cc_disconnect_from_user(conn);
			}
			break;

		  default:
			/* maybe it's for a party */
			LIST_FOREACH(party, &conn->parties, link) {
				switch (party->state) {

				  case PARTY_ADD_WAIT_CREATE:	/* P2 */
					cc_party_destroy(party);
					if (user != NULL)
						cc_user_sig(user,
						    USER_SIG_ADD_PARTY_ERR,
						    NULL, ATM_MKUNIERR(reason));
					goto ex_party_err;

				  case PARTY_ADD_WAIT_OK:	/* P3 */
					cc_party_set_state(party,
					    PARTY_WAIT_DESTROY);
					if (user != NULL)
						cc_user_sig(user,
						    USER_SIG_ADD_PARTY_ERR,
						    NULL, ATM_MKUNIERR(reason));
					goto ex_party_err;

				  case PARTY_DROP_WAIT_OK:	/* P5 */
					cc_party_set_state(party,
					    PARTY_ACTIVE);
					if (user != NULL)
						cc_user_sig(user,
						    USER_SIG_DROP_PARTY_ERR,
						    NULL, ATM_MKUNIERR(reason));
					goto ex_party_err;

				  case PARTY_WAIT_DROP_ACK_OK:	/* P9 */
					cc_party_set_state(party,
					    PARTY_ACTIVE);
					goto ex_party_err;

				  case PARTY_ADD_DROP_WAIT_OK:	/* P11 */
					cc_party_set_state(party,
					    PARTY_ADD_WAIT_ACK);
					if (user != NULL)
						cc_user_sig(user,
						    USER_SIG_DROP_PARTY_ERR,
						    NULL, ATM_MKUNIERR(reason));
					goto ex_party_err;

				  case PARTY_ADD_DROPACK_WAIT_OK:/* P12 */
					cc_party_set_state(party,
					    PARTY_ADD_WAIT_ACK);
					goto ex_party_err;

				  default:
					break;
				}
			}
			cc_conn_log(conn, "unexpected reason=%u ustate=%u "
			    "state=%s\n", reason, state, stab[conn->state]);
		  ex_party_err:
			break;
		}
		break;
	    }

	  case CONN_SIG_PARTY_CREATED:
	    {
		struct uni_msg *msg = arg;
		struct uniapi_party_created *pcr = uni_msg_rptr(msg,
		    struct uniapi_party_created *);

		party = cc_party_find(conn, pcr->epref.epref);
		if (party == NULL) {
			/* for incoming connections we see the party-created
			 * immediately after the call-create so that we
			 * must be in C10 */
			switch (conn->state) {

			  case CONN_IN_PREPARING:
				party = cc_party_create(conn,
				    pcr->epref.epref, 1);
				if (party == NULL)
					break;
				cc_party_set_state(party,
				    PARTY_WAIT_SETUP_COMPL);
				break;

			  case CONN_OUT_WAIT_OK:
				party = cc_party_create(conn,
				    pcr->epref.epref, 0);
				if (party == NULL)
					break;
				cc_party_set_state(party,
				    PARTY_WAIT_SETUP_CONF);
				break;

			  default:
				goto bad_state;
			}
			break;
		}
		/* this is for an ADD-PARTY */
		if (conn->state != CONN_ACTIVE)
			goto bad_state;
		if (party->state != PARTY_ADD_WAIT_CREATE)
			goto bad_party_state;
		cc_party_set_state(party, PARTY_ADD_WAIT_OK);
		break;
	    }

	  case CONN_SIG_PARTY_DESTROYED:
	    {
		struct uni_msg *msg = arg;
		struct uniapi_party_destroyed *pcr = uni_msg_rptr(msg,
		    struct uniapi_party_destroyed *);

		party = cc_party_find(conn, pcr->epref.epref);
		if (party == NULL) {
			cc_conn_log(conn, "no party to destroy %u/%u",
			    pcr->epref.flag, pcr->epref.epref);
			break;
		}
		cc_party_destroy(party);
		break;
	    }

	}

	return;

  bad_state:
	cc_conn_log(conn, "bad state=%s for signal=%s",
	    stab[conn->state], cc_conn_sigtab[sig]);
	return;

  bad_party_state:
	cc_conn_log(conn, "bad party state=%s for signal=%s",
	    ptab[party->state], cc_conn_sigtab[sig]);
	return;
}
