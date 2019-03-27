/*	$OpenBSD: pf_print_state.c,v 1.52 2008/08/12 16:40:18 david Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2001 Daniel Hartmeier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/endian.h>
#include <net/if.h>
#define TCPSTATES
#include <netinet/tcp_fsm.h>
#include <net/pfvar.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pfctl_parser.h"
#include "pfctl.h"

void	print_name(struct pf_addr *, sa_family_t);

void
print_addr(struct pf_addr_wrap *addr, sa_family_t af, int verbose)
{
	switch (addr->type) {
	case PF_ADDR_DYNIFTL:
		printf("(%s", addr->v.ifname);
		if (addr->iflags & PFI_AFLAG_NETWORK)
			printf(":network");
		if (addr->iflags & PFI_AFLAG_BROADCAST)
			printf(":broadcast");
		if (addr->iflags & PFI_AFLAG_PEER)
			printf(":peer");
		if (addr->iflags & PFI_AFLAG_NOALIAS)
			printf(":0");
		if (verbose) {
			if (addr->p.dyncnt <= 0)
				printf(":*");
			else
				printf(":%d", addr->p.dyncnt);
		}
		printf(")");
		break;
	case PF_ADDR_TABLE:
		if (verbose)
			if (addr->p.tblcnt == -1)
				printf("<%s:*>", addr->v.tblname);
			else
				printf("<%s:%d>", addr->v.tblname,
				    addr->p.tblcnt);
		else
			printf("<%s>", addr->v.tblname);
		return;
	case PF_ADDR_RANGE: {
		char buf[48];

		if (inet_ntop(af, &addr->v.a.addr, buf, sizeof(buf)) == NULL)
			printf("?");
		else
			printf("%s", buf);
		if (inet_ntop(af, &addr->v.a.mask, buf, sizeof(buf)) == NULL)
			printf(" - ?");
		else
			printf(" - %s", buf);
		break;
	}
	case PF_ADDR_ADDRMASK:
		if (PF_AZERO(&addr->v.a.addr, AF_INET6) &&
		    PF_AZERO(&addr->v.a.mask, AF_INET6))
			printf("any");
		else {
			char buf[48];

			if (inet_ntop(af, &addr->v.a.addr, buf,
			    sizeof(buf)) == NULL)
				printf("?");
			else
				printf("%s", buf);
		}
		break;
	case PF_ADDR_NOROUTE:
		printf("no-route");
		return;
	case PF_ADDR_URPFFAILED:
		printf("urpf-failed");
		return;
	default:
		printf("?");
		return;
	}

	/* mask if not _both_ address and mask are zero */
	if (addr->type != PF_ADDR_RANGE &&
	    !(PF_AZERO(&addr->v.a.addr, AF_INET6) &&
	    PF_AZERO(&addr->v.a.mask, AF_INET6))) {
		int bits = unmask(&addr->v.a.mask, af);

		if (bits != (af == AF_INET ? 32 : 128))
			printf("/%d", bits);
	}
}

void
print_name(struct pf_addr *addr, sa_family_t af)
{
	char host[NI_MAXHOST];

	strlcpy(host, "?", sizeof(host));
	switch (af) {
	case AF_INET: {
		struct sockaddr_in sin;

		memset(&sin, 0, sizeof(sin));
		sin.sin_len = sizeof(sin);
		sin.sin_family = AF_INET;
		sin.sin_addr = addr->v4;
		getnameinfo((struct sockaddr *)&sin, sin.sin_len,
		    host, sizeof(host), NULL, 0, NI_NOFQDN);
		break;
	}
	case AF_INET6: {
		struct sockaddr_in6 sin6;

		memset(&sin6, 0, sizeof(sin6));
		sin6.sin6_len = sizeof(sin6);
		sin6.sin6_family = AF_INET6;
		sin6.sin6_addr = addr->v6;
		getnameinfo((struct sockaddr *)&sin6, sin6.sin6_len,
		    host, sizeof(host), NULL, 0, NI_NOFQDN);
		break;
	}
	}
	printf("%s", host);
}

void
print_host(struct pf_addr *addr, u_int16_t port, sa_family_t af, int opts)
{
	if (opts & PF_OPT_USEDNS)
		print_name(addr, af);
	else {
		struct pf_addr_wrap aw;

		memset(&aw, 0, sizeof(aw));
		aw.v.a.addr = *addr;
		if (af == AF_INET)
			aw.v.a.mask.addr32[0] = 0xffffffff;
		else {
			memset(&aw.v.a.mask, 0xff, sizeof(aw.v.a.mask));
			af = AF_INET6;
		}
		print_addr(&aw, af, opts & PF_OPT_VERBOSE2);
	}

	if (port) {
		if (af == AF_INET)
			printf(":%u", ntohs(port));
		else
			printf("[%u]", ntohs(port));
	}
}

void
print_seq(struct pfsync_state_peer *p)
{
	if (p->seqdiff)
		printf("[%u + %u](+%u)", ntohl(p->seqlo),
		    ntohl(p->seqhi) - ntohl(p->seqlo), ntohl(p->seqdiff));
	else
		printf("[%u + %u]", ntohl(p->seqlo),
		    ntohl(p->seqhi) - ntohl(p->seqlo));
}

void
print_state(struct pfsync_state *s, int opts)
{
	struct pfsync_state_peer *src, *dst;
	struct pfsync_state_key *key, *sk, *nk;
	struct protoent *p;
	int min, sec;
#ifndef __NO_STRICT_ALIGNMENT
	struct pfsync_state_key aligned_key[2];

	bcopy(&s->key, aligned_key, sizeof(aligned_key));
	key = aligned_key;
#else
	key = s->key;
#endif

	if (s->direction == PF_OUT) {
		src = &s->src;
		dst = &s->dst;
		sk = &key[PF_SK_STACK];
		nk = &key[PF_SK_WIRE];
		if (s->proto == IPPROTO_ICMP || s->proto == IPPROTO_ICMPV6) 
			sk->port[0] = nk->port[0];
	} else {
		src = &s->dst;
		dst = &s->src;
		sk = &key[PF_SK_WIRE];
		nk = &key[PF_SK_STACK];
		if (s->proto == IPPROTO_ICMP || s->proto == IPPROTO_ICMPV6) 
			sk->port[1] = nk->port[1];
	}
	printf("%s ", s->ifname);
	if ((p = getprotobynumber(s->proto)) != NULL)
		printf("%s ", p->p_name);
	else
		printf("%u ", s->proto);

	print_host(&nk->addr[1], nk->port[1], s->af, opts);
	if (PF_ANEQ(&nk->addr[1], &sk->addr[1], s->af) ||
	    nk->port[1] != sk->port[1]) {
		printf(" (");
		print_host(&sk->addr[1], sk->port[1], s->af, opts);
		printf(")");
	}
	if (s->direction == PF_OUT)
		printf(" -> ");
	else
		printf(" <- ");
	print_host(&nk->addr[0], nk->port[0], s->af, opts);
	if (PF_ANEQ(&nk->addr[0], &sk->addr[0], s->af) ||
	    nk->port[0] != sk->port[0]) {
		printf(" (");
		print_host(&sk->addr[0], sk->port[0], s->af, opts);
		printf(")");
	}

	printf("    ");
	if (s->proto == IPPROTO_TCP) {
		if (src->state <= TCPS_TIME_WAIT &&
		    dst->state <= TCPS_TIME_WAIT)
			printf("   %s:%s\n", tcpstates[src->state],
			    tcpstates[dst->state]);
		else if (src->state == PF_TCPS_PROXY_SRC ||
		    dst->state == PF_TCPS_PROXY_SRC)
			printf("   PROXY:SRC\n");
		else if (src->state == PF_TCPS_PROXY_DST ||
		    dst->state == PF_TCPS_PROXY_DST)
			printf("   PROXY:DST\n");
		else
			printf("   <BAD STATE LEVELS %u:%u>\n",
			    src->state, dst->state);
		if (opts & PF_OPT_VERBOSE) {
			printf("   ");
			print_seq(src);
			if (src->wscale && dst->wscale)
				printf(" wscale %u",
				    src->wscale & PF_WSCALE_MASK);
			printf("  ");
			print_seq(dst);
			if (src->wscale && dst->wscale)
				printf(" wscale %u",
				    dst->wscale & PF_WSCALE_MASK);
			printf("\n");
		}
	} else if (s->proto == IPPROTO_UDP && src->state < PFUDPS_NSTATES &&
	    dst->state < PFUDPS_NSTATES) {
		const char *states[] = PFUDPS_NAMES;

		printf("   %s:%s\n", states[src->state], states[dst->state]);
#ifndef INET6
	} else if (s->proto != IPPROTO_ICMP && src->state < PFOTHERS_NSTATES &&
	    dst->state < PFOTHERS_NSTATES) {
#else
	} else if (s->proto != IPPROTO_ICMP && s->proto != IPPROTO_ICMPV6 &&
	    src->state < PFOTHERS_NSTATES && dst->state < PFOTHERS_NSTATES) {
#endif
		/* XXX ICMP doesn't really have state levels */
		const char *states[] = PFOTHERS_NAMES;

		printf("   %s:%s\n", states[src->state], states[dst->state]);
	} else {
		printf("   %u:%u\n", src->state, dst->state);
	}

	if (opts & PF_OPT_VERBOSE) {
		u_int64_t packets[2];
		u_int64_t bytes[2];
		u_int32_t creation = ntohl(s->creation);
		u_int32_t expire = ntohl(s->expire);

		sec = creation % 60;
		creation /= 60;
		min = creation % 60;
		creation /= 60;
		printf("   age %.2u:%.2u:%.2u", creation, min, sec);
		sec = expire % 60;
		expire /= 60;
		min = expire % 60;
		expire /= 60;
		printf(", expires in %.2u:%.2u:%.2u", expire, min, sec);

		bcopy(s->packets[0], &packets[0], sizeof(u_int64_t));
		bcopy(s->packets[1], &packets[1], sizeof(u_int64_t));
		bcopy(s->bytes[0], &bytes[0], sizeof(u_int64_t));
		bcopy(s->bytes[1], &bytes[1], sizeof(u_int64_t));
		printf(", %ju:%ju pkts, %ju:%ju bytes",
		    (uintmax_t )be64toh(packets[0]),
		    (uintmax_t )be64toh(packets[1]),
		    (uintmax_t )be64toh(bytes[0]),
		    (uintmax_t )be64toh(bytes[1]));
		if (ntohl(s->anchor) != -1)
			printf(", anchor %u", ntohl(s->anchor));
		if (ntohl(s->rule) != -1)
			printf(", rule %u", ntohl(s->rule));
		if (s->state_flags & PFSTATE_SLOPPY)
			printf(", sloppy");
		if (s->sync_flags & PFSYNC_FLAG_SRCNODE)
			printf(", source-track");
		if (s->sync_flags & PFSYNC_FLAG_NATSRCNODE)
			printf(", sticky-address");
		printf("\n");
	}
	if (opts & PF_OPT_VERBOSE2) {
		u_int64_t id;

		bcopy(&s->id, &id, sizeof(u_int64_t));
		printf("   id: %016jx creatorid: %08x",
		    (uintmax_t )be64toh(id), ntohl(s->creatorid));
		printf("\n");
	}
}

int
unmask(struct pf_addr *m, sa_family_t af)
{
	int i = 31, j = 0, b = 0;
	u_int32_t tmp;

	while (j < 4 && m->addr32[j] == 0xffffffff) {
		b += 32;
		j++;
	}
	if (j < 4) {
		tmp = ntohl(m->addr32[j]);
		for (i = 31; tmp & (1 << i); --i)
			b++;
	}
	return (b);
}
