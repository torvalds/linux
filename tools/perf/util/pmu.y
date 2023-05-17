%define api.pure full
%parse-param {struct list_head *format}
%parse-param {char *name}
%parse-param {void *scanner}
%lex-param {void* scanner}

%{

#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/bitmap.h>
#include <string.h>
#include "pmu.h"

#define ABORT_ON(val) \
do { \
        if (val) \
                YYABORT; \
} while (0)

%}

%token PP_CONFIG
%token PP_VALUE PP_ERROR
%type <num> PP_VALUE
%type <bits> bit_term
%type <bits> bits

%union
{
	unsigned long num;
	DECLARE_BITMAP(bits, PERF_PMU_FORMAT_BITS);
}

%%

format:
format format_term
|
format_term

format_term:
PP_CONFIG ':' bits
{
	ABORT_ON(perf_pmu__new_format(format, name,
				      PERF_PMU_FORMAT_VALUE_CONFIG,
				      $3));
}
|
PP_CONFIG PP_VALUE ':' bits
{
	ABORT_ON(perf_pmu__new_format(format, name,
				      $2,
				      $4));
}

bits:
bits ',' bit_term
{
	bitmap_or($$, $1, $3, 64);
}
|
bit_term
{
	memcpy($$, $1, sizeof($1));
}

bit_term:
PP_VALUE '-' PP_VALUE
{
	perf_pmu__set_format($$, $1, $3);
}
|
PP_VALUE
{
	perf_pmu__set_format($$, $1, 0);
}

%%

void perf_pmu_error(struct list_head *list __maybe_unused,
		    char *name __maybe_unused,
		    void *scanner __maybe_unused,
		    char const *msg __maybe_unused)
{
}
