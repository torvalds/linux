/*	$OpenBSD: obj_xref.c,v 1.15 2024/08/28 06:53:24 tb Exp $ */

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

#include <openssl/objects.h>

/*
 * Map between signature nids and pairs of (hash, pkey) nids. If the hash nid
 * is NID_undef, this indicates to ASN1_item_{sign,verify}() that the pkey's
 * ASN.1 method needs to handle algorithm identifiers and part of the message
 * digest.
 */

static const struct {
	int sign_nid;
	int hash_nid;
	int pkey_nid;
} nid_triple[] = {
	{
		.sign_nid = NID_md2WithRSAEncryption,
		.hash_nid = NID_md2,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_md5WithRSAEncryption,
		.hash_nid = NID_md5,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_shaWithRSAEncryption,
		.hash_nid = NID_sha,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_sha1WithRSAEncryption,
		.hash_nid = NID_sha1,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_dsaWithSHA,
		.hash_nid = NID_sha,
		.pkey_nid = NID_dsa,
	},
	{
		.sign_nid = NID_dsaWithSHA1_2,
		.hash_nid = NID_sha1,
		.pkey_nid = NID_dsa_2,
	},
	{
		.sign_nid = NID_mdc2WithRSA,
		.hash_nid = NID_mdc2,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_md5WithRSA,
		.hash_nid = NID_md5,
		.pkey_nid = NID_rsa,
	},
	{
		.sign_nid = NID_dsaWithSHA1,
		.hash_nid = NID_sha1,
		.pkey_nid = NID_dsa,
	},
	{
		.sign_nid = NID_sha1WithRSA,
		.hash_nid = NID_sha1,
		.pkey_nid = NID_rsa,
	},
	{
		.sign_nid = NID_ripemd160WithRSA,
		.hash_nid = NID_ripemd160,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_md4WithRSAEncryption,
		.hash_nid = NID_md4,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_ecdsa_with_SHA1,
		.hash_nid = NID_sha1,
		.pkey_nid = NID_X9_62_id_ecPublicKey,
	},
	{
		.sign_nid = NID_sha256WithRSAEncryption,
		.hash_nid = NID_sha256,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_sha384WithRSAEncryption,
		.hash_nid = NID_sha384,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_sha512WithRSAEncryption,
		.hash_nid = NID_sha512,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_sha224WithRSAEncryption,
		.hash_nid = NID_sha224,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_ecdsa_with_Recommended,
		.hash_nid = NID_undef,
		.pkey_nid = NID_X9_62_id_ecPublicKey,
	},
	{
		.sign_nid = NID_ecdsa_with_Specified,
		.hash_nid = NID_undef,
		.pkey_nid = NID_X9_62_id_ecPublicKey,
	},
	{
		.sign_nid = NID_ecdsa_with_SHA224,
		.hash_nid = NID_sha224,
		.pkey_nid = NID_X9_62_id_ecPublicKey,
	},
	{
		.sign_nid = NID_ecdsa_with_SHA256,
		.hash_nid = NID_sha256,
		.pkey_nid = NID_X9_62_id_ecPublicKey,
	},
	{
		.sign_nid = NID_ecdsa_with_SHA384,
		.hash_nid = NID_sha384,
		.pkey_nid = NID_X9_62_id_ecPublicKey,
	},
	{
		.sign_nid = NID_ecdsa_with_SHA512,
		.hash_nid = NID_sha512,
		.pkey_nid = NID_X9_62_id_ecPublicKey,
	},
	{
		.sign_nid = NID_dsa_with_SHA224,
		.hash_nid = NID_sha224,
		.pkey_nid = NID_dsa,
	},
	{
		.sign_nid = NID_dsa_with_SHA256,
		.hash_nid = NID_sha256,
		.pkey_nid = NID_dsa,
	},
	{
		.sign_nid = NID_id_GostR3411_94_with_GostR3410_2001,
		.hash_nid = NID_id_GostR3411_94,
		.pkey_nid = NID_id_GostR3410_2001,
	},
	{
		.sign_nid = NID_id_GostR3411_94_with_GostR3410_94,
		.hash_nid = NID_id_GostR3411_94,
		.pkey_nid = NID_id_GostR3410_94,
	},
	{
		.sign_nid = NID_id_GostR3411_94_with_GostR3410_94_cc,
		.hash_nid = NID_id_GostR3411_94,
		.pkey_nid = NID_id_GostR3410_94_cc,
	},
	{
		.sign_nid = NID_id_GostR3411_94_with_GostR3410_2001_cc,
		.hash_nid = NID_id_GostR3411_94,
		.pkey_nid = NID_id_GostR3410_2001_cc,
	},
	{
		.sign_nid = NID_rsassaPss,
		.hash_nid = NID_undef,
		.pkey_nid = NID_rsassaPss,
	},
	{
		.sign_nid = NID_id_tc26_signwithdigest_gost3410_2012_256,
		.hash_nid = NID_id_tc26_gost3411_2012_256,
		.pkey_nid = NID_id_GostR3410_2001,
	},
	{
		.sign_nid = NID_id_tc26_signwithdigest_gost3410_2012_512,
		.hash_nid = NID_id_tc26_gost3411_2012_512,
		.pkey_nid = NID_id_GostR3410_2001,
	},
	{
		.sign_nid = NID_Ed25519,
		.hash_nid = NID_undef,
		.pkey_nid = NID_Ed25519,
	},
	{
		.sign_nid = NID_dhSinglePass_stdDH_sha1kdf_scheme,
		.hash_nid = NID_sha1,
		.pkey_nid = NID_dh_std_kdf,
	},
	{
		.sign_nid = NID_dhSinglePass_stdDH_sha224kdf_scheme,
		.hash_nid = NID_sha224,
		.pkey_nid = NID_dh_std_kdf,
	},
	{
		.sign_nid = NID_dhSinglePass_stdDH_sha256kdf_scheme,
		.hash_nid = NID_sha256,
		.pkey_nid = NID_dh_std_kdf,
	},
	{
		.sign_nid = NID_dhSinglePass_stdDH_sha384kdf_scheme,
		.hash_nid = NID_sha384,
		.pkey_nid = NID_dh_std_kdf,
	},
	{
		.sign_nid = NID_dhSinglePass_stdDH_sha512kdf_scheme,
		.hash_nid = NID_sha512,
		.pkey_nid = NID_dh_std_kdf,
	},
	{
		.sign_nid = NID_dhSinglePass_cofactorDH_sha1kdf_scheme,
		.hash_nid = NID_sha1,
		.pkey_nid = NID_dh_cofactor_kdf,
	},
	{
		.sign_nid = NID_dhSinglePass_cofactorDH_sha224kdf_scheme,
		.hash_nid = NID_sha224,
		.pkey_nid = NID_dh_cofactor_kdf,
	},
	{
		.sign_nid = NID_dhSinglePass_cofactorDH_sha256kdf_scheme,
		.hash_nid = NID_sha256,
		.pkey_nid = NID_dh_cofactor_kdf,
	},
	{
		.sign_nid = NID_dhSinglePass_cofactorDH_sha384kdf_scheme,
		.hash_nid = NID_sha384,
		.pkey_nid = NID_dh_cofactor_kdf,
	},
	{
		.sign_nid = NID_dhSinglePass_cofactorDH_sha512kdf_scheme,
		.hash_nid = NID_sha512,
		.pkey_nid = NID_dh_cofactor_kdf,
	},
	{
		.sign_nid = NID_RSA_SHA3_224,
		.hash_nid = NID_sha3_224,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_RSA_SHA3_256,
		.hash_nid = NID_sha3_256,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_RSA_SHA3_384,
		.hash_nid = NID_sha3_384,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_RSA_SHA3_512,
		.hash_nid = NID_sha3_512,
		.pkey_nid = NID_rsaEncryption,
	},
	{
		.sign_nid = NID_ecdsa_with_SHA3_224,
		.hash_nid = NID_sha3_224,
		.pkey_nid = NID_X9_62_id_ecPublicKey,
	},
	{
		.sign_nid = NID_ecdsa_with_SHA3_256,
		.hash_nid = NID_sha3_256,
		.pkey_nid = NID_X9_62_id_ecPublicKey,
	},
	{
		.sign_nid = NID_ecdsa_with_SHA3_384,
		.hash_nid = NID_sha3_384,
		.pkey_nid = NID_X9_62_id_ecPublicKey,
	},
	{
		.sign_nid = NID_ecdsa_with_SHA3_512,
		.hash_nid = NID_sha3_512,
		.pkey_nid = NID_X9_62_id_ecPublicKey,
	},
};

#define N_NID_TRIPLES (sizeof(nid_triple) / sizeof(nid_triple[0]))

int
OBJ_find_sigid_algs(int sign_nid, int *hash_nid, int *pkey_nid)
{
	size_t i;

	for (i = 0; i < N_NID_TRIPLES; i++) {
		if (sign_nid != nid_triple[i].sign_nid)
			continue;

		if (hash_nid != NULL)
			*hash_nid = nid_triple[i].hash_nid;
		if (pkey_nid != NULL)
			*pkey_nid = nid_triple[i].pkey_nid;

		return 1;
	}

	return 0;
}
LCRYPTO_ALIAS(OBJ_find_sigid_algs);

int
OBJ_find_sigid_by_algs(int *sign_nid, int hash_nid, int pkey_nid)
{
	size_t i;

	for (i = 0; i < N_NID_TRIPLES; i++) {
		if (hash_nid != nid_triple[i].hash_nid)
			continue;
		if (pkey_nid != nid_triple[i].pkey_nid)
			continue;

		if (sign_nid != NULL)
			*sign_nid = nid_triple[i].sign_nid;

		return 1;
	}

	return 0;
}
LCRYPTO_ALIAS(OBJ_find_sigid_by_algs);
