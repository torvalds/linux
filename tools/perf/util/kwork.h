#ifndef PERF_UTIL_KWORK_H
#define PERF_UTIL_KWORK_H

#include "perf.h"

#include "util/tool.h"
#include "util/event.h"
#include "util/evlist.h"
#include "util/session.h"
#include "util/time-utils.h"

#include <linux/list.h>
#include <linux/bitmap.h>

enum kwork_class_type {
	KWORK_CLASS_IRQ,
	KWORK_CLASS_SOFTIRQ,
	KWORK_CLASS_MAX,
};

struct kwork_class {
	struct list_head list;
	const char *name;
	enum kwork_class_type type;

	unsigned int nr_tracepoints;
	const struct evsel_str_handler *tp_handlers;
};

struct perf_kwork {
	/*
	 * metadata
	 */
	struct list_head class_list;

	/*
	 * options for command
	 */
	bool force;
	const char *event_list_str;
};

#endif  /* PERF_UTIL_KWORK_H */
