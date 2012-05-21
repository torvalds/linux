#ifndef __PERF_PARSE_EVENTS_H
#define __PERF_PARSE_EVENTS_H
/*
 * Parse symbolic events/counts passed in as options:
 */

#include <linux/list.h>
#include <stdbool.h>
#include "types.h"
#include "../../../include/linux/perf_event.h"
#include "types.h"

struct list_head;
struct perf_evsel;
struct perf_evlist;

struct option;

struct tracepoint_path {
	char *system;
	char *name;
	struct tracepoint_path *next;
};

extern struct tracepoint_path *tracepoint_id_to_path(u64 config);
extern bool have_tracepoints(struct list_head *evlist);

const char *event_type(int type);
const char *event_name(struct perf_evsel *event);
extern const char *__event_name(int type, u64 config);

extern int parse_events_option(const struct option *opt, const char *str,
			       int unset);
extern int parse_events(struct perf_evlist *evlist, const char *str,
			int unset);
extern int parse_filter(const struct option *opt, const char *str, int unset);

#define EVENTS_HELP_MAX (128*1024)

enum {
	PARSE_EVENTS__TERM_TYPE_NUM,
	PARSE_EVENTS__TERM_TYPE_STR,
};

enum {
	PARSE_EVENTS__TERM_TYPE_USER,
	PARSE_EVENTS__TERM_TYPE_CONFIG,
	PARSE_EVENTS__TERM_TYPE_CONFIG1,
	PARSE_EVENTS__TERM_TYPE_CONFIG2,
	PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD,
	PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE,
};

struct parse_events__term {
	char *config;
	union {
		char *str;
		long  num;
	} val;
	int type_val;
	int type_term;
	struct list_head list;
};

int parse_events__is_hardcoded_term(struct parse_events__term *term);
int parse_events__term_num(struct parse_events__term **_term,
			   int type_term, char *config, long num);
int parse_events__term_str(struct parse_events__term **_term,
			   int type_term, char *config, char *str);
void parse_events__free_terms(struct list_head *terms);
int parse_events_modifier(struct list_head *list, char *str);
int parse_events_add_tracepoint(struct list_head **list, int *idx,
				char *sys, char *event);
int parse_events_add_numeric(struct list_head **list, int *idx,
			     unsigned long type, unsigned long config,
			     struct list_head *head_config);
int parse_events_add_cache(struct list_head **list, int *idx,
			   char *type, char *op_result1, char *op_result2);
int parse_events_add_breakpoint(struct list_head **list, int *idx,
				void *ptr, char *type);
int parse_events_add_pmu(struct list_head **list, int *idx,
			 char *pmu , struct list_head *head_config);
void parse_events_update_lists(struct list_head *list_event,
			       struct list_head *list_all);
void parse_events_error(struct list_head *list_all,
			int *idx, char const *msg);
int parse_events__test(void);

void print_events(const char *event_glob);
void print_events_type(u8 type);
void print_tracepoint_events(const char *subsys_glob, const char *event_glob);
int print_hwcache_events(const char *event_glob);
extern int is_valid_tracepoint(const char *event_string);

extern int valid_debugfs_mount(const char *debugfs);

#endif /* __PERF_PARSE_EVENTS_H */
