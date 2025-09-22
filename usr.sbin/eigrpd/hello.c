/*	$OpenBSD: hello.c,v 1.6 2022/12/28 21:30:16 jmc Exp $ */

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

#include <arpa/inet.h>
#include <string.h>

#include "eigrpd.h"
#include "eigrpe.h"
#include "log.h"

/* hello packet handling */

void
send_hello(struct eigrp_iface *ei, struct seq_addr_head *seq_addr_list,
    uint32_t mcast_seq)
{
	struct eigrp		*eigrp = ei->eigrp;
	struct ibuf		*buf;
	uint8_t			 flags = 0;

	if ((buf = ibuf_dynamic(PKG_DEF_SIZE,
	    IP_MAXPACKET - sizeof(struct ip))) == NULL)
		fatal("send_hello");

	/* EIGRP header */
	if (gen_eigrp_hdr(buf, EIGRP_OPC_HELLO, flags, 0, eigrp->as))
		goto fail;

	if (gen_parameter_tlv(buf, ei, 0))
		goto fail;

	if (gen_sw_version_tlv(buf))
		goto fail;

	if (seq_addr_list && !TAILQ_EMPTY(seq_addr_list) &&
	    gen_sequence_tlv(buf, seq_addr_list))
		goto fail;

	if (mcast_seq && gen_mcast_seq_tlv(buf, mcast_seq))
		goto fail;

	/* send unreliably */
	send_packet(ei, NULL, 0, buf);
	ibuf_free(buf);
	return;
fail:
	log_warnx("%s: failed to send message", __func__);
	ibuf_free(buf);
}

void
send_peerterm(struct nbr *nbr)
{
	struct eigrp		*eigrp = nbr->ei->eigrp;
	struct ibuf		*buf;
	uint8_t			 flags = 0;

	if ((buf = ibuf_dynamic(PKG_DEF_SIZE,
	    IP_MAXPACKET - sizeof(struct ip))) == NULL)
		fatal("send_hello");

	/* EIGRP header */
	if (gen_eigrp_hdr(buf, EIGRP_OPC_HELLO, flags, 0, eigrp->as))
		goto fail;

	if (gen_parameter_tlv(buf, nbr->ei, 1))
		goto fail;

	/* send unreliably */
	send_packet(nbr->ei, nbr, 0, buf);
	ibuf_free(buf);
	return;
fail:
	log_warnx("%s: failed to send message", __func__);
	ibuf_free(buf);
}


void
recv_hello(struct eigrp_iface *ei, union eigrpd_addr *src, struct nbr *nbr,
    struct tlv_parameter *tp)
{
	/*
	 * Some old Cisco routers seems to use the "parameters tlv" with all
	 * K-values set to 255 (except k6) to inform that the neighbor has been
	 * reset. The "peer termination" tlv described in the draft for the same
	 * purpose is probably something introduced recently. Let's check for
	 * this case to maintain backward compatibility.
	 */
	if (tp->kvalues[0] == 255 && tp->kvalues[1] == 255 &&
	    tp->kvalues[2] == 255 && tp->kvalues[3] == 255 &&
	    tp->kvalues[4] == 255 && tp->kvalues[5] == 0) {
		if (nbr) {
			log_debug("%s: peer termination", __func__);
			nbr_del(nbr);
		}
		return;
	}

	if (nbr == NULL) {
		if (memcmp(ei->eigrp->kvalues, tp->kvalues, 6) != 0) {
			log_debug("%s: k-values don't match (nbr %s)",
			    __func__, log_addr(ei->eigrp->af, src));
			return;
		}

		nbr = nbr_new(ei, src, ntohs(tp->holdtime), 0);

		/* send an expedited hello */
		send_hello(ei, NULL, 0);

		send_update(nbr->ei, nbr, EIGRP_HDR_FLAG_INIT, NULL);
	}
}
