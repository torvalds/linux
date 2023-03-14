%option prefix="perf_bpf_filter_"
%option noyywrap

%{
#include <stdio.h>
#include <stdlib.h>
#include <linux/perf_event.h>

#include "bpf-filter.h"
#include "bpf-filter-bison.h"

static int sample(unsigned long sample_flag)
{
	perf_bpf_filter_lval.sample.type = sample_flag;
	perf_bpf_filter_lval.sample.part = 0;
	return BFT_SAMPLE;
}

static int sample_part(unsigned long sample_flag, int part)
{
	perf_bpf_filter_lval.sample.type = sample_flag;
	perf_bpf_filter_lval.sample.part = part;
	return BFT_SAMPLE;
}

static int operator(enum perf_bpf_filter_op op)
{
	perf_bpf_filter_lval.op = op;
	return BFT_OP;
}

static int value(int base)
{
	long num;

	errno = 0;
	num = strtoul(perf_bpf_filter_text, NULL, base);
	if (errno)
		return BFT_ERROR;

	perf_bpf_filter_lval.num = num;
	return BFT_NUM;
}

static int error(const char *str)
{
	printf("perf_bpf_filter: Unexpected filter %s: %s\n", str, perf_bpf_filter_text);
	return BFT_ERROR;
}

%}

num_dec		[0-9]+
num_hex		0[Xx][0-9a-fA-F]+
space		[ \t]+
ident		[_a-zA-Z][_a-zA-Z0-9]+

%%

{num_dec}	{ return value(10); }
{num_hex}	{ return value(16); }
{space}		{ }

ip		{ return sample(PERF_SAMPLE_IP); }
id		{ return sample(PERF_SAMPLE_ID); }
tid		{ return sample(PERF_SAMPLE_TID); }
pid		{ return sample_part(PERF_SAMPLE_TID, 1); }
cpu		{ return sample(PERF_SAMPLE_CPU); }
time		{ return sample(PERF_SAMPLE_TIME); }
addr		{ return sample(PERF_SAMPLE_ADDR); }
period		{ return sample(PERF_SAMPLE_PERIOD); }
txn		{ return sample(PERF_SAMPLE_TRANSACTION); }
weight		{ return sample(PERF_SAMPLE_WEIGHT); }
weight1		{ return sample_part(PERF_SAMPLE_WEIGHT_STRUCT, 1); }
weight2		{ return sample_part(PERF_SAMPLE_WEIGHT_STRUCT, 2); }
weight3		{ return sample_part(PERF_SAMPLE_WEIGHT_STRUCT, 3); }
ins_lat		{ return sample_part(PERF_SAMPLE_WEIGHT_STRUCT, 2); } /* alias for weight2 */
p_stage_cyc	{ return sample_part(PERF_SAMPLE_WEIGHT_STRUCT, 3); } /* alias for weight3 */
retire_lat	{ return sample_part(PERF_SAMPLE_WEIGHT_STRUCT, 3); } /* alias for weight3 */
phys_addr	{ return sample(PERF_SAMPLE_PHYS_ADDR); }
code_pgsz	{ return sample(PERF_SAMPLE_CODE_PAGE_SIZE); }
data_pgsz	{ return sample(PERF_SAMPLE_DATA_PAGE_SIZE); }

"=="		{ return operator(PBF_OP_EQ); }
"!="		{ return operator(PBF_OP_NEQ); }
">"		{ return operator(PBF_OP_GT); }
"<"		{ return operator(PBF_OP_LT); }
">="		{ return operator(PBF_OP_GE); }
"<="		{ return operator(PBF_OP_LE); }
"&"		{ return operator(PBF_OP_AND); }

","		{ return ','; }

{ident}		{ return error("ident"); }
.		{ return error("input"); }

%%
