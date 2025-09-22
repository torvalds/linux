/*	$OpenBSD: lcp.c,v 1.18 2019/02/27 04:52:19 denis Exp $ */

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
/* $Id: lcp.c,v 1.18 2019/02/27 04:52:19 denis Exp $ */
/**@file
 * This file provides LCP related functions.
 *<pre>
 * RFC1661: The Point-to-Point Protocol (PPP)
 * RFC1570:  PPP LCP Extensions
 *</pre>
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <net/if_dl.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <event.h>
#include <ctype.h>

#include "npppd.h"
#include "ppp.h"
#include "psm-opt.h"

#define	SPACE	" \t\r\n"

#include "debugutil.h"

#ifdef	LCP_DEBUG
#define	LCP_DBG(x)	fsm_log x
#define	LCP_ASSERT(x)	ASSERT(x)
#else
#define	LCP_DBG(x)
#define	LCP_ASSERT(x)
#endif

#define	PROTREJ			0x08
#define	ECHOREQ			0x09
#define	ECHOREP			0x0a
#define	IDENTIFICATION		0x0c

static void  lcp_resetci(fsm *);
static void  lcp_addci(fsm *, u_char *, int *);
static int   lcp_reqci(fsm *, u_char *, int *, int);
static int   lcp_ackci(fsm *, u_char *, int);
static int   lcp_nakci(fsm *, u_char *, int);
static int   lcp_rejci(fsm *, u_char *, int);
static int   lcp_cilen(fsm *);
static void  lcp_open(fsm *);
static void  lcp_down(fsm *);
static void  lcp_finished(fsm *);
static int   lcp_ext(fsm *, int, int, u_char *, int);
static void  lcp_timeout(void *);
static void  lcp_reset_timeout(void *);
static int   lcp_proxy_recv_ci(fsm *, u_char *, int);
static int   lcp_proxy_sent_ci(fsm *, u_char *, int);
static void  lcp_load_authconfig(fsm *f);

static struct fsm_callbacks lcp_callbacks = {
	lcp_resetci,	/* Reset our Configuration Information */
	lcp_cilen,	/* Length of our Configuration Information */
	lcp_addci,	/* Add our Configuration Information */
	lcp_ackci,	/* ACK our Configuration Information */
	lcp_nakci,	/* NAK our Configuration Information */
	lcp_rejci,	/* Reject our Configuration Information */
	lcp_reqci,	/* Request peer's Configuration Information */
	lcp_open,	/* Called when fsm reaches OPENED state */
	lcp_down,	/* Called when fsm leaves OPENED state */
	NULL,		/* Called when we want the lower layer up */
	lcp_finished,	/* Called when we want the lower layer down */
	NULL,		/* Called when Protocol-Reject received */
	NULL,		/* Retransmission is necessary */
	lcp_ext,	/* Called to handle LCP-specific codes */
	"lcp"		/* String name of protocol */
};
#define	NO_AUTH_AGREEABLE(lcp)	\
    (!psm_opt_is_enabled(lcp, pap) || psm_opt_is_rejected(lcp, pap)) &&	    \
    (!psm_opt_is_enabled(lcp, chap) || psm_opt_is_rejected(lcp, chap)) &&   \
    (!psm_opt_is_enabled(lcp, chapms) || psm_opt_is_rejected(lcp, chapms)) &&\
    (!psm_opt_is_enabled(lcp, chapms_v2) || psm_opt_is_rejected(lcp, chapms_v2)) && \
    (!psm_opt_is_enabled(lcp, eap) || psm_opt_is_rejected(lcp, eap))


/** initializing context for LCP. */
void
lcp_init(lcp *_this, npppd_ppp *ppp)
{
	struct tunnconf *conf;

	fsm_init(&_this->fsm);

	_this->fsm.ppp = ppp;
	_this->fsm.callbacks = &lcp_callbacks;
	_this->fsm.protocol = PPP_PROTO_LCP;
	_this->fsm.flags |= OPT_SILENT;
	_this->timerctx.ctx = _this;

	_this->recv_ress = 0;
	_this->recv_reqs = 0;
	_this->magic_number = arc4random();

	conf = ppp_get_tunnconf(ppp);
	PPP_FSM_CONFIG(&_this->fsm, timeouttime, conf->lcp_timeout);
	PPP_FSM_CONFIG(&_this->fsm, maxconfreqtransmits,
	    conf->lcp_max_configure);
	PPP_FSM_CONFIG(&_this->fsm, maxtermtransmits,
	    conf->lcp_max_terminate);
	PPP_FSM_CONFIG(&_this->fsm, maxnakloops,
	    conf->lcp_max_nak_loop);

	_this->echo_failures = 0;
	if (!conf->lcp_keepalive)
		_this->echo_interval = 0;
	else {
		_this->echo_interval = conf->lcp_keepalive_interval;
		_this->echo_retry_interval = conf->lcp_keepalive_retry_interval;
		_this->echo_max_retries = conf->lcp_keepalive_max_retries;
	}
	_this->auth_order[0] = -1;
}


/**
 * This function is called when HDLC as LCP's lower layer is up.
 */
void
lcp_lowerup(lcp *_this)
{
	fsm_lowerup(&_this->fsm);
	fsm_open(&_this->fsm);

	if (_this->dialin_proxy != 0 &&
	    _this->dialin_proxy_lcp_renegotiation == 0) {
		_this->fsm.state = OPENED;
		lcp_open(&_this->fsm);
	}
}

/**
 * sending Protocol-Reject.
 */
void
lcp_send_protrej(lcp *_this, u_char *pktp, int lpktp)
{
	LCP_ASSERT(_this != NULL);
	LCP_ASSERT(pktp != NULL);

	fsm_sdata(&_this->fsm, PROTREJ, _this->fsm.id++, pktp, lpktp);
}

static const char *
lcp_auth_string(int auth)
{
	switch (auth) {
	case PPP_AUTH_PAP:		return "PAP";
	case PPP_AUTH_CHAP_MD5:		return "MD5-CHAP";
	case PPP_AUTH_CHAP_MS:		return "MS-CHAP";
	case PPP_AUTH_CHAP_MS_V2:	return "MS-CHAP-V2";
	case PPP_AUTH_EAP:		return "EAP";
	case 0:				return "none";
	default:			return "ERROR";
	}
}

static void
lcp_open(fsm *f)
{
	lcp *_this;
	int peer_auth = 0;

	LCP_ASSERT(f != NULL);
	_this = &f->ppp->lcp;

	if (psm_opt_is_accepted(_this, pap))
		peer_auth = PPP_AUTH_PAP;
	else if (psm_opt_is_accepted(_this, chap))
		peer_auth = PPP_AUTH_CHAP_MD5;
	else if (psm_opt_is_accepted(_this, chapms))
		peer_auth = PPP_AUTH_CHAP_MS;
	else if (psm_opt_is_accepted(_this, chapms_v2))
		peer_auth = PPP_AUTH_CHAP_MS_V2;
	else if (psm_opt_is_accepted(_this, eap))
		peer_auth = PPP_AUTH_EAP;
	else {
		if (_this->auth_order[0] > 0) {
			fsm_log(f, LOG_INFO,
			    "failed to negotiate a auth protocol.");
			fsm_close(f, "Authentication is required");
			ppp_set_disconnect_cause(f->ppp,
			    PPP_DISCON_AUTH_PROTOCOL_UNACCEPTABLE, 
			    _this->auth_order[0] /* first one */,
			    1 /* peer refused */, NULL);
			ppp_stop(f->ppp, "Authentication is required");
			return;
		}
	}
	f->ppp->peer_auth = peer_auth;

	if (_this->xxxmru > 0 && f->ppp->peer_mru <= 0)
		f->ppp->peer_mru = _this->xxxmru;
	if (f->ppp->peer_mru <= 0)
		f->ppp->peer_mru = f->ppp->mru;

	/* checking the size of ppp->peer_mru. */
	LCP_ASSERT(f->ppp->peer_mru > 500);

	fsm_log(f, LOG_INFO, "logtype=Opened mru=%d/%d auth=%s magic=%08x/%08x"
	    , f->ppp->mru, f->ppp->peer_mru
	    , lcp_auth_string(peer_auth)
	    , f->ppp->lcp.magic_number, f->ppp->lcp.peer_magic_number
	);
	lcp_reset_timeout(_this);

	ppp_lcp_up(f->ppp);
}

static void
lcp_down(fsm *f)
{
	lcp *_this;

	if (f->ppp->disconnect_code == PPP_DISCON_NO_INFORMATION) {
		/*
		 * disconnect code is set when we are closing the lcp, so
		 * 'no info' means the lcp is going down by peer's termreq.
		 */
		ppp_set_disconnect_cause(f->ppp, PPP_DISCON_NORMAL, 0,
		    1 /* peer */, NULL);
#ifdef USE_NPPPD_RADIUS
		ppp_set_radius_terminate_cause(f->ppp,
		    RADIUS_TERMNATE_CAUSE_USER_REQUEST);
#endif
	}

	_this = &f->ppp->lcp;
	UNTIMEOUT(lcp_timeout, _this);
}

static void
lcp_finished(fsm *f)
{
	ppp_lcp_finished(f->ppp);
}

/**
 * resetting ConfReq.
 */
static void
lcp_resetci(fsm *f)
{
	LCP_ASSERT(f != NULL);

	/* Unless doing dialin-proxy without re-negotiation */
	if (!(f->ppp->lcp.dialin_proxy != 0 &&
	    f->ppp->lcp.dialin_proxy_lcp_renegotiation == 0)) {

		/* Reset the LCP options' state */
		memset(&f->ppp->lcp.opt, 0, sizeof(f->ppp->lcp.opt));
		f->ppp->lcp.auth_order[0] = -1;
	}
}

/**
 * The length of ConfReq.
 */
static int
lcp_cilen(fsm *f)
{
	LCP_ASSERT(f != NULL);
	return f->ppp->mru;
}

/**
 * selecting authentication protocols which is not rejected yet in order
 * of auth_order, and adding Authentication-Protocol options in ConfReq
 * packet area.
 */
static int
lcp_add_auth(fsm *f, u_char **ucpp)
{
	int i;
	u_char *ucp;
	lcp *_this;

	ucp = *ucpp;
	_this = &f->ppp->lcp;

	for (i = 0; _this->auth_order[i] > 0 &&
	    i < countof(_this->auth_order); i++) {
		switch (_this->auth_order[i]) {
		case PPP_AUTH_PAP:
			if (psm_opt_is_rejected(_this, pap))
				break;
			PUTCHAR(PPP_LCP_AUTH_PROTOCOL, ucp);
			PUTCHAR(4, ucp);
			PUTSHORT(PPP_AUTH_PAP, ucp);
			psm_opt_set_requested(_this, pap, 1);
			_this->lastauth = PPP_AUTH_PAP;
			goto end_loop;
		case PPP_AUTH_CHAP_MD5:
			if (psm_opt_is_rejected(_this, chap))
				break;
			PUTCHAR(PPP_LCP_AUTH_PROTOCOL, ucp);
			PUTCHAR(5, ucp);
			PUTSHORT(PPP_AUTH_CHAP, ucp);
			PUTCHAR(PPP_AUTH_CHAP_MD5, ucp);
			psm_opt_set_requested(_this, chap, 1);
			_this->lastauth = PPP_AUTH_CHAP_MD5;
			goto end_loop;
		case PPP_AUTH_CHAP_MS:
			if (psm_opt_is_rejected(_this, chapms))
				break;
			PUTCHAR(PPP_LCP_AUTH_PROTOCOL, ucp);
			PUTCHAR(5, ucp);
			PUTSHORT(PPP_AUTH_CHAP, ucp);
			PUTCHAR(PPP_AUTH_CHAP_MS, ucp);
			psm_opt_set_requested(_this, chapms, 1);
			_this->lastauth = PPP_AUTH_CHAP_MS;
			goto end_loop;
		case PPP_AUTH_CHAP_MS_V2:
			if (psm_opt_is_rejected(_this, chapms_v2))
				break;
			PUTCHAR(PPP_LCP_AUTH_PROTOCOL, ucp);
			PUTCHAR(5, ucp);
			PUTSHORT(PPP_AUTH_CHAP, ucp);
			PUTCHAR(PPP_AUTH_CHAP_MS_V2, ucp);
			psm_opt_set_requested(_this, chapms_v2,1);
			_this->lastauth = PPP_AUTH_CHAP_MS_V2;
			goto end_loop;
                case PPP_AUTH_EAP:
                        if (psm_opt_is_rejected(_this, eap))
                                break;
                        PUTCHAR(PPP_LCP_AUTH_PROTOCOL, ucp);
                        PUTCHAR(4, ucp);
                        PUTSHORT(PPP_AUTH_EAP, ucp);
                        psm_opt_set_requested(_this, eap, 1);
                        _this->lastauth = PPP_AUTH_EAP;
                        goto end_loop;
		}
	}
	_this->lastauth = -1;
	return -1;
end_loop:
	*ucpp = ucp;

	return 0;
}

/**
 * making ConfReq.
 */
static void
lcp_addci(fsm *f, u_char *ucp, int *lenp)
{
	lcp *_this;
	u_char *start_ucp = ucp;

	LCP_ASSERT(f != NULL);

	_this = &f->ppp->lcp;
	if (!psm_opt_is_rejected(_this, mru)) {
		PUTCHAR(PPP_LCP_MRU, ucp);
		PUTCHAR(4, ucp);

		if (_this->xxxmru > 0) {	/* this value is got by Nak. */
			PUTSHORT(_this->xxxmru, ucp);
		} else {
			PUTSHORT(f->ppp->mru, ucp);
		}
		psm_opt_set_requested(_this, mru, 1);
	}
	if (f->ppp->has_acf == 1) {
		if (!psm_opt_is_rejected(_this, pfc)) {
			PUTCHAR(PPP_LCP_PFC, ucp);
			PUTCHAR(2, ucp);
			psm_opt_set_requested(_this, pfc, 1);
		}
		if (!psm_opt_is_rejected(_this, acfc)) {
			PUTCHAR(PPP_LCP_ACFC, ucp);
			PUTCHAR(2, ucp);
			psm_opt_set_requested(_this, acfc, 1);
		}
	}
	PUTCHAR(PPP_LCP_MAGICNUMBER, ucp);
	PUTCHAR(6, ucp);
	PUTLONG(_this->magic_number, ucp);

	if (f->ppp->peer_auth != 0) {
		_this->auth_order[0] = f->ppp->peer_auth;
		_this->auth_order[1] = -1;
	} else if (_this->auth_order[0] < 0) {
		lcp_load_authconfig(f);
	}

	lcp_add_auth(f, &ucp);
	*lenp = ucp - start_ucp;
}

static int
lcp_reqci(fsm *f, u_char *inp, int *lenp, int reject_if_disagree)
{
	uint32_t magic;
	int type, len, rcode, mru, lrej;
	u_char *inp0, *rejbuf, *nakbuf, *nakbuf0;
	lcp *_this;

	_this = &f->ppp->lcp;
	rejbuf = NULL;
	rcode = -1;
	inp0 = inp;
	lrej = 0;

	if ((rejbuf = malloc(*lenp)) == NULL)
		return -1;
	if ((nakbuf0 = malloc(*lenp)) == NULL) {
		free(rejbuf);
		return -1;
	}
	nakbuf = nakbuf0;

#define	remlen()	(*lenp - (inp - inp0))
#define	LCP_OPT_PEER_ACCEPTED(opt)				\
	psm_peer_opt_set_accepted(&f->ppp->lcp, opt, 1);

	f->ppp->lcp.recv_reqs++;

	while (remlen() >= 2) {
		GETCHAR(type, inp);
		GETCHAR(len, inp);
		if (len <= 0 || remlen() + 2 < len)
			goto fail;

		switch (type) {
		case PPP_LCP_MRU:
			if (len != 4)
				goto fail;
			GETSHORT(mru, inp);
			f->ppp->peer_mru = mru;
			if (mru < NPPPD_MIN_MRU) {
				if (reject_if_disagree) {
					inp -= 2;
					goto reject;
				}
				if (lrej > 0) {
				/* if there is a reject, will send Rej, not send Nak. */
				} else {
					inp -= 2;
					memcpy(nakbuf, inp, len);
					nakbuf += len;
					inp += 2;
					PUTSHORT(f->ppp->mru, nakbuf);

					rcode = CONFNAK;
				}
			} else
				LCP_OPT_PEER_ACCEPTED(mru);
			break;
		case PPP_LCP_MAGICNUMBER:
			if (len != 6)
				goto fail;
			GETLONG(magic, inp);
			if (magic == _this->magic_number) {
				inp -= 4;
				goto reject;
			}
			_this->peer_magic_number = magic;
			break;
		case PPP_LCP_PFC:
			if (len != 2)
				goto fail;
			LCP_OPT_PEER_ACCEPTED(pfc);
			break;
		case PPP_LCP_ACFC:
			if (len != 2)
				goto fail;
			LCP_OPT_PEER_ACCEPTED(acfc);
			break;
		case PPP_LCP_AUTH_PROTOCOL:
			/* currently never authenticate. */
		case PPP_LCP_QUALITY_PROTOCOL:	/* not used */
		default:
reject:
			inp -= 2;
			memcpy(rejbuf + lrej, inp, len);
			lrej += len;
			inp += len;
			rcode = CONFREJ;
		}
		continue;
	}
	if (rcode == -1)
		rcode = CONFACK;
fail:
	switch (rcode) {
	case CONFREJ:
		memcpy(inp0, rejbuf, lrej);
		*lenp = lrej;
		break;
	case CONFNAK:
		memcpy(inp0, nakbuf0, nakbuf - nakbuf0);
		*lenp = nakbuf - nakbuf0;
		break;
	}
	if (rcode != CONFACK) {
		psm_peer_opt_set_accepted(&f->ppp->lcp, mru, 0);
		psm_peer_opt_set_accepted(&f->ppp->lcp, pfc, 0);
		psm_peer_opt_set_accepted(&f->ppp->lcp, acfc, 0);
	}
	free(rejbuf);
	free(nakbuf0);

	return rcode;
#undef	remlen
#undef LCP_OPT_PEER_ACCEPTED
}

/** receiving ConfAck. */
static int
lcp_ackci(fsm *f, u_char *inp, int inlen)
{
	int chapalg, authproto, type, len, mru, magic;
	u_char *inp0;

#define	remlen()	(inlen - (inp - inp0))
#define	LCP_OPT_ACCEPTED(opt)				\
	if (!psm_opt_is_requested(&f->ppp->lcp, opt))	\
		goto fail;				\
	psm_opt_set_accepted(&f->ppp->lcp, opt, 1);

	f->ppp->lcp.recv_ress++;
	inp0 = inp;
	while (remlen() >= 2) {
		GETCHAR(type, inp);
		GETCHAR(len, inp);

		if (len <= 0 || remlen() + 2 < len)
			goto fail;

		switch (type) {
		case PPP_LCP_MAGICNUMBER:
			if (len != 6)
				goto fail;
			GETLONG(magic, inp);
			if (f->ppp->lcp.magic_number != magic)
				goto fail;
			break;
		case PPP_LCP_MRU:
			if (len != 4)
				goto fail;
			LCP_OPT_ACCEPTED(mru);
			GETSHORT(mru, inp);
			break;
		case PPP_LCP_AUTH_PROTOCOL:
			if (len < 4)
				goto fail;
			GETSHORT(authproto, inp);
			switch (authproto) {
			case PPP_AUTH_PAP:
				if (len != 4)
					goto fail;
				LCP_OPT_ACCEPTED(pap);
				break;
			case PPP_AUTH_CHAP:
				if (len != 5)
					goto fail;
				GETCHAR(chapalg, inp);
				switch (chapalg) {
				case PPP_AUTH_CHAP_MD5:
					LCP_OPT_ACCEPTED(chap);
					break;
				case PPP_AUTH_CHAP_MS:
					LCP_OPT_ACCEPTED(chapms);
					break;
				case PPP_AUTH_CHAP_MS_V2:
					LCP_OPT_ACCEPTED(chapms_v2);
					break;
				}
				break;
                        case PPP_AUTH_EAP:
                                if (len != 4)
                                     goto fail;
                                LCP_OPT_ACCEPTED(eap);
                                break;
			}
			break;

		/*
		 * As RFC1661, ConfRej must be used for boolean options, but
		 * at least RouterTester uses ConfNak for them.
		 */
		case PPP_LCP_PFC:
			if (len != 2)
				goto fail;
			LCP_OPT_ACCEPTED(pfc);
			break;
		case PPP_LCP_ACFC:
			if (len != 2)
				goto fail;
			LCP_OPT_ACCEPTED(acfc);
			break;

		default:
			goto fail;
		}
	}
	return 1;
fail:
	fsm_log(f, LOG_ERR, "Received unexpected ConfAck.");
	if (debug_get_debugfp() != NULL)
		show_hd(debug_get_debugfp(), inp, remlen());
	return 0;
#undef	LCP_OPT_ACCEPTED
}

/** receiving ConfNak. */
static int
lcp_nakci(fsm *f, u_char *inp, int inlen)
{
	int chapalg, authproto, type, len, mru;
	u_char *inp0;
	lcp *_this;
	const char *peer_auth = "unknown";

#define	remlen()	(inlen - (inp - inp0))
#define	LCP_OPT_REJECTED(opt)				\
	if (!psm_opt_is_requested(&f->ppp->lcp, opt))	\
		goto fail;				\
	psm_opt_set_rejected(&f->ppp->lcp, opt, 1);

	f->ppp->lcp.recv_ress++;
	inp0 = inp;
	_this = &f->ppp->lcp;
	while (remlen() >= 2) {
		GETCHAR(type, inp);
		GETCHAR(len, inp);

		if (len <= 0 || remlen() + 2 < len)
			goto fail;

		switch (type) {
		case PPP_LCP_MRU:
			if (len < 4)
				goto fail;
			GETSHORT(mru, inp);
			fsm_log(f, LOG_NOTICE,
			    "ignored ConfNak from the peer: mru=%d", mru);
			_this->xxxmru = mru;
			break;
		case PPP_LCP_AUTH_PROTOCOL:
			if (len < 4)
				goto fail;
			switch (_this->lastauth) {
			case PPP_AUTH_PAP:
				psm_opt_set_rejected(_this, pap, 1);
				break;
			case PPP_AUTH_CHAP_MD5:
				psm_opt_set_rejected(_this, chap, 1);
				break;
			case PPP_AUTH_CHAP_MS:
				psm_opt_set_rejected(_this, chapms, 1);
				break;
			case PPP_AUTH_CHAP_MS_V2:
				psm_opt_set_rejected(_this, chapms_v2, 1);
				break;
                        case PPP_AUTH_EAP:
                                psm_opt_set_rejected(_this, eap, 1);
                                break;
			}
			GETSHORT(authproto, inp);
			switch (authproto) {
			case PPP_AUTH_PAP:
				if (psm_opt_is_requested(_this, pap))
					psm_opt_set_accepted(_this, pap, 1);
				peer_auth = "pap";
				break;
			case PPP_AUTH_CHAP:
				chapalg = 0;
				if (len == 5)
					GETCHAR(chapalg, inp);
				switch (chapalg) {
				case PPP_AUTH_CHAP_MD5:
					if (psm_opt_is_requested(_this, chap))
						psm_opt_set_accepted(_this,
						    chap, 1);
					peer_auth = "chap";
					break;
				case PPP_AUTH_CHAP_MS:
					if (psm_opt_is_requested(_this, chapms))
						psm_opt_set_accepted(_this,
						    chapms, 1);
					peer_auth = "mschap";
					break;
				case PPP_AUTH_CHAP_MS_V2:
					if (psm_opt_is_requested(_this,
					    chapms_v2))
						psm_opt_set_accepted(_this,
						    chapms_v2, 1);
					peer_auth = "mschap_v2";
					break;
				default:
					fsm_log(f, LOG_INFO,
					    "Nacked chap algorithm is "
					    "unknown(%d).", chapalg);
					peer_auth = "unknown";
					break;
				}
				break;
                        case PPP_AUTH_EAP:
                                if (len != 4)
                                        goto fail;
                                peer_auth = "eap";
				if (psm_opt_is_requested(_this, eap))
					psm_opt_set_accepted(_this, eap, 1);
                                break;
			}
			if (NO_AUTH_AGREEABLE(_this)) {
				fsm_log(f, LOG_INFO, "No authentication "
				    "protocols are agreeable.  peer's "
				    "auth proto=%s",
				    peer_auth);
				ppp_set_disconnect_cause(f->ppp,
				    PPP_DISCON_AUTH_PROTOCOL_UNACCEPTABLE, 
				    authproto, 2 /* couldn't accept peer's */,
				    NULL);
				ppp_stop(f->ppp, "Authentication is required");
				return 1;
			}
			break;
		case PPP_LCP_PFC:
			if (len != 2)
				goto fail;
			LCP_OPT_REJECTED(pfc);
			break;
		case PPP_LCP_ACFC:
			if (len != 2)
				goto fail;
			LCP_OPT_REJECTED(acfc);
			break;
		default:
			goto fail;
		}
	}
	return 1;
fail:
	log_printf(LOG_ERR, "Received unexpected ConfNak.");
	if (debug_get_debugfp() != NULL)
		show_hd(debug_get_debugfp(), inp, inlen);
	return 0;
#undef remlen
#undef LCP_OPT_REJECTED
}

/**
 * receiving ConfRej.
 */
static int
lcp_rejci(fsm *f, u_char *inp, int inlen)
{
	int chapalg, authproto, type, len, mru;
	u_char *inp0;
	lcp *_this;

#define	remlen()	(inlen - (inp - inp0))
#define	LCP_OPT_REJECTED(opt)				\
	if (!psm_opt_is_requested(&f->ppp->lcp, opt))	\
		goto fail;				\
	psm_opt_set_rejected(&f->ppp->lcp, opt, 1);

	f->ppp->lcp.recv_ress++;
	inp0 = inp;
	_this = &f->ppp->lcp;
	while (remlen() >= 2) {
		GETCHAR(type, inp);
		GETCHAR(len, inp);

		if (len <= 0 || remlen() + 2 < len)
			goto fail;

		switch (type) {
		case PPP_LCP_MAGICNUMBER:
			if (f->ppp->lcp.echo_interval > 0)
				goto fail;
			inp += 4;
			break;
		case PPP_LCP_MRU:
			LCP_OPT_REJECTED(mru);
			GETSHORT(mru, inp);
			break;
		case PPP_LCP_AUTH_PROTOCOL:
			if (len < 4)
				goto fail;
			GETSHORT(authproto, inp);
			switch (authproto) {
			case PPP_AUTH_PAP:
                                if (len != 4)
                                        goto fail;
				LCP_OPT_REJECTED(pap);
				break;
			case PPP_AUTH_CHAP:
				chapalg = 0;
				if (len == 5)
					GETCHAR(chapalg, inp);
				switch (chapalg) {
				case PPP_AUTH_CHAP_MD5:
					LCP_OPT_REJECTED(chap);
					break;
				case PPP_AUTH_CHAP_MS:
					LCP_OPT_REJECTED(chapms);
					break;
				case PPP_AUTH_CHAP_MS_V2:
					LCP_OPT_REJECTED(chapms_v2);
					break;
				default:
					fsm_log(f, LOG_INFO,
					    "Rejected chap algorithm is "
					    "unknown(%d).", chapalg);
					break;
				}
				break;
                         case PPP_AUTH_EAP:
                                if (len != 4)
                                        goto fail;
                                LCP_OPT_REJECTED(eap);
                                break;
			}
			if (NO_AUTH_AGREEABLE(_this)) {
				fsm_log(f, LOG_INFO, "No authentication "
				    "protocols are agreeable.");
				ppp_set_disconnect_cause(f->ppp,
				    PPP_DISCON_AUTH_PROTOCOL_UNACCEPTABLE,
				    authproto, 1 /* rejected by peer */, NULL);
				ppp_stop(f->ppp, "Authentication is required");
				return 1;
			}
			break;
		case PPP_LCP_PFC:
			if (len != 2)
				goto fail;
			LCP_OPT_REJECTED(pfc);
			break;
		case PPP_LCP_ACFC:
			if (len != 2)
				goto fail;
			LCP_OPT_REJECTED(acfc);
			break;
		default:
			goto fail;
		}
	}
	return 1;
fail:
	log_printf(LOG_ERR, "Received unexpected ConfRej.");
	if (debug_get_debugfp() != NULL)
		show_hd(debug_get_debugfp(), inp, inlen);
	return 0;
#undef remlen
}

static void
lcp_rcoderej(fsm *f, u_char *inp, int inlen)
{
	uint16_t proto;
	fsm *rejfsm;

	if (inlen < 2) {
		fsm_log(f, LOG_WARNING, "Received short ProtRej packet.");
		return;
	}
	GETSHORT(proto, inp);

	rejfsm = NULL;

	switch (proto) {
	case PPP_PROTO_LCP:
		rejfsm = &f->ppp->lcp.fsm;
		break;
	case PPP_PROTO_PAP:
		fsm_log(f, LOG_WARNING, "our PAP packet is rejected");
		return;
	case PPP_PROTO_CHAP:
		fsm_log(f, LOG_WARNING, "our CHAP packet is rejected");
		return;
        case PPP_PROTO_EAP:
                fsm_log(f, LOG_ERR, "our EAP packet is rejected");
                ppp_stop(f->ppp, "Authentication Required");
                break;
	case PPP_PROTO_NCP | NCP_IPCP:
		rejfsm = &f->ppp->ipcp.fsm;
		break;
	case PPP_PROTO_NCP | NCP_CCP:
		rejfsm = &f->ppp->ccp.fsm;
		break;
	}
	if (rejfsm == NULL) {
		fsm_log(f, LOG_WARNING,
		    "Received ProtRej packet for unknown protocol=(%d/%04x)",
		    proto, proto);
		return;
	}
	fsm_protreject(rejfsm);

	return;
}

static void
lcp_reset_timeout(void *ctx)
{
	lcp *_this;

	_this = ctx;

	if (_this->echo_interval > 0) {
		if (_this->echo_failures == 0) {
			TIMEOUT(lcp_timeout, _this, _this->echo_interval);
		} else {
			TIMEOUT(lcp_timeout, _this, _this->echo_retry_interval);
		}
	} else {
		UNTIMEOUT(lcp_timeout, _this);
	}
}

static void
lcp_timeout(void *ctx)
{
	lcp *_this;
	u_char *cp, buf[32];

	_this = ctx;
	if (_this->echo_failures >= _this->echo_max_retries) {
		fsm_log(&_this->fsm, LOG_NOTICE, "keepalive failure.");
		if (_this->fsm.ppp != NULL) {
#ifdef USE_NPPPD_RADIUS
			ppp_set_radius_terminate_cause(_this->fsm.ppp,
			    RADIUS_TERMNATE_CAUSE_IDLE_TIMEOUT);
#endif
			ppp_stop(_this->fsm.ppp, NULL);
		}
		return;
	}
	cp = buf;
	PUTLONG(_this->magic_number, cp);
	fsm_sdata(&_this->fsm, ECHOREQ, _this->fsm.id++, buf, 4);
	_this->echo_failures++;

	lcp_reset_timeout(_this);
}

static int
lcp_rechoreq(fsm *f, int id, u_char *inp, int inlen)
{
	u_char *inp0;
	lcp *_this;
	int len;

	if (inlen < 4)
		return 0;

	_this = &f->ppp->lcp;
	inp0 = inp;
	PUTLONG(_this->magic_number, inp)

	len = MINIMUM(inlen, f->ppp->peer_mru - 8);
	fsm_sdata(f, ECHOREP, id, inp0, len);

	return 1;
}

static int
lcp_ext(fsm *f, int code, int id, u_char *inp, int inlen)
{
	lcp *_this;
	uint32_t magic;
	char buf[256];
	int i, len;

	_this = &f->ppp->lcp;

	switch (code) {
	case IDENTIFICATION:
		/* RFC 1570 */
		if (inlen > 4) {
			GETLONG(magic, inp);
			inlen -= 4;
			memset(buf, 0, sizeof(buf));
			len = MINIMUM(inlen, sizeof(buf) - 1);
			memcpy(buf, inp, len);
			buf[len] = '\0';
			for (i = 0; i < len; i++) {
				if (!isprint((unsigned char)buf[i]))
					buf[i] = '.';
			}
			fsm_log(f, LOG_INFO,
			    "RecvId magic=%08x text=%s", magic, buf);
		}
		return 1;
	case PROTREJ:
		lcp_rcoderej(f, inp, inlen);
		return 1;
	case ECHOREP:
		if (f->state == OPENED) {
			if (inlen >= 4) {
				GETLONG(magic, inp);
				if (_this->peer_magic_number == magic) {
					_this->echo_failures = 0;
					lcp_reset_timeout(_this);
				}
			}
		}
		return 1;
	case ECHOREQ:
		if (f->state == OPENED)
			return lcp_rechoreq(f, id, inp, inlen);
		return 1;
	}

	return 0;
}


/*
 * reading some authentication settings and storing ppp_order in
 * order of settings.
 */
static void
lcp_load_authconfig(fsm *f)
{
	int              i;
	lcp             *_this;
	struct tunnconf *conf;

	i = 0;
	_this = &f->ppp->lcp;
	conf = ppp_get_tunnconf(f->ppp);
	if ((conf->auth_methods & NPPPD_AUTH_METHODS_MSCHAPV2) != 0) {
		_this->auth_order[i++] = PPP_AUTH_CHAP_MS_V2;
		psm_opt_set_enabled(_this,chapms_v2, 1);
	}
	if ((conf->auth_methods & NPPPD_AUTH_METHODS_CHAP) != 0) {
		_this->auth_order[i++] = PPP_AUTH_CHAP_MD5;
		psm_opt_set_enabled(_this, chap, 1);
	}
	if ((conf->auth_methods & NPPPD_AUTH_METHODS_PAP) != 0) {
		_this->auth_order[i++] = PPP_AUTH_PAP;
		psm_opt_set_enabled(_this, pap, 1);
	}

	_this->auth_order[i] = -1;
}

/***********************************************************************
 * related functions of Dialin Proxy
 **********************************************************************/
/**
 * This function set LCP status following dialin proxy information.
 * This returns non-zero value when LCP status is unacceptable.
 *
 */
int
lcp_dialin_proxy(lcp *_this, dialin_proxy_info *dpi, int renegotiation,
    int force_renegotiation)
{
	int i, authok;

	_this->dialin_proxy = 1;
	lcp_load_authconfig(&_this->fsm);

	/* whether authentication type is permitted by configuration or not. */
	authok = 0;
	if (dpi->auth_type != 0) {
		for (i = 0; _this->auth_order[i] > 0; i++) {
			if (_this->auth_order[i] != dpi->auth_type)
				continue;
			authok = 1;
			break;
		}
	}
	if (!authok) {
		if (!renegotiation) {
			fsm_log(&_this->fsm, LOG_NOTICE,
			    "dialin-proxy failed.  auth-method=%s is "
			    "not enabled.  Try 'l2tp.dialin.lcp_renegotion'",
			    lcp_auth_string(dpi->auth_type));
			return 1;
		}
		_this->dialin_proxy_lcp_renegotiation = 1;
	}
	if (force_renegotiation)
		_this->dialin_proxy_lcp_renegotiation = 1;

	if (_this->dialin_proxy_lcp_renegotiation == 0) {
		_this->fsm.ppp->peer_auth = dpi->auth_type;
		/*
		 * Set the rejected flag to all options here for the moment,
		 * the agreeed options will be handled in lcp_proxy_sent_ci().
		 */
		psm_opt_set_rejected(_this, mru, 1);
		psm_opt_set_rejected(_this, pfc, 1);
		psm_opt_set_rejected(_this, acfc, 1);
		psm_opt_set_rejected(_this, pap, 1);
		psm_opt_set_rejected(_this, chap, 1);
		psm_opt_set_rejected(_this, chapms, 1);
		psm_opt_set_rejected(_this, chapms_v2, 1);
		psm_opt_set_rejected(_this, eap, 1);

	}
	switch (dpi->auth_type) {
	case PPP_AUTH_PAP:
		pap_proxy_authen_prepare(&_this->fsm.ppp->pap, dpi);
		break;
	case PPP_AUTH_CHAP_MD5:
		chap_proxy_authen_prepare(&_this->fsm.ppp->chap, dpi);
		break;
	}
	if (lcp_proxy_sent_ci(&_this->fsm, dpi->last_sent_lcp.data,
	    dpi->last_sent_lcp.ldata) != 0) {
		fsm_log(&_this->fsm, LOG_NOTICE,
		    "dialin-proxy failed.  couldn't use proxied lcp.");
		return 1;
	}
	if (lcp_proxy_recv_ci(&_this->fsm, dpi->last_recv_lcp.data,
	    dpi->last_recv_lcp.ldata) != 0) {
		fsm_log(&_this->fsm, LOG_NOTICE,
		    "dialin-proxy failed.  couldn't use proxied lcp.");
		return 1;
	}

	fsm_log(&_this->fsm, LOG_INFO,
	    "dialin-proxy user=%s auth-type=%s renegotiate=%s",
	    dpi->username,
	    (dpi->auth_type == 0)? "none" : lcp_auth_string(dpi->auth_type),
	    (_this->dialin_proxy_lcp_renegotiation != 0)? "yes" : "no");


	if (_this->dialin_proxy_lcp_renegotiation == 0)
		_this->fsm.flags |= OPT_SILENT;	/* It's ready to be "Opened" */
	else
		_this->fsm.flags &= ~OPT_SILENT;

	return 0;
}

/*
 * This function copies from lcp_reqci. It only differs as follows:
 *	- changes LCP_OPT_ACCEPTED.
 *	- Magic Number and MRU.
 */
static int
lcp_proxy_recv_ci(fsm *f, u_char *inp, int inlen)
{
	int type, mru, len;
	uint32_t magic;
	u_char *inp0;
	lcp *_this;

#define	remlen()	(inlen - (inp - inp0))
#define	LCP_OPT_PEER_ACCEPTED(opt)				\
	psm_peer_opt_set_rejected(&f->ppp->lcp, opt, 0);	\
	psm_peer_opt_set_requested(&f->ppp->lcp, opt, 1);	\
	psm_peer_opt_set_accepted(&f->ppp->lcp, opt, 1);

	_this = &f->ppp->lcp;
	inp0 = inp;

	while (remlen() >= 2) {
		GETCHAR(type, inp);
		GETCHAR(len, inp);
		if (len <= 0 || remlen() + 2 < len)
			goto fail;

		switch (type) {
		case PPP_LCP_MRU:
			if (len != 4)
				goto fail;
			GETSHORT(mru, inp);
			f->ppp->peer_mru = mru;
			if (mru < NPPPD_MIN_MRU)
				goto fail;
			else
				LCP_OPT_PEER_ACCEPTED(mru);
			break;
		case PPP_LCP_MAGICNUMBER:
			if (len != 6)
				goto fail;
			GETLONG(magic, inp);
			if (magic == _this->magic_number)
				goto fail;
			_this->peer_magic_number = magic;
			break;
		case PPP_LCP_PFC:
			if (len != 2)
				goto fail;
			LCP_OPT_PEER_ACCEPTED(pfc);
			break;
		case PPP_LCP_ACFC:
			if (len != 2)
				goto fail;
			LCP_OPT_PEER_ACCEPTED(acfc);
			break;
		case PPP_LCP_ACCM:
			if (len != 6)
				goto fail;
			/* we don't use async framing.  ignore this */
			inp += (len - 2);
			break;
		default:
			goto fail;
		}
	}

#undef remlen
#undef LCP_OPT_PEER_ACCEPTED
	return 0;
fail:
	return 1;
}

/*
 * This function copies from lcp_ackci. It only differs as follows:
 *	- Do not recv_reass++.
 *	- changes LCP_OPT_ACCEPTED.
 *	- Magic Number and MRU.
 */
static int
lcp_proxy_sent_ci(fsm *f, u_char *inp, int inlen)
{
	int chapalg, authproto, type, len, mru, magic;
	u_char *inp0;

#define	remlen()	(inlen - (inp - inp0))
#define	LCP_OPT_ACCEPTED(opt)					\
	if (f->ppp->lcp.dialin_proxy_lcp_renegotiation == 0) {	\
		psm_opt_set_rejected(&f->ppp->lcp, opt, 0);	\
		psm_opt_set_requested(&f->ppp->lcp, opt, 1);	\
		psm_opt_set_accepted(&f->ppp->lcp, opt, 1);	\
	}

	inp0 = inp;
	while (remlen() >= 2) {
		GETCHAR(type, inp);
		GETCHAR(len, inp);

		if (len <= 0 || remlen() + 2 < len)
			goto fail;

		switch (type) {
		case PPP_LCP_MAGICNUMBER:
			if (len != 6)
				goto fail;
			GETLONG(magic, inp);
			f->ppp->lcp.magic_number = magic;
			break;
		case PPP_LCP_MRU:
			if (len != 4)
				goto fail;
			LCP_OPT_ACCEPTED(mru);
			GETSHORT(mru, inp);
			f->ppp->lcp.xxxmru = mru;
			break;
		case PPP_LCP_AUTH_PROTOCOL:
			if (len < 4)
				goto fail;
			GETSHORT(authproto, inp);
			switch (authproto) {
			case PPP_AUTH_PAP:
				if (len != 4)
					goto fail;
				LCP_OPT_ACCEPTED(pap);
				break;
			case PPP_AUTH_CHAP:
				if (len != 5)
					goto fail;
				GETCHAR(chapalg, inp);
				switch (chapalg) {
				case PPP_AUTH_CHAP_MD5:
					LCP_OPT_ACCEPTED(chap);
					break;
				case PPP_AUTH_CHAP_MS:
					LCP_OPT_ACCEPTED(chapms);
					break;
				case PPP_AUTH_CHAP_MS_V2:
					LCP_OPT_ACCEPTED(chapms_v2);
					break;
				}
				break;
                        case PPP_AUTH_EAP:
                                if (len != 4)
                                     goto fail;
                                LCP_OPT_ACCEPTED(eap);
                                break;
			}
			break;
		case PPP_LCP_PFC:
			if (len != 2)
				goto fail;
			LCP_OPT_ACCEPTED(pfc);
			break;
		case PPP_LCP_ACFC:
			if (len != 2)
				goto fail;
			LCP_OPT_ACCEPTED(acfc);
			break;
		case PPP_LCP_ACCM:
			if (len != 6)
				goto fail;
			/* we don't use async framing.  ignore this */
			inp += (len - 2);
			break;
		default:
			goto fail;
		}
	}
	return 0;
fail:
	return 1;
#undef	LCP_OPT_ACCEPTED
}
