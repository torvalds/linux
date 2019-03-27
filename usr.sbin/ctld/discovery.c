/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>

#include "ctld.h"
#include "iscsi_proto.h"

static struct pdu *
text_receive(struct connection *conn)
{
	struct pdu *request;
	struct iscsi_bhs_text_request *bhstr;

	request = pdu_new(conn);
	pdu_receive(request);
	if ((request->pdu_bhs->bhs_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) !=
	    ISCSI_BHS_OPCODE_TEXT_REQUEST)
		log_errx(1, "protocol error: received invalid opcode 0x%x",
		    request->pdu_bhs->bhs_opcode);
	bhstr = (struct iscsi_bhs_text_request *)request->pdu_bhs;
#if 0
	if ((bhstr->bhstr_flags & ISCSI_BHSTR_FLAGS_FINAL) == 0)
		log_errx(1, "received Text PDU without the \"F\" flag");
#endif
	/*
	 * XXX: Implement the C flag some day.
	 */
	if ((bhstr->bhstr_flags & BHSTR_FLAGS_CONTINUE) != 0)
		log_errx(1, "received Text PDU with unsupported \"C\" flag");
	if (ISCSI_SNLT(ntohl(bhstr->bhstr_cmdsn), conn->conn_cmdsn)) {
		log_errx(1, "received Text PDU with decreasing CmdSN: "
		    "was %u, is %u", conn->conn_cmdsn, ntohl(bhstr->bhstr_cmdsn));
	}
	if (ntohl(bhstr->bhstr_expstatsn) != conn->conn_statsn) {
		log_errx(1, "received Text PDU with wrong ExpStatSN: "
		    "is %u, should be %u", ntohl(bhstr->bhstr_expstatsn),
		    conn->conn_statsn);
	}
	conn->conn_cmdsn = ntohl(bhstr->bhstr_cmdsn);
	if ((bhstr->bhstr_opcode & ISCSI_BHS_OPCODE_IMMEDIATE) == 0)
		conn->conn_cmdsn++;

	return (request);
}

static struct pdu *
text_new_response(struct pdu *request)
{
	struct pdu *response;
	struct connection *conn;
	struct iscsi_bhs_text_request *bhstr;
	struct iscsi_bhs_text_response *bhstr2;

	bhstr = (struct iscsi_bhs_text_request *)request->pdu_bhs;
	conn = request->pdu_connection;

	response = pdu_new_response(request);
	bhstr2 = (struct iscsi_bhs_text_response *)response->pdu_bhs;
	bhstr2->bhstr_opcode = ISCSI_BHS_OPCODE_TEXT_RESPONSE;
	bhstr2->bhstr_flags = BHSTR_FLAGS_FINAL;
	bhstr2->bhstr_lun = bhstr->bhstr_lun;
	bhstr2->bhstr_initiator_task_tag = bhstr->bhstr_initiator_task_tag;
	bhstr2->bhstr_target_transfer_tag = bhstr->bhstr_target_transfer_tag;
	bhstr2->bhstr_statsn = htonl(conn->conn_statsn++);
	bhstr2->bhstr_expcmdsn = htonl(conn->conn_cmdsn);
	bhstr2->bhstr_maxcmdsn = htonl(conn->conn_cmdsn);

	return (response);
}

static struct pdu *
logout_receive(struct connection *conn)
{
	struct pdu *request;
	struct iscsi_bhs_logout_request *bhslr;

	request = pdu_new(conn);
	pdu_receive(request);
	if ((request->pdu_bhs->bhs_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) !=
	    ISCSI_BHS_OPCODE_LOGOUT_REQUEST)
		log_errx(1, "protocol error: received invalid opcode 0x%x",
		    request->pdu_bhs->bhs_opcode);
	bhslr = (struct iscsi_bhs_logout_request *)request->pdu_bhs;
	if ((bhslr->bhslr_reason & 0x7f) != BHSLR_REASON_CLOSE_SESSION)
		log_debugx("received Logout PDU with invalid reason 0x%x; "
		    "continuing anyway", bhslr->bhslr_reason & 0x7f);
	if (ISCSI_SNLT(ntohl(bhslr->bhslr_cmdsn), conn->conn_cmdsn)) {
		log_errx(1, "received Logout PDU with decreasing CmdSN: "
		    "was %u, is %u", conn->conn_cmdsn,
		    ntohl(bhslr->bhslr_cmdsn));
	}
	if (ntohl(bhslr->bhslr_expstatsn) != conn->conn_statsn) {
		log_errx(1, "received Logout PDU with wrong ExpStatSN: "
		    "is %u, should be %u", ntohl(bhslr->bhslr_expstatsn),
		    conn->conn_statsn);
	}
	conn->conn_cmdsn = ntohl(bhslr->bhslr_cmdsn);
	if ((bhslr->bhslr_opcode & ISCSI_BHS_OPCODE_IMMEDIATE) == 0)
		conn->conn_cmdsn++;

	return (request);
}

static struct pdu *
logout_new_response(struct pdu *request)
{
	struct pdu *response;
	struct connection *conn;
	struct iscsi_bhs_logout_request *bhslr;
	struct iscsi_bhs_logout_response *bhslr2;

	bhslr = (struct iscsi_bhs_logout_request *)request->pdu_bhs;
	conn = request->pdu_connection;

	response = pdu_new_response(request);
	bhslr2 = (struct iscsi_bhs_logout_response *)response->pdu_bhs;
	bhslr2->bhslr_opcode = ISCSI_BHS_OPCODE_LOGOUT_RESPONSE;
	bhslr2->bhslr_flags = 0x80;
	bhslr2->bhslr_response = BHSLR_RESPONSE_CLOSED_SUCCESSFULLY;
	bhslr2->bhslr_initiator_task_tag = bhslr->bhslr_initiator_task_tag;
	bhslr2->bhslr_statsn = htonl(conn->conn_statsn++);
	bhslr2->bhslr_expcmdsn = htonl(conn->conn_cmdsn);
	bhslr2->bhslr_maxcmdsn = htonl(conn->conn_cmdsn);

	return (response);
}

static void
discovery_add_target(struct keys *response_keys, const struct target *targ)
{
	struct port *port;
	struct portal *portal;
	char *buf;
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
	struct addrinfo *ai;
	int ret;

	keys_add(response_keys, "TargetName", targ->t_name);
	TAILQ_FOREACH(port, &targ->t_ports, p_ts) {
	    if (port->p_portal_group == NULL)
		continue;
	    TAILQ_FOREACH(portal, &port->p_portal_group->pg_portals, p_next) {
		ai = portal->p_ai;
		ret = getnameinfo(ai->ai_addr, ai->ai_addrlen,
		    hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
		    NI_NUMERICHOST | NI_NUMERICSERV);
		if (ret != 0) {
			log_warnx("getnameinfo: %s", gai_strerror(ret));
			continue;
		}
		switch (ai->ai_addr->sa_family) {
		case AF_INET:
			if (strcmp(hbuf, "0.0.0.0") == 0)
				continue;
			ret = asprintf(&buf, "%s:%s,%d", hbuf, sbuf,
			    port->p_portal_group->pg_tag);
			break;
		case AF_INET6:
			if (strcmp(hbuf, "::") == 0)
				continue;
			ret = asprintf(&buf, "[%s]:%s,%d", hbuf, sbuf,
			    port->p_portal_group->pg_tag);
			break;
		default:
			continue;
		}
		if (ret <= 0)
		    log_err(1, "asprintf");
		keys_add(response_keys, "TargetAddress", buf);
		free(buf);
	    }
	}
}

static bool
discovery_target_filtered_out(const struct connection *conn,
    const struct port *port)
{
	const struct auth_group *ag;
	const struct portal_group *pg;
	const struct target *targ;
	const struct auth *auth;
	int error;

	targ = port->p_target;
	ag = port->p_auth_group;
	if (ag == NULL)
		ag = targ->t_auth_group;
	pg = conn->conn_portal->p_portal_group;

	assert(pg->pg_discovery_auth_group != PG_FILTER_UNKNOWN);

	if (pg->pg_discovery_filter >= PG_FILTER_PORTAL &&
	    auth_portal_check(ag, &conn->conn_initiator_sa) != 0) {
		log_debugx("initiator does not match initiator portals "
		    "allowed for target \"%s\"; skipping", targ->t_name);
		return (true);
	}

	if (pg->pg_discovery_filter >= PG_FILTER_PORTAL_NAME &&
	    auth_name_check(ag, conn->conn_initiator_name) != 0) {
		log_debugx("initiator does not match initiator names "
		    "allowed for target \"%s\"; skipping", targ->t_name);
		return (true);
	}

	if (pg->pg_discovery_filter >= PG_FILTER_PORTAL_NAME_AUTH &&
	    ag->ag_type != AG_TYPE_NO_AUTHENTICATION) {
		if (conn->conn_chap == NULL) {
			assert(pg->pg_discovery_auth_group->ag_type ==
			    AG_TYPE_NO_AUTHENTICATION);

			log_debugx("initiator didn't authenticate, but target "
			    "\"%s\" requires CHAP; skipping", targ->t_name);
			return (true);
		}

		assert(conn->conn_user != NULL);
		auth = auth_find(ag, conn->conn_user);
		if (auth == NULL) {
			log_debugx("CHAP user \"%s\" doesn't match target "
			    "\"%s\"; skipping", conn->conn_user, targ->t_name);
			return (true);
		}

		error = chap_authenticate(conn->conn_chap, auth->a_secret);
		if (error != 0) {
			log_debugx("password for CHAP user \"%s\" doesn't "
			    "match target \"%s\"; skipping",
			    conn->conn_user, targ->t_name);
			return (true);
		}
	}

	return (false);
}

void
discovery(struct connection *conn)
{
	struct pdu *request, *response;
	struct keys *request_keys, *response_keys;
	const struct port *port;
	const struct portal_group *pg;
	const char *send_targets;

	pg = conn->conn_portal->p_portal_group;

	log_debugx("beginning discovery session; waiting for Text PDU");
	request = text_receive(conn);
	request_keys = keys_new();
	keys_load(request_keys, request);

	send_targets = keys_find(request_keys, "SendTargets");
	if (send_targets == NULL)
		log_errx(1, "received Text PDU without SendTargets");

	response = text_new_response(request);
	response_keys = keys_new();

	if (strcmp(send_targets, "All") == 0) {
		TAILQ_FOREACH(port, &pg->pg_ports, p_pgs) {
			if (discovery_target_filtered_out(conn, port)) {
				/* Ignore this target. */
				continue;
			}
			discovery_add_target(response_keys, port->p_target);
		}
	} else {
		port = port_find_in_pg(pg, send_targets);
		if (port == NULL) {
			log_debugx("initiator requested information on unknown "
			    "target \"%s\"; returning nothing", send_targets);
		} else {
			if (discovery_target_filtered_out(conn, port)) {
				/* Ignore this target. */
			} else {
				discovery_add_target(response_keys, port->p_target);
			}
		}
	}
	keys_save(response_keys, response);

	pdu_send(response);
	pdu_delete(response);
	keys_delete(response_keys);
	pdu_delete(request);
	keys_delete(request_keys);

	log_debugx("done sending targets; waiting for Logout PDU");
	request = logout_receive(conn);
	response = logout_new_response(request);

	pdu_send(response);
	pdu_delete(response);
	pdu_delete(request);

	log_debugx("discovery session done");
}
