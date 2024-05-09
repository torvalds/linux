%define api.pure full
%parse-param {void *format}
%parse-param {void *scanner}
%lex-param {void* scanner}

%{

#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/bitmap.h>
#include <string.h>
#include "pmu.h"
#include "pmu-bison.h"

int perf_pmu_lex(YYSTYPE * yylval_param , void *yyscanner);

#define ABORT_ON(val) \
do { \
        if (val) \
                YYABORT; \
} while (0)

static void perf_pmu_error(void *format, void *scanner, const char *msg);

static void perf_pmu__set_format(unsigned long *bits, long from, long to)
{
	long b;

	if (!to)
		to = from;

	memset(bits, 0, BITS_TO_BYTES(PERF_PMU_FORMAT_BITS));
	for (b = from; b <= to; b++)
		__set_bit(b, bits);
}

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
	perf_pmu_format__set_value(format, PERF_PMU_FORMAT_VALUE_CONFIG, $3);
}
|
PP_CONFIG PP_VALUE ':' bits
{
	perf_pmu_format__set_value(format, $2, $4);
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

static void perf_pmu_error(void *format __maybe_unused,
			   void *scanner __maybe_unused,
			   const char *msg __maybe_unused)
{
}
