%option prefix="perf_bpf_filter_"
%option noyywrap

%{
#include <stdio.h>
#include <stdlib.h>
#include <linux/perf_event.h>

#include "bpf-filter.h"
#include "bpf-filter-bison.h"

extern int perf_bpf_filter_needs_path;

static int sample(enum perf_bpf_filter_term term)
{
	perf_bpf_filter_needs_path = 0;
	perf_bpf_filter_lval.sample.term = term;
	perf_bpf_filter_lval.sample.part = 0;
	return BFT_SAMPLE;
}

static int sample_part(enum perf_bpf_filter_term term, int part)
{
	perf_bpf_filter_needs_path = 0;
	perf_bpf_filter_lval.sample.term = term;
	perf_bpf_filter_lval.sample.part = part;
	return BFT_SAMPLE;
}

static int sample_path(enum perf_bpf_filter_term term)
{
	perf_bpf_filter_needs_path = 1;
	perf_bpf_filter_lval.sample.term = term;
	perf_bpf_filter_lval.sample.part = 0;
	return BFT_SAMPLE_PATH;
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

static int constant(int val)
{
	perf_bpf_filter_lval.num = val;
	return BFT_NUM;
}

static int path_or_error(void)
{
	if (!perf_bpf_filter_needs_path) {
		printf("perf_bpf_filter: Error: Unexpected item: %s\n",
			perf_bpf_filter_text);
		return BFT_ERROR;
	}
	perf_bpf_filter_lval.path = perf_bpf_filter_text;
	return BFT_PATH;
}

%}

num_dec		[0-9]+
num_hex		0[Xx][0-9a-fA-F]+
space		[ \t]+
path		[^ \t\n,]+
ident		[_a-zA-Z][_a-zA-Z0-9]+

%%

{num_dec}	{ return value(10); }
{num_hex}	{ return value(16); }
{space}		{ }

ip		{ return sample(PBF_TERM_IP); }
id		{ return sample(PBF_TERM_ID); }
tid		{ return sample(PBF_TERM_TID); }
pid		{ return sample_part(PBF_TERM_TID, 1); }
cpu		{ return sample(PBF_TERM_CPU); }
time		{ return sample(PBF_TERM_TIME); }
addr		{ return sample(PBF_TERM_ADDR); }
period		{ return sample(PBF_TERM_PERIOD); }
txn		{ return sample(PBF_TERM_TRANSACTION); }
weight		{ return sample(PBF_TERM_WEIGHT); }
weight1		{ return sample_part(PBF_TERM_WEIGHT_STRUCT, 1); }
weight2		{ return sample_part(PBF_TERM_WEIGHT_STRUCT, 2); }
weight3		{ return sample_part(PBF_TERM_WEIGHT_STRUCT, 3); }
ins_lat		{ return sample_part(PBF_TERM_WEIGHT_STRUCT, 2); } /* alias for weight2 */
p_stage_cyc	{ return sample_part(PBF_TERM_WEIGHT_STRUCT, 3); } /* alias for weight3 */
retire_lat	{ return sample_part(PBF_TERM_WEIGHT_STRUCT, 3); } /* alias for weight3 */
phys_addr	{ return sample(PBF_TERM_PHYS_ADDR); }
code_pgsz	{ return sample(PBF_TERM_CODE_PAGE_SIZE); }
data_pgsz	{ return sample(PBF_TERM_DATA_PAGE_SIZE); }
mem_op		{ return sample_part(PBF_TERM_DATA_SRC, 1); }
mem_lvlnum	{ return sample_part(PBF_TERM_DATA_SRC, 2); }
mem_lvl		{ return sample_part(PBF_TERM_DATA_SRC, 2); } /* alias for mem_lvlnum */
mem_snoop	{ return sample_part(PBF_TERM_DATA_SRC, 3); } /* include snoopx */
mem_remote	{ return sample_part(PBF_TERM_DATA_SRC, 4); }
mem_lock	{ return sample_part(PBF_TERM_DATA_SRC, 5); }
mem_dtlb	{ return sample_part(PBF_TERM_DATA_SRC, 6); }
mem_blk		{ return sample_part(PBF_TERM_DATA_SRC, 7); }
mem_hops	{ return sample_part(PBF_TERM_DATA_SRC, 8); }
uid		{ return sample(PBF_TERM_UID); }
gid		{ return sample(PBF_TERM_GID); }
cgroup		{ return sample_path(PBF_TERM_CGROUP); }

"=="		{ return operator(PBF_OP_EQ); }
"!="		{ return operator(PBF_OP_NEQ); }
">"		{ return operator(PBF_OP_GT); }
"<"		{ return operator(PBF_OP_LT); }
">="		{ return operator(PBF_OP_GE); }
"<="		{ return operator(PBF_OP_LE); }
"&"		{ return operator(PBF_OP_AND); }

na		{ return constant(PERF_MEM_OP_NA); }
load		{ return constant(PERF_MEM_OP_LOAD); }
store		{ return constant(PERF_MEM_OP_STORE); }
pfetch		{ return constant(PERF_MEM_OP_PFETCH); }
exec		{ return constant(PERF_MEM_OP_EXEC); }

l1		{ return constant(PERF_MEM_LVLNUM_L1); }
l2		{ return constant(PERF_MEM_LVLNUM_L2); }
l3		{ return constant(PERF_MEM_LVLNUM_L3); }
l4		{ return constant(PERF_MEM_LVLNUM_L4); }
cxl		{ return constant(PERF_MEM_LVLNUM_CXL); }
io		{ return constant(PERF_MEM_LVLNUM_IO); }
any_cache	{ return constant(PERF_MEM_LVLNUM_ANY_CACHE); }
lfb		{ return constant(PERF_MEM_LVLNUM_LFB); }
ram		{ return constant(PERF_MEM_LVLNUM_RAM); }
pmem		{ return constant(PERF_MEM_LVLNUM_PMEM); }

none		{ return constant(PERF_MEM_SNOOP_NONE); }
hit		{ return constant(PERF_MEM_SNOOP_HIT); }
miss		{ return constant(PERF_MEM_SNOOP_MISS); }
hitm		{ return constant(PERF_MEM_SNOOP_HITM); }
fwd		{ return constant(PERF_MEM_SNOOPX_FWD); }
peer		{ return constant(PERF_MEM_SNOOPX_PEER); }

remote		{ return constant(PERF_MEM_REMOTE_REMOTE); }

locked		{ return constant(PERF_MEM_LOCK_LOCKED); }

l1_hit		{ return constant(PERF_MEM_TLB_L1 | PERF_MEM_TLB_HIT); }
l1_miss		{ return constant(PERF_MEM_TLB_L1 | PERF_MEM_TLB_MISS); }
l2_hit		{ return constant(PERF_MEM_TLB_L2 | PERF_MEM_TLB_HIT); }
l2_miss		{ return constant(PERF_MEM_TLB_L2 | PERF_MEM_TLB_MISS); }
any_hit		{ return constant(PERF_MEM_TLB_HIT); }
any_miss	{ return constant(PERF_MEM_TLB_MISS); }
walk		{ return constant(PERF_MEM_TLB_WK); }
os		{ return constant(PERF_MEM_TLB_OS); }
fault		{ return constant(PERF_MEM_TLB_OS); } /* alias for os */

by_data		{ return constant(PERF_MEM_BLK_DATA); }
by_addr		{ return constant(PERF_MEM_BLK_ADDR); }

hops0		{ return constant(PERF_MEM_HOPS_0); }
hops1		{ return constant(PERF_MEM_HOPS_1); }
hops2		{ return constant(PERF_MEM_HOPS_2); }
hops3		{ return constant(PERF_MEM_HOPS_3); }

","		{ return ','; }
"||"		{ return BFT_LOGICAL_OR; }

{path}		{ return path_or_error(); }

%%
