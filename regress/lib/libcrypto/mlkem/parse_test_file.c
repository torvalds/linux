/*	$OpenBSD: parse_test_file.c,v 1.6 2025/06/03 10:29:37 tb Exp $ */

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

#include <sys/types.h>

#include <assert.h>
#include <err.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bytestring.h"

#include "parse_test_file.h"

struct line_data {
	uint8_t	*data;
	size_t	 data_len;
	CBS	 cbs;
	int	 val;
};

static struct line_data *
line_data_new(void)
{
	return calloc(1, sizeof(struct line_data));
}

static void
line_data_clear(struct line_data *ld)
{
	freezero(ld->data, ld->data_len);
	/* The dereference isn't enough for silly old gcc 14. */
	assert(ld != NULL);
	explicit_bzero(ld, sizeof(*ld));
}

static void
line_data_free(struct line_data *ld)
{
	if (ld == NULL)
		return;
	line_data_clear(ld);
	free(ld);
}

static void
line_data_get_int(struct line_data *ld, int *out)
{
	*out = ld->val;
}

static void
line_data_get_cbs(struct line_data *ld, CBS *out)
{
	CBS_dup(&ld->cbs, out);
}

static void
line_data_set_int(struct line_data *ld, int val)
{
	ld->val = val;
}

static int
line_data_set_from_cbb(struct line_data *ld, CBB *cbb)
{
	if (!CBB_finish(cbb, &ld->data, &ld->data_len))
		return 0;

	CBS_init(&ld->cbs, ld->data, ld->data_len);

	return 1;
}

struct parse_state {
	size_t line;
	size_t test;

	size_t max;
	size_t cur;
	struct line_data **data;

	size_t instruction_max;
	size_t instruction_cur;
	struct line_data **instruction_data;

	int running_test_case;
};

static void
parse_state_init(struct parse_state *ps, size_t max, size_t instruction_max)
{
	size_t i;

	assert(max > 0);

	memset(ps, 0, sizeof(*ps));
	ps->test = 1;

	ps->max = max;
	if ((ps->data = calloc(max, sizeof(*ps->data))) == NULL)
		err(1, NULL);
	for (i = 0; i < max; i++) {
		if ((ps->data[i] = line_data_new()) == NULL)
			err(1, NULL);
	}

	if ((ps->instruction_max = instruction_max) > 0) {
		if ((ps->instruction_data = calloc(instruction_max,
		    sizeof(*ps->instruction_data))) == NULL)
			err(1, NULL);
		for (i = 0; i < instruction_max; i++)
			if ((ps->instruction_data[i] = line_data_new()) == NULL)
				err(1, NULL);
	}
}

static void
parse_state_finish(struct parse_state *ps)
{
	size_t i;

	for (i = 0; i < ps->max; i++)
		line_data_free(ps->data[i]);
	free(ps->data);

	for (i = 0; i < ps->instruction_max; i++)
		line_data_free(ps->instruction_data[i]);
	free(ps->instruction_data);
}

static void
parse_state_new_line(struct parse_state *ps)
{
	ps->line++;
}

static void
parse_instruction_advance(struct parse_state *ps)
{
	assert(ps->instruction_cur < ps->instruction_max);
	ps->instruction_cur++;
}

static void
parse_state_advance(struct parse_state *ps)
{
	assert(ps->cur < ps->max);

	ps->cur++;
	if ((ps->cur %= ps->max) == 0)
		ps->test++;
}

struct parse {
	struct parse_state state;

	char	*buf;
	size_t	 buf_max;
	CBS	 cbs;

	const struct test_parse *tctx;
	void *ctx;

	const char *fn;
	FILE *fp;
};

static int
parse_instructions_parsed(struct parse *p)
{
	return p->state.instruction_max == p->state.instruction_cur;
}

static void
parse_advance(struct parse *p)
{
	if (!parse_instructions_parsed(p)) {
		parse_instruction_advance(&p->state);
		return;
	}
	parse_state_advance(&p->state);
}

static size_t
parse_max(struct parse *p)
{
	return p->state.max;
}

static size_t
parse_instruction_max(struct parse *p)
{
	return p->state.instruction_max;
}

static size_t
parse_cur(struct parse *p)
{
	if (!parse_instructions_parsed(p)) {
		assert(p->state.instruction_cur < p->state.instruction_max);
		return p->state.instruction_cur;
	}

	assert(p->state.cur < parse_max(p));
	return p->state.cur;
}

static size_t
parse_must_run_test_case(struct parse *p)
{
	return parse_instructions_parsed(p) && parse_max(p) - parse_cur(p) == 1;
}

static const struct line_spec *
parse_states(struct parse *p)
{
	if (!parse_instructions_parsed(p))
		return p->tctx->instructions;
	return p->tctx->states;
}

static const struct line_spec *
parse_instruction_states(struct parse *p)
{
	return p->tctx->instructions;
}

static const struct line_spec *
parse_state(struct parse *p)
{
	return &parse_states(p)[parse_cur(p)];
}

static size_t
line(struct parse *p)
{
	return p->state.line;
}

static size_t
test(struct parse *p)
{
	return p->state.test;
}

static const char *
name(struct parse *p)
{
	if (p->state.running_test_case)
		return "running test case";
	return parse_state(p)->name;
}

static const char *
label(struct parse *p)
{
	return parse_state(p)->label;
}

static const char *
match(struct parse *p)
{
	return parse_state(p)->match;
}

static enum line
parse_line_type(struct parse *p)
{
	return parse_state(p)->type;
}

static void
parse_vinfo(struct parse *p, const char *fmt, va_list ap)
{
	fprintf(stderr, "%s:%zu test #%zu (%s): ",
	    p->fn, line(p), test(p), name(p));
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
}

void
parse_info(struct parse *p, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	parse_vinfo(p, fmt, ap);
	va_end(ap);
}

void
parse_errx(struct parse *p, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	parse_vinfo(p, fmt, ap);
	va_end(ap);

	exit(1);
}

int
parse_length_equal(struct parse *p, const char *descr, size_t want, size_t got)
{
	if (want == got)
		return 1;

	parse_info(p, "%s length: want %zu, got %zu", descr, want, got);
	return 0;
}

static void
hexdump(const uint8_t *buf, size_t len, const uint8_t *compare)
{
	const char *mark = "", *newline;
	size_t i;

	for (i = 1; i <= len; i++) {
		if (compare != NULL)
			mark = (buf[i - 1] != compare[i - 1]) ? "*" : " ";
		newline = i % 8 ? "" : "\n";
		fprintf(stderr, " %s0x%02x,%s", mark, buf[i - 1], newline);
	}
	if ((len % 8) != 0)
		fprintf(stderr, "\n");
}

int
parse_data_equal(struct parse *p, const char *descr, CBS *want,
    const uint8_t *got, size_t got_len)
{
	if (!parse_length_equal(p, descr, CBS_len(want), got_len))
		return 0;
	if (CBS_mem_equal(want, got, got_len))
		return 1;

	parse_info(p, "%s differs", descr);
	fprintf(stderr, "want:\n");
	hexdump(CBS_data(want), CBS_len(want), got);
	fprintf(stderr, "got:\n");
	hexdump(got, got_len, CBS_data(want));
	fprintf(stderr, "\n");

	return 0;
}

static void
parse_line_data_clear(struct parse *p)
{
	size_t i;

	for (i = 0; i < parse_max(p); i++)
		line_data_clear(p->state.data[i]);
}

static struct line_data **
parse_state_data(struct parse *p)
{
	if (!parse_instructions_parsed(p))
		return p->state.instruction_data;
	return p->state.data;
}

static void
parse_state_set_int(struct parse *p, int val)
{
	if (parse_line_type(p) != LINE_STRING_MATCH)
		parse_errx(p, "%s: want %d, got %d", __func__,
		    LINE_STRING_MATCH, parse_line_type(p));
	line_data_set_int(parse_state_data(p)[parse_cur(p)], val);
}

static void
parse_state_set_from_cbb(struct parse *p, CBB *cbb)
{
	if (parse_line_type(p) != LINE_HEX)
		parse_errx(p, "%s: want %d, got %d", __func__,
		    LINE_HEX, parse_line_type(p));
	if (!line_data_set_from_cbb(parse_state_data(p)[parse_cur(p)], cbb))
		parse_errx(p, "line_data_set_from_cbb");
}

int
parse_get_int(struct parse *p, size_t idx, int *out)
{
	assert(parse_must_run_test_case(p));
	assert(idx < parse_max(p));
	assert(parse_states(p)[idx].type == LINE_STRING_MATCH);

	line_data_get_int(p->state.data[idx], out);

	return 1;
}

int
parse_get_cbs(struct parse *p, size_t idx, CBS *out)
{
	assert(parse_must_run_test_case(p));
	assert(idx < parse_max(p));
	assert(parse_states(p)[idx].type == LINE_HEX);

	line_data_get_cbs(p->state.data[idx], out);

	return 1;
}

int
parse_instruction_get_int(struct parse *p, size_t idx, int *out)
{
	assert(parse_must_run_test_case(p));
	assert(idx < parse_instruction_max(p));
	assert(parse_instruction_states(p)[idx].type == LINE_STRING_MATCH);

	line_data_get_int(p->state.instruction_data[idx], out);

	return 1;
}

int
parse_instruction_get_cbs(struct parse *p, size_t idx, CBS *out)
{
	assert(parse_must_run_test_case(p));
	assert(idx < parse_instruction_max(p));
	assert(parse_instruction_states(p)[idx].type == LINE_HEX);

	line_data_get_cbs(p->state.instruction_data[idx], out);

	return 1;
}

static void
parse_line_skip_to_end(struct parse *p)
{
	if (!CBS_skip(&p->cbs, CBS_len(&p->cbs)))
		parse_errx(p, "CBS_skip");
}

static int
CBS_peek_bytes(CBS *cbs, CBS *out, size_t len)
{
	CBS dup;

	CBS_dup(cbs, &dup);
	return CBS_get_bytes(&dup, out, len);
}

static int
parse_peek_string_cbs(struct parse *p, const char *str)
{
	CBS cbs;
	size_t len = strlen(str);

	if (!CBS_peek_bytes(&p->cbs, &cbs, len))
		parse_errx(p, "CBS_peek_bytes");

	return CBS_mem_equal(&cbs, (const uint8_t *)str, len);
}

static int
parse_get_string_cbs(struct parse *p, const char *str)
{
	CBS cbs;
	size_t len = strlen(str);

	if (!CBS_get_bytes(&p->cbs, &cbs, len))
		parse_errx(p, "CBS_get_bytes");

	return CBS_mem_equal(&cbs, (const uint8_t *)str, len);
}

static int
parse_get_string_end_cbs(struct parse *p, const char *str)
{
	CBS cbs;
	int equal = 1;

	CBS_init(&cbs, (const uint8_t *)str, strlen(str));

	if (CBS_len(&p->cbs) < CBS_len(&cbs))
		parse_errx(p, "line too short to match %s", str);

	while (CBS_len(&cbs) > 0) {
		uint8_t want, got;

		if (!CBS_get_last_u8(&cbs, &want))
			parse_errx(p, "CBS_get_last_u8");
		if (!CBS_get_last_u8(&p->cbs, &got))
			parse_errx(p, "CBS_get_last_u8");
		if (want != got)
			equal = 0;
	}

	return equal;
}

static void
parse_check_label_matches(struct parse *p)
{
	const char *sep = ": ";

	if (!parse_get_string_cbs(p, label(p)))
		parse_errx(p, "label mismatch %s", label(p));

	/* Now we expect either ": " or " = ". */
	if (!parse_peek_string_cbs(p, sep))
		sep = " = ";
	if (!parse_get_string_cbs(p, sep))
		parse_errx(p, "error getting \"%s\"", sep);
}

static int
parse_empty_or_comment_line(struct parse *p)
{
	if (CBS_len(&p->cbs) == 0) {
		return 1;
	}
	if (parse_peek_string_cbs(p, "#")) {
		parse_line_skip_to_end(p);
		return 1;
	}
	return 0;
}

static void
parse_string_match_line(struct parse *p)
{
	int string_matches;

	parse_check_label_matches(p);

	string_matches = parse_get_string_cbs(p, match(p));
	parse_state_set_int(p, string_matches);

	if (!string_matches)
		parse_line_skip_to_end(p);
}

static int
parse_get_hex_nibble_cbs(CBS *cbs, uint8_t *out_nibble)
{
	uint8_t c;

	if (!CBS_get_u8(cbs, &c))
		return 0;

	if (c >= '0' && c <= '9') {
		*out_nibble = c - '0';
		return 1;
	}
	if (c >= 'a' && c <= 'f') {
		*out_nibble = c - 'a' + 10;
		return 1;
	}
	if (c >= 'A' && c <= 'F') {
		*out_nibble = c - 'A' + 10;
		return 1;
	}

	return 0;
}

static void
parse_hex_line(struct parse *p)
{
	CBB cbb;

	parse_check_label_matches(p);

	if (!CBB_init(&cbb, 0))
		parse_errx(p, "CBB_init");

	while (CBS_len(&p->cbs) > 0) {
		uint8_t hi, lo;

		if (!parse_get_hex_nibble_cbs(&p->cbs, &hi))
			parse_errx(p, "parse_get_hex_nibble_cbs");
		if (!parse_get_hex_nibble_cbs(&p->cbs, &lo))
			parse_errx(p, "parse_get_hex_nibble_cbs");

		if (!CBB_add_u8(&cbb, hi << 4 | lo))
			parse_errx(p, "CBB_add_u8");
	}

	parse_state_set_from_cbb(p, &cbb);
}

static void
parse_maybe_prepare_instruction_line(struct parse *p)
{
	if (parse_instructions_parsed(p))
		return;

	/* Should not happen due to parse_empty_or_comment_line(). */
	if (CBS_len(&p->cbs) == 0)
		parse_errx(p, "empty instruction line");

	if (!parse_peek_string_cbs(p, "["))
		parse_errx(p, "expected instruction line");
	if (!parse_get_string_cbs(p, "["))
		parse_errx(p, "expected start of instruction line");
	if (!parse_get_string_end_cbs(p, "]"))
		parse_errx(p, "expected end of instruction line");
}

static void
parse_check_line_consumed(struct parse *p)
{
	if (CBS_len(&p->cbs) > 0)
		parse_errx(p, "%zu unprocessed bytes", CBS_len(&p->cbs));
}

static int
parse_run_test_case(struct parse *p)
{
	const struct test_parse *tctx = p->tctx;

	p->state.running_test_case = 1;
	return tctx->run_test_case(p->ctx);
}

static void
parse_reinit(struct parse *p)
{
	const struct test_parse *tctx = p->tctx;

	p->state.running_test_case = 0;
	parse_line_data_clear(p);
	tctx->finish(p->ctx);
	if (!tctx->init(p->ctx, p))
		parse_errx(p, "init failed");
}

static int
parse_maybe_run_test_case(struct parse *p)
{
	int failed = 0;

	if (parse_must_run_test_case(p)) {
		failed |= parse_run_test_case(p);
		parse_reinit(p);
	}

	parse_advance(p);

	return failed;
}

static int
parse_process_line(struct parse *p)
{
	if (parse_empty_or_comment_line(p))
		return 0;

	parse_maybe_prepare_instruction_line(p);

	switch (parse_line_type(p)) {
	case LINE_STRING_MATCH:
		parse_string_match_line(p);
		break;
	case LINE_HEX:
		parse_hex_line(p);
		break;
	default:
		parse_errx(p, "unknown line type %d", parse_line_type(p));
	}
	parse_check_line_consumed(p);

	return parse_maybe_run_test_case(p);
}

static void
parse_init(struct parse *p, const char *fn, const struct test_parse *tctx,
    void *ctx)
{
	FILE *fp;

	memset(p, 0, sizeof(*p));

	if ((fp = fopen(fn, "r")) == NULL)
		err(1, "error opening %s", fn);

	/* Poor man's basename since POSIX basename is stupid. */
	if ((p->fn = strrchr(fn, '/')) != NULL)
		p->fn++;
	else
		p->fn = fn;

	p->fp = fp;
	parse_state_init(&p->state, tctx->num_states, tctx->num_instructions);
	p->tctx = tctx;
	p->ctx = ctx;
	if (!tctx->init(p->ctx, p))
		parse_errx(p, "init failed");
}

static int
parse_next_line(struct parse *p)
{
	ssize_t len;
	uint8_t u8;

	if ((len = getline(&p->buf, &p->buf_max, p->fp)) == -1)
		return 0;

	CBS_init(&p->cbs, (const uint8_t *)p->buf, len);
	parse_state_new_line(&p->state);

	if (!CBS_get_last_u8(&p->cbs, &u8))
		parse_errx(p, "CBS_get_last_u8");

	assert(u8 == '\n');

	return 1;
}

static void
parse_finish(struct parse *p)
{
	const struct test_parse *tctx = p->tctx;

	parse_state_finish(&p->state);
	tctx->finish(p->ctx);

	free(p->buf);

	if (ferror(p->fp))
		err(1, "%s", p->fn);
	fclose(p->fp);
}

int
parse_test_file(const char *fn, const struct test_parse *tctx, void *ctx)
{
	struct parse p;
	int failed = 0;

	parse_init(&p, fn, tctx, ctx);

	while (parse_next_line(&p))
		failed |= parse_process_line(&p);

	parse_finish(&p);

	return failed;
}
