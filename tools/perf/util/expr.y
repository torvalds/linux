/* Simple expression parser */
%{
#define YYDEBUG 1
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include "util/debug.h"
#define IN_EXPR_Y 1
#include "expr.h"
%}

%define api.pure full

%parse-param { double *final_val }
%parse-param { struct expr_parse_ctx *ctx }
%parse-param { bool compute_ids }
%parse-param {void *scanner}
%lex-param {void* scanner}

%union {
	double	 num;
	char	*str;
	struct ids {
		/*
		 * When creating ids, holds the working set of event ids. NULL
		 * implies the set is empty.
		 */
		struct hashmap *ids;
		/*
		 * The metric value. When not creating ids this is the value
		 * read from a counter, a constant or some computed value. When
		 * creating ids the value is either a constant or BOTTOM. NAN is
		 * used as the special BOTTOM value, representing a "set of all
		 * values" case.
		 */
		double val;
	} ids;
}

%token ID NUMBER MIN MAX IF ELSE LITERAL D_RATIO SOURCE_COUNT EXPR_ERROR
%left MIN MAX IF
%left '|'
%left '^'
%left '&'
%left '<' '>'
%left '-' '+'
%left '*' '/' '%'
%left NEG NOT
%type <num> NUMBER LITERAL
%type <str> ID
%destructor { free ($$); } <str>
%type <ids> expr if_expr
%destructor { ids__free($$.ids); } <ids>

%{
static void expr_error(double *final_val __maybe_unused,
		       struct expr_parse_ctx *ctx __maybe_unused,
		       bool compute_ids __maybe_unused,
		       void *scanner,
		       const char *s)
{
	pr_debug("%s\n", s);
}

/*
 * During compute ids, the special "bottom" value uses NAN to represent the set
 * of all values. NAN is selected as it isn't a useful constant value.
 */
#define BOTTOM NAN

/* During computing ids, does val represent a constant (non-BOTTOM) value? */
static bool is_const(double val)
{
	return isfinite(val);
}

static struct ids union_expr(struct ids ids1, struct ids ids2)
{
	struct ids result = {
		.val = BOTTOM,
		.ids = ids__union(ids1.ids, ids2.ids),
	};
	return result;
}

static struct ids handle_id(struct expr_parse_ctx *ctx, char *id,
			    bool compute_ids, bool source_count)
{
	struct ids result;

	if (!compute_ids) {
		/*
		 * Compute the event's value from ID. If the ID isn't known then
		 * it isn't used to compute the formula so set to NAN.
		 */
		struct expr_id_data *data;

		result.val = NAN;
		if (expr__resolve_id(ctx, id, &data) == 0) {
			result.val = source_count
				? expr_id_data__source_count(data)
				: expr_id_data__value(data);
		}
		result.ids = NULL;
		free(id);
	} else {
		/*
		 * Set the value to BOTTOM to show that any value is possible
		 * when the event is computed. Create a set of just the ID.
		 */
		result.val = BOTTOM;
		result.ids = ids__new();
		if (!result.ids || ids__insert(result.ids, id)) {
			pr_err("Error creating IDs for '%s'", id);
			free(id);
		}
	}
	return result;
}

/*
 * If we're not computing ids or $1 and $3 are constants, compute the new
 * constant value using OP. Its invariant that there are no ids.  If computing
 * ids for non-constants union the set of IDs that must be computed.
 */
#define BINARY_LONG_OP(RESULT, OP, LHS, RHS)				\
	if (!compute_ids || (is_const(LHS.val) && is_const(RHS.val))) { \
		assert(LHS.ids == NULL);				\
		assert(RHS.ids == NULL);				\
		if (isnan(LHS.val) || isnan(RHS.val)) {			\
			RESULT.val = NAN;				\
		} else {						\
			RESULT.val = (long)LHS.val OP (long)RHS.val;	\
		}							\
		RESULT.ids = NULL;					\
	} else {							\
	        RESULT = union_expr(LHS, RHS);				\
	}

#define BINARY_OP(RESULT, OP, LHS, RHS)					\
	if (!compute_ids || (is_const(LHS.val) && is_const(RHS.val))) { \
		assert(LHS.ids == NULL);				\
		assert(RHS.ids == NULL);				\
		if (isnan(LHS.val) || isnan(RHS.val)) {			\
			RESULT.val = NAN;				\
		} else {						\
			RESULT.val = LHS.val OP RHS.val;		\
		}							\
		RESULT.ids = NULL;					\
	} else {							\
	        RESULT = union_expr(LHS, RHS);				\
	}

%}
%%

start: if_expr
{
	if (compute_ids)
		ctx->ids = ids__union($1.ids, ctx->ids);

	if (final_val)
		*final_val = $1.val;
}
;

if_expr: expr IF expr ELSE if_expr
{
	if (fpclassify($3.val) == FP_ZERO) {
		/*
		 * The IF expression evaluated to 0 so treat as false, take the
		 * ELSE and discard everything else.
		 */
		$$.val = $5.val;
		$$.ids = $5.ids;
		ids__free($1.ids);
		ids__free($3.ids);
	} else if (!compute_ids || is_const($3.val)) {
		/*
		 * If ids aren't computed then treat the expression as true. If
		 * ids are being computed and the IF expr is a non-zero
		 * constant, then also evaluate the true case.
		 */
		$$.val = $1.val;
		$$.ids = $1.ids;
		ids__free($3.ids);
		ids__free($5.ids);
	} else if ($1.val == $5.val) {
		/*
		 * LHS == RHS, so both are an identical constant. No need to
		 * evaluate any events.
		 */
		$$.val = $1.val;
		$$.ids = NULL;
		ids__free($1.ids);
		ids__free($3.ids);
		ids__free($5.ids);
	} else {
		/*
		 * Value is either the LHS or RHS and we need the IF expression
		 * to compute it.
		 */
		$$ = union_expr($1, union_expr($3, $5));
	}
}
| expr
;

expr: NUMBER
{
	$$.val = $1;
	$$.ids = NULL;
}
| ID				{ $$ = handle_id(ctx, $1, compute_ids, /*source_count=*/false); }
| SOURCE_COUNT '(' ID ')'	{ $$ = handle_id(ctx, $3, compute_ids, /*source_count=*/true); }
| expr '|' expr { BINARY_LONG_OP($$, |, $1, $3); }
| expr '&' expr { BINARY_LONG_OP($$, &, $1, $3); }
| expr '^' expr { BINARY_LONG_OP($$, ^, $1, $3); }
| expr '<' expr { BINARY_OP($$, <, $1, $3); }
| expr '>' expr { BINARY_OP($$, >, $1, $3); }
| expr '+' expr { BINARY_OP($$, +, $1, $3); }
| expr '-' expr { BINARY_OP($$, -, $1, $3); }
| expr '*' expr { BINARY_OP($$, *, $1, $3); }
| expr '/' expr
{
	if (fpclassify($3.val) == FP_ZERO) {
		pr_debug("division by zero\n");
		YYABORT;
	} else if (!compute_ids || (is_const($1.val) && is_const($3.val))) {
		assert($1.ids == NULL);
		assert($3.ids == NULL);
		$$.val = $1.val / $3.val;
		$$.ids = NULL;
	} else {
		/* LHS and/or RHS need computing from event IDs so union. */
		$$ = union_expr($1, $3);
	}
}
| expr '%' expr
{
	if (fpclassify($3.val) == FP_ZERO) {
		pr_debug("division by zero\n");
		YYABORT;
	} else if (!compute_ids || (is_const($1.val) && is_const($3.val))) {
		assert($1.ids == NULL);
		assert($3.ids == NULL);
		$$.val = (long)$1.val % (long)$3.val;
		$$.ids = NULL;
	} else {
		/* LHS and/or RHS need computing from event IDs so union. */
		$$ = union_expr($1, $3);
	}
}
| D_RATIO '(' expr ',' expr ')'
{
	if (fpclassify($5.val) == FP_ZERO) {
		/*
		 * Division by constant zero always yields zero and no events
		 * are necessary.
		 */
		assert($5.ids == NULL);
		$$.val = 0.0;
		$$.ids = NULL;
		ids__free($3.ids);
	} else if (!compute_ids || (is_const($3.val) && is_const($5.val))) {
		assert($3.ids == NULL);
		assert($5.ids == NULL);
		$$.val = $3.val / $5.val;
		$$.ids = NULL;
	} else {
		/* LHS and/or RHS need computing from event IDs so union. */
		$$ = union_expr($3, $5);
	}
}
| '-' expr %prec NEG
{
	$$.val = -$2.val;
	$$.ids = $2.ids;
}
| '(' if_expr ')'
{
	$$ = $2;
}
| MIN '(' expr ',' expr ')'
{
	if (!compute_ids) {
		$$.val = $3.val < $5.val ? $3.val : $5.val;
		$$.ids = NULL;
	} else {
		$$ = union_expr($3, $5);
	}
}
| MAX '(' expr ',' expr ')'
{
	if (!compute_ids) {
		$$.val = $3.val > $5.val ? $3.val : $5.val;
		$$.ids = NULL;
	} else {
		$$ = union_expr($3, $5);
	}
}
| LITERAL
{
	$$.val = $1;
	$$.ids = NULL;
}
;

%%
