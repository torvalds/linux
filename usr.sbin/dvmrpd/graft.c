/*	$OpenBSD: graft.c,v 1.6 2023/06/26 10:08:56 claudio Exp $ */

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

#include "igmp.h"
#include "dvmrpd.h"
#include "dvmrp.h"
#include "log.h"
#include "dvmrpe.h"

/* DVMRP graft packet handling */
int
send_graft(struct iface *iface, struct in_addr addr, void *data, int len)
{
	struct sockaddr_in	 dst;
	struct ibuf		*buf;
	int			 ret = 0;

	log_debug("send_graft: interface %s addr %s",
	    iface->name, inet_ntoa(addr));

	if (iface->passive)
		return (0);

	if ((buf = ibuf_open(iface->mtu - sizeof(struct ip))) == NULL)
		fatal("send_graft");

	/* DVMRP header */
	if (gen_dvmrp_hdr(buf, iface, DVMRP_CODE_GRAFT))
		goto fail;

	dst.sin_family = AF_INET;
	dst.sin_len = sizeof(struct sockaddr_in);
	dst.sin_addr.s_addr = addr.s_addr;

	ret = send_packet(iface, buf, &dst);
	ibuf_free(buf);
	return (ret);
fail:
	log_warn("send_graft");
	ibuf_free(buf);
	return (-1);
}

void
recv_graft(struct nbr *nbr, char *buf, u_int16_t len)
{
	log_debug("recv_graft: neighbor ID %s", inet_ntoa(nbr->id));

	return;
}
