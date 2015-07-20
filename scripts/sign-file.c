/* Sign a module file using the given key.
 *
 * Copyright (C) 2014 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <err.h>
#include <arpa/inet.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/err.h>
#include <openssl/engine.h>

struct module_signature {
	uint8_t		algo;		/* Public-key crypto algorithm [0] */
	uint8_t		hash;		/* Digest algorithm [0] */
	uint8_t		id_type;	/* Key identifier type [PKEY_ID_PKCS7] */
	uint8_t		signer_len;	/* Length of signer's name [0] */
	uint8_t		key_id_len;	/* Length of key identifier [0] */
	uint8_t		__pad[3];
	uint32_t	sig_len;	/* Length of signature data */
};

#define PKEY_ID_PKCS7 2

static char magic_number[] = "~Module signature appended~\n";

static __attribute__((noreturn))
void format(void)
{
	fprintf(stderr,
		"Usage: scripts/sign-file [-dp] <hash algo> <key> <x509> <module> [<dest>]\n");
	exit(2);
}

static void display_openssl_errors(int l)
{
	const char *file;
	char buf[120];
	int e, line;

	if (ERR_peek_error() == 0)
		return;
	fprintf(stderr, "At main.c:%d:\n", l);

	while ((e = ERR_get_error_line(&file, &line))) {
		ERR_error_string(e, buf);
		fprintf(stderr, "- SSL %s: %s:%d\n", buf, file, line);
	}
}

static void drain_openssl_errors(void)
{
	const char *file;
	int line;

	if (ERR_peek_error() == 0)
		return;
	while (ERR_get_error_line(&file, &line)) {}
}

#define ERR(cond, fmt, ...)				\
	do {						\
		bool __cond = (cond);			\
		display_openssl_errors(__LINE__);	\
		if (__cond) {				\
			err(1, fmt, ## __VA_ARGS__);	\
		}					\
	} while(0)

static const char *key_pass;

static int pem_pw_cb(char *buf, int len, int w, void *v)
{
	int pwlen;

	if (!key_pass)
		return -1;

	pwlen = strlen(key_pass);
	if (pwlen >= len)
		return -1;

	strcpy(buf, key_pass);

	/* If it's wrong, don't keep trying it. */
	key_pass = NULL;

	return pwlen;
}

int main(int argc, char **argv)
{
	struct module_signature sig_info = { .id_type = PKEY_ID_PKCS7 };
	char *hash_algo = NULL;
	char *private_key_name, *x509_name, *module_name, *dest_name;
	bool save_pkcs7 = false, replace_orig;
	bool sign_only = false;
	unsigned char buf[4096];
	unsigned long module_size, pkcs7_size;
	const EVP_MD *digest_algo;
	EVP_PKEY *private_key;
	PKCS7 *pkcs7;
	X509 *x509;
	BIO *b, *bd = NULL, *bm;
	int opt, n;

	OpenSSL_add_all_algorithms();
	ERR_load_crypto_strings();
	ERR_clear_error();

	key_pass = getenv("KBUILD_SIGN_PIN");

	do {
		opt = getopt(argc, argv, "dp");
		switch (opt) {
		case 'p': save_pkcs7 = true; break;
		case 'd': sign_only = true; save_pkcs7 = true; break;
		case -1: break;
		default: format();
		}
	} while (opt != -1);

	argc -= optind;
	argv += optind;
	if (argc < 4 || argc > 5)
		format();

	hash_algo = argv[0];
	private_key_name = argv[1];
	x509_name = argv[2];
	module_name = argv[3];
	if (argc == 5) {
		dest_name = argv[4];
		replace_orig = false;
	} else {
		ERR(asprintf(&dest_name, "%s.~signed~", module_name) < 0,
		    "asprintf");
		replace_orig = true;
	}

	/* Read the private key and the X.509 cert the PKCS#7 message
	 * will point to.
	 */
	if (!strncmp(private_key_name, "pkcs11:", 7)) {
		ENGINE *e;

		ENGINE_load_builtin_engines();
		drain_openssl_errors();
		e = ENGINE_by_id("pkcs11");
		ERR(!e, "Load PKCS#11 ENGINE");
		if (ENGINE_init(e))
			drain_openssl_errors();
		else
			ERR(1, "ENGINE_init");
		if (key_pass)
			ERR(!ENGINE_ctrl_cmd_string(e, "PIN", key_pass, 0), "Set PKCS#11 PIN");
		private_key = ENGINE_load_private_key(e, private_key_name, NULL,
						      NULL);
		ERR(!private_key, "%s", private_key_name);
	} else {
		b = BIO_new_file(private_key_name, "rb");
		ERR(!b, "%s", private_key_name);
		private_key = PEM_read_bio_PrivateKey(b, NULL, pem_pw_cb, NULL);
		ERR(!private_key, "%s", private_key_name);
		BIO_free(b);
	}

	b = BIO_new_file(x509_name, "rb");
	ERR(!b, "%s", x509_name);
	x509 = d2i_X509_bio(b, NULL); /* Binary encoded X.509 */
	if (!x509) {
		BIO_reset(b);
		x509 = PEM_read_bio_X509(b, NULL, NULL, NULL); /* PEM encoded X.509 */
		if (x509)
			drain_openssl_errors();
	}
	BIO_free(b);
	ERR(!x509, "%s", x509_name);

	/* Open the destination file now so that we can shovel the module data
	 * across as we read it.
	 */
	if (!sign_only) {
		bd = BIO_new_file(dest_name, "wb");
		ERR(!bd, "%s", dest_name);
	}

	/* Digest the module data. */
	OpenSSL_add_all_digests();
	display_openssl_errors(__LINE__);
	digest_algo = EVP_get_digestbyname(hash_algo);
	ERR(!digest_algo, "EVP_get_digestbyname");

	bm = BIO_new_file(module_name, "rb");
	ERR(!bm, "%s", module_name);

	/* Load the PKCS#7 message from the digest buffer. */
	pkcs7 = PKCS7_sign(NULL, NULL, NULL, NULL,
			   PKCS7_NOCERTS | PKCS7_PARTIAL | PKCS7_BINARY | PKCS7_DETACHED | PKCS7_STREAM);
	ERR(!pkcs7, "PKCS7_sign");

	ERR(!PKCS7_sign_add_signer(pkcs7, x509, private_key, digest_algo, PKCS7_NOCERTS | PKCS7_BINARY),
	    "PKCS7_sign_add_signer");
	ERR(PKCS7_final(pkcs7, bm, PKCS7_NOCERTS | PKCS7_BINARY) < 0,
	    "PKCS7_final");

	if (save_pkcs7) {
		char *pkcs7_name;

		ERR(asprintf(&pkcs7_name, "%s.pkcs7", module_name) < 0, "asprintf");
		b = BIO_new_file(pkcs7_name, "wb");
		ERR(!b, "%s", pkcs7_name);
		ERR(i2d_PKCS7_bio_stream(b, pkcs7, NULL, 0) < 0, "%s", pkcs7_name);
		BIO_free(b);
	}

	if (sign_only)
		return 0;

	/* Append the marker and the PKCS#7 message to the destination file */
	ERR(BIO_reset(bm) < 0, "%s", module_name);
	while ((n = BIO_read(bm, buf, sizeof(buf))),
	       n > 0) {
		ERR(BIO_write(bd, buf, n) < 0, "%s", dest_name);
	}
	ERR(n < 0, "%s", module_name);
	module_size = BIO_number_written(bd);

	ERR(i2d_PKCS7_bio_stream(bd, pkcs7, NULL, 0) < 0, "%s", dest_name);
	pkcs7_size = BIO_number_written(bd) - module_size;
	sig_info.sig_len = htonl(pkcs7_size);
	ERR(BIO_write(bd, &sig_info, sizeof(sig_info)) < 0, "%s", dest_name);
	ERR(BIO_write(bd, magic_number, sizeof(magic_number) - 1) < 0, "%s", dest_name);

	ERR(BIO_free(bd) < 0, "%s", dest_name);

	/* Finally, if we're signing in place, replace the original. */
	if (replace_orig)
		ERR(rename(dest_name, module_name) < 0, "%s", dest_name);

	return 0;
}
