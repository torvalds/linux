/*	$OpenBSD: prune.c,v 1.7 2023/06/26 10:08:56 claudio Exp $ */

/*
 * Copyright (c) 2005, 2006 Esben Norby <norby@openbsd.org>
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
#include <netinet/ip.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <string.h>

#include "igmp.h"
#include "dvmrpd.h"
#include "dvmrp.h"
#include "log.h"
#include "dvmrpe.h"

/* DVMRP prune packet handling */
int
send_prune(struct nbr *nbr, struct prune *p)
{
	struct sockaddr_in	 dst;
	struct ibuf		*buf;
	struct prune_hdr	 prune;
	int			 ret = 0;

	log_debug("send_prune: interface %s nbr %s", nbr->iface->name,
	    inet_ntoa(nbr->addr));

	if (nbr->iface->passive)
		return (0);

	memset(&prune, 0, sizeof(prune));

	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);
	dst.sin_addr = nbr->addr;

	if ((buf = ibuf_open(nbr->iface->mtu - sizeof(struct ip))) == NULL)
		fatal("send_prune");

	/* DVMRP header */
	if (gen_dvmrp_hdr(buf, nbr->iface, DVMRP_CODE_PRUNE))
		goto fail;

	prune.src_host_addr = p->origin.s_addr;
	prune.group_addr = p->group.s_addr;

	/* XXX */
	prune.lifetime = htonl(MAX_PRUNE_LIFETIME);
	prune.src_netmask = p->netmask.s_addr;

	ibuf_add(buf, &prune, sizeof(prune));

	ret = send_packet(nbr->iface, buf, &dst);
	ibuf_free(buf);

	return (ret);
fail:
	log_warn("send_prune");
	ibuf_free(buf);
	return (-1);
}

void
recv_prune(struct nbr *nbr, char *buf, u_int16_t len)
{
	struct prune		 p;
	struct prune_hdr	*prune;

	log_debug("recv_prune: neighbor ID %s", inet_ntoa(nbr->id));

	if (len < PRUNE_MIN_LEN) {
		log_debug("recv_prune: packet malformed from %s",
		    inet_ntoa(nbr->id));
		return;
	}

	memset(&p, 0, sizeof(p));

	prune = (struct prune_hdr *)buf;

	p.origin.s_addr = prune->src_host_addr;
	p.group.s_addr = prune->group_addr;
	p.lifetime = prune->lifetime;

	if (len >= sizeof(*prune))
		p.netmask.s_addr = prune->src_netmask;

	p.ifindex = nbr->iface->ifindex;

	dvmrpe_imsg_compose_rde(IMSG_RECV_PRUNE, nbr->peerid, 0, &p, sizeof(p));

	return;
}
