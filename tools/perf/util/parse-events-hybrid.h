/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_PARSE_EVENTS_HYBRID_H
#define __PERF_PARSE_EVENTS_HYBRID_H

#include <linux/list.h>
#include <stdbool.h>
#include <linux/types.h>
#include <linux/perf_event.h>
#include <string.h>

int parse_events__add_numeric_hybrid(struct parse_events_state *parse_state,
				     struct list_head *list,
				     struct perf_event_attr *attr,
				     char *name, struct list_head *config_terms,
				     bool *hybrid);

int parse_events__add_cache_hybrid(struct list_head *list, int *idx,
				   struct perf_event_attr *attr, char *name,
				   struct list_head *config_terms,
				   bool *hybrid,
				   struct parse_events_state *parse_state);

#endif /* __PERF_PARSE_EVENTS_HYBRID_H */
