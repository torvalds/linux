#ifndef __PERF_SESSION_H
#define __PERF_SESSION_H

#include "hist.h"
#include "event.h"
#include "header.h"
#include "symbol.h"
#include "thread.h"
#include <linux/rbtree.h>
#include "../../../include/linux/perf_event.h"

struct sample_queue;
struct ip_callchain;
struct thread;

struct ordered_samples {
	u64			last_flush;
	u64			next_flush;
	u64			max_timestamp;
	struct list_head	samples;
	struct list_head	sample_cache;
	struct list_head	to_free;
	struct sample_queue	*sample_buffer;
	struct sample_queue	*last_sample;
	int			sample_buffer_idx;
	unsigned int		nr_samples;
};

struct perf_session {
	struct perf_header	header;
	unsigned long		size;
	unsigned long		mmap_window;
	struct rb_root		threads;
	struct list_head	dead_threads;
	struct thread		*last_match;
	struct machine		host_machine;
	struct rb_root		machines;
	struct perf_evlist	*evlist;
	/*
	 * FIXME: Need to split this up further, we need global
	 *	  stats + per event stats. 'perf diff' also needs
	 *	  to properly support multiple events in a single
	 *	  perf.data file.
	 */
	struct hists		hists;
	u64			sample_type;
	int			sample_size;
	int			fd;
	bool			fd_pipe;
	bool			repipe;
	bool			sample_id_all;
	u16			id_hdr_size;
	int			cwdlen;
	char			*cwd;
	struct ordered_samples	ordered_samples;
	struct callchain_cursor	callchain_cursor;
	char			filename[0];
};

struct perf_evsel;
struct perf_event_ops;

typedef int (*event_sample)(union perf_event *event, struct perf_sample *sample,
			    struct perf_evsel *evsel, struct perf_session *session);
typedef int (*event_op)(union perf_event *self, struct perf_sample *sample,
			struct perf_session *session);
typedef int (*event_synth_op)(union perf_event *self,
			      struct perf_session *session);
typedef int (*event_op2)(union perf_event *self, struct perf_session *session,
			 struct perf_event_ops *ops);

struct perf_event_ops {
	event_sample	sample;
	event_op	mmap,
			comm,
			fork,
			exit,
			lost,
			read,
			throttle,
			unthrottle;
	event_synth_op	attr,
			event_type,
			tracing_data,
			build_id;
	event_op2	finished_round;
	bool		ordered_samples;
	bool		ordering_requires_timestamps;
};

struct perf_session *perf_session__new(const char *filename, int mode,
				       bool force, bool repipe,
				       struct perf_event_ops *ops);
void perf_session__delete(struct perf_session *self);

void perf_event_header__bswap(struct perf_event_header *self);

int __perf_session__process_events(struct perf_session *self,
				   u64 data_offset, u64 data_size, u64 size,
				   struct perf_event_ops *ops);
int perf_session__process_events(struct perf_session *self,
				 struct perf_event_ops *event_ops);

int perf_session__resolve_callchain(struct perf_session *self,
				    struct thread *thread,
				    struct ip_callchain *chain,
				    struct symbol **parent);

bool perf_session__has_traces(struct perf_session *self, const char *msg);

int perf_session__set_kallsyms_ref_reloc_sym(struct map **maps,
					     const char *symbol_name,
					     u64 addr);

void mem_bswap_64(void *src, int byte_size);
void perf_event__attr_swap(struct perf_event_attr *attr);

int perf_session__create_kernel_maps(struct perf_session *self);

void perf_session__update_sample_type(struct perf_session *self);
void perf_session__remove_thread(struct perf_session *self, struct thread *th);

static inline
struct machine *perf_session__find_host_machine(struct perf_session *self)
{
	return &self->host_machine;
}

static inline
struct machine *perf_session__find_machine(struct perf_session *self, pid_t pid)
{
	if (pid == HOST_KERNEL_ID)
		return &self->host_machine;
	return machines__find(&self->machines, pid);
}

static inline
struct machine *perf_session__findnew_machine(struct perf_session *self, pid_t pid)
{
	if (pid == HOST_KERNEL_ID)
		return &self->host_machine;
	return machines__findnew(&self->machines, pid);
}

static inline
void perf_session__process_machines(struct perf_session *self,
				    machine__process_t process)
{
	process(&self->host_machine, self);
	return machines__process(&self->machines, process, self);
}

size_t perf_session__fprintf_dsos(struct perf_session *self, FILE *fp);

size_t perf_session__fprintf_dsos_buildid(struct perf_session *self,
					  FILE *fp, bool with_hits);

size_t perf_session__fprintf_nr_events(struct perf_session *session, FILE *fp);

static inline int perf_session__parse_sample(struct perf_session *session,
					     const union perf_event *event,
					     struct perf_sample *sample)
{
	return perf_event__parse_sample(event, session->sample_type,
					session->sample_size,
					session->sample_id_all, sample,
					session->header.needs_swap);
}

struct perf_evsel *perf_session__find_first_evtype(struct perf_session *session,
					    unsigned int type);

void perf_session__print_ip(union perf_event *event,
				 struct perf_sample *sample,
				 struct perf_session *session,
				 int print_sym, int print_dso);

int perf_session__cpu_bitmap(struct perf_session *session,
			     const char *cpu_list, unsigned long *cpu_bitmap);

void perf_session__fprintf_info(struct perf_session *s, FILE *fp, bool full);
#endif /* __PERF_SESSION_H */
