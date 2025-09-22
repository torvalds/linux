/* $OpenBSD: camellia.h,v 1.1 2024/03/30 04:58:12 joshua Exp $ */
/*
 * Copyright (c) 2024 Joshua Sing <joshua@joshuasing.dev>
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

#ifndef _LIBCRYPTO_CAMELLIA_H
#define _LIBCRYPTO_CAMELLIA_H

#ifndef _MSC_VER
#include_next <openssl/camellia.h>
#else
#include "../include/openssl/camellia.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(Camellia_set_key);
LCRYPTO_USED(Camellia_encrypt);
LCRYPTO_USED(Camellia_decrypt);
LCRYPTO_USED(Camellia_ecb_encrypt);
LCRYPTO_USED(Camellia_cbc_encrypt);
LCRYPTO_USED(Camellia_cfb128_encrypt);
LCRYPTO_USED(Camellia_cfb1_encrypt);
LCRYPTO_USED(Camellia_cfb8_encrypt);
LCRYPTO_USED(Camellia_ofb128_encrypt);
LCRYPTO_USED(Camellia_ctr128_encrypt);

#endif /* _LIBCRYPTO_CAMELLIA_H */
