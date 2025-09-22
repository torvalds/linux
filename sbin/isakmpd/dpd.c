/*	$OpenBSD: dpd.c,v 1.20 2017/12/05 20:31:45 jca Exp $	*/

/*
 * Copyright (c) 2004 Håkan Olsson.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

#include "conf.h"
#include "dpd.h"
#include "exchange.h"
#include "hash.h"
#include "ipsec.h"
#include "isakmp_fld.h"
#include "log.h"
#include "message.h"
#include "pf_key_v2.h"
#include "sa.h"
#include "timer.h"
#include "transport.h"
#include "util.h"

/* From RFC 3706.  */
#define DPD_MAJOR		0x01
#define DPD_MINOR		0x00
#define DPD_SEQNO_SZ		4

static const u_int8_t dpd_vendor_id[] = {
	0xAF, 0xCA, 0xD7, 0x13, 0x68, 0xA1, 0xF1,	/* RFC 3706 */
	0xC9, 0x6B, 0x86, 0x96, 0xFC, 0x77, 0x57,
	DPD_MAJOR,
	DPD_MINOR
};

#define DPD_RETRANS_MAX		5	/* max number of retries.  */
#define DPD_RETRANS_WAIT	5	/* seconds between retries.  */

/* DPD Timer State */
enum dpd_tstate { DPD_TIMER_NORMAL, DPD_TIMER_CHECK };

static void	 dpd_check_event(void *);
static void	 dpd_event(void *);
static u_int32_t dpd_timer_interval(u_int32_t);
static void	 dpd_timer_reset(struct sa *, u_int32_t, enum dpd_tstate);

/* Add the DPD VENDOR ID payload.  */
int
dpd_add_vendor_payload(struct message *msg)
{
	u_int8_t *buf;
	size_t buflen = sizeof dpd_vendor_id + ISAKMP_GEN_SZ;

	buf = malloc(buflen);
	if (!buf) {
		log_error("dpd_add_vendor_payload: malloc(%lu) failed",
		    (unsigned long)buflen);
		return -1;
	}

	SET_ISAKMP_GEN_LENGTH(buf, buflen);
	memcpy(buf + ISAKMP_VENDOR_ID_OFF, dpd_vendor_id,
	    sizeof dpd_vendor_id);
	if (message_add_payload(msg, ISAKMP_PAYLOAD_VENDOR, buf, buflen, 1)) {
		free(buf);
		return -1;
	}

	return 0;
}

/*
 * Check an incoming message for DPD capability markers.
 */
void
dpd_check_vendor_payload(struct message *msg, struct payload *p)
{
	u_int8_t *pbuf = p->p;
	size_t vlen;

	/* Already checked? */
	if (msg->exchange->flags & EXCHANGE_FLAG_DPD_CAP_PEER) {
		/* Just mark it as handled and return.  */
		p->flags |= PL_MARK;
		return;
	}

	vlen = GET_ISAKMP_GEN_LENGTH(pbuf) - ISAKMP_GEN_SZ;
	if (vlen != sizeof dpd_vendor_id) {
		LOG_DBG((LOG_EXCHANGE, 90,
		    "dpd_check_vendor_payload: bad size %lu != %lu",
		    (unsigned long)vlen, (unsigned long)sizeof dpd_vendor_id));
		return;
	}

	if (memcmp(dpd_vendor_id, pbuf + ISAKMP_GEN_SZ, vlen) == 0) {
		/* This peer is DPD capable.  */
		if (msg->isakmp_sa) {
			msg->exchange->flags |= EXCHANGE_FLAG_DPD_CAP_PEER;
			LOG_DBG((LOG_EXCHANGE, 10, "dpd_check_vendor_payload: "
			    "DPD capable peer detected"));
		}
		p->flags |= PL_MARK;
	}
}

/*
 * Arm the DPD timer
 */
void
dpd_start(struct sa *isakmp_sa)
{
	if (dpd_timer_interval(0) != 0) {
		LOG_DBG((LOG_EXCHANGE, 10, "dpd_enable: enabling"));
		isakmp_sa->flags |= SA_FLAG_DPD;
		dpd_timer_reset(isakmp_sa, 0, DPD_TIMER_NORMAL);
	}
}

/*
 * All incoming DPD Notify messages enter here. Message has been validated.
 */
void
dpd_handle_notify(struct message *msg, struct payload *p)
{
	struct sa	*isakmp_sa = msg->isakmp_sa;
	u_int16_t	 notify = GET_ISAKMP_NOTIFY_MSG_TYPE(p->p);
	u_int32_t	 p_seq;

	/* Extract the sequence number.  */
	memcpy(&p_seq, p->p + ISAKMP_NOTIFY_SPI_OFF + ISAKMP_HDR_COOKIES_LEN,
	    sizeof p_seq);
	p_seq = ntohl(p_seq);

	LOG_DBG((LOG_MESSAGE, 40, "dpd_handle_notify: got %s seq %u",
	    constant_name(isakmp_notify_cst, notify), p_seq));

	switch (notify) {
	case ISAKMP_NOTIFY_STATUS_DPD_R_U_THERE:
		/* The other peer wants to know we're alive.  */
		if (p_seq < isakmp_sa->dpd_rseq ||
		    (p_seq == isakmp_sa->dpd_rseq &&
		    ++isakmp_sa->dpd_rdupcount >= DPD_RETRANS_MAX)) {
			log_print("dpd_handle_notify: bad R_U_THERE seqno "
			    "%u <= %u", p_seq, isakmp_sa->dpd_rseq);
			return;
		}
		if (isakmp_sa->dpd_rseq != p_seq) {
			isakmp_sa->dpd_rdupcount = 0;
			isakmp_sa->dpd_rseq = p_seq;
		}
		message_send_dpd_notify(isakmp_sa,
		    ISAKMP_NOTIFY_STATUS_DPD_R_U_THERE_ACK, p_seq);
		break;

	case ISAKMP_NOTIFY_STATUS_DPD_R_U_THERE_ACK:
		/* This should be a response to a R_U_THERE we've sent.  */
		if (isakmp_sa->dpd_seq != p_seq) {
			log_print("dpd_handle_notify: got bad ACK seqno %u, "
			    "expected %u", p_seq, isakmp_sa->dpd_seq);
			/* XXX Give up? Retry? */
			return;
		}
		break;
	default:
		break;
	}

	/* Mark handled.  */
	p->flags |= PL_MARK;

	/* The other peer is alive, so we can safely wait a while longer.  */
	if (isakmp_sa->flags & SA_FLAG_DPD)
		dpd_timer_reset(isakmp_sa, 0, DPD_TIMER_NORMAL);
}

/* Calculate the time until next DPD exchange.  */
static u_int32_t
dpd_timer_interval(u_int32_t offset)
{
	int32_t v = 0;

#ifdef notyet
	v = ...; /* XXX Per-peer specified DPD intervals?  */
#endif
	if (!v)
		v = conf_get_num("General", "DPD-check-interval", 0);
	if (v < 1)
		return 0;	/* DPD-Check-Interval < 1 means disable DPD */

	v -= offset;
	return v < 1 ? 1 : v;
}

static void
dpd_timer_reset(struct sa *sa, u_int32_t time_passed, enum dpd_tstate mode)
{
	struct timespec	ts;

	if (sa->dpd_event)
		timer_remove_event(sa->dpd_event);

	clock_gettime(CLOCK_MONOTONIC, &ts);
	switch (mode) {
	case DPD_TIMER_NORMAL:
		sa->dpd_failcount = 0;
		ts.tv_sec += dpd_timer_interval(time_passed);
		sa->dpd_event = timer_add_event("dpd_event", dpd_event, sa,
		    &ts);
		break;
	case DPD_TIMER_CHECK:
		ts.tv_sec += DPD_RETRANS_WAIT;
		sa->dpd_event = timer_add_event("dpd_check_event",
		    dpd_check_event, sa, &ts);
		break;
	default:
		break;
	}
	if (!sa->dpd_event)
		log_print("dpd_timer_reset: timer_add_event failed");
}

/* Helper function for dpd_exchange_finalization().  */
static int
dpd_find_sa(struct sa *sa, void *v_sa)
{
	struct sa	*isakmp_sa = v_sa;

	if (!isakmp_sa->id_i || !isakmp_sa->id_r)
		return 0;
	return (sa->phase == 2 && (sa->flags & SA_FLAG_READY) &&
	    memcmp(sa->id_i, isakmp_sa->id_i, sa->id_i_len) == 0 &&
	    memcmp(sa->id_r, isakmp_sa->id_r, sa->id_r_len) == 0);
}

struct dpd_args {
	struct sa	*isakmp_sa;
	u_int32_t	 interval;
};

/* Helper function for dpd_event().  */
static int
dpd_check_time(struct sa *sa, void *v_arg)
{
	struct dpd_args *args = v_arg;
	struct sockaddr *dst;
	struct proto *proto;
	struct sa_kinfo *ksa;
	struct timespec ts;

	if (sa->phase == 1 || (args->isakmp_sa->flags & SA_FLAG_DPD) == 0 ||
	    dpd_find_sa(sa, args->isakmp_sa) == 0)
		return 0;

	proto = TAILQ_FIRST(&sa->protos);
	if (!proto || !proto->data)
		return 0;
	sa->transport->vtbl->get_src(sa->transport, &dst);

	clock_gettime(CLOCK_MONOTONIC, &ts);
	ksa = pf_key_v2_get_kernel_sa(proto->spi[1], proto->spi_sz[1],
	    proto->proto, dst);

	if (!ksa || !ksa->last_used)
		return 0;

	LOG_DBG((LOG_MESSAGE, 80, "dpd_check_time: "
	    "SA %p last use %u second(s) ago", sa,
	    (u_int32_t)(ts.tv_sec - ksa->last_used)));

	if ((u_int32_t)(ts.tv_sec - ksa->last_used) < args->interval) {
		args->interval = (u_int32_t)(ts.tv_sec - ksa->last_used);
		return 1;
	}
	return 0;
}

/* Called by the timer.  */
static void
dpd_event(void *v_sa)
{
	struct sa	*isakmp_sa = v_sa;
	struct dpd_args args;
	struct sockaddr *dst;
	char *addr;

	isakmp_sa->dpd_event = 0;

	/* Check if there's been any incoming SA activity since last time.  */
	args.isakmp_sa = isakmp_sa;
	args.interval = dpd_timer_interval(0);
	if (sa_find(dpd_check_time, &args)) {
		if (args.interval > dpd_timer_interval(0))
			args.interval = 0;
		dpd_timer_reset(isakmp_sa, args.interval, DPD_TIMER_NORMAL);
		return;
	}

	/* No activity seen, do a DPD exchange.  */
	if (isakmp_sa->dpd_seq == 0) {
		/*
		 * RFC 3706: first seq# should be random, with MSB zero,
		 * otherwise we just increment it.
		 */
		arc4random_buf((u_int8_t *)&isakmp_sa->dpd_seq,
		    sizeof isakmp_sa->dpd_seq);
		isakmp_sa->dpd_seq &= 0x7FFF;
	} else
		isakmp_sa->dpd_seq++;

	isakmp_sa->transport->vtbl->get_dst(isakmp_sa->transport, &dst);
	if (sockaddr2text(dst, &addr, 0) == -1)
		addr = 0;
	LOG_DBG((LOG_MESSAGE, 30, "dpd_event: sending R_U_THERE to %s seq %u",
	    addr ? addr : "<unknown>", isakmp_sa->dpd_seq));
	free(addr);
	message_send_dpd_notify(isakmp_sa, ISAKMP_NOTIFY_STATUS_DPD_R_U_THERE,
	    isakmp_sa->dpd_seq);

	/* And set the short timer.  */
	dpd_timer_reset(isakmp_sa, 0, DPD_TIMER_CHECK);
}

/*
 * Called by the timer. If this function is called, it means we did not
 * received any R_U_THERE_ACK confirmation from the other peer.
 */
static void
dpd_check_event(void *v_sa)
{
	struct sa	*isakmp_sa = v_sa;
	struct sa	*sa;

	isakmp_sa->dpd_event = 0;

	if (++isakmp_sa->dpd_failcount < DPD_RETRANS_MAX) {
		LOG_DBG((LOG_MESSAGE, 10, "dpd_check_event: "
		    "peer not responding, retry %u of %u",
		    isakmp_sa->dpd_failcount, DPD_RETRANS_MAX));
		message_send_dpd_notify(isakmp_sa,
		    ISAKMP_NOTIFY_STATUS_DPD_R_U_THERE, isakmp_sa->dpd_seq);
		dpd_timer_reset(isakmp_sa, 0, DPD_TIMER_CHECK);
		return;
	}

	/*
	 * Peer is considered dead. Delete all SAs created under isakmp_sa.
	 */
	LOG_DBG((LOG_MESSAGE, 10, "dpd_check_event: peer is dead, "
	    "deleting all SAs connected to SA %p", isakmp_sa));
	while ((sa = sa_find(dpd_find_sa, isakmp_sa)) != 0) {
		LOG_DBG((LOG_MESSAGE, 30, "dpd_check_event: deleting SA %p",
		    sa));
		sa_delete(sa, 0);
	}
	LOG_DBG((LOG_MESSAGE, 30, "dpd_check_event: deleting ISAKMP SA %p",
	    isakmp_sa));
	sa_delete(isakmp_sa, 0);
}
