#ifndef _PARSE_EVENTS_H
#define _PARSE_EVENTS_H
/*
 * Parse symbolic events/counts passed in as options:
 */

struct option;

struct tracepoint_path {
	char *system;
	char *name;
	struct tracepoint_path *next;
};

extern struct tracepoint_path *tracepoint_id_to_path(u64 config);

extern int			nr_counters;

extern struct perf_event_attr attrs[MAX_COUNTERS];

extern const char *event_name(int ctr);
extern const char *__event_name(int type, u64 config);

extern int parse_events(const struct option *opt, const char *str, int unset);

#define EVENTS_HELP_MAX (128*1024)

extern void print_events(void);

extern char debugfs_path[];
extern int valid_debugfs_mount(const char *debugfs);


#endif /* _PARSE_EVENTS_H */
