/*	$OpenBSD: test-seqnum.c,v 1.4 2025/09/10 06:28:20 tb Exp $ */

/*
 * Copyright (c) 2024 Theo Buehler <tb@openbsd.org>
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

#include <openssl/asn1.h>
#include <openssl/bn.h>

#include <err.h>
#include <stdio.h>
#include <unistd.h>

#include "extern.h"

#define MAX_DER 25

static const struct seqnum {
	const char *descr;
	const unsigned char der[MAX_DER];
	int der_len;
	int valid;
} seqnum_tests[] = {
	{
		.descr = "0 - smallest acceptable value:",
		.der = {
			0x02, 0x01, 0x00,
		},
		.der_len = 3,
		.valid = 1,
	},
	{
		.descr = "1 - acceptable:",
		.der = {
			0x02, 0x01, 0x01,
		},
		.der_len = 3,
		.valid = 1,
	},
	{
		.descr = "-1 - invalid:",
		.der = {
			0x02, 0x01, 0xff,
		},
		.der_len = 3,
		.valid = 0,
	},
	{
		.descr = "2^159 - 1 - largest acceptable value:",
		.der = {
			0x02, 0x14,
			0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff,
		},
		.der_len = 22,
		.valid = 1,
	},
	{
		.descr = "-2^159 - invalid, but fits in 20 octets:",
		.der = {
			0x02, 0x14,
			0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00,
		},
		.der_len = 22,
		.valid = 0,
	},
	{
		.descr = "2^159 - smallest inacceptable positive value:",
		.der = {
			0x02, 0x15,
			0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00,
		},
		.der_len = 23,
		.valid = 0,
	},
	{
		.descr = "2^160 - 1 - largest unsigned 20-bit number:",
		.der = {
			0x02, 0x15,
			0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
			0xff, 0xff, 0xff, 0xff, 0xff,
		},
		.der_len = 23,
		.valid = 0,
	},
	{
		.descr = "2^160: too large:",
		.der = {
			0x02, 0x15,
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00,
		},
		.der_len = 23,
		.valid = 0,
	},
};

#define N_SEQNUM_TESTS (sizeof(seqnum_tests) / sizeof(seqnum_tests[0]))

static int
seqnum_testcase(const struct seqnum *test)
{
	ASN1_INTEGER *aint = NULL;
	const unsigned char *p;
	char *s = NULL;
	int failed = 1;

	p = test->der;
	if ((aint = d2i_ASN1_INTEGER(NULL, &p, test->der_len)) == NULL) {
		fprintf(stderr, "FAIL: %s d2i_ASN1_INTEGER\n", test->descr);
		goto err;
	}

	s = x509_convert_seqnum(__func__, test->descr, aint);

	if (s == NULL && test->valid) {
		fprintf(stderr, "FAIL: %s failed to convert seqnum\n",
		    test->descr);
		goto err;
	}
	if (s != NULL && !test->valid) {
		fprintf(stderr, "FAIL: %s invalid seqnum succeeded\n",
		    test->descr);
		goto err;
	}

	failed = 0;
 err:
	ASN1_INTEGER_free(aint);
	free(s);

	return failed;
}

static int
seqnum_test(void)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_SEQNUM_TESTS; i++)
		failed |= seqnum_testcase(&seqnum_tests[i]);

	return failed;
}

time_t
get_current_time(void)
{
	return time(NULL);
}

int experimental, filemode, outformats, verbose;

int
main(void)
{
	int failed = 0;

	failed = seqnum_test();

	if (!failed)
		printf("OK\n");

	return failed;
}
