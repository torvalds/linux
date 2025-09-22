/* $OpenBSD: dsa.h,v 1.3 2024/07/08 17:11:05 beck Exp $ */
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

#ifndef _LIBCRYPTO_DSA_H
#define _LIBCRYPTO_DSA_H

#ifndef _MSC_VER
#include_next <openssl/dsa.h>
#else
#include "../include/openssl/dsa.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(d2i_DSAparams_bio);
LCRYPTO_USED(i2d_DSAparams_bio);
LCRYPTO_USED(d2i_DSAparams_fp);
LCRYPTO_USED(i2d_DSAparams_fp);
LCRYPTO_USED(DSAparams_dup);
LCRYPTO_USED(DSA_SIG_new);
LCRYPTO_USED(DSA_SIG_free);
LCRYPTO_USED(i2d_DSA_SIG);
LCRYPTO_USED(d2i_DSA_SIG);
LCRYPTO_USED(DSA_SIG_get0);
LCRYPTO_USED(DSA_SIG_set0);
LCRYPTO_USED(DSA_do_sign);
LCRYPTO_USED(DSA_do_verify);
LCRYPTO_USED(DSA_OpenSSL);
LCRYPTO_USED(DSA_set_default_method);
LCRYPTO_USED(DSA_get_default_method);
LCRYPTO_USED(DSA_set_method);
LCRYPTO_USED(DSA_new);
LCRYPTO_USED(DSA_new_method);
LCRYPTO_USED(DSA_free);
LCRYPTO_USED(DSA_up_ref);
LCRYPTO_USED(DSA_size);
LCRYPTO_USED(DSA_bits);
LCRYPTO_USED(DSA_sign_setup);
LCRYPTO_USED(DSA_sign);
LCRYPTO_USED(DSA_verify);
LCRYPTO_USED(DSA_get_ex_new_index);
LCRYPTO_USED(DSA_set_ex_data);
LCRYPTO_USED(DSA_get_ex_data);
LCRYPTO_USED(DSA_security_bits);
LCRYPTO_USED(d2i_DSAPublicKey);
LCRYPTO_USED(i2d_DSAPublicKey);
LCRYPTO_USED(d2i_DSAPrivateKey);
LCRYPTO_USED(i2d_DSAPrivateKey);
LCRYPTO_USED(d2i_DSAparams);
LCRYPTO_USED(i2d_DSAparams);
LCRYPTO_USED(DSA_generate_parameters_ex);
LCRYPTO_USED(DSA_generate_key);
LCRYPTO_USED(DSAparams_print);
LCRYPTO_USED(DSA_print);
LCRYPTO_USED(DSAparams_print_fp);
LCRYPTO_USED(DSA_print_fp);
LCRYPTO_USED(DSA_dup_DH);
LCRYPTO_USED(DSA_get0_pqg);
LCRYPTO_USED(DSA_set0_pqg);
LCRYPTO_USED(DSA_get0_key);
LCRYPTO_USED(DSA_set0_key);
LCRYPTO_USED(DSA_get0_p);
LCRYPTO_USED(DSA_get0_q);
LCRYPTO_USED(DSA_get0_g);
LCRYPTO_USED(DSA_get0_pub_key);
LCRYPTO_USED(DSA_get0_priv_key);
LCRYPTO_USED(DSA_clear_flags);
LCRYPTO_USED(DSA_test_flags);
LCRYPTO_USED(DSA_set_flags);
LCRYPTO_USED(DSA_get0_engine);
LCRYPTO_USED(DSA_meth_new);
LCRYPTO_USED(DSA_meth_free);
LCRYPTO_USED(DSA_meth_dup);
LCRYPTO_USED(DSA_meth_get0_name);
LCRYPTO_USED(DSA_meth_set1_name);
LCRYPTO_USED(DSA_meth_set_sign);
LCRYPTO_USED(DSA_meth_set_finish);
LCRYPTO_USED(ERR_load_DSA_strings);
#if defined(LIBRESSL_NAMESPACE)
extern LCRYPTO_USED(DSAPublicKey_it);
extern LCRYPTO_USED(DSAPrivateKey_it);
extern LCRYPTO_USED(DSAparams_it);
#endif

#endif /* _LIBCRYPTO_DSA_H */
