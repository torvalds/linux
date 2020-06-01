/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PARSE_CTX_H
#define PARSE_CTX_H 1

#define EXPR_MAX_OTHER 20
#define MAX_PARSE_ID EXPR_MAX_OTHER

struct expr_parse_id {
	const char *name;
	double val;
};

struct expr_parse_ctx {
	int num_ids;
	struct expr_parse_id ids[MAX_PARSE_ID];
};

struct expr_scanner_ctx {
	int start_token;
	int runtime;
};

void expr__ctx_init(struct expr_parse_ctx *ctx);
void expr__add_id(struct expr_parse_ctx *ctx, const char *id, double val);
int expr__parse(double *final_val, struct expr_parse_ctx *ctx, const char *expr, int runtime);
int expr__find_other(const char *expr, const char *one, const char ***other,
		int *num_other, int runtime);

#endif
