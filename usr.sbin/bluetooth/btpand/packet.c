/*	$NetBSD: packet.c,v 1.1 2008/08/17 13:20:57 plunky Exp $	*/

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
__RCSID("$NetBSD: packet.c,v 1.1 2008/08/17 13:20:57 plunky Exp $");

#define L2CAP_SOCKET_CHECKED
#include "btpand.h"

packet_t *
packet_alloc(channel_t *chan)
{
	packet_t *pkt;

	pkt = malloc(sizeof(packet_t) + chan->mru);
	if (pkt == NULL) {
		log_err("%s() failed: %m", __func__);
		return NULL;
	}

	memset(pkt, 0, sizeof(packet_t));
	STAILQ_INIT(&pkt->extlist);
	pkt->ptr = pkt->buf;

	pkt->chan = chan;
	chan->refcnt++;

	return pkt;
}

void
packet_free(packet_t *pkt)
{
	exthdr_t *eh;

	if (pkt->refcnt-- > 0)
		return;

	while ((eh = STAILQ_FIRST(&pkt->extlist)) != NULL) {
		STAILQ_REMOVE_HEAD(&pkt->extlist, next);
		free(eh);
	}

	pkt->chan->refcnt--;
	if (pkt->chan->refcnt == 0)
		channel_free(pkt->chan);

	free(pkt);
}

void
packet_adj(packet_t *pkt, size_t size)
{

	assert(pkt->refcnt == 0);
	assert(pkt->len >= size);

	pkt->ptr += size;
	pkt->len -= size;
}

pkthdr_t *
pkthdr_alloc(packet_t *pkt)
{
	pkthdr_t *ph;

	ph = malloc(sizeof(pkthdr_t));
	if (ph == NULL) {
		log_err("%s() failed: %m", __func__);
		return NULL;
	}

	ph->data = pkt;
	pkt->refcnt++;

	return ph;
}

void
pkthdr_free(pkthdr_t *ph)
{

	packet_free(ph->data);
	free(ph);
}
