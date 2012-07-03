%pure-parser
%name-prefix "parse_events_"
%parse-param {void *_data}
%parse-param {void *scanner}
%lex-param {void* scanner}

%{

#define YYDEBUG 1

#include <linux/compiler.h>
#include <linux/list.h>
#include "types.h"
#include "util.h"
#include "parse-events.h"
#include "parse-events-bison.h"

extern int parse_events_lex (YYSTYPE* lvalp, void* scanner);

#define ABORT_ON(val) \
do { \
	if (val) \
		YYABORT; \
} while (0)

%}

%token PE_START_EVENTS PE_START_TERMS
%token PE_VALUE PE_VALUE_SYM_HW PE_VALUE_SYM_SW PE_RAW PE_TERM
%token PE_NAME
%token PE_MODIFIER_EVENT PE_MODIFIER_BP
%token PE_NAME_CACHE_TYPE PE_NAME_CACHE_OP_RESULT
%token PE_PREFIX_MEM PE_PREFIX_RAW
%token PE_ERROR
%type <num> PE_VALUE
%type <num> PE_VALUE_SYM_HW
%type <num> PE_VALUE_SYM_SW
%type <num> PE_RAW
%type <num> PE_TERM
%type <str> PE_NAME
%type <str> PE_NAME_CACHE_TYPE
%type <str> PE_NAME_CACHE_OP_RESULT
%type <str> PE_MODIFIER_EVENT
%type <str> PE_MODIFIER_BP
%type <num> value_sym
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

start:
PE_START_EVENTS events
|
PE_START_TERMS  terms

events:
events ',' event | event

event:
event_def PE_MODIFIER_EVENT
{
	struct parse_events_data__events *data = _data;

	/*
	 * Apply modifier on all events added by single event definition
	 * (there could be more events added for multiple tracepoint
	 * definitions via '*?'.
	 */
	ABORT_ON(parse_events_modifier($1, $2));
	parse_events_update_lists($1, &data->list);
}
|
event_def
{
	struct parse_events_data__events *data = _data;

	parse_events_update_lists($1, &data->list);
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
	struct parse_events_data__events *data = _data;
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_pmu(&list, &data->idx, $1, $3));
	parse_events__free_terms($3);
	$$ = list;
}

value_sym:
PE_VALUE_SYM_HW
|
PE_VALUE_SYM_SW

event_legacy_symbol:
value_sym '/' event_config '/'
{
	struct parse_events_data__events *data = _data;
	struct list_head *list = NULL;
	int type = $1 >> 16;
	int config = $1 & 255;

	ABORT_ON(parse_events_add_numeric(&list, &data->idx,
					  type, config, $3));
	parse_events__free_terms($3);
	$$ = list;
}
|
value_sym sep_slash_dc
{
	struct parse_events_data__events *data = _data;
	struct list_head *list = NULL;
	int type = $1 >> 16;
	int config = $1 & 255;

	ABORT_ON(parse_events_add_numeric(&list, &data->idx,
					  type, config, NULL));
	$$ = list;
}

event_legacy_cache:
PE_NAME_CACHE_TYPE '-' PE_NAME_CACHE_OP_RESULT '-' PE_NAME_CACHE_OP_RESULT
{
	struct parse_events_data__events *data = _data;
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_cache(&list, &data->idx, $1, $3, $5));
	$$ = list;
}
|
PE_NAME_CACHE_TYPE '-' PE_NAME_CACHE_OP_RESULT
{
	struct parse_events_data__events *data = _data;
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_cache(&list, &data->idx, $1, $3, NULL));
	$$ = list;
}
|
PE_NAME_CACHE_TYPE
{
	struct parse_events_data__events *data = _data;
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_cache(&list, &data->idx, $1, NULL, NULL));
	$$ = list;
}

event_legacy_mem:
PE_PREFIX_MEM PE_VALUE ':' PE_MODIFIER_BP sep_dc
{
	struct parse_events_data__events *data = _data;
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_breakpoint(&list, &data->idx,
					     (void *) $2, $4));
	$$ = list;
}
|
PE_PREFIX_MEM PE_VALUE sep_dc
{
	struct parse_events_data__events *data = _data;
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_breakpoint(&list, &data->idx,
					     (void *) $2, NULL));
	$$ = list;
}

event_legacy_tracepoint:
PE_NAME ':' PE_NAME
{
	struct parse_events_data__events *data = _data;
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_tracepoint(&list, &data->idx, $1, $3));
	$$ = list;
}

event_legacy_numeric:
PE_VALUE ':' PE_VALUE
{
	struct parse_events_data__events *data = _data;
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_numeric(&list, &data->idx, $1, $3, NULL));
	$$ = list;
}

event_legacy_raw:
PE_RAW
{
	struct parse_events_data__events *data = _data;
	struct list_head *list = NULL;

	ABORT_ON(parse_events_add_numeric(&list, &data->idx,
					  PERF_TYPE_RAW, $1, NULL));
	$$ = list;
}

terms: event_config
{
	struct parse_events_data__terms *data = _data;
	data->terms = $1;
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

void parse_events_error(void *data __used, void *scanner __used,
			char const *msg __used)
{
}
