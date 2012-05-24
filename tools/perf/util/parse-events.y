
%name-prefix "parse_events_"
%parse-param {struct list_head *list_all}
%parse-param {struct list_head *list_event}
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

%token PE_VALUE PE_VALUE_SYM PE_RAW PE_TERM
%token PE_NAME
%token PE_MODIFIER_EVENT PE_MODIFIER_BP
%token PE_NAME_CACHE_TYPE PE_NAME_CACHE_OP_RESULT
%token PE_PREFIX_MEM PE_PREFIX_RAW
%token PE_ERROR
%type <num> PE_VALUE
%type <num> PE_VALUE_SYM
%type <num> PE_RAW
%type <num> PE_TERM
%type <str> PE_NAME
%type <str> PE_NAME_CACHE_TYPE
%type <str> PE_NAME_CACHE_OP_RESULT
%type <str> PE_MODIFIER_EVENT
%type <str> PE_MODIFIER_BP
%type <head> event_config
%type <term> event_term

%union
{
	char *str;
	unsigned long num;
	struct list_head *head;
	struct parse_events__term *term;
}
%%

events:
events ',' event | event

event:
event_def PE_MODIFIER_EVENT
{
	/*
	 * Apply modifier on all events added by single event definition
	 * (there could be more events added for multiple tracepoint
	 * definitions via '*?'.
	 */
	ABORT_ON(parse_events_modifier(list_event, $2));
	parse_events_update_lists(list_event, list_all);
}
|
event_def
{
	parse_events_update_lists(list_event, list_all);
}

event_def: event_pmu |
	   event_legacy_symbol |
	   event_legacy_cache sep_dc |
	   event_legacy_mem |
	   event_legacy_tracepoint sep_dc |
	   event_legacy_numeric sep_dc |
	   event_legacy_raw sep_dc

event_pmu:
PE_NAME '/' event_config '/'
{
	ABORT_ON(parse_events_add_pmu(list_event, idx, $1, $3));
	parse_events__free_terms($3);
}

event_legacy_symbol:
PE_VALUE_SYM '/' event_config '/'
{
	int type = $1 >> 16;
	int config = $1 & 255;

	ABORT_ON(parse_events_add_numeric(list_event, idx, type, config, $3));
	parse_events__free_terms($3);
}
|
PE_VALUE_SYM sep_slash_dc
{
	int type = $1 >> 16;
	int config = $1 & 255;

	ABORT_ON(parse_events_add_numeric(list_event, idx, type, config, NULL));
}

event_legacy_cache:
PE_NAME_CACHE_TYPE '-' PE_NAME_CACHE_OP_RESULT '-' PE_NAME_CACHE_OP_RESULT
{
	ABORT_ON(parse_events_add_cache(list_event, idx, $1, $3, $5));
}
|
PE_NAME_CACHE_TYPE '-' PE_NAME_CACHE_OP_RESULT
{
	ABORT_ON(parse_events_add_cache(list_event, idx, $1, $3, NULL));
}
|
PE_NAME_CACHE_TYPE
{
	ABORT_ON(parse_events_add_cache(list_event, idx, $1, NULL, NULL));
}

event_legacy_mem:
PE_PREFIX_MEM PE_VALUE ':' PE_MODIFIER_BP sep_dc
{
	ABORT_ON(parse_events_add_breakpoint(list_event, idx, (void *) $2, $4));
}
|
PE_PREFIX_MEM PE_VALUE sep_dc
{
	ABORT_ON(parse_events_add_breakpoint(list_event, idx, (void *) $2, NULL));
}

event_legacy_tracepoint:
PE_NAME ':' PE_NAME
{
	ABORT_ON(parse_events_add_tracepoint(list_event, idx, $1, $3));
}

event_legacy_numeric:
PE_VALUE ':' PE_VALUE
{
	ABORT_ON(parse_events_add_numeric(list_event, idx, $1, $3, NULL));
}

event_legacy_raw:
PE_RAW
{
	ABORT_ON(parse_events_add_numeric(list_event, idx, PERF_TYPE_RAW, $1, NULL));
}

event_config:
event_config ',' event_term
{
	struct list_head *head = $1;
	struct parse_events__term *term = $3;

	ABORT_ON(!head);
	list_add_tail(&term->list, head);
	$$ = $1;
}
|
event_term
{
	struct list_head *head = malloc(sizeof(*head));
	struct parse_events__term *term = $1;

	ABORT_ON(!head);
	INIT_LIST_HEAD(head);
	list_add_tail(&term->list, head);
	$$ = head;
}

event_term:
PE_NAME '=' PE_NAME
{
	struct parse_events__term *term;

	ABORT_ON(parse_events__new_term(&term, PARSE_EVENTS__TERM_TYPE_STR,
		 $1, $3, 0));
	$$ = term;
}
|
PE_NAME '=' PE_VALUE
{
	struct parse_events__term *term;

	ABORT_ON(parse_events__new_term(&term, PARSE_EVENTS__TERM_TYPE_NUM,
		 $1, NULL, $3));
	$$ = term;
}
|
PE_NAME
{
	struct parse_events__term *term;

	ABORT_ON(parse_events__new_term(&term, PARSE_EVENTS__TERM_TYPE_NUM,
		 $1, NULL, 1));
	$$ = term;
}
|
PE_TERM '=' PE_VALUE
{
	struct parse_events__term *term;

	ABORT_ON(parse_events__new_term(&term, $1, NULL, NULL, $3));
	$$ = term;
}
|
PE_TERM
{
	struct parse_events__term *term;

	ABORT_ON(parse_events__new_term(&term, $1, NULL, NULL, 1));
	$$ = term;
}

sep_dc: ':' |

sep_slash_dc: '/' | ':' |

%%

void parse_events_error(struct list_head *list_all __used,
			struct list_head *list_event __used,
			int *idx __used,
			char const *msg __used)
{
}
