/*	$OpenBSD: hmac.h,v 1.3 2024/08/31 10:42:21 tb Exp $	*/
/*
 * Copyright (c) 2016 Philip Guenther <guenther@openbsd.org>
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

#ifndef _LIBCRYPTO_HMAC_H_
#define _LIBCRYPTO_HMAC_H_

#ifndef _MSC_VER
#include_next <openssl/hmac.h>
#else
#include "../include/openssl/hmac.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(HMAC_CTX_new);
LCRYPTO_USED(HMAC_CTX_free);
LCRYPTO_UNUSED(HMAC_CTX_reset);
LCRYPTO_USED(HMAC_Init_ex);
LCRYPTO_USED(HMAC_Update);
LCRYPTO_USED(HMAC_Final);
LCRYPTO_USED(HMAC);
LCRYPTO_USED(HMAC_CTX_copy);
LCRYPTO_USED(HMAC_CTX_set_flags);
LCRYPTO_USED(HMAC_CTX_get_md);

#endif /* _LIBCRYPTO_HMAC_H_ */
