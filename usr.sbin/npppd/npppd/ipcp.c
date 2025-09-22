/*	$OpenBSD: ipcp.c,v 1.6 2019/02/27 04:52:19 denis Exp $ */

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
 * This is an implementation of IPCP. This code is currently implemented
 * as network service provider, and the peer is forced to obey our proposal.
 */
/*
 * RFC 1332, 1877
 */
/* $Id: ipcp.c,v 1.6 2019/02/27 04:52:19 denis Exp $ */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <net/if_dl.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <event.h>

#include "debugutil.h"
#include "npppd.h"

#ifdef	IPCP_DEBUG
#define	IPCP_DBG(x)	fsm_log x
#define	IPCP_ASSERT(x)	ASSERT(x)
#else
#define	IPCP_DBG(x)
#define	IPCP_ASSERT(x)
#endif


#define	IPCP_IP_ADDRESSES	1
#define	IPCP_IP_COMP		2
#define	IPCP_IP_ADDRESS		3
#define	IPCP_PRI_DNS		129	/* 0x81 */
#define	IPCP_PRI_NBNS		130	/* 0x82 */
#define	IPCP_SEC_DNS		131	/* 0x83 */
#define	IPCP_SEC_NBNS		132	/* 0x84 */

#define u32maskcmp(mask, a, b) (((a) & (mask)) == ((b) & (mask)))

static void  ipcp_resetci (fsm *);
static int   ipcp_cilen (fsm *);
static void  ipcp_addci (fsm *, u_char *, int *);
static int   ipcp_ackci (fsm *, u_char *, int);
static int   ipcp_nakci (fsm *, u_char *, int);
static int   ipcp_rejci (fsm *, u_char *, int);
static int   ipcp_reqci (fsm *, u_char *, int *, int);
static void  ipcp_open (fsm *);
static void  ipcp_close (fsm *);
static void  ipcp_start (fsm *);
static void  ipcp_stop (fsm *);

static struct fsm_callbacks ipcp_callbacks = {
	ipcp_resetci,	/* Reset our Configuration Information */
	ipcp_cilen,	/* Length of our Configuration Information */
	ipcp_addci,	/* Add our Configuration Information */
	ipcp_ackci,	/* ACK our Configuration Information */
	ipcp_nakci,	/* NAK our Configuration Information */
	ipcp_rejci,	/* Reject our Configuration Information */
	ipcp_reqci,	/* Request peer's Configuration Information */

	ipcp_open,	/* Called when fsm reaches OPENED state */
	ipcp_close,	/* Called when fsm leaves OPENED state */
	ipcp_start,	/* Called when we want the lower layer up */
	ipcp_stop,	/* Called when we want the lower layer down */
	NULL,		/* Called when Protocol-Reject received */
	NULL,		/* Retransmission is necessary */
	NULL,		/* Called to handle LCP-specific codes */
	"ipcp"		/* String name of protocol */
};

/**
 * Initialize {@link ::_ipcp IPCP instance }.
 */
void
ipcp_init(ipcp *_this, npppd_ppp *ppp)
{
	struct tunnconf *conf;

	memset(_this, 0, sizeof(ipcp));

	_this->ppp = ppp;
	_this->fsm.ppp = ppp;

	fsm_init(&_this->fsm);

	_this->fsm.callbacks = &ipcp_callbacks;
	_this->fsm.protocol = PPP_PROTO_NCP | NCP_IPCP;

	conf = ppp_get_tunnconf(ppp);
	PPP_FSM_CONFIG(&_this->fsm, timeouttime, conf->ipcp_timeout);
	PPP_FSM_CONFIG(&_this->fsm, maxconfreqtransmits,
	    conf->ipcp_max_configure);
	PPP_FSM_CONFIG(&_this->fsm, maxtermtransmits,
	    conf->ipcp_max_terminate);
	PPP_FSM_CONFIG(&_this->fsm, maxnakloops,
	    conf->ipcp_max_nak_loop);
}

static void
ipcp_resetci(fsm *f)
{
	IPCP_DBG((f, LOG_DEBUG, "%s", __func__));
	if (npppd_prepare_ip(f->ppp->pppd, f->ppp) != 0) {
		fsm_log(f, LOG_ERR, "failed to assign ip address.");
		ppp_stop(f->ppp, NULL);
	}
}

static int
ipcp_cilen(fsm *f)
{
	IPCP_DBG((f, LOG_DEBUG, "%s", __func__));
	return f->ppp->mru;
}

static void
ipcp_addci(fsm *f, u_char *pktp, int *lpktp)
{
	u_char *pktp0;

	IPCP_DBG((f, LOG_DEBUG, "%s", __func__));
	pktp0 = pktp;

	PUTCHAR(IPCP_IP_ADDRESS, pktp);
	PUTCHAR(6, pktp);
	memcpy(pktp, &f->ppp->ipcp.ip4_our.s_addr, 4);
	pktp += 4;
	*lpktp = pktp - pktp0;
}


static int
ipcp_ackci(fsm *f, u_char *pktp, int lpkt)
{
	IPCP_DBG((f, LOG_DEBUG, "%s", __func__));
	/* TODO */
	return -1;
}

static int
ipcp_nakci(fsm *f, u_char *pktp, int lpkt)
{
	IPCP_DBG((f, LOG_DEBUG, "%s", __func__));

	fsm_log(f, LOG_INFO, "Peer refused(ConfNak) our ip=%s.",
	    inet_ntoa(f->ppp->ipcp.ip4_our));
	fsm_close(f, NULL);
	return -1;
}

static int
ipcp_rejci(fsm *f, u_char *pktp, int lpkt)
{
	IPCP_DBG((f, LOG_DEBUG, "%s", __func__));

	fsm_log(f, LOG_INFO, "Peer refused(ConfRej) our ip=%s.",
	    inet_ntoa(f->ppp->ipcp.ip4_our));
	fsm_close(f, NULL);

	return 0;
}

static int
ipcp_reqci(fsm *f, u_char *pktp, int *lpktp, int reject_if_disagree)
{
	int type, len, rcode, lrej, lnak;
	u_char rejbuf0[256], nakbuf0[256], *nakbuf, *rejbuf, *pktp0;
	char buf0[256];
	struct in_addr ip_addr, *ip_addrp;
	npppd_ppp *ppp;
	npppd *_npppd;
	int ip_address_acked = 0;

	IPCP_DBG((f, LOG_DEBUG, "%s(reject_if_disagree=%d, nakloops=%d)",
	    __func__, reject_if_disagree, f->nakloops));
	ppp = f->ppp;
	_npppd = ppp->pppd;

	nakbuf = nakbuf0;
	rejbuf = rejbuf0;
	lrej = 0;
	lnak = 0;
	pktp0 = pktp;
	rcode = -1;

	if (*lpktp > 128) {
		rcode = CONFREJ;
		rejbuf = pktp;
		lrej = *lpktp;
		goto fail;
	}

#define	remlen()	(*lpktp - (pktp - pktp0))

	ip_address_acked = 0;
	while (remlen() >= 2) {
		GETCHAR(type, pktp);
		GETCHAR(len, pktp);
		if (len <= 0 || remlen() + 2 < len)
			goto fail;

		switch (type) {
		case IPCP_IP_ADDRESS:
		case IPCP_PRI_DNS:
		case IPCP_PRI_NBNS:
		case IPCP_SEC_DNS:
		case IPCP_SEC_NBNS:
			if (remlen() < 4)
				goto fail;
			GETLONG(ip_addr.s_addr, pktp);
			ip_addr.s_addr = htonl(ip_addr.s_addr);

			switch (type) {
			case IPCP_IP_ADDRESS:
				if (!ppp_ip_assigned(ppp)) {
					if (npppd_assign_ip_addr(ppp->pppd, ppp,
					    htonl(ip_addr.s_addr)) != 0 &&
					    npppd_assign_ip_addr(ppp->pppd, ppp,
					    INADDR_ANY) != 0) {
						/*
						 * The reason why it call with INADDR_ANY again here
						 * is to adapt the client expecting to fall back into
						 * dynamic allocation when user-select is allowed.
						 */
						pktp -= 4;
						goto do_reject;
					}
					strlcpy(buf0, inet_ntoa(ip_addr),
					    sizeof(buf0));
					fsm_log(f, LOG_INFO,
					    "IP Address peer=%s our=%s.", buf0,
					    inet_ntoa(
						ppp->ppp_framed_ip_address));
				}

				if (u32maskcmp(ppp->ppp_framed_ip_netmask
				    .s_addr, ip_addr.s_addr,
				    ppp->ppp_framed_ip_address.s_addr)) {
					/*
					 * In case of assigning network address, it obey
					 * peer's proposal if peer's IP-Address Option is
					 * included in network address to assign.
					 */
					ip_addrp = &ip_addr;
				} else {
					ip_addrp = &ppp->
					    ppp_framed_ip_address;
				}
				ip_address_acked = 1;
				break;
			case IPCP_PRI_DNS:
				ip_addrp = &ppp->ipcp.dns_pri;	break;
			case IPCP_SEC_DNS:
				ip_addrp = &ppp->ipcp.dns_sec;	break;
			case IPCP_PRI_NBNS:
				ip_addrp = &ppp->ipcp.nbns_pri;	break;
			case IPCP_SEC_NBNS:
				ip_addrp = &ppp->ipcp.nbns_sec;	break;
			default:
				ip_addrp = NULL;
			}

			if (ip_addrp == NULL ||
			    ip_addrp->s_addr == INADDR_NONE) {
				pktp -= 4;
				goto do_reject;
			}
			if (ip_addrp->s_addr != ip_addr.s_addr) {
				if (reject_if_disagree) {
					pktp -= 4;
					goto do_reject;
				}
				if (lrej > 0) {
				/* if there is a reject, will send Rej, not send Nak. */
				} else {
					PUTCHAR(type, nakbuf);
					PUTCHAR(6, nakbuf);
					PUTLONG(ntohl(ip_addrp->s_addr),
					    nakbuf);
					lnak += 6;
					rcode = CONFNAK;
				}
			}
			break;
		case IPCP_IP_COMP:
		case IPCP_IP_ADDRESSES:
		default:
			fsm_log(f, LOG_DEBUG, "Unhandled Option %02x %d", type,
			    len);
do_reject:
			pktp -= 2;
			memmove(rejbuf + lrej, pktp, len);
			lrej += len;
			pktp += len;
			rcode = CONFREJ;
		}
		continue;
	}
	if (rcode == -1)
		rcode = CONFACK;

fail:
	switch (rcode) {
	case CONFREJ:
		IPCP_DBG((f, LOG_DEBUG, "SendConfRej"));
		memmove(pktp0, rejbuf0, lrej);
		*lpktp = lrej;
		break;
	case CONFNAK:
		/*
		 * In case of Yamaha router is set "pp ppp ipcp ip-address off",
		 * it sends ConfReq without IP-Address Option.
		 * To quote RFC 1332:
		 * If negotiation about the remote IP-address is required, and
		 * the peer did not provide the option in its Configure-Request,
		 * the option SHOULD be appended to a Configure-Nak.
		 *
		 * Is any problem of overrunning 6 bytes of lpkt?
		 *  - In ppp.c, lpkt is allocated mru + 64 bytes. lpkt is less
		 *    than mru, so +6 is enough.
		 */
		if (!ip_address_acked) {
			/* It is mandatory to assign IP address. */
			if (!ppp_ip_assigned(ppp)) {
				if (npppd_assign_ip_addr(ppp->pppd, ppp,
				    INADDR_ANY) != 0) {
				    /* The log already put in npppd_assign_ip_addr(). */
				}
			}
			PUTCHAR(IPCP_IP_ADDRESS, nakbuf);
			PUTCHAR(6, nakbuf);
			PUTLONG(ntohl(ppp->ppp_framed_ip_address.s_addr),
			    nakbuf);
			lnak += 6;
		}
		IPCP_DBG((f, LOG_DEBUG, "SendConfNak"));
		memmove(pktp0, nakbuf0, lnak);
		*lpktp = lnak;
		break;
	case CONFACK:
		IPCP_DBG((f, LOG_DEBUG, "SendConfAck"));
		break;
	}

	return rcode;
#undef	remlen
}

static void
ipcp_open(fsm *f)
{
	if (!ppp_ip_assigned(f->ppp)) {
		fsm_log(f, LOG_INFO, "the ip-address option from the peer was "
		    "not agreed.");
		/*
		 * agreed without IP-Address Option.  try to assign static address.
		 */
		if (f->ppp->realm_framed_ip_address.s_addr
			    != INADDR_USER_SELECT &&
		    f->ppp->realm_framed_ip_address.s_addr
			    != INADDR_NAS_SELECT &&
		    f->ppp->realm_framed_ip_address.s_addr != 0) {
			npppd_assign_ip_addr(f->ppp->pppd, f->ppp, INADDR_ANY);
		}
	}
	if (!ppp_ip_assigned(f->ppp)) {
		fsm_log(f, LOG_NOTICE,
		    "IPCP opened but no IP address for the peer.");
		ppp_stop(f->ppp, NULL);
		return;
	}

	fsm_log(f, LOG_INFO, "logtype=Opened ip=%s assignType=%s",
	    inet_ntoa(f->ppp->ppp_framed_ip_address),
	    (f->ppp->assign_dynapool)? "dynamic" : "static");

	ppp_ipcp_opened(f->ppp);
}

static void
ipcp_close(fsm *f)
{
	IPCP_DBG((f, LOG_DEBUG, "%s", __func__));
}

static void
ipcp_start(fsm *f)
{
}

static void
ipcp_stop(fsm *f)
{
	fsm_log(f, LOG_INFO, "IPCP is stopped");
	ppp_stop(f->ppp, NULL);
}
