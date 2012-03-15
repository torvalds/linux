
%name-prefix "parse_events_"
%parse-param {struct list_head *list}
%parse-param {int *idx}

%{

#define YYDEBUG 1

#include <linux/compiler.h>
#include <linux/list.h>
#include "types.h"
#include "util.h"
#include "parse-events.h"

extern int parse_events_lex (void);

#define ABORT_ON(val) \
do { \
	if (val) \
		YYABORT; \
} while (0)

%}

%token PE_VALUE PE_VALUE_SYM PE_RAW
%token PE_NAME
%token PE_MODIFIER_EVENT PE_MODIFIER_BP
%token PE_NAME_CACHE_TYPE PE_NAME_CACHE_OP_RESULT
%token PE_PREFIX_MEM PE_PREFIX_RAW
%token PE_ERROR
%type <num> PE_VALUE
%type <num> PE_VALUE_SYM
%type <num> PE_RAW
%type <str> PE_NAME
%type <str> PE_NAME_CACHE_TYPE
%type <str> PE_NAME_CACHE_OP_RESULT
%type <str> PE_MODIFIER_EVENT
%type <str> PE_MODIFIER_BP

%union
{
	char *str;
	unsigned long num;
}
%%

events:
events ',' event | event

event:
event_def PE_MODIFIER_EVENT
{
	ABORT_ON(parse_events_modifier(list, $2));
}
|
event_def

event_def: event_legacy_symbol sep_dc |
	   event_legacy_cache sep_dc |
	   event_legacy_mem |
	   event_legacy_tracepoint sep_dc |
	   event_legacy_numeric sep_dc |
	   event_legacy_raw sep_dc

event_legacy_symbol:
PE_VALUE_SYM
{
	int type = $1 >> 16;
	int config = $1 & 255;

	ABORT_ON(parse_events_add_numeric(list, idx, type, config));
}

event_legacy_cache:
PE_NAME_CACHE_TYPE '-' PE_NAME_CACHE_OP_RESULT '-' PE_NAME_CACHE_OP_RESULT
{
	ABORT_ON(parse_events_add_cache(list, idx, $1, $3, $5));
}
|
PE_NAME_CACHE_TYPE '-' PE_NAME_CACHE_OP_RESULT
{
	ABORT_ON(parse_events_add_cache(list, idx, $1, $3, NULL));
}
|
PE_NAME_CACHE_TYPE
{
	ABORT_ON(parse_events_add_cache(list, idx, $1, NULL, NULL));
}

event_legacy_mem:
PE_PREFIX_MEM PE_VALUE ':' PE_MODIFIER_BP sep_dc
{
	ABORT_ON(parse_events_add_breakpoint(list, idx, (void *) $2, $4));
}
|
PE_PREFIX_MEM PE_VALUE sep_dc
{
	ABORT_ON(parse_events_add_breakpoint(list, idx, (void *) $2, NULL));
}

event_legacy_tracepoint:
PE_NAME ':' PE_NAME
{
	ABORT_ON(parse_events_add_tracepoint(list, idx, $1, $3));
}

event_legacy_numeric:
PE_VALUE ':' PE_VALUE
{
	ABORT_ON(parse_events_add_numeric(list, idx, $1, $3));
}

event_legacy_raw:
PE_RAW
{
	ABORT_ON(parse_events_add_numeric(list, idx, PERF_TYPE_RAW, $1));
}

sep_dc: ':' |

%%

void parse_events_error(struct list_head *list __used, int *idx __used,
			char const *msg __used)
{
}
