/* $OpenBSD: configtest.c,v 1.5 2024/08/02 16:02:35 tb Exp $ */
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
#include <stdio.h>
#include <stdlib.h>

#include <tls.h>

struct parse_protocols_test {
	const char *protostr;
	int want_return;
	uint32_t want_protocols;
};

struct parse_protocols_test parse_protocols_tests[] = {
	{
		.protostr = NULL,
		.want_return = 0,
		.want_protocols = TLS_PROTOCOLS_DEFAULT,
	},
	{
		.protostr = "default",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOLS_DEFAULT,
	},
	{
		.protostr = "secure",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOLS_DEFAULT,
	},
	{
		.protostr = "all",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOLS_ALL,
	},
	{
		.protostr = "tlsv1",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOL_TLSv1,
	},
	{
		.protostr = "tlsv1.2",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOL_TLSv1_2,
	},
	{
		.protostr = "tlsv1.3",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOL_TLSv1_3,
	},
	{
		.protostr = "",
		.want_return = -1,
		.want_protocols = 0,
	},
	{
		.protostr = "tlsv1.0:tlsv1.1:tlsv1.2:tlsv1.3",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOL_TLSv1_0 | TLS_PROTOCOL_TLSv1_1 |
		    TLS_PROTOCOL_TLSv1_2 | TLS_PROTOCOL_TLSv1_3,
	},
	{
		.protostr = "tlsv1.0,tlsv1.1,tlsv1.2,tlsv1.3",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOL_TLSv1_0 | TLS_PROTOCOL_TLSv1_1 |
		    TLS_PROTOCOL_TLSv1_2 | TLS_PROTOCOL_TLSv1_3,
	},
	{
		.protostr = "tlsv1.1,tlsv1.2,tlsv1.0",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOL_TLSv1_0 | TLS_PROTOCOL_TLSv1_1 |
		    TLS_PROTOCOL_TLSv1_2,
	},
	{
		.protostr = "tlsv1.1,tlsv1.2,tlsv1.1",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOL_TLSv1_1 | TLS_PROTOCOL_TLSv1_2,
	},
	{
		.protostr = "tlsv1.1,tlsv1.2,!tlsv1.1",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOL_TLSv1_2,
	},
	{
		.protostr = "unknown",
		.want_return = -1,
		.want_protocols = 0,
	},
	{
		.protostr = "all,!unknown",
		.want_return = -1,
		.want_protocols = 0,
	},
	{
		.protostr = "sslv3,tlsv1.0,tlsv1.1,tlsv1.2",
		.want_return = -1,
		.want_protocols = 0,
	},
	{
		.protostr = "all,!tlsv1.0",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOL_TLSv1_2 | TLS_PROTOCOL_TLSv1_3,
	},
	{
		.protostr = "!tlsv1.0",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOL_TLSv1_2 | TLS_PROTOCOL_TLSv1_3,
	},
	{
		.protostr = "!tlsv1.0,!tlsv1.1,!tlsv1.3",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOL_TLSv1_2,
	},
	{
		.protostr = "!tlsv1.0,!tlsv1.1,tlsv1.2,!tlsv1.3",
		.want_return = 0,
		.want_protocols = TLS_PROTOCOL_TLSv1_2,
	},
};

#define N_PARSE_PROTOCOLS_TESTS \
    (sizeof(parse_protocols_tests) / sizeof(*parse_protocols_tests))

static int
do_parse_protocols_test(int test_no, struct parse_protocols_test *ppt)
{
	uint32_t protocols = 0;
	int failed = 1;
	int rv;

	rv = tls_config_parse_protocols(&protocols, ppt->protostr);
	if (rv != ppt->want_return) {
		fprintf(stderr, "FAIL: test %i - tls_config_parse_protocols() "
		    "returned %i, want %i\n", test_no, rv, ppt->want_return);
		goto done;
	}
	if (protocols != ppt->want_protocols) {
		fprintf(stderr, "FAIL: test %i - got protocols 0x%x, "
		    "want 0x%x\n", test_no, protocols, ppt->want_protocols);
		goto done;
	}

	failed = 0;

 done:
	return (failed);
}

int
main(int argc, char **argv)
{
	int failed = 0;
	size_t i;

	tls_init();

	for (i = 0; i < N_PARSE_PROTOCOLS_TESTS; i++)
		failed += do_parse_protocols_test(i, &parse_protocols_tests[i]);

	return (failed);
}
