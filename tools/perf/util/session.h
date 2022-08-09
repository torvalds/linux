/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_SESSION_H
#define __PERF_SESSION_H

#include "trace-event.h"
#include "event.h"
#include "header.h"
#include "machine.h"
#include "data.h"
#include "ordered-events.h"
#include "util/compress.h"
#include <linux/kernel.h>
#include <linux/rbtree.h>
#include <linux/perf_event.h>

struct ip_callchain;
struct symbol;
struct thread;

struct auxtrace;
struct itrace_synth_opts;

struct decomp_data {
	struct decomp	 *decomp;
	struct decomp	 *decomp_last;
	struct zstd_data *zstd_decomp;
};

struct perf_session {
	struct perf_header	header;
	struct machines		machines;
	struct evlist	*evlist;
	struct auxtrace		*auxtrace;
	struct itrace_synth_opts *itrace_synth_opts;
	struct list_head	auxtrace_index;
	struct trace_event	tevent;
	struct perf_record_time_conv	time_conv;
	bool			repipe;
	bool			one_mmap;
	void			*one_mmap_addr;
	u64			one_mmap_offset;
	struct ordered_events	ordered_events;
	struct perf_data	*data;
	struct perf_tool	*tool;
	u64			bytes_transferred;
	u64			bytes_compressed;
	struct zstd_data	zstd_data;
	struct decomp_data	decomp_data;
	struct decomp_data	*active_decomp;
};

struct decomp {
	struct decomp *next;
	u64 file_pos;
	size_t mmap_len;
	u64 head;
	size_t size;
	char data[];
};

struct perf_tool;

struct perf_session *__perf_session__new(struct perf_data *data,
					 bool repipe, int repipe_fd,
					 struct perf_tool *tool);

static inline struct perf_session *perf_session__new(struct perf_data *data,
						     struct perf_tool *tool)
{
	return __perf_session__new(data, false, -1, tool);
}

void perf_session__delete(struct perf_session *session);

void perf_event_header__bswap(struct perf_event_header *hdr);

int perf_session__peek_event(struct perf_session *session, off_t file_offset,
			     void *buf, size_t buf_sz,
			     union perf_event **event_ptr,
			     struct perf_sample *sample);
typedef int (*peek_events_cb_t)(struct perf_session *session,
				union perf_event *event, u64 offset,
				void *data);
int perf_session__peek_events(struct perf_session *session, u64 offset,
			      u64 size, peek_events_cb_t cb, void *data);

int perf_session__process_events(struct perf_session *session);

int perf_session__queue_event(struct perf_session *s, union perf_event *event,
			      u64 timestamp, u64 file_offset);

void perf_tool__fill_defaults(struct perf_tool *tool);

int perf_session__resolve_callchain(struct perf_session *session,
				    struct evsel *evsel,
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
int perf_session__register_idle_thread(struct perf_session *session);

size_t perf_session__fprintf(struct perf_session *session, FILE *fp);

size_t perf_session__fprintf_dsos(struct perf_session *session, FILE *fp);

size_t perf_session__fprintf_dsos_buildid(struct perf_session *session, FILE *fp,
					  bool (fn)(struct dso *dso, int parm), int parm);

size_t perf_session__fprintf_nr_events(struct perf_session *session, FILE *fp,
				       bool skip_empty);

struct evsel *perf_session__find_first_evtype(struct perf_session *session,
					    unsigned int type);

int perf_session__cpu_bitmap(struct perf_session *session,
			     const char *cpu_list, unsigned long *cpu_bitmap);

void perf_session__fprintf_info(struct perf_session *s, FILE *fp, bool full);

struct evsel_str_handler;

#define perf_session__set_tracepoints_handlers(session, array) \
	__evlist__set_tracepoints_handlers(session->evlist, array, ARRAY_SIZE(array))

extern volatile int session_done;

#define session_done()	READ_ONCE(session_done)

int perf_session__deliver_synth_event(struct perf_session *session,
				      union perf_event *event,
				      struct perf_sample *sample);

int perf_event__process_id_index(struct perf_session *session,
				 union perf_event *event);

#endif /* __PERF_SESSION_H */
