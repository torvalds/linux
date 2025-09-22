/* $OpenBSD: x509_asn1.c,v 1.21 2023/12/13 05:57:37 tb Exp $ */
/*
 * Copyright (c) 2023 Job Snijders <job@openbsd.org>
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

/*
 * This program tests whether the presence of "->enc.modified = 1;"
 * in select X509 setter functions properly triggers invalidation of cached
 * DER.
 */

#include <err.h>
#include <stdio.h>
#include <string.h>

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

static void
x509_setup(unsigned char **der, unsigned char **der2, X509 **x,
    long dersz, long *der2sz)
{
	const unsigned char *cpder;

	cpder = *der;
	if ((*x = d2i_X509(NULL, &cpder, dersz)) == NULL)
		errx(1, "d2i_X509");
	if ((*der2sz = i2d_X509(*x, der2)) <= 0)
		errx(1, "i2d_X509");
}

static void
x509_cleanup(X509 **x, unsigned char **der)
{
	X509_free(*x);
	*x = NULL;
	free(*der);
	*der = NULL;
}

static void
x509_set_integer(const char *descr, int (*f)(X509 *, ASN1_INTEGER *), X509 **x,
    int i)
{
	ASN1_INTEGER *ai;

	if ((ai = ASN1_INTEGER_new()) == NULL)
		err(1, NULL);
	if (!ASN1_INTEGER_set(ai, i))
		errx(1, "ASN1_INTEGER_set");
	if (!f(*x, ai))
		errx(1, "%s: %s failed", __func__, descr);

	ASN1_INTEGER_free(ai);
}

static void
x509_set_name(const char *descr, int (*f)(X509 *, X509_NAME *), X509 **x,
    const unsigned char *n)
{
	X509_NAME *xn;

	if ((xn = X509_NAME_new()) == NULL)
		err(1, NULL);
	if (!X509_NAME_add_entry_by_txt(xn, "C", MBSTRING_ASC, n, -1, -1, 0))
		errx(1, "X509_NAME_add_entry_by_txt");
	if (!f(*x, xn))
		errx(1, "%s: %s failed", __func__, descr);

	X509_NAME_free(xn);
}

static void
x509_set_time(const char *descr, int (*f)(X509 *, const ASN1_TIME *), X509 **x,
    int t)
{
	ASN1_TIME *at;

	if ((at = X509_gmtime_adj(NULL, t)) == NULL)
		errx(1, "X509_gmtime_adj");
	if (!f(*x, at))
		errx(1, "%s: %s failed", __func__, descr);

	ASN1_TIME_free(at);
}

static int
x509_compare(char *f, X509 *a, const unsigned char *der, long dersz)
{
	unsigned char *der_test = NULL;
	long der_testsz;
	int rc = 0;

	if ((der_testsz = i2d_X509(a, &der_test)) <= 0)
		errx(1, "i2d_X509");

	if (dersz == der_testsz) {
		if (memcmp(der, der_test, dersz) == 0) {
			warnx("%s() stale version of encoding after i2d", f);
			rc = 1;
		} else
			warnx("%s() OK", f);
	} else
		warnx("%s() OK", f);

	free(der_test);
	return rc;
}

static void
x509_crl_setup(unsigned char **der, unsigned char **der2, X509_CRL **xc,
    long dersz, long *der2sz)
{
	const unsigned char *cpder;

	cpder = *der;
	if ((*xc = d2i_X509_CRL(NULL, &cpder, dersz)) == NULL)
		errx(1, "d2i_X509");
	if ((*der2sz = i2d_X509_CRL(*xc, der2)) <= 0)
		errx(1, "i2d_X509");
}

static void
x509_crl_cleanup(X509_CRL **xc, unsigned char **der)
{
	X509_CRL_free(*xc);
	*xc = NULL;
	free(*der);
	*der = NULL;
}

static void
x509_crl_set_name(const char *descr, int (*f)(X509_CRL *, X509_NAME *),
    X509_CRL **xc, const unsigned char *n)
{
	X509_NAME *xn;

	if ((xn = X509_NAME_new()) == NULL)
		err(1, NULL);
	if (!X509_NAME_add_entry_by_txt(xn, "C", MBSTRING_ASC, n, -1, -1, 0))
		errx(1, "X509_NAME_add_entry_by_txt");
	if (!f(*xc, xn))
		errx(1, "%s: %s failed", __func__, descr);

	X509_NAME_free(xn);
}

static void
x509_crl_set_time(const char *descr, int (*f)(X509_CRL *, const ASN1_TIME *),
    X509_CRL **xc, int t)
{
	ASN1_TIME *at;

	if ((at = X509_gmtime_adj(NULL, t)) == NULL)
		errx(1, "X509_gmtime_adj");
	if (!f(*xc, at))
		errx(1, "%s: %s failed", __func__, descr);

	ASN1_TIME_free(at);
}

static int
x509_crl_compare(char *f, X509_CRL *ac, const unsigned char *der, long dersz)
{
	unsigned char *der_test = NULL;
	long der_testsz;
	int rc = 0;

	if ((der_testsz = i2d_X509_CRL(ac, &der_test)) <= 0)
		errx(1, "i2d_X509_CRL");

	if (dersz == der_testsz) {
		if (memcmp(der, der_test, dersz) == 0) {
			warnx("%s() stale version of encoding after i2d", f);
			rc = 1;
		} else
			warnx("%s() OK", f);
	} else
		warnx("%s() OK", f);

	free(der_test);
	return rc;
}

static int
test_x509_setters(void)
{
	EVP_PKEY *pkey = NULL;
	EVP_PKEY_CTX *pkey_ctx = NULL;
	X509 *a, *x;
	unsigned char *der = NULL, *der2 = NULL;
	long dersz, der2sz;
	int failed = 0;

	if ((x = X509_new()) == NULL)
		err(1, NULL);

	if ((pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL)) == NULL)
		errx(1, "EVP_PKEY_CTX_new_id");
	if (EVP_PKEY_keygen_init(pkey_ctx) != 1)
		errx(1, "EVP_PKEY_keygen_init");
	if (EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_ctx, 2048) <= 0)
		errx(1, "EVP_PKEY_CTX_set_rsa_keygen_bits");
	if (EVP_PKEY_keygen(pkey_ctx, &pkey) <= 0)
		errx(1, "EVP_PKEY_keygen");
	if (X509_set_pubkey(x, pkey) != 1)
		errx(1, "X509_set_pubkey");

	x509_set_integer("X509_set_serialNumber", X509_set_serialNumber, &x, 1);
	x509_set_time("X509_set_notBefore", X509_set_notBefore, &x, 0);
	x509_set_time("X509_set_notAfter", X509_set_notAfter, &x, 60);
	x509_set_name("X509_set_issuer_name", X509_set_issuer_name, &x, "NL");
	x509_set_name("X509_set_subject_name", X509_set_subject_name, &x, "BE");

	/* one time creation of the original DER */
	if (!X509_sign(x, pkey, EVP_sha256()))
		errx(1, "X509_sign");
	if ((dersz = i2d_X509(x, &der)) <= 0)
		errx(1, "i2d_X509");

	/* test X509_set_version */
	x509_setup(&der, &der2, &a, dersz, &der2sz);
	if (!X509_set_version(a, 2))
		errx(1, "X509_set_version");
	failed |= x509_compare("X509_set_version", a, der2, der2sz);
	x509_cleanup(&a, &der2);

	/* test X509_set_serialNumber */
	x509_setup(&der, &der2, &a, dersz, &der2sz);
	x509_set_integer("X509_set_serialNumber", X509_set_serialNumber, &a, 2);
	failed |= x509_compare("X509_set_serialNumber", a, der2, der2sz);
	x509_cleanup(&a, &der2);

	/* test X509_set_issuer_name */
	x509_setup(&der, &der2, &a, dersz, &der2sz);
	x509_set_name("X509_set_issuer_name", X509_set_issuer_name, &a, "DE");
	failed |= x509_compare("X509_set_issuer_name", a, der2, der2sz);
	x509_cleanup(&a, &der2);

	/* test X509_set_subject_name */
	x509_setup(&der, &der2, &a, dersz, &der2sz);
	x509_set_name("X509_set_subject_name", X509_set_subject_name, &a, "FR");
	failed |= x509_compare("X509_set_subject_name", a, der2, der2sz);
	x509_cleanup(&a, &der2);

	/* test X509_set_notBefore */
	x509_setup(&der, &der2, &a, dersz, &der2sz);
	x509_set_time("X509_set_notBefore", X509_set_notBefore, &a, 120);
	failed |= x509_compare("X509_set_notBefore", a, der2, der2sz);
	x509_cleanup(&a, &der2);

	/* test X509_set_notAfter */
	x509_setup(&der, &der2, &a, dersz, &der2sz);
	x509_set_time("X509_set_notAfter", X509_set_notAfter, &a, 180);
	failed |= x509_compare("X509_set_notAfter", a, der2, der2sz);
	x509_cleanup(&a, &der2);

	/* test X509_set_pubkey */
	x509_setup(&der, &der2, &a, dersz, &der2sz);
	if (EVP_PKEY_keygen(pkey_ctx, &pkey) <= 0)
		errx(1, "EVP_PKEY_keygen");
	if (X509_set_pubkey(a, pkey) != 1)
		errx(1, "X509_set_pubkey");
	failed |= x509_compare("X509_set_pubkey", a, der2, der2sz);
	x509_cleanup(&a, &der2);

	EVP_PKEY_CTX_free(pkey_ctx);
	EVP_PKEY_free(pkey);
	X509_free(x);
	free(der);

	return failed;
}

static int
test_x509_crl_setters(void)
{
	EVP_PKEY *pkey = NULL;
	EVP_PKEY_CTX *pkey_ctx = NULL;
	X509_CRL *ac, *xc;
	unsigned char *der = NULL, *der2 = NULL;
	long dersz, der2sz;
	int failed = 0;

	if ((xc = X509_CRL_new()) == NULL)
		err(1, NULL);

	if ((pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL)) == NULL)
		errx(1, "EVP_PKEY_CTX_new_id");
	if (EVP_PKEY_keygen_init(pkey_ctx) != 1)
		errx(1, "EVP_PKEY_keygen_init");
	if (EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_ctx, 2048) <= 0)
		errx(1, "EVP_PKEY_CTX_set_rsa_keygen_bits");
	if (EVP_PKEY_keygen(pkey_ctx, &pkey) <= 0)
		errx(1, "EVP_PKEY_keygen");

	x509_crl_set_time("X509_CRL_set_lastUpdate", X509_CRL_set_lastUpdate,
	    &xc, 0);
	x509_crl_set_time("X509_CRL_set_nextUpdate", X509_CRL_set_nextUpdate,
	    &xc, 60);
	x509_crl_set_name("X509_CRL_set_issuer_name", X509_CRL_set_issuer_name,
	    &xc, "NL");

	/* one time creation of the original DER */
	if (!X509_CRL_sign(xc, pkey, EVP_sha256()))
		errx(1, "X509_CRL_sign");
	if ((dersz = i2d_X509_CRL(xc, &der)) <= 0)
		errx(1, "i2d_X509_CRL");

	/* test X509_CRL_set_version */
	x509_crl_setup(&der, &der2, &ac, dersz, &der2sz);
	if (!X509_CRL_set_version(ac, 1))
		errx(1, "X509_CRL_set_version");
	failed |= x509_crl_compare("X509_CRL_set_version", ac, der2, der2sz);
	x509_crl_cleanup(&ac, &der2);

	/* test X509_CRL_set_issuer_name */
	x509_crl_setup(&der, &der2, &ac, dersz, &der2sz);
	x509_crl_set_name("X509_CRL_set_issuer_name", X509_CRL_set_issuer_name,
	    &ac, "DE");
	failed |= x509_crl_compare("X509_CRL_set_issuer_name", ac, der2,
	    der2sz);
	x509_crl_cleanup(&ac, &der2);

	/* test X509_CRL_set_lastUpdate */
	x509_crl_setup(&der, &der2, &ac, dersz, &der2sz);
	x509_crl_set_time("X509_CRL_set_lastUpdate", X509_CRL_set_lastUpdate,
	    &ac, 120);
	failed |= x509_crl_compare("X509_CRL_set_lastUpdate", ac, der2, der2sz);
	x509_crl_cleanup(&ac, &der2);

	/* test X509_CRL_set_nextUpdate */
	x509_crl_setup(&der, &der2, &ac, dersz, &der2sz);
	x509_crl_set_time("X509_CRL_set_nextUpdate", X509_CRL_set_nextUpdate,
	    &ac, 180);
	failed |= x509_crl_compare("X509_CRL_set_nextUpdate", ac, der2, der2sz);
	x509_crl_cleanup(&ac, &der2);

	EVP_PKEY_free(pkey);
	EVP_PKEY_CTX_free(pkey_ctx);
	X509_CRL_free(xc);
	free(der);

	return failed;
}

static void
x509_req_setup(unsigned char **der, unsigned char **der2, X509_REQ **xr,
    long dersz, long *der2sz)
{
	const unsigned char *cpder;

	cpder = *der;
	if ((*xr = d2i_X509_REQ(NULL, &cpder, dersz)) == NULL)
		errx(1, "d2i_X509");
	if ((*der2sz = i2d_X509_REQ(*xr, der2)) <= 0)
		errx(1, "i2d_X509");
}

static int
x509_req_compare(char *f, X509_REQ *xr, const unsigned char *der, long dersz)
{
	unsigned char *der_test = NULL;
	long der_testsz;
	int rc = 0;

	if ((der_testsz = i2d_X509_REQ(xr, &der_test)) <= 0)
		errx(1, "i2d_X509_REQ");

	if (dersz == der_testsz) {
		if (memcmp(der, der_test, dersz) == 0) {
			warnx("%s() stale version of encoding after i2d", f);
			rc = 1;
		} else
			warnx("%s() OK", f);
	} else
		warnx("%s() OK", f);

	free(der_test);
	return rc;
}

static void
x509_req_cleanup(X509_REQ **xr, unsigned char **der)
{
	X509_REQ_free(*xr);
	*xr = NULL;
	free(*der);
	*der = NULL;
}

static int
test_x509_req_setters(void)
{
	EVP_PKEY *pkey = NULL;
	EVP_PKEY_CTX *pkey_ctx = NULL;
	X509_REQ *ar = NULL, *xr = NULL;
	unsigned char *der = NULL, *der2 = NULL;
	X509_NAME *xn;
	ASN1_OCTET_STRING *aos;
	X509_EXTENSION *xe;
	STACK_OF(X509_EXTENSION) *exts = NULL;
	ASN1_OBJECT *coid;
	X509_ATTRIBUTE *xa;
	long dersz, der2sz;
	int failed = 0;

	if ((xr = X509_REQ_new()) == NULL)
		err(1, NULL);

	if (!X509_REQ_set_version(xr, 0))
		errx(1, "X509_REQ_set_version");

	if ((xn = X509_NAME_new()) == NULL)
		err(1, NULL);
	if (!X509_NAME_add_entry_by_txt(xn, "C", MBSTRING_ASC, "NL", -1, -1, 0))
		errx(1, "X509_NAME_add_entry_by_txt");
	if (!X509_REQ_set_subject_name(xr, xn))
		errx(1, "X509_REQ_set_subject_name");

	if ((pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL)) == NULL)
		errx(1, "EVP_PKEY_CTX_new_id");
	if (EVP_PKEY_keygen_init(pkey_ctx) != 1)
		errx(1, "EVP_PKEY_keygen_init");
	if (EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_ctx, 2048) <= 0)
		errx(1, "EVP_PKEY_CTX_set_rsa_keygen_bits");
	if (EVP_PKEY_keygen(pkey_ctx, &pkey) <= 0)
		errx(1, "EVP_PKEY_keygen");
	if (!X509_REQ_set_pubkey(xr, pkey))
		errx(1, "X509_REQ_set_pubkey");

	if (!X509_REQ_sign(xr, pkey, EVP_sha256()))
		errx(1, "X509_REQ_sign");
	if ((dersz = i2d_X509_REQ(xr, &der)) <= 0)
		errx(1, "i2d_X509_REQ");

	/* test X509_REQ_add_extensions */
	x509_req_setup(&der, &der2, &ar, dersz, &der2sz);
	if ((aos = ASN1_OCTET_STRING_new()) == NULL)
		err(1, NULL);
	ASN1_OCTET_STRING_set(aos, (unsigned char *)"DNS: test.nl",
	    strlen("DNS: test.nl"));
	if ((xe = X509_EXTENSION_new()) == NULL)
		err(1, NULL);
	if (!X509_EXTENSION_create_by_NID(&xe, NID_subject_alt_name, 0, aos))
		errx(1, "X509_EXTENSION_create_by_NID");
	if ((exts = sk_X509_EXTENSION_new_null()) == NULL)
		errx(1, "sk_X509_EXTENSION_new_null");
	sk_X509_EXTENSION_push(exts, xe);
	if (!X509_REQ_add_extensions(ar, exts))
		errx(1, "X509_REQ_add_extensions");
	failed |= x509_req_compare("X509_REQ_add_extensions", ar, der2, der2sz);
	x509_req_cleanup(&ar, &der2);

	/* test X509_REQ_add1_attr */
	x509_req_setup(&der, &der2, &ar, dersz, &der2sz);
	if ((coid = OBJ_nid2obj(NID_pkcs7_data)) == NULL)
		errx(1, "OBJ_nid2obj");
	if ((xa = X509_ATTRIBUTE_create(NID_pkcs9_contentType, V_ASN1_OBJECT,
	    coid)) == NULL)
		errx(1, "X509_ATTRIBUTE_create");
	if (!X509_REQ_add1_attr(ar, xa))
		errx(1, "X509_REQ_add1_attr");
	failed |= x509_req_compare("X509_REQ_add1_attr", ar, der2, der2sz);
	x509_req_cleanup(&ar, &der2);

	ASN1_OBJECT_free(coid);
	X509_NAME_free(xn);
	ASN1_OCTET_STRING_free(aos);
	sk_X509_EXTENSION_pop_free(exts, X509_EXTENSION_free);
	X509_ATTRIBUTE_free(xa);
	EVP_PKEY_free(pkey);
	EVP_PKEY_CTX_free(pkey_ctx);
	X509_REQ_free(xr);
	free(der);

	return failed;
}

static const struct testcase {
	char *data;
	int len;
	int len_to_pass;
	int encode_type;
	int expected_result;
	char *expected_string;
} testCases[] = {
	/* should work */
	{"fozzie", 6, 80, MBSTRING_ASC, 6, "fozzie"},
	/* should work */
	{"fozzie", 6, -1, MBSTRING_ASC, 6, ""},
	/* should fail, truncation */
	{"muppet", 6, 5, MBSTRING_ASC, -1, ""},
	/* should fail, contains 0 byte */
	{"g\0nzo", 5, 80, MBSTRING_ASC, -1, ""},
	/* should fail, can't encode as utf-8 */
	{"\x30\x00", 2, 80, V_ASN1_SEQUENCE, -1, ""},
};

#define NUM_TEST_CASES (sizeof(testCases) / sizeof(testCases[0]))

static int
test_x509_name_get(void)
{
	int failed = 0;
	size_t i;

	for (i = 0; i < NUM_TEST_CASES; i++) {
		const struct testcase *test = testCases + i;
		X509_NAME_ENTRY *entry = NULL;
		X509_NAME *name = NULL;
		char textbuf[80];
		int result;

		textbuf[0] = '\0';
		if ((name = X509_NAME_new()) == NULL)
			err(1, "X509_NAME_new");
		if ((entry = X509_NAME_ENTRY_new()) == NULL)
			err(1, "X509_NAME_ENTRY_new");
		if (!X509_NAME_ENTRY_set_object(entry,
		    OBJ_nid2obj(NID_commonName)))
			err(1, "X509_NAME_ENTRY_set_object");
		if (!X509_NAME_ENTRY_set_data(entry, test->encode_type,
		    test->data, test->len))
			err(1, "X509_NAME_ENTRY_set_data");
		if (!X509_NAME_add_entry(name, entry, -1, 0))
			err(1, "X509_NAME_add_entry");
		if (test->len_to_pass == -1)
			result = X509_NAME_get_text_by_NID(name, NID_commonName,
			    NULL, 0);
		else
			result = X509_NAME_get_text_by_NID(name, NID_commonName,
			    textbuf, test->len_to_pass);
		if (result != test->expected_result) {
			fprintf(stderr,
			    "Test %zu X509_GET_text_by_NID returned %d,"
			    "expected %d\n", i, result, test->expected_result);
			failed++;
		}
		if (result != -1 &&
		    strcmp(test->expected_string, textbuf) != 0) {
			fprintf(stderr,
			    "Test %zu, X509_GET_text_by_NID returned bytes do"
			    "not match \n", i);
			failed++;
		}
		X509_NAME_ENTRY_free(entry);
		X509_NAME_free(name);
	}
	return failed;
}

int
main(void)
{
	int failed = 0;

	failed |= test_x509_setters();
	/* failed |= */ test_x509_crl_setters();
	/* failed |= */ test_x509_req_setters();
	failed |= test_x509_name_get();

	OPENSSL_cleanup();

	return failed;
}
