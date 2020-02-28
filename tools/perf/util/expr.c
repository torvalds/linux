// SPDX-License-Identifier: GPL-2.0
#include <assert.h>
#include "expr.h"

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
