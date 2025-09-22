/*	$OpenBSD: hello.c,v 1.27 2024/08/21 15:18:00 florian Exp $ */

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

#include "ospfd.h"
#include "ospf.h"
#include "log.h"
#include "ospfe.h"

extern struct ospfd_conf	*oeconf;

/* hello packet handling */
int
send_hello(struct iface *iface)
{
	struct sockaddr_in	 dst;
	struct hello_hdr	 hello;
	struct nbr		*nbr;
	struct ibuf		*buf;

	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);

	switch (iface->type) {
	case IF_TYPE_POINTOPOINT:
	case IF_TYPE_BROADCAST:
		inet_pton(AF_INET, AllSPFRouters, &dst.sin_addr);
		break;
	case IF_TYPE_NBMA:
	case IF_TYPE_POINTOMULTIPOINT:
		log_debug("send_hello: type %s not supported, interface %s",
		    if_type_name(iface->type), iface->name);
		return (-1);
	case IF_TYPE_VIRTUALLINK:
		dst.sin_addr = iface->dst;
		break;
	default:
		fatalx("send_hello: unknown interface type");
	}

	if ((buf = ibuf_dynamic(PKG_DEF_SIZE,
	    IP_MAXPACKET - sizeof(struct ip))) == NULL)
		fatal("send_hello");

	/* OSPF header */
	if (gen_ospf_hdr(buf, iface, PACKET_TYPE_HELLO))
		goto fail;

	/* hello header */
	hello.mask = iface->mask.s_addr;
	hello.hello_interval = htons(iface->hello_interval);
	hello.opts = area_ospf_options(iface->area);
	hello.rtr_priority = iface->priority;
	hello.rtr_dead_interval = htonl(iface->dead_interval);

	if (iface->dr) {
		hello.d_rtr = iface->dr->addr.s_addr;
		iface->self->dr.s_addr = iface->dr->addr.s_addr;
	} else
		hello.d_rtr = 0;
	if (iface->bdr) {
		hello.bd_rtr = iface->bdr->addr.s_addr;
		iface->self->bdr.s_addr = iface->bdr->addr.s_addr;
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

	/* update authentication and calculate checksum */
	if (auth_gen(buf, iface))
		goto fail;

	if (send_packet(iface, buf, &dst) == -1)
		goto fail;

	ibuf_free(buf);
	return (0);
fail:
	log_warn("%s", __func__);
	ibuf_free(buf);
	return (-1);
}

void
recv_hello(struct iface *iface, struct in_addr src, u_int32_t rtr_id, char *buf,
    u_int16_t len)
{
	struct hello_hdr	 hello;
	struct nbr		*nbr = NULL, *dr;
	u_int32_t		 nbr_id;
	int			 nbr_change = 0;

	if (len < sizeof(hello) || (len & 0x03)) {
		log_warnx("recv_hello: bad packet size, interface %s",
		    iface->name);
		return;
	}

	memcpy(&hello, buf, sizeof(hello));
	buf += sizeof(hello);
	len -= sizeof(hello);

	if (iface->type != IF_TYPE_POINTOPOINT &&
	    iface->type != IF_TYPE_VIRTUALLINK)
		if (hello.mask != iface->mask.s_addr) {
			log_warnx("recv_hello: invalid netmask, interface %s",
			    iface->name);
			return;
		}

	if (ntohs(hello.hello_interval) != iface->hello_interval) {
		log_warnx("recv_hello: invalid hello-interval %d, "
		    "interface %s", ntohs(hello.hello_interval),
		    iface->name);
		return;
	}

	if (ntohl(hello.rtr_dead_interval) != iface->dead_interval) {
		log_warnx("recv_hello: invalid router-dead-interval %d, "
		    "interface %s", ntohl(hello.rtr_dead_interval),
		    iface->name);
		return;
	}

	if ((hello.opts & OSPF_OPTION_E && iface->area->stub) ||
	    ((hello.opts & OSPF_OPTION_E) == 0 && !iface->area->stub)) {
		log_warnx("recv_hello: ExternalRoutingCapability mismatch, "
		    "interface %s", iface->name);
		return;
	}

	/*
	 * Match router-id, in case of conflict moan and ignore hello.
	 * Only the router-id is compared since the source IP on NBMA,
	 * broadcast and point-to-multipoint interfaces was already
	 * compared in find_iface() and only IPs in the same subnet
	 * are accepted. This is not exactly what the RFC specifies
	 * but works far better.
	 */
	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if (nbr == iface->self) {
			if (nbr->id.s_addr == rtr_id) {
				log_warnx("recv_hello: Router-ID collision on "
				    "interface %s neighbor IP %s", iface->name,
				    inet_ntoa(src));
				return;
			}
			continue;
		}
		if (nbr->id.s_addr == rtr_id)
			break;
	}

	if (!nbr) {
		nbr = nbr_new(rtr_id, iface, 0);
		/* set neighbor parameters */
		nbr->dr.s_addr = hello.d_rtr;
		nbr->bdr.s_addr = hello.bd_rtr;
		nbr->priority = hello.rtr_priority;
		/* XXX neighbor address shouldn't be stored on virtual links */
		nbr->addr.s_addr = src.s_addr;
		ospfe_imsg_compose_rde(IMSG_NEIGHBOR_ADDR, nbr->peerid, 0,
		    &src, sizeof(src));
	}

	if (nbr->addr.s_addr != src.s_addr) {
		log_warnx("%s: neighbor ID %s changed its IP address",
		    __func__, inet_ntoa(nbr->id));
		nbr->addr.s_addr = src.s_addr;
		ospfe_imsg_compose_rde(IMSG_NEIGHBOR_ADDR, nbr->peerid, 0,
		    &src, sizeof(src));
	}

	nbr->options = hello.opts;

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
		nbr->priority = hello.rtr_priority;
		return;
	}

	if (nbr->priority != hello.rtr_priority) {
		nbr->priority = hello.rtr_priority;
		nbr_change = 1;
	}

	if (iface->state & IF_STA_WAITING &&
	    hello.d_rtr == nbr->addr.s_addr && hello.bd_rtr == 0)
		if_fsm(iface, IF_EVT_BACKUP_SEEN);

	if (iface->state & IF_STA_WAITING && hello.bd_rtr == nbr->addr.s_addr) {
		/*
		 * In case we see the BDR make sure that the DR is around
		 * with a bidirectional (2_WAY or better) connection
		 */
		LIST_FOREACH(dr, &iface->nbr_list, entry)
			if (hello.d_rtr == dr->addr.s_addr &&
			    dr->state & NBR_STA_BIDIR)
				if_fsm(iface, IF_EVT_BACKUP_SEEN);
	}

	if ((nbr->addr.s_addr == nbr->dr.s_addr &&
	    nbr->addr.s_addr != hello.d_rtr) ||
	    (nbr->addr.s_addr != nbr->dr.s_addr &&
	    nbr->addr.s_addr == hello.d_rtr))
		/* neighbor changed from or to DR */
		nbr_change = 1;
	if ((nbr->addr.s_addr == nbr->bdr.s_addr &&
	    nbr->addr.s_addr != hello.bd_rtr) ||
	    (nbr->addr.s_addr != nbr->bdr.s_addr &&
	    nbr->addr.s_addr == hello.bd_rtr))
		/* neighbor changed from or to BDR */
		nbr_change = 1;

	nbr->dr.s_addr = hello.d_rtr;
	nbr->bdr.s_addr = hello.bd_rtr;

	if (nbr_change)
		if_fsm(iface, IF_EVT_NBR_CHNG);

	/* TODO NBMA needs some special handling */
}
