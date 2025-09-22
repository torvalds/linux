/*	$OpenBSD: rsa_method_test.c,v 1.6 2025/08/26 05:07:50 tb Exp $ */

/*
 * Copyright (c) 2025 Theo Buehler <tb@openbsd.org>
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

#include <assert.h>
#include <err.h>
#include <stdint.h>
#include <stdio.h>

#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

/*
 * XXX - This currently only covers sign and verify.
 */

/* sigh */
static int ex_index;

/* Unsure if this applies to RSA, ASN.1, or the OpenSSL code base altogether. */
static const uint8_t msg[] = {
	0x44, 0x69, 0x65, 0x2c, 0x20, 0x64, 0x69, 0x65,
	0x2c, 0x20, 0x64, 0x69, 0x65, 0x2c, 0x20, 0x6d,
	0x79, 0x20, 0x64, 0x61, 0x72, 0x6c, 0x69, 0x6e,
	0x67, 0x0a, 0x44, 0x6f, 0x6e, 0x27, 0x74, 0x20,
	0x75, 0x74, 0x74, 0x65, 0x72, 0x20, 0x61, 0x20,
	0x73, 0x69, 0x6e, 0x67, 0x6c, 0x65, 0x20, 0x77,
	0x6f, 0x72, 0x64, 0x0a, 0x44, 0x69, 0x65, 0x2c,
	0x20, 0x64, 0x69, 0x65, 0x2c, 0x20, 0x64, 0x69,
	0x65, 0x2c, 0x20, 0x6d, 0x79, 0x20, 0x64, 0x61,
	0x72, 0x6c, 0x69, 0x6e, 0x67, 0x0a, 0x53, 0x68,
	0x75, 0x74, 0x20, 0x79, 0x6f, 0x75, 0x72, 0x20,
	0x70, 0x72, 0x65, 0x74, 0x74, 0x79, 0x20, 0x65,
	0x79, 0x65, 0x73, 0x0a, 0x0a, 0x49, 0x27, 0x6c,
	0x6c, 0x20, 0x62, 0x65, 0x20, 0x73, 0x65, 0x65,
	0x69, 0x6e, 0x67, 0x20, 0x79, 0x6f, 0x75, 0x20,
	0x61, 0x67, 0x61, 0x69, 0x6e, 0x0a, 0x49, 0x27,
	0x6c, 0x6c, 0x20, 0x62, 0x65, 0x20, 0x73, 0x65,
	0x65, 0x69, 0x6e, 0x67, 0x20, 0x79, 0x6f, 0x75,
	0x20, 0x69, 0x6e, 0x20, 0x68, 0x65, 0x6c, 0x6c,
	0x0a, 0x0a, 0x54, 0x68, 0x65, 0x20, 0x4d, 0x69,
	0x73, 0x66, 0x69, 0x74, 0x73, 0x20, 0x7e, 0x20,
	0x31, 0x39, 0x38, 0x32,
};

static int
sign_and_verify(const char *descr, EVP_PKEY *priv, EVP_PKEY *pub)
{
	ASN1_IA5STRING *message = NULL;
	ASN1_BIT_STRING *signature = NULL;
	X509_ALGOR *x509_alg = NULL;
	const ASN1_OBJECT *oid;
	int nid, ret;
	int failed = 1;

	if ((message = ASN1_IA5STRING_new()) == NULL)
		errx(1, "%s: ASN1_IA5STRING_new", __func__);
	if (!ASN1_STRING_set(message, msg, sizeof(msg)))
		errx(1, "%s: ASN1_STRING_set", __func__);

	if ((signature = ASN1_BIT_STRING_new()) == NULL)
		errx(1, "%s: ASN1_BIT_STRING_new", __func__);
	if ((x509_alg = X509_ALGOR_new()) == NULL)
		errx(1, "%s: X509_ALGOR_new", __func__);
	if ((ret = ASN1_item_sign(&ASN1_IA5STRING_it, x509_alg, NULL, signature,
	    message, priv, EVP_sha256())) <= 0) {
		fprintf(stderr, "FAIL: %s (%s): ASN1_item_sign() returned %d\n",
		    __func__, descr, ret);
		ERR_print_errors_fp(stderr);
		goto err;
	}

	X509_ALGOR_get0(&oid, NULL, NULL, x509_alg);
	if ((nid = OBJ_obj2nid(oid)) != NID_sha256WithRSAEncryption) {
		fprintf(stderr, "FAIL: %s (%s): OBJ_obj2nid(): want %d, got %d\n",
		    __func__, descr, NID_sha256WithRSAEncryption, nid);
		goto err;
	}

	if ((ret = ASN1_item_verify(&ASN1_IA5STRING_it, x509_alg, signature,
	    message, pub)) != 1) {
		fprintf(stderr, "FAIL: %s (%s): ASN1_item_verify() returned %d\n",
		    __func__, descr, ret);
		ERR_print_errors_fp(stderr);
		goto err;
	}

	failed = 0;

 err:
	ASN1_IA5STRING_free(message);
	ASN1_BIT_STRING_free(signature);
	X509_ALGOR_free(x509_alg);

	return failed;
}

static void
generate_rsa_keypair(int bits, int exponent, RSA **out_priv, RSA **out_pub)
{
	BIGNUM *e;
	RSA *rsa;

	assert(out_priv == NULL || *out_priv == NULL);
	assert(out_pub == NULL || *out_pub == NULL);

	if ((e = BN_new()) == NULL)
		errx(1, "%s: BN_new()", __func__);
	if (!BN_set_word(e, exponent))
		errx(1, "%s: BN_set_word()", __func__);

	if ((rsa = RSA_new()) == NULL)
		errx(1, "%s: RSA_new()", __func__);
	if (!RSA_generate_key_ex(rsa, bits, e, NULL))
		errx(1, "%s: RSA_generate_key_ex", __func__);

	/* Take the opportunity to exercise these two functions. */
	if (out_priv != NULL) {
		if ((*out_priv = RSAPrivateKey_dup(rsa)) == NULL)
			errx(1, "%s: RSAPrivateKey_dup", __func__);
	}
	if (out_pub != NULL) {
		if ((*out_pub = RSAPublicKey_dup(rsa)) == NULL)
			errx(1, "%s: RSAPublicKey_dup", __func__);
	}

	RSA_free(rsa);
	BN_free(e);
}

static void
rsa_to_evp(RSA *rsa, EVP_PKEY **out_evp)
{
	assert(*out_evp == NULL);

	if ((*out_evp = EVP_PKEY_new()) == NULL)
		errx(1, "%s: EVP_PKEY_new", __func__);
	if (!EVP_PKEY_set1_RSA(*out_evp, rsa))
		errx(1, "%s: EVP_PKEY_set1_RSA", __func__);
}

static void
clear_evp_keys(EVP_PKEY **evp_priv, EVP_PKEY **evp_pub)
{
	EVP_PKEY_free(*evp_priv);
	EVP_PKEY_free(*evp_pub);
	*evp_priv = NULL;
	*evp_pub = NULL;
}

static int
rsa_method_app_data_sign(int dtype, const unsigned char *m, unsigned int m_len,
    unsigned char *sig, unsigned int *sig_len, const RSA *rsa)
{
	const RSA_METHOD *method = RSA_get_method(rsa);
	RSA *sign_rsa = RSA_meth_get0_app_data(method);

	return RSA_sign(dtype, m, m_len, sig, sig_len, sign_rsa);
}

static int
rsa_ex_data_verify(int dtype, const unsigned char *m, unsigned int m_len,
    const unsigned char *sig, unsigned int sig_len, const RSA *rsa)
{
	RSA *verify_rsa;

	assert(ex_index != 0);

	if ((verify_rsa = RSA_get_ex_data(rsa, ex_index)) == NULL)
		errx(1, "%s: RSA_get_ex_data", __func__);

	return RSA_verify(dtype, m, m_len, sig, sig_len, verify_rsa);
}

static int
sign_and_verify_test(void)
{
	RSA_METHOD *sign_verify_method = NULL;
	RSA *rsa_priv = NULL, *rsa_pub = NULL, *rsa_bogus = NULL;
	EVP_PKEY *evp_priv = NULL, *evp_pub = NULL;
	int failed = 0;

	assert(ex_index != 0);

	/*
	 * XXX - Hilarity ensues if the public key sizes don't match.
	 * One reason is that EVP_PKEY_sign() uses EVP_PKEY_size()
	 * which ignores the RSA method. Awesome design is awesome and
	 * OpenSSL's abstractions are leakier than Manneken Pis.
	 */
	generate_rsa_keypair(2048, RSA_F4, &rsa_priv, &rsa_pub);
	generate_rsa_keypair(2048, RSA_3, NULL, &rsa_bogus);

	rsa_to_evp(rsa_priv, &evp_priv);
	rsa_to_evp(rsa_pub, &evp_pub);

	failed |= sign_and_verify("default method", evp_priv, evp_pub);

	clear_evp_keys(&evp_priv, &evp_pub);

	if (!RSA_set_ex_data(rsa_bogus, ex_index, rsa_pub))
		errx(1, "%s: RSA_set_ex_data", __func__);

	if ((sign_verify_method = RSA_meth_dup(RSA_get_default_method())) == NULL)
		errx(1, "%s: RSA_meth_dup", __func__);
	if (!RSA_meth_set0_app_data(sign_verify_method, rsa_priv))
		errx(1, "%s: RSA_meth_set0_app_data", __func__);

	if (!RSA_meth_set_sign(sign_verify_method, rsa_method_app_data_sign))
		errx(1, "%s: RSA_meth_set_sign", __func__);
	if (!RSA_meth_set_verify(sign_verify_method, rsa_ex_data_verify))
		errx(1, "%s: RSA_meth_set_verify", __func__);

	if (!RSA_set_method(rsa_bogus, sign_verify_method))
		errx(1, "%s: RSA_set_method", __func__);

	rsa_to_evp(rsa_bogus, &evp_priv);
	rsa_to_evp(rsa_pub, &evp_pub);

	failed |= sign_and_verify("app data sign method", evp_priv, evp_pub);

	clear_evp_keys(&evp_priv, &evp_pub);

	rsa_to_evp(rsa_priv, &evp_priv);
	rsa_to_evp(rsa_bogus, &evp_pub);

	failed |= sign_and_verify("ex data verify method", evp_priv, evp_pub);

	clear_evp_keys(&evp_priv, &evp_pub);

	rsa_to_evp(rsa_bogus, &evp_priv);
	rsa_to_evp(rsa_bogus, &evp_pub);

	failed |= sign_and_verify("both sides bogus", evp_priv, evp_pub);

	RSA_free(rsa_priv);
	RSA_free(rsa_pub);
	RSA_free(rsa_bogus);
	EVP_PKEY_free(evp_priv);
	EVP_PKEY_free(evp_pub);
	RSA_meth_free(sign_verify_method);

	return failed;
}

int
main(void)
{
	int failed = 0;

	if ((ex_index = RSA_get_ex_new_index(0, NULL, NULL, NULL, NULL)) <= 0)
		errx(1, "RSA_get_ex_new_index");

	failed |= sign_and_verify_test();

	return failed;
}
