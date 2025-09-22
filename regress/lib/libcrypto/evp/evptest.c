/*	$OpenBSD: evptest.c,v 1.15 2024/03/30 09:49:59 tb Exp $	*/
/* Written by Ben Laurie, 2001 */
/*
 * Copyright (c) 2001 The OpenSSL Project.  All rights reserved.
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

#include <openssl/opensslconf.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/conf.h>

int verbose;

static void
hexdump(FILE *f, const char *title, const unsigned char *s, int l)
{
	int n = 0;

	fprintf(f, "%s",title);
	for (; n < l; ++n) {
		if ((n % 16) == 0)
			fprintf(f, "\n%04x",n);
		fprintf(f, " %02x",s[n]);
	}
	fprintf(f, "\n");
}

static int
convert(unsigned char *s)
{
	unsigned char *d;

	for (d = s; *s; s += 2,++d) {
		unsigned int n;

		if (!s[1]) {
			fprintf(stderr, "Odd number of hex digits!\n");
			exit(4);
		}
		if (sscanf((char *)s, "%2x", &n) != 1) {
			fprintf(stderr, "Invalid hex value at %s\n", s);
			exit(4);
		}

		*d = (unsigned char)n;
	}
	return s - d;
}

static char *
sstrsep(char **string, const char *delim)
{
	char isdelim[256];
	char *token = *string;

	if (**string == 0)
		return NULL;

	memset(isdelim, 0, 256);
	isdelim[0] = 1;

	while (*delim) {
		isdelim[(unsigned char)(*delim)] = 1;
		delim++;
	}

	while (!isdelim[(unsigned char)(**string)]) {
		(*string)++;
	}

	if (**string) {
		**string = 0;
		(*string)++;
	}

	return token;
}

static unsigned char *
ustrsep(char **p, const char *sep)
{
	return (unsigned char *)sstrsep(p, sep);
}

static void
test1(const EVP_CIPHER *c, const unsigned char *key, int kn,
    const unsigned char *iv, int in, const unsigned char *plaintext, int pn,
    const unsigned char *ciphertext, int cn, int encdec)
{
	EVP_CIPHER_CTX *ctx;
	unsigned char out[4096];
	const unsigned char *eiv;
	int outl, outl2;

	if (verbose) {
		printf("Testing cipher %s%s\n", EVP_CIPHER_name(c),
		    (encdec == 1 ? "(encrypt)" : (encdec == 0 ? "(decrypt)" : "(encrypt/decrypt)")));
		hexdump(stdout, "Key",key,kn);
		if (in)
			hexdump(stdout, "IV",iv,in);
		hexdump(stdout, "Plaintext",plaintext,pn);
		hexdump(stdout, "Ciphertext",ciphertext,cn);
	}

	if (kn != EVP_CIPHER_key_length(c)) {
		fprintf(stderr, "Key length doesn't match, got %d expected %lu\n",kn,
		    (unsigned long)EVP_CIPHER_key_length(c));
		exit(5);
	}
	if ((ctx = EVP_CIPHER_CTX_new()) == NULL) {
		fprintf(stderr, "EVP_CIPHER_CTX_new failed\n");
		ERR_print_errors_fp(stderr);
		exit(12);
	}
	EVP_CIPHER_CTX_set_flags(ctx, EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);
	if (encdec != 0) {
		eiv = iv;
		if (EVP_CIPHER_mode(c) == EVP_CIPH_WRAP_MODE && in == 0)
			eiv = NULL;
		if (!EVP_EncryptInit_ex(ctx, c, NULL, key, eiv)) {
			fprintf(stderr, "EncryptInit failed\n");
			ERR_print_errors_fp(stderr);
			exit(10);
		}
		EVP_CIPHER_CTX_set_padding(ctx, 0);

		if (!EVP_EncryptUpdate(ctx, out, &outl, plaintext, pn)) {
			fprintf(stderr, "Encrypt failed\n");
			ERR_print_errors_fp(stderr);
			exit(6);
		}
		if (!EVP_EncryptFinal_ex(ctx, out + outl, &outl2)) {
			fprintf(stderr, "EncryptFinal failed\n");
			ERR_print_errors_fp(stderr);
			exit(7);
		}

		if (outl + outl2 != cn) {
			fprintf(stderr, "Ciphertext length mismatch got %d expected %d\n",
			    outl + outl2, cn);
			exit(8);
		}

		if (memcmp(out, ciphertext, cn)) {
			fprintf(stderr, "Ciphertext mismatch\n");
			hexdump(stderr, "Got",out,cn);
			hexdump(stderr, "Expected",ciphertext,cn);
			exit(9);
		}
	}

	if (encdec <= 0) {
		eiv = iv;
		if (EVP_CIPHER_mode(c) == EVP_CIPH_WRAP_MODE && in == 0)
			eiv = NULL;
		if (!EVP_DecryptInit_ex(ctx, c,NULL, key, eiv)) {
			fprintf(stderr, "DecryptInit failed\n");
			ERR_print_errors_fp(stderr);
			exit(11);
		}
		EVP_CIPHER_CTX_set_padding(ctx, 0);

		if (!EVP_DecryptUpdate(ctx, out, &outl, ciphertext, cn)) {
			fprintf(stderr, "Decrypt failed\n");
			ERR_print_errors_fp(stderr);
			exit(6);
		}
		if (!EVP_DecryptFinal_ex(ctx, out + outl, &outl2)) {
			fprintf(stderr, "DecryptFinal failed\n");
			ERR_print_errors_fp(stderr);
			exit(7);
		}

		if (outl + outl2 != pn) {
			fprintf(stderr, "Plaintext length mismatch got %d expected %d\n",
			    outl + outl2, pn);
			exit(8);
		}

		if (memcmp(out, plaintext, pn)) {
			fprintf(stderr, "Plaintext mismatch\n");
			hexdump(stderr, "Got",out,pn);
			hexdump(stderr, "Expected",plaintext,pn);
			exit(9);
		}
	}

	EVP_CIPHER_CTX_free(ctx);

	if (verbose)
		printf("\n");
}

static int
test_cipher(const char *cipher, const unsigned char *key, int kn,
    const unsigned char *iv, int in, const unsigned char *plaintext, int pn,
    const unsigned char *ciphertext, int cn, int encdec)
{
	const EVP_CIPHER *c;

	c = EVP_get_cipherbyname(cipher);
	if (!c)
		return 0;

	test1(c, key, kn, iv, in, plaintext, pn, ciphertext, cn, encdec);

	return 1;
}

static int
test_digest(const char *digest, const unsigned char *plaintext, int pn,
    const unsigned char *ciphertext, unsigned int cn)
{
	const EVP_MD *d;
	EVP_MD_CTX *ctx;
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int mdn;

	d = EVP_get_digestbyname(digest);
	if (!d)
		return 0;

	if (verbose) {
		printf("Testing digest %s\n",EVP_MD_name(d));
		hexdump(stdout, "Plaintext",plaintext,pn);
		hexdump(stdout, "Digest",ciphertext,cn);
	}

	if ((ctx = EVP_MD_CTX_new()) == NULL) {
		fprintf(stderr, "EVP_CIPHER_CTX_new failed\n");
		ERR_print_errors_fp(stderr);
		exit(104);
	}
	if (!EVP_DigestInit_ex(ctx, d, NULL)) {
		fprintf(stderr, "DigestInit failed\n");
		ERR_print_errors_fp(stderr);
		exit(100);
	}
	if (!EVP_DigestUpdate(ctx, plaintext, pn)) {
		fprintf(stderr, "DigestUpdate failed\n");
		ERR_print_errors_fp(stderr);
		exit(101);
	}
	if (!EVP_DigestFinal_ex(ctx, md, &mdn)) {
		fprintf(stderr, "DigestFinal failed\n");
		ERR_print_errors_fp(stderr);
		exit(101);
	}
	EVP_MD_CTX_free(ctx);
	ctx = NULL;

	if (mdn != cn) {
		fprintf(stderr, "Digest length mismatch, got %d expected %d\n",mdn,cn);
		exit(102);
	}

	if (memcmp(md, ciphertext, cn)) {
		fprintf(stderr, "Digest mismatch\n");
		hexdump(stderr, "Got",md,cn);
		hexdump(stderr, "Expected",ciphertext,cn);
		exit(103);
	}
	if (verbose)
		printf("\n");

	return 1;
}

int
main(int argc, char **argv)
{
	const char *szTestFile;
	FILE *f;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "%s <test file>\n",argv[0]);
		exit(1);
	}
	if (argc == 3 && strcmp(argv[1], "-v") == 0) {
		verbose = 1;
		argv++;
		argc--;
	}

	szTestFile = argv[1];

	f=fopen(szTestFile, "r");
	if (!f) {
		perror(szTestFile);
		exit(2);
	}

	/* Load up the software EVP_CIPHER and EVP_MD definitions */
	OpenSSL_add_all_ciphers();
	OpenSSL_add_all_digests();

	for (;;) {
		char line[8 * 1024];
		char *p;
		char *cipher;
		unsigned char *iv, *key, *plaintext, *ciphertext;
		int encdec;
		int kn, in, pn, cn;

		if (!fgets((char *)line, sizeof line, f))
			break;
		if (line[0] == '#' || line[0] == '\n')
			continue;
		p = line;
		cipher=sstrsep(&p, ":");
		key=ustrsep(&p, ":");
		iv=ustrsep(&p, ":");
		plaintext=ustrsep(&p, ":");
		ciphertext=ustrsep(&p, ":");
		if (p[-1] == '\n') {
			p[-1] = '\0';
			encdec = -1;
		} else {
			encdec = atoi(sstrsep(&p, "\n"));
		}


		kn = convert(key);
		in = convert(iv);
		pn = convert(plaintext);
		cn = convert(ciphertext);

		if (!test_cipher(cipher, key, kn, iv, in, plaintext, pn, ciphertext, cn, encdec) &&
		    !test_digest(cipher, plaintext, pn, ciphertext, cn)) {
#ifdef OPENSSL_NO_AES
			if (strstr(cipher, "AES") == cipher && verbose) {
				if (verbose)
					fprintf(stdout, "Cipher disabled, skipping %s\n", cipher);
				continue;
			}
#endif
#ifdef OPENSSL_NO_DES
			if (strstr(cipher, "DES") == cipher && verbose) {
				if (verbose)
					fprintf(stdout, "Cipher disabled, skipping %s\n", cipher);
				continue;
			}
#endif
#ifdef OPENSSL_NO_RC4
			if (strstr(cipher, "RC4") == cipher && verbose) {
				if (verbose)
					fprintf(stdout, "Cipher disabled, skipping %s\n", cipher);
				continue;
			}
#endif
#ifdef OPENSSL_NO_CAMELLIA
			if (strstr(cipher, "CAMELLIA") == cipher && verbose) {
				if (verbose)
					fprintf(stdout, "Cipher disabled, skipping %s\n", cipher);
				continue;
			}
#endif
#ifdef OPENSSL_NO_SEED
			if (strstr(cipher, "SEED") == cipher) {
				if (verbose)
					fprintf(stdout, "Cipher disabled, skipping %s\n", cipher);
				continue;
			}
#endif
#ifdef OPENSSL_NO_CHACHA
			if (strstr(cipher, "ChaCha") == cipher) {
				if (verbose)
					fprintf(stdout, "Cipher disabled, skipping %s\n", cipher);
				continue;
			}
#endif
#ifdef OPENSSL_NO_GOST
			if (strstr(cipher, "md_gost") == cipher ||
			    strstr(cipher, "streebog") == cipher) {
				if (verbose)
					fprintf(stdout, "Cipher disabled, skipping %s\n", cipher);
				continue;
			}
#endif
			fprintf(stderr, "Can't find %s\n",cipher);
			exit(3);
		}
	}
	fclose(f);

	EVP_cleanup();
	CRYPTO_cleanup_all_ex_data();
	ERR_remove_thread_state(NULL);
	ERR_free_strings();

	return 0;
}
