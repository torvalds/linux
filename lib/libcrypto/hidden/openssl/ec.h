/* $OpenBSD: ec.h,v 1.12 2025/03/09 15:42:19 tb Exp $ */
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

#ifndef _LIBCRYPTO_EC_H
#define _LIBCRYPTO_EC_H

#ifndef _MSC_VER
#include_next <openssl/ec.h>
#else
#include "../include/openssl/ec.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(EC_GROUP_free);
LCRYPTO_USED(EC_GROUP_dup);
LCRYPTO_USED(EC_GROUP_set_generator);
LCRYPTO_USED(EC_GROUP_get0_generator);
LCRYPTO_USED(EC_GROUP_get_order);
LCRYPTO_USED(EC_GROUP_order_bits);
LCRYPTO_USED(EC_GROUP_get_cofactor);
LCRYPTO_USED(EC_GROUP_set_curve_name);
LCRYPTO_USED(EC_GROUP_get_curve_name);
LCRYPTO_USED(EC_GROUP_set_asn1_flag);
LCRYPTO_USED(EC_GROUP_get_asn1_flag);
LCRYPTO_USED(EC_GROUP_set_point_conversion_form);
LCRYPTO_USED(EC_GROUP_get_point_conversion_form);
LCRYPTO_USED(EC_GROUP_get0_seed);
LCRYPTO_USED(EC_GROUP_get_seed_len);
LCRYPTO_USED(EC_GROUP_set_seed);
LCRYPTO_USED(EC_GROUP_set_curve);
LCRYPTO_USED(EC_GROUP_get_curve);
LCRYPTO_USED(EC_GROUP_get_degree);
LCRYPTO_USED(EC_GROUP_check);
LCRYPTO_USED(EC_GROUP_check_discriminant);
LCRYPTO_USED(EC_GROUP_cmp);
LCRYPTO_USED(EC_GROUP_new_curve_GFp);
LCRYPTO_USED(EC_GROUP_new_by_curve_name);
LCRYPTO_USED(EC_get_builtin_curves);
LCRYPTO_USED(EC_curve_nid2nist);
LCRYPTO_USED(EC_curve_nist2nid);
LCRYPTO_USED(EC_POINT_new);
LCRYPTO_USED(EC_POINT_free);
LCRYPTO_USED(EC_POINT_copy);
LCRYPTO_USED(EC_POINT_dup);
LCRYPTO_USED(EC_POINT_set_to_infinity);
LCRYPTO_USED(EC_POINT_set_affine_coordinates);
LCRYPTO_USED(EC_POINT_get_affine_coordinates);
LCRYPTO_USED(EC_POINT_set_compressed_coordinates);
LCRYPTO_USED(EC_POINT_point2oct);
LCRYPTO_USED(EC_POINT_oct2point);
LCRYPTO_USED(EC_POINT_point2bn);
LCRYPTO_USED(EC_POINT_bn2point);
LCRYPTO_USED(EC_POINT_point2hex);
LCRYPTO_USED(EC_POINT_hex2point);
LCRYPTO_USED(EC_POINT_add);
LCRYPTO_USED(EC_POINT_dbl);
LCRYPTO_USED(EC_POINT_invert);
LCRYPTO_USED(EC_POINT_is_at_infinity);
LCRYPTO_USED(EC_POINT_is_on_curve);
LCRYPTO_USED(EC_POINT_cmp);
LCRYPTO_USED(EC_POINT_make_affine);
LCRYPTO_USED(EC_POINT_mul);
LCRYPTO_USED(EC_GROUP_get_basis_type);
LCRYPTO_USED(d2i_ECPKParameters);
LCRYPTO_USED(i2d_ECPKParameters);
LCRYPTO_USED(ECPKParameters_print);
LCRYPTO_USED(ECPKParameters_print_fp);
LCRYPTO_USED(EC_KEY_new);
LCRYPTO_USED(EC_KEY_get_flags);
LCRYPTO_USED(EC_KEY_set_flags);
LCRYPTO_USED(EC_KEY_clear_flags);
LCRYPTO_USED(EC_KEY_new_by_curve_name);
LCRYPTO_USED(EC_KEY_free);
LCRYPTO_USED(EC_KEY_copy);
LCRYPTO_USED(EC_KEY_dup);
LCRYPTO_USED(EC_KEY_up_ref);
LCRYPTO_USED(EC_KEY_get0_group);
LCRYPTO_USED(EC_KEY_set_group);
LCRYPTO_USED(EC_KEY_get0_private_key);
LCRYPTO_USED(EC_KEY_set_private_key);
LCRYPTO_USED(EC_KEY_get0_public_key);
LCRYPTO_USED(EC_KEY_set_public_key);
LCRYPTO_USED(EC_KEY_get_enc_flags);
LCRYPTO_USED(EC_KEY_set_enc_flags);
LCRYPTO_USED(EC_KEY_get_conv_form);
LCRYPTO_USED(EC_KEY_set_conv_form);
LCRYPTO_USED(EC_KEY_set_asn1_flag);
LCRYPTO_USED(EC_KEY_precompute_mult);
LCRYPTO_USED(EC_KEY_generate_key);
LCRYPTO_USED(EC_KEY_check_key);
LCRYPTO_USED(EC_KEY_set_public_key_affine_coordinates);
LCRYPTO_USED(d2i_ECPrivateKey);
LCRYPTO_USED(i2d_ECPrivateKey);
LCRYPTO_USED(d2i_ECParameters);
LCRYPTO_USED(i2d_ECParameters);
LCRYPTO_USED(o2i_ECPublicKey);
LCRYPTO_USED(i2o_ECPublicKey);
LCRYPTO_USED(ECParameters_print);
LCRYPTO_USED(EC_KEY_print);
LCRYPTO_USED(ECParameters_print_fp);
LCRYPTO_USED(EC_KEY_print_fp);
LCRYPTO_USED(EC_KEY_set_ex_data);
LCRYPTO_USED(EC_KEY_get_ex_data);
LCRYPTO_USED(EC_KEY_OpenSSL);
LCRYPTO_USED(EC_KEY_get_default_method);
LCRYPTO_USED(EC_KEY_set_default_method);
LCRYPTO_USED(EC_KEY_get_method);
LCRYPTO_USED(EC_KEY_set_method);
LCRYPTO_USED(EC_KEY_new_method);
LCRYPTO_USED(ECDH_size);
LCRYPTO_USED(ECDH_compute_key);
LCRYPTO_USED(ECDSA_SIG_new);
LCRYPTO_USED(ECDSA_SIG_free);
LCRYPTO_USED(i2d_ECDSA_SIG);
LCRYPTO_USED(d2i_ECDSA_SIG);
LCRYPTO_USED(ECDSA_SIG_get0_r);
LCRYPTO_USED(ECDSA_SIG_get0_s);
LCRYPTO_USED(ECDSA_SIG_get0);
LCRYPTO_USED(ECDSA_SIG_set0);
LCRYPTO_USED(ECDSA_size);
LCRYPTO_USED(ECDSA_do_sign);
LCRYPTO_USED(ECDSA_do_verify);
LCRYPTO_USED(ECDSA_sign);
LCRYPTO_USED(ECDSA_verify);
LCRYPTO_USED(EC_KEY_METHOD_new);
LCRYPTO_USED(EC_KEY_METHOD_free);
LCRYPTO_USED(EC_KEY_METHOD_set_init);
LCRYPTO_USED(EC_KEY_METHOD_set_keygen);
LCRYPTO_USED(EC_KEY_METHOD_set_compute_key);
LCRYPTO_USED(EC_KEY_METHOD_set_sign);
LCRYPTO_USED(EC_KEY_METHOD_set_verify);
LCRYPTO_USED(EC_KEY_METHOD_get_init);
LCRYPTO_USED(EC_KEY_METHOD_get_keygen);
LCRYPTO_USED(EC_KEY_METHOD_get_compute_key);
LCRYPTO_USED(EC_KEY_METHOD_get_sign);
LCRYPTO_USED(EC_KEY_METHOD_get_verify);
LCRYPTO_USED(ECParameters_dup);
LCRYPTO_USED(ERR_load_EC_strings);
LCRYPTO_UNUSED(EC_GROUP_clear_free);
LCRYPTO_UNUSED(EC_GROUP_set_curve_GFp);
LCRYPTO_UNUSED(EC_GROUP_get_curve_GFp);
LCRYPTO_UNUSED(EC_POINT_clear_free);
LCRYPTO_UNUSED(EC_POINT_set_affine_coordinates_GFp);
LCRYPTO_UNUSED(EC_POINT_get_affine_coordinates_GFp);
LCRYPTO_UNUSED(EC_POINT_set_compressed_coordinates_GFp);

#endif /* _LIBCRYPTO_EC_H */
