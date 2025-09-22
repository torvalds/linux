/*	$OpenBSD: aeadtest.c,v 1.26 2023/09/28 14:55:48 tb Exp $	*/
/*
 * Copyright (c) 2022 Joel Sing <jsing@openbsd.org>
 * Copyright (c) 2014, Google Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>

/*
 * This program tests an AEAD against a series of test vectors from a file. The
 * test vector file consists of key-value lines where the key and value are
 * separated by a colon and optional whitespace. The keys are listed in
 * NAMES, below. The values are hex-encoded data.
 *
 * After a number of key-value lines, a blank line indicates the end of the
 * test case.
 *
 * For example, here's a valid test case:
 *
 *   AEAD: chacha20-poly1305
 *   KEY: bcb2639bf989c6251b29bf38d39a9bdce7c55f4b2ac12a39c8a37b5d0a5cc2b5
 *   NONCE: 1e8b4c510f5ca083
 *   IN: 8c8419bc27
 *   AD: 34ab88c265
 *   CT: 1a7c2f33f5
 *   TAG: 2875c659d0f2808de3a40027feff91a4
 */

#define BUF_MAX 1024

/* MS defines in global headers, remove it */
#ifdef _MSC_VER
#ifdef IN
#undef IN
#endif
#endif

/* These are the different types of line that are found in the input file. */
enum {
	AEAD = 0,	/* name of the AEAD algorithm. */
	KEY,		/* hex encoded key. */
	NONCE,		/* hex encoded nonce. */
	IN,		/* hex encoded plaintext. */
	AD,		/* hex encoded additional data. */
	CT,		/* hex encoded ciphertext (not including the
			 * authenticator, which is next. */
	TAG,		/* hex encoded authenticator. */
	NUM_TYPES
};

static const char NAMES[NUM_TYPES][6] = {
	"AEAD",
	"KEY",
	"NONCE",
	"IN",
	"AD",
	"CT",
	"TAG",
};

static unsigned char
hex_digit(char h)
{
	if (h >= '0' && h <= '9')
		return h - '0';
	else if (h >= 'a' && h <= 'f')
		return h - 'a' + 10;
	else if (h >= 'A' && h <= 'F')
		return h - 'A' + 10;
	else
		return 16;
}

static int
aead_from_name(const EVP_AEAD **aead, const EVP_CIPHER **cipher,
    const char *name)
{
	*aead = NULL;
	*cipher = NULL;

	if (strcmp(name, "aes-128-gcm") == 0) {
		*aead = EVP_aead_aes_128_gcm();
		*cipher = EVP_aes_128_gcm();
	} else if (strcmp(name, "aes-192-gcm") == 0) {
		*cipher = EVP_aes_192_gcm();
	} else if (strcmp(name, "aes-256-gcm") == 0) {
		*aead = EVP_aead_aes_256_gcm();
		*cipher = EVP_aes_256_gcm();
	} else if (strcmp(name, "chacha20-poly1305") == 0) {
		*aead = EVP_aead_chacha20_poly1305();
		*cipher = EVP_chacha20_poly1305();
	} else if (strcmp(name, "xchacha20-poly1305") == 0) {
		*aead = EVP_aead_xchacha20_poly1305();
	} else {
		fprintf(stderr, "Unknown AEAD: %s\n", name);
		return 0;
	}

	return 1;
}

static int
run_aead_test(const EVP_AEAD *aead, unsigned char bufs[NUM_TYPES][BUF_MAX],
    const unsigned int lengths[NUM_TYPES], unsigned int line_no)
{
	EVP_AEAD_CTX *ctx;
	unsigned char out[BUF_MAX + EVP_AEAD_MAX_TAG_LENGTH], out2[BUF_MAX];
	size_t out_len, out_len2;
	int ret = 0;

	if ((ctx = EVP_AEAD_CTX_new()) == NULL) {
		fprintf(stderr, "Failed to allocate AEAD context on line %u\n",
		    line_no);
		goto err;
	}

	if (!EVP_AEAD_CTX_init(ctx, aead, bufs[KEY], lengths[KEY],
	    lengths[TAG], NULL)) {
		fprintf(stderr, "Failed to init AEAD on line %u\n", line_no);
		goto err;
	}

	if (!EVP_AEAD_CTX_seal(ctx, out, &out_len, sizeof(out), bufs[NONCE],
	    lengths[NONCE], bufs[IN], lengths[IN], bufs[AD], lengths[AD])) {
		fprintf(stderr, "Failed to run AEAD on line %u\n", line_no);
		goto err;
	}

	if (out_len != lengths[CT] + lengths[TAG]) {
		fprintf(stderr, "Bad output length on line %u: %zu vs %u\n",
		    line_no, out_len, (unsigned)(lengths[CT] + lengths[TAG]));
		goto err;
	}

	if (memcmp(out, bufs[CT], lengths[CT]) != 0) {
		fprintf(stderr, "Bad output on line %u\n", line_no);
		goto err;
	}

	if (memcmp(out + lengths[CT], bufs[TAG], lengths[TAG]) != 0) {
		fprintf(stderr, "Bad tag on line %u\n", line_no);
		goto err;
	}

	if (!EVP_AEAD_CTX_open(ctx, out2, &out_len2, lengths[IN], bufs[NONCE],
	    lengths[NONCE], out, out_len, bufs[AD], lengths[AD])) {
		fprintf(stderr, "Failed to decrypt on line %u\n", line_no);
		goto err;
	}

	if (out_len2 != lengths[IN]) {
		fprintf(stderr, "Bad decrypt on line %u: %zu\n",
		    line_no, out_len2);
		goto err;
	}

	if (memcmp(out2, bufs[IN], out_len2) != 0) {
		fprintf(stderr, "Plaintext mismatch on line %u\n", line_no);
		goto err;
	}

	out[0] ^= 0x80;
	if (EVP_AEAD_CTX_open(ctx, out2, &out_len2, lengths[IN], bufs[NONCE],
	    lengths[NONCE], out, out_len, bufs[AD], lengths[AD])) {
		fprintf(stderr, "Decrypted bad data on line %u\n", line_no);
		goto err;
	}

	ret = 1;

 err:
	EVP_AEAD_CTX_free(ctx);

	return ret;
}

static int
run_cipher_aead_encrypt_test(const EVP_CIPHER *cipher,
    unsigned char bufs[NUM_TYPES][BUF_MAX],
    const unsigned int lengths[NUM_TYPES], unsigned int line_no)
{
	unsigned char out[BUF_MAX + EVP_AEAD_MAX_TAG_LENGTH];
	EVP_CIPHER_CTX *ctx;
	size_t out_len;
	int len;
	int ivlen;
	int ret = 0;

	if ((ctx = EVP_CIPHER_CTX_new()) == NULL) {
		fprintf(stderr, "FAIL: EVP_CIPHER_CTX_new\n");
		goto err;
	}

	if (!EVP_EncryptInit_ex(ctx, cipher, NULL, NULL, NULL)) {
		fprintf(stderr, "FAIL: EVP_EncryptInit_ex with cipher\n");
		goto err;
	}

	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, lengths[NONCE], NULL)) {
		fprintf(stderr, "FAIL: EVP_CTRL_AEAD_SET_IVLEN\n");
		goto err;
	}

	ivlen = EVP_CIPHER_CTX_iv_length(ctx);
	if (ivlen != (int)lengths[NONCE]) {
		fprintf(stderr, "FAIL: ivlen %d != nonce length %d\n", ivlen,
		    (int)lengths[NONCE]);
		goto err;
	}

	if (!EVP_EncryptInit_ex(ctx, NULL, NULL, bufs[KEY], NULL)) {
		fprintf(stderr, "FAIL: EVP_EncryptInit_ex with key\n");
		goto err;
	}
	if (!EVP_EncryptInit_ex(ctx, NULL, NULL, NULL, bufs[NONCE])) {
		fprintf(stderr, "FAIL: EVP_EncryptInit_ex with nonce\n");
		goto err;
	}

	if (!EVP_EncryptUpdate(ctx, NULL, &len, bufs[AD], lengths[AD])) {
		fprintf(stderr, "FAIL: EVP_EncryptUpdate with AD\n");
		goto err;
	}
	if ((unsigned int)len != lengths[AD]) {
		fprintf(stderr, "FAIL: EVP_EncryptUpdate with AD length = %u, "
		    "want %u\n", len, lengths[AD]);
		goto err;
	}
	if (!EVP_EncryptUpdate(ctx, out, &len, bufs[IN], lengths[IN])) {
		fprintf(stderr, "FAIL: EVP_EncryptUpdate with plaintext\n");
		goto err;
	}
	out_len = len;
	if (!EVP_EncryptFinal_ex(ctx, out + out_len, &len)) {
		fprintf(stderr, "FAIL: EVP_EncryptFinal_ex\n");
		goto err;
	}
	out_len += len;
	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, lengths[TAG],
	    out + out_len)) {
		fprintf(stderr, "FAIL: EVP_EncryptInit_ex with cipher\n");
		goto err;
	}
	out_len += lengths[TAG];

	if (out_len != lengths[CT] + lengths[TAG]) {
		fprintf(stderr, "Bad output length on line %u: %zu vs %u\n",
		    line_no, out_len, (unsigned)(lengths[CT] + lengths[TAG]));
		goto err;
	}

	if (memcmp(out, bufs[CT], lengths[CT]) != 0) {
		fprintf(stderr, "Bad output on line %u\n", line_no);
		goto err;
	}

	if (memcmp(out + lengths[CT], bufs[TAG], lengths[TAG]) != 0) {
		fprintf(stderr, "Bad tag on line %u\n", line_no);
		goto err;
	}

	ret = 1;

 err:
	EVP_CIPHER_CTX_free(ctx);

	return ret;
}

static int
run_cipher_aead_decrypt_test(const EVP_CIPHER *cipher, int invalid,
    unsigned char bufs[NUM_TYPES][BUF_MAX],
    const unsigned int lengths[NUM_TYPES], unsigned int line_no)
{
	unsigned char in[BUF_MAX], out[BUF_MAX + EVP_AEAD_MAX_TAG_LENGTH];
	EVP_CIPHER_CTX *ctx;
	size_t out_len;
	int len;
	int ret = 0;

	if ((ctx = EVP_CIPHER_CTX_new()) == NULL) {
		fprintf(stderr, "FAIL: EVP_CIPHER_CTX_new\n");
		goto err;
	}

	if (!EVP_DecryptInit_ex(ctx, cipher, NULL, NULL, NULL)) {
		fprintf(stderr, "FAIL: EVP_DecryptInit_ex with cipher\n");
		goto err;
	}

	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, lengths[NONCE],
	    NULL)) {
		fprintf(stderr, "FAIL: EVP_CTRL_AEAD_SET_IVLEN\n");
		goto err;
	}

	memcpy(in, bufs[TAG], lengths[TAG]);
	if (invalid && lengths[CT] == 0)
		in[0] ^= 0x80;

	if (!EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, lengths[TAG], in)) {
		fprintf(stderr, "FAIL: EVP_CTRL_AEAD_SET_TAG\n");
		goto err;
	}

	if (!EVP_DecryptInit_ex(ctx, NULL, NULL, bufs[KEY], NULL)) {
		fprintf(stderr, "FAIL: EVP_DecryptInit_ex with key\n");
		goto err;
	}
	if (!EVP_DecryptInit_ex(ctx, NULL, NULL, NULL, bufs[NONCE])) {
		fprintf(stderr, "FAIL: EVP_DecryptInit_ex with nonce\n");
		goto err;
	}

	if (!EVP_DecryptUpdate(ctx, NULL, &len, bufs[AD], lengths[AD])) {
		fprintf(stderr, "FAIL: EVP_DecryptUpdate with AD\n");
		goto err;
	}
	if ((unsigned int)len != lengths[AD]) {
		fprintf(stderr, "FAIL: EVP_EncryptUpdate with AD length = %u, "
		    "want %u\n", len, lengths[AD]);
		goto err;
	}

	memcpy(in, bufs[CT], lengths[CT]);
	if (invalid && lengths[CT] > 0)
		in[0] ^= 0x80;

	if (!EVP_DecryptUpdate(ctx, out, &len, in, lengths[CT])) {
		fprintf(stderr, "FAIL: EVP_DecryptUpdate with ciphertext\n");
		goto err;
	}
	out_len = len;

	if (invalid) {
		if (EVP_DecryptFinal_ex(ctx, out + out_len, &len)) {
			fprintf(stderr, "FAIL: EVP_DecryptFinal_ex succeeded "
			    "with invalid ciphertext on line %u\n", line_no);
			goto err;
		}
		goto done;
	}

	if (!EVP_DecryptFinal_ex(ctx, out + out_len, &len)) {
		fprintf(stderr, "FAIL: EVP_DecryptFinal_ex\n");
		goto err;
	}
	out_len += len;

	if (out_len != lengths[IN]) {
		fprintf(stderr, "Bad decrypt on line %u: %zu\n",
		    line_no, out_len);
		goto err;
	}

	if (memcmp(out, bufs[IN], out_len) != 0) {
		fprintf(stderr, "Plaintext mismatch on line %u\n", line_no);
		goto err;
	}

 done:
	ret = 1;

 err:
	EVP_CIPHER_CTX_free(ctx);

	return ret;
}

static int
run_cipher_aead_test(const EVP_CIPHER *cipher,
    unsigned char bufs[NUM_TYPES][BUF_MAX],
    const unsigned int lengths[NUM_TYPES], unsigned int line_no)
{
	if (!run_cipher_aead_encrypt_test(cipher, bufs, lengths, line_no))
		return 0;
	if (!run_cipher_aead_decrypt_test(cipher, 0, bufs, lengths, line_no))
		return 0;
	if (!run_cipher_aead_decrypt_test(cipher, 1, bufs, lengths, line_no))
		return 0;

	return 1;
}

int
main(int argc, char **argv)
{
	FILE *f;
	const EVP_AEAD *aead = NULL;
	const EVP_CIPHER *cipher = NULL;
	unsigned int line_no = 0, num_tests = 0, j;
	unsigned char bufs[NUM_TYPES][BUF_MAX];
	unsigned int lengths[NUM_TYPES];
	const char *aeadname;

	if (argc != 3) {
		fprintf(stderr, "%s <aead> <test file.txt>\n", argv[0]);
		return 1;
	}

	if ((f = fopen(argv[2], "r")) == NULL) {
		perror("failed to open input");
		return 1;
	}

	for (j = 0; j < NUM_TYPES; j++)
		lengths[j] = 0;

	for (;;) {
		char line[4096];
		unsigned int i, type_len = 0;

		unsigned char *buf = NULL;
		unsigned int *buf_len = NULL;

		if (!fgets(line, sizeof(line), f))
			break;

		line_no++;
		if (line[0] == '#')
			continue;

		if (line[0] == '\n' || line[0] == 0) {
			/* Run a test, if possible. */
			char any_values_set = 0;
			for (j = 0; j < NUM_TYPES; j++) {
				if (lengths[j] != 0) {
					any_values_set = 1;
					break;
				}
			}

			if (!any_values_set)
				continue;

			aeadname = argv[1];
			if (lengths[AEAD] != 0)
				aeadname = bufs[AEAD];

			if (!aead_from_name(&aead, &cipher, aeadname)) {
				fprintf(stderr, "Aborting...\n");
				return 4;
			}

			if (aead != NULL) {
				if (!run_aead_test(aead, bufs, lengths,
				    line_no))
					return 4;
			}
			if (cipher != NULL) {
				if (!run_cipher_aead_test(cipher, bufs, lengths,
				    line_no))
					return 4;
			}

			for (j = 0; j < NUM_TYPES; j++)
				lengths[j] = 0;

			num_tests++;
			continue;
		}

		/*
		 * Each line looks like:
		 *   TYPE: 0123abc
		 * Where "TYPE" is the type of the data on the line,
		 * e.g. "KEY".
		 */
		for (i = 0; line[i] != 0 && line[i] != '\n'; i++) {
			if (line[i] == ':') {
				type_len = i;
				break;
			}
		}
		i++;

		if (type_len == 0) {
			fprintf(stderr, "Parse error on line %u\n", line_no);
			return 3;
		}

		/* After the colon, there's optional whitespace. */
		for (; line[i] != 0 && line[i] != '\n'; i++) {
			if (line[i] != ' ' && line[i] != '\t')
				break;
		}

		line[type_len] = 0;
		for (j = 0; j < NUM_TYPES; j++) {
			if (strcmp(line, NAMES[j]) != 0)
				continue;
			if (lengths[j] != 0) {
				fprintf(stderr, "Duplicate value on line %u\n",
				    line_no);
				return 3;
			}
			buf = bufs[j];
			buf_len = &lengths[j];
			break;
		}

		if (buf == NULL) {
			fprintf(stderr, "Unknown line type on line %u\n",
			    line_no);
			return 3;
		}

		if (j == AEAD) {
			*buf_len = strlcpy(buf, line + i, BUF_MAX);
			for (j = 0; j < BUF_MAX; j++) {
				if (buf[j] == '\n')
					buf[j] = '\0';
			}
			continue;
		}

		if (line[i] == '"') {
			i++;
			for (j = 0; line[i] != 0 && line[i] != '\n'; i++) {
				if (line[i] == '"')
					break;
				if (j == BUF_MAX) {
					fprintf(stderr, "Too much data on "
					    "line %u (max is %u bytes)\n",
					    line_no, (unsigned) BUF_MAX);
					return 3;
				}
				buf[j++] = line[i];
				*buf_len = *buf_len + 1;
			}
			if (line[i + 1] != 0 && line[i + 1] != '\n') {
				fprintf(stderr, "Trailing data on line %u\n",
				    line_no);
				return 3;
			}
		} else {
			for (j = 0; line[i] != 0 && line[i] != '\n'; i++) {
				unsigned char v, v2;
				v = hex_digit(line[i++]);
				if (line[i] == 0 || line[i] == '\n') {
					fprintf(stderr, "Odd-length hex data "
					    "on line %u\n", line_no);
					return 3;
				}
				v2 = hex_digit(line[i]);
				if (v > 15 || v2 > 15) {
					fprintf(stderr, "Invalid hex char on "
					    "line %u\n", line_no);
					return 3;
				}
				v <<= 4;
				v |= v2;

				if (j == BUF_MAX) {
					fprintf(stderr, "Too much hex data on "
					    "line %u (max is %u bytes)\n",
					    line_no, (unsigned) BUF_MAX);
					return 3;
				}
				buf[j++] = v;
				*buf_len = *buf_len + 1;
			}
		}
	}

	printf("Completed %u test cases\n", num_tests);
	printf("PASS\n");
	fclose(f);

	return 0;
}
