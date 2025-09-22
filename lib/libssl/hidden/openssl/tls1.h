/* $OpenBSD: tls1.h,v 1.2 2024/03/02 11:44:47 tb Exp $ */
/*
 * Copyright (c) 2023 Bob Beck <beck@openbsd.org>
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

#ifndef _LIBSSL_TLS1_H
#define _LIBSSL_TLS1_H

#ifndef _MSC_VER
#include_next <openssl/tls1.h>
#else
#include "../include/openssl/tls1.h"
#endif
#include "ssl_namespace.h"

LSSL_USED(SSL_get_servername);
LSSL_USED(SSL_get_servername_type);
LSSL_USED(SSL_export_keying_material);
LSSL_USED(SSL_get_peer_signature_type_nid);
LSSL_USED(SSL_get_signature_type_nid);

#endif /* _LIBSSL_TLS1_H */
