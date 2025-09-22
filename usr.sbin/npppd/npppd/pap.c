/*	$OpenBSD: pap.c,v 1.14 2024/07/01 07:09:07 yasuoka Exp $ */

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
/* $Id: pap.c,v 1.14 2024/07/01 07:09:07 yasuoka Exp $ */
/**@file
 * This file provides Password Authentication Protocol (PAP) handlers.
 * @author Yasuoka Masahiko
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <net/if_dl.h>
#include <netinet/in.h>

#include <event.h>
#include <md5.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <vis.h>

#include "npppd.h"
#include "ppp.h"

#ifdef USE_NPPPD_RADIUS
#include <radius.h>
#include "radius_chap_const.h"
#include "npppd_radius.h"
#endif

#include "debugutil.h"

#define	AUTHREQ				0x01
#define	AUTHACK				0x02
#define	AUTHNAK				0x03

#define	PAP_STATE_INITIAL		0
#define	PAP_STATE_STARTING		1
#define	PAP_STATE_AUTHENTICATING	2
#define	PAP_STATE_SENT_RESPONSE		3
#define	PAP_STATE_STOPPED		4
#define	PAP_STATE_PROXY_AUTHENTICATION	5

#define	DEFAULT_SUCCESS_MESSAGE		"OK"
#define	DEFAULT_FAILURE_MESSAGE		"Unknown username or password"
#define	DEFAULT_ERROR_MESSAGE		"Unknown failure"

#ifdef	PAP_DEBUG
#define	PAP_DBG(x)	pap_log x
#define	PAP_ASSERT(cond)					\
	if (!(cond)) {						\
	    fprintf(stderr,					\
		"\nASSERT(" #cond ") failed on %s() at %s:%d.\n"\
		, __func__, __FILE__, __LINE__);		\
	    abort(); 						\
	}
#else
#define	PAP_ASSERT(cond)
#define	PAP_DBG(x)
#endif

static void  pap_log (pap *, uint32_t, const char *, ...) __printflike(3,4);
static void  pap_response (pap *, int, const char *);
static void  pap_authenticate(pap *, const char *);
static void  pap_local_authenticate (pap *, const char *, const char *);
#ifdef USE_NPPPD_RADIUS
static void  pap_radius_authenticate (pap *, const char *, const char *);
static void  pap_radius_response (void *, RADIUS_PACKET *, int, RADIUS_REQUEST_CTX);
#endif

#ifdef __cplusplus
extern "C" {
#endif

void  pap_init (pap *, npppd_ppp *);
int   pap_start (pap *);
int   pap_stop (pap *);
int   pap_input (pap *, u_char *, int);

#ifdef __cplusplus
}
#endif

void
pap_init(pap *_this, npppd_ppp *ppp)
{
	_this->ppp = ppp;
	_this->state = PAP_STATE_INITIAL;
	_this->auth_id = -1;
}

int
pap_start(pap *_this)
{
	pap_log(_this, LOG_DEBUG, "%s", __func__);

	if (_this->state == PAP_STATE_PROXY_AUTHENTICATION) {
		_this->state = PAP_STATE_AUTHENTICATING;
		pap_authenticate(_this, _this->ppp->proxy_authen_resp);
		return 0;
	}

	_this->state = PAP_STATE_STARTING;
	return 0;
}

int
pap_stop(pap *_this)
{
	_this->state = PAP_STATE_STOPPED;
	_this->auth_id = -1;

#ifdef USE_NPPPD_RADIUS
	if (_this->radctx != NULL) {
		radius_cancel_request(_this->radctx);
		_this->radctx = NULL;
	}
#endif
	return 0;
}

/** Receiving PAP packet */
int
pap_input(pap *_this, u_char *pktp, int lpktp)
{
	int code, id, length, len;
	u_char *pktp1;
	char name[MAX_USERNAME_LENGTH], password[MAX_PASSWORD_LENGTH];

	if (_this->state == PAP_STATE_STOPPED ||
	    _this->state == PAP_STATE_INITIAL) {
		pap_log(_this, LOG_ERR, "Received pap packet.  But pap is "
		    "not started.");
		return -1;
	}
	pktp1 = pktp;

	GETCHAR(code, pktp1);
	GETCHAR(id, pktp1);
	GETSHORT(length, pktp1);

	if (code != AUTHREQ) {
		pap_log(_this, LOG_ERR, "%s: Received unknown code=%d",
		    __func__, code);
		return -1;
	}
	if (lpktp < length) {
		pap_log(_this, LOG_ERR, "%s: Received broken packet.",
		    __func__);
		return -1;
	}

	/* retribute the username */
#define	remlen		(lpktp - (pktp1 - pktp))
	if (remlen < 1)
		goto fail;
	GETCHAR(len, pktp1);
	if (len <= 0)
		goto fail;
	if (remlen < len)
		goto fail;
	if (len > 0)
		memcpy(name, pktp1, len);
	name[len] = '\0';
	pktp1 += len;

	if (_this->state != PAP_STATE_STARTING) {
		/*
		 * Receiving identical message again, it must be the message
		 * retransmit by the peer.  Continue if the username is same.
		 */
		if ((_this->state == PAP_STATE_AUTHENTICATING ||
		    _this->state == PAP_STATE_SENT_RESPONSE) &&
		    strcmp(_this->name, name) == 0) {
			/* continue */
		} else {
			pap_log(_this, LOG_ERR,
			    "Received AuthReq is not same as before.  "
			    "(%d,%s) != (%d,%s)", id, name, _this->auth_id,
			    _this->name);
			_this->auth_id = id;
			goto fail;
		}
	}
	if (_this->state == PAP_STATE_AUTHENTICATING)
		return 0;
	_this->auth_id = id;
	strlcpy(_this->name, name, sizeof(_this->name));

	_this->state = PAP_STATE_AUTHENTICATING;

	/* retribute the password */
	if (remlen < 1)
		goto fail;
	GETCHAR(len, pktp1);
	if (remlen < len)
		goto fail;
	if (len > 0)
		memcpy(password, pktp1, len);

	password[len] = '\0';
	pap_authenticate(_this, password);

	return 0;
fail:
	pap_response(_this, 0, DEFAULT_FAILURE_MESSAGE);
	return -1;
}

static void
pap_authenticate(pap *_this, const char *password)
{
	if (npppd_ppp_bind_realm(_this->ppp->pppd, _this->ppp, _this->name, 0)
	    == 0) {
		if (!npppd_ppp_is_realm_ready(_this->ppp->pppd, _this->ppp)) {
			pap_log(_this, LOG_INFO,
			    "username=\"%s\" realm is not ready.", _this->name);
			goto fail;
			/* NOTREACHED */
		}
#if USE_NPPPD_RADIUS
		if (npppd_ppp_is_realm_radius(_this->ppp->pppd, _this->ppp)) {
			pap_radius_authenticate(_this, _this->name, password);
			return;
			/* NOTREACHED */
		} else
#endif
		if (npppd_ppp_is_realm_local(_this->ppp->pppd, _this->ppp)) {
			pap_local_authenticate(_this, _this->name, password);
			return;
			/* NOTREACHED */
		}
	}
fail:
	pap_response(_this, 0, DEFAULT_FAILURE_MESSAGE);
}

static void
pap_log(pap *_this, uint32_t prio, const char *fmt, ...)
{
	char logbuf[BUFSIZ];
	va_list ap;

	va_start(ap, fmt);
	snprintf(logbuf, sizeof(logbuf), "ppp id=%u layer=pap %s",
	    _this->ppp->id, fmt);
	vlog_printf(prio, logbuf, ap);
	va_end(ap);
}

static void
pap_response(pap *_this, int authok, const char *mes)
{
	int lpktp, lmes;
	u_char *pktp, *pktp1;
	const char *realm;

	pktp = ppp_packetbuf(_this->ppp, PPP_PROTO_PAP) + HEADERLEN;
	lpktp = _this->ppp->mru - HEADERLEN;
	realm = npppd_ppp_get_realm_name(_this->ppp->pppd, _this->ppp);

	pktp1 = pktp;
	if (mes == NULL)
		lmes = 0;
	else
		lmes = strlen(mes);
	lmes = MINIMUM(lmes, lpktp - 1);

	PUTCHAR(lmes, pktp1);
	if (lmes > 0)
		memcpy(pktp1, mes, lmes);
	lpktp = lmes + 1;

	if (authok)
		ppp_output(_this->ppp, PPP_PROTO_PAP, AUTHACK, _this->auth_id,
		    pktp, lpktp);
	else
		ppp_output(_this->ppp, PPP_PROTO_PAP, AUTHNAK, _this->auth_id,
		    pktp, lpktp);

	if (!authok) {
		pap_log(_this, LOG_ALERT,
		    "logtype=Failure username=\"%s\" realm=%s", _this->name,
		    realm);
		pap_stop(_this);
		ppp_set_disconnect_cause(_this->ppp, 
		    PPP_DISCON_AUTH_FAILED, PPP_PROTO_PAP, 1 /* peer */, NULL);
		ppp_stop(_this->ppp, "Authentication Required");
	} else {
		strlcpy(_this->ppp->username, _this->name,
		    sizeof(_this->ppp->username));
		pap_log(_this, LOG_INFO,
		    "logtype=Success username=\"%s\" realm=%s", _this->name,
		    realm);
		pap_stop(_this);
		ppp_auth_ok(_this->ppp);
		/* reset the state to response request of retransmision. */
		_this->state = PAP_STATE_SENT_RESPONSE;
	}
}

static void
pap_local_authenticate(pap *_this, const char *username, const char *password)
{
	int lpassword0;
	char password0[MAX_PASSWORD_LENGTH];

	lpassword0 = sizeof(password0);

	if (npppd_get_user_password(_this->ppp->pppd, _this->ppp, username,
	    password0, &lpassword0) == 0) {
		if (!strcmp(password0, password)) {
			pap_response(_this, 1, DEFAULT_SUCCESS_MESSAGE);
			return;
		}
	}
	pap_response(_this, 0, DEFAULT_FAILURE_MESSAGE);
}

/***********************************************************************
 * Proxy Authentication
 ***********************************************************************/
int
pap_proxy_authen_prepare(pap *_this, dialin_proxy_info *dpi)
{

	PAP_ASSERT(dpi->auth_type == PPP_AUTH_PAP);
	PAP_ASSERT(_this->state == PAP_STATE_INITIAL);

	_this->auth_id = dpi->auth_id;
	if (strlen(dpi->username) >= sizeof(_this->name)) {
		pap_log(_this, LOG_NOTICE,
		    "\"Proxy Authen Name\" is too long.");
		return -1;
	}

	/* copy the authentication properties */
	PAP_ASSERT(_this->ppp->proxy_authen_resp == NULL);
	if ((_this->ppp->proxy_authen_resp = malloc(dpi->lauth_resp + 1)) ==
	    NULL) {
		pap_log(_this, LOG_ERR, "malloc() failed in %s(): %m",
		    __func__);
		return -1;
	}
	memcpy(_this->ppp->proxy_authen_resp, dpi->auth_resp,
	    dpi->lauth_resp);
	_this->ppp->proxy_authen_resp[dpi->lauth_resp] = '\0';
	strlcpy(_this->name, dpi->username, sizeof(_this->name));

	_this->state = PAP_STATE_PROXY_AUTHENTICATION;

	return 0;
}

#ifdef USE_NPPPD_RADIUS
static void
pap_radius_authenticate(pap *_this, const char *username, const char *password)
{
	void *radctx;
	RADIUS_PACKET *radpkt;
	MD5_CTX md5ctx;
	int i, j, s_len, passlen;
	u_char ra[16], digest[16], pass[128];
	const char *s;
	radius_req_setting *rad_setting = NULL;
	char buf0[MAX_USERNAME_LENGTH];

	if ((rad_setting = npppd_get_radius_auth_setting(_this->ppp->pppd,
	    _this->ppp)) == NULL)
		goto fail;

	if ((radpkt = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST))
	    == NULL)
		goto fail;

	if (radius_prepare(rad_setting, _this, &radctx, pap_radius_response)
	    != 0) {
		radius_delete_packet(radpkt);
		goto fail;
	}

	if (ppp_set_radius_attrs_for_authreq(_this->ppp, rad_setting, radpkt)
	    != 0)
		goto fail;

	if (radius_put_string_attr(radpkt, RADIUS_TYPE_USER_NAME,
	    npppd_ppp_get_username_for_auth(_this->ppp->pppd, _this->ppp,
	    username, buf0)) != 0)
		goto fail;

	if (_this->radctx != NULL)
		radius_cancel_request(_this->radctx);

	_this->radctx = radctx;

	/* Create RADIUS User-Password Attribute (RFC 2865, 5.2.) */
	s = radius_get_server_secret(_this->radctx);
	s_len = strlen(s);

	memset(pass, 0, sizeof(pass)); /* null padding */
	passlen = MINIMUM(strlen(password), sizeof(pass));
	memcpy(pass, password, passlen);
	if ((passlen % 16) != 0)
		passlen += 16 - (passlen % 16);

	radius_get_authenticator(radpkt, ra);

	MD5Init(&md5ctx);
	MD5Update(&md5ctx, s, s_len);
	MD5Update(&md5ctx, ra, 16);
	MD5Final(digest, &md5ctx);

	for (i = 0; i < 16; i++)
		pass[i] ^= digest[i];

	while (i < passlen) {
		MD5Init(&md5ctx);
		MD5Update(&md5ctx, s, s_len);
		MD5Update(&md5ctx, &pass[i - 16], 16);
		MD5Final(digest, &md5ctx);

		for (j = 0; j < 16; j++, i++)
			pass[i] ^= digest[j];
	}

	if (radius_put_raw_attr(radpkt, RADIUS_TYPE_USER_PASSWORD, pass,
	    passlen) != 0)
		goto fail;

	radius_request(_this->radctx, radpkt);

	return;
fail:
	if (_this->radctx != NULL)
		radius_cancel_request(_this->radctx);
	pap_log(_this, LOG_ERR, "%s() failed: %m", __func__);
	pap_response(_this, 0, DEFAULT_ERROR_MESSAGE);

	return;
}

static void
pap_radius_response(void *context, RADIUS_PACKET *pkt, int flags,
    RADIUS_REQUEST_CTX reqctx)
{
	int code = -1;
	const char *reason = NULL;
	RADIUS_REQUEST_CTX radctx;
	pap *_this;

	_this = context;
	radctx = _this->radctx;
	_this->radctx = NULL;	/* important */

	if (pkt == NULL) {
		if (flags & RADIUS_REQUEST_TIMEOUT)
			reason = "timeout";
		else if (flags & RADIUS_REQUEST_ERROR)
			reason = strerror(errno);
		else
			reason = "error";
		goto auth_failed;
	}
	code = radius_get_code(pkt);
	if (code == RADIUS_CODE_ACCESS_REJECT) {
		reason="reject";
		goto auth_failed;
	} else if (code != RADIUS_CODE_ACCESS_ACCEPT) {
		reason="error";
		goto auth_failed;
	}
	if ((flags & RADIUS_REQUEST_CHECK_AUTHENTICATOR_OK) == 0 &&
	    (flags & RADIUS_REQUEST_CHECK_AUTHENTICATOR_NO_CHECK) == 0) {
		reason="bad_authenticator";
		goto auth_failed;
	}
	if ((flags & RADIUS_REQUEST_CHECK_MSG_AUTHENTICATOR_OK) == 0 &&
	    (flags & RADIUS_REQUEST_CHECK_NO_MSG_AUTHENTICATOR) == 0) {
		reason="bad_authenticator";
		goto auth_failed;
	}
	/* Authentication succeeded */
	pap_response(_this, 1, DEFAULT_SUCCESS_MESSAGE);
	ppp_process_radius_attrs(_this->ppp, pkt);

	return;
auth_failed:
	/* Authentication failure */
	pap_log(_this, LOG_WARNING, "Radius authentication request failed: %s",
	    reason);
	/* log reply messages from radius server */
	if (pkt != NULL) {
		char radmsg[255], vissed[1024];
		size_t rmlen = 0;
		if ((radius_get_raw_attr(pkt, RADIUS_TYPE_REPLY_MESSAGE,
		    radmsg, &rmlen)) == 0) {
			if (rmlen != 0) {
				strvisx(vissed, radmsg, rmlen, VIS_WHITE);
				pap_log(_this, LOG_WARNING,
				    "Radius reply message: %s", vissed);
			}
		}
	}

	pap_response(_this, 0, DEFAULT_FAILURE_MESSAGE);
}
#endif
