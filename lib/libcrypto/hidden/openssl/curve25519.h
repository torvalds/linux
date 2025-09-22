/* $OpenBSD: curve25519.h,v 1.1 2023/07/08 15:12:49 beck Exp $ */
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

#ifndef _LIBCRYPTO_CURVE25519_H
#define _LIBCRYPTO_CURVE25519_H

#ifndef _MSC_VER
#include_next <openssl/curve25519.h>
#else
#include "../include/openssl/curve25519.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(X25519_keypair);
LCRYPTO_USED(X25519);
LCRYPTO_USED(ED25519_keypair);
LCRYPTO_USED(ED25519_sign);
LCRYPTO_USED(ED25519_verify);

#endif /* _LIBCRYPTO_CURVE25519_H */
