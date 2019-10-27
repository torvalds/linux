/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_EVLIST_H
#define __LIBPERF_INTERNAL_EVLIST_H

#include <linux/list.h>
#include <api/fd/array.h>
#include <internal/evsel.h>

#define PERF_EVLIST__HLIST_BITS 8
#define PERF_EVLIST__HLIST_SIZE (1 << PERF_EVLIST__HLIST_BITS)

struct perf_cpu_map;
struct perf_thread_map;

struct perf_evlist {
	struct list_head	 entries;
	int			 nr_entries;
	bool			 has_user_cpus;
	struct perf_cpu_map	*cpus;
	struct perf_thread_map	*threads;
	int			 nr_mmaps;
	size_t			 mmap_len;
	struct fdarray		 pollfd;
	struct hlist_head	 heads[PERF_EVLIST__HLIST_SIZE];
};

int perf_evlist__alloc_pollfd(struct perf_evlist *evlist);
int perf_evlist__add_pollfd(struct perf_evlist *evlist, int fd,
			    void *ptr, short revent);

/**
 * __perf_evlist__for_each_entry - iterate thru all the evsels
 * @list: list_head instance to iterate
 * @evsel: struct perf_evsel iterator
 */
#define __perf_evlist__for_each_entry(list, evsel) \
	list_for_each_entry(evsel, list, node)

/**
 * evlist__for_each_entry - iterate thru all the evsels
 * @evlist: perf_evlist instance to iterate
 * @evsel: struct perf_evsel iterator
 */
#define perf_evlist__for_each_entry(evlist, evsel) \
	__perf_evlist__for_each_entry(&(evlist)->entries, evsel)

/**
 * __perf_evlist__for_each_entry_reverse - iterate thru all the evsels in reverse order
 * @list: list_head instance to iterate
 * @evsel: struct evsel iterator
 */
#define __perf_evlist__for_each_entry_reverse(list, evsel) \
	list_for_each_entry_reverse(evsel, list, node)

/**
 * perf_evlist__for_each_entry_reverse - iterate thru all the evsels in reverse order
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 */
#define perf_evlist__for_each_entry_reverse(evlist, evsel) \
	__perf_evlist__for_each_entry_reverse(&(evlist)->entries, evsel)

static inline struct perf_evsel *perf_evlist__first(struct perf_evlist *evlist)
{
	return list_entry(evlist->entries.next, struct perf_evsel, node);
}

static inline struct perf_evsel *perf_evlist__last(struct perf_evlist *evlist)
{
	return list_entry(evlist->entries.prev, struct perf_evsel, node);
}

u64 perf_evlist__read_format(struct perf_evlist *evlist);

void perf_evlist__id_add(struct perf_evlist *evlist,
			 struct perf_evsel *evsel,
			 int cpu, int thread, u64 id);

int perf_evlist__id_add_fd(struct perf_evlist *evlist,
			   struct perf_evsel *evsel,
			   int cpu, int thread, int fd);

#endif /* __LIBPERF_INTERNAL_EVLIST_H */
