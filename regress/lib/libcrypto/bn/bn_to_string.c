/*	$OpenBSD: bn_to_string.c,v 1.5 2023/04/10 21:00:16 tb Exp $ */
/*
 * Copyright (c) 2019 Theo Buehler <tb@openbsd.org>
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/x509v3.h>

struct bn_to_string_tests {
	const char	*input;
	const char	*want;
} testcases[] = {
	{
		.input = "0x0",
		.want = "0",
	},
	{
		.input = "-0x0",
		.want = "0",
	},
	{
		.input = "0x7",
		.want = "7",
	},
	{
		.input = "-0x7",
		.want = "-7",
	},
	{
		.input = "0x8",
		.want = "8",
	},
	{
		.input = "-0x8",
		.want = "-8",
	},
	{
		.input = "0xF",
		.want = "15",
	},
	{
		.input = "-0xF",
		.want = "-15",
	},
	{
		.input = "0x10",
		.want = "16",
	},
	{
		.input = "-0x10",
		.want = "-16",
	},
	{
		.input = "0x7F",
		.want = "127",
	},
	{
		.input = "-0x7F",
		.want = "-127",
	},
	{
		.input = "0x80",
		.want = "128",
	},
	{
		.input = "-0x80",
		.want = "-128",
	},
	{
		.input = "0xFF",
		.want = "255",
	},
	{
		.input = "-0xFF",
		.want = "-255",
	},
	{
		.input = "0x100",
		.want = "256",
	},
	{
		.input = "0x7FFF",
		.want = "32767",
	},
	{
		.input = "-0x7FFF",
		.want = "-32767",
	},
	{
		.input = "0x8000",
		.want = "32768",
	},
	{
		.input = "-0x8000",
		.want = "-32768",
	},
	{
		.input = "0xFFFF",
		.want = "65535",
	},
	{
		.input = "-0xFFFF",
		.want = "-65535",
	},
	{
		.input = "0x10000",
		.want = "65536",
	},
	{
		.input = "-0x10000",
		.want = "-65536",
	},
	{
		.input = "0x7FFFFFFF",
		.want = "2147483647",
	},
	{
		.input = "-0x7FFFFFFF",
		.want = "-2147483647",
	},
	{
		.input = "0x80000000",
		.want = "2147483648",
	},
	{
		.input = "-0x80000000",
		.want = "-2147483648",
	},
	{
		.input = "0xFFFFFFFF",
		.want = "4294967295",
	},
	{
		.input = "-0xFFFFFFFF",
		.want = "-4294967295",
	},
	{
		.input = "0x100000000",
		.want = "4294967296",
	},
	{
		.input = "-0x100000000",
		.want = "-4294967296",
	},
	{
		.input = "0x7FFFFFFFFFFFFFFF",
		.want = "9223372036854775807",
	},
	{
		.input = "-0x7FFFFFFFFFFFFFFF",
		.want = "-9223372036854775807",
	},
	{
		.input = "0x8000000000000000",
		.want = "9223372036854775808",
	},
	{
		.input = "-0x8000000000000000",
		.want = "-9223372036854775808",
	},
	{
		.input = "0xFFFFFFFFFFFFFFFF",
		.want = "18446744073709551615",
	},
	{
		.input = "-0xFFFFFFFFFFFFFFFF",
		.want = "-18446744073709551615",
	},
	{
		.input = "0x10000000000000000",
		.want = "18446744073709551616",
	},
	{
		.input = "-0x10000000000000000",
		.want = "-18446744073709551616",
	},
	{
		.input = "0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
		.want = "170141183460469231731687303715884105727",
	},
	{
		.input = "-0x7FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
		.want = "-170141183460469231731687303715884105727",
	},
	{
		.input = "0x80000000000000000000000000000000",
		.want = "0x80000000000000000000000000000000",
	},
	{
		.input = "-0x80000000000000000000000000000000",
		.want = "-0x80000000000000000000000000000000",
	},
	{
		.input = "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
		.want = "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
	},
	{
		.input = "-0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
		.want = "-0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF",
	},
	{
		.input = "0x100000000000000000000000000000000",
		.want = "0x0100000000000000000000000000000000",
	},
	{
		.input = "-0x100000000000000000000000000000000",
		.want = "-0x0100000000000000000000000000000000",
	},
	{
		.input = NULL,
	},
};

int
main(void)
{
	struct bn_to_string_tests *test;
	ASN1_INTEGER *aint;
	char *got;
	int failed = 0;

	for (test = testcases; test->input != NULL; test++) {
		if ((aint = s2i_ASN1_INTEGER(NULL, test->input)) == NULL)
			errx(1, "s2i_ASN1_INTEGER(%s)", test->input);
		if ((got = i2s_ASN1_INTEGER(NULL, aint)) == NULL)
			errx(1, "i2s_ASN1_INTEGER(%s)", test->input);
		if (strcmp(got, test->want) != 0) {
			warnx("want: %s, got: %s", test->want, got);
			failed |= 1;
		}
		ASN1_INTEGER_free(aint);
		free(got);
	}

	return failed;
}
