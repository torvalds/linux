// SPDX-License-Identifier: GPL-2.0
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "expr.h"
#include "expr-bison.h"
#include "expr-flex.h"
#include <linux/kernel.h>

#ifdef PARSER_DEBUG
extern int expr_debug;
#endif

static size_t key_hash(const void *key, void *ctx __maybe_unused)
{
	const char *str = (const char *)key;
	size_t hash = 0;

	while (*str != '\0') {
		hash *= 31;
		hash += *str;
		str++;
	}
	return hash;
}

static bool key_equal(const void *key1, const void *key2,
		    void *ctx __maybe_unused)
{
	return !strcmp((const char *)key1, (const char *)key2);
}

/* Caller must make sure id is allocated */
int expr__add_id_val(struct expr_parse_ctx *ctx, const char *id, double val)
{
	struct expr_id_data *data_ptr = NULL, *old_data = NULL;
	char *old_key = NULL;
	int ret;

	if (val != 0.0) {
		data_ptr = malloc(sizeof(*data_ptr));
		if (!data_ptr)
			return -ENOMEM;
		data_ptr->val = val;
	}
	ret = hashmap__set(&ctx->ids, id, data_ptr,
			   (const void **)&old_key, (void **)&old_data);
	free(old_key);
	free(old_data);
	return ret;
}

int expr__get_id(struct expr_parse_ctx *ctx, const char *id, double *val_ptr)
{
	struct expr_id_data *data;

	if (!hashmap__find(&ctx->ids, id, (void **)&data))
		return -1;
	*val_ptr = (data == NULL) ?  0.0 : data->val;
	return 0;
}

void expr__ctx_init(struct expr_parse_ctx *ctx)
{
	hashmap__init(&ctx->ids, key_hash, key_equal, NULL);
}

void expr__ctx_clear(struct expr_parse_ctx *ctx)
{
	struct hashmap_entry *cur;
	size_t bkt;

	hashmap__for_each_entry((&ctx->ids), cur, bkt) {
		free((char *)cur->key);
		free(cur->value);
	}
	hashmap__clear(&ctx->ids);
}

static int
__expr__parse(double *val, struct expr_parse_ctx *ctx, const char *expr,
	      int start, int runtime)
{
	struct expr_scanner_ctx scanner_ctx = {
		.start_token = start,
		.runtime = runtime,
	};
	YY_BUFFER_STATE buffer;
	void *scanner;
	int ret;

	ret = expr_lex_init_extra(&scanner_ctx, &scanner);
	if (ret)
		return ret;

	buffer = expr__scan_string(expr, scanner);

#ifdef PARSER_DEBUG
	expr_debug = 1;
	expr_set_debug(1, scanner);
#endif

	ret = expr_parse(val, ctx, scanner);

	expr__flush_buffer(buffer, scanner);
	expr__delete_buffer(buffer, scanner);
	expr_lex_destroy(scanner);
	return ret;
}

int expr__parse(double *final_val, struct expr_parse_ctx *ctx,
		const char *expr, int runtime)
{
	return __expr__parse(final_val, ctx, expr, EXPR_PARSE, runtime) ? -1 : 0;
}

int expr__find_other(const char *expr, const char *one,
		     struct expr_parse_ctx *ctx, int runtime)
{
	struct expr_id_data *old_val = NULL;
	char *old_key = NULL;
	int ret = __expr__parse(NULL, ctx, expr, EXPR_OTHER, runtime);

	if (one) {
		hashmap__delete(&ctx->ids, one,
				(const void **)&old_key, (void **)&old_val);
		free(old_key);
		free(old_val);
	}

	return ret;
}
