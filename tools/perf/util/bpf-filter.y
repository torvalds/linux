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
#include "cgroup.h"

int perf_bpf_filter_lex(void);

/* To indicate if the current term needs a pathname or not */
int perf_bpf_filter_needs_path;

static void perf_bpf_filter_error(struct list_head *expr __maybe_unused,
				  char const *msg)
{
	printf("perf_bpf_filter: %s\n", msg);
}

%}

%union
{
	unsigned long num;
	char *path;
	struct {
		enum perf_bpf_filter_term term;
		int part;
	} sample;
	enum perf_bpf_filter_op op;
	struct perf_bpf_filter_expr *expr;
}

%token BFT_SAMPLE BFT_SAMPLE_PATH BFT_OP BFT_ERROR BFT_NUM BFT_LOGICAL_OR BFT_PATH
%type <expr> filter_term filter_expr
%destructor { free ($$); } <expr>
%type <sample> BFT_SAMPLE BFT_SAMPLE_PATH
%type <op> BFT_OP
%type <num> BFT_NUM
%type <path> BFT_PATH

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
		expr = perf_bpf_filter_expr__new(PBF_TERM_NONE, /*part=*/0,
						 PBF_OP_GROUP_BEGIN, /*val=*/1);
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
	$$ = perf_bpf_filter_expr__new($1.term, $1.part, $2, $3);
}
|
BFT_SAMPLE_PATH BFT_OP BFT_PATH
{
	struct cgroup *cgrp;
	unsigned long cgroup_id = 0;

	if ($2 != PBF_OP_EQ && $2 != PBF_OP_NEQ) {
		printf("perf_bpf_filter: cgroup accepts '==' or '!=' only\n");
		YYERROR;
	}

	cgrp = cgroup__new($3, /*do_open=*/false);
	if (cgrp && read_cgroup_id(cgrp) == 0)
		cgroup_id = cgrp->id;

	$$ = perf_bpf_filter_expr__new($1.term, $1.part, $2, cgroup_id);
	cgroup__put(cgrp);
}

%%
