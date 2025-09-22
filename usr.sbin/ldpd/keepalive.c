/*	$OpenBSD: keepalive.c,v 1.17 2016/07/01 23:36:38 renato Exp $ */

/*
 * Copyright (c) 2009 Michele Marchetto <michele@openbsd.org>
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
#include <string.h>

#include "ldpd.h"
#include "ldpe.h"
#include "log.h"

void
send_keepalive(struct nbr *nbr)
{
	struct ibuf	*buf;
	uint16_t	 size;

	size = LDP_HDR_SIZE + LDP_MSG_SIZE;
	if ((buf = ibuf_open(size)) == NULL)
		fatal(__func__);

	gen_ldp_hdr(buf, size);
	size -= LDP_HDR_SIZE;
	gen_msg_hdr(buf, MSG_TYPE_KEEPALIVE, size);

	evbuf_enqueue(&nbr->tcp->wbuf, buf);
}

int
recv_keepalive(struct nbr *nbr, char *buf, uint16_t len)
{
	struct ldp_msg msg;

	memcpy(&msg, buf, sizeof(msg));
	if (len != LDP_MSG_SIZE) {
		session_shutdown(nbr, S_BAD_MSG_LEN, msg.id, msg.type);
		return (-1);
	}

	if (nbr->state != NBR_STA_OPER)
		nbr_fsm(nbr, NBR_EVT_KEEPALIVE_RCVD);

	return (0);
}
