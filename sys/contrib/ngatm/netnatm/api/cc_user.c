/*
 * Copyright (c) 2003-2004
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
 * $Begemot: libunimsg/netnatm/api/cc_user.c,v 1.3 2004/07/16 18:46:55 brandt Exp $
 *
 * ATM API as defined per af-saa-0108
 *
 * User side (upper half)
 */

#include <netnatm/unimsg.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/msg/unimsglib.h>
#include <netnatm/api/unisap.h>
#include <netnatm/sig/unidef.h>
#include <netnatm/api/atmapi.h>
#include <netnatm/api/ccatm.h>
#include <netnatm/api/ccpriv.h>

/*
* This file handles messages to a USER.
*/
static const char *stab[] = {
#define DEF(N) [N] = #N,
	USER_STATES
#undef DEF
};

const char *
cc_user_state2str(u_int s)
{
	if (s >= sizeof(stab) / sizeof(stab[0]) || stab[s] == NULL)
		return ("?");
	return (stab[s]);
}

static __inline void
set_state(struct ccuser *user, enum user_state ns)
{
	if (user->state != ns) {
		if (user->cc->log & CCLOG_USER_STATE)
			cc_user_log(user, "%s -> %s",
			    stab[user->state], stab[ns]);
		user->state = ns;
	}
}

static __inline void
cc_user_send(struct ccuser *user, u_int op, void *arg, size_t len)
{
	user->cc->funcs->send_user(user, user->uarg, op, arg, len);
}

static __inline void
cc_user_ok(struct ccuser *user, u_int data, void *arg, size_t len)
{
	user->cc->funcs->respond_user(user, user->uarg,
	    ATMERR_OK, data, arg, len);
}

static __inline void
cc_user_err(struct ccuser *user, int err)
{
	user->cc->funcs->respond_user(user, user->uarg,
	    err, ATMRESP_NONE, NULL, 0);
}


/**********************************************************************
*
* INSTANCE MANAGEMENT
*/
/*
* New endpoint created
*/
struct ccuser *
cc_user_create(struct ccdata *cc, void *uarg, const char *name)
{
	struct ccuser *user;

	user = CCZALLOC(sizeof(*user));
	if (user == NULL)
		return (NULL);

	user->cc = cc;
	user->state = USER_NULL;
	user->uarg = uarg;
	strncpy(user->name, name, sizeof(user->name));
	user->name[sizeof(user->name) - 1] = '\0';
	TAILQ_INIT(&user->connq);
	LIST_INSERT_HEAD(&cc->user_list, user, node_link);

	if (user->cc->log & CCLOG_USER_INST)
		cc_user_log(user, "created with name '%s'", name);

	return (user);
}

/*
 * Reset a user instance
 */
static void
cc_user_reset(struct ccuser *user)
{

	CCASSERT(TAILQ_EMPTY(&user->connq), ("connq not empty"));

	if (user->sap != NULL) {
		CCFREE(user->sap);
		user->sap = NULL;
	}

	if (user->accepted != NULL) {
		user->accepted->acceptor = NULL;
		user->accepted = NULL;
	}
	user->config = USER_P2P;
	user->queue_act = 0;
	user->queue_max = 0;
	user->aborted = 0;

	set_state(user, USER_NULL);

	cc_user_sig_flush(user);
}

static void
cc_user_abort(struct ccuser *user, const struct uni_ie_cause *cause)
{
	struct ccconn *conn;

	/*
	 * Although the standard state that 'all connections
	 * associated with this endpoint are aborted' we only
	 * have to abort the head one, because in state A6
	 * (call present) the endpoint is only associated to the
	 * head connection - the others are 'somewhere else' and
	 * need to be redispatched.
	 *
	 * First bring user into a state that the connections
	 * are not dispatched back to it.
	 */
	set_state(user, USER_NULL);
	if (!user->aborted) {
		if ((conn = TAILQ_FIRST(&user->connq)) != NULL) {
			memset(conn->cause, 0, sizeof(conn->cause));
			if (cause != NULL)
				conn->cause[0] = *cause;
			cc_conn_reset_acceptor(conn);
			cc_disconnect_from_user(conn);
			cc_conn_sig(conn, CONN_SIG_USER_ABORT, NULL);
		}
	}

	while ((conn = TAILQ_FIRST(&user->connq)) != NULL) {
		/* these should be in C21 */
		cc_disconnect_from_user(conn);
		cc_conn_dispatch(conn);
	}

	cc_user_reset(user);
}

/*
 * Application has closed this endpoint. Clean up all user resources and
 * abort all connections. This can be called in any state.
 */
void
cc_user_destroy(struct ccuser *user)
{

	if (user->cc->log & CCLOG_USER_INST)
		cc_user_log(user, "destroy '%s'", user->name);

	cc_user_abort(user, NULL);

	if (user->sap != NULL)
		CCFREE(user->sap);

	cc_user_sig_flush(user);

	LIST_REMOVE(user, node_link);
	CCFREE(user);
}

/**********************************************************************
 *
 * OUTGOING CALLS
 */
/*
 * Return true when the calling address of the connection matches the address.
 */
static int
addr_matches(const struct ccaddr *addr, const struct ccconn *conn)
{

	if (!IE_ISPRESENT(conn->calling))
		return (0);

	return (addr->addr.type == conn->calling.addr.type &&
	    addr->addr.plan == conn->calling.addr.plan &&
	    addr->addr.len == conn->calling.addr.len &&
	    memcmp(addr->addr.addr, conn->calling.addr.addr,
	    addr->addr.len) == 0);
}

/*
 * Check if the user's SAP (given he is in the right state) and
 * the given SAP overlap
 */
static int
check_overlap(struct ccuser *user, struct uni_sap *sap)
{
	return ((user->state == USER_IN_PREPARING ||
	    user->state == USER_IN_WAITING) &&
	    unisve_overlap_sap(user->sap, sap));
}

/*
 * Send arrival notification to user
 */
static void
do_arrival(struct ccuser *user)
{
	struct ccconn *conn;

	user->aborted = 0;
	if ((conn = TAILQ_FIRST(&user->connq)) != NULL) {
		set_state(user, USER_IN_ARRIVED);
		cc_user_send(user, ATMOP_ARRIVAL_OF_INCOMING_CALL, NULL, 0);
		cc_conn_sig(conn, CONN_SIG_ARRIVAL, NULL);
	}
}

/**********************************************************************
 *
 * ATTRIBUTES
 */
/*
 * Query an attribute. This is possible only in some states: preparation
 * of an outgoing call, after an incoming call was offered to the application
 * and in the three active states (P2P, P2PLeaf, P2PRoot).
 */
static struct ccconn *
cc_query_check(struct ccuser *user)
{

	switch (user->state) {

	  case USER_OUT_PREPARING:
	  case USER_IN_ARRIVED:
	  case USER_ACTIVE:
		return (TAILQ_FIRST(&user->connq));

	  case USER_NULL:
		/* if we are waiting for the SETUP_confirm, we are in
		 * the NULL state still (we are the new endpoint), but
		 * have a connection in 'accepted' that is in the
		 * CONN_IN_WAIT_ACCEPT_OK state.
		 */
		if (user->accepted != NULL &&
		    user->accepted->state == CONN_IN_WAIT_ACCEPT_OK)
			return (user->accepted);
		/* FALLTHRU */

	  default:
		return (NULL);
	}
}

/*
 * Query attributes
 */
static void
cc_attr_query(struct ccuser *user, struct ccconn *conn,
    uint32_t *attr, u_int count)
{
	void *val, *ptr;
	size_t total, len;
	u_int i;
	uint32_t *atab;

	/* determine the length of the total attribute buffer */
	total = sizeof(uint32_t) + count * sizeof(uint32_t);
	for (i = 0; i < count; i++) {
		len = 0;
		switch ((enum atm_attribute)attr[i]) {

		  case ATM_ATTR_NONE:
			break;

		  case ATM_ATTR_BLLI_SELECTOR:
			len = sizeof(uint32_t);
			break;

		  case ATM_ATTR_BLLI:
			len = sizeof(struct uni_ie_blli);
			break;

		  case ATM_ATTR_BEARER:
			len = sizeof(struct uni_ie_bearer);
			break;

		  case ATM_ATTR_TRAFFIC:
			len = sizeof(struct uni_ie_traffic);
			break;

		  case ATM_ATTR_QOS:
			len = sizeof(struct uni_ie_qos);
			break;

		  case ATM_ATTR_EXQOS:
			len = sizeof(struct uni_ie_exqos);
			break;

		  case ATM_ATTR_CALLED:
			len = sizeof(struct uni_ie_called);
			break;

		  case ATM_ATTR_CALLEDSUB:
			len = sizeof(struct uni_ie_calledsub);
			break;

		  case ATM_ATTR_CALLING:
			len = sizeof(struct uni_ie_calling);
			break;

		  case ATM_ATTR_CALLINGSUB:
			len = sizeof(struct uni_ie_callingsub);
			break;

		  case ATM_ATTR_AAL:
			len = sizeof(struct uni_ie_aal);
			break;

		  case ATM_ATTR_EPREF:
			len = sizeof(struct uni_ie_epref);
			break;

		  case ATM_ATTR_CONNED:
			len = sizeof(struct uni_ie_conned);
			break;

		  case ATM_ATTR_CONNEDSUB:
			len = sizeof(struct uni_ie_connedsub);
			break;

		  case ATM_ATTR_EETD:
			len = sizeof(struct uni_ie_eetd);
			break;

		  case ATM_ATTR_ABRSETUP:
			len = sizeof(struct uni_ie_abrsetup);
			break;

		  case ATM_ATTR_ABRADD:
			len = sizeof(struct uni_ie_abradd);
			break;

		  case ATM_ATTR_CONNID:
			len = sizeof(struct uni_ie_connid);
			break;

		  case ATM_ATTR_MDCR:
			len = sizeof(struct uni_ie_mdcr);
			break;
		}
		if (len == 0) {
			cc_user_err(user, ATMERR_BAD_ATTR);
			return;
		}
		total += len;
	}

	/* allocate buffer */
	val = CCMALLOC(total);
	if (val == NULL)
		return;

	atab = val;
	atab[0] = count;

	/* fill */
	ptr = (u_char *)val + (sizeof(uint32_t) + count * sizeof(uint32_t));
	for (i = 0; i < count; i++) {
		len = 0;
		atab[i + 1] = attr[i];
		switch (attr[i]) {

		  case ATM_ATTR_NONE:
			break;

		  case ATM_ATTR_BLLI_SELECTOR:
			len = sizeof(uint32_t);
			memcpy(ptr, &conn->blli_selector, len);
			break;

		  case ATM_ATTR_BLLI:
			/* in A6 the blli_selector may be 0 when
			 * there was no blli in the SETUP.
			 */
			len = sizeof(struct uni_ie_blli);
			if (conn->blli_selector == 0)
				memset(ptr, 0, len);
			else
				memcpy(ptr, &conn->blli[conn->blli_selector -
				    1], len);
			break;

		  case ATM_ATTR_BEARER:
			len = sizeof(struct uni_ie_bearer);
			memcpy(ptr, &conn->bearer, len);
			break;

		  case ATM_ATTR_TRAFFIC:
			len = sizeof(struct uni_ie_traffic);
			memcpy(ptr, &conn->traffic, len);
			break;

		  case ATM_ATTR_QOS:
			len = sizeof(struct uni_ie_qos);
			memcpy(ptr, &conn->qos, len);
			break;

		  case ATM_ATTR_EXQOS:
			len = sizeof(struct uni_ie_exqos);
			memcpy(ptr, &conn->exqos, len);
			break;

		  case ATM_ATTR_CALLED:
			len = sizeof(struct uni_ie_called);
			memcpy(ptr, &conn->called, len);
			break;

		  case ATM_ATTR_CALLEDSUB:
			len = sizeof(struct uni_ie_calledsub);
			memcpy(ptr, &conn->calledsub, len);
			break;

		  case ATM_ATTR_CALLING:
			len = sizeof(struct uni_ie_calling);
			memcpy(ptr, &conn->calling, len);
			break;

		  case ATM_ATTR_CALLINGSUB:
			len = sizeof(struct uni_ie_callingsub);
			memcpy(ptr, &conn->callingsub, len);
			break;

		  case ATM_ATTR_AAL:
			len = sizeof(struct uni_ie_aal);
			memcpy(ptr, &conn->aal, len);
			break;

		  case ATM_ATTR_EPREF:
			len = sizeof(struct uni_ie_epref);
			memcpy(ptr, &conn->epref, len);
			break;

		  case ATM_ATTR_CONNED:
			len = sizeof(struct uni_ie_conned);
			memcpy(ptr, &conn->conned, len);
			break;

		  case ATM_ATTR_CONNEDSUB:
			len = sizeof(struct uni_ie_connedsub);
			memcpy(ptr, &conn->connedsub, len);
			break;

		  case ATM_ATTR_EETD:
			len = sizeof(struct uni_ie_eetd);
			memcpy(ptr, &conn->eetd, len);
			break;

		  case ATM_ATTR_ABRSETUP:
			len = sizeof(struct uni_ie_abrsetup);
			memcpy(ptr, &conn->abrsetup, len);
			break;

		  case ATM_ATTR_ABRADD:
			len = sizeof(struct uni_ie_abradd);
			memcpy(ptr, &conn->abradd, len);
			break;

		  case ATM_ATTR_CONNID:
			len = sizeof(struct uni_ie_connid);
			memcpy(ptr, &conn->connid, len);
			break;

		  case ATM_ATTR_MDCR:
			len = sizeof(struct uni_ie_mdcr);
			memcpy(ptr, &conn->mdcr, len);
			break;
		}
		ptr = (u_char *)ptr + len;
	}

	cc_user_ok(user, ATMRESP_ATTRS, val, total);

	CCFREE(val);
}

/*
 * Check whether the state is ok and return the connection
 */
static struct ccconn *
cc_set_check(struct ccuser *user)
{
	switch(user->state) {

	  case USER_OUT_PREPARING:
	  case USER_IN_ARRIVED:
		return (TAILQ_FIRST(&user->connq));

	  default:
		return (NULL);
	}
}

/*
 * Set connection attribute(s)
 */
static void
cc_attr_set(struct ccuser *user, struct ccconn *conn, uint32_t *attr,
    u_int count, u_char *val, size_t vallen)
{
	size_t total, len;
	u_int i;
	u_char *ptr;

	/* determine the length of the total attribute buffer */
	total = 0;
	ptr = val;
	for (i = 0; i < count; i++) {
		len = 0;
		switch ((enum atm_attribute)attr[i]) {

		  case ATM_ATTR_NONE:
			break;

		  case ATM_ATTR_BLLI_SELECTOR:
		    {
			uint32_t sel;

			if (conn->state != CONN_OUT_PREPARING)
				goto rdonly;
			memcpy(&sel, ptr, sizeof(sel));
			if (sel == 0 || sel > UNI_NUM_IE_BLLI)
				goto bad_val;
			len = sizeof(uint32_t);
			break;
		    }

		  case ATM_ATTR_BLLI:
			len = sizeof(struct uni_ie_blli);
			break;

		  case ATM_ATTR_BEARER:
			if (conn->state != CONN_OUT_PREPARING)
				goto rdonly;
			len = sizeof(struct uni_ie_bearer);
			break;

		  case ATM_ATTR_TRAFFIC:
			len = sizeof(struct uni_ie_traffic);
			break;

		  case ATM_ATTR_QOS:
			if (conn->state != CONN_OUT_PREPARING)
				goto rdonly;
			len = sizeof(struct uni_ie_qos);
			break;

		  case ATM_ATTR_EXQOS:
			len = sizeof(struct uni_ie_exqos);
			break;

		  case ATM_ATTR_CALLED:
			goto rdonly;

		  case ATM_ATTR_CALLEDSUB:
			if (conn->state != CONN_OUT_PREPARING)
				goto rdonly;
			len = sizeof(struct uni_ie_calledsub);
			break;

		  case ATM_ATTR_CALLING:
			if (conn->state != CONN_OUT_PREPARING)
				goto rdonly;
			len = sizeof(struct uni_ie_calling);
			break;

		  case ATM_ATTR_CALLINGSUB:
			if (conn->state != CONN_OUT_PREPARING)
				goto rdonly;
			len = sizeof(struct uni_ie_callingsub);
			break;

		  case ATM_ATTR_AAL:
			len = sizeof(struct uni_ie_aal);
			break;

		  case ATM_ATTR_EPREF:
			goto rdonly;

		  case ATM_ATTR_CONNED:
			goto rdonly;

		  case ATM_ATTR_CONNEDSUB:
			goto rdonly;

		  case ATM_ATTR_EETD:
			len = sizeof(struct uni_ie_eetd);
			break;

		  case ATM_ATTR_ABRSETUP:
			len = sizeof(struct uni_ie_abrsetup);
			break;

		  case ATM_ATTR_ABRADD:
			len = sizeof(struct uni_ie_abradd);
			break;

		  case ATM_ATTR_CONNID:
			len = sizeof(struct uni_ie_connid);
			break;

		  case ATM_ATTR_MDCR:
			if (conn->state != CONN_OUT_PREPARING)
				goto rdonly;
			len = sizeof(struct uni_ie_mdcr);
			break;
		}
		if (len == 0) {
			cc_user_err(user, ATMERR_BAD_ATTR);
			return;
		}
		total += len;
		ptr += len;
	}

	/* check the length */
	if (vallen != total) {
		cc_user_err(user, ATMERR_BAD_ARGS);
		return;
	}

	ptr = val;
	for (i = 0; i < count; i++) {
		len = 0;
		switch ((enum atm_attribute)attr[i]) {

		  case ATM_ATTR_NONE:
			break;

		  case ATM_ATTR_BLLI_SELECTOR:
		    {
			uint32_t sel;

			memcpy(&sel, ptr, sizeof(sel));
			conn->blli_selector = sel;
			len = sizeof(uint32_t);
			break;
		    }

		  case ATM_ATTR_BLLI:
			len = sizeof(struct uni_ie_blli);
			memcpy(&conn->blli[conn->blli_selector - 1], ptr, len);
			conn->dirty_attr |= CCDIRTY_BLLI;
			break;

		  case ATM_ATTR_BEARER:
			len = sizeof(struct uni_ie_bearer);
			memcpy(&conn->bearer, ptr, len);
			break;

		  case ATM_ATTR_TRAFFIC:
			len = sizeof(struct uni_ie_traffic);
			memcpy(&conn->traffic, ptr, len);
			conn->dirty_attr |= CCDIRTY_TRAFFIC;
			break;

		  case ATM_ATTR_QOS:
			len = sizeof(struct uni_ie_qos);
			memcpy(&conn->qos, ptr, len);
			break;

		  case ATM_ATTR_EXQOS:
			len = sizeof(struct uni_ie_exqos);
			memcpy(&conn->exqos, ptr, len);
			conn->dirty_attr |= CCDIRTY_EXQOS;
			break;

		  case ATM_ATTR_CALLED:
			len = sizeof(struct uni_ie_called);
			break;

		  case ATM_ATTR_CALLEDSUB:
			len = sizeof(struct uni_ie_calledsub);
			memcpy(&conn->calledsub, ptr, len);
			break;

		  case ATM_ATTR_CALLING:
			len = sizeof(struct uni_ie_calling);
			memcpy(&conn->calling, ptr, len);
			break;

		  case ATM_ATTR_CALLINGSUB:
			len = sizeof(struct uni_ie_callingsub);
			memcpy(&conn->callingsub, ptr, len);
			break;

		  case ATM_ATTR_AAL:
			len = sizeof(struct uni_ie_aal);
			memcpy(&conn->aal, ptr, len);
			conn->dirty_attr |= CCDIRTY_AAL;
			break;

		  case ATM_ATTR_EPREF:
			len = sizeof(struct uni_ie_epref);
			break;

		  case ATM_ATTR_CONNED:
			len = sizeof(struct uni_ie_conned);
			break;

		  case ATM_ATTR_CONNEDSUB:
			len = sizeof(struct uni_ie_connedsub);
			break;

		  case ATM_ATTR_EETD:
			len = sizeof(struct uni_ie_eetd);
			memcpy(&conn->eetd, ptr, len);
			conn->dirty_attr |= CCDIRTY_EETD;
			break;

		  case ATM_ATTR_ABRSETUP:
			len = sizeof(struct uni_ie_abrsetup);
			memcpy(&conn->abrsetup, ptr, len);
			conn->dirty_attr |= CCDIRTY_ABRSETUP;
			break;

		  case ATM_ATTR_ABRADD:
			len = sizeof(struct uni_ie_abradd);
			memcpy(&conn->abradd, ptr, len);
			conn->dirty_attr |= CCDIRTY_ABRADD;
			break;

		  case ATM_ATTR_CONNID:
			len = sizeof(struct uni_ie_connid);
			memcpy(&conn->connid, ptr, len);
			conn->dirty_attr |= CCDIRTY_CONNID;
			break;

		  case ATM_ATTR_MDCR:
			len = sizeof(struct uni_ie_mdcr);
			memcpy(&conn->mdcr, ptr, len);
			break;
		}
		ptr += len;
	}

	cc_user_ok(user, ATMRESP_NONE, NULL, 0);
	return;

  bad_val:
	cc_user_err(user, ATMERR_BAD_VALUE);
	return;

  rdonly:
	cc_user_err(user, ATMERR_RDONLY);
	return;
}

#ifdef CCATM_DEBUG
static const char *op_names[] = {
#define	S(OP)	[ATMOP_##OP] = #OP
	S(RESP),
	S(ABORT_CONNECTION),
	S(ACCEPT_INCOMING_CALL),
	S(ADD_PARTY),
	S(ADD_PARTY_REJECT),
	S(ADD_PARTY_SUCCESS),
	S(ARRIVAL_OF_INCOMING_CALL),
	S(CALL_RELEASE),
	S(CONNECT_OUTGOING_CALL),
	S(DROP_PARTY),
	S(GET_LOCAL_PORT_INFO),
	S(P2MP_CALL_ACTIVE),
	S(P2P_CALL_ACTIVE),
	S(PREPARE_INCOMING_CALL),
	S(PREPARE_OUTGOING_CALL),
	S(QUERY_CONNECTION_ATTRIBUTES),
	S(REJECT_INCOMING_CALL),
	S(SET_CONNECTION_ATTRIBUTES),
	S(WAIT_ON_INCOMING_CALL),
	S(SET_CONNECTION_ATTRIBUTES_X),
	S(QUERY_CONNECTION_ATTRIBUTES_X),
	S(QUERY_STATE),
#undef S
};
#endif

/*
 * Signal from user - map this to our internal signals and queue
 * the mapped signal.
 */
int
cc_user_signal(struct ccuser *user, enum atmop sig, struct uni_msg *msg)
{
	size_t len = uni_msg_len(msg);
	int err = EINVAL;

	if (user->cc->log & CCLOG_USER_SIG)
		cc_user_log(user, "signal %s to user", op_names[sig]);

	if ((u_int)sig > ATMOP_QUERY_STATE)
		goto bad_signal;

	switch (sig) {

	  case ATMOP_ABORT_CONNECTION:
		if (len != sizeof(struct atm_abort_connection))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_ABORT_CONNECTION, msg);
		break;

	  case ATMOP_ACCEPT_INCOMING_CALL:
		if (len != sizeof(struct atm_accept_incoming_call))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_ACCEPT_INCOMING, msg);
		break;

	  case ATMOP_ADD_PARTY:
		if (len != sizeof(struct atm_add_party))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_ADD_PARTY, msg);
		break;

	  case ATMOP_CALL_RELEASE:
		if (len != sizeof(struct atm_call_release))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_CALL_RELEASE, msg);
		break;

	  case ATMOP_CONNECT_OUTGOING_CALL:
		if (len != sizeof(struct atm_connect_outgoing_call))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_CONNECT_OUTGOING, msg);
		break;

	  case ATMOP_DROP_PARTY:
		if (len != sizeof(struct atm_drop_party))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_DROP_PARTY, msg);
		break;

	  case ATMOP_GET_LOCAL_PORT_INFO:
		if (len != sizeof(struct atm_get_local_port_info))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_GET_LOCAL_PORT_INFO, msg);
		break;

	  case ATMOP_PREPARE_INCOMING_CALL:
		if (len != sizeof(struct atm_prepare_incoming_call))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_PREPARE_INCOMING, msg);
		break;

	  case ATMOP_PREPARE_OUTGOING_CALL:
		if (len != 0)
			goto bad_len;
		uni_msg_destroy(msg);
		err = cc_user_sig(user, USER_SIG_PREPARE_OUTGOING, NULL, 0);
		break;

	  case ATMOP_QUERY_CONNECTION_ATTRIBUTES:
		if (len != sizeof(struct atm_query_connection_attributes))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_QUERY_ATTR, msg);
		break;

	  case ATMOP_REJECT_INCOMING_CALL:
		if (len != sizeof(struct atm_reject_incoming_call))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_REJECT_INCOMING, msg);
		break;

	  case ATMOP_SET_CONNECTION_ATTRIBUTES:
		if (len < sizeof(struct atm_set_connection_attributes))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_SET_ATTR, msg);
		break;

	  case ATMOP_WAIT_ON_INCOMING_CALL:
		if (len != 0)
			goto bad_len;
		uni_msg_destroy(msg);
		err = cc_user_sig(user, USER_SIG_WAIT_ON_INCOMING, NULL, 0);
		break;

	  case ATMOP_QUERY_CONNECTION_ATTRIBUTES_X:
		if (len < sizeof(struct atm_set_connection_attributes_x) ||
		    len != offsetof(struct atm_set_connection_attributes_x,
		    attr) + uni_msg_rptr(msg,
		    struct atm_set_connection_attributes_x *)->count *
		    sizeof(uint32_t))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_QUERY_ATTR_X, msg);
		break;

	  case ATMOP_SET_CONNECTION_ATTRIBUTES_X:
		if (len < sizeof(struct atm_set_connection_attributes_x))
			goto bad_len;
		err = cc_user_sig_msg(user, USER_SIG_SET_ATTR_X, msg);
		break;

	  case ATMOP_QUERY_STATE:
		if (len != 0)
			goto bad_len;
		uni_msg_destroy(msg);
		err = cc_user_sig(user, USER_SIG_QUERY_STATE, NULL, 0);
		break;

	  case ATMOP_RESP:
	  case ATMOP_ADD_PARTY_REJECT:
	  case ATMOP_ADD_PARTY_SUCCESS:
	  case ATMOP_ARRIVAL_OF_INCOMING_CALL:
	  case ATMOP_P2MP_CALL_ACTIVE:
	  case ATMOP_P2P_CALL_ACTIVE:
	  bad_signal:
		/* bad signal */
		if (user->cc->log & CCLOG_USER_SIG)
			cc_user_log(user, "bad signal %u", sig);
		cc_user_err(user, ATMERR_BAD_OP);
		uni_msg_destroy(msg);
		break;
	}
	return (err);

  bad_len:
	/* bad argument length */
	if (user->cc->log & CCLOG_USER_SIG)
		cc_user_log(user, "signal %s had bad len=%zu",
		    op_names[sig], len);
	cc_user_err(user, ATMERR_BAD_ARGS);
	uni_msg_destroy(msg);
	return (EINVAL);
}

/*
 * Send active signal to user
 */
static void
cc_user_active(struct ccuser *user)
{
	struct ccconn *conn = TAILQ_FIRST(&user->connq);

	set_state(user, USER_ACTIVE);
	if (conn->bearer.cfg == UNI_BEARER_P2P) {
		struct atm_p2p_call_active *act;

		user->config = USER_P2P;
		act = CCZALLOC(sizeof(*act));
		if (act == NULL)
			return;
		act->connid = conn->connid;
		cc_user_send(user, ATMOP_P2P_CALL_ACTIVE, act, sizeof(*act));
		CCFREE(act);
	} else {
		struct atm_p2mp_call_active *act;

		user->config = USER_ROOT;
		act = CCZALLOC(sizeof(*act));
		if (act == NULL)
			return;
		act->connid = conn->connid;
		cc_user_send(user, ATMOP_P2MP_CALL_ACTIVE, act, sizeof(*act));
		CCFREE(act);
	}
}

/*
* Handle a signal to this user
*/
void
cc_user_sig_handle(struct ccuser *user, enum user_sig sig,
    void *arg, u_int arg2)
{

	if (user->cc->log & CCLOG_USER_SIG)
		cc_user_log(user, "signal %s to user state %s",
		    cc_user_sigtab[sig], stab[user->state]);

	switch (sig) {


	  case USER_SIG_PREPARE_OUTGOING:
	    {
		/*
		 * Here we create a connection for the call we soon will make.
		 * We put this call on the list of orphaned connections,
		 * because we don't know yet, which port will get the
		 * connection. It is assigned, when the user issues the call
		 * to connect.
		 */
		struct ccconn *conn;

		if (user->state != USER_NULL) {
			cc_user_err(user, ATMERR_BAD_STATE);
			goto bad_state;
		}
		conn = cc_conn_create(user->cc);
		if (conn == NULL) {
			cc_user_err(user, ATMERR_NOMEM);
			return;
		}
		set_state(user, USER_OUT_PREPARING);
		cc_conn_set_state(conn, CONN_OUT_PREPARING);
		conn->blli_selector = 1;
		cc_connect_to_user(conn, user);

		cc_user_ok(user, ATMRESP_NONE, NULL, 0);
		return;
	    }


	  case USER_SIG_CONNECT_OUTGOING:
	    {
		/*
		 * Request to connect that call
		 *
		 * Here we assign the connection to a port.
		 */
		struct uni_msg *msg = arg;
		struct atm_connect_outgoing_call *req = uni_msg_rptr(msg,
		    struct atm_connect_outgoing_call *);
		struct ccdata *priv = user->cc;
		struct ccport *port;
		struct ccaddr *addr;
		struct ccconn *conn = TAILQ_FIRST(&user->connq);

		if (user->state != USER_OUT_PREPARING) {
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_BAD_STATE);
			goto bad_state;
		}
		if (!IE_ISPRESENT(req->called)) {
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_BAD_ARGS);
			return;
		}
		CCASSERT(conn->port == NULL, ("connection still on port"));

		if (TAILQ_EMPTY(&priv->port_list)) {
			/*
			 * We have no ports - reject
			 */
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_BAD_PORT);
			return;
		}

		/*
		 * Find the correct port
		 * Routing of outgoing calls goes to the lowest numbered port
		 * with a matching address or, if no address match is found to
		 * the lowest numbered port.
		 */
		TAILQ_FOREACH(port, &priv->port_list, node_link)
			TAILQ_FOREACH(addr, &port->addr_list, port_link)
				if (addr_matches(addr, conn))
					break;

		if (port == NULL)
			port = TAILQ_FIRST(&priv->port_list);

		cc_conn_ins_port(conn, port);
		conn->called = req->called;
		uni_msg_destroy(msg);

		/*
		 * Now move the state
		 */
		set_state(user, USER_OUT_WAIT_OK);
		cc_conn_sig(conn, CONN_SIG_CONNECT_OUTGOING, NULL);

		return;
	    }


	  case USER_SIG_CONNECT_OUTGOING_ERR:
		switch (user->state) {

		  case USER_OUT_WAIT_OK:
			set_state(user, USER_OUT_PREPARING);
			cc_user_err(user, arg2);
			break;

		  case USER_REL_WAIT_CONN:
		    {
			struct ccconn *conn;

			conn = TAILQ_FIRST(&user->connq);
			if (conn != NULL) {
				cc_disconnect_from_user(conn);
				cc_conn_destroy(conn);
			}

			cc_user_reset(user);
			cc_user_ok(user, ATMRESP_NONE, NULL, 0);
			break;
		    }

		  default:
			goto bad_state;
		}
		return;


	  case USER_SIG_CONNECT_OUTGOING_OK:
		switch (user->state) {

		  case USER_OUT_WAIT_OK:
			set_state(user, USER_OUT_WAIT_CONF);
			cc_user_ok(user, ATMRESP_NONE, NULL, 0);
			break;

		  case USER_REL_WAIT_CONN:
			set_state(user, USER_REL_WAIT_SCONF);
			break;

		  default:
			goto bad_state;
		}
		return;


	  case USER_SIG_SETUP_CONFIRM:
		/*
		 * SETUP.confirm from UNI stack.
		 */
		switch (user->state) {

		  case USER_OUT_WAIT_CONF:
			cc_user_active(user);
			break;

		  case USER_REL_WAIT_SCONF:
			/* now try to release */
			set_state(user, USER_REL_WAIT_CONF);
			cc_conn_sig(TAILQ_FIRST(&user->connq),
			    CONN_SIG_RELEASE, NULL);
			break;

		  default:
			goto bad_state;
		}
		return;


	  case USER_SIG_PREPARE_INCOMING:
	    {
		struct uni_msg *msg = arg;
		struct ccuser *ptr;
		struct atm_prepare_incoming_call *prep = uni_msg_rptr(msg,
		    struct atm_prepare_incoming_call *);

		if (user->state != USER_NULL) {
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_BAD_STATE);
			goto bad_state;
		}

		/*
		 * Check the SAP
		 */
		if (unisve_check_sap(&prep->sap) != UNISVE_OK) {
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_BAD_SAP);
			return;
		}

		/*
		 * Loop through all incoming calls and check whether there
		 * is an overlap in SAP space.
		 */
		LIST_FOREACH(ptr, &user->cc->user_list, node_link) {
			if (check_overlap(ptr, &prep->sap)) {
				uni_msg_destroy(msg);
				cc_user_err(user, ATMERR_OVERLAP);
				return;
			}
		}

		/*
		 * Save info and set state
		 */
		user->sap = CCZALLOC(sizeof(struct uni_sap));
		if (user->sap == NULL) {
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_NOMEM);
			return;
		}
		*user->sap = prep->sap;
		user->queue_max = prep->queue_size;
		user->queue_act = 0;
		uni_msg_destroy(msg);

		set_state(user, USER_IN_PREPARING);
		cc_user_ok(user, ATMRESP_NONE, NULL, 0);

		return;
	    }


	  case USER_SIG_WAIT_ON_INCOMING:
		if (user->state != USER_IN_PREPARING) {
			cc_user_err(user, ATMERR_BAD_STATE);
			goto bad_state;
		}

		set_state(user, USER_IN_WAITING);
		cc_user_ok(user, ATMRESP_NONE, NULL, 0);
		return;


	  case USER_SIG_SETUP_IND:
		/*
		 * New connection queued up in the queue. If this is the
		 * first one, inform the application of the arrival.
		 */
		switch (user->state) {

		  case USER_IN_WAITING:
			do_arrival(user);
			break;

		  case USER_IN_ARRIVED:
		  case USER_IN_WAIT_REJ:
		  case USER_IN_WAIT_ACC:
			break;

		  default:
			goto bad_state;
		}
		return;


	  case USER_SIG_REJECT_INCOMING:
	     {
		/*
		 * User rejects call. This is done on the OLD user
		 * (i.e. the one sending the arrival).
		 */
		struct uni_msg *msg = arg;
		struct atm_reject_incoming_call *rej = uni_msg_rptr(msg,
		    struct atm_reject_incoming_call *);
		struct ccconn *conn = TAILQ_FIRST(&user->connq);

		if (user->state != USER_IN_ARRIVED) {
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_BAD_STATE);
			goto bad_state;
		}
		if (user->aborted) {
			/* connection has disappeared. Send an ok
			 * to the user and lock whether there is another
			 * connection at this endpoint */
			uni_msg_destroy(msg);
			cc_user_ok(user, ATMRESP_NONE, NULL, 0);

			set_state(user, USER_IN_WAITING);
			do_arrival(user);
			return;
		}
		conn->cause[0] = rej->cause;
		memset(&conn->cause[1], 0, sizeof(conn->cause[1]));
		uni_msg_destroy(msg);

		set_state(user, USER_IN_WAIT_REJ);
		cc_conn_sig(conn, CONN_SIG_REJECT, NULL);

		return;
	    }


	  case USER_SIG_REJECT_OK:
		if (user->state != USER_IN_WAIT_REJ)
			goto bad_state;
		cc_user_ok(user, ATMRESP_NONE, NULL, 0);

		set_state(user, USER_IN_WAITING);
		do_arrival(user);
		return;


	  case USER_SIG_REJECT_ERR:
		if (user->state != USER_IN_WAIT_REJ)
			goto bad_state;
		cc_user_err(user, arg2);

		if (arg == NULL)
			set_state(user, USER_IN_ARRIVED);
		else {
			set_state(user, USER_IN_WAITING);
			do_arrival(user);
		}
		return;


	  case USER_SIG_ACCEPT_INCOMING:
	    {
		/*
		 * User accepts call. This is done on the OLD user (i.e. the one
		 * sending the arrival), the message contains a pointer to the
		 * new endpoint.
		 */
		struct uni_msg *msg = arg;
		struct atm_accept_incoming_call *acc =
		    uni_msg_rptr(msg, struct atm_accept_incoming_call *);
		struct ccuser *newep;

		if (user->state != USER_IN_ARRIVED) {
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_BAD_STATE);
			return;
		}
		if (user->aborted) {
			/* connection has disappeared. Send an error
			 * to the user and lock whether there is another
			 * connection at this endpoint */
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_PREVIOUSLY_ABORTED);

			set_state(user, USER_IN_WAITING);
			do_arrival(user);
			return;
		}
		acc->newep[sizeof(acc->newep) - 1] = '\0';

		LIST_FOREACH(newep, &user->cc->user_list, node_link)
			if (strcmp(acc->newep, newep->name) == 0)
				break;
		uni_msg_destroy(msg);

		if (newep == NULL) {
			cc_user_err(user, ATMERR_BAD_ENDPOINT);
			return;
		}

		if (newep->state != USER_NULL || newep->accepted != NULL) {
			cc_user_err(user, ATMERR_BAD_STATE);
			return;
		}

		set_state(user, USER_IN_WAIT_ACC);
		cc_conn_sig(TAILQ_FIRST(&user->connq), CONN_SIG_ACCEPT, newep);

		return;
	    }


	  case USER_SIG_ACCEPT_OK:
		if (user->state != USER_IN_WAIT_ACC)
			goto bad_state;
		cc_user_ok(user, ATMRESP_NONE, NULL, 0);

		set_state(user, USER_IN_WAITING);
		do_arrival(user);
		return;


	  case USER_SIG_ACCEPT_ERR:
		if (user->state != USER_IN_WAIT_ACC)
			goto bad_state;
		cc_user_err(user, arg2);

		if (arg == NULL) {
			/* arg used as flag! */
			set_state(user, USER_IN_ARRIVED);
		} else {
			set_state(user, USER_IN_WAITING);
			do_arrival(user);
		}
		return;


	  case USER_SIG_ACCEPTING:
		if (user->state != USER_NULL)
			goto bad_state;
		set_state(user, USER_IN_ACCEPTING);
		return;


	  case USER_SIG_SETUP_COMPL:
	    {
		struct ccconn *conn = TAILQ_FIRST(&user->connq);

		if (user->state != USER_IN_ACCEPTING)
			goto bad_state;

		user->state = USER_ACTIVE;
		if (conn->bearer.cfg == UNI_BEARER_P2P) {
			struct atm_p2p_call_active *act;

			user->config = USER_P2P;
			act = CCZALLOC(sizeof(*act));
			if (act == NULL)
				return;
			act->connid = conn->connid;
			cc_user_send(user, ATMOP_P2P_CALL_ACTIVE,
			    act, sizeof(*act));
			CCFREE(act);
		} else {
			struct atm_p2mp_call_active *act;

			user->config = USER_LEAF;
			act = CCZALLOC(sizeof(*act));
			if (act == NULL)
				return;
			act->connid = conn->connid;
			cc_user_send(user, ATMOP_P2MP_CALL_ACTIVE,
			    act, sizeof(*act));
			CCFREE(act);
		}
		return;
	    }


	  case USER_SIG_CALL_RELEASE:
	    {
		struct uni_msg *msg = arg;
		struct atm_call_release *api = uni_msg_rptr(msg,
		    struct atm_call_release *);
		struct ccconn *conn;

		conn = TAILQ_FIRST(&user->connq);
		switch (user->state) {

		  case USER_OUT_WAIT_OK:	/* U2/A3 */
			/* wait for CONN_OK first */
			conn->cause[0] = api->cause[0];
			conn->cause[1] = api->cause[1];
			set_state(user, USER_REL_WAIT_CONN);
			break;

		  case USER_OUT_WAIT_CONF:	/* U3/A3 */
			/* wait for SETUP.confirm first */
			conn->cause[0] = api->cause[0];
			conn->cause[1] = api->cause[1];
			set_state(user, USER_REL_WAIT_SCONF);
			break;

		  case USER_IN_ACCEPTING:	/* U11/A7 */
			conn->cause[0] = api->cause[0];
			conn->cause[1] = api->cause[1];
			set_state(user, USER_REL_WAIT_SCOMP);
			cc_conn_sig(conn, CONN_SIG_RELEASE, NULL);
			break;

		  case USER_ACTIVE:		/* U4/A8,A9,A10 */
			conn->cause[0] = api->cause[0];
			conn->cause[1] = api->cause[1];
			set_state(user, USER_REL_WAIT);
			cc_conn_sig(conn, CONN_SIG_RELEASE, NULL);
			break;

		  default:
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_BAD_STATE);
			goto bad_state;
		}
		uni_msg_destroy(msg);
		return;
	    }


	  case USER_SIG_RELEASE_CONFIRM:
	    {
		struct atm_call_release *ind;

		switch (user->state) {

		  case USER_OUT_WAIT_CONF:	/* U3/A3 */
		  case USER_ACTIVE:		/* U4/A8,A9,A10 */
			cc_user_reset(user);
			break;

		  case USER_REL_WAIT:		/* U5 /A8,A9,A10 */
		  case USER_REL_WAIT_SCOMP:	/* U12/A7 */
		  case USER_REL_WAIT_SCONF:	/* U13/A3 */
		  case USER_REL_WAIT_CONF:	/* U14/A3 */
			cc_user_reset(user);
			cc_user_ok(user, ATMRESP_NONE, NULL, 0);
			return;

		  case USER_IN_ACCEPTING:	/* U11/A7 */
			cc_user_reset(user);
			break;

		  default:
			goto bad_state;
		}

		ind = CCZALLOC(sizeof(*ind));
		if (ind == NULL)
			return;
		memcpy(ind->cause, user->cause, sizeof(ind->cause));
		cc_user_send(user, ATMOP_CALL_RELEASE, ind, sizeof(*ind));
		CCFREE(ind);
		return;
	    }


	  case USER_SIG_RELEASE_ERR:
		switch (user->state) {

		  case USER_REL_WAIT:		/* U5/A8,A9,A10 */
			set_state(user, USER_ACTIVE);
			cc_user_err(user, ATM_MKUNIERR(arg2));
			break;

		  case USER_REL_WAIT_CONF:	/* U14/A3 */
			cc_user_err(user, ATM_MKUNIERR(arg2));
			cc_user_active(user);
			break;

		  case USER_REL_WAIT_SCOMP:	/* U12/A7 */
			set_state(user, USER_IN_ACCEPTING);
			cc_user_err(user, ATM_MKUNIERR(arg2));
			break;

		  default:
			goto bad_state;
		}
		return;


	  case USER_SIG_ADD_PARTY:
	    {
		struct uni_msg *msg = arg;
		struct atm_add_party *add = uni_msg_rptr(msg,
		    struct atm_add_party *);
		struct ccconn *conn;

		if (user->state != USER_ACTIVE || user->config != USER_ROOT) {
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_BAD_STATE);
			return;
		}

		if (add->leaf_ident == 0 || add->leaf_ident >= 32786) {
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_BAD_LEAF_IDENT);
			return;
		}

		conn = TAILQ_FIRST(&user->connq);
		conn->called = add->called;

		cc_conn_sig(conn, CONN_SIG_ADD_PARTY,
		    (void *)(uintptr_t)add->leaf_ident);

		uni_msg_destroy(msg);
		return;
	    }


	  case USER_SIG_ADD_PARTY_ERR:
		if (user->state != USER_ACTIVE)
			goto bad_state;
		cc_user_err(user, arg2);
		return;


	  case USER_SIG_ADD_PARTY_OK:
		if (user->state != USER_ACTIVE)
			goto bad_state;
		cc_user_ok(user, ATMRESP_NONE, NULL, 0);
		return;


	  case USER_SIG_ADD_PARTY_ACK:
	    {
		u_int leaf_ident = arg2;
		struct atm_add_party_success *succ;

		if (user->state != USER_ACTIVE)
			goto bad_state;

		succ = CCZALLOC(sizeof(*succ));
		if (succ == NULL)
			return;

		succ->leaf_ident = leaf_ident;
		cc_user_send(user, ATMOP_ADD_PARTY_SUCCESS,
		    succ, sizeof(*succ));

		CCFREE(succ);
		return;
	    }


	  case USER_SIG_ADD_PARTY_REJ:
	    {
		u_int leaf_ident = arg2;
		struct atm_add_party_reject *reject;

		if (user->state != USER_ACTIVE)
			goto bad_state;

		reject = CCZALLOC(sizeof(*reject));
		if (reject == NULL)
			return;

		reject->leaf_ident = leaf_ident;
		reject->cause = user->cause[0];
		cc_user_send(user, ATMOP_ADD_PARTY_REJECT,
		    reject, sizeof(*reject));

		CCFREE(reject);
		return;
	    }


	  case USER_SIG_DROP_PARTY:
	    {
		struct uni_msg *msg = arg;
		struct atm_drop_party *drop = uni_msg_rptr(msg,
		    struct atm_drop_party *);
		struct ccconn *conn;

		if (user->state != USER_ACTIVE || user->config != USER_ROOT) {
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_BAD_STATE);
			return;
		}

		if (drop->leaf_ident >= 32786) {
			uni_msg_destroy(msg);
			cc_user_err(user, ATMERR_BAD_LEAF_IDENT);
			return;
		}

		conn = TAILQ_FIRST(&user->connq);
		conn->cause[0] = drop->cause;
		memset(&conn->cause[1], 0, sizeof(conn->cause[1]));

		cc_conn_sig(conn, CONN_SIG_DROP_PARTY,
		    (void *)(uintptr_t)drop->leaf_ident);

		uni_msg_destroy(msg);
		return;
	    }


	  case USER_SIG_DROP_PARTY_ERR:
		if (user->state != USER_ACTIVE)
			goto bad_state;
		cc_user_err(user, arg2);
		return;


	  case USER_SIG_DROP_PARTY_OK:
		if (user->state != USER_ACTIVE)
			goto bad_state;
		cc_user_ok(user, ATMRESP_NONE, NULL, 0);
		return;


	  case USER_SIG_DROP_PARTY_IND:
	    {
		u_int leaf_ident = arg2;
		struct atm_drop_party *drop;

		if (user->state != USER_ACTIVE)
			goto bad_state;

		drop = CCZALLOC(sizeof(*drop));
		if (drop == NULL)
			return;

		drop->leaf_ident = leaf_ident;
		drop->cause = user->cause[0];
		cc_user_send(user, ATMOP_DROP_PARTY, drop, sizeof(*drop));

		CCFREE(drop);
		return;
	    }


	  case USER_SIG_QUERY_ATTR:
	    {
		struct uni_msg *msg = arg;
		struct atm_query_connection_attributes *req;
		struct ccconn *conn;

		if (user->aborted) {
			cc_user_err(user, ATMERR_PREVIOUSLY_ABORTED);
			uni_msg_destroy(msg);
			return;
		}
		conn = cc_query_check(user);
		if (conn == NULL) {
			cc_user_err(user, ATMERR_BAD_STATE);
			uni_msg_destroy(msg);
			return;
		}
		req = uni_msg_rptr(msg,
		    struct atm_query_connection_attributes *);
		cc_attr_query(user, conn, &req->attr, 1);
		uni_msg_destroy(msg);
		return;
	    }

	  case USER_SIG_QUERY_ATTR_X:
	    {
		struct uni_msg *msg = arg;
		struct atm_query_connection_attributes_x *req;
		struct ccconn *conn;

		conn = cc_query_check(user);
		if (conn == NULL) {
			cc_user_err(user, ATMERR_BAD_STATE);
			uni_msg_destroy(msg);
			return;
		}
		req = uni_msg_rptr(msg,
		    struct atm_query_connection_attributes_x *);
		cc_attr_query(user, conn, req->attr, req->count);
		uni_msg_destroy(msg);
		return;
	    }

	  case USER_SIG_SET_ATTR:
	    {
		struct uni_msg *msg = arg;
		struct atm_set_connection_attributes *req;
		struct ccconn *conn;

		if (user->aborted) {
			cc_user_err(user, ATMERR_PREVIOUSLY_ABORTED);
			uni_msg_destroy(msg);
			return;
		}
		conn = cc_set_check(user);
		if (conn == NULL) {
			cc_user_err(user, ATMERR_BAD_STATE);
			uni_msg_destroy(msg);
			return;
		}
		req = uni_msg_rptr(msg, struct atm_set_connection_attributes *);
		cc_attr_set(user, conn, &req->attr, 1, (u_char *)(req + 1),
		    uni_msg_len(msg) - sizeof(*req));
		uni_msg_destroy(msg);
		return;
	    }

	  case USER_SIG_SET_ATTR_X:
	    {
		struct uni_msg *msg = arg;
		struct atm_set_connection_attributes_x *req;
		struct ccconn *conn;

		conn = cc_set_check(user);
		if (conn == NULL) {
			cc_user_err(user, ATMERR_BAD_STATE);
			uni_msg_destroy(msg);
			return;
		}
		req = uni_msg_rptr(msg,
		    struct atm_set_connection_attributes_x *);
		cc_attr_set(user, conn, req->attr, req->count,
		    (u_char *)req->attr + req->count * sizeof(req->attr[0]),
		    uni_msg_len(msg) -
		    offsetof(struct atm_set_connection_attributes_x, attr) -
		    req->count * sizeof(req->attr[0]));
		uni_msg_destroy(msg);
		return;
	    }

	  case USER_SIG_QUERY_STATE:
	    {
		struct atm_epstate state;

		strcpy(state.name, user->name);
		switch (user->state) {

		  case USER_NULL:
			if (user->accepted != NULL)
				state.state = ATM_A7;
			else
				state.state = ATM_A1;
			break;

		  case USER_OUT_PREPARING:
			state.state = ATM_A2;
			break;

		  case USER_OUT_WAIT_OK:
		  case USER_OUT_WAIT_CONF:
		  case USER_REL_WAIT_SCONF:
		  case USER_REL_WAIT_CONF:
		  case USER_REL_WAIT_CONN:
			state.state = ATM_A3;
			break;

		  case USER_ACTIVE:
		  case USER_REL_WAIT:
			switch (user->config) {

			  case USER_P2P:
				state.state = ATM_A8;
				break;

			  case USER_ROOT:
				state.state = ATM_A9;
				break;

			  case USER_LEAF:
				state.state = ATM_A10;
				break;
			}
			break;

		  case USER_IN_PREPARING:
			state.state = ATM_A4;
			break;

		  case USER_IN_WAITING:
			state.state = ATM_A5;
			break;

		  case USER_IN_ARRIVED:
		  case USER_IN_WAIT_REJ:
		  case USER_IN_WAIT_ACC:
			state.state = ATM_A6;
			break;

		  case USER_IN_ACCEPTING:
		  case USER_REL_WAIT_SCOMP:
			state.state = ATM_A7;
			break;
		}
		cc_user_ok(user, ATMRESP_STATE, &state, sizeof(state));
		return;
	    }

	  case USER_SIG_GET_LOCAL_PORT_INFO:
	    {
		struct uni_msg *msg = arg;
		struct atm_port_list *list;
		size_t list_len;

		list = cc_get_local_port_info(user->cc,
		    uni_msg_rptr(msg, struct atm_get_local_port_info *)->port,
		    &list_len);
		uni_msg_destroy(msg);
		if (list == NULL) {
			cc_user_err(user, ATMERR_NOMEM);
			return;
		}
		cc_user_ok(user, ATMRESP_PORTS, list, list_len);
		CCFREE(list);
		return;
	    }

	  case USER_SIG_ABORT_CONNECTION:
	    {
		struct uni_msg *msg = arg;
		struct atm_abort_connection *abo = uni_msg_rptr(msg,
		    struct atm_abort_connection *);

		cc_user_abort(user, &abo->cause);
		uni_msg_destroy(msg);
		cc_user_ok(user, ATMRESP_NONE, NULL, 0);
		return;
	    }

	}
	if (user->cc->log & CCLOG_USER_SIG)
		cc_user_log(user, "bad signal=%u in state=%u",
		    sig, user->state);
	return;

  bad_state:
	if (user->cc->log & CCLOG_USER_SIG)
		cc_user_log(user, "bad state=%u for signal=%u",
		    user->state, sig);
	return;
}
