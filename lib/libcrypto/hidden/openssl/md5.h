/* $OpenBSD: md5.h,v 1.1 2023/07/08 10:45:57 beck Exp $ */
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

#ifndef _LIBCRYPTO_MD5_H
#define _LIBCRYPTO_MD5_H

#ifndef _MSC_VER
#include_next <openssl/md5.h>
#else
#include "../include/openssl/md5.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(MD5_Init);
LCRYPTO_USED(MD5_Update);
LCRYPTO_USED(MD5_Final);
LCRYPTO_USED(MD5);
LCRYPTO_USED(MD5_Transform);

#endif /* _LIBCRYPTO_MD5_H */
