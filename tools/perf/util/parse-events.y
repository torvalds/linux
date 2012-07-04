
%name-prefix "parse_events_"
%parse-param {struct list_head *list_all}
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
%type <head> event_pmu
%type <head> event_legacy_symbol
%type <head> event_legacy_cache
%type <head> event_legacy_mem
%type <head> event_legacy_tracepoint
%type <head> event_legacy_numeric
%type <head> event_legacy_raw
%type <head> event_def

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
	ABORT_ON(parse_events_modifier($1, $2));
	parse_events_update_lists($1, list_all);
}
|
event_def
{
	parse_events_update_lists($1, list_all);
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
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_pmu(&list, idx, $1, $3));
	parse_events__free_terms($3);
	$$ = list;
}

event_legacy_symbol:
PE_VALUE_SYM '/' event_config '/'
{
	struct list_head *list = NULL;
	int type = $1 >> 16;
	int config = $1 & 255;

	ABORT_ON(parse_events_add_numeric(&list, idx, type, config, $3));
	parse_events__free_terms($3);
	$$ = list;
}
|
PE_VALUE_SYM sep_slash_dc
{
	struct list_head *list = NULL;
	int type = $1 >> 16;
	int config = $1 & 255;

	ABORT_ON(parse_events_add_numeric(&list, idx, type, config, NULL));
	$$ = list;
}

event_legacy_cache:
PE_NAME_CACHE_TYPE '-' PE_NAME_CACHE_OP_RESULT '-' PE_NAME_CACHE_OP_RESULT
{
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_cache(&list, idx, $1, $3, $5));
	$$ = list;
}
|
PE_NAME_CACHE_TYPE '-' PE_NAME_CACHE_OP_RESULT
{
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_cache(&list, idx, $1, $3, NULL));
	$$ = list;
}
|
PE_NAME_CACHE_TYPE
{
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_cache(&list, idx, $1, NULL, NULL));
	$$ = list;
}

event_legacy_mem:
PE_PREFIX_MEM PE_VALUE ':' PE_MODIFIER_BP sep_dc
{
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_breakpoint(&list, idx, (void *) $2, $4));
	$$ = list;
}
|
PE_PREFIX_MEM PE_VALUE sep_dc
{
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_breakpoint(&list, idx, (void *) $2, NULL));
	$$ = list;
}

event_legacy_tracepoint:
PE_NAME ':' PE_NAME
{
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_tracepoint(&list, idx, $1, $3));
	$$ = list;
}

event_legacy_numeric:
PE_VALUE ':' PE_VALUE
{
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_numeric(&list, idx, $1, $3, NULL));
	$$ = list;
}

event_legacy_raw:
PE_RAW
{
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_numeric(&list, idx, PERF_TYPE_RAW, $1, NULL));
	$$ = list;
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

	ABORT_ON(parse_events__term_str(&term, PARSE_EVENTS__TERM_TYPE_USER,
					$1, $3));
	$$ = term;
}
|
PE_NAME '=' PE_VALUE
{
	struct parse_events__term *term;

	ABORT_ON(parse_events__term_num(&term, PARSE_EVENTS__TERM_TYPE_USER,
					$1, $3));
	$$ = term;
}
|
PE_NAME
{
	struct parse_events__term *term;

	ABORT_ON(parse_events__term_num(&term, PARSE_EVENTS__TERM_TYPE_USER,
					$1, 1));
	$$ = term;
}
|
PE_TERM '=' PE_NAME
{
	struct parse_events__term *term;

	ABORT_ON(parse_events__term_str(&term, $1, NULL, $3));
	$$ = term;
}
|
PE_TERM '=' PE_VALUE
{
	struct parse_events__term *term;

	ABORT_ON(parse_events__term_num(&term, $1, NULL, $3));
	$$ = term;
}
|
PE_TERM
{
	struct parse_events__term *term;

	ABORT_ON(parse_events__term_num(&term, $1, NULL, 1));
	$$ = term;
}

sep_dc: ':' |

sep_slash_dc: '/' | ':' |

%%

void parse_events_error(struct list_head *list_all __used,
			int *idx __used,
			char const *msg __used)
{
}
