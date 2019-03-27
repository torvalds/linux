/*
* Copyright (c) 2004
*	Hartmut Brandt
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
* $Begemot: libunimsg/netnatm/api/cc_data.c,v 1.1 2004/07/08 08:21:50 brandt Exp $
*
* ATM API as defined per af-saa-0108
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
 * Create a new call control instance
 */
struct ccdata *
cc_create(const struct cc_funcs *vtab)
{
	struct ccdata *cc;

	cc = CCMALLOC(sizeof(*cc));
	if (cc == NULL)
		return (NULL);

	LIST_INIT(&cc->user_list);
	TAILQ_INIT(&cc->port_list);
	LIST_INIT(&cc->orphaned_conns);
	TAILQ_INIT(&cc->sigs);
	TAILQ_INIT(&cc->def_sigs);
	TAILQ_INIT(&cc->free_sigs);
	cc->funcs = vtab;
	cc->cookie = 0;

	return (cc);
}

/*
 * Reset everything the hard way by just freeing the data
 */
void
cc_reset(struct ccdata *cc)
{

	while (!LIST_EMPTY(&cc->user_list))
		cc_user_destroy(LIST_FIRST(&cc->user_list));

	while (!TAILQ_EMPTY(&cc->port_list))
		cc_port_destroy(TAILQ_FIRST(&cc->port_list), 1);

	while (!LIST_EMPTY(&cc->orphaned_conns))
		cc_conn_destroy(LIST_FIRST(&cc->orphaned_conns));

	CCASSERT(LIST_EMPTY(&cc->user_list),
	    ("user list not empty"));
	CCASSERT(LIST_EMPTY(&cc->orphaned_conns),
	    ("still orphaned conns"));

	cc_sig_flush_all(cc);

	cc->cookie = 0;
}

/*
 * Destroy a call control instance and free all data
 */
void
cc_destroy(struct ccdata *cc)
{

	cc_reset(cc);
	CCFREE(cc);
}

/*
 * set/get logging flags
 */
void
cc_set_log(struct ccdata *cc, u_int flags)
{
	cc->log = flags;
}
u_int
cc_get_log(const struct ccdata *cc)
{
	return (cc->log);
}

/* get extended status */
int
cc_get_extended_status(const struct ccdata *cc, struct atm_exstatus *status,
    struct atm_exstatus_ep **pep, struct atm_exstatus_port **pport,
    struct atm_exstatus_conn **pconn, struct atm_exstatus_party **pparty)
{
	const struct ccuser *user;
	const struct ccport *port;
	const struct ccconn *conn;
	const struct ccparty *party;
	struct atm_exstatus_ep *eep;
	struct atm_exstatus_port *eport;
	struct atm_exstatus_conn *econn;
	struct atm_exstatus_party *eparty;

	/* count and allocate */
	status->neps = 0;
	LIST_FOREACH(user, &cc->user_list, node_link)
		status->neps++;

	status->nports = 0;
	status->nconns = 0;
	status->nparties = 0;
	LIST_FOREACH(conn, &cc->orphaned_conns, port_link) {
		status->nconns++;
		LIST_FOREACH(party, &conn->parties, link)
			status->nparties++;
	}
	TAILQ_FOREACH(port, &cc->port_list, node_link) {
		status->nports++;
		LIST_FOREACH(conn, &port->conn_list, port_link) {
			status->nconns++;
			LIST_FOREACH(party, &conn->parties, link)
				status->nparties++;
		}
	}

	*pep = CCMALLOC(sizeof(**pep) * status->neps);
	*pport = CCMALLOC(sizeof(**pport) * status->nports);
	*pconn = CCMALLOC(sizeof(**pconn) * status->nconns);
	*pparty = CCMALLOC(sizeof(**pparty) * status->nparties);

	if (*pep == NULL || *pport == NULL ||
	    *pconn == NULL || *pparty == NULL) {
		CCFREE(*pep);
		CCFREE(*pport);
		CCFREE(*pconn);
		CCFREE(*pparty);
		return (ENOMEM);
	}

	eep = *pep;
	eport = *pport;
	econn = *pconn;
	eparty = *pparty;

	/* collect information */
	LIST_FOREACH(user, &cc->user_list, node_link) {
		strcpy(eep->name, user->name);
		eep->state = user->state;
		eep++;
	}

	LIST_FOREACH(conn, &cc->orphaned_conns, port_link) {
		econn->id = econn - *pconn;
		econn->port = 0;
		if (conn->user != NULL)
			strcpy(econn->ep, conn->user->name);
		else
			econn->ep[0] = '\0';
		econn->state = conn->state;
		econn->cref = conn->cref.cref;
		if (conn->cref.flag)
			econn->cref |= (1 << 23);
		LIST_FOREACH(party, &conn->parties, link) {
			eparty->connid = econn - *pconn;
			eparty->epref = party->epref.epref;
			eparty->state = party->state;
			eparty++;
		}
		econn++;
	}

	TAILQ_FOREACH(port, &cc->port_list, node_link) {
		eport->portno = port->param.port;
		eport->state = port->admin;
		LIST_FOREACH(conn, &port->conn_list, port_link) {
			econn->id = econn - *pconn;
			econn->port = port->param.port;
			if (conn->user != NULL)
				strcpy(econn->ep, conn->user->name);
			else
				econn->ep[0] = '\0';
			econn->state = conn->state;
			econn->cref = conn->cref.cref;
			if (conn->cref.flag)
				econn->cref |= (1 << 23);
			LIST_FOREACH(party, &conn->parties, link) {
				eparty->connid = econn - *pconn;
				eparty->epref = party->epref.epref;
				eparty->state = party->state;
				eparty++;
			}
			econn++;
		}
		eport++;
	}
	return (0);
}
