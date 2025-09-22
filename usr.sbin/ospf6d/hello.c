/*	$OpenBSD: hello.c,v 1.23 2020/07/15 14:47:41 denis Exp $ */

/*
 * Copyright (c) 2005 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2004, 2005 Esben Norby <norby@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <event.h>

#include "ospf6d.h"
#include "ospf6.h"
#include "log.h"
#include "ospfe.h"

/* hello packet handling */
int
send_hello(struct iface *iface)
{
	struct in6_addr		 dst;
	struct hello_hdr	 hello;
	struct nbr		*nbr;
	struct ibuf		*buf;

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		inet_pton(AF_INET6, AllSPFRouters, &dst);
		break;
	case IF_TYPE_NBMA:
	case IF_TYPE_POINTOMULTIPOINT:
		log_debug("send_hello: type %s not supported, interface %s",
		    if_type_name(iface->type), iface->name);
		return (-1);
	case IF_TYPE_VIRTUALLINK:
		dst = iface->dst;
		break;
	default:
		fatalx("send_hello: unknown interface type");
	}

	/* XXX IBUF_READ_SIZE */
	if ((buf = ibuf_dynamic(PKG_DEF_SIZE, IBUF_READ_SIZE)) == NULL)
		fatal("send_hello");

	/* OSPF header */
	if (gen_ospf_hdr(buf, iface, PACKET_TYPE_HELLO))
		goto fail;

	/* hello header */
	hello.iface_id = htonl(iface->ifindex);
	LSA_24_SETHI(hello.opts, iface->priority);
	LSA_24_SETLO(hello.opts, area_ospf_options(iface->area));
	hello.opts = htonl(hello.opts);
	hello.hello_interval = htons(iface->hello_interval);
	hello.rtr_dead_interval = htons(iface->dead_interval);

	if (iface->dr) {
		hello.d_rtr = iface->dr->id.s_addr;
		iface->self->dr.s_addr = iface->dr->id.s_addr;
	} else
		hello.d_rtr = 0;
	if (iface->bdr) {
		hello.bd_rtr = iface->bdr->id.s_addr;
		iface->self->bdr.s_addr = iface->bdr->id.s_addr;
	} else
		hello.bd_rtr = 0;

	if (ibuf_add(buf, &hello, sizeof(hello)))
		goto fail;

	/* active neighbor(s) */
	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if ((nbr->state >= NBR_STA_INIT) && (nbr != iface->self))
			if (ibuf_add(buf, &nbr->id, sizeof(nbr->id)))
				goto fail;
	}

	/* calculate checksum */
	if (upd_ospf_hdr(buf, iface))
		goto fail;

	if (send_packet(iface, buf, &dst) == -1)
		goto fail;

	ibuf_free(buf);
	return (0);
fail:
	log_warn("send_hello");
	ibuf_free(buf);
	return (-1);
}

void
recv_hello(struct iface *iface, struct in6_addr *src, u_int32_t rtr_id,
    char *buf, u_int16_t len)
{
	struct hello_hdr	 hello;
	struct nbr		*nbr = NULL, *dr;
	u_int32_t		 nbr_id, opts;
	int			 nbr_change = 0;

	if (len < sizeof(hello) || (len & 0x03)) {
		log_warnx("recv_hello: bad packet size, interface %s",
		    iface->name);
		return;
	}

	memcpy(&hello, buf, sizeof(hello));
	buf += sizeof(hello);
	len -= sizeof(hello);

	if (ntohs(hello.hello_interval) != iface->hello_interval) {
		log_warnx("recv_hello: invalid hello-interval %d, "
		    "interface %s", ntohs(hello.hello_interval),
		    iface->name);
		return;
	}

	if (ntohs(hello.rtr_dead_interval) != iface->dead_interval) {
		log_warnx("recv_hello: invalid router-dead-interval %d, "
		    "interface %s", ntohl(hello.rtr_dead_interval),
		    iface->name);
		return;
	}

	opts = LSA_24_GETLO(ntohl(hello.opts));
	if ((opts & OSPF_OPTION_E && iface->area->stub) ||
	    ((opts & OSPF_OPTION_E) == 0 && !iface->area->stub)) {
		log_warnx("recv_hello: ExternalRoutingCapability mismatch, "
		    "interface %s", iface->name);
		return;
	}

	/* match router-id */
	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if (nbr == iface->self) {
			if (nbr->id.s_addr == rtr_id) {
				log_warnx("recv_hello: Router-ID collision on "
				    "interface %s neighbor IP %s", iface->name,
				    log_in6addr(src));
				return;
			}
			continue;
		}
		if (nbr->id.s_addr == rtr_id)
			break;
	}

	if (!nbr) {
		nbr = nbr_new(rtr_id, iface, ntohl(hello.iface_id), 0, src);
		/* set neighbor parameters */
		nbr->dr.s_addr = hello.d_rtr;
		nbr->bdr.s_addr = hello.bd_rtr;
		nbr->priority = LSA_24_GETHI(ntohl(hello.opts));
		/* XXX neighbor address shouldn't be stored on virtual links */
		nbr->addr = *src;
	}

	if (!IN6_ARE_ADDR_EQUAL(&nbr->addr, src)) {
		log_warnx("%s: neighbor ID %s changed its address to %s",
		    __func__, inet_ntoa(nbr->id), log_in6addr(src));
		nbr->addr = *src;
	}

	nbr->options = opts;

	nbr_fsm(nbr, NBR_EVT_HELLO_RCVD);

	while (len >= sizeof(nbr_id)) {
		memcpy(&nbr_id, buf, sizeof(nbr_id));
		if (nbr_id == ospfe_router_id()) {
			/* seen myself */
			if (nbr->state & NBR_STA_PRELIM) {
				nbr_fsm(nbr, NBR_EVT_2_WAY_RCVD);
				nbr_change = 1;
			}
			break;
		}
		buf += sizeof(nbr_id);
		len -= sizeof(nbr_id);
	}

	if (len == 0) {
		nbr_fsm(nbr, NBR_EVT_1_WAY_RCVD);
		/* set neighbor parameters */
		nbr->dr.s_addr = hello.d_rtr;
		nbr->bdr.s_addr = hello.bd_rtr;
		nbr->priority = LSA_24_GETHI(ntohl(hello.opts));
		return;
	}

	if (nbr->priority != LSA_24_GETHI(ntohl(hello.opts))) {
		nbr->priority = LSA_24_GETHI(ntohl(hello.opts));
		nbr_change = 1;
	}

	if (iface->state & IF_STA_WAITING &&
	    hello.d_rtr == nbr->id.s_addr && hello.bd_rtr == 0)
		if_fsm(iface, IF_EVT_BACKUP_SEEN);

	if (iface->state & IF_STA_WAITING && hello.bd_rtr == nbr->id.s_addr) {
		/*
		 * In case we see the BDR make sure that the DR is around
		 * with a bidirectional (2_WAY or better) connection
		 */
		LIST_FOREACH(dr, &iface->nbr_list, entry)
			if (hello.d_rtr == dr->id.s_addr &&
			    dr->state & NBR_STA_BIDIR)
				if_fsm(iface, IF_EVT_BACKUP_SEEN);
	}

	if ((nbr->id.s_addr == nbr->dr.s_addr &&
	    nbr->id.s_addr != hello.d_rtr) ||
	    (nbr->id.s_addr != nbr->dr.s_addr &&
	    nbr->id.s_addr == hello.d_rtr))
		/* neighbor changed from or to DR */
		nbr_change = 1;
	if ((nbr->id.s_addr == nbr->bdr.s_addr &&
	    nbr->id.s_addr != hello.bd_rtr) ||
	    (nbr->id.s_addr != nbr->bdr.s_addr &&
	    nbr->id.s_addr == hello.bd_rtr))
		/* neighbor changed from or to BDR */
		nbr_change = 1;

	nbr->dr.s_addr = hello.d_rtr;
	nbr->bdr.s_addr = hello.bd_rtr;

	if (nbr_change)
		if_fsm(iface, IF_EVT_NBR_CHNG);

	/* TODO NBMA needs some special handling */
}
