/* $OpenBSD: idea.h,v 1.2 2023/07/29 03:13:38 tb Exp $ */
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

#ifndef _LIBCRYPTO_IDEA_H
#define _LIBCRYPTO_IDEA_H

#ifndef _MSC_VER
#include_next <openssl/idea.h>
#else
#include "../include/openssl/idea.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(idea_ecb_encrypt);
LCRYPTO_USED(idea_set_encrypt_key);
LCRYPTO_USED(idea_set_decrypt_key);
LCRYPTO_USED(idea_cbc_encrypt);
LCRYPTO_USED(idea_cfb64_encrypt);
LCRYPTO_USED(idea_ofb64_encrypt);
LCRYPTO_USED(idea_encrypt);

#endif /* _LIBCRYPTO_IDEA_H */
