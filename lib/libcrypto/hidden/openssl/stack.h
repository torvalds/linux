/* $OpenBSD: stack.h,v 1.3 2024/03/02 11:20:36 tb Exp $ */
/*
 * Copyright (c) 2022 Bob Beck <beck@openbsd.org>
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

#ifndef _LIBCRYPTO_STACK_H
#define _LIBCRYPTO_STACK_H

#ifndef _MSC_VER
#include_next <openssl/stack.h>
#else
#include "../include/openssl/stack.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(sk_num);
LCRYPTO_USED(sk_value);
LCRYPTO_USED(sk_set);
LCRYPTO_USED(sk_new);
LCRYPTO_USED(sk_new_null);
LCRYPTO_USED(sk_free);
LCRYPTO_USED(sk_pop_free);
LCRYPTO_USED(sk_insert);
LCRYPTO_USED(sk_delete);
LCRYPTO_USED(sk_delete_ptr);
LCRYPTO_USED(sk_find);
LCRYPTO_USED(sk_push);
LCRYPTO_USED(sk_unshift);
LCRYPTO_USED(sk_shift);
LCRYPTO_USED(sk_pop);
LCRYPTO_USED(sk_zero);
LCRYPTO_USED(sk_set_cmp_func);
LCRYPTO_USED(sk_dup);
LCRYPTO_USED(sk_sort);
LCRYPTO_USED(sk_is_sorted);

#endif /* _LIBCRYPTO_STACK_H */
