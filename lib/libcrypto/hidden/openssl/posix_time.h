/* $OpenBSD: posix_time.h,v 1.1 2024/02/18 16:28:38 tb Exp $ */
/*
 * Copyright (c) 2024 Bob Beck <beck@openbsd.org>
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

#ifndef _LIBCRYPTO_POSIX_TIME_H
#define _LIBCRYPTO_POSIX_TIME_H

#ifndef _MSC_VER
#include_next <openssl/posix_time.h>
#else
#include "../include/openssl/posix_time.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(OPENSSL_posix_to_tm);
LCRYPTO_USED(OPENSSL_tm_to_posix);
LCRYPTO_USED(OPENSSL_timegm);

#endif /* _LIBCRYPTO_POSIX_TIME_H */
