#ifndef __PERF_SESSION_H
#define __PERF_SESSION_H

#include "trace-event.h"
#include "event.h"
#include "header.h"
#include "machine.h"
#include "symbol.h"
#include "thread.h"
#include "data.h"
#include "ordered-events.h"
#include <linux/rbtree.h>
#include <linux/perf_event.h>

struct ip_callchain;
struct thread;

struct auxtrace;
struct itrace_synth_opts;

struct perf_session {
	struct perf_header	header;
	struct machines		machines;
	struct perf_evlist	*evlist;
	struct auxtrace		*auxtrace;
	struct itrace_synth_opts *itrace_synth_opts;
	struct list_head	auxtrace_index;
	struct trace_event	tevent;
	bool			repipe;
	bool			one_mmap;
	void			*one_mmap_addr;
	u64			one_mmap_offset;
	struct ordered_events	ordered_events;
	struct perf_data_file	*file;
	struct perf_tool	*tool;
};

#define PRINT_IP_OPT_IP		(1<<0)
#define PRINT_IP_OPT_SYM		(1<<1)
#define PRINT_IP_OPT_DSO		(1<<2)
#define PRINT_IP_OPT_SYMOFFSET	(1<<3)
#define PRINT_IP_OPT_ONELINE	(1<<4)
#define PRINT_IP_OPT_SRCLINE	(1<<5)

struct perf_tool;

struct perf_session *perf_session__new(struct perf_data_file *file,
				       bool repipe, struct perf_tool *tool);
void perf_session__delete(struct perf_session *session);

void perf_event_header__bswap(struct perf_event_header *hdr);

int perf_session__peek_event(struct perf_session *session, off_t file_offset,
			     void *buf, size_t buf_sz,
			     union perf_event **event_ptr,
			     struct perf_sample *sample);

int perf_session__process_events(struct perf_session *session);

int perf_session__queue_event(struct perf_session *s, union perf_event *event,
			      struct perf_sample *sample, u64 file_offset);

void perf_tool__fill_defaults(struct perf_tool *tool);

int perf_session__resolve_callchain(struct perf_session *session,
				    struct perf_evsel *evsel,
				    struct thread *thread,
				    struct ip_callchain *chain,
				    struct symbol **parent);

bool perf_session__has_traces(struct perf_session *session, const char *msg);

void perf_event__attr_swap(struct perf_event_attr *attr);

int perf_session__create_kernel_maps(struct perf_session *session);

void perf_session__set_id_hdr_size(struct perf_session *session);

static inline
struct machine *perf_session__find_machine(struct perf_session *session, pid_t pid)
{
	return machines__find(&session->machines, pid);
}

static inline
struct machine *perf_session__findnew_machine(struct perf_session *session, pid_t pid)
{
	return machines__findnew(&session->machines, pid);
}

struct thread *perf_session__findnew(struct perf_session *session, pid_t pid);
struct thread *perf_session__register_idle_thread(struct perf_session *session);

size_t perf_session__fprintf(struct perf_session *session, FILE *fp);

size_t perf_session__fprintf_dsos(struct perf_session *session, FILE *fp);

size_t perf_session__fprintf_dsos_buildid(struct perf_session *session, FILE *fp,
					  bool (fn)(struct dso *dso, int parm), int parm);

size_t perf_session__fprintf_nr_events(struct perf_session *session, FILE *fp);

struct perf_evsel *perf_session__find_first_evtype(struct perf_session *session,
					    unsigned int type);

void perf_evsel__print_ip(struct perf_evsel *evsel, struct perf_sample *sample,
			  struct addr_location *al,
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

extern volatile int session_done;

#define session_done()	ACCESS_ONCE(session_done)

int perf_session__deliver_synth_event(struct perf_session *session,
				      union perf_event *event,
				      struct perf_sample *sample);

int perf_event__process_id_index(struct perf_tool *tool,
				 union perf_event *event,
				 struct perf_session *session);

int perf_event__synthesize_id_index(struct perf_tool *tool,
				    perf_event__handler_t process,
				    struct perf_evlist *evlist,
				    struct machine *machine);

#endif /* __PERF_SESSION_H */
