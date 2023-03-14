%parse-param {struct list_head *expr_head}
%define parse.error verbose

%{

#include <stdio.h>
#include <string.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include "bpf-filter.h"

static void perf_bpf_filter_error(struct list_head *expr __maybe_unused,
				  char const *msg)
{
	printf("perf_bpf_filter: %s\n", msg);
}

%}

%union
{
	unsigned long num;
	unsigned long sample;
	enum perf_bpf_filter_op op;
	struct perf_bpf_filter_expr *expr;
}

%token BFT_SAMPLE BFT_OP BFT_ERROR BFT_NUM
%type <expr> filter_term
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
BFT_SAMPLE BFT_OP BFT_NUM
{
	$$ = perf_bpf_filter_expr__new($1, $2, $3);
}

%%
