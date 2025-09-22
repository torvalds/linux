/*	$OpenBSD: radius_req.c,v 1.13 2025/01/29 10:21:03 yasuoka Exp $ */

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
 * This file provides functions for RADIUS request using radius(3) and event(3).
 * @author	Yasuoka Masahiko
 * $Id: radius_req.c,v 1.13 2025/01/29 10:21:03 yasuoka Exp $
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <syslog.h>
#include <stdlib.h>
#include <debugutil.h>
#include <time.h>
#include <event.h>
#include <string.h>
#include <errno.h>

#include "radius_req.h"
#include <radius.h>

#ifndef nitems
#define	nitems(_a)	(sizeof((_a)) / sizeof((_a)[0]))
#endif

struct overlapped {
	struct event		 ev_sock;
	int			 socket;
	int			 ntry;
	int			 max_tries;
	int			 failovers;
	int			 acct_delay_time;
	int			 response_fn_calling;
	struct sockaddr_storage	 ss;
	struct timespec		 req_time;
	void			*context;
	radius_response		*response_fn;
	char			 secret[MAX_RADIUS_SECRET];
	RADIUS_PACKET		*pkt;
	radius_req_setting	*setting;
};

static int   radius_request0(struct overlapped *);
static int   radius_prepare_socket(struct overlapped *);
static void  radius_request_io_event (int, short, void *);
static void  radius_on_response(RADIUS_REQUEST_CTX, RADIUS_PACKET *, int, int);
static int   select_srcaddr(struct sockaddr const *, struct sockaddr *, socklen_t *);
static void  radius_req_setting_ref(radius_req_setting *);
static void  radius_req_setting_unref(radius_req_setting *);

#ifdef	RADIUS_REQ_DEBUG
#define RADIUS_REQ_DBG(x)	log_printf x
#define	RADIUS_REQ_ASSERT(cond)					\
	if (!(cond)) {						\
	    fprintf(stderr,					\
		"\nASSERT(" #cond ") failed on %s() at %s:%d.\n"\
		, __func__, __FILE__, __LINE__);		\
	    abort(); 						\
	}
#else
#define	RADIUS_REQ_ASSERT(cond)
#define RADIUS_REQ_DBG(x)
#endif

/**
 * Send RADIUS request message.  The pkt(RADIUS packet) will be released
 * by this implementation.
 */
void
radius_request(RADIUS_REQUEST_CTX ctx, RADIUS_PACKET *pkt)
{
	uint32_t ival;
	struct overlapped *lap;

	RADIUS_REQ_ASSERT(pkt != NULL);
	RADIUS_REQ_ASSERT(ctx != NULL);
	lap = ctx;
	lap->pkt = pkt;
	if (radius_get_uint32_attr(pkt, RADIUS_TYPE_ACCT_DELAY_TIME, &ival)
	    == 0)
		lap->acct_delay_time = 1;
	radius_request0(lap);
}

/**
 * Prepare NAS-IP-Address or NAS-IPv6-Address.  If
 * setting->server[setting->curr_server].sock is not initialized, address
 * will be selected automatically.
 */
int
radius_prepare_nas_address(radius_req_setting *setting,
    RADIUS_PACKET *pkt)
{
	int af;
	struct sockaddr_in *sin4;
	struct sockaddr_in6 *sin6;
	socklen_t socklen;

	/* See RFC 2765, 3162 */
	RADIUS_REQ_ASSERT(setting != NULL);

	af = setting->server[setting->curr_server].peer.sin6.sin6_family;
	RADIUS_REQ_ASSERT(af == AF_INET6 || af == AF_INET);

	sin4 = &setting->server[setting->curr_server].sock.sin4;
	sin6 = &setting->server[setting->curr_server].sock.sin6;

	switch (af) {
	case AF_INET:
		socklen = sizeof(*sin4);
		if (sin4->sin_addr.s_addr == INADDR_ANY) {
			if (select_srcaddr((struct sockaddr const *)
			    &setting->server[setting->curr_server].peer,
			    (struct sockaddr *)sin4, &socklen) != 0) {
				RADIUS_REQ_ASSERT("NOTREACHED" == NULL);
				goto fail;
			}
		}
		if (radius_put_ipv4_attr(pkt, RADIUS_TYPE_NAS_IP_ADDRESS,
		    sin4->sin_addr) != 0)
			goto fail;
		break;
	case AF_INET6:
		socklen = sizeof(*sin6);
		if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
			if (select_srcaddr((struct sockaddr const *)
			    &setting->server[setting->curr_server].peer,
			    (struct sockaddr *)sin4, &socklen) != 0) {
				RADIUS_REQ_ASSERT("NOTREACHED" == NULL);
				goto fail;
			}
		}
		if (radius_put_raw_attr(pkt, RADIUS_TYPE_NAS_IPV6_ADDRESS,
		    sin6->sin6_addr.s6_addr, sizeof(sin6->sin6_addr.s6_addr))
		    != 0)
			goto fail;
		break;
	}

	return 0;
fail:
	return 1;
}


/** Checks whether the request can fail over to another server */
int
radius_request_can_failover(RADIUS_REQUEST_CTX ctx)
{
	struct overlapped *lap;
	radius_req_setting *setting;

	lap = ctx;
	setting = lap->setting;

	if (lap->failovers >= setting->max_failovers)
		return 0;
	if (memcmp(&lap->ss, &setting->server[setting->curr_server].peer,
	    setting->server[setting->curr_server].peer.sin6.sin6_len) == 0)
		/* flagged server doesn't differ from the last server. */
		return 0;

	return 1;
}

/** Send RADIUS request failing over to another server. */
int
radius_request_failover(RADIUS_REQUEST_CTX ctx)
{
	struct overlapped *lap;

	lap = ctx;
	RADIUS_REQ_ASSERT(lap != NULL);
	RADIUS_REQ_ASSERT(lap->socket >= 0)

	if (!radius_request_can_failover(lap))
		return -1;

	if (radius_prepare_socket(lap) != 0)
		return -1;

	if (radius_request0(lap) != 0)
		return -1;

	lap->failovers++;

	return 0;
}

static int
radius_prepare_socket(struct overlapped *lap)
{
	int sock;
	radius_req_setting *setting;
	struct sockaddr *sa;

	setting = lap->setting;
	if (lap->socket >= 0)
		close(lap->socket);
	lap->socket = -1;

	sa = (struct sockaddr *)&setting->server[setting->curr_server].peer;

	if ((sock = socket(sa->sa_family, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		log_printf(LOG_ERR, "socket() failed in %s: %m", __func__);
		return -1;
	}
	if (connect(sock, sa, sa->sa_len) != 0) {
		log_printf(LOG_ERR, "connect() failed in %s: %m", __func__);
		close(sock);
		return -1;
	}
	memcpy(&lap->ss, sa, sa->sa_len);
	lap->socket = sock;
	memcpy(lap->secret, setting->server[setting->curr_server].secret,
	    sizeof(lap->secret));
	lap->ntry = lap->max_tries;

	return 0;
}

/**
 * Prepare sending RADIUS request.  This implementation will call back to
 * notice that it receives the response or it fails for timeouts to the
 * The context that is set as 'pctx' and response packet that is given
 * by the callback function will be released by this implementation internally.
 * @param setting	Setting for RADIUS server or request.
 * @param context	Context for the caller.
 * @param pctx		Pointer to the space for context of RADIUS request
 *			(RADIUS_REQUEST_CTX).  This will be used for canceling.
 *			NULL can be specified when you don't need.
 * @param response_fn	Specify callback function as a pointer. The function
 *			will be called when it receives a response or when
 *			request fails for timeouts.
 * @param timeout	response timeout in second.
 */
int
radius_prepare(radius_req_setting *setting, void *context,
    RADIUS_REQUEST_CTX *pctx, radius_response response_fn)
{
	struct overlapped *lap;

	RADIUS_REQ_ASSERT(setting != NULL);
	lap = NULL;

	if (setting->server[setting->curr_server].enabled == 0)
		return 1;
	if ((lap = calloc(1, sizeof(struct overlapped))) == NULL) {
		log_printf(LOG_ERR, "calloc() failed in %s: %m", __func__);
		goto fail;
	}
	lap->context = context;
	lap->response_fn = response_fn;
	lap->socket = -1;
	lap->setting = setting;

	lap->max_tries = setting->max_tries;
	if (lap->max_tries <= 0)
		lap->max_tries = 3;	/* default max tries */

	if (radius_prepare_socket(lap) != 0)
		goto fail;

	if (pctx != NULL)
		*pctx = lap;

	radius_req_setting_ref(setting);	

	return 0;
fail:
	free(lap);

	return 1;
}

/**
 * Cancel the RADIUS request.
 * @param	The context received by {@link radius_request()}
 */
void
radius_cancel_request(RADIUS_REQUEST_CTX ctx)
{
	struct overlapped	*lap = ctx;

	/*
	 * Don't call this function from the callback function.
	 * The context will be freed after the callback function is called.
	 */
	RADIUS_REQ_ASSERT(lap->response_fn_calling == 0);
	if (lap->response_fn_calling != 0)
		return;

	if (lap->socket >= 0) {
		event_del(&lap->ev_sock);
		close(lap->socket);
		lap->socket = -1;
	}
	if (lap->pkt != NULL) {
		radius_delete_packet(lap->pkt);
		lap->pkt = NULL;
	}
	radius_req_setting_unref(lap->setting);

	explicit_bzero(lap->secret, sizeof(lap->secret));

	free(lap);
}

/** Return the shared secret for RADIUS server that is used by this context.  */
const char *
radius_get_server_secret(RADIUS_REQUEST_CTX ctx)
{
	struct overlapped *lap;

	lap = ctx;
	RADIUS_REQ_ASSERT(lap != NULL);

	return lap->secret;
}

/** Return the address of RADIUS server that is used by this context.  */
struct sockaddr *
radius_get_server_address(RADIUS_REQUEST_CTX ctx)
{
	struct overlapped *lap;

	lap = ctx;
	RADIUS_REQ_ASSERT(lap != NULL);

	return (struct sockaddr *)&lap->ss;
}

static int
radius_request0(struct overlapped *lap)
{
	struct timeval tv0;

	RADIUS_REQ_ASSERT(lap->ntry > 0);

	if (lap->acct_delay_time != 0) {
		struct timespec curr, delta;

		if (clock_gettime(CLOCK_MONOTONIC, &curr) != 0) {
			log_printf(LOG_CRIT,
			    "clock_gettime(CLOCK_MONOTONIC,) failed: %m");
			RADIUS_REQ_ASSERT(0);
		}
		if (!timespecisset(&lap->req_time))
			lap->req_time = curr;
		else {
			timespecsub(&curr, &lap->req_time, &delta);
			if (radius_set_uint32_attr(lap->pkt,
			    RADIUS_TYPE_ACCT_DELAY_TIME, delta.tv_sec) == 0)
				radius_update_id(lap->pkt);
		}
	}
	if (radius_get_code(lap->pkt) == RADIUS_CODE_ACCOUNTING_REQUEST)
		radius_set_accounting_request_authenticator(lap->pkt,
		    radius_get_server_secret(lap));
	else
		radius_put_message_authenticator(lap->pkt,
		    radius_get_server_secret(lap));

	lap->ntry--;
	if (radius_send(lap->socket, lap->pkt, 0) != 0) {
		log_printf(LOG_ERR, "sendto() failed in %s: %m",
		    __func__);
		radius_on_response(lap, NULL, RADIUS_REQUEST_ERROR, 1);
		return 1;
	}
	tv0.tv_usec = 0;
	tv0.tv_sec = lap->setting->timeout;

	event_set(&lap->ev_sock, lap->socket, EV_READ | EV_PERSIST,
	    radius_request_io_event, lap);
	event_add(&lap->ev_sock, &tv0);

	return 0;
}

static void
radius_request_io_event(int fd, short evmask, void *context)
{
	struct overlapped *lap;
	struct sockaddr_storage ss;
	int flags;
	socklen_t len;
	RADIUS_PACKET *respkt;

	RADIUS_REQ_ASSERT(context != NULL);

	lap = context;
	respkt = NULL;
	flags = 0;
	if ((evmask & EV_READ) != 0) {
		RADIUS_REQ_ASSERT(lap->socket >= 0);
		if (lap->socket < 0)
			return;
		RADIUS_REQ_ASSERT(lap->pkt != NULL);
		memset(&ss, 0, sizeof(ss));
		len = sizeof(ss);
		if ((respkt = radius_recv(lap->socket, 0)) == NULL) {
			RADIUS_REQ_DBG((LOG_DEBUG,
			    "radius_recv() on %s(): %m", __func__));
			/*
			 * Ignore error by icmp.  Wait a response from the
			 * server anyway, it may eventually become ready.
			 */
			switch (errno) {
			case EHOSTDOWN: case EHOSTUNREACH: case ECONNREFUSED:
				return;	/* sleep the rest of timeout time */
			}
			flags |= RADIUS_REQUEST_ERROR;
		} else if (lap->secret[0] == '\0') {
			flags |= RADIUS_REQUEST_CHECK_AUTHENTICATOR_NO_CHECK
			    | RADIUS_REQUEST_CHECK_MSG_AUTHENTICATOR_NO_CHECK;
		} else {
			radius_set_request_packet(respkt, lap->pkt);
			if (!radius_check_response_authenticator(respkt,
			    lap->secret))
				flags |= RADIUS_REQUEST_CHECK_AUTHENTICATOR_OK;
			if (!radius_has_attr(respkt, RADIUS_TYPE_MESSAGE_AUTHENTICATOR))
				flags |= RADIUS_REQUEST_CHECK_NO_MSG_AUTHENTICATOR;
			else if (radius_check_message_authenticator(respkt, lap->secret) == 0)
				flags |= RADIUS_REQUEST_CHECK_MSG_AUTHENTICATOR_OK;
		}
		radius_on_response(lap, respkt, flags, 0);
		radius_delete_packet(respkt);
	} else if ((evmask & EV_TIMEOUT) != 0) {
		if (lap->ntry > 0) {
			RADIUS_REQ_DBG((LOG_DEBUG,
			    "%s() timed out retry", __func__));
			radius_request0(lap);
			return;
		}
		RADIUS_REQ_DBG((LOG_DEBUG, "%s() timed out", __func__));
		flags |= RADIUS_REQUEST_TIMEOUT;
		radius_on_response(lap, NULL, flags, 1);
	}
}

static void
radius_on_response(RADIUS_REQUEST_CTX ctx, RADIUS_PACKET *pkt, int flags,
    int server_failure)
{
	struct overlapped *lap;
	int failovers;

	lap = ctx;
	if (server_failure) {
		int i, n;
		struct sockaddr *sa_curr;

		sa_curr = (struct sockaddr *)&lap->setting->server[
		    lap->setting->curr_server].peer;
		if (sa_curr->sa_len == lap->ss.ss_len &&
		    memcmp(sa_curr, &lap->ss, sa_curr->sa_len) == 0) {
			/*
			 * The server on failure is flagged as the current.
			 * change the current
			 */
			for (i = 1; i < nitems(lap->setting->server); i++) {
				n = (lap->setting->curr_server + i) %
				    nitems(lap->setting->server);
				if (lap->setting->server[n].enabled) {
					lap->setting->curr_server = n;
					break;
				}
			}
		}
	}

	failovers = lap->failovers;
	if (lap->response_fn != NULL) {
		lap->response_fn_calling++;
		lap->response_fn(lap->context, pkt, flags, ctx);
		lap->response_fn_calling--;
	}
	if (failovers == lap->failovers)
		radius_cancel_request(lap);
}

static int
select_srcaddr(struct sockaddr const *dst, struct sockaddr *src,
    socklen_t *srclen)
{
	int sock;

	sock = -1;
	if ((sock = socket(dst->sa_family, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		goto fail;
	if (connect(sock, dst, dst->sa_len) != 0)
		goto fail;
	if (getsockname(sock, src, srclen) != 0)
		goto fail;

	close(sock);

	return 0;
fail:
	if (sock >= 0)
		close(sock);

	return 1;
}

radius_req_setting *
radius_req_setting_create(void)
{
	return calloc(1, sizeof(radius_req_setting));
}

int
radius_req_setting_has_server(radius_req_setting *setting)
{
	return setting->server[setting->curr_server].enabled;
}

void
radius_req_setting_destroy(radius_req_setting *setting)
{
	setting->destroyed = 1;

	if (setting->refcnt == 0)
		free(setting);
}

static void
radius_req_setting_ref(radius_req_setting *setting)
{
	setting->refcnt++;
}

static void
radius_req_setting_unref(radius_req_setting *setting)
{
	setting->refcnt--;
	if (setting->destroyed)
		radius_req_setting_destroy(setting);
}
