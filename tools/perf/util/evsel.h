#ifndef __PERF_EVSEL_H
#define __PERF_EVSEL_H 1

#include <linux/list.h>
#include <linux/perf_event.h>
#include "types.h"
#include "xyarray.h"

struct perf_evsel {
	struct list_head	node;
	struct perf_event_attr	attr;
	char			*filter;
	struct xyarray		*fd;
	int			idx;
	void			*priv;
};

struct perf_evsel *perf_evsel__new(u32 type, u64 config, int idx);
void perf_evsel__delete(struct perf_evsel *evsel);

int perf_evsel__alloc_fd(struct perf_evsel *evsel, int ncpus, int nthreads);
void perf_evsel__free_fd(struct perf_evsel *evsel);

#endif /* __PERF_EVSEL_H */
