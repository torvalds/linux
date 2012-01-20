#ifndef __PERF_EVLIST_H
#define __PERF_EVLIST_H 1

#include <linux/list.h>
#include <stdio.h>
#include "../perf.h"
#include "event.h"
#include "util.h"
#include <unistd.h>

struct pollfd;
struct thread_map;
struct cpu_map;
struct perf_record_opts;

#define PERF_EVLIST__HLIST_BITS 8
#define PERF_EVLIST__HLIST_SIZE (1 << PERF_EVLIST__HLIST_BITS)

struct perf_evlist {
	struct list_head entries;
	struct hlist_head heads[PERF_EVLIST__HLIST_SIZE];
	int		 nr_entries;
	int		 nr_fds;
	int		 nr_mmaps;
	int		 mmap_len;
	struct {
		int	cork_fd;
		pid_t	pid;
	} workload;
	bool		 overwrite;
	union perf_event event_copy;
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

struct perf_evsel;

struct perf_evlist *perf_evlist__new(struct cpu_map *cpus,
				     struct thread_map *threads);
void perf_evlist__init(struct perf_evlist *evlist, struct cpu_map *cpus,
		       struct thread_map *threads);
void perf_evlist__exit(struct perf_evlist *evlist);
void perf_evlist__delete(struct perf_evlist *evlist);

void perf_evlist__add(struct perf_evlist *evlist, struct perf_evsel *entry);
int perf_evlist__add_default(struct perf_evlist *evlist);
int perf_evlist__add_attrs(struct perf_evlist *evlist,
			   struct perf_event_attr *attrs, size_t nr_attrs);
int perf_evlist__add_tracepoints(struct perf_evlist *evlist,
				 const char *tracepoints[], size_t nr_tracepoints);
int perf_evlist__set_tracepoints_handlers(struct perf_evlist *evlist,
					  const struct perf_evsel_str_handler *assocs,
					  size_t nr_assocs);

#define perf_evlist__add_attrs_array(evlist, array) \
	perf_evlist__add_attrs(evlist, array, ARRAY_SIZE(array))

#define perf_evlist__add_tracepoints_array(evlist, array) \
	perf_evlist__add_tracepoints(evlist, array, ARRAY_SIZE(array))

#define perf_evlist__set_tracepoints_handlers_array(evlist, array) \
	perf_evlist__set_tracepoints_handlers(evlist, array, ARRAY_SIZE(array))

void perf_evlist__id_add(struct perf_evlist *evlist, struct perf_evsel *evsel,
			 int cpu, int thread, u64 id);

void perf_evlist__add_pollfd(struct perf_evlist *evlist, int fd);

struct perf_evsel *perf_evlist__id2evsel(struct perf_evlist *evlist, u64 id);

union perf_event *perf_evlist__mmap_read(struct perf_evlist *self, int idx);

int perf_evlist__open(struct perf_evlist *evlist, bool group);

void perf_evlist__config_attrs(struct perf_evlist *evlist,
			       struct perf_record_opts *opts);

int perf_evlist__prepare_workload(struct perf_evlist *evlist,
				  struct perf_record_opts *opts,
				  const char *argv[]);
int perf_evlist__start_workload(struct perf_evlist *evlist);

int perf_evlist__mmap(struct perf_evlist *evlist, unsigned int pages,
		      bool overwrite);
void perf_evlist__munmap(struct perf_evlist *evlist);

void perf_evlist__disable(struct perf_evlist *evlist);
void perf_evlist__enable(struct perf_evlist *evlist);

void perf_evlist__set_selected(struct perf_evlist *evlist,
			       struct perf_evsel *evsel);

static inline void perf_evlist__set_maps(struct perf_evlist *evlist,
					 struct cpu_map *cpus,
					 struct thread_map *threads)
{
	evlist->cpus	= cpus;
	evlist->threads	= threads;
}

int perf_evlist__create_maps(struct perf_evlist *evlist, pid_t target_pid,
			     pid_t target_tid, const char *cpu_list);
void perf_evlist__delete_maps(struct perf_evlist *evlist);
int perf_evlist__set_filters(struct perf_evlist *evlist);

u64 perf_evlist__sample_type(const struct perf_evlist *evlist);
bool perf_evlist__sample_id_all(const const struct perf_evlist *evlist);
u16 perf_evlist__id_hdr_size(const struct perf_evlist *evlist);

bool perf_evlist__valid_sample_type(const struct perf_evlist *evlist);
bool perf_evlist__valid_sample_id_all(const struct perf_evlist *evlist);
#endif /* __PERF_EVLIST_H */
