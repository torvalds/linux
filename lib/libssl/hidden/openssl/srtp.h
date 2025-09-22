/* $OpenBSD: srtp.h,v 1.1 2023/07/08 16:40:14 beck Exp $ */
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

#ifndef _LIBSSL_SRTP_H
#define _LIBSSL_SRTP_H

#ifndef _MSC_VER
#include_next <openssl/srtp.h>
#else
#include "../include/openssl/srtp.h"
#endif
#include "ssl_namespace.h"

LSSL_USED(SSL_CTX_set_tlsext_use_srtp);
LSSL_USED(SSL_set_tlsext_use_srtp);
LSSL_USED(SSL_get_srtp_profiles);
LSSL_USED(SSL_get_selected_srtp_profile);

#endif /* _LIBSSL_SRTP_H */
