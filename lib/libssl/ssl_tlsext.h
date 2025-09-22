/* $OpenBSD: ssl_tlsext.h,v 1.34 2024/03/26 03:44:11 beck Exp $ */
/*
 * Copyright (c) 2016, 2017 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2017 Doug Hogan <doug@openbsd.org>
 * Copyright (c) 2019 Bob Beck <beck@openbsd.org>
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

#ifndef HEADER_SSL_TLSEXT_H
#define HEADER_SSL_TLSEXT_H

/* TLSv1.3 - RFC 8446 Section 4.2. */
#define SSL_TLSEXT_MSG_CH	0x0001	/* ClientHello */
#define SSL_TLSEXT_MSG_SH	0x0002	/* ServerHello */
#define SSL_TLSEXT_MSG_EE	0x0004	/* EncryptedExtension */
#define SSL_TLSEXT_MSG_CT	0x0008	/* Certificate */
#define SSL_TLSEXT_MSG_CR	0x0010	/* CertificateRequest */
#define SSL_TLSEXT_MSG_NST	0x0020	/* NewSessionTicket */
#define SSL_TLSEXT_MSG_HRR	0x0040	/* HelloRetryRequest */

__BEGIN_HIDDEN_DECLS

int tlsext_alpn_check_format(CBS *cbs);
int tlsext_sni_is_valid_hostname(CBS *cbs, int *is_ip);

int tlsext_client_build(SSL *s, uint16_t msg_type, CBB *cbb);
int tlsext_client_parse(SSL *s, uint16_t msg_type, CBS *cbs, int *alert);

int tlsext_server_build(SSL *s, uint16_t msg_type, CBB *cbb);
int tlsext_server_parse(SSL *s, uint16_t msg_type, CBS *cbs, int *alert);

int tlsext_extension_seen(SSL *s, uint16_t);
int tlsext_extension_processed(SSL *s, uint16_t);
int tlsext_randomize_build_order(SSL *s);

__END_HIDDEN_DECLS

#endif
