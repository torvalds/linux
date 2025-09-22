/*	$OpenBSD: rsa_padding_test.c,v 1.2 2024/03/30 02:20:39 jsing Exp $	*/
/*
 * Copyright (c) 2024 Joel Sing <jsing@openbsd.org>
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

#include <stdint.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/rsa.h>

#if 0
static void
hexdump(const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		fprintf(stderr, " 0x%02hhx,%s", buf[i - 1], i % 8 ? "" : "\n");

	fprintf(stderr, "\n");
}
#endif

struct pkcs1_test {
	uint8_t in[128];
	size_t in_len;
	int want;
	int want_error;
};

static const struct pkcs1_test pkcs1_type1_tests[] = {
	{
		.in = {
			0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0x00, 0x6f, 0x6f, 0x6f,
			0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f,
			0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f,
		},
		.in_len = 32,
		.want = 19,
	},
	{
		.in = {
			0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x00,
		},
		.in_len = 11,
		.want = 0,
	},
	{
		.in = {
			0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x00, 0xff,
		},
		.in_len = 12,
		.want = 1,
	},
	{
		/* Insufficient padding bytes (< 8). */
		.in = {
			0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0x00, 0xff, 0xff,
		},
		.in_len = 12,
		.want = -1,
		.want_error = RSA_R_BAD_PAD_BYTE_COUNT,
	},
	{
		/* Incorrect padding type (0x00). */
		.in = {
			0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x00, 0xff,
		},
		.in_len = 12,
		.want = -1,
		.want_error = RSA_R_BLOCK_TYPE_IS_NOT_01,
	},
	{
		/* Incorrect padding type (0x02). */
		.in = {
			0x00, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x00, 0xff,
		},
		.in_len = 12,
		.want = -1,
		.want_error = RSA_R_BLOCK_TYPE_IS_NOT_01,
	},
	{
		/* Non-padding byte before end of padding marker. */
		.in = {
			0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xfe, 0x00, 0xff,
		},
		.in_len = 12,
		.want = -1,
		.want_error = RSA_R_BAD_FIXED_HEADER_DECRYPT,
	},
	{
		/* No end of padding marker. */
		.in = {
			0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		},
		.in_len = 32,
		.want = -1,
		.want_error = RSA_R_NULL_BEFORE_BLOCK_MISSING,
	},
};

#define N_PKCS1_TYPE1_TESTS \
	(sizeof(pkcs1_type1_tests) / sizeof(pkcs1_type1_tests[0]))

static int
test_pkcs1_type1(void)
{
	const struct pkcs1_test *pt;
	uint8_t buf[32], in[19], out[512];
	int pad_len;
	long err;
	size_t i;
	int failed = 1;

	for (i = 0; i < 1000; i++) {
		arc4random_buf(in, sizeof(in));

		if (!RSA_padding_add_PKCS1_type_1(buf, sizeof(buf), in,
		    sizeof(in))) {
			fprintf(stderr, "FAIL: failed to add PKCS1 type 1 "
			    "padding\n");
			goto failed;
		}

		pad_len = RSA_padding_check_PKCS1_type_1(out, sizeof(out) - 1,
		    buf + 1, sizeof(buf) - 1, sizeof(buf));
		if (pad_len != sizeof(in)) {
			fprintf(stderr, "FAIL: failed to check PKCS1 type 1 "
			    "padding\n");
			ERR_print_errors_fp(stderr);
			goto failed;
		}
	}

	for (i = 0; i < N_PKCS1_TYPE1_TESTS; i++) {
		pt = &pkcs1_type1_tests[i];

		ERR_clear_error();

		pad_len = RSA_padding_check_PKCS1_type_1(out, sizeof(out) - 1,
		    pt->in + 1, pt->in_len - 1, pt->in_len);

		if (pad_len != pt->want) {
			fprintf(stderr, "FAIL: test %zu - failed to check "
			    "PKCS1 type 1 padding (%d != %d)\n", i, pad_len,
			    pt->want);
			ERR_print_errors_fp(stderr);
			goto failed;
		}

		err = ERR_peek_error();
		if (pt->want == -1 && ERR_GET_REASON(err) != pt->want_error) {
			fprintf(stderr, "FAIL: test %zu - PKCS1 type 1 padding "
			    "check failed with error reason %i, want %i\n",
			    i, ERR_GET_REASON(err), pt->want_error);
			ERR_print_errors_fp(stderr);
			goto failed;
		}
	}

	failed = 0;

 failed:
	return failed;
}

static const struct pkcs1_test pkcs1_type2_tests[] = {
	{
		.in = {
			0x00, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0x00, 0x6f, 0x6f, 0x6f,
			0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f,
			0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f,
		},
		.in_len = 32,
		.want = 19,
	},
	{
		.in = {
			0x00, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x00,
		},
		.in_len = 11,
		.want = 0,
	},
	{
		.in = {
			0x00, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x00, 0xff,
		},
		.in_len = 12,
		.want = 1,
	},
	{
		/* Insufficient padding bytes (< 8). */
		.in = {
			0x00, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0x00, 0xff, 0xff,
		},
		.in_len = 12,
		.want = -1,
		.want_error = RSA_R_BAD_PAD_BYTE_COUNT,
	},
	{
		/* Incorrect padding type (0x00). */
		.in = {
			0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x00, 0xff,
		},
		.in_len = 12,
		.want = -1,
		.want_error = RSA_R_BLOCK_TYPE_IS_NOT_02,
	},
	{
		/* Incorrect padding type (0x01). */
		.in = {
			0x00, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0x00, 0xff,
		},
		.in_len = 12,
		.want = -1,
		.want_error = RSA_R_BLOCK_TYPE_IS_NOT_02,
	},
	{
		/* No end of padding marker. */
		.in = {
			0x00, 0x02, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0x6f, 0x6f, 0x6f,
			0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f,
			0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f, 0x6f,
		},
		.in_len = 32,
		.want = -1,
		.want_error = RSA_R_NULL_BEFORE_BLOCK_MISSING,
	},
};

#define N_PKCS1_TYPE2_TESTS \
	(sizeof(pkcs1_type2_tests) / sizeof(pkcs1_type2_tests[0]))

static int
test_pkcs1_type2(void)
{
	const struct pkcs1_test *pt;
	uint8_t buf[32], in[19], out[512];
	int pad_len;
	long err;
	size_t i;
	int failed = 1;

	for (i = 0; i < 1000; i++) {
		arc4random_buf(in, sizeof(in));

		if (!RSA_padding_add_PKCS1_type_2(buf, sizeof(buf), in,
		    sizeof(in))) {
			fprintf(stderr, "FAIL: failed to add PKCS1 type 2 "
			    "padding\n");
			goto failed;
		}

		pad_len = RSA_padding_check_PKCS1_type_2(out, sizeof(out) - 1,
		    buf + 1, sizeof(buf) - 1, sizeof(buf));
		if (pad_len != sizeof(in)) {
			fprintf(stderr, "FAIL: failed to check PKCS1 type 2 "
			    "padding\n");
			ERR_print_errors_fp(stderr);
			goto failed;
		}
	}

	for (i = 0; i < N_PKCS1_TYPE2_TESTS; i++) {
		pt = &pkcs1_type2_tests[i];

		ERR_clear_error();

		pad_len = RSA_padding_check_PKCS1_type_2(out, sizeof(out) - 1,
		    pt->in + 1, pt->in_len - 1, pt->in_len);

		if (pad_len != pt->want) {
			fprintf(stderr, "FAIL: test %zu - failed to check "
			    "PKCS1 type 2 padding (%d != %d)\n", i, pad_len,
			    pt->want);
			ERR_print_errors_fp(stderr);
			goto failed;
		}

		err = ERR_peek_error();
		if (pt->want == -1 && ERR_GET_REASON(err) != pt->want_error) {
			fprintf(stderr, "FAIL: test %zu - PKCS1 type 2 padding "
			    "check failed with error reason %i, want %i\n",
			    i, ERR_GET_REASON(err), pt->want_error);
			ERR_print_errors_fp(stderr);
			goto failed;
		}
	}

	failed = 0;

 failed:
	return failed;
}

int
main(int argc, char **argv)
{
	int failed = 0;

	failed |= test_pkcs1_type1();
	failed |= test_pkcs1_type2();

	return failed;
}
