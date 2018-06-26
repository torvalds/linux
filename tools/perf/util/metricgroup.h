#ifndef METRICGROUP_H
#define METRICGROUP_H 1

#include "linux/list.h"
#include "rblist.h"
#include <subcmd/parse-options.h>
#include "evlist.h"
#include "strbuf.h"

struct metric_event {
	struct rb_node nd;
	struct perf_evsel *evsel;
	struct list_head head; /* list of metric_expr */
};

struct metric_expr {
	struct list_head nd;
	const char *metric_expr;
	const char *metric_name;
	struct perf_evsel **metric_events;
};

struct metric_event *metricgroup__lookup(struct rblist *metric_events,
					 struct perf_evsel *evsel,
					 bool create);
int metricgroup__parse_groups(const struct option *opt,
			const char *str,
			struct rblist *metric_events);

void metricgroup__print(bool metrics, bool groups, char *filter, bool raw);
bool metricgroup__has_metric(const char *metric);
#endif
