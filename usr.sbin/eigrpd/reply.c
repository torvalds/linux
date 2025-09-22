/*	$OpenBSD: reply.c,v 1.5 2016/09/02 16:46:29 renato Exp $ */

/*
 * Copyright (c) 2015 Renato Westphal <renato@openbsd.org>
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
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>

#include <stdlib.h>

#include "eigrpd.h"
#include "eigrpe.h"
#include "log.h"

/* reply packet handling */

void
send_reply(struct nbr *nbr, struct rinfo_head *rinfo_list, int siareply)
{
	struct eigrp		*eigrp = nbr->ei->eigrp;
	struct ibuf		*buf;
	uint16_t		 opcode;
	struct rinfo_entry	*re;
	int			 size;
	int			 route_len;

	if (rinfo_list == NULL || TAILQ_EMPTY(rinfo_list))
		return;

	do {
		if ((buf = ibuf_dynamic(PKG_DEF_SIZE,
		    IP_MAXPACKET - sizeof(struct ip))) == NULL)
			fatal("send_reply");

		if (!siareply)
			 opcode = EIGRP_OPC_REPLY;
		else
			 opcode = EIGRP_OPC_SIAREPLY;

		/* EIGRP header */
		if (gen_eigrp_hdr(buf, opcode, 0, eigrp->seq_num, eigrp->as))
			goto fail;

		switch (eigrp->af) {
		case AF_INET:
			size = sizeof(struct ip);
			break;
		case AF_INET6:
			size = sizeof(struct ip6_hdr);
			break;
		default:
			fatalx("send_reply: unknown af");
		}
		size += sizeof(struct eigrp_hdr);

		while ((re = TAILQ_FIRST(rinfo_list)) != NULL) {
			route_len = len_route_tlv(&re->rinfo);
			/* don't exceed the MTU to avoid IP fragmentation */
			if (size + route_len > nbr->ei->iface->mtu) {
				rtp_send_ucast(nbr, buf);
				break;
			}
			size += route_len;

			if (gen_route_tlv(buf, &re->rinfo))
				goto fail;
			TAILQ_REMOVE(rinfo_list, re, entry);
			free(re);
		}
	} while (!TAILQ_EMPTY(rinfo_list));

	/* reply packets are always unicast */
	rtp_send_ucast(nbr, buf);
	return;
fail:
	log_warnx("%s: failed to send message", __func__);
	if (rinfo_list)
		message_list_clr(rinfo_list);
	ibuf_free(buf);
	return;
}

void
recv_reply(struct nbr *nbr, struct rinfo_head *rinfo_list, int siareply)
{
	int			 type;
	struct rinfo_entry	*re;

	/*
	 * draft-savage-eigrp-02 - Section 4.3:
	 * "When a REPLY packet is received, there is no reason to process
	 * the packet before an acknowledgment is sent. Therefore, an Ack
	 * packet is sent immediately and then the packet is processed."
	 */
	rtp_send_ack(nbr);

	if (!siareply)
		type = IMSG_RECV_REPLY;
	else
		type = IMSG_RECV_SIAREPLY;

	TAILQ_FOREACH(re, rinfo_list, entry)
		eigrpe_imsg_compose_rde(type, nbr->peerid, 0, &re->rinfo,
		    sizeof(re->rinfo));
}
