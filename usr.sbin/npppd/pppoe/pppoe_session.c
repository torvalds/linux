/*	$OpenBSD: pppoe_session.c,v 1.12 2021/03/29 03:54:40 yasuoka Exp $ */

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
 * Session management of PPPoE protocol
 * $Id: pppoe_session.c,v 1.12 2021/03/29 03:54:40 yasuoka Exp $
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <net/if.h>
#if defined(__NetBSD__)
#include <net/if_ether.h>
#else
#include <netinet/if_ether.h>
#endif
#include <net/if_dl.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <event.h>
#include <syslog.h>
#include <stdarg.h>

#include "hash.h"
#include "slist.h"
#include "debugutil.h"
#include "bytebuf.h"
#include "pppoe.h"
#include "pppoe_local.h"

#include "npppd.h"
#include "ppp.h"

#ifdef	PPPOE_SESSION_DEBUG
#define	PPPOE_SESSION_ASSERT(x)	ASSERT(x)
#define	PPPOE_SESSION_DBG(x)	pppoe_session_log x
#else
#define	PPPOE_SESSION_ASSERT(x)
#define	PPPOE_SESSION_DBG(x)
#endif

#define	pppoed_listener_this(sess)					\
	((pppoed_listener *)slist_get(&(sess)->pppoed->listener, 	\
	    (sess)->listener_index))

static void  pppoe_session_log (pppoe_session *, int, const char *, ...) __printflike(3,4);
static int   pppoe_session_send_PADS (pppoe_session *, struct pppoe_tlv *,
    struct pppoe_tlv *);
static int   pppoe_session_send_PADT (pppoe_session *);
static int   pppoe_session_ppp_output (npppd_ppp *, u_char *, int, int);
static void  pppoe_session_close_by_ppp(npppd_ppp *);
static int   pppoe_session_bind_ppp (pppoe_session *);
static void  pppoe_session_dispose_event(int, short, void *);

/* Initialize PPPoE session context */
int
pppoe_session_init(pppoe_session *_this, pppoed *_pppoed, int idx,
    int session_id, u_char *ether_addr)
{
	memset(_this, 0, sizeof(pppoe_session));

	_this->pppoed = _pppoed;
	_this->session_id = session_id;
	_this->listener_index = idx;
	memcpy(_this->ether_addr, ether_addr, ETHER_ADDR_LEN);

	memcpy(_this->ehdr.ether_dhost, ether_addr, ETHER_ADDR_LEN);
	memcpy(_this->ehdr.ether_shost, pppoe_session_sock_ether_addr(_this),
	    ETHER_ADDR_LEN);

	evtimer_set(&_this->ev_disposing, pppoe_session_dispose_event, _this);

	return 0;
}

/* Disconnect PPPoE session */
void
pppoe_session_disconnect(pppoe_session *_this)
{
	struct timeval tv;

	if (_this->state != PPPOE_SESSION_STATE_DISPOSING) {
		pppoe_session_send_PADT(_this);

		/* free process should be par event */
		timerclear(&tv);
		evtimer_add(&_this->ev_disposing, &tv);
		_this->state = PPPOE_SESSION_STATE_DISPOSING;
	}
	if (_this->ppp != NULL)
		ppp_phy_downed(_this->ppp);
}

/* Stop PPPoE session */
void
pppoe_session_stop(pppoe_session *_this)
{
	if (_this->state != PPPOE_SESSION_STATE_DISPOSING)
		pppoe_session_disconnect(_this);

}

/* Finish PPPoE session */
void
pppoe_session_fini(pppoe_session *_this)
{
	evtimer_del(&_this->ev_disposing);
}

/* call back function from event(3) */
static void
pppoe_session_dispose_event(int fd, short ev, void *ctx)
{
	pppoe_session *_this;

	_this = ctx;
	pppoed_pppoe_session_close_notify(_this->pppoed, _this);
}

/*
 * I/O
 */
void
pppoe_session_input(pppoe_session *_this, u_char *pkt, int lpkt)
{
	int rval;
	npppd_ppp *ppp;

	ppp = _this->ppp;
	if (_this->ppp == NULL)
		return;

	if (_this->state != PPPOE_SESSION_STATE_RUNNING)
		return;

	rval = ppp->recv_packet(ppp, pkt, lpkt, 0);
	if (_this->ppp == NULL)	/* ppp is freed */
		return;

	if (rval == 2) {
		/*
		 * Quit this function before statistics counter
		 * is processed when the packet will be processed by
		 * PIPEX. Because current NPPPD PPPOE implementation
		 * is receiving all packet from BPF even though the
		 * PIPEX will process it.
		 */
	} else if (rval != 0)  {
		ppp->ierrors++;
	} else {
		ppp->ipackets++;
		ppp->ibytes += lpkt;
	}

	return;
}

static int
pppoe_session_output(pppoe_session *_this, int is_disc, u_char *pkt,
    int lpkt)
{
	int sz, niov, tlen;
	struct iovec iov[4];
	struct pppoe_header pppoe0, *pppoe;
	char pad[ETHERMIN];


	niov = 0;
	tlen = 0;
	iov[niov].iov_base = &_this->ehdr;
	iov[niov++].iov_len = sizeof(_this->ehdr);

	if (is_disc) {
		_this->ehdr.ether_type = htons(ETHERTYPE_PPPOEDISC);
		iov[niov].iov_base = pkt;
		iov[niov++].iov_len = lpkt;
		pppoe = (struct pppoe_header *)pkt;
		pppoe->length = htons(lpkt - sizeof(pppoe0));
		tlen += lpkt;
	} else {
		_this->ehdr.ether_type = htons(ETHERTYPE_PPPOE);
		pppoe0.ver = PPPOE_RFC2516_VER;
		pppoe0.type = PPPOE_RFC2516_TYPE;
		pppoe0.code = 0;
		pppoe0.session_id = htons(_this->session_id);
		pppoe0.length = htons(lpkt);
		iov[niov].iov_base = &pppoe0;
		iov[niov++].iov_len = sizeof(pppoe0);
		tlen += sizeof(pppoe0);
		iov[niov].iov_base = pkt;
		iov[niov++].iov_len = lpkt;
		tlen += lpkt;
	}
	if (tlen < ETHERMIN) {
		memset(pad, 0, ETHERMIN - tlen);
		iov[niov].iov_base = pad;
		iov[niov++].iov_len = ETHERMIN - tlen;
	}

	sz = writev(pppoe_session_sock_bpf(_this), iov, niov);

	return (sz > 0)? 0 : -1;
}

static int
pppoe_session_send_PADT(pppoe_session *_this)
{
	u_char bufspace[2048];
	bytebuffer *buf;
	struct pppoe_header pppoe;
	int rval = 0;
	struct pppoe_tlv tlv;

	if ((buf = bytebuffer_wrap(bufspace, sizeof(bufspace))) == NULL) {
		pppoe_session_log(_this, LOG_ERR,
		"bytebuffer_wrap() failed on %s(): %m", __func__);
		return -1;
	}
	bytebuffer_clear(buf);

	/*
	 * PPPoE Header
	 */
	memset(&pppoe, 0, sizeof(pppoe));
	pppoe.ver = PPPOE_RFC2516_VER;
	pppoe.type = PPPOE_RFC2516_TYPE;
	pppoe.code = PPPOE_CODE_PADT;
	pppoe.session_id = htons(_this->session_id);
	bytebuffer_put(buf, &pppoe, sizeof(pppoe));

	/*
	 * Tag - End-of-List
	 */
	tlv.type = htons(PPPOE_TAG_END_OF_LIST);
	tlv.length = 0;
	bytebuffer_put(buf, &tlv, sizeof(tlv));
	tlv.type = htons(PPPOE_TAG_END_OF_LIST);
	tlv.length = 0;
	bytebuffer_put(buf, &tlv, sizeof(tlv));

	bytebuffer_flip(buf);
	if (pppoe_session_output(_this, 1, bytebuffer_pointer(buf),
	    bytebuffer_remaining(buf)) != 0) {
		pppoe_session_log(_this, LOG_ERR, "pppoed_output failed: %m");
		rval = 1;
	}
	pppoe_session_log(_this, LOG_INFO, "SendPADT");

	bytebuffer_unwrap(buf);
	bytebuffer_destroy(buf);

	return rval;
}

/* send PADS */
static int
pppoe_session_send_PADS(pppoe_session *_this, struct pppoe_tlv *hostuniq,
    struct pppoe_tlv *service_name)
{
	int rval, len;
	u_char bufspace[2048], msgbuf[80];
	bytebuffer *buf;
	struct pppoe_header pppoe;
	struct pppoe_tlv tlv;

	if ((buf = bytebuffer_wrap(bufspace, sizeof(bufspace))) == NULL) {
		pppoe_session_log(_this, LOG_ERR,
		"bytebuffer_wrap() failed on %s(): %m", __func__);
		return -1;
	}
	bytebuffer_clear(buf);

	/*
	 * PPPoE Header
	 */
	memset(&pppoe, 0, sizeof(pppoe));
	pppoe.ver = PPPOE_RFC2516_VER;
	pppoe.type = PPPOE_RFC2516_TYPE;
	pppoe.code = PPPOE_CODE_PADS;
	pppoe.session_id = htons(_this->session_id);
	bytebuffer_put(buf, &pppoe, sizeof(pppoe));

	/*
	 * Tag - Service-Name
	 */
	msgbuf[0] = '\0';
	if (service_name != NULL) {
		tlv.type = htons(PPPOE_TAG_SERVICE_NAME);
		tlv.length = htons(service_name->length);
		bytebuffer_put(buf, &tlv, sizeof(tlv));

		len = service_name->length;
		if (len > 0) {
			bytebuffer_put(buf, service_name->value, len);
			strlcpy(msgbuf, service_name->value,
			    MINIMUM(len + 1, sizeof(msgbuf)));
		}
	}

	/*
	 * Tag - Host-Uniq
	 */
	if (hostuniq != NULL) {
		tlv.type = htons(PPPOE_TAG_HOST_UNIQ);
		tlv.length = htons(hostuniq->length);
		bytebuffer_put(buf, &tlv, sizeof(tlv));
		bytebuffer_put(buf, hostuniq->value, hostuniq->length);
	}
	tlv.type = htons(PPPOE_TAG_END_OF_LIST);
	tlv.length = 0;
	bytebuffer_put(buf, &tlv, sizeof(tlv));

	bytebuffer_flip(buf);
	rval = 0;
	if (pppoe_session_output(_this, 1, bytebuffer_pointer(buf),
	    bytebuffer_remaining(buf)) != 0) {
		pppoe_session_log(_this, LOG_ERR, "pppoed_output failed: %m");
		rval = 1;
	}
	pppoe_session_log(_this, LOG_INFO, "SendPADS serviceName=%s "
	    "hostUniq=%s", msgbuf,
	    hostuniq? pppoed_tlv_value_string(hostuniq) : "none");

	bytebuffer_unwrap(buf);
	bytebuffer_destroy(buf);

	return rval;
}

/* process PADR from the peer */
int
pppoe_session_recv_PADR(pppoe_session *_this, slist *tag_list)
{
	pppoed *pppoed0 = _this->pppoed;
	struct pppoe_tlv *tlv, *hostuniq, *service_name, *ac_cookie;

	service_name = NULL;
	hostuniq = NULL;
	ac_cookie = NULL;
	for (slist_itr_first(tag_list); slist_itr_has_next(tag_list); ) {
		tlv = slist_itr_next(tag_list);
		if (tlv->type == PPPOE_TAG_HOST_UNIQ)
			hostuniq = tlv;
		if (tlv->type == PPPOE_TAG_SERVICE_NAME)
			service_name = tlv;
		if (tlv->type == PPPOE_TAG_AC_COOKIE)
			ac_cookie = tlv;
	}

	if (ac_cookie) {
		/* avoid a session which has already has cookie. */
		if (hash_lookup(pppoed0->acookie_hash,
		    (void *)ac_cookie->value) != NULL)
			goto fail;

		_this->acookie = *(uint32_t *)(ac_cookie->value);
		hash_insert(pppoed0->acookie_hash,
			(void *)(intptr_t)_this->acookie, _this);
	}

	if (pppoe_session_send_PADS(_this, hostuniq, service_name) != 0)
		goto fail;

	if (pppoe_session_bind_ppp(_this) != 0)
		goto fail;

	_this->state = PPPOE_SESSION_STATE_RUNNING;
	return 0;
fail:
	return -1;
}

/* process PADT from the peer */
int
pppoe_session_recv_PADT(pppoe_session *_this, slist *tag_list)
{
	pppoe_session_log(_this, LOG_INFO, "RecvPADT");

	pppoe_session_stop(_this);
	_this->state = PPPOE_SESSION_STATE_DISPOSING;

	return 0;
}

/*
 * Log
 */
static void
pppoe_session_log(pppoe_session *_this, int prio, const char *fmt, ...)
{
	char logbuf[BUFSIZ];
	va_list ap;

	PPPOE_SESSION_ASSERT(_this != NULL);
	va_start(ap, fmt);
#ifdef	PPPOED_MULTIPLE
	snprintf(logbuf, sizeof(logbuf), "pppoed id=%u session=%d %s",
	    _this->pppoed->id, _this->session_id, fmt);
#else
	snprintf(logbuf, sizeof(logbuf), "pppoed if=%s session=%d %s",
	    pppoe_session_listen_ifname(_this), _this->session_id, fmt);
#endif
	vlog_printf(prio, logbuf, ap);
	va_end(ap);
}

/*
 * PPP
 */
static int
pppoe_session_ppp_output(npppd_ppp *ppp, u_char *pkt, int lpkt, int flag)
{
	int rval;
	pppoe_session *_this;

	_this = ppp->phy_context;

	rval = pppoe_session_output(_this, 0, pkt, lpkt);

	if (_this->ppp == NULL)		/* ppp is freed */
		return 0;

	if (rval != 0) {
		ppp->oerrors++;
	} else {
		ppp->opackets++;
		ppp->obytes += lpkt;
	}

	return 0;
}

static void
pppoe_session_close_by_ppp(npppd_ppp *ppp)
{
	pppoe_session *_this;

	_this = ppp->phy_context;
	PPPOE_SESSION_ASSERT(_this != NULL);
	if (_this != NULL)
		/* do this before pptp_call_disconnect() */
		_this->ppp = NULL;

	pppoe_session_disconnect(_this);
}

/* bind for PPP */
static int
pppoe_session_bind_ppp(pppoe_session *_this)
{
	int len;
	npppd_ppp *ppp;
	struct sockaddr_dl sdl;

	ppp = NULL;
	if ((ppp = ppp_create()) == NULL)
		goto fail;

	PPPOE_SESSION_ASSERT(_this->ppp == NULL);

	if (_this->ppp != NULL)
		return -1;

	_this->ppp = ppp;

	ppp->tunnel_type = NPPPD_TUNNEL_PPPOE;
	ppp->tunnel_session_id = _this->session_id;
	ppp->phy_context = _this;
	ppp->send_packet = pppoe_session_ppp_output;
	ppp->phy_close = pppoe_session_close_by_ppp;

	strlcpy(ppp->phy_label, PPPOE_SESSION_LISTENER_TUN_NAME(_this),
	    sizeof(ppp->phy_label));

	memset(&sdl, 0, sizeof(sdl));
	sdl.sdl_len = sizeof(sdl);
	sdl.sdl_family = AF_LINK;
	sdl.sdl_index = if_nametoindex(pppoe_session_listen_ifname(_this));
	len = strlen(pppoe_session_listen_ifname(_this));
	memcpy(sdl.sdl_data, pppoe_session_listen_ifname(_this), len);
	sdl.sdl_nlen = len;
	sdl.sdl_alen = ETHER_ADDR_LEN;
	memcpy(sdl.sdl_data + len, _this->ether_addr, ETHER_ADDR_LEN);

	memcpy(&ppp->phy_info.peer_dl, &sdl, sizeof(sdl));

	if (ppp_init(npppd_get_npppd(), ppp) != 0)
		goto fail;
	ppp->has_acf = 0;


	pppoe_session_log(_this, LOG_NOTICE, "logtype=PPPBind ppp=%d", ppp->id);
	ppp_start(ppp);

	return 0;
fail:
	pppoe_session_log(_this, LOG_ERR, "failed binding ppp");

	if (ppp != NULL)
		ppp_destroy(ppp);
	_this->ppp = NULL;

	return 1;
}
