/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_PRINT_EVENTS_H
#define __PERF_PRINT_EVENTS_H

#include <stdbool.h>

struct event_symbol;

void print_events(const char *event_glob, bool name_only, bool quiet_flag,
		  bool long_desc, bool details_flag, bool deprecated,
		  const char *pmu_name);
int print_hwcache_events(const char *event_glob, bool name_only);
void print_sdt_events(const char *subsys_glob, const char *event_glob,
		      bool name_only);
void print_symbol_events(const char *event_glob, unsigned int type,
			 struct event_symbol *syms, unsigned int max,
			 bool name_only);
void print_tool_events(const char *event_glob, bool name_only);
void print_tracepoint_events(const char *subsys_glob, const char *event_glob,
			     bool name_only);

#endif /* __PERF_PRINT_EVENTS_H */
