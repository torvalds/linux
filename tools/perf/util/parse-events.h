#ifndef __PERF_PARSE_EVENTS_H
#define __PERF_PARSE_EVENTS_H
/*
 * Parse symbolic events/counts passed in as options:
 */

#include <linux/list.h>
#include <stdbool.h>
#include <linux/types.h>
#include <linux/perf_event.h>

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
extern struct tracepoint_path *tracepoint_name_to_path(const char *name);
extern bool have_tracepoints(struct list_head *evlist);

const char *event_type(int type);

extern int parse_events_option(const struct option *opt, const char *str,
			       int unset);
extern int parse_events(struct perf_evlist *evlist, const char *str);
extern int parse_events_terms(struct list_head *terms, const char *str);
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
	PARSE_EVENTS__TERM_TYPE_NAME,
	PARSE_EVENTS__TERM_TYPE_SAMPLE_PERIOD,
	PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE,
};

struct parse_events_term {
	char *config;
	union {
		char *str;
		u64  num;
	} val;
	int type_val;
	int type_term;
	struct list_head list;
};

struct parse_events_evlist {
	struct list_head list;
	int idx;
	int nr_groups;
};

struct parse_events_terms {
	struct list_head *terms;
};

int parse_events__is_hardcoded_term(struct parse_events_term *term);
int parse_events_term__num(struct parse_events_term **_term,
			   int type_term, char *config, u64 num);
int parse_events_term__str(struct parse_events_term **_term,
			   int type_term, char *config, char *str);
int parse_events_term__sym_hw(struct parse_events_term **term,
			      char *config, unsigned idx);
int parse_events_term__clone(struct parse_events_term **new,
			     struct parse_events_term *term);
void parse_events__free_terms(struct list_head *terms);
int parse_events__modifier_event(struct list_head *list, char *str, bool add);
int parse_events__modifier_group(struct list_head *list, char *event_mod);
int parse_events_name(struct list_head *list, char *name);
int parse_events_add_tracepoint(struct list_head *list, int *idx,
				char *sys, char *event);
int parse_events_add_numeric(struct list_head *list, int *idx,
			     u32 type, u64 config,
			     struct list_head *head_config);
int parse_events_add_cache(struct list_head *list, int *idx,
			   char *type, char *op_result1, char *op_result2);
int parse_events_add_breakpoint(struct list_head *list, int *idx,
				void *ptr, char *type);
int parse_events_add_pmu(struct list_head *list, int *idx,
			 char *pmu , struct list_head *head_config);
void parse_events__set_leader(char *name, struct list_head *list);
void parse_events_update_lists(struct list_head *list_event,
			       struct list_head *list_all);
void parse_events_error(void *data, void *scanner, char const *msg);

void print_events(const char *event_glob, bool name_only);
void print_events_type(u8 type);
void print_tracepoint_events(const char *subsys_glob, const char *event_glob,
			     bool name_only);
int print_hwcache_events(const char *event_glob, bool name_only);
extern int is_valid_tracepoint(const char *event_string);

extern int valid_debugfs_mount(const char *debugfs);

#endif /* __PERF_PARSE_EVENTS_H */
