/* $OpenBSD: ssl_packet.c,v 1.16 2024/06/28 13:37:49 jsing Exp $ */
/*
 * Copyright (c) 2016, 2017 Joel Sing <jsing@openbsd.org>
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

#include "bytestring.h"
#include "ssl_local.h"

static int
ssl_is_sslv3_handshake(CBS *header)
{
	uint16_t record_version;
	uint8_t record_type;
	CBS cbs;

	CBS_dup(header, &cbs);

	if (!CBS_get_u8(&cbs, &record_type) ||
	    !CBS_get_u16(&cbs, &record_version))
		return 0;

	if (record_type != SSL3_RT_HANDSHAKE)
		return 0;
	if ((record_version >> 8) != SSL3_VERSION_MAJOR)
		return 0;

	return 1;
}

/*
 * Potentially do legacy processing on the first packet received by a TLS
 * server. We return 1 if we want SSLv3/TLS record processing to continue
 * normally, otherwise we must set an SSLerr and return -1.
 */
int
ssl_server_legacy_first_packet(SSL *s)
{
	const char *data;
	CBS header;

	if (SSL_is_dtls(s))
		return 1;

	CBS_init(&header, s->packet, SSL3_RT_HEADER_LENGTH);

	if (ssl_is_sslv3_handshake(&header) == 1)
		return 1;

	/* Only continue if this is not a version locked method. */
	if (s->method->min_tls_version == s->method->max_tls_version)
		return 1;

	/* Ensure that we have SSL3_RT_HEADER_LENGTH (5 bytes) of the packet. */
	if (CBS_len(&header) != SSL3_RT_HEADER_LENGTH) {
		SSLerror(s, ERR_R_INTERNAL_ERROR);
		return -1;
	}
	data = (const char *)CBS_data(&header);

	/* Is this a cleartext protocol? */
	if (strncmp("GET ", data, 4) == 0 ||
	    strncmp("POST ", data, 5) == 0 ||
	    strncmp("HEAD ", data, 5) == 0 ||
	    strncmp("PUT ", data, 4) == 0) {
		SSLerror(s, SSL_R_HTTP_REQUEST);
		return -1;
	}
	if (strncmp("CONNE", data, 5) == 0) {
		SSLerror(s, SSL_R_HTTPS_PROXY_REQUEST);
		return -1;
	}

	SSLerror(s, SSL_R_UNKNOWN_PROTOCOL);

	return -1;
}
