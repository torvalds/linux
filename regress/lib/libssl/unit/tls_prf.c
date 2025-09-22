/* $OpenBSD: tls_prf.c,v 1.11 2024/07/16 14:38:59 jsing Exp $ */
/*
 * Copyright (c) 2017 Joel Sing <jsing@openbsd.org>
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

#include "ssl_local.h"

int tls1_PRF(SSL *s, const unsigned char *secret, size_t secret_len,
    const void *seed1, size_t seed1_len, const void *seed2, size_t seed2_len,
    const void *seed3, size_t seed3_len, const void *seed4, size_t seed4_len,
    const void *seed5, size_t seed5_len, unsigned char *out, size_t out_len);

#define TLS_PRF_OUT_LEN 128

struct tls_prf_test {
	const unsigned char *desc;
	const SSL_METHOD *(*ssl_method)(void);
	const uint16_t cipher_value;
	const unsigned char out[TLS_PRF_OUT_LEN];
};

static const struct tls_prf_test tls_prf_tests[] = {
	{
		.desc = "SHA256",
		.ssl_method = TLSv1_2_method,
		.cipher_value = 0x0033,
		.out = {
			 0x37, 0xa7, 0x06, 0x71, 0x6e, 0x19, 0x19, 0xda,
			 0x23, 0x8c, 0xcc, 0xb4, 0x2f, 0x31, 0x64, 0x9d,
			 0x05, 0x29, 0x1c, 0x33, 0x7e, 0x09, 0x1b, 0x0c,
			 0x0e, 0x23, 0xc1, 0xb0, 0x40, 0xcc, 0x31, 0xf7,
			 0x55, 0x66, 0x68, 0xd9, 0xa8, 0xae, 0x74, 0x75,
			 0xf3, 0x46, 0xe9, 0x3a, 0x54, 0x9d, 0xe0, 0x8b,
			 0x7e, 0x6c, 0x63, 0x1c, 0xfa, 0x2f, 0xfd, 0xc9,
			 0xd3, 0xf1, 0xd3, 0xfe, 0x7b, 0x9e, 0x14, 0x95,
			 0xb5, 0xd0, 0xad, 0x9b, 0xee, 0x78, 0x8c, 0x83,
			 0x18, 0x58, 0x7e, 0xa2, 0x23, 0xc1, 0x8b, 0x62,
			 0x94, 0x12, 0xcb, 0xb6, 0x60, 0x69, 0x32, 0xfe,
			 0x98, 0x0e, 0x93, 0xb0, 0x8e, 0x5c, 0xfb, 0x6e,
			 0xdb, 0x9a, 0xc2, 0x9f, 0x8c, 0x5c, 0x43, 0x19,
			 0xeb, 0x4a, 0x52, 0xad, 0x62, 0x2b, 0xdd, 0x9f,
			 0xa3, 0x74, 0xa6, 0x96, 0x61, 0x4d, 0x98, 0x40,
			 0x63, 0xa6, 0xd4, 0xbb, 0x17, 0x11, 0x75, 0xed,
		},
	},
	{
		.desc = "SHA384",
		.ssl_method = TLSv1_2_method,
		.cipher_value = 0x009d,
		.out = {
			 0x00, 0x93, 0xc3, 0xfd, 0xa7, 0xbb, 0xdc, 0x5b,
			 0x13, 0x3a, 0xe6, 0x8b, 0x1b, 0xac, 0xf3, 0xfb,
			 0x3c, 0x9a, 0x78, 0xf6, 0x19, 0xf0, 0x13, 0x0f,
			 0x0d, 0x01, 0x9d, 0xdf, 0x0a, 0x28, 0x38, 0xce,
			 0x1a, 0x9b, 0x43, 0xbe, 0x56, 0x12, 0xa7, 0x16,
			 0x58, 0xe1, 0x8a, 0xe4, 0xc5, 0xbb, 0x10, 0x4c,
			 0x3a, 0xf3, 0x7f, 0xd3, 0xdb, 0xe4, 0xe0, 0x3d,
			 0xcc, 0x83, 0xca, 0xf0, 0xf9, 0x69, 0xcc, 0x70,
			 0x83, 0x32, 0xf6, 0xfc, 0x81, 0x80, 0x02, 0xe8,
			 0x31, 0x1e, 0x7c, 0x3b, 0x34, 0xf7, 0x34, 0xd1,
			 0xcf, 0x2a, 0xc4, 0x36, 0x2f, 0xe9, 0xaa, 0x7f,
			 0x6d, 0x1f, 0x5e, 0x0e, 0x39, 0x05, 0x15, 0xe1,
			 0xa2, 0x9a, 0x4d, 0x97, 0x8c, 0x62, 0x46, 0xf1,
			 0x87, 0x65, 0xd8, 0xe9, 0x14, 0x11, 0xa6, 0x48,
			 0xd7, 0x0e, 0x6e, 0x70, 0xad, 0xfb, 0x3f, 0x36,
			 0x05, 0x76, 0x4b, 0xe4, 0x28, 0x50, 0x4a, 0xf2,
		},
	},
};

#define N_TLS_PRF_TESTS \
    (sizeof(tls_prf_tests) / sizeof(*tls_prf_tests))

#define TLS_PRF_SEED1	"tls prf seed 1"
#define TLS_PRF_SEED2	"tls prf seed 2"
#define TLS_PRF_SEED3	"tls prf seed 3"
#define TLS_PRF_SEED4	"tls prf seed 4"
#define TLS_PRF_SEED5	"tls prf seed 5"
#define TLS_PRF_SECRET	"tls prf secretz"

static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}

static int
do_tls_prf_test(int test_no, const struct tls_prf_test *tpt)
{
	unsigned char *out = NULL;
	const SSL_CIPHER *cipher;
	SSL_CTX *ssl_ctx = NULL;
	SSL *ssl = NULL;
	int failure = 1;
	int len;

	fprintf(stderr, "Test %d - %s\n", test_no, tpt->desc);

	if ((out = malloc(TLS_PRF_OUT_LEN)) == NULL)
		errx(1, "failed to allocate out");

	if ((ssl_ctx = SSL_CTX_new(tpt->ssl_method())) == NULL)
		errx(1, "failed to create SSL context");
	if ((ssl = SSL_new(ssl_ctx)) == NULL)
		errx(1, "failed to create SSL context");

	if ((cipher = ssl3_get_cipher_by_value(tpt->cipher_value)) == NULL) {
		fprintf(stderr, "FAIL: no cipher %hx\n", tpt->cipher_value);
		goto failure;
	}

	ssl->s3->hs.cipher = cipher;

	for (len = 1; len <= TLS_PRF_OUT_LEN; len++) {
		memset(out, 'A', TLS_PRF_OUT_LEN);

		if (tls1_PRF(ssl, TLS_PRF_SECRET, sizeof(TLS_PRF_SECRET),
		    TLS_PRF_SEED1, sizeof(TLS_PRF_SEED1), TLS_PRF_SEED2,
		    sizeof(TLS_PRF_SEED2), TLS_PRF_SEED3, sizeof(TLS_PRF_SEED3),
		    TLS_PRF_SEED4, sizeof(TLS_PRF_SEED4), TLS_PRF_SEED5,
		    sizeof(TLS_PRF_SEED5), out, len) != 1) {
			fprintf(stderr, "FAIL: tls_PRF failed for len %d\n",
			    len);
			goto failure;
		}

		if (memcmp(out, tpt->out, len) != 0) {
			fprintf(stderr, "FAIL: tls_PRF output differs for "
			    "len %d\n", len);
			fprintf(stderr, "output:\n");
			hexdump(out, TLS_PRF_OUT_LEN);
			fprintf(stderr, "test data:\n");
			hexdump(tpt->out, TLS_PRF_OUT_LEN);
			fprintf(stderr, "\n");
			goto failure;
		}
	}

	failure = 0;

 failure:
	SSL_free(ssl);
	SSL_CTX_free(ssl_ctx);

	free(out);

	return failure;
}

int
main(int argc, char **argv)
{
	int failed = 0;
	size_t i;

	SSL_library_init();
	SSL_load_error_strings();

	for (i = 0; i < N_TLS_PRF_TESTS; i++)
		failed |= do_tls_prf_test(i, &tls_prf_tests[i]);

	return failed;
}
