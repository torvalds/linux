#ifndef __PERF_EVLIST_H
#define __PERF_EVLIST_H 1

#include <linux/list.h>

struct pollfd;

struct perf_evlist {
	struct list_head entries;
	int		 nr_entries;
	int		 nr_fds;
	struct pollfd	 *pollfd;
};

struct perf_evsel;

struct perf_evlist *perf_evlist__new(void);
void perf_evlist__delete(struct perf_evlist *evlist);

void perf_evlist__add(struct perf_evlist *evlist, struct perf_evsel *entry);
int perf_evlist__add_default(struct perf_evlist *evlist);

int perf_evlist__alloc_pollfd(struct perf_evlist *evlist, int ncpus, int nthreads);

#endif /* __PERF_EVLIST_H */
