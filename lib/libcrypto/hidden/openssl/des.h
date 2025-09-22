/* $OpenBSD: des.h,v 1.3 2024/08/31 10:30:16 tb Exp $ */
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

#ifndef _LIBCRYPTO_DES_H
#define _LIBCRYPTO_DES_H

#ifndef _MSC_VER
#include_next <openssl/des.h>
#else
#include "../include/openssl/des.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(DES_ecb3_encrypt);
LCRYPTO_USED(DES_cbc_cksum);
LCRYPTO_USED(DES_cbc_encrypt);
LCRYPTO_USED(DES_ncbc_encrypt);
LCRYPTO_USED(DES_xcbc_encrypt);
LCRYPTO_USED(DES_cfb_encrypt);
LCRYPTO_USED(DES_ecb_encrypt);
LCRYPTO_USED(DES_encrypt1);
LCRYPTO_USED(DES_encrypt2);
LCRYPTO_USED(DES_encrypt3);
LCRYPTO_USED(DES_decrypt3);
LCRYPTO_USED(DES_ede3_cbc_encrypt);
LCRYPTO_USED(DES_ede3_cbcm_encrypt);
LCRYPTO_USED(DES_ede3_cfb64_encrypt);
LCRYPTO_USED(DES_ede3_cfb_encrypt);
LCRYPTO_USED(DES_ede3_ofb64_encrypt);
LCRYPTO_USED(DES_fcrypt);
LCRYPTO_USED(DES_crypt);
LCRYPTO_USED(DES_ofb_encrypt);
LCRYPTO_USED(DES_pcbc_encrypt);
LCRYPTO_USED(DES_quad_cksum);
LCRYPTO_USED(DES_random_key);
LCRYPTO_USED(DES_set_odd_parity);
LCRYPTO_USED(DES_check_key_parity);
LCRYPTO_USED(DES_is_weak_key);
LCRYPTO_USED(DES_set_key);
LCRYPTO_USED(DES_key_sched);
LCRYPTO_USED(DES_set_key_checked);
LCRYPTO_USED(DES_set_key_unchecked);
LCRYPTO_USED(DES_string_to_key);
LCRYPTO_USED(DES_string_to_2keys);
LCRYPTO_USED(DES_cfb64_encrypt);
LCRYPTO_USED(DES_ofb64_encrypt);
#if defined(LIBRESSL_NAMESPACE)
extern LCRYPTO_USED(DES_check_key);
#endif

#endif /* _LIBCRYPTO_DES_H */
