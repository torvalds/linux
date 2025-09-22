/*	$OpenBSD: npppd_auth.c,v 1.23 2024/02/26 10:42:05 yasuoka Exp $ */

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
 * All rights reserved.
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
 */
/**@file authentication realm */
/* $Id: npppd_auth.c,v 1.23 2024/02/26 10:42:05 yasuoka Exp $ */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if_dl.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <time.h>
#include <event.h>
#include <stdarg.h>
#include <stdlib.h>
#include <netdb.h>
#include <errno.h>

#include "debugutil.h"
#include "npppd_local.h"
#include "npppd_auth.h"
#include "net_utils.h"

#include "npppd_auth_local.h"
#include "npppd_radius.h"

/**
 * Create a npppd_auth_base object.
 * @param auth_type	the authentication type.
 *	specify {@link ::NPPPD_AUTH_TYPE_LOCAL} to authenticate by the local
 *	file, or specify {@link ::NPPPD_AUTH_TYPE_RADIUS} for RADIUS
 *	authentication.
 * @param name		the configuration name
 * @param _npppd	the parent {@link ::npppd} object
 * @see	::NPPPD_AUTH_TYPE_LOCAL
 * @see	::NPPPD_AUTH_TYPE_RADIUS
 * @return The pointer to the {@link ::npppd_auth_base} object will be returned
 * in case success otherwise NULL will be returned.
 */
npppd_auth_base *
npppd_auth_create(int auth_type, const char *name, void *_npppd)
{
	npppd_auth_base *base;

	NPPPD_AUTH_ASSERT(name != NULL);

	switch (auth_type) {
	case NPPPD_AUTH_TYPE_LOCAL:
		if ((base = calloc(1, sizeof(npppd_auth_local))) != NULL) {
			base->type = NPPPD_AUTH_TYPE_LOCAL;
			strlcpy(base->name, name, sizeof(base->name));
			base->npppd = _npppd;

			return base;
		}
		break;

#ifdef USE_NPPPD_RADIUS
	case NPPPD_AUTH_TYPE_RADIUS:
		if ((base = calloc(1, sizeof(npppd_auth_radius))) != NULL) {
			npppd_auth_radius *_this = (npppd_auth_radius *)base;
			base->type = NPPPD_AUTH_TYPE_RADIUS;
			strlcpy(base->name, name, sizeof(base->name));
			base->npppd = _npppd;
			if ((_this->rad_auth_setting =
			    radius_req_setting_create()) == NULL)
				goto radius_fail;
			if ((_this->rad_acct_setting =
			    radius_req_setting_create()) == NULL)
				goto radius_fail;

			return base;
radius_fail:
			if (_this->rad_auth_setting != NULL)
				radius_req_setting_destroy(
				    _this->rad_auth_setting);
			if (_this->rad_acct_setting != NULL)
				radius_req_setting_destroy(
				    _this->rad_acct_setting);
			free(base);
			return NULL;
		}

		break;
#endif

	default:
		NPPPD_AUTH_ASSERT(0);
		break;
	}

	return NULL;
}

/**
 * Call this function to make the object unusable.
 * <p>
 * {@link ::npppd_auth_base} objects is referred by the {@link ::npppd_ppp}
 * object.   After this function is called, npppd will disconnect the PPP
 * links that refers the object, it will call {@link ::npppd_auth_destroy()}
 * when all the references to the object are released.</p>
 */
void
npppd_auth_dispose(npppd_auth_base *base)
{

	base->disposing = 1;

	return;
}

/** Destroy the {@link ::npppd_auth_base} object.  */
void
npppd_auth_destroy(npppd_auth_base *base)
{

	if (base->disposing == 0)
		npppd_auth_dispose(base);

	npppd_auth_base_log(base, LOG_INFO, "Finalized");

	switch(base->type) {
	case NPPPD_AUTH_TYPE_LOCAL:
		memset(base, 0, sizeof(npppd_auth_local));
		break;

#ifdef USE_NPPPD_RADIUS
	case NPPPD_AUTH_TYPE_RADIUS:
	    {
		npppd_auth_radius *_this = (npppd_auth_radius *)base;
		if (_this->rad_auth_setting != NULL)
			radius_req_setting_destroy(_this->rad_auth_setting);
		_this->rad_auth_setting = NULL;
		if (_this->rad_acct_setting != NULL)
			radius_req_setting_destroy(_this->rad_acct_setting);
		_this->rad_acct_setting = NULL;
		memset(base, 0, sizeof(npppd_auth_local));
		break;
	    }
#endif
	}
	free(base);

	return;
}

/** Reload the configuration */
int
npppd_auth_reload(npppd_auth_base *base)
{
	struct authconf *auth;

	TAILQ_FOREACH(auth, &base->npppd->conf.authconfs, entry) {
		if (strcmp(auth->name, base->name) == 0)
			break;
	}
	if (auth == NULL)
		return 1;

	base->pppsuffix[0] = '\0';
	if (auth->username_suffix != NULL)
		strlcpy(base->pppsuffix, auth->username_suffix,
		    sizeof(base->pppsuffix));
	base->eap_capable = auth->eap_capable;
	base->strip_nt_domain = auth->strip_nt_domain;
	base->strip_atmark_realm = auth->strip_atmark_realm;
	base->has_users_file = 0;
	base->radius_ready = 0;
	base->user_max_session = auth->user_max_session;

	if (strlen(auth->users_file_path) > 0) {
		strlcpy(base->users_file_path, auth->users_file_path,
		    sizeof(base->users_file_path));
		base->has_users_file = 1;
	} else {
		if (base->type == NPPPD_AUTH_TYPE_LOCAL) {
			npppd_auth_base_log(base,
			    LOG_WARNING, "missing users_file property.");
			goto fail;
		}
	}

	switch (base->type) {
#ifdef USE_NPPPD_RADIUS
	case NPPPD_AUTH_TYPE_RADIUS:
		if (npppd_auth_radius_reload(base, auth) != 0)
			goto fail;
		break;
#endif
	}
	base->initialized = 1;

	return 0;

fail:
	base->initialized = 0;
	base->has_users_file = 0;
	base->radius_ready = 0;

	return 1;
}

/**
 * This function gets specified user's password. The value 0 is returned
 * if the call succeeds.
 *
 * @param	username	username which gets the password
 * @param	password	buffers which stores the password
 *				Specify NULL if you want to known the length of
 *				the password only.
 * @param	lppassword	pointer which indicates the length of
 *				the buffer which stores the password.
 * @return A value 1 is returned if user is unknown. A value 2 is returned
 *				if password buffer is sufficient. A negative value is
 *				returned if other error occurred.
 */
int
npppd_auth_get_user_password(npppd_auth_base *base,
    const char *username, char *password, int *plpassword)
{
	int              retval, sz, lpassword;
	npppd_auth_user *user;

	NPPPD_AUTH_ASSERT(base != NULL);
	NPPPD_AUTH_DBG((base, LOG_DEBUG, "%s(%s)", __func__, username));

	user = NULL;
	retval = 0;
	if (base->has_users_file == 0) {
		retval = -1;
		goto out;
	}
	if ((user = npppd_auth_get_user(base, username)) == NULL) {
		retval = 1;
		goto out;
	}
	if (password == NULL && plpassword == NULL) {
		retval = 0;
		goto out;
	}
	if (plpassword == NULL) {
		retval = -1;
		goto out;
	}
	lpassword = strlen(user->password) + 1;
	sz = *plpassword;
	*plpassword = lpassword;
	if (password == NULL) {
		retval = 0;
		goto out;
	}
	if (sz < lpassword) {
		retval = 2;
		goto out;
	}
	strlcpy(password, user->password, sz);
out:
	free(user);

	return retval;
}

/**
 * This function gets specified users' Framed-IP-{Address,Netmask}.
 * The value 0 is returned if the call succeeds.
 * <p>
 * Because authentication database is updated at any time, the password is
 * possible to be inconsistent if this function is not called immediately
 * after authentication. So this function is called immediately after
 * authentication. </p>
 * @param	username	username which gets the password
 * @param	ip4address	pointer which indicates struct in_addr which
 *						stores the Framed-IP-Address
 * @param	ip4netmask	pointer which indicates struct in_addr which
 *						stores Framed-IP-Netmask
 */
int
npppd_auth_get_framed_ip(npppd_auth_base *base, const char *username,
    struct in_addr *ip4address, struct in_addr *ip4netmask)
{
	npppd_auth_user *user;

	NPPPD_AUTH_ASSERT(base != NULL);
	NPPPD_AUTH_DBG((base, LOG_DEBUG, "%s(%s)", __func__, username));
	if (base->has_users_file == 0)
		return -1;

	if ((user = npppd_auth_get_user(base, username)) == NULL)
		return 1;

	if (user->framed_ip_address.s_addr != 0) {
		*ip4address = user->framed_ip_address;
		if (ip4netmask != NULL)
			*ip4netmask = user->framed_ip_netmask;

		free(user);
		return 0;
	}
	free(user);

	return 1;
}

/**
 * Retribute "Calling-Number" attribute of the user from the realm.
 *
 * @param username	Username.
 * @param number	Pointer to the space for the Calling-Number.  This
 *	can be NULL in case retributing the Calling-Number only.
 * @param plnumber	Pointer to the length of the space for the
 *	Calling-Number.
 * @return 0 if the Calling-Number attribute is successfully retributed.
 *	1 if the user has no Calling-Number attribute.  return -1 if the realm
 *	doesn't have user attributes or other errors.   return 2 if the space
 *	is not enough.
 */
int
npppd_auth_get_calling_number(npppd_auth_base *base, const char *username,
    char *number, int *plnumber)
{
	int              retval, lcallnum, sz;
	npppd_auth_user *user;

	user = NULL;
	retval = 0;
	if (base->has_users_file == 0)
		return -1;

	if ((user = npppd_auth_get_user(base, username)) == NULL)
		return 1;

	if (number == NULL && plnumber == NULL) {
		retval = 0;
		goto out;
	}
	if (plnumber == NULL) {
		retval = -1;
		goto out;
	}
	lcallnum = strlen(user->calling_number) + 1;
	sz = *plnumber;
	*plnumber = lcallnum;
	if (sz < lcallnum) {
		retval = 2;
		goto out;
	}
	strlcpy(number, user->calling_number, sz);

out:
	free(user);

	return retval;
}

int
npppd_auth_get_type(npppd_auth_base *base)
{
	return base->type;
}

int
npppd_auth_is_usable(npppd_auth_base *base)
{
    	return (base->initialized != 0 && base->disposing == 0)? 1 : 0;
}

int
npppd_auth_is_ready(npppd_auth_base *base)
{
	if (!npppd_auth_is_usable(base))
		return 0;

	switch(base->type) {
	case NPPPD_AUTH_TYPE_LOCAL:
		return (base->has_users_file)? 1 : 0;
		/* NOTREACHED */

	case NPPPD_AUTH_TYPE_RADIUS:
		return (base->has_users_file != 0 ||
		    base->radius_ready != 0)? 1 : 0;
		/* NOTREACHED */
	}
	NPPPD_AUTH_ASSERT(0);

    	return 0;
}

int
npppd_auth_is_disposing(npppd_auth_base *base)
{
	return (base->disposing != 0)? 1 : 0;
}

int
npppd_auth_is_eap_capable(npppd_auth_base *base)
{
	return (base->eap_capable != 0)? 1 : 0;
}

const char *
npppd_auth_get_name(npppd_auth_base *base)
{
	return base->name;
}

const char *
npppd_auth_get_suffix(npppd_auth_base *base)
{
	return base->pppsuffix;
}

const char *
npppd_auth_username_for_auth(npppd_auth_base *base, const char *username,
    char *username_buffer)
{
	const char *u0;
	char *atmark, *u1;

	u0 = NULL;
	if (base->strip_nt_domain != 0) {
		if ((u0 = strchr(username, '\\')) != NULL)
			u0++;
	}
	if (u0 == NULL)
		u0 = username;
	u1 = username_buffer;
	if (username_buffer != u0)
		memmove(username_buffer, u0, MINIMUM(strlen(u0) + 1,
		    MAX_USERNAME_LENGTH));
	if (base->strip_atmark_realm != 0) {
		if ((atmark = strrchr(u1, '@')) != NULL)
			*atmark = '\0';
	}

	return username_buffer;
}

int
npppd_auth_user_session_unlimited(npppd_auth_base *_this)
{
	return (_this->user_max_session == 0) ? 1 : 0;
}

int
npppd_check_auth_user_max_session(npppd_auth_base *_this, int count)
{
	if (!npppd_auth_user_session_unlimited(_this) &&
	    _this->user_max_session <= count)
		return 1;
	else
		return 0;
}

/***********************************************************************
 * Account list related functions
 ***********************************************************************/
static npppd_auth_user *
npppd_auth_get_user(npppd_auth_base *base, const char *username)
{
	int              lsuffix, lusername;
	const char      *un;
	char             buf[MAX_USERNAME_LENGTH];
	npppd_auth_user *u;

	un = username;
	lsuffix = strlen(base->pppsuffix);
	lusername = strlen(username);
	if (lsuffix > 0 && lusername > lsuffix &&
	    strcmp(username + lusername - lsuffix, base->pppsuffix) == 0 &&
	    lusername - lsuffix < sizeof(buf)) {
		memcpy(buf, username, lusername - lsuffix);
		buf[lusername - lsuffix] = '\0';
		un = buf;
	}
	
	if (priv_get_user_info(base->users_file_path, un, &u) == 0)
		return u;

	return NULL;
}

#ifdef USE_NPPPD_RADIUS
/***********************************************************************
 * RADIUS
 ***********************************************************************/
/** reload the configuration of RADIUS authentication realm */
static int
npppd_auth_radius_reload(npppd_auth_base *base, struct authconf *auth)
{
	npppd_auth_radius  *_this = (npppd_auth_radius *)base;
	radius_req_setting *rad;
	struct radserver   *server;
	int                 i, nauth, nacct;

	_this->rad_auth_setting->timeout =
	    (auth->data.radius.auth.timeout == 0)
		    ? DEFAULT_RADIUS_TIMEOUT : auth->data.radius.auth.timeout;
	_this->rad_acct_setting->timeout =
	    (auth->data.radius.acct.timeout == 0)
		    ? DEFAULT_RADIUS_TIMEOUT : auth->data.radius.acct.timeout;


	_this->rad_auth_setting->max_tries =
	    (auth->data.radius.auth.max_tries == 0)
		    ? DEFAULT_RADIUS_MAX_TRIES : auth->data.radius.auth.max_tries;
	_this->rad_acct_setting->max_tries =
	    (auth->data.radius.acct.max_tries == 0)
		    ? DEFAULT_RADIUS_MAX_TRIES : auth->data.radius.acct.max_tries;

	_this->rad_auth_setting->max_failovers =
	    (auth->data.radius.auth.max_failovers == 0)
		    ? DEFAULT_RADIUS_MAX_FAILOVERS
		    : auth->data.radius.auth.max_failovers;
	_this->rad_acct_setting->max_failovers =
	    (auth->data.radius.acct.max_failovers == 0)
		    ? DEFAULT_RADIUS_MAX_FAILOVERS
		    : auth->data.radius.acct.max_failovers;

	_this->rad_acct_setting->curr_server = 
	_this->rad_auth_setting->curr_server = 0;

	/* load configs for authentication server */
	rad = _this->rad_auth_setting;
	for (i = 0; i < countof(rad->server); i++)
		memset(&rad->server[i], 0, sizeof(rad->server[0]));
	i = 0;
	TAILQ_FOREACH(server, &auth->data.radius.auth.servers, entry) {
		if (i >= countof(rad->server))
			break;
		memcpy(&rad->server[i].peer, &server->address,
		    server->address.ss_len);
		if (((struct sockaddr_in *)&rad->server[i].peer)->sin_port
		    == 0)
			((struct sockaddr_in *)&rad->server[i].peer)->sin_port
			    = htons(DEFAULT_RADIUS_AUTH_PORT);
		strlcpy(rad->server[i].secret, server->secret,
		    sizeof(rad->server[i].secret));
		rad->server[i].enabled = 1;
		i++;
	}
	nauth = i;

	/* load configs for accounting server */
	rad = _this->rad_acct_setting;
	for (i = 0; i < countof(rad->server); i++)
		memset(&rad->server[i], 0, sizeof(rad->server[0]));
	i = 0;
	TAILQ_FOREACH(server, &auth->data.radius.acct.servers, entry) {
		if (i >= countof(rad->server))
			break;
		memcpy(&rad->server[i].peer, &server->address,
		    server->address.ss_len);
		if (((struct sockaddr_in *)&rad->server[i].peer)->sin_port
		    == 0)
			((struct sockaddr_in *)&rad->server[i].peer)->sin_port
			    = htons(DEFAULT_RADIUS_ACCT_PORT);
		strlcpy(rad->server[i].secret, server->secret,
		    sizeof(rad->server[i].secret));
		rad->server[i].enabled = 1;
		i++;
	}
	nacct = i;

	for (i = 0; i < countof(_this->rad_auth_setting->server); i++) {
		if (_this->rad_auth_setting->server[i].enabled)
			base->radius_ready = 1;
	}

	npppd_auth_base_log(&_this->nar_base, LOG_INFO,
	    "Loaded configuration.  %d authentication server%s, %d accounting "
	    "server%s.",
	    nauth, (nauth > 1)? "s" : "", nacct, (nacct > 1)? "s" : "");

	if (nacct > 0 && _this->rad_acct_on == 0) {
		radius_acct_on(base->npppd, _this->rad_acct_setting);
		_this->rad_acct_on = 1;
	}

	return 0;
}

/**
 * Get {@link ::radius_req_setting} for RADIUS authentication of specified
 * {@link ::npppd_auth_base} object.
 */
void *
npppd_auth_radius_get_radius_auth_setting(npppd_auth_radius *_this)
{
	return _this->rad_auth_setting;
}

/**
 * Get {@link ::radius_req_setting} for RADIUS accounting of specified
 * {@link ::npppd_auth_base} object.
 */
void *
npppd_auth_radius_get_radius_acct_setting(npppd_auth_radius *_this)
{
	return _this->rad_acct_setting;
}

#endif

/***********************************************************************
 * Helper functions
 ***********************************************************************/
/** Log it which starts the label based on this instance. */
static int
npppd_auth_base_log(npppd_auth_base *_this, int prio, const char *fmt, ...)
{
	int status;
	char logbuf[BUFSIZ];
	va_list ap;

	NPPPD_AUTH_ASSERT(_this != NULL);
	va_start(ap, fmt);
	snprintf(logbuf, sizeof(logbuf), "realm name=%s %s",
	    _this->name, fmt);
	status = vlog_printf(prio, logbuf, ap);
	va_end(ap);

	return status;
}
