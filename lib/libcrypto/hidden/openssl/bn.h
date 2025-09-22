/* $OpenBSD: bn.h,v 1.7 2024/04/10 14:58:06 beck Exp $ */
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

#ifndef _LIBCRYPTO_BN_H
#define _LIBCRYPTO_BN_H

#ifndef _MSC_VER
#include_next <openssl/bn.h>
#else
#include "../include/openssl/bn.h"
#endif
#include "crypto_namespace.h"

LCRYPTO_USED(BN_set_flags);
LCRYPTO_USED(BN_get_flags);
LCRYPTO_USED(BN_with_flags);
LCRYPTO_USED(BN_GENCB_new);
LCRYPTO_USED(BN_GENCB_free);
LCRYPTO_USED(BN_GENCB_call);
LCRYPTO_USED(BN_GENCB_set_old);
LCRYPTO_USED(BN_GENCB_set);
LCRYPTO_USED(BN_GENCB_get_arg);
LCRYPTO_USED(BN_abs_is_word);
LCRYPTO_USED(BN_is_zero);
LCRYPTO_USED(BN_is_one);
LCRYPTO_USED(BN_is_word);
LCRYPTO_USED(BN_is_odd);
LCRYPTO_USED(BN_zero);
LCRYPTO_USED(BN_one);
LCRYPTO_USED(BN_value_one);
LCRYPTO_USED(BN_CTX_new);
LCRYPTO_USED(BN_CTX_free);
LCRYPTO_USED(BN_CTX_start);
LCRYPTO_USED(BN_CTX_get);
LCRYPTO_USED(BN_CTX_end);
LCRYPTO_USED(BN_rand);
LCRYPTO_USED(BN_pseudo_rand);
LCRYPTO_USED(BN_rand_range);
LCRYPTO_USED(BN_pseudo_rand_range);
LCRYPTO_USED(BN_num_bits);
LCRYPTO_USED(BN_num_bits_word);
LCRYPTO_USED(BN_new);
LCRYPTO_USED(BN_clear_free);
LCRYPTO_USED(BN_copy);
LCRYPTO_USED(BN_swap);
LCRYPTO_USED(BN_bin2bn);
LCRYPTO_USED(BN_bn2bin);
LCRYPTO_USED(BN_bn2binpad);
LCRYPTO_USED(BN_lebin2bn);
LCRYPTO_USED(BN_bn2lebinpad);
LCRYPTO_USED(BN_mpi2bn);
LCRYPTO_USED(BN_bn2mpi);
LCRYPTO_USED(BN_sub);
LCRYPTO_USED(BN_usub);
LCRYPTO_USED(BN_uadd);
LCRYPTO_USED(BN_add);
LCRYPTO_USED(BN_mul);
LCRYPTO_USED(BN_sqr);
LCRYPTO_USED(BN_set_negative);
LCRYPTO_USED(BN_is_negative);
LCRYPTO_USED(BN_nnmod);
LCRYPTO_USED(BN_mod_add);
LCRYPTO_USED(BN_mod_add_quick);
LCRYPTO_USED(BN_mod_sub);
LCRYPTO_USED(BN_mod_sub_quick);
LCRYPTO_USED(BN_mod_mul);
LCRYPTO_USED(BN_mod_sqr);
LCRYPTO_USED(BN_mod_lshift1);
LCRYPTO_USED(BN_mod_lshift1_quick);
LCRYPTO_USED(BN_mod_lshift);
LCRYPTO_USED(BN_mod_lshift_quick);
LCRYPTO_USED(BN_mod_word);
LCRYPTO_USED(BN_div_word);
LCRYPTO_USED(BN_mul_word);
LCRYPTO_USED(BN_add_word);
LCRYPTO_USED(BN_sub_word);
LCRYPTO_USED(BN_set_word);
LCRYPTO_USED(BN_get_word);
LCRYPTO_USED(BN_cmp);
LCRYPTO_USED(BN_free);
LCRYPTO_USED(BN_is_bit_set);
LCRYPTO_USED(BN_lshift);
LCRYPTO_USED(BN_lshift1);
LCRYPTO_USED(BN_exp);
LCRYPTO_USED(BN_mod_exp_mont_consttime);
LCRYPTO_USED(BN_mask_bits);
LCRYPTO_USED(BN_print_fp);
LCRYPTO_USED(BN_print);
LCRYPTO_USED(BN_rshift);
LCRYPTO_USED(BN_rshift1);
LCRYPTO_USED(BN_clear);
LCRYPTO_USED(BN_dup);
LCRYPTO_USED(BN_ucmp);
LCRYPTO_USED(BN_set_bit);
LCRYPTO_USED(BN_clear_bit);
LCRYPTO_USED(BN_bn2hex);
LCRYPTO_USED(BN_bn2dec);
LCRYPTO_USED(BN_hex2bn);
LCRYPTO_USED(BN_dec2bn);
LCRYPTO_USED(BN_asc2bn);
LCRYPTO_USED(BN_kronecker);
LCRYPTO_USED(BN_mod_sqrt);
LCRYPTO_USED(BN_consttime_swap);
LCRYPTO_USED(BN_security_bits);
LCRYPTO_USED(BN_generate_prime_ex);
LCRYPTO_USED(BN_is_prime_ex);
LCRYPTO_USED(BN_is_prime_fasttest_ex);
LCRYPTO_USED(BN_MONT_CTX_new);
LCRYPTO_USED(BN_mod_mul_montgomery);
LCRYPTO_USED(BN_to_montgomery);
LCRYPTO_USED(BN_from_montgomery);
LCRYPTO_USED(BN_MONT_CTX_free);
LCRYPTO_USED(BN_MONT_CTX_set);
LCRYPTO_USED(BN_MONT_CTX_copy);
LCRYPTO_USED(BN_MONT_CTX_set_locked);
LCRYPTO_USED(BN_get_rfc2409_prime_768);
LCRYPTO_USED(BN_get_rfc2409_prime_1024);
LCRYPTO_USED(BN_get_rfc3526_prime_1536);
LCRYPTO_USED(BN_get_rfc3526_prime_2048);
LCRYPTO_USED(BN_get_rfc3526_prime_3072);
LCRYPTO_USED(BN_get_rfc3526_prime_4096);
LCRYPTO_USED(BN_get_rfc3526_prime_6144);
LCRYPTO_USED(BN_get_rfc3526_prime_8192);
LCRYPTO_USED(ERR_load_BN_strings);
LCRYPTO_UNUSED(BN_div);
LCRYPTO_UNUSED(BN_mod_exp);
LCRYPTO_UNUSED(BN_mod_exp_mont);
LCRYPTO_UNUSED(BN_gcd);
LCRYPTO_UNUSED(BN_mod_inverse);

#endif /* _LIBCRYPTO_BN_H */
