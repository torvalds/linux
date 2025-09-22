/*	$OpenBSD: pbkdf2.c,v 1.3 2023/11/19 13:11:06 tb Exp $	*/
/* Written by Christian Heimes, 2013 */
/*
 * Copyright (c) 2013 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
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
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <openssl/opensslconf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/conf.h>

typedef struct {
	const char *pass;
	int passlen;
	const char *salt;
	int saltlen;
	int iter;
} testdata;

static const testdata test_cases[] = {
	{"password", 8, "salt", 4, 1},
	{"password", 8, "salt", 4, 2},
	{"password", 8, "salt", 4, 4096},
	{"passwordPASSWORDpassword", 24,
	 "saltSALTsaltSALTsaltSALTsaltSALTsalt", 36, 4096},
	{"pass\0word", 9, "sa\0lt", 5, 4096},
	{NULL},
};

static const char *sha1_results[] = {
	"0c60c80f961f0e71f3a9b524af6012062fe037a6",
	"ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957",
	"4b007901b765489abead49d926f721d065a429c1",
	"3d2eec4fe41c849b80c8d83662c0e44a8b291a964cf2f07038",
	"56fa6aa75548099dcc37d7f03425e0c3",
};

static const char *sha256_results[] = {
	"120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b",
	"ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43",
	"c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a",
	"348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c63551"
		"8c7dac47e9",
	"89b69d0516f829893c696226650a8687",
};

static const char *sha512_results[] = {
	"867f70cf1ade02cff3752599a3a53dc4af34c7a669815ae5d513554e1c8cf252c02d47"
		"0a285a0501bad999bfe943c08f050235d7d68b1da55e63f73b60a57fce",
	"e1d9c16aa681708a45f5c7c4e215ceb66e011a2e9f0040713f18aefdb866d53cf76cab"
		"2868a39b9f7840edce4fef5a82be67335c77a6068e04112754f27ccf4e",
	"d197b1b33db0143e018b12f3d1d1479e6cdebdcc97c5c0f87f6902e072f457b5143f30"
		"602641b3d55cd335988cb36b84376060ecd532e039b742a239434af2d5",
	"8c0511f4c6e597c6ac6315d8f0362e225f3c501495ba23b868c005174dc4ee71115b59"
		"f9e60cd9532fa33e0f75aefe30225c583a186cd82bd4daea9724a3d3b8",
	"9d9e9c4cd21fe4be24d5b8244c759665",
};

static void
hexdump(FILE *f, const char *title, const unsigned char *s, size_t len)
{
	fprintf(f, "%s", title);
	for (; len != 0; len--)
		fprintf(f, " 0x%02x", *(s++));
	fprintf(f, "\n");
}

static int
convert(unsigned char *dst, const unsigned char *src, size_t len)
{
	unsigned int n;

	for (; len != 0; src += 2, len--) {
		if (sscanf((char *)src, "%2x", &n) != 1)
			return EINVAL;
		*dst++ = (unsigned char)n;
	}
	return 0;
}

static void
test_p5_pbkdf2(unsigned int n, const char *digestname, const testdata *test,
    const char *hex)
{
	const EVP_MD *digest;
	unsigned char *out;
	unsigned char *expected;
	size_t keylen;
	int r;

	digest = EVP_get_digestbyname(digestname);
	if (digest == NULL) {
		fprintf(stderr, "unknown digest %s\n", digestname);
		exit(5);
	}

	keylen = strlen(hex);
	if ((keylen % 2) != 0) {
		fprintf(stderr, "odd hex string %s, digest %u\n", digestname, n);
		exit(5);
	}
	keylen /= 2;
	expected = malloc(keylen);
	out = malloc(keylen);
	if (expected == NULL || out == NULL) {
		fprintf(stderr, "malloc() failed\n");
		exit(5);
	}
	if (convert(expected, (const unsigned char *)hex, keylen) != 0) {
		fprintf(stderr, "invalid hex string %s, digest %u\n", hex, n);
		exit(5);
	}

	r = PKCS5_PBKDF2_HMAC(test->pass, test->passlen,
	    (const unsigned char *)test->salt, test->saltlen,
	    test->iter, digest, keylen, out);

	if (r == 0) {
		fprintf(stderr, "PKCS5_PBKDF2_HMAC(%s) failure test %u\n",
		    digestname, n);
		exit(3);
	}
	if (memcmp(expected, out, keylen) != 0) {
		fprintf(stderr,
		    "Wrong result for PKCS5_PBKDF2_HMAC(%s) test %u\n",
		    digestname, n);
		hexdump(stderr, "expected: ", expected, keylen);
		hexdump(stderr, "result:   ", out, keylen);
		exit(2);
	}
	free(expected);
	free(out);
}

int
main(int argc,char **argv)
{
	unsigned int n;
	const testdata *test = test_cases;

	OpenSSL_add_all_digests();

	for (n = 0; test->pass != NULL; n++, test++) {
		test_p5_pbkdf2(n, "sha1", test, sha1_results[n]);
		test_p5_pbkdf2(n, "sha256", test, sha256_results[n]);
		test_p5_pbkdf2(n, "sha512", test, sha512_results[n]);
	}

	EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();
	ERR_remove_thread_state(NULL);
	ERR_free_strings();
	return 0;
}
