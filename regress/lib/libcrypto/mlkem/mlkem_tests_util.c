/*	$OpenBSD: mlkem_tests_util.c,v 1.10 2025/08/15 14:47:54 tb Exp $ */
/*
 * Copyright (c) 2024 Google Inc.
 * Copyright (c) 2024 Bob Beck <beck@obtuse.com>
 * Copyright (c) 2024 Theo Buehler <tb@openbsd.org>
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

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mlkem_tests_util.h"

static void
hexdump(const uint8_t *buf, size_t len, const uint8_t *compare)
{
	const char *mark = "";
	size_t i;

	for (i = 1; i <= len; i++) {
		if (compare != NULL)
			mark = (buf[i - 1] != compare[i - 1]) ? "*" : " ";
		fprintf(stderr, " %s0x%02hhx,%s", mark, buf[i - 1],
		    i % 8 && i != len ? "" : "\n");
	}
	fprintf(stderr, "\n");
}

int
compare_data(const uint8_t *want, const uint8_t *got, size_t len, const char *msg)
{
	if (memcmp(want, got, len) == 0)
		return 0;

	warnx("FAIL: %s differs", msg);
	fprintf(stderr, "want:\n");
	hexdump(want, len, got);
	fprintf(stderr, "got:\n");
	hexdump(got, len, want);
	fprintf(stderr, "\n");

	return 1;
}
