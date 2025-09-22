/*	$OpenBSD: probe.c,v 1.7 2024/08/21 09:18:47 florian Exp $ */

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
#include <arpa/inet.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <event.h>

#include "igmp.h"
#include "dvmrpd.h"
#include "dvmrp.h"
#include "log.h"
#include "dvmrpe.h"

extern struct dvmrpd_conf	*deconf;

/* DVMRP probe packet handling */
int
send_probe(struct iface *iface)
{
	struct sockaddr_in	 dst;
	struct ibuf		*buf;
	struct nbr		*nbr;
	int			 ret = 0;

	if (iface->passive)
		return (0);

	if ((buf = ibuf_open(iface->mtu - sizeof(struct ip))) == NULL)
		fatal("send_probe");

	/* DVMRP header */
	if (gen_dvmrp_hdr(buf, iface, DVMRP_CODE_PROBE))
		goto fail;

	/* generation ID */
	ibuf_add(buf, &iface->gen_id, sizeof(iface->gen_id));

	/* generate neighbor list */
	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if (nbr->state > NBR_STA_DOWN)
			ibuf_add(buf, &nbr->id, sizeof(nbr->id));
	}

	/* set destination address */
	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);
	inet_pton(AF_INET, AllDVMRPRouters, &dst.sin_addr);

	ret = send_packet(iface, buf, &dst);
	ibuf_free(buf);
	return (ret);
fail:
	log_warn("send_probe");
	ibuf_free(buf);
	return (-1);
}

void
recv_probe(struct iface *iface, struct in_addr src, u_int32_t src_ip,
    u_int8_t capabilities, char *buf, u_int16_t len)
{
	struct nbr	*nbr = NULL;
	u_int32_t	 gen_id;
	u_int32_t	 nbr_id;

	LIST_FOREACH(nbr, &iface->nbr_list, entry) {
		if (nbr->id.s_addr == src_ip)
			break;
	}

	memcpy(&gen_id, buf, sizeof(gen_id));
	len -= sizeof(gen_id);
	buf += sizeof(gen_id);

	if (!nbr) {
		nbr = nbr_new(src_ip, iface, 0);
		nbr->gen_id = gen_id;
		nbr->capabilities = capabilities;
		nbr->addr = src;
	}

	nbr_fsm(nbr, NBR_EVT_PROBE_RCVD);

	if ((nbr->gen_id != gen_id) || (nbr->capabilities != capabilities)) {
		if (!nbr->compat)
			nbr_fsm(nbr, NBR_EVT_1_WAY_RCVD);
		nbr->gen_id = gen_id;
		nbr->capabilities = capabilities;

		/* XXX handle nbr change! */
	}

	while (len >= sizeof(nbr_id)) {
		memcpy(&nbr_id, buf, sizeof(nbr_id));
		if (nbr_id == iface->addr.s_addr) {
			/* seen myself */
			if (nbr->state < NBR_STA_2_WAY)
				nbr_fsm(nbr, NBR_EVT_2_WAY_RCVD);
			break;
		}
		buf += sizeof(nbr_id);
		len -= sizeof(nbr_id);
	}

	if (len == 0) {
		nbr_fsm(nbr, NBR_EVT_1_WAY_RCVD);
		return;
	}

	/* XXX len correct?? */

	return;
}
