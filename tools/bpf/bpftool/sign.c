// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/*
 * Copyright (C) 2025 Google LLC.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <err.h>
#include <openssl/opensslv.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/cms.h>
#include <linux/keyctl.h>
#include <errno.h>

#include <bpf/skel_internal.h>

#include "main.h"

#define OPEN_SSL_ERR_BUF_LEN 256

static void display_openssl_errors(int l)
{
	char buf[OPEN_SSL_ERR_BUF_LEN];
	const char *file;
	const char *data;
	unsigned long e;
	int flags;
	int line;

	while ((e = ERR_get_error_all(&file, &line, NULL, &data, &flags))) {
		ERR_error_string_n(e, buf, sizeof(buf));
		if (data && (flags & ERR_TXT_STRING)) {
			p_err("OpenSSL %s: %s:%d: %s", buf, file, line, data);
		} else {
			p_err("OpenSSL %s: %s:%d", buf, file, line);
		}
	}
}

#define DISPLAY_OSSL_ERR(cond)				 \
	do {						 \
		bool __cond = (cond);			 \
		if (__cond && ERR_peek_error())		 \
			display_openssl_errors(__LINE__);\
	} while (0)

static EVP_PKEY *read_private_key(const char *pkey_path)
{
	EVP_PKEY *private_key = NULL;
	BIO *b;

	b = BIO_new_file(pkey_path, "rb");
	private_key = PEM_read_bio_PrivateKey(b, NULL, NULL, NULL);
	BIO_free(b);
	DISPLAY_OSSL_ERR(!private_key);
	return private_key;
}

static X509 *read_x509(const char *x509_name)
{
	unsigned char buf[2];
	X509 *x509 = NULL;
	BIO *b;
	int n;

	b = BIO_new_file(x509_name, "rb");
	if (!b)
		goto cleanup;

	/* Look at the first two bytes of the file to determine the encoding */
	n = BIO_read(b, buf, 2);
	if (n != 2)
		goto cleanup;

	if (BIO_reset(b) != 0)
		goto cleanup;

	if (buf[0] == 0x30 && buf[1] >= 0x81 && buf[1] <= 0x84)
		/* Assume raw DER encoded X.509 */
		x509 = d2i_X509_bio(b, NULL);
	else
		/* Assume PEM encoded X.509 */
		x509 = PEM_read_bio_X509(b, NULL, NULL, NULL);

cleanup:
	BIO_free(b);
	DISPLAY_OSSL_ERR(!x509);
	return x509;
}

__u32 register_session_key(const char *key_der_path)
{
	unsigned char *der_buf = NULL;
	X509 *x509 = NULL;
	int key_id = -1;
	int der_len;

	if (!key_der_path)
		return key_id;
	x509 = read_x509(key_der_path);
	if (!x509)
		goto cleanup;
	der_len = i2d_X509(x509, &der_buf);
	if (der_len < 0)
		goto cleanup;
	key_id = syscall(__NR_add_key, "asymmetric", key_der_path, der_buf,
			     (size_t)der_len, KEY_SPEC_SESSION_KEYRING);
cleanup:
	X509_free(x509);
	OPENSSL_free(der_buf);
	DISPLAY_OSSL_ERR(key_id == -1);
	return key_id;
}

int bpftool_prog_sign(struct bpf_load_and_run_opts *opts)
{
	BIO *bd_in = NULL, *bd_out = NULL;
	EVP_PKEY *private_key = NULL;
	CMS_ContentInfo *cms = NULL;
	long actual_sig_len = 0;
	X509 *x509 = NULL;
	int err = 0;

	bd_in = BIO_new_mem_buf(opts->insns, opts->insns_sz);
	if (!bd_in) {
		err = -ENOMEM;
		goto cleanup;
	}

	private_key = read_private_key(private_key_path);
	if (!private_key) {
		err = -EINVAL;
		goto cleanup;
	}

	x509 = read_x509(cert_path);
	if (!x509) {
		err = -EINVAL;
		goto cleanup;
	}

	cms = CMS_sign(NULL, NULL, NULL, NULL,
		       CMS_NOCERTS | CMS_PARTIAL | CMS_BINARY | CMS_DETACHED |
			       CMS_STREAM);
	if (!cms) {
		err = -EINVAL;
		goto cleanup;
	}

	if (!CMS_add1_signer(cms, x509, private_key, EVP_sha256(),
			     CMS_NOCERTS | CMS_BINARY | CMS_NOSMIMECAP |
			     CMS_USE_KEYID | CMS_NOATTR)) {
		err = -EINVAL;
		goto cleanup;
	}

	if (CMS_final(cms, bd_in, NULL, CMS_NOCERTS | CMS_BINARY) != 1) {
		err = -EIO;
		goto cleanup;
	}

	EVP_Digest(opts->insns, opts->insns_sz, opts->excl_prog_hash,
		   &opts->excl_prog_hash_sz, EVP_sha256(), NULL);

		bd_out = BIO_new(BIO_s_mem());
	if (!bd_out) {
		err = -ENOMEM;
		goto cleanup;
	}

	if (!i2d_CMS_bio_stream(bd_out, cms, NULL, 0)) {
		err = -EIO;
		goto cleanup;
	}

	actual_sig_len = BIO_get_mem_data(bd_out, NULL);
	if (actual_sig_len <= 0) {
		err = -EIO;
		goto cleanup;
	}

	if ((size_t)actual_sig_len > opts->signature_sz) {
		err = -ENOSPC;
		goto cleanup;
	}

	if (BIO_read(bd_out, opts->signature, actual_sig_len) != actual_sig_len) {
		err = -EIO;
		goto cleanup;
	}

	opts->signature_sz = actual_sig_len;
cleanup:
	BIO_free(bd_out);
	CMS_ContentInfo_free(cms);
	X509_free(x509);
	EVP_PKEY_free(private_key);
	BIO_free(bd_in);
	DISPLAY_OSSL_ERR(err < 0);
	return err;
}
