/* $OpenBSD: asn1t.h,v 1.6 2025/08/22 14:07:34 tb Exp $ */
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

#ifndef _LIBCRYPTO_ASN1T_H
#define _LIBCRYPTO_ASN1T_H

#ifndef _MSC_VER
#include_next <openssl/asn1t.h>
#else
#include "../include/openssl/asn1t.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(ASN1_item_ex_new);
LCRYPTO_USED(ASN1_item_ex_free);
LCRYPTO_USED(ASN1_item_ex_d2i);
LCRYPTO_USED(ASN1_item_ex_i2d);
#if defined(LIBRESSL_NAMESPACE)
extern LCRYPTO_USED(ASN1_BOOLEAN_it);
extern LCRYPTO_USED(ASN1_TBOOLEAN_it);
extern LCRYPTO_USED(ASN1_FBOOLEAN_it);
extern LCRYPTO_USED(ASN1_SEQUENCE_it);
extern LCRYPTO_USED(BIGNUM_it);
extern LCRYPTO_USED(LONG_it);
extern LCRYPTO_USED(ZLONG_it);
extern LCRYPTO_UNUSED(CBIGNUM_it);
#endif

#endif /* _LIBCRYPTO_ASN1T_H */
