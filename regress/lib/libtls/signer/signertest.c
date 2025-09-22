/* $OpenBSD: signertest.c,v 1.6 2023/04/14 12:41:26 tb Exp $ */
/*
 * Copyright (c) 2017, 2018, 2022 Joel Sing <jsing@openbsd.org>
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

#include <sys/socket.h>
#include <sys/stat.h>

#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <tls.h>

#include "tls_internal.h"

#ifndef CERTSDIR
#define CERTSDIR "."
#endif

const char *cert_path = CERTSDIR;
int sign_cb_count;

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}

static void
load_file(const char *filename, const uint8_t **data, size_t *data_len)
{
	char *filepath;
	struct stat sb;
	uint8_t *buf;
	size_t len;
	ssize_t n;
	int fd;

	if (asprintf(&filepath, "%s/%s", cert_path, filename) == -1)
		err(1, "asprintf");
	if ((fd = open(filepath, O_RDONLY)) == -1)
		err(1, "failed to open '%s'", filepath);
	if ((fstat(fd, &sb)) == -1)
		err(1, "failed to stat '%s'", filepath);
	if (sb.st_size < 0)
		err(1, "file size invalid for '%s'", filepath);
	len = (size_t)sb.st_size;
	if ((buf = malloc(len)) == NULL)
		err(1, "out of memory");
	n = read(fd, buf, len);
	if (n < 0 || (size_t)n != len)
		err(1, "failed to read '%s'", filepath);
	close(fd);

	*data = buf;
	*data_len = len;

	free(filepath);
}

static int
compare_mem(char *label, const uint8_t *data1, size_t data1_len,
    const uint8_t *data2, size_t data2_len)
{
	if (data1_len != data2_len) {
		fprintf(stderr, "FAIL: %s length mismatch (%zu != %zu)\n",
		    label, data1_len, data2_len);
		fprintf(stderr, "Got:\n");
		hexdump(data1, data1_len);
		fprintf(stderr, "Want:\n");
		hexdump(data2, data2_len);
		return -1;
	}
	if (data1 == data2) {
		fprintf(stderr, "FAIL: %s comparing same memory (%p == %p)\n",
		    label, data1, data2);
		return -1;
	}
	if (memcmp(data1, data2, data1_len) != 0) {
		fprintf(stderr, "FAIL: %s data mismatch\n", label);
		fprintf(stderr, "Got:\n");
		hexdump(data1, data1_len);
		fprintf(stderr, "Want:\n");
		hexdump(data2, data2_len);
		return -1;
	}
	return 0;
}

const char *server_ecdsa_pubkey_hash = \
    "SHA256:cef2616ece9a57a76d072013b0faad2232511487c67c45bf00fbcecc070e2f5b";
const char *server_rsa_pubkey_hash = \
    "SHA256:f03c535d374614e7356c0a4e6fd37fe94297b60ed86212adcba40e8e0b07bc9f";
const char *server_unknown_pubkey_hash = \
    "SHA256:f03c535d374614e7356c0a4e6fd37fe94297b60ed86212adcba40e8e0b07bc9e";

const uint8_t test_digest[] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
	0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
	0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
};

const uint8_t test_rsa_signature[] = {
	0x77, 0xfb, 0xdd, 0x41, 0x45, 0x40, 0x25, 0xd6,
	0x01, 0xe0, 0x59, 0x04, 0x65, 0xae, 0xa1, 0x59,
	0xae, 0xa2, 0x44, 0x08, 0xf7, 0x02, 0x3d, 0xe4,
	0xc6, 0x0d, 0x4d, 0x9a, 0x3a, 0xce, 0x34, 0xbe,
	0x2e, 0xc0, 0xfc, 0xbd, 0x5b, 0x21, 0xe4, 0xbb,
	0xce, 0x02, 0xfd, 0xc3, 0xfc, 0x3d, 0x25, 0xe7,
	0xd1, 0x9a, 0x13, 0x60, 0xcb, 0x07, 0xda, 0x23,
	0xf7, 0xa3, 0xf0, 0xaf, 0x16, 0x1b, 0x28, 0x54,
	0x0a, 0x3c, 0xc1, 0x31, 0x08, 0x0f, 0x2f, 0xce,
	0x6d, 0x09, 0x45, 0x48, 0xee, 0x37, 0xa8, 0xc3,
	0x91, 0xcb, 0xde, 0xad, 0xc6, 0xcf, 0x18, 0x19,
	0xeb, 0xad, 0x08, 0x66, 0x2f, 0xce, 0x1d, 0x07,
	0xe3, 0x03, 0x84, 0x00, 0xca, 0x0f, 0x1d, 0x0f,
	0x0e, 0x6e, 0x54, 0xc1, 0x39, 0x3f, 0x2a, 0x78,
	0xc8, 0xa3, 0x6d, 0x52, 0xb9, 0x26, 0x8e, 0x7e,
	0x7a, 0x18, 0x3c, 0x8a, 0x50, 0xa3, 0xad, 0xab,
	0xd0, 0x03, 0xc5, 0x3e, 0xa5, 0x46, 0x87, 0xb0,
	0x03, 0xde, 0xd9, 0xe5, 0x4d, 0x73, 0x95, 0xcf,
	0xe1, 0x59, 0x8e, 0x2e, 0x50, 0x69, 0xe6, 0x20,
	0xaf, 0x21, 0x4f, 0xe6, 0xc4, 0x86, 0x11, 0x36,
	0x79, 0x68, 0x83, 0xde, 0x0e, 0x81, 0xde, 0x2e,
	0xd0, 0x19, 0x3f, 0x4b, 0xad, 0x3e, 0xbf, 0xdd,
	0x14, 0x4d, 0x66, 0xf3, 0x7f, 0x7d, 0xca, 0xed,
	0x99, 0x62, 0xdc, 0x7c, 0xb2, 0x8b, 0x57, 0xcb,
	0xdf, 0xed, 0x16, 0x13, 0x86, 0xd8, 0xd8, 0xb4,
	0x44, 0x6e, 0xd5, 0x54, 0xbc, 0xdf, 0xe7, 0x34,
	0x10, 0xa4, 0x17, 0x5f, 0xb7, 0xe1, 0x33, 0x2c,
	0xc1, 0x70, 0x5b, 0x87, 0x0d, 0x39, 0xee, 0xe8,
	0xec, 0x18, 0x92, 0xe8, 0x95, 0xa8, 0x93, 0x26,
	0xdf, 0x26, 0x93, 0x96, 0xfd, 0xad, 0x81, 0xb6,
	0xeb, 0x72, 0x9c, 0xd4, 0xcc, 0xf6, 0x9f, 0xb0,
	0xbb, 0xbd, 0xbd, 0x44, 0x1c, 0x99, 0x07, 0x6d,
};

static int
do_signer_tests(void)
{
	char *server_rsa_filepath = NULL;
	const uint8_t *server_ecdsa = NULL;
	size_t server_ecdsa_len;
	struct tls_signer *signer = NULL;
	uint8_t *signature = NULL;
	size_t signature_len;
	EC_KEY *ec_key = NULL;
	X509 *x509 = NULL;
	BIO *bio = NULL;
	int failed = 1;

	load_file("server1-ecdsa.pem", &server_ecdsa, &server_ecdsa_len);

	if (asprintf(&server_rsa_filepath, "%s/%s", cert_path,
	    "server1-rsa.pem") == -1) {
		fprintf(stderr, "FAIL: failed to build rsa file path\n");
		goto failure;
	}

	/* Load the ECDSA public key - we'll need it later. */
	if ((bio = BIO_new_mem_buf(server_ecdsa, server_ecdsa_len)) == NULL) {
		fprintf(stderr, "FAIL: failed to create bio\n");
		goto failure;
	}
	if ((x509 = PEM_read_bio_X509(bio, NULL, NULL, NULL)) == NULL) {
		fprintf(stderr, "FAIL: failed to load certificate\n");
		goto failure;
	}
	if ((ec_key = EVP_PKEY_get1_EC_KEY(X509_get0_pubkey(x509))) == NULL) {
		fprintf(stderr, "FAIL: failed to get EC public key\n");
		goto failure;
	}

	/* Create signer and add key pairs (one ECDSA, one RSA). */
	if ((signer = tls_signer_new()) == NULL) {
		fprintf(stderr, "FAIL: failed to create tls signer\n");
		goto failure;
	}
	if (tls_signer_add_keypair_mem(signer, server_ecdsa, server_ecdsa_len,
	    server_ecdsa, server_ecdsa_len) == -1) {
		fprintf(stderr, "FAIL: failed to add ECDSA keypair to tls "
		    "signer: %s\n", tls_signer_error(signer));
		goto failure;
	}
	if (tls_signer_add_keypair_file(signer, server_rsa_filepath,
	    server_rsa_filepath) == -1) {
		fprintf(stderr, "FAIL: failed to add RSA keypair to tls "
		    "signer: %s\n", tls_signer_error(signer));
		goto failure;
	}

	/* Sign with RSA. */
	if (tls_signer_sign(signer, server_rsa_pubkey_hash, test_digest,
	    sizeof(test_digest), TLS_PADDING_RSA_PKCS1, &signature,
	    &signature_len) == -1) {
		fprintf(stderr, "FAIL: failed to sign with RSA key: %s\n",
		    tls_signer_error(signer));
		goto failure;
	}
	if (compare_mem("rsa signature", signature, signature_len,
	    test_rsa_signature, sizeof(test_rsa_signature)) == -1)
		goto failure;

	free(signature);
	signature = NULL;

	/*
	 * Sign with ECDSA - ECDSA signatures are non-deterministic so we cannot
	 * check against a known value, rather we can only verify the signature.
	 */
	if (tls_signer_sign(signer, server_ecdsa_pubkey_hash, test_digest,
	    sizeof(test_digest), TLS_PADDING_NONE, &signature,
	    &signature_len) == -1) {
		fprintf(stderr, "FAIL: failed to sign with ECDSA key: %s\n",
		    tls_signer_error(signer));
		goto failure;
	}
	if (ECDSA_verify(0, test_digest, sizeof(test_digest), signature,
	    signature_len, ec_key) != 1) {
		fprintf(stderr, "FAIL: failed to verify ECDSA signature\n");
		goto failure;
	}

	free(signature);
	signature = NULL;

	/* Attempt to sign with an unknown cert pubkey hash. */
	if (tls_signer_sign(signer, server_unknown_pubkey_hash, test_digest,
	    sizeof(test_digest), TLS_PADDING_NONE, &signature,
	    &signature_len) != -1) {
		fprintf(stderr, "FAIL: signing succeeded with unknown key\n");
		goto failure;
	}
	if (strcmp(tls_signer_error(signer), "key not found") != 0) {
		fprintf(stderr, "FAIL: got tls signer error '%s', want "
		    "'key not found'\n", tls_signer_error(signer));
		goto failure;
	}

	failed = 0;

 failure:
	BIO_free(bio);
	EC_KEY_free(ec_key);
	X509_free(x509);
	tls_signer_free(signer);
	free((uint8_t *)server_ecdsa);
	free(server_rsa_filepath);
	free(signature);

	return failed;
}

static int
do_tls_handshake(char *name, struct tls *ctx)
{
	int rv;

	rv = tls_handshake(ctx);
	if (rv == 0)
		return (1);
	if (rv == TLS_WANT_POLLIN || rv == TLS_WANT_POLLOUT)
		return (0);

	errx(1, "%s handshake failed: %s", name, tls_error(ctx));
}

static int
do_client_server_handshake(char *desc, struct tls *client,
    struct tls *server_cctx)
{
	int i, client_done, server_done;

	i = client_done = server_done = 0;
	do {
		if (client_done == 0)
			client_done = do_tls_handshake("client", client);
		if (server_done == 0)
			server_done = do_tls_handshake("server", server_cctx);
	} while (i++ < 100 && (client_done == 0 || server_done == 0));

	if (client_done == 0 || server_done == 0) {
		printf("FAIL: %s TLS handshake did not complete\n", desc);
		return (1);
	}

	return (0);
}

static int
test_tls_handshake_socket(struct tls *client, struct tls *server)
{
	struct tls *server_cctx;
	int failure;
	int sv[2];

	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, PF_UNSPEC,
	    sv) == -1)
		err(1, "failed to create socketpair");

	if (tls_accept_socket(server, &server_cctx, sv[0]) == -1)
		errx(1, "failed to accept: %s", tls_error(server));

	if (tls_connect_socket(client, sv[1], "test") == -1)
		errx(1, "failed to connect: %s", tls_error(client));

	failure = do_client_server_handshake("socket", client, server_cctx);

	tls_free(server_cctx);

	close(sv[0]);
	close(sv[1]);

	return (failure);
}

static int
test_signer_tls_sign(void *cb_arg, const char *pubkey_hash,
    const uint8_t *input, size_t input_len, int padding_type,
    uint8_t **out_signature, size_t *out_signature_len)
{
	struct tls_signer *signer = cb_arg;

	sign_cb_count++;

	return tls_signer_sign(signer, pubkey_hash, input, input_len,
	    padding_type, out_signature, out_signature_len);
}

static int
test_signer_tls(char *certfile, char *keyfile, char *cafile)
{
	struct tls_config *client_cfg, *server_cfg;
	struct tls_signer *signer;
	struct tls *client, *server;
	int failure = 0;

	if ((signer = tls_signer_new()) == NULL)
		errx(1, "failed to create tls signer");
	if (tls_signer_add_keypair_file(signer, certfile, keyfile))
		errx(1, "failed to add keypair to signer");

	if ((client = tls_client()) == NULL)
		errx(1, "failed to create tls client");
	if ((client_cfg = tls_config_new()) == NULL)
		errx(1, "failed to create tls client config");
	tls_config_insecure_noverifyname(client_cfg);
	if (tls_config_set_ca_file(client_cfg, cafile) == -1)
		errx(1, "failed to set ca: %s", tls_config_error(client_cfg));

	if ((server = tls_server()) == NULL)
		errx(1, "failed to create tls server");
	if ((server_cfg = tls_config_new()) == NULL)
		errx(1, "failed to create tls server config");
	if (tls_config_set_sign_cb(server_cfg, test_signer_tls_sign,
	    signer) == -1)
		errx(1, "failed to set server signer callback: %s",
		    tls_config_error(server_cfg));
	if (tls_config_set_cert_file(server_cfg, certfile) == -1)
		errx(1, "failed to set server certificate: %s",
		    tls_config_error(server_cfg));

	if (tls_configure(client, client_cfg) == -1)
		errx(1, "failed to configure client: %s", tls_error(client));
	if (tls_configure(server, server_cfg) == -1)
		errx(1, "failed to configure server: %s", tls_error(server));

	tls_config_free(client_cfg);
	tls_config_free(server_cfg);

	failure |= test_tls_handshake_socket(client, server);

	tls_signer_free(signer);
	tls_free(client);
	tls_free(server);

	return (failure);
}

static int
do_signer_tls_tests(void)
{
	char *server_ecdsa_cert = NULL, *server_ecdsa_key = NULL;
	char *server_rsa_cert = NULL, *server_rsa_key = NULL;
	char *ca_root_ecdsa = NULL, *ca_root_rsa = NULL;
	int failure = 0;

	if (asprintf(&ca_root_ecdsa, "%s/%s", cert_path,
	    "ca-root-ecdsa.pem") == -1)
		err(1, "ca ecdsa root");
	if (asprintf(&ca_root_rsa, "%s/%s", cert_path,
	    "ca-root-rsa.pem") == -1)
		err(1, "ca rsa root");
	if (asprintf(&server_ecdsa_cert, "%s/%s", cert_path,
	    "server1-ecdsa-chain.pem") == -1)
		err(1, "server ecdsa chain");
	if (asprintf(&server_ecdsa_key, "%s/%s", cert_path,
	    "server1-ecdsa.pem") == -1)
		err(1, "server ecdsa key");
	if (asprintf(&server_rsa_cert, "%s/%s", cert_path,
	    "server1-rsa-chain.pem") == -1)
		err(1, "server rsa chain");
	if (asprintf(&server_rsa_key, "%s/%s", cert_path,
	    "server1-rsa.pem") == -1)
		err(1, "server rsa key");

	failure |= test_signer_tls(server_ecdsa_cert, server_ecdsa_key,
	    ca_root_ecdsa);
	failure |= test_signer_tls(server_rsa_cert, server_rsa_key,
	    ca_root_rsa);

	if (sign_cb_count != 2) {
		fprintf(stderr, "FAIL: sign callback was called %d times, "
		    "want 2\n", sign_cb_count);
		failure |= 1;
	}

	free(ca_root_ecdsa);
	free(ca_root_rsa);
	free(server_ecdsa_cert);
	free(server_ecdsa_key);
	free(server_rsa_cert);
	free(server_rsa_key);

	return (failure);
}

int
main(int argc, char **argv)
{
	int failure = 0;

	if (argc > 2) {
		fprintf(stderr, "usage: %s [certpath]\n", argv[0]);
		return (1);
	}
	if (argc == 2)
		cert_path = argv[1];

	failure |= do_signer_tests();
	failure |= do_signer_tls_tests();

	return (failure);
}
