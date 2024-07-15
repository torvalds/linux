/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_EVLIST_H
#define __LIBPERF_INTERNAL_EVLIST_H

#include <linux/list.h>
#include <api/fd/array.h>
#include <internal/cpumap.h>
#include <internal/evsel.h>

#define PERF_EVLIST__HLIST_BITS 8
#define PERF_EVLIST__HLIST_SIZE (1 << PERF_EVLIST__HLIST_BITS)

struct perf_cpu_map;
struct perf_thread_map;
struct perf_mmap_param;

struct perf_evlist {
	struct list_head	 entries;
	int			 nr_entries;
	bool			 has_user_cpus;
	bool			 needs_map_propagation;
	/**
	 * The cpus passed from the command line or all online CPUs by
	 * default.
	 */
	struct perf_cpu_map	*user_requested_cpus;
	/** The union of all evsel cpu maps. */
	struct perf_cpu_map	*all_cpus;
	struct perf_thread_map	*threads;
	int			 nr_mmaps;
	size_t			 mmap_len;
	struct fdarray		 pollfd;
	struct hlist_head	 heads[PERF_EVLIST__HLIST_SIZE];
	struct perf_mmap	*mmap;
	struct perf_mmap	*mmap_ovw;
	struct perf_mmap	*mmap_first;
	struct perf_mmap	*mmap_ovw_first;
};

typedef void
(*perf_evlist_mmap__cb_idx_t)(struct perf_evlist*, struct perf_evsel*,
			      struct perf_mmap_param*, int);
typedef struct perf_mmap*
(*perf_evlist_mmap__cb_get_t)(struct perf_evlist*, bool, int);
typedef int
(*perf_evlist_mmap__cb_mmap_t)(struct perf_mmap*, struct perf_mmap_param*, int, struct perf_cpu);

struct perf_evlist_mmap_ops {
	perf_evlist_mmap__cb_idx_t	idx;
	perf_evlist_mmap__cb_get_t	get;
	perf_evlist_mmap__cb_mmap_t	mmap;
};

int perf_evlist__alloc_pollfd(struct perf_evlist *evlist);
int perf_evlist__add_pollfd(struct perf_evlist *evlist, int fd,
			    void *ptr, short revent, enum fdarray_flags flags);

int perf_evlist__mmap_ops(struct perf_evlist *evlist,
			  struct perf_evlist_mmap_ops *ops,
			  struct perf_mmap_param *mp);

void perf_evlist__init(struct perf_evlist *evlist);
void perf_evlist__exit(struct perf_evlist *evlist);

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

/**
 * __perf_evlist__for_each_entry_safe - safely iterate thru all the evsels
 * @list: list_head instance to iterate
 * @tmp: struct evsel temp iterator
 * @evsel: struct evsel iterator
 */
#define __perf_evlist__for_each_entry_safe(list, tmp, evsel) \
	list_for_each_entry_safe(evsel, tmp, list, node)

/**
 * perf_evlist__for_each_entry_safe - safely iterate thru all the evsels
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 * @tmp: struct evsel temp iterator
 */
#define perf_evlist__for_each_entry_safe(evlist, tmp, evsel) \
	__perf_evlist__for_each_entry_safe(&(evlist)->entries, tmp, evsel)

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
			 int cpu_map_idx, int thread, u64 id);

int perf_evlist__id_add_fd(struct perf_evlist *evlist,
			   struct perf_evsel *evsel,
			   int cpu_map_idx, int thread, int fd);

void perf_evlist__reset_id_hash(struct perf_evlist *evlist);

void __perf_evlist__set_leader(struct list_head *list, struct perf_evsel *leader);

void perf_evlist__go_system_wide(struct perf_evlist *evlist, struct perf_evsel *evsel);
#endif /* __LIBPERF_INTERNAL_EVLIST_H */
