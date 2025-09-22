/*	$OpenBSD: logmsg.c,v 1.1 2016/09/02 16:22:31 benno Exp $ */

/*
 * Copyright (c) 2009 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

#include <arpa/inet.h>
#include <endian.h>
#include <event.h>
#include <scsi/iscsi.h>
#include <stdio.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "iscsid.h"
#include "log.h"

extern int	 debug;
extern int	 verbose;

void
log_hexdump(void *buf, size_t len)
{
	u_char b[16];
	size_t i, j, l;

	if (!debug)
		return;

	for (i = 0; i < len; i += l) {
		fprintf(stderr, "%4zi:", i);
		l = sizeof(b) < len - i ? sizeof(b) : len - i;
		memcpy(b, (char *)buf + i, l);

		for (j = 0; j < sizeof(b); j++) {
			if (j % 2 == 0)
				fprintf(stderr, " ");
			if (j % 8 == 0)
				fprintf(stderr, " ");
			if (j < l)
				fprintf(stderr, "%02x", (int)b[j]);
			else
				fprintf(stderr, "  ");
		}
		fprintf(stderr, "  |");
		for (j = 0; j < l; j++) {
			if (b[j] >= 0x20 && b[j] <= 0x7e)
				fprintf(stderr, "%c", b[j]);
			else
				fprintf(stderr, ".");
		}
		fprintf(stderr, "|\n");
	}
}

void
log_pdu(struct pdu *p, int all)
{
	struct iscsi_pdu *pdu;
	void *b;
	size_t s;

	if (!debug)
		return;

	if (!(pdu = pdu_getbuf(p, NULL, PDU_HEADER))) {
		log_debug("empty pdu");
		return;
	}

	fprintf(stderr, "PDU: op %x%s flags %02x%02x%02x ahs %d len %d\n",
		ISCSI_PDU_OPCODE(pdu->opcode), ISCSI_PDU_I(pdu) ? " I" : "",
		pdu->flags, pdu->_reserved1[0], pdu->_reserved1[1],
		pdu->ahslen, pdu->datalen[0] << 16 | pdu->datalen[1] << 8 |
		pdu->datalen[2]);
	fprintf(stderr, "     lun %02x%02x%02x%02x%02x%02x%02x%02x itt %u "
	    "cmdsn %u expstatsn %u\n", pdu->lun[0], pdu->lun[1], pdu->lun[2],
	    pdu->lun[3], pdu->lun[4], pdu->lun[5], pdu->lun[6], pdu->lun[7],
	    ntohl(pdu->itt), ntohl(pdu->cmdsn), ntohl(pdu->expstatsn));
	log_hexdump(pdu, sizeof(*pdu));

	if (all && (b = pdu_getbuf(p, &s, PDU_DATA)))
		log_hexdump(b, s);
}
