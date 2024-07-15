%parse-param {struct list_head *expr_head}
%define parse.error verbose

%{

#ifndef NDEBUG
#define YYDEBUG 1
#endif

#include <stdio.h>
#include <string.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include "bpf-filter.h"

int perf_bpf_filter_lex(void);

static void perf_bpf_filter_error(struct list_head *expr __maybe_unused,
				  char const *msg)
{
	printf("perf_bpf_filter: %s\n", msg);
}

%}

%union
{
	unsigned long num;
	struct {
		unsigned long type;
		int part;
	} sample;
	enum perf_bpf_filter_op op;
	struct perf_bpf_filter_expr *expr;
}

%token BFT_SAMPLE BFT_OP BFT_ERROR BFT_NUM BFT_LOGICAL_OR
%type <expr> filter_term filter_expr
%destructor { free ($$); } <expr>
%type <sample> BFT_SAMPLE
%type <op> BFT_OP
%type <num> BFT_NUM

%%

filter:
filter ',' filter_term
{
	list_add_tail(&$3->list, expr_head);
}
|
filter_term
{
	list_add_tail(&$1->list, expr_head);
}

filter_term:
filter_term BFT_LOGICAL_OR filter_expr
{
	struct perf_bpf_filter_expr *expr;

	if ($1->op == PBF_OP_GROUP_BEGIN) {
		expr = $1;
	} else {
		expr = perf_bpf_filter_expr__new(0, 0, PBF_OP_GROUP_BEGIN, 1);
		list_add_tail(&$1->list, &expr->groups);
	}
	expr->val++;
	list_add_tail(&$3->list, &expr->groups);
	$$ = expr;
}
|
filter_expr
{
	$$ = $1;
}

filter_expr:
BFT_SAMPLE BFT_OP BFT_NUM
{
	$$ = perf_bpf_filter_expr__new($1.type, $1.part, $2, $3);
}

%%
