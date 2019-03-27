/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2007 David Malone <dwmalone@FreeBSD.org>
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
 *
 *	$FreeBSD$
 */

#define ACCEPT_FILTER_MOD

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/module.h>
#include <sys/signalvar.h>
#include <sys/sysctl.h>
#include <sys/socketvar.h>

/* check for full DNS request */
static int sohasdns(struct socket *so, void *arg, int waitflag);

struct packet {
	struct mbuf *m;		/* Current mbuf. */
	struct mbuf *n;		/* nextpkt mbuf. */
	unsigned long moff;	/* Offset of the beginning of m. */
	unsigned long offset;	/* Which offset we are working at. */
	unsigned long len;	/* The number of bytes we have to play with. */
};

#define DNS_OK 0
#define DNS_WAIT -1
#define DNS_RUN -2

/* check we can skip over various parts of DNS request */
static int skippacket(struct sockbuf *sb);

static struct accept_filter accf_dns_filter = {
	"dnsready",
	sohasdns,
	NULL,
	NULL
};

static moduledata_t accf_dns_mod = {
	"accf_dns",
	accept_filt_generic_mod_event,
	&accf_dns_filter
};

DECLARE_MODULE(accf_dns, accf_dns_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);

static int
sohasdns(struct socket *so, void *arg, int waitflag)
{
	struct sockbuf *sb = &so->so_rcv;

	/* If the socket is full, we're ready. */
	if (sbused(sb) >= sb->sb_hiwat || sb->sb_mbcnt >= sb->sb_mbmax)
		goto ready;

	/* Check to see if we have a request. */
	if (skippacket(sb) == DNS_WAIT)
		return (SU_OK);

ready:
	return (SU_ISCONNECTED);
}

#define GET8(p, val) do { \
	if (p->offset < p->moff) \
		return DNS_RUN; \
	while (p->offset >= p->moff + p->m->m_len) { \
		p->moff += p->m->m_len; \
		p->m = p->m->m_next; \
		if (p->m == NULL) { \
			p->m = p->n; \
			p->n = p->m->m_nextpkt; \
		} \
		if (p->m == NULL) \
			return DNS_WAIT; \
	} \
	val = *(mtod(p->m, unsigned char *) + (p->offset - p->moff)); \
	p->offset++; \
	} while (0)

#define GET16(p, val) do { \
	unsigned int v0, v1; \
	GET8(p, v0); \
	GET8(p, v1); \
	val = v0 * 0x100 + v1; \
	} while (0)

static int
skippacket(struct sockbuf *sb) {
	unsigned long packlen;
	struct packet q, *p = &q;

	if (sbavail(sb) < 2)
		return DNS_WAIT;

	q.m = sb->sb_mb;
	q.n = q.m->m_nextpkt;
	q.moff = 0;
	q.offset = 0;
	q.len = sbavail(sb);

	GET16(p, packlen);
	if (packlen + 2 > q.len)
		return DNS_WAIT;

	return DNS_OK;
}
