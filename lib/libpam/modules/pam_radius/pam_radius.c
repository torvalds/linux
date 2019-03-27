/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright 1998 Juniper Networks, Inc.
 * All rights reserved.
 * Copyright (c) 2001-2003 Networks Associates Technology, Inc.
 * All rights reserved.
 * Copyright (c) 2015-2018 The University of Oslo
 * All rights reserved.
 *
 * Portions of this software were developed for the FreeBSD Project by
 * ThinkSec AS and NAI Labs, the Security Research Division of Network
 * Associates, Inc.  under DARPA/SPAWAR contract N66001-01-C-8035
 * ("CBOSS"), as part of the DARPA CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pwd.h>
#include <radlib.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#define PAM_SM_AUTH

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <security/pam_mod_misc.h>

#define PAM_OPT_CONF		"conf"
#define PAM_OPT_TEMPLATE_USER	"template_user"
#define PAM_OPT_NAS_ID		"nas_id"
#define PAM_OPT_NAS_IPADDR	"nas_ipaddr"
#define PAM_OPT_NO_REPLYMSG	"no_reply_message"

#define	MAX_CHALLENGE_MSGS	10
#define	PASSWORD_PROMPT		"RADIUS Password:"

static int	 build_access_request(struct rad_handle *, const char *,
		    const char *, const char *, const char *, const char *,
		    const void *, size_t);
static int	 do_accept(pam_handle_t *, struct rad_handle *);
static int	 do_challenge(pam_handle_t *, struct rad_handle *,
		    const char *, const char *, const char *, const char *);

/*
 * Construct an access request, but don't send it.  Returns 0 on success,
 * -1 on failure.
 */
static int
build_access_request(struct rad_handle *radh, const char *user,
    const char *pass, const char *nas_id, const char *nas_ipaddr,
    const char *rhost, const void *state, size_t state_len)
{
	int error;
	char host[MAXHOSTNAMELEN];
	struct sockaddr_in *haddr;
	struct addrinfo hints;
	struct addrinfo *res;

	if (rad_create_request(radh, RAD_ACCESS_REQUEST) == -1) {
		syslog(LOG_CRIT, "rad_create_request: %s", rad_strerror(radh));
		return (-1);
	}
	if (nas_id == NULL ||
	    (nas_ipaddr != NULL && strlen(nas_ipaddr) == 0)) {
		if (gethostname(host, sizeof host) != -1) {
			if (nas_id == NULL)
				nas_id = host;
			if (nas_ipaddr != NULL && strlen(nas_ipaddr) == 0)
				nas_ipaddr = host;
		}
	}
	if ((user != NULL &&
	    rad_put_string(radh, RAD_USER_NAME, user) == -1) ||
	    (pass != NULL &&
	    rad_put_string(radh, RAD_USER_PASSWORD, pass) == -1) ||
	    (nas_id != NULL &&
	    rad_put_string(radh, RAD_NAS_IDENTIFIER, nas_id) == -1)) {
		syslog(LOG_CRIT, "rad_put_string: %s", rad_strerror(radh));
		return (-1);
	}
	if (nas_ipaddr != NULL) {
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		if (getaddrinfo(nas_ipaddr, NULL, &hints, &res) == 0 &&
		    res != NULL && res->ai_family == AF_INET) {
			haddr = (struct sockaddr_in *)res->ai_addr;
			error = rad_put_addr(radh, RAD_NAS_IP_ADDRESS,
			    haddr->sin_addr);
			freeaddrinfo(res);
			if (error == -1) {
				syslog(LOG_CRIT, "rad_put_addr: %s",
				    rad_strerror(radh));
				return (-1);
			}
		}
	}
	if (rhost != NULL &&
	    rad_put_string(radh, RAD_CALLING_STATION_ID, rhost) == -1) {
		syslog(LOG_CRIT, "rad_put_string: %s", rad_strerror(radh));
		return (-1);
	}
	if (state != NULL &&
	    rad_put_attr(radh, RAD_STATE, state, state_len) == -1) {
		syslog(LOG_CRIT, "rad_put_attr: %s", rad_strerror(radh));
		return (-1);
	}
	if (rad_put_int(radh, RAD_SERVICE_TYPE, RAD_AUTHENTICATE_ONLY) == -1) {
		syslog(LOG_CRIT, "rad_put_int: %s", rad_strerror(radh));
		return (-1);
	}
	return (0);
}

static int
do_accept(pam_handle_t *pamh, struct rad_handle *radh)
{
	int attrtype;
	const void *attrval;
	size_t attrlen;
	char *s;

	while ((attrtype = rad_get_attr(radh, &attrval, &attrlen)) > 0) {
		switch (attrtype) {
		case RAD_USER_NAME:
			if ((s = rad_cvt_string(attrval, attrlen)) == NULL)
				goto enomem;
			pam_set_item(pamh, PAM_USER, s);
			free(s);
			break;
		case RAD_REPLY_MESSAGE:
			if ((s = rad_cvt_string(attrval, attrlen)) == NULL)
				goto enomem;
			if (!openpam_get_option(pamh, PAM_OPT_NO_REPLYMSG))
				pam_info(pamh, "%s", s);
			free(s);
			break;
		default:
			PAM_LOG("%s(): ignoring RADIUS attribute %d",
			    __func__, attrtype);
		}
	}
	if (attrtype == -1) {
		syslog(LOG_CRIT, "rad_get_attr: %s", rad_strerror(radh));
		return (-1);
	}
	return (0);
enomem:
	syslog(LOG_CRIT, "%s(): out of memory", __func__);
	return (-1);
}

static int
do_reject(pam_handle_t *pamh, struct rad_handle *radh)
{
	int attrtype;
	const void *attrval;
	size_t attrlen;
	char *s;

	while ((attrtype = rad_get_attr(radh, &attrval, &attrlen)) > 0) {
		switch (attrtype) {
		case RAD_REPLY_MESSAGE:
			if ((s = rad_cvt_string(attrval, attrlen)) == NULL)
				goto enomem;
			if (!openpam_get_option(pamh, PAM_OPT_NO_REPLYMSG))
				pam_error(pamh, "%s", s);
			free(s);
			break;
		default:
			PAM_LOG("%s(): ignoring RADIUS attribute %d",
			    __func__, attrtype);
		}
	}
	if (attrtype < 0) {
		syslog(LOG_CRIT, "rad_get_attr: %s", rad_strerror(radh));
		return (-1);
	}
	return (0);
enomem:
	syslog(LOG_CRIT, "%s(): out of memory", __func__);
	return (-1);
}

static int
do_challenge(pam_handle_t *pamh, struct rad_handle *radh, const char *user,
    const char *nas_id, const char *nas_ipaddr, const char *rhost)
{
	int retval;
	int attrtype;
	const void *attrval;
	size_t attrlen;
	const void *state;
	size_t statelen;
	struct pam_message msgs[MAX_CHALLENGE_MSGS];
	const struct pam_message *msg_ptrs[MAX_CHALLENGE_MSGS];
	struct pam_response *resp;
	int num_msgs;
	const void *item;
	const struct pam_conv *conv;

	state = NULL;
	statelen = 0;
	num_msgs = 0;
	while ((attrtype = rad_get_attr(radh, &attrval, &attrlen)) > 0) {
		switch (attrtype) {

		case RAD_STATE:
			state = attrval;
			statelen = attrlen;
			break;

		case RAD_REPLY_MESSAGE:
			if (num_msgs >= MAX_CHALLENGE_MSGS) {
				syslog(LOG_CRIT,
				    "Too many RADIUS challenge messages");
				return (PAM_SERVICE_ERR);
			}
			msgs[num_msgs].msg = rad_cvt_string(attrval, attrlen);
			if (msgs[num_msgs].msg == NULL) {
				syslog(LOG_CRIT,
				    "rad_cvt_string: out of memory");
				return (PAM_SERVICE_ERR);
			}
			msgs[num_msgs].msg_style = PAM_TEXT_INFO;
			msg_ptrs[num_msgs] = &msgs[num_msgs];
			num_msgs++;
			break;
		}
	}
	if (attrtype == -1) {
		syslog(LOG_CRIT, "rad_get_attr: %s", rad_strerror(radh));
		return (PAM_SERVICE_ERR);
	}
	if (num_msgs == 0) {
		msgs[num_msgs].msg = strdup("(null RADIUS challenge): ");
		if (msgs[num_msgs].msg == NULL) {
			syslog(LOG_CRIT, "Out of memory");
			return (PAM_SERVICE_ERR);
		}
		msgs[num_msgs].msg_style = PAM_TEXT_INFO;
		msg_ptrs[num_msgs] = &msgs[num_msgs];
		num_msgs++;
	}
	msgs[num_msgs-1].msg_style = PAM_PROMPT_ECHO_ON;
	if ((retval = pam_get_item(pamh, PAM_CONV, &item)) != PAM_SUCCESS) {
		syslog(LOG_CRIT, "do_challenge: cannot get PAM_CONV");
		return (retval);
	}
	conv = (const struct pam_conv *)item;
	if ((retval = conv->conv(num_msgs, msg_ptrs, &resp,
	    conv->appdata_ptr)) != PAM_SUCCESS)
		return (retval);
	if (build_access_request(radh, user, resp[num_msgs-1].resp, nas_id,
	    nas_ipaddr, rhost, state, statelen) == -1)
		return (PAM_SERVICE_ERR);
	memset(resp[num_msgs-1].resp, 0, strlen(resp[num_msgs-1].resp));
	free(resp[num_msgs-1].resp);
	free(resp);
	while (num_msgs > 0)
		free(msgs[--num_msgs].msg);
	return (PAM_SUCCESS);
}

PAM_EXTERN int
pam_sm_authenticate(pam_handle_t *pamh, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{
	struct rad_handle *radh;
	const char *user, *pass;
	const void *rhost, *tmpuser;
	const char *conf_file, *template_user, *nas_id, *nas_ipaddr;
	int retval;
	int e;

	conf_file = openpam_get_option(pamh, PAM_OPT_CONF);
	template_user = openpam_get_option(pamh, PAM_OPT_TEMPLATE_USER);
	nas_id = openpam_get_option(pamh, PAM_OPT_NAS_ID);
	nas_ipaddr = openpam_get_option(pamh, PAM_OPT_NAS_IPADDR);
	pam_get_item(pamh, PAM_RHOST, &rhost);

	retval = pam_get_user(pamh, &user, NULL);
	if (retval != PAM_SUCCESS)
		return (retval);

	PAM_LOG("Got user: %s", user);

	retval = pam_get_authtok(pamh, PAM_AUTHTOK, &pass, PASSWORD_PROMPT);
	if (retval != PAM_SUCCESS)
		return (retval);

	PAM_LOG("Got password");

	radh = rad_open();
	if (radh == NULL) {
		syslog(LOG_CRIT, "rad_open failed");
		return (PAM_SERVICE_ERR);
	}

	PAM_LOG("Radius opened");

	if (rad_config(radh, conf_file) == -1) {
		syslog(LOG_ALERT, "rad_config: %s", rad_strerror(radh));
		rad_close(radh);
		return (PAM_SERVICE_ERR);
	}

	PAM_LOG("Radius config file read");

	if (build_access_request(radh, user, pass, nas_id, nas_ipaddr, rhost,
	    NULL, 0) == -1) {
		rad_close(radh);
		return (PAM_SERVICE_ERR);
	}

	PAM_LOG("Radius build access done");

	for (;;) {
		switch (rad_send_request(radh)) {

		case RAD_ACCESS_ACCEPT:
			e = do_accept(pamh, radh);
			rad_close(radh);
			if (e == -1)
				return (PAM_SERVICE_ERR);
			if (template_user != NULL) {

				PAM_LOG("Trying template user: %s",
				    template_user);

				/*
				 * If the given user name doesn't exist in
				 * the local password database, change it
				 * to the value given in the "template_user"
				 * option.
				 */
				retval = pam_get_item(pamh, PAM_USER, &tmpuser);
				if (retval != PAM_SUCCESS)
					return (retval);
				if (getpwnam(tmpuser) == NULL) {
					pam_set_item(pamh, PAM_USER,
					    template_user);
					PAM_LOG("Using template user");
				}

			}
			return (PAM_SUCCESS);

		case RAD_ACCESS_REJECT:
			retval = do_reject(pamh, radh);
			rad_close(radh);
			PAM_VERBOSE_ERROR("Radius rejection");
			return (PAM_AUTH_ERR);

		case RAD_ACCESS_CHALLENGE:
			retval = do_challenge(pamh, radh, user, nas_id,
			    nas_ipaddr, rhost);
			if (retval != PAM_SUCCESS) {
				rad_close(radh);
				return (retval);
			}
			break;

		case -1:
			syslog(LOG_CRIT, "rad_send_request: %s",
			    rad_strerror(radh));
			rad_close(radh);
			PAM_VERBOSE_ERROR("Radius failure");
			return (PAM_AUTHINFO_UNAVAIL);

		default:
			syslog(LOG_CRIT,
			    "rad_send_request: unexpected return value");
			rad_close(radh);
			PAM_VERBOSE_ERROR("Radius error");
			return (PAM_SERVICE_ERR);
		}
	}
}

PAM_EXTERN int
pam_sm_setcred(pam_handle_t *pamh __unused, int flags __unused,
    int argc __unused, const char *argv[] __unused)
{

	return (PAM_SUCCESS);
}

PAM_MODULE_ENTRY("pam_radius");
