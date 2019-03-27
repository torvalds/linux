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

#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/linker.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libxo/xo.h>

#include <iscsi_ioctl.h>
#include "iscsictl.h"

struct conf *
conf_new(void)
{
	struct conf *conf;

	conf = calloc(1, sizeof(*conf));
	if (conf == NULL)
		xo_err(1, "calloc");

	TAILQ_INIT(&conf->conf_targets);

	return (conf);
}

struct target *
target_find(struct conf *conf, const char *nickname)
{
	struct target *targ;

	TAILQ_FOREACH(targ, &conf->conf_targets, t_next) {
		if (targ->t_nickname != NULL &&
		    strcasecmp(targ->t_nickname, nickname) == 0)
			return (targ);
	}

	return (NULL);
}

struct target *
target_new(struct conf *conf)
{
	struct target *targ;

	targ = calloc(1, sizeof(*targ));
	if (targ == NULL)
		xo_err(1, "calloc");
	targ->t_conf = conf;
	TAILQ_INSERT_TAIL(&conf->conf_targets, targ, t_next);

	return (targ);
}

void
target_delete(struct target *targ)
{

	TAILQ_REMOVE(&targ->t_conf->conf_targets, targ, t_next);
	free(targ);
}

static char *
default_initiator_name(void)
{
	char *name;
	size_t namelen;
	int error;

	namelen = _POSIX_HOST_NAME_MAX + strlen(DEFAULT_IQN);

	name = calloc(1, namelen + 1);
	if (name == NULL)
		xo_err(1, "calloc");
	strcpy(name, DEFAULT_IQN);
	error = gethostname(name + strlen(DEFAULT_IQN),
	    namelen - strlen(DEFAULT_IQN));
	if (error != 0)
		xo_err(1, "gethostname");

	return (name);
}

static bool
valid_hex(const char ch)
{
	switch (ch) {
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	case 'a':
	case 'A':
	case 'b':
	case 'B':
	case 'c':
	case 'C':
	case 'd':
	case 'D':
	case 'e':
	case 'E':
	case 'f':
	case 'F':
		return (true);
	default:
		return (false);
	}
}

int
parse_enable(const char *enable)
{
	if (enable == NULL)
		return (ENABLE_UNSPECIFIED);

	if (strcasecmp(enable, "on") == 0 ||
	    strcasecmp(enable, "yes") == 0)
		return (ENABLE_ON);

	if (strcasecmp(enable, "off") == 0 ||
	    strcasecmp(enable, "no") == 0)
		return (ENABLE_OFF);

	return (ENABLE_UNSPECIFIED);
}

bool
valid_iscsi_name(const char *name)
{
	int i;

	if (strlen(name) >= MAX_NAME_LEN) {
		xo_warnx("overlong name for \"%s\"; max length allowed "
		    "by iSCSI specification is %d characters",
		    name, MAX_NAME_LEN);
		return (false);
	}

	/*
	 * In the cases below, we don't return an error, just in case the admin
	 * was right, and we're wrong.
	 */
	if (strncasecmp(name, "iqn.", strlen("iqn.")) == 0) {
		for (i = strlen("iqn."); name[i] != '\0'; i++) {
			/*
			 * XXX: We should verify UTF-8 normalisation, as defined
			 *      by 3.2.6.2: iSCSI Name Encoding.
			 */
			if (isalnum(name[i]))
				continue;
			if (name[i] == '-' || name[i] == '.' || name[i] == ':')
				continue;
			xo_warnx("invalid character \"%c\" in iSCSI name "
			    "\"%s\"; allowed characters are letters, digits, "
			    "'-', '.', and ':'", name[i], name);
			break;
		}
		/*
		 * XXX: Check more stuff: valid date and a valid reversed domain.
		 */
	} else if (strncasecmp(name, "eui.", strlen("eui.")) == 0) {
		if (strlen(name) != strlen("eui.") + 16)
			xo_warnx("invalid iSCSI name \"%s\"; the \"eui.\" "
			    "should be followed by exactly 16 hexadecimal "
			    "digits", name);
		for (i = strlen("eui."); name[i] != '\0'; i++) {
			if (!valid_hex(name[i])) {
				xo_warnx("invalid character \"%c\" in iSCSI "
				    "name \"%s\"; allowed characters are 1-9 "
				    "and A-F", name[i], name);
				break;
			}
		}
	} else if (strncasecmp(name, "naa.", strlen("naa.")) == 0) {
		if (strlen(name) > strlen("naa.") + 32)
			xo_warnx("invalid iSCSI name \"%s\"; the \"naa.\" "
			    "should be followed by at most 32 hexadecimal "
			    "digits", name);
		for (i = strlen("naa."); name[i] != '\0'; i++) {
			if (!valid_hex(name[i])) {
				xo_warnx("invalid character \"%c\" in ISCSI "
				    "name \"%s\"; allowed characters are 1-9 "
				    "and A-F", name[i], name);
				break;
			}
		}
	} else {
		xo_warnx("invalid iSCSI name \"%s\"; should start with "
		    "either \".iqn\", \"eui.\", or \"naa.\"",
		    name);
	}
	return (true);
}

void
conf_verify(struct conf *conf)
{
	struct target *targ;

	TAILQ_FOREACH(targ, &conf->conf_targets, t_next) {
		assert(targ->t_nickname != NULL);
		if (targ->t_session_type == SESSION_TYPE_UNSPECIFIED)
			targ->t_session_type = SESSION_TYPE_NORMAL;
		if (targ->t_session_type == SESSION_TYPE_NORMAL &&
		    targ->t_name == NULL)
			xo_errx(1, "missing TargetName for target \"%s\"",
			    targ->t_nickname);
		if (targ->t_session_type == SESSION_TYPE_DISCOVERY &&
		    targ->t_name != NULL)
			xo_errx(1, "cannot specify TargetName for discovery "
			    "sessions for target \"%s\"", targ->t_nickname);
		if (targ->t_name != NULL) {
			if (valid_iscsi_name(targ->t_name) == false)
				xo_errx(1, "invalid target name \"%s\"",
				    targ->t_name);
		}
		if (targ->t_protocol == PROTOCOL_UNSPECIFIED)
			targ->t_protocol = PROTOCOL_ISCSI;
		if (targ->t_address == NULL)
			xo_errx(1, "missing TargetAddress for target \"%s\"",
			    targ->t_nickname);
		if (targ->t_initiator_name == NULL)
			targ->t_initiator_name = default_initiator_name();
		if (valid_iscsi_name(targ->t_initiator_name) == false)
			xo_errx(1, "invalid initiator name \"%s\"",
			    targ->t_initiator_name);
		if (targ->t_header_digest == DIGEST_UNSPECIFIED)
			targ->t_header_digest = DIGEST_NONE;
		if (targ->t_data_digest == DIGEST_UNSPECIFIED)
			targ->t_data_digest = DIGEST_NONE;
		if (targ->t_auth_method == AUTH_METHOD_UNSPECIFIED) {
			if (targ->t_user != NULL || targ->t_secret != NULL ||
			    targ->t_mutual_user != NULL ||
			    targ->t_mutual_secret != NULL)
				targ->t_auth_method =
				    AUTH_METHOD_CHAP;
			else
				targ->t_auth_method =
				    AUTH_METHOD_NONE;
		}
		if (targ->t_auth_method == AUTH_METHOD_CHAP) {
			if (targ->t_user == NULL) {
				xo_errx(1, "missing chapIName for target \"%s\"",
				    targ->t_nickname);
			}
			if (targ->t_secret == NULL)
				xo_errx(1, "missing chapSecret for target \"%s\"",
				    targ->t_nickname);
			if (targ->t_mutual_user != NULL ||
			    targ->t_mutual_secret != NULL) {
				if (targ->t_mutual_user == NULL)
					xo_errx(1, "missing tgtChapName for "
					    "target \"%s\"", targ->t_nickname);
				if (targ->t_mutual_secret == NULL)
					xo_errx(1, "missing tgtChapSecret for "
					    "target \"%s\"", targ->t_nickname);
			}
		}
	}
}

static void
conf_from_target(struct iscsi_session_conf *conf,
    const struct target *targ)
{
	memset(conf, 0, sizeof(*conf));

	/*
	 * XXX: Check bounds and return error instead of silently truncating.
	 */
	if (targ->t_initiator_name != NULL)
		strlcpy(conf->isc_initiator, targ->t_initiator_name,
		    sizeof(conf->isc_initiator));
	if (targ->t_initiator_address != NULL)
		strlcpy(conf->isc_initiator_addr, targ->t_initiator_address,
		    sizeof(conf->isc_initiator_addr));
	if (targ->t_initiator_alias != NULL)
		strlcpy(conf->isc_initiator_alias, targ->t_initiator_alias,
		    sizeof(conf->isc_initiator_alias));
	if (targ->t_name != NULL)
		strlcpy(conf->isc_target, targ->t_name,
		    sizeof(conf->isc_target));
	if (targ->t_address != NULL)
		strlcpy(conf->isc_target_addr, targ->t_address,
		    sizeof(conf->isc_target_addr));
	if (targ->t_user != NULL)
		strlcpy(conf->isc_user, targ->t_user,
		    sizeof(conf->isc_user));
	if (targ->t_secret != NULL)
		strlcpy(conf->isc_secret, targ->t_secret,
		    sizeof(conf->isc_secret));
	if (targ->t_mutual_user != NULL)
		strlcpy(conf->isc_mutual_user, targ->t_mutual_user,
		    sizeof(conf->isc_mutual_user));
	if (targ->t_mutual_secret != NULL)
		strlcpy(conf->isc_mutual_secret, targ->t_mutual_secret,
		    sizeof(conf->isc_mutual_secret));
	if (targ->t_session_type == SESSION_TYPE_DISCOVERY)
		conf->isc_discovery = 1;
	if (targ->t_enable != ENABLE_OFF)
		conf->isc_enable = 1;
	if (targ->t_protocol == PROTOCOL_ISER)
		conf->isc_iser = 1;
	if (targ->t_offload != NULL)
		strlcpy(conf->isc_offload, targ->t_offload,
		    sizeof(conf->isc_offload));
	if (targ->t_header_digest == DIGEST_CRC32C)
		conf->isc_header_digest = ISCSI_DIGEST_CRC32C;
	else
		conf->isc_header_digest = ISCSI_DIGEST_NONE;
	if (targ->t_data_digest == DIGEST_CRC32C)
		conf->isc_data_digest = ISCSI_DIGEST_CRC32C;
	else
		conf->isc_data_digest = ISCSI_DIGEST_NONE;
}

static int
kernel_add(int iscsi_fd, const struct target *targ)
{
	struct iscsi_session_add isa;
	int error;

	memset(&isa, 0, sizeof(isa));
	conf_from_target(&isa.isa_conf, targ);
	error = ioctl(iscsi_fd, ISCSISADD, &isa);
	if (error != 0)
		xo_warn("ISCSISADD");
	return (error);
}

static int
kernel_modify(int iscsi_fd, unsigned int session_id, const struct target *targ)
{
	struct iscsi_session_modify ism;
	int error;

	memset(&ism, 0, sizeof(ism));
	ism.ism_session_id = session_id;
	conf_from_target(&ism.ism_conf, targ);
	error = ioctl(iscsi_fd, ISCSISMODIFY, &ism);
	if (error != 0)
		xo_warn("ISCSISMODIFY");
	return (error);
}

static void
kernel_modify_some(int iscsi_fd, unsigned int session_id, const char *target,
  const char *target_addr, const char *user, const char *secret, int enable)
{
	struct iscsi_session_state *states = NULL;
	struct iscsi_session_state *state;
	struct iscsi_session_conf *conf;
	struct iscsi_session_list isl;
	struct iscsi_session_modify ism;
	unsigned int i, nentries = 1;
	int error;

	for (;;) {
		states = realloc(states,
		    nentries * sizeof(struct iscsi_session_state));
		if (states == NULL)
			xo_err(1, "realloc");

		memset(&isl, 0, sizeof(isl));
		isl.isl_nentries = nentries;
		isl.isl_pstates = states;

		error = ioctl(iscsi_fd, ISCSISLIST, &isl);
		if (error != 0 && errno == EMSGSIZE) {
			nentries *= 4;
			continue;
		}
		break;
	}
	if (error != 0)
		xo_errx(1, "ISCSISLIST");

	for (i = 0; i < isl.isl_nentries; i++) {
		state = &states[i];

		if (state->iss_id == session_id)
			break;
	}
	if (i == isl.isl_nentries)
		xo_errx(1, "session-id %u not found", session_id);

	conf = &state->iss_conf;

	if (target != NULL)
		strlcpy(conf->isc_target, target, sizeof(conf->isc_target));
	if (target_addr != NULL)
		strlcpy(conf->isc_target_addr, target_addr,
		    sizeof(conf->isc_target_addr));
	if (user != NULL)
		strlcpy(conf->isc_user, user, sizeof(conf->isc_user));
	if (secret != NULL)
		strlcpy(conf->isc_secret, secret, sizeof(conf->isc_secret));
	if (enable == ENABLE_ON)
		conf->isc_enable = 1;
	else if (enable == ENABLE_OFF)
		conf->isc_enable = 0;

	memset(&ism, 0, sizeof(ism));
	ism.ism_session_id = session_id;
	memcpy(&ism.ism_conf, conf, sizeof(ism.ism_conf));
	error = ioctl(iscsi_fd, ISCSISMODIFY, &ism);
	if (error != 0)
		xo_warn("ISCSISMODIFY");
}

static int
kernel_remove(int iscsi_fd, const struct target *targ)
{
	struct iscsi_session_remove isr;
	int error;

	memset(&isr, 0, sizeof(isr));
	conf_from_target(&isr.isr_conf, targ);
	error = ioctl(iscsi_fd, ISCSISREMOVE, &isr);
	if (error != 0)
		xo_warn("ISCSISREMOVE");
	return (error);
}

/*
 * XXX: Add filtering.
 */
static int
kernel_list(int iscsi_fd, const struct target *targ __unused,
    int verbose)
{
	struct iscsi_session_state *states = NULL;
	const struct iscsi_session_state *state;
	const struct iscsi_session_conf *conf;
	struct iscsi_session_list isl;
	unsigned int i, nentries = 1;
	int error;

	for (;;) {
		states = realloc(states,
		    nentries * sizeof(struct iscsi_session_state));
		if (states == NULL)
			xo_err(1, "realloc");

		memset(&isl, 0, sizeof(isl));
		isl.isl_nentries = nentries;
		isl.isl_pstates = states;

		error = ioctl(iscsi_fd, ISCSISLIST, &isl);
		if (error != 0 && errno == EMSGSIZE) {
			nentries *= 4;
			continue;
		}
		break;
	}
	if (error != 0) {
		xo_warn("ISCSISLIST");
		return (error);
	}

	if (verbose != 0) {
		xo_open_list("session");
		for (i = 0; i < isl.isl_nentries; i++) {
			state = &states[i];
			conf = &state->iss_conf;

			xo_open_instance("session");

			/*
			 * Display-only modifier as this information
			 * is also present within the 'session' container
			 */
			xo_emit("{L:/%-26s}{V:sessionId/%u}\n",
			    "Session ID:", state->iss_id);

			xo_open_container("initiator");
			xo_emit("{L:/%-26s}{V:name/%s}\n",
			    "Initiator name:", conf->isc_initiator);
			xo_emit("{L:/%-26s}{V:portal/%s}\n",
			    "Initiator portal:", conf->isc_initiator_addr);
			xo_emit("{L:/%-26s}{V:alias/%s}\n",
			    "Initiator alias:", conf->isc_initiator_alias);
			xo_close_container("initiator");

			xo_open_container("target");
			xo_emit("{L:/%-26s}{V:name/%s}\n",
			    "Target name:", conf->isc_target);
			xo_emit("{L:/%-26s}{V:portal/%s}\n",
			    "Target portal:", conf->isc_target_addr);
			xo_emit("{L:/%-26s}{V:alias/%s}\n",
			    "Target alias:", state->iss_target_alias);
			xo_close_container("target");

			xo_open_container("auth");
			xo_emit("{L:/%-26s}{V:user/%s}\n",
			    "User:", conf->isc_user);
			xo_emit("{L:/%-26s}{V:secret/%s}\n",
			    "Secret:", conf->isc_secret);
			xo_emit("{L:/%-26s}{V:mutualUser/%s}\n",
			    "Mutual user:", conf->isc_mutual_user);
			xo_emit("{L:/%-26s}{V:mutualSecret/%s}\n",
			    "Mutual secret:", conf->isc_mutual_secret);
			xo_close_container("auth");

			xo_emit("{L:/%-26s}{V:type/%s}\n",
			    "Session type:",
			    conf->isc_discovery ? "Discovery" : "Normal");
			xo_emit("{L:/%-26s}{V:enable/%s}\n",
			    "Enable:",
			    conf->isc_enable ? "Yes" : "No");
			xo_emit("{L:/%-26s}{V:state/%s}\n",
			    "Session state:",
			    state->iss_connected ? "Connected" : "Disconnected");
			xo_emit("{L:/%-26s}{V:failureReason/%s}\n",
			    "Failure reason:", state->iss_reason);
			xo_emit("{L:/%-26s}{V:headerDigest/%s}\n",
			    "Header digest:",
			    state->iss_header_digest == ISCSI_DIGEST_CRC32C ?
			    "CRC32C" : "None");
			xo_emit("{L:/%-26s}{V:dataDigest/%s}\n",
			    "Data digest:",
			    state->iss_data_digest == ISCSI_DIGEST_CRC32C ?
			    "CRC32C" : "None");
			xo_emit("{L:/%-26s}{V:recvDataSegmentLen/%d}\n",
			    "MaxRecvDataSegmentLength:",
			    state->iss_max_recv_data_segment_length);
			xo_emit("{L:/%-26s}{V:sendDataSegmentLen/%d}\n",
			    "MaxSendDataSegmentLength:",
			    state->iss_max_send_data_segment_length);
			xo_emit("{L:/%-26s}{V:maxBurstLen/%d}\n",
			    "MaxBurstLen:", state->iss_max_burst_length);
			xo_emit("{L:/%-26s}{V:firstBurstLen/%d}\n",
			    "FirstBurstLen:", state->iss_first_burst_length);
			xo_emit("{L:/%-26s}{V:immediateData/%s}\n",
			    "ImmediateData:", state->iss_immediate_data ? "Yes" : "No");
			xo_emit("{L:/%-26s}{V:iSER/%s}\n",
			    "iSER (RDMA):", conf->isc_iser ? "Yes" : "No");
			xo_emit("{L:/%-26s}{V:offloadDriver/%s}\n",
			    "Offload driver:", state->iss_offload);
			xo_emit("{L:/%-26s}",
			    "Device nodes:");
			print_periphs(state->iss_id);
			xo_emit("\n\n");
			xo_close_instance("session");
		}
		xo_close_list("session");
	} else {
		xo_emit("{T:/%-36s} {T:/%-16s} {T:/%s}\n",
		    "Target name", "Target portal", "State");

		if (isl.isl_nentries != 0)
			xo_open_list("session");
		for (i = 0; i < isl.isl_nentries; i++) {

			state = &states[i];
			conf = &state->iss_conf;

			xo_open_instance("session");
			xo_emit("{V:name/%-36s/%s} {V:portal/%-16s/%s} ",
			    conf->isc_target, conf->isc_target_addr);

			if (state->iss_reason[0] != '\0' &&
			    conf->isc_enable != 0) {
				xo_emit("{V:state/%s}\n", state->iss_reason);
			} else {
				if (conf->isc_discovery) {
					xo_emit("{V:state}\n", "Discovery");
				} else if (conf->isc_enable == 0) {
					xo_emit("{V:state}\n", "Disabled");
				} else if (state->iss_connected) {
					xo_emit("{V:state}: ", "Connected");
					print_periphs(state->iss_id);
					xo_emit("\n");
				} else {
					xo_emit("{V:state}\n", "Disconnected");
				}
			}
			xo_close_instance("session");
		}
		if (isl.isl_nentries != 0)
			xo_close_list("session");
	}

	return (0);
}

static int
kernel_wait(int iscsi_fd, int timeout)
{
	struct iscsi_session_state *states = NULL;
	const struct iscsi_session_state *state;
	struct iscsi_session_list isl;
	unsigned int i, nentries = 1;
	bool all_connected;
	int error;

	for (;;) {
		for (;;) {
			states = realloc(states,
			    nentries * sizeof(struct iscsi_session_state));
			if (states == NULL)
				xo_err(1, "realloc");

			memset(&isl, 0, sizeof(isl));
			isl.isl_nentries = nentries;
			isl.isl_pstates = states;

			error = ioctl(iscsi_fd, ISCSISLIST, &isl);
			if (error != 0 && errno == EMSGSIZE) {
				nentries *= 4;
				continue;
			}
			break;
		}
		if (error != 0) {
			xo_warn("ISCSISLIST");
			return (error);
		}

		all_connected = true;
		for (i = 0; i < isl.isl_nentries; i++) {
			state = &states[i];

			if (!state->iss_connected) {
				all_connected = false;
				break;
			}
		}

		if (all_connected)
			return (0);

		sleep(1);

		if (timeout > 0) {
			timeout--;
			if (timeout == 0)
				return (1);
		}
	}
}

static void
usage(void)
{

	fprintf(stderr, "usage: iscsictl -A -p portal -t target "
	    "[-u user -s secret] [-w timeout] [-e on | off]\n");
	fprintf(stderr, "       iscsictl -A -d discovery-host "
	    "[-u user -s secret] [-e on | off]\n");
	fprintf(stderr, "       iscsictl -A -a [-c path]\n");
	fprintf(stderr, "       iscsictl -A -n nickname [-c path]\n");
	fprintf(stderr, "       iscsictl -M -i session-id [-p portal] "
	    "[-t target] [-u user] [-s secret] [-e on | off]\n");
	fprintf(stderr, "       iscsictl -M -i session-id -n nickname "
	    "[-c path]\n");
	fprintf(stderr, "       iscsictl -R [-p portal] [-t target]\n");
	fprintf(stderr, "       iscsictl -R -a\n");
	fprintf(stderr, "       iscsictl -R -n nickname [-c path]\n");
	fprintf(stderr, "       iscsictl -L [-v] [-w timeout]\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	int Aflag = 0, Mflag = 0, Rflag = 0, Lflag = 0, aflag = 0,
	    rflag = 0, vflag = 0;
	const char *conf_path = DEFAULT_CONFIG_PATH;
	char *nickname = NULL, *discovery_host = NULL, *portal = NULL,
	    *target = NULL, *user = NULL, *secret = NULL;
	int timeout = -1, enable = ENABLE_UNSPECIFIED;
	long long session_id = -1;
	char *end;
	int ch, error, iscsi_fd, retval, saved_errno;
	int failed = 0;
	struct conf *conf;
	struct target *targ;

	argc = xo_parse_args(argc, argv);
	xo_open_container("iscsictl");

	while ((ch = getopt(argc, argv, "AMRLac:d:e:i:n:p:rt:u:s:vw:")) != -1) {
		switch (ch) {
		case 'A':
			Aflag = 1;
			break;
		case 'M':
			Mflag = 1;
			break;
		case 'R':
			Rflag = 1;
			break;
		case 'L':
			Lflag = 1;
			break;
		case 'a':
			aflag = 1;
			break;
		case 'c':
			conf_path = optarg;
			break;
		case 'd':
			discovery_host = optarg;
			break;
		case 'e':
			enable = parse_enable(optarg);
			if (enable == ENABLE_UNSPECIFIED) {
				xo_errx(1, "invalid argument to -e, "
				    "must be either \"on\" or \"off\"");
			}
			break;
		case 'i':
			session_id = strtol(optarg, &end, 10);
			if ((size_t)(end - optarg) != strlen(optarg))
				xo_errx(1, "trailing characters after session-id");
			if (session_id < 0)
				xo_errx(1, "session-id cannot be negative");
			if (session_id > UINT_MAX)
				xo_errx(1, "session-id cannot be greater than %u",
				    UINT_MAX);
			break;
		case 'n':
			nickname = optarg;
			break;
		case 'p':
			portal = optarg;
			break;
		case 'r':
			rflag = 1;
			break;
		case 't':
			target = optarg;
			break;
		case 'u':
			user = optarg;
			break;
		case 's':
			secret = optarg;
			break;
		case 'v':
			vflag = 1;
			break;
		case 'w':
			timeout = strtol(optarg, &end, 10);
			if ((size_t)(end - optarg) != strlen(optarg))
				xo_errx(1, "trailing characters after timeout");
			if (timeout < 0)
				xo_errx(1, "timeout cannot be negative");
			break;
		case '?':
		default:
			usage();
		}
	}
	argc -= optind;
	if (argc != 0)
		usage();

	if (Aflag + Mflag + Rflag + Lflag == 0)
		Lflag = 1;
	if (Aflag + Mflag + Rflag + Lflag > 1)
		xo_errx(1, "at most one of -A, -M, -R, or -L may be specified");

	/*
	 * Note that we ignore unnecessary/inapplicable "-c" flag; so that
	 * people can do something like "alias ISCSICTL="iscsictl -c path"
	 * in shell scripts.
	 */
	if (Aflag != 0) {
		if (aflag != 0) {
			if (enable != ENABLE_UNSPECIFIED)
				xo_errx(1, "-a and -e are mutually exclusive");
			if (portal != NULL)
				xo_errx(1, "-a and -p are mutually exclusive");
			if (target != NULL)
				xo_errx(1, "-a and -t are mutually exclusive");
			if (user != NULL)
				xo_errx(1, "-a and -u are mutually exclusive");
			if (secret != NULL)
				xo_errx(1, "-a and -s are mutually exclusive");
			if (nickname != NULL)
				xo_errx(1, "-a and -n are mutually exclusive");
			if (discovery_host != NULL)
				xo_errx(1, "-a and -d are mutually exclusive");
			if (rflag != 0)
				xo_errx(1, "-a and -r are mutually exclusive");
		} else if (nickname != NULL) {
			if (enable != ENABLE_UNSPECIFIED)
				xo_errx(1, "-n and -e are mutually exclusive");
			if (portal != NULL)
				xo_errx(1, "-n and -p are mutually exclusive");
			if (target != NULL)
				xo_errx(1, "-n and -t are mutually exclusive");
			if (user != NULL)
				xo_errx(1, "-n and -u are mutually exclusive");
			if (secret != NULL)
				xo_errx(1, "-n and -s are mutually exclusive");
			if (discovery_host != NULL)
				xo_errx(1, "-n and -d are mutually exclusive");
			if (rflag != 0)
				xo_errx(1, "-n and -r are mutually exclusive");
		} else if (discovery_host != NULL) {
			if (portal != NULL)
				xo_errx(1, "-d and -p are mutually exclusive");
			if (target != NULL)
				xo_errx(1, "-d and -t are mutually exclusive");
		} else {
			if (target == NULL && portal == NULL)
				xo_errx(1, "must specify -a, -n or -t/-p");

			if (target != NULL && portal == NULL)
				xo_errx(1, "-t must always be used with -p");
			if (portal != NULL && target == NULL)
				xo_errx(1, "-p must always be used with -t");
		}

		if (user != NULL && secret == NULL)
			xo_errx(1, "-u must always be used with -s");
		if (secret != NULL && user == NULL)
			xo_errx(1, "-s must always be used with -u");

		if (session_id != -1)
			xo_errx(1, "-i cannot be used with -A");
		if (vflag != 0)
			xo_errx(1, "-v cannot be used with -A");

	} else if (Mflag != 0) {
		if (session_id == -1)
			xo_errx(1, "-M requires -i");

		if (nickname != NULL) {
			if (enable != ENABLE_UNSPECIFIED)
				xo_errx(1, "-n and -e are mutually exclusive");
			if (portal != NULL)
				xo_errx(1, "-n and -p are mutually exclusive");
			if (target != NULL)
				xo_errx(1, "-n and -t are mutually exclusive");
			if (user != NULL)
				xo_errx(1, "-n and -u are mutually exclusive");
			if (secret != NULL)
				xo_errx(1, "-n and -s are mutually exclusive");
		}

		if (aflag != 0)
			xo_errx(1, "-a cannot be used with -M");
		if (discovery_host != NULL)
			xo_errx(1, "-d cannot be used with -M");
		if (rflag != 0)
			xo_errx(1, "-r cannot be used with -M");
		if (vflag != 0)
			xo_errx(1, "-v cannot be used with -M");
		if (timeout != -1)
			xo_errx(1, "-w cannot be used with -M");

	} else if (Rflag != 0) {
		if (aflag != 0) {
			if (portal != NULL)
				xo_errx(1, "-a and -p are mutually exclusive");
			if (target != NULL)
				xo_errx(1, "-a and -t are mutually exclusive");
			if (nickname != NULL)
				xo_errx(1, "-a and -n are mutually exclusive");
		} else if (nickname != NULL) {
			if (portal != NULL)
				xo_errx(1, "-n and -p are mutually exclusive");
			if (target != NULL)
				xo_errx(1, "-n and -t are mutually exclusive");
		} else if (target == NULL && portal == NULL) {
			xo_errx(1, "must specify either -a, -n, -t, or -p");
		}

		if (discovery_host != NULL)
			xo_errx(1, "-d cannot be used with -R");
		if (enable != ENABLE_UNSPECIFIED)
			xo_errx(1, "-e cannot be used with -R");
		if (session_id != -1)
			xo_errx(1, "-i cannot be used with -R");
		if (rflag != 0)
			xo_errx(1, "-r cannot be used with -R");
		if (user != NULL)
			xo_errx(1, "-u cannot be used with -R");
		if (secret != NULL)
			xo_errx(1, "-s cannot be used with -R");
		if (vflag != 0)
			xo_errx(1, "-v cannot be used with -R");
		if (timeout != -1)
			xo_errx(1, "-w cannot be used with -R");

	} else {
		assert(Lflag != 0);

		if (discovery_host != NULL)
			xo_errx(1, "-d cannot be used with -L");
		if (session_id != -1)
			xo_errx(1, "-i cannot be used with -L");
		if (nickname != NULL)
			xo_errx(1, "-n cannot be used with -L");
		if (portal != NULL)
			xo_errx(1, "-p cannot be used with -L");
		if (rflag != 0)
			xo_errx(1, "-r cannot be used with -L");
		if (target != NULL)
			xo_errx(1, "-t cannot be used with -L");
		if (user != NULL)
			xo_errx(1, "-u cannot be used with -L");
		if (secret != NULL)
			xo_errx(1, "-s cannot be used with -L");
	}

	iscsi_fd = open(ISCSI_PATH, O_RDWR);
	if (iscsi_fd < 0 && errno == ENOENT) {
		saved_errno = errno;
		retval = kldload("iscsi");
		if (retval != -1)
			iscsi_fd = open(ISCSI_PATH, O_RDWR);
		else
			errno = saved_errno;
	}
	if (iscsi_fd < 0)
		xo_err(1, "failed to open %s", ISCSI_PATH);

	if (Aflag != 0 && aflag != 0) {
		conf = conf_new_from_file(conf_path);

		TAILQ_FOREACH(targ, &conf->conf_targets, t_next)
			failed += kernel_add(iscsi_fd, targ);
	} else if (nickname != NULL) {
		conf = conf_new_from_file(conf_path);
		targ = target_find(conf, nickname);
		if (targ == NULL)
			xo_errx(1, "target %s not found in %s",
			    nickname, conf_path);

		if (Aflag != 0)
			failed += kernel_add(iscsi_fd, targ);
		else if (Mflag != 0)
			failed += kernel_modify(iscsi_fd, session_id, targ);
		else if (Rflag != 0)
			failed += kernel_remove(iscsi_fd, targ);
		else
			failed += kernel_list(iscsi_fd, targ, vflag);
	} else if (Mflag != 0) {
		kernel_modify_some(iscsi_fd, session_id, target, portal,
		    user, secret, enable);
	} else {
		if (Aflag != 0 && target != NULL) {
			if (valid_iscsi_name(target) == false)
				xo_errx(1, "invalid target name \"%s\"", target);
		}
		conf = conf_new();
		targ = target_new(conf);
		targ->t_initiator_name = default_initiator_name();
		targ->t_header_digest = DIGEST_NONE;
		targ->t_data_digest = DIGEST_NONE;
		targ->t_name = target;
		if (discovery_host != NULL) {
			targ->t_session_type = SESSION_TYPE_DISCOVERY;
			targ->t_address = discovery_host;
		} else {
			targ->t_session_type = SESSION_TYPE_NORMAL;
			targ->t_address = portal;
		}
		targ->t_enable = enable;
		if (rflag != 0)
			targ->t_protocol = PROTOCOL_ISER;
		targ->t_user = user;
		targ->t_secret = secret;

		if (Aflag != 0)
			failed += kernel_add(iscsi_fd, targ);
		else if (Rflag != 0)
			failed += kernel_remove(iscsi_fd, targ);
		else
			failed += kernel_list(iscsi_fd, targ, vflag);
	}

	if (timeout != -1)
		failed += kernel_wait(iscsi_fd, timeout);

	error = close(iscsi_fd);
	if (error != 0)
		xo_err(1, "close");

	xo_close_container("iscsictl");
	xo_finish();

	if (failed != 0)
		return (1);

	return (0);
}
