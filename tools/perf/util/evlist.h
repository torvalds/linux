#ifndef __PERF_EVLIST_H
#define __PERF_EVLIST_H 1

#include <linux/list.h>
#include <stdio.h>
#include "../perf.h"
#include "event.h"
#include "evsel.h"
#include "util.h"
#include <unistd.h>

struct pollfd;
struct thread_map;
struct cpu_map;
struct record_opts;

#define PERF_EVLIST__HLIST_BITS 8
#define PERF_EVLIST__HLIST_SIZE (1 << PERF_EVLIST__HLIST_BITS)

struct perf_mmap {
	void		 *base;
	int		 mask;
	unsigned int	 prev;
	char		 event_copy[PERF_SAMPLE_MAX_SIZE];
};

struct perf_evlist {
	struct list_head entries;
	struct hlist_head heads[PERF_EVLIST__HLIST_SIZE];
	int		 nr_entries;
	int		 nr_groups;
	int		 nr_fds;
	int		 nr_mmaps;
	size_t		 mmap_len;
	int		 id_pos;
	int		 is_pos;
	u64		 combined_sample_type;
	struct {
		int	cork_fd;
		pid_t	pid;
	} workload;
	bool		 overwrite;
	struct perf_mmap *mmap;
	struct pollfd	 *pollfd;
	struct thread_map *threads;
	struct cpu_map	  *cpus;
	struct perf_evsel *selected;
};

struct perf_evsel_str_handler {
	const char *name;
	void	   *handler;
};

struct perf_evlist *perf_evlist__new(void);
struct perf_evlist *perf_evlist__new_default(void);
void perf_evlist__init(struct perf_evlist *evlist, struct cpu_map *cpus,
		       struct thread_map *threads);
void perf_evlist__exit(struct perf_evlist *evlist);
void perf_evlist__delete(struct perf_evlist *evlist);

void perf_evlist__add(struct perf_evlist *evlist, struct perf_evsel *entry);
int perf_evlist__add_default(struct perf_evlist *evlist);
int __perf_evlist__add_default_attrs(struct perf_evlist *evlist,
				     struct perf_event_attr *attrs, size_t nr_attrs);

#define perf_evlist__add_default_attrs(evlist, array) \
	__perf_evlist__add_default_attrs(evlist, array, ARRAY_SIZE(array))

int perf_evlist__add_newtp(struct perf_evlist *evlist,
			   const char *sys, const char *name, void *handler);

int perf_evlist__set_filter(struct perf_evlist *evlist, const char *filter);

struct perf_evsel *
perf_evlist__find_tracepoint_by_id(struct perf_evlist *evlist, int id);

struct perf_evsel *
perf_evlist__find_tracepoint_by_name(struct perf_evlist *evlist,
				     const char *name);

void perf_evlist__id_add(struct perf_evlist *evlist, struct perf_evsel *evsel,
			 int cpu, int thread, u64 id);

void perf_evlist__add_pollfd(struct perf_evlist *evlist, int fd);

int perf_evlist__filter_pollfd(struct perf_evlist *evlist, short revents_and_mask);

struct perf_evsel *perf_evlist__id2evsel(struct perf_evlist *evlist, u64 id);

struct perf_sample_id *perf_evlist__id2sid(struct perf_evlist *evlist, u64 id);

union perf_event *perf_evlist__mmap_read(struct perf_evlist *evlist, int idx);

void perf_evlist__mmap_consume(struct perf_evlist *evlist, int idx);

int perf_evlist__open(struct perf_evlist *evlist);
void perf_evlist__close(struct perf_evlist *evlist);

void perf_evlist__set_id_pos(struct perf_evlist *evlist);
bool perf_can_sample_identifier(void);
void perf_evlist__config(struct perf_evlist *evlist, struct record_opts *opts);
int record_opts__config(struct record_opts *opts);

int perf_evlist__prepare_workload(struct perf_evlist *evlist,
				  struct target *target,
				  const char *argv[], bool pipe_output,
				  void (*exec_error)(int signo, siginfo_t *info,
						     void *ucontext));
int perf_evlist__start_workload(struct perf_evlist *evlist);

int perf_evlist__parse_mmap_pages(const struct option *opt,
				  const char *str,
				  int unset);

int perf_evlist__mmap(struct perf_evlist *evlist, unsigned int pages,
		      bool overwrite);
void perf_evlist__munmap(struct perf_evlist *evlist);

void perf_evlist__disable(struct perf_evlist *evlist);
void perf_evlist__enable(struct perf_evlist *evlist);

int perf_evlist__disable_event(struct perf_evlist *evlist,
			       struct perf_evsel *evsel);
int perf_evlist__enable_event(struct perf_evlist *evlist,
			      struct perf_evsel *evsel);
int perf_evlist__enable_event_idx(struct perf_evlist *evlist,
				  struct perf_evsel *evsel, int idx);

void perf_evlist__set_selected(struct perf_evlist *evlist,
			       struct perf_evsel *evsel);

static inline void perf_evlist__set_maps(struct perf_evlist *evlist,
					 struct cpu_map *cpus,
					 struct thread_map *threads)
{
	evlist->cpus	= cpus;
	evlist->threads	= threads;
}

int perf_evlist__create_maps(struct perf_evlist *evlist, struct target *target);
int perf_evlist__apply_filters(struct perf_evlist *evlist);

void __perf_evlist__set_leader(struct list_head *list);
void perf_evlist__set_leader(struct perf_evlist *evlist);

u64 perf_evlist__read_format(struct perf_evlist *evlist);
u64 __perf_evlist__combined_sample_type(struct perf_evlist *evlist);
u64 perf_evlist__combined_sample_type(struct perf_evlist *evlist);
bool perf_evlist__sample_id_all(struct perf_evlist *evlist);
u16 perf_evlist__id_hdr_size(struct perf_evlist *evlist);

int perf_evlist__parse_sample(struct perf_evlist *evlist, union perf_event *event,
			      struct perf_sample *sample);

bool perf_evlist__valid_sample_type(struct perf_evlist *evlist);
bool perf_evlist__valid_sample_id_all(struct perf_evlist *evlist);
bool perf_evlist__valid_read_format(struct perf_evlist *evlist);

void perf_evlist__splice_list_tail(struct perf_evlist *evlist,
				   struct list_head *list,
				   int nr_entries);

static inline struct perf_evsel *perf_evlist__first(struct perf_evlist *evlist)
{
	return list_entry(evlist->entries.next, struct perf_evsel, node);
}

static inline struct perf_evsel *perf_evlist__last(struct perf_evlist *evlist)
{
	return list_entry(evlist->entries.prev, struct perf_evsel, node);
}

size_t perf_evlist__fprintf(struct perf_evlist *evlist, FILE *fp);

int perf_evlist__strerror_tp(struct perf_evlist *evlist, int err, char *buf, size_t size);
int perf_evlist__strerror_open(struct perf_evlist *evlist, int err, char *buf, size_t size);

static inline unsigned int perf_mmap__read_head(struct perf_mmap *mm)
{
	struct perf_event_mmap_page *pc = mm->base;
	int head = ACCESS_ONCE(pc->data_head);
	rmb();
	return head;
}

static inline void perf_mmap__write_tail(struct perf_mmap *md,
					 unsigned long tail)
{
	struct perf_event_mmap_page *pc = md->base;

	/*
	 * ensure all reads are done before we write the tail out.
	 */
	mb();
	pc->data_tail = tail;
}

bool perf_evlist__can_select_event(struct perf_evlist *evlist, const char *str);
void perf_evlist__to_front(struct perf_evlist *evlist,
			   struct perf_evsel *move_evsel);

/**
 * __evlist__for_each - iterate thru all the evsels
 * @list: list_head instance to iterate
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each(list, evsel) \
        list_for_each_entry(evsel, list, node)

/**
 * evlist__for_each - iterate thru all the evsels
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 */
#define evlist__for_each(evlist, evsel) \
	__evlist__for_each(&(evlist)->entries, evsel)

/**
 * __evlist__for_each_continue - continue iteration thru all the evsels
 * @list: list_head instance to iterate
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each_continue(list, evsel) \
        list_for_each_entry_continue(evsel, list, node)

/**
 * evlist__for_each_continue - continue iteration thru all the evsels
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 */
#define evlist__for_each_continue(evlist, evsel) \
	__evlist__for_each_continue(&(evlist)->entries, evsel)

/**
 * __evlist__for_each_reverse - iterate thru all the evsels in reverse order
 * @list: list_head instance to iterate
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each_reverse(list, evsel) \
        list_for_each_entry_reverse(evsel, list, node)

/**
 * evlist__for_each_reverse - iterate thru all the evsels in reverse order
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 */
#define evlist__for_each_reverse(evlist, evsel) \
	__evlist__for_each_reverse(&(evlist)->entries, evsel)

/**
 * __evlist__for_each_safe - safely iterate thru all the evsels
 * @list: list_head instance to iterate
 * @tmp: struct evsel temp iterator
 * @evsel: struct evsel iterator
 */
#define __evlist__for_each_safe(list, tmp, evsel) \
        list_for_each_entry_safe(evsel, tmp, list, node)

/**
 * evlist__for_each_safe - safely iterate thru all the evsels
 * @evlist: evlist instance to iterate
 * @evsel: struct evsel iterator
 * @tmp: struct evsel temp iterator
 */
#define evlist__for_each_safe(evlist, tmp, evsel) \
	__evlist__for_each_safe(&(evlist)->entries, tmp, evsel)

void perf_evlist__set_tracking_event(struct perf_evlist *evlist,
				     struct perf_evsel *tracking_evsel);

#endif /* __PERF_EVLIST_H */
