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
 * $Begemot: libunimsg/netnatm/api/cc_port.c,v 1.1 2004/07/08 08:21:53 brandt Exp $
 *
 * ATM API as defined per af-saa-0108
 *
 * Port-global stuff (ILMI and Co.)
 */
#include <netnatm/unimsg.h>
#include <netnatm/msg/unistruct.h>
#include <netnatm/api/unisap.h>
#include <netnatm/sig/unidef.h>
#include <netnatm/api/atmapi.h>
#include <netnatm/api/ccatm.h>
#include <netnatm/api/ccpriv.h>

/*
 * Find a port with a given number
 */
static struct ccport *
find_port(struct ccdata *cc, u_int portno)
{
	struct ccport *port;

	TAILQ_FOREACH(port, &cc->port_list, node_link)
		if (port->param.port == portno)
			return (port);
	return (NULL);
}

/*
 * Create a new port structure, initialize it and link it to the node.
 * Returns 0 on success, an errno otherwise.
 */
struct ccport *
cc_port_create(struct ccdata *cc, void *uarg, u_int portno)
{
	struct ccport *port, *p1;

	if (portno == 0 || portno > 0xffffffff)
		return (NULL);

	TAILQ_FOREACH(port, &cc->port_list, node_link)
		if (port->param.port == portno)
			return (NULL);

	port = CCZALLOC(sizeof(*port));
	if (port == NULL)
		return (NULL);

	port->uarg = uarg;
	port->cc = cc;
	port->admin = CCPORT_STOPPED;
	LIST_INIT(&port->conn_list);
	TAILQ_INIT(&port->addr_list);
	port->param.port = portno;
	port->param.pcr = 350053;
	port->param.max_vpi_bits = 0;
	port->param.max_vci_bits = 8;
	port->param.max_svpc_vpi = 0;
	port->param.max_svcc_vpi = 0;
	port->param.min_svcc_vci = 32;
	port->param.num_addrs = 0;
	TAILQ_INIT(&port->cookies);

	TAILQ_FOREACH(p1, &cc->port_list, node_link)
		if (p1->param.port > portno) {
			TAILQ_INSERT_BEFORE(p1, port, node_link);
			break;
		}
	if (p1 == NULL)
		TAILQ_INSERT_TAIL(&cc->port_list, port, node_link);

	return (port);
}

/*
 * Destroy a port. This closes all connections and aborts all the users of
 * these connections.
 * This should be called only after work has returned so that no signals
 * are pending.
 */
void
cc_port_destroy(struct ccport *port, int shutdown)
{
	struct ccaddr *addr;
	struct ccreq *r;

	TAILQ_REMOVE(&port->cc->port_list, port, node_link);

	while ((r = TAILQ_FIRST(&port->cookies)) != NULL) {
		TAILQ_REMOVE(&port->cookies, r, link);
		CCFREE(r);
	}

	/*
	 * Abort all connections.
	 */
	while (!LIST_EMPTY(&port->conn_list))
		cc_conn_abort(LIST_FIRST(&port->conn_list), shutdown);

	/*
	 * Free addresses.
	 */
	while ((addr = TAILQ_FIRST(&port->addr_list)) != NULL) {
		TAILQ_REMOVE(&port->addr_list, addr, port_link);
		CCFREE(addr);
	}

	CCFREE(port);
}

/*
 * Management is given up on this node. Remove all addresses from the port.
 */
void
cc_unmanage(struct ccdata *cc)
{
	struct ccport *port;
	struct ccaddr *addr;

	TAILQ_FOREACH(port, &cc->port_list, node_link) {
		while ((addr = TAILQ_FIRST(&port->addr_list)) != NULL) {
			TAILQ_REMOVE(&port->addr_list, addr, port_link);
			CCFREE(addr);
		}
	}
}

/*
 * Compare two addresses
 */
static __inline int
addr_eq(const struct uni_addr *a1, const struct uni_addr *a2)
{
	return (a1->type == a2->type && a1->plan == a2->plan &&
	    a1->len == a2->len && memcmp(a1->addr, a2->addr, a1->len) == 0);
}


/*
 * retrieve addresses
 */
int
cc_get_addrs(struct ccdata *cc, u_int portno,
    struct uni_addr **pa, u_int **ports, u_int *count)
{
	struct ccport *port = NULL;
	struct ccaddr *addr;
	struct uni_addr *buf, *ptr;
	u_int *pports;

	/*
	 * If a port number is specified and the port does not exist,
	 * return an error.
	 */
	if (portno != 0)
		if ((port = find_port(cc, portno)) == NULL)
			return (ENOENT);

	/*
	 * Count the addresses
	 */
	*count = 0;
	if (portno != 0) {
		TAILQ_FOREACH(addr, &port->addr_list, port_link)
			(*count)++;
	} else {
		TAILQ_FOREACH(port, &cc->port_list, node_link)
			TAILQ_FOREACH(addr, &port->addr_list, port_link)
				(*count)++;
	}

	buf = CCMALLOC(*count * sizeof(struct uni_addr));
	if (buf == NULL)
		return (ENOMEM);
	ptr = buf;

	*ports = CCMALLOC(*count * sizeof(u_int));
	if (*ports == NULL) {
		CCFREE(buf);
		return (ENOMEM);
	}
	pports = *ports;

	if (portno != 0) {
		TAILQ_FOREACH(addr, &port->addr_list, port_link) {
			*ptr++ = addr->addr;
			*pports++ = portno;
		}
	} else {
		TAILQ_FOREACH(port, &cc->port_list, node_link)
			TAILQ_FOREACH(addr, &port->addr_list, port_link) {
				*ptr++ = addr->addr;
				*pports++ = port->param.port;
			}
	}

	*pa = buf;
	return (0);
}

/*
 * return port number
 */
u_int
cc_port_no(struct ccport *port)
{
	return (port->param.port);
}

/*
 * Address unregisterd.
 */
int
cc_addr_unregister(struct ccdata *cc, u_int portno, const struct uni_addr *arg)
{
	struct ccport *port;
	struct ccaddr *a;

	if ((port = find_port(cc, portno)) == NULL)
		return (ENOENT);

	/* Find the address */
	TAILQ_FOREACH(a, &port->addr_list, port_link)
		if (addr_eq(arg, &a->addr)) {
			TAILQ_REMOVE(&port->addr_list, a, port_link);
			CCFREE(a);
			return (0);
		}

	return (ENOENT);
}

/*
 * Address registerd.
 */
int
cc_addr_register(struct ccdata *cc, u_int portno, const struct uni_addr *arg)
{
	struct ccport *port, *p1;
	struct ccaddr *a;

	if ((port = find_port(cc, portno)) == NULL)
		return (ENOENT);

	/* maybe we know it already? */
	TAILQ_FOREACH(p1, &port->cc->port_list, node_link)
		TAILQ_FOREACH(a, &p1->addr_list, port_link)
			if (addr_eq(arg, &a->addr))
				return (EISCONN);

	a = CCZALLOC(sizeof(*a));
	if (a == NULL)
		return (ENOMEM);
	a->addr = *arg;

	TAILQ_INSERT_TAIL(&port->addr_list, a, port_link);

	return (0);
}

/*
 * Set/get port parameters.
 */
int
cc_port_get_param(struct ccdata *cc, u_int portno,
    struct atm_port_info *param)
{
	struct ccport *port;

	if ((port = find_port(cc, portno)) == NULL)
		return (ENOENT);

	*param = port->param;
	return (0);
}

/* XXX maybe allow only in stopped. */
int
cc_port_set_param(struct ccdata *cc, const struct atm_port_info *param)
{
	struct ccport *port;
	struct ccaddr *addr;

	if ((port = find_port(cc, param->port)) == NULL)
		return (ENOENT);

	port->param = *param;

	port->param.num_addrs = 0;
	TAILQ_FOREACH(addr, &port->addr_list, port_link)
		port->param.num_addrs++;

	return (0);
}

/*
 * get port list
 */
int
cc_port_getlist(struct ccdata *cc, u_int *cnt, u_int **ports)
{
	struct ccport *p;
	u_int n;

	n = 0;
	TAILQ_FOREACH(p, &cc->port_list, node_link)
		n++;

	*ports = CCMALLOC(n * sizeof(u_int));
	if (*ports == NULL)
		return (ENOMEM);

	n = 0;
	TAILQ_FOREACH(p, &cc->port_list, node_link)
		(*ports)[n++] = p->param.port;
	*cnt = n;

	return (0);
}

/*
 * START and STOP signalling
 */
int
cc_port_start(struct ccdata *cc, u_int portno)
{
	struct ccport *port;

	if ((port = find_port(cc, portno)) == NULL)
		return (ENOENT);
	if (port->admin != CCPORT_STOPPED)
		return (EISCONN);

	cc->funcs->send_uni_glob(port, port->uarg,
	    UNIAPI_LINK_ESTABLISH_request, 0, NULL);
	port->admin = CCPORT_RUNNING;

	return (0);
}

int
cc_port_stop(struct ccdata *cc, u_int portno)
{
	struct ccport *port;

	if ((port = find_port(cc, portno)) == NULL)
		return (ENOENT);
	if (port->admin != CCPORT_RUNNING)
		return (ENOTCONN);

	port->admin = CCPORT_STOPPED;

	/*
	 * Abort all connections.
	 */
	while (!LIST_EMPTY(&port->conn_list))
		cc_conn_destroy(LIST_FIRST(&port->conn_list));

	return (0);
}

/*
 * is port running?
 */
int
cc_port_isrunning(struct ccdata *cc, u_int portno, int *state)
{
	struct ccport *port;

	if ((port = find_port(cc, portno)) == NULL)
		return (ENOENT);
	if (port->admin == CCPORT_RUNNING)
		*state = 1;
	else
		*state = 0;
	return (0);
}

/*
 * Clear address and prefix information from the named port.
 */
int
cc_port_clear(struct ccdata *cc, u_int portno)
{
	struct ccaddr *addr;
	struct ccport *port;

	if ((port = find_port(cc, portno)) == NULL)
		return (ENOENT);

	while ((addr = TAILQ_FIRST(&port->addr_list)) != NULL) {
		TAILQ_REMOVE(&port->addr_list, addr, port_link);
		CCFREE(addr);
	}
	return (0);
}

/*
 * retrieve info on local ports
 */
struct atm_port_list *
cc_get_local_port_info(struct ccdata *cc, u_int portno, size_t *lenp)
{
	struct atm_port_list *list;
	struct atm_port_info *pp;
	struct uni_addr *aa;
	struct ccaddr *addr;
	struct ccport *port;
	u_int nports, naddrs;

	/*
	 * Count ports and addresses.
	 */
	nports = 0;
	naddrs = 0;
	TAILQ_FOREACH(port, &cc->port_list, node_link) {
		if (portno == 0 || port->param.port == portno) {
			nports++;
			TAILQ_FOREACH(addr, &port->addr_list, port_link)
				naddrs++;
		}
	}

	/*
	 * Size and allocate message
	 */
	*lenp = sizeof(*list) + nports * sizeof(*pp) + naddrs * sizeof(*aa);

	list = CCZALLOC(*lenp);
	if (list == NULL)
		return (NULL);

	/*
	 * Fill the message.
	 */
	list->num_ports = nports;
	list->num_addrs = naddrs;

	pp = (void *)((u_char *)list + sizeof(*list));
	aa = (void *)((u_char *)list + sizeof(*list) + nports * sizeof(*pp));

	TAILQ_FOREACH(port, &cc->port_list, node_link) {
		if (portno == 0 || port->param.port == portno) {
			*pp = port->param;
			pp->num_addrs = 0;
			TAILQ_FOREACH(addr, &port->addr_list, port_link) {
				*aa++ = addr->addr;
				pp->num_addrs++;
			}
			pp++;
		}
	}

	return (list);
}

static struct ccreq *
find_cookie(struct ccport *port, u_int cookie)
{
	struct ccreq *r;

	TAILQ_FOREACH(r, &port->cookies, link)
		if (r->cookie == cookie)
			return (r);
	return (NULL);
}

/*
 * input a response from the UNI layer to CC
 */
int
cc_uni_response(struct ccport *port, u_int cookie, u_int reason, u_int state)
{
	struct ccconn *conn;
	struct ccreq *req;

	if (cookie == 0)
		return (EINVAL);

	if (port->admin != CCPORT_RUNNING)
		return (ENOTCONN);

	if ((req = find_cookie(port, cookie)) == NULL) {
		cc_port_log(port, "UNI response for unknown cookie %u", cookie);
		return (EINVAL);
	}
	conn = req->conn;

	TAILQ_REMOVE(&port->cookies, req, link);
	CCFREE(req);

	if (reason == UNIAPI_OK)
		return (cc_conn_resp(conn, CONN_SIG_OK,
		    cookie, reason, state));
	else
		return (cc_conn_resp(conn, CONN_SIG_ERROR,
		    cookie, reason, state));
}

static struct ccconn *
find_cref(const struct ccport *port, const struct uni_cref *cref)
{
	struct ccconn *conn;

	LIST_FOREACH(conn, &port->conn_list, port_link)
		if (conn->cref.cref == cref->cref &&
		    conn->cref.flag == cref->flag)
			return (conn);
	return (NULL);
}

/*
 * Signal from UNI on this port
 */
int
cc_uni_signal(struct ccport *port, u_int cookie, u_int sig, struct uni_msg *msg)
{
	int error = 0;
	size_t len, ilen = 0;
	struct uni_cref *cref;
	struct ccconn *conn;

	if (port->admin != CCPORT_RUNNING) {
		error = ENOTCONN;
		goto out;
	}
	len = (msg != NULL) ? uni_msg_len(msg) : 0;

	switch ((enum uni_sig)sig) {

	  case UNIAPI_ERROR:
		/* handled above */
		cc_port_log(port, "bad UNIAPI_ERROR cookie=%u", cookie);
		error = EINVAL;
		break;

	  case UNIAPI_CALL_CREATED:
		ilen = sizeof(struct uniapi_call_created);
		if (len != ilen)
			goto bad_len;

		if (cookie != 0) {
			/* outgoing call */
			struct ccreq *req;

			if ((req = find_cookie(port, cookie)) == NULL) {
				cc_port_log(port, "bad cookie %u in CREATE",
				    cookie);
				error = EINVAL;
				goto out;
			}
			conn = req->conn;

		} else {
			if ((conn = cc_conn_create(port->cc)) == NULL) {
				error = ENOMEM;
				goto out;
			}
			cc_conn_ins_port(conn, port);
		}

		cc_conn_sig_msg_nodef(conn, CONN_SIG_CREATED, msg);
		msg = NULL;
		goto out;

	  case UNIAPI_CALL_DESTROYED:
		ilen = sizeof(struct uniapi_call_destroyed);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg, struct uniapi_call_destroyed *)->cref;
		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig(conn, CONN_SIG_DESTROYED, NULL);
		goto out;

	  case UNIAPI_LINK_ESTABLISH_confirm:
		goto out;

	  case UNIAPI_LINK_RELEASE_confirm:
		/* Ups. If we administratively up, restart the link */
		if (port->admin == CCPORT_RUNNING)
			port->cc->funcs->send_uni_glob(port, port->uarg,
			    UNIAPI_LINK_ESTABLISH_request, 0, NULL);
		goto out;

	  case UNIAPI_PARTY_CREATED:
		ilen = sizeof(struct uniapi_party_created);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg, struct uniapi_party_created *)->cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg_nodef(conn,
		     CONN_SIG_PARTY_CREATED, msg);
		msg = NULL;
		goto out;

	  case UNIAPI_PARTY_DESTROYED:
		ilen = sizeof(struct uniapi_party_destroyed);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg,
		    struct uniapi_party_destroyed *)->cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_PARTY_DESTROYED, msg);
		msg = NULL;
		goto out;

	  case UNIAPI_DROP_PARTY_ACK_indication:	/* UNI -> API */
		ilen = sizeof(struct uniapi_drop_party_ack_indication);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg,
		    struct uniapi_drop_party_ack_indication *)->drop.hdr.cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_DROP_PARTY_ACK_IND, msg);
		msg = NULL;
		goto out;

	  case UNIAPI_RESET_indication:			/* UNI -> API */
	    {
		/*
		 * XXX - do the right thing
		 */
		struct uniapi_reset_indication *ind = uni_msg_rptr(msg,
		    struct uniapi_reset_indication *);
		struct uniapi_reset_response *resp;
		struct uni_msg *u;
		
		/*
		 * Construct message to UNI.
		 */
		if ((u = uni_msg_alloc(sizeof(*resp))) == NULL)
			return (ENOMEM);

		resp = uni_msg_wptr(u, struct uniapi_reset_response *);
		memset(resp, 0, sizeof(*resp));
		u->b_wptr += sizeof(*resp);

		resp->restart = ind->restart;
		resp->connid = ind->connid;

		port->cc->funcs->send_uni_glob(port, port->uarg,
		    UNIAPI_RESET_response, 0, u);

		goto out;
	    }

	  case UNIAPI_RELEASE_indication:		/* UNI -> API */
		ilen = sizeof(struct uniapi_release_indication);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg, struct uniapi_release_indication *)
		    ->release.hdr.cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_REL_IND, msg);
		msg = NULL;
		goto out;

	  case UNIAPI_RELEASE_confirm:			/* UNI -> API */
		ilen = sizeof(struct uniapi_release_confirm);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg, struct uniapi_release_confirm *)
		    ->release.hdr.cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_REL_CONF, msg);
		msg = NULL;
		goto out;

	  case UNIAPI_SETUP_confirm:			/* UNI -> API */
		ilen = sizeof(struct uniapi_setup_confirm);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg, struct uniapi_setup_confirm *)
		    ->connect.hdr.cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_SETUP_CONFIRM, msg);
		msg = NULL;
		goto out;


	  case UNIAPI_ALERTING_indication:		/* UNI -> API */
		ilen = sizeof(struct uniapi_alerting_indication);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg, struct uniapi_alerting_indication *)
		    ->alerting.hdr.cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_ALERTING_IND, msg);
		msg = NULL;
		goto out;


	  case UNIAPI_PROCEEDING_indication:		/* UNI -> API */
		ilen = sizeof(struct uniapi_proceeding_indication);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg, struct uniapi_proceeding_indication *)
		    ->call_proc.hdr.cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_PROC_IND, msg);
		msg = NULL;
		goto out;


	  case UNIAPI_SETUP_indication:			/* UNI -> API */
		ilen = sizeof(struct uniapi_setup_indication);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg, struct uniapi_setup_indication *)
		    ->setup.hdr.cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_SETUP_IND, msg);
		msg = NULL;
		goto out;

	  case UNIAPI_SETUP_COMPLETE_indication:	/* UNI -> API */
		ilen = sizeof(struct uniapi_setup_complete_indication);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg,
		    struct uniapi_setup_complete_indication *)
		    ->connect_ack.hdr.cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_SETUP_COMPL, msg);
		msg = NULL;
		goto out;

	  case UNIAPI_PARTY_ALERTING_indication:	/* UNI -> API */
		ilen = sizeof(struct uniapi_party_alerting_indication);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg,
		    struct uniapi_party_alerting_indication *)->alert.hdr.cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_PARTY_ALERTING_IND, msg);
		msg = NULL;
		goto out;

	  case UNIAPI_ADD_PARTY_ACK_indication:		/* UNI -> API */
		ilen = sizeof(struct uniapi_add_party_ack_indication);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg,
		    struct uniapi_add_party_ack_indication *)->ack.hdr.cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_PARTY_ADD_ACK_IND, msg);
		msg = NULL;
		goto out;

	  case UNIAPI_ADD_PARTY_REJ_indication:		/* UNI -> API */
		ilen = sizeof(struct uniapi_add_party_rej_indication);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg,
		    struct uniapi_add_party_rej_indication *)->rej.hdr.cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_PARTY_ADD_REJ_IND, msg);
		msg = NULL;
		goto out;

	  case UNIAPI_DROP_PARTY_indication:		/* UNI -> API */
		ilen = sizeof(struct uniapi_drop_party_indication);
		if (len != ilen)
			goto bad_len;

		cref = &uni_msg_rptr(msg, struct uniapi_drop_party_indication *)
		    ->drop.hdr.cref;

		if ((conn = find_cref(port, cref)) == NULL)
			goto unk_call;

		error = cc_conn_sig_msg(conn, CONN_SIG_DROP_PARTY_IND, msg);
		msg = NULL;
		goto out;

	  case UNIAPI_RESET_confirm:			/* UNI -> API */
	  case UNIAPI_RESET_ERROR_indication:		/* UNI -> API */
	  case UNIAPI_RESET_STATUS_indication:		/* UNI -> API */
		/* XXX */
		goto out;

	  case UNIAPI_NOTIFY_indication:		/* UNI -> API */
	  case UNIAPI_STATUS_indication:		/* UNI -> API */
		break;

	  case UNIAPI_ADD_PARTY_indication:		/* UNI -> API */
		/* not supported by the API */
		break;

	/*
	 * All these are illegal in this direction
	 */
	  case UNIAPI_LINK_ESTABLISH_request:		/* API -> UNI */
	  case UNIAPI_LINK_RELEASE_request:		/* API -> UNI */
	  case UNIAPI_RESET_request:			/* API -> UNI */
	  case UNIAPI_RESET_response:			/* API -> UNI */
	  case UNIAPI_RESET_ERROR_response:		/* API -> UNI */
	  case UNIAPI_SETUP_request:			/* API -> UNI */
	  case UNIAPI_SETUP_response:			/* API -> UNI */
	  case UNIAPI_ALERTING_request:			/* API -> UNI */
	  case UNIAPI_PROCEEDING_request:		/* API -> UNI */
	  case UNIAPI_RELEASE_request:			/* API -> UNI */
	  case UNIAPI_RELEASE_response:			/* API -> UNI */
	  case UNIAPI_NOTIFY_request:			/* API -> UNI */
	  case UNIAPI_STATUS_ENQUIRY_request:		/* API -> UNI */
	  case UNIAPI_ADD_PARTY_request:		/* API -> UNI */
	  case UNIAPI_PARTY_ALERTING_request:		/* API -> UNI */
	  case UNIAPI_ADD_PARTY_ACK_request:		/* API -> UNI */
	  case UNIAPI_ADD_PARTY_REJ_request:		/* API -> UNI */
	  case UNIAPI_DROP_PARTY_request:		/* API -> UNI */
	  case UNIAPI_DROP_PARTY_ACK_request:		/* API -> UNI */
	  case UNIAPI_ABORT_CALL_request:		/* API -> UNI */
	  case UNIAPI_SETUP_COMPLETE_request:		/* API -> UNI */
	  case UNIAPI_MAXSIG:
		break;
	}
	cc_port_log(port, "bad signal %u", sig);
	error = EINVAL;
	goto out;

  bad_len:
	cc_port_log(port, "signal %u bad length: %zu, need %zu", len, ilen);
	error = EINVAL;
	goto out;

  unk_call:
	cc_port_log(port, "unknown call %u/%u", cref->cref, cref->flag);
	error = EINVAL;

  out:
	if (msg != NULL)
		uni_msg_destroy(msg);
	return (error);
}

