/* $OpenBSD: lhash.h,v 1.4 2024/03/02 11:11:11 tb Exp $ */
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

#ifndef _LIBCRYPTO_LHASH_H
#define _LIBCRYPTO_LHASH_H

#ifndef _MSC_VER
#include_next <openssl/lhash.h>
#else
#include "../include/openssl/lhash.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(lh_new);
LCRYPTO_USED(lh_free);
LCRYPTO_USED(lh_error);
LCRYPTO_USED(lh_insert);
LCRYPTO_USED(lh_delete);
LCRYPTO_USED(lh_retrieve);
LCRYPTO_USED(lh_doall);
LCRYPTO_USED(lh_doall_arg);
LCRYPTO_USED(lh_strhash);
LCRYPTO_USED(lh_num_items);

#endif /* _LIBCRYPTO_LHASH_H */
