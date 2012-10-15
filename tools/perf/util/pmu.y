
%name-prefix "perf_pmu_"
%parse-param {struct list_head *format}
%parse-param {char *name}

%{

#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/bitmap.h>
#include <string.h>
#include "pmu.h"

extern int perf_pmu_lex (void);

#define ABORT_ON(val) \
do { \
        if (val) \
                YYABORT; \
} while (0)

%}

%token PP_CONFIG PP_CONFIG1 PP_CONFIG2
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
PP_CONFIG1 ':' bits
{
	ABORT_ON(perf_pmu__new_format(format, name,
				      PERF_PMU_FORMAT_VALUE_CONFIG1,
				      $3));
}
|
PP_CONFIG2 ':' bits
{
	ABORT_ON(perf_pmu__new_format(format, name,
				      PERF_PMU_FORMAT_VALUE_CONFIG2,
				      $3));
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
		    char const *msg __maybe_unused)
{
}
