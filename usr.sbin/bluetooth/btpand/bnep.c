/*	$NetBSD: bnep.c,v 1.1 2008/08/17 13:20:57 plunky Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2008 Iain Hibbert
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

/* $FreeBSD$ */

#include <sys/cdefs.h>
__RCSID("$NetBSD: bnep.c,v 1.1 2008/08/17 13:20:57 plunky Exp $");

#include <sys/uio.h>
#define L2CAP_SOCKET_CHECKED
#include <bluetooth.h>
#include <sdp.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#include "btpand.h"
#include "bnep.h"

static bool bnep_recv_extension(packet_t *);
static size_t bnep_recv_control(channel_t *, uint8_t *, size_t, bool);
static size_t bnep_recv_control_command_not_understood(channel_t *, uint8_t *, size_t);
static size_t bnep_recv_setup_connection_req(channel_t *, uint8_t *, size_t);
static size_t bnep_recv_setup_connection_rsp(channel_t *, uint8_t *, size_t);
static size_t bnep_recv_filter_net_type_set(channel_t *, uint8_t *, size_t);
static size_t bnep_recv_filter_net_type_rsp(channel_t *, uint8_t *, size_t);
static size_t bnep_recv_filter_multi_addr_set(channel_t *, uint8_t *, size_t);
static size_t bnep_recv_filter_multi_addr_rsp(channel_t *, uint8_t *, size_t);

static bool bnep_pfilter(channel_t *, packet_t *);
static bool bnep_mfilter(channel_t *, packet_t *);

static uint8_t NAP_UUID[] = {
	0x00, 0x00, 0x11, 0x16,
	0x00, 0x00,
	0x10, 0x00,
	0x80, 0x00,
	0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb
};

static uint8_t GN_UUID[] = {
	0x00, 0x00, 0x11, 0x17,
	0x00, 0x00,
	0x10, 0x00,
	0x80, 0x00,
	0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb,
};

static uint8_t PANU_UUID[] = {
	0x00, 0x00, 0x11, 0x15,
	0x00, 0x00,
	0x10, 0x00,
	0x80, 0x00,
	0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb
};

/*
 * receive BNEP packet
 * return true if packet is to be forwarded
 */
bool
bnep_recv(packet_t *pkt)
{
	size_t len;
	uint8_t type;

	if (pkt->len < 1)
		return false;

	type = pkt->ptr[0];
	packet_adj(pkt, 1);

	switch (BNEP_TYPE(type)) {
	case BNEP_GENERAL_ETHERNET:
		if (pkt->len < (ETHER_ADDR_LEN * 2) + ETHER_TYPE_LEN) {
			log_debug("dropped short packet (type 0x%2.2x)", type);
			return false;
		}

		pkt->dst = pkt->ptr;
		packet_adj(pkt, ETHER_ADDR_LEN);
		pkt->src = pkt->ptr;
		packet_adj(pkt, ETHER_ADDR_LEN);
		pkt->type = pkt->ptr;
		packet_adj(pkt, ETHER_TYPE_LEN);
		break;

	case BNEP_CONTROL:
		len = bnep_recv_control(pkt->chan, pkt->ptr, pkt->len, false);
		if (len == 0)
			return false;

		packet_adj(pkt, len);
		break;

	case BNEP_COMPRESSED_ETHERNET:
		if (pkt->len < ETHER_TYPE_LEN) {
			log_debug("dropped short packet (type 0x%2.2x)", type);
			return false;
		}

		pkt->dst = pkt->chan->laddr;
		pkt->src = pkt->chan->raddr;
		pkt->type = pkt->ptr;
		packet_adj(pkt, ETHER_TYPE_LEN);
		break;

	case BNEP_COMPRESSED_ETHERNET_SRC_ONLY:
		if (pkt->len < ETHER_ADDR_LEN + ETHER_TYPE_LEN) {
			log_debug("dropped short packet (type 0x%2.2x)", type);
			return false;
		}

		pkt->dst = pkt->chan->laddr;
		pkt->src = pkt->ptr;
		packet_adj(pkt, ETHER_ADDR_LEN);
		pkt->type = pkt->ptr;
		packet_adj(pkt, ETHER_TYPE_LEN);
		break;

	case BNEP_COMPRESSED_ETHERNET_DST_ONLY:
		if (pkt->len < ETHER_ADDR_LEN + ETHER_TYPE_LEN) {
			log_debug("dropped short packet (type 0x%2.2x)", type);
			return false;
		}

		pkt->dst = pkt->ptr;
		packet_adj(pkt, ETHER_ADDR_LEN);
		pkt->src = pkt->chan->raddr;
		pkt->type = pkt->ptr;
		packet_adj(pkt, ETHER_TYPE_LEN);
		break;

	default:
		/*
		 * Any packet containing a reserved BNEP
		 * header packet type SHALL be dropped.
		 */

		log_debug("dropped packet with reserved type 0x%2.2x", type);
		return false;
	}

	if (BNEP_TYPE_EXT(type)
	    && !bnep_recv_extension(pkt))
		return false;	/* invalid extensions */

	if (BNEP_TYPE(type) == BNEP_CONTROL
	    || pkt->chan->state != CHANNEL_OPEN)
		return false;	/* no forwarding */

	return true;
}

static bool
bnep_recv_extension(packet_t *pkt)
{
	exthdr_t *eh;
	size_t len, size;
	uint8_t type;

	do {
		if (pkt->len < 2)
			return false;

		type = pkt->ptr[0];
		size = pkt->ptr[1];

		if (pkt->len < size + 2)
			return false;

		switch (type) {
		case BNEP_EXTENSION_CONTROL:
			len = bnep_recv_control(pkt->chan, pkt->ptr + 2, size, true);
			if (len != size)
				log_err("ignored spurious data in exthdr");

			break;

		default:
			/* Unknown extension headers in data packets	 */
			/* SHALL be forwarded irrespective of any	 */
			/* network protocol or multicast filter settings */
			/* and any local filtering policy.		 */

			eh = malloc(sizeof(exthdr_t));
			if (eh == NULL) {
				log_err("exthdr malloc() failed: %m");
				break;
			}

			eh->ptr = pkt->ptr;
			eh->len = size;
			STAILQ_INSERT_TAIL(&pkt->extlist, eh, next);
			break;
		}

		packet_adj(pkt, size + 2);
	} while (BNEP_TYPE_EXT(type));

	return true;
}

static size_t
bnep_recv_control(channel_t *chan, uint8_t *ptr, size_t size, bool isext)
{
	uint8_t type;
	size_t len;

	if (size-- < 1)
		return 0;

	type = *ptr++;

	switch (type) {
	case BNEP_CONTROL_COMMAND_NOT_UNDERSTOOD:
		len = bnep_recv_control_command_not_understood(chan, ptr, size);
		break;

	case BNEP_SETUP_CONNECTION_REQUEST:
		if (isext)
			return 0;	/* not allowed in extension headers */

		len = bnep_recv_setup_connection_req(chan, ptr, size);
		break;

	case BNEP_SETUP_CONNECTION_RESPONSE:
		if (isext)
			return 0;	/* not allowed in extension headers */

		len = bnep_recv_setup_connection_rsp(chan, ptr, size);
		break;

	case BNEP_FILTER_NET_TYPE_SET:
		len = bnep_recv_filter_net_type_set(chan, ptr, size);
		break;

	case BNEP_FILTER_NET_TYPE_RESPONSE:
		len = bnep_recv_filter_net_type_rsp(chan, ptr, size);
		break;

	case BNEP_FILTER_MULTI_ADDR_SET:
		len = bnep_recv_filter_multi_addr_set(chan, ptr, size);
		break;

	case BNEP_FILTER_MULTI_ADDR_RESPONSE:
		len = bnep_recv_filter_multi_addr_rsp(chan, ptr, size);
		break;

	default:
		len = 0;
		break;
	}

	if (len == 0)
		bnep_send_control(chan, BNEP_CONTROL_COMMAND_NOT_UNDERSTOOD, type);

	return len;
}

static size_t
bnep_recv_control_command_not_understood(channel_t *chan, uint8_t *ptr, size_t size)
{
	uint8_t type;

	if (size < 1)
		return 0;

	type = *ptr++;
	log_err("received Control Command Not Understood (0x%2.2x)", type);

	/* we didn't send any reserved commands, just cut them off */
	channel_close(chan);

	return 1;
}

static size_t
bnep_recv_setup_connection_req(channel_t *chan, uint8_t *ptr, size_t size)
{
	uint8_t off;
	int src, dst, rsp;
	size_t len;

	if (size < 1)
		return 0;

	len = *ptr++;
	if (size < (len * 2 + 1))
		return 0;

	if (chan->state != CHANNEL_WAIT_CONNECT_REQ
	    && chan->state != CHANNEL_OPEN) {
		log_debug("ignored");
		return (len * 2 + 1);
	}

	if (len == 2)
		off = 2;
	else if (len == 4)
		off = 0;
	else if (len == 16)
		off = 0;
	else {
		rsp = BNEP_SETUP_INVALID_UUID_SIZE;
		goto done;
	}

	if (memcmp(ptr, NAP_UUID + off, len) == 0)
		dst = SDP_SERVICE_CLASS_NAP;
	else if (memcmp(ptr, GN_UUID + off, len) == 0)
		dst = SDP_SERVICE_CLASS_GN;
	else if (memcmp(ptr, PANU_UUID + off, len) == 0)
		dst = SDP_SERVICE_CLASS_PANU;
	else
		dst = 0;

	if (dst != service_class) {
		rsp = BNEP_SETUP_INVALID_DST_UUID;
		goto done;
	}

	ptr += len;

	if (memcmp(ptr, NAP_UUID + off, len) == 0)
		src = SDP_SERVICE_CLASS_NAP;
	else if (memcmp(ptr, GN_UUID + off, len) == 0)
		src = SDP_SERVICE_CLASS_GN;
	else if (memcmp(ptr, PANU_UUID + off, len) == 0)
		src = SDP_SERVICE_CLASS_PANU;
	else
		src = 0;

	if ((dst != SDP_SERVICE_CLASS_PANU && src != SDP_SERVICE_CLASS_PANU)
	    || src == 0) {
		rsp = BNEP_SETUP_INVALID_SRC_UUID;
		goto done;
	}

	rsp = BNEP_SETUP_SUCCESS;
	chan->state = CHANNEL_OPEN;
	channel_timeout(chan, 0);

done:
	log_debug("addr %s response 0x%2.2x",
	    ether_ntoa((struct ether_addr *)chan->raddr), rsp);

	bnep_send_control(chan, BNEP_SETUP_CONNECTION_RESPONSE, rsp);
	return (len * 2 + 1);
}

static size_t
bnep_recv_setup_connection_rsp(channel_t *chan, uint8_t *ptr, size_t size)
{
	int rsp;

	if (size < 2)
		return 0;

	rsp = be16dec(ptr);

	if (chan->state != CHANNEL_WAIT_CONNECT_RSP) {
		log_debug("ignored");
		return 2;
	}

	log_debug("addr %s response 0x%2.2x",
	    ether_ntoa((struct ether_addr *)chan->raddr), rsp);

	if (rsp == BNEP_SETUP_SUCCESS) {
		chan->state = CHANNEL_OPEN;
		channel_timeout(chan, 0);
	} else {
		channel_close(chan);
	}

	return 2;
}

static size_t
bnep_recv_filter_net_type_set(channel_t *chan, uint8_t *ptr, size_t size)
{
	pfilter_t *pf;
	int i, nf, rsp;
	size_t len;

	if (size < 2)
		return 0;

	len = be16dec(ptr);
	ptr += 2;

	if (size < (len + 2))
		return 0;

	if (chan->state != CHANNEL_OPEN) {
		log_debug("ignored");
		return (len + 2);
	}

	nf = len / 4;
	pf = malloc(nf * sizeof(pfilter_t));
	if (pf == NULL) {
		rsp = BNEP_FILTER_TOO_MANY_FILTERS;
		goto done;
	}

	log_debug("nf = %d", nf);

	for (i = 0; i < nf; i++) {
		pf[i].start = be16dec(ptr);
		ptr += 2;
		pf[i].end = be16dec(ptr);
		ptr += 2;

		if (pf[i].start > pf[i].end) {
			free(pf);
			rsp = BNEP_FILTER_INVALID_RANGE;
			goto done;
		}

		log_debug("pf[%d] = %#4.4x, %#4.4x", i, pf[i].start, pf[i].end);
	}

	if (chan->pfilter)
		free(chan->pfilter);

	chan->pfilter = pf;
	chan->npfilter = nf;

	rsp = BNEP_FILTER_SUCCESS;

done:
	log_debug("addr %s response 0x%2.2x",
	    ether_ntoa((struct ether_addr *)chan->raddr), rsp);

	bnep_send_control(chan, BNEP_FILTER_NET_TYPE_RESPONSE, rsp);
	return (len + 2);
}

static size_t
bnep_recv_filter_net_type_rsp(channel_t *chan, uint8_t *ptr, size_t size)
{
	int rsp;

	if (size < 2)
		return 0;

	if (chan->state != CHANNEL_OPEN) {
		log_debug("ignored");
		return 2;
	}

	rsp = be16dec(ptr);

	log_debug("addr %s response 0x%2.2x",
	    ether_ntoa((struct ether_addr *)chan->raddr), rsp);

	/* we did not send any filter_net_type_set message */
	return 2;
}

static size_t
bnep_recv_filter_multi_addr_set(channel_t *chan, uint8_t *ptr, size_t size)
{
	mfilter_t *mf;
	int i, nf, rsp;
	size_t len;

	if (size < 2)
		return 0;

	len = be16dec(ptr);
	ptr += 2;

	if (size < (len + 2))
		return 0;

	if (chan->state != CHANNEL_OPEN) {
		log_debug("ignored");
		return (len + 2);
	}

	nf = len / (ETHER_ADDR_LEN * 2);
	mf = malloc(nf * sizeof(mfilter_t));
	if (mf == NULL) {
		rsp = BNEP_FILTER_TOO_MANY_FILTERS;
		goto done;
	}

	log_debug("nf = %d", nf);

	for (i = 0; i < nf; i++) {
		memcpy(mf[i].start, ptr, ETHER_ADDR_LEN);
		ptr += ETHER_ADDR_LEN;

		memcpy(mf[i].end, ptr, ETHER_ADDR_LEN);
		ptr += ETHER_ADDR_LEN;

		if (memcmp(mf[i].start, mf[i].end, ETHER_ADDR_LEN) > 0) {
			free(mf);
			rsp = BNEP_FILTER_INVALID_RANGE;
			goto done;
		}

		log_debug("pf[%d] = "
		    "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x, "
		    "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x", i,
		    mf[i].start[0], mf[i].start[1], mf[i].start[2],
		    mf[i].start[3], mf[i].start[4], mf[i].start[5],
		    mf[i].end[0], mf[i].end[1], mf[i].end[2],
		    mf[i].end[3], mf[i].end[4], mf[i].end[5]);
	}

	if (chan->mfilter)
		free(chan->mfilter);

	chan->mfilter = mf;
	chan->nmfilter = nf;

	rsp = BNEP_FILTER_SUCCESS;

done:
	log_debug("addr %s response 0x%2.2x",
	    ether_ntoa((struct ether_addr *)chan->raddr), rsp);

	bnep_send_control(chan, BNEP_FILTER_MULTI_ADDR_RESPONSE, rsp);
	return (len + 2);
}

static size_t
bnep_recv_filter_multi_addr_rsp(channel_t *chan, uint8_t *ptr, size_t size)
{
	int rsp;

	if (size < 2)
		return false;

	if (chan->state != CHANNEL_OPEN) {
		log_debug("ignored");
		return 2;
	}

	rsp = be16dec(ptr);
	log_debug("addr %s response 0x%2.2x",
	    ether_ntoa((struct ether_addr *)chan->raddr), rsp);

	/* we did not send any filter_multi_addr_set message */
	return 2;
}

void
bnep_send_control(channel_t *chan, unsigned type, ...)
{
	packet_t *pkt;
	uint8_t *p;
	va_list ap;

	assert(chan->state != CHANNEL_CLOSED);

	pkt = packet_alloc(chan);
	if (pkt == NULL)
		return;

	p = pkt->ptr;
	va_start(ap, type);

	*p++ = BNEP_CONTROL;
	*p++ = (uint8_t)type;

	switch(type) {
	case BNEP_CONTROL_COMMAND_NOT_UNDERSTOOD:
		*p++ = va_arg(ap, int);
		break;

	case BNEP_SETUP_CONNECTION_REQUEST:
		*p++ = va_arg(ap, int);
		be16enc(p, va_arg(ap, int));
		p += 2;
		be16enc(p, va_arg(ap, int));
		p += 2;
		break;

	case BNEP_SETUP_CONNECTION_RESPONSE:
	case BNEP_FILTER_NET_TYPE_RESPONSE:
	case BNEP_FILTER_MULTI_ADDR_RESPONSE:
		be16enc(p, va_arg(ap, int));
		p += 2;
		break;

	case BNEP_FILTER_NET_TYPE_SET:		/* TODO */
	case BNEP_FILTER_MULTI_ADDR_SET:	/* TODO */
	default:
		log_err("Can't send control type 0x%2.2x", type);
		break;
	}

	va_end(ap);
	pkt->len = p - pkt->ptr;

	channel_put(chan, pkt);
	packet_free(pkt);
}

/*
 * BNEP send packet routine
 * return true if packet can be removed from queue
 */
bool
bnep_send(channel_t *chan, packet_t *pkt)
{
	struct iovec iov[2];
	uint8_t *p, *type, *proto;
	exthdr_t *eh;
	bool src, dst;
	size_t nw;

	if (pkt->type == NULL) {
		iov[0].iov_base = pkt->ptr;
		iov[0].iov_len = pkt->len;
		iov[1].iov_base = NULL;
		iov[1].iov_len = 0;
	} else {
		p = chan->sendbuf;

		dst = (memcmp(pkt->dst, chan->raddr, ETHER_ADDR_LEN) != 0);
		src = (memcmp(pkt->src, chan->laddr, ETHER_ADDR_LEN) != 0);

		type = p;
		p += 1;

		if (dst && src)
			*type = BNEP_GENERAL_ETHERNET;
		else if (dst && !src)
			*type = BNEP_COMPRESSED_ETHERNET_DST_ONLY;
		else if (!dst && src)
			*type = BNEP_COMPRESSED_ETHERNET_SRC_ONLY;
		else /* (!dst && !src) */
			*type = BNEP_COMPRESSED_ETHERNET;

		if (dst) {
			memcpy(p, pkt->dst, ETHER_ADDR_LEN);
			p += ETHER_ADDR_LEN;
		}

		if (src) {
			memcpy(p, pkt->src, ETHER_ADDR_LEN);
			p += ETHER_ADDR_LEN;
		}

		proto = p;
		memcpy(p, pkt->type, ETHER_TYPE_LEN);
		p += ETHER_TYPE_LEN;

		STAILQ_FOREACH(eh, &pkt->extlist, next) {
			if (p + eh->len > chan->sendbuf + chan->mtu)
				break;

			*type |= BNEP_EXT;
			type = p;

			memcpy(p, eh->ptr, eh->len);
			p += eh->len;
		}

		*type &= ~BNEP_EXT;

		iov[0].iov_base = chan->sendbuf;
		iov[0].iov_len = (p - chan->sendbuf);

		if ((chan->npfilter == 0 || bnep_pfilter(chan, pkt))
		    && (chan->nmfilter == 0 || bnep_mfilter(chan, pkt))) {
			iov[1].iov_base = pkt->ptr;
			iov[1].iov_len = pkt->len;
		} else if (be16dec(proto) == ETHERTYPE_VLAN
		    && pkt->len >= ETHER_VLAN_ENCAP_LEN) {
			iov[1].iov_base = pkt->ptr;
			iov[1].iov_len = ETHER_VLAN_ENCAP_LEN;
		} else {
			iov[1].iov_base = NULL;
			iov[1].iov_len = 0;
			memset(proto, 0, ETHER_TYPE_LEN);
		}
	}

	if (iov[0].iov_len + iov[1].iov_len > chan->mtu) {
		log_err("packet exceeded MTU (dropped)");
		return false;
	}

	nw = writev(chan->fd, iov, __arraycount(iov));
	return (nw > 0);
}

static bool
bnep_pfilter(channel_t *chan, packet_t *pkt)
{
	int proto, i;

	proto = be16dec(pkt->type);
	if (proto == ETHERTYPE_VLAN) {	/* IEEE 802.1Q tag header */
		if (pkt->len < 4)
			return false;

		proto = be16dec(pkt->ptr + 2);
	}

	for (i = 0; i < chan->npfilter; i++) {
		if (chan->pfilter[i].start <= proto
		    && chan->pfilter[i].end >=proto)
			return true;
	}

	return false;
}

static bool
bnep_mfilter(channel_t *chan, packet_t *pkt)
{
	int i;

	if (!ETHER_IS_MULTICAST(pkt->dst))
		return true;

	for (i = 0; i < chan->nmfilter; i++) {
		if (memcmp(pkt->dst, chan->mfilter[i].start, ETHER_ADDR_LEN) >= 0
		    && memcmp(pkt->dst, chan->mfilter[i].end, ETHER_ADDR_LEN) <= 0)
			return true;
	}

	return false;
}
