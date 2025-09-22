/* $OpenBSD: rsa.h,v 1.3 2024/07/08 17:10:18 beck Exp $ */
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

#ifndef _LIBCRYPTO_RSA_H
#define _LIBCRYPTO_RSA_H

#ifndef _MSC_VER
#include_next <openssl/rsa.h>
#else
#include "../include/openssl/rsa.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(RSA_new);
LCRYPTO_USED(RSA_new_method);
LCRYPTO_USED(RSA_bits);
LCRYPTO_USED(RSA_size);
LCRYPTO_USED(RSA_generate_key);
LCRYPTO_USED(RSA_generate_key_ex);
LCRYPTO_USED(RSA_check_key);
LCRYPTO_USED(RSA_public_encrypt);
LCRYPTO_USED(RSA_private_encrypt);
LCRYPTO_USED(RSA_public_decrypt);
LCRYPTO_USED(RSA_private_decrypt);
LCRYPTO_USED(RSA_free);
LCRYPTO_USED(RSA_up_ref);
LCRYPTO_USED(RSA_flags);
LCRYPTO_USED(RSA_set_default_method);
LCRYPTO_USED(RSA_get_default_method);
LCRYPTO_USED(RSA_get_method);
LCRYPTO_USED(RSA_set_method);
LCRYPTO_USED(RSA_PKCS1_OpenSSL);
LCRYPTO_USED(RSA_PKCS1_SSLeay);
LCRYPTO_USED(RSA_pkey_ctx_ctrl);
LCRYPTO_USED(d2i_RSAPublicKey);
LCRYPTO_USED(i2d_RSAPublicKey);
LCRYPTO_USED(d2i_RSAPrivateKey);
LCRYPTO_USED(i2d_RSAPrivateKey);
LCRYPTO_USED(RSA_PSS_PARAMS_new);
LCRYPTO_USED(RSA_PSS_PARAMS_free);
LCRYPTO_USED(d2i_RSA_PSS_PARAMS);
LCRYPTO_USED(i2d_RSA_PSS_PARAMS);
LCRYPTO_USED(RSA_OAEP_PARAMS_new);
LCRYPTO_USED(RSA_OAEP_PARAMS_free);
LCRYPTO_USED(d2i_RSA_OAEP_PARAMS);
LCRYPTO_USED(i2d_RSA_OAEP_PARAMS);
LCRYPTO_USED(RSA_print_fp);
LCRYPTO_USED(RSA_print);
LCRYPTO_USED(RSA_sign);
LCRYPTO_USED(RSA_verify);
LCRYPTO_USED(RSA_sign_ASN1_OCTET_STRING);
LCRYPTO_USED(RSA_verify_ASN1_OCTET_STRING);
LCRYPTO_USED(RSA_blinding_on);
LCRYPTO_USED(RSA_blinding_off);
LCRYPTO_USED(RSA_padding_add_PKCS1_type_1);
LCRYPTO_USED(RSA_padding_check_PKCS1_type_1);
LCRYPTO_USED(RSA_padding_add_PKCS1_type_2);
LCRYPTO_USED(RSA_padding_check_PKCS1_type_2);
LCRYPTO_USED(PKCS1_MGF1);
LCRYPTO_USED(RSA_padding_add_PKCS1_OAEP);
LCRYPTO_USED(RSA_padding_check_PKCS1_OAEP);
LCRYPTO_USED(RSA_padding_add_PKCS1_OAEP_mgf1);
LCRYPTO_USED(RSA_padding_check_PKCS1_OAEP_mgf1);
LCRYPTO_USED(RSA_padding_add_none);
LCRYPTO_USED(RSA_padding_check_none);
LCRYPTO_USED(RSA_verify_PKCS1_PSS);
LCRYPTO_USED(RSA_padding_add_PKCS1_PSS);
LCRYPTO_USED(RSA_verify_PKCS1_PSS_mgf1);
LCRYPTO_USED(RSA_padding_add_PKCS1_PSS_mgf1);
LCRYPTO_USED(RSA_get_ex_new_index);
LCRYPTO_USED(RSA_set_ex_data);
LCRYPTO_USED(RSA_get_ex_data);
LCRYPTO_USED(RSA_security_bits);
LCRYPTO_USED(RSA_get0_key);
LCRYPTO_USED(RSA_set0_key);
LCRYPTO_USED(RSA_get0_crt_params);
LCRYPTO_USED(RSA_set0_crt_params);
LCRYPTO_USED(RSA_get0_factors);
LCRYPTO_USED(RSA_set0_factors);
LCRYPTO_USED(RSA_get0_n);
LCRYPTO_USED(RSA_get0_e);
LCRYPTO_USED(RSA_get0_d);
LCRYPTO_USED(RSA_get0_p);
LCRYPTO_USED(RSA_get0_q);
LCRYPTO_USED(RSA_get0_dmp1);
LCRYPTO_USED(RSA_get0_dmq1);
LCRYPTO_USED(RSA_get0_iqmp);
LCRYPTO_USED(RSA_get0_pss_params);
LCRYPTO_USED(RSA_clear_flags);
LCRYPTO_USED(RSA_test_flags);
LCRYPTO_USED(RSA_set_flags);
LCRYPTO_USED(RSAPublicKey_dup);
LCRYPTO_USED(RSAPrivateKey_dup);
LCRYPTO_USED(RSA_meth_new);
LCRYPTO_USED(RSA_meth_free);
LCRYPTO_USED(RSA_meth_dup);
LCRYPTO_USED(RSA_meth_set1_name);
LCRYPTO_USED(RSA_meth_set_priv_enc);
LCRYPTO_USED(RSA_meth_set_priv_dec);
LCRYPTO_USED(RSA_meth_get_finish);
LCRYPTO_USED(RSA_meth_set_finish);
LCRYPTO_USED(RSA_meth_set_pub_enc);
LCRYPTO_USED(RSA_meth_set_pub_dec);
LCRYPTO_USED(RSA_meth_set_mod_exp);
LCRYPTO_USED(RSA_meth_set_bn_mod_exp);
LCRYPTO_USED(RSA_meth_set_init);
LCRYPTO_USED(RSA_meth_set_keygen);
LCRYPTO_USED(RSA_meth_set_flags);
LCRYPTO_USED(RSA_meth_set0_app_data);
LCRYPTO_USED(RSA_meth_get0_name);
LCRYPTO_USED(RSA_meth_get_pub_enc);
LCRYPTO_USED(RSA_meth_get_pub_dec);
LCRYPTO_USED(RSA_meth_get_priv_enc);
LCRYPTO_USED(RSA_meth_get_priv_dec);
LCRYPTO_USED(RSA_meth_get_mod_exp);
LCRYPTO_USED(RSA_meth_get_bn_mod_exp);
LCRYPTO_USED(RSA_meth_get_init);
LCRYPTO_USED(RSA_meth_get_keygen);
LCRYPTO_USED(RSA_meth_get_flags);
LCRYPTO_USED(RSA_meth_get0_app_data);
LCRYPTO_USED(RSA_meth_get_sign);
LCRYPTO_USED(RSA_meth_set_sign);
LCRYPTO_USED(RSA_meth_get_verify);
LCRYPTO_USED(RSA_meth_set_verify);
LCRYPTO_USED(ERR_load_RSA_strings);
#if defined(LIBRESSL_NAMESPACE)
extern LCRYPTO_USED(RSAPublicKey_it);
extern LCRYPTO_USED(RSAPrivateKey_it);
extern LCRYPTO_USED(RSA_PSS_PARAMS_it);
extern LCRYPTO_USED(RSA_OAEP_PARAMS_it);
#endif

#endif /* _LIBCRYPTO_RSA_H */
