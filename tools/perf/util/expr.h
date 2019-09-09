/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PARSE_CTX_H
#define PARSE_CTX_H 1

#define EXPR_MAX_OTHER 15
#define MAX_PARSE_ID EXPR_MAX_OTHER

struct parse_id {
	const char *name;
	double val;
};

struct parse_ctx {
	int num_ids;
	struct parse_id ids[MAX_PARSE_ID];
};

void expr__ctx_init(struct parse_ctx *ctx);
void expr__add_id(struct parse_ctx *ctx, const char *id, double val);
#ifndef IN_EXPR_Y
int expr__parse(double *final_val, struct parse_ctx *ctx, const char **pp);
#endif
int expr__find_other(const char *p, const char *one, const char ***other,
		int *num_other);

#endif
