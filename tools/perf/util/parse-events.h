#ifndef __PERF_PARSE_EVENTS_H
#define __PERF_PARSE_EVENTS_H
/*
 * Parse symbolic events/counts passed in as options:
 */

#include <linux/list.h>
#include <stdbool.h>
#include <linux/types.h>
#include <linux/perf_event.h>
#include <string.h>

struct list_head;
struct perf_evsel;
struct perf_evlist;
struct parse_events_error;

struct option;

struct tracepoint_path {
	char *system;
	char *name;
	struct tracepoint_path *next;
};

struct tracepoint_path *tracepoint_id_to_path(u64 config);
struct tracepoint_path *tracepoint_name_to_path(const char *name);
bool have_tracepoints(struct list_head *evlist);

const char *event_type(int type);

int parse_events_option(const struct option *opt, const char *str, int unset);
int parse_events(struct perf_evlist *evlist, const char *str,
		 struct parse_events_error *error);
int parse_events_terms(struct list_head *terms, const char *str);
int parse_filter(const struct option *opt, const char *str, int unset);
int exclude_perf(const struct option *opt, const char *arg, int unset);

#define EVENTS_HELP_MAX (128*1024)

enum perf_pmu_event_symbol_type {
	PMU_EVENT_SYMBOL_ERR,		/* not a PMU EVENT */
	PMU_EVENT_SYMBOL,		/* normal style PMU event */
	PMU_EVENT_SYMBOL_PREFIX,	/* prefix of pre-suf style event */
	PMU_EVENT_SYMBOL_SUFFIX,	/* suffix of pre-suf style event */
};

struct perf_pmu_event_symbol {
	char	*symbol;
	enum perf_pmu_event_symbol_type	type;
};

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
	PARSE_EVENTS__TERM_TYPE_SAMPLE_FREQ,
	PARSE_EVENTS__TERM_TYPE_BRANCH_SAMPLE_TYPE,
	PARSE_EVENTS__TERM_TYPE_TIME,
	PARSE_EVENTS__TERM_TYPE_CALLGRAPH,
	PARSE_EVENTS__TERM_TYPE_STACKSIZE,
	PARSE_EVENTS__TERM_TYPE_NOINHERIT,
	PARSE_EVENTS__TERM_TYPE_INHERIT,
	PARSE_EVENTS__TERM_TYPE_MAX_STACK,
	PARSE_EVENTS__TERM_TYPE_NOOVERWRITE,
	PARSE_EVENTS__TERM_TYPE_OVERWRITE,
	PARSE_EVENTS__TERM_TYPE_DRV_CFG,
	__PARSE_EVENTS__TERM_TYPE_NR,
};

struct parse_events_array {
	size_t nr_ranges;
	struct {
		unsigned int start;
		size_t length;
	} *ranges;
};

struct parse_events_term {
	char *config;
	struct parse_events_array array;
	union {
		char *str;
		u64  num;
	} val;
	int type_val;
	int type_term;
	struct list_head list;
	bool used;
	bool no_value;

	/* error string indexes for within parsed string */
	int err_term;
	int err_val;
};

struct parse_events_error {
	int   idx;	/* index in the parsed string */
	char *str;      /* string to display at the index */
	char *help;	/* optional help string */
};

struct parse_events_evlist {
	struct list_head	   list;
	int			   idx;
	int			   nr_groups;
	struct parse_events_error *error;
	struct perf_evlist	  *evlist;
};

struct parse_events_terms {
	struct list_head *terms;
};

void parse_events__shrink_config_terms(void);
int parse_events__is_hardcoded_term(struct parse_events_term *term);
int parse_events_term__num(struct parse_events_term **term,
			   int type_term, char *config, u64 num,
			   bool novalue,
			   void *loc_term, void *loc_val);
int parse_events_term__str(struct parse_events_term **term,
			   int type_term, char *config, char *str,
			   void *loc_term, void *loc_val);
int parse_events_term__sym_hw(struct parse_events_term **term,
			      char *config, unsigned idx);
int parse_events_term__clone(struct parse_events_term **new,
			     struct parse_events_term *term);
void parse_events_terms__delete(struct list_head *terms);
void parse_events_terms__purge(struct list_head *terms);
void parse_events__clear_array(struct parse_events_array *a);
int parse_events__modifier_event(struct list_head *list, char *str, bool add);
int parse_events__modifier_group(struct list_head *list, char *event_mod);
int parse_events_name(struct list_head *list, char *name);
int parse_events_add_tracepoint(struct list_head *list, int *idx,
				const char *sys, const char *event,
				struct parse_events_error *error,
				struct list_head *head_config);
int parse_events_load_bpf(struct parse_events_evlist *data,
			  struct list_head *list,
			  char *bpf_file_name,
			  bool source,
			  struct list_head *head_config);
/* Provide this function for perf test */
struct bpf_object;
int parse_events_load_bpf_obj(struct parse_events_evlist *data,
			      struct list_head *list,
			      struct bpf_object *obj,
			      struct list_head *head_config);
int parse_events_add_numeric(struct parse_events_evlist *data,
			     struct list_head *list,
			     u32 type, u64 config,
			     struct list_head *head_config);
int parse_events_add_cache(struct list_head *list, int *idx,
			   char *type, char *op_result1, char *op_result2,
			   struct parse_events_error *error,
			   struct list_head *head_config);
int parse_events_add_breakpoint(struct list_head *list, int *idx,
				void *ptr, char *type, u64 len);
int parse_events_add_pmu(struct parse_events_evlist *data,
			 struct list_head *list, char *name,
			 struct list_head *head_config);

int parse_events_multi_pmu_add(struct parse_events_evlist *data,
			       char *str,
			       struct list_head **listp);

int parse_events_copy_term_list(struct list_head *old,
				 struct list_head **new);

enum perf_pmu_event_symbol_type
perf_pmu__parse_check(const char *name);
void parse_events__set_leader(char *name, struct list_head *list);
void parse_events_update_lists(struct list_head *list_event,
			       struct list_head *list_all);
void parse_events_evlist_error(struct parse_events_evlist *data,
			       int idx, const char *str);

void print_events(const char *event_glob, bool name_only, bool quiet,
		  bool long_desc, bool details_flag);

struct event_symbol {
	const char	*symbol;
	const char	*alias;
};
extern struct event_symbol event_symbols_hw[];
extern struct event_symbol event_symbols_sw[];
void print_symbol_events(const char *event_glob, unsigned type,
				struct event_symbol *syms, unsigned max,
				bool name_only);
void print_tracepoint_events(const char *subsys_glob, const char *event_glob,
			     bool name_only);
int print_hwcache_events(const char *event_glob, bool name_only);
void print_sdt_events(const char *subsys_glob, const char *event_glob,
		      bool name_only);
int is_valid_tracepoint(const char *event_string);

int valid_event_mount(const char *eventfs);
char *parse_events_formats_error_string(char *additional_terms);

#ifdef HAVE_LIBELF_SUPPORT
/*
 * If the probe point starts with '%',
 * or starts with "sdt_" and has a ':' but no '=',
 * then it should be a SDT/cached probe point.
 */
static inline bool is_sdt_event(char *str)
{
	return (str[0] == '%' ||
		(!strncmp(str, "sdt_", 4) &&
		 !!strchr(str, ':') && !strchr(str, '=')));
}
#else
static inline bool is_sdt_event(char *str __maybe_unused)
{
	return false;
}
#endif /* HAVE_LIBELF_SUPPORT */

#endif /* __PERF_PARSE_EVENTS_H */
