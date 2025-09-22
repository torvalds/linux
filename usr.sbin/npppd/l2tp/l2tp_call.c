/*	$OpenBSD: l2tp_call.c,v 1.20 2021/03/29 03:54:39 yasuoka Exp $	*/

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
/* $Id: l2tp_call.c,v 1.20 2021/03/29 03:54:39 yasuoka Exp $ */
/**@file L2TP LNS call */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stddef.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <stdarg.h>
#include <unistd.h>
#include <event.h>
#include <net/if_dl.h>

#include "debugutil.h"
#include "bytebuf.h"
#include "hash.h"
#include "slist.h"
#include "l2tp.h"
#include "l2tp_subr.h"

#include "npppd.h"
#include "l2tp_local.h"

#ifdef	L2TP_CALL_DEBUG
#define	L2TP_CALL_DBG(m)	l2tp_call_log m
#define	L2TP_CALL_ASSERT(x)	ASSERT(x)
#else
#define	L2TP_CALL_DBG(m)
#define	L2TP_CALL_ASSERT(x)
#endif

static void  l2tp_call_log (l2tp_call *, int, const char *, ...) __printflike(3,4);
static void               l2tp_call_disconnect (l2tp_call *, int, int, const char *, struct l2tp_avp *[], int);
static int                l2tp_call_recv_ICRQ (l2tp_call *, u_char *, int);
static int                l2tp_call_send_ICRP (l2tp_call *);
static int                l2tp_call_recv_ICCN (l2tp_call *, u_char *, int, dialin_proxy_info *);
static int                l2tp_recv_CDN (l2tp_call *, u_char *, int);
static int                l2tp_call_send_CDN (l2tp_call *, int, int, const char *, struct l2tp_avp *[], int);
static int                l2tp_call_send_ZLB (l2tp_call *);
static inline const char  *l2tp_call_state_string (l2tp_call *);
static int                l2tp_call_bind_ppp (l2tp_call *, dialin_proxy_info *);
static void               l2tp_call_notify_down (l2tp_call *);
static int                l2tp_call_send_data_packet (l2tp_call *, bytebuffer *);

static int   l2tp_call_ppp_output (npppd_ppp *, unsigned char *, int, int);
static void  l2tp_call_closed_by_ppp (npppd_ppp *);

/* create {@link ::_l2tp_call L2TP call} instance */
l2tp_call *
l2tp_call_create(void)
{
	l2tp_call *_this;

	if ((_this = malloc(sizeof(l2tp_call))) == NULL)
		return NULL;

	return _this;
}

/* initialize {@link ::_l2tp_call L2TP call} instance */
int
l2tp_call_init(l2tp_call *_this, l2tp_ctrl *ctrl)
{
	memset(_this, 0, sizeof(l2tp_call));

	_this->ctrl = ctrl;
	if (l2tpd_assign_call(ctrl->l2tpd, _this) != 0)
		return -1;

	_this->use_seq = ctrl->data_use_seq;

	return 0;
}

/* free {@link ::_l2tp_call L2TP call} instance */
void
l2tp_call_destroy(l2tp_call *_this, int from_l2tp_ctrl)
{
	l2tpd_release_call(_this->ctrl->l2tpd, _this);
	free(_this);
}

/*
 * l2tp disconnect will occur when
 *      1) disconnect request issued from nppdctl command
 *      2) npppd is terminated
 * in case 1) ppp_stop() is used to terminal. (PPP LCP TermReq)
 * and in case 2) l2tp_call_disconnect() is used (L2TP CDN)
 */
/* administrative reason disconnection */
void
l2tp_call_admin_disconnect(l2tp_call *_this)
{
	l2tp_call_disconnect(_this, L2TP_CDN_RCODE_ADMINISTRATIVE_REASON, 0,
	    NULL, NULL, 0);
}

void
l2tp_call_drop(l2tp_call *_this)
{
	l2tp_call_disconnect(_this, 0, 0, NULL, NULL, 0);
}

/*
 * disconnect l2tp connection
 * @param result_code	disconnect without CDN, specify zero
 */
static void
l2tp_call_disconnect(l2tp_call *_this, int result_code, int error_code,
    const char *errmes, struct l2tp_avp *addavp[], int naddavp)
{
	L2TP_CALL_ASSERT(_this != NULL);

	if (_this->state == L2TP_CALL_STATE_CLEANUP_WAIT) {
		/* CDN received, or have been sent */
		l2tp_call_notify_down(_this);	/* just in case */
		return;
	}
	if (result_code > 0) {
		if (l2tp_call_send_CDN(_this, result_code, error_code, errmes,
		    addavp, naddavp)
		    != 0)
			l2tp_call_log(_this, LOG_ERR, "Error sending CDN: %m");
	}
	_this->state = L2TP_CALL_STATE_CLEANUP_WAIT;
	l2tp_call_notify_down(_this);
}

/*
 * control packet
 */

/* call it when control packet is received */
int
l2tp_call_recv_packet(l2tp_ctrl *ctrl, l2tp_call *_this, int mestype,
    u_char *pkt, int pktlen)
{
	int i, len, session_id, send_cdn;
	l2tp_call *call;
	dialin_proxy_info dpi;

	/* when ICRQ, this will be NULL */
	L2TP_CALL_ASSERT(_this != NULL ||
	    mestype == L2TP_AVP_MESSAGE_TYPE_ICRQ);

	if (_this == NULL) {
		if (mestype != L2TP_AVP_MESSAGE_TYPE_ICRQ)
			return 1;
		if ((_this = l2tp_call_create()) == NULL) {
			l2tp_ctrl_log(ctrl, LOG_ERR,
			    "l2tp_call_create failed in %s(): %m", __func__);
			return 1;
		}
		l2tp_call_init(_this, ctrl);

		if (l2tp_call_recv_ICRQ(_this, pkt, pktlen) != 0)
			return 1;

		len = slist_length(&ctrl->call_list);
		session_id = _this->id;
	    again:
		/* assign a session ID */
		session_id &= 0xffff;
		if (session_id == 0)
			session_id = 1;
		for (i = 0; i < len; i++) {
			call = slist_get(&ctrl->call_list, i);
			if (call->session_id == session_id) {
				session_id++;
				goto again;
			}
		}
		_this->session_id = session_id;

		/* add the l2tp_call to call list */
		slist_add(&_this->ctrl->call_list, _this);

		if (l2tp_call_send_ICRP(_this) != 0)
			return 1;
		_this->state = L2TP_CALL_STATE_WAIT_CONN;
		return 0;
	}

	/* state machine */
	send_cdn = 0;
	switch (_this->state) {
	default:
		break;
	case L2TP_CALL_STATE_WAIT_CONN:
		switch (mestype) {
		case L2TP_AVP_MESSAGE_TYPE_ICCN:
			memset(&dpi, 0, sizeof(dpi));
			if (l2tp_call_recv_ICCN(_this, pkt, pktlen, &dpi) != 0)
				return 1;
			l2tp_call_bind_ppp(_this, &dpi);
			l2tp_call_send_ZLB(_this);
			_this->state = L2TP_CALL_STATE_ESTABLISHED;
			_this->ctrl->ncalls++;
			return 0;
		case L2TP_AVP_MESSAGE_TYPE_ICRQ:
		case L2TP_AVP_MESSAGE_TYPE_ICRP:
			send_cdn = 1;
			/* FALLTHROUGH */
		default:
			l2tp_call_log(_this, LOG_ERR,
			    "Waiting ICCN.  But received %s",
			    avp_mes_type_string(mestype));
			if (send_cdn) {
				l2tp_call_disconnect(_this,
				    L2TP_CDN_RCODE_ERROR_CODE,
				    L2TP_ECODE_GENERIC_ERROR, "Illegal state.",
				    NULL, 0);
				return 0;
			}
		}
		break;
	case L2TP_CALL_STATE_ESTABLISHED:
		switch (mestype) {
		case L2TP_AVP_MESSAGE_TYPE_CDN:
			/* disconnect from peer. log it */
			l2tp_recv_CDN(_this, pkt, pktlen);
			_this->state = L2TP_CALL_STATE_CLEANUP_WAIT;
			l2tp_call_notify_down(_this);
			l2tp_call_send_ZLB(_this);
			return 0;
		case L2TP_AVP_MESSAGE_TYPE_ICRQ:
		case L2TP_AVP_MESSAGE_TYPE_ICRP:
		case L2TP_AVP_MESSAGE_TYPE_ICCN:
			send_cdn = 1;
			break;
		default:
			break;
		}
		l2tp_call_log(_this, LOG_ERR,
		    "Call established.  But received %s",
		    avp_mes_type_string(mestype));
		if (send_cdn) {
			l2tp_call_disconnect(_this,
			    L2TP_CDN_RCODE_ERROR_CODE,
			    L2TP_ECODE_GENERIC_ERROR, "Illegal state.",
			    NULL, 0);
			return 0;
		}
		l2tp_call_disconnect(_this, 0, 0, NULL, NULL, 0);
		return 1;
	}
	l2tp_call_log(_this, LOG_INFO, "Received %s in unexpected state=%s",
	    avp_mes_type_string(mestype), l2tp_call_state_string(_this));
	l2tp_call_disconnect(_this, 0, 0, NULL, NULL, 0);
	return 1;
}
/*
 * receive ICRQ
 * @return	return 0 if the ICRQ is acceptable.
 *		other values means fail to receive, and
 *		CDN was sent and status was updated.
 */
static int
l2tp_call_recv_ICRQ(l2tp_call *_this, u_char *pkt, int pktlen)
{
	int avpsz, slen;
	struct l2tp_avp *avp;
	char buf[L2TP_AVP_MAXSIZ], emes[256];

	avp = (struct l2tp_avp *)buf;
	while (pktlen >= 6 && (avpsz = avp_enum(avp, pkt, pktlen, 1)) > 0) {
		pkt += avpsz;
		pktlen -= avpsz;
		if (avp->vendor_id != 0) {
			L2TP_CALL_DBG((_this, LOG_DEBUG,
			    "Received a Vendor-specific AVP vendor-id=%d "
			    "type=%d", avp->vendor_id, avp->attr_type));
			continue;
		}
		if (avp->is_hidden != 0) {
			l2tp_call_log(_this, LOG_WARNING,
			    "Received AVP (%s/%d) is hidden.  But we don't "
			    "share secret.",
			    avp_attr_type_string(avp->attr_type),
			    avp->attr_type);
			if (avp->is_mandatory != 0) {
				l2tp_call_disconnect(_this,
				    L2TP_CDN_RCODE_ERROR_CODE,
				    L2TP_ECODE_UNKNOWN_MANDATORY_AVP, NULL,
				    NULL, 0);
				return 1;
			}
			continue;
		}
		switch (avp->attr_type) {
		case L2TP_AVP_TYPE_MESSAGE_TYPE:
			AVP_SIZE_CHECK(avp, ==, 8);
			continue;
		case L2TP_AVP_TYPE_ASSIGNED_SESSION_ID:
			AVP_SIZE_CHECK(avp, ==, 8);
			_this->peer_session_id = avp_get_val16(avp);
			continue;
		case L2TP_AVP_TYPE_CALL_SERIAL_NUMBER:
		case L2TP_AVP_TYPE_BEARER_TYPE:
		case L2TP_AVP_TYPE_PHYSICAL_CHANNEL_ID:
			/*
			 * Memo:
			 * Microsoft "L2TP/IPsec VPN Client" for
			 * Windows 98/Me/NT asserts mandatory bit in
			 * Physical Channel Id
			 */
			break;
		case L2TP_AVP_TYPE_CALLING_NUMBER:
			slen = MINIMUM(sizeof(_this->calling_number) - 1,
			    avp_attr_length(avp));
			memcpy(_this->calling_number, avp->attr_value, slen);
			_this->calling_number[slen] = '\0';
			break;
		case L2TP_AVP_TYPE_CALLED_NUMBER:
		case L2TP_AVP_TYPE_SUB_ADDRESS:
			continue;
		default:
			if (avp->is_mandatory) {
				l2tp_call_log(_this, LOG_WARNING,
				    "AVP (%s/%d) is not supported, but it's "
				    "mandatory",
				    avp_attr_type_string(avp->attr_type),
				    avp->attr_type);
				if (avp->is_mandatory != 0) {
					l2tp_call_disconnect(_this,
					    L2TP_CDN_RCODE_ERROR_CODE,
					    L2TP_ECODE_UNKNOWN_MANDATORY_AVP,
					    NULL, NULL, 0);
					return 1;
				}
#ifdef L2TP_CALL_DEBUG
			} else {
				L2TP_CALL_DBG((_this, LOG_DEBUG,
				    "AVP (%s/%d) is not handled",
				    avp_attr_type_string(avp->attr_type),
				    avp->attr_type));
#endif
			}
		}
	}
	if (_this->peer_session_id == 0) {
		l2tp_call_log(_this, LOG_ERR,
		    "Received a bad ICRP: SessionId = 0");
		l2tp_call_disconnect(_this, L2TP_CDN_RCODE_ERROR_CODE,
		    L2TP_ECODE_INVALID_MESSAGE, "Session Id must not be 0",
		    NULL, 0);
		return 1;
	}
	l2tp_call_log(_this, LOG_INFO, "RecvICRQ session_id=%u",
	    _this->peer_session_id);

	return 0;
size_check_failed:
	l2tp_call_log(_this, LOG_ERR, "Received bad ICRQ: %s", emes);
	l2tp_call_disconnect(_this, L2TP_CDN_RCODE_ERROR_CODE,
	    L2TP_ECODE_WRONG_LENGTH, NULL, NULL, 0);

	return 1;
}

/* send ICRP */
static int
l2tp_call_send_ICRP(l2tp_call *_this)
{
	int rval;
	struct l2tp_avp *avp;
	char buf[L2TP_AVP_MAXSIZ];
	bytebuffer *bytebuf;

	bytebuf = l2tp_ctrl_prepare_snd_buffer(_this->ctrl, 1);
	if (bytebuf == NULL) {
		l2tp_call_log(_this, LOG_ERR, "sending ICRP failed: no buffer");
		return 1;
	}
	avp = (struct l2tp_avp *)buf;

	/* Message Type = ICRP */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_MESSAGE_TYPE;
	avp_set_val16(avp, L2TP_AVP_MESSAGE_TYPE_ICRP);
	bytebuf_add_avp(bytebuf, avp, 2);

	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_ASSIGNED_SESSION_ID;
	avp_set_val16(avp, _this->session_id);
	bytebuf_add_avp(bytebuf, avp, 2);

	if ((rval = l2tp_ctrl_send_packet(_this->ctrl, _this->peer_session_id,
	    bytebuf)) != 0) {
		l2tp_call_log(_this, LOG_ERR, "failed to SendICRP: %m");
		return 1;
	}
	l2tp_call_log(_this, LOG_INFO, "SendICRP session_id=%u",
	    _this->session_id);
	return 0;
}

/* send L2TP data message */
static int
l2tp_call_send_data_packet(l2tp_call *_this, bytebuffer *buffer)
{
	int rval;
	struct l2tp_header *hdr;

	bytebuffer_flip(buffer);
	hdr = (struct l2tp_header *)bytebuffer_pointer(buffer);
	memset(hdr, 0, sizeof(*hdr) - 4);	/* Nr, NS are option */

	hdr->t = 0;
	hdr->ver = L2TP_HEADER_VERSION_RFC2661;
	hdr->l = 1;
	hdr->length = htons(bytebuffer_remaining(buffer));
	hdr->tunnel_id = htons(_this->ctrl->peer_tunnel_id);
	hdr->session_id = htons(_this->peer_session_id);
	if (_this->use_seq) {
		hdr->s = 1;
		hdr->ns = htons(_this->snd_nxt++);
		hdr->nr = htons(_this->rcv_nxt);
	}

	if (L2TP_CTRL_CONF(_this->ctrl)->data_out_pktdump != 0) {
		l2tpd_log(_this->ctrl->l2tpd, LOG_DEBUG,
		    "ctrl=%u call=%u L2TP Data output packet dump",
		    _this->ctrl->id, _this->id);
		show_hd(debug_get_debugfp(), bytebuffer_pointer(buffer),
		    bytebuffer_remaining(buffer));
	}
	if ((rval = l2tp_ctrl_send(_this->ctrl, bytebuffer_pointer(buffer),
	    bytebuffer_remaining(buffer))) < 0) {
		L2TP_CALL_DBG((_this, LOG_DEBUG, "sendto() failed: %m"));
	}

	return (rval == bytebuffer_remaining(buffer))? 0 : 1;
}

/*
 * receive ICCN
 * @return	return 0 if the ICCN is acceptable.
 *		other value means fail to receive, and
 *		CDN was sent and status was updated.
 */
static int
l2tp_call_recv_ICCN(l2tp_call *_this, u_char *pkt, int pktlen,
    dialin_proxy_info *dpi)
{
	int avpsz, tx_conn_speed;
	uint32_t framing_type = 0;
	struct l2tp_avp *avp;
	char buf[L2TP_AVP_MAXSIZ], emes[256];

	tx_conn_speed = 0;
	avp = (struct l2tp_avp *)buf;
	while (pktlen >= 6 && (avpsz = avp_enum(avp, pkt, pktlen, 1)) > 0) {
		pkt += avpsz;
		pktlen -= avpsz;
		if (avp->vendor_id != 0) {
			L2TP_CALL_DBG((_this, LOG_DEBUG,
			    "Received a Vendor-specific AVP vendor-id=%d "
			    "type=%d", avp->vendor_id, avp->attr_type));
			continue;
		}
		if (avp->is_hidden != 0) {
			l2tp_call_log(_this, LOG_WARNING,
			    "Received AVP (%s/%d) is hidden.  But we don't "
			    "share secret.",
			    avp_attr_type_string(avp->attr_type),
			    avp->attr_type);
			if (avp->is_mandatory != 0) {
				l2tp_call_disconnect(_this,
				    L2TP_CDN_RCODE_ERROR_CODE,
				    L2TP_ECODE_UNKNOWN_MANDATORY_AVP, NULL,
				    NULL, 0);
				return 1;
			}
			continue;
		}
		switch (avp->attr_type) {
		case L2TP_AVP_TYPE_MESSAGE_TYPE:
			AVP_SIZE_CHECK(avp, ==, 8);
			continue;
		case L2TP_AVP_TYPE_RX_CONNECT_SPEED:
			/*
			 * As RFC 2661 this AVP is not mandatory.  But `xl2tpd'
			 * sends this as a mandatory AVP.  Handle this to
			 * ignore the xl2tpd' bug.
			 */
			AVP_SIZE_CHECK(avp, ==, 10);
			continue;
		case L2TP_AVP_TYPE_TX_CONNECT_SPEED:
			AVP_SIZE_CHECK(avp, ==, 10);
			tx_conn_speed = avp_get_val32(avp);
			continue;
		case L2TP_AVP_TYPE_FRAMING_TYPE:
			AVP_SIZE_CHECK(avp, ==, 10);
			framing_type = avp_get_val32(avp);
			continue;
		case L2TP_AVP_TYPE_SEQUENCING_REQUIRED:
			_this->seq_required = 1;
			_this->use_seq = 1;
			continue;
	    /*
	     * AVP's for Proxy-LCP and Proxy-Authen
	     */
		case L2TP_AVP_TYPE_LAST_SENT_LCP_CONFREQ:
			AVP_MAXLEN_CHECK(avp, sizeof(dpi->last_sent_lcp.data));
			memcpy(dpi->last_sent_lcp.data, avp->attr_value,
			    avp_attr_length(avp));
			dpi->last_sent_lcp.ldata = avp_attr_length(avp);
			break;
		case L2TP_AVP_TYPE_LAST_RECV_LCP_CONFREQ:
			AVP_MAXLEN_CHECK(avp, sizeof(dpi->last_recv_lcp.data));
			memcpy(dpi->last_recv_lcp.data, avp->attr_value,
			    avp_attr_length(avp));
			dpi->last_recv_lcp.ldata = avp_attr_length(avp);
			break;
		case L2TP_AVP_TYPE_PROXY_AUTHEN_CHALLENGE:
			AVP_MAXLEN_CHECK(avp, sizeof(dpi->auth_chall));
			memcpy(dpi->auth_chall, avp->attr_value,
			    avp_attr_length(avp));
			dpi->lauth_chall = avp_attr_length(avp);
			break;
		case L2TP_AVP_TYPE_PROXY_AUTHEN_ID:
			AVP_SIZE_CHECK(avp, ==, 8);
			dpi->auth_id = avp_get_val16(avp);
			break;
		case L2TP_AVP_TYPE_PROXY_AUTHEN_NAME:
			AVP_MAXLEN_CHECK(avp, sizeof(dpi->username) - 1);
			memcpy(dpi->username, avp->attr_value,
			    avp_attr_length(avp));
			break;
		case L2TP_AVP_TYPE_PROXY_AUTHEN_RESPONSE:
			AVP_MAXLEN_CHECK(avp, sizeof(dpi->auth_resp));
			memcpy(dpi->auth_resp, avp->attr_value,
			    avp_attr_length(avp));
			dpi->lauth_resp = avp_attr_length(avp);
			break;
		case L2TP_AVP_TYPE_PROXY_AUTHEN_TYPE:
			AVP_SIZE_CHECK(avp, ==, 8);
			switch (avp_get_val16(avp)) {
			default:
				l2tp_call_log(_this, LOG_WARNING,
				    "RecvICCN Unknown proxy-authen-type=%d",
				    avp_get_val16(avp));
				/* FALLTHROUGH */
			case L2TP_AUTH_TYPE_NO_AUTH:
				dpi->auth_type = 0;
				break;
			case L2TP_AUTH_TYPE_PPP_CHAP:
				dpi->auth_type = PPP_AUTH_CHAP_MD5;
				break;
			case L2TP_AUTH_TYPE_PPP_PAP:
				dpi->auth_type = PPP_AUTH_PAP;
				break;
			case L2TP_AUTH_TYPE_MS_CHAP_V1:
				dpi->auth_type = PPP_AUTH_CHAP_MS;
				break;
			}
			break;
		default:
			if (avp->is_mandatory != 0) {
				l2tp_call_log(_this, LOG_WARNING,
				    "AVP (%s/%d) is not supported, but it's "
				    "mandatory",
				    avp_attr_type_string(avp->attr_type),
				    avp->attr_type);
				l2tp_call_disconnect(_this,
				    L2TP_CDN_RCODE_ERROR_CODE,
				    L2TP_ECODE_UNKNOWN_MANDATORY_AVP, NULL,
				    NULL, 0);
				return 1;
#ifdef L2TP_CALL_DEBUG
			} else {
				L2TP_CALL_DBG((_this, LOG_DEBUG,
				    "AVP (%s/%d) is not handled",
				    avp_attr_type_string(avp->attr_type),
				    avp->attr_type));
#endif
			}
		}
	}
	l2tp_call_log(_this, LOG_INFO, "RecvICCN "
	    "session_id=%u calling_number=%s tx_conn_speed=%u framing=%s",
	    _this->peer_session_id, _this->calling_number, tx_conn_speed,
	    ((framing_type & L2TP_FRAMING_CAP_FLAGS_ASYNC) != 0)? "async" :
	    ((framing_type & L2TP_FRAMING_CAP_FLAGS_SYNC) != 0)? "sync" :
	    "unknown");

	return 0;
size_check_failed:
	l2tp_call_log(_this, LOG_ERR, "Received bad ICCN: %s", emes);
	l2tp_call_disconnect(_this, L2TP_CDN_RCODE_ERROR_CODE,
	    L2TP_ECODE_WRONG_LENGTH, NULL, NULL, 0);
	return 1;
}

/* receive CDN */
static int
l2tp_recv_CDN(l2tp_call *_this, u_char *pkt, int pktlen)
{
	int result, error, avpsz, len, sessid;
	struct l2tp_avp *avp;
	char buf[L2TP_AVP_MAXSIZ], emes[256], pmes[256];

	/* initialize */
	result = 0;
	error = 0;
	sessid = 0;
	strlcpy(pmes, "(none)", sizeof(pmes));

	avp = (struct l2tp_avp *)buf;
	while (pktlen >= 6 && (avpsz = avp_enum(avp, pkt, pktlen, 1)) > 0) {
		pkt += avpsz;
		pktlen -= avpsz;
		if (avp->vendor_id != 0) {
			L2TP_CALL_DBG((_this, LOG_DEBUG,
			    "Received a Vendor-specific AVP vendor-id=%d "
			    "type=%d", avp->vendor_id, avp->attr_type));
			continue;
		}
		if (avp->is_hidden != 0) {
			l2tp_call_log(_this, LOG_WARNING,
			    "Received AVP (%s/%d) is hidden.  But we don't "
			    "share secret.",
			    avp_attr_type_string(avp->attr_type),
			    avp->attr_type);
			if (avp->is_mandatory != 0) {
				l2tp_call_disconnect(_this,
				    L2TP_CDN_RCODE_ERROR_CODE,
				    L2TP_ECODE_UNKNOWN_MANDATORY_AVP, NULL,
				    NULL, 0);
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
		case L2TP_AVP_TYPE_ASSIGNED_SESSION_ID:
			AVP_SIZE_CHECK(avp, >=, 8);
			sessid = avp_get_val16(avp);
			continue;
		default:
			if (avp->is_mandatory) {
				l2tp_call_log(_this, LOG_WARNING,
				    "AVP (%s/%d) is not supported, but it's "
				    "mandatory",
				    avp_attr_type_string(avp->attr_type),
				    avp->attr_type);
				if (avp->is_mandatory != 0) {
					l2tp_call_disconnect(_this,
					    L2TP_CDN_RCODE_ERROR_CODE,
					    L2TP_ECODE_UNKNOWN_MANDATORY_AVP,
					    NULL, NULL, 0);
					return 1;
				}
#ifdef L2TP_CALL_DEBUG
			} else {
				L2TP_CALL_DBG((_this, LOG_DEBUG,
				    "AVP (%s/%d) is not handled",
				    avp_attr_type_string(avp->attr_type),
				    avp->attr_type));
#endif
			}
		}
	}
	if (error == 0) {
		l2tp_call_log(_this, LOG_INFO,
		    "RecvCDN result=%s/%d", l2tp_cdn_rcode_string(result),
		    result);
	} else {
		l2tp_call_log(_this, LOG_INFO,
		    "RecvCDN result=%s/%d error=%s/%d message=%s",
		    l2tp_cdn_rcode_string(result), result,
		    l2tp_ecode_string(error), error, pmes);
	}

	return 0;

size_check_failed:
	/* continue to process even if the CDN message was broken */
	l2tp_call_log(_this, LOG_ERR, "Received bad CDN: %s", emes);

	return 0;
}

/* send CDN */
static int
l2tp_call_send_CDN(l2tp_call *_this, int result_code, int error_code, const
    char *errmes, struct l2tp_avp *addavp[], int naddavp)
{
	uint32_t val32;
	int i, avplen, len;
	struct l2tp_avp *avp;
	char buf[L2TP_AVP_MAXSIZ];
	bytebuffer *bytebuf;

	L2TP_CALL_ASSERT(_this != NULL);
	bytebuf = l2tp_ctrl_prepare_snd_buffer(_this->ctrl, 1);
	if (bytebuf == NULL) {
		l2tp_call_log(_this, LOG_ERR, "sending CDN failed: no buffer");
		return 1;
	}
	avp = (struct l2tp_avp *)buf;

	/* Message Type = CDN */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_MESSAGE_TYPE;
	avp_set_val16(avp, L2TP_AVP_MESSAGE_TYPE_CDN);
	bytebuf_add_avp(bytebuf, avp, 2);

	/* Result Code */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_RESULT_CODE;
#if 0
/*
 * Windows 2000 work around:
 * Windows 2000 will return "2 - Length is wrong" in StopCCN,
 * when it received "length = 8 and no error code AVP".
 * Avoid the error, use AVP length = 10.
 */
	if (error_code > 0) {
		val32 = (result_code << 16) | (error_code & 0xffff);
		avplen = 4;
		avp_set_val32(avp, val32);
	} else {
		avplen = 2;
		avp_set_val16(avp, result_code);
	}
#else
	val32 = (result_code << 16) | (error_code & 0xffff);
	avplen = 4;
	avp_set_val32(avp, val32);
#endif

	if (errmes != NULL) {
		len = MINIMUM(strlen(errmes), L2TP_AVP_MAXSIZ - 128);
		memcpy(&avp->attr_value[avplen], errmes, len);
		avplen += len;
	}
	bytebuf_add_avp(bytebuf, avp, avplen);

	/* Assigned Session Id */
	memset(avp, 0, sizeof(*avp));
	avp->is_mandatory = 1;
	avp->attr_type = L2TP_AVP_TYPE_ASSIGNED_SESSION_ID;
	if (_this != NULL && _this->session_id != 0)
		avp_set_val16(avp, _this->session_id);
	else
		avp_set_val16(avp, 0);
	bytebuf_add_avp(bytebuf, avp, 2);

	for (i = 0; i < naddavp; i++)
		bytebuf_add_avp(bytebuf, addavp[i], addavp[i]->length - 6);

	if (l2tp_ctrl_send_packet(_this->ctrl, _this->peer_session_id,
	    bytebuf) != 0) {
		l2tp_call_log(_this, LOG_ERR, "Error sending CDN: %m");
		return 1;
	}

	if (error_code > 0) {
		l2tp_call_log(_this, LOG_INFO,
		    "SendCDN result=%s/%d error=%s/%d message=%s",
		    l2tp_cdn_rcode_string(result_code), result_code,
		    l2tp_ecode_string(error_code), error_code,
		    (errmes == NULL)? "none" : errmes);
	} else {
		l2tp_call_log(_this, LOG_INFO, "SendCDN result=%s/%d",
		    l2tp_cdn_rcode_string(result_code), result_code);
	}

	return 0;
}

/* send ZLB */
static int
l2tp_call_send_ZLB(l2tp_call *_this)
{
	bytebuffer *bytebuf;

	l2tp_call_log(_this, LOG_INFO, "SendZLB");
	bytebuf = l2tp_ctrl_prepare_snd_buffer(_this->ctrl, 1);
	if (bytebuf == NULL) {
		l2tp_call_log(_this, LOG_ERR, "sending ZLB failed: no buffer");
		return 1;
	}
	return l2tp_ctrl_send_packet(_this->ctrl, _this->peer_session_id,
	    bytebuf);
}

/*
 * misc
 */
/* logging with the label of the instance */
static void
l2tp_call_log(l2tp_call *_this, int prio, const char *fmt, ...)
{
	char logbuf[BUFSIZ];
	va_list ap;

	va_start(ap, fmt);
#ifdef	L2TPD_MULTIPLE
	snprintf(logbuf, sizeof(logbuf), "l2tpd id=%u ctrl=%u call=%u %s",
	    _this->ctrl->l2tpd->id, _this->ctrl->id, _this->id, fmt);
#else
	snprintf(logbuf, sizeof(logbuf), "l2tpd ctrl=%u call=%u %s",
	    _this->ctrl->id, _this->id, fmt);
#endif
	vlog_printf(prio, logbuf, ap);
	va_end(ap);
}

/* convert current status to strings */
static inline const char *
l2tp_call_state_string(l2tp_call *_this)
{
	switch (_this->state) {
	case L2TP_CALL_STATE_IDLE:		return "idle";
	case L2TP_CALL_STATE_WAIT_CONN:		return "wait-conn";
	case L2TP_CALL_STATE_ESTABLISHED:	return "established";
	case L2TP_CALL_STATE_CLEANUP_WAIT:	return "cleanup-wait";
	}
	return "unknown";
}

/*
 * npppd physical layer
 */

/* input packet to ppp */
void
l2tp_call_ppp_input(l2tp_call *_this, u_char *pkt, int pktlen, int delayed)
{
	int rval;
	npppd_ppp *ppp;

	ppp = _this->ppp;
	rval = ppp->recv_packet(ppp, pkt, pktlen,
	    delayed ? PPP_IO_FLAGS_DELAYED : 0);

	if (_this->ppp == NULL)		/* ppp is freed */
		return;

	if (rval != 0)
		ppp->ierrors++;
	else {
		ppp->ipackets++;
		ppp->ibytes += pktlen;
	}
}

/* called ppp output a packet */
static int
l2tp_call_ppp_output(npppd_ppp *ppp, unsigned char *bytes, int nbytes,
    int flags)
{
	l2tp_call *_this;
	bytebuffer *bytebuf;

	_this = ppp->phy_context;

	bytebuf = l2tp_ctrl_prepare_snd_buffer(_this->ctrl, _this->use_seq);

	if (bytebuf != NULL) {
		bytebuffer_put(bytebuf, bytes, nbytes);
		if (l2tp_call_send_data_packet(_this, bytebuf) != 0)
			ppp->oerrors++;
		else {
			ppp->opackets++;
			ppp->obytes += nbytes;
		}
	} else
		ppp->oerrors++;

	return 0;
}

/* it will be called when the connection was closed at ppp */
static void
l2tp_call_closed_by_ppp(npppd_ppp *ppp)
{
	l2tp_call *_this;

	L2TP_CALL_ASSERT(ppp != NULL);
	L2TP_CALL_ASSERT(ppp->phy_context != NULL);

	_this = ppp->phy_context;

	/* do before l2tp_call_disconnect() */
	_this->ppp = NULL;

	if (_this->state == L2TP_CALL_STATE_CLEANUP_WAIT) {
		/*  no need to call l2tp_call_disconnect */
	} else if (ppp->disconnect_code == PPP_DISCON_NO_INFORMATION) {
		l2tp_call_disconnect(_this,
		    L2TP_CDN_RCODE_ADMINISTRATIVE_REASON, 0, NULL, NULL, 0);
	} else {
		/*
		 * RFC3145 L2TP Disconnect Cause Information
		 */
		struct l2tp_avp *avp[1];
		struct _ppp_cause {
			struct l2tp_avp avp;
			uint16_t	code;
			uint16_t	proto;
			uint8_t		direction;
			char		message[128];
		} __attribute__((__packed__)) ppp_cause;

		ppp_cause.avp.is_mandatory = 0;
		ppp_cause.avp.is_hidden = 0;
		ppp_cause.avp.vendor_id = 0;	/* ietf */
		ppp_cause.avp.attr_type =
		    L2TP_AVP_TYPE_PPP_DISCONNECT_CAUSE_CODE;
		ppp_cause.code = htons(ppp->disconnect_code);
		ppp_cause.proto = htons(ppp->disconnect_proto);
		ppp_cause.direction = ppp->disconnect_direction;
		ppp_cause.avp.length = offsetof(struct _ppp_cause, message[0]);

		if (ppp->disconnect_message != NULL) {
			strlcpy(ppp_cause.message, ppp->disconnect_message,
			    sizeof(ppp_cause.message));
			ppp_cause.avp.length += strlen(ppp_cause.message);
		}
		avp[0] = &ppp_cause.avp;
		l2tp_call_disconnect(_this,
		    L2TP_CDN_RCODE_ERROR_CODE, L2TP_ECODE_GENERIC_ERROR,
		    "Disconnected by local PPP", avp, 1);
	}
	l2tp_call_log(_this, LOG_NOTICE, "logtype=PPPUnbind");
}

/* notify disconnection to ppp to terminate or free of ppp */
static void
l2tp_call_notify_down(l2tp_call *_this)
{
	if (_this->ppp != NULL)
		ppp_phy_downed(_this->ppp);
}

/* bind ppp */
static int
l2tp_call_bind_ppp(l2tp_call *_this, dialin_proxy_info *dpi)
{
	int code, errcode;
	npppd_ppp *ppp;

	code = L2TP_CDN_RCODE_BUSY;
	errcode = 0;
	ppp = NULL;
	if ((ppp = ppp_create()) == NULL)
		goto fail;

	ASSERT(_this->ppp == NULL);

	if (_this->ppp != NULL)
		return -1;

	_this->ppp = ppp;

	ppp->tunnel_type = NPPPD_TUNNEL_L2TP;
	ppp->tunnel_session_id = _this->session_id;
	ppp->phy_context = _this;
	ppp->send_packet = l2tp_call_ppp_output;
	ppp->phy_close = l2tp_call_closed_by_ppp;

	strlcpy(ppp->phy_label, L2TP_CTRL_LISTENER_TUN_NAME(_this->ctrl),
	    sizeof(ppp->phy_label));
	L2TP_CALL_ASSERT(sizeof(ppp->phy_info) >= _this->ctrl->peer.ss_len);
	memcpy(&ppp->phy_info, &_this->ctrl->peer,
	    MINIMUM(sizeof(ppp->phy_info), _this->ctrl->peer.ss_len));
	strlcpy(ppp->calling_number, _this->calling_number,
	    sizeof(ppp->calling_number));
	if (ppp_init(npppd_get_npppd(), ppp) != 0) {
		l2tp_call_log(_this, LOG_ERR, "failed binding ppp");
		goto fail;
	}

	l2tp_call_log(_this, LOG_NOTICE, "logtype=PPPBind ppp=%d", ppp->id);
	if (DIALIN_PROXY_IS_REQUESTED(dpi)) {
		if (!L2TP_CTRL_CONF(_this->ctrl)->accept_dialin) {
			l2tp_call_log(_this, LOG_ERR,
			    "'accept_dialin' is 'false' in the setting.");
			code = L2TP_CDN_RCODE_ERROR_CODE;
			errcode = L2TP_ECODE_INVALID_MESSAGE;
			goto fail;
		}

		if (ppp_dialin_proxy_prepare(ppp, dpi) != 0) {
			code = L2TP_CDN_RCODE_TEMP_NOT_AVALIABLE;
			goto fail;
		}
	}
	ppp_start(ppp);

	return 0;
fail:
	if (ppp != NULL)
		ppp_destroy(ppp);
	_this->ppp = NULL;

	l2tp_call_disconnect(_this, code, errcode, NULL, NULL, 0);
	return 1;
}
