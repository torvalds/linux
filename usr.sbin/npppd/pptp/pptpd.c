/*	$OpenBSD: pptpd.c,v 1.35 2025/02/03 07:46:06 yasuoka Exp $	*/

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
/* $Id: pptpd.c,v 1.35 2025/02/03 07:46:06 yasuoka Exp $ */

/**@file
 * This file provides a implementation of PPTP daemon.  Currently it
 * provides functions for PAC (PPTP Access Concentrator) only.
 * $Id: pptpd.c,v 1.35 2025/02/03 07:46:06 yasuoka Exp $
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_gre.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <signal.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <event.h>

#ifdef USE_LIBSOCKUTIL
#include <seil/sockfromto.h>
#endif

#include "net_utils.h"
#include "bytebuf.h"
#include "debugutil.h"
#include "hash.h"
#include "slist.h"
#include "addr_range.h"

#include "pptp.h"
#include "pptp_local.h"
#include "privsep.h"
#include "accept.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

static int pptpd_seqno = 0;

#ifdef	PPTPD_DEBUG
#define	PPTPD_ASSERT(x)	ASSERT(x)
#define	PPTPD_DBG(x)	pptpd_log x
#else
#define	PPTPD_ASSERT(x)
#define	PPTPD_DBG(x)
#endif

static void      pptpd_log (pptpd *, int, const char *, ...) __printflike(3,4);
static void      pptpd_close_gre (pptpd *);
static void      pptpd_close_1723 (pptpd *);
static void      pptpd_io_event (int, short, void *);
static void      pptpd_gre_io_event (int, short, void *);
static void      pptpd_gre_input (pptpd_listener *, struct sockaddr *, u_char *, int);
static void      pptp_ctrl_start_by_pptpd (pptpd *, int, int, struct sockaddr *);
static int       pptp_call_cmp (const void *, const void *);
static uint32_t  pptp_call_hash (const void *, int);
static void      pptp_gre_header_string (struct pptp_gre_header *, char *, int);

#define	PPTPD_SHUFFLE_MARK	-1

/* initialize pptp daemon */
int
pptpd_init(pptpd *_this)
{
	int i, m;
	uint16_t call0, call[UINT16_MAX - 1];

	int mib[] = { CTL_NET, PF_INET, IPPROTO_GRE, GRECTL_ALLOW };
	int value;
	size_t size;
	size = sizeof(value);

	if (sysctl(mib, sizeof(mib)/sizeof(mib[0]), &value, &size, NULL, 0) == 0) {
		if(value == 0) {
			pptpd_log(_this, LOG_WARNING, "GRE protocol not allowed");
		}
	}

	memset(_this, 0, sizeof(pptpd));
	_this->id = pptpd_seqno++;

	slist_init(&_this->ctrl_list);
	slist_init(&_this->call_free_list);

	/* randomize call id */
	for (i = 0; i < countof(call) ; i++)
		call[i] = i + 1;
	for (i = countof(call); i > 1; i--) {
		m = arc4random_uniform(i);
		call0 = call[m];
		call[m] = call[i - 1];
		call[i - 1] = call0;
	}

	for (i = 0; i < MINIMUM(PPTP_MAX_CALL, countof(call)); i++)
		slist_add(&_this->call_free_list, (void *)(uintptr_t)call[i]);
	slist_add(&_this->call_free_list, (void *)PPTPD_SHUFFLE_MARK);

	if (_this->call_id_map == NULL)
		_this->call_id_map = hash_create(pptp_call_cmp, pptp_call_hash,
		    0);

	return 0;
}

/* add a listener to pptpd daemon context */
int
pptpd_add_listener(pptpd *_this, int idx, struct pptp_conf *conf,
    struct sockaddr *addr)
{
	int inaddr_any;
	pptpd_listener *plistener, *plstn;

	plistener = NULL;
	if (idx == 0 && slist_length(&_this->listener) > 0) {
		slist_itr_first(&_this->listener);
		while (slist_itr_has_next(&_this->listener)) {
			slist_itr_next(&_this->listener);
			plstn = slist_itr_remove(&_this->listener);
			PPTPD_ASSERT(plstn != NULL);
			PPTPD_ASSERT(plstn->sock == -1);
			PPTPD_ASSERT(plstn->sock_gre == -1);
			free(plstn);
		}
	}
	PPTPD_ASSERT(slist_length(&_this->listener) == idx);
	if (slist_length(&_this->listener) != idx) {
		pptpd_log(_this, LOG_ERR,
		    "Invalid argument error on %s(): idx must be %d but %d",
		    __func__, slist_length(&_this->listener), idx);
		goto fail;
	}
	if ((plistener = calloc(1, sizeof(pptpd_listener))) == NULL) {
		pptpd_log(_this, LOG_ERR, "calloc() failed in %s: %m",
		    __func__);
		goto fail;
	}

	PPTPD_ASSERT(sizeof(plistener->bind_sin) >= addr->sa_len);
	memcpy(&plistener->bind_sin, addr, addr->sa_len);
	memcpy(&plistener->bind_sin_gre, addr, addr->sa_len);

	if (plistener->bind_sin.sin_port == 0)
		plistener->bind_sin.sin_port = htons(PPTPD_DEFAULT_TCP_PORT);

	/* When a raw socket binds both of an INADDR_ANY and specific IP
	 * address sockets, packets will be received by those sockets
	 * simultaneously. To avoid this duplicate receives, not
	 * permit such kind of configuration */
	inaddr_any = 0;
	slist_itr_first(&_this->listener);
	while (slist_itr_has_next(&_this->listener)) {
		plstn = slist_itr_next(&_this->listener);
		if (plstn->bind_sin_gre.sin_addr.s_addr == INADDR_ANY)
			inaddr_any++;
	}
	if (plistener->bind_sin_gre.sin_addr.s_addr == INADDR_ANY)
		inaddr_any++;
	if (inaddr_any > 0 && idx > 0) {
		log_printf(LOG_ERR, "configuration error at pptpd.listener_in: "
		    "combination 0.0.0.0 and other address is not allowed.");
		goto fail;
	}

	plistener->bind_sin_gre.sin_port = 0;
	plistener->sock = -1;
	plistener->sock_gre = -1;
	plistener->self = _this;
	plistener->index = idx;
	plistener->conf = conf;
	strlcpy(plistener->tun_name, conf->name, sizeof(plistener->tun_name));

	if (slist_add(&_this->listener, plistener) == NULL) {
		pptpd_log(_this, LOG_ERR, "slist_add() failed in %s: %m",
		    __func__);
		goto fail;
	}
	return 0;
fail:
	free(plistener);
	return 1;
}

void
pptpd_uninit(pptpd *_this)
{
	pptpd_listener *plstn;

	slist_fini(&_this->ctrl_list);
	slist_fini(&_this->call_free_list);

	slist_itr_first(&_this->listener);
	while (slist_itr_has_next(&_this->listener)) {
		plstn = slist_itr_next(&_this->listener);
		PPTPD_ASSERT(plstn != NULL);
		PPTPD_ASSERT(plstn->sock == -1);
		PPTPD_ASSERT(plstn->sock_gre == -1);
		free(plstn);
	}
	slist_fini(&_this->listener);
	if (_this->call_id_map != NULL)
		hash_free(_this->call_id_map);
	_this->call_id_map = NULL;
}

#define	CALL_ID_KEY(call_id, listener_idx)	\
	((void *)((uintptr_t)(call_id) | (listener_idx) << 16))
#define	CALL_KEY(call)				\
	CALL_ID_KEY((call)->id, (call)->ctrl->listener_index)
int
pptpd_assign_call(pptpd *_this, pptp_call *call)
{
	int shuffle_cnt = 0;
	u_int call_id;

	shuffle_cnt = 0;
	slist_itr_first(&_this->call_free_list);
	while (slist_length(&_this->call_free_list) > 1 &&
	    slist_itr_has_next(&_this->call_free_list)) {
		call_id = (uintptr_t)slist_itr_next(&_this->call_free_list);
		if (call_id == 0)
			break;
		slist_itr_remove(&_this->call_free_list);
		if (call_id == PPTPD_SHUFFLE_MARK) {
			if (shuffle_cnt++ > 0)
				break;
			slist_shuffle(&_this->call_free_list);
			slist_add(&_this->call_free_list,
			    (void *)PPTPD_SHUFFLE_MARK);
			slist_itr_first(&_this->call_free_list);
			continue;
		}
		call->id = call_id;
		hash_insert(_this->call_id_map, CALL_KEY(call), call);

		return 0;
	}
	errno = EBUSY;
	pptpd_log(_this, LOG_ERR, "call request reached limit=%d",
	    PPTP_MAX_CALL);
	return -1;
}

void
pptpd_release_call(pptpd *_this, pptp_call *call)
{
	if (call->id != 0)
		slist_add(&_this->call_free_list, (void *)(uintptr_t)call->id);
	hash_delete(_this->call_id_map, CALL_KEY(call), 0);
	call->id = 0;
}

static int
pptpd_listener_start(pptpd_listener *_this)
{
	int sock, ival, sock_gre;
	struct sockaddr_in bind_sin, bind_sin_gre;
	int wildcardbinding;

	wildcardbinding =
	    (_this->bind_sin.sin_addr.s_addr == INADDR_ANY)?  1 : 0;
	sock = -1;
	sock_gre = -1;
	memcpy(&bind_sin, &_this->bind_sin, sizeof(bind_sin));
	memcpy(&bind_sin_gre, &_this->bind_sin_gre, sizeof(bind_sin_gre));

	if ((sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP))
	    < 0) {
		pptpd_log(_this->self, LOG_ERR, "socket() failed at %s(): %m",
		    __func__);
		goto fail;
	}
	ival = 1;
	if(setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &ival, sizeof(ival)) < 0){
		pptpd_log(_this->self, LOG_WARNING,
		    "setsockopt(SO_REUSEPORT) failed at %s(): %m", __func__);
	}
#if defined(IP_STRICT_RCVIF) && defined(USE_STRICT_RCVIF)
	ival = 1;
	if (setsockopt(sock, IPPROTO_IP, IP_STRICT_RCVIF, &ival, sizeof(ival))
	    != 0)
		pptpd_log(_this->self, LOG_WARNING,
		    "%s(): setsockopt(IP_STRICT_RCVIF) failed: %m", __func__);
#endif
	if (bind(sock, (struct sockaddr *)&_this->bind_sin,
	    _this->bind_sin.sin_len) != 0) {
		pptpd_log(_this->self, LOG_ERR,
		    "bind(%s:%u) failed at %s(): %m",
		    inet_ntoa(_this->bind_sin.sin_addr),
		    ntohs(_this->bind_sin.sin_port), __func__);
		goto fail;
	}
	if (listen(sock, PPTP_BACKLOG) != 0) {
		pptpd_log(_this->self, LOG_ERR,
		    "listen(%s:%u) failed at %s(): %m",
		    inet_ntoa(_this->bind_sin.sin_addr),
		    ntohs(_this->bind_sin.sin_port), __func__);
		goto fail;
	}
	pptpd_log(_this->self, LOG_INFO, "Listening %s:%u/tcp (PPTP PAC) [%s]",
	    inet_ntoa(_this->bind_sin.sin_addr),
	    ntohs(_this->bind_sin.sin_port), _this->tun_name);

	/* GRE */
	bind_sin_gre.sin_port = 0;
	if ((sock_gre = priv_socket(AF_INET, SOCK_RAW, IPPROTO_GRE)) < 0) {
		pptpd_log(_this->self, LOG_ERR, "socket() failed at %s(): %m",
		    __func__);
		goto fail;
	}
#if defined(IP_STRICT_RCVIF) && defined(USE_STRICT_RCVIF)
	ival = 1;
	if (setsockopt(sock_gre, IPPROTO_IP, IP_STRICT_RCVIF, &ival,
	    sizeof(ival)) != 0)
		pptpd_log(_this->self, LOG_WARNING,
		    "%s(): setsockopt(IP_STRICT_RCVIF) failed: %m", __func__);
#endif
#ifdef IP_PIPEX
	ival = 1;
	if (setsockopt(sock_gre, IPPROTO_IP, IP_PIPEX, &ival, sizeof(ival))
	    != 0)
		pptpd_log(_this->self, LOG_WARNING,
		    "%s(): setsockopt(IP_PIPEX) failed: %m", __func__);
#endif
	if ((ival = fcntl(sock_gre, F_GETFL)) < 0) {
		pptpd_log(_this->self, LOG_ERR,
		    "fcntl(F_GET_FL) failed at %s(): %m", __func__);
		goto fail;
	} else if (fcntl(sock_gre, F_SETFL, ival | O_NONBLOCK) < 0) {
		pptpd_log(_this->self, LOG_ERR,
		    "fcntl(F_SET_FL) failed at %s(): %m", __func__);
		goto fail;
	}
	if (bind(sock_gre, (struct sockaddr *)&bind_sin_gre,
	    bind_sin_gre.sin_len) != 0) {
		pptpd_log(_this->self, LOG_ERR,
		    "bind(%s:%u) failed at %s(): %m",
		    inet_ntoa(bind_sin_gre.sin_addr),
		    ntohs(bind_sin_gre.sin_port), __func__);
		goto fail;
	}
	if (wildcardbinding) {
#ifdef USE_LIBSOCKUTIL
		if (setsockoptfromto(sock) != 0) {
			pptpd_log(_this->self, LOG_ERR,
			    "setsockoptfromto() failed in %s(): %m", __func__);
			goto fail;
		}
#else
		/* nothing to do */
#endif
	}
	pptpd_log(_this->self, LOG_INFO, "Listening %s:gre (PPTP PAC)",
	    inet_ntoa(bind_sin_gre.sin_addr));

	_this->sock = sock;
	_this->sock_gre = sock_gre;

	if (accept_add(_this->sock, pptpd_io_event, _this) != 0) {
		pptpd_log(_this->self, LOG_ERR,
		    "accept_add() failed in %s(): %m", __func__);
		goto fail;
	}

	event_set(&_this->ev_sock_gre, _this->sock_gre, EV_READ | EV_PERSIST,
	    pptpd_gre_io_event, _this);
	event_add(&_this->ev_sock_gre, NULL);

	return 0;
fail:
	if (sock >= 0)
		close(sock);
	if (sock_gre >= 0)
		close(sock_gre);

	_this->sock = -1;
	_this->sock_gre = -1;

	return 1;
}

int
pptpd_start(pptpd *_this)
{
	int rval = 0;
	pptpd_listener *plistener;

	slist_itr_first(&_this->listener);
	while (slist_itr_has_next(&_this->listener)) {
		plistener = slist_itr_next(&_this->listener);
		PPTPD_ASSERT(plistener != NULL);
		rval |= pptpd_listener_start(plistener);
	}
	if (rval == 0)
		_this->state = PPTPD_STATE_RUNNING;

	return rval;
}

static void
pptpd_listener_close_gre(pptpd_listener *_this)
{
	if (_this->sock_gre >= 0) {
		event_del(&_this->ev_sock_gre);
		close(_this->sock_gre);
		pptpd_log(_this->self, LOG_INFO, "Shutdown %s/gre",
		    inet_ntoa(_this->bind_sin_gre.sin_addr));
	}
	_this->sock_gre = -1;
}

static void
pptpd_close_gre(pptpd *_this)
{
	pptpd_listener *plistener;

	slist_itr_first(&_this->listener);
	while (slist_itr_has_next(&_this->listener)) {
		plistener = slist_itr_next(&_this->listener);
		pptpd_listener_close_gre(plistener);
	}
}

static void
pptpd_listener_close_1723(pptpd_listener *_this)
{
	if (_this->sock >= 0) {
		accept_del(_this->sock);
		close(_this->sock);
		pptpd_log(_this->self, LOG_INFO, "Shutdown %s:%u/tcp",
		    inet_ntoa(_this->bind_sin.sin_addr),
		    ntohs(_this->bind_sin.sin_port));
	}
	_this->sock = -1;
}

static void
pptpd_close_1723(pptpd *_this)
{
	pptpd_listener *plistener;

	slist_itr_first(&_this->listener);
	while (slist_itr_has_next(&_this->listener)) {
		plistener = slist_itr_next(&_this->listener);
		pptpd_listener_close_1723(plistener);
	}
}

void
pptpd_stop_immediatly(pptpd *_this)
{
	pptp_ctrl *ctrl;

	if (event_initialized(&_this->ev_timer))
		evtimer_del(&_this->ev_timer);
	if (_this->state != PPTPD_STATE_STOPPED) {
		/* lock, to avoid multiple call from pptp_ctrl_stop() */
		_this->state = PPTPD_STATE_STOPPED;

		pptpd_close_1723(_this);
		for (slist_itr_first(&_this->ctrl_list);
		    (ctrl = slist_itr_next(&_this->ctrl_list)) != NULL;) {
			pptp_ctrl_stop(ctrl, 0);
		}
		pptpd_close_gre(_this);
		slist_fini(&_this->ctrl_list);
		slist_fini(&_this->call_free_list);
		PPTPD_DBG((_this, LOG_DEBUG, "Stopped"));
	} else {
		PPTPD_DBG((_this, LOG_DEBUG, "(Already) Stopped"));
	}
}

static void
pptpd_stop_timeout(int fd, short event, void *ctx)
{
	pptpd *_this;

	_this = ctx;
	pptpd_stop_immediatly(_this);
}

void
pptpd_stop(pptpd *_this)
{
	int nctrl;
	pptp_ctrl *ctrl;
	struct timeval tv;

	if (event_initialized(&_this->ev_timer))
		evtimer_del(&_this->ev_timer);
	pptpd_close_1723(_this);

	/* XXX: use common procedure with l2tpd_stop */

	if (pptpd_is_stopped(_this))
		return;
	if (pptpd_is_shutting_down(_this)) {
		pptpd_stop_immediatly(_this);
		return;
	}
	_this->state = PPTPD_STATE_SHUTTING_DOWN;
	nctrl = 0;
	for (slist_itr_first(&_this->ctrl_list);
	    (ctrl = slist_itr_next(&_this->ctrl_list)) != NULL;) {
		pptp_ctrl_stop(ctrl, PPTP_CDN_RESULT_ADMIN_SHUTDOWN);
		nctrl++;
	}
	if (nctrl > 0) {
		tv.tv_sec = PPTPD_SHUTDOWN_TIMEOUT;
		tv.tv_usec = 0;

		evtimer_set(&_this->ev_timer, pptpd_stop_timeout, _this);
		evtimer_add(&_this->ev_timer, &tv);

		return;
	}
	pptpd_stop_immediatly(_this);
}

/*
 * PPTP Configuration
 */
int
pptpd_reload(pptpd *_this, struct pptp_confs *pptp_conf)
{
	int			 i;
	struct pptp_conf	*conf;
	pptpd_listener 		*listener;
	struct pptp_listen_addr	*addr;

	if (slist_length(&_this->listener) > 0) {
		/*
		 * TODO: add / remove / restart listener.
		 */
		slist_itr_first(&_this->listener);
		while (slist_itr_has_next(&_this->listener)) {
			listener = slist_itr_next(&_this->listener);
			TAILQ_FOREACH(conf, pptp_conf, entry) {
				if (strcmp(listener->tun_name,
				    conf->name) == 0) {
					listener->conf = conf;
					break;
				}
			}
		}

		return 0;
	}

	i = 0;
	TAILQ_FOREACH(conf, pptp_conf, entry) {
		TAILQ_FOREACH(addr, &conf->listen, entry)
			pptpd_add_listener(_this, i++, conf,
			    (struct sockaddr *)&addr->addr);
	}
	if (pptpd_start(_this) != 0)
		return -1;

	return 0;
}

/*
 * I/O functions
 */
/* I/O event handler of 1723/tcp */
static void
pptpd_io_event(int fd, short evmask, void *ctx)
{
	int newsock;
	const char *reason;
	socklen_t peerlen;
	struct sockaddr_storage peer;
	pptpd *_this;
	pptpd_listener *listener;

	listener = ctx;
	PPTPD_ASSERT(listener != NULL);
	_this = listener->self;
	PPTPD_ASSERT(_this != NULL);

	if ((evmask & EV_READ) != 0) {
		for (;;) { /* accept till EAGAIN occurred */
			peerlen = sizeof(peer);
			if ((newsock = accept(listener->sock,
			    (struct sockaddr *)&peer, &peerlen)) < 0) {
				if (errno != EAGAIN && errno == EINTR &&
				    errno != ECONNABORTED) {
					if (errno == EMFILE || errno == ENFILE)
						accept_pause();
					pptpd_log(_this, LOG_ERR,
					    "accept() failed at %s(): %m",
						__func__);
				}
				break;
			}
		/* check peer */
			switch (peer.ss_family) {
			case AF_INET:
				pptp_ctrl_start_by_pptpd(_this, newsock,
				    listener->index, (struct sockaddr *)&peer);
				break;
			default:
				reason = "address family is not supported.";
				break;
			}
		}
	}
}

/* I/O event handler of GRE */
static void
pptpd_gre_io_event(int fd, short evmask, void *ctx)
{
	int sz;
	u_char pkt[65535];
	socklen_t peerlen;
	struct sockaddr_storage peer;
	pptpd *_this;
	pptpd_listener *listener;

	listener = ctx;
	PPTPD_ASSERT(listener != NULL);
	_this = listener->self;
	PPTPD_ASSERT(_this != NULL);

	if (evmask & EV_READ) {
		for (;;) {
			/* read till blocked */
			peerlen = sizeof(peer);
			if ((sz = recvfrom(listener->sock_gre, pkt, sizeof(pkt),
			    0, (struct sockaddr *)&peer, &peerlen)) == -1) {
				if (errno == EAGAIN || errno == EINTR)
					break;
				pptpd_log(_this, LOG_INFO,
				    "read(GRE) failed: %m");
				pptpd_stop(_this);
				return;
			}
			pptpd_gre_input(listener, (struct sockaddr *)&peer, pkt,
			    sz);
		}
	}
}

/* receive GRE then route to pptp_call */
static void
pptpd_gre_input(pptpd_listener *listener, struct sockaddr *peer, u_char *pkt,
    int lpkt)
{
	int hlen, input_flags;
	uint32_t seq, ack, call_id;
	struct ip *iphdr;
	struct pptp_gre_header *grehdr;
	char hbuf0[NI_MAXHOST], logbuf[512];
	const char *reason;
	pptp_call *call;
	hash_link *hl;
	pptpd *_this;

	seq = 0;
	ack = 0;
	input_flags = 0;
	reason = "No error";
	_this = listener->self;

	PPTPD_ASSERT(peer->sa_family == AF_INET);

	strlcpy(hbuf0, "<unknown>", sizeof(hbuf0));
	if (getnameinfo(peer, peer->sa_len, hbuf0, sizeof(hbuf0), NULL, 0,
	    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
		pptpd_log(_this, LOG_ERR,
		    "getnameinfo() failed at %s(): %m", __func__);
		goto fail;
	}
	if (listener->conf->data_in_pktdump != 0) {
		pptpd_log(_this, LOG_DEBUG, "PPTP Data input packet dump");
		show_hd(debug_get_debugfp(), pkt, lpkt);
	}
	if (peer->sa_family != AF_INET) {
		pptpd_log(_this, LOG_ERR,
		    "Received malformed GRE packet: address family is not "
		    "supported: peer=%s af=%d", hbuf0, peer->sa_family);
		goto fail;
	}

	if (lpkt < sizeof(struct ip)) {
		pptpd_log(_this, LOG_ERR,
		    "Received a short length packet length=%d, from %s", lpkt,
			hbuf0);
		goto fail;
	}
	iphdr = (struct ip *)pkt;

	iphdr->ip_len = ntohs(iphdr->ip_len);
	hlen = iphdr->ip_hl * 4;

	if (iphdr->ip_len > lpkt ||
	    iphdr->ip_len < hlen + sizeof(struct pptp_gre_header)) {
		pptpd_log(_this, LOG_ERR,
		    "Received a broken packet: ip_hl=%d iplen=%d lpkt=%d", hlen,
			iphdr->ip_len, lpkt);
		show_hd(debug_get_debugfp(), pkt, lpkt);
		goto fail;
	}
	pkt += hlen;
	lpkt -= hlen;
	grehdr = (struct pptp_gre_header *)pkt;
	pkt += sizeof(struct pptp_gre_header);
	lpkt -= sizeof(struct pptp_gre_header);

	grehdr->protocol_type = htons(grehdr->protocol_type);
	grehdr->payload_length = htons(grehdr->payload_length);
	grehdr->call_id = htons(grehdr->call_id);

	if (!(grehdr->protocol_type == PPTP_GRE_PROTOCOL_TYPE &&
	    grehdr->C == 0 && grehdr->R == 0 && grehdr->K != 0 &&
	    grehdr->recur == 0 && grehdr->s == 0 && grehdr->flags == 0 &&
	    grehdr->ver == PPTP_GRE_VERSION)) {
		reason = "GRE header is broken";
		goto bad_gre;
	}
	if (grehdr->S != 0) {
		if (lpkt < 2) {
			reason = "No enough space for seq number";
			goto bad_gre;
		}
		input_flags |= PPTP_GRE_PKT_SEQ_PRESENT;
		seq = ntohl(*(uint32_t *)pkt);
		pkt += 4;
		lpkt -= 4;
	}

	if (grehdr->A != 0) {
		if (lpkt < 2) {
			reason = "No enough space for ack number";
			goto bad_gre;
		}
		input_flags |= PPTP_GRE_PKT_ACK_PRESENT;
		ack = ntohl(*(uint32_t *)pkt);
		pkt += 4;
		lpkt -= 4;
	}

	if (grehdr->payload_length > lpkt) {
		reason = "'Payload Length' is mismatch from actual length";
		goto bad_gre;
	}


	/* route to pptp_call */
	call_id = grehdr->call_id;

	hl = hash_lookup(_this->call_id_map, CALL_ID_KEY(call_id,
	    listener->index));
	if (hl == NULL) {
		reason = "Received GRE packet has unknown call_id";
		goto bad_gre;
	}
	call = hl->item;

	if (!(peer->sa_family == AF_INET &&
	    call->ctrl->peer.ss_family == AF_INET &&
	    ((struct sockaddr_in *)peer)->sin_addr.s_addr ==
	    ((struct sockaddr_in *)&call->ctrl->peer)->sin_addr.s_addr)) {
		reason = "Received GRE packet from invalid host";
		goto bad_gre;
	}

	pptp_call_gre_input(call, seq, ack, input_flags, pkt, lpkt);

	return;
bad_gre:
	pptp_gre_header_string(grehdr, logbuf, sizeof(logbuf));
	pptpd_log(_this, LOG_INFO,
	    "Received malformed GRE packet: %s: peer=%s sock=%s %s seq=%u: "
	    "ack=%u ifidx=%d", reason, hbuf0, inet_ntoa(iphdr->ip_dst), logbuf,
	    seq, ack, listener->index);
fail:
	return;
}

/* start PPTP control, when new connection is established */
static void
pptp_ctrl_start_by_pptpd(pptpd *_this, int sock, int listener_index,
    struct sockaddr *peer)
{
	pptp_ctrl *ctrl;
	socklen_t  sslen;

	ctrl = NULL;
	if ((ctrl = pptp_ctrl_create()) == NULL)
		goto fail;
	if (pptp_ctrl_init(ctrl) != 0)
		goto fail;

	memset(&ctrl->peer, 0, sizeof(ctrl->peer));
	memcpy(&ctrl->peer, peer, peer->sa_len);
	ctrl->pptpd = _this;
	ctrl->sock = sock;
	ctrl->listener_index = listener_index;

	sslen = sizeof(ctrl->our);
	if (getsockname(ctrl->sock, (struct sockaddr *)&ctrl->our,
	    &sslen) != 0) {
		pptpd_log(_this, LOG_WARNING,
		    "getsockname() failed at %s(): %m", __func__);
		goto fail;
	}

	if (PPTP_CTRL_CONF(ctrl)->echo_interval != 0)
		ctrl->echo_interval = PPTP_CTRL_CONF(ctrl)->echo_interval;
	if (PPTP_CTRL_CONF(ctrl)->echo_timeout != 0)
		ctrl->echo_timeout = PPTP_CTRL_CONF(ctrl)->echo_timeout;

	if (pptp_ctrl_start(ctrl) != 0)
		goto fail;

	slist_add(&_this->ctrl_list, ctrl);

	return;
fail:
	close(sock);
	pptp_ctrl_destroy(ctrl);
	return;
}

void
pptpd_ctrl_finished_notify(pptpd *_this, pptp_ctrl *ctrl)
{
	pptp_ctrl *ctrl1;
	int i, nctrl;

	PPTPD_ASSERT(_this != NULL);
	PPTPD_ASSERT(ctrl != NULL);

	accept_unpause();

	nctrl = 0;
	for (i = 0; i < slist_length(&_this->ctrl_list); i++) {
		ctrl1 = slist_get(&_this->ctrl_list, i);
		if (ctrl1 == ctrl) {
			slist_remove(&_this->ctrl_list, i);
			break;
		}
	}
	pptp_ctrl_destroy(ctrl);

	PPTPD_DBG((_this, LOG_DEBUG, "Remains %d ctrls", nctrl));
	if (pptpd_is_shutting_down(_this) && nctrl == 0)
		pptpd_stop_immediatly(_this);
}

/*
 * utility functions
 */

/* logging with the this PPTP instance */
static void
pptpd_log(pptpd *_this, int prio, const char *fmt, ...)
{
	char logbuf[BUFSIZ];
	va_list ap;

	PPTPD_ASSERT(_this != NULL);
	va_start(ap, fmt);
#ifdef	PPTPD_MULTIPLE
	snprintf(logbuf, sizeof(logbuf), "pptpd id=%u %s", _this->id, fmt);
#else
	snprintf(logbuf, sizeof(logbuf), "pptpd %s", fmt);
#endif
	vlog_printf(prio, logbuf, ap);
	va_end(ap);
}

static int
pptp_call_cmp(const void *a0, const void *b0)
{
	return ((intptr_t)a0 - (intptr_t)b0);
}

static uint32_t
pptp_call_hash(const void *ctx, int size)
{
	return (uintptr_t)ctx % size;
}

/* convert GRE packet header to strings */
static void
pptp_gre_header_string(struct pptp_gre_header *grehdr, char *buf, int lbuf)
{
	snprintf(buf, lbuf,
	    "[%s%s%s%s%s%s] ver=%d "
	    "protocol_type=%04x payload_length=%d call_id=%d",
	    (grehdr->C != 0)? "C" : "", (grehdr->R != 0)? "R" : "",
	    (grehdr->K != 0)? "K" : "", (grehdr->S != 0)? "S" : "",
	    (grehdr->s != 0)? "s" : "", (grehdr->A != 0)? "A" : "", grehdr->ver,
	    grehdr->protocol_type, grehdr->payload_length, grehdr->call_id);
}
