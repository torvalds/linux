/* $OpenBSD: aes.h,v 1.1 2024/03/30 05:14:12 joshua Exp $ */
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

#ifndef _LIBCRYPTO_AES_H
#define _LIBCRYPTO_AES_H

#ifndef _MSC_VER
#include_next <openssl/aes.h>
#else
#include "../include/openssl/aes.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(AES_set_encrypt_key);
LCRYPTO_USED(AES_set_decrypt_key);
LCRYPTO_USED(AES_encrypt);
LCRYPTO_USED(AES_decrypt);
LCRYPTO_USED(AES_ecb_encrypt);
LCRYPTO_USED(AES_cbc_encrypt);
LCRYPTO_USED(AES_cfb128_encrypt);
LCRYPTO_USED(AES_cfb1_encrypt);
LCRYPTO_USED(AES_cfb8_encrypt);
LCRYPTO_USED(AES_ofb128_encrypt);
LCRYPTO_USED(AES_ctr128_encrypt);
LCRYPTO_USED(AES_ige_encrypt);
LCRYPTO_USED(AES_wrap_key);
LCRYPTO_USED(AES_unwrap_key);

#endif /* _LIBCRYPTO_AES_H */
