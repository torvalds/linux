
%option prefix="parse_events_"

%{
#include <errno.h>
#include "../perf.h"
#include "parse-events-bison.h"
#include "parse-events.h"

static int __value(char *str, int base, int token)
{
	long num;

	errno = 0;
	num = strtoul(str, NULL, base);
	if (errno)
		return PE_ERROR;

	parse_events_lval.num = num;
	return token;
}

static int value(int base)
{
	return __value(parse_events_text, base, PE_VALUE);
}

static int raw(void)
{
	return __value(parse_events_text + 1, 16, PE_RAW);
}

static int str(int token)
{
	parse_events_lval.str = strdup(parse_events_text);
	return token;
}

static int sym(int type, int config)
{
	parse_events_lval.num = (type << 16) + config;
	return PE_VALUE_SYM;
}

static int term(int type)
{
	parse_events_lval.num = type;
	return PE_TERM;
}

%}

num_dec		[0-9]+
num_hex		0x[a-fA-F0-9]+
num_raw_hex	[a-fA-F0-9]+
name		[a-zA-Z_*?][a-zA-Z0-9_*?]*
modifier_event	[ukhp]{1,5}
modifier_bp	[rwx]

%%
cpu-cycles|cycles				{ return sym(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES); }
stalled-cycles-frontend|idle-cycles-frontend	{ return sym(PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_FRONTEND); }
stalled-cycles-backend|idle-cycles-backend	{ return sym(PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_BACKEND); }
instructions					{ return sym(PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS); }
cache-references				{ return sym(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES); }
cache-misses					{ return sym(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES); }
branch-instructions|branches			{ return sym(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS); }
branch-misses					{ return sym(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES); }
bus-cycles					{ return sym(PERF_TYPE_HARDWARE, PERF_COUNT_HW_BUS_CYCLES); }
ref-cycles					{ return sym(PERF_TYPE_HARDWARE, PERF_COUNT_HW_REF_CPU_CYCLES); }
cpu-clock					{ return sym(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_CLOCK); }
task-clock					{ return sym(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK); }
page-faults|faults				{ return sym(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS); }
minor-faults					{ return sym(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MIN); }
major-faults					{ return sym(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_PAGE_FAULTS_MAJ); }
context-switches|cs				{ return sym(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CONTEXT_SWITCHES); }
cpu-migrations|migrations			{ return sym(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_CPU_MIGRATIONS); }
alignment-faults				{ return sym(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_ALIGNMENT_FAULTS); }
emulation-faults				{ return sym(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_EMULATION_FAULTS); }

L1-dcache|l1-d|l1d|L1-data		|
L1-icache|l1-i|l1i|L1-instruction	|
LLC|L2					|
dTLB|d-tlb|Data-TLB			|
iTLB|i-tlb|Instruction-TLB		|
branch|branches|bpu|btb|bpc		|
node					{ return str(PE_NAME_CACHE_TYPE); }

load|loads|read				|
store|stores|write			|
prefetch|prefetches			|
speculative-read|speculative-load	|
refs|Reference|ops|access		|
misses|miss				{ return str(PE_NAME_CACHE_OP_RESULT); }

	/*
	 * These are event config hardcoded term names to be specified
	 * within xxx/.../ syntax. So far we dont clash with other names,
	 * so we can put them here directly. In case the we have a conflict
	 * in future, this needs to go into '//' condition block.
	 */
config			{ return term(PARSE_EVENTS__TERM_TYPE_CONFIG); }
config1			{ return term(PARSE_EVENTS__TERM_TYPE_CONFIG1); }
config2			{ return term(PARSE_EVENTS__TERM_TYPE_CONFIG2); }
period			{ return term(PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD); }
branch_type		{ return term(PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE); }

mem:			{ return PE_PREFIX_MEM; }
r{num_raw_hex}		{ return raw(); }
{num_dec}		{ return value(10); }
{num_hex}		{ return value(16); }

{modifier_event}	{ return str(PE_MODIFIER_EVENT); }
{modifier_bp}		{ return str(PE_MODIFIER_BP); }
{name}			{ return str(PE_NAME); }
"/"			{ return '/'; }
-			{ return '-'; }
,			{ return ','; }
:			{ return ':'; }
=			{ return '='; }

%%

int parse_events_wrap(void)
{
	return 1;
}
