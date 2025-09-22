/*	$OpenBSD: parse_test_file.h,v 1.1 2024/12/26 00:04:24 tb Exp $ */

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

#ifndef PARSE_TEST_FILE_H
#define PARSE_TEST_FILE_H

#include <stdint.h>
#include <stdio.h>

#include "bytestring.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct parse;

enum line {
	LINE_STRING_MATCH,	/* Checks if string after label matches. */
	LINE_HEX,		/* Parses hex into cbb from type2cbb. */
};

struct line_spec {
	int		 state;
	enum line	 type;
	const char	*name;
	const char	*label; /* followed by ": " or " = " */
	const char	*match;	/* only for LINE_STRING_MATCH */
};

struct test_parse {
	const struct line_spec *states;
	size_t num_states;

	const struct line_spec *instructions;
	size_t num_instructions;

	int (*init)(void *ctx, void *parse_ctx);
	void (*finish)(void *ctx);

	int (*run_test_case)(void *ctx);
};

int parse_test_file(const char *fn, const struct test_parse *lctx, void *ctx);

int parse_get_int(struct parse *p, size_t idx, int *out);
int parse_get_cbs(struct parse *p, size_t idx, CBS *out);

int parse_instruction_get_int(struct parse *p, size_t idx, int *out);
int parse_instruction_get_cbs(struct parse *p, size_t idx, CBS *out);

int parse_length_equal(struct parse *p, const char *descr, size_t want, size_t got);
int parse_data_equal(struct parse *p, const char *descr, CBS *want,
    const uint8_t *got, size_t len);

void parse_info(struct parse *ctx, const char *fmt, ...)
    __attribute__((__format__ (printf, 2, 3)))
    __attribute__((__nonnull__ (2)));
void parse_errx(struct parse *ctx, const char *fmt, ...)
    __attribute__((__format__ (printf, 2, 3)))
    __attribute__((__nonnull__ (2)))
    __attribute__((__noreturn__));

#ifdef __cplusplus
}
#endif

#endif /* PARSE_TEST_FILE_H */
