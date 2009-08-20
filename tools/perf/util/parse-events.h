
/*
 * Parse symbolic events/counts passed in as options:
 */

struct option;

extern int			nr_counters;

extern struct perf_counter_attr attrs[MAX_COUNTERS];

extern char *event_name(int ctr);
extern char *__event_name(int type, u64 config);

extern int parse_events(const struct option *opt, const char *str, int unset);

#define EVENTS_HELP_MAX (128*1024)

extern void print_events(void);

extern char debugfs_path[];
extern int valid_debugfs_mount(const char *debugfs);

