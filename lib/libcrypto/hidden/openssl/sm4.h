/* $OpenBSD: sm4.h,v 1.2 2023/07/07 19:37:54 beck Exp $ */
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

#ifndef _LIBCRYPTO_SM4_H
#define _LIBCRYPTO_SM4_H

#ifndef _MSC_VER
#include_next <openssl/sm4.h>
#else
#include "../include/openssl/sm4.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(SM4_set_key);
LCRYPTO_USED(SM4_decrypt);
LCRYPTO_USED(SM4_encrypt);

#endif /* _LIBCRYPTO_SM4_H */
