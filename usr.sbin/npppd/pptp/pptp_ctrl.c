/*	$OpenBSD: pptp_ctrl.c,v 1.13 2021/03/29 03:54:40 yasuoka Exp $	*/

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
 * PPTP(RFC 2637) control connection implementation.
 * currently it only support PAC part
 */
/* $Id: pptp_ctrl.c,v 1.13 2021/03/29 03:54:40 yasuoka Exp $ */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <netdb.h>
#include <unistd.h>
#include <syslog.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <event.h>

#include "bytebuf.h"
#include "debugutil.h"
#include "hash.h"
#include "slist.h"
#include "time_utils.h"

#include "version.h"

#include "pptp.h"
#include "pptp_local.h"
#include "pptp_subr.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))
#define MAXIMUM(a, b)	(((a) > (b)) ? (a) : (b))

/* periods of pptp_ctrl_timeout */
#define PPTP_CTRL_TIMEOUT_IVAL_SEC	2

#ifdef	PPTP_CTRL_DEBUG
#define	PPTP_CTRL_ASSERT(x)	ASSERT(x)
#define	PPTP_CTRL_DBG(x)	pptp_ctrl_log x
#else
#define	PPTP_CTRL_ASSERT(x)
#define	PPTP_CTRL_DBG(x)
#endif

static unsigned pptp_ctrl_seqno = 0;

static void  pptp_ctrl_log (pptp_ctrl *, int, const char *, ...) __printflike(3,4);
static void        pptp_ctrl_timeout (int, short, void *);
static void        pptp_ctrl_reset_timeout (pptp_ctrl *);
static void        pptp_ctrl_io_event (int, short, void *);
static void        pptp_ctrl_set_io_event (pptp_ctrl *);
static int         pptp_ctrl_output_flush (pptp_ctrl *);
static void        pptp_ctrl_SCCRx_string (struct pptp_scc *, u_char *, int);
static int         pptp_ctrl_recv_SCCRQ (pptp_ctrl *, u_char *, int);
static int         pptp_ctrl_recv_StopCCRP (pptp_ctrl *, u_char *, int);
static int         pptp_ctrl_send_StopCCRQ (pptp_ctrl *, int);
static int         pptp_ctrl_recv_StopCCRQ (pptp_ctrl *, u_char *, int);
static int         pptp_ctrl_send_StopCCRP (pptp_ctrl *, int, int);
static int         pptp_ctrl_send_SCCRP (pptp_ctrl *, int, int);
static void        pptp_ctrl_send_CDN (pptp_ctrl *, int, int, int, const char *);
static void        pptp_ctrl_process_echo_req (pptp_ctrl *, u_char *, int);
static int         pptp_ctrl_recv_echo_rep (pptp_ctrl *, u_char *, int);
static void        pptp_ctrl_send_echo_req (pptp_ctrl *);
static int         pptp_ctrl_input (pptp_ctrl *, u_char *, int);
static int         pptp_ctrl_call_input (pptp_ctrl *, int, u_char *, int);
static const char  *pptp_ctrl_state_string (int);
static void        pptp_ctrl_fini(pptp_ctrl *);

/*
 * pptp_ctrl instance operation functions
 */
pptp_ctrl *
pptp_ctrl_create(void)
{
	pptp_ctrl *_this;

	if ((_this = malloc(sizeof(pptp_ctrl))) == NULL)
		return NULL;

	return _this;
}

int
pptp_ctrl_init(pptp_ctrl *_this)
{
	time_t curr_time;

	PPTP_CTRL_ASSERT(_this != NULL);
	curr_time = get_monosec();
	memset(_this, 0, sizeof(pptp_ctrl));
	_this->id = pptp_ctrl_seqno++;
	_this->sock = -1;

	if ((_this->recv_buf = bytebuffer_create(PPTP_BUFSIZ)) == NULL) {
		pptp_ctrl_log(_this, LOG_ERR, "bytebuffer_create() failed at "
		    "%s(): %m", __func__);
		goto fail;
	}
	if ((_this->send_buf = bytebuffer_create(PPTP_BUFSIZ)) == NULL) {
		pptp_ctrl_log(_this, LOG_ERR, "bytebuffer_create() failed at "
		    "%s(): %m", __func__);
		goto fail;
	}
	_this->last_rcv_ctrl = curr_time;
	_this->last_snd_ctrl = curr_time;
	_this->echo_seq = arc4random();
	_this->echo_interval = PPTP_CTRL_DEFAULT_ECHO_INTERVAL;
	_this->echo_timeout = PPTP_CTRL_DEFAULT_ECHO_TIMEOUT;
	slist_init(&_this->call_list);
	evtimer_set(&_this->ev_timer, pptp_ctrl_timeout, _this);

	return 0;
fail:
	return 1;
}

int
pptp_ctrl_start(pptp_ctrl *_this)
{
	int ival;
	char hbuf0[NI_MAXHOST], sbuf0[NI_MAXSERV];
	char hbuf1[NI_MAXHOST], sbuf1[NI_MAXSERV];
	struct sockaddr_storage sock;
	socklen_t socklen;

	PPTP_CTRL_ASSERT(_this != NULL);
	PPTP_CTRL_ASSERT(_this->sock >= 0);

	/* convert address to strings for logging */
	strlcpy(hbuf0, "<unknown>", sizeof(hbuf0));
	strlcpy(sbuf0, "<unknown>", sizeof(sbuf0));
	strlcpy(hbuf1, "<unknown>", sizeof(hbuf1));
	strlcpy(sbuf1, "<unknown>", sizeof(sbuf1));
	if (getnameinfo((struct sockaddr *)&_this->peer, _this->peer.ss_len,
	    hbuf0, sizeof(hbuf0), sbuf0, sizeof(sbuf0),
	    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
		pptp_ctrl_log(_this, LOG_ERR,
		    "getnameinfo() failed at %s(): %m", __func__);
	}
	socklen = sizeof(sock);
	if (getsockname(_this->sock, (struct sockaddr *)&sock, &socklen) != 0) {
		pptp_ctrl_log(_this, LOG_ERR,
		    "getsockname() failed at %s(): %m", __func__);
		goto fail;
	}
	if (getnameinfo((struct sockaddr *)&sock, sock.ss_len, hbuf1,
	    sizeof(hbuf1), sbuf1, sizeof(sbuf1),
	    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
		pptp_ctrl_log(_this, LOG_ERR,
		    "getnameinfo() failed at %s(): %m", __func__);
	}
	pptp_ctrl_log(_this, LOG_INFO, "Starting peer=%s:%s/tcp "
	    "sock=%s:%s/tcp", hbuf0, sbuf0, hbuf1, sbuf1);

	if ((ival = fcntl(_this->sock, F_GETFL)) < 0) {
		pptp_ctrl_log(_this, LOG_ERR,
		    "fcntl(F_GET_FL) failed at %s(): %m", __func__);
		goto fail;
	} else if (fcntl(_this->sock, F_SETFL, ival | O_NONBLOCK) < 0) {
		pptp_ctrl_log(_this, LOG_ERR,
		    "fcntl(F_SET_FL) failed at %s(): %m", __func__);
		goto fail;
	}
	pptp_ctrl_set_io_event(_this);
	pptp_ctrl_reset_timeout(_this);

	return 0;
fail:
	return 1;
}

/* Timer */
static void
pptp_ctrl_timeout(int fd, short event, void *ctx)
{
	int i;
	pptp_call *call;
	pptp_ctrl *_this;
	time_t last, curr_time;

	_this = ctx;
	curr_time = get_monosec();

	PPTP_CTRL_DBG((_this, DEBUG_LEVEL_3, "enter %s()", __func__));
	/* clean up call */
	i = 0;
	while (i < slist_length(&_this->call_list)) {
		call = slist_get(&_this->call_list, i);
		if (call->state == PPTP_CALL_STATE_CLEANUP_WAIT &&
		    curr_time - call->last_io > PPTP_CALL_CLEANUP_WAIT_TIME) {
			pptp_call_stop(call);
			pptp_call_destroy(call);
			slist_remove(&_this->call_list, i);
		} else
			i++;
	}

	/* State machine: Timeout */
	switch (_this->state) {
	default:
	case PPTP_CTRL_STATE_WAIT_CTRL_REPLY:
	case PPTP_CTRL_STATE_IDLE:
		if (curr_time - _this->last_rcv_ctrl > PPTPD_IDLE_TIMEOUT) {
			pptp_ctrl_log(_this, LOG_ERR,
			    "Timeout in state %s",
			    pptp_ctrl_state_string(_this->state));
			pptp_ctrl_fini(_this);
			return;
		}
		break;
	case PPTP_CTRL_STATE_ESTABLISHED:
		last = MAXIMUM(_this->last_rcv_ctrl, _this->last_snd_ctrl);

		if (curr_time - _this->last_rcv_ctrl
			    >= _this->echo_interval + _this->echo_timeout) {
			pptp_ctrl_log(_this, LOG_INFO,
			    "Timeout waiting for echo reply");
			pptp_ctrl_fini(_this);
			return;
		}
		if (curr_time - last >= _this->echo_interval) {
			PPTP_CTRL_DBG((_this, LOG_DEBUG, "Echo"));
			_this->echo_seq++;
			pptp_ctrl_send_echo_req(_this);
		}
		break;
	case PPTP_CTRL_STATE_WAIT_STOP_REPLY:
		if (curr_time - _this->last_snd_ctrl >
		    PPTP_CTRL_StopCCRP_WAIT_TIME) {
			pptp_ctrl_log(_this, LOG_WARNING,
			    "Timeout waiting for StopCCRP");
			pptp_ctrl_fini(_this);
			return;
		}
		break;
	case PPTP_CTRL_STATE_DISPOSING:
		pptp_ctrl_fini(_this);
		return;
	}
	pptp_ctrl_reset_timeout(_this);
}

static void
pptp_ctrl_reset_timeout(pptp_ctrl *_this)
{
	struct timeval tv;

	switch (_this->state) {
	case PPTP_CTRL_STATE_DISPOSING:
		/* call back immediately */
		timerclear(&tv);
		break;
	default:
		tv.tv_sec = PPTP_CTRL_TIMEOUT_IVAL_SEC;
		tv.tv_usec = 0;
		break;
	}
	evtimer_add(&_this->ev_timer, &tv);
}

/**
 * Terminate PPTP control connection
 * @result	The value for Stop-Control-Connection-Request(StopCCRQ) result.
		This function will not sent StopCCRQ when the value == 0 and
		the specification does not require to sent it.
 * @see		::#PPTP_StopCCRQ_REASON_STOP_PROTOCOL
 * @see		::#PPTP_StopCCRQ_REASON_STOP_LOCAL_SHUTDOWN
 */
void
pptp_ctrl_stop(pptp_ctrl *_this, int result)
{
	int i;
	pptp_call *call;

	switch (_this->state) {
	case PPTP_CTRL_STATE_WAIT_STOP_REPLY:
		/* waiting response. */
		/* this state will timeout by pptp_ctrl_timeout */
		break;
	case PPTP_CTRL_STATE_ESTABLISHED:
		if (result != 0) {
			for (i = 0; i < slist_length(&_this->call_list); i++) {
				call = slist_get(&_this->call_list, i);
				pptp_call_disconnect(call,
				    PPTP_CDN_RESULT_ADMIN_SHUTDOWN, 0, NULL);
			}
			pptp_ctrl_send_StopCCRQ(_this, result);
			_this->state = PPTP_CTRL_STATE_WAIT_STOP_REPLY;
			break;
		}
		/* FALLTHROUGH */
	default:
		pptp_ctrl_fini(_this);
	}
	return;
}


/* finish PPTP control */
static void
pptp_ctrl_fini(pptp_ctrl *_this)
{
	pptp_call *call;

	PPTP_CTRL_ASSERT(_this != NULL);

	if (_this->sock >= 0) {
		event_del(&_this->ev_sock);
		close(_this->sock);
		_this->sock = -1;
	}
	for (slist_itr_first(&_this->call_list);
	    slist_itr_has_next(&_this->call_list);) {
		call = slist_itr_next(&_this->call_list);
		pptp_call_stop(call);
		pptp_call_destroy(call);
		slist_itr_remove(&_this->call_list);
	}

	if (_this->on_io_event != 0) {
		/*
		 * as the complete terminate process needs complicated
		 * exception handling, do partially at here.
		 * rest of part will be handled by timer-event-handler.
		 */
		PPTP_CTRL_DBG((_this, LOG_DEBUG, "Disposing"));
		_this->state = PPTP_CTRL_STATE_DISPOSING;
		pptp_ctrl_reset_timeout(_this);
		return;
	}

	evtimer_del(&_this->ev_timer);
	slist_fini(&_this->call_list);

	pptp_ctrl_log (_this, LOG_NOTICE, "logtype=Finished");

	/* disable _this */
	pptpd_ctrl_finished_notify(_this->pptpd, _this);
}

/* free PPTP control context */
void
pptp_ctrl_destroy(pptp_ctrl *_this)
{
	if (_this->send_buf != NULL) {
		bytebuffer_destroy(_this->send_buf);
		_this->send_buf = NULL;
	}
	if (_this->recv_buf != NULL) {
		bytebuffer_destroy(_this->recv_buf);
		_this->recv_buf = NULL;
	}
	free(_this);
}

/*
 * network I/O
 */
/* I/O event dispather */
static void
pptp_ctrl_io_event(int fd, short evmask, void *ctx)
{
	int sz, lpkt, hdrlen;
	u_char *pkt;
	pptp_ctrl *_this;

	_this = ctx;
	PPTP_CTRL_ASSERT(_this != NULL);

	_this->on_io_event = 1;
	if ((evmask & EV_WRITE) != 0) {
		if (pptp_ctrl_output_flush(_this) != 0 ||
		    _this->state == PPTP_CTRL_STATE_DISPOSING)
			goto fail;
		_this->send_ready = 1;
	}
	if ((evmask & EV_READ) != 0) {
		sz = read(_this->sock, bytebuffer_pointer(_this->recv_buf),
		    bytebuffer_remaining(_this->recv_buf));
		if (sz <= 0) {
			if (sz == 0 || errno == ECONNRESET) {
				pptp_ctrl_log(_this, LOG_INFO,
				    "Connection closed by foreign host");
				pptp_ctrl_fini(_this);
				goto fail;
			} else if (errno != EAGAIN && errno != EINTR) {
				pptp_ctrl_log(_this, LOG_INFO,
				    "read() failed at %s(): %m", __func__);
				pptp_ctrl_fini(_this);
				goto fail;
			}
		}
		bytebuffer_put(_this->recv_buf, BYTEBUFFER_PUT_DIRECT, sz);
		bytebuffer_flip(_this->recv_buf);

		for (;;) {
			pkt = bytebuffer_pointer(_this->recv_buf);
			lpkt = bytebuffer_remaining(_this->recv_buf);
			if (pkt == NULL ||
			    lpkt < sizeof(struct pptp_ctrl_header))
				break;	/* read again */

			hdrlen = pkt[0] << 8 | pkt[1];
			if (lpkt < hdrlen)
				break;	/* read again */

			bytebuffer_get(_this->recv_buf, NULL, hdrlen);
			if (pptp_ctrl_input(_this, pkt, hdrlen) != 0 ||
			    _this->state == PPTP_CTRL_STATE_DISPOSING) {
				bytebuffer_compact(_this->recv_buf);
				goto fail;
			}
		}
		bytebuffer_compact(_this->recv_buf);
	}
	if (pptp_ctrl_output_flush(_this) != 0)
		goto fail;
	pptp_ctrl_set_io_event(_this);
fail:
	_this->on_io_event = 0;
}


/* set i/o event mask */
static void
pptp_ctrl_set_io_event(pptp_ctrl *_this)
{
	int evmask;

	PPTP_CTRL_ASSERT(_this != NULL);
	PPTP_CTRL_ASSERT(_this->sock >= 0);

	evmask = 0;
	if (bytebuffer_remaining(_this->recv_buf) > 128)
		evmask |= EV_READ;
	if (_this->send_ready == 0)
		evmask |= EV_WRITE;

	event_del(&_this->ev_sock);
	if (evmask != 0) {
		event_set(&_this->ev_sock, _this->sock, evmask,
		    pptp_ctrl_io_event, _this);
		event_add(&_this->ev_sock, NULL);
	}
}

/**
 * Output PPTP control packet
 * @param pkt	pointer to packet buffer.
 *		when it was appended by _this-.send_buf using bytebuffer,
 *		specify NULL.
 * @param lpkt	packet length
 */
void
pptp_ctrl_output(pptp_ctrl *_this, u_char *pkt, int lpkt)
{
	PPTP_CTRL_ASSERT(_this != NULL);
	PPTP_CTRL_ASSERT(lpkt > 0);

	/* just put the packet into the buffer now.  send it later */
	bytebuffer_put(_this->send_buf, pkt, lpkt);

	if (_this->on_io_event != 0) {
		/*
		 * pptp_ctrl_output_flush() will be called by the end of
		 * the I/O event handler.
		 */
	} else {
		/*
		 * When this function is called by other than I/O event handler,
		 * we need to call pptp_ctrl_output_flush().  However if we do
		 * it here, then we need to consider the situation
		 * 'flush => write failure => finalize'.  The situation requires
		 * the caller function to handle the exception and causes
		 * complication.  So we call pptp_ctrl_output_flush() by the
		 * the next send ready event.
		 */
		_this->send_ready = 0;		/* clear 'send ready' */
		pptp_ctrl_set_io_event(_this);	/* wait 'send ready */
	}

	return;
}

/* Send Stop-Control-Connection-Request */

/* flush output packet */
static int
pptp_ctrl_output_flush(pptp_ctrl *_this)
{
	int sz;
	time_t curr_time;

	curr_time = get_monosec();

	if (bytebuffer_position(_this->send_buf) <= 0)
		return 0;		/* nothing to write */
	if (_this->send_ready == 0) {
		pptp_ctrl_set_io_event(_this);
		return 0;		/* not ready to write */
	}

	bytebuffer_flip(_this->send_buf);

	if (PPTP_CTRL_CONF(_this)->ctrl_out_pktdump != 0) {
		pptp_ctrl_log(_this, LOG_DEBUG, "PPTP Control output packet");
		show_hd(debug_get_debugfp(),
		    bytebuffer_pointer(_this->send_buf),
		    bytebuffer_remaining(_this->send_buf));
	}
	if ((sz = write(_this->sock, bytebuffer_pointer(_this->send_buf),
	    bytebuffer_remaining(_this->send_buf))) < 0) {
		pptp_ctrl_log(_this, LOG_ERR, "write to socket failed: %m");
		pptp_ctrl_fini(_this);

		return 1;
	}
	_this->last_snd_ctrl = curr_time;
	bytebuffer_get(_this->send_buf, NULL, sz);
	bytebuffer_compact(_this->send_buf);
	_this->send_ready = 0;

	return 0;
}

/* convert Start-Control-Connection-{Request,Reply} packet to strings */
static void
pptp_ctrl_SCCRx_string(struct pptp_scc *scc, u_char *buf, int lbuf)
{
	char results[128];

	if (scc->result_code != 0)
		snprintf(results, sizeof(results), "result=%d error=%d ",
		    scc->result_code, scc->error_code);
	else
		results[0] = '\0';

	snprintf(buf, lbuf,
	    "protocol_version=%d.%d %sframing=%s bearer=%s max_channels=%d "
	    "firmware_revision=%d(0x%04x) host_name=\"%.*s\" "
	    "vendor_string=\"%.*s\"",
	    scc->protocol_version >> 8, scc->protocol_version & 0xff, results,
	    pptp_framing_string(scc->framing_caps),
	    pptp_bearer_string(scc->bearer_caps), scc->max_channels,
	    scc->firmware_revision, scc->firmware_revision,
	    (u_int)sizeof(scc->host_name), scc->host_name,
	    (u_int)sizeof(scc->vendor_string), scc->vendor_string);
}

/* receive Start-Control-Connection-Request */
static int
pptp_ctrl_recv_SCCRQ(pptp_ctrl *_this, u_char *pkt, int lpkt)
{
	char logbuf[512];
	struct pptp_scc *scc;

	/* sanity check */
	if (lpkt < sizeof(struct pptp_scc)) {
		pptp_ctrl_log(_this, LOG_ERR, "Received bad SCCRQ: packet too "
		    "short: %d < %d", lpkt, (int)sizeof(struct pptp_scc));
		return 1;
	}
	scc = (struct pptp_scc *)pkt;

	scc->protocol_version = ntohs(scc->protocol_version);
	scc->framing_caps = htonl(scc->framing_caps);
	scc->bearer_caps = htonl(scc->bearer_caps);
	scc->max_channels = htons(scc->max_channels);
	scc->firmware_revision = htons(scc->firmware_revision);

	/* check protocol version */
	if (scc->protocol_version != PPTP_RFC_2637_VERSION) {
		pptp_ctrl_log(_this, LOG_ERR, "Received bad SCCRQ: "
		    "unknown protocol version %d", scc->protocol_version);
		return 1;
	}

	pptp_ctrl_SCCRx_string(scc, logbuf, sizeof(logbuf));
	pptp_ctrl_log(_this, LOG_INFO, "RecvSCCRQ %s", logbuf);

	return 0;
}

/* Receive Stop-Control-Connection-Reply */
static int
pptp_ctrl_recv_StopCCRP(pptp_ctrl *_this, u_char *pkt, int lpkt)
{
	struct pptp_stop_ccrp *stop_ccrp;

	if (lpkt < sizeof(struct pptp_stop_ccrp)) {
		pptp_ctrl_log(_this, LOG_ERR, "Received bad StopCCRP: packet "
		    "too short: %d < %d", lpkt,
		    (int)sizeof(struct pptp_stop_ccrp));
		return 1;
	}
	stop_ccrp = (struct pptp_stop_ccrp *)pkt;

	pptp_ctrl_log(_this, LOG_INFO, "RecvStopCCRP reason=%s(%u)",
	    pptp_StopCCRP_result_string(stop_ccrp->result), stop_ccrp->result);

	return 0;
}

static int
pptp_ctrl_send_StopCCRQ(pptp_ctrl *_this, int reason)
{
	int lpkt;
	struct pptp_stop_ccrq *stop_ccrq;

	stop_ccrq = bytebuffer_pointer(_this->send_buf);
	lpkt = bytebuffer_remaining(_this->send_buf);
	if (lpkt < sizeof(struct pptp_stop_ccrq)) {
		pptp_ctrl_log(_this, LOG_ERR,
		    "SendCCRP failed: No buffer space available");
		return -1;
	}
	memset(stop_ccrq, 0, sizeof(struct pptp_stop_ccrq));

	pptp_init_header(&stop_ccrq->header, sizeof(struct pptp_stop_ccrq),
	    PPTP_CTRL_MES_CODE_StopCCRQ);

	stop_ccrq->reason = reason;

	pptp_ctrl_log(_this, LOG_INFO, "SendStopCCRQ reason=%s(%u)",
	    pptp_StopCCRQ_reason_string(stop_ccrq->reason), stop_ccrq->reason);

	pptp_ctrl_output(_this, NULL, sizeof(struct pptp_stop_ccrq));

	return 0;
}

/* Receive Stop-Control-Connection-Request */
static int
pptp_ctrl_recv_StopCCRQ(pptp_ctrl *_this, u_char *pkt, int lpkt)
{
	struct pptp_stop_ccrq *stop_ccrq;

	if (lpkt < sizeof(struct pptp_stop_ccrq)) {
		pptp_ctrl_log(_this, LOG_ERR, "Received bad StopCCRQ: packet "
		    "too short: %d < %d", lpkt,
		    (int)sizeof(struct pptp_stop_ccrq));
		return 1;
	}
	stop_ccrq = (struct pptp_stop_ccrq *)pkt;

	pptp_ctrl_log(_this, LOG_INFO, "RecvStopCCRQ reason=%s(%u)",
	    pptp_StopCCRQ_reason_string(stop_ccrq->reason), stop_ccrq->reason);

	return 0;
}

/* Send Stop-Control-Connection-Reply */
static int
pptp_ctrl_send_StopCCRP(pptp_ctrl *_this, int result, int error)
{
	int lpkt;
	struct pptp_stop_ccrp *stop_ccrp;

	stop_ccrp = bytebuffer_pointer(_this->send_buf);

	lpkt = bytebuffer_remaining(_this->send_buf);
	if (lpkt < sizeof(struct pptp_stop_ccrp)) {
		pptp_ctrl_log(_this, LOG_ERR,
		    "SendCCRQ failed: No buffer space available");
		return -1;
	}
	memset(stop_ccrp, 0, sizeof(struct pptp_stop_ccrp));

	pptp_init_header(&stop_ccrp->header, sizeof(struct pptp_stop_ccrp),
	    PPTP_CTRL_MES_CODE_StopCCRP);

	stop_ccrp->result = result;
	stop_ccrp->error = error;

	pptp_ctrl_log(_this, LOG_INFO,
	    "SendStopCCRP result=%s(%u) error=%s(%u)",
	    pptp_StopCCRP_result_string(stop_ccrp->result), stop_ccrp->result,
	    pptp_general_error_string(stop_ccrp->error), stop_ccrp->error);

	pptp_ctrl_output(_this, NULL, sizeof(struct pptp_stop_ccrp));

	return 0;
}

/* Send Start-Control-Connection-Reply */
static int
pptp_ctrl_send_SCCRP(pptp_ctrl *_this, int result, int error)
{
	int lpkt;
	struct pptp_scc *scc;
	char logbuf[512];
	const char *val;

	scc = bytebuffer_pointer(_this->send_buf);
	lpkt = bytebuffer_remaining(_this->send_buf);
	if (lpkt < sizeof(struct pptp_scc)) {
		pptp_ctrl_log(_this, LOG_ERR,
		    "SendSCCRP failed: No buffer space available");
		return -1;
	}
	memset(scc, 0, sizeof(struct pptp_scc));

	pptp_init_header(&scc->header, sizeof(struct pptp_scc),
	    PPTP_CTRL_MES_CODE_SCCRP);

	scc->protocol_version = PPTP_RFC_2637_VERSION;
	scc->result_code = result;
	scc->error_code = error;

	/* XXX only support sync frames */
	scc->framing_caps = PPTP_CTRL_FRAMING_SYNC;
	scc->bearer_caps = PPTP_CTRL_BEARER_DIGITAL;

	scc->max_channels = 4;		/* XXX */
	scc->firmware_revision = MAJOR_VERSION << 8 | MINOR_VERSION;

	/* this implementation only support these strings up to
	 * 63 character */
	/* host name */

	if ((val = PPTP_CTRL_CONF(_this)->hostname) == NULL)
		val = "";
	strlcpy(scc->host_name, val, sizeof(scc->host_name));

	/* vendor name */
	if (PPTP_CTRL_CONF(_this)->vendor_name == NULL)
		val = PPTPD_DEFAULT_VENDOR_NAME;
	strlcpy(scc->vendor_string, val, sizeof(scc->vendor_string));

	pptp_ctrl_SCCRx_string(scc, logbuf, sizeof(logbuf));
	pptp_ctrl_log(_this, LOG_INFO, "SendSCCRP %s", logbuf);

	scc->protocol_version = htons(scc->protocol_version);
	scc->framing_caps = htonl(scc->framing_caps);
	scc->bearer_caps = htonl(scc->bearer_caps);
	scc->max_channels = htons(scc->max_channels);
	scc->firmware_revision = htons(scc->firmware_revision);

	pptp_ctrl_output(_this, NULL, sizeof(struct pptp_scc));

	return 0;
}

/* receive ECHO and reply */
static void
pptp_ctrl_process_echo_req(pptp_ctrl *_this, u_char *pkt, int lpkt)
{
	struct pptp_echo_rq *echo_rq;
	struct pptp_echo_rp *echo_rp;

	if (lpkt < sizeof(struct pptp_echo_rq)) {
		pptp_ctrl_log(_this, LOG_ERR, "Received bad EchoReq: packet "
		    "too short: %d < %d", lpkt,
		    (int)sizeof(struct pptp_echo_rq));
		return;
	}
	echo_rq = (struct pptp_echo_rq *)pkt;

	PPTP_CTRL_DBG((_this, LOG_DEBUG, "RecvEchoReq"));

	echo_rp = bytebuffer_pointer(_this->send_buf);
	lpkt = bytebuffer_remaining(_this->send_buf);
	if (echo_rp == NULL || lpkt < sizeof(struct pptp_echo_rp)) {
		pptp_ctrl_log(_this, LOG_ERR,
		    "Failed to send EchoReq: No buffer space available");
		return;
	}
	memset(echo_rp, 0, sizeof(struct pptp_echo_rp));

	pptp_init_header(&echo_rp->header, sizeof(struct pptp_echo_rp),
	    PPTP_CTRL_MES_CODE_ECHO_RP);

	echo_rp->identifier = echo_rq->identifier;
	echo_rp->result_code = PPTP_ECHO_RP_RESULT_OK;
	echo_rp->error_code = PPTP_ERROR_NONE;
	echo_rp->reserved1 = htons(0);

	pptp_ctrl_output(_this, NULL, sizeof(struct pptp_echo_rp));
	PPTP_CTRL_DBG((_this, LOG_DEBUG, "SendEchoReply"));
}

/* receiver Echo-Reply */
static int
pptp_ctrl_recv_echo_rep(pptp_ctrl *_this, u_char *pkt, int lpkt)
{
	struct pptp_echo_rp *echo_rp;

	if (lpkt < sizeof(struct pptp_echo_rp)) {
		pptp_ctrl_log(_this, LOG_ERR, "Received bad EchoReq: packet "
		    "too short: %d < %d", lpkt,
		    (int)sizeof(struct pptp_echo_rp));
		return 1;
	}
	echo_rp = (struct pptp_echo_rp *)pkt;

	if (echo_rp->result_code != PPTP_ECHO_RP_RESULT_OK) {
		pptp_ctrl_log(_this, LOG_ERR, "Received negative EchoReply: %s",
		    pptp_general_error_string(echo_rp->error_code));
		return 1;
	}
	if (_this->echo_seq != ntohl(echo_rp->identifier)) {
		pptp_ctrl_log(_this, LOG_ERR, "Received bad EchoReply: "
		    "Identifier mismatch sent=%u recv=%u",
		    _this->echo_seq , ntohl(echo_rp->identifier));
		return 1;
	}
	PPTP_CTRL_DBG((_this, LOG_DEBUG, "RecvEchoReply"));
	return 0;
}

/* send Echo-Request */
static void
pptp_ctrl_send_echo_req(pptp_ctrl *_this)
{
	int lpkt;
	struct pptp_echo_rq *echo_rq;

	echo_rq = (struct pptp_echo_rq *)bytebuffer_pointer(_this->send_buf);
	lpkt = bytebuffer_remaining(_this->send_buf);
	if (echo_rq == NULL || lpkt < sizeof(struct pptp_echo_rq)) {
		pptp_ctrl_log(_this, LOG_ERR,
		    "SendEchoReq failed: No buffer space available");
		return;
	}
	memset(echo_rq, 0, sizeof(struct pptp_echo_rq));

	pptp_init_header(&echo_rq->header, sizeof(struct pptp_echo_rq),
	    PPTP_CTRL_MES_CODE_ECHO_RQ);

	echo_rq->identifier = htonl(_this->echo_seq);

	pptp_ctrl_output(_this, NULL, sizeof(struct pptp_echo_rq));
	PPTP_CTRL_DBG((_this, LOG_DEBUG, "SendEchoReq"));
}

/* send Call-Disconnect-Notify */
static void
pptp_ctrl_send_CDN(pptp_ctrl *_this, int result, int error, int cause,
    const char *statistics)
{
	int lpkt;
	struct pptp_cdn *cdn;

	cdn = bytebuffer_pointer(_this->send_buf);
	lpkt = bytebuffer_remaining(_this->send_buf);
	if (lpkt < sizeof(struct pptp_cdn)) {
		pptp_ctrl_log(_this, LOG_ERR,
		    "SendCCR failed: No buffer space available");
		return;
	}
	memset(cdn, 0, sizeof(struct pptp_cdn));

	pptp_init_header(&cdn->header, sizeof(struct pptp_cdn),
	    PPTP_CTRL_MES_CODE_CDN);

	cdn->call_id = _this->id;
	cdn->result_code = result;
	cdn->error_code = error;
	cdn->cause_code = cause;
	if (statistics != NULL)
		strlcpy(cdn->statistics, statistics, sizeof(cdn->statistics));

	cdn->call_id = htons(cdn->call_id);
	cdn->cause_code = htons(cdn->cause_code);

	pptp_ctrl_output(_this, NULL, sizeof(struct pptp_cdn));
}

/* receive Control-packet */
static int
pptp_ctrl_input(pptp_ctrl *_this, u_char *pkt, int lpkt)
{
	char errmes[256];
	time_t curr_time;
	struct pptp_ctrl_header *hdr;

	PPTP_CTRL_ASSERT(lpkt >= sizeof(struct pptp_ctrl_header));

	curr_time = get_monosec();
	hdr = (struct pptp_ctrl_header *)pkt;

	hdr->length = ntohs(hdr->length);
	hdr->pptp_message_type  = ntohs(hdr->pptp_message_type);
	hdr->magic_cookie  = ntohl(hdr->magic_cookie);
	hdr->control_message_type = ntohs(hdr->control_message_type);
	hdr->reserved0 = ntohs(hdr->reserved0);

	/* sanity check */
	PPTP_CTRL_ASSERT(hdr->length <= lpkt);

	_this->last_rcv_ctrl = curr_time;

	if (PPTP_CTRL_CONF(_this)->ctrl_in_pktdump != 0) {
		pptp_ctrl_log(_this, LOG_DEBUG,
		    "PPTP Control input packet dump: mestype=%s(%d)",
		    pptp_ctrl_mes_type_string(hdr->control_message_type),
		    hdr->control_message_type);
		show_hd(debug_get_debugfp(), pkt, lpkt);
	}

	/* inspect packet body */
	/* message type */
	if (hdr->pptp_message_type != PPTP_MES_TYPE_CTRL) {
		snprintf(errmes, sizeof(errmes), "unknown message type %d",
		    hdr->pptp_message_type);
		goto bad_packet;
	}
	/* magic cookie */
	if (hdr->magic_cookie != PPTP_MAGIC_COOKIE) {
		snprintf(errmes, sizeof(errmes), "wrong magic %08x != %08x",
		    hdr->magic_cookie, PPTP_MAGIC_COOKIE);
		goto bad_packet;
	}

	/* As there is possibility of state conflicts,
	 * ECHO Reply requiries special care.
	 */
	switch (hdr->control_message_type) {
	case PPTP_CTRL_MES_CODE_ECHO_RP:
		if (pptp_ctrl_recv_echo_rep(_this, pkt, lpkt) != 0) {
			pptp_ctrl_fini(_this);
			return 1;
		}
		return 0;
	}

	/*
	 * State machine
	 */
	switch (_this->state) {
	case PPTP_CTRL_STATE_IDLE:
		switch (hdr->control_message_type) {
		case PPTP_CTRL_MES_CODE_SCCRQ:
			if (pptp_ctrl_recv_SCCRQ(_this, pkt, lpkt) != 0) {
				return 0;
			}
			if (pptp_ctrl_send_SCCRP(_this,
			    PPTP_SCCRP_RESULT_SUCCESS, PPTP_ERROR_NONE) != 0) {
				return 0;
			}
			_this->state = PPTP_CTRL_STATE_ESTABLISHED;
			return 0;
		default:
			break;
		}
		break;
	case PPTP_CTRL_STATE_ESTABLISHED:
		switch (hdr->control_message_type) {
		case PPTP_CTRL_MES_CODE_ECHO_RQ:
			pptp_ctrl_process_echo_req(_this, pkt, lpkt);
			return 0;
		/* dispatch to pptp_call_input() if it is call-related-packet */
		case PPTP_CTRL_MES_CODE_SLI:
		case PPTP_CTRL_MES_CODE_ICRQ:
		case PPTP_CTRL_MES_CODE_ICRP:
		case PPTP_CTRL_MES_CODE_OCRQ:
		case PPTP_CTRL_MES_CODE_OCRP:
		case PPTP_CTRL_MES_CODE_ICCN:
		case PPTP_CTRL_MES_CODE_CDN:
		case PPTP_CTRL_MES_CODE_CCR:
			return pptp_ctrl_call_input(_this,
			    hdr->control_message_type, pkt, lpkt);
		case PPTP_CTRL_MES_CODE_StopCCRQ:
			if (pptp_ctrl_recv_StopCCRQ(_this, pkt, lpkt) != 0) {
				pptp_ctrl_stop(_this,
				    PPTP_StopCCRQ_REASON_STOP_PROTOCOL);
				return 0;
			}
			if (pptp_ctrl_send_StopCCRP(_this,
				PPTP_StopCCRP_RESULT_OK, PPTP_ERROR_NONE)!= 0) {
				return 0;
			}
			pptp_ctrl_fini(_this);
			return 1;
		default:
			break;
		}
	case PPTP_CTRL_STATE_WAIT_STOP_REPLY:
		switch (hdr->control_message_type) {
		case PPTP_CTRL_MES_CODE_StopCCRP:
			pptp_ctrl_recv_StopCCRP(_this, pkt, lpkt);
			pptp_ctrl_fini(_this);
			return 1;
		}
		break;
	case PPTP_CTRL_STATE_WAIT_CTRL_REPLY:
		/* XXX this implementation only support PAC mode */
		break;
	}
	pptp_ctrl_log(_this, LOG_WARNING,
	    "Unhandled control message type=%s(%d)",
	    pptp_ctrl_mes_type_string(hdr->control_message_type),
	    hdr->control_message_type);
	return 0;

bad_packet:
	pptp_ctrl_log(_this, LOG_ERR, "Received bad packet: %s", errmes);
	pptp_ctrl_stop(_this, PPTP_StopCCRQ_REASON_STOP_PROTOCOL);

	return 0;
}

/* receiver PPTP Call related messages */
static int
pptp_ctrl_call_input(pptp_ctrl *_this, int mes_type, u_char *pkt, int lpkt)
{
	int i, call_id, lpkt0;
	pptp_call *call;
	const char *reason;
	u_char *pkt0;

	pkt0 = pkt;
	lpkt0 = lpkt;
	call_id = -1;
	pkt += sizeof(struct pptp_ctrl_header);
	lpkt -= sizeof(struct pptp_ctrl_header);
	reason = "(no reason)";

	/* sanity check */
	if (lpkt < 4) {
		reason = "received packet is too short";
		goto badpacket;
	}
	call = NULL;
	call_id = ntohs(*(uint16_t *)pkt);

	switch (mes_type) {
	case PPTP_CTRL_MES_CODE_SLI:	/* PNS <=> PAC */
		/* only SLI contains Call-ID of this peer */
		for (i = 0; i < slist_length(&_this->call_list); i++) {
			call = slist_get(&_this->call_list, i);
			if (call->id == call_id)
				break;
			call = NULL;
		}
		if (call == NULL) {
			reason = "Call Id is not associated by this control";
			goto badpacket;
		}
		goto call_searched;
	case PPTP_CTRL_MES_CODE_ICRP:	/* PNS => PAC */
		/*
		 * as this implementation never sent ICRQ, this case
		 * should not happen.
		 * But just to make sure, pptp_call.c can handle this
		 * message.
		 */
		/* FALLTHROUGH */
	case PPTP_CTRL_MES_CODE_OCRQ:	/* PNS => PAC */
	case PPTP_CTRL_MES_CODE_CCR:	/* PNS => PAC */
		/* liner-search will be enough */
		for (i = 0; i < slist_length(&_this->call_list); i++) {
			call = slist_get(&_this->call_list, i);
			if (call->peers_call_id == call_id)
				break;
			call = NULL;
		}
		if (call == NULL && mes_type == PPTP_CTRL_MES_CODE_CCR) {
			pptp_ctrl_send_CDN(_this, PPTP_CDN_RESULT_GENRIC_ERROR,
			    PPTP_ERROR_BAD_CALL, 0, NULL);
			goto call_searched;
		}
		if (mes_type == PPTP_CTRL_MES_CODE_OCRQ) {
			/* make new call */
			if (call != NULL) {
				pptp_call_input(call, mes_type, pkt0, lpkt0);
				return 0;
			}
			if ((call = pptp_call_create()) == NULL) {
				pptp_ctrl_log(_this, LOG_ERR,
				    "pptp_call_create() failed: %m");
				goto fail;
			}
			if (pptp_call_init(call, _this) != 0) {
				pptp_ctrl_log(_this, LOG_ERR,
				    "pptp_call_init() failed: %m");
				pptp_call_destroy(call);
				goto fail;
			}
			slist_add(&_this->call_list, call);
		}
call_searched:
		if (call == NULL) {
			reason = "Call Id is not associated by this control";
			goto badpacket;
		}
		pptp_call_input(call, mes_type, pkt0, lpkt0);
		return 0;
	case PPTP_CTRL_MES_CODE_OCRP:	/* PAC => PNS */
	case PPTP_CTRL_MES_CODE_ICRQ:	/* PAC => PNS */
	case PPTP_CTRL_MES_CODE_ICCN:	/* PAC => PNS */
	case PPTP_CTRL_MES_CODE_CDN:	/* PAC => PNS */
		/* don't receive because above messages are only of PNS */
	default:
		break;
	}
	reason = "Message type is unexpected.";
	/* FALLTHROUGH */
badpacket:
	pptp_ctrl_log(_this, LOG_INFO,
	    "Received a bad %s(%d) call_id=%d: %s",
		pptp_ctrl_mes_type_string(mes_type), mes_type, call_id, reason);
	return 0;
fail:
	pptp_ctrl_stop(_this, PPTP_StopCCRQ_REASON_STOP_PROTOCOL);
	return 0;
}


/*
 * utilities
 */

/* logging with the label of the instance */
static void
pptp_ctrl_log(pptp_ctrl *_this, int prio, const char *fmt, ...)
{
	char logbuf[BUFSIZ];
	va_list ap;

	va_start(ap, fmt);
#ifdef	PPTPD_MULTIPLE
	snprintf(logbuf, sizeof(logbuf), "pptpd id=%u ctrl=%u %s",
	    _this->pptpd->id, _this->id, fmt);
#else
	snprintf(logbuf, sizeof(logbuf), "pptpd ctrl=%u %s", _this->id, fmt);
#endif
	vlog_printf(prio, logbuf, ap);
	va_end(ap);
}

static const char *
pptp_ctrl_state_string(int state)
{
	switch (state) {
	case PPTP_CTRL_STATE_IDLE:
		return "idle";
	case PPTP_CTRL_STATE_WAIT_CTRL_REPLY:
		return "wait-ctrl-reply";
	case PPTP_CTRL_STATE_ESTABLISHED:
		return "established";
	case PPTP_CTRL_STATE_WAIT_STOP_REPLY:
		return "wait-stop-reply";
	}
	return "unknown";
}
