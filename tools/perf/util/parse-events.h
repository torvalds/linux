#ifndef __PERF_PARSE_EVENTS_H
#define __PERF_PARSE_EVENTS_H
/*
 * Parse symbolic events/counts passed in as options:
 */

#include "../../../include/linux/perf_event.h"

struct list_head;
struct perf_evsel;

extern struct list_head evsel_list;

int perf_evsel_list__create_default(void);
void perf_evsel_list__delete(void);

struct option;

struct tracepoint_path {
	char *system;
	char *name;
	struct tracepoint_path *next;
};

extern struct tracepoint_path *tracepoint_id_to_path(u64 config);
extern bool have_tracepoints(struct list_head *evlist);

extern int			nr_counters;

const char *event_name(struct perf_evsel *event);
extern const char *__event_name(int type, u64 config);

extern int parse_events(const struct option *opt, const char *str, int unset);
extern int parse_filter(const struct option *opt, const char *str, int unset);

#define EVENTS_HELP_MAX (128*1024)

extern void print_events(void);
extern int is_valid_tracepoint(const char *event_string);

extern char debugfs_path[];
extern int valid_debugfs_mount(const char *debugfs);

#endif /* __PERF_PARSE_EVENTS_H */
