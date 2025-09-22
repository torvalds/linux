/*	$OpenBSD: l2tpd.c,v 1.22 2021/03/29 03:54:39 yasuoka Exp $ */

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
/**@file L2TP(Layer Two Tunneling Protocol "L2TP") / RFC2661 */
/* $Id: l2tpd.c,v 1.22 2021/03/29 03:54:39 yasuoka Exp $ */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <syslog.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <event.h>

#ifdef USE_LIBSOCKUTIL
#include <seil/sockfromto.h>
#else
#include "recvfromto.h"
#endif

#include "bytebuf.h"
#include "hash.h"
#include "slist.h"
#include "debugutil.h"
#include "l2tp.h"
#include "l2tp_subr.h"
#include "l2tp_local.h"
#include "addr_range.h"
#include "net_utils.h"

#ifdef	L2TPD_DEBUG
#define	L2TPD_ASSERT(x)	ASSERT(x)
#define	L2TPD_DBG(x)	l2tpd_log x
#else
#define	L2TPD_ASSERT(x)
#endif
#define L2TPD_IPSEC_POLICY_IN	"in ipsec esp/transport//require"
#define L2TPD_IPSEC_POLICY_OUT	"out ipsec esp/transport//require"

static void             l2tpd_io_event (int, short, void *);
static inline int       short_cmp (const void *, const void *);
static inline uint32_t  short_hash (const void *, int);

/* sequence # of l2tpd ID */
static u_int l2tpd_id_seq = 0;

/* L2TP daemon instance */

/**
 * initialize L2TP daemon instance
 * <p>
 * {@link _l2tpd#bind_sin} will return with .sin_family = AF_INET,
 * .sin_port = 1701 and .sin_len = "appropriate value"
 * </p>
 */
int
l2tpd_init(l2tpd *_this)
{
	int i, off;
	u_int id;

	L2TPD_ASSERT(_this != NULL);
	memset(_this, 0, sizeof(l2tpd));

	slist_init(&_this->listener);
	slist_init(&_this->free_session_id_list);

	_this->id = l2tpd_id_seq++;

	if ((_this->ctrl_map = hash_create(short_cmp, short_hash,
	    L2TPD_TUNNEL_HASH_SIZ)) == NULL) {
		log_printf(LOG_ERR, "hash_create() failed in %s(): %m",
		    __func__);
		return 1;
	}

	if (slist_add(&_this->free_session_id_list,
	    (void *)L2TP_SESSION_ID_SHUFFLE_MARK) == NULL) {
		l2tpd_log(_this, LOG_ERR, "slist_add() failed on %s(): %m",
		    __func__);
		return 1;
	}
	off = arc4random() & L2TP_SESSION_ID_MASK;
	for (i = 0; i < L2TP_NCALL; i++) {
		id = (i + off) & L2TP_SESSION_ID_MASK;
		if (id == 0)
			id = (off - 1) & L2TP_SESSION_ID_MASK;
		if (slist_add(&_this->free_session_id_list,
		    (void *)(uintptr_t)id) == NULL) {
			l2tpd_log(_this, LOG_ERR,
			    "slist_add() failed on %s(): %m", __func__);
			return 1;
		}
	}
	_this->purge_ipsec_sa = 1;
	_this->state = L2TPD_STATE_INIT;

	return 0;
}

/*
 * Add a {@link :l2tpd_listener} to the {@link ::l2tpd L2TP daemon}
 * @param	_this	{@link ::l2tpd L2TP daemon}
 * @param	idx	index of the lisnter
 * @param	tun_name	tunnel name (ex. "L2TP")
 * @param	bindaddr	bind address
 */
int
l2tpd_add_listener(l2tpd *_this, int idx, struct l2tp_conf *conf,
    struct sockaddr *addr)
{
	l2tpd_listener *plistener, *plsnr;

	plistener = NULL;
	if (idx == 0 && slist_length(&_this->listener) > 0) {
		slist_itr_first(&_this->listener);
		while (slist_itr_has_next(&_this->listener)) {
			slist_itr_next(&_this->listener);
			plsnr = slist_itr_remove(&_this->listener);
			L2TPD_ASSERT(plsnr != NULL);
			L2TPD_ASSERT(plsnr->sock == -1);
			free(plsnr);
		}
	}
	L2TPD_ASSERT(slist_length(&_this->listener) == idx);
	if (slist_length(&_this->listener) != idx) {
		l2tpd_log(_this, LOG_ERR,
		    "Invalid argument error on %s(): idx must be %d but %d",
		    __func__, slist_length(&_this->listener), idx);
		goto fail;
	}
	if ((plistener = calloc(1, sizeof(l2tpd_listener))) == NULL) {
		l2tpd_log(_this, LOG_ERR, "calloc() failed in %s: %m",
		    __func__);
		goto fail;
	}
	L2TPD_ASSERT(sizeof(plistener->bind) >= addr->sa_len);
	memcpy(&plistener->bind, addr, addr->sa_len);

	if (plistener->bind.sin6.sin6_port == 0)
		plistener->bind.sin6.sin6_port = htons(L2TPD_DEFAULT_UDP_PORT);

	plistener->sock = -1;
	plistener->self = _this;
	plistener->index = idx;
	plistener->conf = conf;
	strlcpy(plistener->tun_name, conf->name, sizeof(plistener->tun_name));

	if (slist_add(&_this->listener, plistener) == NULL) {
		l2tpd_log(_this, LOG_ERR, "slist_add() failed in %s: %m",
		    __func__);
		goto fail;
	}
	return 0;
fail:
	free(plistener);
	return 1;
}

/* finalize L2TP daemon instance */
void
l2tpd_uninit(l2tpd *_this)
{
	l2tpd_listener *plsnr;

	L2TPD_ASSERT(_this != NULL);

	slist_fini(&_this->free_session_id_list);
	if (_this->ctrl_map != NULL) {
		hash_free(_this->ctrl_map);
		_this->ctrl_map = NULL;
	}

	slist_itr_first(&_this->listener);
	while (slist_itr_has_next(&_this->listener)) {
		plsnr = slist_itr_next(&_this->listener);
		L2TPD_ASSERT(plsnr != NULL);
		L2TPD_ASSERT(plsnr->sock == -1);
		free(plsnr);
	}
	slist_fini(&_this->listener);

	event_del(&_this->ev_timeout);	/* just in case */
	_this->state = L2TPD_STATE_STOPPED;
}

/** assign the call to the l2tpd */
int
l2tpd_assign_call(l2tpd *_this, l2tp_call *call)
{
	int    shuffle_cnt;
	u_int  session_id;

	shuffle_cnt = 0;
	do {
		session_id = (uintptr_t)slist_remove_first(
		    &_this->free_session_id_list);
		if (session_id != L2TP_SESSION_ID_SHUFFLE_MARK)
			break;
		L2TPD_ASSERT(shuffle_cnt == 0);
		if (shuffle_cnt++ > 0) {
			l2tpd_log(_this, LOG_ERR,
			    "unexpected error in %s(): free_session_id_list "
			    "full", __func__);
			slist_add(&_this->free_session_id_list,
			    (void *)L2TP_SESSION_ID_SHUFFLE_MARK);
			return 1;
		}
		slist_shuffle(&_this->free_session_id_list);
		slist_add(&_this->free_session_id_list,
		    (void *)L2TP_SESSION_ID_SHUFFLE_MARK);
	} while (1);
	call->id = session_id;

	return 0;
}

/* this function will be called when the call is released */
void
l2tpd_release_call(l2tpd *_this, l2tp_call *call)
{
	slist_add(&_this->free_session_id_list, (void *)(uintptr_t)call->id);
}

/* start l2tpd listener */
static int
l2tpd_listener_start(l2tpd_listener *_this)
{
	l2tpd *_l2tpd;
	int    af, lvl, opt, sock, ival;
	char   hbuf[NI_MAXHOST + NI_MAXSERV + 16];

	_l2tpd = _this->self;
	sock = -1;
	af = _this->bind.sin6.sin6_family;
	lvl = (af == AF_INET)? IPPROTO_IP : IPPROTO_IPV6;

	if (_this->tun_name[0] == '\0')
		strlcpy(_this->tun_name, L2TPD_DEFAULT_LAYER2_LABEL,
		    sizeof(_this->tun_name));
	if ((sock = socket(_this->bind.sin6.sin6_family,
	    SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP)) < 0) {
		l2tpd_log(_l2tpd, LOG_ERR,
		    "socket() failed in %s(): %m", __func__);
		goto fail;
	}
#if defined(IP_STRICT_RCVIF) && defined(USE_STRICT_RCVIF)
	ival = 1;
	if (setsockopt(sock, IPPROTO_IP, IP_STRICT_RCVIF, &ival, sizeof(ival))
	    != 0)
		l2tpd_log(_l2tpd, LOG_WARNING,
		    "%s(): setsockopt(IP_STRICT_RCVIF) failed: %m", __func__);
#endif
	ival = 1;
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &ival, sizeof(ival))
	    != 0) {
		l2tpd_log(_l2tpd, LOG_ERR,
		    "setsockopt(,,SO_REUSEPORT) failed in %s(): %m", __func__);
		goto fail;
	}
	if (bind(sock, (struct sockaddr *)&_this->bind,
	    _this->bind.sin6.sin6_len) != 0) {
		l2tpd_log(_l2tpd, LOG_ERR, "Binding %s/udp: %m",
		    addrport_tostring((struct sockaddr *)&_this->bind,
		    _this->bind.sin6.sin6_len, hbuf, sizeof(hbuf)));
		goto fail;
	}
#ifdef USE_LIBSOCKUTIL
	if (setsockoptfromto(sock) != 0) {
		l2tpd_log(_l2tpd, LOG_ERR,
		    "setsockoptfromto() failed in %s(): %m", __func__);
		goto fail;
	}
#else
	opt = (af == AF_INET)? IP_RECVDSTADDR : IPV6_RECVPKTINFO;
	ival = 1;
	if (setsockopt(sock, lvl, opt, &ival, sizeof(ival)) != 0) {
		l2tpd_log(_l2tpd, LOG_ERR,
		    "setsockopt(,,IP{,V6}_RECVDSTADDR) failed in %s(): %m",
		    __func__);
		goto fail;
	}
#endif
#ifdef USE_SA_COOKIE
	if (af == AF_INET) {
		ival = 1;
		if (setsockopt(sock, IPPROTO_IP, IP_IPSECFLOWINFO, &ival,
		    sizeof(ival)) != 0) {
			l2tpd_log(_l2tpd, LOG_ERR,
			    "setsockopt(,,IP_IPSECFLOWINFO) failed in %s(): %m",
			    __func__);
			goto fail;
		}
	}
#endif
#ifdef IP_PIPEX
	opt = (af == AF_INET)? IP_PIPEX : IPV6_PIPEX;
	ival = 1;
	if (setsockopt(sock, lvl, opt, &ival, sizeof(ival)) != 0)
		l2tpd_log(_l2tpd, LOG_WARNING,
		    "%s(): setsockopt(IP{,V6}_PIPEX) failed: %m", __func__);
#endif
	if (_this->conf->require_ipsec) {
#ifdef IP_IPSEC_POLICY
		caddr_t  ipsec_policy_in, ipsec_policy_out;

		opt = (af == AF_INET)? IP_IPSEC_POLICY : IPV6_IPSEC_POLICY;
		/*
		 * Note: ipsec_set_policy() will assign the buffer for
		 * yacc parser stack, however it never free.
		 * it cause memory leak (-2000byte).
		 */
		if ((ipsec_policy_in = ipsec_set_policy(L2TPD_IPSEC_POLICY_IN,
		    strlen(L2TPD_IPSEC_POLICY_IN))) == NULL) {
			l2tpd_log(_l2tpd, LOG_ERR,
			    "ipsec_set_policy(L2TPD_IPSEC_POLICY_IN) failed "
			    "at %s(): %s: %m", __func__, ipsec_strerror());
		} else if (setsockopt(sock, lvl, opt, ipsec_policy_in,
		    ipsec_get_policylen(ipsec_policy_in)) < 0) {
			l2tpd_log(_l2tpd, LOG_WARNING,
			    "setsockopt(,,IP_IPSEC_POLICY(in)) failed "
			    "in %s(): %m", __func__);
		}
		if ((ipsec_policy_out = ipsec_set_policy(L2TPD_IPSEC_POLICY_OUT,
		    strlen(L2TPD_IPSEC_POLICY_OUT))) == NULL) {
			l2tpd_log(_l2tpd, LOG_ERR,
			    "ipsec_set_policy(L2TPD_IPSEC_POLICY_OUT) failed "
			    "at %s(): %s: %m", __func__, ipsec_strerror());
		}
		if (ipsec_policy_out != NULL &&
		    setsockopt(sock, lvl, opt, ipsec_policy_out,
		    ipsec_get_policylen(ipsec_policy_out)) < 0) {
			l2tpd_log(_l2tpd, LOG_WARNING,
			    "setsockopt(,,IP_IPSEC_POLICY(out)) failed "
			    "in %s(): %m", __func__);
		}
		free(ipsec_policy_in);
		free(ipsec_policy_out);
#elif defined(IP_ESP_TRANS_LEVEL)
		opt = (af == AF_INET)
		    ? IP_ESP_TRANS_LEVEL : IPV6_ESP_TRANS_LEVEL;
		ival = IPSEC_LEVEL_REQUIRE;
		if (setsockopt(sock, lvl, opt, &ival, sizeof(ival)) != 0) {
			l2tpd_log(_l2tpd, LOG_WARNING,
			    "setsockopt(,,IP{,V6}_ESP_TRANS_LEVEL(out)) failed "
			    "in %s(): %m", __func__);
		}
#else
#error IP_IPSEC_POLICY or IP_ESP_TRANS_LEVEL must be usable.
#endif
	}

	_this->sock = sock;

	event_set(&_this->ev_sock, _this->sock, EV_READ | EV_PERSIST,
	    l2tpd_io_event, _this);
	event_add(&_this->ev_sock, NULL);

	l2tpd_log(_l2tpd, LOG_INFO, "Listening %s/udp (L2TP LNS) [%s]",
	    addrport_tostring((struct sockaddr *)&_this->bind,
	    _this->bind.sin6.sin6_len, hbuf, sizeof(hbuf)), _this->tun_name);

	return 0;
fail:
	if (sock >= 0)
		close(sock);

	return 1;
}

/* start L2TP daemon */
int
l2tpd_start(l2tpd *_this)
{
	int rval;
	l2tpd_listener *plsnr;

	rval = 0;

	L2TPD_ASSERT(_this->state == L2TPD_STATE_INIT);
	if (_this->state != L2TPD_STATE_INIT) {
		l2tpd_log(_this, LOG_ERR, "Failed to start l2tpd: illegal "
		    "state.");
		return -1;
	}

	slist_itr_first(&_this->listener);
	while (slist_itr_has_next(&_this->listener)) {
		plsnr = slist_itr_next(&_this->listener);
		rval |= l2tpd_listener_start(plsnr);
	}

	if (rval == 0)
		_this->state = L2TPD_STATE_RUNNING;

	return rval;
}

/* stop l2tp lisnter */
static void
l2tpd_listener_stop(l2tpd_listener *_this)
{
	char hbuf[NI_MAXHOST + NI_MAXSERV + 16];

	if (_this->sock >= 0) {
		event_del(&_this->ev_sock);
		close(_this->sock);
		l2tpd_log(_this->self, LOG_INFO,
		    "Shutdown %s/udp (L2TP LNS)",
		    addrport_tostring((struct sockaddr *)&_this->bind,
		    _this->bind.sin6.sin6_len, hbuf, sizeof(hbuf)));
		_this->sock = -1;
	}
}

/* stop immediattly without disconnect operation */
void
l2tpd_stop_immediatly(l2tpd *_this)
{
	l2tpd_listener *plsnr;

	slist_itr_first(&_this->listener);
	while (slist_itr_has_next(&_this->listener)) {
		plsnr = slist_itr_next(&_this->listener);
		l2tpd_listener_stop(plsnr);
	}
	event_del(&_this->ev_timeout);	/* XXX */
	_this->state = L2TPD_STATE_STOPPED;
}

/*
 * this function will be called when {@link ::_l2tp_ctrl control}
 * is terminated.
 */
void
l2tpd_ctrl_finished_notify(l2tpd *_this)
{
	if (_this->state != L2TPD_STATE_SHUTTING_DOWN)
		return;

	if (hash_first(_this->ctrl_map) != NULL)
		return;

	l2tpd_stop_immediatly(_this);
}

static void
l2tpd_stop_timeout(int fd, short evtype, void *ctx)
{
	hash_link *hl;
	l2tp_ctrl *ctrl;
	l2tpd *_this;

	_this = ctx;
	l2tpd_log(_this, LOG_INFO, "Shutdown timeout");
	for (hl = hash_first(_this->ctrl_map); hl != NULL;
	    hl = hash_next(_this->ctrl_map)) {
		ctrl = hl->item;
		l2tp_ctrl_stop(ctrl, 0);
	}
	l2tpd_stop_immediatly(_this);
}

/* stop L2TP daemon */
void
l2tpd_stop(l2tpd *_this)
{
	int nctrls = 0;
	hash_link *hl;
	l2tp_ctrl *ctrl;

	nctrls = 0;
	event_del(&_this->ev_timeout);
	if (l2tpd_is_stopped(_this))
		return;
	if (l2tpd_is_shutting_down(_this)) {
		/* terminate immediately, when 2nd call */
		l2tpd_stop_immediatly(_this);
		return;
	}
	for (hl = hash_first(_this->ctrl_map); hl != NULL;
	    hl = hash_next(_this->ctrl_map)) {
		ctrl = hl->item;
		l2tp_ctrl_stop(ctrl, L2TP_STOP_CCN_RCODE_SHUTTING_DOWN);
		nctrls++;
	}
	_this->state = L2TPD_STATE_SHUTTING_DOWN;
	if (nctrls > 0) {
		struct timeval tv0;

		tv0.tv_usec = 0;
		tv0.tv_sec = L2TPD_SHUTDOWN_TIMEOUT;

		evtimer_set(&_this->ev_timeout, l2tpd_stop_timeout, _this);
		evtimer_add(&_this->ev_timeout, &tv0);

		return;
	}
	l2tpd_stop_immediatly(_this);
}

/*
 * Configuration
 */
int
l2tpd_reload(l2tpd *_this, struct l2tp_confs *l2tp_conf)
{
	int			 i;
	struct l2tp_conf	*conf;
	l2tpd_listener		*listener;
	struct l2tp_listen_addr	*addr;

	if (slist_length(&_this->listener) > 0) {
		/*
		 * TODO: add / remove / restart listener.
		 */
		slist_itr_first(&_this->listener);
		while (slist_itr_has_next(&_this->listener)) {
			listener = slist_itr_next(&_this->listener);
			TAILQ_FOREACH(conf, l2tp_conf, entry) {
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
	TAILQ_FOREACH(conf, l2tp_conf, entry) {
		TAILQ_FOREACH(addr, &conf->listen, entry)
			l2tpd_add_listener(_this, i++, conf, 
			    (struct sockaddr *)&addr->addr);
	}
	if (l2tpd_start(_this) != 0)
		return -1;

	return 0;
}

/*
 * I/O functions
 */
/* logging when deny an access */
void
l2tpd_log_access_deny(l2tpd *_this, const char *reason, struct sockaddr *peer)
{
	char buf[BUFSIZ];

	l2tpd_log(_this, LOG_ALERT, "Received packet from %s/udp: "
	    "%s", addrport_tostring(peer, peer->sa_len, buf, sizeof(buf)),
	    reason);
}

/* I/O event handler */
static void
l2tpd_io_event(int fd, short evtype, void *ctx)
{
	int sz;
	l2tpd *_l2tpd;
	l2tpd_listener *_this;
	socklen_t peerlen, socklen;
	struct sockaddr_storage peer, sock;
	u_char buf[8192];
	void *nat_t;

	_this = ctx;
	_l2tpd = _this->self;
	if ((evtype & EV_READ) != 0) {
		peerlen = sizeof(peer);
		socklen = sizeof(sock);
		while (!l2tpd_is_stopped(_l2tpd)) {
#if defined(USE_LIBSOCKUTIL) || defined(USE_SA_COOKIE)
			int sa_cookie_len;
			struct in_ipsec_sa_cookie sa_cookie;

			sa_cookie_len = sizeof(sa_cookie);
			if ((sz = recvfromto_nat_t(_this->sock, buf,
			    sizeof(buf), 0,
			    (struct sockaddr *)&peer, &peerlen,
			    (struct sockaddr *)&sock, &socklen,
			    &sa_cookie, &sa_cookie_len)) == -1) {
#else
			if ((sz = recvfromto(_this->sock, buf,
			    sizeof(buf), 0,
			    (struct sockaddr *)&peer, &peerlen,
			    (struct sockaddr *)&sock, &socklen)) == -1) {
#endif
				if (errno == EAGAIN || errno == EINTR)
					break;
				l2tpd_log(_l2tpd, LOG_ERR,
				    "recvfrom() failed in %s(): %m",
				    __func__);
				l2tpd_stop(_l2tpd);
				return;
			}
			/* source address check (allows.in) */
			switch (peer.ss_family) {
			case AF_INET:
#if defined(USE_LIBSOCKUTIL) || defined(USE_SA_COOKIE)
				if (sa_cookie_len > 0)
					nat_t = &sa_cookie;
				else
					nat_t = NULL;
#else
				nat_t = NULL;
#endif
				l2tp_ctrl_input(_l2tpd, _this->index,
				    (struct sockaddr *)&peer,
				    (struct sockaddr *)&sock, nat_t,
				    buf, sz);
				break;
			case AF_INET6:
				l2tp_ctrl_input(_l2tpd, _this->index,
				    (struct sockaddr *)&peer,
				    (struct sockaddr *)&sock, NULL,
				    buf, sz);
				break;
			default:
				l2tpd_log(_l2tpd, LOG_ERR,
				    "received from unknown address family = %d",
				    peer.ss_family);
				break;
			}
		}
	}
}

/*
 * L2TP control
 */
l2tp_ctrl *
l2tpd_get_ctrl(l2tpd *_this, unsigned tunid)
{
	hash_link *hl;

	hl = hash_lookup(_this->ctrl_map, (void *)(uintptr_t)tunid);
	if (hl == NULL)
		return NULL;

	return hl->item;
}

void
l2tpd_add_ctrl(l2tpd *_this, l2tp_ctrl *ctrl)
{
	hash_insert(_this->ctrl_map, (void *)(uintptr_t)ctrl->tunnel_id, ctrl);
}

void
l2tpd_remove_ctrl(l2tpd *_this, unsigned tunid)
{
	hash_delete(_this->ctrl_map, (void *)(uintptr_t)tunid, 0);
}


/*
 * misc
 */

void
l2tpd_log(l2tpd *_this, int prio, const char *fmt, ...)
{
	char logbuf[BUFSIZ];
	va_list ap;

	va_start(ap, fmt);
#ifdef	L2TPD_MULTIPLE
	snprintf(logbuf, sizeof(logbuf), "l2tpd id=%u %s", _this->id, fmt);
#else
	snprintf(logbuf, sizeof(logbuf), "l2tpd %s", fmt);
#endif
	vlog_printf(prio, logbuf, ap);
	va_end(ap);
}
