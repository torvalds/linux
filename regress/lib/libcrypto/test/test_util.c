/*	$OpenBSD: test_util.c,v 1.1 2025/05/21 08:57:13 joshua Exp $ */
/*
 * Copyright (c) 2017 Joel Sing <jsing@openbsd.org>
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

#include <stdio.h>
#include <stdint.h>

#include "test.h"

void
test_hexdump(struct test *t, const unsigned char *buf, size_t len)
{
	size_t i;

	for (i = 1; i <= len; i++)
		test_printf(t, " 0x%02x,%s", buf[i - 1], i % 8 ? "" : "\n");

	if ((len % 8) != 0)
		test_printf(t, "\n");
}

void
test_hexdiff(struct test *t, const uint8_t *buf, size_t len, const uint8_t *compare)
{
	const char *mark = "", *newline;
	size_t i;

	for (i = 1; i <= len; i++) {
		if (compare != NULL)
			mark = (buf[i - 1] != compare[i - 1]) ? "*" : " ";
		newline = i % 8 ? "" : "\n";
		test_printf(t, " %s0x%02x,%s", mark, buf[i - 1], newline);
	}

	if ((len % 8) != 0)
		test_printf(t, "\n");
}
