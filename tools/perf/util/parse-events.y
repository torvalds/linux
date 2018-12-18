%pure-parser
%parse-param {void *_parse_state}
%parse-param {void *scanner}
%lex-param {void* scanner}
%locations

%{

#define YYDEBUG 1

#include <fnmatch.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/types.h>
#include "util.h"
#include "pmu.h"
#include "debug.h"
#include "parse-events.h"
#include "parse-events-bison.h"

void parse_events_error(YYLTYPE *loc, void *parse_state, void *scanner, char const *msg);

#define ABORT_ON(val) \
do { \
	if (val) \
		YYABORT; \
} while (0)

#define ALLOC_LIST(list) \
do { \
	list = malloc(sizeof(*list)); \
	ABORT_ON(!list);              \
	INIT_LIST_HEAD(list);         \
} while (0)

static void inc_group_count(struct list_head *list,
		       struct parse_events_state *parse_state)
{
	/* Count groups only have more than 1 members */
	if (!list_is_last(list->next, list))
		parse_state->nr_groups++;
}

%}

%token PE_START_EVENTS PE_START_TERMS
%token PE_VALUE PE_VALUE_SYM_HW PE_VALUE_SYM_SW PE_RAW PE_TERM
%token PE_EVENT_NAME
%token PE_NAME
%token PE_BPF_OBJECT PE_BPF_SOURCE
%token PE_MODIFIER_EVENT PE_MODIFIER_BP
%token PE_NAME_CACHE_TYPE PE_NAME_CACHE_OP_RESULT
%token PE_PREFIX_MEM PE_PREFIX_RAW PE_PREFIX_GROUP
%token PE_ERROR
%token PE_PMU_EVENT_PRE PE_PMU_EVENT_SUF PE_KERNEL_PMU_EVENT
%token PE_ARRAY_ALL PE_ARRAY_RANGE
%token PE_DRV_CFG_TERM
%type <num> PE_VALUE
%type <num> PE_VALUE_SYM_HW
%type <num> PE_VALUE_SYM_SW
%type <num> PE_RAW
%type <num> PE_TERM
%type <str> PE_NAME
%type <str> PE_BPF_OBJECT
%type <str> PE_BPF_SOURCE
%type <str> PE_NAME_CACHE_TYPE
%type <str> PE_NAME_CACHE_OP_RESULT
%type <str> PE_MODIFIER_EVENT
%type <str> PE_MODIFIER_BP
%type <str> PE_EVENT_NAME
%type <str> PE_PMU_EVENT_PRE PE_PMU_EVENT_SUF PE_KERNEL_PMU_EVENT
%type <str> PE_DRV_CFG_TERM
%type <num> value_sym
%type <head> event_config
%type <head> opt_event_config
%type <head> opt_pmu_config
%type <term> event_term
%type <head> event_pmu
%type <head> event_legacy_symbol
%type <head> event_legacy_cache
%type <head> event_legacy_mem
%type <head> event_legacy_tracepoint
%type <tracepoint_name> tracepoint_name
%type <head> event_legacy_numeric
%type <head> event_legacy_raw
%type <head> event_bpf_file
%type <head> event_def
%type <head> event_mod
%type <head> event_name
%type <head> event
%type <head> events
%type <head> group_def
%type <head> group
%type <head> groups
%type <array> array
%type <array> array_term
%type <array> array_terms

%union
{
	char *str;
	u64 num;
	struct list_head *head;
	struct parse_events_term *term;
	struct tracepoint_name {
		char *sys;
		char *event;
	} tracepoint_name;
	struct parse_events_array array;
}
%%

start:
PE_START_EVENTS start_events
|
PE_START_TERMS  start_terms

start_events: groups
{
	struct parse_events_state *parse_state = _parse_state;

	parse_events_update_lists($1, &parse_state->list);
}

groups:
groups ',' group
{
	struct list_head *list  = $1;
	struct list_head *group = $3;

	parse_events_update_lists(group, list);
	$$ = list;
}
|
groups ',' event
{
	struct list_head *list  = $1;
	struct list_head *event = $3;

	parse_events_update_lists(event, list);
	$$ = list;
}
|
group
|
event

group:
group_def ':' PE_MODIFIER_EVENT
{
	struct list_head *list = $1;

	ABORT_ON(parse_events__modifier_group(list, $3));
	$$ = list;
}
|
group_def

group_def:
PE_NAME '{' events '}'
{
	struct list_head *list = $3;

	inc_group_count(list, _parse_state);
	parse_events__set_leader($1, list, _parse_state);
	$$ = list;
}
|
'{' events '}'
{
	struct list_head *list = $2;

	inc_group_count(list, _parse_state);
	parse_events__set_leader(NULL, list, _parse_state);
	$$ = list;
}

events:
events ',' event
{
	struct list_head *event = $3;
	struct list_head *list  = $1;

	parse_events_update_lists(event, list);
	$$ = list;
}
|
event

event: event_mod

event_mod:
event_name PE_MODIFIER_EVENT
{
	struct list_head *list = $1;

	/*
	 * Apply modifier on all events added by single event definition
	 * (there could be more events added for multiple tracepoint
	 * definitions via '*?'.
	 */
	ABORT_ON(parse_events__modifier_event(list, $2, false));
	$$ = list;
}
|
event_name

event_name:
PE_EVENT_NAME event_def
{
	ABORT_ON(parse_events_name($2, $1));
	free($1);
	$$ = $2;
}
|
event_def

event_def: event_pmu |
	   event_legacy_symbol |
	   event_legacy_cache sep_dc |
	   event_legacy_mem |
	   event_legacy_tracepoint sep_dc |
	   event_legacy_numeric sep_dc |
	   event_legacy_raw sep_dc |
	   event_bpf_file

event_pmu:
PE_NAME opt_pmu_config
{
	struct parse_events_state *parse_state = _parse_state;
	struct parse_events_error *error = parse_state->error;
	struct list_head *list, *orig_terms, *terms;

	if (parse_events_copy_term_list($2, &orig_terms))
		YYABORT;

	if (error)
		error->idx = @1.first_column;

	ALLOC_LIST(list);
	if (parse_events_add_pmu(_parse_state, list, $1, $2, false, false)) {
		struct perf_pmu *pmu = NULL;
		int ok = 0;
		char *pattern;

		if (asprintf(&pattern, "%s*", $1) < 0)
			YYABORT;

		while ((pmu = perf_pmu__scan(pmu)) != NULL) {
			char *name = pmu->name;

			if (!strncmp(name, "uncore_", 7) &&
			    strncmp($1, "uncore_", 7))
				name += 7;
			if (!fnmatch(pattern, name, 0)) {
				if (parse_events_copy_term_list(orig_terms, &terms)) {
					free(pattern);
					YYABORT;
				}
				if (!parse_events_add_pmu(_parse_state, list, pmu->name, terms, true, false))
					ok++;
				parse_events_terms__delete(terms);
			}
		}

		free(pattern);

		if (!ok)
			YYABORT;
	}
	parse_events_terms__delete($2);
	parse_events_terms__delete(orig_terms);
	$$ = list;
}
|
PE_KERNEL_PMU_EVENT sep_dc
{
	struct list_head *list;

	if (parse_events_multi_pmu_add(_parse_state, $1, &list) < 0)
		YYABORT;
	$$ = list;
}
|
PE_PMU_EVENT_PRE '-' PE_PMU_EVENT_SUF sep_dc
{
	struct list_head *list;
	char pmu_name[128];

	snprintf(&pmu_name, 128, "%s-%s", $1, $3);
	if (parse_events_multi_pmu_add(_parse_state, pmu_name, &list) < 0)
		YYABORT;
	$$ = list;
}

value_sym:
PE_VALUE_SYM_HW
|
PE_VALUE_SYM_SW

event_legacy_symbol:
value_sym '/' event_config '/'
{
	struct list_head *list;
	int type = $1 >> 16;
	int config = $1 & 255;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_add_numeric(_parse_state, list, type, config, $3));
	parse_events_terms__delete($3);
	$$ = list;
}
|
value_sym sep_slash_dc
{
	struct list_head *list;
	int type = $1 >> 16;
	int config = $1 & 255;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_add_numeric(_parse_state, list, type, config, NULL));
	$$ = list;
}

event_legacy_cache:
PE_NAME_CACHE_TYPE '-' PE_NAME_CACHE_OP_RESULT '-' PE_NAME_CACHE_OP_RESULT opt_event_config
{
	struct parse_events_state *parse_state = _parse_state;
	struct parse_events_error *error = parse_state->error;
	struct list_head *list;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_add_cache(list, &parse_state->idx, $1, $3, $5, error, $6));
	parse_events_terms__delete($6);
	$$ = list;
}
|
PE_NAME_CACHE_TYPE '-' PE_NAME_CACHE_OP_RESULT opt_event_config
{
	struct parse_events_state *parse_state = _parse_state;
	struct parse_events_error *error = parse_state->error;
	struct list_head *list;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_add_cache(list, &parse_state->idx, $1, $3, NULL, error, $4));
	parse_events_terms__delete($4);
	$$ = list;
}
|
PE_NAME_CACHE_TYPE opt_event_config
{
	struct parse_events_state *parse_state = _parse_state;
	struct parse_events_error *error = parse_state->error;
	struct list_head *list;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_add_cache(list, &parse_state->idx, $1, NULL, NULL, error, $2));
	parse_events_terms__delete($2);
	$$ = list;
}

event_legacy_mem:
PE_PREFIX_MEM PE_VALUE '/' PE_VALUE ':' PE_MODIFIER_BP sep_dc
{
	struct parse_events_state *parse_state = _parse_state;
	struct list_head *list;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_add_breakpoint(list, &parse_state->idx,
					     (void *) $2, $6, $4));
	$$ = list;
}
|
PE_PREFIX_MEM PE_VALUE '/' PE_VALUE sep_dc
{
	struct parse_events_state *parse_state = _parse_state;
	struct list_head *list;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_add_breakpoint(list, &parse_state->idx,
					     (void *) $2, NULL, $4));
	$$ = list;
}
|
PE_PREFIX_MEM PE_VALUE ':' PE_MODIFIER_BP sep_dc
{
	struct parse_events_state *parse_state = _parse_state;
	struct list_head *list;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_add_breakpoint(list, &parse_state->idx,
					     (void *) $2, $4, 0));
	$$ = list;
}
|
PE_PREFIX_MEM PE_VALUE sep_dc
{
	struct parse_events_state *parse_state = _parse_state;
	struct list_head *list;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_add_breakpoint(list, &parse_state->idx,
					     (void *) $2, NULL, 0));
	$$ = list;
}

event_legacy_tracepoint:
tracepoint_name opt_event_config
{
	struct parse_events_state *parse_state = _parse_state;
	struct parse_events_error *error = parse_state->error;
	struct list_head *list;

	ALLOC_LIST(list);
	if (error)
		error->idx = @1.first_column;

	if (parse_events_add_tracepoint(list, &parse_state->idx, $1.sys, $1.event,
					error, $2))
		return -1;

	$$ = list;
}

tracepoint_name:
PE_NAME '-' PE_NAME ':' PE_NAME
{
	char sys_name[128];
	struct tracepoint_name tracepoint;

	snprintf(&sys_name, 128, "%s-%s", $1, $3);
	tracepoint.sys = &sys_name;
	tracepoint.event = $5;

	$$ = tracepoint;
}
|
PE_NAME ':' PE_NAME
{
	struct tracepoint_name tracepoint = {$1, $3};

	$$ = tracepoint;
}

event_legacy_numeric:
PE_VALUE ':' PE_VALUE opt_event_config
{
	struct list_head *list;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_add_numeric(_parse_state, list, (u32)$1, $3, $4));
	parse_events_terms__delete($4);
	$$ = list;
}

event_legacy_raw:
PE_RAW opt_event_config
{
	struct list_head *list;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_add_numeric(_parse_state, list, PERF_TYPE_RAW, $1, $2));
	parse_events_terms__delete($2);
	$$ = list;
}

event_bpf_file:
PE_BPF_OBJECT opt_event_config
{
	struct parse_events_state *parse_state = _parse_state;
	struct parse_events_error *error = parse_state->error;
	struct list_head *list;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_load_bpf(parse_state, list, $1, false, $2));
	parse_events_terms__delete($2);
	$$ = list;
}
|
PE_BPF_SOURCE opt_event_config
{
	struct list_head *list;

	ALLOC_LIST(list);
	ABORT_ON(parse_events_load_bpf(_parse_state, list, $1, true, $2));
	parse_events_terms__delete($2);
	$$ = list;
}

opt_event_config:
'/' event_config '/'
{
	$$ = $2;
}
|
'/' '/'
{
	$$ = NULL;
}
|
{
	$$ = NULL;
}

opt_pmu_config:
'/' event_config '/'
{
	$$ = $2;
}
|
'/' '/'
{
	$$ = NULL;
}

start_terms: event_config
{
	struct parse_events_state *parse_state = _parse_state;
	parse_state->terms = $1;
}

event_config:
event_config ',' event_term
{
	struct list_head *head = $1;
	struct parse_events_term *term = $3;

	ABORT_ON(!head);
	list_add_tail(&term->list, head);
	$$ = $1;
}
|
event_term
{
	struct list_head *head = malloc(sizeof(*head));
	struct parse_events_term *term = $1;

	ABORT_ON(!head);
	INIT_LIST_HEAD(head);
	list_add_tail(&term->list, head);
	$$ = head;
}

event_term:
PE_NAME '=' PE_NAME
{
	struct parse_events_term *term;

	ABORT_ON(parse_events_term__str(&term, PARSE_EVENTS__TERM_TYPE_USER,
					$1, $3, &@1, &@3));
	$$ = term;
}
|
PE_NAME '=' PE_VALUE
{
	struct parse_events_term *term;

	ABORT_ON(parse_events_term__num(&term, PARSE_EVENTS__TERM_TYPE_USER,
					$1, $3, false, &@1, &@3));
	$$ = term;
}
|
PE_NAME '=' PE_VALUE_SYM_HW
{
	struct parse_events_term *term;
	int config = $3 & 255;

	ABORT_ON(parse_events_term__sym_hw(&term, $1, config));
	$$ = term;
}
|
PE_NAME
{
	struct parse_events_term *term;

	ABORT_ON(parse_events_term__num(&term, PARSE_EVENTS__TERM_TYPE_USER,
					$1, 1, true, &@1, NULL));
	$$ = term;
}
|
PE_VALUE_SYM_HW
{
	struct parse_events_term *term;
	int config = $1 & 255;

	ABORT_ON(parse_events_term__sym_hw(&term, NULL, config));
	$$ = term;
}
|
PE_TERM '=' PE_NAME
{
	struct parse_events_term *term;

	ABORT_ON(parse_events_term__str(&term, (int)$1, NULL, $3, &@1, &@3));
	$$ = term;
}
|
PE_TERM '=' PE_VALUE
{
	struct parse_events_term *term;

	ABORT_ON(parse_events_term__num(&term, (int)$1, NULL, $3, false, &@1, &@3));
	$$ = term;
}
|
PE_TERM
{
	struct parse_events_term *term;

	ABORT_ON(parse_events_term__num(&term, (int)$1, NULL, 1, true, &@1, NULL));
	$$ = term;
}
|
PE_NAME array '=' PE_NAME
{
	struct parse_events_term *term;
	int i;

	ABORT_ON(parse_events_term__str(&term, PARSE_EVENTS__TERM_TYPE_USER,
					$1, $4, &@1, &@4));

	term->array = $2;
	$$ = term;
}
|
PE_NAME array '=' PE_VALUE
{
	struct parse_events_term *term;

	ABORT_ON(parse_events_term__num(&term, PARSE_EVENTS__TERM_TYPE_USER,
					$1, $4, false, &@1, &@4));
	term->array = $2;
	$$ = term;
}
|
PE_DRV_CFG_TERM
{
	struct parse_events_term *term;

	ABORT_ON(parse_events_term__str(&term, PARSE_EVENTS__TERM_TYPE_DRV_CFG,
					$1, $1, &@1, NULL));
	$$ = term;
}

array:
'[' array_terms ']'
{
	$$ = $2;
}
|
PE_ARRAY_ALL
{
	$$.nr_ranges = 0;
	$$.ranges = NULL;
}

array_terms:
array_terms ',' array_term
{
	struct parse_events_array new_array;

	new_array.nr_ranges = $1.nr_ranges + $3.nr_ranges;
	new_array.ranges = malloc(sizeof(new_array.ranges[0]) *
				  new_array.nr_ranges);
	ABORT_ON(!new_array.ranges);
	memcpy(&new_array.ranges[0], $1.ranges,
	       $1.nr_ranges * sizeof(new_array.ranges[0]));
	memcpy(&new_array.ranges[$1.nr_ranges], $3.ranges,
	       $3.nr_ranges * sizeof(new_array.ranges[0]));
	free($1.ranges);
	free($3.ranges);
	$$ = new_array;
}
|
array_term

array_term:
PE_VALUE
{
	struct parse_events_array array;

	array.nr_ranges = 1;
	array.ranges = malloc(sizeof(array.ranges[0]));
	ABORT_ON(!array.ranges);
	array.ranges[0].start = $1;
	array.ranges[0].length = 1;
	$$ = array;
}
|
PE_VALUE PE_ARRAY_RANGE PE_VALUE
{
	struct parse_events_array array;

	ABORT_ON($3 < $1);
	array.nr_ranges = 1;
	array.ranges = malloc(sizeof(array.ranges[0]));
	ABORT_ON(!array.ranges);
	array.ranges[0].start = $1;
	array.ranges[0].length = $3 - $1 + 1;
	$$ = array;
}

sep_dc: ':' |

sep_slash_dc: '/' | ':' |

%%

void parse_events_error(YYLTYPE *loc, void *parse_state,
			void *scanner __maybe_unused,
			char const *msg __maybe_unused)
{
	parse_events_evlist_error(parse_state, loc->last_column, "parser error");
}
