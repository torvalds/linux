#ifndef __PERF_SESSION_H
#define __PERF_SESSION_H

#include "hist.h"
#include "event.h"
#include "header.h"
#include "machine.h"
#include "symbol.h"
#include "thread.h"
#include <linux/rbtree.h>
#include <linux/perf_event.h>

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
	struct machines		machines;
	struct perf_evlist	*evlist;
	struct pevent		*pevent;
	struct events_stats	stats;
	int			fd;
	bool			fd_pipe;
	bool			repipe;
	struct ordered_samples	ordered_samples;
	char			filename[1];
};

#define PRINT_IP_OPT_IP		(1<<0)
#define PRINT_IP_OPT_SYM		(1<<1)
#define PRINT_IP_OPT_DSO		(1<<2)
#define PRINT_IP_OPT_SYMOFFSET	(1<<3)
#define PRINT_IP_OPT_ONELINE	(1<<4)

struct perf_tool;

struct perf_session *perf_session__new(const char *filename, int mode,
				       bool force, bool repipe,
				       struct perf_tool *tool);
void perf_session__delete(struct perf_session *session);

void perf_event_header__bswap(struct perf_event_header *self);

int __perf_session__process_events(struct perf_session *self,
				   u64 data_offset, u64 data_size, u64 size,
				   struct perf_tool *tool);
int perf_session__process_events(struct perf_session *self,
				 struct perf_tool *tool);

int perf_session_queue_event(struct perf_session *s, union perf_event *event,
			     struct perf_sample *sample, u64 file_offset);

void perf_tool__fill_defaults(struct perf_tool *tool);

int perf_session__resolve_callchain(struct perf_session *self, struct perf_evsel *evsel,
				    struct thread *thread,
				    struct ip_callchain *chain,
				    struct symbol **parent);

bool perf_session__has_traces(struct perf_session *self, const char *msg);

void mem_bswap_64(void *src, int byte_size);
void mem_bswap_32(void *src, int byte_size);
void perf_event__attr_swap(struct perf_event_attr *attr);

int perf_session__create_kernel_maps(struct perf_session *self);

void perf_session__set_id_hdr_size(struct perf_session *session);

static inline
struct machine *perf_session__find_machine(struct perf_session *self, pid_t pid)
{
	return machines__find(&self->machines, pid);
}

static inline
struct machine *perf_session__findnew_machine(struct perf_session *self, pid_t pid)
{
	return machines__findnew(&self->machines, pid);
}

struct thread *perf_session__findnew(struct perf_session *self, pid_t pid);
size_t perf_session__fprintf(struct perf_session *self, FILE *fp);

size_t perf_session__fprintf_dsos(struct perf_session *self, FILE *fp);

size_t perf_session__fprintf_dsos_buildid(struct perf_session *session, FILE *fp,
					  bool (fn)(struct dso *dso, int parm), int parm);

size_t perf_session__fprintf_nr_events(struct perf_session *session, FILE *fp);

struct perf_evsel *perf_session__find_first_evtype(struct perf_session *session,
					    unsigned int type);

void perf_evsel__print_ip(struct perf_evsel *evsel, union perf_event *event,
			  struct perf_sample *sample, struct machine *machine,
			  unsigned int print_opts, unsigned int stack_depth);

int perf_session__cpu_bitmap(struct perf_session *session,
			     const char *cpu_list, unsigned long *cpu_bitmap);

void perf_session__fprintf_info(struct perf_session *s, FILE *fp, bool full);

struct perf_evsel_str_handler;

int __perf_session__set_tracepoints_handlers(struct perf_session *session,
					     const struct perf_evsel_str_handler *assocs,
					     size_t nr_assocs);

#define perf_session__set_tracepoints_handlers(session, array) \
	__perf_session__set_tracepoints_handlers(session, array, ARRAY_SIZE(array))
#endif /* __PERF_SESSION_H */
