/*	$OpenBSD: evp_names.c,v 1.19 2025/05/10 05:54:38 tb Exp $ */
/*
 * Copyright (c) 2023 Theo Buehler <tb@openbsd.org>
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

#include <openssl/evp.h>
#include <openssl/objects.h>

#include <stdlib.h>
#include <string.h>

/*
 * In the following two structs, .name is the lookup name that is used
 * for EVP_get_cipherbyname() and EVP_get_digestbyname(), while .alias
 * keeps track of the aliased name.
 */

struct cipher_name {
	const char *name;
	const EVP_CIPHER *(*cipher)(void);
	const char *alias;
};

struct digest_name {
	const char *name;
	const EVP_MD *(*digest)(void);
	const char *alias;
};

/*
 * Keep this table alphabetically sorted by increasing .name.
 * regress/lib/libcrypto/evp/evp_test.c checks that.
 */

static const struct cipher_name cipher_names[] = {
	{
		.name = SN_aes_128_cbc,
		.cipher = EVP_aes_128_cbc,
	},
	{
		.name = SN_aes_128_cfb128,
		.cipher = EVP_aes_128_cfb128,
	},
	{
		.name = SN_aes_128_cfb1,
		.cipher = EVP_aes_128_cfb1,
	},
	{
		.name = SN_aes_128_cfb8,
		.cipher = EVP_aes_128_cfb8,
	},
	{
		.name = SN_aes_128_ctr,
		.cipher = EVP_aes_128_ctr,
	},
	{
		.name = SN_aes_128_ecb,
		.cipher = EVP_aes_128_ecb,
	},
	{
		.name = SN_aes_128_ofb128,
		.cipher = EVP_aes_128_ofb,
	},
	{
		.name = SN_aes_128_xts,
		.cipher = EVP_aes_128_xts,
	},

	{
		.name = SN_aes_192_cbc,
		.cipher = EVP_aes_192_cbc,
	},
	{
		.name = SN_aes_192_cfb128,
		.cipher = EVP_aes_192_cfb128,
	},
	{
		.name = SN_aes_192_cfb1,
		.cipher = EVP_aes_192_cfb1,
	},
	{
		.name = SN_aes_192_cfb8,
		.cipher = EVP_aes_192_cfb8,
	},
	{
		.name = SN_aes_192_ctr,
		.cipher = EVP_aes_192_ctr,
	},
	{
		.name = SN_aes_192_ecb,
		.cipher = EVP_aes_192_ecb,
	},
	{
		.name = SN_aes_192_ofb128,
		.cipher = EVP_aes_192_ofb,
	},

	{
		.name = SN_aes_256_cbc,
		.cipher = EVP_aes_256_cbc,
	},
	{
		.name = SN_aes_256_cfb128,
		.cipher = EVP_aes_256_cfb128,
	},
	{
		.name = SN_aes_256_cfb1,
		.cipher = EVP_aes_256_cfb1,
	},
	{
		.name = SN_aes_256_cfb8,
		.cipher = EVP_aes_256_cfb8,
	},
	{
		.name = SN_aes_256_ctr,
		.cipher = EVP_aes_256_ctr,
	},
	{
		.name = SN_aes_256_ecb,
		.cipher = EVP_aes_256_ecb,
	},
	{
		.name = SN_aes_256_ofb128,
		.cipher = EVP_aes_256_ofb,
	},
	{
		.name = SN_aes_256_xts,
		.cipher = EVP_aes_256_xts,
	},

	{
		.name = "AES128",
		.cipher = EVP_aes_128_cbc,
		.alias = SN_aes_128_cbc,
	},
	{
		.name = "AES192",
		.cipher = EVP_aes_192_cbc,
		.alias = SN_aes_192_cbc,
	},
	{
		.name = "AES256",
		.cipher = EVP_aes_256_cbc,
		.alias = SN_aes_256_cbc,
	},

	{
		.name = "BF",
		.cipher = EVP_bf_cbc,
		.alias = SN_bf_cbc,
	},

	{
		.name = SN_bf_cbc,
		.cipher = EVP_bf_cbc,
	},
	{
		.name = SN_bf_cfb64,
		.cipher = EVP_bf_cfb64,
	},
	{
		.name = SN_bf_ecb,
		.cipher = EVP_bf_ecb,
	},
	{
		.name = SN_bf_ofb64,
		.cipher = EVP_bf_ofb,
	},

	{
		.name = SN_camellia_128_cbc,
		.cipher = EVP_camellia_128_cbc,
	},
	{
		.name = SN_camellia_128_cfb128,
		.cipher = EVP_camellia_128_cfb128,
	},
	{
		.name = SN_camellia_128_cfb1,
		.cipher = EVP_camellia_128_cfb1,
	},
	{
		.name = SN_camellia_128_cfb8,
		.cipher = EVP_camellia_128_cfb8,
	},
	{
		.name = SN_camellia_128_ecb,
		.cipher = EVP_camellia_128_ecb,
	},
	{
		.name = SN_camellia_128_ofb128,
		.cipher = EVP_camellia_128_ofb,
	},

	{
		.name = SN_camellia_192_cbc,
		.cipher = EVP_camellia_192_cbc,
	},
	{
		.name = SN_camellia_192_cfb128,
		.cipher = EVP_camellia_192_cfb128,
	},
	{
		.name = SN_camellia_192_cfb1,
		.cipher = EVP_camellia_192_cfb1,
	},
	{
		.name = SN_camellia_192_cfb8,
		.cipher = EVP_camellia_192_cfb8,
	},
	{
		.name = SN_camellia_192_ecb,
		.cipher = EVP_camellia_192_ecb,
	},
	{
		.name = SN_camellia_192_ofb128,
		.cipher = EVP_camellia_192_ofb,
	},

	{
		.name = SN_camellia_256_cbc,
		.cipher = EVP_camellia_256_cbc,
	},
	{
		.name = SN_camellia_256_cfb128,
		.cipher = EVP_camellia_256_cfb128,
	},
	{
		.name = SN_camellia_256_cfb1,
		.cipher = EVP_camellia_256_cfb1,
	},
	{
		.name = SN_camellia_256_cfb8,
		.cipher = EVP_camellia_256_cfb8,
	},
	{
		.name = SN_camellia_256_ecb,
		.cipher = EVP_camellia_256_ecb,
	},
	{
		.name = SN_camellia_256_ofb128,
		.cipher = EVP_camellia_256_ofb,
	},

	{
		.name = "CAMELLIA128",
		.cipher = EVP_camellia_128_cbc,
		.alias = SN_camellia_128_cbc,
	},
	{
		.name = "CAMELLIA192",
		.cipher = EVP_camellia_192_cbc,
		.alias = SN_camellia_192_cbc,
	},
	{
		.name = "CAMELLIA256",
		.cipher = EVP_camellia_256_cbc,
		.alias = SN_camellia_256_cbc,
	},

	{
		.name = "CAST",
		.cipher = EVP_cast5_cbc,
		.alias = SN_cast5_cbc,
	},
	{
		.name = "CAST-cbc",
		.cipher = EVP_cast5_cbc,
		.alias = SN_cast5_cbc,
	},

	{
		.name = SN_cast5_cbc,
		.cipher = EVP_cast5_cbc,
	},
	{
		.name = SN_cast5_cfb64,
		.cipher = EVP_cast5_cfb,
	},
	{
		.name = SN_cast5_ecb,
		.cipher = EVP_cast5_ecb,
	},
	{
		.name = SN_cast5_ofb64,
		.cipher = EVP_cast5_ofb,
	},

	{
		.name = SN_chacha20,
		.cipher = EVP_chacha20,
	},
	{
		.name = "ChaCha20",
		.cipher = EVP_chacha20,
		.alias = SN_chacha20,
	},

	{
		.name = SN_chacha20_poly1305,
		.cipher = EVP_chacha20_poly1305,
	},

	{
		.name = "DES",
		.cipher = EVP_des_cbc,
		.alias = SN_des_cbc,
	},

	{
		.name = SN_des_cbc,
		.cipher = EVP_des_cbc,
	},
	{
		.name = SN_des_cfb64,
		.cipher = EVP_des_cfb64,
	},
	{
		.name = SN_des_cfb1,
		.cipher = EVP_des_cfb1,
	},
	{
		.name = SN_des_cfb8,
		.cipher = EVP_des_cfb8,
	},
	{
		.name = SN_des_ecb,
		.cipher = EVP_des_ecb,
	},
	{
		.name = SN_des_ede_ecb,
		.cipher = EVP_des_ede,
	},
	{
		.name = SN_des_ede_cbc,
		.cipher = EVP_des_ede_cbc,
	},
	{
		.name = SN_des_ede_cfb64,
		.cipher = EVP_des_ede_cfb64,
	},
	{
		.name = SN_des_ede_ofb64,
		.cipher = EVP_des_ede_ofb,
	},
	{
		.name = SN_des_ede3_ecb,
		.cipher = EVP_des_ede3_ecb,
	},
	{
		.name = SN_des_ede3_cbc,
		.cipher = EVP_des_ede3_cbc,
	},
	{
		.name = SN_des_ede3_cfb64,
		.cipher = EVP_des_ede3_cfb,
	},
	{
		.name = SN_des_ede3_cfb1,
		.cipher = EVP_des_ede3_cfb1,
	},
	{
		.name = SN_des_ede3_cfb8,
		.cipher = EVP_des_ede3_cfb8,
	},
	{
		.name = SN_des_ede3_ofb64,
		.cipher = EVP_des_ede3_ofb,
	},
	{
		.name = SN_des_ofb64,
		.cipher = EVP_des_ofb,
	},

	{
		.name = "DES3",
		.cipher = EVP_des_ede3_cbc,
		.alias = SN_des_ede3_cbc,
	},

	{
		.name = "DESX",
		.cipher = EVP_desx_cbc,
		.alias = SN_desx_cbc,
	},
	{
		.name = SN_desx_cbc,
		.cipher = EVP_desx_cbc,
	},

	{
		.name = "IDEA",
		.cipher = EVP_idea_cbc,
		.alias = SN_idea_cbc,
	},

	{
		.name = SN_idea_cbc,
		.cipher = EVP_idea_cbc,
	},
	{
		.name = SN_idea_cfb64,
		.cipher = EVP_idea_cfb64,
	},
	{
		.name = SN_idea_ecb,
		.cipher = EVP_idea_ecb,
	},
	{
		.name = SN_idea_ofb64,
		.cipher = EVP_idea_ofb,
	},

	{
		.name = "RC2",
		.cipher = EVP_rc2_cbc,
		.alias = SN_rc2_cbc,
	},

	{
		.name = SN_rc2_40_cbc,
		.cipher = EVP_rc2_40_cbc,
	},
	{
		.name = SN_rc2_64_cbc,
		.cipher = EVP_rc2_64_cbc,
	},
	{
		.name = SN_rc2_cbc,
		.cipher = EVP_rc2_cbc,
	},
	{
		.name = SN_rc2_cfb64,
		.cipher = EVP_rc2_cfb64,
	},
	{
		.name = SN_rc2_ecb,
		.cipher = EVP_rc2_ecb,
	},
	{
		.name = SN_rc2_ofb64,
		.cipher = EVP_rc2_ofb,
	},

	{
		.name = SN_rc4,
		.cipher = EVP_rc4,
	},
	{
		.name = SN_rc4_40,
		.cipher = EVP_rc4_40,
	},

	{
		.name = "SM4",
		.cipher = EVP_sm4_cbc,
		.alias = SN_sm4_cbc,
	},

	{
		.name = SN_sm4_cbc,
		.cipher = EVP_sm4_cbc,
	},
	{
		.name = SN_sm4_cfb128,
		.cipher = EVP_sm4_cfb128,
	},
	{
		.name = SN_sm4_ctr,
		.cipher = EVP_sm4_ctr,
	},
	{
		.name = SN_sm4_ecb,
		.cipher = EVP_sm4_ecb,
	},
	{
		.name = SN_sm4_ofb128,
		.cipher = EVP_sm4_ofb,
	},

	{
		.name = LN_aes_128_cbc,
		.cipher = EVP_aes_128_cbc,
	},
	{
		.name = LN_aes_128_ccm,
		.cipher = EVP_aes_128_ccm,
	},
	{
		.name = LN_aes_128_cfb128,
		.cipher = EVP_aes_128_cfb128,
	},
	{
		.name = LN_aes_128_cfb1,
		.cipher = EVP_aes_128_cfb1,
	},
	{
		.name = LN_aes_128_cfb8,
		.cipher = EVP_aes_128_cfb8,
	},
	{
		.name = LN_aes_128_ctr,
		.cipher = EVP_aes_128_ctr,
	},
	{
		.name = LN_aes_128_ecb,
		.cipher = EVP_aes_128_ecb,
	},
	{
		.name = LN_aes_128_gcm,
		.cipher = EVP_aes_128_gcm,
	},
	{
		.name = LN_aes_128_ofb128,
		.cipher = EVP_aes_128_ofb,
	},
	{
		.name = LN_aes_128_xts,
		.cipher = EVP_aes_128_xts,
	},

	{
		.name = LN_aes_192_cbc,
		.cipher = EVP_aes_192_cbc,
	},
	{
		.name = LN_aes_192_ccm,
		.cipher = EVP_aes_192_ccm,
	},
	{
		.name = LN_aes_192_cfb128,
		.cipher = EVP_aes_192_cfb128,
	},
	{
		.name = LN_aes_192_cfb1,
		.cipher = EVP_aes_192_cfb1,
	},
	{
		.name = LN_aes_192_cfb8,
		.cipher = EVP_aes_192_cfb8,
	},
	{
		.name = LN_aes_192_ctr,
		.cipher = EVP_aes_192_ctr,
	},
	{
		.name = LN_aes_192_ecb,
		.cipher = EVP_aes_192_ecb,
	},
	{
		.name = LN_aes_192_gcm,
		.cipher = EVP_aes_192_gcm,
	},
	{
		.name = LN_aes_192_ofb128,
		.cipher = EVP_aes_192_ofb,
	},

	{
		.name = LN_aes_256_cbc,
		.cipher = EVP_aes_256_cbc,
	},
	{
		.name = LN_aes_256_ccm,
		.cipher = EVP_aes_256_ccm,
	},
	{
		.name = LN_aes_256_cfb128,
		.cipher = EVP_aes_256_cfb128,
	},
	{
		.name = LN_aes_256_cfb1,
		.cipher = EVP_aes_256_cfb1,
	},
	{
		.name = LN_aes_256_cfb8,
		.cipher = EVP_aes_256_cfb8,
	},
	{
		.name = LN_aes_256_ctr,
		.cipher = EVP_aes_256_ctr,
	},
	{
		.name = LN_aes_256_ecb,
		.cipher = EVP_aes_256_ecb,
	},
	{
		.name = LN_aes_256_gcm,
		.cipher = EVP_aes_256_gcm,
	},
	{
		.name = LN_aes_256_ofb128,
		.cipher = EVP_aes_256_ofb,
	},
	{
		.name = LN_aes_256_xts,
		.cipher = EVP_aes_256_xts,
	},

	{
		.name = "aes128",
		.cipher = EVP_aes_128_cbc,
		.alias = SN_aes_128_cbc,
	},
	{
		.name = "aes192",
		.cipher = EVP_aes_192_cbc,
		.alias = SN_aes_192_cbc,
	},
	{
		.name = "aes256",
		.cipher = EVP_aes_256_cbc,
		.alias = SN_aes_256_cbc,
	},

	{
		.name = "bf",
		.cipher = EVP_bf_cbc,
		.alias = SN_bf_cbc,
	},

	{
		.name = LN_bf_cbc,
		.cipher = EVP_bf_cbc,
	},
	{
		.name = LN_bf_cfb64,
		.cipher = EVP_bf_cfb64,
	},
	{
		.name = LN_bf_ecb,
		.cipher = EVP_bf_ecb,
	},
	{
		.name = LN_bf_ofb64,
		.cipher = EVP_bf_ofb,
	},

	{
		.name = "blowfish",
		.cipher = EVP_bf_cbc,
		.alias = SN_bf_cbc,
	},

	{
		.name = LN_camellia_128_cbc,
		.cipher = EVP_camellia_128_cbc,
	},
	{
		.name = LN_camellia_128_cfb128,
		.cipher = EVP_camellia_128_cfb128,
	},
	{
		.name = LN_camellia_128_cfb1,
		.cipher = EVP_camellia_128_cfb1,
	},
	{
		.name = LN_camellia_128_cfb8,
		.cipher = EVP_camellia_128_cfb8,
	},
	{
		.name = LN_camellia_128_ecb,
		.cipher = EVP_camellia_128_ecb,
	},
	{
		.name = LN_camellia_128_ofb128,
		.cipher = EVP_camellia_128_ofb,
	},

	{
		.name = LN_camellia_192_cbc,
		.cipher = EVP_camellia_192_cbc,
	},
	{
		.name = LN_camellia_192_cfb128,
		.cipher = EVP_camellia_192_cfb128,
	},
	{
		.name = LN_camellia_192_cfb1,
		.cipher = EVP_camellia_192_cfb1,
	},
	{
		.name = LN_camellia_192_cfb8,
		.cipher = EVP_camellia_192_cfb8,
	},
	{
		.name = LN_camellia_192_ecb,
		.cipher = EVP_camellia_192_ecb,
	},
	{
		.name = LN_camellia_192_ofb128,
		.cipher = EVP_camellia_192_ofb,
	},

	{
		.name = LN_camellia_256_cbc,
		.cipher = EVP_camellia_256_cbc,
	},
	{
		.name = LN_camellia_256_cfb128,
		.cipher = EVP_camellia_256_cfb128,
	},
	{
		.name = LN_camellia_256_cfb1,
		.cipher = EVP_camellia_256_cfb1,
	},
	{
		.name = LN_camellia_256_cfb8,
		.cipher = EVP_camellia_256_cfb8,
	},
	{
		.name = LN_camellia_256_ecb,
		.cipher = EVP_camellia_256_ecb,
	},
	{
		.name = LN_camellia_256_ofb128,
		.cipher = EVP_camellia_256_ofb,
	},

	{
		.name = "camellia128",
		.cipher = EVP_camellia_128_cbc,
		.alias = SN_camellia_128_cbc,
	},
	{
		.name = "camellia192",
		.cipher = EVP_camellia_192_cbc,
		.alias = SN_camellia_192_cbc,
	},
	{
		.name = "camellia256",
		.cipher = EVP_camellia_256_cbc,
		.alias = SN_camellia_256_cbc,
	},

	{
		.name = "cast",
		.cipher = EVP_cast5_cbc,
		.alias = SN_cast5_cbc,
	},
	{
		.name = "cast-cbc",
		.cipher = EVP_cast5_cbc,
		.alias = SN_cast5_cbc,
	},

	{
		.name = LN_cast5_cbc,
		.cipher = EVP_cast5_cbc,
	},
	{
		.name = LN_cast5_cfb64,
		.cipher = EVP_cast5_cfb,
	},
	{
		.name = LN_cast5_ecb,
		.cipher = EVP_cast5_ecb,
	},
	{
		.name = LN_cast5_ofb64,
		.cipher = EVP_cast5_ofb,
	},

	{
		.name = LN_chacha20,
		.cipher = EVP_chacha20,
	},
	{
		.name = "chacha20",
		.cipher = EVP_chacha20,
		.alias = LN_chacha20,
	},

	{
		.name = LN_chacha20_poly1305,
		.cipher = EVP_chacha20_poly1305,
	},

	{
		.name = "des",
		.cipher = EVP_des_cbc,
		.alias = SN_des_cbc,
	},

	{
		.name = LN_des_cbc,
		.cipher = EVP_des_cbc,
	},
	{
		.name = LN_des_cfb64,
		.cipher = EVP_des_cfb64,
	},
	{
		.name = LN_des_cfb1,
		.cipher = EVP_des_cfb1,
	},
	{
		.name = LN_des_cfb8,
		.cipher = EVP_des_cfb8,
	},
	{
		.name = LN_des_ecb,
		.cipher = EVP_des_ecb,
	},
	{
		.name = LN_des_ede_ecb,
		.cipher = EVP_des_ede,
	},
	{
		.name = LN_des_ede_cbc,
		.cipher = EVP_des_ede_cbc,
	},
	{
		.name = LN_des_ede_cfb64,
		.cipher = EVP_des_ede_cfb64,
	},
	{
		.name = LN_des_ede_ofb64,
		.cipher = EVP_des_ede_ofb,
	},
	{
		.name = LN_des_ede3_ecb,
		.cipher = EVP_des_ede3_ecb,
	},
	{
		.name = LN_des_ede3_cbc,
		.cipher = EVP_des_ede3_cbc,
	},
	{
		.name = LN_des_ede3_cfb64,
		.cipher = EVP_des_ede3_cfb,
	},
	{
		.name = LN_des_ede3_cfb1,
		.cipher = EVP_des_ede3_cfb1,
	},
	{
		.name = LN_des_ede3_cfb8,
		.cipher = EVP_des_ede3_cfb8,
	},
	{
		.name = LN_des_ede3_ofb64,
		.cipher = EVP_des_ede3_ofb,
	},
	{
		.name = LN_des_ofb64,
		.cipher = EVP_des_ofb,
	},

	{
		.name = "des3",
		.cipher = EVP_des_ede3_cbc,
		.alias = SN_des_ede3_cbc,
	},

	{
		.name = "desx",
		.cipher = EVP_desx_cbc,
		.alias = SN_desx_cbc,
	},
	{
		.name = LN_desx_cbc,
		.cipher = EVP_desx_cbc,
	},

	{
		.name = SN_aes_128_ccm,
		.cipher = EVP_aes_128_ccm,
	},
	{
		.name = SN_aes_128_gcm,
		.cipher = EVP_aes_128_gcm,
	},
	{
		.name = SN_id_aes128_wrap,
		.cipher = EVP_aes_128_wrap,
	},

	{
		.name = SN_aes_192_ccm,
		.cipher = EVP_aes_192_ccm,
	},
	{
		.name = SN_aes_192_gcm,
		.cipher = EVP_aes_192_gcm,
	},
	{
		.name = SN_id_aes192_wrap,
		.cipher = EVP_aes_192_wrap,
	},

	{
		.name = SN_aes_256_ccm,
		.cipher = EVP_aes_256_ccm,
	},
	{
		.name = SN_aes_256_gcm,
		.cipher = EVP_aes_256_gcm,
	},
	{
		.name = SN_id_aes256_wrap,
		.cipher = EVP_aes_256_wrap,
	},

	{
		.name = "idea",
		.cipher = EVP_idea_cbc,
		.alias = SN_idea_cbc,
	},

	{
		.name = LN_idea_cbc,
		.cipher = EVP_idea_cbc,
	},
	{
		.name = LN_idea_cfb64,
		.cipher = EVP_idea_cfb64,
	},
	{
		.name = LN_idea_ecb,
		.cipher = EVP_idea_ecb,
	},
	{
		.name = LN_idea_ofb64,
		.cipher = EVP_idea_ofb,
	},

	{
		.name = "rc2",
		.cipher = EVP_rc2_cbc,
		.alias = SN_rc2_cbc,
	},

	{
		.name = LN_rc2_40_cbc,
		.cipher = EVP_rc2_40_cbc,
	},
	{
		.name = LN_rc2_64_cbc,
		.cipher = EVP_rc2_64_cbc,
	},
	{
		.name = LN_rc2_cbc,
		.cipher = EVP_rc2_cbc,
	},
	{
		.name = LN_rc2_cfb64,
		.cipher = EVP_rc2_cfb64,
	},
	{
		.name = LN_rc2_ecb,
		.cipher = EVP_rc2_ecb,
	},
	{
		.name = LN_rc2_ofb64,
		.cipher = EVP_rc2_ofb,
	},

	{
		.name = LN_rc4,
		.cipher = EVP_rc4,
	},
	{
		.name = LN_rc4_40,
		.cipher = EVP_rc4_40,
	},

	{
		.name = "sm4",
		.cipher = EVP_sm4_cbc,
		.alias = SN_sm4_cbc,
	},

	{
		.name = LN_sm4_cbc,
		.cipher = EVP_sm4_cbc,
	},
	{
		.name = LN_sm4_cfb128,
		.cipher = EVP_sm4_cfb128,
	},
	{
		.name = LN_sm4_ctr,
		.cipher = EVP_sm4_ctr,
	},
	{
		.name = LN_sm4_ecb,
		.cipher = EVP_sm4_ecb,
	},
	{
		.name = LN_sm4_ofb128,
		.cipher = EVP_sm4_ofb,
	},
};

#define N_CIPHER_NAMES (sizeof(cipher_names) / sizeof(cipher_names[0]))

/*
 * Keep this table alphabetically sorted by increasing .name.
 * regress/lib/libcrypto/evp/evp_test.c checks that.
 */

static const struct digest_name digest_names[] = {
	{
		.name = SN_dsaWithSHA1,
		.digest = EVP_sha1,
		.alias = SN_sha1,
	},

	{
		.name = SN_md4,
		.digest = EVP_md4,
	},

	{
		.name = SN_md5,
		.digest = EVP_md5,
	},

	{
		.name = SN_md5_sha1,
		.digest = EVP_md5_sha1,
	},

	{
		.name = SN_ripemd160,
		.digest = EVP_ripemd160,
	},

	{
		.name = SN_md4WithRSAEncryption,
		.digest = EVP_md4,
		.alias = SN_md4,
	},
	{
		.name = SN_md5WithRSAEncryption,
		.digest = EVP_md5,
		.alias = SN_md5,
	},
	{
		.name = SN_ripemd160WithRSA,
		.digest = EVP_ripemd160,
		.alias = SN_ripemd160,
	},
	{
		.name = SN_sha1WithRSAEncryption,
		.digest = EVP_sha1,
		.alias = SN_sha1,
	},
	{
		.name = SN_sha1WithRSA,
		.digest = EVP_sha1,
		.alias = SN_sha1, /* XXX - alias to SN_sha1WithRSAEncryption? */
	},
	{
		.name = SN_sha224WithRSAEncryption,
		.digest = EVP_sha224,
		.alias = SN_sha224,
	},
	{
		.name = SN_sha256WithRSAEncryption,
		.digest = EVP_sha256,
		.alias = SN_sha256,
	},
	{
		.name = LN_RSA_SHA3_224,
		.digest = EVP_sha3_224,
		.alias = SN_sha3_224,
	},
	{
		.name = LN_RSA_SHA3_256,
		.digest = EVP_sha3_256,
		.alias = SN_sha3_256,
	},
	{
		.name = LN_RSA_SHA3_384,
		.digest = EVP_sha3_384,
		.alias = SN_sha3_384,
	},
	{
		.name = LN_RSA_SHA3_512,
		.digest = EVP_sha3_512,
		.alias = SN_sha3_512,
	},
	{
		.name = SN_sha384WithRSAEncryption,
		.digest = EVP_sha384,
		.alias = SN_sha384,
	},
	{
		.name = SN_sha512WithRSAEncryption,
		.digest = EVP_sha512,
		.alias = SN_sha512,
	},
	{
		.name = SN_sha512_224WithRSAEncryption,
		.digest = EVP_sha512_224,
		.alias = SN_sha512_224,
	},
	{
		.name = SN_sha512_256WithRSAEncryption,
		.digest = EVP_sha512_256,
		.alias = SN_sha512_256,
	},
	{
		.name = SN_sm3WithRSAEncryption,
		.digest = EVP_sm3,
		.alias = SN_sm3,
	},

	{
		.name = SN_sha1,
		.digest = EVP_sha1,
	},
	{
		.name = SN_sha224,
		.digest = EVP_sha224,
	},
	{
		.name = SN_sha256,
		.digest = EVP_sha256,
	},
	{
		.name = SN_sha3_224,
		.digest = EVP_sha3_224,
	},
	{
		.name = SN_sha3_256,
		.digest = EVP_sha3_256,
	},
	{
		.name = SN_sha3_384,
		.digest = EVP_sha3_384,
	},
	{
		.name = SN_sha3_512,
		.digest = EVP_sha3_512,
	},

	{
		.name = SN_sha384,
		.digest = EVP_sha384,
	},
	{
		.name = SN_sha512,
		.digest = EVP_sha512,
	},
	{
		.name = SN_sha512_224,
		.digest = EVP_sha512_224,
	},
	{
		.name = SN_sha512_256,
		.digest = EVP_sha512_256,
	},

	{
		.name = SN_sm3,
		.digest = EVP_sm3,
	},

	{
		.name = LN_dsaWithSHA1,
		.digest = EVP_sha1,
		.alias = SN_sha1,
	},

	{
		.name = LN_dsa_with_SHA224,
		.digest = EVP_sha224,
		.alias = SN_sha224,
	},
	{
		.name = LN_dsa_with_SHA256,
		.digest = EVP_sha256,
		.alias = SN_sha256,
	},
	{
		.name = LN_dsa_with_SHA384,
		.digest = EVP_sha384,
		.alias = SN_sha384,
	},
	{
		.name = LN_dsa_with_SHA512,
		.digest = EVP_sha512,
		.alias = SN_sha512,
	},

	{
		.name = SN_ecdsa_with_SHA1,
		.digest = EVP_sha1,
		.alias = SN_sha1,
	},

	{
		.name = SN_ecdsa_with_SHA224,
		.digest = EVP_sha224,
		.alias = SN_sha224,
	},
	{
		.name = SN_ecdsa_with_SHA256,
		.digest = EVP_sha256,
		.alias = SN_sha256,
	},
	{
		.name = SN_ecdsa_with_SHA384,
		.digest = EVP_sha384,
		.alias = SN_sha384,
	},
	{
		.name = SN_ecdsa_with_SHA512,
		.digest = EVP_sha512,
		.alias = SN_sha512,
	},

	{
		.name = SN_dsa_with_SHA224,
		.digest = EVP_sha224,
		.alias = SN_sha224,
	},
	{
		.name = SN_dsa_with_SHA256,
		.digest = EVP_sha256,
		.alias = SN_sha256,
	},

	{
		.name = SN_dsa_with_SHA3_224,
		.digest = EVP_sha3_224,
		.alias = SN_sha3_224,
	},
	{
		.name = SN_dsa_with_SHA3_256,
		.digest = EVP_sha3_256,
		.alias = SN_sha3_256,
	},
	{
		.name = SN_dsa_with_SHA3_384,
		.digest = EVP_sha3_384,
		.alias = SN_sha3_384,
	},
	{
		.name = SN_dsa_with_SHA3_512,
		.digest = EVP_sha3_512,
		.alias = SN_sha3_512,
	},

	{
		.name = SN_dsa_with_SHA384,
		.digest = EVP_sha384,
		.alias = SN_sha384,
	},
	{
		.name = SN_dsa_with_SHA512,
		.digest = EVP_sha512,
		.alias = SN_sha512,
	},

	{
		.name = SN_ecdsa_with_SHA3_224,
		.digest = EVP_sha3_224,
		.alias = SN_sha3_224,
	},
	{
		.name = SN_ecdsa_with_SHA3_256,
		.digest = EVP_sha3_256,
		.alias = SN_sha3_256,
	},
	{
		.name = SN_ecdsa_with_SHA3_384,
		.digest = EVP_sha3_384,
		.alias = SN_sha3_384,
	},
	{
		.name = SN_ecdsa_with_SHA3_512,
		.digest = EVP_sha3_512,
		.alias = SN_sha3_512,
	},

	{
		.name = SN_RSA_SHA3_224,
		.digest = EVP_sha3_224,
		.alias = SN_sha3_224,
	},
	{
		.name = SN_RSA_SHA3_256,
		.digest = EVP_sha3_256,
		.alias = SN_sha3_256,
	},
	{
		.name = SN_RSA_SHA3_384,
		.digest = EVP_sha3_384,
		.alias = SN_sha3_384,
	},
	{
		.name = SN_RSA_SHA3_512,
		.digest = EVP_sha3_512,
		.alias = SN_sha3_512,
	},

	{
		.name = LN_md4,
		.digest = EVP_md4,
	},
	{
		.name = LN_md4WithRSAEncryption,
		.digest = EVP_md4,
		.alias = SN_md4,
	},

	{
		.name = LN_md5,
		.digest = EVP_md5,
	},
	{
		.name = LN_md5_sha1,
		.digest = EVP_md5_sha1,
	},
	{
		.name = LN_md5WithRSAEncryption,
		.digest = EVP_md5,
		.alias = SN_md5,
	},

	{
		.name = "ripemd",
		.digest = EVP_ripemd160,
		.alias = SN_ripemd160,
	},
	{
		.name = LN_ripemd160,
		.digest = EVP_ripemd160,
	},
	{
		.name = LN_ripemd160WithRSA,
		.digest = EVP_ripemd160,
		.alias = SN_ripemd160,
	},
	{
		.name = "rmd160",
		.digest = EVP_ripemd160,
		.alias = SN_ripemd160,
	},

	{
		.name = LN_sha1,
		.digest = EVP_sha1,
	},
	{
		.name = LN_sha1WithRSAEncryption,
		.digest = EVP_sha1,
		.alias = SN_sha1,
	},

	{
		.name = LN_sha224,
		.digest = EVP_sha224,
	},
	{
		.name = LN_sha224WithRSAEncryption,
		.digest = EVP_sha224,
		.alias = SN_sha224,
	},
	{
		.name = LN_sha256,
		.digest = EVP_sha256,
	},
	{
		.name = LN_sha256WithRSAEncryption,
		.digest = EVP_sha256,
		.alias = SN_sha256,
	},

	{
		.name = LN_sha3_224,
		.digest = EVP_sha3_224,
	},
	{
		.name = LN_sha3_256,
		.digest = EVP_sha3_256,
	},
	{
		.name = LN_sha3_384,
		.digest = EVP_sha3_384,
	},
	{
		.name = LN_sha3_512,
		.digest = EVP_sha3_512,
	},

	{
		.name = LN_sha384,
		.digest = EVP_sha384,
	},
	{
		.name = LN_sha384WithRSAEncryption,
		.digest = EVP_sha384,
		.alias = SN_sha384,
	},
	{
		.name = LN_sha512,
		.digest = EVP_sha512,
	},
	{
		.name = LN_sha512_224,
		.digest = EVP_sha512_224,
	},
	{
		.name = LN_sha512_224WithRSAEncryption,
		.digest = EVP_sha512_224,
		.alias = SN_sha512_224,
	},
	{
		.name = LN_sha512_256,
		.digest = EVP_sha512_256,
	},
	{
		.name = LN_sha512_256WithRSAEncryption,
		.digest = EVP_sha512_256,
		.alias = SN_sha512_256,
	},
	{
		.name = LN_sha512WithRSAEncryption,
		.digest = EVP_sha512,
		.alias = SN_sha512,
	},

	{
		.name = LN_sm3,
		.digest = EVP_sm3,
	},
	{
		.name = LN_sm3WithRSAEncryption,
		.digest = EVP_sm3,
		.alias = SN_sm3,
	},

	{
		.name = "ssl2-md5",
		.digest = EVP_md5,
		.alias = SN_md5,
	},
	{
		.name = "ssl3-md5",
		.digest = EVP_md5,
		.alias = SN_md5,
	},

	{
		.name = "ssl3-sha1",
		.digest = EVP_sha1,
		.alias = SN_sha1,
	},
};

#define N_DIGEST_NAMES (sizeof(digest_names) / sizeof(digest_names[0]))

void
EVP_CIPHER_do_all_sorted(void (*fn)(const EVP_CIPHER *, const char *,
    const char *, void *), void *arg)
{
	size_t i;

	/* Prayer and clean living lets you ignore errors, OpenSSL style. */
	(void)OPENSSL_init_crypto(0, NULL);

	for (i = 0; i < N_CIPHER_NAMES; i++) {
		const struct cipher_name *cipher = &cipher_names[i];
		const EVP_CIPHER *evp_cipher;

		if ((evp_cipher = cipher->cipher()) == NULL)
			continue;

		if (cipher->alias != NULL)
			fn(NULL, cipher->name, cipher->alias, arg);
		else
			fn(evp_cipher, cipher->name, NULL, arg);
	}
}
LCRYPTO_ALIAS(EVP_CIPHER_do_all_sorted);

void
EVP_CIPHER_do_all(void (*fn)(const EVP_CIPHER *, const char *, const char *,
    void *), void *arg)
{
	EVP_CIPHER_do_all_sorted(fn, arg);
}
LCRYPTO_ALIAS(EVP_CIPHER_do_all);

void
EVP_MD_do_all_sorted(void (*fn)(const EVP_MD *, const char *, const char *,
    void *), void *arg)
{
	size_t i;

	/* Prayer and clean living lets you ignore errors, OpenSSL style. */
	(void)OPENSSL_init_crypto(0, NULL);

	for (i = 0; i < N_DIGEST_NAMES; i++) {
		const struct digest_name *digest = &digest_names[i];
		const EVP_MD *evp_md;

		if ((evp_md = digest->digest()) == NULL)
			continue;

		if (digest->alias != NULL)
			fn(NULL, digest->name, digest->alias, arg);
		else
			fn(evp_md, digest->name, NULL, arg);
	}
}
LCRYPTO_ALIAS(EVP_MD_do_all_sorted);

void
EVP_MD_do_all(void (*fn)(const EVP_MD *, const char *, const char *, void *),
    void *arg)
{
	EVP_MD_do_all_sorted(fn, arg);
}
LCRYPTO_ALIAS(EVP_MD_do_all);

/*
 * The OBJ_NAME API is completely misnamed. It has little to do with objects
 * and a lot to do with EVP. Therefore we implement a saner replacement for
 * the part of the old madness that we need to keep in the evp directory.
 */

static int
OBJ_NAME_from_cipher_name(OBJ_NAME *obj_name, const struct cipher_name *cipher)
{
	const EVP_CIPHER *evp_cipher;

	if ((evp_cipher = cipher->cipher()) == NULL)
		return 0;

	obj_name->type = OBJ_NAME_TYPE_CIPHER_METH;
	obj_name->name = cipher->name;
	if (cipher->alias != NULL) {
		obj_name->alias = OBJ_NAME_ALIAS;
		obj_name->data = cipher->alias;
	} else {
		obj_name->alias = 0;
		obj_name->data = evp_cipher;
	}

	return 1;
}

static void
OBJ_NAME_do_all_ciphers(void (*fn)(const OBJ_NAME *, void *), void *arg)
{
	size_t i;

	for (i = 0; i < N_CIPHER_NAMES; i++) {
		const struct cipher_name *cipher = &cipher_names[i];
		OBJ_NAME name;

		if (OBJ_NAME_from_cipher_name(&name, cipher))
			fn(&name, arg);
	}
}

static int
OBJ_NAME_from_digest_name(OBJ_NAME *obj_name, const struct digest_name *digest)
{
	const EVP_MD *evp_md;

	if ((evp_md = digest->digest()) == NULL)
		return 0;

	obj_name->type = OBJ_NAME_TYPE_MD_METH;
	obj_name->name = digest->name;
	if (digest->alias != NULL) {
		obj_name->alias = OBJ_NAME_ALIAS;
		obj_name->data = digest->alias;
	} else {
		obj_name->alias = 0;
		obj_name->data = evp_md;
	}

	return 1;
}

static void
OBJ_NAME_do_all_digests(void (*fn)(const OBJ_NAME *, void *), void *arg)
{
	size_t i;

	for (i = 0; i < N_DIGEST_NAMES; i++) {
		const struct digest_name *digest = &digest_names[i];
		OBJ_NAME name;

		if (OBJ_NAME_from_digest_name(&name, digest))
			fn(&name, arg);
	}
}

void
OBJ_NAME_do_all_sorted(int type, void (*fn)(const OBJ_NAME *, void *), void *arg)
{
	/* Prayer and clean living lets you ignore errors, OpenSSL style. */
	(void)OPENSSL_init_crypto(0, NULL);

	if (type == OBJ_NAME_TYPE_CIPHER_METH)
		OBJ_NAME_do_all_ciphers(fn, arg);
	if (type == OBJ_NAME_TYPE_MD_METH)
		OBJ_NAME_do_all_digests(fn, arg);
}
LCRYPTO_ALIAS(OBJ_NAME_do_all_sorted);

void
OBJ_NAME_do_all(int type, void (*fn)(const OBJ_NAME *, void *), void *arg)
{
	OBJ_NAME_do_all_sorted(type, fn, arg);
}
LCRYPTO_ALIAS(OBJ_NAME_do_all);

static int
cipher_cmp(const void *a, const void *b)
{
	return strcmp(a, ((const struct cipher_name *)b)->name);
}

const EVP_CIPHER *
EVP_get_cipherbyname(const char *name)
{
	const struct cipher_name *cipher;

	if (!OPENSSL_init_crypto(0, NULL))
		return NULL;

	if (name == NULL)
		return NULL;

	if ((cipher = bsearch(name, cipher_names, N_CIPHER_NAMES,
	    sizeof(*cipher), cipher_cmp)) == NULL)
		return NULL;

	return cipher->cipher();
}
LCRYPTO_ALIAS(EVP_get_cipherbyname);

static int
digest_cmp(const void *a, const void *b)
{
	return strcmp(a, ((const struct digest_name *)b)->name);
}

const EVP_MD *
EVP_get_digestbyname(const char *name)
{
	const struct digest_name *digest;

	if (!OPENSSL_init_crypto(0, NULL))
		return NULL;

	if (name == NULL)
		return NULL;

	if ((digest = bsearch(name, digest_names, N_DIGEST_NAMES,
	    sizeof(*digest), digest_cmp)) == NULL)
		return NULL;

	return digest->digest();
}
LCRYPTO_ALIAS(EVP_get_digestbyname);

/*
 * XXX - this is here because most of its job was to clean up the dynamic
 * tables of ciphers and digests. If we get an evp_lib.c again, it should
 * probably move there.
 */

void
EVP_cleanup(void)
{
}
LCRYPTO_ALIAS(EVP_cleanup);
