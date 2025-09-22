/* $OpenBSD: blowfish.h,v 1.1 2024/03/29 02:37:20 joshua Exp $ */
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

#ifndef _LIBCRYPTO_BLOWFISH_H
#define _LIBCRYPTO_BLOWFISH_H

#ifndef _MSC_VER
#include_next <openssl/blowfish.h>
#else
#include "../include/openssl/blowfish.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(BF_set_key);
LCRYPTO_USED(BF_encrypt);
LCRYPTO_USED(BF_decrypt);
LCRYPTO_USED(BF_ecb_encrypt);
LCRYPTO_USED(BF_cbc_encrypt);
LCRYPTO_USED(BF_cfb64_encrypt);
LCRYPTO_USED(BF_ofb64_encrypt);

#endif /* _LIBCRYPTO_BLOWFISH_H */
