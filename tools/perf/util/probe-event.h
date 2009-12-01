#ifndef _PROBE_EVENT_H
#define _PROBE_EVENT_H

#include "probe-finder.h"

extern int parse_perf_probe_event(const char *str, struct probe_point *pp);
extern int synthesize_trace_kprobe_event(struct probe_point *pp);
extern void add_trace_kprobe_events(struct probe_point *probes, int nr_probes);

#endif /*_PROBE_EVENT_H */
