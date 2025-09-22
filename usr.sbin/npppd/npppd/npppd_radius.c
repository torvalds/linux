/* $Id: npppd_radius.c,v 1.14 2025/06/23 05:26:01 yasuoka Exp $ */
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
 * THIS SOFTWARE IS PROVIDED BY THE"AUTHOR" AND CONTRIBUTORS AS IS'' AND
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

/*
 *	RFC 2865 Remote Authentication Dial In User Service (RADIUS)
 *	RFC 2866 RADIUS Accounting
 *	RFC 2868 RADIUS Attributes for Tunnel Protocol Support
 *	RFC 2869 RADIUS Extensions
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/syslog.h>
#include <netinet/in.h>
#include <net/if_dl.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <netdb.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <radius.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

#include <event.h>

#include "radius_req.h"
#include "npppd_local.h"
#include "npppd_radius.h"
#include "net_utils.h"

#ifdef NPPPD_RADIUS_DEBUG
#define NPPPD_RADIUS_DBG(x) 	ppp_log x
#define NPPPD_RADIUS_ASSERT(x)	ASSERT(x)
#else
#define NPPPD_RADIUS_DBG(x)
#define NPPPD_RADIUS_ASSERT(x)
#endif

static int l2tp_put_tunnel_attributes(RADIUS_PACKET *, void *);
static int pptp_put_tunnel_attributes(RADIUS_PACKET *, void *);
static int radius_acct_request(npppd *, npppd_ppp *, int );
static void radius_acct_on_cb(void *, RADIUS_PACKET *, int, RADIUS_REQUEST_CTX);
static void npppd_ppp_radius_acct_reqcb(void *, RADIUS_PACKET *, int, RADIUS_REQUEST_CTX);

/***********************************************************************
 * RADIUS common functions
 ***********************************************************************/
/**
 * Retribute Framed-IP-Address and Framed-IP-Netmask attribute of from 
 * the given RADIUS packet and set them as the fields of ppp context.
 */
void
ppp_process_radius_attrs(npppd_ppp *_this, RADIUS_PACKET *pkt)
{
	struct in_addr	 ip4;
	int		 got_pri, got_sec;
	char		 buf0[40], buf1[40];
	
	if (radius_get_ipv4_attr(pkt, RADIUS_TYPE_FRAMED_IP_ADDRESS, &ip4)
	    == 0)
		_this->realm_framed_ip_address = ip4;

	_this->realm_framed_ip_netmask.s_addr = 0xffffffffL;
#ifndef	NPPPD_COMPAT_4_2
	if (radius_get_ipv4_attr(pkt, RADIUS_TYPE_FRAMED_IP_NETMASK, &ip4)
	    == 0)
		_this->realm_framed_ip_netmask = ip4;
#endif

	if (!ppp_ipcp(_this)->dns_configured) {
		got_pri = got_sec = 0;
		if (radius_get_vs_ipv4_attr(pkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_PRIMARY_DNS_SERVER, &ip4) == 0) {
			got_pri = 1;
			_this->ipcp.dns_pri = ip4;
		}
		if (radius_get_vs_ipv4_attr(pkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_SECONDARY_DNS_SERVER, &ip4) == 0) {
			got_sec = 1;
			_this->ipcp.dns_sec = ip4;
		}
		if (got_pri || got_sec)
			ppp_log(_this, LOG_INFO, "DNS server address%s "
			    "(%s%s%s) %s configured by RADIUS server",
			    ((got_pri + got_sec) > 1)? "es" : "",
			    (got_pri)? inet_ntop(AF_INET, &_this->ipcp.dns_pri,
			    buf0, sizeof(buf0)) : "",
			    (got_pri != 0 && got_sec != 0)? "," : "",
			    (got_sec)? inet_ntop(AF_INET, &_this->ipcp.dns_sec,
			    buf1, sizeof(buf1)) : "",
			    ((got_pri + got_sec) > 1)? "are" : "is");
	}
	if (!ppp_ipcp(_this)->nbns_configured) {
		got_pri = got_sec = 0;
		if (radius_get_vs_ipv4_attr(pkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_PRIMARY_NBNS_SERVER, &ip4) == 0) {
			got_pri = 1;
			_this->ipcp.nbns_pri = ip4;
		}
		if (radius_get_vs_ipv4_attr(pkt, RADIUS_VENDOR_MICROSOFT,
		    RADIUS_VTYPE_MS_SECONDARY_NBNS_SERVER, &ip4) == 0) {
			got_sec = 1;
			_this->ipcp.nbns_sec = ip4;
		}
		if (got_pri || got_sec)
			ppp_log(_this, LOG_INFO, "NBNS server address%s "
			    "(%s%s%s) %s configured by RADIUS server",
			    ((got_pri + got_sec) > 1)? "es" : "",
			    (got_pri)? inet_ntop(AF_INET, &_this->ipcp.nbns_pri,
			    buf0, sizeof(buf0)) : "",
			    (got_pri != 0 && got_sec != 0)? "," : "",
			    (got_sec)? inet_ntop(AF_INET, &_this->ipcp.nbns_sec,
			    buf1, sizeof(buf1)) : "",
			    ((got_pri + got_sec) > 1)? "are" : "is");
	}
}

/***********************************************************************
 * RADIUS Accounting Events
 ***********************************************************************/

/** Called by PPP on start */
void
npppd_ppp_radius_acct_start(npppd *pppd, npppd_ppp *ppp)
{
	NPPPD_RADIUS_DBG((ppp, LOG_INFO, "%s()", __func__));

	if (ppp->realm == NULL || !npppd_ppp_is_realm_radius(pppd, ppp))
		return;
	radius_acct_request(pppd, ppp, 0);
}

/** Called by PPP on stop*/
void
npppd_ppp_radius_acct_stop(npppd *pppd, npppd_ppp *ppp)
{
	NPPPD_RADIUS_DBG((ppp, LOG_INFO, "%s()", __func__));

	if (ppp->realm == NULL || !npppd_ppp_is_realm_radius(pppd, ppp))
		return;
	radius_acct_request(pppd, ppp, 1);
}

/** Called by radius_req.c */
static void
npppd_ppp_radius_acct_reqcb(void *context, RADIUS_PACKET *pkt, int flags,
    RADIUS_REQUEST_CTX ctx)
{
	u_int ppp_id;

	ppp_id = (uintptr_t)context;
	if ((flags & RADIUS_REQUEST_TIMEOUT) != 0) {
		log_printf(LOG_WARNING, "ppp id=%u radius accounting request "
		    "failed: no response from the server.", ppp_id);
	}
	else if ((flags & RADIUS_REQUEST_ERROR) != 0)
		log_printf(LOG_WARNING, "ppp id=%u radius accounting request "
		    "failed: %m", ppp_id);
	else if ((flags & RADIUS_REQUEST_CHECK_AUTHENTICATOR_NO_CHECK) == 0 &&
	    (flags & RADIUS_REQUEST_CHECK_AUTHENTICATOR_OK) == 0)
		log_printf(LOG_WARNING, "ppp id=%d radius accounting request "
		    "failed: the server responses with bad authenticator",
		    ppp_id);
	else {
#ifdef NPPPD_RADIUS_DEBUG
		log_printf(LOG_DEBUG, "ppp id=%u radius accounting request "
		    "succeeded.", ppp_id);
#endif
		return;
		/* NOTREACHED */
	}
	if (radius_request_can_failover(ctx)) {
		if (radius_request_failover(ctx) == 0) {
			struct sockaddr *sa;
			char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

			sa = radius_get_server_address(ctx);
			if (getnameinfo(sa, sa->sa_len, hbuf, sizeof(hbuf),
			    sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV)
			    != 0) {
				strlcpy(hbuf, "unknown", sizeof(hbuf));
				strlcpy(sbuf, "", sizeof(sbuf));
			}
			log_printf(LOG_DEBUG, "ppp id=%u "
			    "fail over to %s:%s for radius accounting request",
			    ppp_id, hbuf, sbuf);
		} else {
			log_printf(LOG_WARNING, "ppp id=%u "
			    "failed to fail over for radius accounting request",
			    ppp_id);
		}
	}
}

/***********************************************************************
 * RADIUS attributes
 ***********************************************************************/
#define	ATTR_INT32(_a,_v)						\
	do {								\
		if (radius_put_uint32_attr(radpkt, (_a), (_v)) != 0)	\
			goto fail; 					\
	} while (0 /* CONSTCOND */)
#define	ATTR_STR(_a,_v)							\
	do {								\
		if (radius_put_string_attr(radpkt, (_a), (_v)) != 0)	\
		    goto fail; 					\
	} while (0 /* CONSTCOND */)

static int
radius_acct_request(npppd *pppd, npppd_ppp *ppp, int stop)
{
	RADIUS_PACKET *radpkt;
	RADIUS_REQUEST_CTX radctx;
	radius_req_setting *rad_setting;
	char buf[128];

	if (ppp->username[0] == '\0')
		return 0;

	radpkt = NULL;
	radctx = NULL;
	rad_setting = npppd_auth_radius_get_radius_acct_setting(ppp->realm);
	if (!radius_req_setting_has_server(rad_setting))
		return 0;
	if ((radpkt = radius_new_request_packet(RADIUS_CODE_ACCOUNTING_REQUEST))
	    == NULL)
		goto fail;

	if (radius_prepare(rad_setting, (void *)(uintptr_t)ppp->id, &radctx,
	    npppd_ppp_radius_acct_reqcb) != 0)
		goto fail;

    /* NAS Information */
	/*
	 * RFC 2865 "5.4.  NAS-IP-Address" or RFC 3162 "2.1. NAS-IPv6-Address"
	 */
	if (radius_prepare_nas_address(rad_setting, radpkt) != 0)
		goto fail;

	/* RFC 2865 "5.41. NAS-Port-Type" */
	ATTR_INT32(RADIUS_TYPE_NAS_PORT_TYPE, RADIUS_NAS_PORT_TYPE_VIRTUAL);

	/* RFC 2865 "5.5. NAS-Port" */
	ATTR_INT32(RADIUS_TYPE_NAS_PORT, ppp->id);
	    /* npppd has no physical / virtual ports in design. */

	/* RFC 2865  5.32. NAS-Identifier */
	ATTR_STR(RADIUS_TYPE_NAS_IDENTIFIER, pppd->conf.nas_id);

	/* RFC 2865 5.31. Calling-Station-Id */
	if (ppp->calling_number[0] != '\0')
		ATTR_STR(RADIUS_TYPE_CALLING_STATION_ID, ppp->calling_number);

    /* Tunnel Protocol Information */
	switch (ppp->tunnel_type) {
	case NPPPD_TUNNEL_L2TP:
		/* RFC 2868 3.1. Tunnel-Type */
		ATTR_INT32(RADIUS_TYPE_TUNNEL_TYPE, RADIUS_TUNNEL_TYPE_L2TP);
		if (l2tp_put_tunnel_attributes(radpkt, ppp->phy_context) != 0)
			goto fail;
		break;
	case NPPPD_TUNNEL_PPTP:
		/* RFC 2868 3.1. Tunnel-Type */
		ATTR_INT32(RADIUS_TYPE_TUNNEL_TYPE, RADIUS_TUNNEL_TYPE_PPTP);
		if (pptp_put_tunnel_attributes(radpkt, ppp->phy_context) != 0)
			goto fail;
		break;
	}

    /* Framed Protocol (PPP) Information */
	/* RFC 2865 5.1 User-Name */
	ATTR_STR(RADIUS_TYPE_USER_NAME, ppp->username);

	/* RFC 2865 "5.7. Service-Type" */
	ATTR_INT32(RADIUS_TYPE_SERVICE_TYPE, RADIUS_SERVICE_TYPE_FRAMED);

	/* RFC 2865 "5.8. Framed-Protocol" */
	ATTR_INT32(RADIUS_TYPE_FRAMED_PROTOCOL, RADIUS_FRAMED_PROTOCOL_PPP);

	/* RFC 2865 "5.8. Framed-IP-Address" */
	if (ppp->acct_framed_ip_address.s_addr != INADDR_ANY)
		ATTR_INT32(RADIUS_TYPE_FRAMED_IP_ADDRESS,
		    ntohl(ppp->acct_framed_ip_address.s_addr));

    /* Accounting */
	/* RFC 2866  5.1. Acct-Status-Type */
	ATTR_INT32(RADIUS_TYPE_ACCT_STATUS_TYPE, (stop)
	    ? RADIUS_ACCT_STATUS_TYPE_STOP : RADIUS_ACCT_STATUS_TYPE_START);

	/* RFC 2866  5.2.  Acct-Delay-Time */
	ATTR_INT32(RADIUS_TYPE_ACCT_DELAY_TIME, 0);

	if (stop) {
		/* RFC 2866  5.3 Acct-Input-Octets */
		ATTR_INT32(RADIUS_TYPE_ACCT_INPUT_OCTETS,
		    (uint32_t)(ppp->ibytes & 0xFFFFFFFFU));	/* LSB 32bit */

		/* RFC 2866  5.4 Acct-Output-Octets */
		ATTR_INT32(RADIUS_TYPE_ACCT_OUTPUT_OCTETS,
		    (uint32_t)(ppp->obytes & 0xFFFFFFFFU));	/* LSB 32bit */
	}

	/* RFC 2866  5.5 Acct-Session-Id */
	snprintf(buf, sizeof(buf), "%08X%08X", pppd->boot_id, ppp->id);
	ATTR_STR(RADIUS_TYPE_ACCT_SESSION_ID, buf);

	/* RFC 2866 5.6.  Acct-Authentic */
	ATTR_INT32(RADIUS_TYPE_ACCT_AUTHENTIC, RADIUS_ACCT_AUTHENTIC_RADIUS);

	if (stop) {
		/* RFC 2866 5.7. Acct-Session-Time */
		ATTR_INT32(RADIUS_TYPE_ACCT_SESSION_TIME,
		    ppp->end_monotime - ppp->start_monotime);

		/* RFC 2866  5.8 Acct-Input-Packets */
		ATTR_INT32(RADIUS_TYPE_ACCT_INPUT_PACKETS, ppp->ipackets);

		/* RFC 2866  5.9 Acct-Output-Packets */
		ATTR_INT32(RADIUS_TYPE_ACCT_OUTPUT_PACKETS, ppp->opackets);

		/* RFC 2866  5.10. Acct-Terminate-Cause */
		if (ppp->terminate_cause != 0)
			ATTR_INT32(RADIUS_TYPE_ACCT_TERMINATE_CAUSE,
			    ppp->terminate_cause);

		/* RFC 2869  5.1 Acct-Input-Gigawords */
		ATTR_INT32(RADIUS_TYPE_ACCT_INPUT_GIGAWORDS, ppp->ibytes >> 32);

		/* RFC 2869  5.2 Acct-Output-Gigawords */
		ATTR_INT32(RADIUS_TYPE_ACCT_OUTPUT_GIGAWORDS,
		    ppp->obytes >> 32);
	}

	/* Send the request */
	radius_request(radctx, radpkt);

	return 0;

fail:
	ppp_log(ppp, LOG_WARNING, "radius accounting request failed: %m");

	if (radctx != NULL)
		radius_cancel_request(radctx);
	if (radpkt != NULL)
		radius_delete_packet(radpkt);

	return -1;
}

void
radius_acct_on(npppd *pppd, radius_req_setting *rad_setting)
{
	RADIUS_REQUEST_CTX radctx = NULL;
	RADIUS_PACKET *radpkt = NULL;

	if (!radius_req_setting_has_server(rad_setting))
		return;
	if ((radpkt = radius_new_request_packet(RADIUS_CODE_ACCOUNTING_REQUEST))
	    == NULL)
		goto fail;

	if (radius_prepare(rad_setting, NULL, &radctx, radius_acct_on_cb) != 0)
		goto fail;

	/*
	 * RFC 2865 "5.4.  NAS-IP-Address" or RFC 3162 "2.1. NAS-IPv6-Address"
	 */
	if (radius_prepare_nas_address(rad_setting, radpkt) != 0)
		goto fail;

	/* RFC 2865 "5.41. NAS-Port-Type" */
	ATTR_INT32(RADIUS_TYPE_NAS_PORT_TYPE, RADIUS_NAS_PORT_TYPE_VIRTUAL);

	/* RFC 2866  5.1. Acct-Status-Type */
	ATTR_INT32(RADIUS_TYPE_ACCT_STATUS_TYPE, RADIUS_ACCT_STATUS_TYPE_ACCT_ON);
	/* RFC 2865  5.32. NAS-Identifier */
	ATTR_STR(RADIUS_TYPE_NAS_IDENTIFIER, pppd->conf.nas_id);

	/* Send the request */
	radius_request(radctx, radpkt);

	return;
 fail:
	if (radctx != NULL)
		radius_cancel_request(radctx);
	if (radpkt != NULL)
		radius_delete_packet(radpkt);
}

static void
radius_acct_on_cb(void *context, RADIUS_PACKET *pkt, int flags,
    RADIUS_REQUEST_CTX ctx)
{
	if ((flags & (RADIUS_REQUEST_TIMEOUT | RADIUS_REQUEST_ERROR)) != 0)
		radius_request_failover(ctx);
}

#ifdef USE_NPPPD_PPTP
#include "pptp.h"
#endif

static int
pptp_put_tunnel_attributes(RADIUS_PACKET *radpkt, void *call0)
{
#ifdef USE_NPPPD_PPTP
	pptp_call *call = call0;
	pptp_ctrl *ctrl;
	char hbuf[NI_MAXHOST], buf[128];

	ctrl = call->ctrl;

	/* RFC 2868  3.2.  Tunnel-Medium-Type */
	switch (ctrl->peer.ss_family) {
	case AF_INET:
		ATTR_INT32(RADIUS_TYPE_TUNNEL_MEDIUM_TYPE,
		    RADIUS_TUNNEL_MEDIUM_TYPE_IPV4);
		break;

	case AF_INET6:
		ATTR_INT32(RADIUS_TYPE_TUNNEL_MEDIUM_TYPE,
		    RADIUS_TUNNEL_MEDIUM_TYPE_IPV6);
		break;

	default:
		return -1;
	}

	/* RFC 2868  3.3.  Tunnel-Client-Endpoint */
	if (getnameinfo((struct sockaddr *)&ctrl->peer, ctrl->peer.ss_len, hbuf,
	    sizeof(hbuf), NULL, 0, NI_NUMERICHOST))
		return 1;
	ATTR_STR(RADIUS_TYPE_TUNNEL_CLIENT_ENDPOINT, hbuf);

	/* RFC 2868  3.4.  Tunnel-Server-Endpoint */
	if (getnameinfo((struct sockaddr *)&ctrl->our, ctrl->our.ss_len, hbuf,
	    sizeof(hbuf), NULL, 0, NI_NUMERICHOST))
		return 1;
	ATTR_STR(RADIUS_TYPE_TUNNEL_SERVER_ENDPOINT, hbuf);

	/* RFC 2868  3.7.  Tunnel-Assignment-ID */
	snprintf(buf, sizeof(buf), "PPTP-CALL-%d", call->id);
	ATTR_STR(RADIUS_TYPE_TUNNEL_ASSIGNMENT_ID, buf);

	/* RFC 2867  4.1. Acct-Tunnel-Connection   */
	snprintf(buf, sizeof(buf), "PPTP-CTRL-%d", ctrl->id);
	ATTR_STR(RADIUS_TYPE_ACCT_TUNNEL_CONNECTION, buf);

	return 0;
fail:
#endif
	return 1;
}

#ifdef USE_NPPPD_L2TP
#include "l2tp.h"
#endif

static int
l2tp_put_tunnel_attributes(RADIUS_PACKET *radpkt, void *call0)
{
#ifdef USE_NPPPD_L2TP
	l2tp_call *call = call0;
	l2tp_ctrl *ctrl;
	char hbuf[NI_MAXHOST], buf[128];

	ctrl = call->ctrl;

	/* RFC 2868  3.2.  Tunnel-Medium-Type */
	switch (ctrl->peer.ss_family) {
	case AF_INET:
		ATTR_INT32(RADIUS_TYPE_TUNNEL_MEDIUM_TYPE,
		    RADIUS_TUNNEL_MEDIUM_TYPE_IPV4);
		break;

	case AF_INET6:
		ATTR_INT32(RADIUS_TYPE_TUNNEL_MEDIUM_TYPE,
		    RADIUS_TUNNEL_MEDIUM_TYPE_IPV6);
		break;

	default:
		return -1;
	}

	/* RFC 2868  3.3.  Tunnel-Client-Endpoint */
	if (getnameinfo((struct sockaddr *)&ctrl->peer, ctrl->peer.ss_len, hbuf,
	    sizeof(hbuf), NULL, 0, NI_NUMERICHOST))
		return 1;
	ATTR_STR(RADIUS_TYPE_TUNNEL_CLIENT_ENDPOINT, hbuf);

	/* RFC 2868  3.4.  Tunnel-Server-Endpoint */
	if (getnameinfo((struct sockaddr *)&ctrl->sock, ctrl->sock.ss_len, hbuf,
	    sizeof(hbuf), NULL, 0, NI_NUMERICHOST))
		return 1;
	ATTR_STR(RADIUS_TYPE_TUNNEL_SERVER_ENDPOINT, hbuf);

	/* RFC 2868  3.7.  Tunnel-Assignment-ID */
	snprintf(buf, sizeof(buf), "L2TP-CALL-%d", call->id);
	ATTR_STR(RADIUS_TYPE_TUNNEL_ASSIGNMENT_ID, buf);

	/* RFC 2867  4.1. Acct-Tunnel-Connection   */
	snprintf(buf, sizeof(buf), "L2TP-CTRL-%d", ctrl->id);
	ATTR_STR(RADIUS_TYPE_ACCT_TUNNEL_CONNECTION, buf);

	return 0;
fail:
#endif
	return 1;
}

/**
 * Set RADIUS attributes for RADIUS authentication request.
 * Return 0 on success.
 */
int
ppp_set_radius_attrs_for_authreq(npppd_ppp *_this,
    radius_req_setting *rad_setting, RADIUS_PACKET *radpkt)
{
	/* RFC 2865 "5.4 NAS-IP-Address" or RFC3162 "2.1. NAS-IPv6-Address" */
	if (radius_prepare_nas_address(rad_setting, radpkt) != 0)
		goto fail;

	/* RFC 2865 "5.6. Service-Type" */
	if (radius_put_uint32_attr(radpkt, RADIUS_TYPE_SERVICE_TYPE,
	    RADIUS_SERVICE_TYPE_FRAMED) != 0)
		goto fail;

	/* RFC 2865 "5.7. Framed-Protocol" */
	if (radius_put_uint32_attr(radpkt, RADIUS_TYPE_FRAMED_PROTOCOL,
	    RADIUS_FRAMED_PROTOCOL_PPP) != 0)
		goto fail;

	if (_this->calling_number[0] != '\0') {
		if (radius_put_string_attr(radpkt,
		    RADIUS_TYPE_CALLING_STATION_ID, _this->calling_number) != 0)
			return 1;
	}
	/* RFC 2865 "5.5.  NAS-Port" */
	if (radius_put_uint32_attr(radpkt, RADIUS_TYPE_NAS_PORT, _this->id)
	    != 0)
		goto fail;

	return 0;
fail:
	return 1;
}

/***********************************************************************
 * Dynamic Authorization Extensions for RADIUS
 ***********************************************************************/
static int	npppd_radius_dae_listen_start(struct npppd_radius_dae_listen *);
static void	npppd_radius_dae_on_event(int, short, void *);
static void	npppd_radius_dae_listen_stop(struct npppd_radius_dae_listen *);

void
npppd_radius_dae_init(npppd *_this)
{
	struct npppd_radius_dae_listens	 listens;
	struct npppd_radius_dae_listen	*listen, *listent;
	struct radlistenconf		*listenconf;

	TAILQ_INIT(&listens);

	TAILQ_FOREACH(listenconf, &_this->conf.raddaelistenconfs, entry) {
		TAILQ_FOREACH_SAFE(listen, &_this->raddae_listens, entry,
		    listent) {
			if ((listen->addr.sin4.sin_family == AF_INET &&
			    listenconf->addr.sin4.sin_family == AF_INET &&
			    memcmp(&listen->addr.sin4, &listenconf->addr.sin4,
			    sizeof(struct sockaddr_in)) == 0) ||
			    (listen->addr.sin6.sin6_family == AF_INET6 &&
			    listenconf->addr.sin6.sin6_family == AF_INET6 &&
			    memcmp(&listen->addr.sin6, &listenconf->addr.sin6,
			    sizeof(struct sockaddr_in6)) == 0))
				break;
		}
		if (listen != NULL)
			/* keep using this */
			TAILQ_REMOVE(&_this->raddae_listens, listen, entry);
		else {
			if ((listen = calloc(1, sizeof(*listen))) == NULL) {
				log_printf(LOG_ERR, "%s: calloc failed: %m",
				    __func__);
				goto fail;
			}
			listen->pppd = _this;
			listen->sock = -1;
			if (listenconf->addr.sin4.sin_family == AF_INET)
				listen->addr.sin4 = listenconf->addr.sin4;
			else
				listen->addr.sin6 = listenconf->addr.sin6;
		}
		TAILQ_INSERT_TAIL(&listens, listen, entry);
	}

	/* listen on the new addresses */
	TAILQ_FOREACH(listen, &listens, entry) {
		if (listen->sock == -1)
			npppd_radius_dae_listen_start(listen);
	}

	/* stop listening on the old addresses */
	TAILQ_FOREACH_SAFE(listen, &_this->raddae_listens, entry, listent) {
		TAILQ_REMOVE(&_this->raddae_listens, listen, entry);
		npppd_radius_dae_listen_stop(listen);
		free(listen);
	}
 fail:
	TAILQ_CONCAT(&_this->raddae_listens, &listens, entry);

	return;
}

void
npppd_radius_dae_fini(npppd *_this)
{
	struct npppd_radius_dae_listen *listen, *listent;

	TAILQ_FOREACH_SAFE(listen, &_this->raddae_listens, entry, listent) {
		TAILQ_REMOVE(&_this->raddae_listens, listen, entry);
		npppd_radius_dae_listen_stop(listen);
		free(listen);
	}
}

int
npppd_radius_dae_listen_start(struct npppd_radius_dae_listen *listen)
{
	char	 buf[80];
	int	 sock = -1, on = 1;

	if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
		log_printf(LOG_ERR, "%s: socket(): %m", __func__);
		goto on_error;
	}
	on = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
		log_printf(LOG_WARNING, "%s: setsockopt(,,SO_REUSEADDR): %m",
		    __func__);
		goto on_error;
	}
	if (bind(sock, (struct sockaddr *)&listen->addr,
	    listen->addr.sin4.sin_len) == -1) {
		log_printf(LOG_ERR, "%s: bind(): %m", __func__);
		goto on_error;
	}

	listen->sock = sock;
	event_set(&listen->evsock, listen->sock, EV_READ | EV_PERSIST,
	    npppd_radius_dae_on_event, listen);
	event_add(&listen->evsock, NULL);
	log_printf(LOG_INFO, "radius Listening %s/udp (DAE)",
	    addrport_tostring((struct sockaddr *)&listen->addr,
	    listen->addr.sin4.sin_len, buf, sizeof(buf)));

	return (0);
 on_error:
	if (sock >= 0)
		close(sock);

	return (-1);
}

void
npppd_radius_dae_on_event(int fd, short ev, void *ctx)
{
	char				 buf[80], attr[256], username[256];
	char				*endp;
	const char			*reason, *nakcause = NULL;
	struct npppd_radius_dae_listen	*listen = ctx;
	struct radclientconf		*client;
	npppd				*_this = listen->pppd;
	RADIUS_PACKET			*req = NULL, *res = NULL;
	struct sockaddr_storage		 ss;
	socklen_t			 sslen;
	unsigned long long		 ppp_id;
	int				 code, n = 0;
	uint32_t			 cause = 0;
	struct in_addr			 ina;
	slist				*users;
	npppd_ppp			*ppp;

	reason = "disconnect requested";
	sslen = sizeof(ss);
	req = radius_recvfrom(listen->sock, 0, (struct sockaddr *)&ss, &sslen);
	if (req == NULL) {
		log_printf(LOG_WARNING, "%s: receiving a RADIUS message "
		    "failed: %m", __func__);
		return;
	}
	TAILQ_FOREACH(client, &_this->conf.raddaeclientconfs, entry) {
		if (ss.ss_family == AF_INET &&
		    ((struct sockaddr_in *)&ss)->sin_addr.s_addr ==
		    client->addr.sin4.sin_addr.s_addr)
			break;
		else if (ss.ss_family == AF_INET6 &&
		    IN6_ARE_ADDR_EQUAL(&((struct sockaddr_in6 *)&ss)->sin6_addr,
		    &client->addr.sin6.sin6_addr))
			break;
	}

	if (client == NULL) {
		log_printf(LOG_WARNING, "radius received a RADIUS message from "
		    "%s: unknown client", addrport_tostring(
		    (struct sockaddr *)&ss, ss.ss_len, buf, sizeof(buf)));
		goto out;
	}

	if (radius_check_accounting_request_authenticator(req,
	    client->secret) != 0) {
		log_printf(LOG_WARNING, "radius received an invalid RADIUS "
		    "message from %s: bad response authenticator",
		    addrport_tostring(
		    (struct sockaddr *)&ss, ss.ss_len, buf, sizeof(buf)));
		goto out;
	}
	if ((code = radius_get_code(req)) != RADIUS_CODE_DISCONNECT_REQUEST) {
		/* Code other than Disconnect-Request is not supported */
		if (code == RADIUS_CODE_COA_REQUEST) {
			code = RADIUS_CODE_COA_NAK;
			cause = RADIUS_ERROR_CAUSE_ADMINISTRATIVELY_PROHIBITED;
			nakcause = "Coa-Request is not supported";
			goto send;
		}
		log_printf(LOG_WARNING, "radius received an invalid RADIUS "
		    "message from %s: unknown code %d",
		    addrport_tostring((struct sockaddr *)&ss, ss.ss_len, buf,
		    sizeof(buf)), code);
		goto out;
	}

	log_printf(LOG_INFO, "radius received Disconnect-Request from %s",
	    addrport_tostring((struct sockaddr *)&ss, ss.ss_len, buf,
	    sizeof(buf)));

	if (radius_get_string_attr(req, RADIUS_TYPE_NAS_IDENTIFIER, attr,
	    sizeof(attr)) == 0 && strcmp(attr, _this->conf.nas_id) != 0) {
		cause = RADIUS_ERROR_CAUSE_NAS_IDENTIFICATION_MISMATCH;
		nakcause = "NAS Identifier is not matched";
		goto search_done;
	}

	/* prepare User-Name attribute */
	memset(username, 0, sizeof(username));
	radius_get_string_attr(req, RADIUS_TYPE_USER_NAME, username,
	    sizeof(username));

	/* Our Session-Id is represented in "%08X%08x" (boot_id, ppp_id) */
	snprintf(buf, sizeof(buf), "%08X", _this->boot_id);
	if (radius_get_string_attr(req, RADIUS_TYPE_ACCT_SESSION_ID, attr,
	    sizeof(attr)) == 0) {
		ppp = NULL;
		/* the client is to disconnect a session */
		if (strlen(attr) != 16 || strncmp(buf, attr, 8) != 0) {
			cause = RADIUS_ERROR_CAUSE_INVALID_ATTRIBUTE_VALUE;
			nakcause = "Session-Id is wrong";
			goto search_done;
		}
		ppp_id = strtoull(attr + 8, &endp, 16);
		if (*endp != '\0' || errno == ERANGE || ppp_id == ULLONG_MAX) {
			cause = RADIUS_ERROR_CAUSE_INVALID_ATTRIBUTE_VALUE;
			nakcause = "Session-Id is invalid";
			goto search_done;
		}
		if ((ppp = npppd_get_ppp_by_id(_this, ppp_id)) == NULL)
			goto search_done;
		if (username[0] != '\0' &&
		    strcmp(username, ppp->username) != 0) {
			/* specified User-Name attribute is mismatched */
			cause = RADIUS_ERROR_CAUSE_INVALID_ATTRIBUTE_VALUE;
			nakcause = "User-Name is not matched";
			goto search_done;
		}
		ppp_stop(ppp, reason);
		n++;
	} else if (username[0] != '\0') {
		users = npppd_get_ppp_by_user(_this, username);
		if (users == NULL)
			goto search_done;
		memset(&ina, 0, sizeof(ina));
		radius_get_uint32_attr(req, RADIUS_TYPE_FRAMED_IP_ADDRESS,
		    &ina.s_addr);
		slist_itr_first(users);
		while ((ppp = slist_itr_next(users)) != NULL) {
			if (ntohl(ina.s_addr) != 0 &&
			    ina.s_addr != ppp->ppp_framed_ip_address.s_addr)
				continue;
			ppp_stop(ppp, reason);
			n++;
		}
	} else if (radius_get_uint32_attr(req, RADIUS_TYPE_FRAMED_IP_ADDRESS,
	    &ina.s_addr) == 0) {
		ppp = npppd_get_ppp_by_ip(_this, ina);
		if (ppp != NULL) {
			ppp_stop(ppp, reason);
			n++;
		}
	}
 search_done:
	if (n > 0)
		code = RADIUS_CODE_DISCONNECT_ACK;
	else {
		if (nakcause == NULL)
			nakcause = "session not found";
		if (cause == 0)
			cause = RADIUS_ERROR_CAUSE_SESSION_NOT_FOUND;
		code = RADIUS_CODE_DISCONNECT_NAK;
	}
 send:
	res = radius_new_response_packet(code, req);
	if (res == NULL) {
		log_printf(LOG_WARNING, "%s: radius_new_response_packet: %m",
		    __func__);
		goto out;
	}
	if (cause != 0)
		radius_put_uint32_attr(res, RADIUS_TYPE_ERROR_CAUSE, cause);
	radius_set_response_authenticator(res, client->secret);
	if (radius_sendto(listen->sock, res, 0, (struct sockaddr *)&ss, sslen)
	    == -1)
		log_printf(LOG_WARNING, "%s: sendto(): %m", __func__);
	log_printf(LOG_INFO, "radius send %s to %s%s%s",
	    (code == RADIUS_CODE_DISCONNECT_ACK)? "Disconnect-ACK" :
	    (code == RADIUS_CODE_DISCONNECT_NAK)? "Disconnect-NAK" : "CoA-NAK",
	    addrport_tostring((struct sockaddr *)&ss, ss.ss_len, buf,
	    sizeof(buf)), (nakcause)? ": " : "", (nakcause)? nakcause : "");
 out:
	radius_delete_packet(req);
	if (res != NULL)
		radius_delete_packet(res);
}

void
npppd_radius_dae_listen_stop(struct npppd_radius_dae_listen *listen)
{
	char	 buf[80];

	if (listen->sock >= 0) {
		log_printf(LOG_INFO, "radius Shutdown %s/udp (DAE)",
		    addrport_tostring((struct sockaddr *)&listen->addr,
		    listen->addr.sin4.sin_len, buf, sizeof(buf)));
		event_del(&listen->evsock);
		close(listen->sock);
		listen->sock = -1;
	}
}
