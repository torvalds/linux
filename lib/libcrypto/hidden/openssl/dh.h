/* $OpenBSD: dh.h,v 1.1 2023/07/08 15:29:04 beck Exp $ */
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

#ifndef _LIBCRYPTO_DH_H
#define _LIBCRYPTO_DH_H

#ifndef _MSC_VER
#include_next <openssl/dh.h>
#else
#include "../include/openssl/dh.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(d2i_DHparams_bio);
LCRYPTO_USED(i2d_DHparams_bio);
LCRYPTO_USED(d2i_DHparams_fp);
LCRYPTO_USED(i2d_DHparams_fp);
LCRYPTO_USED(DHparams_dup);
LCRYPTO_USED(DH_OpenSSL);
LCRYPTO_USED(DH_set_default_method);
LCRYPTO_USED(DH_get_default_method);
LCRYPTO_USED(DH_set_method);
LCRYPTO_USED(DH_new_method);
LCRYPTO_USED(DH_new);
LCRYPTO_USED(DH_free);
LCRYPTO_USED(DH_up_ref);
LCRYPTO_USED(DH_size);
LCRYPTO_USED(DH_bits);
LCRYPTO_USED(DH_get_ex_new_index);
LCRYPTO_USED(DH_set_ex_data);
LCRYPTO_USED(DH_get_ex_data);
LCRYPTO_USED(DH_security_bits);
LCRYPTO_USED(DH_get0_engine);
LCRYPTO_USED(DH_get0_pqg);
LCRYPTO_USED(DH_set0_pqg);
LCRYPTO_USED(DH_get0_key);
LCRYPTO_USED(DH_set0_key);
LCRYPTO_USED(DH_get0_p);
LCRYPTO_USED(DH_get0_q);
LCRYPTO_USED(DH_get0_g);
LCRYPTO_USED(DH_get0_priv_key);
LCRYPTO_USED(DH_get0_pub_key);
LCRYPTO_USED(DH_clear_flags);
LCRYPTO_USED(DH_test_flags);
LCRYPTO_USED(DH_set_flags);
LCRYPTO_USED(DH_get_length);
LCRYPTO_USED(DH_set_length);
LCRYPTO_USED(DH_generate_parameters);
LCRYPTO_USED(DH_generate_parameters_ex);
LCRYPTO_USED(DH_check);
LCRYPTO_USED(DH_check_pub_key);
LCRYPTO_USED(DH_generate_key);
LCRYPTO_USED(DH_compute_key);
LCRYPTO_USED(d2i_DHparams);
LCRYPTO_USED(i2d_DHparams);
LCRYPTO_USED(DHparams_print_fp);
LCRYPTO_USED(DHparams_print);
LCRYPTO_USED(ERR_load_DH_strings);

#endif /* _LIBCRYPTO_DH_H */
