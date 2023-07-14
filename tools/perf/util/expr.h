/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PARSE_CTX_H
#define PARSE_CTX_H 1

struct hashmap;
struct metric_ref;

struct expr_scanner_ctx {
	char *user_requested_cpu_list;
	int runtime;
	bool system_wide;
	bool is_test;
};

struct expr_parse_ctx {
	struct hashmap	*ids;
	struct expr_scanner_ctx sctx;
};

struct expr_id_data;

struct hashmap *ids__new(void);
void ids__free(struct hashmap *ids);
int ids__insert(struct hashmap *ids, const char *id);
/*
 * Union two sets of ids (hashmaps) and construct a third, freeing ids1 and
 * ids2.
 */
struct hashmap *ids__union(struct hashmap *ids1, struct hashmap *ids2);

struct expr_parse_ctx *expr__ctx_new(void);
void expr__ctx_clear(struct expr_parse_ctx *ctx);
void expr__ctx_free(struct expr_parse_ctx *ctx);

void expr__del_id(struct expr_parse_ctx *ctx, const char *id);
int expr__add_id(struct expr_parse_ctx *ctx, const char *id);
int expr__add_id_val(struct expr_parse_ctx *ctx, const char *id, double val);
int expr__add_id_val_source_count(struct expr_parse_ctx *ctx, const char *id,
				double val, int source_count);
int expr__add_ref(struct expr_parse_ctx *ctx, struct metric_ref *ref);
int expr__get_id(struct expr_parse_ctx *ctx, const char *id,
		 struct expr_id_data **data);
bool expr__subset_of_ids(struct expr_parse_ctx *haystack,
			 struct expr_parse_ctx *needles);
int expr__resolve_id(struct expr_parse_ctx *ctx, const char *id,
		     struct expr_id_data **datap);

int expr__parse(double *final_val, struct expr_parse_ctx *ctx,
		const char *expr);

int expr__find_ids(const char *expr, const char *one,
		   struct expr_parse_ctx *ids);

double expr_id_data__value(const struct expr_id_data *data);
double expr_id_data__source_count(const struct expr_id_data *data);
double expr__get_literal(const char *literal, const struct expr_scanner_ctx *ctx);
double expr__has_event(const struct expr_parse_ctx *ctx, bool compute_ids, const char *id);

#endif
