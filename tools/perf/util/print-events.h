/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_PRINT_EVENTS_H
#define __PERF_PRINT_EVENTS_H

#include <linux/perf_event.h>
#include <stdbool.h>

struct event_symbol;

struct print_callbacks {
	void (*print_start)(void *print_state);
	void (*print_end)(void *print_state);
	void (*print_event)(void *print_state, const char *topic,
			const char *pmu_name,
			const char *event_name, const char *event_alias,
			const char *scale_unit,
			bool deprecated, const char *event_type_desc,
			const char *desc, const char *long_desc,
			const char *encoding_desc);
	void (*print_metric)(void *print_state,
			const char *group,
			const char *name,
			const char *desc,
			const char *long_desc,
			const char *expr,
			const char *unit);
};

/** Print all events, the default when no options are specified. */
void print_events(const struct print_callbacks *print_cb, void *print_state);
int print_hwcache_events(const struct print_callbacks *print_cb, void *print_state);
void print_sdt_events(const struct print_callbacks *print_cb, void *print_state);
void print_symbol_events(const struct print_callbacks *print_cb, void *print_state,
			 unsigned int type, const struct event_symbol *syms,
			 unsigned int max);
void print_tool_events(const struct print_callbacks *print_cb, void *print_state);
void print_tracepoint_events(const struct print_callbacks *print_cb, void *print_state);

#endif /* __PERF_PRINT_EVENTS_H */
