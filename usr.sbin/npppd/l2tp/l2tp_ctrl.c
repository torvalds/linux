/*	$OpenBSD: l2tp_ctrl.c,v 1.27 2022/12/28 21:30:17 jmc Exp $	*/

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
/**@file Control connection processing functions for L2TP LNS */
/* $Id: l2tp_ctrl.c,v 1.27 2022/12/28 21:30:17 jmc Exp $ */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <endian.h>
#include <errno.h>
#include <event.h>
#include <netdb.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

#ifdef USE_LIBSOCKUTIL
#include <seil/sockfromto.h>
#endif

#include "time_utils.h"
#include "bytebuf.h"
#include "hash.h"
#include "debugutil.h"
#include "slist.h"
#include "l2tp.h"
#include "l2tp_local.h"
#include "l2tp_subr.h"
#include "net_utils.h"
#include "version.h"
#include "recvfromto.h"

#define MINIMUM(a, b)	(((a) < (b)) ? (a) : (b))

static int		 l2tp_ctrl_init(l2tp_ctrl *, l2tpd *, struct sockaddr *, struct sockaddr *, void *);
static void		 l2tp_ctrl_reload(l2tp_ctrl *);
static int		 l2tp_ctrl_send_disconnect_notify(l2tp_ctrl *);
static void		 l2tp_ctrl_timeout(int, short, void *);
static int		 l2tp_ctrl_resend_una_packets(l2tp_ctrl *, bool);
static void		 l2tp_ctrl_destroy_all_calls(l2tp_ctrl *);
static int		 l2tp_ctrl_disconnect_all_calls(l2tp_ctrl *, int);
static void		 l2tp_ctrl_reset_timeout(l2tp_ctrl *);
static int		 l2tp_ctrl_txwin_size(l2tp_ctrl *);
static bool		 l2tp_ctrl_txwin_is_full(l2tp_ctrl *);
static bool		 l2tp_ctrl_in_peer_window(l2tp_ctrl *, uint16_t);
static bool		 l2tp_ctrl_in_our_window(l2tp_ctrl *, uint16_t);
static int		 l2tp_ctrl_recv_SCCRQ(l2tp_ctrl *, u_char *, int, l2tpd *, struct sockaddr *);
static int		 l2tp_ctrl_send_StopCCN(l2tp_ctrl *, int);
static int		 l2tp_ctrl_recv_StopCCN(l2tp_ctrl *, u_char *, int);
static void		 l2tp_ctrl_send_SCCRP(l2tp_ctrl *);
static int		 l2tp_ctrl_send_HELLO(l2tp_ctrl *);
static int		 l2tp_ctrl_send_ZLB(l2tp_ctrl *);
static const char	*l2tp_ctrl_state_string(l2tp_ctrl *);

#ifdef	L2TP_CTRL_DEBUG
#define	L2TP_CTRL_ASSERT(x)	ASSERT(x)
#define	L2TP_CTRL_DBG(x)	l2tp_ctrl_log x
#else
#define	L2TP_CTRL_ASSERT(x)
#define	L2TP_CTRL_DBG(x)
#endif

/* Sequence # of l2tp_ctrl ID */
static u_int l2tp_ctrl_id_seq = 0;

#define SEQ_LT(a,b)	((int16_t)((a) - (b)) <  0)
#define SEQ_GT(a,b)	((int16_t)((a) - (b)) >  0)

/**
 * Build instance of {@link ::_l2tp_ctrl L2TP LNS control connection}
 */
l2tp_ctrl *
l2tp_ctrl_create(void)
{

	return calloc(1, sizeof(l2tp_ctrl));
}

/**
 * initialize and startup of {@link ::_l2tp_ctrl L2TP LNS control connection}
 * instance
 */
static int
l2tp_ctrl_init(l2tp_ctrl *_this, l2tpd *_l2tpd, struct sockaddr *peer,
    struct sockaddr *sock, void *nat_t_ctx)
{
	int tunid, i;
	bytebuffer *bytebuf;
	time_t curr_time;

	memset(_this, 0, sizeof(l2tp_ctrl));

	curr_time = get_monosec();
	_this->l2tpd = _l2tpd;
	_this->state = L2TP_CTRL_STATE_IDLE;
	_this->last_snd_ctrl = curr_time;

	slist_init(&_this->call_list);

	/* seek a free tunnel ID */
	i = 0;
	_this->id = ++l2tp_ctrl_id_seq;
	for (i = 0, tunid = _this->id; ; i++, tunid++) {
		tunid &= 0xffff;
		_this->tunnel_id = l2tp_ctrl_id_seq & 0xffff;
		if (tunid == 0)
			continue;
		if (l2tpd_get_ctrl(_l2tpd, tunid) == NULL)
			break;
		if (i > 80000) {
			/* this must be happen, just log it. */
			l2tpd_log(_l2tpd, LOG_ERR, "Too many l2tp controls");
			return -1;
		}
	}

	_this->tunnel_id = tunid;

	L2TP_CTRL_ASSERT(peer != NULL);
	L2TP_CTRL_ASSERT(sock != NULL);
	memcpy(&_this->peer, peer, peer->sa_len);
	memcpy(&_this->sock, sock, sock->sa_len);

	/* prepare send buffer */
	_this->winsz = L2TPD_DEFAULT_SEND_WINSZ;
	/*
	 * _this->winsz is informed as our receive window size.  Also
	 * MIN(_this->winsz, _this->peer_winsiz) is used for the size of
	 * transmit side window.  We need winsz * 2 sized buffer so that a
	 * stingy client can fill both of window separately.
	 */
	_this->snd_buffercnt = _this->winsz * 2;
	if ((_this->snd_buffers = calloc(_this->snd_buffercnt,
	    sizeof(bytebuffer *))) == NULL) {
		l2tpd_log(_l2tpd, LOG_ERR,
		    "calloc() failed in %s(): %m", __func__);
		goto fail;
	}
	for (i = 0; i < _this->snd_buffercnt; i++) {
		if ((bytebuf = bytebuffer_create(L2TPD_SND_BUFSIZ)) == NULL) {
			l2tpd_log(_l2tpd, LOG_ERR,
			    "bytebuffer_create() failed in %s(): %m", __func__);
			goto fail;
		}
		_this->snd_buffers[i] = bytebuf;
	}
	if ((_this->zlb_buffer = bytebuffer_create(sizeof(struct l2tp_header)
	    + 128)) == NULL) {
		l2tpd_log(_l2tpd, LOG_ERR,
		    "bytebuffer_create() failed in %s(): %m", __func__);
		goto fail;
	}
#if defined(USE_LIBSOCKUTIL) || defined(USE_SA_COOKIE)
	if (nat_t_ctx != NULL) {
		if ((_this->sa_cookie = malloc(
		    sizeof(struct in_ipsec_sa_cookie))) != NULL) {
			*(struct in_ipsec_sa_cookie *)_this->sa_cookie =
			    *(struct in_ipsec_sa_cookie *)nat_t_ctx;
		} else {
			l2tpd_log(_l2tpd, LOG_ERR,
			    "creating sa_cookie failed: %m");
			goto fail;
		}
	}
#endif
	_this->hello_interval = L2TP_CTRL_DEFAULT_HELLO_INTERVAL;
	_this->hello_timeout = L2TP_CTRL_DEFAULT_HELLO_TIMEOUT;
	_this->hello_io_time = curr_time;

	/* initialize timeout timer */
	l2tp_ctrl_reset_timeout(_this);

	/* register l2tp context */
	l2tpd_add_ctrl(_l2tpd, _this);
	return 0;
fail:
	l2tp_ctrl_stop(_this, 0);
	return -1;
}

/*
 * setup {@link ::_l2tp_ctrl L2TP LNS control connection} instance
 */
static void
l2tp_ctrl_reload(l2tp_ctrl *_this)
{
	_this->data_use_seq = L2TP_CTRL_CONF(_this)->data_use_seq;
	if (L2TP_CTRL_CONF(_this)->hello_interval != 0)
		_this->hello_interval =  L2TP_CTRL_CONF(_this)->hello_interval;
	if (L2TP_CTRL_CONF(_this)->hello_timeout != 0)
		_this->hello_timeout = L2TP_CTRL_CONF(_this)->hello_timeout;
}

/*
 * free {@link ::_l2tp_ctrl L2TP LNS control connection} instance
 */
void
l2tp_ctrl_destroy(l2tp_ctrl *_this)
{
	L2TP_CTRL_ASSERT(_this != NULL);
#if defined(USE_LIBSOCKUTIL) || defined(USE_SA_COOKIE)
	free(_this->sa_cookie);
#endif
	free(_this);
}

/*
 * nortify disconnection to peer
 *
 * @return 	0: all CDN and StopCCN have been sent.
 *		N: if the remaining calls which still not sent CDN exist,
 *		   return # of the calls.
 *		-1: when try to send of StopCCN failed.
 */
static int
l2tp_ctrl_send_disconnect_notify(l2tp_ctrl *_this)
{
	int ncalls;

	L2TP_CTRL_ASSERT(_this != NULL)
	L2TP_CTRL_ASSERT(_this->state == L2TP_CTRL_STATE_ESTABLISHED ||
	    _this->state == L2TP_CTRL_STATE_CLEANUP_WAIT);

	/* this control is not actively closing or StopCCN have been sent */
	if (_this->active_closing == 0)
		return 0;

	/* Send CDN all Calls */
	ncalls = 0;
	if (slist_length(&_this->call_list) != 0) {
		ncalls = l2tp_ctrl_disconnect_all_calls(_this, 0);
		if (ncalls > 0) {
			/*
			 * Call the function again to check whether the
			 * sending window is fulled.  In case ncalls == 0,
			 * it means we've sent CDN for all calls.
			 */
			ncalls = l2tp_ctrl_disconnect_all_calls(_this, 0);
		}
	}
	if (ncalls > 0)
		return ncalls;

	if (l2tp_ctrl_send_StopCCN(_this, _this->active_closing) != 0)
		return -1;
	_this->active_closing = 0;

	return 0;
}

/*
 * Terminate the control connection
 *
 * <p>
 * please specify an appropriate value to result( >0 ) for
 * StopCCN ResultCode AVP, when to sent Active Close (which
 * require StopCCN sent).</p>
 * <p>
 * When the return value of this function is zero, the _this
 * is already released. The lt2p_ctrl process that was bound to it
 * could not continue.
 * When the return value of this function is one, the timer
 * is reset.</p>
 *
 * @return	return 0 if terminate process was completed.
 */
int
l2tp_ctrl_stop(l2tp_ctrl *_this, int result)
{
	int i;
	l2tpd *_l2tpd;

	L2TP_CTRL_ASSERT(_this != NULL);

	switch (_this->state) {
	case L2TP_CTRL_STATE_ESTABLISHED:
		_this->state = L2TP_CTRL_STATE_CLEANUP_WAIT;
		if (result > 0) {
			_this->active_closing = result;
			l2tp_ctrl_send_disconnect_notify(_this);
			break;
		}
		goto cleanup;
	default:
		l2tp_ctrl_log(_this, LOG_DEBUG, "%s() unexpected state=%s",
		    __func__, l2tp_ctrl_state_string(_this));
		/* FALLTHROUGH */
	case L2TP_CTRL_STATE_WAIT_CTL_CONN:
		/* FALLTHROUGH */
	case L2TP_CTRL_STATE_CLEANUP_WAIT:
cleanup:
		if (slist_length(&_this->call_list) != 0) {
			if (l2tp_ctrl_disconnect_all_calls(_this, 1) > 0)
				break;
		}

		l2tp_ctrl_log(_this, LOG_NOTICE, "logtype=Finished");

		evtimer_del(&_this->ev_timeout);

		/* free send buffer */
		if (_this->snd_buffers != NULL) {
			for (i = 0; i < _this->snd_buffercnt; i++)
				bytebuffer_destroy(_this->snd_buffers[i]);
			free(_this->snd_buffers);
			_this->snd_buffers = NULL;
		}
		if (_this->zlb_buffer != NULL) {
			bytebuffer_destroy(_this->zlb_buffer);
			_this->zlb_buffer = NULL;
		}

		/* free l2tp_call */
		l2tp_ctrl_destroy_all_calls(_this);
		slist_fini(&_this->call_list);

		l2tpd_remove_ctrl(_this->l2tpd, _this->tunnel_id);

		_l2tpd = _this->l2tpd;
		l2tp_ctrl_destroy(_this);

		l2tpd_ctrl_finished_notify(_l2tpd);
		return 0;	/* stopped */
	}
	l2tp_ctrl_reset_timeout(_this);

	return 1;
}

/* timeout processing */
static void
l2tp_ctrl_timeout(int fd, short evtype, void *ctx)
{
	int next_timeout, need_resend;
	time_t curr_time;
	l2tp_ctrl *_this;
	l2tp_call *call;

	/*
	 * the timer must be reset, when leave this function.
	 * MEMO: l2tp_ctrl_stop() will reset the timer in it.
	 * and please remember that the l2tp_ctrl_stop() may free _this.
	 */
	_this = ctx;
	L2TP_CTRL_ASSERT(_this != NULL);

	curr_time = get_monosec();

	next_timeout = 2;
	need_resend = 0;

	if (l2tp_ctrl_txwin_size(_this) > 0)  {
		if (_this->state == L2TP_CTRL_STATE_ESTABLISHED) {
			if (_this->hello_wait_ack != 0) {
				/* wait Hello reply */
				if (curr_time - _this->hello_io_time >=
				    _this->hello_timeout) {
					l2tp_ctrl_log(_this, LOG_NOTICE,
					    "timeout waiting ack for hello "
					    "packets.");
					l2tp_ctrl_stop(_this,
					    L2TP_STOP_CCN_RCODE_GENERAL);
					return;
				}
			}
		} else if (curr_time - _this->last_snd_ctrl >=
		    L2TP_CTRL_CTRL_PKT_TIMEOUT) {
			l2tp_ctrl_log(_this, LOG_NOTICE,
			    "timeout waiting ack for ctrl packets.");
			l2tp_ctrl_stop(_this,
			    L2TP_STOP_CCN_RCODE_GENERAL);
			return;
		}
		need_resend = 1;
	} else {
		for (slist_itr_first(&_this->call_list);
		    slist_itr_has_next(&_this->call_list);) {
			call = slist_itr_next(&_this->call_list);
			if (call->state == L2TP_CALL_STATE_CLEANUP_WAIT) {
				l2tp_call_destroy(call, 1);
				slist_itr_remove(&_this->call_list);
			}
		}
	}

	switch (_this->state) {
	case L2TP_CTRL_STATE_IDLE:
		/*
		 * idle:
		 * XXX: never happen in current implementation
		 */
		l2tp_ctrl_log(_this, LOG_ERR,
		    "Internal error, timeout on illegal state=idle");
		l2tp_ctrl_stop(_this, L2TP_STOP_CCN_RCODE_GENERAL);
		break;
	case L2TP_CTRL_STATE_WAIT_CTL_CONN:
		/*
		 * wait-ctrl-conn:
		 * 	if there is no ack for SCCRP, the peer will
		 * 	resend SCCRQ. however this implementation can
		 *	not recognize that the SCCRQ was resent or not.
		 *	Therefore, never resent from this side.
		 */
		need_resend = 0;
		break;
	case L2TP_CTRL_STATE_ESTABLISHED:
		if (slist_length(&_this->call_list) == 0 &&
		    curr_time - _this->last_snd_ctrl >=
			    L2TP_CTRL_WAIT_CALL_TIMEOUT) {
			if (_this->ncalls == 0)
				/* fail to receive first call */
				l2tp_ctrl_log(_this, LOG_WARNING,
				    "timeout waiting call");
			l2tp_ctrl_stop(_this,
			    L2TP_STOP_CCN_RCODE_GENERAL);
			return;
		}
		if (_this->hello_wait_ack == 0 && _this->hello_interval > 0) {
			/* send Hello */
			if (curr_time - _this->hello_interval >=
			    _this->hello_io_time) {
				if (l2tp_ctrl_send_HELLO(_this) == 0)
					/* success */
					_this->hello_wait_ack = 1;
				_this->hello_io_time = curr_time;
				need_resend = 0;
			}
		}
		break;
	case L2TP_CTRL_STATE_CLEANUP_WAIT:
		if (curr_time - _this->last_snd_ctrl >=
		    L2TP_CTRL_CLEANUP_WAIT_TIME) {
			l2tp_ctrl_log(_this, LOG_NOTICE,
			    "Cleanup timeout state=%d", _this->state);
			l2tp_ctrl_stop(_this, 0);
			return;
		}
		if (_this->active_closing != 0)
			l2tp_ctrl_send_disconnect_notify(_this);
		break;
	default:
		l2tp_ctrl_log(_this, LOG_ERR,
		    "Internal error, timeout on illegal state=%d",
			_this->state);
		l2tp_ctrl_stop(_this, L2TP_STOP_CCN_RCODE_GENERAL);
		return;
	}
	/* resend if required */
	if (need_resend)
		l2tp_ctrl_resend_una_packets(_this, true);
	l2tp_ctrl_reset_timeout(_this);
}

int
l2tp_ctrl_send(l2tp_ctrl *_this, const void *msg, int len)
{
	int rval;

#ifdef USE_LIBSOCKUTIL
	if (_this->sa_cookie != NULL)
		rval = sendfromto_nat_t(LISTENER_SOCK(_this), msg, len, 0,
		    (struct sockaddr *)&_this->sock,
		    (struct sockaddr *)&_this->peer, _this->sa_cookie);
	else
		rval = sendfromto(LISTENER_SOCK(_this), msg, len, 0,
		    (struct sockaddr *)&_this->sock,
		    (struct sockaddr *)&_this->peer);
#else
#ifdef USE_SA_COOKIE
	if (_this->sa_cookie != NULL)
		rval = sendto_nat_t(LISTENER_SOCK(_this), msg, len, 0,
		    (struct sockaddr *)&_this->peer, _this->peer.ss_len,
		    _this->sa_cookie);
	else
#endif
	rval = sendto(LISTENER_SOCK(_this), msg, len, 0,
	    (struct sockaddr *)&_this->peer, _this->peer.ss_len);
#endif
	return rval;
}

/* resend una packets */
static int
l2tp_ctrl_resend_una_packets(l2tp_ctrl *_this, bool resend)
{
	uint16_t seq;
	bytebuffer *bytebuf;
	struct l2tp_header *header;
	int nsend;

	nsend = 0;
	for (seq = _this->snd_una; SEQ_LT(seq, _this->snd_nxt); seq++) {
		if (!l2tp_ctrl_in_peer_window(_this, seq))
			break;
		if (SEQ_LT(seq, _this->snd_last) && !resend)
			continue;
		bytebuf = _this->snd_buffers[seq % _this->snd_buffercnt];
		header = bytebuffer_pointer(bytebuf);
		header->nr = htons(_this->rcv_nxt);
		_this->snd_lastnr = _this->rcv_nxt;
#ifdef L2TP_CTRL_DEBUG
		if (debuglevel >= 3) {
			l2tp_ctrl_log(_this, DEBUG_LEVEL_3, "RESEND seq=%u",
			    ntohs(header->ns));
			show_hd(debug_get_debugfp(),
			    bytebuffer_pointer(bytebuf),
			    bytebuffer_remaining(bytebuf));
		}
#endif
		if (l2tp_ctrl_send(_this, bytebuffer_pointer(bytebuf),
		    bytebuffer_remaining(bytebuf)) < 0) {
			l2tp_ctrl_log(_this, LOG_ERR,
			    "sendto() failed in %s: %m", __func__);
			return -1;
		}
		nsend++;
	}
	return nsend;
}

/* free all calls */
static void
l2tp_ctrl_destroy_all_calls(l2tp_ctrl *_this)
{
	l2tp_call *call;

	L2TP_CTRL_ASSERT(_this != NULL);

	while ((call = slist_remove_first(&_this->call_list)) != NULL)
		l2tp_call_destroy(call, 1);
}


/* disconnect all calls on the control context
 * @return return # of calls that is not waiting cleanup.
 */
static int
l2tp_ctrl_disconnect_all_calls(l2tp_ctrl *_this, int drop)
{
	int i, len, ncalls;
	l2tp_call *call;

	L2TP_CTRL_ASSERT(_this != NULL);

	ncalls = 0;
	len = slist_length(&_this->call_list);
	for (i = 0; i < len; i++) {
		call = slist_get(&_this->call_list, i);
		if (call->state != L2TP_CALL_STATE_CLEANUP_WAIT) {
			ncalls++;
			if (l2tp_ctrl_txwin_is_full(_this)) {
				L2TP_CTRL_DBG((_this, LOG_INFO,
				    "Too many calls.  Sending window is not "
				    "enough to send CDN to all clients."));
				if (drop)
					l2tp_call_drop(call);
			} else
				l2tp_call_admin_disconnect(call);
		}
	}
	return ncalls;
}

/* reset timeout */
static void
l2tp_ctrl_reset_timeout(l2tp_ctrl *_this)
{
	int intvl;
	struct timeval tv0;

	L2TP_CTRL_ASSERT(_this != NULL);

	if (evtimer_initialized(&_this->ev_timeout))
		evtimer_del(&_this->ev_timeout);

	switch (_this->state) {
	case L2TP_CTRL_STATE_CLEANUP_WAIT:
		intvl = 1;
		break;
	default:
		intvl = 2;
		break;
	}
	tv0.tv_usec = 0;
	tv0.tv_sec = intvl;
	if (!evtimer_initialized(&_this->ev_timeout))
		evtimer_set(&_this->ev_timeout, l2tp_ctrl_timeout, _this);
	evtimer_add(&_this->ev_timeout, &tv0);
}

/*
 * protocols / send and receive
 */
/* Receive packet */
void
l2tp_ctrl_input(l2tpd *_this, int listener_index, struct sockaddr *peer,
    struct sockaddr *sock, void *nat_t_ctx, u_char *pkt, int pktlen)
{
	int i, len, offsiz, reqlen, is_ctrl;
	uint16_t mestype;
	struct l2tp_avp *avp, *avp0;
	l2tp_ctrl *ctrl;
	l2tp_call *call;
	char buf[L2TP_AVP_MAXSIZ], errmsg[256];
	time_t curr_time;
	u_char *pkt0;
	struct l2tp_header hdr;
	char hbuf[NI_MAXHOST + NI_MAXSERV + 16];

	ctrl = NULL;
	curr_time = get_monosec();
	pkt0 = pkt;

	L2TP_CTRL_ASSERT(peer->sa_family == sock->sa_family);
	L2TP_CTRL_ASSERT(peer->sa_family == AF_INET ||
	    peer->sa_family == AF_INET6)
    /*
     * Parse L2TP Header
     */
	memset(&hdr, 0, sizeof(hdr));
	if (pktlen < 2) {
		snprintf(errmsg, sizeof(errmsg), "a short packet.  "
		    "length=%d", pktlen);
		goto bad_packet;
	}
	memcpy(&hdr, pkt, 2);
	pkt += 2;
	if (hdr.ver != L2TP_HEADER_VERSION_RFC2661) {
		/* XXX: only RFC2661 is supported */
		snprintf(errmsg, sizeof(errmsg),
		    "Unsupported version at header = %d", hdr.ver);
		goto bad_packet;
	}
	is_ctrl = (hdr.t != 0)? 1 : 0;

	/* calc required length */
	reqlen = 6;		/* for Flags, Tunnel-Id, Session-Id field */
	if (hdr.l) reqlen += 2;	/* for Length field (opt) */
	if (hdr.s) reqlen += 4;	/* for Ns, Nr field (opt) */
	if (hdr.o) reqlen += 2;	/* for Offset Size field (opt) */
	if (reqlen > pktlen) {
		snprintf(errmsg, sizeof(errmsg),
		    "a short packet. length=%d", pktlen);
		goto bad_packet;
	}

	if (hdr.l != 0) {
		GETSHORT(hdr.length, pkt);
		if (hdr.length > pktlen) {
			snprintf(errmsg, sizeof(errmsg),
			    "Actual packet size is smaller than the length "
			    "field %d < %d", pktlen, hdr.length);
			goto bad_packet;
		}
		pktlen = hdr.length;	/* remove trailing trash */
	}
	GETSHORT(hdr.tunnel_id, pkt);
	GETSHORT(hdr.session_id, pkt);
	if (hdr.s != 0) {
		GETSHORT(hdr.ns, pkt);
		GETSHORT(hdr.nr, pkt);
	}
	if (hdr.o != 0) {
		GETSHORT(offsiz, pkt);
		if (pktlen < offsiz) {
			snprintf(errmsg, sizeof(errmsg),
			    "offset field is bigger than remaining packet "
			    "length %d > %d", offsiz, pktlen);
			goto bad_packet;
		}
		pkt += offsiz;
	}
	L2TP_CTRL_ASSERT(pkt - pkt0 == reqlen);
	pktlen -= (pkt - pkt0);	/* cut down the length of header */

	ctrl = NULL;
	memset(buf, 0, sizeof(buf));
	mestype = 0;
	avp = NULL;

	if (is_ctrl) {
		avp0 = (struct l2tp_avp *)buf;
		avp = avp_find_message_type_avp(avp0, pkt, pktlen);
		if (avp != NULL)
			mestype = avp->attr_value[0] << 8 | avp->attr_value[1];
	}
	ctrl = l2tpd_get_ctrl(_this, hdr.tunnel_id);

	if (ctrl == NULL) {
		/* new control */
		if (!is_ctrl) {
			snprintf(errmsg, sizeof(errmsg),
			    "bad data message: tunnelId=%d is not "
			    "found.", hdr.tunnel_id);
			goto bad_packet;
		}
		if (mestype != L2TP_AVP_MESSAGE_TYPE_SCCRQ) {
			snprintf(errmsg, sizeof(errmsg),
			    "bad control message: tunnelId=%d is not "
			    "found.  mestype=%s", hdr.tunnel_id,
			    avp_mes_type_string(mestype));
			goto bad_packet;
		}

		if ((ctrl = l2tp_ctrl_create()) == NULL) {
			l2tp_ctrl_log(ctrl, LOG_ERR,
			    "l2tp_ctrl_create() failed: %m");
			goto fail;
		}

		if (l2tp_ctrl_init(ctrl, _this, peer, sock, nat_t_ctx) != 0) {
			l2tp_ctrl_log(ctrl, LOG_ERR,
			    "l2tp_ctrl_start() failed: %m");
			goto fail;
		}

		ctrl->listener_index = listener_index;
		l2tp_ctrl_reload(ctrl);
	} else {
		/*
		 * treat as an error if src address and port is not
		 * match. (because it is potentially DoS attach)
		 */
		int notmatch = 0;

		if (ctrl->peer.ss_family != peer->sa_family)
			notmatch = 1;
		else if (peer->sa_family == AF_INET) {
			if (SIN(peer)->sin_addr.s_addr != 
			    SIN(&ctrl->peer)->sin_addr.s_addr ||
			    SIN(peer)->sin_port != SIN(&ctrl->peer)->sin_port)
				notmatch = 1;
		} else if (peer->sa_family == AF_INET6) {
			if (!IN6_ARE_ADDR_EQUAL(&(SIN6(peer)->sin6_addr),
				    &(SIN6(&ctrl->peer)->sin6_addr)) ||
			    SIN6(peer)->sin6_port !=
				    SIN6(&ctrl->peer)->sin6_port)
				notmatch = 1;
 		}
		if (notmatch) {
			snprintf(errmsg, sizeof(errmsg),
			    "tunnelId=%u is already assigned for %s",
			    hdr.tunnel_id, addrport_tostring(
				(struct sockaddr *)&ctrl->peer,
				ctrl->peer.ss_len, hbuf, sizeof(hbuf)));
			goto bad_packet;
		}
	}
	ctrl->last_rcv = curr_time;
	call = NULL;
	if (hdr.session_id != 0) {
		/* search l2tp_call by Session ID */
		/* linear search is enough for this purpose */
		len = slist_length(&ctrl->call_list);
		for (i = 0; i < len; i++) {
			call = slist_get(&ctrl->call_list, i);
			if (call->session_id == hdr.session_id)
				break;
			call = NULL;
		}
	}
	if (!is_ctrl) {
		int delayed = 0;

		/* L2TP data */
		if (ctrl->state != L2TP_CTRL_STATE_ESTABLISHED) {
			l2tp_ctrl_log(ctrl, LOG_WARNING,
			    "Received Data packet in '%s'",
			    l2tp_ctrl_state_string(ctrl));
			goto fail;
		}
		if (call == NULL) {
			l2tp_ctrl_log(ctrl, LOG_WARNING,
			    "Received a data packet but it has no call.  "
			    "session_id=%u",  hdr.session_id);
			goto fail;
		}
		L2TP_CTRL_DBG((ctrl, DEBUG_LEVEL_2,
		    "call=%u RECV   ns=%u nr=%u snd_nxt=%u rcv_nxt=%u len=%d",
		    call->id, hdr.ns, hdr.nr, call->snd_nxt, call->rcv_nxt,
		    pktlen));
		if (call->state != L2TP_CALL_STATE_ESTABLISHED){
			l2tp_ctrl_log(ctrl, LOG_WARNING,
			    "Received a data packet but call is not "
			    "established");
			goto fail;
		}

		if (hdr.s != 0) {
			if (SEQ_LT(hdr.ns, call->rcv_nxt)) {
				if (SEQ_LT(hdr.ns,
				    call->rcv_nxt - L2TP_CALL_DELAY_LIMIT)) {
					/* sequence number seems to be delayed */
					/* XXX: need to log? */
					L2TP_CTRL_DBG((ctrl, LOG_DEBUG,
					    "receive a out of sequence "
					    "data packet: %u < %u.",
					    hdr.ns, call->rcv_nxt));
					return;
				}
				delayed = 1;
			} else {
				call->rcv_nxt = hdr.ns + 1;
			}
		}

		l2tp_call_ppp_input(call, pkt, pktlen, delayed);

		return;
	}
	if (hdr.s != 0) {
		L2TP_CTRL_DBG((ctrl, DEBUG_LEVEL_2,
		    "RECV %s ns=%u nr=%u snd_nxt=%u snd_una=%u rcv_nxt=%u "
		    "len=%d", (is_ctrl)? "C" : "", hdr.ns, hdr.nr,
		    ctrl->snd_nxt, ctrl->snd_una, ctrl->rcv_nxt, pktlen));

		if (pktlen <= 0)
			l2tp_ctrl_log(ctrl, LOG_INFO, "RecvZLB");

		if (SEQ_GT(hdr.nr, ctrl->snd_una)) {
			/* a new ack arrived */
			if (SEQ_GT(hdr.nr, ctrl->snd_nxt)) {
				/* ack is proceeded us */
				l2tp_ctrl_log(ctrl, LOG_INFO,
				    "Received message has bad Nr field: "
				    "%u < %u", ctrl->snd_nxt, hdr.nr);
				goto fail;
			}
			ctrl->snd_una = hdr.nr;
			/* peer window is moved.  send out pending packets */
			l2tp_ctrl_resend_una_packets(ctrl, false);
		}
		if (l2tp_ctrl_txwin_size(ctrl) <= 0) {
			/* no waiting ack */
			if (ctrl->hello_wait_ack != 0) {
				/*
				 * Reset Hello state, as an ack for the Hello
				 * is received.
				 */
				ctrl->hello_wait_ack = 0;
				ctrl->hello_io_time = curr_time;
			}
			switch (ctrl->state) {
			case L2TP_CTRL_STATE_CLEANUP_WAIT:
				l2tp_ctrl_stop(ctrl, 0);
				return;
			}
		}
		if (hdr.ns != ctrl->rcv_nxt) {
			/* there are remaining packet */
			if (l2tp_ctrl_resend_una_packets(ctrl, true) <= 0) {
				/* resend or sent ZLB */
				l2tp_ctrl_send_ZLB(ctrl);
			}
#ifdef	L2TP_CTRL_DEBUG
			if (pktlen != 0) {	/* not ZLB */
				L2TP_CTRL_DBG((ctrl, LOG_DEBUG,
				    "receive out of sequence %u must be %u.  "
				    "mestype=%s", hdr.ns, ctrl->rcv_nxt,
				    avp_mes_type_string(mestype)));
			}
#endif
			return;
		}
		if (pktlen <= 0)
			return;		/* ZLB */

		if (!l2tp_ctrl_in_our_window(ctrl, hdr.ns)) {
			l2tp_ctrl_log(ctrl, LOG_WARNING,
			    "received message is outside of window.  "
			    "ns=%d window=%u:%u",
			    hdr.ns, ctrl->snd_lastnr,
			    (uint16_t)(ctrl->snd_lastnr + ctrl->winsz - 1));
			return;
		}

		ctrl->rcv_nxt++;
		if (avp == NULL) {
			l2tpd_log(_this, LOG_WARNING,
			    "bad control message: no message-type AVP.");
			goto fail;
		}
	}

    /*
     * state machine (RFC2661 pp. 56-57)
     */
	switch (ctrl->state) {
	case L2TP_CTRL_STATE_IDLE:
		switch (mestype) {
		case L2TP_AVP_MESSAGE_TYPE_SCCRQ:
			if (l2tp_ctrl_recv_SCCRQ(ctrl, pkt, pktlen, _this,
			    peer) == 0) {
				/* acceptable */
				l2tp_ctrl_send_SCCRP(ctrl);
				ctrl->state = L2TP_CTRL_STATE_WAIT_CTL_CONN;
				return;
			}
			/*
			 * in case un-acceptable, it was already processed
			 * at l2tcp_ctrl_recv_SCCRQ
			 */
			return;
		case L2TP_AVP_MESSAGE_TYPE_SCCRP:
			/*
			 * RFC specifies that sent of StopCCN in the state,
			 * However as this implementation only support Passive
			 * open, this packet will not received.
			 */
			/* FALLTHROUGH */
		case L2TP_AVP_MESSAGE_TYPE_SCCCN:
		default:
			break;
		}
		goto fsm_fail;

	case L2TP_CTRL_STATE_WAIT_CTL_CONN:
	    /* Wait-Ctl-Conn */
		switch (mestype) {
		case L2TP_AVP_MESSAGE_TYPE_SCCCN:
			l2tp_ctrl_log(ctrl, LOG_INFO, "RecvSCCN");
			if (l2tp_ctrl_send_ZLB(ctrl) == 0) {
				ctrl->state = L2TP_CTRL_STATE_ESTABLISHED;
			}
			return;
		case L2TP_AVP_MESSAGE_TYPE_StopCCN:
			goto receive_stop_ccn;
		case L2TP_AVP_MESSAGE_TYPE_SCCRQ:
		case L2TP_AVP_MESSAGE_TYPE_SCCRP:
		default:
			break;
		}
		break;	/* fsm_fail */
	case L2TP_CTRL_STATE_ESTABLISHED:
	    /* Established */
		switch (mestype) {
		case L2TP_AVP_MESSAGE_TYPE_SCCCN:
		case L2TP_AVP_MESSAGE_TYPE_SCCRQ:
		case L2TP_AVP_MESSAGE_TYPE_SCCRP:
			break;
receive_stop_ccn:
		case L2TP_AVP_MESSAGE_TYPE_StopCCN:
			l2tp_ctrl_recv_StopCCN(ctrl, pkt, pktlen);
			l2tp_ctrl_send_ZLB(ctrl);
			l2tp_ctrl_stop(ctrl, 0);
			return;

		case L2TP_AVP_MESSAGE_TYPE_HELLO:
			l2tp_ctrl_send_ZLB(ctrl);
			return;
		case L2TP_AVP_MESSAGE_TYPE_CDN:
		case L2TP_AVP_MESSAGE_TYPE_ICRP:
		case L2TP_AVP_MESSAGE_TYPE_ICCN:
			if (call == NULL) {
				l2tp_ctrl_log(ctrl, LOG_INFO,
				    "Unknown call message: %s",
				    avp_mes_type_string(mestype));
				goto fail;
			}
			/* FALLTHROUGH */
		case L2TP_AVP_MESSAGE_TYPE_ICRQ:
			l2tp_call_recv_packet(ctrl, call, mestype, pkt,
			    pktlen);
			return;
		default:
			break;
		}
		break;	/* fsm_fail */
	case L2TP_CTRL_STATE_CLEANUP_WAIT:
		if (mestype == L2TP_AVP_MESSAGE_TYPE_StopCCN) {
			/*
			 * We left ESTABLISHED state, but the peer sent StopCCN.
			 */
			goto receive_stop_ccn;
		}
		break;	/* fsm_fail */
	}

fsm_fail:
	/* state machine error */
	l2tp_ctrl_log(ctrl, LOG_WARNING, "Received %s in '%s' state",
	    avp_mes_type_string(mestype), l2tp_ctrl_state_string(ctrl));
	l2tp_ctrl_stop(ctrl, L2TP_STOP_CCN_RCODE_FSM_ERROR);

	return;
fail:
	if (ctrl != NULL && mestype != 0) {
		l2tp_ctrl_log(ctrl, LOG_WARNING, "Received %s in '%s' state",
		    avp_mes_type_string(mestype), l2tp_ctrl_state_string(ctrl));
		l2tp_ctrl_stop(ctrl, L2TP_STOP_CCN_RCODE_GENERAL_ERROR);
	}
	return;

bad_packet:
	l2tpd_log(_this, LOG_INFO, "Received from=%s: %s",
	    addrport_tostring(peer, peer->sa_len, hbuf, sizeof(hbuf)), errmsg);

	return;
}

static int
l2tp_ctrl_txwin_size(l2tp_ctrl *_this)
{
	uint16_t sz;

	sz = _this->snd_nxt - _this->snd_una;

	L2TP_CTRL_ASSERT(sz <= _this->buffercnt);

	return sz;
}

static bool
l2tp_ctrl_txwin_is_full(l2tp_ctrl *_this)
{
	return (l2tp_ctrl_txwin_size(_this) >= _this->snd_buffercnt)? 1 : 0;
}

static bool
l2tp_ctrl_in_peer_window(l2tp_ctrl *_this, uint16_t seq)
{
	uint16_t off;
	int winsz;

	winsz = MINIMUM(_this->winsz, _this->peer_winsz);
	off = seq - _this->snd_una;

	return ((off < winsz)? true : false);
}

static bool
l2tp_ctrl_in_our_window(l2tp_ctrl *_this, uint16_t seq)
{
	uint16_t off;

	off = seq - _this->snd_lastnr;

	return ((off < _this->winsz)? true : false);
}
/* send control packet */
int
l2tp_ctrl_send_packet(l2tp_ctrl *_this, int call_id, bytebuffer *bytebuf)
{
	struct l2tp_header *hdr;
	int rval;
	time_t curr_time;
	uint16_t seq;

	curr_time = get_monosec();

	bytebuffer_flip(bytebuf);
	hdr = (struct l2tp_header *)bytebuffer_pointer(bytebuf);
	memset(hdr, 0, sizeof(*hdr));

	hdr->t = 1;
	hdr->ver = L2TP_HEADER_VERSION_RFC2661;
	hdr->l = 1;
	hdr->length = htons(bytebuffer_remaining(bytebuf));
	hdr->tunnel_id = htons(_this->peer_tunnel_id);
	hdr->session_id = htons(call_id);

	hdr->s = 1;
	seq = _this->snd_nxt;
	hdr->ns = htons(seq);
	hdr->nr = htons(_this->rcv_nxt);

	if (bytebuffer_remaining(bytebuf) > sizeof(struct l2tp_header))
		/* Not ZLB */
		_this->snd_nxt++;

	if (!l2tp_ctrl_in_peer_window(_this, seq))
		return (0);

	L2TP_CTRL_DBG((_this, DEBUG_LEVEL_2,
	    "SEND C ns=%u nr=%u snd_nxt=%u snd_una=%u rcv_nxt=%u ",
	    ntohs(hdr->ns), htons(hdr->nr),
	    _this->snd_nxt, _this->snd_una, _this->rcv_nxt));

	if (L2TP_CTRL_CONF(_this)->ctrl_out_pktdump  != 0) {
		l2tpd_log(_this->l2tpd, LOG_DEBUG,
		    "L2TP Control output packet dump");
		show_hd(debug_get_debugfp(), bytebuffer_pointer(bytebuf),
		    bytebuffer_remaining(bytebuf));
	}

	if ((rval = l2tp_ctrl_send(_this, bytebuffer_pointer(bytebuf),
	    bytebuffer_remaining(bytebuf))) < 0) {
		L2TP_CTRL_DBG((_this, LOG_DEBUG, "sendto() failed: %m"));
	}

	_this->snd_lastnr = _this->rcv_nxt;
	_this->last_snd_ctrl = curr_time;
	_this->snd_last = seq;

	return (rval == bytebuffer_remaining(bytebuf))? 0 : 1;
}

/*
 * receiver SCCRQ
 */
static int
l2tp_ctrl_recv_SCCRQ(l2tp_ctrl *_this, u_char *pkt, int pktlen, l2tpd *_l2tpd,
    struct sockaddr *peer)
{
	int avpsz, len, protover, protorev, firmrev, result;
	struct l2tp_avp *avp;
	char host[NI_MAXHOST], serv[NI_MAXSERV];
	char buf[L2TP_AVP_MAXSIZ], emes[256], hostname[256], vendorname[256];

	result = L2TP_STOP_CCN_RCODE_GENERAL_ERROR;
	strlcpy(hostname, "(no hostname)", sizeof(hostname));
	strlcpy(vendorname, "(no vendorname)", sizeof(vendorname));

	_this->peer_winsz = 4;	/* default is 4 in RFC 2661 */
	firmrev = 0;
	protover = 0;
	protorev = 0;
	avp = (struct l2tp_avp *)buf;
	while (pktlen >= 6 && (avpsz = avp_enum(avp, pkt, pktlen, 1)) > 0) {
		pkt += avpsz;
		pktlen -= avpsz;
		if (avp->vendor_id != 0) {
			L2TP_CTRL_DBG((_this, LOG_DEBUG,
			    "Received a Vendor-specific AVP vendor-id=%d "
			    "type=%d", avp->vendor_id, avp->attr_type));
			continue;
		}
		switch (avp->attr_type) {
		case L2TP_AVP_TYPE_MESSAGE_TYPE:
			AVP_SIZE_CHECK(avp, ==, 8);
			continue;
		case L2TP_AVP_TYPE_PROTOCOL_VERSION:
			AVP_SIZE_CHECK(avp, ==, 8);
			protover = avp->attr_value[0];
			protorev = avp->attr_value[1];

			if (protover != L2TP_RFC2661_VERSION ||
			    protorev != L2TP_RFC2661_REVISION) {
				result = L2TP_STOP_CCN_RCODE_GENERAL_ERROR;
				snprintf(emes, sizeof(emes),
				    "Peer's protocol version is not supported:"
				    " %d.%d", protover, protorev);
				goto not_acceptable;
			}
			continue;
		case L2TP_AVP_TYPE_FRAMING_CAPABILITIES:
			AVP_SIZE_CHECK(avp, ==, 10);
			if ((avp_get_val32(avp) & L2TP_FRAMING_CAP_FLAGS_SYNC)
			    == 0) {
				L2TP_CTRL_DBG((_this, LOG_DEBUG, "Peer doesn't "
				    "support synchronous framing"));
			}
			continue;
		case L2TP_AVP_TYPE_BEARER_CAPABILITIES:
			AVP_SIZE_CHECK(avp, ==, 10);
			continue;
		case L2TP_AVP_TYPE_TIE_BREAKER:
			AVP_SIZE_CHECK(avp, ==, 14);
			/*
			 * As the implementation never send SCCRQ,
			 * the peer is always winner
			 */
			continue;
		case L2TP_AVP_TYPE_FIRMWARE_REVISION:
			AVP_SIZE_CHECK(avp, >=, 6);
			firmrev = avp_get_val16(avp);
			continue;
		case L2TP_AVP_TYPE_HOST_NAME:
			AVP_SIZE_CHECK(avp, >, 4);
			len = MINIMUM(sizeof(hostname) - 1, avp->length - 6);
			memcpy(hostname, avp->attr_value, len);
			hostname[len] = '\0';
			continue;
		case L2TP_AVP_TYPE_VENDOR_NAME:
			AVP_SIZE_CHECK(avp, >, 4);
			len = MINIMUM(sizeof(vendorname) - 1, avp->length - 6);
			memcpy(vendorname, avp->attr_value, len);
			vendorname[len] = '\0';
			continue;
		case L2TP_AVP_TYPE_ASSINGED_TUNNEL_ID:
			AVP_SIZE_CHECK(avp, ==, 8);
			_this->peer_tunnel_id = avp_get_val16(avp);
			continue;
		case L2TP_AVP_TYPE_RECV_WINDOW_SIZE:
			AVP_SIZE_CHECK(avp, ==, 8);
			_this->peer_winsz = avp_get_val16(avp);
			continue;
		}
		if (avp->is_mandatory) {
			l2tp_ctrl_log(_this, LOG_WARNING,
			    "Received AVP (%s/%d) is not supported, but it's "
			    "mandatory", avp_attr_type_string(avp->attr_type),
			    avp->attr_type);
#ifdef L2TP_CTRL_DEBUG
		} else {
			L2TP_CTRL_DBG((_this, LOG_DEBUG,
			    "AVP (%s/%d) is not handled",
			    avp_attr_type_string(avp->attr_type),
			    avp->attr_type));
#endif
		}
	}
	if (getnameinfo((struct sockaddr *)&_this->peer, _this->peer.ss_len,
	    host, sizeof(host), serv, sizeof(serv),
	    NI_NUMERICHOST | NI_NUMERICSERV | NI_DGRAM) != 0) {
		l2tp_ctrl_log(_this, LOG_ERR,
		    "getnameinfo() failed at %s(): %m", __func__);
		strlcpy(host, "error", sizeof(host));
		strlcpy(serv, "error", sizeof(serv));
	}
	l2tp_ctrl_log(_this, LOG_NOTICE, "logtype=Started RecvSCCRQ "
	    "from=%s:%s/udp tunnel_id=%u/%u protocol=%d.%d winsize=%d "
	    "hostname=%s vendor=%s firm=%04X", host, serv, _this->tunnel_id,
	    _this->peer_tunnel_id, protover, protorev, _this->peer_winsz,
	    hostname, vendorname, firmrev);

	if (_this->peer_winsz == 0)
		_this->peer_winsz = 1;

	return 0;
not_acceptable:
size_check_failed:
	l2tp_ctrl_log(_this, LOG_ERR, "Received bad SCCRQ: %s", emes);
	l2tp_ctrl_stop(_this, result);

	return 1;
}

/*
 * send StopCCN
 */
static int
l2tp_ctrl_send_StopCCN(l2tp_ctrl *_this, int result)
{
	struct l2tp_avp *avp;
	char buf[L2TP_AVP_MAXSIZ];
	bytebuffer *bytebuf;

	if ((bytebuf = l2tp_ctrl_prepare_snd_buffer(_this, 1)) == NULL) {
		l2tp_ctrl_log(_this, LOG_ERR,
		    "sending StopCCN failed: no buffer.");
		return -1;
	}
	avp = (struct l2tp_avp *)buf;

	/* Message Type = StopCCN */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_MESSAGE_TYPE;
	avp_set_val16(avp, L2TP_AVP_MESSAGE_TYPE_StopCCN);
	bytebuf_add_avp(bytebuf, avp, 2);

	/* Assigned Tunnel Id */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_ASSINGED_TUNNEL_ID;
	avp_set_val16(avp, _this->tunnel_id);
	bytebuf_add_avp(bytebuf, avp, 2);

	/* Result Code */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_RESULT_CODE;
	avp_set_val16(avp, result);
	bytebuf_add_avp(bytebuf, avp, 2);

	if (l2tp_ctrl_send_packet(_this, 0, bytebuf) != 0) {
		l2tp_ctrl_log(_this, LOG_ERR, "sending StopCCN failed");
		return - 1;
	}
	l2tp_ctrl_log(_this, LOG_INFO, "SendStopCCN result=%d", result);

	return 0;
}

/*
 * Receiver StopCCN
 */
static int
l2tp_ctrl_recv_StopCCN(l2tp_ctrl *_this, u_char *pkt, int pktlen)
{
	int result, error, avpsz, len;
	uint16_t tunid;
	struct l2tp_avp *avp;
	char buf[L2TP_AVP_MAXSIZ + 16], emes[256], pmes[256];

	result = 0;
	error = 0;
	tunid = 0;
	pmes[0] = '\0';
	avp = (struct l2tp_avp *)buf;
	while (pktlen >= 6 && (avpsz = avp_enum(avp, pkt, pktlen, 1)) > 0) {
		pkt += avpsz;
		pktlen -= avpsz;
		if (avp->vendor_id != 0) {
			L2TP_CTRL_DBG((_this, LOG_DEBUG,
			    "Received a Vendor-specific AVP vendor-id=%d "
			    "type=%d", avp->vendor_id, avp->attr_type));
			continue;
		}
		if (avp->is_hidden != 0) {
			l2tp_ctrl_log(_this, LOG_WARNING,
			    "Received AVP (%s/%d) is hidden.  But we don't "
			    "share secret.",
			    avp_attr_type_string(avp->attr_type),
			    avp->attr_type);
			if (avp->is_mandatory != 0) {
				l2tp_ctrl_stop(_this,
				    L2TP_STOP_CCN_RCODE_GENERAL_ERROR |
				    L2TP_ECODE_UNKNOWN_MANDATORY_AVP);
				return 1;
			}
			continue;
		}
		switch (avp->attr_type) {
		case L2TP_AVP_TYPE_MESSAGE_TYPE:
			AVP_SIZE_CHECK(avp, ==, 8);
			continue;
		case L2TP_AVP_TYPE_RESULT_CODE:
			AVP_SIZE_CHECK(avp, >=, 8);
			result = avp->attr_value[0] << 8 | avp->attr_value[1];
			if (avp->length >= 10) {
				error = avp->attr_value[2] << 8 |
				    avp->attr_value[3];
				len = avp->length - 12;
				if (len > 0) {
					len = MINIMUM(len, sizeof(pmes) - 1);
					memcpy(pmes, &avp->attr_value[4], len);
					pmes[len] = '\0';
				}
			}
			continue;
		case L2TP_AVP_TYPE_ASSINGED_TUNNEL_ID:
			AVP_SIZE_CHECK(avp, ==, 8);
			tunid = avp_get_val16(avp);
			continue;
		default:
			if (avp->is_mandatory != 0) {
				l2tp_ctrl_log(_this, LOG_WARNING,
				    "Received AVP (%s/%d) is not supported, "
				    "but it's mandatory",
				    avp_attr_type_string(avp->attr_type),
				    avp->attr_type);
#ifdef L2TP_CTRL_DEBUG
			} else {
				L2TP_CTRL_DBG((_this, LOG_DEBUG,
				    "AVP (%s/%d) is not handled",
				    avp_attr_type_string(avp->attr_type),
				    avp->attr_type));
#endif
			}
		}
	}

	if (result == L2TP_CDN_RCODE_ERROR_CODE &&
	    error == L2TP_ECODE_NO_RESOURCE) {
		/*
		 * Memo:
		 * This state may be happen in following state.
		 * - lots of connect/disconnect by long-running
		 *   windows2000, sometimes it fall to this state.
		 *   Once it fall to here, connection will fail till
		 *   the windows rebooted
		 */
		l2tp_ctrl_log(_this, LOG_WARNING,
		    "Peer indicates \"No Resource\" error.");
	}

	l2tp_ctrl_log(_this, LOG_INFO, "RecvStopCCN result=%s/%u "
	    "error=%s/%u tunnel_id=%u message=\"%s\"",
	    l2tp_stopccn_rcode_string(result), result,
	    l2tp_ecode_string(error), error, tunid, pmes);

	return 0;

size_check_failed:
	l2tp_ctrl_log(_this, LOG_ERR, "Received bad StopCCN: %s", emes);

	return -1;
}

/*
 * send SCCRP
 */
static void
l2tp_ctrl_send_SCCRP(l2tp_ctrl *_this)
{
	int len;
	struct l2tp_avp *avp;
	char buf[L2TP_AVP_MAXSIZ], hbuf[HOST_NAME_MAX+1];
	const char *val;
	bytebuffer *bytebuf;

	if ((bytebuf = l2tp_ctrl_prepare_snd_buffer(_this, 1)) == NULL) {
		l2tp_ctrl_log(_this, LOG_ERR,
		    "sending SCCRP failed: no buffer.");
		return;
	}
	avp = (struct l2tp_avp *)buf;

	/* Message Type = SCCRP */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_MESSAGE_TYPE;
	avp_set_val16(avp, L2TP_AVP_MESSAGE_TYPE_SCCRP);
	bytebuf_add_avp(bytebuf, avp, 2);

	/* Protocol Version = 1.0 */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_PROTOCOL_VERSION;
	avp->attr_value[0] = L2TP_RFC2661_VERSION;
	avp->attr_value[1] = L2TP_RFC2661_REVISION;
	bytebuf_add_avp(bytebuf, avp, 2);

	/* Framing Capability = Async */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_FRAMING_CAPABILITIES;
	avp_set_val32(avp, L2TP_FRAMING_CAP_FLAGS_SYNC);
	bytebuf_add_avp(bytebuf, avp, 4);

	/* Host Name */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_HOST_NAME;
	if ((val = L2TP_CTRL_CONF(_this)->hostname) == NULL) {
		gethostname(hbuf, sizeof(hbuf));
		val = hbuf;
	}
	len = strlen(val);
	memcpy(avp->attr_value, val, len);
	bytebuf_add_avp(bytebuf, avp, len);

	/* Assigned Tunnel Id */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_ASSINGED_TUNNEL_ID;
	avp_set_val16(avp, _this->tunnel_id);
	bytebuf_add_avp(bytebuf, avp, 2);

	/* Bearer Capability
	 * This implementation never act as LAC.
	 *
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_BEARER_CAPABILITIES;
	avp_set_val32(avp, 0);
	bytebuf_add_avp(bytebuf, avp, 4);
	 */

	/* Firmware Revision */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 0;
	avp->attr_type = L2TP_AVP_TYPE_FIRMWARE_REVISION;
	avp->attr_value[0] = MAJOR_VERSION;
	avp->attr_value[1] = MINOR_VERSION;
	bytebuf_add_avp(bytebuf, avp, 2);

	/* Vendor Name */
	if ((val = L2TP_CTRL_CONF(_this)->vendor_name) != NULL) {
		memset(avp, 0, sizeof(*avp));
		avp->is_mandatory = 0;
		avp->attr_type = L2TP_AVP_TYPE_VENDOR_NAME;

		len = strlen(val);
		memcpy(avp->attr_value, val, len);
		bytebuf_add_avp(bytebuf, avp, len);
	}

	/* Window Size */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_RECV_WINDOW_SIZE;
	avp_set_val16(avp, _this->winsz);
	bytebuf_add_avp(bytebuf, avp, 2);

	if ((l2tp_ctrl_send_packet(_this, 0, bytebuf)) != 0) {
		l2tp_ctrl_log(_this, LOG_ERR, "sending SCCRP failed");
		l2tp_ctrl_stop(_this, L2TP_STOP_CCN_RCODE_GENERAL);
		return;
	}
	l2tp_ctrl_log(_this, LOG_INFO, "SendSCCRP");
}

static int
l2tp_ctrl_send_HELLO(l2tp_ctrl *_this)
{
	struct l2tp_avp *avp;
	char buf[L2TP_AVP_MAXSIZ];
	bytebuffer *bytebuf;

	if ((bytebuf = l2tp_ctrl_prepare_snd_buffer(_this, 1)) == NULL) {
		l2tp_ctrl_log(_this, LOG_ERR,
		    "sending HELLO failed: no buffer.");
		return 1;
	}
	avp = (struct l2tp_avp *)buf;

	/* Message Type = HELLO */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_MESSAGE_TYPE;
	avp_set_val16(avp, L2TP_AVP_MESSAGE_TYPE_HELLO);
	bytebuf_add_avp(bytebuf, avp, 2);

	if ((l2tp_ctrl_send_packet(_this, 0, bytebuf)) != 0) {
		l2tp_ctrl_log(_this, LOG_ERR, "sending HELLO failed");
		l2tp_ctrl_stop(_this, L2TP_STOP_CCN_RCODE_GENERAL);
		return 1;
	}
	l2tp_ctrl_log(_this, LOG_DEBUG, "SendHELLO");

	return 0;
}

/* Send  ZLB */
static int
l2tp_ctrl_send_ZLB(l2tp_ctrl *_this)
{
	int loglevel;

	loglevel = (_this->state == L2TP_CTRL_STATE_ESTABLISHED)
	    ? LOG_DEBUG : LOG_INFO;
	l2tp_ctrl_log(_this, loglevel, "SendZLB");
	bytebuffer_clear(_this->zlb_buffer);
	bytebuffer_put(_this->zlb_buffer, BYTEBUFFER_PUT_DIRECT,
	    sizeof(struct l2tp_header));

	return l2tp_ctrl_send_packet(_this, 0, _this->zlb_buffer);
}

/*
 * Utility
 */

/**
 * Prepare send buffer
 * @return return Null when the send buffer exceed Window.
 */
bytebuffer *
l2tp_ctrl_prepare_snd_buffer(l2tp_ctrl *_this, int with_seq)
{
	bytebuffer *bytebuf;

	L2TP_CTRL_ASSERT(_this != NULL);

	if (l2tp_ctrl_txwin_is_full(_this)) {
		l2tp_ctrl_log(_this, LOG_INFO, "sending buffer is full.");
		return NULL;
	}
	bytebuf = _this->snd_buffers[_this->snd_nxt % _this->snd_buffercnt];
	bytebuffer_clear(bytebuf);
	if (with_seq)
		bytebuffer_put(bytebuf, BYTEBUFFER_PUT_DIRECT,
		    sizeof(struct l2tp_header));
	else
		bytebuffer_put(bytebuf, BYTEBUFFER_PUT_DIRECT,
		    offsetof(struct l2tp_header, ns));

	return bytebuf;
}

/**
 * return current state as strings
 */
static inline const char *
l2tp_ctrl_state_string(l2tp_ctrl *_this)
{
	switch (_this->state) {
	case L2TP_CTRL_STATE_IDLE:		return "idle";
	case L2TP_CTRL_STATE_WAIT_CTL_CONN:	return "wait-ctl-conn";
	case L2TP_CTRL_STATE_WAIT_CTL_REPLY:	return "wait-ctl-reply";
	case L2TP_CTRL_STATE_ESTABLISHED:	return "established";
	case L2TP_CTRL_STATE_CLEANUP_WAIT:	return "cleanup-wait";
	}
	return "unknown";
}

/* logging with the label of the l2tp instance. */
void
l2tp_ctrl_log(l2tp_ctrl *_this, int prio, const char *fmt, ...)
{
	char logbuf[BUFSIZ];
	va_list ap;

	va_start(ap, fmt);
#ifdef	L2TPD_MULTIPLE
	snprintf(logbuf, sizeof(logbuf), "l2tpd id=%u ctrl=%u %s",
	    _this->l2tpd->id, _this->id, fmt);
#else
	snprintf(logbuf, sizeof(logbuf), "l2tpd ctrl=%u %s", _this->id, fmt);
#endif
	vlog_printf(prio, logbuf, ap);
	va_end(ap);
}
