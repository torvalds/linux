/*	$OpenBSD: chap.c,v 1.20 2024/07/14 10:52:50 yasuoka Exp $ */

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
/**@file
 * This file provides CHAP (PPP Challenge Handshake Authentication Protocol,
 * RFC 1994) handlers.  Currently this contains authenticator side
 * implementation only.
 *<p>
 * Supported authentication types:
 *  <li>MD5-CHAP</li>
 *  <li>MS-CHAP Version 2 (RFC 2759)</li>
 * </ul></p>
 */
/* RFC 1994, 2433 */
/* $Id: chap.c,v 1.20 2024/07/14 10:52:50 yasuoka Exp $ */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <net/if_dl.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <event.h>
#include <md5.h>
#include <vis.h>

#include "npppd.h"
#include "ppp.h"

#ifdef USE_NPPPD_RADIUS
#include "radius_chap_const.h"
#include "npppd_radius.h"
#endif
#include "npppd_defs.h"

#include "debugutil.h"
#include "chap_ms.h"

#define	HEADERLEN	4

#define	CHAP_STATE_INITIAL		1
#define	CHAP_STATE_SENT_CHALLENGE	2
#define	CHAP_STATE_AUTHENTICATING	3
#define	CHAP_STATE_SENT_RESPONSE	4
#define	CHAP_STATE_STOPPED		5
#define	CHAP_STATE_PROXY_AUTHENTICATION	6

/* retry intervals */
#define	CHAP_TIMEOUT	3
#define	CHAP_RETRY	10

#define	CHAP_CHALLENGE	1
#define	CHAP_RESPONSE	2
#define	CHAP_SUCCESS	3
#define	CHAP_FAILURE	4

/* from RFC 2433 */
#define	ERROR_RESTRICTED_LOGIN_HOURS		646
#define	ERROR_ACCT_DISABLED			647
#define	ERROR_PASSWD_EXPIRED			648
#define	ERROR_NO_DIALIN_PERMISSION		649
#define	ERROR_AUTHENTICATION_FAILURE		691
#define	ERROR_CHANGING_PASSWORD			709

/*  MprError.h */
#define	ERROR_AUTH_SERVER_TIMEOUT		930

#ifdef	CHAP_DEBUG
#define	CHAP_DBG(x)	chap_log x
#define	CHAP_ASSERT(cond)					\
	if (!(cond)) {						\
	    fprintf(stderr,					\
		"\nASSERT(" #cond ") failed on %s() at %s:%d.\n"\
		, __func__, __FILE__, __LINE__);		\
	    abort(); 						\
	}
#else
#define	CHAP_ASSERT(cond)
#define	CHAP_DBG(x)
#endif

static void        chap_authenticate(chap *_this, u_char *, int);
static void        chap_failure(chap *, const char *, int);
static void        chap_response (chap *, int, u_char *, int);
static void        chap_create_challenge (chap *);
static void        chap_send_error (chap *, const char *);
static void        md5chap_authenticate (chap *, int, char *, u_char *, int, u_char *);
static void        mschapv2_send_error (chap *, int, int);
static void        mschapv2_authenticate (chap *, int, char *, u_char *, int, u_char *);
#ifdef USE_NPPPD_RADIUS
static void        chap_radius_authenticate (chap *, int, char *, u_char *, int, u_char *);
static void        chap_radius_response (void *, RADIUS_PACKET *, int, RADIUS_REQUEST_CTX);
#endif
static char       *strip_nt_domain (char *);
static void        chap_log (chap *, uint32_t, const char *, ...) __printflike(3,4);

/** Initialize the CHAP */
void
chap_init(chap *_this, npppd_ppp *ppp)
{
	struct tunnconf *conf;

	CHAP_ASSERT(ppp != NULL);
	CHAP_ASSERT(_this != NULL);

	memset(_this, 0, sizeof(chap));
	_this->ppp = ppp;

	conf = ppp_get_tunnconf(ppp);

	if (conf->chap_name == NULL)
		gethostname(_this->myname, sizeof(_this->myname));
	else
		strlcpy(_this->myname, conf->chap_name, sizeof(_this->myname));

	_this->timerctx.ctx = _this;
	_this->state = CHAP_STATE_INITIAL;

	_this->ntry = CHAP_RETRY;
}

/** Start CHAP as a authenticator.  Send a challenge */
void
chap_start(chap *_this)
{
	u_char *challp, *challp0;
	int lmyname;

	CHAP_ASSERT(_this != NULL);
	CHAP_ASSERT(_this->ppp != NULL);

	if (_this->state == CHAP_STATE_PROXY_AUTHENTICATION) {
		_this->type = PPP_AUTH_CHAP_MD5;
		_this->state = CHAP_STATE_AUTHENTICATING;
		chap_authenticate(_this, _this->ppp->proxy_authen_resp,
		    _this->ppp->lproxy_authen_resp);
		return;
	}

	if (_this->state == CHAP_STATE_INITIAL ||
	    _this->state == CHAP_STATE_SENT_CHALLENGE) {
		if (_this->ntry > 0) {
			_this->ntry--;
			_this->type = _this->ppp->peer_auth;

			/* The type is supported? */
			if (_this->type != PPP_AUTH_CHAP_MS_V2 &&
			    _this->type != PPP_AUTH_CHAP_MD5) {
				chap_log(_this, LOG_ALERT,
				    "Requested authentication type(0x%x) "
				    "is not supported.", _this->type);
				ppp_set_disconnect_cause(_this->ppp, 
				    PPP_DISCON_AUTH_PROTOCOL_UNACCEPTABLE,
				    PPP_PROTO_CHAP, 2 /* local */, NULL);
				ppp_stop(_this->ppp, "Authentication Required");
				return;
			}


#ifdef USE_NPPPD_MPPE
			/* The peer must use MS-CHAP-V2 as the type */
			if (MPPE_IS_REQUIRED(_this->ppp) &&
			    _this->type != PPP_AUTH_CHAP_MS_V2) {
				chap_log(_this, LOG_ALERT,
				    "mppe is required but try to start chap "
				    "type=0x%02x", _this->type);
				ppp_set_disconnect_cause(_this->ppp,
				    PPP_DISCON_AUTH_PROTOCOL_UNACCEPTABLE,
				    PPP_PROTO_CHAP, 2 /* local */, NULL);
				ppp_stop(_this->ppp, "Authentication Required");
				return;
			}
#endif
			/* Generate a challenge packet and send it */
			challp = ppp_packetbuf(_this->ppp, PPP_AUTH_CHAP);
			challp += HEADERLEN;
			challp0 = challp;

			chap_create_challenge(_this);

			PUTCHAR(_this->lchall, challp);
			memcpy(challp, &_this->chall, _this->lchall);
			challp += _this->lchall;

			lmyname = strlen(_this->myname);

			memcpy(challp, _this->myname, lmyname);
			challp += lmyname;

			_this->challid = ++_this->pktid;

			ppp_output(_this->ppp, PPP_PROTO_CHAP, CHAP_CHALLENGE,
			    _this->challid, challp0, challp - challp0);

			_this->state = CHAP_STATE_SENT_CHALLENGE;

			TIMEOUT((void (*)(void *))chap_start, _this,
			    CHAP_TIMEOUT);
		} else {
			chap_log(_this, LOG_INFO,
			    "Client did't respond our challenage.");
			ppp_set_disconnect_cause(_this->ppp, 
			    PPP_DISCON_AUTH_FSM_TIMEOUT,
			    PPP_PROTO_CHAP, 0, NULL);
			ppp_stop(_this->ppp, "Authentication Required");
		}
	}
}

/** Stop the CHAP */
void
chap_stop(chap *_this)
{
	_this->state = CHAP_STATE_STOPPED;
	UNTIMEOUT(chap_start, _this);
#ifdef USE_NPPPD_RADIUS
	if (_this->radctx != NULL) {
		radius_cancel_request(_this->radctx);
		_this->radctx = NULL;
	}
#endif
}

/** Called when a CHAP packet is received. */
void
chap_input(chap *_this, u_char *pktp, int len)
{
	int code, id, length, lval, lname, authok;
	u_char *pktp1, *val, namebuf[MAX_USERNAME_LENGTH];
	char *name;

	if (_this->state == CHAP_STATE_STOPPED ||
	    _this->state == CHAP_STATE_INITIAL) {
		chap_log(_this, LOG_INFO, "Received chap packet.  But chap is "
		    "not started");
		return;
	}

	CHAP_ASSERT(_this != NULL);
	if (len < 4) {
		chap_log(_this, LOG_ERR, "%s: Received broken packet.",
		    __func__);
		return;
	}

	pktp1 = pktp;

	GETCHAR(code, pktp1);
	GETCHAR(id, pktp1);
	GETSHORT(length, pktp1);
	if (len < length || len < 5) {
		chap_log(_this, LOG_ERR, "%s: Received broken packet.",
		    __func__);
		return;
	}

	if (code != CHAP_RESPONSE) {
		chap_log(_this, LOG_ERR, "Received unknown code=%d", code);
		return;
	}

	/* Create a chap response */

	if (id != _this->challid) {
		chap_log(_this, LOG_ERR,
		    "Received challenge response has unknown id.");
		return;
	}
	if (_this->state == CHAP_STATE_AUTHENTICATING)
		return;

	authok = 0;
	UNTIMEOUT(chap_start, _this);

	/* pick the username */
	GETCHAR(lval, pktp1);
	val = pktp1;
	pktp1 += lval;

	if (lval > length) {
		chap_log(_this, LOG_ERR,
		    "Received challenge response has invalid Value-Size "
		    "field. %d", lval);
		return;
	}
	name = pktp1;
	lname = len - (pktp1 - pktp);
	if (lname <= 0 || sizeof(namebuf) <= lname + 1) {
		chap_log(_this, LOG_ERR,
		    "Received challenge response has invalid Name "
		    "field.");
		return;
	}
	memcpy(namebuf, name, lname);
	namebuf[lname] = '\0';
	name = namebuf;
	if (_this->state == CHAP_STATE_SENT_RESPONSE) {
		if (strcmp(_this->name, name) != 0) {
			/*
			 * The peer requests us to resend, but the username
			 * has been changed.
			 */
			chap_log(_this, LOG_ERR,
			    "Received AuthReq is not same as before.  "
			    "%s != %s", name, _this->name);
			return;
		}
	} else if (_this->state != CHAP_STATE_SENT_CHALLENGE) {
		chap_log(_this, LOG_ERR,
		    "Received AuthReq in illegal state.  username=%s", name);
		return;
	}
	_this->state = CHAP_STATE_AUTHENTICATING;
	strlcpy(_this->name, name, sizeof(_this->name));

	chap_authenticate(_this, val, lval);
}

static void
chap_failure(chap *_this, const char *msg, int mschapv2err)
{

	switch(_this->type) {
	case PPP_AUTH_CHAP_MD5:
		chap_send_error(_this, "FAILED");
		break;
	case PPP_AUTH_CHAP_MS_V2:
		mschapv2_send_error(_this, mschapv2err, 0);
		break;
	}
}

static void
chap_authenticate(chap *_this, u_char *response, int lresponse)
{

	switch(_this->type) {
	case PPP_AUTH_CHAP_MD5:
		/* check the length */
		if (lresponse != 16) {
			chap_log(_this, LOG_ERR,
			    "Invalid response length %d != 16", lresponse);
			chap_failure(_this, "FAILED",
			    ERROR_AUTHENTICATION_FAILURE);
			return;
		}
		break;
	case PPP_AUTH_CHAP_MS_V2:
		/* check the length */
		if (lresponse < 49) {
			chap_log(_this, LOG_ERR, "Packet too short.");
			chap_failure(_this, "FAILED",
			    ERROR_AUTHENTICATION_FAILURE);
			return;
		}
		break;
	}
	if (npppd_ppp_bind_realm(_this->ppp->pppd, _this->ppp, _this->name, 0)
	    == 0) {
		if (!npppd_ppp_is_realm_ready(_this->ppp->pppd, _this->ppp)) {
			chap_log(_this, LOG_INFO,
			    "username=\"%s\" realm is not ready.", _this->name);
			chap_failure(_this, "FAILED",
			    ERROR_AUTH_SERVER_TIMEOUT);
			return;
		}
#ifdef USE_NPPPD_RADIUS
		if (npppd_ppp_is_realm_radius(_this->ppp->pppd, _this->ppp)) {
			chap_radius_authenticate(_this, _this->challid,
			    _this->name, _this->chall, _this->lchall, response);
			return;
			/* NOTREACHED */
		} else
#endif
		if (npppd_ppp_is_realm_local(_this->ppp->pppd, _this->ppp)) {
			switch(_this->type) {
			case PPP_AUTH_CHAP_MD5:
				md5chap_authenticate(_this, _this->challid,
				    _this->name, _this->chall, _this->lchall,
				    response);
				return;
				/* NOTREACHED */
			case PPP_AUTH_CHAP_MS_V2:
				mschapv2_authenticate(_this, _this->challid,
				    strip_nt_domain(_this->name),
				    _this->chall, _this->lchall, response);
				return;
				/* NOTREACHED */
			}
		}
	}
	chap_failure(_this, "FAILED", ERROR_AUTHENTICATION_FAILURE);

	return;
}

static void
chap_response(chap *_this, int authok, u_char *pktp, int lpktp)
{
	const char *realm_name;

	CHAP_ASSERT(_this != NULL);
	CHAP_ASSERT(pktp != NULL);
	CHAP_ASSERT(_this->type == PPP_AUTH_CHAP_MD5 ||
	    _this->type == PPP_AUTH_CHAP_MS_V2);

	ppp_output(_this->ppp, PPP_PROTO_CHAP, (authok)? 3 : 4, _this->challid,
	    pktp, lpktp);

	realm_name = npppd_ppp_get_realm_name(_this->ppp->pppd, _this->ppp);
	if (!authok) {
		chap_log(_this, LOG_ALERT,
		    "logtype=Failure username=\"%s\" realm=%s", _this->name,
		    realm_name);
		chap_stop(_this);
		/* Stop the PPP if the authentication is failed. */
		ppp_set_disconnect_cause(_this->ppp,
		    PPP_DISCON_AUTH_FAILED, PPP_PROTO_CHAP, 1 /* peer */, NULL);
		ppp_stop(_this->ppp, "Authentication Required");
	} else {
		strlcpy(_this->ppp->username, _this->name,
		    sizeof(_this->ppp->username));
		chap_log(_this, LOG_INFO,
		    "logtype=Success username=\"%s\" "
		    "realm=%s", _this->name, realm_name);
		chap_stop(_this);
		/* We change our state to prepare to resend requests. */
		_this->state = CHAP_STATE_SENT_RESPONSE;
		ppp_auth_ok(_this->ppp);
	}
}

/** Generate a challenge */
static void
chap_create_challenge(chap *_this)
{
	CHAP_ASSERT(_this->ppp->peer_auth == PPP_AUTH_CHAP_MS_V2 ||
	    _this->ppp->peer_auth == PPP_AUTH_CHAP_MD5);

	_this->lchall = 16;
	arc4random_buf(_this->chall, _this->lchall);
}

/***********************************************************************
 * Proxy Authentication
 ***********************************************************************/
int
chap_proxy_authen_prepare(chap *_this, dialin_proxy_info *dpi)
{

	CHAP_ASSERT(dpi->auth_type == PPP_AUTH_CHAP_MD5);
	CHAP_ASSERT(_this->state == CHAP_STATE_INITIAL);

	_this->pktid = dpi->auth_id;

#ifdef USE_NPPPD_MPPE
	if (MPPE_IS_REQUIRED(_this->ppp) &&
	    _this->type != PPP_AUTH_CHAP_MS_V2) {
		chap_log(_this, LOG_ALERT,
		    "mppe is required but try to start chap "
		    "type=0x%02x", dpi->auth_type);
		return -1;
	}
#endif
	/* authentication */
	if (strlen(dpi->username) >= sizeof(_this->name)) {
		chap_log(_this, LOG_NOTICE,
		    "\"Proxy Authen Name\" is too long.");
		return -1;
	}
	if (dpi->lauth_chall >= sizeof(_this->chall)) {
		chap_log(_this, LOG_NOTICE,
		    "\"Proxy Authen Challenge\" is too long.");
		return -1;
	}

	/* copy the authentication properties */
	CHAP_ASSERT(_this->ppp->proxy_authen_resp == NULL);
	if ((_this->ppp->proxy_authen_resp = malloc(dpi->lauth_resp)) ==
	    NULL) {
		chap_log(_this, LOG_ERR, "malloc() failed in %s(): %m",
		    __func__);
		return -1;
	}
	memcpy(_this->ppp->proxy_authen_resp, dpi->auth_resp,
	    dpi->lauth_resp);
	_this->ppp->lproxy_authen_resp = dpi->lauth_resp;

	_this->challid = dpi->auth_id;
	strlcpy(_this->name, dpi->username, sizeof(_this->name));

	memcpy(_this->chall, dpi->auth_chall, dpi->lauth_chall);
	_this->lchall = dpi->lauth_chall;

	_this->state = CHAP_STATE_PROXY_AUTHENTICATION;

	return 0;
}

/************************************************************************
 * Functions for MD5-CHAP(RFC1994)
 ************************************************************************/
static void
md5chap_authenticate(chap *_this, int id, char *username, u_char *challenge,
    int lchallenge, u_char *response)
{
	MD5_CTX md5ctx;
	int rval, passlen;
	u_char digest[16];
	char idpass[1 + MAX_PASSWORD_LENGTH + 1];

	idpass[0] = id;
	passlen = MAX_PASSWORD_LENGTH;
	rval = npppd_get_user_password(_this->ppp->pppd, _this->ppp, username,
	    idpass + 1, &passlen);

	if (rval != 0) {
		switch (rval) {
		case 1:
			chap_log(_this, LOG_INFO,
			    "username=\"%s\" user unknown", username);
			break;
		default:
			chap_log(_this, LOG_ERR,
			    "username=\"%s\" generic error", username);
			break;
		}
		goto auth_failed;
	}
	passlen = strlen(idpass + 1);
	MD5Init(&md5ctx);
	MD5Update(&md5ctx, idpass, 1 + passlen);
	MD5Update(&md5ctx, challenge, lchallenge);
	MD5Final(digest, &md5ctx);

	if (memcmp(response, digest, 16) == 0) {
		chap_response(_this, 1, "OK", 2);
		return;
	}
	/* FALLTHROUGH.  The password are not matched */
auth_failed:
	/* No extra information, just "FAILED" */
	chap_send_error(_this, "FAILED");

	return;
}

static void
chap_send_error(chap *_this, const char *msg)
{
	u_char *pkt, *challenge;
	int lpkt;

	challenge = _this->chall;

	pkt = ppp_packetbuf(_this->ppp, PPP_PROTO_CHAP) + HEADERLEN;
	lpkt = _this->ppp->mru - HEADERLEN;

	strlcpy(pkt, msg, lpkt);
	lpkt = strlen(msg);

	chap_response(_this, 0, pkt, lpkt);
}

/************************************************************************
 * Functions for MS-CHAP-V2(RFC 2759)
 ************************************************************************/
static void
mschapv2_send_error(chap *_this, int error, int can_retry)
{
	u_char *pkt, *challenge;
	int lpkt;

	challenge = _this->chall;

	pkt = ppp_packetbuf(_this->ppp, PPP_PROTO_CHAP) + HEADERLEN;
	lpkt = _this->ppp->mru - HEADERLEN;

	/*
	 * We don't use "M=<msg>"
	 *  - pppd on Mac OS 10.4 hungs up if it received a failure packet
	 *    with "M=<msg>".
	 *  - RRAS on windows server 2003 never uses "M=".
	 */
	snprintf(pkt, lpkt, "E=%d R=%d C=%02x%02x%02x%02x%02x%02x%02x%02x"
	    "%02x%02x%02x%02x%02x%02x%02x%02x V=3", error, can_retry,
	    challenge[0], challenge[1], challenge[2], challenge[3],
	    challenge[4], challenge[5], challenge[6], challenge[7],
	    challenge[8], challenge[9], challenge[10], challenge[11],
	    challenge[12], challenge[13], challenge[14], challenge[15]
	);
	lpkt = strlen(pkt);

	chap_response(_this, 0, pkt, lpkt);
}

static void
mschapv2_authenticate(chap *_this, int id, char *username, u_char *challenge,
    int lchallenge, u_char *response)
{
	int i, rval, passlen, lpkt;
	u_char *pkt;
	char password[MAX_PASSWORD_LENGTH * 2], ntresponse[24];
#ifdef	USE_NPPPD_MPPE
	char pwdhash[16], pwdhashhash[16];
#endif

	CHAP_DBG((_this, LOG_DEBUG, "%s()", __func__));
	pkt = ppp_packetbuf(_this->ppp, PPP_PROTO_CHAP) + HEADERLEN;
	lpkt = _this->ppp->mru - HEADERLEN;

	passlen = sizeof(password) / 2;
	rval = npppd_get_user_password(_this->ppp->pppd, _this->ppp, username,
	    password, &passlen);

	if (rval != 0) {
		switch (rval) {
		case 1:
			chap_log(_this, LOG_INFO,
			    "username=\"%s\" user unknown", username);
			break;
		default:
			chap_log(_this, LOG_ERR,
			    "username=\"%s\" generic error", username);
			break;
		}
		goto auth_failed;
	}

	/* Convert the string charset from ASCII to UTF16-LE */
	passlen = strlen(password);
	for (i = passlen - 1; i >= 0; i--) {
		password[i*2] = password[i];
		password[i*2+1] = 0;
	}

	mschap_nt_response(challenge, response, username, strlen(username),
		    password, passlen * 2, ntresponse);

	if (memcmp(ntresponse, response + 24, 24) != 0) {
		chap_log(_this, LOG_INFO,
		    "username=\"%s\" password mismatch.", username);
		goto auth_failed;
	}

    /*
     * Authentication succeed
     */
	CHAP_DBG((_this, LOG_DEBUG, "%s() OK", __func__));

	mschap_auth_response(password, passlen * 2, ntresponse,
	    challenge, response, username, strlen(username), pkt);
	lpkt = 42;
#ifdef	USE_NPPPD_MPPE
	if (_this->ppp->mppe.enabled != 0) {
		mschap_ntpassword_hash(password, passlen * 2, pwdhash);
		mschap_ntpassword_hash(pwdhash, sizeof(pwdhash), pwdhashhash);

		mschap_masterkey(pwdhashhash, ntresponse,
		    _this->ppp->mppe.master_key);
		mschap_asymetric_startkey(_this->ppp->mppe.master_key,
		    _this->ppp->mppe.recv.master_key, MPPE_KEYLEN, 0, 1);
		mschap_asymetric_startkey(_this->ppp->mppe.master_key,
		    _this->ppp->mppe.send.master_key, MPPE_KEYLEN, 1, 1);
	}
#endif
	chap_response(_this, 1, pkt, lpkt);

	return;
auth_failed:
	/* No extra information */
	mschapv2_send_error(_this, ERROR_AUTHENTICATION_FAILURE, 0);

	return;
}

#ifdef USE_NPPPD_RADIUS
/************************************************************************
 * Functions for RADIUS
 * RFC 2058: RADIUS
 * RFC 2548: Microsoft Vendor-specific RADIUS Attributes
 ************************************************************************/
static void
chap_radius_authenticate(chap *_this, int id, char *username,
    u_char *challenge, int lchallenge, u_char *response)
{
	void *radctx;
	RADIUS_PACKET *radpkt;
	radius_req_setting *rad_setting;
	int lpkt;
	u_char *pkt;
	char buf0[MAX_USERNAME_LENGTH];

	radpkt = NULL;
	radctx = NULL;

	if ((rad_setting = npppd_get_radius_auth_setting(_this->ppp->pppd,
	    _this->ppp)) == NULL) {
		goto fail;	/* no radius server */
	}
	pkt = ppp_packetbuf(_this->ppp, PPP_PROTO_CHAP) + HEADERLEN;
	lpkt = _this->ppp->mru - HEADERLEN;

	if ((radpkt = radius_new_request_packet(RADIUS_CODE_ACCESS_REQUEST))
	    == NULL)
		goto fail;
	if (radius_prepare(rad_setting, _this, &radctx, chap_radius_response)
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

	switch (_this->type) {
	case PPP_AUTH_CHAP_MD5:
	    {
		u_char md5response[17];

		md5response[0] = _this->challid;
		memcpy(&md5response[1], response, 16);
		if (radius_put_raw_attr(radpkt,
		    RADIUS_TYPE_CHAP_PASSWORD, md5response, 17) != 0)
			goto fail;
		if (radius_put_raw_attr(radpkt,
		    RADIUS_TYPE_CHAP_CHALLENGE, challenge, lchallenge) != 0)
			goto fail;
		break;
	    }
	case PPP_AUTH_CHAP_MS_V2:
	    {
		struct RADIUS_MS_CHAP2_RESPONSE msresponse;

		/* Preparing RADIUS_MS_CHAP2_RESPONSE  */
		memset(&msresponse, 0, sizeof(msresponse));
		msresponse.ident = id;
		msresponse.flags = response[48];
		memcpy(&msresponse.peer_challenge, response, 16);
		memcpy(&msresponse.response, response + 24, 24);

		if (radius_put_vs_raw_attr(radpkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP_CHALLENGE, challenge, 16) != 0)
			goto fail;
		if (radius_put_vs_raw_attr(radpkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP2_RESPONSE, &msresponse,
		    sizeof(msresponse)) != 0)
			goto fail;
		break;
	    }

	}
	radius_get_authenticator(radpkt, _this->authenticator);

	/* Cancel previous request */
	if (_this->radctx != NULL)
		radius_cancel_request(_this->radctx);

	/* Send a request */
	_this->radctx = radctx;
	radius_request(radctx, radpkt);

	return;
fail:
	switch (_this->type) {
	case PPP_AUTH_CHAP_MD5:
		/* No extra information, just "FAILED" */
		chap_send_error(_this, "FAILED");
		break;
	case PPP_AUTH_CHAP_MS_V2:
		/* No extra information */
		mschapv2_send_error(_this, ERROR_AUTHENTICATION_FAILURE, 0);
		break;
	}
	if (radctx != NULL)
		radius_cancel_request(radctx);
}

static void
chap_radius_response(void *context, RADIUS_PACKET *pkt, int flags,
    RADIUS_REQUEST_CTX reqctx)
{
	int code, lrespkt;
	const char *secret, *reason = "";
	chap *_this;
	u_char *respkt, *respkt0;
	int errorCode;
	RADIUS_REQUEST_CTX radctx;

	CHAP_ASSERT(context != NULL);

	reason = "";
	errorCode = ERROR_AUTH_SERVER_TIMEOUT;
	_this = context;
	secret = radius_get_server_secret(_this->radctx);
	radctx = _this->radctx;
	_this->radctx = NULL;	/* IMPORTANT */

	respkt = respkt0 = ppp_packetbuf(_this->ppp, PPP_PROTO_CHAP)
	    + HEADERLEN;
	lrespkt = _this->ppp->mru - HEADERLEN;
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
		errorCode = ERROR_AUTHENTICATION_FAILURE;
		/* Windows peer will reset the password by this error code */
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
		reason="bad_msg_authenticator";
		goto auth_failed;
	}
	/*
	 * Authentication OK
	 */
	switch (_this->type) {
	case PPP_AUTH_CHAP_MD5:
	    chap_response(_this, 1, "OK", 2);
	    break;
	case PPP_AUTH_CHAP_MS_V2:
	    {
		struct RADIUS_MS_CHAP2_SUCCESS success;
#ifdef USE_NPPPD_MPPE
		struct RADIUS_MPPE_KEY sendkey, recvkey;
#endif
		size_t len;

		len = sizeof(success);
		if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_CHAP2_SUCCESS, &success, &len) != 0) {
			chap_log(_this, LOG_ERR, "no ms_chap2_success");
			goto auth_failed;
		}
#ifdef	USE_NPPPD_MPPE
		if (_this->ppp->mppe.enabled != 0) {
			len = sizeof(sendkey);
			if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
			    RADIUS_VTYPE_MPPE_SEND_KEY, &sendkey, &len) != 0) {
				chap_log(_this, LOG_ERR, "no mppe_send_key");
				goto auth_failed;
			}
			len = sizeof(recvkey);
			if (radius_get_vs_raw_attr(pkt, RADIUS_VENDOR_MICROSOFT,
			    RADIUS_VTYPE_MPPE_RECV_KEY, &recvkey, &len) != 0) {
				chap_log(_this, LOG_ERR, "no mppe_recv_key");
				goto auth_failed;
			}

			mschap_radiuskey(_this->ppp->mppe.send.master_key,
			    sendkey.salt, _this->authenticator, secret);

			mschap_radiuskey(_this->ppp->mppe.recv.master_key,
			    recvkey.salt, _this->authenticator, secret);
		}
#endif
		chap_response(_this, 1, success.str, sizeof(success.str));
		break;
	    }
	}
	ppp_process_radius_attrs(_this->ppp, pkt);

	return;
auth_failed:
	chap_log(_this, LOG_WARNING, "Radius authentication request failed: %s",
	    reason);
	/* log reply messages from radius server */
	if (pkt != NULL) {
		char radmsg[255], vissed[1024];
		size_t rmlen = 0;
		if ((radius_get_raw_attr(pkt, RADIUS_TYPE_REPLY_MESSAGE,
		    radmsg, &rmlen)) == 0) {
			if (rmlen != 0) {
				strvisx(vissed, radmsg, rmlen, VIS_WHITE);
				chap_log(_this, LOG_WARNING,
				    "Radius reply message: %s", vissed);
			}
		}
	}

	/* No extra information */
	chap_failure(_this, "FAILED", errorCode);
}

#endif

/************************************************************************
 * Miscellaneous functions
 ************************************************************************/
static char *
strip_nt_domain(char *username)
{
	char *lastbackslash;

	if ((lastbackslash = strrchr(username, '\\')) != NULL)
		return lastbackslash + 1;

	return username;
}

static void
chap_log(chap *_this, uint32_t prio, const char *fmt, ...)
{
	const char *protostr;
	char logbuf[BUFSIZ];
	va_list ap;

	CHAP_ASSERT(_this != NULL);
	CHAP_ASSERT(_this->ppp != NULL);

	switch (_this->type) {
	case PPP_AUTH_CHAP_MD5:
		protostr = "chap";
		break;
	case PPP_AUTH_CHAP_MS_V2:
		protostr = "mschap_v2";
		break;
	default:
		protostr = "unknown";
		break;
	}

	va_start(ap, fmt);
	snprintf(logbuf, sizeof(logbuf), "ppp id=%u layer=chap proto=%s %s",
	    _this->ppp->id, protostr, fmt);
	vlog_printf(prio, logbuf, ap);
	va_end(ap);
}
