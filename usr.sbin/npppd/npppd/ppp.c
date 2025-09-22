/*	$OpenBSD: ppp.c,v 1.33 2025/02/03 08:26:51 yasuoka Exp $ */

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
/* $Id: ppp.c,v 1.33 2025/02/03 08:26:51 yasuoka Exp $ */
/**@file
 * This file provides PPP(Point-to-Point Protocol, RFC 1661) and
 * {@link :: _npppd_ppp PPP instance} related functions.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if_dl.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <sys/time.h>
#include <time.h>
#include <event.h>

#include "npppd.h"
#include "time_utils.h"
#include "ppp.h"
#include "psm-opt.h"
#ifdef USE_NPPPD_RADIUS
#include <radius.h>
#include "npppd_radius.h"
#endif

#include "debugutil.h"

#ifdef	PPP_DEBUG
#define	PPP_DBG(x)	ppp_log x
#define	PPP_ASSERT(cond)					\
	if (!(cond)) {						\
	    fprintf(stderr,					\
		"\nASSERT(" #cond ") failed on %s() at %s:%d.\n"\
		, __func__, __FILE__, __LINE__);		\
	    abort(); 						\
	}
#else
#define	PPP_ASSERT(cond)
#define	PPP_DBG(x)
#endif

static u_int ppp_seq = 0;

static void             ppp_stop0 (npppd_ppp *);
static int              ppp_recv_packet (npppd_ppp *, unsigned char *, int, int);
static const char      *ppp_peer_auth_string (npppd_ppp *);
static void             ppp_idle_timeout (int, short, void *);
#ifdef USE_NPPPD_PIPEX
static void             ppp_on_network_pipex(npppd_ppp *);
#endif
static uint32_t         ppp_proto_bit(int);

#define AUTH_IS_PAP(ppp) 	((ppp)->peer_auth == PPP_AUTH_PAP)
#define AUTH_IS_CHAP(ppp)	((ppp)->peer_auth == PPP_AUTH_CHAP_MD5 ||\
				(ppp)->peer_auth == PPP_AUTH_CHAP_MS ||	\
				(ppp)->peer_auth == PPP_AUTH_CHAP_MS_V2)
#define AUTH_IS_EAP(ppp) 	((ppp)->peer_auth == PPP_AUTH_EAP)

/*
 * About termination procedures:
 *	ppp_lcp_finished	LCP is terminated
 *				Terminate-Request by the peer.
 *				Terminate-Request by ourself. (From ppp_stop())
 *	ppp_phy_downed		Down the datalink/physical.
 *
 * On both cases, ppp_stop0 and ppp_down_others are called.
 */
/** Create a npppd_ppp instance */
npppd_ppp *
ppp_create()
{
	npppd_ppp *_this;

	if ((_this = calloc(1, sizeof(npppd_ppp))) == NULL) {
		log_printf(LOG_ERR, "calloc() failed in %s(): %m", __func__ );
		return NULL;
	}

	_this->snp.snp_family = AF_INET;
	_this->snp.snp_len = sizeof(_this->snp);
	_this->snp.snp_type = SNP_PPP;
	_this->snp.snp_data_ptr = _this;

	return _this;
}

/**
 * Initialize the npppd_ppp instance
 * Set npppd_ppp#mru and npppd_ppp#phy_label before call this function.
 */
int
ppp_init(npppd *pppd, npppd_ppp *_this)
{
	struct tunnconf *conf;

	PPP_ASSERT(_this != NULL);
	PPP_ASSERT(strlen(_this->phy_label) > 0);

	_this->id = -1;
	_this->ifidx = -1;
	_this->has_acf = 1;
	_this->recv_packet = ppp_recv_packet;
	_this->id = ppp_seq++;
	_this->pppd = pppd;

	lcp_init(&_this->lcp, _this);

	conf = ppp_get_tunnconf(_this);
	_this->mru = conf->mru;

	if (_this->outpacket_buf == NULL) {
		_this->outpacket_buf = malloc(_this->mru + 64);
		if (_this->outpacket_buf == NULL){
			log_printf(LOG_ERR, "malloc() failed in %s(): %m",
			    __func__);
			return -1;
		}
	}
	_this->adjust_mss = (conf->tcp_mss_adjust)? 1 : 0;

#ifdef USE_NPPPD_PIPEX
	_this->use_pipex = (conf->pipex)? 1 : 0;
#endif
	/* load the logging configuration */
	_this->ingress_filter = (conf->ingress_filter)? 1 : 0;

#ifdef	USE_NPPPD_MPPE
	mppe_init(&_this->mppe, _this);
#endif
	ccp_init(&_this->ccp, _this);
	ipcp_init(&_this->ipcp, _this);
	pap_init(&_this->pap, _this);
	chap_init(&_this->chap, _this);

	/* load the idle timer configuration */
	_this->timeout_sec = conf->idle_timeout;

	if (!evtimer_initialized(&_this->idle_event))
		evtimer_set(&_this->idle_event, ppp_idle_timeout, _this);

	if (conf->lcp_keepalive) {
		_this->lcp.echo_interval = conf->lcp_keepalive_interval;
		_this->lcp.echo_retry_interval =
		    conf->lcp_keepalive_retry_interval;
		_this->lcp.echo_max_retries = conf->lcp_keepalive_max_retries;
	} else {
		_this->lcp.echo_interval = 0;
		_this->lcp.echo_retry_interval = 0;
		_this->lcp.echo_max_retries = 0;
	}
	_this->log_dump_in = (conf->debug_dump_pktin == 0)? 0 : 1;
	_this->log_dump_out = (conf->debug_dump_pktout == 0)? 0 : 1;

	return 0;
}

static void
ppp_set_tunnel_label(npppd_ppp *_this, char *buf, int lbuf)
{
	int flag, af;
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
	u_char *ea;

	hbuf[0] = 0;
	sbuf[0] = 0;
	af = ((struct sockaddr *)&_this->phy_info)->sa_family;
	if (af == AF_LINK) {
		ea = LLADDR((struct sockaddr_dl *)&_this->phy_info);
		snprintf(buf, lbuf, "%02x:%02x:%02x:%02x:%02x:%02x", *ea,
		    *(ea + 1), *(ea + 2), *(ea + 3), *(ea + 4), *(ea + 5));
	} else if (af < AF_MAX) {
		flag = NI_NUMERICHOST;
		if (af == AF_INET || af == AF_INET6)
			flag |= NI_NUMERICSERV;
		if (getnameinfo((struct sockaddr *)&_this->phy_info,
		    ((struct sockaddr *)&_this->phy_info)->sa_len, hbuf,
		    sizeof(hbuf), sbuf, sizeof(sbuf), flag) != 0) {
			ppp_log(_this, LOG_ERR, "getnameinfo() failed at %s",
			    __func__);
			strlcpy(hbuf, "0.0.0.0", sizeof(hbuf));
			strlcpy(sbuf, "0", sizeof(sbuf));
		}
		if (af == AF_INET || af == AF_INET6)
			snprintf(buf, lbuf, "%s:%s", hbuf, sbuf);
		else
			snprintf(buf, lbuf, "%s", hbuf);
	} else if (af == NPPPD_AF_PHONE_NUMBER) {
		strlcpy(buf,
		    ((npppd_phone_number *)&_this->phy_info)->pn_number, lbuf);
	}
}
/**
 * Start the npppd_ppp.
 * Set npppd_ppp#phy_context, npppd_ppp#send_packet, npppd_ppp#phy_close and
 * npppd_ppp#phy_info before call this function.
 */
void
ppp_start(npppd_ppp *_this)
{
	char label[512];

	PPP_ASSERT(_this != NULL);
	PPP_ASSERT(_this->recv_packet != NULL);
	PPP_ASSERT(_this->send_packet != NULL);
	PPP_ASSERT(_this->phy_close != NULL);

	_this->start_time = time(NULL);
	_this->start_monotime = get_monosec();
	/* log the lower layer information */
	ppp_set_tunnel_label(_this, label, sizeof(label));
	ppp_log(_this, LOG_INFO, "logtype=Started tunnel=%s(%s)",
	    _this->phy_label, label);

	lcp_lowerup(&_this->lcp);
}

/** Prepare "dialin proxy".  Return 0 if "dialin proxy" is not available.  */
int
ppp_dialin_proxy_prepare(npppd_ppp *_this, dialin_proxy_info *dpi)
{
	int renego_force, renego;
	struct tunnconf *conf;

	conf = ppp_get_tunnconf(_this);

	renego = conf->proto.l2tp.lcp_renegotiation;
	renego_force = conf->proto.l2tp.force_lcp_renegotiation;

	if (renego_force)
		renego = 1;

	if (lcp_dialin_proxy(&_this->lcp, dpi, renego, renego_force) != 0) {
		ppp_log(_this, LOG_ERR,
		    "Failed to dialin-proxy, proxied lcp is broken.");
		return 1;
	}

	return 0;
}

static void
ppp_down_others(npppd_ppp *_this)
{
	fsm_lowerdown(&_this->ccp.fsm);
	fsm_lowerdown(&_this->ipcp.fsm);

	npppd_release_ip(_this->pppd, _this);
	if (AUTH_IS_PAP(_this))
		pap_stop(&_this->pap);
	if (AUTH_IS_CHAP(_this))
		chap_stop(&_this->chap);
#ifdef USE_NPPPD_EAP_RADIUS
	if (AUTH_IS_EAP(_this))
		eap_stop(&_this->eap);
#endif
	evtimer_del(&_this->idle_event);
}

/**
 * Stop the PPP and destroy the npppd_ppp instance
 * @param reason	Reason of stopping the PPP.  Specify NULL if there is
 *			no special reason.  This reason will be used as a
 *			reason field of LCP Terminate-Request message and
 *			notified to the peer.
 */
void
ppp_stop(npppd_ppp *_this, const char *reason)
{

	PPP_ASSERT(_this != NULL);

#ifdef USE_NPPPD_RADIUS
	ppp_set_radius_terminate_cause(_this,
	    RADIUS_TERMNATE_CAUSE_ADMIN_RESET);
#endif
	ppp_set_disconnect_cause(_this, PPP_DISCON_NORMAL, 0, 2 /* by local */,
	    NULL);

	ppp_down_others(_this);
	fsm_close(&_this->lcp.fsm, reason);
}

/**
 * Set disconnect cause
 * @param code		disconnect code in {@link ::npppd_ppp_disconnect_code}.
 * @param proto		control protocol number.  see RFC3145.
 * @param direction	disconnect direction.  see RFC 3145
 */
void
ppp_set_disconnect_cause(npppd_ppp *_this, npppd_ppp_disconnect_code code,
    int proto, int direction, const char *message)
{
	if (_this->disconnect_code == PPP_DISCON_NO_INFORMATION) {
		_this->disconnect_code = code;
		_this->disconnect_proto = proto;
		_this->disconnect_direction = direction;
		_this->disconnect_message = message;
	}
}

/** Set RADIUS Acct-Terminate-Cause code */
void
ppp_set_radius_terminate_cause(npppd_ppp *_this, int cause)
{
	if (_this->terminate_cause == 0)
		_this->terminate_cause = cause;
}

static void
ppp_stop0(npppd_ppp *_this)
{
	char mppe_str[BUFSIZ];
	char label[512];

#ifdef USE_NPPPD_RADIUS
	ppp_set_radius_terminate_cause(_this, RADIUS_TERMNATE_CAUSE_NAS_ERROR);
#endif
	ppp_set_disconnect_cause(_this, PPP_DISCON_NORMAL, 0, 1 /* by local */,
	    NULL);

	_this->end_monotime = get_monosec();

	if (_this->phy_close != NULL)
		_this->phy_close(_this);
	_this->phy_close = NULL;

	/*
	 * NAT/Blackhole detection for PPTP(GRE)
	 */
	if (_this->lcp.dialin_proxy != 0 &&
	    _this->lcp.dialin_proxy_lcp_renegotiation == 0) {
		/* No LCP packets on dialin proxy without LCP renegotiation */
	} else if (_this->lcp.recv_ress == 0) {	/* No responses */
		if (_this->lcp.recv_reqs == 0)	/* No requests */
			ppp_log(_this, LOG_WARNING, "no PPP frames from the "
			    "peer.  router/NAT issue? (may have filtered out)");
		else
			ppp_log(_this, LOG_WARNING, "my PPP frames may not "
			    "have arrived at the peer.  router/NAT issue? (may "
			    "be the only-first-person problem)");
	}
#ifdef USE_NPPPD_PIPEX
	if (npppd_ppp_pipex_disable(_this->pppd, _this) != 0)
		ppp_log(_this, LOG_ERR,
		    "npppd_ppp_pipex_disable() failed: %m");
#endif

	ppp_set_tunnel_label(_this, label, sizeof(label));
#ifdef	USE_NPPPD_MPPE
	if (_this->mppe_started) {
		snprintf(mppe_str, sizeof(mppe_str),
		    "mppe=yes mppe_in=%dbits,%s mppe_out=%dbits,%s",
		    _this->mppe.recv.keybits,
		    (_this->mppe.recv.stateless)? "stateless" : "stateful",
		    _this->mppe.send.keybits,
		    (_this->mppe.send.stateless)? "stateless" : "stateful");
	} else
#endif
		snprintf(mppe_str, sizeof(mppe_str), "mppe=no");
	ppp_log(_this, LOG_NOTICE,
		"logtype=TUNNELUSAGE user=\"%s\" duration=%ldsec layer2=%s "
		"layer2from=%s auth=%s data_in=%llubytes,%upackets "
		"data_out=%llubytes,%upackets error_in=%u error_out=%u %s "
		"iface=%s",
		_this->username[0]? _this->username : "<unknown>",
		(long)(_this->end_monotime - _this->start_monotime),
		_this->phy_label,  label,
		_this->username[0]? ppp_peer_auth_string(_this) : "none",
		(unsigned long long)_this->ibytes, _this->ipackets,
		(unsigned long long)_this->obytes, _this->opackets,
		_this->ierrors, _this->oerrors, mppe_str,
		npppd_ppp_get_iface_name(_this->pppd, _this));

#ifdef USE_NPPPD_RADIUS
	npppd_ppp_radius_acct_stop(_this->pppd, _this);
#endif
	npppd_on_ppp_stop(_this->pppd, _this);
	npppd_ppp_unbind_iface(_this->pppd, _this);
#ifdef	USE_NPPPD_MPPE
	mppe_fini(&_this->mppe);
#endif
	evtimer_del(&_this->idle_event);

	npppd_release_ip(_this->pppd, _this);
	ppp_destroy(_this);
}

/**
 * Destroy the npppd_ppp instance.  Don't use this function after calling
 * the ppp_start, please use ppp_stop() instead.
 */
void
ppp_destroy(void *ctx)
{
	npppd_ppp *_this = ctx;

	free(_this->proxy_authen_resp);

	/*
	 * Down/stop the protocols again to make sure they are stopped
	 * even if ppp_stop is done.  They might be change their state
	 * by receiving packets from the peer.
	 */
	fsm_lowerdown(&_this->ccp.fsm);
	fsm_lowerdown(&_this->ipcp.fsm);
	pap_stop(&_this->pap);
	chap_stop(&_this->chap);

	free(_this->outpacket_buf);

	free(_this);
}

/************************************************************************
 * Protocol events
 ************************************************************************/
static const char *
ppp_peer_auth_string(npppd_ppp *_this)
{
	switch(_this->peer_auth) {
	case PPP_AUTH_PAP:		return "PAP";
	case PPP_AUTH_CHAP_MD5:		return "MD5-CHAP";
	case PPP_AUTH_CHAP_MS:		return "MS-CHAP";
	case PPP_AUTH_CHAP_MS_V2:	return "MS-CHAP-V2";
	case PPP_AUTH_EAP:		return "EAP";
	default:			return "ERROR";
	}
}

/** called when the lcp is up */
void
ppp_lcp_up(npppd_ppp *_this)
{
#ifdef USE_NPPPD_MPPE
	if (MPPE_IS_REQUIRED(_this) && !MPPE_MUST_NEGO(_this)) {
		ppp_log(_this, LOG_ERR, "MPPE is required, auth protocol must "
		    "be MS-CHAP-V2 or EAP");
		ppp_stop(_this, "Encryption required");
		return;
	}
#endif
	/*
	 * Use our MRU value even if the peer insists on larger value.
	 * We set the peer_mtu here, the value will be used as the MTU of the
	 * routing entry.  So we will not receive packets larger than the MTU.
	 */
	if (_this->peer_mru > _this->mru)
		_this->peer_mru = _this->mru;

	if (_this->peer_auth != 0 && _this->auth_runonce == 0) {
		if (AUTH_IS_PAP(_this)) {
			pap_start(&_this->pap);
			_this->auth_runonce = 1;
			return;
		}
		if (AUTH_IS_CHAP(_this)) {
			chap_start(&_this->chap);
			_this->auth_runonce = 1;
			return;
		}
#ifdef USE_NPPPD_EAP_RADIUS
                if (AUTH_IS_EAP(_this)) {
                        eap_init(&_this->eap, _this);
                        eap_start(&_this->eap);
                        return;
                }
#endif
	}
	if (_this->peer_auth == 0)
		ppp_auth_ok(_this);
}

/**
 * This function will be called the LCP is terminated.
 * (On entering STOPPED or  CLOSED state)
 */
void
ppp_lcp_finished(npppd_ppp *_this)
{
	PPP_ASSERT(_this != NULL);

	ppp_down_others(_this);

	fsm_lowerdown(&_this->lcp.fsm);
	ppp_stop0(_this);
}

/**
 * This function will be called by the physical layer when it is down.
 * <p>
 * Use this function only on such conditions that the physical layer cannot
 * input or output PPP frames.  Use {@link ::ppp_stop()} instead if we can
 * disconnect PPP gently.</p>
 */
void
ppp_phy_downed(npppd_ppp *_this)
{
	PPP_ASSERT(_this != NULL);

	ppp_down_others(_this);
	fsm_lowerdown(&_this->lcp.fsm);
	fsm_close(&_this->lcp.fsm, NULL);

#ifdef USE_NPPPD_RADIUS
	ppp_set_radius_terminate_cause(_this,
	    RADIUS_TERMNATE_CAUSE_LOST_CARRIER);
#endif
	ppp_stop0(_this);
}

static const char *
proto_name(uint16_t proto)
{
	switch (proto) {
	case PPP_PROTO_IP:			return "ip";
	case PPP_PROTO_LCP:			return "lcp";
	case PPP_PROTO_PAP:			return "pap";
	case PPP_PROTO_CHAP:			return "chap";
	case PPP_PROTO_EAP:			return "eap";
	case PPP_PROTO_MPPE:			return "mppe";
	case PPP_PROTO_NCP | NCP_CCP:		return "ccp";
	case PPP_PROTO_NCP | NCP_IPCP:		return "ipcp";
	/* following protocols are just for logging */
	case PPP_PROTO_NCP | NCP_IPV6CP:	return "ipv6cp";
	case PPP_PROTO_ACSP:			return "acsp";
	}
	return "unknown";
}

/** This function is called on authentication succeed */
void
ppp_auth_ok(npppd_ppp *_this)
{
	if (npppd_ppp_bind_iface(_this->pppd, _this) != 0) {
		ppp_log(_this, LOG_WARNING, "No interface binding.");
		ppp_stop(_this, NULL);

		return;
	}
	if (_this->realm != NULL) {
		npppd_ppp_get_username_for_auth(_this->pppd, _this,
		    _this->username, _this->username);
		if (!npppd_check_calling_number(_this->pppd, _this)) {
			ppp_log(_this, LOG_ALERT,
			    "logtype=TUNNELDENY user=\"%s\" "
			    "reason=\"Calling number check is failed\"",
			    _this->username);
			    /* XXX */
			ppp_stop(_this, NULL);
			return;
		}
	}
	if (_this->peer_auth != 0) {
		/* Limit the number of connections per the user */
		if (!npppd_check_user_max_session(_this->pppd, _this)) {
			ppp_stop(_this, NULL);

			return;
		}
		PPP_ASSERT(_this->realm != NULL);
	}

	if (!npppd_ppp_iface_is_ready(_this->pppd, _this)) {
		ppp_log(_this, LOG_WARNING,
		    "interface '%s' is not ready.",
		    npppd_ppp_get_iface_name(_this->pppd, _this));
		ppp_stop(_this, NULL);

		return;
	}
	free(_this->proxy_authen_resp);
	_this->proxy_authen_resp = NULL;

	fsm_lowerup(&_this->ipcp.fsm);
	fsm_open(&_this->ipcp.fsm);
#ifdef	USE_NPPPD_MPPE
	if (MPPE_MUST_NEGO(_this)) {
		fsm_lowerup(&_this->ccp.fsm);
		fsm_open(&_this->ccp.fsm);
	}
#endif

	return;
}

/** timer event handler for idle timer */
static void
ppp_idle_timeout(int fd, short evtype, void *context)
{
	npppd_ppp *_this;

	_this = context;

	ppp_log(_this, LOG_NOTICE, "Idle timeout(%d sec)", _this->timeout_sec);
#ifdef USE_NPPPD_RADIUS
	ppp_set_radius_terminate_cause(_this,
	    RADIUS_TERMNATE_CAUSE_IDLE_TIMEOUT);
#endif
	ppp_stop(_this, NULL);
}

/** reset the idle-timer.  Call this function when the PPP is not idle. */
void
ppp_reset_idle_timeout(npppd_ppp *_this)
{
	struct timeval tv;

	evtimer_del(&_this->idle_event);
	if (_this->timeout_sec > 0) {
		tv.tv_usec = 0;
		tv.tv_sec = _this->timeout_sec;

		evtimer_add(&_this->idle_event, &tv);
	}
}

/** This function is called when IPCP is opened */
void
ppp_ipcp_opened(npppd_ppp *_this)
{
	time_t curr_time;

	curr_time = get_monosec();

	npppd_set_ip_enabled(_this->pppd, _this, 1);
	if (_this->logged_acct_start == 0) {
		char label[512], ipstr[64];

		ppp_set_tunnel_label(_this, label, sizeof(label));

		strlcpy(ipstr, " ip=", sizeof(ipstr));
		strlcat(ipstr, inet_ntoa(_this->ppp_framed_ip_address),
		    sizeof(ipstr));
		if (_this->ppp_framed_ip_netmask.s_addr != 0xffffffffL) {
			strlcat(ipstr, ":", sizeof(ipstr));
			strlcat(ipstr, inet_ntoa(_this->ppp_framed_ip_netmask),
			    sizeof(ipstr));
		}

		ppp_log(_this, LOG_NOTICE,
		    "logtype=TUNNELSTART user=\"%s\" duration=%lusec layer2=%s "
 		    "layer2from=%s auth=%s %s iface=%s%s",
		    _this->username[0]? _this->username : "<unknown>",
		    (long)(curr_time - _this->start_monotime),
		    _this->phy_label, label,
		    _this->username[0]? ppp_peer_auth_string(_this) : "none",
 		    ipstr, npppd_ppp_get_iface_name(_this->pppd, _this),
		    (_this->lcp.dialin_proxy != 0)? " dialin_proxy=yes" : ""
		    );
#ifdef USE_NPPPD_RADIUS
		npppd_ppp_radius_acct_start(_this->pppd, _this);
#endif
		npppd_on_ppp_start(_this->pppd, _this);

		_this->logged_acct_start = 1;
		ppp_reset_idle_timeout(_this);
	}
#ifdef USE_NPPPD_PIPEX
	ppp_on_network_pipex(_this);
#endif
}

/** This function is called when CCP is opened */
void
ppp_ccp_opened(npppd_ppp *_this)
{
#ifdef USE_NPPPD_MPPE
	if (_this->ccp.mppe_rej == 0) {
		if (_this->mppe_started == 0) {
			mppe_start(&_this->mppe);
		}
	} else {
		ppp_log(_this, LOG_INFO, "mppe is rejected by peer");
		if (_this->mppe.required)
			ppp_stop(_this, "MPPE is required");
	}
#endif
#ifdef USE_NPPPD_PIPEX
	ppp_on_network_pipex(_this);
#endif
}

void
ppp_ccp_stopped(npppd_ppp *_this)
{
#ifdef USE_NPPPD_MPPE
	if (_this->mppe.required) {
		ppp_stop(_this, NULL);
		return;
	}
#endif
#ifdef USE_NPPPD_PIPEX
	ppp_on_network_pipex(_this);
#endif
}

/************************************************************************
 * Network I/O related functions
 ************************************************************************/
/**
 * Receive the PPP packet.
 * @param	flags	Indicate information of received packet by bit flags.
 *			{@link ::PPP_IO_FLAGS_MPPE_ENCRYPTED} and
 *			{@link ::PPP_IO_FLAGS_DELAYED} may be used.
 * @return	return 0 on success.  return 1 on failure.
 */
static int
ppp_recv_packet(npppd_ppp *_this, unsigned char *pkt, int lpkt, int flags)
{
	u_char *inp, *inp_proto;
	uint16_t proto;

	PPP_ASSERT(_this != NULL);

	inp = pkt;

	if (lpkt < 4) {
		ppp_log(_this, LOG_DEBUG, "%s(): Rcvd short header.", __func__);
		return 0;
	}


	if (_this->has_acf == 0) {
		/* nothing to do */
	} else if (inp[0] == PPP_ALLSTATIONS && inp[1] == PPP_UI) {
		inp += 2;
	} else {
		/*
		 * Address and Control Field Compression
		 */
		if (!psm_opt_is_accepted(&_this->lcp, acfc) &&
		    _this->logged_no_address == 0) {
			/*
			 * On packet loss condition, we may receive ACFC'ed
			 * packets before our LCP is opened because the peer's
			 * LCP is opened already.
			 */
			ppp_log(_this, LOG_INFO,
			    "%s: Rcvd broken frame.  ACFC is not accepted, "
			    "but received ppp frame that has no address.",
			    __func__);
			/*
			 * Log this once because it may be noisy.
			 * For example, Yahama RTX-1000 refuses to use ACFC
			 * but it send PPP frames without the address field.
			 */
			_this->logged_no_address = 1;
		}
	}
	inp_proto = inp;
	if ((inp[0] & 0x01) != 0) {
		/*
		 * Protocol Field Compression
		 */
		if (!psm_opt_is_accepted(&_this->lcp, pfc)) {
			ppp_log(_this, LOG_INFO,
			    "%s: Rcvd broken frame.  No protocol field: "
			    "%02x %02x", __func__, inp[0], inp[1]);
			return 1;
		}
		GETCHAR(proto, inp);
	} else {
		GETSHORT(proto, inp);
	}

	/*
	 * if the PPP frame is reordered, drop it
	 * unless proto is reorder-tolerant
	 */
	if (flags & PPP_IO_FLAGS_DELAYED && proto != PPP_PROTO_IP)
		return 1;

	if (_this->log_dump_in != 0 && debug_get_debugfp() != NULL) {
		struct tunnconf *conf = ppp_get_tunnconf(_this);
		if ((ppp_proto_bit(proto) & conf->debug_dump_pktin) != 0) {
			ppp_log(_this, LOG_DEBUG,
			    "PPP input dump proto=%s(%d/%04x)",
			    proto_name(proto), proto, proto);
			show_hd(debug_get_debugfp(), pkt, lpkt);
		}
	}
#ifdef USE_NPPPD_PIPEX
	if (_this->pipex_enabled != 0 &&
	    _this->tunnel_type == NPPPD_TUNNEL_PPPOE) {
		switch (proto) {
		case PPP_PROTO_IP:
			return 2;		/* handled by PIPEX */
		case PPP_PROTO_NCP | NCP_CCP:
			if (lpkt - (inp - pkt) < 4)
				break;		/* error but do it on fsm.c */
			if (*inp == 0x0e ||	/* Reset-Request */
			    *inp == 0x0f	/* Reset-Ack */) {
				return 2;	/* handled by PIPEX */
			}
			/* FALLTHROUGH */
		default:
			break;
		}
	}
#endif /* USE_NPPPD_PIPEX */

	switch (proto) {
#ifdef	USE_NPPPD_MPPE
	case PPP_PROTO_IP:
		/* Checks for MPPE */
		if ((flags & PPP_IO_FLAGS_MPPE_ENCRYPTED) == 0) {
			if (MPPE_IS_REQUIRED(_this)) {
				/* MPPE is required but naked ip */

				if (_this->logged_naked_ip == 0) {
					ppp_log(_this, LOG_INFO,
					    "mppe is required but received "
					    "naked IP.");
					/* log this once */
					_this->logged_naked_ip = 1;
				}
				/*
				 * Windows sends naked IP packets in condition
				 * such that MPPE is not opened and IPCP is
				 * opened(*1).  This occurs at a high
				 * probability when the CCP establishment is
				 * delayed because of packet loss etc.  If we
				 * call ppp_stop() here, Windows on the packet
				 * loss condition etc cannot not connect us.
				 * So we don't call ppp_stop() here.
				 * (*1) At least Microsoft Windows 2000
				 * Professional SP4 does.
				 */
				 /*ppp_stop(_this, "Encryption is required.");*/

				return 1;
			}
			if (MPPE_RECV_READY(_this)) {
				/* MPPE is opened but naked ip packet */
				ppp_log(_this, LOG_WARNING,
				    "mppe is available but received naked IP.");
			}
		}
		/* else input from MPPE */
		break;
	case PPP_PROTO_MPPE:
#ifdef USE_NPPPD_MPPE
		if (!MPPE_RECV_READY(_this)) {
#else
		{
#endif
			ppp_log(_this, LOG_ERR,
			    "mppe packet is received but mppe is stopped.");
			return 1;
		}
		break;
#endif
	}

	switch (proto) {
	case PPP_PROTO_IP:
		npppd_network_output(_this->pppd, _this, AF_INET, inp,
		    lpkt - (inp - pkt));
		goto handled;
	case PPP_PROTO_LCP:
		fsm_input(&_this->lcp.fsm, inp, lpkt - (inp - pkt));
		goto handled;
	case PPP_PROTO_PAP:
		pap_input(&_this->pap, inp, lpkt - (inp - pkt));
		goto handled;
	case PPP_PROTO_CHAP:
		chap_input(&_this->chap, inp, lpkt - (inp - pkt));
		goto handled;
#ifdef USE_NPPPD_EAP_RADIUS
	case PPP_PROTO_EAP:
		eap_input(&_this->eap, inp, lpkt - (inp - pkt));
		goto handled;
#endif
#ifdef	USE_NPPPD_MPPE
	case PPP_PROTO_MPPE:
#ifdef USE_NPPPD_PIPEX
		if (_this->pipex_enabled != 0)
			return -1; /* silent discard */
#endif /* USE_NPPPD_PIPEX */
		mppe_input(&_this->mppe, inp, lpkt - (inp - pkt));
		goto handled;
#endif
	default:
		if ((proto & 0xff00) == PPP_PROTO_NCP) {
			switch (proto & 0xff) {
			case NCP_CCP:	/* Compression */
#ifdef	USE_NPPPD_MPPE
				if (MPPE_MUST_NEGO(_this)) {
					fsm_input(&_this->ccp.fsm, inp,
					    lpkt - (inp - pkt));
					goto handled;
				}
				/* protocol-reject if MPPE is not necessary */
#endif
				break;
			case NCP_IPCP:	/* IPCP */
				fsm_input(&_this->ipcp.fsm, inp,
				    lpkt - (inp - pkt));
				goto handled;
			}
		}
	}
	/* Protocol reject.  Log it with protocol number */
	ppp_log(_this, LOG_INFO, "unhandled protocol %s, %d(%04x)",
	    proto_name(proto), proto, proto);

	if ((flags & PPP_IO_FLAGS_MPPE_ENCRYPTED) != 0) {
		/*
		 * Don't return a protocol-reject for the packet was encrypted,
		 * because lcp protocol-reject is not encrypted by mppe.
		 */
	} else {
		/*
		 * as RFC1661: Rejected-Information MUST be truncated to
		 * comply with the peer's established MRU.
		 */
		lcp_send_protrej(&_this->lcp, inp_proto,
		    MINIMUM(lpkt - (inp_proto - pkt), NPPPD_MIN_MRU - 32));
	}

	return 1;
handled:

	return 0;
}

/** This function is called to output PPP packets */
void
ppp_output(npppd_ppp *_this, uint16_t proto, u_char code, u_char id,
    u_char *datap, int ldata)
{
	u_char *outp;
	int outlen, hlen, is_lcp = 0;

	outp = _this->outpacket_buf;

	/* No header compressions for LCP */
	is_lcp = (proto == PPP_PROTO_LCP)? 1 : 0;

	if (_this->has_acf == 0 ||
	    (!is_lcp && psm_peer_opt_is_accepted(&_this->lcp, acfc))) {
		/*
		 * Don't add ACF(Address and Control Field) if ACF is not
		 * needed on this link or ACFC is negotiated.
		 */
	} else {
		PUTCHAR(PPP_ALLSTATIONS, outp);
		PUTCHAR(PPP_UI, outp);
	}
	if (!is_lcp && proto <= 0xff &&
	    psm_peer_opt_is_accepted(&_this->lcp, pfc)) {
		/*
		 * Protocol Field Compression
		 */
		PUTCHAR(proto, outp);
	} else {
		PUTSHORT(proto, outp);
	}
	hlen = outp - _this->outpacket_buf;

	if (_this->mru > 0) {
		if (MRU_PKTLEN(_this->mru, proto) < ldata) {
			PPP_DBG((_this, LOG_ERR, "packet too large %d. mru=%d",
			    ldata , _this->mru));
			_this->oerrors++;
			PPP_ASSERT("NOT REACHED HERE" == NULL);
			return;
		}
	}

	if (code != 0) {
		outlen = ldata + HEADERLEN;

		PUTCHAR(code, outp);
		PUTCHAR(id, outp);
		PUTSHORT(outlen, outp);
	} else {
		outlen = ldata;
	}

	if (outp != datap && ldata > 0)
		memmove(outp, datap, ldata);

	if (_this->log_dump_out != 0 && debug_get_debugfp() != NULL) {
		struct tunnconf *conf = ppp_get_tunnconf(_this);
		if ((ppp_proto_bit(proto) & conf->debug_dump_pktout) != 0) {
			ppp_log(_this, LOG_DEBUG,
			    "PPP output dump proto=%s(%d/%04x)",
			    proto_name(proto), proto, proto);
			show_hd(debug_get_debugfp(),
			    _this->outpacket_buf, outlen + hlen);
		}
	}
	_this->send_packet(_this, _this->outpacket_buf, outlen + hlen, 0);
}

/**
 * Return the buffer space for PPP output.  The returned pointer will be
 * adjusted for header compression. The length of the space is larger than
 * {@link npppd_ppp#mru}.
 */
u_char *
ppp_packetbuf(npppd_ppp *_this, int proto)
{
	int save;

	save = 0;
	if (proto != PPP_PROTO_LCP) {
		if (psm_peer_opt_is_accepted(&_this->lcp, acfc))
			save += 2;
		if (proto <= 0xff && psm_peer_opt_is_accepted(&_this->lcp, pfc))
			save += 1;
	}
	return _this->outpacket_buf + (PPP_HDRLEN - save);
}

/** Record log that begins the label based this instance. */
int
ppp_log(npppd_ppp *_this, int prio, const char *fmt, ...)
{
	int status;
	char logbuf[BUFSIZ];
	va_list ap;

	PPP_ASSERT(_this != NULL);

	va_start(ap, fmt);
	snprintf(logbuf, sizeof(logbuf), "ppp id=%u layer=base %s",
	    _this->id, fmt);
	status = vlog_printf(prio, logbuf, ap);
	va_end(ap);

	return status;
}

#ifdef USE_NPPPD_PIPEX
/** The callback function on network is available for pipex */
static void
ppp_on_network_pipex(npppd_ppp *_this)
{
	if (_this->use_pipex == 0)
		return;
	if (_this->tunnel_type != NPPPD_TUNNEL_PPTP &&
	    _this->tunnel_type != NPPPD_TUNNEL_PPPOE &&
	    _this->tunnel_type != NPPPD_TUNNEL_L2TP)
		return;

	if (_this->pipex_started != 0)
		return;	/* already started */

	if (_this->assigned_ip4_enabled != 0 &&
	    (!MPPE_MUST_NEGO(_this) || _this->ccp.fsm.state == OPENED ||
		    _this->ccp.fsm.state == STOPPED)) {
		/* IPCP is opened and MPPE is not required or MPPE is opened */
		if (npppd_ppp_pipex_enable(_this->pppd, _this) != 0) {
			ppp_log(_this, LOG_WARNING, "failed enable pipex: %m");
			/* failed to create pipex session */
			ppp_phy_downed(_this);
			return;
		}
		ppp_log(_this, LOG_NOTICE, "Using pipex=%s",
		    (_this->pipex_enabled != 0)? "yes" : "no");
		_this->pipex_started = 1;
	}
	/* else wait CCP or IPCP */
}
#endif

static uint32_t
ppp_proto_bit(int proto)
{
	switch (proto) {
	case PPP_PROTO_IP:		return NPPPD_PROTO_BIT_IP;
	case PPP_PROTO_LCP:		return NPPPD_PROTO_BIT_LCP;
	case PPP_PROTO_PAP:		return NPPPD_PROTO_BIT_PAP;
	case PPP_PROTO_CHAP:		return NPPPD_PROTO_BIT_CHAP;
	case PPP_PROTO_EAP:		return NPPPD_PROTO_BIT_EAP;
	case PPP_PROTO_MPPE:		return NPPPD_PROTO_BIT_MPPE;
	case PPP_PROTO_NCP | NCP_CCP:	return NPPPD_PROTO_BIT_CCP;
	case PPP_PROTO_NCP | NCP_IPCP:	return NPPPD_PROTO_BIT_IPCP;
	}
	return 0;
}

struct tunnconf tunnconf_default_l2tp = {
	.mru = 1360,
	.tcp_mss_adjust = false,
	.pipex = true,
	.ingress_filter = false,
	.lcp_keepalive = false,
	.lcp_keepalive_interval = DEFAULT_LCP_ECHO_INTERVAL,
	.lcp_keepalive_retry_interval = DEFAULT_LCP_ECHO_RETRY_INTERVAL,
	.lcp_keepalive_max_retries = DEFAULT_LCP_ECHO_MAX_RETRIES,
	.auth_methods = NPPPD_AUTH_METHODS_CHAP | NPPPD_AUTH_METHODS_MSCHAPV2,
	.mppe_yesno = true,
	.mppe_required = false,
	.mppe_keylen = NPPPD_MPPE_40BIT | NPPPD_MPPE_56BIT | NPPPD_MPPE_128BIT,
	.mppe_keystate = NPPPD_MPPE_STATELESS | NPPPD_MPPE_STATEFUL,
	.callnum_check = 0,
	.proto = {
		.l2tp = {
			.hostname = NULL,
			.vendor_name = NULL,
			.listen = TAILQ_HEAD_INITIALIZER(
			    tunnconf_default_l2tp.proto.l2tp.listen),
			/* .hello_interval, */
			/* .hello_timeout, */
			.data_use_seq = true,
			.require_ipsec = false,
			/* .accept_dialin, */
			.lcp_renegotiation = true,
			.force_lcp_renegotiation = false,
			/* .ctrl_in_pktdump, */
			/* .ctrl_out_pktdump, */
			/* .data_in_pktdump, */
			/* .data_out_pktdump, */
		}
	}
};
struct tunnconf tunnconf_default_pptp = {
	.mru = 1400,
	.tcp_mss_adjust = false,
	.pipex = true,
	.ingress_filter = false,
	.lcp_keepalive = true,
	.lcp_keepalive_interval = DEFAULT_LCP_ECHO_INTERVAL,
	.lcp_keepalive_retry_interval = DEFAULT_LCP_ECHO_RETRY_INTERVAL,
	.lcp_keepalive_max_retries = DEFAULT_LCP_ECHO_MAX_RETRIES,
	.auth_methods = NPPPD_AUTH_METHODS_CHAP | NPPPD_AUTH_METHODS_MSCHAPV2,
	.mppe_yesno = true,
	.mppe_required = true,
	.mppe_keylen = NPPPD_MPPE_40BIT | NPPPD_MPPE_56BIT | NPPPD_MPPE_128BIT,
	.mppe_keystate = NPPPD_MPPE_STATELESS | NPPPD_MPPE_STATEFUL,
	.callnum_check = 0,
	.proto = {
		.pptp = {
			.hostname = NULL,
			.vendor_name = NULL,
			.listen = TAILQ_HEAD_INITIALIZER(
			    tunnconf_default_pptp.proto.pptp.listen),
			/* .echo_interval, */
			/* .echo_timeout, */
		}
	}
};
struct tunnconf tunnconf_default_pppoe = {
	.mru = 1492,
	.tcp_mss_adjust = false,
	.pipex = true,
	.ingress_filter = false,
	.lcp_keepalive = true,
	.lcp_keepalive_interval = DEFAULT_LCP_ECHO_INTERVAL,
	.lcp_keepalive_retry_interval = DEFAULT_LCP_ECHO_RETRY_INTERVAL,
	.lcp_keepalive_max_retries = DEFAULT_LCP_ECHO_MAX_RETRIES,
	.auth_methods = NPPPD_AUTH_METHODS_CHAP | NPPPD_AUTH_METHODS_MSCHAPV2,
	.mppe_yesno = true,
	.mppe_required = false,
	.mppe_keylen = NPPPD_MPPE_40BIT | NPPPD_MPPE_56BIT | NPPPD_MPPE_128BIT,
	.mppe_keystate = NPPPD_MPPE_STATELESS | NPPPD_MPPE_STATEFUL,
	.callnum_check = 0,
	.proto = {
		.pppoe = {
			/* .service_name */
			.accept_any_service = true,
			/* .ac_name */
			/* .desc_in_pktdump */
			/* .desc_out_pktdump */
			/* .session_in_pktdump */
			/* .session_out_pktdump */
		}
	}
};

struct tunnconf *
ppp_get_tunnconf(npppd_ppp *_this)
{
	struct tunnconf *conf;

	conf = npppd_get_tunnconf(_this->pppd, _this->phy_label);
	if (conf != NULL)
		return conf;

	switch (_this->tunnel_type) {
	case NPPPD_TUNNEL_L2TP:
		return &tunnconf_default_l2tp;
		break;
	case NPPPD_TUNNEL_PPTP:
		return &tunnconf_default_pptp;
		break;
	case NPPPD_TUNNEL_PPPOE:
		return &tunnconf_default_pppoe;
		break;
	}

	return NULL;
}
