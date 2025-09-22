/*	$OpenBSD: nat_traversal.c,v 1.25 2017/12/05 20:31:45 jca Exp $	*/

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
#include "exchange.h"
#include "hash.h"
#include "ipsec.h"
#include "isakmp_fld.h"
#include "isakmp_num.h"
#include "ipsec_num.h"
#include "log.h"
#include "message.h"
#include "nat_traversal.h"
#include "prf.h"
#include "sa.h"
#include "timer.h"
#include "transport.h"
#include "util.h"
#include "virtual.h"

int	disable_nat_t = 0;

/*
 * NAT-T capability of the other peer is determined by a particular vendor
 * ID sent in the first message. This vendor ID string is supposed to be a
 * MD5 hash of "RFC 3947".
 *
 * These seem to be the "well" known variants of this string in use by
 * products today.
 *
 * Note that the VID specified in draft 2 is ambiguous: It was
 * accidentally calculated from the string "draft-ietf-ipsec-nat-t-ike-02\n"
 * although the string was documented without the trailing '\n'. The authors
 * suggested afterwards to use the string with the trailing '\n'.
 */

static struct nat_t_cap isakmp_nat_t_cap[] = {
	{ VID_DRAFT_V2_N, EXCHANGE_FLAG_NAT_T_DRAFT,
	  "draft-ietf-ipsec-nat-t-ike-02\n", NULL, 0 },
	{ VID_DRAFT_V3, EXCHANGE_FLAG_NAT_T_DRAFT,
	  "draft-ietf-ipsec-nat-t-ike-03", NULL, 0 },
	{ VID_RFC3947, EXCHANGE_FLAG_NAT_T_RFC,
	  "RFC 3947", NULL, 0 },
};

#define NUMNATTCAP	(sizeof isakmp_nat_t_cap / sizeof isakmp_nat_t_cap[0])

/* In seconds. Recommended in draft-ietf-ipsec-udp-encaps-09.  */
#define NAT_T_KEEPALIVE_INTERVAL	20

static int	nat_t_setup_hashes(void);
static int	nat_t_add_vendor_payload(struct message *, struct nat_t_cap *);
static int	nat_t_add_nat_d(struct message *, struct sockaddr *);
static int	nat_t_match_nat_d_payload(struct message *, struct sockaddr *);

void
nat_t_init(void)
{
	nat_t_setup_hashes();
}

/* Generate the NAT-T capability marker hashes. Executed only once.  */
static int
nat_t_setup_hashes(void)
{
	struct hash *hash;
	int n = NUMNATTCAP;
	int i;

	/* The draft says to use MD5.  */
	hash = hash_get(HASH_MD5);
	if (!hash) {
		/* Should never happen.  */
		log_print("nat_t_setup_hashes: "
		    "could not find MD5 hash structure!");
		return -1;
	}

	/* Populate isakmp_nat_t_cap with hashes.  */
	for (i = 0; i < n; i++) {
		isakmp_nat_t_cap[i].hashsize = hash->hashsize;
		isakmp_nat_t_cap[i].hash = malloc(hash->hashsize);
		if (!isakmp_nat_t_cap[i].hash) {
			log_error("nat_t_setup_hashes: malloc (%lu) failed",
			    (unsigned long)hash->hashsize);
			goto errout;
		}

		hash->Init(hash->ctx);
		hash->Update(hash->ctx,
		    (unsigned char *)isakmp_nat_t_cap[i].text,
		    strlen(isakmp_nat_t_cap[i].text));
		hash->Final(isakmp_nat_t_cap[i].hash, hash->ctx);

		LOG_DBG((LOG_EXCHANGE, 50, "nat_t_setup_hashes: "
		    "MD5(\"%s\") (%lu bytes)", isakmp_nat_t_cap[i].text,
		    (unsigned long)hash->hashsize));
		LOG_DBG_BUF((LOG_EXCHANGE, 50, "nat_t_setup_hashes",
		    isakmp_nat_t_cap[i].hash, hash->hashsize));
	}

	return 0;

errout:
	for (i = 0; i < n; i++)
		free(isakmp_nat_t_cap[i].hash);
	return -1;
}

/* Add one NAT-T VENDOR payload.  */
static int
nat_t_add_vendor_payload(struct message *msg, struct nat_t_cap *cap)
{
	size_t	  buflen = cap->hashsize + ISAKMP_GEN_SZ;
	u_int8_t *buf;

	if (disable_nat_t)
		return 0;

	buf = malloc(buflen);
	if (!buf) {
		log_error("nat_t_add_vendor_payload: malloc (%lu) failed",
		    (unsigned long)buflen);
		return -1;
	}

	SET_ISAKMP_GEN_LENGTH(buf, buflen);
	memcpy(buf + ISAKMP_VENDOR_ID_OFF, cap->hash, cap->hashsize);
	if (message_add_payload(msg, ISAKMP_PAYLOAD_VENDOR, buf, buflen, 1)) {
		free(buf);
		return -1;
	}
	return 0;
}

/* Add the NAT-T capability markers (VENDOR payloads).  */
int
nat_t_add_vendor_payloads(struct message *msg)
{
	int i;

	if (disable_nat_t)
		return 0;

	for (i = 0; i < NUMNATTCAP; i++)
		if (nat_t_add_vendor_payload(msg, &isakmp_nat_t_cap[i]))
			return -1;
	return 0;
}

/*
 * Check an incoming message for NAT-T capability markers.
 */
void
nat_t_check_vendor_payload(struct message *msg, struct payload *p)
{
	u_int8_t *pbuf = p->p;
	size_t	  vlen;
	int	  i;

	if (disable_nat_t)
		return;

	vlen = GET_ISAKMP_GEN_LENGTH(pbuf) - ISAKMP_GEN_SZ;

	for (i = 0; i < NUMNATTCAP; i++) {
		if (vlen != isakmp_nat_t_cap[i].hashsize) {
			continue;
		}
		if (memcmp(isakmp_nat_t_cap[i].hash, pbuf + ISAKMP_GEN_SZ,
		    vlen) == 0) {
			/* This peer is NAT-T capable.  */
			msg->exchange->flags |= EXCHANGE_FLAG_NAT_T_CAP_PEER;
			msg->exchange->flags |= isakmp_nat_t_cap[i].flags;
			LOG_DBG((LOG_EXCHANGE, 10,
			    "nat_t_check_vendor_payload: "
			    "NAT-T capable peer detected"));
			p->flags |= PL_MARK;
		}
	}

	return;
}

/* Generate the NAT-D payload hash : HASH(CKY-I | CKY-R | IP | Port).  */
static u_int8_t *
nat_t_generate_nat_d_hash(struct message *msg, struct sockaddr *sa,
    size_t *hashlen)
{
	struct ipsec_exch *ie = (struct ipsec_exch *)msg->exchange->data;
	struct hash	 *hash;
	u_int8_t	 *res;
	in_port_t	  port;

	hash = hash_get(ie->hash->type);
	if (hash == NULL) {
		log_print ("nat_t_generate_nat_d_hash: no hash");
		return NULL;
	}

	*hashlen = hash->hashsize;

	res = malloc(*hashlen);
	if (!res) {
		log_print("nat_t_generate_nat_d_hash: malloc (%lu) failed",
		    (unsigned long)*hashlen);
		*hashlen = 0;
		return NULL;
	}

	port = sockaddr_port(sa);
	bzero(res, *hashlen);

	hash->Init(hash->ctx);
	hash->Update(hash->ctx, msg->exchange->cookies,
	    sizeof msg->exchange->cookies);
	hash->Update(hash->ctx, sockaddr_addrdata(sa), sockaddr_addrlen(sa));
	hash->Update(hash->ctx, (unsigned char *)&port, sizeof port);
	hash->Final(res, hash->ctx);
	return res;
}

/* Add a NAT-D payload to our message.  */
static int
nat_t_add_nat_d(struct message *msg, struct sockaddr *sa)
{
	int	  ret;
	u_int8_t *hbuf, *buf;
	size_t	  hbuflen, buflen;

	hbuf = nat_t_generate_nat_d_hash(msg, sa, &hbuflen);
	if (!hbuf) {
		log_print("nat_t_add_nat_d: NAT-D hash gen failed");
		return -1;
	}

	buflen = ISAKMP_NAT_D_DATA_OFF + hbuflen;
	buf = malloc(buflen);
	if (!buf) {
		log_error("nat_t_add_nat_d: malloc (%lu) failed",
		    (unsigned long)buflen);
		free(hbuf);
		return -1;
	}

	SET_ISAKMP_GEN_LENGTH(buf, buflen);
	memcpy(buf + ISAKMP_NAT_D_DATA_OFF, hbuf, hbuflen);
	free(hbuf);

	if (msg->exchange->flags & EXCHANGE_FLAG_NAT_T_RFC)
		ret = message_add_payload(msg, ISAKMP_PAYLOAD_NAT_D, buf,
		    buflen, 1);
	else if (msg->exchange->flags & EXCHANGE_FLAG_NAT_T_DRAFT)
		ret = message_add_payload(msg, ISAKMP_PAYLOAD_NAT_D_DRAFT,
		    buf, buflen, 1);
	else
		ret = -1;
		
	if (ret) {
		free(buf);
		return -1;
	}
	return 0;
}

/* We add two NAT-D payloads, one each for src and dst.  */
int
nat_t_exchange_add_nat_d(struct message *msg)
{
	struct sockaddr *sa;

	/* Remote address first. */
	msg->transport->vtbl->get_dst(msg->transport, &sa);
	if (nat_t_add_nat_d(msg, sa))
		return -1;

	msg->transport->vtbl->get_src(msg->transport, &sa);
	if (nat_t_add_nat_d(msg, sa))
		return -1;
	return 0;
}

/* Generate and match a NAT-D hash against the NAT-D payload (pl.) data.  */
static int
nat_t_match_nat_d_payload(struct message *msg, struct sockaddr *sa)
{
	struct payload *p;
	u_int8_t *hbuf;
	size_t	 hbuflen;
	int	 found = 0;

	/*
	 * If there are no NAT-D payloads in the message, return "found"
	 * as this will avoid NAT-T (see nat_t_exchange_check_nat_d()).
	 */
	if ((p = payload_first(msg, ISAKMP_PAYLOAD_NAT_D_DRAFT)) == NULL &&
	    (p = payload_first(msg, ISAKMP_PAYLOAD_NAT_D)) == NULL)
		return 1;

	hbuf = nat_t_generate_nat_d_hash(msg, sa, &hbuflen);
	if (!hbuf)
		return 0;

	for (; p; p = TAILQ_NEXT(p, link)) {
		if (GET_ISAKMP_GEN_LENGTH (p->p) !=
		    hbuflen + ISAKMP_NAT_D_DATA_OFF)
			continue;

		if (memcmp(p->p + ISAKMP_NAT_D_DATA_OFF, hbuf, hbuflen) == 0) {
			found++;
			break;
		}
	}
	free(hbuf);
	return found;
}

/*
 * Check if we need to activate NAT-T, and if we need to send keepalive
 * messages to the other side, i.e if we are a nat:ed peer.
 */
int
nat_t_exchange_check_nat_d(struct message *msg)
{
	struct sockaddr *sa;
	int	 outgoing_path_is_clear, incoming_path_is_clear;

	/* Assume trouble, i.e NAT-boxes in our path.  */
	outgoing_path_is_clear = incoming_path_is_clear = 0;

	msg->transport->vtbl->get_src(msg->transport, &sa);
	if (nat_t_match_nat_d_payload(msg, sa))
		outgoing_path_is_clear = 1;

	msg->transport->vtbl->get_dst(msg->transport, &sa);
	if (nat_t_match_nat_d_payload(msg, sa))
		incoming_path_is_clear = 1;

	if (outgoing_path_is_clear && incoming_path_is_clear) {
		LOG_DBG((LOG_EXCHANGE, 40, "nat_t_exchange_check_nat_d: "
		    "no NAT"));
		return 0; /* No NAT-T required.  */
	}

	/* NAT-T handling required.  */
	msg->exchange->flags |= EXCHANGE_FLAG_NAT_T_ENABLE;

	if (!outgoing_path_is_clear) {
		msg->exchange->flags |= EXCHANGE_FLAG_NAT_T_KEEPALIVE;
		LOG_DBG((LOG_EXCHANGE, 10, "nat_t_exchange_check_nat_d: "
		    "NAT detected, we're behind it"));
	} else
		LOG_DBG ((LOG_EXCHANGE, 10,
		    "nat_t_exchange_check_nat_d: NAT detected"));
	return 1;
}

static void
nat_t_send_keepalive(void *v_arg)
{
	struct sa *sa = (struct sa *)v_arg;
	struct transport *t;
	struct timespec now;
	int interval;

	/* Send the keepalive message.  */
	t = ((struct virtual_transport *)sa->transport)->encap;
	t->vtbl->send_message(NULL, t);

	/* Set new timer.  */
	interval = conf_get_num("General", "NAT-T-Keepalive", 0);
	if (interval < 1)
		interval = NAT_T_KEEPALIVE_INTERVAL;
	clock_gettime(CLOCK_MONOTONIC, &now);
	now.tv_sec += interval;

	sa->nat_t_keepalive = timer_add_event("nat_t_send_keepalive",
	    nat_t_send_keepalive, v_arg, &now);
	if (!sa->nat_t_keepalive)
		log_print("nat_t_send_keepalive: "
		    "timer_add_event() failed, will send no more keepalives");
}

void
nat_t_setup_keepalive(struct sa *sa)
{
	struct sockaddr *src;
	struct timespec now;

	if (sa->initiator)
		sa->transport->vtbl->get_src(sa->transport, &src);
	else
		sa->transport->vtbl->get_dst(sa->transport, &src);

	if (!virtual_listen_lookup(src))
		return;

	clock_gettime(CLOCK_MONOTONIC, &now);
	now.tv_sec += NAT_T_KEEPALIVE_INTERVAL;

	sa->nat_t_keepalive = timer_add_event("nat_t_send_keepalive",
	    nat_t_send_keepalive, sa, &now);
	if (!sa->nat_t_keepalive)
		log_print("nat_t_setup_keepalive: "
		    "timer_add_event() failed, will not send keepalives");

	LOG_DBG((LOG_TRANSPORT, 50, "nat_t_setup_keepalive: "
	    "added event for phase 1 SA %p", sa));
}
