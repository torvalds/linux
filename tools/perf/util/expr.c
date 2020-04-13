// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <assert.h>
#include "expr.h"
#include "expr-bison.h"
#define YY_EXTRA_TYPE int
#include "expr-flex.h"

#ifdef PARSER_DEBUG
extern int expr_debug;
#endif

/* Caller must make sure id is allocated */
void expr__add_id(struct parse_ctx *ctx, const char *name, double val)
{
	int idx;

	assert(ctx->num_ids < MAX_PARSE_ID);
	idx = ctx->num_ids++;
	ctx->ids[idx].name = name;
	ctx->ids[idx].val = val;
}

void expr__ctx_init(struct parse_ctx *ctx)
{
	ctx->num_ids = 0;
}

static int
__expr__parse(double *val, struct parse_ctx *ctx, const char *expr,
	      int start)
{
	YY_BUFFER_STATE buffer;
	void *scanner;
	int ret;

	ret = expr_lex_init_extra(start, &scanner);
	if (ret)
		return ret;

	buffer = expr__scan_string(expr, scanner);

#ifdef PARSER_DEBUG
	expr_debug = 1;
#endif

	ret = expr_parse(val, ctx, scanner);

	expr__flush_buffer(buffer, scanner);
	expr__delete_buffer(buffer, scanner);
	expr_lex_destroy(scanner);
	return ret;
}

int expr__parse(double *final_val, struct parse_ctx *ctx, const char *expr)
{
	return __expr__parse(final_val, ctx, expr, EXPR_PARSE) ? -1 : 0;
}

static bool
already_seen(const char *val, const char *one, const char **other,
	     int num_other)
{
	int i;

	if (one && !strcasecmp(one, val))
		return true;
	for (i = 0; i < num_other; i++)
		if (!strcasecmp(other[i], val))
			return true;
	return false;
}

int expr__find_other(const char *expr, const char *one, const char ***other,
		     int *num_other)
{
	int err, i = 0, j = 0;
	struct parse_ctx ctx;

	expr__ctx_init(&ctx);
	err = __expr__parse(NULL, &ctx, expr, EXPR_OTHER);
	if (err)
		return -1;

	*other = malloc((ctx.num_ids + 1) * sizeof(char *));
	if (!*other)
		return -ENOMEM;

	for (i = 0, j = 0; i < ctx.num_ids; i++) {
		const char *str = ctx.ids[i].name;

		if (already_seen(str, one, *other, j))
			continue;

		str = strdup(str);
		if (!str)
			goto out;
		(*other)[j++] = str;
	}
	(*other)[j] = NULL;

out:
	if (i != ctx.num_ids) {
		while (--j)
			free((char *) (*other)[i]);
		free(*other);
		err = -1;
	}

	*num_other = j;
	return err;
}
