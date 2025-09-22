/*	$OpenBSD: vendor.c,v 1.6 2017/11/08 13:33:49 patrick Exp $	*/
/*
 * Copyright (c) 2006 Hans-Joerg Hoexer <hshoexer@openbsd.org>
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
#include <stdlib.h>
#include <string.h>

#include "exchange.h"
#include "hash.h"
#include "log.h"
#include "message.h"
#include "vendor.h"

static struct vendor_cap openbsd_vendor_cap[] = {
	{ "OpenBSD-6.3", NULL, 0 },
};

#define NUMVIDS	(sizeof openbsd_vendor_cap / sizeof openbsd_vendor_cap[0])

static int
setup_vendor_hashes(void)
{
	struct hash	*hash;
	int		 i, n = NUMVIDS;

	hash = hash_get(HASH_MD5);
	if (!hash) {
		log_print("setup_vendor_hashes: could not find MD5 hash");
		return -1;
	}

	for (i = 0; i < n; i++) {
		openbsd_vendor_cap[i].hashsize = hash->hashsize;
		openbsd_vendor_cap[i].hash = calloc(hash->hashsize,
		    sizeof(u_int8_t));
		if (openbsd_vendor_cap[i].hash == NULL) {
			log_error("setup_vendor_hashes: calloc failed");
			goto errout;
		}

		hash->Init(hash->ctx);
		hash->Update(hash->ctx,
		    (unsigned char *)openbsd_vendor_cap[i].text,
		    strlen(openbsd_vendor_cap[i].text));
		hash->Final(openbsd_vendor_cap[i].hash, hash->ctx);

		LOG_DBG((LOG_EXCHANGE, 50, "setup_vendor_hashes: "
		    "MD5(\"%s\") (%lu bytes)", openbsd_vendor_cap[i].text,
		    (unsigned long)hash->hashsize));
		LOG_DBG_BUF((LOG_EXCHANGE, 50, "setup_vendor_hashes",
		    openbsd_vendor_cap[i].hash, hash->hashsize));
	}
	return 0;

errout:
	for (i = 0; i < n; i++)
		free(openbsd_vendor_cap[i].hash);
	return -1;
}

void
vendor_init(void)
{
	setup_vendor_hashes();
}

int
add_vendor_openbsd(struct message *msg)
{
	u_int8_t	*buf;
	size_t		 buflen;
	int		 i, n = NUMVIDS;

	for (i = 0; i < n; i++) {
		buflen = openbsd_vendor_cap[i].hashsize + ISAKMP_GEN_SZ;
		if ((buf = calloc(buflen, sizeof(char))) == NULL) {
			log_error("add_vendor_payload: calloc(%lu) failed",
			    (unsigned long)buflen);
			return -1;
		}

		SET_ISAKMP_GEN_LENGTH(buf, buflen);
		memcpy(buf + ISAKMP_VENDOR_ID_OFF, openbsd_vendor_cap[i].hash,
		    openbsd_vendor_cap[i].hashsize);
		if (message_add_payload(msg, ISAKMP_PAYLOAD_VENDOR, buf,
		    buflen, 1)) {
			free(buf);
			return -1;
		}
	}

	return 0;
}

void
check_vendor_openbsd(struct message *msg, struct payload *p)
{
	u_int8_t	*pbuf = p->p;
	ssize_t		 vlen;
	int		 i, n = NUMVIDS;

	if (msg->exchange->flags & EXCHANGE_FLAG_OPENBSD) {
		p->flags |= PL_MARK;
		return;
	}

	vlen = GET_ISAKMP_GEN_LENGTH(pbuf) - ISAKMP_GEN_SZ;

	for (i = 0; i < n; i++) {
		if (vlen != openbsd_vendor_cap[i].hashsize) {
			LOG_DBG((LOG_EXCHANGE, 90,
			    "check_vendor_openbsd: bad size %lu != %lu",
			    (unsigned long)vlen,
			    (unsigned long)openbsd_vendor_cap[i].hashsize));
			continue;
		}
		if (memcmp(openbsd_vendor_cap[i].hash, pbuf + ISAKMP_GEN_SZ,
		    vlen) == 0) {
			msg->exchange->flags |= EXCHANGE_FLAG_OPENBSD;
			LOG_DBG((LOG_EXCHANGE, 10, "check_vendor_openbsd: "
			    "OpenBSD (%s)", openbsd_vendor_cap[i].text));
		}
		p->flags |= PL_MARK;
	}
}
