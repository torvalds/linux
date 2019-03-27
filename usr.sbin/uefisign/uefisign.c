/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/wait.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <openssl/conf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>

#include "uefisign.h"
#include "magic.h"

static void
usage(void)
{

	fprintf(stderr, "usage: uefisign -c cert -k key -o outfile [-v] file\n"
			"       uefisign -V [-c cert] [-v] file\n");
	exit(1);
}

static char *
checked_strdup(const char *s)
{
	char *c;

	c = strdup(s);
	if (c == NULL)
		err(1, "strdup");
	return (c);
}

FILE *
checked_fopen(const char *path, const char *mode)
{
	FILE *fp;

	assert(path != NULL);

	fp = fopen(path, mode);
	if (fp == NULL)
		err(1, "%s", path);
	return (fp);
}

void
send_chunk(const void *buf, size_t len, int pipefd)
{
	ssize_t ret;

	ret = write(pipefd, &len, sizeof(len));
	if (ret != sizeof(len))
		err(1, "write");
	ret = write(pipefd, buf, len);
	if (ret != (ssize_t)len)
		err(1, "write");
}

void
receive_chunk(void **bufp, size_t *lenp, int pipefd)
{
	ssize_t ret;
	size_t len;
	void *buf;

	ret = read(pipefd, &len, sizeof(len));
	if (ret != sizeof(len))
		err(1, "read");

	buf = calloc(1, len);
	if (buf == NULL)
		err(1, "calloc");

	ret = read(pipefd, buf, len);
	if (ret != (ssize_t)len)
		err(1, "read");

	*bufp = buf;
	*lenp = len;
}

static char *
bin2hex(const char *bin, size_t bin_len)
{
	unsigned char *hex, *tmp, ch;
	size_t hex_len;
	size_t i;

	hex_len = bin_len * 2 + 1; /* +1 for '\0'. */
	hex = malloc(hex_len);
	if (hex == NULL)
		err(1, "malloc");

	tmp = hex;
	for (i = 0; i < bin_len; i++) {
		ch = bin[i];
		tmp += sprintf(tmp, "%02x", ch);
	}

	return (hex);
}

/*
 * We need to replace a standard chunk of PKCS7 signature with one mandated
 * by Authenticode.  Problem is, replacing it just like that and then calling
 * PKCS7_final() would make OpenSSL segfault somewhere in PKCS7_dataFinal().
 * So, instead, we call PKCS7_dataInit(), then put our Authenticode-specific
 * data into BIO it returned, then call PKCS7_dataFinal() - which now somehow
 * does not panic - and _then_ we replace it in the signature.  This technique
 * was used in sbsigntool by Jeremy Kerr, and might have originated in
 * osslsigncode.
 */
static void
magic(PKCS7 *pkcs7, const char *digest, size_t digest_len)
{
	BIO *bio, *t_bio;
	ASN1_TYPE *t;
	ASN1_STRING *s;
	CONF *cnf;
	unsigned char *buf, *tmp;
	char *digest_hex, *magic_conf, *str;
	int len, nid, ok;

	digest_hex = bin2hex(digest, digest_len);

	/*
	 * Construct the SpcIndirectDataContent chunk.
	 */
	nid = OBJ_create("1.3.6.1.4.1.311.2.1.4", NULL, NULL);

	asprintf(&magic_conf, magic_fmt, digest_hex);
	if (magic_conf == NULL)
		err(1, "asprintf");

	bio = BIO_new_mem_buf((void *)magic_conf, -1);
	if (bio == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "BIO_new_mem_buf(3) failed");
	}

	cnf = NCONF_new(NULL);
	if (cnf == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "NCONF_new(3) failed");
	}

	ok = NCONF_load_bio(cnf, bio, NULL);
	if (ok == 0) {
		ERR_print_errors_fp(stderr);
		errx(1, "NCONF_load_bio(3) failed");
	}

	str = NCONF_get_string(cnf, "default", "asn1");
	if (str == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "NCONF_get_string(3) failed");
	}

	t = ASN1_generate_nconf(str, cnf);
	if (t == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "ASN1_generate_nconf(3) failed");
	}

	/*
	 * We now have our proprietary piece of ASN.1.  Let's do
	 * the actual signing.
	 */
	len = i2d_ASN1_TYPE(t, NULL);
	tmp = buf = calloc(1, len);
	if (tmp == NULL)
		err(1, "calloc");
	i2d_ASN1_TYPE(t, &tmp);

	/*
	 * We now have contents of 't' stuffed into memory buffer 'buf'.
	 */
	tmp = NULL;
	t = NULL;

	t_bio = PKCS7_dataInit(pkcs7, NULL);
	if (t_bio == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "PKCS7_dataInit(3) failed");
	}

	BIO_write(t_bio, buf + 2, len - 2);

	ok = PKCS7_dataFinal(pkcs7, t_bio);
	if (ok == 0) {
		ERR_print_errors_fp(stderr);
		errx(1, "PKCS7_dataFinal(3) failed");
	}

	t = ASN1_TYPE_new();
	s = ASN1_STRING_new();
	ASN1_STRING_set(s, buf, len);
	ASN1_TYPE_set(t, V_ASN1_SEQUENCE, s);

	PKCS7_set0_type_other(pkcs7->d.sign->contents, nid, t);
}

static void
sign(X509 *cert, EVP_PKEY *key, int pipefd)
{
	PKCS7 *pkcs7;
	BIO *bio, *out;
	const EVP_MD *md;
	PKCS7_SIGNER_INFO *info;
	void *digest, *signature;
	size_t digest_len, signature_len;
	int ok;

	assert(cert != NULL);
	assert(key != NULL);

	receive_chunk(&digest, &digest_len, pipefd);

	bio = BIO_new_mem_buf(digest, digest_len);
	if (bio == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "BIO_new_mem_buf(3) failed");
	}

	pkcs7 = PKCS7_sign(NULL, NULL, NULL, bio, PKCS7_BINARY | PKCS7_PARTIAL);
	if (pkcs7 == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "PKCS7_sign(3) failed");
	}

	md = EVP_get_digestbyname(DIGEST);
	if (md == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "EVP_get_digestbyname(\"%s\") failed", DIGEST);
	}

	info = PKCS7_sign_add_signer(pkcs7, cert, key, md, 0);
	if (info == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "PKCS7_sign_add_signer(3) failed");
	}

	/*
	 * XXX: All the signed binaries seem to have this, but where is it
	 *      described in the spec?
	 */
	PKCS7_add_signed_attribute(info, NID_pkcs9_contentType,
	    V_ASN1_OBJECT, OBJ_txt2obj("1.3.6.1.4.1.311.2.1.4", 1));

	magic(pkcs7, digest, digest_len);

#if 0
	out = BIO_new(BIO_s_file());
	BIO_set_fp(out, stdout, BIO_NOCLOSE);
	PKCS7_print_ctx(out, pkcs7, 0, NULL);

	i2d_PKCS7_bio(out, pkcs7);
#endif

	out = BIO_new(BIO_s_mem());
	if (out == NULL) {
		ERR_print_errors_fp(stderr);
		errx(1, "BIO_new(3) failed");
	}

	ok = i2d_PKCS7_bio(out, pkcs7);
	if (ok == 0) {
		ERR_print_errors_fp(stderr);
		errx(1, "i2d_PKCS7_bio(3) failed");
	}

	signature_len = BIO_get_mem_data(out, &signature);
	if (signature_len <= 0) {
		ERR_print_errors_fp(stderr);
		errx(1, "BIO_get_mem_data(3) failed");
	}

	(void)BIO_set_close(out, BIO_NOCLOSE);
	BIO_free(out);

	send_chunk(signature, signature_len, pipefd);
}

static int
wait_for_child(pid_t pid)
{
	int status;

	pid = waitpid(pid, &status, 0);
	if (pid == -1)
		err(1, "waitpid");

	return (WEXITSTATUS(status));
}

int
main(int argc, char **argv)
{
	int ch, error;
	bool Vflag = false, vflag = false;
	const char *certpath = NULL, *keypath = NULL, *outpath = NULL, *inpath = NULL;
	FILE *certfp = NULL, *keyfp = NULL;
	X509 *cert = NULL;
	EVP_PKEY *key = NULL;
	pid_t pid;
	int pipefds[2];

	while ((ch = getopt(argc, argv, "Vc:k:o:v")) != -1) {
		switch (ch) {
		case 'V':
			Vflag = true;
			break;
		case 'c':
			certpath = checked_strdup(optarg);
			break;
		case 'k':
			keypath = checked_strdup(optarg);
			break;
		case 'o':
			outpath = checked_strdup(optarg);
			break;
		case 'v':
			vflag = true;
			break;
		default:
			usage();
		}
	}

	argc -= optind;
	argv += optind;
	if (argc != 1)
		usage();

	if (Vflag) {
		if (certpath != NULL)
			errx(1, "-V and -c are mutually exclusive");
		if (keypath != NULL)
			errx(1, "-V and -k are mutually exclusive");
		if (outpath != NULL)
			errx(1, "-V and -o are mutually exclusive");
	} else {
		if (certpath == NULL)
			errx(1, "-c option is mandatory");
		if (keypath == NULL)
			errx(1, "-k option is mandatory");
		if (outpath == NULL)
			errx(1, "-o option is mandatory");
	}

	inpath = argv[0];

	OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CONFIG |
	    OPENSSL_INIT_LOAD_CRYPTO_STRINGS |
	    OPENSSL_INIT_ADD_ALL_CIPHERS | OPENSSL_INIT_ADD_ALL_DIGESTS, NULL);

	error = pipe(pipefds);
	if (error != 0)
		err(1, "pipe");

	pid = fork();
	if (pid < 0)
		err(1, "fork");

	if (pid == 0)
		return (child(inpath, outpath, pipefds[1], Vflag, vflag));

	if (!Vflag) {
		certfp = checked_fopen(certpath, "r");
		cert = PEM_read_X509(certfp, NULL, NULL, NULL);
		if (cert == NULL) {
			ERR_print_errors_fp(stderr);
			errx(1, "failed to load certificate from %s", certpath);
		}

		keyfp = checked_fopen(keypath, "r");
		key = PEM_read_PrivateKey(keyfp, NULL, NULL, NULL);
		if (key == NULL) {
			ERR_print_errors_fp(stderr);
			errx(1, "failed to load private key from %s", keypath);
		}

		sign(cert, key, pipefds[0]);
	}

	return (wait_for_child(pid));
}
