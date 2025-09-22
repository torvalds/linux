/* $OpenBSD: keypairtest.c,v 1.7 2024/03/20 10:38:05 jsing Exp $ */
/*
 * Copyright (c) 2018 Joel Sing <jsing@openbsd.org>
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

#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <openssl/x509.h>

#include <tls.h>
#include <tls_internal.h>

#define PUBKEY_HASH \
    "SHA256:f03c535d374614e7356c0a4e6fd37fe94297b60ed86212adcba40e8e0b07bc9f"

char *cert_file, *key_file, *ocsp_staple_file;

static void
load_file(const char *filename, const uint8_t **data, size_t *data_len)
{
	struct stat sb;
	uint8_t *buf;
	size_t len;
	ssize_t n;
	int fd;

	if ((fd = open(filename, O_RDONLY)) == -1)
		err(1, "failed to open '%s'", filename);
	if ((fstat(fd, &sb)) == -1)
		err(1, "failed to stat '%s'", filename);
	if (sb.st_size < 0)
		err(1, "file size invalid for '%s'", filename);
	len = (size_t)sb.st_size;
	if ((buf = malloc(len)) == NULL)
		err(1, "out of memory");
	n = read(fd, buf, len);
	if (n < 0 || (size_t)n != len)
		err(1, "failed to read '%s'", filename);
	close(fd);

	*data = buf;
	*data_len = len;
}

static int
compare_mem(char *label, const uint8_t *data1, size_t data1_len,
    const uint8_t *data2, size_t data2_len)
{
	if (data1_len != data2_len) {
		fprintf(stderr, "FAIL: %s length mismatch (%zu != %zu)\n",
		    label, data1_len, data2_len);
		return -1;
	}
	if (data1 == data2) {
		fprintf(stderr, "FAIL: %s comparing same memory (%p == %p)\n",
		    label, data1, data2);
		return -1;
	}
	if (memcmp(data1, data2, data1_len) != 0) {
		fprintf(stderr, "FAIL: %s data mismatch\n", label);
		return -1;
	}
	return 0;
}

static int
do_keypair_tests(void)
{
	size_t cert_len, key_len, ocsp_staple_len;
	const uint8_t *cert, *key, *ocsp_staple;
	X509 *x509_cert = NULL;
	struct tls_keypair *kp;
	struct tls_error err;
	int failed = 1;

	load_file(cert_file, &cert, &cert_len);
	load_file(key_file, &key, &key_len);
	load_file(ocsp_staple_file, &ocsp_staple, &ocsp_staple_len);

	if ((kp = tls_keypair_new()) == NULL) {
		fprintf(stderr, "FAIL: failed to create keypair\n");
		goto done;
	}

	if (tls_keypair_set_cert_file(kp, &err, cert_file) == -1) {
		fprintf(stderr, "FAIL: failed to load cert file: %s\n",
		    err.msg);
		goto done;
	}
	if (tls_keypair_set_key_file(kp, &err, key_file) == -1) {
		fprintf(stderr, "FAIL: failed to load key file: %s\n", err.msg);
		goto done;
	}
	if (tls_keypair_set_ocsp_staple_file(kp, &err, ocsp_staple_file) == -1) {
		fprintf(stderr, "FAIL: failed to load ocsp staple file: %s\n",
		    err.msg);
		goto done;
	}

	if (compare_mem("certificate", cert, cert_len, kp->cert_mem,
	    kp->cert_len) == -1)
		goto done;
	if (compare_mem("key", key, key_len, kp->key_mem, kp->cert_len) == -1)
		goto done;
	if (compare_mem("ocsp staple", ocsp_staple, ocsp_staple_len,
	    kp->ocsp_staple, kp->ocsp_staple_len) == -1)
		goto done;
	if (strcmp(kp->pubkey_hash, PUBKEY_HASH) != 0) {
		fprintf(stderr, "FAIL: got pubkey hash '%s', want '%s'",
		    kp->pubkey_hash, PUBKEY_HASH);
		goto done;
	}

	tls_keypair_clear_key(kp);

	if (kp->key_mem != NULL || kp->key_len != 0) {
		fprintf(stderr, "FAIL: key not cleared (mem %p, len %zu)",
		    kp->key_mem, kp->key_len);
		goto done;
	}

	if (tls_keypair_set_cert_mem(kp, &err, cert, cert_len) == -1) {
		fprintf(stderr, "FAIL: failed to load cert: %s\n", err.msg);
		goto done;
	}
	if (tls_keypair_set_key_mem(kp, &err, key, key_len) == -1) {
		fprintf(stderr, "FAIL: failed to load key: %s\n", err.msg);
		goto done;
	}
	if (tls_keypair_set_ocsp_staple_mem(kp, &err, ocsp_staple,
	    ocsp_staple_len) == -1) {
		fprintf(stderr, "FAIL: failed to load ocsp staple: %s\n", err.msg);
		goto done;
	}
	if (compare_mem("certificate", cert, cert_len, kp->cert_mem,
	    kp->cert_len) == -1)
		goto done;
	if (compare_mem("key", key, key_len, kp->key_mem, kp->cert_len) == -1)
		goto done;
	if (compare_mem("ocsp staple", ocsp_staple, ocsp_staple_len,
	    kp->ocsp_staple, kp->ocsp_staple_len) == -1)
		goto done;
	if (strcmp(kp->pubkey_hash, PUBKEY_HASH) != 0) {
		fprintf(stderr, "FAIL: got pubkey hash '%s', want '%s'",
		    kp->pubkey_hash, PUBKEY_HASH);
		goto done;
	}

	if (tls_keypair_load_cert(kp, &err, &x509_cert) == -1) {
		fprintf(stderr, "FAIL: failed to load X509 certificate: %s\n",
		    err.msg);
		goto done;
	}

	tls_keypair_clear_key(kp);

	if (kp->key_mem != NULL || kp->key_len != 0) {
		fprintf(stderr, "FAIL: key not cleared (mem %p, len %zu)",
		    kp->key_mem, kp->key_len);
		goto done;
	}

	failed = 0;

 done:
	tls_keypair_free(kp);
	X509_free(x509_cert);
	free((uint8_t *)cert);
	free((uint8_t *)key);
	free((uint8_t *)ocsp_staple);

	return (failed);
}

int
main(int argc, char **argv)
{
	int failure = 0;

	if (argc != 4) {
		fprintf(stderr, "usage: %s ocspstaplefile certfile keyfile\n",
		    argv[0]);
		return (1);
	}

	ocsp_staple_file = argv[1];
	cert_file = argv[2];
	key_file = argv[3];

	failure |= do_keypair_tests();

	return (failure);
}
