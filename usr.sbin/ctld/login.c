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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

#include "ctld.h"
#include "iscsi_proto.h"

static void login_send_error(struct pdu *request,
    char class, char detail);

static void
login_set_nsg(struct pdu *response, int nsg)
{
	struct iscsi_bhs_login_response *bhslr;

	assert(nsg == BHSLR_STAGE_SECURITY_NEGOTIATION ||
	    nsg == BHSLR_STAGE_OPERATIONAL_NEGOTIATION ||
	    nsg == BHSLR_STAGE_FULL_FEATURE_PHASE);

	bhslr = (struct iscsi_bhs_login_response *)response->pdu_bhs;

	bhslr->bhslr_flags &= 0xFC;
	bhslr->bhslr_flags |= nsg;
	bhslr->bhslr_flags |= BHSLR_FLAGS_TRANSIT;
}

static int
login_csg(const struct pdu *request)
{
	struct iscsi_bhs_login_request *bhslr;

	bhslr = (struct iscsi_bhs_login_request *)request->pdu_bhs;

	return ((bhslr->bhslr_flags & 0x0C) >> 2);
}

static void
login_set_csg(struct pdu *response, int csg)
{
	struct iscsi_bhs_login_response *bhslr;

	assert(csg == BHSLR_STAGE_SECURITY_NEGOTIATION ||
	    csg == BHSLR_STAGE_OPERATIONAL_NEGOTIATION ||
	    csg == BHSLR_STAGE_FULL_FEATURE_PHASE);

	bhslr = (struct iscsi_bhs_login_response *)response->pdu_bhs;

	bhslr->bhslr_flags &= 0xF3;
	bhslr->bhslr_flags |= csg << 2;
}

static struct pdu *
login_receive(struct connection *conn, bool initial)
{
	struct pdu *request;
	struct iscsi_bhs_login_request *bhslr;

	request = pdu_new(conn);
	pdu_receive(request);
	if ((request->pdu_bhs->bhs_opcode & ~ISCSI_BHS_OPCODE_IMMEDIATE) !=
	    ISCSI_BHS_OPCODE_LOGIN_REQUEST) {
		/*
		 * The first PDU in session is special - if we receive any PDU
		 * different than login request, we have to drop the connection
		 * without sending response ("A target receiving any PDU
		 * except a Login request before the Login Phase is started MUST
		 * immediately terminate the connection on which the PDU
		 * was received.")
		 */
		if (initial == false)
			login_send_error(request, 0x02, 0x0b);
		log_errx(1, "protocol error: received invalid opcode 0x%x",
		    request->pdu_bhs->bhs_opcode);
	}
	bhslr = (struct iscsi_bhs_login_request *)request->pdu_bhs;
	/*
	 * XXX: Implement the C flag some day.
	 */
	if ((bhslr->bhslr_flags & BHSLR_FLAGS_CONTINUE) != 0) {
		login_send_error(request, 0x03, 0x00);
		log_errx(1, "received Login PDU with unsupported \"C\" flag");
	}
	if (bhslr->bhslr_version_max != 0x00) {
		login_send_error(request, 0x02, 0x05);
		log_errx(1, "received Login PDU with unsupported "
		    "Version-max 0x%x", bhslr->bhslr_version_max);
	}
	if (bhslr->bhslr_version_min != 0x00) {
		login_send_error(request, 0x02, 0x05);
		log_errx(1, "received Login PDU with unsupported "
		    "Version-min 0x%x", bhslr->bhslr_version_min);
	}
	if (initial == false &&
	    ISCSI_SNLT(ntohl(bhslr->bhslr_cmdsn), conn->conn_cmdsn)) {
		login_send_error(request, 0x02, 0x00);
		log_errx(1, "received Login PDU with decreasing CmdSN: "
		    "was %u, is %u", conn->conn_cmdsn,
		    ntohl(bhslr->bhslr_cmdsn));
	}
	if (initial == false &&
	    ntohl(bhslr->bhslr_expstatsn) != conn->conn_statsn) {
		login_send_error(request, 0x02, 0x00);
		log_errx(1, "received Login PDU with wrong ExpStatSN: "
		    "is %u, should be %u", ntohl(bhslr->bhslr_expstatsn),
		    conn->conn_statsn);
	}
	conn->conn_cmdsn = ntohl(bhslr->bhslr_cmdsn);

	return (request);
}

static struct pdu *
login_new_response(struct pdu *request)
{
	struct pdu *response;
	struct connection *conn;
	struct iscsi_bhs_login_request *bhslr;
	struct iscsi_bhs_login_response *bhslr2;

	bhslr = (struct iscsi_bhs_login_request *)request->pdu_bhs;
	conn = request->pdu_connection;

	response = pdu_new_response(request);
	bhslr2 = (struct iscsi_bhs_login_response *)response->pdu_bhs;
	bhslr2->bhslr_opcode = ISCSI_BHS_OPCODE_LOGIN_RESPONSE;
	login_set_csg(response, BHSLR_STAGE_SECURITY_NEGOTIATION);
	memcpy(bhslr2->bhslr_isid,
	    bhslr->bhslr_isid, sizeof(bhslr2->bhslr_isid));
	bhslr2->bhslr_initiator_task_tag = bhslr->bhslr_initiator_task_tag;
	bhslr2->bhslr_statsn = htonl(conn->conn_statsn++);
	bhslr2->bhslr_expcmdsn = htonl(conn->conn_cmdsn);
	bhslr2->bhslr_maxcmdsn = htonl(conn->conn_cmdsn);

	return (response);
}

static void
login_send_error(struct pdu *request, char class, char detail)
{
	struct pdu *response;
	struct iscsi_bhs_login_response *bhslr2;

	log_debugx("sending Login Response PDU with failure class 0x%x/0x%x; "
	    "see next line for reason", class, detail);
	response = login_new_response(request);
	bhslr2 = (struct iscsi_bhs_login_response *)response->pdu_bhs;
	bhslr2->bhslr_status_class = class;
	bhslr2->bhslr_status_detail = detail;

	pdu_send(response);
	pdu_delete(response);
}

static int
login_list_contains(const char *list, const char *what)
{
	char *tofree, *str, *token;

	tofree = str = checked_strdup(list);

	while ((token = strsep(&str, ",")) != NULL) {
		if (strcmp(token, what) == 0) {
			free(tofree);
			return (1);
		}
	}
	free(tofree);
	return (0);
}

static int
login_list_prefers(const char *list,
    const char *choice1, const char *choice2)
{
	char *tofree, *str, *token;

	tofree = str = checked_strdup(list);

	while ((token = strsep(&str, ",")) != NULL) {
		if (strcmp(token, choice1) == 0) {
			free(tofree);
			return (1);
		}
		if (strcmp(token, choice2) == 0) {
			free(tofree);
			return (2);
		}
	}
	free(tofree);
	return (-1);
}

static struct pdu *
login_receive_chap_a(struct connection *conn)
{
	struct pdu *request;
	struct keys *request_keys;
	const char *chap_a;

	request = login_receive(conn, false);
	request_keys = keys_new();
	keys_load(request_keys, request);

	chap_a = keys_find(request_keys, "CHAP_A");
	if (chap_a == NULL) {
		login_send_error(request, 0x02, 0x07);
		log_errx(1, "received CHAP Login PDU without CHAP_A");
	}
	if (login_list_contains(chap_a, "5") == 0) {
		login_send_error(request, 0x02, 0x01);
		log_errx(1, "received CHAP Login PDU with unsupported CHAP_A "
		    "\"%s\"", chap_a);
	}
	keys_delete(request_keys);

	return (request);
}

static void
login_send_chap_c(struct pdu *request, struct chap *chap)
{
	struct pdu *response;
	struct keys *response_keys;
	char *chap_c, *chap_i;

	chap_c = chap_get_challenge(chap);
	chap_i = chap_get_id(chap);

	response = login_new_response(request);
	response_keys = keys_new();
	keys_add(response_keys, "CHAP_A", "5");
	keys_add(response_keys, "CHAP_I", chap_i);
	keys_add(response_keys, "CHAP_C", chap_c);
	free(chap_i);
	free(chap_c);
	keys_save(response_keys, response);
	pdu_send(response);
	pdu_delete(response);
	keys_delete(response_keys);
}

static struct pdu *
login_receive_chap_r(struct connection *conn, struct auth_group *ag,
    struct chap *chap, const struct auth **authp)
{
	struct pdu *request;
	struct keys *request_keys;
	const char *chap_n, *chap_r;
	const struct auth *auth;
	int error;

	request = login_receive(conn, false);
	request_keys = keys_new();
	keys_load(request_keys, request);

	chap_n = keys_find(request_keys, "CHAP_N");
	if (chap_n == NULL) {
		login_send_error(request, 0x02, 0x07);
		log_errx(1, "received CHAP Login PDU without CHAP_N");
	}
	chap_r = keys_find(request_keys, "CHAP_R");
	if (chap_r == NULL) {
		login_send_error(request, 0x02, 0x07);
		log_errx(1, "received CHAP Login PDU without CHAP_R");
	}
	error = chap_receive(chap, chap_r);
	if (error != 0) {
		login_send_error(request, 0x02, 0x07);
		log_errx(1, "received CHAP Login PDU with malformed CHAP_R");
	}

	/*
	 * Verify the response.
	 */
	assert(ag->ag_type == AG_TYPE_CHAP ||
	    ag->ag_type == AG_TYPE_CHAP_MUTUAL);
	auth = auth_find(ag, chap_n);
	if (auth == NULL) {
		login_send_error(request, 0x02, 0x01);
		log_errx(1, "received CHAP Login with invalid user \"%s\"",
		    chap_n);
	}

	assert(auth->a_secret != NULL);
	assert(strlen(auth->a_secret) > 0);

	error = chap_authenticate(chap, auth->a_secret);
	if (error != 0) {
		login_send_error(request, 0x02, 0x01);
		log_errx(1, "CHAP authentication failed for user \"%s\"",
		    auth->a_user);
	}

	keys_delete(request_keys);

	*authp = auth;
	return (request);
}

static void
login_send_chap_success(struct pdu *request,
    const struct auth *auth)
{
	struct pdu *response;
	struct keys *request_keys, *response_keys;
	struct rchap *rchap;
	const char *chap_i, *chap_c;
	char *chap_r;
	int error;

	response = login_new_response(request);
	login_set_nsg(response, BHSLR_STAGE_OPERATIONAL_NEGOTIATION);

	/*
	 * Actually, one more thing: mutual authentication.
	 */
	request_keys = keys_new();
	keys_load(request_keys, request);
	chap_i = keys_find(request_keys, "CHAP_I");
	chap_c = keys_find(request_keys, "CHAP_C");
	if (chap_i != NULL || chap_c != NULL) {
		if (chap_i == NULL) {
			login_send_error(request, 0x02, 0x07);
			log_errx(1, "initiator requested target "
			    "authentication, but didn't send CHAP_I");
		}
		if (chap_c == NULL) {
			login_send_error(request, 0x02, 0x07);
			log_errx(1, "initiator requested target "
			    "authentication, but didn't send CHAP_C");
		}
		if (auth->a_auth_group->ag_type != AG_TYPE_CHAP_MUTUAL) {
			login_send_error(request, 0x02, 0x01);
			log_errx(1, "initiator requests target authentication "
			    "for user \"%s\", but mutual user/secret "
			    "is not set", auth->a_user);
		}

		log_debugx("performing mutual authentication as user \"%s\"",
		    auth->a_mutual_user);

		rchap = rchap_new(auth->a_mutual_secret);
		error = rchap_receive(rchap, chap_i, chap_c);
		if (error != 0) {
			login_send_error(request, 0x02, 0x07);
			log_errx(1, "received CHAP Login PDU with malformed "
			    "CHAP_I or CHAP_C");
		}
		chap_r = rchap_get_response(rchap);
		rchap_delete(rchap);
		response_keys = keys_new();
		keys_add(response_keys, "CHAP_N", auth->a_mutual_user);
		keys_add(response_keys, "CHAP_R", chap_r);
		free(chap_r);
		keys_save(response_keys, response);
		keys_delete(response_keys);
	} else {
		log_debugx("initiator did not request target authentication");
	}

	keys_delete(request_keys);
	pdu_send(response);
	pdu_delete(response);
}

static void
login_chap(struct connection *conn, struct auth_group *ag)
{
	const struct auth *auth;
	struct chap *chap;
	struct pdu *request;

	/*
	 * Receive CHAP_A PDU.
	 */
	log_debugx("beginning CHAP authentication; waiting for CHAP_A");
	request = login_receive_chap_a(conn);

	/*
	 * Generate the challenge.
	 */
	chap = chap_new();

	/*
	 * Send the challenge.
	 */
	log_debugx("sending CHAP_C, binary challenge size is %zd bytes",
	    sizeof(chap->chap_challenge));
	login_send_chap_c(request, chap);
	pdu_delete(request);

	/*
	 * Receive CHAP_N/CHAP_R PDU and authenticate.
	 */
	log_debugx("waiting for CHAP_N/CHAP_R");
	request = login_receive_chap_r(conn, ag, chap, &auth);

	/*
	 * Yay, authentication succeeded!
	 */
	log_debugx("authentication succeeded for user \"%s\"; "
	    "transitioning to operational parameter negotiation", auth->a_user);
	login_send_chap_success(request, auth);
	pdu_delete(request);

	/*
	 * Leave username and CHAP information for discovery().
	 */
	conn->conn_user = auth->a_user;
	conn->conn_chap = chap;
}

static void
login_negotiate_key(struct pdu *request, const char *name,
    const char *value, bool skipped_security, struct keys *response_keys)
{
	int which;
	size_t tmp;
	struct connection *conn;

	conn = request->pdu_connection;

	if (strcmp(name, "InitiatorName") == 0) {
		if (!skipped_security)
			log_errx(1, "initiator resent InitiatorName");
	} else if (strcmp(name, "SessionType") == 0) {
		if (!skipped_security)
			log_errx(1, "initiator resent SessionType");
	} else if (strcmp(name, "TargetName") == 0) {
		if (!skipped_security)
			log_errx(1, "initiator resent TargetName");
	} else if (strcmp(name, "InitiatorAlias") == 0) {
		if (conn->conn_initiator_alias != NULL)
			free(conn->conn_initiator_alias);
		conn->conn_initiator_alias = checked_strdup(value);
	} else if (strcmp(value, "Irrelevant") == 0) {
		/* Ignore. */
	} else if (strcmp(name, "HeaderDigest") == 0) {
		/*
		 * We don't handle digests for discovery sessions.
		 */
		if (conn->conn_session_type == CONN_SESSION_TYPE_DISCOVERY) {
			log_debugx("discovery session; digests disabled");
			keys_add(response_keys, name, "None");
			return;
		}

		which = login_list_prefers(value, "CRC32C", "None");
		switch (which) {
		case 1:
			log_debugx("initiator prefers CRC32C "
			    "for header digest; we'll use it");
			conn->conn_header_digest = CONN_DIGEST_CRC32C;
			keys_add(response_keys, name, "CRC32C");
			break;
		case 2:
			log_debugx("initiator prefers not to do "
			    "header digest; we'll comply");
			keys_add(response_keys, name, "None");
			break;
		default:
			log_warnx("initiator sent unrecognized "
			    "HeaderDigest value \"%s\"; will use None", value);
			keys_add(response_keys, name, "None");
			break;
		}
	} else if (strcmp(name, "DataDigest") == 0) {
		if (conn->conn_session_type == CONN_SESSION_TYPE_DISCOVERY) {
			log_debugx("discovery session; digests disabled");
			keys_add(response_keys, name, "None");
			return;
		}

		which = login_list_prefers(value, "CRC32C", "None");
		switch (which) {
		case 1:
			log_debugx("initiator prefers CRC32C "
			    "for data digest; we'll use it");
			conn->conn_data_digest = CONN_DIGEST_CRC32C;
			keys_add(response_keys, name, "CRC32C");
			break;
		case 2:
			log_debugx("initiator prefers not to do "
			    "data digest; we'll comply");
			keys_add(response_keys, name, "None");
			break;
		default:
			log_warnx("initiator sent unrecognized "
			    "DataDigest value \"%s\"; will use None", value);
			keys_add(response_keys, name, "None");
			break;
		}
	} else if (strcmp(name, "MaxConnections") == 0) {
		keys_add(response_keys, name, "1");
	} else if (strcmp(name, "InitialR2T") == 0) {
		keys_add(response_keys, name, "Yes");
	} else if (strcmp(name, "ImmediateData") == 0) {
		if (conn->conn_session_type == CONN_SESSION_TYPE_DISCOVERY) {
			log_debugx("discovery session; ImmediateData irrelevant");
			keys_add(response_keys, name, "Irrelevant");
		} else {
			if (strcmp(value, "Yes") == 0) {
				conn->conn_immediate_data = true;
				keys_add(response_keys, name, "Yes");
			} else {
				conn->conn_immediate_data = false;
				keys_add(response_keys, name, "No");
			}
		}
	} else if (strcmp(name, "MaxRecvDataSegmentLength") == 0) {
		tmp = strtoul(value, NULL, 10);
		if (tmp <= 0) {
			login_send_error(request, 0x02, 0x00);
			log_errx(1, "received invalid "
			    "MaxRecvDataSegmentLength");
		}

		/*
		 * MaxRecvDataSegmentLength is a direction-specific parameter.
		 * We'll limit our _send_ to what the initiator can handle but
		 * our MaxRecvDataSegmentLength is not influenced by the
		 * initiator in any way.
		 */
		if ((int)tmp > conn->conn_max_send_data_segment_limit) {
			log_debugx("capping MaxRecvDataSegmentLength "
			    "from %zd to %d", tmp,
			    conn->conn_max_send_data_segment_limit);
			tmp = conn->conn_max_send_data_segment_limit;
		}
		conn->conn_max_send_data_segment_length = tmp;
		conn->conn_max_recv_data_segment_length =
		    conn->conn_max_recv_data_segment_limit;
		keys_add_int(response_keys, name,
		    conn->conn_max_recv_data_segment_length);
	} else if (strcmp(name, "MaxBurstLength") == 0) {
		tmp = strtoul(value, NULL, 10);
		if (tmp <= 0) {
			login_send_error(request, 0x02, 0x00);
			log_errx(1, "received invalid MaxBurstLength");
		}
		if ((int)tmp > conn->conn_max_burst_limit) {
			log_debugx("capping MaxBurstLength from %zd to %d",
			    tmp, conn->conn_max_burst_limit);
			tmp = conn->conn_max_burst_limit;
		}
		conn->conn_max_burst_length = tmp;
		keys_add_int(response_keys, name, tmp);
	} else if (strcmp(name, "FirstBurstLength") == 0) {
		tmp = strtoul(value, NULL, 10);
		if (tmp <= 0) {
			login_send_error(request, 0x02, 0x00);
			log_errx(1, "received invalid FirstBurstLength");
		}
		if ((int)tmp > conn->conn_first_burst_limit) {
			log_debugx("capping FirstBurstLength from %zd to %d",
			    tmp, conn->conn_first_burst_limit);
			tmp = conn->conn_first_burst_limit;
		}
		conn->conn_first_burst_length = tmp;
		keys_add_int(response_keys, name, tmp);
	} else if (strcmp(name, "DefaultTime2Wait") == 0) {
		keys_add(response_keys, name, value);
	} else if (strcmp(name, "DefaultTime2Retain") == 0) {
		keys_add(response_keys, name, "0");
	} else if (strcmp(name, "MaxOutstandingR2T") == 0) {
		keys_add(response_keys, name, "1");
	} else if (strcmp(name, "DataPDUInOrder") == 0) {
		keys_add(response_keys, name, "Yes");
	} else if (strcmp(name, "DataSequenceInOrder") == 0) {
		keys_add(response_keys, name, "Yes");
	} else if (strcmp(name, "ErrorRecoveryLevel") == 0) {
		keys_add(response_keys, name, "0");
	} else if (strcmp(name, "OFMarker") == 0) {
		keys_add(response_keys, name, "No");
	} else if (strcmp(name, "IFMarker") == 0) {
		keys_add(response_keys, name, "No");
	} else if (strcmp(name, "iSCSIProtocolLevel") == 0) {
		tmp = strtoul(value, NULL, 10);
		if (tmp > 2)
			tmp = 2;
		keys_add_int(response_keys, name, tmp);
	} else {
		log_debugx("unknown key \"%s\"; responding "
		    "with NotUnderstood", name);
		keys_add(response_keys, name, "NotUnderstood");
	}
}

static void
login_redirect(struct pdu *request, const char *target_address)
{
	struct pdu *response;
	struct iscsi_bhs_login_response *bhslr2;
	struct keys *response_keys;

	response = login_new_response(request);
	login_set_csg(response, login_csg(request));
	bhslr2 = (struct iscsi_bhs_login_response *)response->pdu_bhs;
	bhslr2->bhslr_status_class = 0x01;
	bhslr2->bhslr_status_detail = 0x01;

	response_keys = keys_new();
	keys_add(response_keys, "TargetAddress", target_address);

	keys_save(response_keys, response);
	pdu_send(response);
	pdu_delete(response);
	keys_delete(response_keys);
}

static bool
login_portal_redirect(struct connection *conn, struct pdu *request)
{
	const struct portal_group *pg;

	pg = conn->conn_portal->p_portal_group;
	if (pg->pg_redirection == NULL)
		return (false);

	log_debugx("portal-group \"%s\" configured to redirect to %s",
	    pg->pg_name, pg->pg_redirection);
	login_redirect(request, pg->pg_redirection);

	return (true);
}

static bool
login_target_redirect(struct connection *conn, struct pdu *request)
{
	const char *target_address;

	assert(conn->conn_portal->p_portal_group->pg_redirection == NULL);

	if (conn->conn_target == NULL)
		return (false);

	target_address = conn->conn_target->t_redirection;
	if (target_address == NULL)
		return (false);

	log_debugx("target \"%s\" configured to redirect to %s",
	  conn->conn_target->t_name, target_address);
	login_redirect(request, target_address);

	return (true);
}

static void
login_negotiate(struct connection *conn, struct pdu *request)
{
	struct pdu *response;
	struct iscsi_bhs_login_response *bhslr2;
	struct keys *request_keys, *response_keys;
	int i;
	bool redirected, skipped_security;

	if (conn->conn_session_type == CONN_SESSION_TYPE_NORMAL) {
		/*
		 * Query the kernel for various size limits.  In case of
		 * offload, it depends on hardware capabilities.
		 */
		assert(conn->conn_target != NULL);
		conn->conn_max_recv_data_segment_limit = (1 << 24) - 1;
		conn->conn_max_send_data_segment_limit = (1 << 24) - 1;
		conn->conn_max_burst_limit = (1 << 24) - 1;
		conn->conn_first_burst_limit = (1 << 24) - 1;
		kernel_limits(conn->conn_portal->p_portal_group->pg_offload,
		    &conn->conn_max_recv_data_segment_limit,
		    &conn->conn_max_send_data_segment_limit,
		    &conn->conn_max_burst_limit,
		    &conn->conn_first_burst_limit);

		/* We expect legal, usable values at this point. */
		assert(conn->conn_max_recv_data_segment_limit >= 512);
		assert(conn->conn_max_recv_data_segment_limit < (1 << 24));
		assert(conn->conn_max_send_data_segment_limit >= 512);
		assert(conn->conn_max_send_data_segment_limit < (1 << 24));
		assert(conn->conn_max_burst_limit >= 512);
		assert(conn->conn_max_burst_limit < (1 << 24));
		assert(conn->conn_first_burst_limit >= 512);
		assert(conn->conn_first_burst_limit < (1 << 24));
		assert(conn->conn_first_burst_limit <=
		    conn->conn_max_burst_limit);

		/*
		 * Limit default send length in case it won't be negotiated.
		 * We can't do it for other limits, since they may affect both
		 * sender and receiver operation, and we must obey defaults.
		 */
		if (conn->conn_max_send_data_segment_limit <
		    conn->conn_max_send_data_segment_length) {
			conn->conn_max_send_data_segment_length =
			    conn->conn_max_send_data_segment_limit;
		}
	} else {
		conn->conn_max_recv_data_segment_limit =
		    MAX_DATA_SEGMENT_LENGTH;
		conn->conn_max_send_data_segment_limit =
		    MAX_DATA_SEGMENT_LENGTH;
	}

	if (request == NULL) {
		log_debugx("beginning operational parameter negotiation; "
		    "waiting for Login PDU");
		request = login_receive(conn, false);
		skipped_security = false;
	} else
		skipped_security = true;

	/*
	 * RFC 3720, 10.13.5.  Status-Class and Status-Detail, says
	 * the redirection SHOULD be accepted by the initiator before
	 * authentication, but MUST be accepted afterwards; that's
	 * why we're doing it here and not earlier.
	 */
	redirected = login_target_redirect(conn, request);
	if (redirected) {
		log_debugx("initiator redirected; exiting");
		exit(0);
	}

	request_keys = keys_new();
	keys_load(request_keys, request);

	response = login_new_response(request);
	bhslr2 = (struct iscsi_bhs_login_response *)response->pdu_bhs;
	bhslr2->bhslr_tsih = htons(0xbadd);
	login_set_csg(response, BHSLR_STAGE_OPERATIONAL_NEGOTIATION);
	login_set_nsg(response, BHSLR_STAGE_FULL_FEATURE_PHASE);
	response_keys = keys_new();

	if (skipped_security &&
	    conn->conn_session_type == CONN_SESSION_TYPE_NORMAL) {
		if (conn->conn_target->t_alias != NULL)
			keys_add(response_keys,
			    "TargetAlias", conn->conn_target->t_alias);
		keys_add_int(response_keys, "TargetPortalGroupTag",
		    conn->conn_portal->p_portal_group->pg_tag);
	}

	for (i = 0; i < KEYS_MAX; i++) {
		if (request_keys->keys_names[i] == NULL)
			break;

		login_negotiate_key(request, request_keys->keys_names[i],
		    request_keys->keys_values[i], skipped_security,
		    response_keys);
	}

	/*
	 * We'd started with usable values at our end.  But a bad initiator
	 * could have presented a large FirstBurstLength and then a smaller
	 * MaxBurstLength (in that order) and because we process the key/value
	 * pairs in the order they are in the request we might have ended up
	 * with illegal values here.
	 */
	if (conn->conn_session_type == CONN_SESSION_TYPE_NORMAL &&
	    conn->conn_first_burst_length > conn->conn_max_burst_length) {
		log_errx(1, "initiator sent FirstBurstLength > MaxBurstLength");
	}

	log_debugx("operational parameter negotiation done; "
	    "transitioning to Full Feature Phase");

	keys_save(response_keys, response);
	pdu_send(response);
	pdu_delete(response);
	keys_delete(response_keys);
	pdu_delete(request);
	keys_delete(request_keys);
}

static void
login_wait_transition(struct connection *conn)
{
	struct pdu *request, *response;
	struct iscsi_bhs_login_request *bhslr;

	log_debugx("waiting for state transition request");
	request = login_receive(conn, false);
	bhslr = (struct iscsi_bhs_login_request *)request->pdu_bhs;
	if ((bhslr->bhslr_flags & BHSLR_FLAGS_TRANSIT) == 0) {
		login_send_error(request, 0x02, 0x00);
		log_errx(1, "got no \"T\" flag after answering AuthMethod");
	}

	log_debugx("got state transition request");
	response = login_new_response(request);
	pdu_delete(request);
	login_set_nsg(response, BHSLR_STAGE_OPERATIONAL_NEGOTIATION);
	pdu_send(response);
	pdu_delete(response);

	login_negotiate(conn, NULL);
}

void
login(struct connection *conn)
{
	struct pdu *request, *response;
	struct iscsi_bhs_login_request *bhslr;
	struct keys *request_keys, *response_keys;
	struct auth_group *ag;
	struct portal_group *pg;
	const char *initiator_name, *initiator_alias, *session_type,
	    *target_name, *auth_method;
	bool redirected, fail, trans;

	/*
	 * Handle the initial Login Request - figure out required authentication
	 * method and either transition to the next phase, if no authentication
	 * is required, or call appropriate authentication code.
	 */
	log_debugx("beginning Login Phase; waiting for Login PDU");
	request = login_receive(conn, true);
	bhslr = (struct iscsi_bhs_login_request *)request->pdu_bhs;
	if (bhslr->bhslr_tsih != 0) {
		login_send_error(request, 0x02, 0x0a);
		log_errx(1, "received Login PDU with non-zero TSIH");
	}

	pg = conn->conn_portal->p_portal_group;

	memcpy(conn->conn_initiator_isid, bhslr->bhslr_isid,
	    sizeof(conn->conn_initiator_isid));

	/*
	 * XXX: Implement the C flag some day.
	 */
	request_keys = keys_new();
	keys_load(request_keys, request);

	assert(conn->conn_initiator_name == NULL);
	initiator_name = keys_find(request_keys, "InitiatorName");
	if (initiator_name == NULL) {
		login_send_error(request, 0x02, 0x07);
		log_errx(1, "received Login PDU without InitiatorName");
	}
	if (valid_iscsi_name(initiator_name) == false) {
		login_send_error(request, 0x02, 0x00);
		log_errx(1, "received Login PDU with invalid InitiatorName");
	}
	conn->conn_initiator_name = checked_strdup(initiator_name);
	log_set_peer_name(conn->conn_initiator_name);
	setproctitle("%s (%s)", conn->conn_initiator_addr, conn->conn_initiator_name);

	redirected = login_portal_redirect(conn, request);
	if (redirected) {
		log_debugx("initiator redirected; exiting");
		exit(0);
	}

	initiator_alias = keys_find(request_keys, "InitiatorAlias");
	if (initiator_alias != NULL)
		conn->conn_initiator_alias = checked_strdup(initiator_alias);

	assert(conn->conn_session_type == CONN_SESSION_TYPE_NONE);
	session_type = keys_find(request_keys, "SessionType");
	if (session_type != NULL) {
		if (strcmp(session_type, "Normal") == 0) {
			conn->conn_session_type = CONN_SESSION_TYPE_NORMAL;
		} else if (strcmp(session_type, "Discovery") == 0) {
			conn->conn_session_type = CONN_SESSION_TYPE_DISCOVERY;
		} else {
			login_send_error(request, 0x02, 0x00);
			log_errx(1, "received Login PDU with invalid "
			    "SessionType \"%s\"", session_type);
		}
	} else
		conn->conn_session_type = CONN_SESSION_TYPE_NORMAL;

	assert(conn->conn_target == NULL);
	if (conn->conn_session_type == CONN_SESSION_TYPE_NORMAL) {
		target_name = keys_find(request_keys, "TargetName");
		if (target_name == NULL) {
			login_send_error(request, 0x02, 0x07);
			log_errx(1, "received Login PDU without TargetName");
		}

		conn->conn_port = port_find_in_pg(pg, target_name);
		if (conn->conn_port == NULL) {
			login_send_error(request, 0x02, 0x03);
			log_errx(1, "requested target \"%s\" not found",
			    target_name);
		}
		conn->conn_target = conn->conn_port->p_target;
	}

	/*
	 * At this point we know what kind of authentication we need.
	 */
	if (conn->conn_session_type == CONN_SESSION_TYPE_NORMAL) {
		ag = conn->conn_port->p_auth_group;
		if (ag == NULL)
			ag = conn->conn_target->t_auth_group;
		if (ag->ag_name != NULL) {
			log_debugx("initiator requests to connect "
			    "to target \"%s\"; auth-group \"%s\"",
			    conn->conn_target->t_name,
			    ag->ag_name);
		} else {
			log_debugx("initiator requests to connect "
			    "to target \"%s\"", conn->conn_target->t_name);
		}
	} else {
		assert(conn->conn_session_type == CONN_SESSION_TYPE_DISCOVERY);
		ag = pg->pg_discovery_auth_group;
		if (ag->ag_name != NULL) {
			log_debugx("initiator requests "
			    "discovery session; auth-group \"%s\"", ag->ag_name);
		} else {
			log_debugx("initiator requests discovery session");
		}
	}

	if (ag->ag_type == AG_TYPE_DENY) {
		login_send_error(request, 0x02, 0x01);
		log_errx(1, "auth-type is \"deny\"");
	}

	if (ag->ag_type == AG_TYPE_UNKNOWN) {
		/*
		 * This can happen with empty auth-group.
		 */
		login_send_error(request, 0x02, 0x01);
		log_errx(1, "auth-type not set, denying access");
	}

	/*
	 * Enforce initiator-name and initiator-portal.
	 */
	if (auth_name_check(ag, initiator_name) != 0) {
		login_send_error(request, 0x02, 0x02);
		log_errx(1, "initiator does not match allowed initiator names");
	}

	if (auth_portal_check(ag, &conn->conn_initiator_sa) != 0) {
		login_send_error(request, 0x02, 0x02);
		log_errx(1, "initiator does not match allowed "
		    "initiator portals");
	}

	/*
	 * Let's see if the initiator intends to do any kind of authentication
	 * at all.
	 */
	if (login_csg(request) == BHSLR_STAGE_OPERATIONAL_NEGOTIATION) {
		if (ag->ag_type != AG_TYPE_NO_AUTHENTICATION) {
			login_send_error(request, 0x02, 0x01);
			log_errx(1, "initiator skipped the authentication, "
			    "but authentication is required");
		}

		keys_delete(request_keys);

		log_debugx("initiator skipped the authentication, "
		    "and we don't need it; proceeding with negotiation");
		login_negotiate(conn, request);
		return;
	}

	fail = false;
	response = login_new_response(request);
	response_keys = keys_new();
	trans = (bhslr->bhslr_flags & BHSLR_FLAGS_TRANSIT) != 0;
	auth_method = keys_find(request_keys, "AuthMethod");
	if (ag->ag_type == AG_TYPE_NO_AUTHENTICATION) {
		log_debugx("authentication not required");
		if (auth_method == NULL ||
		    login_list_contains(auth_method, "None")) {
			keys_add(response_keys, "AuthMethod", "None");
		} else {
			log_warnx("initiator requests "
			    "AuthMethod \"%s\" instead of \"None\"",
			    auth_method);
			keys_add(response_keys, "AuthMethod", "Reject");
		}
		if (trans)
			login_set_nsg(response, BHSLR_STAGE_OPERATIONAL_NEGOTIATION);
	} else {
		log_debugx("CHAP authentication required");
		if (auth_method == NULL ||
		    login_list_contains(auth_method, "CHAP")) {
			keys_add(response_keys, "AuthMethod", "CHAP");
		} else {
			log_warnx("initiator requests unsupported "
			    "AuthMethod \"%s\" instead of \"CHAP\"",
			    auth_method);
			keys_add(response_keys, "AuthMethod", "Reject");
			fail = true;
		}
	}
	if (conn->conn_session_type == CONN_SESSION_TYPE_NORMAL) {
		if (conn->conn_target->t_alias != NULL)
			keys_add(response_keys,
			    "TargetAlias", conn->conn_target->t_alias);
		keys_add_int(response_keys,
		    "TargetPortalGroupTag", pg->pg_tag);
	}
	keys_save(response_keys, response);

	pdu_send(response);
	pdu_delete(response);
	keys_delete(response_keys);
	pdu_delete(request);
	keys_delete(request_keys);

	if (fail) {
		log_debugx("sent reject for AuthMethod; exiting");
		exit(1);
	}

	if (ag->ag_type != AG_TYPE_NO_AUTHENTICATION) {
		login_chap(conn, ag);
		login_negotiate(conn, NULL);
	} else if (trans) {
		login_negotiate(conn, NULL);
	} else {
		login_wait_transition(conn);
	}
}
