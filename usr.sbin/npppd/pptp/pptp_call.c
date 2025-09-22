/*	$OpenBSD: pptp_call.c,v 1.12 2021/03/29 03:54:40 yasuoka Exp $	*/

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
/* $Id: pptp_call.c,v 1.12 2021/03/29 03:54:40 yasuoka Exp $ */
/**@file PPTP Call */
/* currently it supports PAC mode only */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <net/if_dl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <event.h>

#ifdef USE_LIBSOCKUTIL
#include <seil/sockfromto.h>
#endif

#include "bytebuf.h"
#include "slist.h"
#include "hash.h"
#include "debugutil.h"
#include "time_utils.h"

#include "pptp.h"
#include "pptp_local.h"
#include "pptp_subr.h"

#include "npppd.h"

#ifdef PPTP_CALL_DEBUG
#define PPTP_CALL_DBG(x)	pptp_call_log x
#define PPTP_CALL_ASSERT(x)	ASSERT(x)
#else
#define PPTP_CALL_DBG(x)
#define PPTP_CALL_ASSERT(x)
#endif

static void  pptp_call_log (pptp_call *, int, const char *, ...) __printflike(3,4);


static void  pptp_call_notify_down (pptp_call *);
static int   pptp_call_recv_SLI (pptp_call *, u_char *, int);
static int   pptp_call_recv_CCR (pptp_call *, u_char *, int);
static int   pptp_call_recv_OCRQ (pptp_call *, u_char *, int);
static void  pptp_call_send_CDN (pptp_call *, int, int, int, const char *);
static int   pptp_call_send_OCRP (pptp_call *, int, int, int);
static int   pptp_call_gre_output (pptp_call *, int, int, u_char *, int);
static int   pptp_call_bind_ppp (pptp_call *);
static void  pptp_call_log (pptp_call *, int, const char *, ...);
static void  pptp_call_OCRQ_string (struct pptp_ocrq *, char *, int);
static void  pptp_call_OCRP_string (struct pptp_ocrp *, char *, int);
static void   pptp_call_ppp_input (pptp_call *, unsigned char *, int, int);
static char * pptp_call_state_string(int);

static int   pptp_call_ppp_output (npppd_ppp *, unsigned char *, int, int);
static void  pptp_call_closed_by_ppp (npppd_ppp *);

/* not used
static int   pptp_call_send_SLI (pptp_call *);
 */

#define SEQ_LT(a,b)	((int)((a) - (b)) <  0)
#define SEQ_LE(a,b)	((int)((a) - (b)) <= 0)
#define SEQ_GT(a,b)	((int)((a) - (b)) >  0)
#define SEQ_GE(a,b)	((int)((a) - (b)) >= 0)
#define SEQ_SUB(a,b)	((int32_t)((a) - (b)))

/* round-up division */
#define RUPDIV(n,d)	(((n) + ((d) - ((n) % (d)))) / (d))

/*
 * instance related functions
 */
pptp_call *
pptp_call_create(void)
{
	pptp_call *_this;

	if ((_this = malloc(sizeof(pptp_call))) == NULL)
		return NULL;

	return _this;
}

int
pptp_call_init(pptp_call *_this, pptp_ctrl *ctrl)
{
	memset(_this, 0, sizeof(pptp_call));
	_this->ctrl = ctrl;

	_this->maxwinsz = PPTP_CALL_DEFAULT_MAXWINSZ;
	_this->winsz = RUPDIV(_this->maxwinsz, 2);
	_this->last_io = get_monosec();
	_this->snd_nxt = 1;

	return 0;
}

int
pptp_call_start(pptp_call *_this)
{
	if (pptp_call_bind_ppp(_this) != 0)
		return 1;

	return 0;
}

int
pptp_call_stop(pptp_call *_this)
{
	if (_this->state != PPTP_CALL_STATE_CLEANUP_WAIT)
		pptp_call_disconnect(_this, 0, 0, NULL);

	pptp_call_log(_this, LOG_NOTICE, "logtype=Terminated");
	pptpd_release_call(_this->ctrl->pptpd, _this);

	return 0;
}

void
pptp_call_destroy(pptp_call *_this)
{
	PPTP_CALL_ASSERT(_this != NULL);
	free(_this);
}

void
pptp_call_disconnect(pptp_call *_this, int result, int error, const char *
    statistics)
{
	if (_this->state == PPTP_CALL_STATE_CLEANUP_WAIT) {
		pptp_call_notify_down(_this);
		return;
	}
	if (result > 0)
		pptp_call_send_CDN(_this, result, error, 0, statistics);
	_this->state = PPTP_CALL_STATE_CLEANUP_WAIT;
	pptp_call_notify_down(_this);
}

/*
 * PPTP control packet I/O
 */

void
pptp_call_input(pptp_call *_this, int mes_type, u_char *pkt, int lpkt)
{

	PPTP_CALL_ASSERT(_this != NULL);
	PPTP_CALL_ASSERT(pkt != NULL);
	PPTP_CALL_ASSERT(lpkt >= 4);

	_this->last_io = get_monosec();
	switch (mes_type) {
	case PPTP_CTRL_MES_CODE_OCRQ:
		if (_this->state != PPTP_CALL_STATE_IDLE) {
			pptp_call_send_OCRP(_this,
			    PPTP_OCRP_RESULT_GENERIC_ERROR,
			    PPTP_ERROR_BAD_CALL, 0);
			goto bad_state;
		}
		if (pptp_call_recv_OCRQ(_this, pkt, lpkt) != 0) {
			pptp_call_send_OCRP(_this,
			    PPTP_OCRP_RESULT_GENERIC_ERROR,
			    PPTP_ERROR_BAD_CALL, 0);
			return;
		}
		if (pptpd_assign_call(_this->ctrl->pptpd, _this) != 0) {
			pptp_call_send_OCRP(_this, PPTP_OCRP_RESULT_BUSY,
			    PPTP_ERROR_NONE, 0);
			return;
		}
		if (pptp_call_send_OCRP(_this, PPTP_OCRP_RESULT_CONNECTED,
		    PPTP_ERROR_NONE, 0) != 0) {
			pptp_call_disconnect(_this,
			    PPTP_CDN_RESULT_GENRIC_ERROR,
			    PPTP_ERROR_PAC_ERROR, NULL);
			return;
		}
		if (pptp_call_start(_this) != 0) {
			pptp_call_disconnect(_this,
			    PPTP_CDN_RESULT_GENRIC_ERROR,
			    PPTP_ERROR_PAC_ERROR, NULL);
			return;
		}
		_this->state = PPTP_CALL_STATE_ESTABLISHED;
		break;
	case PPTP_CTRL_MES_CODE_SLI:
		if (pptp_call_recv_SLI(_this, pkt, lpkt) != 0) {
			return;
		}
		return;
	case PPTP_CTRL_MES_CODE_CCR:
		pptp_call_recv_CCR(_this, pkt, lpkt);
		if (_this->state == PPTP_CALL_STATE_ESTABLISHED) {
			pptp_call_disconnect(_this, PPTP_CDN_RESULT_REQUEST,
			    PPTP_ERROR_NONE, NULL);
		} else {
			pptp_call_disconnect(_this, 0, 0, NULL);
			if (_this->state != PPTP_CALL_STATE_CLEANUP_WAIT)
				goto bad_state;
		}
		return;
	default:
	    pptp_call_log(_this, LOG_WARNING,
		"Unhandled control message type=%s(%d)",
		    pptp_ctrl_mes_type_string(mes_type), mes_type);
	}
	return;
bad_state:
	pptp_call_log(_this, LOG_WARNING,
	    "Received control message %s(%d) in bad state=%s",
		pptp_ctrl_mes_type_string(mes_type), mes_type,
		pptp_call_state_string(_this->state));
}

/* receive Set-Link-Info */
/* XXX: this implementation is only supporting "Sync-Frame",
 * ACCM (Asynchronous Control Character Map) will be discarded */
static int
pptp_call_recv_SLI(pptp_call *_this, u_char *pkt, int lpkt)
{
	struct pptp_sli *sli;

	if (lpkt < sizeof(struct pptp_sli)) {
		pptp_call_log(_this, LOG_ERR, "Received bad SLI: packet too "
		    "short: %d < %d", lpkt, (int)sizeof(struct pptp_sli));
		return 1;
	}
	sli = (struct pptp_sli *)pkt;
	sli->send_accm = ntohl(sli->send_accm);
	sli->recv_accm = ntohl(sli->recv_accm);
	if (sli->send_accm != 0xffffffffL ||
	    sli->recv_accm != 0xffffffffL) {
		pptp_call_log(_this, LOG_WARNING,
		    "RecvSLI Received bad ACCM %08x:%08x: asynchronous framing "
		    "is not supported.\n", sli->send_accm, sli->recv_accm);
		return 1;
	}
	pptp_call_log(_this, LOG_INFO, "RecvSLI accm=%08x:%08x",
	    sli->send_accm, sli->recv_accm);

	return 0;
}

#if 0
/* Some route implementation which has "PPTP pass-through" function
 * will discard 1723/tcp packet between PAC and PNS, when it recognize
 * SLI from PAC. (for example, FLASHWAVE by Fujitsu).
 *
 * To avoid avobe situation, npppd send well-known SLI only.
 */
static int
pptp_call_send_SLI(pptp_call *_this)
{
	int lpkt;
	struct pptp_sli *sli;

	sli = bytebuffer_pointer(_this->ctrl->send_buf);
	lpkt = bytebuffer_remaining(_this->ctrl->send_buf);
	if (lpkt < sizeof(struct pptp_sli)) {
		pptp_call_log(_this, LOG_ERR,
		    "SendOCRP failed: No buffer space available");
		return -1;
	}
	memset(sli, 0, sizeof(struct pptp_sli));

	pptp_init_header(&sli->header, sizeof(struct pptp_sli),
	    PPTP_CTRL_MES_CODE_SLI);

	sli->peers_call_id = _this->id;
	sli->send_accm = 0xffffffff;
	sli->recv_accm = 0xffffffff;

	_this->last_io = get_monosec();
	pptp_call_log(_this, LOG_INFO, "SendSLI accm=%08x:%08x",
	    sli->send_accm, sli->recv_accm);
	sli->peers_call_id = htons(sli->peers_call_id);
	sli->send_accm = htonl(sli->send_accm);
	sli->recv_accm = htonl(sli->recv_accm);
	pptp_ctrl_output(_this->ctrl, NULL,  sizeof(struct pptp_sli));

	return 0;
}
#endif

/* Receive Call-Clear-Request */
static int
pptp_call_recv_CCR(pptp_call *_this, u_char *pkt, int lpkt)
{
	struct pptp_ccr *ccr;

	/* check size */
	PPTP_CALL_ASSERT(lpkt >= sizeof(struct pptp_ccr));
	if (lpkt < sizeof(struct pptp_ccr)) {
		/* never happen, should KASSERT() here? */
		return 1;
	}
	ccr = (struct pptp_ccr *)pkt;

	/* convert byte-order */
	ccr->call_id = ntohs(ccr->call_id);
	pptp_call_log(_this, LOG_INFO, "RecvCCR call_id=%u", ccr->call_id);

	return 0;
}

/* Send Call-Disconnect-Notify */
static void
pptp_call_send_CDN(pptp_call *_this, int result, int error, int cause,
    const char *statistics)
{
	int lpkt;
	struct pptp_cdn *cdn;

	cdn = bytebuffer_pointer(_this->ctrl->send_buf);
	lpkt = bytebuffer_remaining(_this->ctrl->send_buf);
	if (lpkt < sizeof(struct pptp_cdn)) {
		pptp_call_log(_this, LOG_ERR,
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

	pptp_call_log(_this, LOG_INFO, "SendCDN "
	    "call_id=%u result=%s(%d) error=%s(%d) cause=%d statistics=%s",
	    cdn->call_id,
	    pptp_CDN_result_string(cdn->result_code), cdn->result_code,
	    pptp_general_error_string(cdn->error_code), cdn->error_code,
	    cdn->cause_code,
		(statistics == NULL)? "(none)" : (char *)cdn->statistics);

	cdn->call_id = htons(cdn->call_id);
	cdn->cause_code = htons(cdn->cause_code);

	_this->last_io = get_monosec();
	pptp_ctrl_output(_this->ctrl, NULL, sizeof(struct pptp_cdn));
}

/* Send Outgoing-Call-Reply */
static int
pptp_call_send_OCRP(pptp_call *_this, int result, int error, int cause)
{
	int lpkt;
	struct pptp_ocrp *ocrp;
	char logbuf[512];

	ocrp = bytebuffer_pointer(_this->ctrl->send_buf);
	lpkt = bytebuffer_remaining(_this->ctrl->send_buf);
	if (lpkt < sizeof(struct pptp_ocrp)) {
		pptp_call_log(_this, LOG_ERR,
		    "SendOCRP failed: No buffer space available");
		return -1;
	}
	memset(ocrp, 0, sizeof(struct pptp_ocrp));

	pptp_init_header(&ocrp->header, sizeof(struct pptp_ocrp),
	    PPTP_CTRL_MES_CODE_OCRP);

	ocrp->call_id = _this->id;
	ocrp->peers_call_id = _this->peers_call_id;
	ocrp->result_code = result;
	ocrp->error_code = error;
	ocrp->cause_code = cause;
	ocrp->connect_speed = PPTP_CALL_CONNECT_SPEED;
	ocrp->recv_winsz =  _this->maxwinsz;
	ocrp->packet_proccessing_delay = PPTP_CALL_INITIAL_PPD;
	ocrp->physical_channel_id = _this->id;

	pptp_call_OCRP_string(ocrp, logbuf, sizeof(logbuf));
	pptp_call_log(_this, LOG_INFO, "SendOCRP %s", logbuf);

	ocrp->call_id = htons(ocrp->call_id);
	ocrp->peers_call_id = htons(ocrp->peers_call_id);
	ocrp->cause_code = htons(ocrp->cause_code);
	ocrp->connect_speed = htons(ocrp->connect_speed);
	ocrp->recv_winsz = htons(ocrp->recv_winsz);
	ocrp->packet_proccessing_delay = htons(ocrp->packet_proccessing_delay);
	ocrp->physical_channel_id = htonl(ocrp->physical_channel_id);

	_this->last_io = get_monosec();
	pptp_ctrl_output(_this->ctrl, NULL,  sizeof(struct pptp_ocrp));

	return 0;
}

/* Receive Outgoing-Call-Request */
static int
pptp_call_recv_OCRQ(pptp_call *_this, u_char *pkt, int lpkt)
{
	char logbuf[512];
	struct pptp_ocrq *ocrq;

	/* check size */
	if (lpkt < sizeof(struct pptp_ocrq)) {
		pptp_call_log(_this, LOG_ERR, "Received bad OCRQ: packet too "
		    "short: %d < %d", lpkt, (int)sizeof(struct pptp_ocrq));
		return 1;
	}
	ocrq = (struct pptp_ocrq *)pkt;

	/* convert byte-order */
	ocrq->call_id = ntohs(ocrq->call_id);
	ocrq->call_serial_number = ntohs(ocrq->call_serial_number);
	ocrq->recv_winsz = ntohs(ocrq->recv_winsz);
	ocrq->packet_proccessing_delay =
	    ntohs(ocrq->packet_proccessing_delay);
	ocrq->phone_number_length = ntohs(ocrq->phone_number_length);
	ocrq->reservied1 = ntohs(ocrq->reservied1);
	ocrq->maximum_bps = ntohl(ocrq->maximum_bps);
	ocrq->minimum_bps = ntohl(ocrq->minimum_bps);
	ocrq->bearer_type = ntohl(ocrq->bearer_type);
	ocrq->framing_type = ntohl(ocrq->framing_type);

	_this->peers_call_id = ocrq->call_id;
	_this->peers_maxwinsz = ocrq->recv_winsz;

	pptp_call_OCRQ_string(ocrq, logbuf, sizeof(logbuf));
	pptp_call_log(_this, LOG_INFO, "RecvOCRQ %s", logbuf);

	return 0;
}

/*
 * GRE I/O
 */

/* Reciver packet via GRE */
void
pptp_call_gre_input(pptp_call *_this, uint32_t seq, uint32_t ack,
    int input_flags, u_char *pkt, int pktlen)
{
	int log_prio;
	const char *reason;
	int delayed = 0;

	PPTP_CALL_ASSERT(_this != NULL);

	log_prio = LOG_INFO;

#ifdef	PPTP_CALL_DEBUG
	if (debuglevel >= 2) {
		pptp_call_log(_this, LOG_DEBUG,
		    "Received data packet seq=%u(%u-%u) ack=%u(%u-%u)",
		    seq, _this->rcv_nxt,
		    _this->rcv_nxt + _this->peers_maxwinsz - 1,
		    ack, _this->snd_una, _this->snd_nxt);
	}
#endif
	if (_this->state != PPTP_CALL_STATE_ESTABLISHED) {
		pptp_call_log(_this, LOG_INFO,
		    "Received data packet in illegal state=%s",
		    pptp_call_state_string(_this->state));
		return;
	}
	PPTP_CALL_ASSERT(_this->state == PPTP_CALL_STATE_ESTABLISHED);

	if (input_flags & PPTP_GRE_PKT_ACK_PRESENT) {
		if (ack + 1 == _this->snd_una) {
			/* nothing to do */
		} else if (SEQ_LT(ack, _this->snd_una)) {
			delayed = 1;
		} else if (SEQ_GT(ack, _this->snd_nxt)) {
			reason = "ack for unknown sequence.";
			goto bad_pkt;
		} else {
			ack++;
			_this->snd_una = ack;
		}
	}

	if ((input_flags & PPTP_GRE_PKT_SEQ_PRESENT) == 0)
		return;	/* ack only packet */

	/* check sequence# */
	if (SEQ_LT(seq, _this->rcv_nxt)) {
		/* delayed delivery? */
		if (SEQ_LT(seq, _this->rcv_nxt - PPTP_CALL_DELAY_LIMIT)) {
			reason = "out of sequence";
			goto bad_pkt;
		}
		delayed = 1;
	} else if (SEQ_GE(seq, _this->rcv_nxt + _this->maxwinsz)){
		/* MUST Process them */
		/* XXX FIXME: if over 4096 packets lost, it can not
		 * fix MPPE state */
		pptp_call_log(_this, LOG_INFO,
		    "Received packet caused window overflow.  seq=%u(%u-%u), "
		    "may lost %d packets.", seq, _this->rcv_nxt,
		    _this->rcv_nxt + _this->maxwinsz - 1,
		    SEQ_SUB(seq, _this->rcv_nxt));
	}

	if (!delayed) {
		seq++;
		/* XXX : TODO: should it counts lost packets  ppp->ierrors
		 * and update ppp->ierrors counter? */
		_this->rcv_nxt = seq;

		if (SEQ_SUB(seq, _this->rcv_acked) > RUPDIV(_this->winsz, 2)) {
			/*
			 * Multi-packet acknowledgement.
			 * send ack when it reaches to half of window size
			 */
			PPTP_CALL_DBG((_this, LOG_DEBUG,
			    "rcv window size=%u %u %u\n",
			    SEQ_SUB(seq, _this->rcv_acked), seq,
			    _this->rcv_acked));
			pptp_call_gre_output(_this, 0, 1, NULL, 0);
		}
	}

	pptp_call_ppp_input(_this, pkt, pktlen, delayed);

	return;
bad_pkt:
	pptp_call_log(_this, log_prio,
	    "Received bad data packet: %s: seq=%u(%u-%u) ack=%u(%u-%u)",
	    reason, seq, _this->rcv_nxt, _this->rcv_nxt + _this->maxwinsz - 1,
	    ack, _this->snd_una, _this->snd_nxt);
}

/* output to GRE */
/* flags: fseq: contain SEQ field, fack: contain ACK field */
static int
pptp_call_gre_output(pptp_call *_this, int fseq, int fack, u_char *pkt,
    int lpkt)
{
	int sz;
	struct pptp_gre_header *grehdr;
	u_char buf[65535], *opkt;
	struct sockaddr_storage peer, sock;
#ifndef USE_LIBSOCKUTIL
	socklen_t peerlen;
#endif

	memset(buf, 0, sizeof(buf));

	opkt = buf;
	grehdr = (struct pptp_gre_header *)opkt;
	opkt += sizeof(struct pptp_gre_header);

	/* GRE header */
	grehdr->K = 1;
	grehdr->ver = PPTP_GRE_VERSION;
	grehdr->protocol_type = htons(PPTP_GRE_PROTOCOL_TYPE);
	grehdr->payload_length = htons(lpkt);
	grehdr->call_id = htons(_this->peers_call_id);

#ifdef	PPTP_CALL_DEBUG
	if (debuglevel >= 2 && (fseq || fack)) {
		pptp_call_log(_this, LOG_DEBUG,
		    "Sending data packet seq=%u ack=%u",
		    _this->snd_nxt, _this->rcv_nxt - 1);
	}
#endif
	PPTP_CALL_ASSERT(ALIGNED_POINTER(opkt, uint32_t));
	if (fseq) {
		grehdr->S = 1;
		*(uint32_t *)opkt = htonl(_this->snd_nxt++);
		opkt += 4;
	}
	if (fack) {
		grehdr->A = 1;
		_this->rcv_acked = _this->rcv_nxt;
		*(uint32_t *)opkt = htonl(_this->rcv_nxt - 1);
		opkt += 4;
	}
	if (lpkt > 0) {
		memcpy(opkt, pkt, lpkt);
		opkt += lpkt;
	}
	memcpy(&peer, &_this->ctrl->peer, sizeof(peer));
	memcpy(&sock, &_this->ctrl->our, sizeof(sock));
	switch (peer.ss_family) {
	case AF_INET:
		((struct sockaddr_in *)&peer)->sin_port = 0;
		((struct sockaddr_in *)&sock)->sin_port = 0;
#ifndef USE_LIBSOCKUTIL
		peerlen = sizeof(struct sockaddr_in);
#endif
		break;
	default:
		return 1;
	}

	if (PPTP_CTRL_CONF(_this->ctrl)->data_out_pktdump != 0) {
		pptp_call_log(_this, LOG_DEBUG, "PPTP Data output packet dump");
		show_hd(debug_get_debugfp(), buf, opkt - buf);
	}
#ifdef USE_LIBSOCKUTIL
	sz = sendfromto(pptp_ctrl_sock_gre(_this->ctrl), buf, opkt - buf,
	    0, (struct sockaddr *)&sock, (struct sockaddr *)&peer);
#else
	sz = sendto(pptp_ctrl_sock_gre(_this->ctrl), buf, opkt - buf, 0,
	    (struct sockaddr *)&peer, peerlen);
#endif

	if (sz <= 0)
		pptp_call_log(_this, LOG_WARNING, "sendto(%d) failed: %m",
		    pptp_ctrl_sock_gre(_this->ctrl));

	return (sz > 0)? 0 : 1;
}

/*
 * npppd physical layer functions
 */

/* notify to ppp that the PPTP physical layer is already downed */
static void
pptp_call_notify_down(pptp_call *_this)
{
	if (_this->ppp != NULL)
		ppp_phy_downed(_this->ppp);
}


/* input packet to ppp */
static void
pptp_call_ppp_input(pptp_call *_this, u_char *pkt, int pktlen, int delayed)
{
	int rval;
	npppd_ppp *ppp;

	ppp = _this->ppp;
	if (ppp == NULL) {
		pptp_call_log(_this, LOG_WARNING,
		    "Received ppp frame but ppp is not assigned yet");
		return;
	}
	rval = ppp->recv_packet(ppp, pkt, pktlen, delayed ? PPP_IO_FLAGS_DELAYED : 0);
	if (_this->ppp == NULL)		/* ppp is freed */
		return;

	if (rval != 0) {
		ppp->ierrors++;
	} else {
		ppp->ipackets++;
		ppp->ibytes += pktlen;
	}
}

/* it called when ppp outputs packet */
static int
pptp_call_ppp_output(npppd_ppp *ppp, unsigned char *bytes, int nbytes,
    int flags)
{
	pptp_call *_this;

	_this = ppp->phy_context;
	PPTP_CALL_ASSERT(_this != NULL);

	if (_this == NULL)
		return 0;

	if (pptp_call_gre_output(_this, 1, 1, bytes, nbytes) != 0) {
		ppp->oerrors++;
		return 1;
	}
	ppp->opackets++;
	ppp->obytes += nbytes;

	return 0;
}

/* it called when pptp call was closed at ppp */
static void
pptp_call_closed_by_ppp(npppd_ppp *ppp)
{
	pptp_call *_this;

	PPTP_CALL_ASSERT(ppp != NULL);
	PPTP_CALL_ASSERT(ppp->phy_context != NULL);

	_this = ppp->phy_context;

	/* do this before pptp_call_disconnect() */
	_this->ppp = NULL;

	if (_this->state != PPTP_CALL_STATE_CLEANUP_WAIT) {
		pptp_call_disconnect(_this, PPTP_CDN_RESULT_LOST_CARRIER, 0,
		    NULL);
	}
	pptp_call_log(_this, LOG_NOTICE, "logtype=PPPUnbind");
}

/* bind() for ppp */
static int
pptp_call_bind_ppp(pptp_call *_this)
{
	npppd_ppp *ppp;

	ppp = NULL;
	if ((ppp = ppp_create()) == NULL)
		goto fail;

	PPTP_CALL_ASSERT(_this->ppp == NULL);

	if (_this->ppp != NULL)
		return -1;

	_this->ppp = ppp;

	ppp->phy_context = _this;
	ppp->tunnel_type = NPPPD_TUNNEL_PPTP;
	ppp->tunnel_session_id = _this->id;
	ppp->send_packet = pptp_call_ppp_output;
	ppp->phy_close = pptp_call_closed_by_ppp;

	strlcpy(ppp->phy_label, PPTP_CTRL_LISTENER_TUN_NAME(_this->ctrl),
	    sizeof(ppp->phy_label));

	PPTP_CALL_ASSERT(sizeof(ppp->phy_info) >= _this->ctrl->peer.ss_len);
	memcpy(&ppp->phy_info, &_this->ctrl->peer,
	    MINIMUM(sizeof(ppp->phy_info), _this->ctrl->peer.ss_len));

	if (ppp_init(npppd_get_npppd(), ppp) != 0)
		goto fail;

	pptp_call_log(_this, LOG_NOTICE, "logtype=PPPBind ppp=%d", ppp->id);
	ppp_start(ppp);

	return 0;
fail:
	pptp_call_log(_this, LOG_ERR, "failed binding ppp");

	if (ppp != NULL)
		ppp_destroy(ppp);
	_this->ppp = NULL;

	return 1;
}

/*
 * utility functions
 */

/* logging with the label for the instance */
static void
pptp_call_log(pptp_call *_this, int prio, const char *fmt, ...)
{
	char logbuf[BUFSIZ];
	va_list ap;

	va_start(ap, fmt);
#ifdef	PPTPD_MULTIPLE
	snprintf(logbuf, sizeof(logbuf), "pptpd id=%u ctrl=%u call=%u %s",
	    _this->ctrl->pptpd->id, _this->ctrl->id, _this->id, fmt);
#else
	snprintf(logbuf, sizeof(logbuf), "pptpd ctrl=%u call=%u %s",
	    _this->ctrl->id, _this->id, fmt);
#endif
	vlog_printf(prio, logbuf, ap);
	va_end(ap);
}

/* convert Outgoing-Call-Request packet to strings */
static void
pptp_call_OCRQ_string(struct pptp_ocrq *ocrq, char *buf, int lbuf)
{
	snprintf(buf, lbuf,
	    "call_id=%u call_serial_number=%u max_bps=%u min_bps=%u bearer=%s "
	    "framing=%s recv_winsz=%u packet_proccessing_delay=%u "
	    "phone_number=%.*s subaddress=%.*s",
	    ocrq->call_id, ocrq->call_serial_number, ocrq->maximum_bps,
	    ocrq->minimum_bps, pptp_bearer_string(ocrq->bearer_type),
	    pptp_framing_string(ocrq->framing_type), ocrq->recv_winsz,
	    ocrq->packet_proccessing_delay,
	    (u_int)sizeof(ocrq->phone_number), ocrq->phone_number,
	    (u_int)sizeof(ocrq->subaddress), ocrq->subaddress);
}

/* convert Outgoing-Call-Reply packet to strings */
void
pptp_call_OCRP_string(struct pptp_ocrp *ocrp, char *buf, int lbuf)
{
	snprintf(buf, lbuf,
	    "call_id=%u peers_call_id=%u result=%u error=%u cause=%u "
	    "conn_speed=%u recv_winsz=%u packet_proccessing_delay=%u "
	    "physical_channel_id=%u",
	    ocrp->call_id, ocrp->peers_call_id, ocrp->result_code,
	    ocrp->error_code, ocrp->cause_code, ocrp->connect_speed,
	    ocrp->recv_winsz, ocrp->packet_proccessing_delay,
	    ocrp->physical_channel_id);
}

static char *
pptp_call_state_string(int state)
{
	switch (state) {
	case PPTP_CALL_STATE_IDLE:
		return "idle";
	case PPTP_CALL_STATE_WAIT_CONN:
		return "wait-conn";
	case PPTP_CALL_STATE_ESTABLISHED:
		return "established";
	case PPTP_CALL_STATE_CLEANUP_WAIT:
		return "cleanup-wait";
	}
	return "unknown";
}

