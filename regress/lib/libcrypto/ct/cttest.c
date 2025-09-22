/* $OpenBSD: cttest.c,v 1.8 2023/04/14 14:36:13 tb Exp $ */
/*
 * Copyright (c) 2021 Joel Sing <jsing@openbsd.org>
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

#include <err.h>
#include <string.h>

#include <openssl/ct.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#ifndef CTPATH
#define CTPATH "."
#endif

char *test_ctlog_conf_file;
char *test_cert_file;
char *test_issuer_file;

const int debug = 0;

const uint8_t scts_asn1[] = {
	0x04, 0x81, 0xf2, 0x00, 0xf0, 0x00, 0x77, 0x00,
	0x29, 0x79, 0xbe, 0xf0, 0x9e, 0x39, 0x39, 0x21,
	0xf0, 0x56, 0x73, 0x9f, 0x63, 0xa5, 0x77, 0xe5,
	0xbe, 0x57, 0x7d, 0x9c, 0x60, 0x0a, 0xf8, 0xf9,
	0x4d, 0x5d, 0x26, 0x5c, 0x25, 0x5d, 0xc7, 0x84,
	0x00, 0x00, 0x01, 0x7d, 0x39, 0x51, 0x1f, 0x6f,
	0x00, 0x00, 0x04, 0x03, 0x00, 0x48, 0x30, 0x46,
	0x02, 0x21, 0x00, 0x93, 0xed, 0x3a, 0x65, 0x98,
	0x9a, 0x85, 0xf0, 0x3b, 0x3c, 0x26, 0xf7, 0x52,
	0x94, 0xd7, 0x92, 0x48, 0xc2, 0xc0, 0x64, 0xcb,
	0x01, 0xf5, 0xec, 0xf7, 0x6d, 0x41, 0xe0, 0xbd,
	0x28, 0x56, 0xad, 0x02, 0x21, 0x00, 0xc2, 0x4f,
	0x92, 0xfb, 0xa0, 0xbb, 0xef, 0x55, 0x67, 0x80,
	0x06, 0x10, 0x07, 0xe7, 0xb9, 0xb1, 0x96, 0xa7,
	0xa9, 0x8b, 0xb2, 0xcb, 0xd3, 0x9c, 0x4e, 0x02,
	0xe8, 0xdb, 0x24, 0x65, 0x1e, 0xc8, 0x00, 0x75,
	0x00, 0x6f, 0x53, 0x76, 0xac, 0x31, 0xf0, 0x31,
	0x19, 0xd8, 0x99, 0x00, 0xa4, 0x51, 0x15, 0xff,
	0x77, 0x15, 0x1c, 0x11, 0xd9, 0x02, 0xc1, 0x00,
	0x29, 0x06, 0x8d, 0xb2, 0x08, 0x9a, 0x37, 0xd9,
	0x13, 0x00, 0x00, 0x01, 0x7d, 0x39, 0x51, 0x20,
	0x3b, 0x00, 0x00, 0x04, 0x03, 0x00, 0x46, 0x30,
	0x44, 0x02, 0x20, 0x26, 0xc9, 0x12, 0x28, 0x70,
	0x2d, 0x15, 0x05, 0xa7, 0xa2, 0xea, 0x12, 0x1a,
	0xff, 0x39, 0x36, 0x5f, 0x93, 0xdf, 0x83, 0x36,
	0x5f, 0xed, 0x07, 0x38, 0xb8, 0x0a, 0x40, 0xe1,
	0x8d, 0xb9, 0xfa, 0x02, 0x20, 0x61, 0xae, 0x2b,
	0x86, 0xbd, 0x8e, 0x86, 0x65, 0x2b, 0xfb, 0x63,
	0xe1, 0xda, 0x77, 0xb3, 0xf3, 0xc5, 0x2a, 0x32,
	0xb8, 0x23, 0x1e, 0x7e, 0xfa, 0x7d, 0x83, 0xa5,
	0x49, 0x00, 0xc4, 0x57, 0xb8,
};

const char *sct_log_id1_base64 = "KXm+8J45OSHwVnOfY6V35b5XfZxgCvj5TV0mXCVdx4Q=";

const uint8_t sct_signature1[] = {
	0x30, 0x46, 0x02, 0x21, 0x00, 0x93, 0xed, 0x3a,
	0x65, 0x98, 0x9a, 0x85, 0xf0, 0x3b, 0x3c, 0x26,
	0xf7, 0x52, 0x94, 0xd7, 0x92, 0x48, 0xc2, 0xc0,
	0x64, 0xcb, 0x01, 0xf5, 0xec, 0xf7, 0x6d, 0x41,
	0xe0, 0xbd, 0x28, 0x56, 0xad, 0x02, 0x21, 0x00,
	0xc2, 0x4f, 0x92, 0xfb, 0xa0, 0xbb, 0xef, 0x55,
	0x67, 0x80, 0x06, 0x10, 0x07, 0xe7, 0xb9, 0xb1,
	0x96, 0xa7, 0xa9, 0x8b, 0xb2, 0xcb, 0xd3, 0x9c,
	0x4e, 0x02, 0xe8, 0xdb, 0x24, 0x65, 0x1e, 0xc8
};

const char *sct_signature1_base64 =
    "BAMASDBGAiEAk+06ZZiahfA7PCb3UpTXkkjCwGTLAfXs921B4L0oVq0CIQDCT5L7oLvvVWeABh"
    "AH57mxlqepi7LL05xOAujbJGUeyA==";

const char *sct_log_id2_base64 = "b1N2rDHwMRnYmQCkURX/dxUcEdkCwQApBo2yCJo32RM=";

const uint8_t sct_signature2[] = {
	0x30, 0x44, 0x02, 0x20, 0x26, 0xc9, 0x12, 0x28,
	0x70, 0x2d, 0x15, 0x05, 0xa7, 0xa2, 0xea, 0x12,
	0x1a, 0xff, 0x39, 0x36, 0x5f, 0x93, 0xdf, 0x83,
	0x36, 0x5f, 0xed, 0x07, 0x38, 0xb8, 0x0a, 0x40,
	0xe1, 0x8d, 0xb9, 0xfa, 0x02, 0x20, 0x61, 0xae,
	0x2b, 0x86, 0xbd, 0x8e, 0x86, 0x65, 0x2b, 0xfb,
	0x63, 0xe1, 0xda, 0x77, 0xb3, 0xf3, 0xc5, 0x2a,
	0x32, 0xb8, 0x23, 0x1e, 0x7e, 0xfa, 0x7d, 0x83,
	0xa5, 0x49, 0x00, 0xc4, 0x57, 0xb8
};

const char *sct_signature2_base64 =
    "BAMARjBEAiAmyRIocC0VBaei6hIa/zk2X5PfgzZf7Qc4uApA4Y25+gIgYa4rhr2OhmUr+2Ph2n"
    "ez88UqMrgjHn76fYOlSQDEV7g=";

struct sct_data {
	uint8_t version;
	uint8_t log_id[32];
	uint64_t timestamp;
	size_t extensions_len;
	int signature_nid;
	const uint8_t *signature;
	size_t signature_len;
};

const struct sct_data sct_test_data[] = {
	{
		.version = 0,
		.log_id = {
			0x29, 0x79, 0xbe, 0xf0, 0x9e, 0x39, 0x39, 0x21,
			0xf0, 0x56, 0x73, 0x9f, 0x63, 0xa5, 0x77, 0xe5,
			0xbe, 0x57, 0x7d, 0x9c, 0x60, 0x0a, 0xf8, 0xf9,
			0x4d, 0x5d, 0x26, 0x5c, 0x25, 0x5d, 0xc7, 0x84,
		},
		.timestamp = 1637344157551LL,
		.extensions_len = 0,
		.signature_nid = NID_ecdsa_with_SHA256,
		.signature = sct_signature1,
		.signature_len = sizeof(sct_signature1),
	},
	{
		.version = 0,
		.log_id = {
			0x6f, 0x53, 0x76, 0xac, 0x31, 0xf0, 0x31, 0x19,
			0xd8, 0x99, 0x00, 0xa4, 0x51, 0x15, 0xff, 0x77,
			0x15, 0x1c, 0x11, 0xd9, 0x02, 0xc1, 0x00, 0x29,
			0x06, 0x8d, 0xb2, 0x08, 0x9a, 0x37, 0xd9, 0x13
		},
		.timestamp = 1637344157755LL,
		.extensions_len = 0,
		.signature_nid = NID_ecdsa_with_SHA256,
		.signature = sct_signature2,
		.signature_len = sizeof(sct_signature2),
	},
};

#define N_SCT_TEST_DATA (sizeof(sct_test_data) / sizeof(*sct_test_data))

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	if (len % 8)
		fprintf(stderr, "\n");
}

static void
cert_from_file(const char *filename, X509 **cert)
{
	BIO *bio = NULL;
	X509 *x;

	if ((bio = BIO_new_file(filename, "r")) == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "failed to create bio");
	}
	if ((x = PEM_read_bio_X509(bio, NULL, NULL, NULL)) == NULL)
		errx(1, "failed to read PEM");

	*cert = x;

	BIO_free(bio);
}

static int
ct_compare_test_scts(STACK_OF(SCT) *scts)
{
	const struct sct_data *sdt;
	BIO *bio_err = NULL;
	SCT *sct;
	uint8_t *data;
	size_t len;
	int i;
	int ret = 0;

	bio_err = BIO_new_fp(stderr, BIO_NOCLOSE);

	if (sk_SCT_num(scts) != N_SCT_TEST_DATA) {
		fprintf(stderr, "FAIL: got %d SCTS, want %zu\n",
		    sk_SCT_num(scts), N_SCT_TEST_DATA);
		goto failure;
	}

	for (i = 0; i < sk_SCT_num(scts); i++) {
		sct = sk_SCT_value(scts, i);
		sdt = &sct_test_data[i];

		if (debug > 0) {
			SCT_print(sct, bio_err, 0, NULL);
			BIO_printf(bio_err, "\n");
		}

		if (SCT_get_version(sct) != sdt->version) {
			fprintf(stderr, "FAIL: SCT %d - got version %u, "
			    "want %u\n", i, SCT_get_version(sct), sdt->version);
			goto failure;
		}
		len = SCT_get0_log_id(sct, &data);
		if (len != sizeof(sdt->log_id)) {
			fprintf(stderr, "FAIL: SCT %d - got version %u, "
			    "want %u\n", i, SCT_get_version(sct), sdt->version);
			goto failure;
		}
		if (memcmp(data, sdt->log_id, len) != 0) {
			fprintf(stderr, "FAIL: SCT %d - log ID differs\n", i);
			fprintf(stderr, "Got:\n");
			hexdump(data, len);
			fprintf(stderr, "Want:\n");
			hexdump(sdt->log_id, sizeof(sdt->log_id));
			goto failure;
		}
		if (SCT_get_timestamp(sct) != sdt->timestamp) {
			fprintf(stderr, "FAIL: SCT %d - got timestamp %llu, "
			    "want %llu\n", i,
			    (unsigned long long)SCT_get_timestamp(sct),
			    (unsigned long long)sdt->timestamp);
			goto failure;
		}
		if (SCT_get_signature_nid(sct) != sdt->signature_nid) {
			fprintf(stderr, "FAIL: SCT %d - got signature_nid %d, "
			    "want %d\n", i, SCT_get_signature_nid(sct),
			    sdt->signature_nid);
			goto failure;
		}
		len = SCT_get0_extensions(sct, &data);
		if (len != sdt->extensions_len) {
			fprintf(stderr, "FAIL: SCT %d - got extensions with "
			    "length %zu, want %zu\n", i, len,
			    sdt->extensions_len);
			goto failure;
		}
		len = SCT_get0_signature(sct, &data);
		if (len != sdt->signature_len) {
			fprintf(stderr, "FAIL: SCT %d - got signature with "
			    "length %zu, want %zu\n", i, len,
			    sdt->signature_len);
			goto failure;
		}
		if (memcmp(data, sdt->signature, len) != 0) {
			fprintf(stderr, "FAIL: SCT %d - signature differs\n",
			    i);
			fprintf(stderr, "Got:\n");
			hexdump(data, len);
			fprintf(stderr, "Want:\n");
			hexdump(sdt->signature, sdt->signature_len);
			goto failure;
		}
	}

	ret = 1;

 failure:
	BIO_free(bio_err);

	return ret;
}

static int
ct_cert_test(void)
{
	X509 *cert = NULL;
	X509_EXTENSION *ext;
	STACK_OF(SCT) *scts = NULL;
	int idx;
	int failed = 1;

	cert_from_file(test_cert_file, &cert);

	if ((idx = X509_get_ext_by_NID(cert, NID_ct_precert_scts, -1)) == -1) {
		fprintf(stderr, "FAIL: failed to find SCTs\n");
		goto failure;
	}
	if ((ext = X509_get_ext(cert, idx)) == NULL) {
		fprintf(stderr, "FAIL: failed to get SCT extension\n");
		goto failure;
	}
	if ((scts = X509V3_EXT_d2i(ext)) == NULL) {
		fprintf(stderr, "FAIL: failed to decode SCTs\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	if (!ct_compare_test_scts(scts))
		goto failure;

	failed = 0;

 failure:
	SCT_LIST_free(scts);
	X509_free(cert);

	return failed;
}

static int
ct_sct_test(void)
{
	STACK_OF(SCT) *scts = NULL;
	const uint8_t *p;
	uint8_t *data = NULL;
	int len;
	int failed = 1;

	p = scts_asn1;
	if ((scts = d2i_SCT_LIST(NULL, &p, sizeof(scts_asn1))) == NULL) {
		fprintf(stderr, "FAIL: failed to decode SCTS from ASN.1\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	if (!ct_compare_test_scts(scts))
		goto failure;

	data = NULL;
	if ((len = i2d_SCT_LIST(scts, &data)) <= 0) {
		fprintf(stderr, "FAIL: failed to encode SCTS to ASN.1\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}
	if (len != sizeof(scts_asn1)) {
		fprintf(stderr, "FAIL: ASN.1 length differs - got %d, want "
		    "%zu\n", len, sizeof(scts_asn1));
		goto failure;
	}
	if (memcmp(data, scts_asn1, len) != 0) {
		fprintf(stderr, "FAIL: ASN.1 for SCTS differs\n");
		fprintf(stderr, "Got:\n");
		hexdump(data, len);
		fprintf(stderr, "Want:\n");
		hexdump(scts_asn1, sizeof(scts_asn1));
		goto failure;
	}

	failed = 0;

 failure:
	SCT_LIST_free(scts);
	free(data);

	return failed;
}

static int
ct_sct_base64_test(void)
{
	SCT *sct1 = NULL, *sct2 = NULL;
	STACK_OF(SCT) *scts = NULL;
	int failed = 1;

	if ((sct1 = SCT_new_from_base64(SCT_VERSION_V1, sct_log_id1_base64,
	    CT_LOG_ENTRY_TYPE_X509, 1637344157551LL, "",
	    sct_signature1_base64)) == NULL) {
		fprintf(stderr, "FAIL: SCT_new_from_base64() failed\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}
	if ((sct2 = SCT_new_from_base64(SCT_VERSION_V1, sct_log_id2_base64,
	    CT_LOG_ENTRY_TYPE_X509, 1637344157755LL, "",
	    sct_signature2_base64)) == NULL) {
		fprintf(stderr, "FAIL: SCT_new_from_base64() failed\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}
	if ((scts = sk_SCT_new_null()) == NULL)
		goto failure;
	if (!sk_SCT_push(scts, sct1))
		goto failure;
	sct1 = NULL;
	if (!sk_SCT_push(scts, sct2))
		goto failure;
	sct2 = NULL;

	if (!ct_compare_test_scts(scts))
		goto failure;

	failed = 0;

 failure:
	SCT_LIST_free(scts);
	SCT_free(sct1);
	SCT_free(sct2);

	return failed;
}

static int
ct_sct_verify_test(void)
{
	STACK_OF(SCT) *scts = NULL;
	CT_POLICY_EVAL_CTX *ct_policy = NULL;
	CTLOG_STORE *ctlog_store = NULL;
	X509 *cert = NULL, *issuer = NULL;
	const uint8_t *p;
	SCT *sct;
	int failed = 1;

	cert_from_file(test_cert_file, &cert);
	cert_from_file(test_issuer_file, &issuer);

	if ((ctlog_store = CTLOG_STORE_new()) == NULL)
		goto failure;
	if (!CTLOG_STORE_load_file(ctlog_store, test_ctlog_conf_file))
		goto failure;

	if ((ct_policy = CT_POLICY_EVAL_CTX_new()) == NULL)
		goto failure;

	CT_POLICY_EVAL_CTX_set_shared_CTLOG_STORE(ct_policy, ctlog_store);
	CT_POLICY_EVAL_CTX_set_time(ct_policy, 1641393117000LL);

	if (!CT_POLICY_EVAL_CTX_set1_cert(ct_policy, cert))
		goto failure;
	if (!CT_POLICY_EVAL_CTX_set1_issuer(ct_policy, issuer))
		goto failure;

	p = scts_asn1;
	if ((scts = d2i_SCT_LIST(NULL, &p, sizeof(scts_asn1))) == NULL) {
		fprintf(stderr, "FAIL: failed to decode SCTS from ASN.1\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}
	sct = sk_SCT_value(scts, 0);

	if (!SCT_set_log_entry_type(sct, CT_LOG_ENTRY_TYPE_PRECERT))
		goto failure;
	if (!SCT_validate(sct, ct_policy)) {
		fprintf(stderr, "FAIL: SCT_validate failed\n");
		ERR_print_errors_fp(stderr);
		goto failure;
	}

	failed = 0;

 failure:
	CT_POLICY_EVAL_CTX_free(ct_policy);
	CTLOG_STORE_free(ctlog_store);
	X509_free(cert);
	X509_free(issuer);
	SCT_LIST_free(scts);

	return failed;
}

int
main(int argc, char **argv)
{
	const char *ctpath = CTPATH;
	int failed = 0;

	if (argc > 2) {
		fprintf(stderr, "usage %s [ctpath]\n", argv[0]);
		exit(1);
	}
	if (argc == 2)
		ctpath = argv[1];

	if (asprintf(&test_cert_file, "%s/%s", ctpath,
	    "libressl.org.crt") == -1)
		errx(1, "asprintf test_cert_file");
	if (asprintf(&test_issuer_file, "%s/%s", ctpath,
	    "letsencrypt-r3.crt") == -1)
		errx(1, "asprintf test_issuer_file");
	if (asprintf(&test_ctlog_conf_file, "%s/%s", ctpath,
	    "ctlog.conf") == -1)
		errx(1, "asprintf test_ctlog_conf_file");

	failed |= ct_cert_test();
	failed |= ct_sct_test();
	failed |= ct_sct_base64_test();
	failed |= ct_sct_verify_test();

	free(test_cert_file);
	free(test_issuer_file);
	free(test_ctlog_conf_file);

	return (failed);
}
