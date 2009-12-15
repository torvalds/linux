#ifndef _PROBE_EVENT_H
#define _PROBE_EVENT_H

#include <stdbool.h>
#include "probe-finder.h"
#include "strlist.h"

extern void parse_perf_probe_event(const char *str, struct probe_point *pp,
				   bool *need_dwarf);
extern int synthesize_perf_probe_point(struct probe_point *pp);
extern int synthesize_perf_probe_event(struct probe_point *pp);
extern void parse_trace_kprobe_event(const char *str, struct probe_point *pp);
extern int synthesize_trace_kprobe_event(struct probe_point *pp);
extern void add_trace_kprobe_events(struct probe_point *probes, int nr_probes,
				    bool force_add);
extern void del_trace_kprobe_events(struct strlist *dellist);
extern void show_perf_probe_events(void);

/* Maximum index number of event-name postfix */
#define MAX_EVENT_INDEX	1024

#endif /*_PROBE_EVENT_H */
