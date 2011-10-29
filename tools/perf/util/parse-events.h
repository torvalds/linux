#ifndef __PERF_PARSE_EVENTS_H
#define __PERF_PARSE_EVENTS_H
/*
 * Parse symbolic events/counts passed in as options:
 */

#include "../../../include/linux/perf_event.h"

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

void print_events(const char *event_glob);
void print_events_type(u8 type);
void print_tracepoint_events(const char *subsys_glob, const char *event_glob);
int print_hwcache_events(const char *event_glob);
extern int is_valid_tracepoint(const char *event_string);

extern char debugfs_path[];
extern int valid_debugfs_mount(const char *debugfs);

#endif /* __PERF_PARSE_EVENTS_H */
