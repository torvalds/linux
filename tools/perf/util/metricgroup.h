// SPDX-License-Identifier: GPL-2.0-only
#ifndef METRICGROUP_H
#define METRICGROUP_H 1

#include <linux/list.h>
#include <linux/rbtree.h>
#include <stdbool.h>

struct evsel;
struct option;
struct rblist;

struct metric_event {
	struct rb_node nd;
	struct evsel *evsel;
	struct list_head head; /* list of metric_expr */
};

struct metric_expr {
	struct list_head nd;
	const char *metric_expr;
	const char *metric_name;
	const char *metric_unit;
	struct evsel **metric_events;
	int runtime;
};

struct metric_event *metricgroup__lookup(struct rblist *metric_events,
					 struct evsel *evsel,
					 bool create);
int metricgroup__parse_groups(const struct option *opt,
			      const char *str,
			      bool metric_no_group,
			      bool metric_no_merge,
			      struct rblist *metric_events);

void metricgroup__print(bool metrics, bool groups, char *filter,
			bool raw, bool details);
bool metricgroup__has_metric(const char *metric);
int arch_get_runtimeparam(void);
#endif
