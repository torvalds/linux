/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_EVLIST_H
#define __PERF_EVLIST_H 1

#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/refcount.h>
#include <linux/list.h>
#include <api/fd/array.h>
#include <stdio.h>
#include "../perf.h"
#include "event.h"
#include "evsel.h"
#include "mmap.h"
#include "util.h"
#include <signal.h>
#include <unistd.h>

struct pollfd;
struct thread_map;
struct cpu_map;
struct record_opts;

#define PERF_EVLIST__HLIST_BITS 8
#define PERF_EVLIST__HLIST_SIZE (1 << PERF_EVLIST__HLIST_BITS)

struct perf_evlist {
	struct list_head entries;
	struct hlist_head heads[PERF_EVLIST__HLIST_SIZE];
	int		 nr_entries;
	int		 nr_groups;
	int		 nr_mmaps;
	bool		 enabled;
	bool		 has_user_cpus;
	size_t		 mmap_len;
	int		 id_pos;
	int		 is_pos;
	u64		 combined_sample_type;
	enum bkw_mmap_state bkw_mmap_state;
	struct {
		int	cork_fd;
		pid_t	pid;
	} workload;
	struct fdarray	 pollfd;
	struct perf_mmap *mmap;
	struct perf_mmap *overwrite_mmap;
	struct thread_map *threads;
	struct cpu_map	  *cpus;
	struct perf_evsel *selected;
	struct events_stats stats;
	struct perf_env	*env;
	void (*trace_event_sample_raw)(struct perf_evlist *evlist,
				       union perf_event *event,
				       struct perf_sample *sample);
	u64		first_sample_time;
	u64		last_sample_time;
	struct {
		pthread_t		th;
		volatile int		done;
	} thread;
};

struct perf_evsel_str_handler {
	const char *name;
	void	   *handler;
};

struct perf_evlist *perf_evlist__new(void);
struct perf_evlist *perf_evlist__new_default(void);
struct perf_evlist *perf_evlist__new_dummy(void);
void perf_evlist__init(struct perf_evlist *evlist, struct cpu_map *cpus,
		       struct thread_map *threads);
void perf_evlist__exit(struct perf_evlist *evlist);
void perf_evlist__delete(struct perf_evlist *evlist);

void perf_evlist__add(struct perf_evlist *evlist, struct perf_evsel *entry);
void perf_evlist__remove(struct perf_evlist *evlist, struct perf_evsel *evsel);

int __perf_evlist__add_default(struct perf_evlist *evlist, bool precise);

static inline int perf_evlist__add_default(struct perf_evlist *evlist)
{
	return __perf_evlist__add_default(evlist, true);
}

int __perf_evlist__add_default_attrs(struct perf_evlist *evlist,
				     struct perf_event_attr *attrs, size_t nr_attrs);

#define perf_evlist__add_default_attrs(evlist, array) \
	__perf_evlist__add_default_attrs(evlist, array, ARRAY_SIZE(array))

int perf_evlist__add_dummy(struct perf_evlist *evlist);

int perf_evlist__add_sb_event(struct perf_evlist **evlist,
			      struct perf_event_attr *attr,
			      perf_evsel__sb_cb_t cb,
			      void *data);
int perf_evlist__start_sb_thread(struct perf_evlist *evlist,
				 struct target *target);
void perf_evlist__stop_sb_thread(struct perf_evlist *evlist);

int perf_evlist__add_newtp(struct perf_evlist *evlist,
			   const char *sys, const char *name, void *handler);

void __perf_evlist__set_sample_bit(struct perf_evlist *evlist,
				   enum perf_event_sample_format bit);
void __perf_evlist__reset_sample_bit(struct perf_evlist *evlist,
				     enum perf_event_sample_format bit);

#define perf_evlist__set_sample_bit(evlist, bit) \
	__perf_evlist__set_sample_bit(evlist, PERF_SAMPLE_##bit)

#define perf_evlist__reset_sample_bit(evlist, bit) \
	__perf_evlist__reset_sample_bit(evlist, PERF_SAMPLE_##bit)

int perf_evlist__set_tp_filter(struct perf_evlist *evlist, const char *filter);
int perf_evlist__set_tp_filter_pid(struct perf_evlist *evlist, pid_t pid);
int perf_evlist__set_tp_filter_pids(struct perf_evlist *evlist, size_t npids, pid_t *pids);

struct perf_evsel *
perf_evlist__find_tracepoint_by_id(struct perf_evlist *evlist, int id);

struct perf_evsel *
perf_evlist__find_tracepoint_by_name(struct perf_evlist *evlist,
				     const char *name);

void perf_evlist__id_add(struct perf_evlist *evlist, struct perf_evsel *evsel,
			 int cpu, int thread, u64 id);
int perf_evlist__id_add_fd(struct perf_evlist *evlist,
			   struct perf_evsel *evsel,
			   int cpu, int thread, int fd);

int perf_evlist__add_pollfd(struct perf_evlist *evlist, int fd);
int perf_evlist__alloc_pollfd(struct perf_evlist *evlist);
int perf_evlist__filter_pollfd(struct perf_evlist *evlist, short revents_and_mask);

int perf_evlist__poll(struct perf_evlist *evlist, int timeout);

struct perf_evsel *perf_evlist__id2evsel(struct perf_evlist *evlist, u64 id);
struct perf_evsel *perf_evlist__id2evsel_strict(struct perf_evlist *evlist,
						u64 id);

struct perf_sample_id *perf_evlist__id2sid(struct perf_evlist *evlist, u64 id);

void perf_evlist__toggle_bkw_mmap(struct perf_evlist *evlist, enum bkw_mmap_state state);

void perf_evlist__mmap_consume(struct perf_evlist *evlist, int idx);

int perf_evlist__open(struct perf_evlist *evlist);
void perf_evlist__close(struct perf_evlist *evlist);

struct callchain_param;

void perf_evlist__set_id_pos(struct perf_evlist *evlist);
bool perf_can_sample_identifier(void);
bool perf_can_record_switch_events(void);
bool perf_can_record_cpu_wide(void);
void perf_evlist__config(struct perf_evlist *evlist, struct record_opts *opts,
			 struct callchain_param *callchain);
int record_opts__config(struct record_opts *opts);

int perf_evlist__prepare_workload(struct perf_evlist *evlist,
				  struct target *target,
				  const char *argv[], bool pipe_output,
				  void (*exec_error)(int signo, siginfo_t *info,
						     void *ucontext));
int perf_evlist__start_workload(struct perf_evlist *evlist);

struct option;

int __perf_evlist__parse_mmap_pages(unsigned int *mmap_pages, const char *str);
int perf_evlist__parse_mmap_pages(const struct option *opt,
				  const char *str,
				  int unset);

unsigned long perf_event_mlock_kb_in_pages(void);

int perf_evlist__mmap_ex(struct perf_evlist *evlist, unsigned int pages,
			 unsigned int auxtrace_pages,
			 bool auxtrace_overwrite, int nr_cblocks,
			 int affinity, int flush);
int perf_evlist__mmap(struct perf_evlist *evlist, unsigned int pages);
void perf_evlist__munmap(struct perf_evlist *evlist);

size_t perf_evlist__mmap_size(unsigned long pages);

void perf_evlist__disable(struct perf_evlist *evlist);
void perf_evlist__enable(struct perf_evlist *evlist);
void perf_evlist__toggle_enable(struct perf_evlist *evlist);

int perf_evlist__enable_event_idx(struct perf_evlist *evlist,
				  struct perf_evsel *evsel, int idx);

void perf_evlist__set_selected(struct perf_evlist *evlist,
			       struct perf_evsel *evsel);

void perf_evlist__set_maps(struct perf_evlist *evlist, struct cpu_map *cpus,
			   struct thread_map *threads);
int perf_evlist__create_maps(struct perf_evlist *evlist, struct target *target);
int perf_evlist__apply_filters(struct perf_evlist *evlist, struct perf_evsel **err_evsel);

void __perf_evlist__set_leader(struct list_head *list);
void perf_evlist__set_leader(struct perf_evlist *evlist);

u64 perf_evlist__read_format(struct perf_evlist *evlist);
u64 __perf_evlist__combined_sample_type(struct perf_evlist *evlist);
u64 perf_evlist__combined_sample_type(struct perf_evlist *evlist);
u64 perf_evlist__combined_branch_type(struct perf_evlist *evlist);
bool perf_evlist__sample_id_all(struct perf_evlist *evlist);
u16 perf_evlist__id_hdr_size(struct perf_evlist *evlist);

int perf_evlist__parse_sample(struct perf_evlist *evlist, union perf_event *event,
			      struct perf_sample *sample);

int perf_evlist__parse_sample_timestamp(struct perf_evlist *evlist,
					union perf_event *event,
					u64 *timestamp);

bool perf_evlist__valid_sample_type(struct perf_evlist *evlist);
bool perf_evlist__valid_sample_id_all(struct perf_evlist *evlist);
bool perf_evlist__valid_read_format(struct perf_evlist *evlist);

void perf_evlist__splice_list_tail(struct perf_evlist *evlist,
				   struct list_head *list);

static inline bool perf_evlist__empty(struct perf_evlist *evlist)
{
	return list_empty(&evlist->entries);
}

static inline struct perf_evsel *perf_evlist__first(struct perf_evlist *evlist)
{
	return list_entry(evlist->entries.next, struct perf_evsel, node);
}

static inline struct perf_evsel *perf_evlist__last(struct perf_evlist *evlist)
{
	return list_entry(evlist->entries.prev, struct perf_evsel, node);
}

size_t perf_evlist__fprintf(struct perf_evlist *evlist, FILE *fp);

int perf_evlist__strerror_open(struct perf_evlist *evlist, int err, char *buf, size_t size);
int perf_evlist__strerror_mmap(struct perf_evlist *evlist, int err, char *buf, size_t size);

bool perf_evlist__can_select_event(struct perf_evlist *evlist, const char *str);
void perf_evlist__to_front(struct perf_evlist *evlist,
			   struct perf_evsel *move_evsel);

/**
 * __evlist__for_each_entry - iterate thru all the evsels
 * @list: list_head instance to iterate
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each_entry(list, evsel) \
        list_for_each_entry(evsel, list, node)

/**
 * evlist__for_each_entry - iterate thru all the evsels
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 */
#define evlist__for_each_entry(evlist, evsel) \
	__evlist__for_each_entry(&(evlist)->entries, evsel)

/**
 * __evlist__for_each_entry_continue - continue iteration thru all the evsels
 * @list: list_head instance to iterate
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each_entry_continue(list, evsel) \
        list_for_each_entry_continue(evsel, list, node)

/**
 * evlist__for_each_entry_continue - continue iteration thru all the evsels
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 */
#define evlist__for_each_entry_continue(evlist, evsel) \
	__evlist__for_each_entry_continue(&(evlist)->entries, evsel)

/**
 * __evlist__for_each_entry_reverse - iterate thru all the evsels in reverse order
 * @list: list_head instance to iterate
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each_entry_reverse(list, evsel) \
        list_for_each_entry_reverse(evsel, list, node)

/**
 * evlist__for_each_entry_reverse - iterate thru all the evsels in reverse order
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 */
#define evlist__for_each_entry_reverse(evlist, evsel) \
	__evlist__for_each_entry_reverse(&(evlist)->entries, evsel)

/**
 * __evlist__for_each_entry_safe - safely iterate thru all the evsels
 * @list: list_head instance to iterate
 * @tmp: struct evsel temp iterator
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each_entry_safe(list, tmp, evsel) \
        list_for_each_entry_safe(evsel, tmp, list, node)

/**
 * evlist__for_each_entry_safe - safely iterate thru all the evsels
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 * @tmp: struct evsel temp iterator
 */
#define evlist__for_each_entry_safe(evlist, tmp, evsel) \
	__evlist__for_each_entry_safe(&(evlist)->entries, tmp, evsel)

void perf_evlist__set_tracking_event(struct perf_evlist *evlist,
				     struct perf_evsel *tracking_evsel);

struct perf_evsel *
perf_evlist__find_evsel_by_str(struct perf_evlist *evlist, const char *str);

struct perf_evsel *perf_evlist__event2evsel(struct perf_evlist *evlist,
					    union perf_event *event);

bool perf_evlist__exclude_kernel(struct perf_evlist *evlist);

void perf_evlist__force_leader(struct perf_evlist *evlist);

struct perf_evsel *perf_evlist__reset_weak_group(struct perf_evlist *evlist,
						 struct perf_evsel *evsel);
#endif /* __PERF_EVLIST_H */
