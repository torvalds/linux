/*	$OpenBSD: ecdsatest.c,v 1.18 2023/11/19 13:11:06 tb Exp $	*/
/*
 * Written by Nils Larsch for the OpenSSL project.
 */
/* ====================================================================
 * Copyright (c) 2000-2005 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */
/* ====================================================================
 * Copyright 2002 Sun Microsystems, Inc. ALL RIGHTS RESERVED.
 *
 * Portions of the attached software ("Contribution") are developed by
 * SUN MICROSYSTEMS, INC., and are contributed to the OpenSSL project.
 *
 * The Contribution is licensed pursuant to the OpenSSL open source
 * license provided above.
 *
 * The elliptic curve binary polynomial software is originally written by
 * Sheueling Chang Shantz and Douglas Stebila of Sun Microsystems Laboratories.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/crypto.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/ecdsa.h>
#include <openssl/err.h>

int test_builtin(void);

int
test_builtin(void)
{
	unsigned char digest[20], wrong_digest[20];
	EC_builtin_curve *curves = NULL;
	size_t num_curves = 0, n = 0;
	EC_KEY *eckey = NULL, *wrong_eckey = NULL;
	EC_GROUP *group;
	ECDSA_SIG *ecdsa_sig = NULL;
	BIGNUM *r = NULL, *s = NULL;
	unsigned char *signature = NULL;
	const unsigned char *sig_ptr;
	unsigned char *sig_ptr2;
	unsigned char *raw_buf = NULL;
	unsigned int sig_len, degree, r_len, s_len, bn_len, buf_len;
	int nid;
	int failed = 1;

	/* fill digest values with some random data */
	arc4random_buf(digest, 20);
	arc4random_buf(wrong_digest, 20);

	/* create and verify a ecdsa signature with every available curve */
	printf("\ntesting ECDSA_sign() and ECDSA_verify() "
	    "with some internal curves:\n");

	/* get a list of all internal curves */
	num_curves = EC_get_builtin_curves(NULL, 0);

	curves = reallocarray(NULL, sizeof(EC_builtin_curve), num_curves);
	if (curves == NULL) {
		printf("reallocarray error\n");
		goto err;
	}

	if (!EC_get_builtin_curves(curves, num_curves)) {
		printf("unable to get internal curves\n");
		goto err;
	}

	/* now create and verify a signature for every curve */
	for (n = 0; n < num_curves; n++) {
		unsigned char dirt, offset;

		nid = curves[n].nid;
		if (nid == NID_ipsec4)
			continue;

		if ((eckey = EC_KEY_new()) == NULL)
			goto err;
		group = EC_GROUP_new_by_curve_name(nid);
		if (group == NULL)
			goto err;
		if (EC_KEY_set_group(eckey, group) == 0)
			goto err;
		degree = EC_GROUP_get_degree(group);
		EC_GROUP_free(group);
		if (degree < 160) {
			/* drop the curve */
			EC_KEY_free(eckey);
			eckey = NULL;
			continue;
		}
		printf("%s: ", OBJ_nid2sn(nid));

		if (!EC_KEY_generate_key(eckey)) {
			goto err;
		}

		/* Exercise ECParameters_dup() and let ASAN test for leaks. */
		if ((wrong_eckey = ECParameters_dup(eckey)) == NULL)
			goto err;
		group = EC_GROUP_new_by_curve_name(nid);
		if (group == NULL)
			goto err;
		if (EC_KEY_set_group(wrong_eckey, group) == 0)
			goto err;
		EC_GROUP_free(group);
		if (!EC_KEY_generate_key(wrong_eckey))
			goto err;

		printf(".");
		fflush(stdout);

		if (!EC_KEY_check_key(eckey))
			goto err;

		printf(".");
		fflush(stdout);

		if ((sig_len = ECDSA_size(eckey)) == 0)
			goto err;
		if ((signature = malloc(sig_len)) == NULL)
			goto err;
		if (!ECDSA_sign(0, digest, 20, signature, &sig_len, eckey))
			goto err;

		printf(".");
		fflush(stdout);

		if (ECDSA_verify(0, digest, 20, signature, sig_len, eckey) != 1)
			goto err;

		printf(".");
		fflush(stdout);

		/* verify signature with the wrong key */
		if (ECDSA_verify(0, digest, 20, signature, sig_len,
		    wrong_eckey) == 1)
			goto err;

		printf(".");
		fflush(stdout);

		if (ECDSA_verify(0, wrong_digest, 20, signature, sig_len,
		    eckey) == 1)
			goto err;

		printf(".");
		fflush(stdout);

		if (ECDSA_verify(0, digest, 20, signature, sig_len - 1,
		    eckey) == 1)
			goto err;

		printf(".");
		fflush(stdout);

		/*
		 * Modify a single byte of the signature: to ensure we don't
		 * garble the ASN1 structure, we read the raw signature and
		 * modify a byte in one of the bignums directly.
		 */
		sig_ptr = signature;
		if ((ecdsa_sig = d2i_ECDSA_SIG(NULL, &sig_ptr,
		    sig_len)) == NULL)
			goto err;

		/* Store the two BIGNUMs in raw_buf. */
		r_len = BN_num_bytes(ECDSA_SIG_get0_r(ecdsa_sig));
		s_len = BN_num_bytes(ECDSA_SIG_get0_s(ecdsa_sig));
		bn_len = (degree + 7) / 8;
		if ((r_len > bn_len) || (s_len > bn_len))
			goto err;

		buf_len = 2 * bn_len;
		if ((raw_buf = calloc(1, buf_len)) == NULL)
			goto err;
		BN_bn2bin(ECDSA_SIG_get0_r(ecdsa_sig),
		    raw_buf + bn_len - r_len);
		BN_bn2bin(ECDSA_SIG_get0_s(ecdsa_sig),
		    raw_buf + buf_len - s_len);

		/* Modify a single byte in the buffer. */
		offset = raw_buf[10] % buf_len;
		dirt = raw_buf[11] ? raw_buf[11] : 1;
		raw_buf[offset] ^= dirt;
		/* Now read the BIGNUMs back in from raw_buf. */
		if ((r = BN_bin2bn(raw_buf, bn_len, NULL)) == NULL ||
		    (s = BN_bin2bn(raw_buf + bn_len, bn_len, NULL)) == NULL)
			goto err;
		if (!ECDSA_SIG_set0(ecdsa_sig, r, s))
			goto err;
		r = NULL;
		s = NULL;

		if ((sig_len = i2d_ECDSA_SIG(ecdsa_sig, NULL)) <= 0)
			goto err;
		free(signature);
		if ((signature = calloc(1, sig_len)) == NULL)
			goto err;

		sig_ptr2 = signature;
		if ((sig_len = i2d_ECDSA_SIG(ecdsa_sig, &sig_ptr2)) <= 0)
			goto err;
		if (ECDSA_verify(0, digest, 20, signature, sig_len, eckey) == 1)
			goto err;

		/* Sanity check: undo the modification and verify signature. */
		raw_buf[offset] ^= dirt;
		if ((r = BN_bin2bn(raw_buf, bn_len, NULL)) == NULL ||
		    (s = BN_bin2bn(raw_buf + bn_len, bn_len, NULL)) == NULL)
			goto err;
		if (!ECDSA_SIG_set0(ecdsa_sig, r, s))
			goto err;
		r = NULL;
		s = NULL;

		if ((sig_len = i2d_ECDSA_SIG(ecdsa_sig, NULL)) <= 0)
			goto err;
		free(signature);
		if ((signature = calloc(1, sig_len)) == NULL)
			goto err;

		sig_ptr2 = signature;
		if ((sig_len = i2d_ECDSA_SIG(ecdsa_sig, &sig_ptr2)) <= 0)
			goto err;
		if (ECDSA_verify(0, digest, 20, signature, sig_len,
		    eckey) != 1)
			goto err;

		printf(".");
		fflush(stdout);

		printf(" ok\n");

		ERR_clear_error();
		free(signature);
		signature = NULL;
		EC_KEY_free(eckey);
		eckey = NULL;
		EC_KEY_free(wrong_eckey);
		wrong_eckey = NULL;
		ECDSA_SIG_free(ecdsa_sig);
		ecdsa_sig = NULL;
		free(raw_buf);
		raw_buf = NULL;
	}

	failed = 0;

 err:
	if (failed)
		printf(" failed\n");

	BN_free(r);
	BN_free(s);
	EC_KEY_free(eckey);
	EC_KEY_free(wrong_eckey);
	ECDSA_SIG_free(ecdsa_sig);
	free(signature);
	free(raw_buf);
	free(curves);

	return failed;
}

int
main(void)
{
	int failed = 1;

	/* the tests */
	if (test_builtin())
		goto err;

	printf("\nECDSA test passed\n");
	failed = 0;

 err:
	if (failed) {
		printf("\nECDSA test failed\n");
		ERR_print_errors_fp(stdout);
	}

	CRYPTO_cleanup_all_ex_data();
	ERR_remove_thread_state(NULL);
	ERR_free_strings();

	return failed;
}
